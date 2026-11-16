module CustomAnimation;

import Aero.UI;
import RealScript.Math;

[Observable]
public class CustomAnimationViewModel : ViewModelBase
{
    public string ActivePreset { get; set; } = "All Easings Ready";
    public int ReplayCount { get; set; } = 0;

    public CustomAnimationViewModel()
    {
    }

    public void RecordPlay(string preset)
    {
        this.ActivePreset = preset;
        this.ReplayCount = this.ReplayCount + 1;
        this.NotifyPropertyChanged("ActivePreset");
        this.NotifyPropertyChanged("ReplayCount");
    }
}

[Observable]
public partial class MainWindow : Page
{
    public CustomAnimationViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new CustomAnimationViewModel();
        this.DataContext = this.ViewModel;
        BindingEngine.Flush();
    }

    protected override void OnLoaded()
    {
        base.OnLoaded();
        this.PlayCircle();
    }

    public Storyboard BuildBounceStoryboard()
    {
        Storyboard sb = new Storyboard();
        sb.AddDoubleAnimation("BounceTarget", "Opacity", 0.0f, 1.0f, 0.2f, EasingFunctionKind.Linear, EasingMode.EaseOut);
        sb.AddDoubleAnimation("BounceTarget", "ScaleX", 0.0f, 1.0f, 1.5f, EasingFunctionKind.Bounce, EasingMode.EaseOut);
        sb.AddDoubleAnimation("BounceTarget", "ScaleY", 0.0f, 1.0f, 1.5f, EasingFunctionKind.Bounce, EasingMode.EaseOut);
        return sb;
    }

    public Storyboard BuildElasticStoryboard()
    {
        Storyboard sb = new Storyboard();
        sb.AddDoubleAnimation("ElasticTarget", "Angle", 90.0f, 0.0f, 2.5f, EasingFunctionKind.Elastic, EasingMode.EaseOut);
        return sb;
    }

    public Storyboard BuildBackStoryboard()
    {
        Storyboard sb = new Storyboard();
        sb.AddDoubleAnimation("BackTarget", "TranslateX", -200.0f, 0.0f, 0.5f, EasingFunctionKind.Back, EasingMode.EaseOut);
        return sb;
    }

    public Storyboard BuildExpoStoryboard()
    {
        Storyboard sb = new Storyboard();
        DoubleAnimationUsingKeyFrames kf = new DoubleAnimationUsingKeyFrames();
        kf.TargetName = "GaugeNeedle";
        kf.TargetProperty = "Angle";
        kf.AddKeyFrame(new LinearDoubleKeyFrame(-90.0f, 0.0f));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(-40.0f, 0.15f, EasingFunctionKind.Exponential, EasingMode.EaseOut));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(-70.0f, 0.30f, EasingFunctionKind.Exponential, EasingMode.EaseIn));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(-10.0f, 0.45f, EasingFunctionKind.Exponential, EasingMode.EaseOut));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(-45.0f, 0.60f, EasingFunctionKind.Exponential, EasingMode.EaseIn));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(85.0f, 0.90f, EasingFunctionKind.Exponential, EasingMode.EaseOut));
        kf.AddKeyFrame(new EasingDoubleKeyFrame(-90.0f, 1.70f, EasingFunctionKind.Exponential, EasingMode.EaseOut));
        sb.Add(kf);
        return sb;
    }

    private DoubleAnimationUsingKeyFrames BuildOrbitAxis(string targetName, string prop, float radius, float phaseOffsetRad, bool isY)
    {
        DoubleAnimationUsingKeyFrames kf = new DoubleAnimationUsingKeyFrames();
        kf.TargetName = targetName;
        kf.TargetProperty = prop;
        kf.RepeatCount = 0;

        int steps = 8;
        int i = 0;
        while (i <= steps)
        {
            float t = (float)i / (float)steps;
            float angle = t * 6.2831853f - phaseOffsetRad;
            float val = isY ? (Math.Sinf(angle) * radius) : (Math.Cosf(angle) * radius);
            kf.AddKeyFrame(new LinearDoubleKeyFrame(val, t * 2.0f));
            i = i + 1;
        }
        return kf;
    }

    public Storyboard BuildCircleStoryboard()
    {
        Storyboard sb = new Storyboard();
        sb.Add(this.BuildOrbitAxis("Dot1", "TranslateX", 40.0f, 0.0f, false));
        sb.Add(this.BuildOrbitAxis("Dot1", "TranslateY", 40.0f, 0.0f, true));
        sb.Add(this.BuildOrbitAxis("Dot2", "TranslateX", 40.0f, 0.6283185f, false));
        sb.Add(this.BuildOrbitAxis("Dot2", "TranslateY", 40.0f, 0.6283185f, true));
        sb.Add(this.BuildOrbitAxis("Dot3", "TranslateX", 40.0f, 1.2566371f, false));
        sb.Add(this.BuildOrbitAxis("Dot3", "TranslateY", 40.0f, 1.2566371f, true));
        return sb;
    }

    public Storyboard BuildShakeStoryboard()
    {
        Storyboard sb = new Storyboard();
        DoubleAnimationUsingKeyFrames kf = new DoubleAnimationUsingKeyFrames();
        kf.TargetName = "LockContainer";
        kf.TargetProperty = "TranslateX";
        kf.AddKeyFrame(new LinearDoubleKeyFrame(0.0f, 0.00f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(14.0f, 0.06f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(-12.0f, 0.12f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(10.0f, 0.18f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(-8.0f, 0.24f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(4.0f, 0.32f));
        kf.AddKeyFrame(new LinearDoubleKeyFrame(0.0f, 0.40f));
        sb.Add(kf);
        sb.AddColorAnimation("LockBackground", "Background", "#FFEF4444", "#FF061B2B", 0.4f, EasingFunctionKind.Linear, EasingMode.EaseOut);
        return sb;
    }

    public void PlayBounce()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Bounce"); }
        this.BuildBounceStoryboard().Begin(this);
    }

    public void PlayElastic()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Elastic"); }
        this.BuildElasticStoryboard().Begin(this);
    }

    public void PlayBack()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Back"); }
        this.BuildBackStoryboard().Begin(this);
    }

    public void PlayExpo()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Expo"); }
        this.BuildExpoStoryboard().Begin(this);
    }

    public void PlayCircle()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Circle"); }
        this.BuildCircleStoryboard().Begin(this);
    }

    public void PlayShake()
    {
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("Shake"); }
        this.BuildShakeStoryboard().Begin(this);
    }

    public void PlayAll()
    {
        this.PlayBounce();
        this.PlayElastic();
        this.PlayBack();
        this.PlayExpo();
        this.PlayShake();
        if (this.ViewModel != null) { this.ViewModel.RecordPlay("All"); }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
