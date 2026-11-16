module Rive;

import Aero.UI;

[Observable]
public class MenuViewModel : ViewModelBase
{
    private string _selectedMenu = "None";
    private int _stateValue = 0;
    private int _clickCount = 0;
    private string _lastAction = "Ready";
    private RelayCommand _startCommand;
    private RelayCommand _rewardsCommand;
    private RelayCommand _settingsCommand;
    private RelayCommand _resetStateCommand;

    public RelayCommand StartCommand { get { return this._startCommand; } }
    public RelayCommand RewardsCommand { get { return this._rewardsCommand; } }
    public RelayCommand SettingsCommand { get { return this._settingsCommand; } }
    public RelayCommand ResetStateCommand { get { return this._resetStateCommand; } }

    public string SelectedMenu
    {
        get { return this._selectedMenu; }
        set { this.SetProperty(ref this._selectedMenu, value, "SelectedMenu"); }
    }

    public int StateValue
    {
        get { return this._stateValue; }
        set { this.SetProperty(ref this._stateValue, value, "StateValue"); }
    }

    public int ClickCount
    {
        get { return this._clickCount; }
        set { this.SetProperty(ref this._clickCount, value, "ClickCount"); }
    }

    public string LastAction
    {
        get { return this._lastAction; }
        set { this.SetProperty(ref this._lastAction, value, "LastAction"); }
    }

    public MenuViewModel()
    {
    }

    public void BindCommands(Action onStart, Action onRewards, Action onSettings, Action onReset)
    {
        this._startCommand = new RelayCommand(onStart);
        this._rewardsCommand = new RelayCommand(onRewards);
        this._settingsCommand = new RelayCommand(onSettings);
        this._resetStateCommand = new RelayCommand(onReset);
    }
}

public partial class MainWindow : Page
{
    private MenuViewModel _vm;

    public MenuViewModel VM { get { return this._vm; } }
    public MenuViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new MenuViewModel();
        this._vm.BindCommands(
            () => { this.SelectStart(); },
            () => { this.SelectRewards(); },
            () => { this.SelectSettings(); },
            () => { this.ResetState(); });
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();

        if (this.start != null)
        {
            this.start.Click += (object s, RoutedEventArgs e) => { this.SelectStart(); };
        }
        if (this.rewards != null)
        {
            this.rewards.Click += (object s, RoutedEventArgs e) => { this.SelectRewards(); };
        }
        if (this.settings != null)
        {
            this.settings.Click += (object s, RoutedEventArgs e) => { this.SelectSettings(); };
        }
    }

    public void SelectStart()
    {
        if (this.start != null)
        {
            this.start.Focus();
        }
        this.ActivateMenu("Start", 1);
    }

    public void SelectRewards()
    {
        if (this.rewards != null)
        {
            this.rewards.Focus();
        }
        this.ActivateMenu("Rewards", 2);
    }

    public void SelectSettings()
    {
        if (this.settings != null)
        {
            this.settings.Focus();
        }
        this.ActivateMenu("Settings", 3);
    }

    public void SetState(int stateValue)
    {
        if (this.VM != null)
        {
            this.VM.StateValue = stateValue;
        }
        UIElement inputEl = this.FindName("bgInput");
        if (inputEl != null)
        {
            inputEl.SetProperty("InputValue", (float)stateValue);
        }
        BindingEngine.Flush();
    }

    public void ResetState()
    {
        if (this.VM != null)
        {
            this.VM.SelectedMenu = "None";
            this.VM.LastAction = "Reset to Idle";
        }
        this.SetState(0);
    }

    public void PlayIntro()
    {
        this.BeginStoryboard("Intro");
    }

    private void ActivateMenu(string menuName, int stateValue)
    {
        if (this.VM != null)
        {
            this.VM.SelectedMenu = menuName;
            this.VM.ClickCount = this.VM.ClickCount + 1;
            this.VM.LastAction = "Activated " + menuName + " (state " + stateValue + ")";
        }
        this.SetState(stateValue);
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
