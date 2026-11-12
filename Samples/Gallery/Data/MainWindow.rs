module Gallery;
import Aero.UI;

[Observable]
public class Sample
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
public class Category
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
public class ColorItem
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
public class ViewModel
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
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; } = null;
    public int ActiveContainerIndex { get; set; } = 0;
    public string LoadedSampleUri { get; set; } = "";
    private bool _lastLoadSucceeded;

    public MainWindow()
    {
        this._lastLoadSucceeded = false;
        this.ViewModel = new ViewModel(this);
    }

    public void OnInitialized()
    {
        this.ApplyTheme();
    }

    public bool LoadSampleXaml(string xamlUri)
    {
        if (xamlUri == null || xamlUri == "")
        {
            this._lastLoadSucceeded = false;
            return false;
        }

        bool useFirst = (this.ActiveContainerIndex != 1);
        bool loaded = false;
        if (useFirst)
        {
            loaded = ContentControl_LoadXaml(this.SampleContainer1, xamlUri);
            if (loaded)
            {
                this.ActiveContainerIndex = 1;
                this.LoadedSampleUri = xamlUri;
                UIElement_SetVisibility(this.WelcomeScreen, 2);
                UIElement_SetVisibility(this.SampleContainer1, 0);
                UIElement_SetVisibility(this.SampleContainer2, 2);
                Storyboard_Begin(this.__nativeRootHandle, "ShowContainer1");
            }
        }
        else
        {
            loaded = ContentControl_LoadXaml(this.SampleContainer2, xamlUri);
            if (loaded)
            {
                this.ActiveContainerIndex = 2;
                this.LoadedSampleUri = xamlUri;
                UIElement_SetVisibility(this.WelcomeScreen, 2);
                UIElement_SetVisibility(this.SampleContainer2, 0);
                UIElement_SetVisibility(this.SampleContainer1, 2);
                Storyboard_Begin(this.__nativeRootHandle, "ShowContainer2");
            }
        }

        this._lastLoadSucceeded = loaded;
        return loaded;
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
        return Application_LoadTheme(this.__nativeRootHandle, colorName, accentName);
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
