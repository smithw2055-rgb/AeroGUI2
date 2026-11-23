module BrushShaders;

import Aero.UI;

[Observable]
public class BrushShadersViewModel : ViewModelBase
{
    public float PlasmaSeed { get; set; } = 100.0f;
    public string LastAction { get; set; } = "PLAY";

    public BrushShadersViewModel()
    {
    }
}

[Observable]
public partial class MainWindow : Page
{
    public BrushShadersViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new BrushShadersViewModel();
        this.DataContext = this.ViewModel;

        if (this.Buttons != null)
        {
            int count = this.Buttons.ChildCount();
            int i = 0;
            while (i < count)
            {
                UIElement child = this.Buttons.ChildAt(i);
                Button btn = child as Button;
                if (btn != null)
                {
                    int buttonIdx = i;
                    btn.Click += (object s, RoutedEventArgs e) =>
                    {
                        this.OnShaderButtonClicked(buttonIdx);
                    };
                }
                i = i + 1;
            }
        }

        BindingEngine.Flush();
    }

    protected override void OnLoaded()
    {
        base.OnLoaded();
        if (this.Buttons != null && this.Buttons.ChildCount() > 0)
        {
            UIElement b0 = this.Buttons.ChildAt(0);
            if (b0 != null)
            {
                b0.Focus();
            }
        }
        this.BeginStoryboard("AnimNoise");
    }

    public void OnShaderButtonClicked(int index)
    {
        if (this.ViewModel != null)
        {
            if (index == 0) { this.ViewModel.LastAction = "PLAY"; }
            else if (index == 1) { this.ViewModel.LastAction = "STAGE"; }
            else { this.ViewModel.LastAction = "EXIT"; }
            this.ViewModel.PlasmaSeed = 100.0f + (float)(index * 100);
            this.ViewModel.NotifyPropertyChanged("LastAction");
            this.ViewModel.NotifyPropertyChanged("PlasmaSeed");
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
