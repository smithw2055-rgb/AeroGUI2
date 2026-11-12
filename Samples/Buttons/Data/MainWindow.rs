module Buttons;

import Aero.UI;

[Observable]
public class ViewModel
{
    public string LastAction { get; set; } = "";
    public string State { get; set; } = "Menu";
    public int StartCount { get; set; } = 0;
    public int SettingsCount { get; set; } = 0;
    public int ExitCount { get; set; } = 0;
    public bool IsExited { get; set; } = false;

    public ViewModel()
    {
        this.LastAction = "";
        this.State = "Menu";
        this.StartCount = 0;
        this.SettingsCount = 0;
        this.ExitCount = 0;
        this.IsExited = false;
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
    public ViewModel ViewModel { get; set; }

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
    }

    public void OnInitialized()
    {
    }

    public void Start()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.Start();
        }
    }

    public void Settings()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.Settings();
        }
    }

    public void Exit()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.Exit();
        }
    }

    public void StartCommand()
    {
        this.Start();
    }

    public void SettingsCommand()
    {
        this.Settings();
    }

    public void ExitCommand()
    {
        this.Exit();
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
