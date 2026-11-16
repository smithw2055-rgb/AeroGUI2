module HelloWorld;

import Aero.UI;

[Observable]
public class HelloWorldViewModel : ViewModelBase
{
    public string Title { get; set; } = "Hello, World!";
    public string Subtitle { get; set; } = "AeroGUI v3 RealScript Composition";
    public int IntroPlayCount { get; set; } = 0;

    public HelloWorldViewModel()
    {
    }

    public void RecordIntroPlay()
    {
        this.IntroPlayCount = this.IntroPlayCount + 1;
        this.NotifyPropertyChanged("IntroPlayCount");
    }
}

[Observable]
public partial class MainWindow : Page
{
    public HelloWorldViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new HelloWorldViewModel();
        this.DataContext = this.ViewModel;
        BindingEngine.Flush();
    }

    protected override void OnLoaded()
    {
        base.OnLoaded();
        this.PlayIntro();
    }

    public void PlayIntro()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.RecordIntroPlay();
        }
        this.BeginStoryboard("Intro");
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
