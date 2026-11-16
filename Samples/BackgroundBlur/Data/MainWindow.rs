module BackgroundBlur;

import Aero.UI;
import RealScript.Math;

[Observable]
public class BackgroundBlurViewModel : ViewModelBase
{
    public float TopRadius { get; set; } = 20.0f;
    public float BottomRadius { get; set; } = 10.0f;
    public bool IsPanelOpen { get; set; } = true;
    public int BackgroundPresetIndex { get; set; } = 0;
    public string StatusText { get; set; } = "Glassmorphism Backdrop Blur Active";

    public BackgroundBlurViewModel()
    {
    }

    public void UpdateRadii(float topR, float bottomR)
    {
        this.TopRadius = Math.Clampf(topR, 0.0f, 64.0f);
        this.BottomRadius = Math.Clampf(bottomR, 0.0f, 64.0f);
        int it = (int)this.TopRadius;
        int ib = (int)this.BottomRadius;
        this.StatusText = "Top Blur: " + it + "px | Bottom Blur: " + ib + "px";
        this.NotifyPropertyChanged("TopRadius");
        this.NotifyPropertyChanged("BottomRadius");
        this.NotifyPropertyChanged("StatusText");
    }
}

[Observable]
public partial class MainWindow : Page
{
    public BackgroundBlurViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new BackgroundBlurViewModel();
        this.DataContext = this.ViewModel;

        if (this.TopBlurRadius != null)
        {
            this.TopBlurRadius.ValueChanged += (object s, RoutedPropertyChangedEventArgs<float> e) =>
            {
                this.SetTopBlurRadius(e.NewValue);
            };
        }
        if (this.BottomBlurRadius != null)
        {
            this.BottomBlurRadius.ValueChanged += (object s, RoutedPropertyChangedEventArgs<float> e) =>
            {
                this.SetBottomBlurRadius(e.NewValue);
            };
        }

        this.SyncEffects();
        BindingEngine.Flush();
    }

    public void SyncEffects()
    {
        float topR = this.TopBlurRadius != null ? this.TopBlurRadius.Value : (this.ViewModel != null ? this.ViewModel.TopRadius : 20.0f);
        float bottomR = this.BottomBlurRadius != null ? this.BottomBlurRadius.Value : (this.ViewModel != null ? this.ViewModel.BottomRadius : 10.0f);

        if (this.TopPanelBlur != null)
        {
            this.TopPanelBlur.Effect = new BackdropBlurEffect(topR);
        }
        if (this.BottomPanelBlur != null)
        {
            this.BottomPanelBlur.Effect = new BackdropBlurEffect(bottomR);
        }
        if (this.ViewModel != null)
        {
            this.ViewModel.UpdateRadii(topR, bottomR);
        }
    }

    public void SetTopBlurRadius(float radius)
    {
        float clamped = Math.Clampf(radius, 0.0f, 64.0f);
        if (this.TopBlurRadius != null && this.TopBlurRadius.Value != clamped)
        {
            this.TopBlurRadius.Value = clamped;
        }
        if (this.TopPanelBlur != null)
        {
            this.TopPanelBlur.Effect = new BackdropBlurEffect(clamped);
        }
        if (this.ViewModel != null)
        {
            this.ViewModel.UpdateRadii(clamped, this.ViewModel.BottomRadius);
        }
    }

    public void SetBottomBlurRadius(float radius)
    {
        float clamped = Math.Clampf(radius, 0.0f, 64.0f);
        if (this.BottomBlurRadius != null && this.BottomBlurRadius.Value != clamped)
        {
            this.BottomBlurRadius.Value = clamped;
        }
        if (this.BottomPanelBlur != null)
        {
            this.BottomPanelBlur.Effect = new BackdropBlurEffect(clamped);
        }
        if (this.ViewModel != null)
        {
            this.ViewModel.UpdateRadii(this.ViewModel.TopRadius, clamped);
        }
    }

    public void ToggleBackgroundPreset()
    {
        if (this.source == null || this.ViewModel == null) { return; }
        this.ViewModel.BackgroundPresetIndex = (this.ViewModel.BackgroundPresetIndex + 1) % 3;
        int mode = this.ViewModel.BackgroundPresetIndex;
        if (mode == 0)
        {
            LinearGradientBrush grad0 = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
            grad0.AddStop("#FF0D203A", 0.0f);
            grad0.AddStop("#FF1D6FB7", 0.5f);
            grad0.AddStop("#FF0C1832", 1.0f);
            this.source.Background = grad0;
        }
        else if (mode == 1)
        {
            LinearGradientBrush grad1 = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
            grad1.AddStop("#FF1A1038", 0.0f);
            grad1.AddStop("#FF6B2D9E", 0.5f);
            grad1.AddStop("#FF122347", 1.0f);
            this.source.Background = grad1;
        }
        else
        {
            LinearGradientBrush grad2 = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
            grad2.AddStop("#FF0A242E", 0.0f);
            grad2.AddStop("#FF1E847F", 0.5f);
            grad2.AddStop("#FF102749", 1.0f);
            this.source.Background = grad2;
        }
    }

    public void OpenPanel()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.IsPanelOpen = true;
            this.ViewModel.NotifyPropertyChanged("IsPanelOpen");
        }
        Storyboard sb = new Storyboard();
        sb.AddDoubleAnimation("PanelGrid", "ScaleX", 0.0f, 1.0f, 0.25f, EasingFunctionKind.Cubic, EasingMode.EaseOut);
        sb.AddDoubleAnimation("PanelGrid", "ScaleY", 0.0f, 1.0f, 0.25f, EasingFunctionKind.Cubic, EasingMode.EaseOut);
        sb.Begin(this);
    }

    public void ClosePanel()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.IsPanelOpen = false;
            this.ViewModel.NotifyPropertyChanged("IsPanelOpen");
        }
        this.BeginStoryboard("AnimClose");
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
