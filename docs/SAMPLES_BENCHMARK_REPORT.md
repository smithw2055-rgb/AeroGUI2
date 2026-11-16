# AeroGUI 3.0 (`v3-microkernel`) 全量示例与编译器优化基准报告

> 测试环境：Windows x64 Release (`/GR-` zero-RTTI, 32-bit `float` UI 坐标域) · `AeroCompilerV3` (`treeShakeStdlib = true`, 磁盘 stdlib 缓存启用) · `SoftwareRasterizer` CPU 无头光栅化 (`320x180` RGBA8, 64-bit FNV-1a 像素哈希)

## 1. 总览与优化成果汇总

| 验证维度 | 覆盖数量 | 通过数量 | 通过率 | 关键实测指标 |
| :--- | ---: | ---: | ---: | :--- |
| 全量迁移工程 (`23` 经典示例 + `5` 个 V3 原生示例) | 28 | 28 | 100.0% | 零错误编译、挂载、布局与非均匀多色无头渲染 |
| `Gallery/Data/Samples/*.xaml` 独立子页面 | 6 | 6 | 100.0% | `Brushes`, `Blending`, `Effects`, `Button`, `Slider`, `Canvas` 全部通过 (`>= 36` 独立色) |
| 交互式 MVVM / 命令 / 输入 / 主题切换回归 | 10 | 10 | 100.0% | 全部状态变更与前后帧像素差异均验证通过 |
| 零 RTTI C++17 AOT 编译与纯 AOT 无头执行 | 1 | 1 | 100.0% | `aero-compiler --O2 --aot-cpp` + `cl.exe /std:c++17 /GR-` + `rs_aot::ExecutionContext` 全通过 |

### 1.1 `AeroCompilerV3` 编译期优化与 AOT 实测对比

#### MIR 可达性剪枝（Tree-Shaking）与字节码包体积缩减

| 基准场景 | 未剪枝 `.rsb` | 剪枝后 `.rsb` | 体积降幅 | 保留 / 剪枝 MIR 类型 | 保留 / 剪枝 MIR 函数 | 实测来源 |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `TreeShake Probe`（最小 XAML 工程） | `18,984,064 B` (`18,539.13 KB`) | `4,260,441 B` (`4,160.59 KB`) | `-77.6%` (降至 `22.4%`) | `175` kept / `274` pruned (`449` total) | `1,303` kept / `1,989` pruned (`3,292` total) | `compiler_v3_tests` `[TreeShake]` |
| `Samples/Buttons`（完整标准示例工程） | `18,865.41 KB` | `6,460.58 KB` | `-65.8%` (降至 `34.2%`) | `279` kept / `175` pruned (`454` total) | `1,897` kept / `1,423` pruned (`3,320` total) | `v3_legacy_samples_tests` `[OPT]` |

#### 进程内 / 持久化 Stdlib 缓存冷热编译耗时对比

| 基准场景 | 冷编译 (`cold_ms`) | 热缓存编译 (`warm`) | 加速比 | 耗时降幅 | 实测来源 |
| :--- | ---: | ---: | ---: | ---: | :--- |
| `compiler_v3_tests` (`CompilerSession=nullptr`, `usedStdlibCache=1`) | `16,985 ms` | `6,138 ms` | **2.77x** | `-63.9%` | `compiler_v3_tests` `[StdlibCache]` |
| `Samples/Buttons` (`allowDiskCache=false` -> `true`) | `19,850.55 ms` | `6,517.95 ms` | **3.05x** | `-67.2%` | `v3_legacy_samples_tests` `[OPT]` |

#### C++17 AOT 代码生成与 `/GR-` 原生编译（`v3_slim_no_rtti_and_aot_tests`）

| 阶段 / 指标 | 实测耗时 (ms) | 产出大小 (Bytes) | 备注 |
| :--- | ---: | ---: | :--- |
| `aero-compiler --O2 --aot-cpp` (`AotProbeUi`) | `13,116 ms` | `33,383,137 B` (`.cpp`, 含默认开启的 MIR Tree-Shaking) | 同组字节码包：`4` 个模块，`4,443,147 B` |
| `cl.exe /nologo /bigobj /c /std:c++17 /GR- /EHsc` | `88,814 ms` | `82,680,029 B` (`.obj`) | 严格关闭 MSVC C++ RTTI (`_CPPRTTI` 未定义) |
| 运行时执行 (`TestLiveAotExecutionAndHeadlessRenderWithoutInterpreter`) | - | 零字节码虚拟机 (`Interpreter = nullptr`) | 验证 `CanSleepIdle()`、指针点击命中、动画播放与像素更新 |

> 诚实边界说明：`v3_slim_no_rtti_and_aot_tests` 仅针对开启 `--O2` 与默认 `treeShakeStdlib=true` 的 C++17 AOT 链路采集了直接计时；未在同一二进制中另行测量未剪枝 (`treeShakeStdlib=false`) 的 C++17 AOT 对比耗时，因此本报告仅列出实测值，不做未实测的倍速推算。

## 2. 全量 28 个应用端到端无头基准表（23 经典示例 + 5 个 V3 示例）

- **汇总统计（28 个应用）**：
  - 剪枝后 `.rsb` 合计：`210,507.12 KB`（均值 `7,518.11 KB`，最小 `4,424.11 KB` @ `CustomRender`，最大 `14,512.47 KB` @ `Gallery`）
  - 热编译耗时合计：`273,814.55 ms`（均值 `9,779.09 ms`，最小 `5,935.29 ms` @ `V3/04_VectorAndCanvas`，最大 `18,571.06 ms` @ `Theme`）
  - `VisualWorld` 活跃节点总计：`7,142`（均值 `255.1`，峰值 `3,727` @ `Inventory`）；`PagedRenderTree` 渲染节点总计：`1,754`（均值 `62.6`，峰值 `177` @ `Scoreboard` / `TicTacToe`）；4KB 渲染页总计：`342`（均值 `12.2`）
  - 布局求解耗时合计：`1.25 ms`（均值 `0.04 ms`，峰值 `0.42 ms` @ `Inventory`）；`320x180` CPU 光栅化合计：`306.72 ms`（均值 `10.95 ms`，最快 `1.54 ms` @ `HelloWorld` / `Rive`，最慢 `102.75 ms` @ `Inventory`）

| # | 示例名称 | 入口 XAML / RS 路径 | 剪枝 `.rsb` (KB) | MIR 类型 (保留/总计) | MIR 函数 (保留/总计) | 热编译 (ms) | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 4KB 页数 | 布局 (ms) | `320x180` 光栅化 (ms) | 独立色数 | 非空像素率 | 64-bit FNV-1a RGBA 哈希 |
| ---: | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 7439.97 | 296/457 | 1991/3319 | 6540.94 | 65 | 36 | 4 | 0.03 | 4.92 | 1423 | 100.00% | `0xDB652185CB26DCFB` |
| 2 | `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 6407.34 | 276/453 | 1862/3310 | 5949.84 | 57 | 44 | 3 | 0.02 | 6.38 | 2051 | 100.00% | `0x16F37500759D09A9` |
| 3 | `Buttons` | `Buttons/Data/MainPage.xaml` | 6460.58 | 279/454 | 1897/3320 | 6326.48 | 47 | 28 | 2 | 0.01 | 4.42 | 1594 | 100.00% | `0x147569F3C276F009` |
| 4 | `CustomAnimation` | `CustomAnimation/Data/MainPage.xaml` | 7262.93 | 283/452 | 1954/3317 | 6294.28 | 76 | 50 | 17 | 0.02 | 4.68 | 1860 | 100.00% | `0x63D37D2F23B56ADC` |
| 5 | `CustomControl` | `CustomControl/Data/MainPage.xaml` | 5595.11 | 198/452 | 1582/3304 | 6261.46 | 47 | 99 | 8 | 0.02 | 2.05 | 502 | 100.00% | `0x0D680C1C8D014558` |
| 6 | `CustomRender` | `CustomRender/Data/MainWindow.xaml` | 4424.11 | 179/450 | 1361/3317 | 6165.89 | 11 | 98 | 5 | 0.01 | 2.95 | 349 | 100.00% | `0x4F0AE337116AB098` |
| 7 | `DataBinding` | `DataBinding/Data/MainPage.xaml` | 7230.33 | 297/459 | 2054/3357 | 14847.49 | 461 | 33 | 3 | 0.03 | 24.78 | 5493 | 100.00% | `0xFA0900E55346FD5E` |
| 8 | `Gallery` | `Gallery/Data/MainWindow.xaml` | 14512.47 | 412/546 | 2402/3501 | 17691.40 | 203 | 45 | 12 | 0.03 | 22.90 | 526 | 100.00% | `0x7B4D0AD7915BED3B` |
| 9 | `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 6116.56 | 270/450 | 1821/3304 | 6269.17 | 16 | 11 | 1 | 0.01 | 1.54 | 191 | 100.00% | `0x916986083A386852` |
| 10 | `Integration` | `Integration/Data/MainPage.xaml` | 7076.82 | 227/450 | 1755/3353 | 6195.73 | 330 | 124 | 26 | 0.05 | 4.77 | 2399 | 100.00% | `0x02861E776D15FD05` |
| 11 | `IntegrationGLUT` | `IntegrationGLUT/MainWindow.xaml` | 4789.69 | 199/454 | 1441/3325 | 6695.86 | 23 | 64 | 9 | 0.01 | 7.81 | 731 | 100.00% | `0x585E12BC27525914` |
| 12 | `Inventory` | `Inventory/Data/MainPage.xaml` | 9906.04 | 325/468 | 2254/3394 | 15543.25 | 3727 | 133 | 9 | 0.42 | 102.75 | 200 | 100.00% | `0x2541FD5612E3FF26` |
| 13 | `Localization` | `Localization/Data/MainPage.xaml` | 7932.64 | 305/464 | 2157/3360 | 15762.35 | 65 | 41 | 13 | 0.02 | 5.59 | 2455 | 100.00% | `0x06A63EA606B3B18A` |
| 14 | `Login` | `Login/Data/MainPage.xaml` | 7625.37 | 300/455 | 2076/3317 | 6630.31 | 60 | 34 | 5 | 0.04 | 4.55 | 959 | 100.00% | `0x767380BB41E7A9BA` |
| 15 | `Menu3D` | `Menu3D/Data/MainPage.xaml` | 10196.14 | 327/471 | 2247/3382 | 7002.97 | 116 | 25 | 5 | 0.02 | 1.97 | 1009 | 17.65% | `0xFE3470D2AED77B1C` |
| 16 | `QuestLog` | `QuestLog/Data/MainPage.xaml` | 8353.19 | 310/463 | 2100/3364 | 15811.62 | 76 | 36 | 8 | 0.03 | 7.89 | 6018 | 100.00% | `0x9930F8292F3E19D4` |
| 17 | `Rive` | `Rive/Data/MainPage.xaml` | 6508.23 | 292/458 | 1926/3323 | 6906.75 | 39 | 10 | 2 | 0.10 | 1.54 | 102 | 100.00% | `0x6F5E23442DAC208C` |
| 18 | `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 8443.12 | 318/471 | 2150/3396 | 16905.87 | 298 | 177 | 61 | 0.10 | 17.62 | 3413 | 100.00% | `0xA40D851F99C2AEC0` |
| 19 | `Theme` | `Theme/Data/MainWindow.xaml` | 12658.61 | 348/528 | 2171/3424 | 18571.06 | 265 | 127 | 26 | 0.07 | 22.09 | 289 | 100.00% | `0x88759A5D0A7A9411` |
| 20 | `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 9627.59 | 302/455 | 2107/3344 | 6975.17 | 377 | 177 | 6 | 0.06 | 9.65 | 3382 | 100.00% | `0xCB380205335574F5` |
| 21 | `UserControl` | `UserControl/Data/MainPage.xaml` | 7908.78 | 285/454 | 2036/3357 | 8601.10 | 84 | 46 | 6 | 0.02 | 6.88 | 1606 | 100.00% | `0x4BF06904908B48A6` |
| 22 | `VideoEffect` | `VideoEffect/Data/MainPage.xaml` | 6408.74 | 290/456 | 1928/3343 | 6365.13 | 14 | 12 | 2 | 0.01 | 8.94 | 7360 | 100.00% | `0x55DDFD327EFC9D70` |
| 23 | `WpfQuickStart` | `WpfQuickStart/Data/MainWindow.xaml` | 9280.70 | 271/464 | 1952/3386 | 18464.52 | 250 | 75 | 31 | 0.04 | 4.61 | 192 | 100.00% | `0x4FD2EFF40FE03D22` |
| 24 | `V3/01_CodeFirstCounter` | `V3/01_CodeFirstCounter/Program.rs` | 4737.99 | 207/456 | 1418/3308 | 6082.26 | 30 | 28 | 8 | 0.01 | 5.97 | 199 | 100.00% | `0xF3B3776237AB91FE` |
| 25 | `V3/02_CompiledXamlMvvm` | `V3/02_CompiledXamlMvvm/MainWindow.xaml` | 6424.09 | 238/459 | 1730/3347 | 15867.88 | 84 | 42 | 14 | 0.02 | 5.23 | 175 | 100.00% | `0xFBC8B211A5D2BCDE` |
| 26 | `V3/03_ControlGallery` | `V3/03_ControlGallery/MainWindow.xaml` | 7319.88 | 246/451 | 1813/3310 | 6804.19 | 248 | 62 | 30 | 0.03 | 5.35 | 202 | 100.00% | `0x7D4B4F0846DBEE13` |
| 27 | `V3/04_VectorAndCanvas` | `V3/04_VectorAndCanvas/MainWindow.xaml` | 5160.17 | 204/453 | 1586/3305 | 5935.29 | 30 | 60 | 13 | 0.01 | 6.03 | 591 | 100.00% | `0x787505EFC68D652C` |
| 28 | `V3/05_DualDomainSimHud` | `V3/05_DualDomainSimHud/MainWindow.xaml` | 4699.93 | 188/453 | 1379/3305 | 6346.29 | 43 | 37 | 13 | 0.01 | 2.86 | 229 | 100.00% | `0x3229EB2068E53F0D` |

## 3. `Gallery` 独立子页面无头编译渲染验证 (`Samples/Gallery/Data/Samples/*.xaml`)

通过共享 `Gallery` 资源与字体搜索路径 (`DiscoverSampleProject`)，独立编译、步进 5 帧并光栅化 `320x180` RGBA8：

| # | 子页面路径 | `VisualWorld` 节点 | `PagedRenderTree` 节点 | 独立色数 | 状态 |
| ---: | :--- | ---: | ---: | ---: | :--- |
| 1 | `Gallery/Data/Samples/Brushes.xaml` | 282 | 71 | 36 | PASS |
| 2 | `Gallery/Data/Samples/Blending.xaml` | 42 | 19 | 425 | PASS |
| 3 | `Gallery/Data/Samples/Effects.xaml` | 118 | 40 | 194 | PASS |
| 4 | `Gallery/Data/Samples/Button.xaml` | 96 | 62 | 1681 | PASS |
| 5 | `Gallery/Data/Samples/Slider.xaml` | 98 | 50 | 185 | PASS |
| 6 | `Gallery/Data/Samples/Canvas.xaml` | 80 | 42 | 230 | PASS |

## 4. 交互式 MVVM、命令绑定与控件状态验证 (`v3_legacy_samples_tests`)

1. **`Buttons` (`800x600`)**：点击 `StartButton` 与 `SettingsButton`，验证 `StartCount` / `SettingsCount` 递增、`State` 由 `Menu -> Started -> Settings` 切换及渲染像素变化 (`CountChangedPixels > 0`)。
2. **`Login` (`800x600`)**：向 `UserBox` / `PassBox` 注入文本并触发 `LoginBtn`（及回退验证 `LoginWithPassword`），校验 `LoginSuccessful == true` 与界面像素刷新 (`CountChangedPixels > 0`)。
3. **`TicTacToe` (`800x600`)**：等待入场动画后点击 `Cell0`，校验 `Cell0 != 0` 与落子前后像素变化 (`CountChangedPixels > 0`)。
4. **`Inventory` (`800x600`)**：校验 `ObservableList` 物品列表绑定、装备 (`EquipFromInventory`) / 卸下 (`UnequipSlot`) 状态更新与渲染像素刷新 (`CountChangedPixels > 0`)。
5. **`QuestLog` (`800x600`)**：校验 `SelectedIndex`、`SelectedQuest.Title` 与 `SelectedQuest.Completed` 绑定、`SelectQuestByIndex(2)` 切换、任务完成态翻转 (`ToggleSelectedQuestCompleted`) 及 `CountChangedPixels > 0`。
6. **`Localization` (`800x600`)**：校验 `Languages` (4 语言)、`SelectLanguageByIndex(1)` 切换至 `Français` (`Language-fr.xaml`) 并触发页面重绘 (`CountChangedPixels > 0`)，以及切换 `Language-ar.xaml` (阿拉伯语 RTL)。
7. **`UserControl` (`800x600`)**：校验 `NumericUpDown` 自定义控件与 `SetStrength(25)` 预算上限夹紧 (`10 -> 16`, `PointsAvailable 6 -> 0`)、`ResetStats()` 复位、`IncrementStrength()` 递增 (`10 -> 11`) 与像素刷新。
8. **`WpfQuickStart` (`800x600`)**：点击 `AddTaskButton` 触发 `AddTaskCommand`，验证 `TaskCount` / `TaskRows` / `ActiveTasks` 由 `3` 增至 `4` 且界面像素更新 (`CountChangedPixels > 0`)。
9. **`CustomControl` (`800x600`)**：调用 `AdvanceAllClocks(3665)` 推进指针与读数，校验时钟视觉变化 (`CountChangedPixels > 0`)。
10. **`Theme` (`800x600`)**：调用 `ToggleTheme()` 翻转 `IsDarkTheme` 并校验明暗主题像素重绘 (`CountChangedPixels > 0`)，随后调用 `NextAccent()` 切换 `CurrentAccent` 强调色。

## 5. 测试范围与诚实边界说明 (Honest Scope Notes)

- **运行模式**：表 2、表 3 均在无头模式（`NativeWindowConfig::forceHeadless = true`, `SoftwareRasterizer`）下采集，避免 GPU 驱动差异对 64-bit FNV-1a 像素回归造成影响；编译期热计时在启用持久化 `aero_stdlib_cache`、单次独立调用 `AeroCompilerV3::Compile` 的 Release 进程下测得。
- **外部宿主与媒体能力边界**：`IntegrationGLUT` 的宿主 GLUT 主循环与 `VideoEffect` 外部实时视频解码流在无头测试中以宿主初始化与静态/占位帧路径运行；`Menu3D` 为居中透视 3D 菜单布局，清屏背景外有效前景覆盖率为 `17.65%` (`1009` 独立色)。
- **资源与包路径解析**：`DiscoverSampleProject` 注册各示例 `<sampleDir>`、`Data/`、`Images/`、`Fonts/`、`Theme/` 以及祖先级 `Samples/Theme/Data/Theme`，使得 `Gallery/Data/Resources.xaml` 引用的 `/Noesis.GUI.Extensions;component/Theme/NoesisTheme.DarkBlue.xaml` 等主题与字体资源可直接解析并参与渲染。
- **编译器与 ABI 约束**：全部二进制目标及 C++17 AOT 探针均在 MSVC `/GR-`（严格无 RTTI）与 32-bit `float` UI 呈现域约束下编译通过，且不修改 `CMakeLists.txt` 或外部 `C:/Projects/RealScript`。
