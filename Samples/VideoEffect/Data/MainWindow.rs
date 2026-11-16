module VideoEffect;

import Aero.UI;

[Observable]
public class VideoEffectViewModel : ViewModelBase
{
    private bool _isPlaying = true;
    private bool _isPaused = false;
    private bool _isMuted = false;
    private float _volume = 1.0f;
    private float _videoBlurRadius = 10.0f;
    private float _aberrationAmount = 2.0f;
    private float _pixelSize = 0.0f;
    private float _directionalBlurRadius = 0.0f;
    private float _directionalBlurAngle = 5.0f;
    private string _tintColor = "#FFFFFFFF";
    private string _statusText = "Playing Particles.mp4";
    private RelayCommand _playCommand;
    private RelayCommand _pauseCommand;
    private RelayCommand _stopCommand;
    private RelayCommand _restartCommand;
    private RelayCommand _toggleMuteCommand;
    private RelayCommand _resetEffectsCommand;

    public RelayCommand PlayCommand { get { return this._playCommand; } }
    public RelayCommand PauseCommand { get { return this._pauseCommand; } }
    public RelayCommand StopCommand { get { return this._stopCommand; } }
    public RelayCommand RestartCommand { get { return this._restartCommand; } }
    public RelayCommand ToggleMuteCommand { get { return this._toggleMuteCommand; } }
    public RelayCommand ResetEffectsCommand { get { return this._resetEffectsCommand; } }

    public bool IsPlaying
    {
        get { return this._isPlaying; }
        set { this.SetProperty(ref this._isPlaying, value, "IsPlaying"); }
    }

    public bool IsPaused
    {
        get { return this._isPaused; }
        set { this.SetProperty(ref this._isPaused, value, "IsPaused"); }
    }

    public bool IsMuted
    {
        get { return this._isMuted; }
        set { this.SetProperty(ref this._isMuted, value, "IsMuted"); }
    }

    public float Volume
    {
        get { return this._volume; }
        set { this.SetProperty(ref this._volume, value, "Volume"); }
    }

    public float VideoBlurRadius
    {
        get { return this._videoBlurRadius; }
        set { this.SetProperty(ref this._videoBlurRadius, value, "VideoBlurRadius"); }
    }

    public float AberrationAmount
    {
        get { return this._aberrationAmount; }
        set { this.SetProperty(ref this._aberrationAmount, value, "AberrationAmount"); }
    }

    public float PixelSize
    {
        get { return this._pixelSize; }
        set { this.SetProperty(ref this._pixelSize, value, "PixelSize"); }
    }

    public float DirectionalBlurRadius
    {
        get { return this._directionalBlurRadius; }
        set { this.SetProperty(ref this._directionalBlurRadius, value, "DirectionalBlurRadius"); }
    }

    public float DirectionalBlurAngle
    {
        get { return this._directionalBlurAngle; }
        set { this.SetProperty(ref this._directionalBlurAngle, value, "DirectionalBlurAngle"); }
    }

    public string TintColor
    {
        get { return this._tintColor; }
        set { this.SetProperty(ref this._tintColor, value, "TintColor"); }
    }

    public string StatusText
    {
        get { return this._statusText; }
        set { this.SetProperty(ref this._statusText, value, "StatusText"); }
    }

    public VideoEffectViewModel()
    {
    }

    public void BindCommands(
        Action onPlay,
        Action onPause,
        Action onStop,
        Action onRestart,
        Action onToggleMute,
        Action onResetEffects)
    {
        this._playCommand = new RelayCommand(onPlay);
        this._pauseCommand = new RelayCommand(onPause);
        this._stopCommand = new RelayCommand(onStop);
        this._restartCommand = new RelayCommand(onRestart);
        this._toggleMuteCommand = new RelayCommand(onToggleMute);
        this._resetEffectsCommand = new RelayCommand(onResetEffects);
    }
}

public partial class MainWindow : Page
{
    private VideoEffectViewModel _vm;

    public VideoEffectViewModel VM { get { return this._vm; } }
    public VideoEffectViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new VideoEffectViewModel();
        this._vm.BindCommands(
            () => { this.Play(); },
            () => { this.Pause(); },
            () => { this.Stop(); },
            () => { this.Restart(); },
            () => { this.ToggleMute(); },
            () => { this.ResetEffects(); });
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void Play()
    {
        if (this.VM != null)
        {
            this.VM.IsPlaying = true;
            this.VM.IsPaused = false;
            this.VM.StatusText = "Playing Particles.mp4";
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.SetProperty("IsPlaying", true);
        }
    }

    public void Pause()
    {
        if (this.VM != null)
        {
            this.VM.IsPlaying = false;
            this.VM.IsPaused = true;
            this.VM.StatusText = "Paused";
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.SetProperty("IsPlaying", false);
        }
    }

    public void Stop()
    {
        if (this.VM != null)
        {
            this.VM.IsPlaying = false;
            this.VM.IsPaused = false;
            this.VM.StatusText = "Stopped";
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.SetProperty("IsPlaying", false);
            videoEl.SetProperty("Position", 0.0f);
        }
    }

    public void Restart()
    {
        this.Stop();
        this.Play();
        this.PlayAnimations();
    }

    public void ToggleMute()
    {
        bool nextMuted = this.VM != null ? !this.VM.IsMuted : true;
        if (this.VM != null)
        {
            this.VM.IsMuted = nextMuted;
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.SetProperty("IsMuted", nextMuted);
        }
    }

    public void SetVolume(float volume)
    {
        float clamped = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
        if (this.VM != null)
        {
            this.VM.Volume = clamped;
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.SetProperty("Volume", clamped);
        }
    }

    public void SetVideoBlur(float radius)
    {
        float clamped = radius < 0.0f ? 0.0f : radius;
        if (this.VM != null)
        {
            this.VM.VideoBlurRadius = clamped;
        }
        UIElement videoEl = this.FindName("Video");
        if (videoEl != null)
        {
            videoEl.Effect = new BlurEffect(clamped);
        }
    }

    public void SetTintColor(string colorHex, float opacity = 1.0f)
    {
        string hex = (colorHex == null || colorHex == "") ? "#FFFFFFFF" : colorHex;
        if (this.VM != null)
        {
            this.VM.TintColor = hex;
        }
        if (this.VideoTint != null)
        {
            this.VideoTint.Effect = new TintEffect(hex, opacity);
        }
    }

    public void SetAberration(float amount)
    {
        float clamped = amount < 0.0f ? 0.0f : amount;
        if (this.VM != null)
        {
            this.VM.AberrationAmount = clamped;
        }
        if (this.NoesisTextAberration != null)
        {
            this.NoesisTextAberration.Effect = new ChromaticAberrationEffect(clamped);
        }
        if (this.SubtitleTextAberration != null)
        {
            this.SubtitleTextAberration.Effect = new ChromaticAberrationEffect(clamped);
        }
    }

    public void SetPixelate(float pixelSize)
    {
        float clamped = pixelSize < 0.0f ? 0.0f : pixelSize;
        if (this.VM != null)
        {
            this.VM.PixelSize = clamped;
        }
        if (this.NoesisTextPixelate != null)
        {
            this.NoesisTextPixelate.Effect = new PixelateEffect(clamped);
        }
    }

    public void SetDirectionalBlur(float radius, float angle = 5.0f)
    {
        float clamped = radius < 0.0f ? 0.0f : radius;
        if (this.VM != null)
        {
            this.VM.DirectionalBlurRadius = clamped;
            this.VM.DirectionalBlurAngle = angle;
        }
        if (this.SubtitleTextBlur != null)
        {
            this.SubtitleTextBlur.Effect = new DirectionalBlurEffect(clamped, angle);
        }
    }

    public void ResetEffects()
    {
        this.SetVideoBlur(10.0f);
        this.SetTintColor("#FFFFFFFF", 1.0f);
        this.SetAberration(2.0f);
        this.SetPixelate(0.0f);
        this.SetDirectionalBlur(0.0f, 5.0f);
    }

    public void PlayAnimations()
    {
        this.BeginStoryboard("Anim.FadeIn");
        this.BeginStoryboard("Anim.Loop1");
        this.BeginStoryboard("Anim.Loop2");
        this.BeginStoryboard("Anim.Loop3");
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
