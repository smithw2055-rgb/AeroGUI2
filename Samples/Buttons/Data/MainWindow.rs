module Buttons;

import Aero.UI;

[Observable]
public class ViewModel : ViewModelBase
{
    private DelegateCommand _startCommand;
    private DelegateCommand _settingsCommand;
    private DelegateCommand _exitCommand;

    public DelegateCommand StartCommand { get { return this._startCommand; } }
    public DelegateCommand SettingsCommand { get { return this._settingsCommand; } }
    public DelegateCommand ExitCommand { get { return this._exitCommand; } }

    public string LastAction { get; set; } = "";
    public string State { get; set; } = "Menu";
    public int StartCount { get; set; } = 0;
    public int SettingsCount { get; set; } = 0;
    public int ExitCount { get; set; } = 0;
    public bool IsExited { get; set; } = false;

    public ViewModel()
    {
        this._startCommand = new DelegateCommand(() => { this.Start(); });
        this._settingsCommand = new DelegateCommand(() => { this.Settings(); });
        this._exitCommand = new DelegateCommand(() => { this.Exit(); });

        this.Reset();
    }

    public void Start()
    {
        this.LastAction = "Start Game";
        this.State = "Started";
        this.StartCount = this.StartCount + 1;
    }

    public void Settings()
    {
        this.LastAction = "Change Settings";
        this.State = "Settings";
        this.SettingsCount = this.SettingsCount + 1;
    }

    public void Exit()
    {
        this.LastAction = "Exit Game";
        this.State = "Exited";
        this.ExitCount = this.ExitCount + 1;
        this.IsExited = true;
    }

    public void Reset()
    {
        this.LastAction = "";
        this.State = "Menu";
        this.StartCount = 0;
        this.SettingsCount = 0;
        this.ExitCount = 0;
        this.IsExited = false;
    }
}

public partial class MainWindow : Page
{
    public ViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();

        this.ViewModel = new ViewModel();
        this.DataContext = this.ViewModel;
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
    }

    public void PlayIntro()
    {
        this.BeginStoryboard("Intro");
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
