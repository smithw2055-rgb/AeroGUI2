module Menu3D;

import Aero.UI;

public enum Platform
{
    PC,
    PS4,
    XBOX
}

[Observable]
public class MenuDescription : ObservableObject
{
    private string _description = "";
    private string _acceptText = "";
    private string _cancelText = "";

    public string Description
    {
        get { return this._description; }
        set { this.SetProperty(ref this._description, value, "Description"); }
    }

    public string AcceptText
    {
        get { return this._acceptText; }
        set { this.SetProperty(ref this._acceptText, value, "AcceptText"); }
    }

    public string CancelText
    {
        get { return this._cancelText; }
        set { this.SetProperty(ref this._cancelText, value, "CancelText"); }
    }

    public MenuDescription()
    {
    }

    public MenuDescription(string desc, string accept, string cancel)
    {
        this._description = desc == null ? "" : desc;
        this._acceptText = accept == null ? "" : accept;
        this._cancelText = cancel == null ? "" : cancel;
    }
}

[Observable]
public class MainMenu : ObservableObject
{
    private DelegateCommand _exit;
    private MenuDescription _description;
    private ViewModel _owner = null;

    public MenuDescription Description
    {
        get { return this._description; }
        set
        {
            if (this._description != value)
            {
                this._description = value;
                this.NotifyPropertyChanged("Description");
            }
        }
    }

    public DelegateCommand Exit { get { return this._exit; } }

    public ViewModel Owner
    {
        get { return this._owner; }
        set
        {
            if (this._owner != value)
            {
                this._owner = value;
                this.NotifyPropertyChanged("Owner");
            }
        }
    }

    public MainMenu()
    {
        this._description = new MenuDescription("MAIN MENU", "ACCEPT", "EXIT");
        this._exit = new DelegateCommand(() => { this.OnExit(); });
    }

    public void OnExit()
    {
        if (this._owner != null)
        {
            this._owner.OnExit();
        }
    }
}

[Observable]
public class StartMenu : ObservableObject
{
    private DelegateCommand _startCasual;
    private DelegateCommand _startNormal;
    private DelegateCommand _startExpert;
    private MenuDescription _description;
    private ViewModel _owner = null;

    public MenuDescription Description
    {
        get { return this._description; }
        set
        {
            if (this._description != value)
            {
                this._description = value;
                this.NotifyPropertyChanged("Description");
            }
        }
    }

    public DelegateCommand StartCasual { get { return this._startCasual; } }
    public DelegateCommand StartNormal { get { return this._startNormal; } }
    public DelegateCommand StartExpert { get { return this._startExpert; } }

    public ViewModel Owner
    {
        get { return this._owner; }
        set
        {
            if (this._owner != value)
            {
                this._owner = value;
                this.NotifyPropertyChanged("Owner");
            }
        }
    }

    public StartMenu()
    {
        this._description = new MenuDescription("SELECT DIFFICULTY", "ACCEPT", "BACK");
        this._startCasual = new DelegateCommand(() => { this.OnStartCasual(); });
        this._startNormal = new DelegateCommand(() => { this.OnStartNormal(); });
        this._startExpert = new DelegateCommand(() => { this.OnStartExpert(); });
    }

    public void OnStartCasual()
    {
        if (this._owner != null)
        {
            this._owner.OnStartCasual();
        }
    }

    public void OnStartNormal()
    {
        if (this._owner != null)
        {
            this._owner.OnStartNormal();
        }
    }

    public void OnStartExpert()
    {
        if (this._owner != null)
        {
            this._owner.OnStartExpert();
        }
    }
}

[Observable]
public class SettingsMenu : ObservableObject
{
    private MenuDescription _description;
    private ViewModel _owner = null;
    private int _textureDetailIndex = 1;
    private bool _antiAliasing = false;
    private int _soundVolume = 5;
    private int _videoResolutionIndex = 2;
    private int _gammaCorrection = 5;
    private bool _subtitles = false;

    public MenuDescription Description
    {
        get { return this._description; }
        set
        {
            if (this._description != value)
            {
                this._description = value;
                this.NotifyPropertyChanged("Description");
            }
        }
    }

    public ViewModel Owner
    {
        get { return this._owner; }
        set
        {
            if (this._owner != value)
            {
                this._owner = value;
                this.NotifyPropertyChanged("Owner");
            }
        }
    }

    public int TextureDetailIndex
    {
        get { return this._textureDetailIndex; }
        set { this.SetProperty(ref this._textureDetailIndex, value, "TextureDetailIndex"); }
    }

    public bool AntiAliasing
    {
        get { return this._antiAliasing; }
        set { this.SetProperty(ref this._antiAliasing, value, "AntiAliasing"); }
    }

    public int SoundVolume
    {
        get { return this._soundVolume; }
        set { this.SetProperty(ref this._soundVolume, value, "SoundVolume"); }
    }

    public int VideoResolutionIndex
    {
        get { return this._videoResolutionIndex; }
        set { this.SetProperty(ref this._videoResolutionIndex, value, "VideoResolutionIndex"); }
    }

    public int GammaCorrection
    {
        get { return this._gammaCorrection; }
        set { this.SetProperty(ref this._gammaCorrection, value, "GammaCorrection"); }
    }

    public bool Subtitles
    {
        get { return this._subtitles; }
        set { this.SetProperty(ref this._subtitles, value, "Subtitles"); }
    }

    public SettingsMenu()
    {
        this._description = new MenuDescription("SETTINGS", "ACCEPT", "BACK");
    }
}

[Observable]
public class ViewModel : ViewModelBase
{
    private Platform _platform = Platform.PC;
    private string _currentDifficulty = "";
    private string _activeMenu = "Main";
    private bool _exited = false;
    private int _startCount = 0;
    private MainMenu _mainMenu;
    private StartMenu _startMenu;
    private SettingsMenu _settingsMenu;

    public MainMenu MainMenu { get { return this._mainMenu; } }
    public StartMenu StartMenu { get { return this._startMenu; } }
    public SettingsMenu SettingsMenu { get { return this._settingsMenu; } }

    public Platform Platform
    {
        get { return this._platform; }
        set
        {
            if (this._platform != value)
            {
                this._platform = value;
                this.NotifyPropertyChanged("Platform");
            }
        }
    }

    public string CurrentDifficulty
    {
        get { return this._currentDifficulty; }
        set { this.SetProperty(ref this._currentDifficulty, value, "CurrentDifficulty"); }
    }

    public string ActiveMenu
    {
        get { return this._activeMenu; }
        set { this.SetProperty(ref this._activeMenu, value, "ActiveMenu"); }
    }

    public bool Exited
    {
        get { return this._exited; }
        set { this.SetProperty(ref this._exited, value, "Exited"); }
    }

    public int StartCount
    {
        get { return this._startCount; }
        set { this.SetProperty(ref this._startCount, value, "StartCount"); }
    }

    public ViewModel()
    {
        this._mainMenu = new MainMenu();
        this._startMenu = new StartMenu();
        this._settingsMenu = new SettingsMenu();
        this._mainMenu.Owner = this;
        this._startMenu.Owner = this;
        this._settingsMenu.Owner = this;
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

public partial class MainWindow : Page
{
    private ViewModel _vm;

    public ViewModel VM { get { return this._vm; } }
    public ViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new ViewModel();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
    }

    public void NavigateToMain()
    {
        if (this.VM != null) { this.VM.SetActiveMenu("Main"); }
    }

    public void NavigateToStart()
    {
        if (this.VM != null) { this.VM.SetActiveMenu("Start"); }
    }

    public void NavigateToSettings()
    {
        if (this.VM != null) { this.VM.SetActiveMenu("Settings"); }
    }

    public void SetPlatform(Platform platform)
    {
        if (this.VM != null) { this.VM.SetPlatform(platform); }
    }

    public void StartCasual()
    {
        if (this.VM != null) { this.VM.StartMenu.StartCasual.Execute(null); }
    }

    public void StartNormal()
    {
        if (this.VM != null) { this.VM.StartMenu.StartNormal.Execute(null); }
    }

    public void StartExpert()
    {
        if (this.VM != null) { this.VM.StartMenu.StartExpert.Execute(null); }
    }

    public void Exit()
    {
        if (this.VM != null) { this.VM.MainMenu.Exit.Execute(null); }
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
