# AeroGUI 3.0 (`v3-microkernel`) 全量示例与编译器/运行时优化基准报告

> 测试环境：Windows x64 Release (`/GR-` zero-RTTI, 32-bit `float` UI 坐标域) · `RealScript` Phase 0–3（`RSLB v4` 零拷贝 `.rodata`、`SlotStorage::HybridRecord`、无后备字段自定义事件、分层 `StencilJIT`、`TrustedReleaseOnly` AOT）· `AeroGUI2` Phase 0–4（四包标准库拆分、`CompiledXamlCatalog`、多页纹理图集、4 像素宽 SSE2/NEON `SoftwareRasterizer` 与多线程脏瓦片渲染、双域 `StencilJIT`）

## 1. 总览与优化成果汇总

| 验证维度 | 覆盖数量 | 通过数量 | 通过率 | 关键实测指标 |
| :--- | ---: | ---: | ---: | :--- |
| 全量迁移工程 (`23` 经典示例 + `5` 个 V3 原生示例) | 28 | 28 | 100.0% | `aerogui_stdlib.dll` 惰性模式下零错误编译、挂载、布局与多色无头渲染（编译均值 `373.30 ms`，惰性解码率 `11.88%`） |
| `Gallery/Data/Samples/*.xaml` 独立子页面 | 6 | 6 | 100.0% | `Brushes`, `Blending`, `Effects`, `Button`, `Slider`, `Canvas` 全部通过（`591`–`28,942` 独立色） |
| 交互式 MVVM / 命令 / 输入 / 动画 / 3D / 模糊 / 零泄漏回归 | 18 | 18 | 100.0% | 全部交互路径通过；`Gallery` 9 子页双轮切换 `R1->R2` 节点/对象/内存增量 `0`；3 轮容器复用零泄漏 |
| 渲染宿主与 SIMD / 脏瓦片回归 (`render_host_tests`) | 23 | 23 | 100.0% | 多页图集 Scissor 消除、CPU 像素驱逐懒重载、4 像素 SIMD 位精确混合与 4 线程脏瓦片并行光栅化全部通过 |
| 控件、虚拟化、EditorKit 与双域 `StencilJIT` 回归 | 全量 | 全量 | 100.0% | `v3_controls_and_samples_tests`（含 `05_DualDomainSimHud` UI/Sim 双域独立堆 `StencilJIT`）与 `v3_editorkit_tests` 全部通过 |
| 零 RTTI C++17 AOT 纯运行时测试 (`v3_slim_no_rtti_and_aot_tests`) | 1 | 1 | 100.0% | `rs_aot::ExecutionContext` + `VisualWorld` + `NativeWindowHost`（零 `Interpreter`、`/GR-`）及纯 `AppRuntime` `.arbc` 加载通过 |

### 1.1 `AeroCompilerV3`、四包拆分、AOT 与 `aerogui_stdlib.dll` 实测对比

#### MIR 可达性剪枝（按需属性访问器保留）与输出体积缩减

| 基准场景 | 优化前 / 未剪枝基线 | 当前剪枝 / 精简后 | 降幅 | 保留 / 剪枝 MIR 规模 | 实测来源 |
| :--- | ---: | ---: | ---: | :--- | :--- |
| `TreeShake Probe`（`AeroStdlibPkg_All` 最小 `<Window>`） | `21,065,870 B` (`20,572.14 KB`) | **`5,828,345 B`** (`5,691.74 KB`) | **`-72.3%`** | 类型 `230` 保留 / `341` 剪枝；函数 **`1,327` 保留 / `3,253` 剪枝** | `compiler_v3_tests` `[TreeShake]` |
| `Minimal <Window>`（`AeroStdlibPkg_Auto -> Core`） | `5,755,738 B` (`All` `.arbc`) | **`5,643,403 B`** (`5,511.14 KB` `.arbc`) | 冷编译耗时 **`-43.5%`** | 类型 **`222`** 保留；函数 **`1,282` 保留 / `1,735` 剪枝** | `compiler_v3_tests` `[MinimalWindowPrune]` |
| C++17 AOT 生成源码（最小 `<Window>`） | `50,737,818 B` (`49,548.65 KB`, `FullMultiTier`) | **`19,111,164 B`** (`18,663.25 KB`, `TrustedReleaseOnly`) | **`-62.3%` (`2.65x` 缩小)** | 消除多档模板与逐指令预算计费，共享多态分发与调用签名去重 | `aero-compiler --aot-cpp` |

#### 编译、惰性加载与冷热启动延迟对比

| 基准场景 | 冷路径耗时 | 热缓存 / DLL 耗时 | 加速比 | 关键指标与实测来源 |
| :--- | ---: | ---: | ---: | :--- |
| 源码四包拆分无缓存冷编译 (`All` -> `Auto: Core`) | `12,342 ms` (`all`) | **`6,979 ms`** (`core`) | **1.77x** | 词法级无误判包推断跳过 `Controls` / `RuntimeXaml` / `Workbench` 前端解析 |
| 进程内 `StdlibCache` (`compiler_v3_tests`) | `8,394 ms` | **`3,931 ms`** | **2.14x** | `CompilerSession=nullptr`, `usedStdlibCache=1` |
| `aerogui_stdlib.dll` (`8.03 MB`, `RSLB v4` 零拷贝 `.rodata`) | `5,484.66 ms` (旧源码热均值) | **`373.30 ms`** (28 示例编译+链接均值) | **14.69x** | DLL 零拷贝头解析 `28.08 ms`；运行期平均仅惰性解码 **`11.88%`** (`6.1%–18.7%`) stdlib 函数 |
| `aerogui_stdlib.dll` + 用户 `.arbc` 磁盘缓存 | `326.97 ms` | **`46.79 ms`** | **6.99x** | `v3_stdlib_dll_tests` `[DLL_ARBC_CACHE]` |
| 独立进程 `aero-viewer` 无头运行 (`Buttons`, `800x600`) | 首帧 `1.69 ms` | 稳态 **`0.84 ms/帧` (`1,188.9 FPS`)** | — | 总启动 `373.94 ms`，`17` draw calls，进程常驻集 RSS **`54.29 MB`** |

#### 分层 `StencilJIT` 与内存剖析实测（`v3_stdlib_dll_tests` & `v3_controls_and_samples_tests`）

- **惰性 DLL 分层 `StencilJIT`**：
  - `Buttons`：跟踪 `57` 个函数，热点晋升 **`7` 个原生函数**（`46,720 B` 机器码），原生命中 `31` 次，惰性解码保持 `630/5561`（`88.7%` 冷函数未解码）。
  - `Gallery`：跟踪 `252` 个函数，热点晋升 **`21` 个原生函数**（`73,370 B` 机器码），原生命中 **`201` 次**（回退 `31` 次，热点命中率 **`86.6%`**），惰性解码保持 `1024/5561`（`81.6%` 未解码）。
  - `05_DualDomainSimHud`：`UiPresentation` 与 `SimDeterministic` 双域各自持有独立堆绑定的 `StencilModule`，跨帧 Tick 与 `[SimCommand]` 点击均完成原生晋升与隔离验证。
- **独立进程内存拆解 (`[MEM_BREAKDOWN]`)**：
  - `aero_sample_buttons` (`1000x640`)：RSS `64.65 MB`（私有 `49.94 MB`），`ui_heap = 0.19 MB`（`1,439` 对象），`world = 0.15 MB`（`47` 节点），`render = 0.09 MB`（`23` 节点 / `17` 页）。
  - `aero_sample_gallery` (`1280x760`)：RSS `91.82 MB`（私有 `77.25 MB`），`ui_heap = 0.98 MB`（`6,446` 对象），`world = 0.17 MB`（`215` 节点），`render = 0.18 MB`（`44` 节点 / `9` 页）。
  - `aero_sample_inventory` (`1280x720`)：RSS `95.16 MB`（私有 `80.86 MB`），`ui_heap = 2.80 MB`（`21,094` 对象），`world = 0.79 MB`（`800` 节点），`render = 0.66 MB`（`19` 节点 / `3` 页）。

## 2. 全量 28 个应用端到端无头基准表（`aerogui_stdlib.dll` 模式）

> 注：本表由 `v3_legacy_samples_tests --stdlib-dll` 实测采集（用户模块 MIR 计数列为各示例自身的用户级 MIR 类型与函数；标准库通过 `aerogui_stdlib.dll` `RSLB v4` 按需惰性链接）。
> - **28 应用汇总对比（相较 Phase 0–4 重构前基线）**：
>   - 编译+链接耗时合计：**`10,271.50 ms`**（均值 **`366.84 ms`**，最小 `188.40 ms` @ `V3/01_CodeFirstCounter`，较旧源码热均值 `5,484.66 ms` 提速 **14.95x**）。
>   - `VisualWorld` 活跃节点总计：**`4,068`**（均值 `145.29`，较重构前 `6,673` 下降 **`-39.0%`**；其中 `Gallery` `1,325 -> 215` **`-83.8%`**，`Inventory` `1,991 -> 800` **`-59.8%`**）。
>   - 4KB 渲染命令页总计：**`365`**（均值 `13.04`，较重构前 `719` 页下降 **`-49.2%`**；其中 `Gallery` `32 -> 2` 页 **`-93.8%`**，`BackgroundBlur` `26 -> 4` 页 **`-84.6%`**，`Inventory` `11 -> 4` 页 **`-63.6%`**）。
>   - `320x180` CPU 光栅化合计：**`227.19 ms`**（均值 **`8.11 ms`**；其中 `Gallery` `34.59 -> 3.98 ms` **8.69x**，`Inventory` `94.89 -> 18.20 ms` **5.21x**，`Menu3D` `15.41 -> 4.36 ms` **3.53x**，`Scoreboard` `48.91 -> 18.90 ms` **2.59x**）。

| # | 示例名称 | 入口 XAML / RS 路径 | 用户 MIR 类型 | 用户 MIR 函数 | 编译+链接 (ms) | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 4KB 页数 | 布局 (ms) | `320x180` 光栅化 (ms) | 独立色数 | 非空像素率 | 64-bit FNV-1a RGBA 哈希 |
| ---: | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 220 | 30 | 300.09 | 67 | 40 | 4 | 0.03 | 11.59 | 32354 | 100.00% | `0x272E4FBC121FED14` |
| 2 | `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 195 | 14 | 242.03 | 57 | 39 | 13 | 0.02 | 5.44 | 3834 | 100.00% | `0xF79C93F38F31D037` |
| 3 | `Buttons` | `Buttons/Data/MainPage.xaml` | 204 | 32 | 245.41 | 47 | 23 | 16 | 0.03 | 4.44 | 1829 | 100.00% | `0x8A4B4CAB55F8488B` |
| 4 | `CustomAnimation` | `CustomAnimation/Data/MainPage.xaml` | 200 | 29 | 419.92 | 76 | 158 | 14 | 0.06 | 5.79 | 1754 | 100.00% | `0x988D4D398DC46CE8` |
| 5 | `CustomControl` | `CustomControl/Data/MainPage.xaml` | 184 | 16 | 320.13 | 47 | 104 | 5 | 0.02 | 4.02 | 846 | 100.00% | `0xD5DD4F61CECAA9C9` |
| 6 | `CustomRender` | `CustomRender/Data/MainWindow.xaml` | 171 | 40 | 248.28 | 11 | 350 | 12 | 0.01 | 13.55 | 5058 | 100.00% | `0x2312385BD640C483` |
| 7 | `DataBinding` | `DataBinding/Data/MainPage.xaml` | 217 | 69 | 334.34 | 260 | 21 | 20 | 0.03 | 11.45 | 7324 | 100.00% | `0x6FCF4084E9943057` |
| 8 | `Gallery` | `Gallery/Data/MainWindow.xaml` | 348 | 220 | 1115.15 | 215 | 33 | 2 | 0.06 | 3.98 | 1209 | 100.00% | `0x0A9B72C796F03D32` |
| 9 | `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 182 | 15 | 237.37 | 16 | 7 | 4 | 0.01 | 0.71 | 299 | 100.00% | `0x41AAEA8C2F7ABC14` |
| 10 | `Integration` | `Integration/Data/MainPage.xaml` | 194 | 64 | 370.49 | 175 | 43 | 11 | 0.08 | 3.56 | 925 | 100.00% | `0xB212DEBEDC8BA6FB` |
| 11 | `IntegrationGLUT` | `IntegrationGLUT/MainWindow.xaml` | 184 | 36 | 235.46 | 23 | 41 | 7 | 0.02 | 7.52 | 1893 | 100.00% | `0xF01A9F81316A54BC` |
| 12 | `Inventory` | `Inventory/Data/MainPage.xaml` | 254 | 106 | 589.58 | 800 | 21 | 4 | 0.53 | 18.20 | 22082 | 100.00% | `0xD8C2FF854D3A677B` |
| 13 | `Localization` | `Localization/Data/MainPage.xaml` | 241 | 72 | 348.71 | 83 | 57 | 25 | 0.03 | 9.70 | 2541 | 100.00% | `0x070DA6D54DC6CF9A` |
| 14 | `Login` | `Login/Data/MainPage.xaml` | 225 | 28 | 321.35 | 62 | 21 | 6 | 0.04 | 9.78 | 939 | 100.00% | `0xF7E5AA5426E9AE31` |
| 15 | `Menu3D` | `Menu3D/Data/MainPage.xaml` | 270 | 94 | 505.22 | 114 | 44 | 17 | 0.06 | 4.36 | 13499 | 100.00% | `0xC5865DBD922E32AF` |
| 16 | `QuestLog` | `QuestLog/Data/MainPage.xaml` | 238 | 76 | 412.69 | 283 | 99 | 31 | 0.14 | 10.04 | 11713 | 100.00% | `0x7A9244E0FFF3141D` |
| 17 | `Rive` | `Rive/Data/MainPage.xaml` | 200 | 34 | 225.54 | 43 | 9 | 9 | 0.04 | 18.48 | 12721 | 100.00% | `0xF4F6D1B86F175D37` |
| 18 | `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 247 | 108 | 451.69 | 220 | 146 | 25 | 0.10 | 18.90 | 4682 | 100.00% | `0x9193611AB8541E05` |
| 19 | `Theme` | `Theme/Data/MainWindow.xaml` | 268 | 100 | 649.40 | 433 | 67 | 10 | 0.06 | 11.93 | 1222 | 100.00% | `0xDC18BAFE16619B05` |
| 20 | `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 218 | 55 | 617.50 | 377 | 145 | 47 | 0.13 | 9.38 | 3250 | 100.00% | `0xC9EF8513331AACBE` |
| 21 | `UserControl` | `UserControl/Data/MainPage.xaml` | 207 | 68 | 325.69 | 84 | 46 | 10 | 0.03 | 6.16 | 1519 | 100.00% | `0x389C6840F4E8FF9E` |
| 22 | `VideoEffect` | `VideoEffect/Data/MainPage.xaml` | 208 | 54 | 255.65 | 14 | 11 | 3 | 0.01 | 13.16 | 5518 | 100.00% | `0x3187BF91575ADE4A` |
| 23 | `WpfQuickStart` | `WpfQuickStart/Data/MainWindow.xaml` | 235 | 98 | 438.41 | 240 | 56 | 16 | 0.09 | 3.63 | 1203 | 100.00% | `0x3262D343079500F9` |
| 24 | `V3/01_CodeFirstCounter` | `V3/01_CodeFirstCounter/Program.rs` | 179 | 19 | 188.40 | 30 | 27 | 7 | 0.01 | 7.66 | 738 | 54.08% | `0x7874A426B2A3F221` |
| 25 | `V3/02_CompiledXamlMvvm` | `V3/02_CompiledXamlMvvm/MainWindow.xaml` | 207 | 61 | 277.14 | 78 | 23 | 10 | 0.19 | 5.23 | 1056 | 67.88% | `0xB7719C586EC2C689` |
| 26 | `V3/03_ControlGallery` | `V3/03_ControlGallery/MainWindow.xaml` | 201 | 21 | 339.06 | 140 | 46 | 18 | 0.07 | 2.48 | 1113 | 71.51% | `0x74DB1F672918F779` |
| 27 | `V3/04_VectorAndCanvas` | `V3/04_VectorAndCanvas/MainWindow.xaml` | 184 | 16 | 229.62 | 30 | 19 | 8 | 0.02 | 2.82 | 1270 | 100.00% | `0x8F745CEE5F98281B` |
| 28 | `V3/05_DualDomainSimHud` | `V3/05_DualDomainSimHud/MainWindow.xaml` | 165 | 16 | 226.91 | 43 | 30 | 11 | 0.02 | 3.23 | 900 | 78.73% | `0xB9ACE3E4E1F0EA3F` |

## 3. `Gallery` 独立子页面无头编译渲染验证 (`Samples/Gallery/Data/Samples/*.xaml`)

| # | 子页面路径 | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 独立色数 | 状态 |
| ---: | :--- | ---: | ---: | ---: | :--- |
| 1 | `Gallery/Data/Samples/Brushes.xaml` | 190 | 43 | 1205 | PASS |
| 2 | `Gallery/Data/Samples/Blending.xaml` | 42 | 25 | 1308 | PASS |
| 3 | `Gallery/Data/Samples/Effects.xaml` | 119 | 9 | 28942 | PASS |
| 4 | `Gallery/Data/Samples/Button.xaml` | 96 | 56 | 1392 | PASS |
| 5 | `Gallery/Data/Samples/Slider.xaml` | 98 | 54 | 591 | PASS |
| 6 | `Gallery/Data/Samples/Canvas.xaml` | 79 | 42 | 993 | PASS |

## 4. 交互式 MVVM、命令绑定、动画、3D 与零泄漏回归 (`v3_legacy_samples_tests` 共 18 项)

1. **`BackgroundBlur` (`960x540`)**：校验 `BackdropBlur` `BlurLayerSpan` 及滑块拖拽实时更新模糊半径与像素变化。
2. **`Buttons` (`800x600`)**：点击 `StartButton` / `SettingsButton`，校验命令计数、状态切换与像素刷新。
3. **`DataBinding` (`960x540`)**：校验太阳系 `ListBoxItem`、`Tooltip` 弹窗格式化及窗口缩放自动重定位。
4. **`Gallery` (`1280x800`) + 9 子页双轮零泄漏 (`R1->R2`) + 3 轮容器惰性实例化回归**：校验 `SelectorTree`、`XamlReader.LoadUri`、`ThemeColorPicker` 切换，并实测 `ComboBox` / `ListBox` / `ListView` / `TreeView` / `TabControl` / `Menu` / `ContextMenu` / `Canvas` / `Expander` 连续两轮导航 `delta nodes=0, brushes=0, uiObjs=0, uiKB=0, privMB=0.0`，3 轮容器复用 `c2_nodes == c3_nodes == 573`, `c2_objs == c3_objs == 15218`。
5. **`Integration` (`960x540`)**：校验 `TabItem` 切换、`ComboBox` 弹窗展开与 `ScrollBar` 悬停动画。
6. **`Login` (`800x600`)**：严格通过真实 UI 点击输入账号密码与提交验证（无反射绕过）。
7. **`TicTacToe` (`800x600`)**：入场动画后点击落子与状态/像素更新验证。
8. **`Inventory` (`800x600`)**：`ObservableList` 装备/卸下绑定与像素刷新验证。
9. **`QuestLog` (`800x600`)**：选中项切换、完成态翻转与详情面板同步验证。
10. **`Localization` (`800x600`)**：法语 (`Language-fr.xaml`) 与阿拉伯语 RTL (`Language-ar.xaml`) 动态资源字典切换验证。
11. **`Menu3D` (`1280x720`)**：视差背景、3D 透视菜单切换、`OptionSelector` 键盘/按钮交互及 `Esc` 返回验证。
12. **`UserControl` (`800x600`)**：`NumericUpDown` 自定义控件、点数上限夹紧与点击同步验证。
13. **`WpfQuickStart` (`800x600`)**：`AddTaskCommand` 列表新增与计数同步验证。
14. **`CustomControl` & `Theme` (`800x600`)**：自定义绘制时钟推进与主题/强调色实时切换验证。
15. **`Scoreboard` (`1280x800`)**：滚动条拖拽、滚轮偏移与 `ComboBox` 阵营过滤验证。
16. **`Rive` / `CustomRender` / `BrushShaders`**：矢量状态机/数字输入路径、粒子破碎复位与自定义画刷着色器验证。

## 5. 测试范围与边界说明

- **运行模式**：端到端像素哈希与光栅化耗时均在无头模式（`NativeWindowConfig::forceHeadless = true`, 4 像素宽 SSE2/NEON `SoftwareRasterizer` + `ScanlineWorkerPool` 4 线程分带）下采集，确保跨驱动一致性。
- **CMake SDK 导出与 ABI 约束**：全部运行时二进制目标均在 MSVC `/GR-`（严格无 RTTI）与 32-bit `float` UI 呈现域约束下编译通过；`aerogui_stdlib` 数据 DLL 采用 `/utf-8 /GR- /Od /Ob0` 纯 `.rodata` 64 字节对齐布局，零编译警告。
