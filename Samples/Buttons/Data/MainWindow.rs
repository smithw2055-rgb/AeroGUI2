module Buttons;

import Aero.UI;

[Observable]
public class ViewModel
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
        this._startCommand = new DelegateCommand();
        this._settingsCommand = new DelegateCommand();
        this._exitCommand = new DelegateCommand();
        this._startCommand.SetExecuteFunc(this.Start);
        this._settingsCommand.SetExecuteFunc(this.Settings);
        this._exitCommand.SetExecuteFunc(this.Exit);

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

[Observable]
public partial class MainWindow
{
    public ViewModel DataContext { get; set; } = null;
    public ViewModel ViewModel { get { return this.DataContext; } }

    public MainWindow()
    {
        Application_SetResources("Resources.xaml");
        this.InitializeComponent();

        this.DataContext = new ViewModel();
        this.SetDataContext<ViewModel>(this.DataContext);
    }

    public void OnInitialized()
    {
    }

    public void PlayIntro()
    {
        Storyboard_Begin(this.__nativeRootHandle, "Intro");
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
