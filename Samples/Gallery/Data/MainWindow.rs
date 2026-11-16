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

    public MainWindow()
    {
        this.InitializeComponent();

        this._lastLoadSucceeded = false;
        this.ViewModel = new ViewModel(this);
        this.DataContext = this.ViewModel;
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
        this.ApplyTheme();
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

    private UIElement BuildSamplePreview(string sampleName)
    {
        Border root = new Border();
        root.Background = new SolidColorBrush(this._activeThemeSurface);
        root.BorderBrush = new SolidColorBrush(this._activeThemeBorder);
        root.BorderThickness = new Thickness(1.0f);
        root.CornerRadius = new CornerRadius(10.0f);
        root.Padding = new Thickness(24.0f);
        root.Margin = new Thickness(24.0f);

        StackPanel stack = new StackPanel();
        stack.Spacing = 14.0f;

        TextBlock title = new TextBlock(sampleName);
        title.FontSize = 26.0f;
        title.FontWeight = "Bold";
        title.Foreground = new SolidColorBrush(this._activeAccent);
        stack.AddChild(title);

        TextBlock subtitle = new TextBlock("AeroGUI v3 Controls Gallery - " + sampleName);
        subtitle.FontSize = 14.0f;
        subtitle.Foreground = new SolidColorBrush(this._activeThemeFg);
        stack.AddChild(subtitle);

        if (sampleName == "Button" || sampleName == "RepeatButton" || sampleName == "ToggleButton")
        {
            StackPanel row = new StackPanel();
            row.Orientation = Orientation.Horizontal;
            row.Spacing = 12.0f;

            Button primary = new Button();
            primary.SetContent(sampleName + " Action");
            primary.Background = new SolidColorBrush(this._activeAccent);
            primary.Foreground = new SolidColorBrush("#FFFFFFFF");
            primary.Padding = new Thickness(16.0f, 8.0f, 16.0f, 8.0f);
            row.AddChild(primary);

            ToggleButton toggle = new ToggleButton();
            toggle.SetContent("Toggle Option");
            toggle.IsChecked = true;
            toggle.Padding = new Thickness(14.0f, 8.0f, 14.0f, 8.0f);
            row.AddChild(toggle);

            stack.AddChild(row);
        }
        else if (sampleName == "CheckBox" || sampleName == "RadioButton")
        {
            CheckBox cb1 = new CheckBox();
            cb1.SetContent("Enable hardware acceleration");
            cb1.IsChecked = true;
            stack.AddChild(cb1);

            RadioButton rb1 = new RadioButton();
            rb1.SetContent("Balanced Quality Mode");
            rb1.IsChecked = true;
            stack.AddChild(rb1);

            RadioButton rb2 = new RadioButton();
            rb2.SetContent("High Performance Mode");
            stack.AddChild(rb2);
        }
        else if (sampleName == "Slider" || sampleName == "ProgressBar")
        {
            Slider slider = new Slider();
            slider.Minimum = 0.0f;
            slider.Maximum = 100.0f;
            slider.Value = 65.0f;
            slider.Width = 320.0f;
            stack.AddChild(slider);

            ProgressBar bar = new ProgressBar();
            bar.Minimum = 0.0f;
            bar.Maximum = 100.0f;
            bar.Value = 65.0f;
            bar.Width = 320.0f;
            bar.Height = 14.0f;
            stack.AddChild(bar);
        }
        else if (sampleName == "TextBlock" || sampleName == "TextBox" || sampleName == "PasswordBox" || sampleName == "Hyperlink")
        {
            TextBox tb = new TextBox();
            tb.Text = "Editable v3 " + sampleName + " input";
            tb.Width = 340.0f;
            stack.AddChild(tb);
        }
        else if (sampleName == "ComboBox" || sampleName == "ListBox" || sampleName == "ListView" || sampleName == "ItemsControl")
        {
            ListBox lb = new ListBox();
            lb.Width = 300.0f;
            lb.Height = 140.0f;
            lb.AddItem("Alpha Control Item");
            lb.AddItem("Beta Control Item");
            lb.AddItem("Gamma Control Item");
            lb.SelectedIndex = 0;
            stack.AddChild(lb);
        }
        else if (sampleName == "TabControl" || sampleName == "Expander" || sampleName == "GroupBox")
        {
            GroupBox gb = new GroupBox();
            gb.SetHeader(sampleName + " Preview");
            gb.SetContent("InteractiveAero.UI container for " + sampleName);
            stack.AddChild(gb);

            Expander exp = new Expander();
            exp.SetHeader("Details");
            exp.IsExpanded = true;
            exp.SetContent("Compiled AeroGUI v3 layout subtree.");
            stack.AddChild(exp);
        }
        else if (sampleName == "TreeView")
        {
            TreeView tv = new TreeView();
            TreeViewItem nodeA = new TreeViewItem();
            nodeA.SetHeader("Controls");
            nodeA.IsExpanded = true;
            TreeViewItem child1 = new TreeViewItem();
            child1.SetHeader("Buttons & Input");
            TreeViewItem child2 = new TreeViewItem();
            child2.SetHeader("Layout Panels");
            nodeA.AddItem(child1);
            nodeA.AddItem(child2);
            tv.AddItem(nodeA);
            stack.AddChild(tv);
        }
        else
        {
            UniformGrid grid = new UniformGrid();
            grid.Columns = 3;
            grid.Rows = 1;
            int idx = 0;
            while (idx < 3)
            {
                Border tile = new Border();
                tile.Background = new SolidColorBrush(this._activeThemeBg);
                tile.BorderBrush = new SolidColorBrush(this._activeAccent);
                tile.BorderThickness = new Thickness(1.0f);
                tile.CornerRadius = new CornerRadius(6.0f);
                tile.Margin = new Thickness(6.0f);
                tile.Padding = new Thickness(12.0f);
                TextBlock label = new TextBlock(sampleName + " #" + (idx + 1));
                label.Foreground = new SolidColorBrush(this._activeThemeFg);
                tile.Child = label;
                grid.AddChild(tile);
                idx = idx + 1;
            }
            stack.AddChild(grid);
        }

        root.Child = stack;
        return root;
    }

    public bool LoadSampleXaml(string xamlUri)
    {
        if (xamlUri == null || xamlUri == "")
        {
            this._lastLoadSucceeded = false;
            return false;
        }

        string sampleName = this.ExtractSampleName(xamlUri);
        UIElement preview = this.BuildSamplePreview(sampleName);
        bool useFirst = (this.ActiveContainerIndex != 1);

        if (this.WelcomeScreen != null)
        {
            this.WelcomeScreen.Visibility = Visibility.Collapsed;
        }

        if (useFirst)
        {
            if (this.SampleContainer1 != null)
            {
                this.SampleContainer1.SetContent(preview);
                this.SampleContainer1.Visibility = Visibility.Visible;
            }
            if (this.SampleContainer2 != null)
            {
                this.SampleContainer2.Visibility = Visibility.Collapsed;
            }
            this.ActiveContainerIndex = 1;
            this.LoadedSampleUri = xamlUri;
            this.BeginStoryboard("ShowContainer1");
        }
        else
        {
            if (this.SampleContainer2 != null)
            {
                this.SampleContainer2.SetContent(preview);
                this.SampleContainer2.Visibility = Visibility.Visible;
            }
            if (this.SampleContainer1 != null)
            {
                this.SampleContainer1.Visibility = Visibility.Collapsed;
            }
            this.ActiveContainerIndex = 2;
            this.LoadedSampleUri = xamlUri;
            this.BeginStoryboard("ShowContainer2");
        }

        this._lastLoadSucceeded = true;
        return true;
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
        bool isDark = (colorName == "Dark");
        this._activeThemeBg = isDark ? "#FF101418" : "#FFF6F7F8";
        this._activeThemeSurface = isDark ? "#FF1A2028" : "#FFFFFFFF";
        this._activeThemeFg = isDark ? "#FFEBF0F6" : "#FF18202A";
        this._activeThemeBorder = isDark ? "#FF2F3B4A" : "#FFD0D7DE";

        ColorItem acc = this.ViewModel != null ? this.ViewModel.FindThemeAccent(accentName) : null;
        this._activeAccent = acc != null ? acc.Color : "#FF1098DF";

        SolidColorBrush bgBrush = new SolidColorBrush(this._activeThemeBg);
        SolidColorBrush fgBrush = new SolidColorBrush(this._activeThemeFg);
        SolidColorBrush surfaceBrush = new SolidColorBrush(this._activeThemeSurface);
        SolidColorBrush accentBrush = new SolidColorBrush(this._activeAccent);

        this.Background = bgBrush;
        this.Foreground = fgBrush;
        this.Resources.Set("Brush.Window.Background", bgBrush);
        this.Resources.Set("Brush.Control.Background", surfaceBrush);
        this.Resources.Set("Brush.Text.Foreground", fgBrush);
        this.Resources.Set("Brush.Accent", accentBrush);

        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        if (appRes != null)
        {
            appRes.Set("Brush.Window.Background", bgBrush);
            appRes.Set("Brush.Control.Background", surfaceBrush);
            appRes.Set("Brush.Text.Foreground", fgBrush);
            appRes.Set("Brush.Accent", accentBrush);
        }

        if (this.LoadedSampleUri != null && this.LoadedSampleUri != "")
        {
            this.LoadSampleXaml(this.LoadedSampleUri);
        }
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
