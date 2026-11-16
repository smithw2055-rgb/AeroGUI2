module WpfQuickStart;

import Aero.UI;

[Observable]
public class TaskRowItem : ObservableObject
{
    private string _title = "";
    private string _track = "";
    private string _priority = "P1";

    public string Title
    {
        get { return this._title; }
        set { this.SetProperty(ref this._title, value, "Title"); }
    }

    public string Track
    {
        get { return this._track; }
        set { this.SetProperty(ref this._track, value, "Track"); }
    }

    public string Priority
    {
        get { return this._priority; }
        set { this.SetProperty(ref this._priority, value, "Priority"); }
    }

    public TaskRowItem()
    {
    }

    public TaskRowItem(string title, string track, string priority)
    {
        this._title = title == null ? "" : title;
        this._track = track == null ? "" : track;
        this._priority = priority == null ? "P1" : priority;
    }
}

[Observable]
public class TaskBoardViewModel : ViewModelBase
{
    private RelayCommand _addTaskCommand;
    private RelayCommand _resetBoardCommand;
    private RelayCommand<string> _quickAddCommand;
    private ObservableList<string> _activeTasks;
    private ObservableList<TaskRowItem> _taskRows;
    private string _priorityFilter = "All";
    private string _newTaskTitle = "";
    private string _statusMessage = "Ready (3 total tasks)";
    private int _taskCount = 3;
    private int _selectedTaskIndex = -1;

    public ObservableList<string> ActiveTasks { get { return this._activeTasks; } }
    public ObservableList<TaskRowItem> TaskRows { get { return this._taskRows; } }
    public RelayCommand AddTaskCommand { get { return this._addTaskCommand; } }
    public RelayCommand ResetBoardCommand { get { return this._resetBoardCommand; } }
    public RelayCommand<string> QuickAddCommand { get { return this._quickAddCommand; } }

    public string PriorityFilter
    {
        get { return this._priorityFilter; }
        set { this.SetProperty(ref this._priorityFilter, value, "PriorityFilter"); }
    }

    public string NewTaskTitle
    {
        get { return this._newTaskTitle; }
        set { this.SetProperty(ref this._newTaskTitle, value, "NewTaskTitle"); }
    }

    public string StatusMessage
    {
        get { return this._statusMessage; }
        set { this.SetProperty(ref this._statusMessage, value, "StatusMessage"); }
    }

    public int TaskCount
    {
        get { return this._taskCount; }
        set { this.SetProperty(ref this._taskCount, value, "TaskCount"); }
    }

    public int SelectedTaskIndex
    {
        get { return this._selectedTaskIndex; }
        set { this.SetProperty(ref this._selectedTaskIndex, value, "SelectedTaskIndex"); }
    }

    public TaskBoardViewModel()
    {
        this._activeTasks = new ObservableList<string>();
        this._taskRows = new ObservableList<TaskRowItem>();
        this._addTaskCommand = new RelayCommand(() => { this.AddTask(); });
        this._resetBoardCommand = new RelayCommand(() => { this.ResetBoard(); });
        this._quickAddCommand = new RelayCommand<string>((string title) => { this.AddNamedTask(title); });
        this.ResetBoard();
    }

    public void AddTask()
    {
        string title = this.NewTaskTitle;
        if (title == null || title == "")
        {
            title = "Untitled Task";
        }
        this.AddNamedTask(title);
        this.NewTaskTitle = "";
    }

    public void AddNamedTask(string title)
    {
        string cleanTitle = title;
        if (cleanTitle == null || cleanTitle == "")
        {
            cleanTitle = "Untitled Task";
        }
        this._activeTasks.Add(cleanTitle);
        this._taskRows.Add(new TaskRowItem(cleanTitle, "Runtime", "P1"));
        this.TaskCount = this._activeTasks.Count();
        this.StatusMessage = "Added '" + cleanTitle + "' (" + this.TaskCount + " total tasks)";
    }

    public void ResetBoard()
    {
        this._activeTasks.Clear();
        this._taskRows.Clear();

        this._activeTasks.Add("Upgrade XAML HierarchicalDataTemplate & Run Bindings");
        this._taskRows.Add(new TaskRowItem("Upgrade XAML HierarchicalDataTemplate & Run Bindings", "XAML", "P0"));

        this._activeTasks.Add("Implement TreeView, ListView, Menu & GridSplitter");
        this._taskRows.Add(new TaskRowItem("Implement TreeView, ListView, Menu & GridSplitter", "Controls", "P0"));

        this._activeTasks.Add("Accelerate ImageAtlas, FontAtlas & Parallel Blur");
        this._taskRows.Add(new TaskRowItem("Accelerate ImageAtlas, FontAtlas & Parallel Blur", "Render", "P1"));

        this.TaskCount = this._activeTasks.Count();
        this.StatusMessage = "Ready (" + this.TaskCount + " total tasks)";
    }
}
