# AeroGUI 2.0 + RealScript 融合架构总体设计规范（ABI v2.0 & P0/P1/P2 全量实装版）

**架构版本**：`ABI v2.0`（`RealScript RuntimeAbiMajor = 2`，`.axb4` 格式版本 `0x00020000`）
**设计定位**：以 **RealScript（v0.98.0，`AeroGUI-Integration` 分支）** 编译器、MIR 优化器、字节码虚拟机、增量 GC、零拷贝 ECS 与 C++17 AOT（`rsaot`）为唯一托管与反射底座；从零构建**零第三方依赖、纯 32 位 `float` 管线、无 C++ RTTI/异常运行时依赖**的现代 C++17 UI 内核 **AeroGUI 2.0**，一套架构同时支撑**高性能游戏内嵌 HUD / 3D 空间 UI**、**0% 静止 CPU 占用原生桌面应用（Linux X11 / Win32）**以及 **WebGL2 / WebGPU + WebAssembly 浏览器端应用**。

**关联参考索引**：

* [AeroGUI 云盘项目根目录](https://drive.google.com/drive/folders/1pyAXOm4o8NrHcKiNWaP0brz87u1GsjE7)
* [AeroGUI/samples 官方样例工程参考集](https://drive.google.com/drive/folders/1juGtPNwOnNJkAEo4RddoLamzzc6YacbJ)（对照 [AeroGUI/Noesis_Samples 基准目录](https://drive.google.com/drive/folders/1EWu3sbWuOFE5ciKt9KTqeIqQx7OTrm1K)）
* [旧版 AeroGUI 代码架构深度审查报告（历史存档参考）](https://docs.google.com/document/d/1UeFe_QUCXnq5zdFHxngI_jB-HAGg8TcFja2XBuA3yO4/edit?usp=drivesdk)
* [Samples.zip 全量 21 个官方示例黄金像素与解析基准报告](SAMPLES_BENCHMARK_REPORT.md)

---

# 一、 核心二进制契约与尺寸不变量总表（100% 源码 `static_assert` 锁死）

AeroGUI 2.0 与 RealScript 在编译期通过 `static_assert` 锁死全部跨界数据结构的字节尺寸与对齐边界，确保 C++17 内核、RealScript 字节码 VM、`rsaot` 生成代码以及 GPU 环形顶点流之间**零拆装箱、零拷贝、零隐式填充空洞**：

| 核心数据结构 | 源码头文件位置 | 精确尺寸 (`sizeof`) | 对齐 (`alignof`) | 核心布局与设计职责 |
| :--- | :--- | ---: | ---: | :--- |
| **`UnifiedValue`** (`rs::runtime::Value`) | `realscript/runtime/Runtime.h`<br>`aero/core/UnifiedValue.hpp` | **16 Bytes** | **16 Bytes** | 全局唯一 ABI v2.0 值单元；字节 `0..14` 为联合体载荷，字节 `15` 为统一 `ValueTag` (`0..29`)，单个 64B 缓存行紧密容纳 4 个值。 |
| **`Float4Value`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **4 Bytes** | 4 通道 32 位 `float {x, y, z, w}`，统一承载 UI `Color`、`Rect`、`Point`、`Thickness`、`CornerRadius` 与变换向量。 |
| **`ObjectRef` / `StringValue` / `StructValue`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **4 / 8 Bytes** | 均严格控制为 16B，且在 `offsetof == 15` 处内嵌 `vtag_`，与 `Value::u_` 原位重合，无需外包 `std::variant` 判别器。 |
| **`NativeHandle`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **8 Bytes** | 代际原生句柄（`slot:32`、`generation:16`、`registryId:16`、`typeId:64`）；存入 `Value` 时经 `typeIdx` 池化压缩至 `NativeHandleCell`（16B）。 |
| **`FastInlineStorage`** | `aero/core/DependencyProperty.hpp` | **56 Bytes** | **4 Bytes** | `UIElement` 内联高频属性块（`Width/Height/MinWidth/MinHeight/Margin/Padding/Opacity/Visibility` + 对齐位 + 交互焦点状态位 + 布局脏标记）。 |
| **`CompactPropertyEntry`** | `aero/core/DependencyProperty.hpp` | **32 Bytes** | **16 Bytes** | 零空洞稀疏依赖属性槽：字节 `0..7` 存 `propId/sourceRank/flags/bindingSlotId/animationTrackId`；字节 `8..15` 存双轨混合与契约输入槽；字节 `16..31` 存 16B `UnifiedValue`（每 64B 缓存行恰好 2 个条目）。 |
| **`AxbFlatKeyframe`** | `aero/compiler/AxbFormat.hpp` | **28 Bytes** | **4 Bytes** | `MOT6` 扁平连续关键帧记录（`timeSec` + `Float4Value` + 插值模式 + Q7.8 定点三次贝塞尔超调切线控制点）。 |
| **`AeroVertex`** | `aero/render/PagedRenderTree.hpp` | **24 Bytes** | **4 Bytes** | 纯 `float` GPU UberShader 顶点（`x, y, u, v` + `uint32_t rgba` + `uint16_t primType` + `uint16_t flags`）。 |
| **`CommandPageHeader`** | `aero/render/PagedRenderTree.hpp` | **64 Bytes** | **8 Bytes** | 4KB 渲染页头（含 `pageIndex`、`vertexCount`、`dirtyRevision`、64 位 `dirtyQuadMask`、`clipRect` 与 `reserved[0]` 独立 `textureId` 槽位）。 |
| **`CommandPage`** | `aero/render/PagedRenderTree.hpp` | **4096 Bytes** | **64 Bytes** | 4KB 分页 GPU 命令与顶点块：`64B Header + 168 * 24B AeroVertex`（精确容纳 **42 个独立 Quad**，零字节浪费）。 |

---

# 二、 16B 统一值模型（ABI v2.0）与双堆托管底座

## 1. 16 字节 `UnifiedValue`（`rs::runtime::Value`）内存布局

旧版方案采用 24 字节 `std::variant` 导致缓存行利用率偏低且存在对齐空洞。在 **ABI v2.0（`RuntimeAbiMajor = 2`）** 中，`realscript::runtime::Value` 与 `aero::UnifiedValue` 彻底升级为 **16 字节、`alignas(16)` 平凡可拷贝联合体**，将类型判别标签统一固定在**第 15 字节（`offset 15`）**：

```text
+-----------------------------------------------------------------------+---------------+
| Bytes 0 .. 14 (15 Bytes Inline Payload / Arena Handle / Object Slot)  | Byte 15 (1B)  |
|  - ValueTaggedCell<T> (sizeof(T) <= 12): bool, i64, f32, f64, Vec3... |   ValueTag    |
|  - Float4InlineCell: float x (0..3), float y (4..7), float z (8..11), |   (0 .. 29)   |
|                      uint8 w0, w1, w2 (top 24 bits of IEEE-754 float) |   Tag = 22    |
|  - NativeHandleCell: slot(u32), gen(u16), regId(u16), typeIdx(u32)    |   Tag = 23    |
|  - ArenaRefCell:     arenaIdx(u32) -> EnumArena/QuatArena/Float4Arena |  Tag = 24..26 |
|  - StructValue:      typeId(u64), storageHandle(u32), inlineMeta(3B)  |   Tag = 27    |
|  - StringValue:      const std::string* ptr(8B), internId(u32), pad   |   Tag = 28    |
|  - ObjectRef:        slot(u32), generation(u32), heapId(u32), kind    |   Tag = 29    |
+-----------------------------------------------------------------------+---------------+
```

* **非追踪内联值标签（`ValueTag` `0..23`，`< ValueTag::FirstTraceable`）**：
  * `Monostate(0)`、`NullString(1)`、`NullObject(2)`、`NullArray(3)`、`Bool(4)`、`Byte(5)`、`SByte(6)`、`Short(7)`、`UShort(8)`、`Int64(9)`、`UInt(10)`、`Long(11)`、`ULong(12)`、`Float(13)`、`Double(14)`、`Char(15)`、`EnumInline(16)`（64 位 `SymbolId` + 32 位枚举值原位内联）、`Fixed(17)`、`Fixed16(18)`、`Vec2(19)`、`Vec3(20)`、`QuatInline(21)`、`Float4Inline(22)`、`NativeHandle(23)`。
* **可追踪/池驻留标签（`ValueTag` `24..29`，`>= ValueTag::FirstTraceable`）**：
  * `EnumArena(24)`（超 32 位枚举值）、`QuatArena(25)`、`Float4Arena(26)`、`Struct(27)`、`String(28)`、`ObjectRef(29)`。`Value::isTraceable()` 仅需单条无分支比较 `tag >= FirstTraceable`。

## 2. `Float4Inline` 高频动画零分配快径与 `MakeFloat4InlineFast`

UI 引擎中 `Color(r,g,b,a)`、`Thickness(l,t,r,b)`、`CornerRadius(tl,tr,br,bl)` 与 `Rect(x,y,w,h)` 均为 16 字节 `Float4Value`。为了在 16B `Value` 中同时装下 4 个 `float` 和 1 字节 `ValueTag`：

1. **无损自动内联**：当第 4 分量 `w` 的 IEEE-754 单精度尾数低 8 位全为 `0`（例如 `w = 1.0f` 不透明 Alpha、`0.0f`、`0.5f`、`0.25f` 或任何整数像素圆角/边距 `0..32768`）时，`Value` 自动将 `w` 的高 24 位（1 位符号 + 8 位阶码 + 高 15 位尾数）存入字节 `12..14`，标记 `ValueTag::Float4Inline(22)`，**0 池分配、0 锁竞争**。
2. **高精度溢出回退**：当静态字面量的 `w` 含有非零低 8 位尾数时，自动通过 `detail::internArenaFloat4` 驻留到 `ValuePool` 连续槽位并标记 `ValueTag::Float4Arena(26)`，保持 32 位全精度无损往返。
3. **动画热路径 `MakeFloat4InlineFast`**：在 `AnimationEngine` 每帧插值 `Color` / `Thickness` / `CornerRadius` 时，调用 `aero::MakeFloat4InlineFast(Float4Value)` 对 `w` 执行 IEEE-754 最近偶数舍入至高 24 位（精度达 `1/32768`，远超 8-bit/10-bit 色彩与亚像素几何精度），保证每帧数千通道属性插值 **100% 命中 `Float4Inline`**，完全不触碰 `ValuePool`。

## 3. `ValuePool` 大结构驻留与外部 GC 根标记协议（External Root Sweep Survival）

RealScript 运行时通过 `SharedValuePoolBridge` 统一管理跨模块共享的 `StringValue` 驻留池、`Float4Arena` / `QuatArena` / `EnumArena`、`InlineStructArenaEntry`（`<= 4` 槽位结构体扁平池）、堆分配 `StructStorage` 与 `NativeHandle` `SymbolId` 映射表。

当 `ManagedHeap` 触发增量或全量垃圾回收并调用 `ValuePool::sweepUnmarked()` 时，为了防止驻留在 C++ 视觉树、模板缓存或样式/动画引擎中的池化字符串与大结构被误回收，AeroGUI 2.0 实现了严密的**外部根标记协议**：

* **自动注册外部标记回调**：活跃的 C++ 子系统在初始化/挂载时接入 `ValuePool` 外部根扫描链，GC 标记阶段自动遍历并调用 `rs_rt::markValueInPools(val)`：
  1. **视觉树与控件层**：`UIElement::MarkSparseValuesInPools()`、`UIElement::MarkSubtreeValuesInPools()` 与 `ItemsControl::MarkExtraValuesInPools()`（保护稀疏属性表与虚拟化列表 `ItemsSource` 数据项）；
  2. **二进制包与全局注册表**：`AxbPackage::PinValuesInPools()` / `MarkValuesInPools()` 与 `AxbTemplateRegistry::MarkValuesInPools()`（保护已反序列化的静态/动态资源、样式 Setter、触发器条件值及模板默认值）；
  3. **绑定、样式与动画引擎**：`DualTrackBindingEngine::MarkValuesInPools()`、`StyleTriggerEngine::MarkValuesInPools()`（保护属性快照 `PropertySnapshotSlot::baseValue`）以及 `AnimationEngine::MarkValuesInPools()`（保护关键帧与基础值快照）。
* **视觉树 `PersistentRoot` 托管包装保活**：当 `UIElement` 连接到根视觉树（`SetAsRootVisual(true)`）且绑定了 `UiHeap` 上的脚本派生包装对象（`BindScriptWrapper`）时，自动通过 `PersistentRoot` 将该 `ObjectRef` 固定为 `UiHeap` 强根；断开子树时自动释放，杜绝悬垂引用与内存泄漏。

## 4. 一语言双堆（`SimDomain` + `UiDomain`）、`partial class` 与 `[Observable]`

* **双堆动态路由与确定性隔离（`ExecutionDomainContext`）**：
  * `SimDomain`（绑定 `SimHeap`）运行固定 Tick 确定性逻辑；`UiDomain`（绑定 `UiHeap`）跟随渲染帧或 OS 事件唤醒并在帧末执行有界增量 GC（`stepIdleForDuration`）。
  * `readObjectField` 动态匹配 `ref.heapId`（兼容 `<= 4` 字段 `inlineValues` 与 `> 4` 字段 `values`），跨堆读取 `ObjectKind::String` 时自动池化为不可变 `StringValue`；`writeObjectField` 严禁从 `UiDomain` 修改 `SimHeap` 状态，UI 对游戏状态的变更一律通过 `[SimCommand]` 封送至 `SimCommandEnvelope` 队列。
* **`partial class` 与 64 位安全 `dirtyFieldMask`**：
  * `AeroCompiler` 单通解析 `.xaml` 并合成强类型 `*.g.rs` `partial class` 桩代码（含命名控件槽位与事件处理器绑定），与手写 `.rs` 源码在同一次编译会话中归并为单一 `ProgramImage`；
  * `[Observable]` 属性在编译期分配独立 `observableBitIndex (0..62)`，超出 63 个可观察成员时归并至溢出位 `63`，彻底根除 `1ULL << idx` 移位未定义行为，并在值相等时短路跳过脏队列入队。

---

# 三、 `.axb4` 二进制包规范全表（Version `0x00020000`）

`.axb4` 是 AeroGUI 2.0 面向运行时的零 XML 解析器、单通线性二进制包格式（定义于 `include/aero/compiler/AxbFormat.hpp` 与 `src/core/AxbFormat.cpp`）。纯运行时（`aerogui_runtime`）无需链接任何 XML 或脚本编译器前端即可在亚毫秒内完成反序列化、热重载重绑定（`BindAgainstProgram`）与视觉树挂载（`MountAxbTemplate`）。

## 1. 文件头与核心基础表结构

所有多字节整数与 IEEE-754 单精度浮点数均采用 **Little-Endian（小端序）** 编码；字符串统一编码为 `uint32_t byteLen + UTF-8 bytes`；`UnifiedValue` 字面量按 `AxbLiteralTag` 编码（`Empty=0`、`Bool=1`、`Int64=2`、`Float32=3`、`Float4=4`、`String=5`）。

| 顺序 | 区段名称 | 魔数 / 标识 | 二进制内容与字段布局 |
| ---: | :--- | :--- | :--- |
| 1 | **文件魔数与版本头** | `"AXB4"` (`0x34425841`) | `magic:u32` (`0x34425841`)、`version:u32` (`0x00020000`)、`className:str`、`rootTypeName:str` |
| 2 | **字符串驻留池** | `stringTable` | `count:u32` + `count * str`（供指令流与 `MOT6` 状态机通过 `uint32_t strIdx` O(1) 索引） |
| 3 | **命名控件槽位表** | `namedControls` | `count:u32` + `{name:str, typeName:str, typeSymbolId:u64, fieldIndexInClass:u32}` |
| 4 | **编译期槽位链基表** | `bindingChains` | `count:u32` + `{rawPath:str, targetPropId:u16, mode:u8, hopCount:u32, hops[...]}`（每跳含 `memberName`、64 位 `declaringTypeId`、`fieldIndex`、`observableBitIndex`、`getter/setterSymbolId` 与标志位） |
| 5 | **事件路由绑定表** | `eventBindings` | `count:u32` + `{controlName:str, eventName:str, handlerMethodName:str, handlerSymbolId:u64}` |
| 6 | **主视觉树指令流** | `instructions` | `count:u32` + `{opcode:u8, propId:u16, flags:u8, strIdx0:u32, strIdx1:u32, symbolId:u64, literalValue:Value}` |
| 7 | **根句柄与静态资源表** | `resources` & `fallbacks` | `rootHandleFieldIndex:u32`、`resCount:u32 + {key:str, val:Value}`、`fbCount:u32 + fallbackValues[...]` |

## 2. 主指令集全表（`AxbOpcode` `1..11`）

| Opcode | 枚举名称 | `propId` 语义 | `flags` 语义 | `strIdx0` / `strIdx1` / `symbolId` / `literalValue` 操作数契约 |
| ---: | :--- | :--- | :--- | :--- |
| **1** | `BeginElement` | 保留 (`0`) | `0x01` = 挂载至父 `HeaderedContentControl` 的 `Header` 槽 | `strIdx0`: 控件类型名字符串索引；`strIdx1`: `x:Name` 字符串索引（无名称时为 `0xFFFFFFFF`）；压入当前挂载元素栈。 |
| **2** | `EndElement` | 保留 (`0`) | `0` | 弹出当前挂载元素栈顶节点，闭合子树作用域。 |
| **3** | `SetStaticProperty` | 目标 `BuiltinPropertyId` | `0` | 将预解析的 `literalValue` 以 `ValueSourceRank::Local` 写入栈顶元素对应快槽或稀疏槽（渐变刷自动解析注册至 `GradientBrushRegistry`）。 |
| **4** | `BindProperty` | 目标 `BuiltinPropertyId` | `0` | `strIdx0` 指向 `bindingChains[strIdx0]`，在 `DualTrackBindingEngine` 中建立 Track A 多跳槽位链或 `ElementName`/`RelativeSource` 绑定。 |
| **5** | `BindEcsColumn` | 目标 `BuiltinPropertyId` | `0` | `symbolId`: ECS 组件 64 位 `SymbolId`；`strIdx0`: 字段名索引；`strIdx1`: 行索引；建立 Track B `NativeRawColumnView` 零拷贝列绑定。 |
| **6** | `BindEvent` | 保留 (`0`) | `0` | `strIdx0` 指向 `eventBindings[strIdx0]`，`symbolId` 为目标方法 64 位符号 ID（支持 VM `Interpreter`、AOT 函数表与弱目标委托路由）。 |
| **7** | `AddRowDefinition` | 保留 (`0`) | `GridUnitType` (`Auto=0, Pixel=1, Star=2`) | `literalValue` (`Float32`) 为行高数值/权重，向栈顶 `Grid` 追加 `RowDefinition`。 |
| **8** | `AddColumnDefinition` | 保留 (`0`) | `GridUnitType` (`Auto=0, Pixel=1, Star=2`) | `literalValue` (`Float32`) 为列宽数值/权重，向栈顶 `Grid` 追加 `ColumnDefinition`。 |
| **9** | `SetResourceProperty` | 目标 `BuiltinPropertyId` | `AxbResourceLookupFlags`：<br>`0x00`=`StaticResource`<br>`0x02`=`DynamicResource`<br>`0x04`=`{noesis:Loc}` | `strIdx0` 为资源键名索引；`0x00` 执行静态资源查表注入，`0x02` 与 `0x04` 同时在 `AxbTemplateRegistry` 注册弱订阅以支持亚毫秒级运行期主题/多语言热切换。 |
| **10** | `BindTemplateParent` | 目标子元素 `BuiltinPropertyId` | `ValueConverterKind` (`0..7`) | `strIdx0` 存储源宿主控件属性 ID（`sourcePropId`），在 `ControlTemplate` 内部建立 O(1) 零分配 `TemplateBinding` 同步通道。 |
| **11** | `BindMultiProperty` | 目标 `BuiltinPropertyId` | `0` | `strIdx0` 指向 `multiBindings[strIdx0]`（由 `AP14` 段承载），建立多源聚合 `MultiBinding`（支持嵌套子 `MultiBinding`、`StringFormat`、`MathConverter` 与 `IMultiValueConverter`）。 |

## 3. 扩展二进制段全表（Optional Trailing Sections）

主指令流与资源表之后，`.axb4` 按需追加自描述的四字节魔数扩展段，全部经过双向边界防御校验（零越界、零异常）：

| 扩展段魔数 | ASCII 标识 | 引入阶段 | 承载数据结构与核心职责 |
| :--- | :---: | :---: | :--- |
| `0x4644534D` | **`"MSDF"`** | Phase 4 / 6 | **自适应 4 通道 MTSDF 矢量图集段**：序列化 `AxbMtsdfPathEntry[]`（64 位 `pathHash`、包围盒、图集 UV、自适应 `32/64/128` 单元格尺寸、`pxRange` 及 RGBA8 四通道方向距离场像素，其中 RGB 保角点锐度、A 存真实欧氏 SDF 供描边与柔和阴影采样）。 |
| `0x35435453` | **`"STC5"`** | Phase 5C | **声明式样式、缓动故事板与视觉状态组段**：序列化 `AxbStyleDefinition[]`（含 `BasedOn` 继承链、属性 Setter、带进出场动画的 `Trigger`/`MultiTrigger`、`VisualStateManager` 状态组）与 `AxbStoryboard[]`（11 种解析缓动函数 × 3 模式 + 关键帧序列）。 |
| `0x35505444` | **`"DTP5"`** | Phase 5D | **元素相对绑定、数据/事件触发器与数据模板段**：扩展 `bindingChains` 的 `ElementName` / `RelativeSource (Self / AncestorType)` 与内建 `ValueConverterKind`；序列化 `DataTrigger` / `MultiDataTrigger` 多跳编译路径、`AxbEventTrigger[]`（`Loaded/Click/MouseEnter/Checked/ValueChanged` 等 8 类路由事件）及递归子包 `AxbDataTemplate[]`。 |
| `0x36544F4D` | **`"MOT6"`** | Phase 6 / P1-5 | **多前端扁平动画曲线、骨骼约束与多层状态机段**：连续存储 12 张扁平数组：`motionKeyframes`（28B `AxbFlatKeyframe`）、`motionTracks`、`motionEvents`（时间轴帧事件标记）、`motionClips`、`transformConstraints`（非树父子骨骼/Null 层变换约束）、`smConditions`、`smTransitions`（含交叉淡入淡出与 `ExitTime` 门控）、`smStates`（`SingleClip` / `ScrubbedClip` / `Blend1D` / `ExitState`）、`smLayers`（`Override` 覆盖层与 `Additive` 叠加层）、`smListeners`（零 VM 往返指针/焦点监听器）、`contractInputs`（映射 `0x1000 + slot`）与 `stateMachines`。 |
| `0x31505443` | **`"CTP1"`** | P1-1 | **无头控件模板段（`ControlTemplate`）**：序列化 `AxbControlTemplate[]`（`key`、`targetTypeName`、内嵌完整 `.axb4` 子包字节流、带 `TargetName` 的模板触发器、模板路由事件触发器与 `VisualStateGroups`），反序列化后自动与 `Style` 的 `PropId_Template` 关联。 |
| `0x3231524D` | **`"MR12"`** | P1-2 | **合并资源字典溯源段（`MergedDictionaries`）**：按拓扑顺序记录跨文件合并至当前包的 `ResourceDictionary Source` URI 列表（含 `.noesis` 工程级全局字典自动合并路径）。 |
| `0x34315041` | **`"AP14"`** | P1-4 | **多源绑定、数学/格式化转换器与交互行为段**：序列化 `StringFormat`、`MathConverter` 表达式、自定义 C++/RealScript `IValueConverter`/`IMultiValueConverter` 符号、`AxbCompiledMultiBinding[]`（支持递归嵌套）、触发器关系比较算子 `TriggerComparisonOp`（`==, !=, <, <=, >, >=`）及 `<b:Interaction.Triggers>` 四大动作（`ChangePropertyAction`、`ControlStoryboardAction`、`InvokeCommandAction`、`PlaySoundAction`）。 |

---

# 四、 零外部依赖渲染管线与跨平台窗口闭环

AeroGUI 2.0 彻底剔除了 FreeType、HarfBuzz、libpng、zlib、libjpeg、stb、GLFW/SDL 与 rive-cpp 等一切外部第三方二进制依赖，以精炼的自研 C++17 模块打通从资产解码到硬件上屏的完整闭环：

## 1. `FontAtlas`：纯 C++17 TrueType 解析、Cardano 二次贝塞尔解析 SDF 与 `"AGFS"` 分片

* **零依赖 `.ttf` 解析器（`TrueTypeFontFace`）**：直接解析 `head`、`hhea`、`maxp`、`hmtx`、`loca`、`glyf`（支持简单字形与递归复合字形 `Compound Glyph` 仿射拼接）、`cmap`（Format 4 / Format 6 / Format 12 全 Unicode 平面及 Symbol 编码回退）、`kern` 字距调整表与 `name` 家族名表，全程零异常、零 RTTI、纯 `float` 计算。
* **Cardano 解析二次贝塞尔欧氏距离场烘焙**：对每条 TrueType 二次贝塞尔轮廓段 $B(t) = (1-t)^2 P_0 + 2(1-t)t P_1 + t^2 P_2$，通过 **Cardano 三次方程解析求根**求取采样点到曲线的精确最短欧氏距离与绕数符号，烘焙至 `1024x1024` 统一图集上半区（`v in [0.0, 0.5)`）的 Skyline R8 动态图集中；未加载外部 `.ttf` 时自带几何矢量化 ASCII SDF 保底字库，确保无头测试 100% 清晰可读。
* **UTF-8 排版与 `"AGFS"` WebFont 流式分片**：`FontAtlas::MeasureLayout` 完整支持字距、`Ascent/Descent/LineGap` 多行度量及 `NoWrap` / `Wrap` / `WrapWithOverflow` 断词折行；`ExportGlyphShard` / `UnpackGlyphShard` 与 `WebFontStreamProvider` 支持按 256 码位区间导出紧凑 `"AGFS"`（`0x53464741`）二进制字形分片，在 Web 端非阻塞异步拉取 CJK 分片并在到达后局部刷新脏渲染页。

## 2. `ImageAtlas`：零依赖 RFC1951 / PNG / JPEG / BMP / TGA 解码与多贴图拆合批

* **纯 C++17 原生图像解码栈**：内建完整 **RFC1951 DEFLATE / RFC1950 zlib 解压器**（定长与动态 Huffman 树 + LZ77 滑动窗口）、**PNG 解码器**（支持 5 种 Paeth/Sub/Up/Average 扫描线滤波还原、8/16-bit 灰度/索引调色板 `PLTE`+`tRNS`/RGB/RGBA）、**Baseline JPEG 解码器**（SOF0/DHT/DQT、ZigZag 反量化、2D IDCT 与 YCbCr 转 RGBA8）、**BMP**（24/32-bit 上下翻转兼容）与 **TGA**（未压缩及 RLE 压缩）。
* **XAML Pack URI 归一化与零 Scissor 拉伸裁剪**：`NormalizeImageUriPath` 自动剥离 `pack://application:,,,/Assembly;component/` 前缀；`ComputeImageStretchPlacement` 精确计算 `None`、`Fill`、`Uniform` 与 `UniformToFill` 的目标矩形与裁剪子 UV，无需额外插入 GPU Scissor 断批即可完成居中裁切。
* **Skyline 共享图集与独立大贴图槽位拆合批**：小尺寸 UI 图标（默认 `<= 512x512`）自动通过 Skyline 算法打包进 `2048x2048` 共享图集（`textureId = 0`，四周自动外扩 1px 边缘像素消除双线性采样渗色）；大幅背景原画自动分配独立 `textureId > 0` 槽位，由 `CommandPageHeader::TextureId()` 与 `GpuClipDrawBatch::textureId` 自动完成合批与切页。

## 3. `GradientBrushRegistry` 与 9 类 GPU UberShader 解析几何图元

* **多停点渐变刷与 256-Texel 1D LUT 烘焙**：`GradientBrushRegistry`（上限 `4095` 把活跃画刷）统一管理多停点 `LinearGradientBrush` 与带偏移焦点 `GradientOrigin` 的 `RadialGradientBrush`（支持 `Pad` / `Reflect` / `Repeat` 延展模式及 `RelativeToBoundingBox` / `Absolute` 映射模式）。每把画刷预烘焙为 256 像素宽 RGBA8 一维色带行与浮点几何元数据行上传至共享图集底部，GPU 片元着色器与 CPU 参考光栅化器均通过 O(1) 采样完成精确求值。
* **24B 顶点统一承载全部 9 种 `AeroPrimType`**：
  * `0: SolidQuad`（纯色矩形快径）
  * `1: SdfRoundedRect`（含 4 种 SDF 子模式：统一圆角快径、统一圆角镂空四边独立厚度边框环、非对称四角 `sdRoundedBox4` 填充、非对称四角镂空 `BorderBrush` 环）
  * `2: SdfTextGlyph`（单通道 SDF 抗锯齿字形）
  * `3: TexturedQuad`（`Image` / `ImageBrush` 双线性采样贴图矩形）
  * `4: MsdfVectorPath`（4 通道 `median(R,G,B)` 保角点矢量图标 + A 通道欧氏描边）
  * `5: SdfRadialArc`（零细分解析 SDF 圆环/扇形仪表盘）
  * `6: DynamicMesh`（4KB 页内可变形 2D 网格）
  * `7: SdfEllipse`（`<Ellipse>` 解析 SDF 抗锯齿椭圆填充与镂空描边环）
  * `8: GradientRoundedRect`（12-bit `brushId` 驱动的多停点线性/径向渐变圆角矩形与渐变椭圆）

## 4. `Matrix4x4` 3D 透视平面单应性正逆投影、`BlurEffect`/`DropShadowEffect` 与 Rive v7 解码器

* **纯 `float` 3D 透视变换与闭式逆单应性命中测试**：
  * `UIElement` 原生支持 `<noesis:CompositeTransform3D>` 与 `<PlaneProjection>`（`RotationX/Y/Z`、`TranslateX/Y/Z`、`ScaleX/Y/Z`、自定义枢轴与虚拟相机距离 `d = 1000px`），每个通道均开放独立 `BuiltinPropertyId (138..143)` 供故事板关键帧动画驱动。
  * `Matrix4x4::ComposePlanarHomography` 精确级联多层祖先 3D 平面单应性矩阵，渲染树通过 `ProjectPoint2D` 输出真实透视梯形顶点；输入命中测试通过 `Matrix4x4::InverseProjectScreenToLocalPlane` 闭式求解 $3 \times 3$ 射线-平面（$z=0$）逆单应性伴随矩阵并校验前向裁剪面 $w > 0$，使 3D 倾斜面板内的按钮、滑块与输入框保持像素级精准点击交互。
* **`DropShadowEffect` 与 `BlurEffect`**：
  * `DropShadowEffect` 在主体图元前自动生成外扩 `sdfPadPx` 的解析柔和距离场阴影节点（零离屏 RT 切换开销）；
  * `BlurEffect` 在 `PagedRenderTree` 中记录 `BlurLayerSpan` 子树区间，渲染后端在预乘 Alpha 离屏层完成子树光栅化与三遍可分离盒式近似高斯卷积模糊后再合成回主帧缓冲。
* **Rive v7.0 LEB128 `.riv` 原生二进制流解码器与 `RiveControl`**：
  * `DecodeRiveBinaryToAxbPackage` 与 `RiveFrontendImporter` 直接解析真实 Rive v7.0 二进制文件头（`"RIVE"` + 主次版本 + LEB128 属性表 Schema），提取 Artboard、骨骼/变换层级、参数化与三次贝塞尔闭合/开放轮廓（自动转为 SVG 路径并烘焙自适应 MTSDF）、线性动画与三次贝塞尔时间切线曲线、多层状态机、混合态与交互监听器，一键降级为标准 `.axb4 + MOT6` 包；XAML `<noesis:RiveControl Source="..."/>` 原生支持自动加载、拉伸对齐与 `<noesis:RiveInput>` 契约端口驱动。

## 5. `GpuBackend` 与 `NativeWindowHost` 三栖零轮询宿主闭环

* **双模 GPU 渲染设备（`IGpuDeviceBackend`）**：
  * `OpenGlDeviceBackend`：零外部 GL 头文件依赖，内置完整 40 函数指针 `GlFunctionTable`，运行期在 Linux 通过 `dlopen("libGL.so.1" / "libEGL.so.1")`、在 Windows 通过 `opengl32.dll` + `wglGetProcAddress` 动态解析 OpenGL 3.3 Core / WebGL2 入口点，管理环形 VBO、静态扩容 Quad EBO、多槽位贴图与 Scissor 裁剪批次提交。
  * `SoftwareSurfaceBackend`：无 GPU/无显示服务环境下由 `CreateBestAvailableGpuBackend` 自动无感回退，直接复用 `ZeroCopyGpuStream::RasterizeTreeToRgba8` 软件 UberShader 管线，确保 CI 黄金像素测试与桌面硬件渲染共享同一套数学语义，并提供零依赖 PPM/BMP 帧导出。
* **三栖窗口与事件阻塞唤醒桥（`NativeWindowHost` + `HostDrivers`）**：
  * **Linux (`LinuxX11OpenGL` / `LinuxX11Software`)**：动态 `dlopen("libX11.so.6")` 创建原生窗口，当 `DesktopEventHost::CanSleepIdle()` 为真时阻塞于 `poll(XConnectionNumber, wakePipeFd)`，实现 **0.0% 静止 CPU/GPU 占用**，任意后台线程可通过自管道 `WakeEventLoop()` 亚毫秒唤醒；无 `DISPLAY` 时自动降级至 `HeadlessSoftware`。
  * **Windows (`Win32WglOpenGL` / `Win32GdiSoftware`)**：原生 `HWND` + WGL 3.3 Core 上下文 + `IMM32` 输入法光标跟随 + `MsgWaitForMultipleObjects` 零轮询阻塞唤醒。
  * **WebAssembly (`EmscriptenWebGL2`)**：编译参数严格锁定 `-Oz -flto -fno-exceptions -fno-rtti -msimd128 -sUSE_WEBGL2=1 -sASYNCIFY=0`；通过按需 `requestAnimationFrame` 状态机（干净帧自动挂起 `SuspendedIdle`）、`WebImeAdapter` 透明 DOM `<input>` 组合输入桥与 `AriaShadowNode` 无障碍语义树，将纯运行时体积严格控制在轻量级预算之内。

---

# 五、 全栈模块目录与里程碑实装矩阵

```text
AeroGUI2/
├── include/aero/
│   ├── core/        #RsAliases, UnifiedValue(16B + Matrix2D/Matrix4x4), TypeClass(64-bit SymbolId),
│   │                #DependencyProperty(FastInlineStorage 56B + CompactPropertyEntry 32B), UIElement
│   ├── bridge/      #ScriptUiBridge (UIElement <-> NativeHandle O(1) 互转与可观察槽位桥接)
│   ├── compiler/    #AxbFormat(.axb4 Opcodes 1..11 + MSDF/STC5/DTP5/MOT6/CTP1/MR12/AP14 + Rive v7),
│   │                #AeroSceneIR(Xaml/Rive/Lottie/Svg Importers + ContractInputBank), AeroCompiler
│   ├── layout/      #DepthBucketLayoutQueue (64 层位图扫描深度桶增量 Measure/Arrange 队列)
│   ├── binding/     #DualTrackBindingEngine (Track A 多跳链/MultiBinding/转换器 + Track B ECS SIMD 列)
│   ├── controls/    #Controls (Panel/Grid/Dock/Wrap/UniformGrid/ScrollViewer/Viewbox/TabControl/Expander/
│   │                #Button/CheckBox/RadioButton/Slider/TextBox/ComboBox/ItemsControl/Shapes/RiveControl)
│   ├── input/       #InputEventRouter (64 槽零分配输入环、2D/3D 逆投影命中测试、滚轮冒泡与焦点导航)
│   ├── animation/   #AnimationEngine (11x3 缓动、C1 二阶弹簧、MakeFloat4InlineFast 与 MOT6 多层状态机)
│   ├── style/       #StyleTriggerEngine (BasedOn 样式、Property/Data/Event/Interaction 触发器与 VSM)
│   ├── render/      #FontAtlas(TTF+Cardano SDF+AGFS), ImageAtlas(DEFLATE+PNG/JPG/BMP/TGA+Skyline),
│   │                #PagedRenderTree(4KB CommandPage + 9 PrimTypes), ZeroCopyGpuStream, GpuBackend
│   └── host/        #HostDrivers(EmbeddedGame/DesktopEvent/WebCanvas + IME/WebFont), NativeWindowHost
├── src/             #对应纯 C++17 实现（运行时与编译器物理隔离，支持 -fno-rtti / -fno-exceptions）
├── third-party/
│   └── RealScript/  #Git 子模块（ABI v2.0 16B Value、ValuePool、双堆 ExecutionDomain、partial/[Observable]）
└── tests/           #phase1..phase6 + p0_1..p0_4 + p1_1..p1_5 + p2_1..p2_3 全量回归套件
```

| 阶段里程碑 | 核心交付内容 | 回归测试套件 | 状态 |
| :--- | :--- | :--- | :---: |
| **Phase 1 ~ 6** | 统一 ABI v2.0 底座、双轨绑定、`DepthBucketLayoutQueue`、4KB `PagedRenderTree`、`MOT6` 多层状态机与多前端 IR | `phase1_foundation_tests` ~ `phase6_abi_motion_importers_tests` + `phase4_slim_runtime_smoke_tests` | 已实装 |
| **P0-1 ~ P0-4** | 非对称圆角/镂空边框 SDF、`OpenGlDeviceBackend` + `NativeWindowHost`、纯 C++17 `.ttf` Cardano SDF `FontAtlas`、RFC1951 + PNG/JPG/BMP/TGA `ImageAtlas` | `p0_1_tests` ~ `p0_4_tests` | 已实装 |
| **P1-1 ~ P1-5** | `CTP1` `ControlTemplate`、`MR12` 跨文件资源字典与 `{noesis:Loc}` 热切换、`GradientBrushRegistry` + 几何图元/复合控件、`AP14` `MultiBinding`/`MathConverter`/交互行为、3D 透视单应性 + `Blur/DropShadow` + Rive v7 `.riv` 解码器 | `p1_1_tests` ~ `p1_5_tests` | 已实装 |
| **P2-3（本阶段）** | 架构设计文档全面同步至 ABI v2.0、`.gitmodules` 与 `CMakeLists.txt` CTest 工程化闭环、`Samples.zip` 全量 **21 工程 / 74 `.xaml` / 9 `.riv` / 16 主场景黄金像素基准库** | `p2_3_tests`（**318/318 断言 100% 通过**） | 已实装 |
