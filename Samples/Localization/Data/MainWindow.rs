module Localization;

import Aero.UI;

public enum FlowDirection
{
    LeftToRight = 0,
    RightToLeft = 1
}

[Observable]
public class Language
{
    public string Name { get; set; } = "";
    public string Source { get; set; } = "";
    public string Flag { get; set; } = "";
    public FlowDirection FlowDirection { get; set; } = FlowDirection.LeftToRight;

    public Language()
    {
    }

    public Language(string name, string source, string flag, FlowDirection flow)
    {
        this.Name = name;
        this.Source = source;
        this.Flag = flag;
        this.FlowDirection = flow;
    }
}

[Observable]
public class ViewModel
{
    public ObservableList<Language> Languages { get; set; }
    public Language SelectedLanguage { get; set; }
    public int Level { get; set; } = 1;
    public float SoundLevel { get; set; } = 100.0f;
    public float MusicLevel { get; set; } = 70.0f;

    public ViewModel()
    {
        this.Languages = new ObservableList<Language>(4);
        this.Languages.Add(new Language("English", "Language-en.xaml", "Flag-en.png", FlowDirection.LeftToRight));
        this.Languages.Add(new Language("Français", "Language-fr.xaml", "Flag-fr.png", FlowDirection.LeftToRight));
        this.Languages.Add(new Language("日本語", "Language-jp.xaml", "Flag-jp.png", FlowDirection.LeftToRight));
        this.Languages.Add(new Language("عربي", "Language-ar.xaml", "Flag-ar.png", FlowDirection.RightToLeft));

        this.SelectedLanguage = this.Languages.Get(0);
        this.Level = 1;
        this.SoundLevel = 100.0f;
        this.MusicLevel = 70.0f;
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
        if (level < 0.0f)
        {
            this.SoundLevel = 0.0f;
        }
        else if (level > 100.0f)
        {
            this.SoundLevel = 100.0f;
        }
        else
        {
            this.SoundLevel = level;
        }
    }

    public void SetMusic(float level)
    {
        if (level < 0.0f)
        {
            this.MusicLevel = 0.0f;
        }
        else if (level > 100.0f)
        {
            this.MusicLevel = 100.0f;
        }
        else
        {
            this.MusicLevel = level;
        }
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

    public void SelectLanguageByIndex(int index)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.SelectLanguageByIndex(index);
        }
    }

    public void SelectLanguage(Language language)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.SelectLanguage(language);
        }
    }

    public void NextLanguage()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.NextLanguage();
        }
    }

    public void LevelUp()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.LevelUp();
        }
    }

    public void SetSoundLevel(float level)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.SetSound(level);
        }
    }

    public void SetMusicLevel(float level)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.SetMusic(level);
        }
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
