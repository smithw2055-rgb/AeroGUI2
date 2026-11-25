module Gallery;
import Aero.UI;

[Observable]
public class Sample : ViewModelBase
{
    public string Name { get; set; } = "";
    public string Icon { get; set; } = "";
    public string XamlPath { get; set; } = "";

    public Sample()
    {
    }

    public Sample(string name, string icon)
    {
        this.Name = name;
        this.Icon = icon;
        this.XamlPath = "Samples/" + name + ".xaml";
    }
}

[Observable]
public class Category : ViewModelBase
{
    public string Name { get; set; } = "";
    public ObservableList<Sample> Samples { get; set; } = null;

    public Category()
    {
        this.Samples = new ObservableList<Sample>();
    }

    public Category(string name)
    {
        this.Name = name;
        this.Samples = new ObservableList<Sample>();
    }
}

[Observable]
public class ColorItem : ViewModelBase
{
    public string Name { get; set; } = "";
    public string Color { get; set; } = "#FF000000";

    public ColorItem()
    {
    }

    public ColorItem(string name, string color)
    {
        this.Name = name;
        this.Color = color;
    }
}

[Observable]
public class ViewModel : ViewModelBase
{
    public MainWindow Window { get; set; } = null;
    public ObservableList<Category> Categories { get; set; } = null;
    public ObservableList<ColorItem> ThemeColors { get; set; } = null;
    public ObservableList<ColorItem> ThemeAccents { get; set; } = null;

    private Sample _selectedSample;
    private ColorItem _selectedThemeColor;
    private ColorItem _selectedThemeAccent;

    public Sample SelectedSample
    {
        get { return this._selectedSample; }
        set
        {
            if (this._selectedSample != value)
            {
                this._selectedSample = value;
                this.NotifyPropertyChanged("SelectedSample");
                if (this.Window != null && value != null)
                {
                    this.Window.UpdateSample(value.Name);
                }
            }
        }
    }

    public ColorItem SelectedThemeColor
    {
        get { return this._selectedThemeColor; }
        set
        {
            if (this._selectedThemeColor != value && value != null)
            {
                this._selectedThemeColor = value;
                this.NotifyPropertyChanged("SelectedThemeColor");
                if (this.Window != null)
                {
                    this.Window.ApplyTheme();
                }
            }
        }
    }

    public ColorItem SelectedThemeAccent
    {
        get { return this._selectedThemeAccent; }
        set
        {
            if (this._selectedThemeAccent != value && value != null)
            {
                this._selectedThemeAccent = value;
                this.NotifyPropertyChanged("SelectedThemeAccent");
                if (this.Window != null)
                {
                    this.Window.ApplyTheme();
                }
            }
        }
    }

    public ViewModel()
    {
        this.InitCatalog(null);
    }

    public ViewModel(MainWindow owner)
    {
        this.InitCatalog(owner);
    }

    private void InitCatalog(MainWindow owner)
    {
        this.Categories = new ObservableList<Category>(8);

        Category basicInput = new Category("Basic Input");
        basicInput.Samples.Add(new Sample("Button", "A"));
        basicInput.Samples.Add(new Sample("RepeatButton", "B"));
        basicInput.Samples.Add(new Sample("ToggleButton", "C"));
        basicInput.Samples.Add(new Sample("CheckBox", "D"));
        basicInput.Samples.Add(new Sample("RadioButton", "E"));
        basicInput.Samples.Add(new Sample("Slider", "F"));
        this.Categories.Add(basicInput);

        Category textCat = new Category("Text");
        textCat.Samples.Add(new Sample("TextBlock", "G"));
        textCat.Samples.Add(new Sample("TextBox", "H"));
        textCat.Samples.Add(new Sample("PasswordBox", "I"));
        textCat.Samples.Add(new Sample("Hyperlink", "J"));
        this.Categories.Add(textCat);

        Category collections = new Category("Collections");
        collections.Samples.Add(new Sample("ItemsControl", "K"));
        collections.Samples.Add(new Sample("ComboBox", "L"));
        collections.Samples.Add(new Sample("ListBox", "M"));
        collections.Samples.Add(new Sample("ListView", "N"));
        collections.Samples.Add(new Sample("TreeView", "O"));
        collections.Samples.Add(new Sample("TabControl", "P"));
        this.Categories.Add(collections);

        Category layout = new Category("Layout");
        layout.Samples.Add(new Sample("Canvas", "Q"));
        layout.Samples.Add(new Sample("StackPanel", "R"));
        layout.Samples.Add(new Sample("WrapPanel", "S"));
        layout.Samples.Add(new Sample("DockPanel", "T"));
        layout.Samples.Add(new Sample("Grid", "U"));
        layout.Samples.Add(new Sample("UniformGrid", "V"));
        layout.Samples.Add(new Sample("GroupBox", "W"));
        layout.Samples.Add(new Sample("Expander", "X"));
        layout.Samples.Add(new Sample("ScrollViewer", "Y"));
        this.Categories.Add(layout);

        Category menusAndToolbars = new Category("Menus and Toolbars");
        menusAndToolbars.Samples.Add(new Sample("Menu", "Z"));
        menusAndToolbars.Samples.Add(new Sample("ContextMenu", "Z"));
        menusAndToolbars.Samples.Add(new Sample("ToolBar", "a"));
        this.Categories.Add(menusAndToolbars);

        Category statusAndInfo = new Category("Status and Info");
        statusAndInfo.Samples.Add(new Sample("StatusBar", "b"));
        statusAndInfo.Samples.Add(new Sample("ProgressBar", "c"));
        statusAndInfo.Samples.Add(new Sample("ToolTip", "d"));
        this.Categories.Add(statusAndInfo);

        Category media = new Category("Media");
        media.Samples.Add(new Sample("Brushes", "e"));
        media.Samples.Add(new Sample("Image", "f"));
        media.Samples.Add(new Sample("Effects", "g"));
        media.Samples.Add(new Sample("Blending", "w"));
        media.Samples.Add(new Sample("Animation", "h"));
        this.Categories.Add(media);

        this.ThemeColors = new ObservableList<ColorItem>(2);
        this.ThemeColors.Add(new ColorItem("Dark", "#FF101418"));
        this.ThemeColors.Add(new ColorItem("Light", "#FFF6F7F8"));

        this.ThemeAccents = new ObservableList<ColorItem>(9);
        this.ThemeAccents.Add(new ColorItem("Red", "#FFDE2B10"));
        this.ThemeAccents.Add(new ColorItem("Green", "#FF2AA60C"));
        this.ThemeAccents.Add(new ColorItem("Blue", "#FF1098DF"));
        this.ThemeAccents.Add(new ColorItem("Orange", "#FFDF9910"));
        this.ThemeAccents.Add(new ColorItem("Emerald", "#FF10DE62"));
        this.ThemeAccents.Add(new ColorItem("Purple", "#FF894EED"));
        this.ThemeAccents.Add(new ColorItem("Crimson", "#FFDE1043"));
        this.ThemeAccents.Add(new ColorItem("Lime", "#FF99DF10"));
        this.ThemeAccents.Add(new ColorItem("Aqua", "#FF10DDDF"));

        this._selectedSample = null;
        this._selectedThemeColor = this.ThemeColors.Get(1);
        this._selectedThemeAccent = this.ThemeAccents.Get(2);
        this.Window = owner;
    }

    public Sample FindSample(string sampleName)
    {
        if (sampleName == null || sampleName == "")
        {
            return null;
        }
        int catCount = this.Categories.Count();
        int c = 0;
        while (c < catCount)
        {
            Category cat = this.Categories.Get(c);
            int sCount = cat.Samples.Count();
            int s = 0;
            while (s < sCount)
            {
                Sample item = cat.Samples.Get(s);
                if (item.Name == sampleName)
                {
                    return item;
                }
                s = s + 1;
            }
            c = c + 1;
        }
        return null;
    }

    public ColorItem FindThemeColor(string colorName)
    {
        int n = this.ThemeColors.Count();
        int i = 0;
        while (i < n)
        {
            ColorItem item = this.ThemeColors.Get(i);
            if (item.Name == colorName)
            {
                return item;
            }
            i = i + 1;
        }
        return null;
    }

    public ColorItem FindThemeAccent(string accentName)
    {
        int n = this.ThemeAccents.Count();
        int i = 0;
        while (i < n)
        {
            ColorItem item = this.ThemeAccents.Get(i);
            if (item.Name == accentName)
            {
                return item;
            }
            i = i + 1;
        }
        return null;
    }
}

[Observable]
public partial class MainWindow : Window
{
    public ViewModel ViewModel { get; set; } = null;
    public int ActiveContainerIndex { get; set; } = 0;
    public string LoadedSampleUri { get; set; } = "";
    private bool _lastLoadSucceeded;
    private string _activeThemeBg = "#FFF6F7F8";
    private string _activeThemeSurface = "#FFFFFFFF";
    private string _activeThemeFg = "#FF18202A";
    private string _activeThemeBorder = "#FFD0D7DE";
    private string _activeAccent = "#FF1098DF";
    private ResourceDictionary _sampleThemeResources = null;
    private string _appliedThemeKey = "";
    private int _sidebarMode = 1;

    public MainWindow()
    {
        this.InitializeComponent();

        this._lastLoadSucceeded = false;
        this.ViewModel = new ViewModel(this);
        this.DataContext = this.ViewModel;
        this.DockSidebarWide();
        this.ApplyTheme();
        BindingEngine.Flush();
    }

    private void DockSidebarWide()
    {
        this._sidebarMode = 1;
        if (this.SelectorExpanderButton != null)
        {
            this.SelectorExpanderButton.Visibility = Visibility.Collapsed;
            this.SelectorExpanderButton.IsChecked = true;
        }
        if (this.SelectorExpanderButtonShadow != null)
        {
            this.SelectorExpanderButtonShadow.Visibility = Visibility.Collapsed;
        }
        if (this.SampleOffset != null)
        {
            this.SampleOffset.Width = 215.0f;
        }
        if (this.SampleOverlay != null)
        {
            this.SampleOverlay.Visibility = Visibility.Collapsed;
        }
        this.BeginStoryboard("ExpandSelectorInstant");
    }

    private void CollapseSidebarFloating()
    {
        this._sidebarMode = 2;
        if (this.SampleOffset != null)
        {
            this.SampleOffset.Width = 0.0f;
        }
        if (this.SelectorExpanderButton != null)
        {
            this.SelectorExpanderButton.Visibility = Visibility.Visible;
            this.SelectorExpanderButton.IsChecked = false;
        }
        if (this.SelectorExpanderButtonShadow != null)
        {
            this.SelectorExpanderButtonShadow.Visibility = Visibility.Visible;
        }
        if (this.SampleOverlay != null)
        {
            this.SampleOverlay.Visibility = Visibility.Collapsed;
        }
        this.BeginStoryboard("CollapseSelectorInstant");
    }

    public override void OnHostViewportResized(float width, float height)
    {
        if (width <= 1.0f || height <= 1.0f)
        {
            return;
        }
        bool wantFloating = (width < 880.0f) || (width < height);
        if (wantFloating && this._sidebarMode != 2)
        {
            this.CollapseSidebarFloating();
        }
        else if (!wantFloating && this._sidebarMode != 1)
        {
            this.DockSidebarWide();
        }
    }

    public void OnInitialized()
    {
        if (this._sampleThemeResources == null)
        {
            this.ApplyTheme();
        }
    }

    public void SetUiScale(float scale)
    {
        if (this.LayoutRoot != null && scale > 0.1f)
        {
            this.LayoutRoot.LayoutTransform = new ScaleTransform(scale, scale);
        }
    }

    public bool SetUiScalePercent(int pct)
    {
        if (this.LayoutRoot != null && pct > 10)
        {
            float s = (1.0f * pct) / 100.0f;
            this.LayoutRoot.LayoutTransform = new ScaleTransform(s, s);
            return true;
        }
        return false;
    }

    private string ExtractSampleName(string xamlUri)
    {
        string s = xamlUri;
        if (s.StartsWith("Samples/"))
        {
            s = s.Substring(8, s.Length - 8);
        }
        if (s.EndsWith(".xaml"))
        {
            s = s.Substring(0, s.Length - 5);
        }
        return s;
    }

    private UIElement BuildLoadErrorFallback(string xamlUri)
    {
        Border root = new Border();
        root.Background = new SolidColorBrush(this._activeThemeSurface);
        root.BorderBrush = new SolidColorBrush(this._activeThemeBorder);
        root.BorderThickness = new Thickness(1.0f);
        root.CornerRadius = new CornerRadius(8.0f);
        root.Padding = new Thickness(24.0f);
        root.Margin = new Thickness(24.0f);

        TextBlock message = new TextBlock("Failed to load sample XAML: " + xamlUri);
        message.FontSize = 14.0f;
        message.Foreground = new SolidColorBrush(this._activeThemeFg);
        root.Child = message;
        return root;
    }

    public bool LoadSampleXaml(string xamlUri)
    {
        if (xamlUri == null || xamlUri == "")
        {
            this._lastLoadSucceeded = false;
            return false;
        }

        if (this.SampleContainer1 != null)
        {
            UIElement old1 = this.SampleContainer1.Content as UIElement;
            if (old1 != null)
            {
                this.SampleContainer1.SetContentBox(null);
                old1.DisposeSubtree();
            }
        }
        if (this.SampleContainer2 != null)
        {
            UIElement old2 = this.SampleContainer2.Content as UIElement;
            if (old2 != null)
            {
                this.SampleContainer2.SetContentBox(null);
                old2.DisposeSubtree();
            }
        }

        ResourceDictionary sampleRes = this._sampleThemeResources != null ? this._sampleThemeResources : this.Resources;
        UIElement preview = XamlReader.LoadUri(xamlUri, sampleRes);
        bool loadedReal = (preview != null);
        if (preview == null)
        {
            preview = this.BuildLoadErrorFallback(xamlUri);
        }

        bool useFirst = (this.ActiveContainerIndex != 1);

        if (this.WelcomeScreen != null)
        {
            this.WelcomeScreen.Visibility = Visibility.Collapsed;
        }

        ContentControl activeContainer = useFirst ? this.SampleContainer1 : this.SampleContainer2;
        ContentControl inactiveContainer = useFirst ? this.SampleContainer2 : this.SampleContainer1;

        if (inactiveContainer != null)
        {
            inactiveContainer.Visibility = Visibility.Collapsed;
        }
        if (activeContainer != null)
        {
            activeContainer.Visibility = Visibility.Visible;
            activeContainer.SetContent(preview);
            preview.ApplyTemplateSubtree();
            BindingEngine.FlushSubtree(preview);
            preview.EnsureLoadedSubtree();
        }

        if (this._sidebarMode == 2 && this.SelectorExpanderButton != null && this.SelectorExpanderButton.IsChecked)
        {
            this.SelectorExpanderButton.IsChecked = false;
        }
        this.ActiveContainerIndex = useFirst ? 1 : 2;
        this.LoadedSampleUri = xamlUri;
        this.BeginStoryboard(useFirst ? "ShowContainer1" : "ShowContainer2");
        BindingEngine.Flush();

        this._lastLoadSucceeded = loadedReal;
        return loadedReal;
    }

    public bool UpdateSample(string sampleName)
    {
        if (sampleName == null || sampleName == "")
        {
            this._lastLoadSucceeded = false;
            return false;
        }
        return this.LoadSampleXaml("Samples/" + sampleName + ".xaml");
    }

    public bool SelectSample(Sample sample)
    {
        if (sample == null)
        {
            return false;
        }
        if (this.ViewModel.SelectedSample == sample)
        {
            return this.UpdateSample(sample.Name);
        }
        this.ViewModel.SelectedSample = sample;
        return this._lastLoadSucceeded;
    }

    public bool SelectSampleByName(string sampleName)
    {
        Sample found = this.ViewModel.FindSample(sampleName);
        if (found != null)
        {
            return this.SelectSample(found);
        }
        return this.UpdateSample(sampleName);
    }

    public bool UpdateTheme(string colorName, string accentName)
    {
        string safeColor = (colorName == "Dark") ? "Dark" : "Light";
        if (colorName != null && colorName != "" && colorName != "Dark" && colorName != "Light")
        {
            safeColor = colorName;
        }
        string safeAccent = (accentName != null && accentName != "") ? accentName : "Blue";
        string nextKey = safeColor + "|" + safeAccent;
        if (this._sampleThemeResources != null && this._appliedThemeKey == nextKey)
        {
            return true;
        }
        this._appliedThemeKey = nextKey;
        bool isDark = (safeColor == "Dark");

        bool hasStylesLoaded = (this.Resources != null) &&
            (this.Resources.Contains("Brush.Window.Background") || this.Resources.Contains("Style.ButtonBase"));
        ResourceDictionary themeDict = null;
        if (hasStylesLoaded)
        {
            themeDict = XamlReader.LoadResourceDictionary("Theme/NoesisTheme.Brushes." + safeColor + safeAccent + ".xaml");
            if (themeDict == null)
            {
                themeDict = XamlReader.LoadResourceDictionary("NoesisTheme.Brushes." + safeColor + safeAccent + ".xaml");
            }
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("Theme/NoesisTheme." + safeColor + safeAccent + ".xaml");
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("NoesisTheme." + safeColor + safeAccent + ".xaml");
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("Theme/NoesisTheme.Brushes." + safeColor + safeAccent + ".xaml");
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("NoesisTheme.Brushes." + safeColor + safeAccent + ".xaml");
        }
        if (themeDict == null && hasStylesLoaded)
        {
            themeDict = XamlReader.LoadResourceDictionary("Theme/NoesisTheme.Brushes." + safeColor + "Blue.xaml");
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("Theme/NoesisTheme." + safeColor + "Blue.xaml");
        }
        if (themeDict == null)
        {
            themeDict = XamlReader.LoadResourceDictionary("NoesisTheme." + safeColor + "Blue.xaml");
        }

        if (this.Resources == null)
        {
            this.Resources = new ResourceDictionary();
        }

        this._activeThemeBg = isDark ? "#FF20282F" : "#FFF6F7F8";
        this._activeThemeSurface = isDark ? "#FF2E3944" : "#FFFFFFFF";
        this._activeThemeFg = isDark ? "#FFFFFFFF" : "#FF20282F";
        this._activeThemeBorder = isDark ? "#FF3C4752" : "#FFD0D7DE";
        ColorItem acc = this.ViewModel != null ? this.ViewModel.FindThemeAccent(safeAccent) : null;
        this._activeAccent = acc != null ? acc.Color : "#FF1098DF";

        ResourceDictionary scoped = new ResourceDictionary();
        scoped.Merge(this.Resources);
        if (themeDict != null)
        {
            scoped.Merge(themeDict);
        }
        else
        {
            scoped.Set("Brush.Window.Background", new SolidColorBrush(this._activeThemeBg));
            scoped.Set("Brush.Background.Normal", new SolidColorBrush(this._activeThemeSurface));
            scoped.Set("Brush.Foreground.Normal", new SolidColorBrush(this._activeThemeFg));
            scoped.Set("Brush.Border.Normal", new SolidColorBrush(this._activeThemeBorder));
            scoped.Set("Brush.Hyperlink.Normal", new SolidColorBrush(this._activeAccent));
        }
        // Keep sample page header TitleBrush & DescBrush readable over MainWindowBackground blue gradient
        Brush mainTitle = this.Resources.Get("TitleBrush") as Brush;
        Brush mainDesc = this.Resources.Get("DescBrush") as Brush;
        if (mainTitle != null) { scoped.Set("TitleBrush", mainTitle); }
        if (mainDesc != null) { scoped.Set("DescBrush", mainDesc); }

        this._sampleThemeResources = scoped;
        if (this.SampleContainer1 != null)
        {
            this.SampleContainer1.Resources = scoped;
        }
        if (this.SampleContainer2 != null)
        {
            this.SampleContainer2.Resources = scoped;
        }

        if (this.LoadedSampleUri != null && this.LoadedSampleUri != "")
        {
            this.LoadSampleXaml(this.LoadedSampleUri);
        }
        BindingEngine.Flush();
        return true;
    }

    public bool SetTheme(string colorName, string accentName)
    {
        ColorItem col = this.ViewModel.FindThemeColor(colorName);
        ColorItem acc = this.ViewModel.FindThemeAccent(accentName);
        if (col != null)
        {
            this.ViewModel.SelectedThemeColor = col;
        }
        if (acc != null)
        {
            this.ViewModel.SelectedThemeAccent = acc;
        }
        return this.UpdateTheme(colorName, accentName);
    }

    public bool ApplyTheme()
    {
        ColorItem colorItem = this.ViewModel.SelectedThemeColor;
        ColorItem accentItem = this.ViewModel.SelectedThemeAccent;
        string colorName = colorItem != null ? colorItem.Name : "Light";
        string accentName = accentItem != null ? accentItem.Name : "Blue";
        return this.UpdateTheme(colorName, accentName);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
