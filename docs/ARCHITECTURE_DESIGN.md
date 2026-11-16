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
| **`UnifiedValue`** (`rs::runtime::Value`) | `realscript/runtime/Runtime.h`<br>`aero/core/UnifiedValue.hpp` | **16 Bytes** | **16 Bytes** | 全局唯一 ABI v2.0 值单元；字节 `0..14` 为联合体载荷，字节 `15` 为统一 `ValueTag` (`0..29`)，单个 64B 缓存行紧密容纳 4 个值（纯 float 几何类型定义于 `aero/drawing/Geometry.hpp`）。 |
| **`Float4Value`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **4 Bytes** | 4 通道 32 位 `float {x, y, z, w}`，统一承载 UI `Color`、`Rect`、`Point`、`Thickness`、`CornerRadius` 与变换向量。 |
| **`ObjectRef` / `StringValue` / `StructValue`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **4 / 8 Bytes** | 均严格控制为 16B，且在 `offsetof == 15` 处内嵌 `vtag_`，与 `Value::u_` 原位重合，无需外包 `std::variant` 判别器。 |
| **`NativeHandle`** | `realscript/runtime/Runtime.h` | **16 Bytes** | **8 Bytes** | 代际原生句柄（`slot:32`、`generation:16`、`registryId:16`、`typeId:64`）；存入 `Value` 时经 `typeIdx` 池化压缩至 `NativeHandleCell`（16B）。 |
| **`FastInlineStorage`** | `aero/core/DependencyProperty.hpp` | **56 Bytes** | **4 Bytes** | `UIElement` 内联高频属性块（`Width/Height/MinWidth/MinHeight/Margin/Padding/Opacity/Visibility` + 对齐位 + 交互焦点状态位 + 布局脏标记）。 |
| **`CompactPropertyEntry`** | `aero/core/DependencyProperty.hpp` | **32 Bytes** | **16 Bytes** | 零空洞稀疏依赖属性槽：字节 `0..7` 存 `propId/sourceRank/flags/bindingSlotId/animationTrackId`；字节 `8..15` 存双轨混合与契约输入槽；字节 `16..31` 存 16B `UnifiedValue`（每 64B 缓存行恰好 2 个条目）。 |
| **`AxbFlatKeyframe`** | `aero/asset/AxbFormat.hpp` | **28 Bytes** | **4 Bytes** | `MOT6` 扁平连续关键帧记录（`timeSec` + `Float4Value` + 插值模式 + Q7.8 定点三次贝塞尔超调切线控制点）。 |
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

`.axb4` 是 AeroGUI 2.0 面向运行时的零 XML 解析器、单通线性二进制包格式（定义于 `include/aero/asset/AxbFormat.hpp`——聚合 `GradientBrush.hpp`、`MtsdfPathBaker.hpp`、`RiveDecoder.hpp`、`ValueConverters.hpp`——并由模块化 `src/asset/*.cpp` 实现；原兼容转发头 `include/aero/compiler/AxbFormat.hpp` 与 `include/aero/script/ScriptUIBridge.hpp` 已彻底移除）。纯运行时内核（`AeroGUI::Kernel` / `AeroGUI::Runtime`）无需链接任何 XML 或脚本编译器前端即可在亚毫秒内完成反序列化、热重载重绑定（`BindAgainstProgram`）与视觉树挂载（`MountAxbTemplate`）。

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
| **1** | `BeginElement` | 保留 (`0`) | `0x01` = 挂载至父 `HeaderedContentControl` 的 `Header` 槽 | `strIdx0`: 控件类型名字符串索引；`strIdx1`: `x:Name` 字符串索引（无名称时为 `0`，因为空串 `""` 固定驻留于字符串表索引 `0`）；压入当前挂载元素栈。 |
| **2** | `EndElement` | 保留 (`0`) | `0` | 弹出当前挂载元素栈顶节点，闭合子树作用域。 |
| **3** | `SetStaticProperty` | 目标 `BuiltinPropertyId` | `0` | 将预解析的 `literalValue` 以 `ValueSourceRank::Local` 写入栈顶元素对应快槽或稀疏槽（渐变刷自动解析注册至 `GradientBrushRegistry`）。 |
| **4** | `BindProperty` | 目标 `BuiltinPropertyId` | `0` | `strIdx0` 指向 `bindingChains[strIdx0]`，在 `DualTrackBindingEngine` 中建立 Track A 多跳槽位链或 `ElementName`/`RelativeSource` 绑定。 |
| **5** | `BindEcsColumn` | 目标 `BuiltinPropertyId` | `0` | `symbolId`: ECS 组件 64 位 `SymbolId`；`strIdx0`: 组件名字符串索引；`strIdx1`: 字段名字符串索引；建立 Track B `NativeRawColumnView` 零拷贝列绑定。 |
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

# 五、 五层单向依赖架构、模块化目标与全栈模块目录

## 1. 五层单向依赖架构与正交控件继承体系（NoesisGUI 风格工程化分层）

### 1.1 五层单向依赖拓扑（5-Tier Unidirectional DAG）与跨层解耦机制

AeroGUI 2.0 借鉴 NoesisGUI 的分层工程化结构，确立严格自底向上的 **5-Tier 单向依赖图（禁止任何跨层反向 `#include` 或循环依赖）**：

```text
Tier 0 (drawing / core / runtime)
  └─> Tier 1 (asset)
        └─> Tier 2 (layout / animation / style / binding / input / render / host)
              └─> Tier 3 (controls)
                    └─> Tier 4 (bridge / compiler / tools)
```

* **Tier 0 独立数学底座与工具解耦**：
  * **`aero/drawing/Geometry.hpp`**：将纯 32 位 `float` 几何与变换原语（`Color`、`Size`、`Point`、`Rect`、`Thickness`、`CornerRadius`、`Stretch`、`StretchDirection`、`Orientation`、`ExpandDirection`、`Matrix2D`、`CompositeTransform3DParams`、`Matrix4x4`）从 `UnifiedValue.hpp` 抽离，零 `RealScript` / `std::variant` 头文件依赖、零 `double`；`UnifiedValue.hpp` 包含 `Geometry.hpp` 保持 100% 源码兼容并提供统一判等函数 `aero::UnifiedValueEquals`。
  * **`aero/core/StringUtils.hpp`**：在 `namespace aero::str` 中提供零堆分配字符串/数值解析工具（`Trim`、`EqualsIgnoreCase`、`StartsWith`、`EndsWith`、`StripNamespacePrefix`、`ShortTypeName`、`TryParseFloat32`、`ParseFloat32Or`），消除跨模块重复辅助函数。
  * **`UIResourceContext` Pimpl 头文件隔离**：`include/aero/core/UIResourceContext.hpp` 仅前向声明 `AxbTemplateRegistry`、`FontAtlas`、`ImageAtlas`、`GradientBrushRegistry` 并通过 `struct Impl` Pimpl 隐藏底层容器与互斥锁，确保 Tier 0 头文件零 Tier 1/2 头文件与 `<mutex>` 泄露。
* **Tier 0–2 零控件依赖铁律与双向解耦**：`drawing`、`core`、`runtime`、`asset`、`layout`、`animation`、`style`、`binding`、`input`、`render`、`host` 全量头文件与实现单元对 `aero/controls/*` 保持 **0 头文件包含、0 具体控件向下转型（0 `dynamic_cast` / 0 `typeid(`）**：
  * **虚能力钩子与全局静态注册**：Tier 2 子系统通过 `UIElement` 虚能力钩子（渲染、输入、IME、ARIA、样式/绑定/模板钩子）、`UIElement::DispatchLoadedSubtree(bool skipNestedUserControls = true)` 以及 `RegisterUIElementResourceHooks`、`RegisterUIElementTemplateHooks`、`RegisterUIElementInteractionHooks` 调度 Tier 3 控件能力；
  * **`ControlRenderEmitter`（`aero/render/PagedRenderTreeInternal.hpp`）**：作为 `PagedRenderTree` 友元发射器暴露底层图元原语，由 Tier 3 `src/controls/ControlRenderEmitters.cpp` 承接具体控件绘制。
* **Tier 2/3 反向依赖彻底清零与去重抽象**：
  * **输入法与宿主解耦（`aero/input/TextInputMethod.hpp`）**：将 `ITextInputMethodHost` 从 `HostDrivers.hpp` 迁至 `aero/input/TextInputMethod.hpp`，并提取共享 `ImeCompositionController` 封装 8 项组合态字段、UTF-8 编码点步进与有界提交历史；`src/input/InputEventRouter.cpp` 对 `host/` 保持 **0 包含**，而 `DesktopImeAdapter` 与 `WebImeAdapter` 统一复用 `ImeCompositionController`。
  * **跨宿主统一基类 `HostDriverBase`（`aero/host/HostDrivers.hpp`）**：收纳 `UIViewContext`、`rootVisual_`、`resourceContext_`、`viewportSize_`、`HostFrameTelemetry`、统一 `RunUiPhases` 管线与唤醒注册表监听；`EmbeddedGameHost`、`DesktopEventHost`、`WebCanvasHost` 仅保留各自的空闲休眠与 rAF 策略。
  * **渲染树与绑定头解耦**：`include/aero/render/PagedRenderTree.hpp` 通过前向声明 `class DualTrackBindingEngine;` 消除对 `binding/` 头文件的传递包含。
  * **绑定路径公共库与域清理钩子**：`include/aero/binding/PropertyPathUtils.hpp`（`namespace aero::binding_util`）集中承载 `FindTypeSymbolByRawName`、`ResolveCompiledHopTargetType`、`NextContextParent`、`ResolveEffectiveDataContext`、`HasFocusedDescendant`，由 `DualTrackBindingEngine`、`StyleTriggerEngine` 与 `ScriptUIBridge` 共同复用；`DualTrackBindingEngine::RegisterDomainCleanupHook` 允许 Tier 4 `ScriptUIBridge` 注册 `&ClearScriptUiDomainState`，使 `src/binding/` 与 `src/controls/` 对 `bridge/` 与 `compiler/` 保持 **0 包含**。
  * **废弃转发头与历史过渡别名清零**：已物理删除 `include/aero/compiler/AxbFormat.hpp` 与 `include/aero/script/ScriptUIBridge.hpp`，同步清理 `UiResourceContext` / `ScopedUiResourceContext` / `ScriptUiBridge` 混合大小写别名以及 `std::shared_ptr<UIElement>` 兼容桥 `AdoptSharedOwnership` / `CustomControlFactory`。

### 1.2 反射、侵入式引用计数与核心对象底座（`core` + `runtime`）

* **`AeroObject` 侵入式引用计数根基类（`include/aero/core/AeroObject.hpp`）**：取代分散的手写计数，通过 `std::atomic<std::uint32_t> refCount_` 提供 `AddRef()`、`Release()`、`RefCount()` 与 `OnDestroy()` 生命周期回调，配套 `IntrusivePtr<T>` 与 `MakeIntrusive<T>(...)` 零控制块二次分配智能指针（规范名 `"Aero.Core.AeroObject"`）。
* **零 RTTI 反射注册表与 O(1) 继承判定（`include/aero/core/TypeClass.hpp`）**：
  * 提供 `AERO_DECLARE_CLASS(Self, Base)` 与 `AERO_IMPLEMENT_CLASS(Self, Base, CanonicalName)` 宏完成 `TypeClass` 静态定义与工厂关联；
  * 通过 `aero::ScopedTypeBatch`（RAII 守卫，支持嵌套 `batchDepth_`）在批量注册类型或别名时暂缓 DFS 遍历，在最外层作用域退出时一次性重算所有 `TypeClass` 的 `[treeIn, treeOut]` 欧拉序区间；
  * `AeroObject::IsA(const TypeClass*)`、`AeroObject::IsA<T>()` 与 `AeroObject::DynamicCast<T>()` 均通过 `TypeClass::IsDerivedFrom` 执行 O(1) 无分支整数区间比对，100% 兼容 MSVC `/GR-` 与 Clang/GCC `-fno-rtti`。
* **`UIElement : public AeroObject` 与 `RareElementData`（`include/aero/core/UIElement.hpp`）**：合并原有的 `FrameworkElement` 职责，注册 `"FrameworkElement"` / `"Aero.UI.FrameworkElement"` 反射别名（自身规范名 `"Aero.UI.UIElement"`）；高频属性内联驻留在 `FastInlineStorage`（`kFastInlinePropertySlotCount = 12u`，56B，align 4），而极少使用的冷字段（`name`、`declaredTypeName`、`cachedNativeHandle`、`boundWrapperRef`、`pinnedWrapperRoot`、`boundRegistry`、`boundUiHeap`、`boundBindingEngine`、`boundStyleEngine`、`boundAnimationEngine`、`keyDownCallback`、`keyUpCallback`、`events` 等）全部收敛进 `RareElementData`，经 `EnsureRareData()` 按需懒分配，空读或置空（如 `Bind*Engine(nullptr)`）不会触发 `rareData_` 分配。`DispatchLoadedSubtree(bool skipNestedUserControls = true)` 直接从视觉树根节点安全派发 `Loaded` 生命周期事件。
* **表驱动 `PropertyMetadata`（`include/aero/core/DependencyProperty.hpp` / `src/core/DependencyProperty.cpp`）**：内置属性枚举涵盖 `PropId_Width (0)` 至 `PropId_EasingExponent (221)`，编译期定长表容量 `kBuiltinPropertyMetadataCount = 222`（`std::array<PropertyMetadata, 222>`），由 `BuildBuiltinPropertyMetadataTable()` 填充 **202** 条显式 `set(PropId_...)` 元数据规则，并经 `static_assert(VerifyBuiltinPropertyMetadataTable())` 编译期自检；同时为契约输入（`0x1000u` 起）与运行时动态自定义依赖属性（`0x2000u..0x2FFFu`）保留映射槽位。
* **视图级上下文 `UIViewContext`（`include/aero/core/UIViewContext.hpp`）**：统一收纳单个视图所需的 8 个非拥有子系统指针（`ExecutionDomainContext*`、`DualTrackBindingEngine*`、`StyleTriggerEngine*`、`AnimationEngine*`、`DepthBucketLayoutQueue*`、`InputEventRouter*`、`PagedRenderTree*`、`UIResourceContext*`），替代散落成员指针并保持节点与单视图上下文的清晰边界。

### 1.3 资产管线、编译前端物理拆分与上下文资源管理（`asset` + `compiler` + `UIResourceContext`）

* **Tier 1 `src/asset/` 物理拆分（原单体 `AxbFormat.cpp` 解构为 6 个职责单一编译单元 + `AxbTemplateRegistry.cpp`）**：
  * [`GradientBrush.hpp`](../include/aero/asset/GradientBrush.hpp) / `src/asset/GradientBrush.cpp`：`GradientStop`、`GradientBrushKind`、`GradientSpreadMethod`、`BrushMappingMode`、`GradientBrushDesc`、`GradientBrushRegistry` 与 256-Texel 渐变采样插值；
  * [`MtsdfPathBaker.hpp`](../include/aero/asset/MtsdfPathBaker.hpp) / `src/asset/MtsdfPathBaker.cpp`：`AxbMtsdfPathEntry`、SVG 路径分词、贝塞尔扁平化与自适应 MTSDF 烘焙（`BakeSvgPathToMtsdfEntry`、`MeasureSvgPathBounds`、`ConvertPointsToSvgPath`、`PackMtsdfAtlasSkyline`）；
  * [`RiveDecoder.hpp`](../include/aero/asset/RiveDecoder.hpp) / `src/asset/RiveDecoder.cpp`：`IsRiveBinaryStream` 与 Rive v7 LEB128 `.riv` 二进制原生解码器 `DecodeRiveBinaryToAxbPackage`；
  * [`ValueConverters.hpp`](../include/aero/asset/ValueConverters.hpp) / `src/asset/ValueConverters.cpp`：`ValueConverterKind`、`ParseValueConverterKind`、`ApplyBuiltinValueConverter`、`EvaluateMathConverterExpression` 与 `FormatBindingString`；
  * `src/asset/XamlLiteralParser.cpp`：387 项内置 `PropId_*` 映射表、XAML 字面量解析与 `ParseBuiltinLiteralPropertyValue` / `ResolvePropertyIdByName`；
  * [`AxbFormat.hpp`](../include/aero/asset/AxbFormat.hpp) / `src/asset/AxbFormat.cpp`：核心 `.axb4` 二进制包编解码、价值池钉扎与 `BindAgainstProgram`，聚合包含上述 4 个细粒度资产头以保持调用方兼容；
  * [`AxbTemplateRegistry.hpp`](../include/aero/asset/AxbTemplateRegistry.hpp) / `src/asset/AxbTemplateRegistry.cpp`：运行期模板包、样式、控件模板与动态资源订阅注册表。
* **Tier 4 `src/compiler/` 物理拆分（原单体 `AeroCompiler.cpp` 解构为 3 个编译单元 + 内部头）**：
  * `src/compiler/AeroCompilerInternal.hpp`（`namespace aero::compiler_detail`）：跨编译单元共享内部常量、零分配 `ParseFloat32`、`CanonicalizeEcsComponentName` 与前向声明；
  * `src/compiler/FrontendImporters.cpp`：`PopulateSceneIrFromPackage` 与 4 个 `IFrontendImporter` 实现（`SvgTokenFrontendImporter`、`LottieFrontendImporter`、`RiveFrontendImporter`、`XamlFrontendImporter`）；
  * `src/compiler/AeroAotCppEmitter.cpp`：`AeroCompiler::GenerateAotCppFromPackage` 与 C++17 静态视觉树/绑定/故事板 AOT 代码发射器；
  * `src/compiler/AeroCompiler.cpp`、`src/compiler/CSharpCompatLowerer.cpp` 与 `src/compiler/XamlApplication.cpp`：核心 XAML/CSS 编译解析、C# 语法降级与 WPF `Application` 加载装配。
* **`UIResourceContext` 与 `ScopedUIResourceContext`（`include/aero/core/UIResourceContext.hpp` / `src/core/UIResourceContext.cpp`）**：通过 `struct Impl` Pimpl 隔离持有 `AxbTemplateRegistry`、`FontAtlas`、`ImageAtlas`、`GradientBrushRegistry` 与 Rive/XAML 资源检索目录（提供 `BindToDomains` 绑定执行域）；`ScopedUIResourceContext` 提供基于 `thread_local` 栈的活动上下文 RAII 守卫，实现多实例资源作用域无锁隔离。

### 1.4 正交控件继承体系、模板内容宿主与 `TypeRegistry` 工厂化（`controls`）

AeroGUI 2.0 控件层按照正交职责与单一继承原则重构（`EnsureControlTypesRegistered()` 共注册 **71** 个独立控件 `TypeClass` 及 **22** 条标准 XAML 类型别名，外加 `core/` 中的 `AeroObject` 与 `UIElement`），避免“大杂烩基类”与虚假类型伪装：

| 控件基类 / 家族 | 所属头文件 | 核心继承关系与成员控件（`TypeClass` 注册） |
| :--- | :--- | :--- |
| **基础与容器外壳** | `aero/controls/Control.hpp` | `Control : UIElement`（派生 `Thumb`、`Separator`）-> `ContentControl` -> `HeaderedContentControl`（派生 `GroupBox`、`Expander`）；内含供 `ButtonBase` 与 `MenuItem` 复用的 `ScriptClickBinding` |
| **装饰器与窗口代理** | `aero/controls/Decorators.hpp` | `Decorator : UIElement`（派生 `Border`、`BoxShadow`、`Viewbox` -> `LayoutScaler`）；`UserControl : ContentControl` |
| **布局面板与浮层宿** | `aero/controls/Panels.hpp` | `Panel : UIElement`（派生 `StackPanel`、`DockPanel`、`WrapPanel`、`Grid`、`Canvas`、`UniformGrid`）；`ScrollViewer : ContentControl`、`Popup : Control`、`ToolTip : ContentControl`、`Window : ContentControl` |
| **矢量几何图元** | `aero/controls/Shapes.hpp` | `Shape : UIElement`（派生 `Rectangle`、`Ellipse`、`Line`、`Path`、`Polygon`、`Polyline`） |
| **按钮与可切换族** | `aero/controls/Buttons.hpp` | `ButtonBase : ContentControl`（派生 `Button`、`RepeatButton`、`Hyperlink`、`ToggleButton` -> `CheckBox`、`RadioButton`） |
| **数值范围与分割器** | `aero/controls/RangeControls.hpp` | `RangeBase : Control`（派生 `Slider`、`ProgressBar`）；`GridSplitter : Thumb` |
| **排版与可编辑文本** | `aero/controls/TextControls.hpp` | 静态文本 `TextBlock : UIElement` 与内容标签 `Label : ContentControl` 正交解耦；可编辑控件独立继承 `TextBoxBase : Control`（派生 `TextBox`、`PasswordBox`），承接光标、选区与 IME 能力 |
| **条目、菜单与选择器** | `aero/controls/ItemsControls.hpp` | `ContentPresenter : Control`；`ItemsControl : Control`（派生 `TreeView`、`StatusBar`、`HeaderedItemsControl` -> `ToolBar`/`MenuItem`/`TreeViewItem`、`MenuBase` -> `Menu`/`ContextMenu`、`Selector` -> `ComboBox`/`TabControl`/`ListBox` -> `ListView`）；**`ListBoxItem : ContentControl`、`ComboBoxItem : ContentControl`、`ListViewItem : ListBoxItem`**、`TabItem : HeaderedContentControl`、`StatusBarItem : ContentControl` |
| **图像与多媒体矢量** | `aero/controls/MediaControls.hpp` | `Image : UIElement`、`MediaElement : UIElement`、`RiveControl : Control` |
| **模板挂载器** | `aero/controls/AxbTemplateMounter.hpp` | `.axb4` 指令流实例化、`TypeRegistry` 驱动控件工厂分派、类型批量注册与 `ControlTemplate` / `MergedDictionaries` 装配（外加聚合总入口 `aero/controls/Controls.hpp`） |

* **`ContentControl` 与 `Border` 职责正位**：`ContentTemplate` 自动物化托管（`contentTemplateChild_`、`SyncContentTemplateChild()`、`SyncTemplateContentHost()`、`OnPropertyChanged` 自动同步与 `MarkExtraValuesInPools`）由 `Border` 迁回 `ContentControl`；新增虚钩子 `HostsDirectContentTemplateChild()`（默认 `true`，在 `ButtonBase`、`HeaderedContentControl`、`Label`、`ScrollViewer`、`ToolTip` 上返回 `false`，在 `UserControl` 与 `Window` 上返回 `!templateMounted_`）。`Border` 回归纯 `Decorator`，彻底移除内容模板字段与虚假 `Matches*` 祖先伪装；`AxbTemplateMounter.cpp` 挂载同步循环统一调用 `el->SyncTemplateContentHost()`。
* **真实条目容器与 `ItemsControl` 引擎访问器统一**：`ItemsControl::CreateMaterializedItemVisual` 直接实例化真实的 `ListViewItem` / `ListBoxItem` / `ComboBoxItem` / `ContentControl`（取代早期的 `Border` + 类型名伪装）；`ItemsControl` 内部统一通过 `EffectiveDomains()`、`EffectiveBindingEngine()`、`EffectiveStyleEngine()`、`EffectiveAnimationEngine()` 回退至 `Bound*Engine()` / `UIViewContext`。
* **`TypeRegistry` 驱动控件实例化与模块化编译单元**：`EnsureControlTypesRegistered()` 在 `ScopedTypeBatch` 中注册 71 个控件 `TypeClass`、短名称别名及 22 个标准 XAML 别名（如 `Page -> UserControl`、`RichTextBox -> TextBox`、`VirtualizingStackPanel -> StackPanel`、`TabPanel`/`ToolBarOverflowPanel -> WrapPanel`、`ScrollBar`/`TickBar -> Control`、`ItemsPresenter -> ItemsControl` 等），`CreateElementByTypeNameCore` 直接通过 `TypeRegistry::Instance().FindByName(shortName)` 调用 `TypeClass::createInstance` 完成工厂化创建（保留 `ToolBarPanel` 水平 `StackPanel` 预设、`IntrusiveCustomControlFactory`、`AxbTemplateRegistry` `UserControl` 与 `UIElement` 回退）。控件实现物理拆分为 10 个家族头文件与 7 个 `.cpp` 编译单元（`ControlContainers.cpp`、`ControlPanelsShapes.cpp`、`ControlButtonsRanges.cpp`、`ControlTextMedia.cpp`、`ControlItems.cpp`、`ControlRenderEmitters.cpp`、`AxbTemplateMounter.cpp`，共享 `ControlsInternal.hpp`）。

---

## 2. CMake 分层目标体系（NoesisGUI 风格构建分层）与全栈模块目录

### 2.1 CMake 分层链接目标（`CMakeLists.txt`）

| CMake 目标 | 导出别名 (`EXPORT AeroGUI2Targets`) | 目标类型 | 核心包含实现单元与职责 | 链接依赖与系统库边界 |
| :--- | :--- | :---: | :--- | :--- |
| `aerogui_kernel` | `AeroGUI::Kernel` | `STATIC` | 30 个无头内核 TU：`runtime/`(2)、`core/`(6)、`asset/`(7)、`layout/`(1)、`binding/`(1)、`controls/`(7)、`input/`(1)、`animation/`(1)、`style/`(1)、`render/{PagedRenderTree,FontAtlas,ImageAtlas}.cpp`(3) | `RealScript::SlimRuntime` (+ `Threads::Threads`)；**零 `user32`/`gdi32`/`imm32`** |
| `aerogui_render` | `AeroGUI::Render` | `STATIC` | 2 个渲染后端 TU：`src/render/ZeroCopyGpuStream.cpp`、`src/render/GpuBackend.cpp` | `aerogui_kernel` (+ `${CMAKE_DL_LIBS}` on Unix)；**零 `user32`/`gdi32`/`imm32`** |
| `aerogui_app_host` | `AeroGUI::AppHost` | `STATIC` | 3 个应用宿主与桥接 TU：`src/bridge/ScriptUIBridge.cpp`、`src/host/HostDrivers.cpp`、`src/host/NativeWindowHost.cpp` | `aerogui_render` (+ `user32 gdi32 imm32` on `WIN32`，MSVC `/STACK:8388608`) |
| `aerogui_runtime` | `AeroGUI::Runtime` | `INTERFACE` | 纯运行时聚合目标（`AERO_RUNTIME_SOURCES` 汇总全部 35 个运行时 TU，供 `aerogui_slim_runtime_tests` 以 `/GR-` / `-fno-rtti` 全量校验） | `INTERFACE aerogui_app_host` |
| `aerogui_compiler_lib` | `AeroGUI::Compiler` | `STATIC` | 5 个编译器前端/AOT TU：`AeroCompiler.cpp`、`FrontendImporters.cpp`、`AeroAotCppEmitter.cpp`、`CSharpCompatLowerer.cpp`、`XamlApplication.cpp` | `aerogui_runtime` + `RealScript::Frontend` |
| `aerocore` | `AeroGUI::Core` | `INTERFACE` | 全栈兼容聚合目标（开启工具链时聚合 `aerogui_runtime + aerogui_compiler_lib`；Wasm / Slim 模式别名 `aerogui_runtime`） | `aerogui_runtime` (+ `aerogui_compiler_lib`) |

### 2.2 全栈模块目录结构

```text
AeroGUI2/
├── CMakeLists.txt           # 模块化构建脚本（Kernel/Render/AppHost/Runtime/Compiler/Core、5 个 CTest 套件）
├── cmake/                   # AeroGUI2Config.cmake.in、toolchains/EmscriptenWasm.cmake
├── include/aero/
│   ├── drawing/             # [Tier 0] Geometry.hpp (纯 32-bit float 几何、Transform3D 与 Matrix2D/Matrix4x4)
│   ├── core/                # [Tier 0] RsAliases, StringUtils, UnifiedValue(16B + UnifiedValueEquals),
│   │                        # TypeClass(64-bit SymbolId + ScopedTypeBatch), AeroObject(IntrusivePtr),
│   │                        # DependencyProperty(FastInlineStorage 56B + CompactPropertyEntry 32B, 222 槽/202 条元数据),
│   │                        # UIElement(RareElementData + DispatchLoadedSubtree + 虚能力钩子), UIViewContext, UIResourceContext(Pimpl)
│   ├── runtime/             # [Tier 0] AeroExecutionDomains(SimDomain/UiDomain + 异步任务帧), AeroObservableList
│   ├── asset/               # [Tier 1] AxbFormat(.axb4 Opcodes 1..11 + 扩展段) 聚合 GradientBrush, MtsdfPathBaker,
│   │                        # RiveDecoder, ValueConverters; AxbTemplateRegistry(运行期模板包注册与 URI 查找缓存)
│   ├── layout/              # [Tier 2] DepthBucketLayoutQueue (64 层位图扫描深度桶增量 Measure/Arrange 队列)
│   ├── animation/           # [Tier 2] AnimationEngine (11x3 缓动、C1 二阶弹簧、MakeFloat4InlineFast 与 MOT6 多层状态机)
│   ├── style/               # [Tier 2] StyleTriggerEngine (BasedOn 样式、Property/Data/Event/Interaction 触发器与 VSM)
│   ├── binding/             # [Tier 2] DualTrackBindingEngine (Track A/B + RegisterDomainCleanupHook), PropertyPathUtils
│   ├── input/               # [Tier 2] TextInputMethod(ITextInputMethodHost + ImeCompositionController),
│   │                        # InputEventRouter (64 槽零分配输入环、2D/3D 逆投影命中测试、滚轮冒泡与焦点导航)
│   ├── render/              # [Tier 2] FontAtlas(TTF+Cardano SDF+AGFS), ImageAtlas(DEFLATE+PNG/JPG/BMP/TGA+Skyline),
│   │                        # PagedRenderTree(4KB CommandPage + 9 PrimTypes), PagedRenderTreeInternal(ControlRenderEmitter),
│   │                        # ZeroCopyGpuStream, GpuBackend
│   ├── host/                # [Tier 2] HostDrivers(HostDriverBase + EmbeddedGame/DesktopEvent/WebCanvas + IME/WebFont), NativeWindowHost
│   ├── controls/            # [Tier 3] 10 个分家族头文件(Control, Decorators, Panels, Shapes, Buttons, RangeControls,
│   │                        # TextControls, ItemsControls[含 ListBoxItem/ComboBoxItem/ListViewItem], MediaControls,
│   │                        # AxbTemplateMounter) + 总入口 Controls.hpp
│   ├── bridge/              # [Tier 4] ScriptUIBridge (UIElement <-> NativeHandle O(1) 互转、域清理钩子注册与可观察槽位桥接)
│   └── compiler/            # [Tier 4] AeroSceneIR, AeroCompiler, CSharpCompatLowerer, XamlApplication
├── src/                     # 纯 C++17 模块化实现（asset/ 7 TU、controls/ 7 TU + ControlsInternal.hpp、
│                            # compiler/ 5 TU + AeroCompilerInternal.hpp，严格遵守 5-Tier DAG）
├── tools/                   # aero_compiler (CLI 编译工具)、aero_viewer (交互预览器)
├── Samples/                 # 23 个目录（22 个示例工程 + Theme 主题目录，157 个 .xaml / 118 个常规回归 .xaml、9 个 .riv）
├── tests/                   # 5 个核心 CTest 套件：wpf_controls_and_script_tests、samples_mainwindow_tests、
│                            # samples_completeness_tests、samples_golden_regression_tests、aerogui_slim_runtime_tests
└── docs/                    # ARCHITECTURE_DESIGN.md、CODING_STYLE.md、SAMPLES_BENCHMARK_REPORT.md
```

---

## 3. 阶段里程碑实装矩阵

| 阶段里程碑 | 核心交付内容 | 回归测试套件 | 状态 |
| :--- | :--- | :--- | :---: |
| **Phase 1 ~ 6** | 统一 ABI v2.0 底座、双轨绑定、`DepthBucketLayoutQueue`、4KB `PagedRenderTree`、`MOT6` 多层状态机与多前端 IR | 原 `phase1` ~ `phase6` 套件（已整合至全栈回归测试） | 已实装 |
| **P0-1 ~ P0-4** | 非对称圆角/镂空边框 SDF、`OpenGlDeviceBackend` + `NativeWindowHost`、纯 C++17 `.ttf` Cardano SDF `FontAtlas`、RFC1951 + PNG/JPG/BMP/TGA `ImageAtlas` | 原 `p0_1` ~ `p0_4` 套件（已整合至全栈回归测试） | 已实装 |
| **P1-1 ~ P1-5** | `CTP1` `ControlTemplate`、`MR12` 跨文件资源字典与 `{noesis:Loc}` 热切换、`GradientBrushRegistry` + 几何图元/复合控件、`AP14` `MultiBinding`/`MathConverter`/交互行为、3D 透视单应性 + `Blur/DropShadow` + Rive v7 `.riv` 解码器 | 原 `p1_1` ~ `p1_5` 套件（已整合至全栈回归测试） | 已实装 |
| **P2-3** | 架构文档同步至 ABI v2.0、`CMakeLists.txt` CTest 工程化闭环与 `Samples/` 多场景黄金像素回归基准 | 原 `p2_3` 套件（已并入 `samples_*_tests`） | 已实装 |
| **5-Tier 单向分层、物理模块化与 CMake 目标分层重构（当前阶段）** | 5-Tier DAG 反向依赖清零（`Geometry.hpp`、`StringUtils.hpp`、`UnifiedValueEquals`、`UIResourceContext` Pimpl、`TextInputMethod.hpp`、`HostDriverBase`、`PropertyPathUtils.hpp`、`RegisterDomainCleanupHook`、`DispatchLoadedSubtree`、移除废弃转发头）、`src/asset/` 与 `src/compiler/` 物理拆分、`ContentControl` 与 `Border` 职责正位、真实 `ListBoxItem`/`ComboBoxItem`/`ListViewItem`、`ScriptClickBinding`、`TypeRegistry` 工厂化，以及 NoesisGUI 风格 CMake 分层目标（`Kernel`/`Render`/`AppHost`/`Runtime`/`Compiler`/`Core`） | `wpf_controls_and_script_tests`、`samples_mainwindow_tests`、`samples_completeness_tests`、`samples_golden_regression_tests`、`aerogui_slim_runtime_tests`（MSVC `/GR-` / Clang-GCC `-fno-rtti` 无 RTTI 全量运行时校验） | 已实装 |
