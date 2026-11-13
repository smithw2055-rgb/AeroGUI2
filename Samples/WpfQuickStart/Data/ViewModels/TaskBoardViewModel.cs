using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using Aero.UI;

namespace WpfQuickStart
{
    public class TaskRowItem : ObservableObject
    {
        public string Title { get; set; } = "";
        public string Track { get; set; } = "";
        public string Priority { get; set; } = "P1";

        public TaskRowItem() { }

        public TaskRowItem(string title, string track, string priority)
        {
            Title = title;
            Track = track;
            Priority = priority;
        }
    }

    public class TaskBoardViewModel : ViewModelBase
    {
        private RelayCommand _addTaskCommand;
        private RelayCommand _resetBoardCommand;
        private RelayCommand<string> _quickAddCommand;
        private ObservableCollection<string> _activeTasks;
        private ObservableCollection<TaskRowItem> _taskRows;
        private string _priorityFilter = "All";

        public ObservableCollection<string> ActiveTasks { get { return _activeTasks; } }
        public ObservableCollection<TaskRowItem> TaskRows { get { return _taskRows; } }
        public RelayCommand AddTaskCommand { get { return _addTaskCommand; } }
        public RelayCommand ResetBoardCommand { get { return _resetBoardCommand; } }
        public RelayCommand<string> QuickAddCommand { get { return _quickAddCommand; } }

        public string PriorityFilter
        {
            get { return _priorityFilter; }
            set { SetProperty(ref _priorityFilter, value); }
        }

        public string NewTaskTitle { get; set; } = "";
        public string StatusMessage { get; set; } = "Ready (3 total tasks)";
        public int TaskCount { get; set; } = 3;
        public int SelectedTaskIndex { get; set; } = -1;

        public TaskBoardViewModel()
        {
            _activeTasks = new ObservableCollection<string>();
            _taskRows = new ObservableCollection<TaskRowItem>();
            _addTaskCommand = new RelayCommand(AddTask);
            _resetBoardCommand = new RelayCommand(ResetBoard);
            _quickAddCommand = new RelayCommand<string>(AddNamedTask);
            ResetBoard();
        }

        public void AddTask()
        {
            string title = NewTaskTitle;
            if (title == "")
            {
                title = "Untitled Task";
            }
            AddNamedTask(title);
            NewTaskTitle = "";
        }

        public void AddNamedTask(string title)
        {
            string cleanTitle = title;
            if (cleanTitle == "")
            {
                cleanTitle = "Untitled Task";
            }
            _activeTasks.Add(cleanTitle);
            _taskRows.Add(new TaskRowItem(cleanTitle, "Runtime", "P1"));
            TaskCount = _activeTasks.Count;
            StatusMessage = $"Added '{cleanTitle}' ({TaskCount} total tasks)";
        }

        public void ResetBoard()
        {
            _activeTasks.Clear();
            _taskRows.Clear();

            _activeTasks.Add("Upgrade XAML HierarchicalDataTemplate & Run Bindings");
            _taskRows.Add(new TaskRowItem("Upgrade XAML HierarchicalDataTemplate & Run Bindings", "XAML", "P0"));

            _activeTasks.Add("Implement TreeView, ListView, Menu & GridSplitter");
            _taskRows.Add(new TaskRowItem("Implement TreeView, ListView, Menu & GridSplitter", "Controls", "P0"));

            _activeTasks.Add("Accelerate ImageAtlas, FontAtlas & Parallel Blur");
            _taskRows.Add(new TaskRowItem("Accelerate ImageAtlas, FontAtlas & Parallel Blur", "Render", "P1"));

            TaskCount = _activeTasks.Count;
            StatusMessage = $"Ready ({TaskCount} total tasks)";
        }
    }
}
