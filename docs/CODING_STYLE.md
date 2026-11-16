# AeroGUI 2.0 + RealScript C++17 编码规范（Coding Style Guide）

为保持 AeroGUI 2.0 纯 C++17 代码库的极简性、高可读性与跨界零摩擦协作，消除代码中冗长的 `realscript::runtime::`、`realscript::semantic::`、`realscript::aot::` 等跨模块限定前缀，特制定本编码规范。所有 `include/aero/**`、`src/**`、`tools/**` 与 `tests/**` 代码必须严格遵循。

---

## 一、 RealScript 跨界命名空间别名与核心类型直导规范

### 1. 统一头文件入口：`aero/core/RsAliases.hpp`

AeroGUI 2.0 在 `include/aero/core/RsAliases.hpp`（包含 `AeroExecutionDomains.hpp` 与 `AeroObservableList.hpp`，并由 `aero/core/UnifiedValue.hpp` 默认包含）中统一定义了 `namespace aero` 内部的短命名空间别名与高频核心类型别名。

**禁止**在 AeroGUI 2.0 的头文件或实现文件中散落手写冗长的 `realscript::runtime::xxx` 或 `realscript::semantic::xxx`。

### 2. 标准短命名空间别名（在 `namespace aero` 内直接可用）

| 短命名空间别名 | 对应完整命名空间 | 典型使用场景 |
| :--- | :--- | :--- |
| `rs` | `::realscript` | 顶层跨模块访问 |
| `rs_rt` | `::realscript::runtime` | 运行时函数与辅助工具（如 `rs_rt::valueType`、`rs_rt::markValueInPools`） |
| `rs_sem` | `::realscript::semantic` | 语义层符号与工具函数（如 `rs_sem::stableTypeId`、`rs_sem::TypeSymbol`） |
| `rs_bc` | `::realscript::bytecode` | 字节码指令、模块编解码与 Lowerer（如 `rs_bc::FunctionReference`、`rs_bc::Module`） |
| `rs_diag` | `::realscript::diagnostics` | 编译与校验诊断容器（如 `rs_diag::DiagnosticBag`） |
| `rs_bind` | `::realscript::bindgen` | 原生绑定代码生成与绑定元数据 |
| `rs_comp` | `::realscript::compiler` | 编译器前端会话与源文件（如 `rs_comp::Compilation`、`rs_comp::SourceFile`） |
| `rs_hr` | `::realscript::hot_reload` | 状态迁移与热重载会话（如 `rs_hr::HotReloadSession`） |
| `rs_mir` | `::realscript::mir` | MIR 中间表示构建与分析 |
| `rs_opt` | `::realscript::optimization` | MIR / 编译期优化管线 |
| `rs_aot` | `::realscript::aot` | C++17 AOT 代码生成与执行上下文（如 `rs_aot::ExecutionContext`） |

### 3. 核心跨界类型直导（在 `namespace aero` 内零前缀直接使用）

以下高频类型已在 `namespace aero` 中通过 `using`（或直接声明于 `AeroExecutionDomains.hpp`）导出，在 `namespace aero` 内部编写代码时**直接使用裸类型名**，无需任何命名空间前缀：

```cpp
namespace aero {

// 命名空间短别名
namespace rs      = ::realscript;
namespace rs_rt   = ::realscript::runtime;
namespace rs_sem  = ::realscript::semantic;
namespace rs_bc   = ::realscript::bytecode;
namespace rs_diag = ::realscript::diagnostics;
namespace rs_bind = ::realscript::bindgen;
namespace rs_comp = ::realscript::compiler;
namespace rs_hr   = ::realscript::hot_reload;
namespace rs_mir  = ::realscript::mir;
namespace rs_opt  = ::realscript::optimization;
namespace rs_aot  = ::realscript::aot;

// 语义与符号标识
using SymbolId               = rs_sem::SymbolId;
using PrimitiveType          = rs_sem::PrimitiveType;

// 统一值模型与标量/复合包装值
using UnifiedValue           = rs_rt::Value;
using ValueTag               = rs_rt::ValueTag;
using Float4Value            = rs_rt::QuatfValue;
using FloatValue             = rs_rt::FloatValue;
using StringValue            = rs_rt::StringValue;
using StructValue            = rs_rt::StructValue;
using EnumValue              = rs_rt::EnumValue;
using NullObject             = rs_rt::NullObject;
using NullString             = rs_rt::NullString;
using NullArray              = rs_rt::NullArray;

// 托管堆引用、弱引用与原生句柄
using ObjectRef              = rs_rt::ObjectRef;
using ObjectKind             = rs_rt::ObjectKind;
using WeakRef                = rs_rt::WeakRef;
using NativeHandle           = rs_rt::NativeHandle;
using NativeHandleRegistry   = rs_rt::NativeHandleRegistry;
using ManagedHeap            = rs_rt::ManagedHeap;
using PersistentRoot         = rs_rt::PersistentRoot;
using ShadowStack            = rs_rt::ShadowStack;
using NativeRawColumnView    = rs_rt::NativeRawColumnView;
using NativeFieldLayout      = rs_rt::NativeFieldLayout;
using NativeFieldKind        = rs_rt::NativeFieldKind;

// VM 程序镜像、解释器与原生绑定注册表
using ProgramImage           = rs_rt::ProgramImage;
using Interpreter            = rs_rt::Interpreter;
using BindingRegistry        = rs_rt::BindingRegistry;
using ValueSpan              = rs_rt::ValueSpan;
using RuntimeError           = rs_rt::RuntimeError;
using ErrorCode              = rs_rt::ErrorCode;

// 双执行域（SimDomain + UiDomain）与零装箱属性槽位（定义于 AeroExecutionDomains.hpp）
enum class DomainKind : std::uint8_t;
class ExecutionDomainContext;
struct SimCommandEnvelope;
struct PropertySlotHandle;

} // namespace aero
```

### 4. 代码对比示例

* **反例（禁止）**：
  ```cpp
  bool UIElement::BindScriptWrapper(
      realscript::runtime::NativeHandleRegistry& registry,
      realscript::runtime::ManagedHeap& uiHeap,
      realscript::runtime::ObjectRef wrapperRef);
  ```
* **正例（推荐）**：
  ```cpp
  bool UIElement::BindScriptWrapper(
      NativeHandleRegistry& registry,
      ManagedHeap& uiHeap,
      ObjectRef wrapperRef);
  ```

---

## 二、 命名与代码风格规范

1. **命名风格**：
   * **类型名（Class / Struct / Enum / Type Alias）**：使用 `PascalCase`（如 `UIElement`、`DualTrackBindingEngine`、`DepthBucketLayoutQueue`）。
   * **`UI` 缩写全大写铁律（`UI*` 规范）**：所有 UI 子系统、上下文与桥接类的**类名、类型别名、枚举及对应头/源文件名**中出现 `UI` 缩写时，必须使用全大写 `UI`（例如 `UIElement`、`UIViewContext`、`UIResourceContext`、`ScopedUIResourceContext`、`ScriptUIBridge`、`UIEffectKind`、`UIEffectDescriptor` 及其对应的 `UIElement.hpp`、`UIViewContext.hpp`、`UIResourceContext.hpp`、`ScriptUIBridge.hpp`）；**严禁引入 `Ui*` / `*Ui*` 混合大小写别名**（历史过渡别名 `UiResourceContext`、`ScopedUiResourceContext`、`ScriptUiBridge` 已全部清理；仅 RealScript 运行时自身标识 `UiDomain` / `UiHeap` 与 `AeroExecutionDomains` 中的 `UiTask*` 保持原名）。
   * **公共字符串与值相等性工具复用**：所有跨模块零分配 ASCII 修剪、大小写不敏感比较、前缀/后缀裁剪与 `float32` 解析统一复用 [`aero/core/StringUtils.hpp`](../include/aero/core/StringUtils.hpp) 中的 `aero::str::*`（`Trim`、`EqualsIgnoreCase`、`StartsWith`、`EndsWith`、`StripNamespacePrefix`、`ShortTypeName`、`TryParseFloat32`、`ParseFloat32Or`）；`UnifiedValue` 判等统一调用 `aero::UnifiedValueEquals`（[`aero/core/UnifiedValue.hpp`](../include/aero/core/UnifiedValue.hpp)），绑定路径与 `DataContext` 遍历统一复用 [`aero/binding/PropertyPathUtils.hpp`](../include/aero/binding/PropertyPathUtils.hpp)（`namespace aero::binding_util`），禁止在各子模块内复制粘贴私有同类辅助函数。
   * **AeroGUI 公开方法与函数**：使用 `PascalCase`（如 `AddChild`、`FlushReactiveBindings`、`MeasureCore`）。
   * **局部变量与函数参数**：使用 `camelCase`（如 `targetElement`、`sourceSlot`、`dirtyMask`）。
   * **私有成员变量**：使用 `camelCase_`（尾部下划线，如 `parent_`、`fastProps_`、`domains_`）。
   * **结构体公开数据成员**：使用 `camelCase`（如 `propId`、`bindingSlotId`、`effectiveValue`）。
   * **编译期常量与枚举值**：使用 `PascalCase` 或 `kPascalCase`（如 `PropId_Width`、`ValueSourceRank::Local`、`kFastInlinePropertySlotCount`）。
2. **内存与所有权约定**：
   * **侵入式引用计数根类 `AeroObject`**：`AeroObject`（`aero/core/AeroObject.hpp`）是 `UIElement` 及其派生视觉树节点的统一侵入式引用计数根基类（内含原子计数 `std::atomic<std::uint32_t> refCount_` 与 `OnDestroy()` 钩子）。所有 `AeroObject` / `UIElement` 实例统一使用 `IntrusivePtr<T>` 与 `MakeIntrusive<T>(...)` 管理所有权，严禁对 `AeroObject` / `UIElement` 使用 `std::shared_ptr`（原兼容桥 `AdoptSharedOwnership` 与 `std::shared_ptr<UIElement>` 工厂重载已彻底移除；自定义控件统一使用 `IntrusiveCustomControlFactory`）。
   * **单视图上下文隔离 `UIViewContext`**：每视图运行期子系统非拥有指针簇（执行域、绑定、样式、动画、布局、输入、渲染树、资源上下文）集中托管于 `UIViewContext`（`aero/core/UIViewContext.hpp`），避免在单个 `UIElement` 实例中重复存储多路子系统指针。
   * **冷数据懒分配 `RareElementData`**：`UIElement` 将高频属性保持在 `FastInlineStorage`（12 槽内联存储）与 `CompactPropertyEntry` 中；不常用的冷字段（`name`、`declaredTypeName`、`cachedNativeHandle`、`boundWrapperRef`、`pinnedWrapperRoot`、`boundRegistry`、`boundUiHeap`、`boundBindingEngine`、`boundStyleEngine`、`boundAnimationEngine`、`keyDownCallback`、`keyUpCallback`、`events` 等）统一收入 `UIElement::RareElementData`，经 `EnsureRareData()` 按需懒分配，且空读/置空（如 `Bind*Engine(nullptr)`）不得触发 `rareData_` 分配。
   * **Pimpl 头文件隔离（`UIResourceContext`）**：Tier 0 头文件 [`aero/core/UIResourceContext.hpp`](../include/aero/core/UIResourceContext.hpp) 通过 `struct Impl` Pimpl 前向声明 `AxbTemplateRegistry`、`FontAtlas`、`ImageAtlas`、`GradientBrushRegistry`，不在公共头文件中引入 Tier 1/2 头文件或 `<mutex>`。
   * **派生类去重基类（`HostDriverBase`）**：跨宿主通用状态与 9 阶段 UI 帧管线统一沉淀至 [`aero/host/HostDrivers.hpp`](../include/aero/host/HostDrivers.hpp) 中的 `HostDriverBase`，`EmbeddedGameHost`、`DesktopEventHost` 与 `WebCanvasHost` 仅保留各自调度/空闲策略；输入法组合状态统一由 [`aero/input/TextInputMethod.hpp`](../include/aero/input/TextInputMethod.hpp) 中的 `ImeCompositionController` 管理。
   * 热路径（属性读写、布局脏队列、响应式槽位同步、ECS 列扫描）必须保持 **0 堆分配（Zero Heap Allocation）**。
3. **异常与 RTTI 约束**：
   * AeroGUI 2.0 内核代码禁用 C++ `dynamic_cast`、`typeid` 与异常抛接（`try` / `catch` / `throw`），统一使用显式返回值 / `RuntimeError*` 错误传递与内置零 RTTI 反射系统，确保 MSVC `/GR-` 与 WebAssembly / Clang / GCC `-fno-exceptions -fno-rtti` 100% 兼容。
   * **标准反射宏与批量注册（`aero/core/TypeClass.hpp`）**：
     * 在类声明体内使用 `AERO_DECLARE_CLASS(Self, Base)`（例如 `AERO_DECLARE_CLASS(UIElement, AeroObject)`）；
     * 在 `.cpp` 定义单元内使用三参数宏 `AERO_IMPLEMENT_CLASS(Self, Base, CanonicalName)`（例如 `AERO_IMPLEMENT_CLASS(UIElement, AeroObject, "Aero.UI.UIElement")`）；
     * 批量注册多个内置或控件类型及其别名时，使用 RAII 守卫 `aero::ScopedTypeBatch`，将欧拉序 `[treeIn, treeOut]` 区间重算延迟到最外层批次结束时一次性执行，避免 O(N^2) 重复重算；控件工厂实例化统一通过 `TypeRegistry::Instance().FindByName(...)` 查表驱动完成。
   * **O(1) 无分支类型判断与转型（`aero/core/AeroObject.hpp`）**：
     * 统一通过继承自 `AeroObject` 的成员模板 `obj->IsA<T>()`、`obj->IsA(typeClassPtr)` 与 `obj->DynamicCast<T>()`（包括 `UIElement::IsA<T>()` 与 `UIElement::DynamicCast<T>()`）完成运行时类型检查与安全向下转型。
4. **与 NoesisGUI 对齐的全链路 `float` 数值规范（禁用 `double`）**：
   * **全链路 32 位 `float`**：AeroGUI 2.0 的所有 UI 数值类型与 NoesisGUI 严格对齐，统一采用 32 位单精度 `float`（**禁止在 UI 层使用 `double`**），从而与底层 GPU 顶点缓冲区（Vertex Buffer）和着色器 Uniform 直接对齐，彻底消除 CPU 到 GPU 的 `double <-> float` 转换开销。
   * **纯数学/几何独立头 `aero/drawing/Geometry.hpp`**：纯 32 位 `float` 几何与数学原语（`Color`、`Size`、`Point`、`Rect`、`Thickness`、`CornerRadius`、`Stretch`、`StretchDirection`、`Orientation`、`ExpandDirection`、`Matrix2D`、`CompositeTransform3DParams`、`Matrix4x4`）独立抽离至 [`include/aero/drawing/Geometry.hpp`](../include/aero/drawing/Geometry.hpp)（零 `RealScript` / `std::variant` 头文件依赖、零 `double`），并由 `aero/core/UnifiedValue.hpp` 自动包含保持兼容。
   * **`UnifiedValue` 标准浮点接口**：在 C++ 侧读写浮点属性或跨界传参时，唯一标准接口为 `MakeFloat(float v)`（底层直接构造 `FloatValue{v}`）与 `TryGetFloat(const UnifiedValue& v, float& out)`，严禁在 UI 属性槽位中存储 `double`。
   * **几何与编译器对齐**：所有几何结构（`Size`、`Point`、`Rect`、`Thickness`、`CornerRadius`、`Color`）、布局管线（`Measure(Size)` / `Arrange(Rect)`）、控件属性（`Width`、`Height`、`MinWidth`、`MinHeight`、`Opacity`、`FontSize`、`Spacing`）、`.axb4` 二进制指令流浮点字面量以及 RealScript 侧 UI 脚本与 `*.g.rs` 生成代码中的 UI 属性类型，全部统一使用 `float`。

---

## 三、 五层单向依赖与头文件包含规范

1. **五层单向依赖定律（严禁反向包含）**：
   AeroGUI 2.0 严格执行自底向上的 5-Tier 单向依赖链，任何低层级模块均不得反向包含高层级模块头文件：
   ```text
   Tier 0 (drawing / core / runtime)
     ─▶ Tier 1 (asset)
       ─▶ Tier 2 (layout / animation / style / binding / input / render / host)
         ─▶ Tier 3 (controls)
           ─▶ Tier 4 (bridge / compiler / tools)
   ```
   * **Tier 0 头文件零越层铁律**：`include/aero/drawing/*`、`include/aero/core/*` 与 `include/aero/runtime/*` 严禁包含 `asset/`、`render/`、`controls/`、`bridge/`、`compiler/` 或 `host/` 头文件。
   * **Tier 0–2 禁止 `#include "aero/controls/..."`**：`drawing`、`core`、`runtime`、`asset`、`layout`、`animation`、`style`、`binding`、`input`、`render`、`host` 目录下的所有头文件与 `.cpp` 源文件**严禁**包含任何 `aero/controls/*` 头文件，也严禁对具体控件类型做向下转型或直接引用控件符号。
   * **Tier 2 / Tier 3 反向解耦铁律**：
     * `include/aero/input/*` 与 `src/input/*` 严禁包含 `host/`（输入法接口通过 `aero/input/TextInputMethod.hpp` 中的 `ITextInputMethodHost` 解耦）；
     * `include/aero/render/PagedRenderTree.hpp` 通过前向声明解耦 `DualTrackBindingEngine`（零 `binding/` 头文件包含）；
     * `src/binding/*` 与 `src/controls/*` 严禁包含 `bridge/` 或 `compiler/`（域清理通过 `DualTrackBindingEngine::RegisterDomainCleanupHook`，子树 `Loaded` 派发通过 `UIElement::DispatchLoadedSubtree` 完成）。
   * **跨层解耦唯一允许路径**：Tier 0–2 如需触发控件行为，必须通过 `UIElement` 上的虚能力钩子（如渲染、输入、IME、ARIA、样式/绑定/模板钩子）、静态注册函数（`RegisterUIElementResourceHooks`、`RegisterUIElementTemplateHooks`、`RegisterUIElementInteractionHooks`）或 `PagedRenderTreeInternal.hpp` 中的 `ControlRenderEmitter` 友元发射器接口完成多态派发，由 Tier 3 (`src/controls/`) 实现具体逻辑。
2. **控件头文件按家族粒度包含规范**：
   * 仅涉及少数控件家族时，**优先包含细粒度头文件**（如 `aero/controls/Control.hpp`、`aero/controls/Decorators.hpp`、`aero/controls/Panels.hpp`、`aero/controls/Shapes.hpp`、`aero/controls/Buttons.hpp`、`aero/controls/RangeControls.hpp`、`aero/controls/TextControls.hpp`、`aero/controls/ItemsControls.hpp`、`aero/controls/MediaControls.hpp`、`aero/controls/AxbTemplateMounter.hpp`），以最小化预处理体积与重编译涟漪。
   * `aero/controls/Controls.hpp` 仅作为聚合兼容总入口保留，不应在仅使用一两个控件类的头文件或业务模块中无差别全量包含。
3. **规范头文件路径与物理模块化实现单元**：
   * **已移除废弃转发头**：`aero/compiler/AxbFormat.hpp` 与 `aero/script/ScriptUIBridge.hpp` 已彻底删除，禁止任何文件再引用。`.axb4` 二进制规范必须包含 Tier 1 头文件 **`#include "aero/asset/AxbFormat.hpp"`**（或其细分头 `GradientBrush.hpp`、`MtsdfPathBaker.hpp`、`RiveDecoder.hpp`、`ValueConverters.hpp`）；脚本 UI 桥接必须包含 Tier 4 头文件 **`#include "aero/bridge/ScriptUIBridge.hpp"`**。
   * **物理模块化边界**：
     * `src/asset/` 按职责拆分为 `AxbFormat.cpp`、`AxbTemplateRegistry.cpp`、`GradientBrush.cpp`、`MtsdfPathBaker.cpp`、`RiveDecoder.cpp`、`ValueConverters.cpp`、`XamlLiteralParser.cpp`；
     * `src/controls/` 按家族拆分为 `ControlContainers.cpp`、`ControlPanelsShapes.cpp`、`ControlButtonsRanges.cpp`、`ControlTextMedia.cpp`、`ControlItems.cpp`、`ControlRenderEmitters.cpp`、`AxbTemplateMounter.cpp`（共享 `ControlsInternal.hpp`）；
     * `src/compiler/` 按管线阶段拆分为 `AeroCompiler.cpp`、`FrontendImporters.cpp`、`AeroAotCppEmitter.cpp`、`CSharpCompatLowerer.cpp`、`XamlApplication.cpp`（共享 `AeroCompilerInternal.hpp`）。
   * **CMake 分层消费建议**：纯无头内核/工具消费 `AeroGUI::Kernel`；含软件光栅/OpenGL 后端消费 `AeroGUI::Render`；含桥接与跨平台窗口宿主消费 `AeroGUI::AppHost` 或 `AeroGUI::Runtime`；含 XAML/RealScript 编译前端消费 `AeroGUI::Compiler` 或 `AeroGUI::Core`。
