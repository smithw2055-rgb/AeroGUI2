module Menu3D;

import Aero.UI;

public enum Platform
{
    PC,
    PS4,
    XBOX
}

[Observable]
public class MenuDescription
{
    public string Description { get; set; } = "";
    public string AcceptText { get; set; } = "";
    public string CancelText { get; set; } = "";

    public MenuDescription()
    {
    }

    public MenuDescription(string desc, string accept, string cancel)
    {
        this.Description = desc;
        this.AcceptText = accept;
        this.CancelText = cancel;
    }
}

[Observable]
public class MainMenu
{
    private DelegateCommand _exit;

    public MenuDescription Description { get; set; }
    public DelegateCommand Exit { get { return this._exit; } }
    public ViewModel Owner { get; set; } = null;

    public MainMenu()
    {
        this.Description = new MenuDescription("MAIN MENU", "ACCEPT", "EXIT");
        this._exit = new DelegateCommand(this.OnExit);
    }

    public void OnExit()
    {
        if (this.Owner != null)
        {
            this.Owner.OnExit();
        }
    }
}

[Observable]
public class StartMenu
{
    private DelegateCommand _startCasual;
    private DelegateCommand _startNormal;
    private DelegateCommand _startExpert;

    public MenuDescription Description { get; set; }
    public DelegateCommand StartCasual { get { return this._startCasual; } }
    public DelegateCommand StartNormal { get { return this._startNormal; } }
    public DelegateCommand StartExpert { get { return this._startExpert; } }
    public ViewModel Owner { get; set; } = null;

    public StartMenu()
    {
        this.Description = new MenuDescription("SELECT DIFFICULTY", "ACCEPT", "BACK");
        this._startCasual = new DelegateCommand(this.OnStartCasual);
        this._startNormal = new DelegateCommand(this.OnStartNormal);
        this._startExpert = new DelegateCommand(this.OnStartExpert);
    }

    public void OnStartCasual()
    {
        if (this.Owner != null)
        {
            this.Owner.OnStartCasual();
        }
    }

    public void OnStartNormal()
    {
        if (this.Owner != null)
        {
            this.Owner.OnStartNormal();
        }
    }

    public void OnStartExpert()
    {
        if (this.Owner != null)
        {
            this.Owner.OnStartExpert();
        }
    }
}

[Observable]
public class SettingsMenu
{
    public MenuDescription Description { get; set; }
    public ViewModel Owner { get; set; } = null;
    public int TextureDetailIndex { get; set; } = 1;
    public bool AntiAliasing { get; set; } = false;
    public int SoundVolume { get; set; } = 5;
    public int VideoResolutionIndex { get; set; } = 2;
    public int GammaCorrection { get; set; } = 5;
    public bool Subtitles { get; set; } = false;

    public SettingsMenu()
    {
        this.Description = new MenuDescription("SETTINGS", "ACCEPT", "BACK");
    }
}

[Observable]
public class ViewModel
{
    public MainMenu MainMenu { get; set; }
    public StartMenu StartMenu { get; set; }
    public SettingsMenu SettingsMenu { get; set; }
    public Platform Platform { get; set; } = Platform.PC;
    public string CurrentDifficulty { get; set; } = "";
    public string ActiveMenu { get; set; } = "Main";
    public bool Exited { get; set; } = false;
    public int StartCount { get; set; } = 0;

    public ViewModel()
    {
        this.MainMenu = new MainMenu();
        this.StartMenu = new StartMenu();
        this.SettingsMenu = new SettingsMenu();
        this.MainMenu.Owner = this;
        this.StartMenu.Owner = this;
        this.SettingsMenu.Owner = this;
    }

    public void OnExit()
    {
        this.Exited = true;
    }

    public void OnStartCasual()
    {
        this.CurrentDifficulty = "Casual";
        this.StartCount = this.StartCount + 1;
    }

    public void OnStartNormal()
    {
        this.CurrentDifficulty = "Normal";
        this.StartCount = this.StartCount + 1;
    }

    public void OnStartExpert()
    {
        this.CurrentDifficulty = "Expert";
        this.StartCount = this.StartCount + 1;
    }

    public void SetPlatform(Platform platform)
    {
        this.Platform = platform;
    }

    public void SetActiveMenu(string menuName)
    {
        this.ActiveMenu = menuName;
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

    public void NavigateToMain()
    {
        this.DataContext.SetActiveMenu("Main");
    }

    public void NavigateToStart()
    {
        this.DataContext.SetActiveMenu("Start");
    }

    public void NavigateToSettings()
    {
        this.DataContext.SetActiveMenu("Settings");
    }

    public void SetPlatform(Platform platform)
    {
        this.DataContext.SetPlatform(platform);
    }

    public void StartCasual()
    {
        this.DataContext.StartMenu.StartCasual.Execute();
    }

    public void StartNormal()
    {
        this.DataContext.StartMenu.StartNormal.Execute();
    }

    public void StartExpert()
    {
        this.DataContext.StartMenu.StartExpert.Execute();
    }

    public void Exit()
    {
        this.DataContext.MainMenu.Exit.Execute();
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
