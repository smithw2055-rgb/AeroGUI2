# AeroGUI2 WPF (C#) 开发者快速上手样例 (`WpfQuickStart`)

本样例展示如何直接使用与 **WPF (C#)** 一致的开发模式编写 XAML 与后台/MVVM 脚本，并通过 `aero::XamlApplication` 一行代码加载运行：

1. **标准 C# 命名空间与导入语法**：直接编写 `using System; using System.Collections.ObjectModel; using System.Windows; namespace WpfQuickStart { ... }`。
2. **零样板 MVVM 数据绑定**：继承 `ObservableObject`（或实现 `INotifyPropertyChanged`）即可自动为所有自动属性生成变更通知，配合 `ObservableCollection<T>` 与 `RelayCommand` 实现列表与命令实时联动。
3. **强类型 Code-Behind 与路由事件**：`public partial class MainWindow : Window` 自动由编译器生成强类型 `x:Name` 控件属性（如 `this.NewTaskInput.Text`），支持标准 `(object sender, RoutedEventArgs e)` 签名及 `sender is Button btn` / `sender as Button` / `(Button)sender` 模式匹配与类型转换。
4. **一键应用启动**：C++ 宿主侧只需 `aero::XamlApplication app; app.Load("Samples/WpfQuickStart");` 即可自动解析 `App.xaml`（含全局资源与 `StartupUri`）、自动扫描 `.xaml.rs` 与 `ViewModels/*.rs` 并完成挂载。
