module Samples.V3.Mvvm;

import Aero.UI;

[Observable]
public class TaskItem : ViewModelBase {
    private string _name = "";
    private string _badge = "";

    public TaskItem() {}
    public TaskItem(string name, string badge) {
        _name = name == null ? "" : name;
        _badge = badge == null ? "" : badge;
    }

    public string Name {
        get { return _name; }
        set { SetProperty(ref _name, value, "Name"); }
    }

    public string Badge {
        get { return _badge; }
        set { SetProperty(ref _badge, value, "Badge"); }
    }
}

[Observable]
public class MainViewModel : ViewModelBase {
    private string _draftTitle = "Compile XAML View";
    private float _priorityScore = 65.0f;
    private bool _isUrgent = true;
    private string _statusMessage = "MVVM Ready";
    private int _taskSeq = 3;

    public ObservableList<TaskItem> Tasks { get; private set; }
    public RelayCommand AddTaskCommand { get; private set; }
    public RelayCommand RemoveFirstCommand { get; private set; }

    public MainViewModel() {
        Tasks = new ObservableList<TaskItem>();
        Tasks.Add(new TaskItem("Initialize Visual Microkernel", "P80 [URGENT]"));
        Tasks.Add(new TaskItem("Bind TwoWay Controls", "P65 [URGENT]"));

        AddTaskCommand = new RelayCommand(() => { AddTask(); }, () => { return CanAddTask(); });
        RemoveFirstCommand = new RelayCommand(() => { RemoveFirst(); }, () => { return Tasks.Count() > 0; });
    }

    public string DraftTitle {
        get { return _draftTitle; }
        set {
            if (SetProperty(ref _draftTitle, value, "DraftTitle")) {
                NotifyPropertyChanged("SummaryLine");
                AddTaskCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public float PriorityScore {
        get { return _priorityScore; }
        set {
            if (SetProperty(ref _priorityScore, value, "PriorityScore")) {
                NotifyPropertyChanged("SummaryLine");
            }
        }
    }

    public bool IsUrgent {
        get { return _isUrgent; }
        set {
            if (SetProperty(ref _isUrgent, value, "IsUrgent")) {
                NotifyPropertyChanged("SummaryLine");
            }
        }
    }

    public string StatusMessage {
        get { return _statusMessage; }
        set { SetProperty(ref _statusMessage, value, "StatusMessage"); }
    }

    public string SummaryLine {
        get {
            string urgency = _isUrgent ? "URGENT" : "NORMAL";
            return "Tasks: " + Tasks.Count() + " | Priority: " + (int)_priorityScore + " | " + urgency + " | Draft: " + _draftTitle;
        }
    }

    public bool CanAddTask() {
        return _draftTitle != null && _draftTitle.Length > 0;
    }

    public void AddTask() {
        if (!CanAddTask()) { return; }
        string tag = "P" + (int)_priorityScore + (_isUrgent ? " [URGENT]" : " [NORMAL]");
        Tasks.Add(new TaskItem(_draftTitle, tag));
        _taskSeq = _taskSeq + 1;
        StatusMessage = "Added '" + _draftTitle + "' (total " + Tasks.Count() + ")";
        NotifyPropertyChanged("SummaryLine");
        RemoveFirstCommand.RaiseCanExecuteChanged();
    }

    public void RemoveFirst() {
        if (Tasks.Count() == 0) { return; }
        Tasks.RemoveAt(0);
        StatusMessage = "Removed first item (total " + Tasks.Count() + ")";
        NotifyPropertyChanged("SummaryLine");
        RemoveFirstCommand.RaiseCanExecuteChanged();
    }
}

public partial class MainWindow : Window {
    public MainViewModel VM { get; private set; }

    public MainWindow() {
        VM = new MainViewModel();
        DataContext = VM;
        InitializeComponent();
        BindingEngine.Flush();
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
