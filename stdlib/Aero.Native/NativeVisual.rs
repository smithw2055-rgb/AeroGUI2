module Aero.Native;

// Tree & lifecycle
public handle Visual_Create(int kind) { return Visual_Create(kind); }
int __rs_native_Visual_Destroy(handle node) { return 0; }
public void Visual_Destroy(handle node) { __rs_native_Visual_Destroy(node); return; }
int __rs_native_Visual_AddChild(handle parent, handle child) { return 0; }
public void Visual_AddChild(handle parent, handle child) { __rs_native_Visual_AddChild(parent, child); return; }
int __rs_native_Visual_InsertChild(handle parent, int index, handle child) { return 0; }
public void Visual_InsertChild(handle parent, int index, handle child) { __rs_native_Visual_InsertChild(parent, index, child); return; }
int __rs_native_Visual_RemoveChild(handle parent, handle child) { return 0; }
public void Visual_RemoveChild(handle parent, handle child) { __rs_native_Visual_RemoveChild(parent, child); return; }
int __rs_native_Visual_ClearChildren(handle parent) { return 0; }
public void Visual_ClearChildren(handle parent) { __rs_native_Visual_ClearChildren(parent); return; }
int __rs_native_Visual_BindOwner(handle node, object owner, int callbackMask) { return 0; }
public void Visual_BindOwner(handle node, object owner, int callbackMask) { __rs_native_Visual_BindOwner(node, owner, callbackMask); return; }
public object Visual_GetOwner(handle node) { return null; }

// Geometry & Layout
int __rs_native_Visual_SetSize(handle node, float w, float h) { return 0; }
public void Visual_SetSize(handle node, float w, float h) { __rs_native_Visual_SetSize(node, w, h); return; }
int __rs_native_Visual_SetMinMaxSize(handle node, float minW, float minH, float maxW, float maxH) { return 0; }
public void Visual_SetMinMaxSize(handle node, float minW, float minH, float maxW, float maxH) { __rs_native_Visual_SetMinMaxSize(node, minW, minH, maxW, maxH); return; }
int __rs_native_Visual_SetMargin(handle node, float l, float t, float r, float b) { return 0; }
public void Visual_SetMargin(handle node, float l, float t, float r, float b) { __rs_native_Visual_SetMargin(node, l, t, r, b); return; }
int __rs_native_Visual_SetPadding(handle node, float l, float t, float r, float b) { return 0; }
public void Visual_SetPadding(handle node, float l, float t, float r, float b) { __rs_native_Visual_SetPadding(node, l, t, r, b); return; }
int __rs_native_Visual_SetAlignment(handle node, int hAlign, int vAlign) { return 0; }
public void Visual_SetAlignment(handle node, int hAlign, int vAlign) { __rs_native_Visual_SetAlignment(node, hAlign, vAlign); return; }
int __rs_native_Visual_SetVisibility(handle node, int vis) { return 0; }
public void Visual_SetVisibility(handle node, int vis) { __rs_native_Visual_SetVisibility(node, vis); return; }
int __rs_native_Visual_SetOpacity(handle node, float op) { return 0; }
public void Visual_SetOpacity(handle node, float op) { __rs_native_Visual_SetOpacity(node, op); return; }
int __rs_native_Visual_SetZIndex(handle node, int z) { return 0; }
public void Visual_SetZIndex(handle node, int z) { __rs_native_Visual_SetZIndex(node, z); return; }
int __rs_native_Visual_SetClipToBounds(handle node, bool clip) { return 0; }
public void Visual_SetClipToBounds(handle node, bool clip) { __rs_native_Visual_SetClipToBounds(node, clip); return; }
int __rs_native_Visual_SetInteractiveFlags(handle node, bool enabled, bool hitTest, bool focusable) { return 0; }
public void Visual_SetInteractiveFlags(handle node, bool enabled, bool hitTest, bool focusable) { __rs_native_Visual_SetInteractiveFlags(node, enabled, hitTest, focusable); return; }
int __rs_native_Visual_SetLayoutMode(handle node, int mode, float p0, float p1, int i0, int i1) { return 0; }
public void Visual_SetLayoutMode(handle node, int mode, float p0, float p1, int i0, int i1) { __rs_native_Visual_SetLayoutMode(node, mode, p0, p1, i0, i1); return; }
int __rs_native_Visual_GridClearDefs(handle node) { return 0; }
public void Visual_GridClearDefs(handle node) { __rs_native_Visual_GridClearDefs(node); return; }
int __rs_native_Visual_GridAddRowDef(handle node, int unit, float val) { return 0; }
public void Visual_GridAddRowDef(handle node, int unit, float val) { __rs_native_Visual_GridAddRowDef(node, unit, val); return; }
int __rs_native_Visual_GridAddColDef(handle node, int unit, float val) { return 0; }
public void Visual_GridAddColDef(handle node, int unit, float val) { __rs_native_Visual_GridAddColDef(node, unit, val); return; }
int __rs_native_Visual_SetGridCell(handle node, int row, int col, int rowSpan, int colSpan) { return 0; }
public void Visual_SetGridCell(handle node, int row, int col, int rowSpan, int colSpan) { __rs_native_Visual_SetGridCell(node, row, col, rowSpan, colSpan); return; }
int __rs_native_Visual_SetDock(handle node, int dock) { return 0; }
public void Visual_SetDock(handle node, int dock) { __rs_native_Visual_SetDock(node, dock); return; }
int __rs_native_Visual_SetCanvasOffsets(handle node, float l, float t, float r, float b) { return 0; }
public void Visual_SetCanvasOffsets(handle node, float l, float t, float r, float b) { __rs_native_Visual_SetCanvasOffsets(node, l, t, r, b); return; }
int __rs_native_Visual_SetScrollOffsets(handle node, float ox, float oy) { return 0; }
public void Visual_SetScrollOffsets(handle node, float ox, float oy) { __rs_native_Visual_SetScrollOffsets(node, ox, oy); return; }
public float Visual_GetLayoutRect(handle node, int which) { return 0.0f; }
public float Visual_ScreenToLocal(handle node, float sx, float sy, int axis) { return 0.0f; }

// Appearance, Transforms & Effects
int __rs_native_Visual_SetBoxStyle(handle node, uint bgRgba, uint borderRgba, float btL, float btT, float btR, float btB, float crTL, float crTR, float crBR, float crBL) { return 0; }
public void Visual_SetBoxStyle(handle node, uint bgRgba, uint borderRgba, float btL, float btT, float btR, float btB, float crTL, float crTR, float crBR, float crBL) { __rs_native_Visual_SetBoxStyle(node, bgRgba, borderRgba, btL, btT, btR, btB, crTL, crTR, crBR, crBL); return; }
int __rs_native_Visual_SetBoxGradients(handle node, int bgGradId, int borderGradId) { return 0; }
public void Visual_SetBoxGradients(handle node, int bgGradId, int borderGradId) { __rs_native_Visual_SetBoxGradients(node, bgGradId, borderGradId); return; }
int __rs_native_Visual_SetBoxBrushOpacity(handle node, float bgOpacity) { return 0; }
public void Visual_SetBoxBrushOpacity(handle node, float bgOpacity) { __rs_native_Visual_SetBoxBrushOpacity(node, bgOpacity); return; }
int __rs_native_Visual_SetBoxShadow(handle node, bool isBoxShadow) { return 0; }
public void Visual_SetBoxShadow(handle node, bool isBoxShadow) { __rs_native_Visual_SetBoxShadow(node, isBoxShadow); return; }
public int Visual_RegisterLinearGradient(float x0, float y0, float x1, float y1, int spread, int mapping, string stopsSpec) { return 0; }
public int Visual_RegisterRadialGradient(float cx, float cy, float ox, float oy, float rx, float ry, int spread, int mapping, string stopsSpec) { return 0; }
public int Visual_RegisterConicGradient(float cx, float cy, int spread, int mapping, string stopsSpec) { return 0; }
int __rs_native_Visual_SetTransform2D(handle node, float tx, float ty, float sx, float sy, float rotDeg, float skewX, float skewY, float ox, float oy) { return 0; }
public void Visual_SetTransform2D(handle node, float tx, float ty, float sx, float sy, float rotDeg, float skewX, float skewY, float ox, float oy) { __rs_native_Visual_SetTransform2D(node, tx, ty, sx, sy, rotDeg, skewX, skewY, ox, oy); return; }
int __rs_native_Visual_SetLayoutTransform(handle node, float sx, float sy, float rotDeg, bool enabled) { return 0; }
public void Visual_SetLayoutTransform(handle node, float sx, float sy, float rotDeg, bool enabled) { __rs_native_Visual_SetLayoutTransform(node, sx, sy, rotDeg, enabled); return; }
int __rs_native_Visual_SetTransform3D(handle node, float rx, float ry, float rz, float tx, float ty, float tz, float camDist) { return 0; }
public void Visual_SetTransform3D(handle node, float rx, float ry, float rz, float tx, float ty, float tz, float camDist) { __rs_native_Visual_SetTransform3D(node, rx, ry, rz, tx, ty, tz, camDist); return; }
int __rs_native_Visual_SetTransform3DEx(handle node, float rx, float ry, float rz, float tx, float ty, float tz, float sx, float sy, float sz, float cx, float cy, float cz, float camDist, bool useCenter) { return 0; }
public void Visual_SetTransform3DEx(handle node, float rx, float ry, float rz, float tx, float ty, float tz, float sx, float sy, float sz, float cx, float cy, float cz, float camDist, bool useCenter) { __rs_native_Visual_SetTransform3DEx(node, rx, ry, rz, tx, ty, tz, sx, sy, sz, cx, cy, cz, camDist, useCenter); return; }
int __rs_native_Visual_SetFocus(handle node) { return 0; }
public void Visual_SetFocus(handle node) { __rs_native_Visual_SetFocus(node); return; }
int __rs_native_Visual_SetEffect(handle node, int kind, float radius, float depth, float dirDeg, uint colorRgba, float opacity) { return 0; }
public void Visual_SetEffect(handle node, int kind, float radius, float depth, float dirDeg, uint colorRgba, float opacity) { __rs_native_Visual_SetEffect(node, kind, radius, depth, dirDeg, colorRgba, opacity); return; }
int __rs_native_Visual_SetOpacityMask(handle node, bool enabled, int gradId, uint solidRgba, float opacity) { return 0; }
public void Visual_SetOpacityMask(handle node, bool enabled, int gradId, uint solidRgba, float opacity) { __rs_native_Visual_SetOpacityMask(node, enabled, gradId, solidRgba, opacity); return; }
int __rs_native_Visual_SetBlendMode(handle node, int blendMode) { return 0; }
public void Visual_SetBlendMode(handle node, int blendMode) { __rs_native_Visual_SetBlendMode(node, blendMode); return; }

// Text, Image & Vector
int __rs_native_Visual_SetText(handle node, string text, string fontFamily, float fontSize, uint fgRgba, int wrap, int align, int trim) { return 0; }
public void Visual_SetText(handle node, string text, string fontFamily, float fontSize, uint fgRgba, int wrap, int align, int trim) { __rs_native_Visual_SetText(node, text, fontFamily, fontSize, fgRgba, wrap, align, trim); return; }
int __rs_native_Visual_SetTextMetrics(handle node, float lineHeight, float charSpacing, int maxLines, int decorations) { return 0; }
public void Visual_SetTextMetrics(handle node, float lineHeight, float charSpacing, int maxLines, int decorations) { __rs_native_Visual_SetTextMetrics(node, lineHeight, charSpacing, maxLines, decorations); return; }
int __rs_native_Visual_SetTextStyle(handle node, int fontWeight, bool fontItalic, int fgGradId) { return 0; }
public void Visual_SetTextStyle(handle node, int fontWeight, bool fontItalic, int fgGradId) { __rs_native_Visual_SetTextStyle(node, fontWeight, fontItalic, fgGradId); return; }
int __rs_native_Visual_SetTextBrushOpacity(handle node, float fgOpacity) { return 0; }
public void Visual_SetTextBrushOpacity(handle node, float fgOpacity) { __rs_native_Visual_SetTextBrushOpacity(node, fgOpacity); return; }
int __rs_native_Visual_ClearTextSpans(handle node) { return 0; }
public void Visual_ClearTextSpans(handle node) { __rs_native_Visual_ClearTextSpans(node); return; }
int __rs_native_Visual_AddTextSpan(handle node, string text, string fontFamily, float fontSize, int fontWeight, int fontItalic, uint fgRgba, bool hasFg, int decorations) { return 0; }
public void Visual_AddTextSpan(handle node, string text, string fontFamily, float fontSize, int fontWeight, int fontItalic, uint fgRgba, bool hasFg, int decorations) { __rs_native_Visual_AddTextSpan(node, text, fontFamily, fontSize, fontWeight, fontItalic, fgRgba, hasFg, decorations); return; }
int __rs_native_Visual_SetTextSelection(handle node, int selStartCp, int selLenCp, int caretCp, bool showCaret, uint selRgba, uint caretRgba) { return 0; }
public void Visual_SetTextSelection(handle node, int selStartCp, int selLenCp, int caretCp, bool showCaret, uint selRgba, uint caretRgba) { __rs_native_Visual_SetTextSelection(node, selStartCp, selLenCp, caretCp, showCaret, selRgba, caretRgba); return; }
public float Visual_MeasureText(string text, string fontFamily, float fontSize, float maxW, int wrap, int outAxis) { return 0.0f; }
public int Visual_HitTestTextCp(handle node, float localX, float localY) { return 0; }
int __rs_native_Visual_SetImage(handle node, string uri, int stretch, int stretchDir, uint tintRgba) { return 0; }
public void Visual_SetImage(handle node, string uri, int stretch, int stretchDir, uint tintRgba) { __rs_native_Visual_SetImage(node, uri, stretch, stretchDir, tintRgba); return; }
int __rs_native_Visual_SetImageViewbox(handle node, float vx, float vy, float vw, float vh) { return 0; }
public void Visual_SetImageViewbox(handle node, float vx, float vy, float vw, float vh) { __rs_native_Visual_SetImageViewbox(node, vx, vy, vw, vh); return; }
int __rs_native_Visual_SetImageViewport(handle node, float vpx, float vpy, float vpw, float vph, bool hasExplicit, bool isAbsolute) { return 0; }
public void Visual_SetImageViewport(handle node, float vpx, float vpy, float vpw, float vph, bool hasExplicit, bool isAbsolute) { __rs_native_Visual_SetImageViewport(node, vpx, vpy, vpw, vph, hasExplicit, isAbsolute); return; }
int __rs_native_Visual_SetVisualBrushSource(handle node, handle srcNode, int alignX, int alignY) { return 0; }
public void Visual_SetVisualBrushSource(handle node, handle srcNode, int alignX, int alignY) { __rs_native_Visual_SetVisualBrushSource(node, srcNode, alignX, alignY); return; }
int __rs_native_Visual_SetImageTile(handle node, bool tileWrap) { return 0; }
public void Visual_SetImageTile(handle node, bool tileWrap) { __rs_native_Visual_SetImageTile(node, tileWrap); return; }
int __rs_native_Visual_SetVectorShape(handle node, int shapeKind, string pathOrParams, uint fillRgba, int fillGradId, uint strokeRgba, int strokeGradId, float strokeWidth, int stretch, float trimStart, float trimEnd) { return 0; }
public void Visual_SetVectorShape(handle node, int shapeKind, string pathOrParams, uint fillRgba, int fillGradId, uint strokeRgba, int strokeGradId, float strokeWidth, int stretch, float trimStart, float trimEnd) { __rs_native_Visual_SetVectorShape(node, shapeKind, pathOrParams, fillRgba, fillGradId, strokeRgba, strokeGradId, strokeWidth, stretch, trimStart, trimEnd); return; }
int __rs_native_Visual_SetVectorBrushOpacity(handle node, float fillOpacity, float strokeOpacity) { return 0; }
public void Visual_SetVectorBrushOpacity(handle node, float fillOpacity, float strokeOpacity) { __rs_native_Visual_SetVectorBrushOpacity(node, fillOpacity, strokeOpacity); return; }
int __rs_native_Visual_SetVectorStrokeDash(handle node, string dashSpec, int dashCap, int startCap, int endCap) { return 0; }
public void Visual_SetVectorStrokeDash(handle node, string dashSpec, int dashCap, int startCap, int endCap) { __rs_native_Visual_SetVectorStrokeDash(node, dashSpec, dashCap, startCap, endCap); return; }

// Retained DrawingContext
int __rs_native_Draw_Begin(handle node) { return 0; }
public void Draw_Begin(handle node) { __rs_native_Draw_Begin(node); return; }
int __rs_native_Draw_End(handle node) { return 0; }
public void Draw_End(handle node) { __rs_native_Draw_End(node); return; }
int __rs_native_Draw_FillRect(handle node, float x, float y, float w, float h, uint rgba, int gradId) { return 0; }
public void Draw_FillRect(handle node, float x, float y, float w, float h, uint rgba, int gradId) { __rs_native_Draw_FillRect(node, x, y, w, h, rgba, gradId); return; }
int __rs_native_Draw_StrokeRect(handle node, float x, float y, float w, float h, uint rgba, float strokeW) { return 0; }
public void Draw_StrokeRect(handle node, float x, float y, float w, float h, uint rgba, float strokeW) { __rs_native_Draw_StrokeRect(node, x, y, w, h, rgba, strokeW); return; }
int __rs_native_Draw_FillRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, int gradId) { return 0; }
public void Draw_FillRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, int gradId) { __rs_native_Draw_FillRoundedRect(node, x, y, w, h, rx, ry, rgba, gradId); return; }
int __rs_native_Draw_StrokeRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, float strokeW) { return 0; }
public void Draw_StrokeRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, float strokeW) { __rs_native_Draw_StrokeRoundedRect(node, x, y, w, h, rx, ry, rgba, strokeW); return; }
int __rs_native_Draw_FillEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, int gradId) { return 0; }
public void Draw_FillEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, int gradId) { __rs_native_Draw_FillEllipse(node, cx, cy, rx, ry, rgba, gradId); return; }
int __rs_native_Draw_StrokeEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, float strokeW) { return 0; }
public void Draw_StrokeEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, float strokeW) { __rs_native_Draw_StrokeEllipse(node, cx, cy, rx, ry, rgba, strokeW); return; }
int __rs_native_Draw_Line(handle node, float x1, float y1, float x2, float y2, uint rgba, float strokeW) { return 0; }
public void Draw_Line(handle node, float x1, float y1, float x2, float y2, uint rgba, float strokeW) { __rs_native_Draw_Line(node, x1, y1, x2, y2, rgba, strokeW); return; }
int __rs_native_Draw_Path(handle node, string svgPath, float x, float y, float w, float h, uint fillRgba, uint strokeRgba, float strokeW) { return 0; }
public void Draw_Path(handle node, string svgPath, float x, float y, float w, float h, uint fillRgba, uint strokeRgba, float strokeW) { __rs_native_Draw_Path(node, svgPath, x, y, w, h, fillRgba, strokeRgba, strokeW); return; }
int __rs_native_Draw_Text(handle node, string text, float x, float y, float w, float h, string font, float size, uint rgba, int hAlign, int vAlign) { return 0; }
public void Draw_Text(handle node, string text, float x, float y, float w, float h, string font, float size, uint rgba, int hAlign, int vAlign) { __rs_native_Draw_Text(node, text, x, y, w, h, font, size, rgba, hAlign, vAlign); return; }
int __rs_native_Draw_Image(handle node, string uri, float x, float y, float w, float h, uint tintRgba) { return 0; }
public void Draw_Image(handle node, string uri, float x, float y, float w, float h, uint tintRgba) { __rs_native_Draw_Image(node, uri, x, y, w, h, tintRgba); return; }
int __rs_native_Draw_BeginTextRuns(handle node, string text, float x, float y, float w, float h, string font, float size, uint defaultRgba) { return 0; }
public void Draw_BeginTextRuns(handle node, string text, float x, float y, float w, float h, string font, float size, uint defaultRgba) { __rs_native_Draw_BeginTextRuns(node, text, x, y, w, h, font, size, defaultRgba); return; }
int __rs_native_Draw_AddTextRunSpan(handle node, int byteOffset, int byteLength, uint rgba) { return 0; }
public void Draw_AddTextRunSpan(handle node, int byteOffset, int byteLength, uint rgba) { __rs_native_Draw_AddTextRunSpan(node, byteOffset, byteLength, rgba); return; }
int __rs_native_Draw_SyntaxLine(handle node, string lineText, float x, float y, float w, float h, string font, int langKind, float size, uint defaultRgba) { return 0; }
public void Draw_SyntaxLine(handle node, string lineText, float x, float y, float w, float h, string font, int langKind, float size, uint defaultRgba) { __rs_native_Draw_SyntaxLine(node, lineText, x, y, w, h, font, langKind, size, defaultRgba); return; }
public float Host_GetImageNaturalSize(string uri, int axis) { return 0.0f; }
int __rs_native_Draw_PushClip(handle node, float x, float y, float w, float h) { return 0; }
public void Draw_PushClip(handle node, float x, float y, float w, float h) { __rs_native_Draw_PushClip(node, x, y, w, h); return; }
int __rs_native_Draw_PopClip(handle node) { return 0; }
public void Draw_PopClip(handle node) { __rs_native_Draw_PopClip(node); return; }
int __rs_native_Draw_PushTransform(handle node, float tx, float ty, float sx, float sy, float rotDeg) { return 0; }
public void Draw_PushTransform(handle node, float tx, float ty, float sx, float sy, float rotDeg) { __rs_native_Draw_PushTransform(node, tx, ty, sx, sy, rotDeg); return; }
int __rs_native_Draw_PopTransform(handle node) { return 0; }
public void Draw_PopTransform(handle node) { __rs_native_Draw_PopTransform(node); return; }
int __rs_native_Visual_InvalidateDraw(handle node) { return 0; }
public void Visual_InvalidateDraw(handle node) { __rs_native_Visual_InvalidateDraw(node); return; }

// Animation, Host & Reflection
public int Anim_StartFloat(handle node, int prop, float fromVal, float toVal, float durationSec, int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) { return 0; }
public int Anim_StartFloatTo(handle node, int prop, float toVal, float durationSec, int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) { return 0; }
public int Anim_StartColor(handle node, int prop, uint fromRgba, uint toRgba, float durationSec, int easeKind, int easeMode) { return 0; }
public int Anim_StartColorDelayed(handle node, int prop, uint fromRgba, uint toRgba, float durationSec, int easeKind, int easeMode, float delaySec) { return 0; }
public int Anim_StartColorEx(handle node, int prop, uint fromRgba, uint toRgba, float durationSec, int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) { return 0; }
public int Anim_StartColorTo(handle node, int prop, uint toRgba, float durationSec, int easeKind, int easeMode, float delaySec) { return 0; }
public int Anim_StartColorToEx(handle node, int prop, uint toRgba, float durationSec, int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) { return 0; }
public float Anim_ReadCurrentFloat(handle node, int prop) { return 0.0f; }
public uint Anim_ReadCurrentColor(handle node, int prop) { return 0u; }
public int Anim_SpringFloat(handle node, int prop, float targetVal, float stiffness, float damping) { return 0; }
int __rs_native_Anim_StopAll(handle node) { return 0; }
public void Anim_StopAll(handle node) { __rs_native_Anim_StopAll(node); return; }
int __rs_native_Anim_StopTrack(int trackId) { return 0; }
public void Anim_StopTrack(int trackId) { __rs_native_Anim_StopTrack(trackId); return; }
int __rs_native_Anim_SetTrackEasingParams(int trackId, float param0, float param1, int paramMask) { return 0; }
public void Anim_SetTrackEasingParams(int trackId, float param0, float param1, int paramMask = 3) { __rs_native_Anim_SetTrackEasingParams(trackId, param0, param1, paramMask); return; }
public int Anim_BeginKeyFrames(handle node, int prop, float delaySec, bool autoReverse, int repeatCount) { return 0; }
int __rs_native_Anim_AddKeyFrameFloat(int trackId, float timeSec, float value, int easeKind, int easeMode, float param0, float param1, int paramMask, bool discrete) { return 0; }
public void Anim_AddKeyFrameFloat(int trackId, float timeSec, float value, int easeKind, int easeMode, float param0, float param1, int paramMask, bool discrete) { __rs_native_Anim_AddKeyFrameFloat(trackId, timeSec, value, easeKind, easeMode, param0, param1, paramMask, discrete); return; }
int __rs_native_Anim_MarkTrackScriptSync(int trackId) { return 0; }
public void Anim_MarkTrackScriptSync(int trackId) { __rs_native_Anim_MarkTrackScriptSync(trackId); return; }
public int Visual_GetWorldLayoutEpoch() { return 1; }
int __rs_native_Anim_ApplyFloat(handle node, int prop, float value) { return 0; }
public void Anim_ApplyFloat(handle node, int prop, float value) { __rs_native_Anim_ApplyFloat(node, prop, value); return; }
int __rs_native_Anim_ApplyColor(handle node, int prop, uint rgba) { return 0; }
public void Anim_ApplyColor(handle node, int prop, uint rgba) { __rs_native_Anim_ApplyColor(node, prop, rgba); return; }
public bool Anim_IsTrackActive(int trackId) { return false; }
public bool Anim_IsTrackStarted(int trackId) { return false; }
public float Anim_ReadTrackFloat(int trackId, float fallback) { return fallback; }
public uint Anim_ReadTrackColor(int trackId, uint fallback) { return fallback; }
int __rs_native_Host_SetPointerCapture(handle node) { return 0; }
public void Host_SetPointerCapture(handle node) { __rs_native_Host_SetPointerCapture(node); return; }
int __rs_native_Host_ReleasePointerCapture(handle node) { return 0; }
public void Host_ReleasePointerCapture(handle node) { __rs_native_Host_ReleasePointerCapture(node); return; }
int __rs_native_Host_SyncImeRect(float x, float y, float w, float h, bool active) { return 0; }
public void Host_SyncImeRect(float x, float y, float w, float h, bool active) { __rs_native_Host_SyncImeRect(x, y, w, h, active); return; }
public string Host_GetClipboardText() { return ""; }
int __rs_native_Host_SetClipboardText(string text) { return 0; }
public void Host_SetClipboardText(string text) { __rs_native_Host_SetClipboardText(text); return; }
int __rs_native_Host_RequestWake() { return 0; }
public void Host_RequestWake() { __rs_native_Host_RequestWake(); return; }
int __rs_native_Host_RequestRenderWake() { return 0; }
public void Host_RequestRenderWake() { __rs_native_Host_RequestRenderWake(); return; }
int __rs_native_Host_RequestWakeAfterMs(int delayMs) { return 0; }
public void Host_RequestWakeAfterMs(int delayMs) { __rs_native_Host_RequestWakeAfterMs(delayMs); return; }
int __rs_native_Host_CloseWindow() { return 0; }
public void Host_CloseWindow() { __rs_native_Host_CloseWindow(); return; }
int __rs_native_Host_PlaySound(string uri, float volume) { return 0; }
public void Host_PlaySound(string uri, float volume) { __rs_native_Host_PlaySound(uri, volume); return; }
public int Host_GetLocalTimePart(int part) { return 0; }

public string Reflect_GetPropString(object target, string propPath) { return ""; }
public float Reflect_GetPropFloat(object target, string propPath) { return 0.0f; }
public int Reflect_GetPropInt(object target, string propPath) { return 0; }
public int Reflect_ResolveEnumOrdinal(object target, string propPath, string memberName) { return -2147483647; }
public bool Reflect_GetPropBool(object target, string propPath) { return false; }
public object Reflect_GetPropObject(object target, string propPath) { return null; }
public int Reflect_GetPropTypeKind(object target, string propPath) { return 0; }
public bool Reflect_SetPropString(object target, string propPath, string val) { return false; }
public bool Reflect_SetPropFloat(object target, string propPath, float val) { return false; }
public bool Reflect_SetPropInt(object target, string propPath, int val) { return false; }
public bool Reflect_SetPropBool(object target, string propPath, bool val) { return false; }
public bool Reflect_SetPropObject(object target, string propPath, object val) { return false; }
public bool Reflect_ListSet(object list, int index, object item) { return false; }
public bool Reflect_ListAdd(object list, object item) { return false; }
public bool Reflect_ListInsert(object list, int index, object item) { return false; }
public bool Reflect_ListRemoveAt(object list, int index) { return false; }
public bool Reflect_InvokeCommand(object target, string propPath, string param) { return false; }
public int Reflect_GetDirtyMask(object target) { return 0; }
public int Reflect_GetGlobalObservableEpoch() { return 1; }
public uint Color_ParseRgba(string hexOrNamed) { return 0u; }
public string Host_ReadTextAsset(string uri) { return ""; }
public int Utf8_CodePointCount(string text) { return 0; }
public string Utf8_SliceCodePoints(string text, int startCp, int countCp) { return ""; }
public string Utf8_FromCodePoint(int cp) { return ""; }

// Stateless Native XML DOM Parser Intrinsics (no caching; cleared immediately after parse)
public int Xml_ParseDocument(string xmlText) { return -1; }
int __rs_native_Xml_ClearDocument() { return 0; }
public void Xml_ClearDocument() { __rs_native_Xml_ClearDocument(); return; }
public int Xml_NodeMeta(int nodeId) { return 0; }
public string Xml_NodeRawName(int nodeId) { return ""; }
public string Xml_NodeLocalName(int nodeId) { return ""; }
public string Xml_NodeText(int nodeId) { return ""; }
public string Xml_AttrName(int nodeId, int attrIdx) { return ""; }
public string Xml_AttrValue(int nodeId, int attrIdx) { return ""; }
