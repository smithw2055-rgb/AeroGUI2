# AeroGUI2 WPF (C#) QuickStart Guide

`Samples/WpfQuickStart` demonstrates authoring AeroGUI2 UI & MVVM logic using familiar **WPF (C#)** conventions:

1. **Project Structure & Auto-Discovery (`XamlApplication`)**:
   - `App.xaml` + optional `App.xaml.cs` (`public partial class App : Application` with `OnStartup(StartupEventArgs e)`).
   - `MainWindow.xaml` + `MainWindow.xaml.cs` (`public partial class MainWindow : Window` calling `InitializeComponent()`).
   - Reusable `UserControl` components (`Controls/*.xaml` + `Controls/*.xaml.cs`) automatically pre-registered with generated partial stubs.
   - Standard `.cs` files with UTF-8 BOM, `#region`/`#endregion`, `#if`/`#else`/`#endif`, and `.NET` `using` directives work out of the box.
2. **Code-Behind & Routed Events (XAML & `+=` Subscriptions)**:
   - Strongly typed `x:Name` control properties and WPF enums (`Visibility.Visible` / `Hidden` / `Collapsed`, `SelectionMode`, `Orientation`, `HorizontalAlignment`, `VerticalAlignment`, `ClickMode`).
   - Subscribe in XAML (`Click="OnSaveClick"`) or C# (`SaveButton.Click += OnSaveClick;`, lambdas `(s, e) => { ... }`, `-=`) across `Click`, `Checked`/`Unchecked`/`Indeterminate`, `TextChanged`, `PasswordChanged`, `ValueChanged`, `SelectionChanged`, `Expanded`/`Collapsed`, `Loaded`, `GotFocus`/`LostFocus`, `MouseEnter`/`MouseLeave`, `MouseLeftButtonDown`/`MouseLeftButtonUp`, and `KeyDown`/`KeyUp`.
3. **MVVM, `nameof(...)` & Dynamic `RelayCommand.CanExecute`**:
   - Inherit `ViewModelBase` / `ObservableObject`, use `SetProperty(ref _field, value, nameof(Prop))` or `OnPropertyChanged(nameof(Prop))`, `ObservableCollection<T>`, `RelayCommand(exec, canExec)`, and `RaiseCanExecuteChanged()`.


## 新增 WPF (C#) 开发者快速上手能力

1. **直接加载 `.csproj` 工程**：支持 `app.Load("Samples/WpfQuickStart/Data/WpfQuickStart.csproj")`，自动扫描 `App.xaml` (`StartupUri`)、`*.xaml.cs`、`ViewModels/*.cs`、本地 `UserControl` / `ResourceDictionary` 以及 `Fonts/`、`Assets/`、`Images/` 资源目录。
2. **原生 C# 现代语法支持 (RealScript)**：
   - 表达式体成员：`public string Summary => $"{DoneCount}/{TotalCount}";`、`get => _title; set => SetProperty(ref _title, value, nameof(Title));`、表达式体方法与构造函数
   - `: this(...)` 构造函数链、显式类型 Lambda `(TaskItem x) => x.IsDone`、多丢弃参数 `(_, _) => Refresh()`
   - 多跳控件事件订阅 `this.HeaderCard.ActionBtn.Click += OnActionClick;` 与 `$@"..."` / `@$"..."` 逐字插值字符串
3. **WPF 绑定与命令体系增强**：
   - `ViewModelBase` / `ObservableObject` 的 `SetProperty` 与 `OnPropertyChanged` 自动驱动手动属性与计算属性刷新
   - 资源字典注册自定义 C# `IValueConverter`（`<local:PriorityBadgeConverter x:Key="PriorityConv"/>`）
   - `{Binding TargetNullValue=...}`、`<Window.CommandBindings><CommandBinding .../></Window.CommandBindings>`、`<GridViewColumn.CellTemplate>` 内联模板、`SelectedValue` / `SelectedValuePath` 双向绑定
   - `TextBox` 选区与剪贴板（`Select`、`SelectedText`、`Ctrl+A/C/X/V`、`Shift+方向键`、`Ctrl+左右跳词`）、`TabControl` 垂直标签栏（`Left`/`Right` + `Ctrl+Tab`）、`MenuItem` 多级子菜单与点击外部自动收起、`Hyperlink.RequestNavigate`
