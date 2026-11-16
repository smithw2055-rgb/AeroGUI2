module Samples.V3.ControlGallery;

import Aero.UI;

[Observable]
public class GalleryViewModel : ViewModelBase {
    private bool _compactMode = false;
    private int _selectedThemeIndex = 0;
    private int _actionCount = 0;
    private string _statusText = "ControlGallery ready";

    public bool CompactMode {
        get { return _compactMode; }
        set {
            if (SetProperty(ref _compactMode, value, "CompactMode")) {
                NotifyPropertyChanged("DensityLabel");
            }
        }
    }

    public int SelectedThemeIndex {
        get { return _selectedThemeIndex; }
        set {
            if (SetProperty(ref _selectedThemeIndex, value, "SelectedThemeIndex")) {
                NotifyPropertyChanged("ThemeSummary");
                StatusText = "Selected theme #" + _selectedThemeIndex;
            }
        }
    }

    public int ActionCount {
        get { return _actionCount; }
        set { SetProperty(ref _actionCount, value, "ActionCount"); }
    }

    public string StatusText {
        get { return _statusText; }
        set { SetProperty(ref _statusText, value, "StatusText"); }
    }

    public string DensityLabel {
        get { return _compactMode ? "DENSITY: COMPACT" : "DENSITY: COMFORTABLE"; }
    }

    public string ThemeSummary {
        get { return "Active Theme Index: " + _selectedThemeIndex + " | Pill Clicks: " + _actionCount; }
    }
}

public partial class MainWindow : Window {
    public GalleryViewModel VM { get; private set; }

    public MainWindow() {
        VM = new GalleryViewModel();
        DataContext = VM;
        InitializeComponent();
        RefreshBadgeTrigger();
    }

    private void RefreshBadgeTrigger() {
        if (CompactStatusBadge != null && CompactStatusBadge.Style != null) {
            CompactStatusBadge.Style.EvaluateTriggers(CompactStatusBadge);
        }
    }

    public void OnPillButtonClick(object sender, RoutedEventArgs e) {
        VM.ActionCount = VM.ActionCount + 1;
        VM.StatusText = "Pill clicked #" + VM.ActionCount;
        VM.NotifyPropertyChanged("ThemeSummary");
    }

    public void OnMenuRefreshClick(object sender, RoutedEventArgs e) {
        VM.StatusText = "Gallery refreshed via Menu";
    }

    public void OnMenuCompactToggle(object sender, RoutedEventArgs e) {
        if (CompactMenuItem != null) {
            VM.CompactMode = CompactMenuItem.IsChecked;
            RefreshBadgeTrigger();
            BindingEngine.Flush();
        }
    }

    public void OnMenuSelectHierarchyTab(object sender, RoutedEventArgs e) {
        if (GalleryTabs != null) {
            GalleryTabs.SelectedIndex = 1;
            VM.StatusText = "Switched to Hierarchy tab";
        }
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
