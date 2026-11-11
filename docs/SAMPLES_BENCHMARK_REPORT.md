# AeroGUI 2.0 + RealScript `Samples.zip` 全量基准与黄金像素回归报告

## 1. 全局回归总览与覆盖范围说明

| 指标维度 | 扫描总数 | 通过数量 | 成功率 | 总耗时 (ms) | 平均耗时 (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 官方示例工程目录扫描 (`Samples/*`) | 21 | 21 | 100.0% | - | - |
| `.xaml` 编译 + `.axb4` 往返校验（覆盖全部有 `.xaml` 的工程） | 74 | 74 | 100.0% | 19449.09 | 262.83 |
| `.riv` Rive v7 LEB128 二进制解码 | 9 | 9 | 100.0% | 1338.58 | 148.73 |
| 静态/入场动画 XAML 场景 CPU 黄金像素光栅化 (`13` 个工程 / `16` 个场景, `1280x720 -> 320x180`) | 16 | 16 | 100.0% | 9603.63 | 600.23 |

### 测试覆盖边界说明（Honest Scope Notes）

- **编译与 `.axb4` 二进制往返校验（21 个官方示例工程目录，74 个 `.xaml` + 9 个 `.riv`）**：全部 21 个官方示例目录均纳入扫描，其中所有 74 个 `.xaml` 资产 100% 通过 `AeroCompiler::Compile` 与 `.axb4` 序列化/反序列化字节级一致性校验，9 个 `.riv` 资产 100% 通过 `RiveImporter::ImportBytesToAxb`。
- **CPU 黄金像素光栅化覆盖（13 个工程共 16 个静态/动画 XAML 场景）**：覆盖 `BackgroundBlur`、`BrushShaders`、`Buttons`、`Gallery`（含 `Gallery` 主窗体、`Brushes`、`Gallery_Blending`、`Effects` 共 4 个场景）、`HelloWorld`、`Inventory`、`Localization`、`Login`、`QuestLog`、`Rive`、`Scoreboard`、`TicTacToe`、`UserControl`。
- **仅做 `.xaml -> .axb4` 编译往返、未纳入纯 CPU 黄金像素光栅化的 8 个示例工程**：`CustomAnimation`、`CustomControl`、`CustomRender`、`DataBinding`、`Integration`、`IntegrationGLUT`、`Menu3D`、`VideoEffect`。这些工程依赖外部宿主 C++ 自定义控件/自定义渲染器/自定义动画类、GLUT/引擎宿主集成循环、C++ Code-Behind 动态 ViewModel 数据源或外部视频流与自定义 Shader 特效。
- **静态 `DataContext` 挂载范围**：CPU 黄金光栅化在推进指定 `settleAnimSeconds` 入场动画后以空/静态 `DataContext`（`ObjectRef{}`）执行确定性软件光栅化，用于锁定 XAML 布局、样式字典、矢量图元、Rive 画板与混合模式的像素回归，不等同于运行示例原始 C++ ViewModel 业务逻辑或原生 GPU 自定义 Shader。

## 2. 16 个静态/动画 XAML 场景 CPU 黄金像素基准表（跨 13 个示例工程，`320x180` RGBA8）

| 场景名称 | XAML 路径 | 字体数 | 渲染节点 | 4KB 页数 | 非空像素数 | 覆盖率 | 独立色彩数 | FNV-1a 64-bit 黄金哈希 | 编译 (ms) | 挂载+样式 (ms) | 布局 (ms) | 建树 (ms) | 光栅化 (ms) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 1 | 15 | 2 | 57600 | 100.0% | 589 | `0x3422245a225e0984` | 139.33 | 0.46 | 0.10 | 0.64 | 3.43 |
| `Login` | `Login/Data/MainPage.xaml` | 1 | 24 | 5 | 57600 | 100.0% | 972 | `0x196a5d7ab1036412` | 226.30 | 168.99 | 0.10 | 0.77 | 12.62 |
| `Buttons` | `Buttons/Data/MainPage.xaml` | 1 | 46 | 3 | 57600 | 100.0% | 1104 | `0xdec7ee98a01f875b` | 225.43 | 186.75 | 0.10 | 0.88 | 21.48 |
| `QuestLog` | `QuestLog/Data/MainPage.xaml` | 2 | 68 | 6 | 26707 | 46.4% | 1120 | `0x5ae8f767d6ecdc3f` | 366.12 | 62.18 | 0.14 | 0.80 | 20.03 |
| `Inventory` | `Inventory/Data/MainPage.xaml` | 2 | 126 | 12 | 57600 | 100.0% | 1387 | `0x677fa6b917c91341` | 446.68 | 7.08 | 0.18 | 1.05 | 24.36 |
| `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 2 | 81 | 10 | 57600 | 100.0% | 1089 | `0xeb6ec3ac036ee02c` | 1915.08 | 1.04 | 0.26 | 1.28 | 34.78 |
| `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 0 | 257 | 11 | 57600 | 100.0% | 2269 | `0xb0c11338899af61e` | 1551.43 | 1033.43 | 0.28 | 10.43 | 47.87 |
| `Localization` | `Localization/Data/MainPage.xaml` | 5 | 67 | 4 | 57600 | 100.0% | 1651 | `0xa8910ee269be3e72` | 194.72 | 3.05 | 0.49 | 1.54 | 26.33 |
| `Gallery` | `Gallery/Data/MainWindow.xaml` | 7 | 94 | 15 | 57600 | 100.0% | 1051 | `0x59dbf91e923281d3` | 175.12 | 222.97 | 20.35 | 31.27 | 14.71 |
| `Brushes` | `Gallery/Data/Samples/Brushes.xaml` | 7 | 244 | 54 | 7194 | 12.5% | 1141 | `0x74e53e72579708e7` | 208.29 | 172.17 | 0.34 | 1.46 | 1.80 |
| `Gallery_Blending` | `Gallery/Data/Samples/Blending.xaml` | 7 | 73 | 19 | 9669 | 16.8% | 962 | `0x87d878f3cf0570c5` | 1.59 | 200.12 | 32.02 | 1.69 | 3.78 |
| `Rive` | `Rive/Data/MainPage.xaml` | 0 | 1707 | 42 | 57599 | 100.0% | 7416 | `0xa6e4fad4dcdc9ac2` | 3.08 | 776.30 | 1.50 | 5.87 | 130.13 |
| `Effects` | `Gallery/Data/Samples/Effects.xaml` | 7 | 133 | 31 | 10453 | 18.1% | 5394 | `0x06bc32d3bf2cc0c9` | 45.03 | 156.85 | 25.12 | 1.84 | 2.41 |
| `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 1 | 68 | 4 | 48591 | 84.4% | 1559 | `0x69c6a59c4451f65d` | 206.09 | 188.88 | 0.12 | 0.70 | 25.33 |
| `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 1 | 59 | 3 | 57600 | 100.0% | 1459 | `0x4538f05c4e2e2fd0` | 166.16 | 0.70 | 0.10 | 0.94 | 19.60 |
| `UserControl` | `UserControl/Data/MainPage.xaml` | 1 | 74 | 8 | 57600 | 100.0% | 1431 | `0x15e59a6fe76b2a47` | 3.07 | 1.59 | 0.07 | 0.69 | 15.75 |

## 3. 全部 9 个官方 Rive v7 (`.riv`) 二进制资产解码统计表

| `.riv` 相对路径 | 原始大小 (B) | `.axb4` 大小 (B) | 画板尺寸 | IR 节点数 | MTSDF 路径数 | `MOT6` 动画片段 | 状态机数 | 解码+往返耗时 (ms) | 状态 |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `Rive/Data/background.riv` | 126154 | 790980 | 1920x1080 | 884 | 198 | 42 | 1 | 239.05 | PASS |
| `Rive/Data/leaves-bl.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 207.24 | PASS |
| `Rive/Data/leaves-br.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 216.66 | PASS |
| `Rive/Data/leaves-tl.riv` | 126129 | 939191 | 1920x1080 | 74 | 14 | 1 | 1 | 205.12 | PASS |
| `Rive/Data/leaves-tr.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 208.27 | PASS |
| `Rive/Data/logo.riv` | 9105 | 142880 | 420x180 | 123 | 43 | 1 | 1 | 56.97 | PASS |
| `Rive/Data/rewards.riv` | 8560 | 461260 | 500x200 | 78 | 12 | 6 | 1 | 176.36 | PASS |
| `Rive/Data/settings.riv` | 12916 | 78030 | 500x200 | 159 | 13 | 6 | 1 | 12.00 | PASS |
| `Rive/Data/start.riv` | 8918 | 85758 | 500x200 | 89 | 22 | 6 | 1 | 16.91 | PASS |

## 4. 全部 74 个官方 `.xaml` 编译与 `.axb4` 往返明细表

| # | `.xaml` 相对路径 | 指令条数 | 样式数 | `.axb4` 字节数 | 编译耗时 (ms) | 往返校验耗时 (ms) | 结果 |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur/Data/MainPage.xaml` | 239 | 6 | 457891 | 149.82 | 1.85 | PASS |
| 2 | `BackgroundBlur/Data/Resources.xaml` | 2 | 4 | 438271 | 145.43 | 1.51 | PASS |
| 3 | `BrushShaders/Data/MainPage.xaml` | 71 | 3 | 722388 | 138.93 | 2.74 | PASS |
| 4 | `Buttons/Data/MainPage.xaml` | 77 | 2 | 781716 | 151.66 | 2.34 | PASS |
| 5 | `Buttons/Data/Resources.xaml` | 2 | 1 | 769890 | 146.64 | 2.23 | PASS |
| 6 | `CustomAnimation/Data/MainPage.xaml` | 370 | 6 | 113153 | 8.10 | 0.43 | PASS |
| 7 | `CustomControl/Data/MainPage.xaml` | 63 | 2 | 270537 | 24.30 | 2.07 | PASS |
| 8 | `CustomRender/Data/MainWindow.xaml` | 62 | 0 | 2202 | 1.00 | 0.03 | PASS |
| 9 | `DataBinding/Data/MainPage.xaml` | 36 | 2 | 8539 | 1.71 | 0.11 | PASS |
| 10 | `Gallery/Data/MainWindow.xaml` | 393 | 19 | 269120 | 133.99 | 1.16 | PASS |
| 11 | `Gallery/Data/Resources.xaml` | 2 | 11 | 235878 | 141.54 | 1.01 | PASS |
| 12 | `Gallery/Data/Samples/Animation.xaml` | 509 | 11 | 284008 | 16.27 | 0.76 | PASS |
| 13 | `Gallery/Data/Samples/Blending.xaml` | 181 | 0 | 5851 | 1.13 | 0.07 | PASS |
| 14 | `Gallery/Data/Samples/Brushes.xaml` | 557 | 1 | 500921 | 156.24 | 1.89 | PASS |
| 15 | `Gallery/Data/Samples/Button.xaml` | 261 | 4 | 1318041 | 206.07 | 4.72 | PASS |
| 16 | `Gallery/Data/Samples/Canvas.xaml` | 220 | 0 | 7255 | 1.33 | 0.08 | PASS |
| 17 | `Gallery/Data/Samples/CheckBox.xaml` | 178 | 7 | 9915 | 1.15 | 0.10 | PASS |
| 18 | `Gallery/Data/Samples/ComboBox.xaml` | 148 | 0 | 5537 | 0.88 | 0.05 | PASS |
| 19 | `Gallery/Data/Samples/ContextMenu.xaml` | 132 | 1 | 4624 | 0.91 | 0.04 | PASS |
| 20 | `Gallery/Data/Samples/DockPanel.xaml` | 157 | 0 | 5493 | 0.84 | 0.05 | PASS |
| 21 | `Gallery/Data/Samples/Effects.xaml` | 275 | 0 | 109482 | 37.17 | 0.37 | PASS |
| 22 | `Gallery/Data/Samples/Expander.xaml` | 191 | 0 | 203797 | 58.20 | 0.55 | PASS |
| 23 | `Gallery/Data/Samples/Grid.xaml` | 267 | 0 | 8657 | 1.26 | 0.11 | PASS |
| 24 | `Gallery/Data/Samples/GroupBox.xaml` | 143 | 0 | 202841 | 59.01 | 0.55 | PASS |
| 25 | `Gallery/Data/Samples/Hyperlink.xaml` | 90 | 2 | 3834 | 1.03 | 0.05 | PASS |
| 26 | `Gallery/Data/Samples/Image.xaml` | 146 | 0 | 5310 | 0.86 | 0.06 | PASS |
| 27 | `Gallery/Data/Samples/ItemsControl.xaml` | 130 | 0 | 6324 | 1.14 | 0.07 | PASS |
| 28 | `Gallery/Data/Samples/ListBox.xaml` | 194 | 0 | 8589 | 1.07 | 0.09 | PASS |
| 29 | `Gallery/Data/Samples/ListView.xaml` | 52 | 3 | 4480 | 0.99 | 0.05 | PASS |
| 30 | `Gallery/Data/Samples/Menu.xaml` | 287 | 0 | 8243 | 0.99 | 0.08 | PASS |
| 31 | `Gallery/Data/Samples/PasswordBox.xaml` | 96 | 0 | 3577 | 1.12 | 0.03 | PASS |
| 32 | `Gallery/Data/Samples/ProgressBar.xaml` | 207 | 3 | 10998 | 1.36 | 0.11 | PASS |
| 33 | `Gallery/Data/Samples/RadioButton.xaml` | 178 | 8 | 8257 | 0.90 | 0.07 | PASS |
| 34 | `Gallery/Data/Samples/RepeatButton.xaml` | 211 | 1 | 34908 | 1.43 | 0.22 | PASS |
| 35 | `Gallery/Data/Samples/ScrollViewer.xaml` | 183 | 0 | 7000 | 1.01 | 0.05 | PASS |
| 36 | `Gallery/Data/Samples/Slider.xaml` | 315 | 0 | 11308 | 0.98 | 0.09 | PASS |
| 37 | `Gallery/Data/Samples/StackPanel.xaml` | 86 | 0 | 3027 | 0.72 | 0.03 | PASS |
| 38 | `Gallery/Data/Samples/StatusBar.xaml` | 74 | 0 | 2732 | 0.74 | 0.03 | PASS |
| 39 | `Gallery/Data/Samples/TabControl.xaml` | 146 | 0 | 5860 | 0.90 | 0.05 | PASS |
| 40 | `Gallery/Data/Samples/TextBlock.xaml` | 212 | 0 | 7925 | 0.87 | 0.06 | PASS |
| 41 | `Gallery/Data/Samples/TextBox.xaml` | 105 | 0 | 4528 | 0.77 | 0.04 | PASS |
| 42 | `Gallery/Data/Samples/ToggleButton.xaml` | 159 | 4 | 12505 | 1.56 | 0.11 | PASS |
| 43 | `Gallery/Data/Samples/ToolBar.xaml` | 216 | 0 | 11020 | 1.62 | 0.13 | PASS |
| 44 | `Gallery/Data/Samples/ToolTip.xaml` | 75 | 1 | 3195 | 0.95 | 0.07 | PASS |
| 45 | `Gallery/Data/Samples/TreeView.xaml` | 212 | 2 | 8226 | 1.18 | 0.08 | PASS |
| 46 | `Gallery/Data/Samples/UniformGrid.xaml` | 193 | 0 | 6410 | 0.89 | 0.05 | PASS |
| 47 | `Gallery/Data/Samples/WrapPanel.xaml` | 188 | 0 | 6330 | 0.81 | 0.05 | PASS |
| 48 | `HelloWorld/Data/MainPage.xaml` | 95 | 1 | 155581 | 113.98 | 0.49 | PASS |
| 49 | `Integration/Data/MainPage.xaml` | 442 | 0 | 12490 | 1.63 | 0.11 | PASS |
| 50 | `Inventory/Data/MainPage.xaml` | 707 | 13 | 361777 | 371.64 | 1.62 | PASS |
| 51 | `Inventory/Data/Resources.xaml` | 2 | 10 | 42549 | 5.27 | 0.50 | PASS |
| 52 | `Localization/Data/Language-ar.xaml` | 2 | 0 | 1440 | 0.57 | 0.01 | PASS |
| 53 | `Localization/Data/Language-en.xaml` | 2 | 0 | 1035 | 0.45 | 0.01 | PASS |
| 54 | `Localization/Data/Language-fr.xaml` | 2 | 0 | 1094 | 0.44 | 0.01 | PASS |
| 55 | `Localization/Data/Language-jp.xaml` | 2 | 0 | 1144 | 0.47 | 0.01 | PASS |
| 56 | `Localization/Data/MainPage.xaml` | 188 | 10 | 228613 | 132.93 | 0.72 | PASS |
| 57 | `Localization/Data/Resources.xaml` | 2 | 9 | 213093 | 125.48 | 0.77 | PASS |
| 58 | `Login/Data/MainPage.xaml` | 151 | 9 | 791771 | 151.96 | 2.77 | PASS |
| 59 | `Login/Data/Resources.xaml` | 2 | 4 | 775847 | 139.25 | 2.05 | PASS |
| 60 | `Menu3D/Data/MainPage.xaml` | 29 | 6 | 1523901 | 5744.02 | 8.98 | PASS |
| 61 | `Menu3D/Data/PagePreviews/PreviewMain.xaml` | 26 | 0 | 978 | 1.15 | 0.02 | PASS |
| 62 | `Menu3D/Data/PagePreviews/PreviewSettings.xaml` | 26 | 0 | 990 | 1.56 | 0.02 | PASS |
| 63 | `Menu3D/Data/PagePreviews/PreviewStart.xaml` | 26 | 0 | 981 | 0.98 | 0.02 | PASS |
| 64 | `Menu3D/Data/Resources.xaml` | 2 | 6 | 1522700 | 5446.66 | 9.52 | PASS |
| 65 | `QuestLog/Data/MainPage.xaml` | 300 | 9 | 1572177 | 346.01 | 8.75 | PASS |
| 66 | `QuestLog/Data/Resources.xaml` | 2 | 6 | 829128 | 71.50 | 4.12 | PASS |
| 67 | `Rive/Data/MainPage.xaml` | 102 | 3 | 9513 | 2.36 | 0.21 | PASS |
| 68 | `Scoreboard/Data/MainPage.xaml` | 347 | 11 | 1275990 | 2111.37 | 5.91 | PASS |
| 69 | `TicTacToe/Data/MainPage.xaml` | 1279 | 8 | 4407417 | 1844.89 | 16.73 | PASS |
| 70 | `TicTacToe/Data/Resources.xaml` | 2 | 1 | 1398731 | 984.98 | 5.46 | PASS |
| 71 | `UserControl/Data/MainPage.xaml` | 182 | 10 | 13978 | 3.06 | 0.52 | PASS |
| 72 | `UserControl/Data/NumericUpDown.xaml` | 49 | 5 | 12900 | 2.53 | 0.10 | PASS |
| 73 | `UserControl/Data/Resources.xaml` | 2 | 5 | 2466 | 1.47 | 0.06 | PASS |
| 74 | `VideoEffect/Data/MainPage.xaml` | 62 | 1 | 140765 | 138.62 | 0.46 | PASS |
