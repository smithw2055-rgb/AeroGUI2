# AeroGUI 2.0 + RealScript `Samples.zip` 全量基准与黄金像素回归报告

## 1. 全局回归总览与覆盖范围说明

| 指标维度 | 扫描总数 | 通过数量 | 成功率 | 总耗时 (ms) | 平均耗时 (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 官方示例工程目录扫描 (`Samples/*`) | 21 | 21 | 100.0% | - | - |
| `.xaml` 编译 + `.axb4` 往返校验（覆盖全部有 `.xaml` 的工程） | 116 | 116 | 100.0% | 2201.86 | 18.98 |
| `.riv` Rive v7 LEB128 二进制解码 | 9 | 9 | 100.0% | 185.47 | 20.61 |
| 静态/入场动画 XAML 场景 CPU 黄金像素光栅化 (`13` 个工程 / `16` 个场景, `1280x720 -> 320x180`) | 16 | 16 | 100.0% | 1376.93 | 86.06 |

### 测试覆盖边界说明（Honest Scope Notes）

- **编译与 `.axb4` 二进制往返校验（21 个官方示例工程目录，116 个 `.xaml` + 9 个 `.riv`）**：全部 21 个官方示例目录均纳入扫描，其中所有 116 个 `.xaml` 资产 100% 通过 `AeroCompiler::Compile` 与 `.axb4` 序列化/反序列化字节级一致性校验，9 个 `.riv` 资产 100% 通过 `RiveImporter::ImportBytesToAxb`。
- **CPU 黄金像素光栅化覆盖（13 个工程共 16 个静态/动画 XAML 场景）**：覆盖 `BackgroundBlur`、`BrushShaders`、`Buttons`、`Gallery`（含 `Gallery` 主窗体、`Brushes`、`Gallery_Blending`、`Effects` 共 4 个场景）、`HelloWorld`、`Inventory`、`Localization`、`Login`、`QuestLog`、`Rive`、`Scoreboard`、`TicTacToe`、`UserControl`。
- **仅做 `.xaml -> .axb4` 编译往返、未纳入纯 CPU 黄金像素光栅化的 8 个示例工程**：`CustomAnimation`、`CustomControl`、`CustomRender`、`DataBinding`、`Integration`、`IntegrationGLUT`、`Menu3D`、`VideoEffect`。这些工程依赖外部宿主 C++ 自定义控件/自定义渲染器/自定义动画类、GLUT/引擎宿主集成循环、C++ Code-Behind 动态 ViewModel 数据源或外部视频流与自定义 Shader 特效。
- **静态 `DataContext` 挂载范围**：CPU 黄金光栅化在推进指定 `settleAnimSeconds` 入场动画后以空/静态 `DataContext`（`ObjectRef{}`）执行确定性软件光栅化，用于锁定 XAML 布局、样式字典、矢量图元、Rive 画板与混合模式的像素回归，不等同于运行示例原始 C++ ViewModel 业务逻辑或原生 GPU 自定义 Shader。

## 2. 16 个静态/动画 XAML 场景 CPU 黄金像素基准表（跨 13 个示例工程，`320x180` RGBA8）

| 场景名称 | XAML 路径 | 字体数 | 渲染节点 | 4KB 页数 | 非空像素数 | 覆盖率 | 独立色彩数 | FNV-1a 64-bit 黄金哈希 | 编译 (ms) | 挂载+样式 (ms) | 布局 (ms) | 建树 (ms) | 光栅化 (ms) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 1 | 15 | 2 | 57600 | 100.0% | 648 | `0x7863b040a6dc4444` | 1.80 | 0.49 | 0.10 | 0.76 | 2.75 |
| `Login` | `Login/Data/MainPage.xaml` | 1 | 24 | 5 | 57600 | 100.0% | 980 | `0xe52312950e1871af` | 7.40 | 5.06 | 0.08 | 0.54 | 8.56 |
| `Buttons` | `Buttons/Data/MainPage.xaml` | 1 | 46 | 3 | 57600 | 100.0% | 1323 | `0x1dc81e85638e4134` | 4.20 | 3.13 | 0.12 | 0.81 | 11.23 |
| `QuestLog` | `QuestLog/Data/MainPage.xaml` | 2 | 52 | 7 | 57489 | 99.8% | 8723 | `0x57a28126d5c058ef` | 10.75 | 5.10 | 0.11 | 47.84 | 18.88 |
| `Inventory` | `Inventory/Data/MainPage.xaml` | 2 | 224 | 22 | 57600 | 100.0% | 8039 | `0xdc85cde6a258c5e5` | 12.05 | 8.08 | 0.48 | 143.22 | 26.62 |
| `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 2 | 55 | 10 | 57600 | 100.0% | 1236 | `0x063ca2044a4f5443` | 14.26 | 1.23 | 0.34 | 1.33 | 30.14 |
| `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 0 | 257 | 11 | 57600 | 100.0% | 4094 | `0xdf6758fc356ab321` | 37.51 | 14.03 | 0.56 | 12.20 | 40.11 |
| `Localization` | `Localization/Data/MainPage.xaml` | 5 | 65 | 27 | 57600 | 100.0% | 2491 | `0xa3d3d035b759cae9` | 6.40 | 1.23 | 0.19 | 6.43 | 15.86 |
| `Gallery` | `Gallery/Data/MainWindow.xaml` | 7 | 64 | 12 | 57600 | 100.0% | 1121 | `0x97982bf0a0b5c10a` | 45.87 | 47.21 | 19.81 | 20.05 | 8.56 |
| `Brushes` | `Gallery/Data/Samples/Brushes.xaml` | 7 | 277 | 51 | 35456 | 61.6% | 1188 | `0xf5fbaa4f98c890f5` | 3.03 | 43.57 | 0.34 | 2.34 | 2.44 |
| `Gallery_Blending` | `Gallery/Data/Samples/Blending.xaml` | 7 | 76 | 26 | 28920 | 50.2% | 3726 | `0xf81782644ae39d27` | 1.40 | 48.04 | 24.50 | 1.29 | 4.28 |
| `Rive` | `Rive/Data/MainPage.xaml` | 0 | 1248 | 32 | 57600 | 100.0% | 7307 | `0xb5434a846c223365` | 3.54 | 56.55 | 1.07 | 4.71 | 115.56 |
| `Effects` | `Gallery/Data/Samples/Effects.xaml` | 7 | 150 | 28 | 39463 | 68.5% | 5493 | `0x625000cc7552dd34` | 2.01 | 49.10 | 27.68 | 1.68 | 3.43 |
| `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 1 | 62 | 8 | 57578 | 100.0% | 27858 | `0x637465c308a7b16c` | 7.27 | 6.07 | 0.11 | 251.43 | 30.32 |
| `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 1 | 59 | 3 | 57600 | 100.0% | 1873 | `0x9ca3061feedf5fd5` | 4.16 | 0.58 | 0.10 | 0.69 | 15.55 |
| `UserControl` | `UserControl/Data/MainPage.xaml` | 1 | 74 | 8 | 57600 | 100.0% | 1559 | `0x261e72f064aeb55c` | 3.07 | 1.71 | 0.07 | 0.20 | 15.58 |

## 3. 全部 9 个官方 Rive v7 (`.riv`) 二进制资产解码统计表

| `.riv` 相对路径 | 原始大小 (B) | `.axb4` 大小 (B) | 画板尺寸 | IR 节点数 | MTSDF 路径数 | `MOT6` 动画片段 | 状态机数 | 解码+往返耗时 (ms) | 状态 |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `Rive/Data/background.riv` | 126154 | 587500 | 1920x1080 | 697 | 126 | 42 | 1 | 58.54 | PASS |
| `Rive/Data/leaves-bl.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 42.55 | PASS |
| `Rive/Data/leaves-br.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 9.95 | PASS |
| `Rive/Data/leaves-tl.riv` | 126129 | 671607 | 1920x1080 | 50 | 10 | 1 | 1 | 12.30 | PASS |
| `Rive/Data/leaves-tr.riv` | 126129 | 671557 | 1920x1080 | 50 | 10 | 1 | 1 | 10.99 | PASS |
| `Rive/Data/logo.riv` | 9105 | 67684 | 420x180 | 52 | 17 | 1 | 1 | 10.70 | PASS |
| `Rive/Data/rewards.riv` | 8560 | 427288 | 500x200 | 72 | 10 | 6 | 1 | 32.30 | PASS |
| `Rive/Data/settings.riv` | 12916 | 62098 | 500x200 | 87 | 12 | 6 | 1 | 4.32 | PASS |
| `Rive/Data/start.riv` | 8918 | 74474 | 500x200 | 79 | 18 | 6 | 1 | 3.82 | PASS |

## 4. 全部 116 个官方 `.xaml` 编译与 `.axb4` 往返明细表

| # | `.xaml` 相对路径 | 指令条数 | 样式数 | `.axb4` 字节数 | 编译耗时 (ms) | 往返校验耗时 (ms) | 结果 |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur/Data/MainPage.xaml` | 243 | 6 | 458272 | 38.07 | 1.77 | PASS |
| 2 | `BackgroundBlur/Data/Resources.xaml` | 2 | 4 | 438287 | 5.25 | 1.36 | PASS |
| 3 | `BrushShaders/Data/MainPage.xaml` | 71 | 3 | 755763 | 30.05 | 2.09 | PASS |
| 4 | `Buttons/Data/MainPage.xaml` | 75 | 2 | 781670 | 16.30 | 2.18 | PASS |
| 5 | `Buttons/Data/Resources.xaml` | 2 | 1 | 769946 | 6.39 | 2.90 | PASS |
| 6 | `CustomAnimation/Data/MainPage.xaml` | 368 | 6 | 113111 | 4.36 | 0.37 | PASS |
| 7 | `CustomControl/Data/MainPage.xaml` | 63 | 2 | 270581 | 8.33 | 1.10 | PASS |
| 8 | `CustomRender/Data/MainWindow.xaml` | 62 | 0 | 2202 | 0.96 | 0.02 | PASS |
| 9 | `DataBinding/Data/MainPage.xaml` | 34 | 2 | 8753 | 1.88 | 0.12 | PASS |
| 10 | `Gallery/Data/MainWindow.xaml` | 392 | 100 | 779279 | 59.18 | 5.86 | PASS |
| 11 | `Gallery/Data/Resources.xaml` | 2 | 92 | 738203 | 38.20 | 3.89 | PASS |
| 12 | `Gallery/Data/Samples/Animation.xaml` | 511 | 11 | 285199 | 10.27 | 1.01 | PASS |
| 13 | `Gallery/Data/Samples/Blending.xaml` | 180 | 0 | 5946 | 1.44 | 0.07 | PASS |
| 14 | `Gallery/Data/Samples/Brushes.xaml` | 561 | 1 | 502119 | 52.37 | 1.06 | PASS |
| 15 | `Gallery/Data/Samples/Button.xaml` | 260 | 4 | 1318160 | 62.65 | 3.90 | PASS |
| 16 | `Gallery/Data/Samples/Canvas.xaml` | 220 | 0 | 7601 | 1.15 | 0.07 | PASS |
| 17 | `Gallery/Data/Samples/CheckBox.xaml` | 178 | 7 | 11379 | 1.25 | 0.12 | PASS |
| 18 | `Gallery/Data/Samples/ComboBox.xaml` | 148 | 0 | 5923 | 0.87 | 0.06 | PASS |
| 19 | `Gallery/Data/Samples/ContextMenu.xaml` | 133 | 1 | 4904 | 0.96 | 0.04 | PASS |
| 20 | `Gallery/Data/Samples/DockPanel.xaml` | 157 | 0 | 5703 | 1.31 | 0.09 | PASS |
| 21 | `Gallery/Data/Samples/Effects.xaml` | 279 | 0 | 110000 | 11.51 | 0.28 | PASS |
| 22 | `Gallery/Data/Samples/Expander.xaml` | 192 | 0 | 204077 | 19.27 | 0.57 | PASS |
| 23 | `Gallery/Data/Samples/Grid.xaml` | 267 | 0 | 9035 | 1.67 | 0.14 | PASS |
| 24 | `Gallery/Data/Samples/GroupBox.xaml` | 143 | 0 | 203066 | 1.50 | 0.49 | PASS |
| 25 | `Gallery/Data/Samples/Hyperlink.xaml` | 90 | 2 | 4083 | 0.92 | 0.04 | PASS |
| 26 | `Gallery/Data/Samples/Image.xaml` | 144 | 0 | 5060 | 0.86 | 0.04 | PASS |
| 27 | `Gallery/Data/Samples/ItemsControl.xaml` | 123 | 0 | 7349 | 1.18 | 0.07 | PASS |
| 28 | `Gallery/Data/Samples/ListBox.xaml` | 188 | 0 | 9007 | 1.15 | 0.09 | PASS |
| 29 | `Gallery/Data/Samples/ListView.xaml` | 54 | 3 | 5141 | 1.10 | 0.05 | PASS |
| 30 | `Gallery/Data/Samples/Menu.xaml` | 289 | 0 | 8703 | 1.07 | 0.08 | PASS |
| 31 | `Gallery/Data/Samples/PasswordBox.xaml` | 98 | 0 | 4084 | 0.80 | 0.03 | PASS |
| 32 | `Gallery/Data/Samples/ProgressBar.xaml` | 207 | 3 | 11213 | 1.48 | 0.12 | PASS |
| 33 | `Gallery/Data/Samples/RadioButton.xaml` | 177 | 8 | 8376 | 1.09 | 0.08 | PASS |
| 34 | `Gallery/Data/Samples/RepeatButton.xaml` | 209 | 1 | 35072 | 1.56 | 0.16 | PASS |
| 35 | `Gallery/Data/Samples/ScrollViewer.xaml` | 175 | 0 | 7575 | 1.06 | 0.06 | PASS |
| 36 | `Gallery/Data/Samples/Slider.xaml` | 315 | 0 | 11765 | 1.05 | 0.12 | PASS |
| 37 | `Gallery/Data/Samples/StackPanel.xaml` | 85 | 0 | 3058 | 0.74 | 0.03 | PASS |
| 38 | `Gallery/Data/Samples/StatusBar.xaml` | 72 | 0 | 2682 | 0.76 | 0.02 | PASS |
| 39 | `Gallery/Data/Samples/TabControl.xaml` | 148 | 0 | 7253 | 1.00 | 0.06 | PASS |
| 40 | `Gallery/Data/Samples/TextBlock.xaml` | 204 | 0 | 8332 | 0.95 | 0.06 | PASS |
| 41 | `Gallery/Data/Samples/TextBox.xaml` | 108 | 0 | 5162 | 0.82 | 0.04 | PASS |
| 42 | `Gallery/Data/Samples/ToggleButton.xaml` | 159 | 4 | 12848 | 1.68 | 0.12 | PASS |
| 43 | `Gallery/Data/Samples/ToolBar.xaml` | 216 | 0 | 11195 | 1.06 | 0.06 | PASS |
| 44 | `Gallery/Data/Samples/ToolTip.xaml` | 73 | 1 | 3145 | 0.82 | 0.03 | PASS |
| 45 | `Gallery/Data/Samples/TreeView.xaml` | 211 | 2 | 8368 | 1.27 | 0.08 | PASS |
| 46 | `Gallery/Data/Samples/UniformGrid.xaml` | 192 | 0 | 6527 | 0.98 | 0.06 | PASS |
| 47 | `Gallery/Data/Samples/WrapPanel.xaml` | 188 | 0 | 6507 | 1.06 | 0.06 | PASS |
| 48 | `HelloWorld/Data/MainPage.xaml` | 95 | 1 | 155585 | 2.24 | 0.42 | PASS |
| 49 | `Integration/Data/MainPage.xaml` | 440 | 0 | 12440 | 1.43 | 0.10 | PASS |
| 50 | `Inventory/Data/MainPage.xaml` | 708 | 13 | 378227 | 54.16 | 1.72 | PASS |
| 51 | `Inventory/Data/Resources.xaml` | 2 | 10 | 59444 | 4.45 | 0.52 | PASS |
| 52 | `Localization/Data/Language-ar.xaml` | 2 | 0 | 1440 | 0.61 | 0.01 | PASS |
| 53 | `Localization/Data/Language-en.xaml` | 2 | 0 | 1035 | 0.46 | 0.01 | PASS |
| 54 | `Localization/Data/Language-fr.xaml` | 2 | 0 | 1094 | 0.52 | 0.01 | PASS |
| 55 | `Localization/Data/Language-jp.xaml` | 2 | 0 | 1144 | 0.45 | 0.01 | PASS |
| 56 | `Localization/Data/MainPage.xaml` | 186 | 10 | 228542 | 5.82 | 0.76 | PASS |
| 57 | `Localization/Data/Resources.xaml` | 2 | 9 | 213116 | 4.06 | 0.74 | PASS |
| 58 | `Login/Data/MainPage.xaml` | 150 | 9 | 793319 | 11.43 | 1.99 | PASS |
| 59 | `Login/Data/Resources.xaml` | 2 | 4 | 775977 | 6.95 | 1.81 | PASS |
| 60 | `Menu3D/Data/MainPage.xaml` | 27 | 6 | 1919658 | 476.11 | 5.84 | PASS |
| 61 | `Menu3D/Data/PagePreviews/PreviewMain.xaml` | 24 | 0 | 928 | 0.79 | 0.01 | PASS |
| 62 | `Menu3D/Data/PagePreviews/PreviewSettings.xaml` | 24 | 0 | 940 | 0.67 | 0.01 | PASS |
| 63 | `Menu3D/Data/PagePreviews/PreviewStart.xaml` | 24 | 0 | 931 | 0.67 | 0.01 | PASS |
| 64 | `Menu3D/Data/Resources.xaml` | 2 | 6 | 1918490 | 21.11 | 8.23 | PASS |
| 65 | `QuestLog/Data/MainPage.xaml` | 298 | 9 | 1572284 | 85.44 | 7.56 | PASS |
| 66 | `QuestLog/Data/Resources.xaml` | 2 | 6 | 829128 | 7.49 | 3.36 | PASS |
| 67 | `Rive/Data/MainPage.xaml` | 100 | 3 | 9550 | 1.71 | 0.15 | PASS |
| 68 | `Scoreboard/Data/MainPage.xaml` | 347 | 11 | 1276054 | 127.25 | 4.08 | PASS |
| 69 | `Theme/Data/App.xaml` | 3 | 81 | 492868 | 27.12 | 2.55 | PASS |
| 70 | `Theme/Data/MainWindow.xaml` | 303 | 0 | 8313 | 1.27 | 0.09 | PASS |
| 71 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkAqua.xaml` | 2 | 0 | 3236 | 1.23 | 0.03 | PASS |
| 72 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkBlue.xaml` | 2 | 0 | 3236 | 1.15 | 0.02 | PASS |
| 73 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkCrimson.xaml` | 2 | 0 | 3236 | 1.15 | 0.02 | PASS |
| 74 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkEmerald.xaml` | 2 | 0 | 3236 | 1.14 | 0.02 | PASS |
| 75 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkGreen.xaml` | 2 | 0 | 3236 | 1.27 | 0.02 | PASS |
| 76 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkLime.xaml` | 2 | 0 | 3236 | 1.18 | 0.03 | PASS |
| 77 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkOrange.xaml` | 2 | 0 | 3236 | 1.28 | 0.03 | PASS |
| 78 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkPurple.xaml` | 2 | 0 | 3236 | 1.22 | 0.02 | PASS |
| 79 | `Theme/Data/Theme/NoesisTheme.Brushes.DarkRed.xaml` | 2 | 0 | 3236 | 1.13 | 0.02 | PASS |
| 80 | `Theme/Data/Theme/NoesisTheme.Brushes.LightAqua.xaml` | 2 | 0 | 3237 | 1.13 | 0.02 | PASS |
| 81 | `Theme/Data/Theme/NoesisTheme.Brushes.LightBlue.xaml` | 2 | 0 | 3237 | 1.09 | 0.02 | PASS |
| 82 | `Theme/Data/Theme/NoesisTheme.Brushes.LightCrimson.xaml` | 2 | 0 | 3237 | 1.14 | 0.02 | PASS |
| 83 | `Theme/Data/Theme/NoesisTheme.Brushes.LightEmerald.xaml` | 2 | 0 | 3237 | 1.08 | 0.02 | PASS |
| 84 | `Theme/Data/Theme/NoesisTheme.Brushes.LightGreen.xaml` | 2 | 0 | 3237 | 1.09 | 0.02 | PASS |
| 85 | `Theme/Data/Theme/NoesisTheme.Brushes.LightLime.xaml` | 2 | 0 | 3237 | 1.10 | 0.02 | PASS |
| 86 | `Theme/Data/Theme/NoesisTheme.Brushes.LightOrange.xaml` | 2 | 0 | 3237 | 1.08 | 0.02 | PASS |
| 87 | `Theme/Data/Theme/NoesisTheme.Brushes.LightPurple.xaml` | 2 | 0 | 3237 | 1.17 | 0.02 | PASS |
| 88 | `Theme/Data/Theme/NoesisTheme.Brushes.LightRed.xaml` | 2 | 0 | 3237 | 1.11 | 0.02 | PASS |
| 89 | `Theme/Data/Theme/NoesisTheme.Colors.Dark.xaml` | 2 | 0 | 518 | 0.93 | 0.01 | PASS |
| 90 | `Theme/Data/Theme/NoesisTheme.Colors.Light.xaml` | 2 | 0 | 518 | 0.58 | 0.01 | PASS |
| 91 | `Theme/Data/Theme/NoesisTheme.DarkAqua.xaml` | 2 | 81 | 492758 | 27.48 | 2.19 | PASS |
| 92 | `Theme/Data/Theme/NoesisTheme.DarkBlue.xaml` | 2 | 81 | 492758 | 28.36 | 2.21 | PASS |
| 93 | `Theme/Data/Theme/NoesisTheme.DarkCrimson.xaml` | 2 | 81 | 492761 | 28.04 | 2.18 | PASS |
| 94 | `Theme/Data/Theme/NoesisTheme.DarkEmerald.xaml` | 2 | 81 | 492761 | 26.19 | 2.34 | PASS |
| 95 | `Theme/Data/Theme/NoesisTheme.DarkGreen.xaml` | 2 | 81 | 492759 | 25.68 | 2.62 | PASS |
| 96 | `Theme/Data/Theme/NoesisTheme.DarkLime.xaml` | 2 | 81 | 492758 | 27.16 | 2.25 | PASS |
| 97 | `Theme/Data/Theme/NoesisTheme.DarkOrange.xaml` | 2 | 81 | 492760 | 26.70 | 3.31 | PASS |
| 98 | `Theme/Data/Theme/NoesisTheme.DarkPurple.xaml` | 2 | 81 | 492760 | 26.68 | 3.68 | PASS |
| 99 | `Theme/Data/Theme/NoesisTheme.DarkRed.xaml` | 2 | 81 | 492757 | 28.25 | 2.68 | PASS |
| 100 | `Theme/Data/Theme/NoesisTheme.Fonts.xaml` | 2 | 0 | 255 | 0.56 | 0.00 | PASS |
| 101 | `Theme/Data/Theme/NoesisTheme.LightAqua.xaml` | 2 | 81 | 492760 | 28.64 | 2.09 | PASS |
| 102 | `Theme/Data/Theme/NoesisTheme.LightBlue.xaml` | 2 | 81 | 492760 | 28.40 | 2.70 | PASS |
| 103 | `Theme/Data/Theme/NoesisTheme.LightCrimson.xaml` | 2 | 81 | 492763 | 27.67 | 5.86 | PASS |
| 104 | `Theme/Data/Theme/NoesisTheme.LightEmerald.xaml` | 2 | 81 | 492763 | 33.78 | 2.34 | PASS |
| 105 | `Theme/Data/Theme/NoesisTheme.LightGreen.xaml` | 2 | 81 | 492761 | 29.71 | 3.75 | PASS |
| 106 | `Theme/Data/Theme/NoesisTheme.LightLime.xaml` | 2 | 81 | 492760 | 23.26 | 1.99 | PASS |
| 107 | `Theme/Data/Theme/NoesisTheme.LightOrange.xaml` | 2 | 81 | 492762 | 24.89 | 2.58 | PASS |
| 108 | `Theme/Data/Theme/NoesisTheme.LightPurple.xaml` | 2 | 81 | 492762 | 26.17 | 2.45 | PASS |
| 109 | `Theme/Data/Theme/NoesisTheme.LightRed.xaml` | 2 | 81 | 492759 | 24.17 | 2.24 | PASS |
| 110 | `Theme/Data/Theme/NoesisTheme.Styles.xaml` | 2 | 84 | 482727 | 20.96 | 2.03 | PASS |
| 111 | `TicTacToe/Data/MainPage.xaml` | 1285 | 8 | 4408893 | 268.63 | 14.57 | PASS |
| 112 | `TicTacToe/Data/Resources.xaml` | 2 | 1 | 1398662 | 8.56 | 3.13 | PASS |
| 113 | `UserControl/Data/MainPage.xaml` | 182 | 10 | 13778 | 2.58 | 0.18 | PASS |
| 114 | `UserControl/Data/NumericUpDown.xaml` | 47 | 5 | 12850 | 1.74 | 0.07 | PASS |
| 115 | `UserControl/Data/Resources.xaml` | 2 | 5 | 2466 | 0.74 | 0.03 | PASS |
| 116 | `VideoEffect/Data/MainPage.xaml` | 62 | 1 | 140779 | 1.52 | 0.26 | PASS |
