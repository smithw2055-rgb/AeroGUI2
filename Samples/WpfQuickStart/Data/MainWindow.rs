module WpfQuickStart;

import Aero.UI;

public partial class MainWindow : Window
{
    private TaskBoardViewModel _vm;

    public TaskBoardViewModel VM { get { return this._vm; } }
    public TaskBoardViewModel ViewModel { get { return this._vm; } }
    public int ProgrammaticClickCount { get; set; } = 0;

    public MainWindow()
    {
        this.EnsureAppResources();
        this._vm = new TaskBoardViewModel();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();

        if (this.QuickFillButton != null)
        {
            this.QuickFillButton.Click += (object s, RoutedEventArgs args) =>
            {
                this.OnQuickFillProgrammatic(s, args);
            };
        }
    }

    private void EnsureAppResources()
    {
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        if (!appRes.Contains("AppSurfaceBrush")) { appRes.Set("AppSurfaceBrush", new SolidColorBrush("#FF141824")); }
        if (!appRes.Contains("CardSurfaceBrush")) { appRes.Set("CardSurfaceBrush", new SolidColorBrush("#FF1D2334")); }
        if (!appRes.Contains("AccentBrush")) { appRes.Set("AccentBrush", new SolidColorBrush("#FF3B82F6")); }
        if (!appRes.Contains("PrimaryTextBrush")) { appRes.Set("PrimaryTextBrush", new SolidColorBrush("#FFE8EEF8")); }
        if (!appRes.Contains("MutedTextBrush")) { appRes.Set("MutedTextBrush", new SolidColorBrush("#FF94A3B8")); }
    }

    private void OnQuickFillProgrammatic(object sender, RoutedEventArgs e)
    {
        this.ProgrammaticClickCount = this.ProgrammaticClickCount + 1;
    }

    public void ToggleNavVisibility(bool visible)
    {
        if (this.NavBorder != null)
        {
            this.NavBorder.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    public void QuickFillButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button)
        {
            if (this.NewTaskInput != null)
            {
                this.NewTaskInput.Text = "Shipped via Quick Fill";
            }
            if (this._vm != null)
            {
                this._vm.NewTaskTitle = "Shipped via Quick Fill";
            }
            if (e != null)
            {
                e.Handled = true;
            }
        }
    }

    public void TasksList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (this._vm != null && e != null && e.SelectedIndex >= 0)
        {
            this._vm.SelectedTaskIndex = e.SelectedIndex;
        }
    }
}

public static Window CreateMainWindow()
{
    return new MainWindow();
}
