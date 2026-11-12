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
    public string Description { get; set; }
    public string AcceptText { get; set; }
    public string CancelText { get; set; }

    public MenuDescription()
    {
        this.Description = "";
        this.AcceptText = "";
        this.CancelText = "";
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
    public MenuDescription Description { get; set; }
    public MainWindow Window { get; set; }
    public ViewModel Owner { get; set; }

    public MainMenu()
    {
        this.Description = new MenuDescription("MAIN MENU", "ACCEPT", "EXIT");
        this.Window = null;
        this.Owner = null;
    }

    public void Exit()
    {
        if (this.Window != null)
        {
            this.Window.Exit();
        }
        else if (this.Owner != null)
        {
            this.Owner.OnExit();
        }
    }

    public void OnExit()
    {
        this.Exit();
    }
}

[Observable]
public class StartMenu
{
    public MenuDescription Description { get; set; }
    public MainWindow Window { get; set; }
    public ViewModel Owner { get; set; }

    public StartMenu()
    {
        this.Description = new MenuDescription("SELECT DIFFICULTY", "ACCEPT", "BACK");
        this.Window = null;
        this.Owner = null;
    }

    public void StartCasual()
    {
        if (this.Window != null)
        {
            this.Window.StartCasual();
        }
        else if (this.Owner != null)
        {
            this.Owner.OnStartCasual();
        }
    }

    public void StartNormal()
    {
        if (this.Window != null)
        {
            this.Window.StartNormal();
        }
        else if (this.Owner != null)
        {
            this.Owner.OnStartNormal();
        }
    }

    public void StartExpert()
    {
        if (this.Window != null)
        {
            this.Window.StartExpert();
        }
        else if (this.Owner != null)
        {
            this.Owner.OnStartExpert();
        }
    }

    public void OnStartCasual()
    {
        this.StartCasual();
    }

    public void OnStartNormal()
    {
        this.StartNormal();
    }

    public void OnStartExpert()
    {
        this.StartExpert();
    }
}

[Observable]
public class SettingsMenu
{
    public MenuDescription Description { get; set; }
    public MainWindow Window { get; set; }
    public ViewModel Owner { get; set; }
    public int TextureDetailIndex { get; set; }
    public bool AntiAliasing { get; set; }
    public int SoundVolume { get; set; }
    public int VideoResolutionIndex { get; set; }
    public int GammaCorrection { get; set; }
    public bool Subtitles { get; set; }

    public SettingsMenu()
    {
        this.Description = new MenuDescription("SETTINGS", "ACCEPT", "BACK");
        this.Window = null;
        this.Owner = null;
        this.TextureDetailIndex = 1;
        this.AntiAliasing = false;
        this.SoundVolume = 5;
        this.VideoResolutionIndex = 2;
        this.GammaCorrection = 5;
        this.Subtitles = false;
    }
}

[Observable]
public class ViewModel
{
    public MainMenu MainMenu { get; set; }
    public StartMenu StartMenu { get; set; }
    public SettingsMenu SettingsMenu { get; set; }
    public Platform Platform { get; set; }
    public string CurrentDifficulty { get; set; }
    public string ActiveMenu { get; set; }
    public bool Exited { get; set; }
    public int StartCount { get; set; }

    public ViewModel()
    {
        this.MainMenu = new MainMenu();
        this.StartMenu = new StartMenu();
        this.SettingsMenu = new SettingsMenu();
        this.MainMenu.Owner = this;
        this.StartMenu.Owner = this;
        this.SettingsMenu.Owner = this;

        this.Platform = Platform.PC;
        this.CurrentDifficulty = "";
        this.ActiveMenu = "Main";
        this.Exited = false;
        this.StartCount = 0;
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
    public ViewModel ViewModel { get; set; }

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
        this.ViewModel.MainMenu.Window = this;
        this.ViewModel.StartMenu.Window = this;
        this.ViewModel.SettingsMenu.Window = this;
    }

    public void OnInitialized()
    {
    }

    public void Exit()
    {
        this.ViewModel.OnExit();
    }

    public void OnExit()
    {
        this.ViewModel.OnExit();
    }

    public void StartCasual()
    {
        this.ViewModel.OnStartCasual();
    }

    public void OnStartCasual()
    {
        this.ViewModel.OnStartCasual();
    }

    public void StartNormal()
    {
        this.ViewModel.OnStartNormal();
    }

    public void OnStartNormal()
    {
        this.ViewModel.OnStartNormal();
    }

    public void StartExpert()
    {
        this.ViewModel.OnStartExpert();
    }

    public void OnStartExpert()
    {
        this.ViewModel.OnStartExpert();
    }

    public void NavigateToMain()
    {
        this.ViewModel.SetActiveMenu("Main");
    }

    public void NavigateToStart()
    {
        this.ViewModel.SetActiveMenu("Start");
    }

    public void NavigateToSettings()
    {
        this.ViewModel.SetActiveMenu("Settings");
    }

    public void SetPlatform(Platform platform)
    {
        this.ViewModel.SetPlatform(platform);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
