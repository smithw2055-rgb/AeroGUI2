# AeroGUI 3.0 (`v3-microkernel`) 全量示例与编译器优化基准报告

> 测试环境：Windows x64 Release (`/GR-` zero-RTTI, 32-bit `float` UI 坐标域) · `AeroCompilerV3`（`[TreeShake]`/`[OPT]`/`compiler_v3_tests` 使用 `treeShakeStdlib = true`；全量 `[BENCH]`/`[SUBPAGE]`/交互测试在 `BuildDiscoveredSample` 中使用 `treeShakeStdlib = false` 以完整保留运行期 `XamlReader`/动态反射所需的 `Aero.UI` MIR；磁盘 stdlib 缓存启用）· `SoftwareRasterizer` CPU 无头光栅化（Table 2/3 为 `320x180` RGBA8 + 64-bit FNV-1a 像素哈希）

## 1. 总览与优化成果汇总

| 验证维度 | 覆盖数量 | 通过数量 | 通过率 | 关键实测指标 |
| :--- | ---: | ---: | ---: | :--- |
| 全量迁移工程 (`23` 经典示例 + `5` 个 V3 原生示例) | 28 | 28 | 100.0% | 零错误编译、挂载、布局与多色无头渲染（`treeShakeStdlib = false`） |
| `Gallery/Data/Samples/*.xaml` 独立子页面 | 6 | 6 | 100.0% | `Brushes`, `Blending`, `Effects`, `Button`, `Slider`, `Canvas` 全部通过（`591`–`29,539` 独立色） |
| 交互式 MVVM / 命令 / 输入 / 动画 / 3D / 模糊 / 主题回归 | 15 | 15 | 100.0% | 全部真实交互路径（含 `BackgroundBlur`、`Gallery`、`Menu3D` 与无反射绕过 `Login`）及像素/状态变更通过 |
| 零 RTTI C++17 AOT 纯运行时测试 (`v3_slim_no_rtti_and_aot_tests`) | 1 | 1 | 100.0% | `rs_aot::ExecutionContext` + `VisualWorld` + `NativeWindowHost`（零 `Interpreter`、`/GR-`）通过 (`0.13s`)；CLI `--O2 --aot-cpp` + `cl.exe` 探针默认跳过（需 `AERO_TEST_ENABLE_AOT_OPT=1`） |

### 1.1 `AeroCompilerV3` 编译期优化实测对比

#### MIR 可达性剪枝（`treeShakeStdlib = true`）与字节码包体积缩减

| 基准场景 | 未剪枝 `.rsb` | 剪枝后 `.rsb` | 体积降幅 | 保留 / 剪枝 MIR 类型 | 保留 / 剪枝 MIR 函数 | 实测来源 |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `TreeShake Probe`（最小 `<Window>` XAML） | `21,817,876 B` (`21,306.52 KB`) | `5,034,868 B` (`4,916.86 KB`) | `-76.9%` (降至 `23.1%`) | `215` kept / `308` pruned (`523` total) | `1,325` kept / `2,631` pruned (`3,956` total) | `compiler_v3_tests` `[TreeShake]` |
| `Samples/Buttons`（完整标准示例工程） | `21,658.66 KB` | `6,243.23 KB` | `-71.2%` (降至 `28.8%`) | `266` kept / `263` pruned (`529` total) | `1,651` kept / `2,334` pruned (`3,985` total) | `v3_legacy_samples_tests` `[OPT]` |

#### 进程内 / 持久化 Stdlib 缓存冷热编译耗时对比

| 基准场景 | 冷编译 (`cold_ms`) | 热缓存编译 (`warm`) | 加速比 | 耗时降幅 | 实测来源 |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `compiler_v3_tests` (`CompilerSession=nullptr`, `usedStdlibCache=1`) | `9,210 ms` | `5,275 ms` | **1.75x** | `-42.7%` | `compiler_v3_tests` `[StdlibCache]` |
| `Samples/Buttons` (`allowDiskCache=false` -> `true`) | `17,391.67 ms` | `286.50 ms` | **60.70x** | `-98.4%` | `v3_legacy_samples_tests` `[OPT]` |

#### C++17 AOT 零 RTTI 测试说明（`v3_slim_no_rtti_and_aot_tests`）

- 默认 CTest 运行期间，`TestLiveAotExecutionAndHeadlessRenderWithoutInterpreter` 在 MSVC `/GR-`（零 RTTI）与纯 `rs_aot::ExecutionContext`（无 `Interpreter`）下实测通过（总耗时 `0.13s`），覆盖 `CanSleepIdle()`、指针点击命中、动画播放与像素更新验证。
- 端到端命令行 `--O2 --aot-cpp` + `cl.exe` 编译探针受环境变量 `AERO_TEST_ENABLE_AOT_OPT=1` 控制，默认纯解释器回归中按设计 `[Skip]`，本报告不列未在本轮实测的外部 `cl.exe` 耗时与 `.obj` 体积数据。

## 2. 全量 28 个应用端到端无头基准表（23 经典示例 + 5 个 V3 示例）

> 注：本表由 `TestAll23LegacyAnd5V3Samples`（`BuildDiscoveredSample`：`treeShakeStdlib = false`，保留全部 `Aero.UI` MIR 类型与函数供运行期 `XamlReader` 与控件反射使用；启用进程内 `CompilerSession` 缓存）实测采集。

- **汇总统计（28 个应用）**：
  - 未剪枝 `.rsb` 合计：`636,153.94 KB`（均值 `22,719.78 KB`，最小 `21,267.40 KB` @ `V3/01_CodeFirstCounter`，最大 `27,575.17 KB` @ `WpfQuickStart`）
  - 热编译耗时合计：`153,570.50 ms`（均值 `5,484.66 ms`，最小 `4,289.82 ms` @ `V3/04_VectorAndCanvas`，最大 `8,535.16 ms` @ `WpfQuickStart`）
  - `VisualWorld` 活跃节点总计：`6,673`（均值 `238.32`，最小 `11` @ `CustomRender`，峰值 `1,991` @ `Inventory`）；`PagedRenderTree` 渲染节点总计：`1,567`（均值 `55.96`，最小 `4` @ `Rive`，峰值 `155` @ `TicTacToe`）；4KB 渲染页总计：`719`（均值 `25.68`，最小 `2` @ `Rive`，峰值 `138` @ `TicTacToe`）
  - 布局求解耗时合计：`1.59 ms`（均值 `0.06 ms`，最小 `0.01 ms` @ `CustomRender` 等，峰值 `0.43 ms` @ `Inventory`）；`320x180` CPU 光栅化合计：`268.71 ms`（均值 `9.60 ms`，最快 `0.16 ms` @ `Integration`，最慢 `94.89 ms` @ `Inventory`）

| # | 示例名称 | 入口 XAML / RS 路径 | `.rsb` (KB) | MIR 类型 (保留/总计) | MIR 函数 (保留/总计) | 编译耗时 (ms) | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 4KB 页数 | 布局 (ms) | `320x180` 光栅化 (ms) | 独立色数 | 非空像素率 | 64-bit FNV-1a RGBA 哈希 |
| ---: | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 22074.91 | 531/531 | 3983/3983 | 4599.12 | 67 | 44 | 26 | 0.03 | 0.58 | 34370 | 100.00% | `0xECCA9078C068DF91` |
| 2 | `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 21678.16 | 527/527 | 3974/3974 | 4421.03 | 57 | 38 | 34 | 0.03 | 0.75 | 2284 | 100.00% | `0xC843FDC8B0AB2004` |
| 3 | `Buttons` | `Buttons/Data/MainPage.xaml` | 21658.66 | 529/529 | 3985/3985 | 4552.14 | 47 | 23 | 20 | 0.02 | 4.89 | 1688 | 100.00% | `0xC5FB0D2C98D90797` |
| 4 | `CustomAnimation` | `CustomAnimation/Data/MainPage.xaml` | 22082.78 | 526/526 | 3981/3981 | 4497.18 | 76 | 52 | 20 | 0.08 | 4.79 | 1787 | 100.00% | `0x592A9D76975321A1` |
| 5 | `CustomControl` | `CustomControl/Data/MainPage.xaml` | 21828.54 | 526/526 | 3969/3969 | 4488.99 | 47 | 104 | 11 | 0.02 | 0.39 | 853 | 100.00% | `0x269FAA77CA4ED108` |
| 6 | `CustomRender` | `CustomRender/Data/MainWindow.xaml` | 21517.81 | 524/524 | 3981/3981 | 4422.56 | 11 | 98 | 7 | 0.01 | 0.30 | 594 | 100.00% | `0xEF8AAC72DE468B0A` |
| 7 | `DataBinding` | `DataBinding/Data/MainPage.xaml` | 21817.04 | 533/533 | 4021/4021 | 6792.35 | 261 | 32 | 3 | 0.03 | 3.81 | 7324 | 100.00% | `0xC938331067CC95EA` |
| 8 | `Gallery` | `Gallery/Data/MainWindow.xaml` | 26692.34 | 620/620 | 4165/4165 | 7134.05 | 1325 | 48 | 32 | 0.13 | 34.59 | 1215 | 100.00% | `0xB3C7404D3D0A070C` |
| 9 | `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 21485.07 | 524/524 | 3968/3968 | 4680.78 | 16 | 7 | 6 | 0.01 | 0.88 | 547 | 100.00% | `0xE8CF5076A8015CB1` |
| 10 | `Integration` | `Integration/Data/MainPage.xaml` | 22215.65 | 524/524 | 4017/4017 | 4422.24 | 175 | 43 | 16 | 0.03 | 0.16 | 932 | 100.00% | `0x4F8DE34B63212893` |
| 11 | `IntegrationGLUT` | `IntegrationGLUT/MainWindow.xaml` | 21572.67 | 528/528 | 3989/3989 | 4328.63 | 23 | 64 | 9 | 0.01 | 0.28 | 1566 | 100.00% | `0x8FEDFB5139255634` |
| 12 | `Inventory` | `Inventory/Data/MainPage.xaml` | 23701.93 | 542/542 | 4058/4058 | 6816.58 | 1991 | 33 | 11 | 0.43 | 94.89 | 22032 | 100.00% | `0x8C8DA9BABE26AB2C` |
| 13 | `Localization` | `Localization/Data/MainPage.xaml` | 22073.78 | 538/538 | 4024/4024 | 8340.63 | 101 | 57 | 30 | 0.03 | 1.85 | 2516 | 100.00% | `0x85801DEB0A3EECEF` |
| 14 | `Login` | `Login/Data/MainPage.xaml` | 22089.61 | 529/529 | 3981/3981 | 4703.17 | 60 | 19 | 12 | 0.03 | 0.76 | 883 | 100.00% | `0x2B580FC8DF6FAE9B` |
| 15 | `Menu3D` | `Menu3D/Data/MainPage.xaml` | 24012.45 | 545/545 | 4047/4047 | 4762.82 | 189 | 59 | 23 | 0.05 | 15.41 | 13161 | 100.00% | `0xFD336476EB8C93BB` |
| 16 | `QuestLog` | `QuestLog/Data/MainPage.xaml` | 22557.21 | 537/537 | 4028/4028 | 6980.87 | 283 | 100 | 84 | 0.10 | 1.63 | 11707 | 100.00% | `0x4AFA1A583FAFDE10` |
| 17 | `Rive` | `Rive/Data/MainPage.xaml` | 21578.20 | 532/532 | 3987/3987 | 4533.89 | 39 | 4 | 2 | 0.09 | 0.89 | 147 | 100.00% | `0x161C1C4ED0C5F272` |
| 18 | `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 22648.13 | 545/545 | 4060/4060 | 6981.86 | 233 | 146 | 71 | 0.07 | 48.91 | 4315 | 100.00% | `0x328814725F131371` |
| 19 | `Theme` | `Theme/Data/MainWindow.xaml` | 24074.16 | 567/567 | 4053/4053 | 7087.98 | 558 | 112 | 46 | 0.10 | 7.81 | 158 | 100.00% | `0x9B3F27B8F3145931` |
| 20 | `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 24224.99 | 529/529 | 4008/4008 | 4870.07 | 377 | 155 | 138 | 0.09 | 23.72 | 3626 | 100.00% | `0xA537BFB6F7D15B78` |
| 21 | `UserControl` | `UserControl/Data/MainPage.xaml` | 27385.93 | 528/528 | 4021/4021 | 6502.64 | 84 | 38 | 16 | 0.03 | 1.17 | 1638 | 100.00% | `0xA9EC41DE76DC92C1` |
| 22 | `VideoEffect` | `VideoEffect/Data/MainPage.xaml` | 21570.13 | 530/530 | 4007/4007 | 4864.60 | 14 | 12 | 4 | 0.01 | 10.64 | 4996 | 100.00% | `0x75EF95357CC45F51` |
| 23 | `WpfQuickStart` | `WpfQuickStart/Data/MainWindow.xaml` | 27575.17 | 538/538 | 4050/4050 | 8535.16 | 272 | 78 | 30 | 0.06 | 0.75 | 469 | 100.00% | `0x1DD3FBA0E7D250C6` |
| 24 | `V3/01_CodeFirstCounter` | `V3/01_CodeFirstCounter/Program.rs` | 21267.40 | 530/530 | 3972/3972 | 4466.48 | 30 | 27 | 8 | 0.02 | 0.44 | 763 | 54.08% | `0x2E4FC8068D594609` |
| 25 | `V3/02_CompiledXamlMvvm` | `V3/02_CompiledXamlMvvm/MainWindow.xaml` | 21702.65 | 535/535 | 4013/4013 | 6640.89 | 66 | 34 | 10 | 0.02 | 0.76 | 5 | 100.00% | `0x85A88D669723E0A5` |
| 26 | `V3/03_ControlGallery` | `V3/03_ControlGallery/MainWindow.xaml` | 21829.67 | 525/525 | 3974/3974 | 4464.08 | 198 | 46 | 21 | 0.04 | 0.60 | 693 | 100.00% | `0x64DA764689C1AEB7` |
| 27 | `V3/04_VectorAndCanvas` | `V3/04_VectorAndCanvas/MainWindow.xaml` | 21603.53 | 527/527 | 3969/3969 | 4289.82 | 30 | 57 | 16 | 0.01 | 6.86 | 774 | 95.62% | `0x866BCE52D1EB2831` |
| 28 | `V3/05_DualDomainSimHud` | `V3/05_DualDomainSimHud/MainWindow.xaml` | 21635.37 | 527/527 | 3969/3969 | 4389.89 | 43 | 37 | 13 | 0.01 | 0.20 | 308 | 36.63% | `0xBA994D1FA0C99486` |

## 3. `Gallery` 独立子页面无头编译渲染验证 (`Samples/Gallery/Data/Samples/*.xaml`)

通过共享 `Gallery` 资源与字体搜索路径 (`DiscoverSampleProject`)，以 `treeShakeStdlib = false` 独立编译、步进 5 帧并光栅化 `320x180` RGBA8：

| # | 子页面路径 | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 独立色数 | 状态 |
| ---: | :--- | ---: | ---: | ---: | :--- |
| 1 | `Gallery/Data/Samples/Brushes.xaml` | 322 | 76 | 760 | PASS |
| 2 | `Gallery/Data/Samples/Blending.xaml` | 42 | 19 | 1127 | PASS |
| 3 | `Gallery/Data/Samples/Effects.xaml` | 119 | 42 | 29539 | PASS |
| 4 | `Gallery/Data/Samples/Button.xaml` | 96 | 56 | 1177 | PASS |
| 5 | `Gallery/Data/Samples/Slider.xaml` | 98 | 54 | 591 | PASS |
| 6 | `Gallery/Data/Samples/Canvas.xaml` | 79 | 42 | 993 | PASS |

## 4. 交互式 MVVM、命令绑定、动画、3D 与控件状态验证 (`v3_legacy_samples_tests` 共 15 项)

1. **`BackgroundBlur` (`960x540`)**：校验至少 2 个 `BackdropBlur` `BlurLayerSpan` 及其 `maskDestRect` 非零有效，点击 `TopBlurRadius` / `BottomBlurRadius` 滑块更新 `Effect.radius` 与 `BlurLayerSpan::radiusPx` 并产生真实像素模糊变化 (`CountChangedPixels > 20`)。
2. **`Buttons` (`800x600`)**：点击 `StartButton` 与 `SettingsButton`，验证 `StartCount` / `SettingsCount` 递增、`State` 由 `Menu -> Started -> Settings` 切换及渲染像素变化 (`CountChangedPixels > 0`)。
3. **`DataBinding` (`960x540`)**：校验 10 个太阳系 `ListBoxItem` 与 `Tooltip` 弹窗、`Sun` 直径格式为 `"1380000"`（非科学计数法），以及 `Pluto` 弹窗在 `960x540 -> 540x530 -> 960x540` 窗口缩放下自动重定位不裁剪。
4. **`Gallery` (`1280x800`)**：校验 `SelectorTree` 通过 `HierarchicalDataTemplate` 渲染分类与子样例 `TreeViewItem`；展开 `Media` 后点击 `Brushes` / `Effects` 与展开 `Basic Input` 后点击 `Button`，通过 `XamlReader.LoadUri` 加载真实子 XAML 节点树（非占位色块）并产生显著像素切换；点击 `ThemeColorPicker` 打开 `PART_Popup` 选择 `Dark` 后验证主题像素变化 (`CountChangedPixels > 500`)。
5. **`Integration` (`960x540`)**：校验 3 个 `TabItem` 切换至 `Video` 页、分辨率 `ComboBox` 点击展开 `PART_Popup` 与 `ComboBoxItem` 行高，以及垂直 `ScrollBar` 悬停动画像素变化 (`CountChangedPixels > 50`)。
6. **`Login` (`800x600`)**：严格通过真实 UI 点击 `UserBox` / `PassBox` 注入账号密码文本并点击 `LoginBtn`（无反射绕过回退），验证错误凭据失败提示与正确凭据 `LoginSuccessful == true` 及前后帧像素刷新。
7. **`TicTacToe` (`800x600`)**：等待入场动画后点击 `Cell0`，校验 `Cell0 != 0` 与落子前后像素变化 (`CountChangedPixels > 0`)。
8. **`Inventory` (`800x600`)**：等待 `Anim.Intro` 淡入完成后，校验 `ObservableList` 物品列表绑定、装备 (`EquipFromInventory`) / 卸下 (`UnequipSlot`) 状态更新与渲染像素刷新 (`CountChangedPixels > 0`)。
9. **`QuestLog` (`800x600`)**：校验 `SelectedIndex`、`SelectedQuest.Title` 与 `SelectedQuest.Completed` 绑定、`SelectQuestByIndex(2)` 切换、任务完成态翻转 (`ToggleSelectedQuestCompleted`) 及 `CountChangedPixels > 0`。
10. **`Localization` (`800x600`)**：校验 `Languages` (4 语言)、`SelectLanguageByIndex(1)` 切换至 `Français` (`Language-fr.xaml`) 并触发页面重绘 (`CountChangedPixels > 0`)，以及切换 `Language-ar.xaml` (阿拉伯语 RTL)。
11. **`Menu3D` (`1280x720`)**：校验 `SkyImage` / `MountainsBack` / `Clouds` / `Hills` 视差背景布局与非零可见像素；点击 `Start` 进入 `StartMenu`（切换 `Casual`/`Normal` 激活 `CasualTree`/`NormalTree` 并点击 `Back` 返回）；点击 `Settings` 进入 `SettingsMenu`（验证 `TexDetailContent` `OptionSelector` 经 `NextButton` / `PrevButton` / `Right` 键在 `1 -> 2 -> 1 -> 2` 切换且不扰动 `SoundVol`、`AAlias` `CheckBox` 勾选及 `Esc` 返回 `MainMenu`）；以及 Code-Behind `NavigateToSettings()` / `NavigateToStart()` / `NavigateToMain()` 保持 `VM.ActiveMenu` 与 UI 同步。
12. **`UserControl` (`800x600`)**：校验 `NumericUpDown` 自定义控件与 `SetStrength(25)` 预算上限夹紧 (`10 -> 16`, `PointsAvailable 6 -> 0`)、`ResetStats()` 复位、`IncrementStrength()` 递增 (`10 -> 11`) 与像素刷新。
13. **`WpfQuickStart` (`800x600`)**：点击 `AddTaskButton` 触发 `AddTaskCommand`，验证 `TaskCount` / `TaskRows` / `ActiveTasks` 由 `3` 增至 `4` 且界面像素更新 (`CountChangedPixels > 0`)。
14. **`CustomControl` & `Theme` (`800x600`)**：`CustomControl` 推进 `AdvanceAllClocks(3665)` 校验指针与读数视觉变化；`Theme` 点击切换 `Dark` / `Light` 与 `Accent` 强调色单选并校验像素变化 (`CountChangedPixels > 0`)。
15. **`Scoreboard` (`1280x800`)**：校验 `PART_Track`/`Thumb` 拖拽与悬停高亮回退、滚轮滚动偏移、`VisibleTeam` `ComboBox` 悬停与 `PART_Popup` 展开选择 `Alliance` 列表过滤。

## 5. 测试范围与诚实边界说明 (Honest Scope Notes)

- **运行模式**：表 2、表 3 均在无头模式（`NativeWindowConfig::forceHeadless = true`, `SoftwareRasterizer`）下采集，避免 GPU 驱动差异对 64-bit FNV-1a 像素回归造成影响；编译期热计时在启用持久化 `aero_stdlib_cache` 与 `CompilerSession` 的 Release 进程下测得。
- **外部宿主与媒体能力边界**：`IntegrationGLUT` 的宿主 GLUT 主循环与 `VideoEffect` 外部实时视频解码流在无头测试中以宿主初始化与静态/占位帧路径运行；`Menu3D` 在完成视差背景布局与 3D 透视渲染后以 `100.00%` 非空像素率（`13,161` 独立色）渲染完整主菜单场景。
- **资源与包路径解析**：`DiscoverSampleProject` 注册各示例 `<sampleDir>`、`Data/`、`Images/`、`Fonts/`、`Theme/` 以及祖先级 `Samples/Theme/Data/Theme`，使得 `Gallery/Data/Resources.xaml` 引用的 `/Noesis.GUI.Extensions;component/Theme/NoesisTheme.DarkBlue.xaml` 等主题与字体资源可直接解析并参与渲染。
- **CMake SDK 导出与 ABI 约束**：全部二进制目标及零 RTTI 探针均在 MSVC `/GR-`（严格无 RTTI）与 32-bit `float` UI 呈现域约束下编译通过；`CMakeLists.txt` 已修复 `aerogui_compiler` 的 `AERO_STDLIB_DIR` 为 `$<BUILD_INTERFACE:AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib">` 与 `$<INSTALL_INTERFACE:AERO_STDLIB_DIR="$<INSTALL_PREFIX>/${CMAKE_INSTALL_DATADIR}/aerogui2/stdlib">` 并补齐 `install(DIRECTORY stdlib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/aerogui2/stdlib FILES_MATCHING PATTERN "*.rs")`，导出目标 `AeroGUI::aerogui_compiler` 不再泄漏构建机本地绝对路径，且不修改外部 `C:/Projects/RealScript`。
