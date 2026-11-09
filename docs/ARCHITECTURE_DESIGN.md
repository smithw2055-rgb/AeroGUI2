# AeroGUI 2.0 + RealScript 下一代融合架构总体设计方案（集成 10 项源码级契约修正版）

**设计定位**：以 **RealScript（v0.98.0，`AeroGUI-Integration` 分支）** 扎实的编译器、MIR 优化器、字节码虚拟机、增量 GC、零拷贝 ECS 与 C++17 AOT（`rsaot`）为唯一脚本与反射底座（**仅做增量架构改造**）；同时**彻底抛弃旧版 AeroGUI 的历史包袱与冗余设计，从零全新设计极简高性能 C++17 UI 内核（AeroGUI 2.0）**，打造一套同时胜任**高性能游戏内嵌 UI（HUD / 3D 空间 UI）**、**通用跨平台桌面/移动应用**以及 **WebGL2 / WebGPU + WebAssembly 浏览器端应用**的现代类 C# + XAML 融合框架。

**关联云盘资源索引**：

* [AeroGUI 云盘项目根目录](https://drive.google.com/drive/folders/1pyAXOm4o8NrHcKiNWaP0brz87u1GsjE7)
* [AeroGUI/samples 24 个标准样例工程目录](https://drive.google.com/drive/folders/1juGtPNwOnNJkAEo4RddoLamzzc6YacbJ)（对照 [AeroGUI/Noesis_Samples 基准目录](https://drive.google.com/drive/folders/1EWu3sbWuOFE5ciKt9KTqeIqQx7OTrm1K)）
* [旧版 AeroGUI 代码架构深度审查报告（历史存档参考）](https://docs.google.com/document/d/1UeFe_QUCXnq5zdFHxngI_jB-HAGg8TcFja2XBuA3yO4/edit?usp=drivesdk)

---

# 零、 架构审查与 10 项源码级契约修正总览

在对照 RealScript（`third-party/RealScript`）与旧版 AeroGUI 真实源码完成深度可行性核验后，本方案已将以下 **10 项源码级契约修正（P0-1 ~ P0-5、P1-1 ~ P1-5）** 完整集成至各章节架构与 Phase 1 原型底座实现中：

| 修正编号 | 模块与源码位置 | 初始草案中的假设偏差 | 修正后落地方案（已在 Phase 1 实现） |
| :--- | :--- | :--- | :--- |
| **P0-1** | **跨堆互通 (`ExecutionDomainContext`)**<br>`Runtime.h` / `ManagedHeap.cpp` / `ExecutionDomain.h` | 1. 硬编码 `ref.heapId == 1U`（实际 `heapId` 为全局原子自增）；<br>2. 忽略运行期脚本 `string` 是绑定在源堆上的 `ObjectRef(ObjectKind::String)`；<br>3. 调用了尚未存在的 `fieldGetFast` 与 `stepIdleForDuration`。 | 1. 动态比对 `ref.heapId == simHeap_->heapId()` 与 `uiHeap_->heapId()`；<br>2. 跨堆读取 `ObjectKind::String` 时自动提取源堆 `stringView` 转为不可变池化 `StringValue`（并提供跨堆 `resolveStringView` 路由）；<br>3. 在 `ManagedHeap` 中实现零分配 `fieldGetFast`、`fieldSetFast` 与带时间预算的 `stepIdleForDuration`。 |
| **P0-2** | **统一值模型 (`UnifiedValue`)**<br>`Runtime.h` / `UnifiedValue.hpp` | 自定义 `struct UnifiedValue` 在 offset 16..23 放置 `typeSymbolId` + `kind`，与 `realscript::runtime::Value`（24B `std::variant`，offset 16 为 `index()`）二进制布局冲突。 | 直接定义 `using UnifiedValue = realscript::runtime::Value;`，并在 `realscript::runtime::Value` variant 中扩展 16B `Float4Value` 承载 UI 的 `Color` / `Rect` / `Thickness` / `CornerRadius`，严格保持 `sizeof(Value) == 24` 且 Trivially Copyable，跨界传递无需 `reinterpret_cast`。 |
| **P0-3** | **类型标识 (`SymbolId`)**<br>`Semantic.h` / `TypeClass.hpp` | 将 `TypeClass::rsSymbolId` 定义为 `uint32_t`，截断了 RealScript 的 64 位 FNV-1a `semantic::SymbolId`。 | `TypeClass::rsSymbolId` 统一使用 64 位 `realscript::semantic::SymbolId` (`uint64_t`)，与 `NativeHandle::typeId` 和 `ProgramImage` 完全对齐。 |
| **P0-4** | **代际弱引用与多播委托 (`WeakRef` / `WeakDelegate`)**<br>`ManagedHeap.cpp` / `Interpreter.cpp` | 将所有委托节点的 `field[0]` 从 GC `referenceFields` 移除，会导致 8 字段多播委托二叉树左子树（`field[3] == 4` 时 `field[0]` 为 `leftChain`）及 Lambda 闭包实例被 GC 误杀。 | 保持普通委托、多播组合节点（`field[3] == 4`）与闭包委托 `referenceFields = {0u, 7u}` 强引用不变；新增专用弱目标委托支持（`allocateWeakDelegate`，`referenceFields = {7u}`，`flags` 标记 `kDelegateWeakTargetFlag = 8`）与代际 `WeakRef`，在 `flattenDelegate` / `InvokeDelegate` 及 `pruneDelegateChain` 时自动跳过并摘除已销毁的宿主目标。 |
| **P0-5** | **小对象内联槽位访问 (`HeapObject`)**<br>`ManagedHeap.cpp` | 假设对象字段总在 `HeapObject::values` vector 中，忽略了字段数 `<= 4` 时存储在 `inlineValues`（此时 `values` 为空）。 | `ManagedHeap::fieldGetFast` 与 `fieldSetFast` 统一通过 `object->valueCount()` 与 `object->valueAt(fieldIndex)`（`usesInlineValues ? inlineValues[i] : values[i]`）读写。 |
| **P1-1** | **`[Observable]` 与 64 位脏字段位图 (`dirtyFieldMask`)**<br>`Semantic.h` / `Compilation.cpp` / `ManagedHeap.cpp` | 直接执行 `1ULL << fieldIndex`，当类含 `sequence` 合成字段或大量控件字段导致 `fieldIndex >= 64` 时触发 C++ 移位未定义行为（UB）。 | 在编译期为 `[Observable]` 属性/字段分配独立的 `observableBitIndex (0..62)`，超出 63 个可观察字段时统一归并至溢出位 `63`（保证移位量恒在 `0..63`）；在 `HeapObject::Header` 中内置 `uint64_t dirtyFieldMask`、值相等性短路与去重脏对象队列 `dirtyObjects`。 |
| **P1-2** | **`partial class` 跨语法树归并**<br>`Syntax.h` / `SyntaxFacts.cpp` / `Compilation.cpp` | 原编译器对同模块同名 `class` 直接报错 `RS4004`。 | 在词法/语法层支持 `partial` 修饰符（`SyntaxKind::PartialKeyword`），并在 `Compilation.cpp` 中按确定性文件顺序将同模块同名 `partial class` 的字段、属性、方法、事件、序列与接口列表归并。 |
| **P1-3** | **零装箱属性槽位访问 ABI (`PropertySlotHandle`)**<br>`ExecutionDomain.h` / `GameScripting.h` | 缺少绕过字符串查找的编译期/运行期槽位句柄 API。 | 暴露 `PropertySlotHandle`（含 `declaringTypeId`、`fieldIndex`、`dirtyBitIndex`、`primitiveType`、`getterSymbolId`、`setterSymbolId`、`isDirectBackingField`）及 O(1) 槽位直读直写 API。 |
| **P1-4** | **`CompactPropertyEntry` 对齐与视觉树 GC Root**<br>`UIElement.hpp` / `ExecutionDomain.cpp` | 1. `CompactPropertyEntry` 因 `UnifiedValue` 8B 对齐存在 4B 隐式 padding（实际 32B）；<br>2. 视觉树上挂载的派生控件 `cachedWrapper` 未纳入 `UiHeap` GC Root。 | 1. 显式利用 `CompactPropertyEntry` 的 4B 对齐空间存储 `uint16_t bindingSlotId` 与 `uint16_t animationTrackId`（固定 32B 无空洞）；<br>2. 在 `NativeHandleRegistry` 中提供 O(1) 借用槽位 `createBorrowed` / `resolveRawFast`，并在 `ExecutionDomainContext` GC 扫描中将活动视觉树节点的 `cachedWrapper` 作为 `UiHeap` 根保活。 |
| **P1-5** | **AOT 与 Wasm 友好性**<br>`AotRuntime.h` / `AotRuntime.cpp` | `AotRuntime` 含裸 `try/catch/throw`，在 `-fno-exceptions` 下无法编译；Web 端需控制字体光栅化体积。 | 使用 `#if defined(__cpp_exceptions) || defined(_CPPUNWIND)` 宏保护 `AotRuntime` 中的异常块，明确 Wasm 端轻量化字形管线策略。 |

---

# 一、 总体架构愿景与核心设计原则

## 1. 为什么采用“RealScript 增量演进 + AeroGUI 2.0 推倒重来”？

* **RealScript 代码基础扎实，天然具备做 UI 托管底座的基因**：现有 RealScript 已完整实现类 C# 语法、单继承（含脚本继承 C++ 类型）、接口、泛型、委托闭包、24B Trivial `Value`、16B 代际 `ObjectRef` 与 `NativeHandle`（`include/realscript/runtime/Runtime.h:44-114`）、`bindEcsRawColumnAuto` 零拷贝 ECS 列映射、带状态热重载（`include/realscript/hot_reload/HotReload.h`）、LSP/DAP 工具链以及 `rsaot --trusted-release` C++17 AOT 后端。只需针对 UI 场景补充双堆隔离、`partial class`、`[Observable]` 64 位脏位图织入、`PropertySlotHandle` 与弱引用委托支持，即可直接接管一切业务逻辑与反射职责。
* **旧版 AeroGUI 的 10.1 万行代码中有大量“在 C++17 中强行模拟 CLR”的冗余包袱**：旧版为了在纯 C++ 中支持 MVVM、动态绑定和反射，造出了 4 张分裂的元数据表、640 KB 静态表、32B 非平凡 Value、56B/304B 膨胀属性存储、四层 ViewFrame 包装、三遍 XAML 编译与四级渲染顶点拷贝。当引入 RealScript 后，**AeroGUI 2.0 可以砍掉约 75% 的冗余代码（从 10.1 万行精简至约 2.5 万行纯 C++17 核心）**，聚焦于**保留模式视觉树 + 紧凑依赖属性 + 位图深度桶布局器 + 双轨绑定器 + 零拷贝 GPU 渲染内核**。

## 2. 新旧架构全局对比矩阵

| 架构维度 | 旧版 AeroGUI 独立架构（废弃） | AeroGUI 2.0 + RealScript 融合新架构 | 量化收益与本质跨越 |
| :--- | :--- | :--- | :--- |
| **职责分工与代码规模** | C++ 既写底层渲染又写 ViewModel、Converter、反射宏（10.1 万行 C++） | **C++ 仅保留约 2.5 万行视觉/布局/渲染内核**；一切 ViewModel、命令、转换器、Code-Behind 100% 由 RealScript (`.rs`) 承载 | 内核代码量削减 **75%**，应用层样板代码削减 **80%** |
| **反射与类型系统** | 4 张 C++ 元数据表 + 640 KB `g_compactTypeIdTable` + `OrdinaryPropertyAdapter` 堆分配 | C++ 仅留**极简静态 `TypeClass`**（欧拉序区间 + 64 位 `rsSymbolId`）；全量反射统一归口至 RealScript `ProgramImage` | 消除 640 KB 静态表与约 3,500 行 C++ 反射表复制代码，转型耗时低于 1ns |
| **值模型（Value ABI）** | 32B 非平凡 `Aero::Base::Value`（大于 16B 即二次堆分配）与 24B `rs::Value` 割裂 | 全局唯一 **24B Trivial `UnifiedValue`**（即 `realscript::runtime::Value`，扩展 16B `Float4Value` 原位内联 Color/Rect/Thickness/CornerRadius） | 跨 C++/脚本边界传递**零拆装箱、零拷贝、零 `reinterpret_cast`、零原子引用计数增减** |
| **游戏脚本与 UI 脚本兼容** | 无脚本层，或面临确定性逻辑被 UI 帧率/GC 污染的风险 | **一语言双堆（`SimHeap` + `UiHeap` 动态 `heapId` 路由）** 共享只读 `ProgramImage`，Sim 到 UI 零拷贝只读槽穿透（含跨堆字符串自动池化转换），UI 到 Sim 走 `[SimCommand]` 队列 | 同一工程同一语法，**100% 保护帧同步确定性**，零 DTO 转换开销 |
| **属性存储与优先级** | 56B `PropertyStoreEntry` + 304B `StoredValueRare`，单控件分配即占 264B | **快慢分离存储**：8 个高频属性直接内联为 `UIElement` 结构体字段（低于 2ns 直写）；稀疏属性采用 **32B 紧凑槽位数组（含绑定与动画槽位索引）** | 单控件内存降低 **70%**，高频属性写入提速 **20x-40x** |
| **数据绑定引擎** | 运行期字符串路径分词 + 4 波次装箱刷新 + 单步监听限制 | **双轨绑定引擎**：轨道 A 为编译期多级整数槽位链（`CompiledSlotChain` + 64 位安全 `dirtyFieldMask`）；轨道 B 为 `bindEcsRawColumnAuto` SIMD 内存列直写渲染节点 | 支持任意深度路径自动通知；1,000 个游戏 HUD 血条每帧刷新低于 0.08ms、0 堆分配 |
| **编译、热重载与发布** | 三遍 XAML 编译 + 运行期重解析 `{Binding}`；改 C++ 逻辑需重编链接 | **`aero-compiler`（`xamlc` + `rsc` + `rsaot` 合一）**：开发期 `.axb4` + `.rsbc` 引用映射状态迁移热重载（低于 5ms）；发布期生成纯 C++17 代码经 LTO 单体编译 | 开发期毫秒级热重载，发布期**零 XML 解析器、零字节码解释器开销** |
| **跨平台与 WebGL2/Wasm** | 仅桌面端且 16ms 轮询耗电；四级顶点拷贝；OpenGL 3.3 不兼容 WebGL2 | **三栖宿主（`GameHost` / `DesktopHost` 0% 静止功耗 / `WebHost`）** + 零拷贝 GPU 缓冲流 + WebGL2/WebGPU 后端（**650-850 KB Wasm**） | 静止功耗降至 **0%**，Web 端包体积仅为 Avalonia Wasm 的 **1/15** |

---

# 二、 RealScript 架构增量改造设计（在扎实底座上的 5 项演进）

RealScript 现有的前端（`src/syntax/`、`src/semantic/`）、中端（`src/mir/`、`src/optimization/`）与后端（`src/bytecode/`、`src/aot_cpp/`、`src/runtime/`）架构非常清晰，无需推翻重来，仅需在以下 5 个切入点做增量扩展：

## 1. 一语言双堆（`SimDomain` + `UiDomain`）与跨堆零拷贝安全互通（含 P0-1 & P0-5 修正）

为了在一套游戏中同时支撑**确定性游戏脚本（Gameplay）和游戏/通用 UI 脚本（Presentation）**，直接利用 RealScript 现有的只读 `ProgramImage`（`include/realscript/game/GameScripting.h`）和携带 32 位 `heapId` 的 `ObjectRef`（`include/realscript/runtime/Runtime.h:44-76`）：

* **编译期同构（单一 `ProgramImage`）**：游戏逻辑模块（`module Game.Sim;`）与 UI 模块（`module Game.UI;`）在同一个编译会话中编译为同一份只读 `ProgramImage`，共享全部 64 位 `semantic::SymbolId`、结构体布局与字段槽位索引（`fieldIndex`）。UI 脚本可直接 `import Game.Sim;`。
* **运行期双堆隔离（`SimHeap` vs `UiHeap`）**：
  * **`SimDomain`（绑定 `SimHeap`，动态 `simHeap->heapId()`）**：开启 `DeterminismMode::Strict/Record/Replay`，固定 Tick 驱动，使用确定性 `sequence` 帧协程，严禁调用系统时钟或非确定性 UI API。
  * **`UiDomain`（绑定 `UiHeap`，动态 `uiHeap->heapId()`）**：开启 `DeterminismMode::Off`，跟随渲染帧率或 OS 事件唤醒，在每帧渲染提交后的 Idle 窗口执行有界增量 GC（`ManagedHeap::stepIdleForDuration(shadowStack, maxMs)`）。
* **跨堆交互的“只读槽穿透 + 跨堆字符串池化 + 命令队列回写”规则**：
  1. **UI 直读 Sim 对象（O(1) 零拷贝，兼容 `inlineValues`）**：当运行在 `UiDomain` 的代码或 AeroGUI 2.0 绑定器读取一个 `ObjectRef` 的字段时，`ExecutionDomainContext::readObjectFieldFast` 动态比对 `ref.heapId == simHeap_->heapId()` 或 `uiHeap_->heapId()`，直接调用 `targetHeap->fieldGetFast(ref, fieldIndex, outVal)`（内部通过 `object->valueAt(fieldIndex)` 兼容 `<= 4` 字段 `inlineValues` 与 `> 4` 字段 `values`）。若读取出的字段是源堆上的 `ObjectRef(ObjectKind::String)`，自动提取 `targetHeap->stringView(strRef)` 并转换为不可变池化 `StringValue`，彻底消除跨堆字符串 `heapId` 校验失败问题。
  2. **编译期与运行期写保护（`[SimState]`）**：在 `UiDomain` 中对任何属于 `SimHeap` 的对象调用 `writeObjectFieldFast` 将在运行期直接拦截拒绝（返回 `false` 并记录违规计数），同时禁止将 `UiHeap` 的 `ObjectRef` 写入 `SimHeap`，确保 UI 绝不污染逻辑帧确定性状态。
  3. **UI 触发游戏动作（`[SimCommand]` 自动封送）**：当 UI 脚本调用标记了 `[SimCommand]` 的游戏逻辑方法时，通过 `ExecutionDomainContext::enqueueSimCommand` 向命令队列投递紧凑命令包 `SimCommandEnvelope`（含目标 `ObjectRef`、方法 64 位 `SymbolId` 与内联参数槽），在下一个确定性 `SimTick` 开头统一消费并写入 Replay 流。
  4. **单向弱根 GC 安全性**：`SimHeap` 严禁持有 `UiHeap` 的引用；`UiHeap` 持有的 `SimHeap` 引用不参与 `SimHeap` 的 GC 根扫描，而是依靠 `ObjectRef::generation` 代数在访问时做 O(1) 存活校验（`simHeap->isAlive(ref)`），当游戏实体销毁时 UI 自动切回默认值。

```cpp
// RealScript 增量改造 1：include/realscript/runtime/ExecutionDomain.h
namespace realscript::runtime {

enum class DomainKind : std::uint8_t {
    DeterministicSim = 1, // 严格确定性、固定 Tick、sequence 状态机
    UiPresentation   = 2, // 响应式 UI、帧末 Idle GC、事件驱动
};

class ExecutionDomainContext {
public:
    ExecutionDomainContext(
        std::shared_ptr<const ProgramImage> program,
        std::shared_ptr<ManagedHeap> simHeap,
        std::shared_ptr<ManagedHeap> uiHeap,
        std::shared_ptr<NativeHandleRegistry> nativeHandles = nullptr);

    // 跨堆安全只读槽位访问：动态匹配 heapId，兼容小对象 inlineValues，自动转换跨堆 String
    bool readObjectFieldFast(
        ObjectRef ref,
        std::uint16_t fieldIndex,
        Value& outVal) const noexcept;

    // 写入严格校验：禁止从 UiPresentation 修改 SimHeap 对象，禁止将 UiHeap 引用写入 SimHeap
    bool writeObjectFieldFast(
        DomainKind callerDomain,
        ObjectRef ref,
        std::uint16_t fieldIndex,
        const Value& val) noexcept;
};

} // namespace realscript::runtime
```

## 2. `partial class` 跨语法树归并与 `[Observable]` 编译器内建织入（含 P1-1 & P1-2 修正）

* **支持 `partial class`（改造 `include/realscript/syntax/Syntax.h`、`src/syntax/SyntaxFacts.cpp` 与 `src/compiler/Compilation.cpp`）**：
  * 在词法/语法分析器中增加 `SyntaxKind::PartialKeyword` 并纳入声明修饰符集合；
  * 在 `Compilation.cpp` 构建模块类型前，允许同一模块内的 `MyWindow.rs`（手写业务逻辑）与 `MyWindow.g.rs`（由 XAML 编译器自动生成的命名控件字段、属性槽位表与事件绑定代码）在均声明了 `partial` 时，按确定性文件名顺序归并字段、属性、方法、序列、构造函数、事件与接口列表；若同名类未全部标记 `partial`，则保持 `RS4004` 重复类型报错。
* **`[Observable]` 编译器织入与 64 位安全对象脏位图（改造 `include/realscript/semantic/Semantic.h`、`src/compiler/Compilation.cpp` 与 `src/runtime/ManagedHeap.cpp`）**：
  * **独立 `observableBitIndex (0..63)` 分配（消除 `fieldIndex >= 64` 移位 UB）**：在 `FieldSymbol` 与 `PropertySymbol` 中引入 `bool observable` 与 `uint16_t observableBitIndex`。编译期为标记了 `[Observable]` 的属性（或标记了 `[Observable]` 的类的公开属性/字段）按顺序分配独立的 `0..62` 位索引；若单个类的可观察属性超过 63 个，后续属性统一映射到溢出位 `63`（`kDirtyOverflowBit = 63`），保证 `1ULL << bitIndex` 永远不会发生 C++ 未定义移位行为。
  * **值相等性短路与去重脏队列**：在每个 `ManagedHeap` 的 `HeapObject::Header` 内置 `uint64_t dirtyFieldMask = 0`、`uint16_t uiObserverCount = 0` 与 `bool inDirtyQueue = false`。当字段值写入时，若新旧 `Value` 相等则直接短路返回；若不等，则置位 `dirtyFieldMask |= (1ULL << safeBit)`，且在同一帧内无论修改多少次，仅在首次变脏时将 `ObjectRef` 压入 `dirtyObjects` 队列一次。

## 3. 零装箱属性槽位访问 ABI（`PropertySlotHandle`，P1-3）

在 `include/realscript/runtime/ExecutionDomain.h` 与 `include/realscript/game/GameScripting.h` 中增设绕过字符串查找的编译期/运行期槽位句柄：

```cpp
struct PropertySlotHandle {
    semantic::SymbolId declaringTypeId = 0;
    std::uint16_t fieldIndex = 0xFFFFu;       // 直接对应 HeapObject::valueAt(fieldIndex)
    std::uint16_t dirtyBitIndex = 0xFFFFu;    // 对应 dirtyFieldMask 的安全位偏移 (0..63)
    semantic::PrimitiveType primitiveType = semantic::PrimitiveType::Error;
    semantic::SymbolId getterSymbolId = 0;    // 若为自定义计算属性，指向编译期函数符号
    semantic::SymbolId setterSymbolId = 0;
    bool isDirectBackingField = true;         // true 时直接读写 HeapObject 槽位，不进 VM 栈帧
    bool isReadonly = false;
};
```

## 4. 代际弱引用 `WeakRef` 与多播委托弱目标自动断路（含 P0-4 修正）

* **零表代际 `WeakRef`**：在 `Runtime.h` 中提供 16B `WeakRef`（包装 `ObjectRef` 但不参与 GC 强引用扫描）。读取时调用 `weakRef.lock(heap)` 比较 `slots[ref.slot].generation == ref.generation`，实现 O(1) 零哈希表代际存活验证。
* **多播委托二叉树保护与弱目标叶子自动摘除（改造 `ManagedHeap.cpp` 与 `Interpreter.cpp`）**：
  * 普通委托、Lambda 闭包委托与多播委托组合节点（`field[3] == 4`，其中 `field[0]` 为左子树 `leftChain`，`field[7]` 为右子树 `rightChain`）保持 `referenceFields = {0u, 7u}` 强引用不变，确保多播树结构与匿名闭包绝不被 GC 误回收；
  * 新增 `ManagedHeap::allocateWeakDelegate`（仅将 `referenceFields` 设为 `{7u}`，并在 `field[3]` 置位 `kDelegateWeakTargetFlag = 8`）。当遍历或调用委托（`flattenDelegate` / `InvokeDelegate` / `pruneDelegateChain`）时，若发现弱目标叶子节点的 `field[0]`（`target`）已销毁（`!heap->isAlive(target)`），自动跳过并从多播链中摘除该失效叶子。

## 5. `UiDomain` 可重入异步任务与 Wasm 无异常友好运行时（含 P1-5 修正）

* **双轨协程规划**：`SimDomain` 继续使用合成在组件字段上的零分配确定性 `sequence`；`UiDomain` 规划支持堆分配可重入状态机与取消令牌绑定。
* **AOT 无异常宏保护**：在 `AotRuntime.h` 与 `AotRuntime.cpp` 中使用 `#if defined(__cpp_exceptions) || defined(_CPPUNWIND)` 保护异常块，确保在 Emscripten `-fno-exceptions -fno-rtti` 下顺利编译。

---

# 三、 AeroGUI 2.0 全新内核架构设计（抛弃历史包袱，精简至约 2.5 万行 C++17）

## 1. AeroGUI 2.0 极简五层模块划分

1. **`aero/core`（对象、极简类型与快慢分离属性系统）**：24B `UnifiedValue`（直接复用 `realscript::runtime::Value`）、64 位 `rsSymbolId` 的静态 `TypeClass`、`DependencyProperty`、`UIElement`（8 大高频属性内联快槽 + 32B 紧凑稀疏表 + 纯侵入式视觉树 + `NativeHandleRegistry` 零哈希互转）。
2. **`aero/layout`（位图深度桶布局与命中测试引擎）**：64 层侵入式 `DepthBucketLayoutQueue`、核心面板与统一 2D/3D 逆矩阵命中测试。
3. **`aero/binding`（双轨数据绑定、样式触发器与 XAML VM）**：单通二进制 `.axb4` 加载器、`ReactiveSlotBinder`（对接 `ExecutionDomainContext` 与 `PropertySlotHandle`）与 `EcsColumnSimdBinder`（对接 `NativeRawColumnView`）。
4. **`aero/controls`（完全模板驱动的精简标准控件库）**：所有控件统一走 `ControlTemplate` 与标准容器生成契约。
5. **`aero/render` 与 `aero/host`（零拷贝 GPU 渲染器与三栖宿主）**：分页式 `PagedRenderTree`、`ZeroCopyGpuStream`、24B 顶点 Uber-Shader 后端及 `GameHost` / `DesktopHost` / `WebHost`。

## 2. 核心基础层：24B `UnifiedValue`、与 NoesisGUI 对齐的全链路 `float` 数值规范与极简 `TypeClass`（含 P0-2 & P0-3 修正）

AeroGUI 2.0 直接复用 `realscript::runtime::Value` 作为 `aero::UnifiedValue`，并在 `realscript::runtime::Value` 中内置 16B `Float4Value`（承载 `Color`、`Rect`、`Thickness`、`CornerRadius`）与 4B `FloatValue`。

* **与 NoesisGUI 对齐的全链路 `float` 数值规范（禁用 `double`）**：
  * AeroGUI 2.0 的所有 UI 数值类型与 NoesisGUI 严格对齐，统一采用 32 位单精度 `float`（**禁止在 UI 层使用 `double`**），从而与底层 GPU 顶点缓冲和着色器 Uniform 直接对齐，彻底消除 CPU 到 GPU 的 `double <-> float` 转换开销。
  * 在 `UnifiedValue.hpp` 中提供 `MakeFloat(float v)`（底层直接构造 `FloatValue{v}`）与 `TryGetFloat(const UnifiedValue& v, float& out)` 作为 UI 唯一标准浮点接口；所有 `UIElement` 内联快槽/稀疏槽、几何与布局结构（`Size`、`Point`、`Rect`、`Thickness`、`CornerRadius`）、`.axb4` 二进制指令流浮点字面量以及 RealScript 侧 UI 脚本/`*.g.rs` 生成代码中的 UI 属性类型统一采用 `float`。

```cpp
// include/aero/core/UnifiedValue.hpp — 直接复用 realscript::runtime::Value，100% 零转换同构
#pragma once
#include "realscript/runtime/Runtime.h"

namespace aero {

using UnifiedValue = realscript::runtime::Value;
using Float4Value  = realscript::runtime::Float4Value;

static_assert(sizeof(UnifiedValue) == 24, "UnifiedValue must be exactly 24 bytes");
static_assert(std::is_trivially_copyable_v<UnifiedValue>, "Zero copy/dtor overhead");

// 极简静态 TypeClass：采用 64 位 realscript::semantic::SymbolId 与 O(1) 欧拉序区间转型
struct TypeClass final {
    using FactoryFn = class UIElement* (*)() noexcept;

    std::string_view name{};
    const TypeClass* base = nullptr;
    FactoryFn factory = nullptr;
    std::uint16_t treeIn = 0U;
    std::uint16_t treeOut = 0U;
    realscript::semantic::SymbolId rsSymbolId = 0ULL; // 64-bit FNV-1a SymbolId (P0-3)

    [[nodiscard]] constexpr bool IsDerivedFrom(const TypeClass* ancestor) const noexcept {
        return ancestor != nullptr &&
               (this == ancestor || (treeIn >= ancestor->treeIn && treeIn <= ancestor->treeOut));
    }
};

} // namespace aero
```

## 3. 快慢分离的依赖属性与纯侵入式 `UIElement`（含 P1-4 修正）

AeroGUI 2.0 将属性分为**热路径内联快槽（`FastInlineStorage`）与紧凑稀疏扩展槽（`CompactPropertyEntry`）**，并通过 `NativeHandleRegistry::createBorrowed` / `resolveRawFast` 实现 C++ `UIElement*` 与脚本 `NativeHandle` 的 O(1) 零哈希、零堆分配互转：

```cpp
// include/aero/core/UIElement.hpp
namespace aero {

enum FastPropertySlot : std::uint16_t {
    kFastSlotWidth           = 1U << 0,
    kFastSlotHeight          = 1U << 1,
    kFastSlotMargin          = 1U << 2,
    kFastSlotOpacity         = 1U << 3,
    kFastSlotVisibility      = 1U << 4,
    kFastSlotRenderTransform = 1U << 5,
    kFastSlotClipRect        = 1U << 6,
    kFastSlotForegroundRGBA  = 1U << 7,
};

// 稀疏属性条目：32B 紧凑对齐（显式利用 4B 对齐空间存放 bindingSlotId 与 animationTrackId，P1-4）
struct alignas(8) CompactPropertyEntry {
    std::uint16_t dpIndex = 0xFFFFU;
    std::uint8_t  activeRank = 0U;
    std::uint8_t  flags = 0U;
    std::uint16_t bindingSlotId = 0xFFFFU;
    std::uint16_t animationTrackId = 0xFFFFU;
    UnifiedValue  effectiveValue{}; // 24B Trivial Value
};
static_assert(sizeof(CompactPropertyEntry) == 32, "CompactPropertyEntry must be 32 bytes");

} // namespace aero
```

---

# 四、 双轨数据绑定、统一编译与三栖宿主路线图

| 实施阶段 | RealScript 侧改造任务（`third-party/RealScript`） | AeroGUI 2.0 侧设计任务（`c:\Projects\AeroGUI2`） | 里程碑验收标准 |
| :--- | :--- | :--- | :--- |
| **Phase 1：统一 ABI 与双堆底座（当前阶段）** | 1. 落地 `ExecutionDomainContext`（`SimHeap` + `UiHeap` 动态 `heapId`、跨堆 `String` 池化转换与跨堆只读保护）；<br>2. 实现 `partial class` 语法归并与 `[Observable]` 64 位安全 `dirtyFieldMask` 编译期织入；<br>3. 暴露 `PropertySlotHandle` O(1) 槽位读写 API、代际 `WeakRef` 与弱目标委托自动摘除。 | 1. 落地 24B `UnifiedValue`（含 `Float4Value`）、64 位 `rsSymbolId` 的 `TypeClass` 与纯侵入式 `UIElement`；<br>2. 实现 8 大高频属性内联快槽（`FastInlineStorage`）与 32B 紧凑稀疏表（`CompactPropertyEntry`）；<br>3. 打通 `UIElement::nativeSlot_` 与 `NativeHandleRegistry` 零哈希零分配互转及视觉树 `cachedWrapper` GC 保活。 | C++ `UIElement` 与 RealScript 对象跨界读写 0 堆分配；跨堆只读保护、代数失效、`partial class`、`[Observable]` 脏位图与弱委托单测 100% 通过。 |
| **Phase 2：双轨绑定、统一编译器与控件自挂载** | 1. 完善 `[SimState]` 与 `[SimCommand]` 编译器自动封送代码生成；<br>2. 落地 `UiDomain` 堆分配可重入 `async Task`。 | 1. 开发单通 `aero-compiler`（`.xaml + .rs -> .axb4 + .g.rs`）；<br>2. 落地 `DualTrackBindingEngine` 与 `DepthBucketLayoutQueue`。 | 迁移核心 Samples，样板代码减少 **80%**，热重载延迟低于 **5ms**。 |
| **Phase 3：零拷贝渲染、三栖宿主与 WebGL2/Wasm** | 1. 扩展 `rsaot` 栈上局部帧优化；<br>2. 拆分 `realscript_aot_runtime` 纯运行时 CMake 目标。 | 1. 落地 4KB 分页式 `PagedRenderTree` 与 `ZeroCopyGpuStream`；<br>2. 实现 `EmbeddedGameHost`、`DesktopEventHost` 与 `WebCanvasHost`。 | 1,000 个 ECS HUD 节点每帧刷新低于 **0.08ms**；桌面静止 CPU 占用 **0.0%**；产出轻量级 Wasm 演示应用。 |
