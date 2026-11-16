# AeroGUI 3.0 Architecture Design

AeroGUI 3.0 is a **RealScript-First Visual Microkernel Architecture**. Rather than implementing heavyweight widget classes, style engines, data-binding graphs, and XAML object factories in C++, C++ provides only a slim, deterministic, zero-RTTI rendering and input microkernel, while the entire UI framework (`Aero.UI`), control library, MVVM binding engine, styling, and XAML views live in compiled RealScript bytecode or AOT-compiled C++17.

---

## 1. High-Level Layering & 6-Target CMake DAG

```
+-----------------------------------------------------------------------+
|  User Applications (Code-First .rs / Compiled XAML .xaml + .rs)       |
+-----------------------------------+-----------------------------------+
                                    |
        +---------------------------+---------------------------+
        v                                                       v
+-------------------------------+               +-------------------------------+
|  Aero.UI Stdlib (RealScript)  |               |     aerogui_compiler          |
|  - Controls, Layouts, Styles  |               |  - XML Pull Lexer             |
|  - ResourceDictionary, Themes |               |  - Compiled XAML -> .g.rs     |
|  - BindingEngine, Storyboard  |               |  - RealScript Bytecode / AOT  |
+---------------+---------------+               +---------------+---------------+
                | FFI (Aero.Native::*)                          |
                v                                               v
+-----------------------------------------------------------------------+
|  aerogui_app (AeroAppHost) — Dual Bytecode Interpreter & C++17 AOT    |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|  aerogui_host — Tri-Platform Windowing & Event Loop (Zero RTTI /GR-)  |
|  - NativeWindowHost (Win32+WGL, X11+GLX/EGL, Software, Emscripten)    |
|  - DesktopEventHost / WebCanvasHost (0% CPU Idle Sleep)               |
+-----------------------------------+-----------------------------------+
                                    |
          +-------------------------+-------------------------+
          v                                                   v
+-----------------------------------+       +-----------------------------------+
|  aerogui_visual (Microkernel)     |       |  aerogui_render                   |
|  - VisualWorld (5 Primitives)     | ----> |  - PagedRenderTree (4KB Pages)    |
|  - Depth-Bucket LayoutSolver      |       |  - ZeroCopyGpuStream              |
|  - VisualInputRouter & AnimEngine |       |  - SoftwareRasterizer & OpenGL 3.3|
+-----------------------------------+       +-----------------------------------+
          |                                                   |
          +-------------------------+-------------------------+
                                    v
+-----------------------------------------------------------------------+
|  aerogui_foundation — Core Math, Generational Handles, ExecutionDomains|
|  - RealScript::SlimRuntime (Interpreter + AotRuntime, zero Extensibility)|
+-----------------------------------------------------------------------+
```

### Target Dependency DAG (from `CMakeLists.txt`)
1. **`aerogui_foundation`** (Core math, `HandleRegistry`, `UIResourceStore`, `ExecutionDomainContext`, `RealScript::SlimRuntime`). Built with `/GR-`.
2. **`aerogui_render`** (`PagedRenderTree`, `RenderCommandEncoder`, `ZeroCopyGpuStream`, `FontAtlas`, `ImageAtlas`, `MtsdfPathBaker`, `SoftwareRasterizer`, `OpenGlDeviceBackend`). Links `aerogui_foundation` under `/GR-`.
3. **`aerogui_visual`** (`VisualWorld`, 64-level `VisualLayoutSolver`, `VisualAnimationEngine`, `VisualInputRouter`, `DrawingCommandBuffer`, `VisualRenderAdapter`, `VisualHostBindings`). Links `aerogui_render` under `/GR-`.
4. **`aerogui_host`** (`DesktopEventHost`, `WebCanvasHost`, `NativeWindowHost`). Links `aerogui_visual` and OS/GL system libraries under `/GR-`.
5. **`aerogui_compiler`** (`XamlPullParser`, `CSharpCompatLowerer`, `XamlToRsEmitter`, `StdlibModulePack`, `AeroCompilerV3`). Links `aerogui_foundation` + `RealScript::Compiler`.
6. **`aerogui_app`** (`AeroAppHost`). Links `aerogui_host` and optionally `aerogui_compiler` (`AERO_HAS_COMPILER_V3`). Slim AOT binaries link **`aerogui_host` ONLY** and omit `aerogui_compiler` and bytecode `Interpreter` completely.

---

## 2. The 5 Visual Microkernel Primitives

All `Aero.UI` controls (`Button`, `TextBox`, `ListBox`, `TreeView`, `TabControl`, `ScrollViewer`, `Menu`, `Path`, `CustomDrawElement`) decompose cleanly into **5 C++ `VisualKind` primitives** inside `VisualWorld`:

| `VisualKind` | Purpose | Retained Native State |
|---|---|---|
| `Box` (0) | Panels, borders, rounded rects, backgrounds, shadows, clipping containers | Background/border RGBA or gradient IDs, thickness, corner radii, layout mode, 2D/3D transforms, effects |
| `Text` (1) | Labels, `TextBlock`, editable text views | UTF-8 text, font family/size, RGBA foreground, wrapping, trimming, caret/selection spans |
| `Image` (2) | Raster bitmaps, icons, vector-baked sprites | Asset URI, `Stretch`, `StretchDirection`, tint RGBA |
| `Vector` (3) | `Path`, `Rectangle`, `Ellipse`, `Line`, `Polygon`, `Polyline` | Shape kind, SVG path spec, fill/stroke colors & gradients, stroke width, `TrimStart`/`TrimEnd` |
| `CanvasDraw` (4) | Retained imperative `CustomDrawElement` 2D canvas | Recorded `DrawingCommandBuffer` command stream (`FillRect`, `FillRoundedRect`, `FillEllipse`, `DrawPath`, `DrawText`, clip/transform stacks) |

---

## 3. Compiled XAML Pipeline (`aero-compiler`)

AeroGUI 3.0 performs **zero runtime XAML reflection or XML parsing**:
1. **`XamlPullParser`**: Streaming XML pull lexer + attribute/property-element/markup-extension parser (`{Binding}`, `{x:Bind}`, `{StaticResource}`, `{TemplateBinding}`).
2. **`CSharpCompatLowerer`**: Lowers C#-style `.cs` / code-behind conveniences cleanly to RealScript syntax.
3. **`XamlToRsEmitter`**: Emits strongly-typed `partial class <Class> : <Base>` `.g.rs` source with `InitializeComponent()`, typed `x:Name` fields, compiled `BindingEngine` registrations, `DataTemplate`/`ControlTemplate` factories, and `ResourceDictionary` lookups.
4. **`StdlibModulePack` + RealScript Compiler**: Links user `.rs` + `.g.rs` with `Aero.Native` + `Aero.UI`, optionally optimizing bytecode (`--O2`, inlining `Aero.Native` void forwarders cleanly to `__rs_native_*`) or emitting standalone C++17 AOT headers/sources (`--aot-cpp`).

---

## 4. Dual Execution Backends & Dual-Domain Isolation

`AeroAppHost` supports two runtime backends identically via `VisualHostBindings`:
- **Bytecode Mode (`LoadProgramImage` / `CompileAndLoad`)**: Executes linked `ProgramImage` bytecode via `rs_rt::Interpreter` (`UiPresentation` + `SimDeterministic` interpreters).
- **C++17 AOT Mode (`LoadAotProgram`)**: Binds `const rs_aot::ProgramDescriptor*` directly to `rs_aot::ExecutionContext` with zero bytecode `Interpreter`, zero reflection library, and `/GR-` zero-RTTI.

### Dual-Domain & Track-B ECS Architecture
`ExecutionDomainContext` maintains two isolated `ManagedHeap`s (`simHeap_` and `uiHeap_`):
- **`SimDeterministic` (`[SimState]`)**: Deterministic simulation state allocated and mutated exclusively inside `SimDeterministic`. `UiPresentation` reads `[SimState]` via `CrossHeapAccessPolicy::ReadOnlySanitizeStrings`; direct cross-domain writes fail loudly with `CrossHeapWriteViolation`.
- **`[SimCommand]` Queue**: Calling `[SimCommand]` methods from UI event handlers is intercepted (`DomainPolicy::interceptCall`), queued as `SimCommandEnvelope`, and flushed deterministically on `SimDeterministic` during `StepFrame`.
- **Track-B Zero-Alloc ECS**: Native C++ telemetry arrays bind directly via `VisualWorld::BindEcsColumn` / `AeroAppHost::BindSimFloatColumn`, updating `VisualNode` geometry/colors/transforms with zero managed heap allocations.

---

## 5. Frame Pipeline & 0% CPU Idle Sleep

Each frame in `AeroAppHost::StepFrame(dt)` / `DesktopEventHost`:
1. **`SimTick` / `[SimCommand]` Flush**: Run deterministic sim tick & cross-domain command envelopes.
2. **Input Dispatch**: `VisualInputRouter::FlushQueuedEvents()` routes hit-tested pointer/wheel/key/text/focus events to `Aero.UI::InputDispatcher::__AeroDispatch*`.
3. **Binding & Script Sync**: `BindingEngine::Flush()` evaluates OneWay/TwoWay/Multi bindings cleanly.
4. **Track-B ECS & Animation**: `VisualWorld::FlushTrackB()` + `VisualAnimationEngine::Tick(dt, world)`.
5. **Layout & Retained Draw**: 64-level depth-bucket `VisualWorld::UpdateLayout(vpW, vpH)` (including `__AeroDispatchCustomMeasure`), followed by `__AeroDispatchDraw` for dirty/resized `CanvasDraw` nodes.
6. **Render Sync & Present**: `VisualRenderAdapter::SyncPagedRenderTree` packs dirty subtrees into 4KB render pages; `ZeroCopyGpuStream` / `NativeWindowHost` presents via OpenGL 3.3 Core or `SoftwareRasterizer`.

When `DesktopEventHost::CanSleepIdle()` is `true` (no active animations, dirty objects, dirty layout, or pending input), the event loop sleeps at **0% CPU/GPU usage** via Win32 `MsgWaitForMultipleObjects`, X11 `poll()`, or on-demand Emscripten `rAF`.
