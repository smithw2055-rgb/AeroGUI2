# AeroGUI 2.0 + RealScript `Samples.zip` 全量基准与黄金像素回归报告

## 1. 全局回归总览

| 指标维度 | 扫描总数 | 通过数量 | 成功率 | 总耗时 (ms) | 平均耗时 (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 官方示例工程数 (`Samples/*`) | 21 | 21 | 100.0% | - | - |
| `.xaml` 编译 + `.axb4` 往返校验 | 74 | 74 | 100.0% | 9047.72 | 122.27 |
| `.riv` Rive v7 LEB128 二进制解码 | 9 | 9 | 100.0% | 620.12 | 68.90 |
| 核心主场景端到端黄金像素回归 (`1280x720 -> 320x180`) | 16 | 16 | 100.0% | 4274.46 | 267.15 |

## 2. 16 个代表性示例主界面黄金像素基准表 (`320x180` RGBA8)

| 场景名称 | XAML 路径 | 字体数 | 渲染节点 | 4KB 页数 | 非空像素数 | 覆盖率 | 独立色彩数 | FNV-1a 64-bit 黄金哈希 | 编译 (ms) | 挂载+样式 (ms) | 布局 (ms) | 建树 (ms) | 光栅化 (ms) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| `HelloWorld` | `HelloWorld/Data/MainPage.xaml` | 1 | 15 | 2 | 57600 | 100.0% | 589 | `0x3422245a225e0984` | 70.36 | 0.10 | 0.03 | 0.06 | 0.97 |
| `Login` | `Login/Data/MainPage.xaml` | 1 | 24 | 5 | 57600 | 100.0% | 1050 | `0x0c3ffb1f4521c795` | 82.67 | 80.75 | 0.06 | 0.10 | 4.94 |
| `Buttons` | `Buttons/Data/MainPage.xaml` | 1 | 46 | 3 | 57600 | 100.0% | 1104 | `0xdec7ee98a01f875b` | 80.15 | 80.38 | 0.05 | 0.09 | 8.39 |
| `QuestLog` | `QuestLog/Data/MainPage.xaml` | 2 | 68 | 6 | 26707 | 46.4% | 1120 | `0x5ae8f767d6ecdc3f` | 156.06 | 28.86 | 0.08 | 0.14 | 7.03 |
| `Inventory` | `Inventory/Data/MainPage.xaml` | 2 | 126 | 12 | 57600 | 100.0% | 482 | `0xa63da91b98c15c62` | 221.34 | 2.07 | 0.10 | 0.21 | 4.15 |
| `Scoreboard` | `Scoreboard/Data/MainPage.xaml` | 2 | 81 | 10 | 57600 | 100.0% | 1089 | `0xeb6ec3ac036ee02c` | 912.62 | 0.39 | 0.14 | 0.19 | 11.57 |
| `TicTacToe` | `TicTacToe/Data/MainPage.xaml` | 0 | 257 | 11 | 57600 | 100.0% | 2269 | `0xb0c11338899af61e` | 746.58 | 504.02 | 0.21 | 0.35 | 9.44 |
| `Localization` | `Localization/Data/MainPage.xaml` | 5 | 67 | 4 | 57600 | 100.0% | 1651 | `0xa8910ee269be3e72` | 76.98 | 0.30 | 0.12 | 0.14 | 6.17 |
| `Gallery` | `Gallery/Data/MainWindow.xaml` | 7 | 94 | 15 | 57600 | 100.0% | 1051 | `0x59dbf91e923281d3` | 77.87 | 77.05 | 0.09 | 0.14 | 3.84 |
| `Brushes` | `Gallery/Data/Samples/Brushes.xaml` | 7 | 244 | 54 | 6861 | 11.9% | 1082 | `0xc85914cf8813e19b` | 97.23 | 76.45 | 0.18 | 0.32 | 0.62 |
| `Shapes` | `Gallery/Data/Samples/Blending.xaml` | 7 | 73 | 19 | 9669 | 16.8% | 962 | `0xfd5aa2ae710e180b` | 0.27 | 76.71 | 18.03 | 0.16 | 0.76 |
| `Rive` | `Rive/Data/MainPage.xaml` | 0 | 1707 | 42 | 57599 | 100.0% | 7416 | `0xa6e4fad4dcdc9ac2` | 0.46 | 313.37 | 0.67 | 1.46 | 43.00 |
| `Effects` | `Gallery/Data/Samples/Effects.xaml` | 7 | 133 | 31 | 10222 | 17.7% | 5323 | `0xd42010674a216ba8` | 20.83 | 73.77 | 16.92 | 0.22 | 0.87 |
| `BackgroundBlur` | `BackgroundBlur/Data/MainPage.xaml` | 1 | 68 | 4 | 48591 | 84.4% | 1559 | `0x69c6a59c4451f65d` | 87.79 | 85.85 | 0.06 | 0.11 | 8.49 |
| `BrushShaders` | `BrushShaders/Data/MainPage.xaml` | 1 | 59 | 3 | 57600 | 100.0% | 1459 | `0x4538f05c4e2e2fd0` | 78.87 | 0.21 | 0.06 | 0.09 | 7.59 |
| `UserControl` | `UserControl/Data/MainPage.xaml` | 1 | 74 | 8 | 57600 | 100.0% | 1431 | `0x15e59a6fe76b2a47` | 0.46 | 0.31 | 0.03 | 0.07 | 4.80 |

## 3. 全部 9 个官方 Rive v7 (`.riv`) 二进制资产解码统计表

| `.riv` 相对路径 | 原始大小 (B) | `.axb4` 大小 (B) | 画板尺寸 | IR 节点数 | MTSDF 路径数 | `MOT6` 动画片段 | 状态机数 | 解码+往返耗时 (ms) | 状态 |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `Rive/Data/background.riv` | 126154 | 790980 | 1920x1080 | 884 | 198 | 42 | 1 | 111.09 | PASS |
| `Rive/Data/leaves-bl.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 94.13 | PASS |
| `Rive/Data/leaves-br.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 94.90 | PASS |
| `Rive/Data/leaves-tl.riv` | 126129 | 939191 | 1920x1080 | 74 | 14 | 1 | 1 | 93.54 | PASS |
| `Rive/Data/leaves-tr.riv` | 126129 | 939141 | 1920x1080 | 74 | 14 | 1 | 1 | 93.42 | PASS |
| `Rive/Data/logo.riv` | 9105 | 142880 | 420x180 | 123 | 43 | 1 | 1 | 31.18 | PASS |
| `Rive/Data/rewards.riv` | 8560 | 461260 | 500x200 | 78 | 12 | 6 | 1 | 86.14 | PASS |
| `Rive/Data/settings.riv` | 12916 | 78030 | 500x200 | 159 | 13 | 6 | 1 | 6.56 | PASS |
| `Rive/Data/start.riv` | 8918 | 85758 | 500x200 | 89 | 22 | 6 | 1 | 9.16 | PASS |

## 4. 全部 74 个官方 `.xaml` 编译与 `.axb4` 往返明细表

| # | `.xaml` 相对路径 | 指令条数 | 样式数 | `.axb4` 字节数 | 编译耗时 (ms) | 往返校验耗时 (ms) | 结果 |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | `BackgroundBlur/Data/MainPage.xaml` | 239 | 6 | 450845 | 90.89 | 0.90 | PASS |
| 2 | `BackgroundBlur/Data/Resources.xaml` | 2 | 4 | 438385 | 86.88 | 0.92 | PASS |
| 3 | `BrushShaders/Data/MainPage.xaml` | 71 | 3 | 722388 | 77.57 | 2.19 | PASS |
| 4 | `Buttons/Data/MainPage.xaml` | 77 | 2 | 496163 | 84.80 | 0.77 | PASS |
| 5 | `Buttons/Data/Resources.xaml` | 2 | 1 | 769890 | 83.75 | 1.89 | PASS |
| 6 | `CustomAnimation/Data/MainPage.xaml` | 370 | 6 | 113267 | 2.88 | 0.13 | PASS |
| 7 | `CustomControl/Data/MainPage.xaml` | 63 | 2 | 270537 | 12.31 | 0.49 | PASS |
| 8 | `CustomRender/Data/MainWindow.xaml` | 62 | 0 | 2202 | 0.13 | 0.02 | PASS |
| 9 | `DataBinding/Data/MainPage.xaml` | 36 | 2 | 8577 | 0.44 | 0.09 | PASS |
| 10 | `Gallery/Data/MainWindow.xaml` | 393 | 19 | 249826 | 77.37 | 0.81 | PASS |
| 11 | `Gallery/Data/Resources.xaml` | 2 | 11 | 235953 | 75.39 | 0.62 | PASS |
| 12 | `Gallery/Data/Samples/Animation.xaml` | 509 | 11 | 284426 | 7.79 | 0.35 | PASS |
| 13 | `Gallery/Data/Samples/Blending.xaml` | 181 | 0 | 5851 | 0.23 | 0.04 | PASS |
| 14 | `Gallery/Data/Samples/Brushes.xaml` | 557 | 1 | 500959 | 96.36 | 1.26 | PASS |
| 15 | `Gallery/Data/Samples/Button.xaml` | 261 | 4 | 1318193 | 123.96 | 3.57 | PASS |
| 16 | `Gallery/Data/Samples/Canvas.xaml` | 220 | 0 | 7255 | 0.29 | 0.05 | PASS |
| 17 | `Gallery/Data/Samples/CheckBox.xaml` | 178 | 7 | 10181 | 0.32 | 0.07 | PASS |
| 18 | `Gallery/Data/Samples/ComboBox.xaml` | 148 | 0 | 5537 | 0.17 | 0.03 | PASS |
| 19 | `Gallery/Data/Samples/ContextMenu.xaml` | 132 | 1 | 4624 | 0.17 | 0.04 | PASS |
| 20 | `Gallery/Data/Samples/DockPanel.xaml` | 157 | 0 | 5493 | 0.15 | 0.03 | PASS |
| 21 | `Gallery/Data/Samples/Effects.xaml` | 275 | 0 | 109520 | 20.90 | 0.09 | PASS |
| 22 | `Gallery/Data/Samples/Expander.xaml` | 191 | 0 | 203797 | 32.99 | 0.09 | PASS |
| 23 | `Gallery/Data/Samples/Grid.xaml` | 267 | 0 | 8657 | 0.27 | 0.06 | PASS |
| 24 | `Gallery/Data/Samples/GroupBox.xaml` | 143 | 0 | 202841 | 34.15 | 0.09 | PASS |
| 25 | `Gallery/Data/Samples/Hyperlink.xaml` | 90 | 2 | 3910 | 0.22 | 0.03 | PASS |
| 26 | `Gallery/Data/Samples/Image.xaml` | 146 | 0 | 5310 | 0.18 | 0.04 | PASS |
| 27 | `Gallery/Data/Samples/ItemsControl.xaml` | 130 | 0 | 6285 | 0.25 | 0.04 | PASS |
| 28 | `Gallery/Data/Samples/ListBox.xaml` | 194 | 0 | 8511 | 0.29 | 0.06 | PASS |
| 29 | `Gallery/Data/Samples/ListView.xaml` | 52 | 3 | 4480 | 0.23 | 0.04 | PASS |
| 30 | `Gallery/Data/Samples/Menu.xaml` | 287 | 0 | 8243 | 0.25 | 0.06 | PASS |
| 31 | `Gallery/Data/Samples/PasswordBox.xaml` | 96 | 0 | 3577 | 0.16 | 0.03 | PASS |
| 32 | `Gallery/Data/Samples/ProgressBar.xaml` | 207 | 3 | 11112 | 0.37 | 0.07 | PASS |
| 33 | `Gallery/Data/Samples/RadioButton.xaml` | 178 | 8 | 8561 | 0.21 | 0.05 | PASS |
| 34 | `Gallery/Data/Samples/RepeatButton.xaml` | 211 | 1 | 35022 | 0.41 | 0.08 | PASS |
| 35 | `Gallery/Data/Samples/ScrollViewer.xaml` | 183 | 0 | 7000 | 0.25 | 0.04 | PASS |
| 36 | `Gallery/Data/Samples/Slider.xaml` | 315 | 0 | 11308 | 0.25 | 0.07 | PASS |
| 37 | `Gallery/Data/Samples/StackPanel.xaml` | 86 | 0 | 3027 | 0.13 | 0.02 | PASS |
| 38 | `Gallery/Data/Samples/StatusBar.xaml` | 74 | 0 | 2732 | 0.11 | 0.02 | PASS |
| 39 | `Gallery/Data/Samples/TabControl.xaml` | 146 | 0 | 5859 | 0.17 | 0.03 | PASS |
| 40 | `Gallery/Data/Samples/TextBlock.xaml` | 212 | 0 | 7925 | 0.18 | 0.04 | PASS |
| 41 | `Gallery/Data/Samples/TextBox.xaml` | 105 | 0 | 4528 | 0.13 | 0.02 | PASS |
| 42 | `Gallery/Data/Samples/ToggleButton.xaml` | 159 | 4 | 12619 | 0.49 | 0.08 | PASS |
| 43 | `Gallery/Data/Samples/ToolBar.xaml` | 216 | 0 | 11020 | 0.27 | 0.04 | PASS |
| 44 | `Gallery/Data/Samples/ToolTip.xaml` | 75 | 1 | 3195 | 0.14 | 0.02 | PASS |
| 45 | `Gallery/Data/Samples/TreeView.xaml` | 212 | 2 | 8264 | 0.27 | 0.05 | PASS |
| 46 | `Gallery/Data/Samples/UniformGrid.xaml` | 193 | 0 | 6410 | 0.17 | 0.04 | PASS |
| 47 | `Gallery/Data/Samples/WrapPanel.xaml` | 188 | 0 | 6330 | 0.16 | 0.04 | PASS |
| 48 | `HelloWorld/Data/MainPage.xaml` | 95 | 1 | 155619 | 70.25 | 0.11 | PASS |
| 49 | `Integration/Data/MainPage.xaml` | 442 | 0 | 12490 | 0.37 | 0.08 | PASS |
| 50 | `Inventory/Data/MainPage.xaml` | 707 | 13 | 348037 | 224.22 | 1.26 | PASS |
| 51 | `Inventory/Data/Resources.xaml` | 2 | 10 | 42549 | 1.64 | 0.23 | PASS |
| 52 | `Localization/Data/Language-ar.xaml` | 2 | 0 | 1440 | 0.07 | 0.01 | PASS |
| 53 | `Localization/Data/Language-en.xaml` | 2 | 0 | 1035 | 0.04 | 0.00 | PASS |
| 54 | `Localization/Data/Language-fr.xaml` | 2 | 0 | 1094 | 0.04 | 0.00 | PASS |
| 55 | `Localization/Data/Language-jp.xaml` | 2 | 0 | 1144 | 0.04 | 0.01 | PASS |
| 56 | `Localization/Data/MainPage.xaml` | 188 | 10 | 222586 | 77.20 | 0.21 | PASS |
| 57 | `Localization/Data/Resources.xaml` | 2 | 9 | 213207 | 76.76 | 0.20 | PASS |
| 58 | `Login/Data/MainPage.xaml` | 151 | 9 | 500544 | 83.24 | 0.94 | PASS |
| 59 | `Login/Data/Resources.xaml` | 2 | 4 | 775847 | 83.84 | 1.66 | PASS |
| 60 | `Menu3D/Data/MainPage.xaml` | 29 | 6 | 1433674 | 2555.46 | 3.57 | PASS |
| 61 | `Menu3D/Data/PagePreviews/PreviewMain.xaml` | 26 | 0 | 978 | 0.16 | 0.01 | PASS |
| 62 | `Menu3D/Data/PagePreviews/PreviewSettings.xaml` | 26 | 0 | 990 | 0.06 | 0.01 | PASS |
| 63 | `Menu3D/Data/PagePreviews/PreviewStart.xaml` | 26 | 0 | 981 | 0.05 | 0.01 | PASS |
| 64 | `Menu3D/Data/Resources.xaml` | 2 | 6 | 1522700 | 2552.52 | 5.06 | PASS |
| 65 | `QuestLog/Data/MainPage.xaml` | 300 | 9 | 1184424 | 157.07 | 1.78 | PASS |
| 66 | `QuestLog/Data/Resources.xaml` | 2 | 6 | 829128 | 28.73 | 0.71 | PASS |
| 67 | `Rive/Data/MainPage.xaml` | 102 | 3 | 9589 | 0.44 | 0.07 | PASS |
| 68 | `Scoreboard/Data/MainPage.xaml` | 347 | 11 | 1276066 | 861.90 | 2.52 | PASS |
| 69 | `TicTacToe/Data/MainPage.xaml` | 1279 | 8 | 3905899 | 750.83 | 7.17 | PASS |
| 70 | `TicTacToe/Data/Resources.xaml` | 2 | 1 | 1398731 | 494.56 | 1.08 | PASS |
| 71 | `UserControl/Data/MainPage.xaml` | 182 | 10 | 12418 | 0.67 | 0.09 | PASS |
| 72 | `UserControl/Data/NumericUpDown.xaml` | 49 | 5 | 11098 | 0.34 | 0.03 | PASS |
| 73 | `UserControl/Data/Resources.xaml` | 2 | 5 | 2504 | 0.16 | 0.02 | PASS |
| 74 | `VideoEffect/Data/MainPage.xaml` | 62 | 1 | 140803 | 69.11 | 0.09 | PASS |
