module Theme;

import Aero.UI;

public class AccentPalette
{
    public string Name { get; set; } = "Blue";
    public string Accent { get; set; } = "#FF1098DF";
    public string Accent0 { get; set; } = "#FF56BDEC";
    public string Accent1 { get; set; } = "#FF2EAEEB";
    public string Accent2 { get; set; } = "#FF0B80C1";
    public string Accent3 { get; set; } = "#FF0864A1";

    public AccentPalette(string name, string accent, string accent0, string accent1, string accent2, string accent3)
    {
        this.Name = name;
        this.Accent = accent;
        this.Accent0 = accent0;
        this.Accent1 = accent1;
        this.Accent2 = accent2;
        this.Accent3 = accent3;
    }
}

[Observable]
public class ThemeViewModel : ViewModelBase
{
    private bool _isDarkTheme = true;
    private string _currentAccent = "Blue";
    private ObservableList<string> _accents;
    private RelayCommand _toggleThemeCommand;
    private RelayCommand _setDarkCommand;
    private RelayCommand _setLightCommand;
    private RelayCommand _nextAccentCommand;

    public ObservableList<string> Accents { get { return this._accents; } }
    public RelayCommand ToggleThemeCommand { get { return this._toggleThemeCommand; } }
    public RelayCommand SetDarkCommand { get { return this._setDarkCommand; } }
    public RelayCommand SetLightCommand { get { return this._setLightCommand; } }
    public RelayCommand NextAccentCommand { get { return this._nextAccentCommand; } }

    public bool IsDarkTheme
    {
        get { return this._isDarkTheme; }
        set
        {
            if (this.SetProperty(ref this._isDarkTheme, value, "IsDarkTheme"))
            {
                this.NotifyPropertyChanged("ThemeLabel");
            }
        }
    }

    public string CurrentAccent
    {
        get { return this._currentAccent; }
        set
        {
            if (this.SetProperty(ref this._currentAccent, value, "CurrentAccent"))
            {
                this.NotifyPropertyChanged("ThemeLabel");
            }
        }
    }

    public string ThemeLabel
    {
        get
        {
            string mode = this._isDarkTheme ? "Dark" : "Light";
            return mode + " / " + this._currentAccent;
        }
    }

    public ThemeViewModel()
    {
        this._accents = new ObservableList<string>();
        this._accents.Add("Blue");
        this._accents.Add("Red");
        this._accents.Add("Green");
        this._accents.Add("Orange");
        this._accents.Add("Emerald");
        this._accents.Add("Purple");
        this._accents.Add("Crimson");
        this._accents.Add("Lime");
        this._accents.Add("Aqua");
    }

    public void BindCommands(Action onToggle, Action onDark, Action onLight, Action onNextAccent)
    {
        this._toggleThemeCommand = new RelayCommand(onToggle);
        this._setDarkCommand = new RelayCommand(onDark);
        this._setLightCommand = new RelayCommand(onLight);
        this._nextAccentCommand = new RelayCommand(onNextAccent);
    }
}

public partial class MainWindow : Window
{
    private ThemeViewModel _vm;

    public ThemeViewModel VM { get { return this._vm; } }
    public ThemeViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new ThemeViewModel();
        this._vm.BindCommands(
            () => { this.ToggleTheme(); },
            () => { this.SetDarkTheme(); },
            () => { this.SetLightTheme(); },
            () => { this.NextAccent(); });
        this.DataContext = this._vm;

        this.ApplyTheme(true, "Blue");
        this.InitializeComponent();
        this.RefreshThemeVisuals();
        BindingEngine.Flush();
    }

    public void ToggleTheme()
    {
        bool nextDark = this.VM != null ? !this.VM.IsDarkTheme : !ThemeManager.IsDarkTheme();
        string accent = this.VM != null ? this.VM.CurrentAccent : "Blue";
        this.ApplyTheme(nextDark, accent);
    }

    public void SetDarkTheme()
    {
        string accent = this.VM != null ? this.VM.CurrentAccent : "Blue";
        this.ApplyTheme(true, accent);
    }

    public void SetLightTheme()
    {
        string accent = this.VM != null ? this.VM.CurrentAccent : "Blue";
        this.ApplyTheme(false, accent);
    }

    public void SetAccent(string accentName)
    {
        bool isDark = this.VM != null ? this.VM.IsDarkTheme : ThemeManager.IsDarkTheme();
        this.ApplyTheme(isDark, accentName);
    }

    public void NextAccent()
    {
        if (this.VM == null || this.VM.Accents == null || this.VM.Accents.Count() == 0)
        {
            return;
        }
        int idx = this.VM.Accents.IndexOf(this.VM.CurrentAccent);
        int nextIdx = (idx + 1) % this.VM.Accents.Count();
        this.SetAccent(this.VM.Accents.Get(nextIdx));
    }

    public void ApplyTheme(bool isDark, string accentName)
    {
        AccentPalette palette = ResolveAccent(accentName);

        if (this.VM != null)
        {
            this.VM.IsDarkTheme = isDark;
            this.VM.CurrentAccent = palette.Name;
        }

        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        if (isDark)
        {
            ThemeManager.ApplyAeroDarkTheme(appRes);
        }
        else
        {
            ThemeManager.ApplyAeroLightTheme(appRes);
        }
        PopulateNoesisThemeTokens(appRes, isDark, palette);

        if (this.Resources != null)
        {
            if (isDark)
            {
                ThemeManager.ApplyAeroDarkTheme(this.Resources);
            }
            else
            {
                ThemeManager.ApplyAeroLightTheme(this.Resources);
            }
            PopulateNoesisThemeTokens(this.Resources, isDark, palette);
        }

        string winBg = isDark ? "#FF20282F" : "#FFF6F7F8";
        string fgColor = isDark ? "#FFE4E6E8" : "#FF20282F";
        this.Background = new SolidColorBrush(winBg);
        this.Foreground = new SolidColorBrush(fgColor);

        this.RefreshThemeVisuals();
        BindingEngine.Flush();
    }

    private void RefreshThemeVisuals()
    {
        this.RefreshDynamicResourcesRecursive();
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        Brush winBrush = appRes.Get("Brush.Window.Background") as Brush;
        Brush fgBrush = appRes.Get("Brush.Foreground.Normal") as Brush;
        Brush borderBrush = appRes.Get("Brush.Border.Normal") as Brush;
        Style toggleSwitchStyle = appRes.Get("Style.ToggleSwitch") as Style;

        if (winBrush != null)
        {
            this.Background = winBrush;
        }
        if (fgBrush != null)
        {
            this.Foreground = fgBrush;
        }

        this.ApplyThemeRecursive(this, winBrush, fgBrush, borderBrush, toggleSwitchStyle, 0);
    }

    private void ApplyThemeRecursive(
        UIElement node,
        Brush winBrush,
        Brush fgBrush,
        Brush borderBrush,
        Style toggleSwitchStyle,
        int depth)
    {
        if (node == null)
        {
            return;
        }

        if (depth == 2 && node is Grid && winBrush != null)
        {
            Grid innerGrid = (Grid)node;
            innerGrid.Background = winBrush;
        }
        if (node is ListBox && borderBrush != null)
        {
            ListBox lb = (ListBox)node;
            lb.BorderBrush = borderBrush;
        }
        if (node is TreeView && borderBrush != null)
        {
            TreeView tv = (TreeView)node;
            tv.BorderBrush = borderBrush;
        }
        if (node is ToggleButton && toggleSwitchStyle != null)
        {
            ToggleButton tb = (ToggleButton)node;
            if (tb.Style != null)
            {
                tb.Style.EvaluateTriggers(tb);
            }
        }
        if (node is Control && fgBrush != null)
        {
            Control ctrl = (Control)node;
            ctrl.Foreground = fgBrush;
        }
        else if (node is TextBlock && fgBrush != null)
        {
            TextBlock text = (TextBlock)node;
            text.Foreground = fgBrush;
        }

        int count = node.ChildCount();
        for (int i = 0; i < count; i += 1)
        {
            this.ApplyThemeRecursive(node.ChildAt(i), winBrush, fgBrush, borderBrush, toggleSwitchStyle, depth + 1);
        }
    }

    private static AccentPalette ResolveAccent(string accentName)
    {
        if (accentName == "Red")
        {
            return new AccentPalette("Red", "#FFDE2B10", "#FFED6755", "#FFEB452F", "#FFC2240C", "#FFA11F08");
        }
        if (accentName == "Green")
        {
            return new AccentPalette("Green", "#FF2AA60C", "#FF5AB541", "#FF43B324", "#FF218C08", "#FF187306");
        }
        if (accentName == "Orange")
        {
            return new AccentPalette("Orange", "#FFDF9910", "#FFECB556", "#FFEBA62E", "#FFC1840B", "#FFA17608");
        }
        if (accentName == "Emerald")
        {
            return new AccentPalette("Emerald", "#FF10DE62", "#FF55ED8D", "#FF2FEB74", "#FF0CC254", "#FF08A14A");
        }
        if (accentName == "Purple")
        {
            return new AccentPalette("Purple", "#FF894EED", "#FFA67FF5", "#FF9D71F5", "#FF7743D1", "#FF6737B0");
        }
        if (accentName == "Crimson")
        {
            return new AccentPalette("Crimson", "#FFDE1043", "#FFED5580", "#FFEB2F64", "#FFC20C39", "#FFA10829");
        }
        if (accentName == "Lime")
        {
            return new AccentPalette("Lime", "#FF99DF10", "#FFBDED55", "#FFAFEB2F", "#FF85C20C", "#FF69A108");
        }
        if (accentName == "Aqua")
        {
            return new AccentPalette("Aqua", "#FF10DDDF", "#FF55EDED", "#FF2FEBEB", "#FF0CBFC2", "#FF0899A1");
        }
        return new AccentPalette("Blue", "#FF1098DF", "#FF56BDEC", "#FF2EAEEB", "#FF0B80C1", "#FF0864A1");
    }

    private static void PopulateNoesisThemeTokens(ResourceDictionary dict, bool isDark, AccentPalette accent)
    {
        if (dict == null)
        {
            return;
        }

        string cBlack = isDark ? "#FFE4E6E8" : "#FF20282F";
        string cGray0 = isDark ? "#FFBDC0C4" : "#FF41484E";
        string cGray1 = isDark ? "#FF9FA3A7" : "#FF5D6469";
        string cGray2 = isDark ? "#FF82878C" : "#FF7B8085";
        string cGray3 = isDark ? "#FF6D7378" : "#FF93979B";
        string cGray4 = isDark ? "#FF595F65" : "#FFABAEB2";
        string cGray5 = isDark ? "#FF4B5258" : "#FFBBBEC1";
        string cGray6 = isDark ? "#FF3E464C" : "#FFCBCED0";
        string cGray7 = isDark ? "#FF323940" : "#FFDCDEE0";
        string cGray8 = isDark ? "#FF2C343A" : "#FFE5E6E8";
        string cGray9 = isDark ? "#FF262E35" : "#FFEDEFF0";
        string cWhite = isDark ? "#FF20282F" : "#FFF6F7F8";

        dict.Set("Color.Black", cBlack);
        dict.Set("Color.Gray0", cGray0);
        dict.Set("Color.Gray1", cGray1);
        dict.Set("Color.Gray2", cGray2);
        dict.Set("Color.Gray3", cGray3);
        dict.Set("Color.Gray4", cGray4);
        dict.Set("Color.Gray5", cGray5);
        dict.Set("Color.Gray6", cGray6);
        dict.Set("Color.Gray7", cGray7);
        dict.Set("Color.Gray8", cGray8);
        dict.Set("Color.Gray9", cGray9);
        dict.Set("Color.White", cWhite);

        dict.Set("Color.Accent", accent.Accent);
        dict.Set("Color.Accent0", accent.Accent0);
        dict.Set("Color.Accent1", accent.Accent1);
        dict.Set("Color.Accent2", accent.Accent2);
        dict.Set("Color.Accent3", accent.Accent3);

        dict.Set("Brush.Window.Background", new SolidColorBrush(cWhite));
        dict.Set("Brush.Foreground.Normal", new SolidColorBrush(cBlack));
        dict.Set("Brush.Foreground.Placeholder", new SolidColorBrush(cGray2));
        dict.Set("Brush.Foreground.Disabled", new SolidColorBrush(cGray2));
        dict.Set("Brush.Foreground.DisabledChecked", new SolidColorBrush(accent.Accent0));
        dict.Set("Brush.Foreground.DisabledPlaceholder", new SolidColorBrush(cGray4));

        dict.Set("Brush.Background.Normal", new SolidColorBrush(cGray5));
        dict.Set("Brush.Background.Pressed", new SolidColorBrush(cGray3));
        dict.Set("Brush.Background.Checked", new SolidColorBrush(accent.Accent2));
        dict.Set("Brush.Background.Popup", new SolidColorBrush(cGray9));

        dict.Set("Brush.Border.Normal", new SolidColorBrush(cGray3));
        dict.Set("Brush.Border.Over", new SolidColorBrush(cGray1));
        dict.Set("Brush.Border.Pressed", new SolidColorBrush(cGray2));
        dict.Set("Brush.Border.Checked", new SolidColorBrush(accent.Accent1));
        dict.Set("Brush.Border.Focus", new SolidColorBrush(cBlack));
        dict.Set("Brush.Border.FocusAccent", new SolidColorBrush(accent.Accent));
        dict.Set("Brush.Border.Disabled", new SolidColorBrush(cGray5));
        dict.Set("Brush.Border.DisabledChecked", new SolidColorBrush(accent.Accent3));
        dict.Set("Brush.Border.Popup", new SolidColorBrush(cGray6));

        dict.Set("Brush.Track.Normal", new SolidColorBrush(cGray7));
        dict.Set("Brush.Thumb.Normal", new SolidColorBrush(cGray3));
        dict.Set("Brush.Thumb.Over", new SolidColorBrush(cGray1));
        dict.Set("Brush.Thumb.Pressed", new SolidColorBrush(cGray1));
        dict.Set("Brush.Thumb.Disabled", new SolidColorBrush(cGray5));

        dict.Set("Brush.TextBox.Normal", new SolidColorBrush(cGray9));
        dict.Set("Brush.TextBox.Focused", new SolidColorBrush(cWhite));
        dict.Set("Brush.Item.Over", new SolidColorBrush(cGray6));
        dict.Set("Brush.Item.Selected", new SolidColorBrush(accent.Accent3));
        dict.Set("Brush.Item.SelectedOver", new SolidColorBrush(accent.Accent2));

        dict.Set("AeroAccentBrush", new SolidColorBrush(accent.Accent));
        dict.Set("AeroAccentHoverBrush", new SolidColorBrush(accent.Accent1));
        dict.Set("AeroAccentPressedBrush", new SolidColorBrush(accent.Accent3));

        var toggleSwitchStyle = new Style("ToggleButton");
        toggleSwitchStyle.AddSetter("Background", cGray5);
        toggleSwitchStyle.AddSetter("Foreground", cBlack);
        toggleSwitchStyle.AddSetter("BorderBrush", cGray3);
        toggleSwitchStyle.AddSetter("BorderThickness", "1");
        toggleSwitchStyle.AddSetter("CornerRadius", "12");
        toggleSwitchStyle.AddSetter("Padding", "10,4,10,4");
        var swChecked = new Trigger("IsChecked", true);
        swChecked.AddSetter("Background", accent.Accent2);
        swChecked.AddSetter("BorderBrush", accent.Accent1);
        toggleSwitchStyle.AddTrigger(swChecked);
        var swDisabled = new Trigger("IsEnabled", false);
        swDisabled.AddSetter("Opacity", 0.5f);
        toggleSwitchStyle.AddTrigger(swDisabled);
        dict.Set("Style.ToggleSwitch", toggleSwitchStyle);
    }
}

public static Window CreateMainWindow()
{
    return new MainWindow();
}
