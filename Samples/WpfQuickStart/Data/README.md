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
