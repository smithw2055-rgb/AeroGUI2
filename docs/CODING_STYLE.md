# AeroGUI 3.0 Coding Style & Invariants

These rules apply across all C++ microkernel/host targets (`aerogui_*`) and RealScript `Aero.UI` application & stdlib modules.

---

## 1. C++ Microkernel, Render & Host Conventions

### 1.1 Zero RTTI (`/GR-`)
- Every core, render, visual, and host C++ target compiles with `/GR-` (`-fno-rtti`).
- **Strictly forbidden**: `dynamic_cast`, `typeid`, `<typeinfo>`, `<typeindex>`, and any dependency on `RealScript::Extensibility` reflection headers (`realscript/reflection/Reflection.h`).
- Use tagged unions, `enum class VisualKind`, `std::variant`, or static interface dispatch instead of runtime C++ RTTI.

### 1.2 32-Bit `float` UI Geometry
- All UI layout coordinates, dimensions, margins, padding, scroll offsets, transforms, colors (`0..1` normalized RGBA floats or packed RGBA8 `uint32_t`), animation values, and frame `dt` timings MUST use 32-bit **`float`**, never `double`.
- Keep spatial tolerances clean (e.g., `std::fabs(a - b) > 0.25f`).

### 1.3 Generational Handles & GC Safety
- Cross-boundary node references ALWAYS use `NativeHandle` / `VisualNodeId` (`{slot, generation}`), validated via `VisualWorld::ResolveHandle` and `GetNode`. Never store raw `VisualNode*` pointers across frames or callbacks.
- Any `ObjectRef` retained beyond the current invocation frame on C++ (such as `rootObject_`, `simObject_`, `VisualWorld` node owners) MUST be pinned via `ManagedHeap::retain(...)` (`PersistentRoot`) and explicitly cleared on unload/reset.
- Never leak `ObjectKind::String` or `ObjectKind::Array` `ObjectRef` values out of native FFI bindings whose RealScript return type is `object` (`PrimitiveType::Object`); guard with `ov->kind == rs_rt::ObjectKind::Record` and return `rs_rt::NullObject{}` or typed `StringValue` as appropriate.
- Always pass an explicit `DrawingCommandBuffer&` to `RegisterVisualHostBindings` rather than relying on fallback singletons.

### 1.4 0% CPU Idle Sleep Discipline
- Never busy-poll frames when `DesktopEventHost::CanSleepIdle()` is `true`.
- Only mark `NotifyDirtyLayout()`, `NotifyDirtyObjects()`, or `SetActiveAnimations(true)` when state or animation curves actually change.

---

## 2. RealScript (`Aero.UI`) Framework & App Conventions

### 2.1 Avoid Bare `object` Fields and Captured `object` Variables (`RS5151`)
- RealScript strict verification forbids bare `object` class fields and bare `object` closure captures (`RS5151`).
- Use strongly typed fields (`DependencyObject`, `IValueConverter`, `ViewModelBase`, `UIElement`, `TypedBox<T>`) instead of `object` fields.
- Inside lambdas/event handlers, copy captured outer variables into a typed local before calling methods or setting properties (e.g., `var state = vm; state.Increment();`), or bind strong delegates cleanly.

### 2.2 Type-Safe Boxing (`DependencyObject` & `TypedBox<T>`)
- Wrap primitive values crossing generic `Content`, `Header`, `CommandParameter`, or `ItemsSource` boundaries using `DependencyObject.BoxString`, `BoxFloat`, `BoxInt`, `BoxBool`, or `TypedBox<T>` (and unbox via `DependencyObject.UnboxString` / `UnboxFloat` / `UnboxInt` / `UnboxBool`).
- Keep `TypedBox<T>` overrides type-safe (`BoxedString`, `BoxedFloat`, `BoxedInt`, `BoxedBool` return `this` directly in `UnboxObject()`).
- Keep `ObservableList<T>` (`_items` wrapper) free of extra fields that shift field indices expected by `ExecutionDomainContext`.

### 2.3 Override Accessibility & Module Imports
- RealScript enforces `RS2525` (`override` methods MUST match base visibility — e.g., `CustomDrawElement.OnRender(DrawingContext dc)` is `public virtual`, so overrides MUST be `public override void OnRender(DrawingContext dc)`).
- Import `Aero.UI` in every UI module and `RealScript.Math` whenever calling `Math.*` (`Math.Sinf`, `Math.Absf`, `Vec2f`, etc.).

### 2.4 Dual-Domain (`[SimState]` & `[SimCommand]`) Rules
- Never allocate or mutate `[SimState]` objects from `UiPresentation` constructors/handlers (`MainWindow()` runs in `UiPresentation` and will trigger `CrossHeapWriteViolation`).
- Construct `[SimState]` inside a `SimDeterministic` factory (`public static MySimState CreateSimState()`) or `DomainKind::SimDeterministic` step, attach read-only to the UI (`AttachSimState`), and mutate simulation state strictly via `[SimCommand]` methods or `SimTick` callbacks.
- Never store a reference to a `UiHeap` object inside a `SimDeterministic` object (`CrossHeapReferenceViolation`).

### 2.5 Compiled XAML Authoring Rules
- Always declare `x:Class="Namespace.ClassName"` on the root `<Window>` / `<UserControl>` and match with `public partial class ClassName : Window` in `.rs` calling `InitializeComponent()`.
- Declare `x:DataType="MyViewModel"` on views and `<DataTemplate x:DataType="ItemType">` inside `ListBox`/`TreeView` templates for zero-overhead compiled property bindings.
- For programmatic/dynamic gradients (`RadialGradientBrush`), multi-track `Storyboard` timelines (`AddDoubleAnimation`, `AddColorAnimation`, `AddSpringAnimation`), or custom retained `CustomDrawElement` canvases, wire directly in code-behind `.rs` (`MainWindow()`) with explicit 32-bit `float` arguments.
