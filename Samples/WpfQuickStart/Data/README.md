# AeroGUI2 WPF (C#) 开发者快速上手指南 (`WpfQuickStart`)

本样例展示如何直接沿用 **Visual Studio WPF (C#)** 的工程组织与代码习惯开发 AeroGUI2 界面、Code-Behind 与 MVVM 视图模型，并通过 `aero::XamlApplication` 一键编译挂载运行。

## 1. 目录结构（与标准 WPF 项目完全一致）

```text
Samples/WpfQuickStart/Data/
├── App.xaml                        # 全局资源字典与 StartupUri="MainWindow.xaml"
├── MainWindow.xaml                 # 主窗口 XAML（含 KeyBinding、Menu、ToolBar、TreeView、ListView/GridView）
├── MainWindow.xaml.cs              # 主窗口 C# Code-Behind（零样板代码）
└── ViewModels/
    └── TaskBoardViewModel.cs       # MVVM 视图模型（ViewModelBase + ObservableCollection<T> + RelayCommand<T>）
```

## 2. Code-Behind 编写方式（`MainWindow.xaml.cs`）

无需手写 `CreateMainWindow()` 工厂函数，也无需调用额外桥接 API，直接按 WPF 习惯编写 `InitializeComponent()` 与 `DataContext = ...`：

```csharp
using System;
using System.Windows;
using Aero.UI;

namespace WpfQuickStart
{
    public partial class MainWindow : Window
    {
        private TaskBoardViewModel _vm;

        public MainWindow()
        {
            InitializeComponent();
            _vm = new TaskBoardViewModel();
            DataContext = _vm; // 自动同步至底层双轨绑定引擎，同样支持 DataContext = this;
        }

        // 支持强类型事件参数：RoutedEventArgs / SelectionChangedEventArgs / TextChangedEventArgs / KeyEventArgs
        public void QuickFillButton_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn)
            {
                NewTaskInput.Text = "Shipped via Quick Fill"; // x:Name 强类型控件属性自动生成于 .g.rs
                e.Handled = true;
            }
        }
    }
}
```

## 3. MVVM 视图模型与命令（`ViewModels/TaskBoardViewModel.cs`）

- **属性变更通知**：继承 `ViewModelBase` / `ObservableObject` / `BindableBase`（或实现 `INotifyPropertyChanged`），既可直接写 C# 自动属性 `public string NewTaskTitle { get; set; } = "";`，也可使用 `SetProperty(ref _field, value)`。
- **无参及泛型命令**：同时支持 `RelayCommand`（无参）与 `RelayCommand<T>` / `DelegateCommand<T>`（带 `CommandParameter` 自动类型封送），可直接配合 `<Button Command="..." CommandParameter="..."/>` 与 `<Window.InputBindings><KeyBinding Key="N" Modifiers="Ctrl" .../></Window.InputBindings>`。
- **集合与表格视图**：`ObservableCollection<T>` 支持增量刷新 `ItemsControl`、`ListBox`、`ComboBox`、`TreeView`（含 `HierarchicalDataTemplate`）以及带 `GridView` 多列表头的 `ListView`。

## 4. C++ 宿主侧一键加载与驱动

```cpp
aero::XamlApplication app;
std::string err;
if (app.Load("Samples/WpfQuickStart", &err)) {
    // 自动完成：
    // 1. 解析 App.xaml 全局资源与 StartupUri
    // 2. 自动预注册同目录及子目录下的本地 UserControl / ResourceDictionary (.xaml)
    // 3. 自动扫描编译所有 .xaml.cs / .cs / .rs 脚本并实例化 MainWindow
    app.Tick(0.016f);
}
```
