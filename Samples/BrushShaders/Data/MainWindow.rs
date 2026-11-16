module BrushShaders;

import Aero.UI;

[Observable]
public class BrushShadersViewModel : ViewModelBase
{
    public int PaletteIndex { get; set; } = 0;
    public string ActivePalette { get; set; } = "Cyber Cyan / Amber";
    public float PlasmaSeed { get; set; } = 100.0f;
    public string LastAction { get; set; } = "PLAY";

    public BrushShadersViewModel()
    {
    }

    public void SetPalette(int index)
    {
        this.PaletteIndex = index % 3;
        if (this.PaletteIndex < 0) { this.PaletteIndex = 0; }
        if (this.PaletteIndex == 0) { this.ActivePalette = "Cyber Cyan / Amber"; }
        else if (this.PaletteIndex == 1) { this.ActivePalette = "Neon Violet / Emerald"; }
        else { this.ActivePalette = "Solar Crimson / Ice"; }
        this.NotifyPropertyChanged("PaletteIndex");
        this.NotifyPropertyChanged("ActivePalette");
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
        this.CyclePalette();
    }

    public void CyclePalette()
    {
        if (this.ViewModel == null) { return; }
        this.ViewModel.SetPalette(this.ViewModel.PaletteIndex + 1);
        this.ApplyPalette(this.ViewModel.PaletteIndex);
    }

    public void ApplyPalette(int paletteIndex)
    {
        string primaryHex = "#FF01BCFF";
        string secondaryHex = "#FFFFAB03";
        string bgTopHex = "#FF061526";
        string bgBottomHex = "#FF0B243E";

        if (paletteIndex == 1)
        {
            primaryHex = "#FFA855F7";
            secondaryHex = "#FF10B981";
            bgTopHex = "#FF140B29";
            bgBottomHex = "#FF092420";
        }
        else if (paletteIndex == 2)
        {
            primaryHex = "#FFF43F5E";
            secondaryHex = "#FF38BDF8";
            bgTopHex = "#FF220A16";
            bgBottomHex = "#FF0B1E36";
        }

        LinearGradientBrush bgGrad = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
        bgGrad.AddStop(bgTopHex, 0.0f);
        bgGrad.AddStop(bgBottomHex, 1.0f);
        this.Background = bgGrad;

        if (this.Buttons != null)
        {
            int count = this.Buttons.ChildCount();
            int i = 0;
            while (i < count)
            {
                Button btn = this.Buttons.ChildAt(i) as Button;
                if (btn != null)
                {
                    btn.Background = new SolidColorBrush(i == 2 ? secondaryHex : primaryHex);
                }
                i = i + 1;
            }
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
