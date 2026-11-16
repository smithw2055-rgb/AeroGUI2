module Localization;

import Aero.UI;

public enum FlowDirection
{
    LeftToRight = 0,
    RightToLeft = 1
}

[Observable]
public class Language : ObservableObject
{
    private string _name = "";
    private string _source = "";
    private string _flag = "";
    private FlowDirection _flowDirection = FlowDirection.LeftToRight;

    public string Name
    {
        get { return this._name; }
        set { this.SetProperty(ref this._name, value, "Name"); }
    }

    public string Source
    {
        get { return this._source; }
        set { this.SetProperty(ref this._source, value, "Source"); }
    }

    public string Flag
    {
        get { return this._flag; }
        set { this.SetProperty(ref this._flag, value, "Flag"); }
    }

    public FlowDirection FlowDirection
    {
        get { return this._flowDirection; }
        set
        {
            if (this._flowDirection != value)
            {
                this._flowDirection = value;
                this.NotifyPropertyChanged("FlowDirection");
            }
        }
    }

    public Language()
    {
    }

    public Language(string name, string source, string flag, FlowDirection flow)
    {
        this._name = name == null ? "" : name;
        this._source = source == null ? "" : source;
        this._flag = flag == null ? "" : flag;
        this._flowDirection = flow;
    }
}

[Observable]
public class ViewModel : ViewModelBase
{
    private Language _selectedLanguage = null;
    private int _level = 1;
    private float _soundLevel = 100.0f;
    private float _musicLevel = 70.0f;
    private ObservableList<Language> _languages;

    public ObservableList<Language> Languages { get { return this._languages; } }

    public int Level
    {
        get { return this._level; }
        set { this.SetProperty(ref this._level, value, "Level"); }
    }

    public float SoundLevel
    {
        get { return this._soundLevel; }
        set { this.SetProperty(ref this._soundLevel, value, "SoundLevel"); }
    }

    public float MusicLevel
    {
        get { return this._musicLevel; }
        set { this.SetProperty(ref this._musicLevel, value, "MusicLevel"); }
    }

    public Language SelectedLanguage
    {
        get { return this._selectedLanguage; }
        set
        {
            if (this._selectedLanguage != value && value != null)
            {
                this._selectedLanguage = value;
                this.NotifyPropertyChanged("SelectedLanguage");
            }
        }
    }

    public ViewModel()
    {
        this._languages = new ObservableList<Language>();
        this._languages.Add(new Language("English", "Language-en.xaml", "Flag-en.png", FlowDirection.LeftToRight));
        this._languages.Add(new Language("Français", "Language-fr.xaml", "Flag-fr.png", FlowDirection.LeftToRight));
        this._languages.Add(new Language("日本語", "Language-jp.xaml", "Flag-jp.png", FlowDirection.LeftToRight));
        this._languages.Add(new Language("عربي", "Language-ar.xaml", "Flag-ar.png", FlowDirection.RightToLeft));

        this._selectedLanguage = this._languages.Get(0);
        this._level = 1;
        this._soundLevel = 100.0f;
        this._musicLevel = 70.0f;
    }

    public void SelectLanguageByIndex(int index)
    {
        if (this.Languages == null)
        {
            return;
        }
        int count = this.Languages.Count();
        if (index >= 0 && index < count)
        {
            this.SelectedLanguage = this.Languages.Get(index);
        }
    }

    public void SelectLanguage(Language language)
    {
        if (language != null)
        {
            this.SelectedLanguage = language;
        }
    }

    public void NextLanguage()
    {
        if (this.Languages == null || this.Languages.Count() == 0)
        {
            return;
        }
        int cur = this.Languages.IndexOf(this.SelectedLanguage);
        int next = (cur + 1) % this.Languages.Count();
        this.SelectedLanguage = this.Languages.Get(next);
    }

    public void LevelUp()
    {
        this.Level = this.Level + 1;
    }

    public void SetSound(float level)
    {
        if (level < 0.0f) { this.SoundLevel = 0.0f; }
        else if (level > 100.0f) { this.SoundLevel = 100.0f; }
        else { this.SoundLevel = level; }
    }

    public void SetMusic(float level)
    {
        if (level < 0.0f) { this.MusicLevel = 0.0f; }
        else if (level > 100.0f) { this.MusicLevel = 100.0f; }
        else { this.MusicLevel = level; }
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

    public void SelectLanguageByIndex(int index)
    {
        if (this.VM != null)
        {
            this.VM.SelectLanguageByIndex(index);
        }
    }

    public void SelectLanguage(Language language)
    {
        if (this.VM != null)
        {
            this.VM.SelectLanguage(language);
        }
    }

    public void NextLanguage()
    {
        if (this.VM != null)
        {
            this.VM.NextLanguage();
        }
    }

    public void LevelUp()
    {
        if (this.VM != null)
        {
            this.VM.LevelUp();
        }
    }

    public void SetSoundLevel(float level)
    {
        if (this.VM != null)
        {
            this.VM.SetSound(level);
        }
    }

    public void SetMusicLevel(float level)
    {
        if (this.VM != null)
        {
            this.VM.SetMusic(level);
        }
    }

    public void PlayIntro()
    {
        this.BeginStoryboard("Intro");
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
