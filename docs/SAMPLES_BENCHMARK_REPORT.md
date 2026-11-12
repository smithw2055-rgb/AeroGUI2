# AeroGUI 2.0 + RealScript `Samples.zip` 全量基准与黄金像素回归报告

## 1. 全局回归总览与覆盖范围说明

| 指标维度 | 扫描总数 | 通过数量 | 成功率 | 总耗时 (ms) | 平均耗时 (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 官方示例工程目录扫描 (`Samples/*`) | 22 | 22 | 100.0% | - | - |
| `.xaml` 编译 + `.axb4` 往返校验（覆盖全部有 `.xaml` 的工程） | 118 | 118 | 100.0% | 1153.29 | 9.77 |
| `.riv` Rive v7 LEB128 二进制解码 | 9 | 9 | 100.0% | 107.46 | 11.94 |
| 静态/入场动画 XAML 场景 CPU 黄金像素光栅化 (`13` 个工程 / `16` 个场景, `1280x720 -> 320x180`) | 16 | 16 | 100.0% | 617.22 | 38.58 |

### 测试覆盖边界说明（Honest Scope Notes）

- **编译与 `.axb4` 二进制往返校验（21 个官方示例工程目录，116 个 `.xaml` + 9 个 `.riv`）**：全部 21 个官方示例目录均纳入扫描，其中所有 116 个 `.xaml` 资产 100% 通过 `AeroCompiler::Compile` 与 `.axb4` 序列化/反序列化字节级一致性校验，9 个 `.riv` 资产 100% 通过 `RiveImporter::ImportBytesToAxb`。
- **CPU 黄金像素光栅化覆盖（13 个工程共 16 个静态/动画 XAML 场景）**：覆盖 `BackgroundBlur`、`BrushShaders`、`Buttons`、`Gallery`（含 `Gallery` 主窗体、`Brushes`、`Gallery_Blending`、`Effects` 共 4 个场景）、`HelloWorld`、`Inventory`、`Localization`、`Login`、`QuestLog`、`Rive`、`Scoreboard`、`TicTacToe`、`UserControl`。
- **仅做 `.xaml -> .axb4` 编译往返、未纳入纯 CPU 黄金像素光栅化的 8 个示例工程**：`CustomAnimation`、`CustomControl`、`CustomRender`、`DataBinding`、`Integration`、`IntegrationGLUT`、`Menu3D`、`VideoEffect`。这些工程依赖外部宿主 C++ 自定义控件/自定义渲染器/自定义动画类、GLUT/引擎宿主集成循环、C++ Code-Behind 动态 ViewModel 数据源或外部视频流与自定义 Shader 特效。
- **静态 `DataContext` 挂载范围**：CPU 黄金光栅化在推进指定 `settleAnimSeconds` 入场动画后以空/静态 `DataContext`（`ObjectRef{}`）执行确定性软件光栅化，用于锁定 XAML 布局、样式字典、矢量图元、Rive 画板与混合模式的像素回归，不等同于运行示例原始 C++ ViewModel 业务逻辑或原生 GPU 自定义 Shader。

## 2. 16 个静态/动画 XAML 场景 CPU 黄金像素基准表（跨 13 个示例工程，`320x180` RGBA8）

| 场景名称 | XAML 路径 | 字体数 | 渲染节点 | 4KB 页数 | 非空像素数 | 覆盖率 | 独立色彩数 | FNV-1a 64-bit 黄金哈希 | 编译 (ms) | 挂载+样式 (ms) | 布局 (ms) | 建树 (ms) | 光栅化 (ms) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 1 | 15 | 2 | 57600 | 100.0% | 648 | `0x7863b040a6dc4444` | 0.52 | 0.15 | 0.04 | 0.21 | 1.43 |
| `Login` | `Login/Data/MainPage.xaml` | 1 | 24 | 5 | 57600 | 100.0% | 980 | `0xe52312950e1871af` | 2.51 | 2.02 | 0.05 | 0.36 | 2.46 |
| `Buttons` | `Buttons/Data/MainPage.xaml` | 1 | 46 | 3 | 57600 | 100.0% | 1323 | `0x1dc81e85638e4134` | 1.75 | 1.43 | 0.07 | 0.22 | 3.40 |
| `QuestLog` | `QuestLog/Data/MainPage.xaml` | 2 | 52 | 7 | 57489 | 99.8% | 8723 | `0x57a28126d5c058ef` | 3.77 | 2.07 | 0.07 | 46.86 | 11.24 |
| `Inventory` | `Inventory/Data/MainPage.xaml` | 2 | 224 | 22 | 57600 | 100.0% | 8039 | `0xdc85cde6a258c5e5` | 3.88 | 3.47 | 0.25 | 94.80 | 5.73 |
| `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 2 | 55 | 10 | 57600 | 100.0% | 1236 | `0x063ca2044a4f5443` | 4.29 | 0.40 | 0.12 | 0.54 | 6.86 |
| `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 0 | 257 | 11 | 57600 | 100.0% | 4094 | `0xdf6758fc356ab321` | 13.42 | 6.12 | 0.22 | 5.51 | 6.87 |
| `Localization` | `Localization/Data/MainPage.xaml` | 5 | 65 | 27 | 57600 | 100.0% | 2491 | `0xa3d3d035b759cae9` | 1.58 | 0.37 | 0.11 | 10.84 | 3.08 |
| `Gallery` | `Gallery/Data/MainWindow.xaml` | 7 | 64 | 12 | 57600 | 100.0% | 1121 | `0x97982bf0a0b5c10a` | 17.24 | 16.18 | 4.99 | 5.22 | 1.93 |
| `Brushes` | `Gallery/Data/Samples/Brushes.xaml` | 7 | 277 | 51 | 35456 | 61.6% | 1188 | `0xf5fbaa4f98c890f5` | 1.02 | 16.01 | 0.18 | 1.17 | 0.92 |
| `Gallery_Blending` | `Gallery/Data/Samples/Blending.xaml` | 7 | 76 | 26 | 28920 | 50.2% | 3726 | `0xf81782644ae39d27` | 0.34 | 15.31 | 18.91 | 0.17 | 1.19 |
| `Rive` | `Rive/Data/MainPage.xaml` | 0 | 1248 | 32 | 57600 | 100.0% | 7307 | `0xb5434a846c223365` | 0.53 | 16.51 | 0.59 | 2.40 | 49.36 |
| `Effects` | `Gallery/Data/Samples/Effects.xaml` | 7 | 150 | 28 | 39463 | 68.5% | 5493 | `0x625000cc7552dd34` | 0.53 | 15.61 | 0.45 | 0.55 | 1.27 |
| `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 1 | 62 | 8 | 57578 | 100.0% | 27858 | `0x637465c308a7b16c` | 1.82 | 1.48 | 0.07 | 152.47 | 12.38 |
| `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 1 | 59 | 3 | 57600 | 100.0% | 1873 | `0x9ca3061feedf5fd5` | 1.81 | 0.35 | 0.07 | 0.47 | 4.44 |
| `UserControl` | `UserControl/Data/MainPage.xaml` | 1 | 74 | 8 | 57600 | 100.0% | 1559 | `0x261e72f064aeb55c` | 0.72 | 0.51 | 0.04 | 0.12 | 2.83 |

## 3. 全部 9 个官方 Rive v7 (`.riv`) 二进制资产解码统计表

| `.riv` 相对路径 | 原始大小 (B) | `.axb4` 大小 (B) | 画板尺寸 | IR 节点数 | MTSDF 路径数 | `MOT6` 动画片段 | 状态机数 | 解码+往返耗时 (ms) | 状态 |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `Rive/Data/background.riv` | 126154 | 587500 | 1920x1080 | 697 | 126 | 42 | 1 | 35.88 | PASS |
| `Rive/Data/leaves-bl.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 25.51 | PASS |
| `Rive/Data/leaves-br.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 4.28 | PASS |
| `Rive/Data/leaves-tl.riv` | 126129 | 671607 | 1920x1080 | 50 | 10 | 1 | 1 | 4.25 | PASS |
| `Rive/Data/leaves-tr.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 5.45 | PASS |
| `Rive/Data/logo.riv` | 9105 | 67684 | 420x180 | 52 | 17 | 1 | 1 | 7.44 | PASS |
| `Rive/Data/rewards.riv` | 8560 | 427288 | 500x200 | 72 | 10 | 6 | 1 | 19.59 | PASS |
| `Rive/Data/settings.riv` | 12916 | 62098 | 500x200 | 87 | 12 | 6 | 1 | 2.61 | PASS |
| `Rive/Data/start.riv` | 8918 | 74474 | 500x200 | 79 | 18 | 6 | 1 | 2.45 | PASS |

## 4. 全部 116 个官方 `.xaml` 编译与 `.axb4` 往返明细表

| # | `.xaml` 相对路径 | 指令条数 | 样式数 | `.axb4` 字节数 | 编译耗时 (ms) | 往返校验耗时 (ms) | 结果 |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur/Data/MainPage.xaml` | 243 | 6 | 458547 | 20.52 | 1.05 | PASS |
| 2 | `BackgroundBlur/Data/Resources.xaml` | 2 | 4 | 438437 | 1.25 | 0.79 | PASS |
| 3 | `BrushShaders/Data/MainPage.xaml` | 71 | 3 | 755763 | 15.07 | 2.28 | PASS |
| 4 | `Buttons/Data/MainPage.xaml` | 75 | 2 | 781675 | 7.07 | 2.13 | PASS |
| 5 | `Buttons/Data/Resources.xaml` | 2 | 1 | 769936 | 1.61 | 1.54 | PASS |
| 6 | `CustomAnimation/Data/MainPage.xaml` | 368 | 6 | 113261 | 1.80 | 0.16 | PASS |
| 7 | `CustomControl/Data/MainPage.xaml` | 63 | 2 | 270581 | 3.26 | 0.42 | PASS |
| 8 | `CustomRender/Data/MainWindow.xaml` | 62 | 0 | 2202 | 0.17 | 0.02 | PASS |
| 9 | `DataBinding/Data/MainPage.xaml` | 34 | 2 | 8753 | 0.55 | 0.09 | PASS |
| 10 | `Gallery/Data/MainWindow.xaml` | 392 | 100 | 785204 | 26.52 | 4.03 | PASS |
| 11 | `Gallery/Data/Resources.xaml` | 2 | 92 | 743649 | 13.78 | 1.75 | PASS |
| 12 | `Gallery/Data/Samples/Animation.xaml` | 511 | 11 | 285749 | 4.77 | 0.23 | PASS |
| 13 | `Gallery/Data/Samples/Blending.xaml` | 180 | 0 | 5946 | 0.29 | 0.05 | PASS |
| 14 | `Gallery/Data/Samples/Brushes.xaml` | 561 | 1 | 502169 | 29.71 | 0.39 | PASS |
| 15 | `Gallery/Data/Samples/Button.xaml` | 260 | 4 | 1318358 | 35.07 | 2.77 | PASS |
| 16 | `Gallery/Data/Samples/Canvas.xaml` | 220 | 0 | 7601 | 0.34 | 0.06 | PASS |
| 17 | `Gallery/Data/Samples/CheckBox.xaml` | 178 | 7 | 11817 | 0.43 | 0.10 | PASS |
| 18 | `Gallery/Data/Samples/ComboBox.xaml` | 148 | 0 | 5923 | 0.24 | 0.05 | PASS |
| 19 | `Gallery/Data/Samples/ContextMenu.xaml` | 135 | 1 | 7794 | 0.30 | 0.08 | PASS |
| 20 | `Gallery/Data/Samples/DockPanel.xaml` | 157 | 0 | 5769 | 0.21 | 0.04 | PASS |
| 21 | `Gallery/Data/Samples/Effects.xaml` | 279 | 0 | 110050 | 7.89 | 0.11 | PASS |
| 22 | `Gallery/Data/Samples/Expander.xaml` | 192 | 0 | 204077 | 10.99 | 0.10 | PASS |
| 23 | `Gallery/Data/Samples/Grid.xaml` | 270 | 0 | 9659 | 0.36 | 0.09 | PASS |
| 24 | `Gallery/Data/Samples/GroupBox.xaml` | 143 | 0 | 203066 | 0.28 | 0.08 | PASS |
| 25 | `Gallery/Data/Samples/Hyperlink.xaml` | 90 | 2 | 4183 | 0.23 | 0.04 | PASS |
| 26 | `Gallery/Data/Samples/Image.xaml` | 144 | 0 | 5258 | 0.21 | 0.05 | PASS |
| 27 | `Gallery/Data/Samples/ItemsControl.xaml` | 125 | 0 | 7410 | 0.32 | 0.05 | PASS |
| 28 | `Gallery/Data/Samples/ListBox.xaml` | 189 | 0 | 9071 | 0.33 | 0.08 | PASS |
| 29 | `Gallery/Data/Samples/ListView.xaml` | 54 | 3 | 5141 | 0.30 | 0.05 | PASS |
| 30 | `Gallery/Data/Samples/Menu.xaml` | 289 | 0 | 8689 | 0.30 | 0.07 | PASS |
| 31 | `Gallery/Data/Samples/PasswordBox.xaml` | 98 | 0 | 4084 | 0.19 | 0.03 | PASS |
| 32 | `Gallery/Data/Samples/ProgressBar.xaml` | 207 | 3 | 11363 | 0.44 | 0.10 | PASS |
| 33 | `Gallery/Data/Samples/RadioButton.xaml` | 177 | 8 | 8776 | 0.29 | 0.07 | PASS |
| 34 | `Gallery/Data/Samples/RepeatButton.xaml` | 209 | 1 | 35212 | 0.48 | 0.11 | PASS |
| 35 | `Gallery/Data/Samples/ScrollViewer.xaml` | 175 | 0 | 7707 | 0.33 | 0.06 | PASS |
| 36 | `Gallery/Data/Samples/Slider.xaml` | 315 | 0 | 11765 | 0.35 | 0.10 | PASS |
| 37 | `Gallery/Data/Samples/StackPanel.xaml` | 85 | 0 | 3091 | 0.15 | 0.02 | PASS |
| 38 | `Gallery/Data/Samples/StatusBar.xaml` | 72 | 0 | 2682 | 0.15 | 0.02 | PASS |
| 39 | `Gallery/Data/Samples/TabControl.xaml` | 149 | 0 | 7382 | 0.26 | 0.06 | PASS |
| 40 | `Gallery/Data/Samples/TextBlock.xaml` | 204 | 0 | 8332 | 0.27 | 0.06 | PASS |
| 41 | `Gallery/Data/Samples/TextBox.xaml` | 108 | 0 | 5162 | 0.19 | 0.04 | PASS |
| 42 | `Gallery/Data/Samples/ToggleButton.xaml` | 159 | 4 | 13100 | 0.61 | 0.11 | PASS |
| 43 | `Gallery/Data/Samples/ToolBar.xaml` | 216 | 0 | 11195 | 0.35 | 0.06 | PASS |
| 44 | `Gallery/Data/Samples/ToolTip.xaml` | 97 | 1 | 3892 | 0.19 | 0.04 | PASS |
| 45 | `Gallery/Data/Samples/TreeView.xaml` | 211 | 2 | 9706 | 0.39 | 0.09 | PASS |
| 46 | `Gallery/Data/Samples/UniformGrid.xaml` | 192 | 0 | 6527 | 0.24 | 0.06 | PASS |
| 47 | `Gallery/Data/Samples/WrapPanel.xaml` | 188 | 0 | 6606 | 0.22 | 0.06 | PASS |
| 48 | `HelloWorld/Data/MainPage.xaml` | 95 | 1 | 155635 | 0.79 | 0.11 | PASS |
| 49 | `Integration/Data/MainPage.xaml` | 440 | 0 | 12440 | 0.42 | 0.11 | PASS |
| 50 | `Inventory/Data/MainPage.xaml` | 708 | 13 | 379014 | 34.29 | 0.97 | PASS |
| 51 | `Inventory/Data/Resources.xaml` | 2 | 10 | 60206 | 1.54 | 0.28 | PASS |
| 52 | `Localization/Data/Language-ar.xaml` | 2 | 0 | 1440 | 0.11 | 0.01 | PASS |
| 53 | `Localization/Data/Language-en.xaml` | 2 | 0 | 1035 | 0.06 | 0.01 | PASS |
| 54 | `Localization/Data/Language-fr.xaml` | 2 | 0 | 1094 | 0.05 | 0.01 | PASS |
| 55 | `Localization/Data/Language-jp.xaml` | 2 | 0 | 1144 | 0.05 | 0.01 | PASS |
| 56 | `Localization/Data/MainPage.xaml` | 186 | 10 | 228769 | 1.54 | 0.29 | PASS |
| 57 | `Localization/Data/Resources.xaml` | 2 | 9 | 213318 | 0.97 | 0.16 | PASS |
| 58 | `Login/Data/MainPage.xaml` | 150 | 9 | 793324 | 2.71 | 1.21 | PASS |
| 59 | `Login/Data/Resources.xaml` | 2 | 4 | 775967 | 1.62 | 1.36 | PASS |
| 60 | `Menu3D/Data/MainPage.xaml` | 27 | 6 | 1920054 | 312.19 | 5.69 | PASS |
| 61 | `Menu3D/Data/PagePreviews/PreviewMain.xaml` | 24 | 0 | 928 | 0.17 | 0.01 | PASS |
| 62 | `Menu3D/Data/PagePreviews/PreviewSettings.xaml` | 24 | 0 | 940 | 0.08 | 0.01 | PASS |
| 63 | `Menu3D/Data/PagePreviews/PreviewStart.xaml` | 24 | 0 | 931 | 0.07 | 0.01 | PASS |
| 64 | `Menu3D/Data/Resources.xaml` | 2 | 6 | 1918861 | 6.70 | 5.27 | PASS |
| 65 | `QuestLog/Data/MainPage.xaml` | 298 | 9 | 1572309 | 43.34 | 3.70 | PASS |
| 66 | `QuestLog/Data/Resources.xaml` | 2 | 6 | 829128 | 1.63 | 0.54 | PASS |
| 67 | `Rive/Data/MainPage.xaml` | 100 | 3 | 9650 | 0.55 | 0.12 | PASS |
| 68 | `Scoreboard/Data/MainPage.xaml` | 347 | 11 | 1276197 | 82.03 | 3.62 | PASS |
| 69 | `Theme/Data/App.xaml` | 2 | 81 | 497362 | 11.50 | 1.43 | PASS |
| 70 | `Theme/Data/MainWindow.xaml` | 303 | 0 | 8299 | 0.35 | 0.07 | PASS |
| 71 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkAqua.xaml` | 2 | 0 | 3261 | 0.21 | 0.02 | PASS |
| 72 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkBlue.xaml` | 2 | 0 | 3261 | 0.20 | 0.02 | PASS |
| 73 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkCrimson.xaml` | 2 | 0 | 3261 | 0.18 | 0.02 | PASS |
| 74 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkEmerald.xaml` | 2 | 0 | 3261 | 0.18 | 0.02 | PASS |
| 75 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkGreen.xaml` | 2 | 0 | 3261 | 0.17 | 0.02 | PASS |
| 76 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkLime.xaml` | 2 | 0 | 3261 | 0.18 | 0.02 | PASS |
| 77 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkOrange.xaml` | 2 | 0 | 3261 | 0.17 | 0.02 | PASS |
| 78 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkPurple.xaml` | 2 | 0 | 3261 | 0.17 | 0.02 | PASS |
| 79 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkRed.xaml` | 2 | 0 | 3261 | 0.17 | 0.02 | PASS |
| 80 | `Theme/Data/Theme/NoesisTheme.Brushes.LightAqua.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 81 | `Theme/Data/Theme/NoesisTheme.Brushes.LightBlue.xaml` | 2 | 0 | 3262 | 0.18 | 0.02 | PASS |
| 82 | `Theme/Data/Theme/NoesisTheme.Brushes.LightCrimson.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 83 | `Theme/Data/Theme/NoesisTheme.Brushes.LightEmerald.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 84 | `Theme/Data/Theme/NoesisTheme.Brushes.LightGreen.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 85 | `Theme/Data/Theme/NoesisTheme.Brushes.LightLime.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 86 | `Theme/Data/Theme/NoesisTheme.Brushes.LightOrange.xaml` | 2 | 0 | 3262 | 0.18 | 0.02 | PASS |
| 87 | `Theme/Data/Theme/NoesisTheme.Brushes.LightPurple.xaml` | 2 | 0 | 3262 | 0.17 | 0.02 | PASS |
| 88 | `Theme/Data/Theme/NoesisTheme.Brushes.LightRed.xaml` | 2 | 0 | 3262 | 0.24 | 0.02 | PASS |
| 89 | `Theme/Data/Theme/NoesisTheme.Colors.Dark.xaml` | 2 | 0 | 518 | 0.07 | 0.00 | PASS |
| 90 | `Theme/Data/Theme/NoesisTheme.Colors.Light.xaml` | 2 | 0 | 518 | 0.05 | 0.00 | PASS |
| 91 | `Theme/Data/Theme/NoesisTheme.DarkAqua.xaml` | 2 | 81 | 494540 | 9.87 | 1.38 | PASS |
| 92 | `Theme/Data/Theme/NoesisTheme.DarkBlue.xaml` | 2 | 81 | 494540 | 9.93 | 1.32 | PASS |
| 93 | `Theme/Data/Theme/NoesisTheme.DarkCrimson.xaml` | 2 | 81 | 494543 | 9.70 | 1.30 | PASS |
| 94 | `Theme/Data/Theme/NoesisTheme.DarkEmerald.xaml` | 2 | 81 | 494543 | 9.87 | 1.32 | PASS |
| 95 | `Theme/Data/Theme/NoesisTheme.DarkGreen.xaml` | 2 | 81 | 494541 | 10.03 | 1.37 | PASS |
| 96 | `Theme/Data/Theme/NoesisTheme.DarkLime.xaml` | 2 | 81 | 494540 | 9.83 | 1.32 | PASS |
| 97 | `Theme/Data/Theme/NoesisTheme.DarkOrange.xaml` | 2 | 81 | 494542 | 9.78 | 1.30 | PASS |
| 98 | `Theme/Data/Theme/NoesisTheme.DarkPurple.xaml` | 2 | 81 | 494542 | 9.65 | 1.34 | PASS |
| 99 | `Theme/Data/Theme/NoesisTheme.DarkRed.xaml` | 2 | 81 | 494539 | 9.99 | 1.31 | PASS |
| 100 | `Theme/Data/Theme/NoesisTheme.Fonts.xaml` | 2 | 0 | 255 | 0.13 | 0.00 | PASS |
| 101 | `Theme/Data/Theme/NoesisTheme.LightAqua.xaml` | 2 | 81 | 494542 | 9.75 | 1.31 | PASS |
| 102 | `Theme/Data/Theme/NoesisTheme.LightBlue.xaml` | 2 | 81 | 494542 | 9.84 | 1.32 | PASS |
| 103 | `Theme/Data/Theme/NoesisTheme.LightCrimson.xaml` | 2 | 81 | 494545 | 9.44 | 1.34 | PASS |
| 104 | `Theme/Data/Theme/NoesisTheme.LightEmerald.xaml` | 2 | 81 | 494545 | 9.91 | 1.28 | PASS |
| 105 | `Theme/Data/Theme/NoesisTheme.LightGreen.xaml` | 2 | 81 | 494543 | 9.83 | 1.33 | PASS |
| 106 | `Theme/Data/Theme/NoesisTheme.LightLime.xaml` | 2 | 81 | 494542 | 9.81 | 1.30 | PASS |
| 107 | `Theme/Data/Theme/NoesisTheme.LightOrange.xaml` | 2 | 81 | 494544 | 9.87 | 1.26 | PASS |
| 108 | `Theme/Data/Theme/NoesisTheme.LightPurple.xaml` | 2 | 81 | 494544 | 10.74 | 1.34 | PASS |
| 109 | `Theme/Data/Theme/NoesisTheme.LightRed.xaml` | 2 | 81 | 494541 | 9.89 | 1.34 | PASS |
| 110 | `Theme/Data/Theme/NoesisTheme.Styles.xaml` | 2 | 84 | 484409 | 8.05 | 1.22 | PASS |
| 111 | `TicTacToe/Data/MainPage.xaml` | 1285 | 8 | 4408918 | 176.08 | 19.11 | PASS |
| 112 | `TicTacToe/Data/Resources.xaml` | 2 | 1 | 1398662 | 2.80 | 1.28 | PASS |
| 113 | `UserControl/Data/MainPage.xaml` | 182 | 10 | 13853 | 0.83 | 0.15 | PASS |
| 114 | `UserControl/Data/NumericUpDown.xaml` | 47 | 5 | 12925 | 0.43 | 0.06 | PASS |
| 115 | `UserControl/Data/Resources.xaml` | 2 | 5 | 2516 | 0.18 | 0.03 | PASS |
| 116 | `VideoEffect/Data/MainPage.xaml` | 62 | 1 | 140829 | 0.44 | 0.07 | PASS |
| 117 | `WpfQuickStart/Data/App.xaml` | 2 | 0 | 332 | 0.09 | 0.00 | PASS |
| 118 | `WpfQuickStart/Data/MainWindow.xaml` | 149 | 0 | 5891 | 0.25 | 0.05 | PASS |
