# AeroGUI 2.0 + RealScript C++17 编码规范（Coding Style Guide）

为保持 AeroGUI 2.0 内核（约 2.5 万行纯 C++17）的极简性、高可读性与跨界零摩擦协作，消除代码中冗长的 `realscript::runtime::`、`realscript::semantic::`、`realscript::game::` 等跨模块限定前缀，特制定本编码规范。所有 `include/aero/**`、`src/**`、`tools/**` 与 `tests/**` 代码必须严格遵循。

---

## 一、 RealScript 跨界命名空间别名与核心类型直导规范

### 1. 统一头文件入口：`aero/core/RsAliases.hpp`

AeroGUI 2.0 在 `include/aero/core/RsAliases.hpp`（并由 `aero/core/UnifiedValue.hpp` 默认包含）中统一定义了 `namespace aero` 内部的短命名空间别名与高频核心类型别名。

**禁止**在 AeroGUI 2.0 的头文件或实现文件中散落手写冗长的 `realscript::runtime::xxx` 或 `realscript::semantic::xxx`。

### 2. 标准短命名空间别名（在 `namespace aero` 内直接可用）

| 短命名空间别名 | 对应完整命名空间 | 典型使用场景 |
| :--- | :--- | :--- |
| `rs` | `::realscript` | 顶层跨模块访问 |
| `rs_rt` | `::realscript::runtime` | 运行时函数与辅助工具（如 `rs_rt::valueType`、`rs_rt::valueToString`） |
| `rs_sem` | `::realscript::semantic` | 语义层符号与工具函数（如 `rs_sem::stableTypeId`、`rs_sem::TypeSymbol`） |
| `rs_bc` | `::realscript::bytecode` | 字节码指令、模块编解码与 Lowerer（如 `rs_bc::FunctionReference`、`rs_bc::Module`） |
| `rs_comp` | `::realscript::compiler` | 编译器前端会话与源文件（如 `rs_comp::Compilation`、`rs_comp::SourceFile`） |
| `rs_diag` | `::realscript::diagnostics` | 编译与校验诊断容器（如 `rs_diag::DiagnosticBag`） |
| `rs_game` | `::realscript::game` | 游戏宿主运行时与场景桥接（如 `rs_game::ScriptRuntime`、`rs_game::GameProgram`） |
| `rs_hr` | `::realscript::hot_reload` | 状态迁移与热重载会话（如 `rs_hr::HotReloadSession`） |

### 3. 核心跨界类型直导（在 `namespace aero` 内零前缀直接使用）

以下高频类型已在 `namespace aero` 中通过 `using` 直接导出，在 `namespace aero` 内部编写代码时**直接使用裸类型名**，无需任何命名空间前缀：

```cpp
namespace aero {

// 命名空间短别名
namespace rs      = ::realscript;
namespace rs_rt   = ::realscript::runtime;
namespace rs_sem  = ::realscript::semantic;
namespace rs_bc   = ::realscript::bytecode;
namespace rs_diag = ::realscript::diagnostics;
namespace rs_game = ::realscript::game;
namespace rs_comp = ::realscript::compiler;
namespace rs_hr   = ::realscript::hot_reload;

// 语义与符号标识
using SymbolId               = rs_sem::SymbolId;
using PrimitiveType          = rs_sem::PrimitiveType;

// 统一值模型与标量/复合包装值
using UnifiedValue           = rs_rt::Value;
using Float4Value            = rs_rt::Float4Value;
using FloatValue             = rs_rt::FloatValue;
using StringValue            = rs_rt::StringValue;
using StructValue            = rs_rt::StructValue;
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

// 虚拟机镜像、解释器與原生绑定
using ProgramImage           = rs_rt::ProgramImage;
using Interpreter            = rs_rt::Interpreter;
using BindingRegistry        = rs_rt::BindingRegistry;
using ValueSpan              = rs_rt::ValueSpan;
using RuntimeError           = rs_rt::RuntimeError;
using ErrorCode              = rs_rt::ErrorCode;

// 双执行域（SimDomain + UiDomain）与零装箱属性槽位
using DomainKind             = rs_rt::DomainKind;
using ExecutionDomainContext = rs_rt::ExecutionDomainContext;
using SimCommandEnvelope     = rs_rt::SimCommandEnvelope;
using PropertySlotHandle     = rs_rt::PropertySlotHandle;

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
   * **AeroGUI 公开方法与函数**：使用 `PascalCase`（如 `AddChild`、`FlushReactiveBindings`、`MeasureCore`）。
   * **局部变量与函数参数**：使用 `camelCase`（如 `targetElement`、`sourceSlot`、`dirtyMask`）。
   * **私有成员变量**：使用 `camelCase_`（尾部下划线，如 `parent_`、`fastProps_`、`domains_`）。
   * **结构体公开数据成员**：使用 `camelCase`（如 `propId`、`bindingSlotId`、`effectiveValue`）。
   * **编译期常量与枚举值**：使用 `PascalCase` 或 `kPascalCase`（如 `PropId_Width`、`ValueSourceRank::Local`）。
2. **内存与所有权约定**：
   * C++ 视觉树节点（`UIElement` 及其派生类）统一使用侵入式引用计数 `IntrusivePtr<T>` 与 `MakeIntrusive<T>()`，严禁对 `UIElement` 使用 `std::shared_ptr`（避免控制块二次堆分配）。
   * 热路径（属性读写、布局脏队列、响应式槽位同步、ECS 列扫描）必须保持 **0 堆分配（Zero Heap Allocation）**。
3. **异常与 RTTI 约束**：
   * AeroGUI 2.0 内核代码禁用 C++ `dynamic_cast` 与异常抛接，统一使用基于欧拉序区间的 `UIElement::DynamicCast<T>()`（O(1) 无分支比较）与显式返回值 / `RuntimeError*` 错误传递，确保 WebAssembly（`-fno-exceptions -fno-rtti`）100% 兼容。
4. **与 NoesisGUI 对齐的全链路 `float` 数值规范（禁用 `double`）**：
   * **全链路 32 位 `float`**：AeroGUI 2.0 的所有 UI 数值类型与 NoesisGUI 严格对齐，统一采用 32 位单精度 `float`（**禁止在 UI 层使用 `double`**），从而与底层 GPU 顶点缓冲区（Vertex Buffer）和着色器 Uniform 直接对齐，彻底消除 CPU 到 GPU 的 `double <-> float` 转换开销。
   * **`UnifiedValue` 标准浮点接口**：在 C++ 侧读写浮点属性或跨界传参时，唯一标准接口为 `MakeFloat(float v)`（底层直接构造 `FloatValue{v}`）与 `TryGetFloat(const UnifiedValue& v, float& out)`，严禁在 UI 属性槽位中存储 `double`。
   * **几何与编译器对齐**：所有几何结构（`Size`、`Point`、`Rect`、`Thickness`、`CornerRadius`、`Color`）、布局管线（`Measure(Size)` / `Arrange(Rect)`）、控件属性（`Width`、`Height`、`MinWidth`、`MinHeight`、`Opacity`、`FontSize`、`Spacing`）、`.axb4` 二进制指令流浮点字面量以及 RealScript 侧 UI 脚本与 `*.g.rs` 生成代码中的 UI 属性类型，全部统一使用 `float`。
