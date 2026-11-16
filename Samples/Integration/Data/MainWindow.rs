module Integration;

import Aero.UI;

[Observable]
public class IntegrationSettingsViewModel : ViewModelBase
{
    public bool AimingAssistance { get; set; } = false;
    public bool MouseMovements { get; set; } = false;
    public bool AttackDecisionEffects { get; set; } = false;
    public bool WeaponQuickSlots { get; set; } = false;

    public bool ShowOnlineGuildMembers { get; set; } = false;
    public bool ShowOtherNames { get; set; } = false;
    public bool ShowPartyNames { get; set; } = false;
    public bool ShowGuildNames { get; set; } = false;
    public bool DeclineInvites { get; set; } = false;
    public string TargetDisplayMode { get; set; } = "AlwaysDisplay";

    public int ResolutionIndex { get; set; } = 6;
    public bool FullScreen { get; set; } = false;
    public int TexturesQualityIndex { get; set; } = 2;
    public int ShadowsQualityIndex { get; set; } = 2;
    public float FovMultiplier { get; set; } = 1.0f;
    public bool ShowFps { get; set; } = false;
    public bool AntiAliasing { get; set; } = false;
    public bool Ssao { get; set; } = false;
    public bool DepthOfField { get; set; } = false;
    public bool ScreenFilter { get; set; } = false;

    public float MasterVolume { get; set; } = 100.0f;
    public float EffectsVolume { get; set; } = 100.0f;
    public float MusicVolume { get; set; } = 80.0f;
    public float DialogueVolume { get; set; } = 50.0f;
    public bool BoostMicrophone { get; set; } = false;
    public bool NightMode { get; set; } = false;

    public string Status { get; set; } = "Ready";

    public IntegrationSettingsViewModel()
    {
    }

    public void ResetDefaults()
    {
        this.AimingAssistance = false;
        this.MouseMovements = false;
        this.AttackDecisionEffects = false;
        this.WeaponQuickSlots = false;
        this.ResolutionIndex = 6;
        this.FullScreen = false;
        this.TexturesQualityIndex = 2;
        this.ShadowsQualityIndex = 2;
        this.FovMultiplier = 1.0f;
        this.ShowFps = false;
        this.MasterVolume = 100.0f;
        this.EffectsVolume = 100.0f;
        this.MusicVolume = 80.0f;
        this.DialogueVolume = 50.0f;
        this.BoostMicrophone = false;
        this.NightMode = false;
        this.Status = "Defaults Restored";
        this.NotifyPropertyChanged("Status");
    }

    public void ApplySettings()
    {
        this.Status = "Settings Applied";
        this.NotifyPropertyChanged("Status");
    }
}

[Observable]
public partial class MainWindow : Page
{
    public IntegrationSettingsViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new IntegrationSettingsViewModel();
        this.DataContext = this.ViewModel;
        BindingEngine.Flush();
    }

    public void ResetDefaults()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.ResetDefaults();
        }
    }

    public void ApplySettings()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.ApplySettings();
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
