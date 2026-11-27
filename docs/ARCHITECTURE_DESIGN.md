# AeroGUI 3.0 Architecture Design

AeroGUI 3.0 is a **RealScript-First Visual Microkernel Architecture**. Rather than implementing heavyweight widget classes, style engines, data-binding graphs, and XAML object factories in C++, C++ provides a slim, deterministic, zero-RTTI (`/GR-`) rendering and input microkernel, while the UI framework (`Aero.UI`), control library, MVVM binding engine, styling, and XAML views execute as compiled RealScript bytecode or AOT-compiled C++17.

---

## 1. High-Level Layering & CMake Target DAG

```
+-----------------------------------------------------------------------+
|  User Applications (Code-First .rs / Compiled XAML .xaml + .rs)       |
+-----------------------------------+-----------------------------------+
                                    |
        +---------------------------+---------------------------+
        v                                                       v
+-------------------------------+               +-------------------------------+
|  Aero.UI Stdlib (RealScript)  |               |     aerogui_compiler          |
|  - Aero.UI.Core (10 modules)  |               |  - XmlPullLexer + XamlDom     |
|  - Controls / Shapes / Data   |               |  - XamlSchemaDef & Emitters   |
|  - Optional RuntimeXaml       |               |  - CSharpCompatLowerer        |
|  - Workbench / EditorKit      |               |  - StdlibModulePack / DLL     |
+---------------+---------------+               +---------------+---------------+
                | Declarative FFI (Aero.Native::*)              |
                v                                               v
+-----------------------------------+       +-----------------------------------+
|  aerogui_app_runtime (/GR-)       | <---- |  aerogui_app_dev_host (optional)  |
|  - AeroAppHost & AppBootstrap     |       |  - AeroAppDevHost (live compile)  |
+-----------------+-----------------+       +-----------------------------------+
                  |
                  v
+-----------------------------------------------------------------------+
|  aerogui_host — Tri-Platform Windowing, Audio & Event Loop (/GR-)     |
|  - NativeWindowHost (Win32+WGL, X11+GLX/EGL, Software, Emscripten)    |
|  - Win32AudioEngine (WAV/MediaFoundation decode + 8-voice waveOut)    |
|  - DesktopEventHost / WebCanvasHost (0% CPU Idle Sleep)               |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|  aerogui_visual — Retained Visual Microkernel (/GR-)                  |
|  - VisualWorld (5 Primitives) + 64-level VisualLayoutSolver           |
|  - VisualAnimationEngine, VisualInputRouter, VisualRenderAdapter      |
|  - DrawingCommandBuffer + SyntaxHighlighter + NativeXmlDomBridge      |
|  - VisualHostBindings (C++17 Typed FFI Unpacker)                      |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|  aerogui_render — Atlases, Command Pages & GPU/CPU Raster (/GR-)      |
|  - UIResourceStore, TrueTypeFontParser, FontAtlas, TextShapingBidi    |
|  - ImageDecoders, ImageAtlas, RiveRenderer                            |
|  - PagedRenderTree (4KB Pages), ZeroCopyGpuStream, SoftwareRasterizer |
|  - GpuBackend (Desktop OpenGL 3.3 Core / WebGL2 / WebGPU WGSL)        |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|  aerogui_foundation — Math, Handles, Codecs & Runtime Facade (/GR-)   |
|  - Geometry, ChunkedArena, ColorParser, ArbcCodec, AssetUriResolver   |
|  - ExecutionDomainContext, ScriptBackendFacade, AeroObservableList    |
|  - GradientBrush, MtsdfPathBaker + RealScript::SlimRuntime            |
+-----------------------------------------------------------------------+
```

### Target Dependency DAG (from `CMakeLists.txt`)
1. **`aerogui_foundation`** (`Geometry`, `ChunkedArena`, `StringUtils`, `ColorParser`, `ArbcCodec`, `AssetUriResolver`, `GradientBrush`, `MtsdfPathBaker`, `ExecutionDomainContext`, `AeroObservableList`, `ScriptBackendFacade`, `RealScript::SlimRuntime`). Built with `/GR-`.
2. **`aerogui_render`** (`UIResourceStore`, `TrueTypeFontParser`, `FontAtlas`, `TextShapingBidi`, `ImageDecoders`, `ImageAtlas`, `RiveRenderer`, `PagedRenderTree`, `SoftwareRasterizer`, `ZeroCopyGpuStream`, `GpuBackend`). Strictly layered above `aerogui_foundation` (zero function-pointer inversion hooks) and built with `/GR-`.
3. **`aerogui_visual`** (`VisualWorld`, 64-level `VisualLayoutSolver`, `SyntaxHighlighter`, `DrawingCommandBuffer`, `VisualAnimationEngine`, `VisualInputRouter`, `VisualRenderAdapter`, `NativeXmlDomBridge`, `VisualHostBindings`). Links `aerogui_render` under `/GR-`.
4. **`aerogui_host`** (`DesktopEventHost`, `WebCanvasHost`, `Win32AudioEngine`, `NativeWindowHost`). Links `aerogui_visual` and OS/GL/audio system libraries under `/GR-`.
5. **`aerogui_compiler`** (`XmlPullLexer`, `XamlDom`, `XamlSchemaDef`, `MarkupExtensionParser`, `XamlResourcePruner`, `XamlBrushAndStoryboardEmitter`, `XamlToRsEmitter`, `CSharpCompatLowerer`, `MirReachabilityPruner`, `StdlibModulePack`, `StdlibDllLoader`, `AeroCompilerV3`). Links `aerogui_foundation` + `RealScript::Frontend` with zero GUI/render/host dependencies.
6. **`aerogui_app_runtime`** (`AeroAppHost`, `AppBootstrap`). Links `aerogui_host` ONLY under `/GR-` — slim AOT and precompiled `.arbc` binaries never pull in `aerogui_compiler` or RTTI. **`aerogui_app_dev_host`** (`AeroAppDevHost`, aliased via `aerogui_app` when compiler tools are enabled) layers live XAML+RealScript compilation on top for dev tools and compiler tests.

---

## 2. The 5 Visual Microkernel Primitives

All `Aero.UI` controls (`Button`, `TextBox`, `ListBox`, `TreeView`, `TabControl`, `ScrollViewer`, `Menu`, `Path`, `CustomDrawElement`) decompose cleanly into **5 C++ `VisualKind` primitives** inside `VisualWorld`:

| `VisualKind` | Purpose | Retained Native State |
|---|---|---|
| `Box` (0) | Panels, borders, rounded rects, backgrounds, shadows, clipping containers | Background/border RGBA or gradient IDs, thickness, corner radii, layout mode, 2D/3D transforms, effects |
| `Text` (1) | Labels, `TextBlock`, editable text views | UTF-8 text, font family/size, RGBA foreground, wrapping, trimming, rich spans, caret/selection |
| `Image` (2) | Raster bitmaps, procedural plasma, Rive artboards, `VisualBrush` | Asset URI, `Stretch`, `StretchDirection`, viewbox/viewport, tint RGBA |
| `Vector` (3) | `Path`, `Rectangle`, `Ellipse`, `Line`, `Polygon`, `Polyline` | Shape kind, SVG path spec, fill/stroke colors & gradients, stroke width/dash, `TrimStart`/`TrimEnd` |
| `CanvasDraw` (4) | Retained imperative `CustomDrawElement` 2D canvas | Recorded `DrawingCommandBuffer` command stream (`FillRect`, `FillRoundedRect`, `FillEllipse`, `DrawPath`, `DrawText`, `DrawSyntaxLine`, clip/transform stacks) |

---

## 3. Compiled & Optional Runtime XAML Pipeline

1. **Unified XML Lexing (`XmlPullLexer` + `XamlDom`)**: Streaming UTF-8 byte-offset & line/column pull lexer shared across DOM parsing and `NativeXmlDomBridge`, plus `MarkupExtensionParser` (`{Binding}`, `{x:Bind}`, `{StaticResource}`, `{DynamicResource}`, `{TemplateBinding}`, `{RelativeSource}`).
2. **Shared Schema (`XamlSchemaDef`)**: Single source of truth for control property metadata, default TwoWay binding rules (`TextBox.Text`, `CheckBox.IsChecked`, `Slider.Value`, etc.), and storyboard property-path normalization.
3. **Modular Emitter (`XamlResourcePruner` + `XamlBrushAndStoryboardEmitter` + `XamlToRsEmitter`)**: Prunes unreachable static resources, lowers brushes/storyboards/triggers/templates, and emits strongly-typed `partial class <Class> : <Base>` `.g.rs` source.
4. **Modular `Aero.UI` Stdlib & Tree-Shaking (`StdlibModulePack`)**:
   - `Aero.UI.Core` is organized into focused single-responsibility modules (`Core.rs`, `BrushesTransformsEffects.rs`, `UIElement.rs`, `InputAndFocus.rs`, `ResourceDictionary.rs`, `StylesAndTriggers.rs`, `TemplatesAndVsm.rs`, `Storyboard.rs`, `BindingEngine.rs`, `EventsAndCommands.rs`, `MathConverter.rs`).
   - `Aero.UI.RuntimeXaml` (`XamlReader.rs`) is strictly opt-in and only linked when `XamlReader` is referenced.

---

## 4. Unified Dual Execution Backends & Dual-Domain Isolation

`AeroAppHost` delegates reflection, object construction, field reads, and function/method invocation through **`ScriptBackendFacade`**, supporting both execution backends identically without duplicated dispatch branches:
- **Bytecode Mode (`LoadProgramImage` / `LoadArbcFile`)**: Executes linked `ProgramImage` bytecode via `rs_rt::Interpreter` (with optional stencil JIT).
- **C++17 AOT Mode (`LoadAotProgram`)**: Binds `const rs_aot::ProgramDescriptor*` directly to `rs_aot::ExecutionContext` with zero bytecode `Interpreter` and `/GR-` zero-RTTI.

### Dual-Domain & Track-B ECS Architecture
`ExecutionDomainContext` maintains two isolated `ManagedHeap`s (`simHeap_` and `uiHeap_`):
- **`SimDeterministic` (`[SimState]`)**: Deterministic simulation state allocated and mutated exclusively inside `SimDeterministic`. `UiPresentation` reads `[SimState]` via `CrossHeapAccessPolicy::ReadOnlySanitizeStrings`; direct cross-domain writes fail with `CrossHeapWriteViolation`.
- **`[SimCommand]` Queue**: Calling `[SimCommand]` methods from UI event handlers is intercepted (`DomainPolicy::interceptCall`), queued as `SimCommandEnvelope`, and flushed deterministically via `AeroAppHost::FlushPendingSimCommands`.
- **Track-B Zero-Alloc ECS**: Native C++ telemetry arrays bind directly via `VisualWorld::BindEcsColumn` / `AeroAppHost::BindSimFloatColumn`, updating `VisualNode` geometry/colors/transforms with zero managed heap allocations.

---

## 5. Frame Pipeline & 0% CPU Idle Sleep

Each frame in `AeroAppHost::StepFrame(dt)` / `DesktopEventHost`:
1. **`SimTick` / `[SimCommand]` Flush**: Run deterministic sim tick & cross-domain command envelopes.
2. **Input Dispatch**: `VisualInputRouter::FlushQueuedEvents()` routes hit-tested pointer/wheel/key/text/focus events to `Aero.UI::InputDispatcher::__AeroDispatch*`.
3. **Binding & Script Sync**: `BindingEngine::Flush()` evaluates OneWay/TwoWay/Multi bindings cleanly.
4. **Track-B ECS & Animation**: `VisualWorld::FlushTrackB()` + `VisualAnimationEngine::Tick(dt, world)`.
5. **Layout & Retained Draw**: 64-level depth-bucket `VisualWorld::UpdateLayout(vpW, vpH)` (including `__AeroDispatchCustomMeasure`), followed by `__AeroDispatchDraw` for dirty/resized `CanvasDraw` nodes.
6. **Render Sync, Present & Bounded GC**: `VisualRenderAdapter::SyncPagedRenderTree` packs dirty subtrees into 4KB render pages; `ZeroCopyGpuStream` / `NativeWindowHost` presents via OpenGL 3.3 Core or `SoftwareRasterizer` (with explicit `RasterBandClip` parallel scanline bands), followed by `RunFrameGcMaintenance`.

When `DesktopEventHost::CanSleepIdle()` is `true` (no active animations, dirty objects, dirty layout, or pending input), the event loop sleeps at **0% CPU/GPU usage** via Win32 `MsgWaitForMultipleObjects`, X11 `poll()`, or on-demand Emscripten `rAF`.
