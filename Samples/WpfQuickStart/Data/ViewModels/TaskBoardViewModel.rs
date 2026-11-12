using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using Aero.UI;

namespace WpfQuickStart
{
    public class TaskBoardViewModel : ObservableObject
    {
        private RelayCommand _addTaskCommand;
        private RelayCommand _resetBoardCommand;
        private ObservableCollection<string> _activeTasks;

        public ObservableCollection<string> ActiveTasks { get { return this._activeTasks; } }
        public RelayCommand AddTaskCommand { get { return this._addTaskCommand; } }
        public RelayCommand ResetBoardCommand { get { return this._resetBoardCommand; } }

        public string NewTaskTitle { get; set; } = "";
        public string StatusMessage { get; set; } = "Ready (3 total tasks)";
        public int TaskCount { get; set; } = 3;

        public TaskBoardViewModel()
        {
            this._activeTasks = new ObservableCollection<string>();
            this._addTaskCommand = new RelayCommand(this.AddTask);
            this._resetBoardCommand = new RelayCommand(this.ResetBoard);
            this.ResetBoard();
        }

        public void AddTask()
        {
            string title = this.NewTaskTitle;
            if (title == "")
            {
                title = "Untitled Task";
            }
            this._activeTasks.Add(title);
            this.TaskCount = this._activeTasks.Count;
            this.StatusMessage = $"Added '{title}' ({this.TaskCount} total tasks)";
            this.NewTaskTitle = "";
        }

        public void ResetBoard()
        {
            this._activeTasks.Clear();
            this._activeTasks.Add("Upgrade XAML HierarchicalDataTemplate & Run Bindings");
            this._activeTasks.Add("Implement TreeView, Menu, StatusBar & GridSplitter");
            this._activeTasks.Add("Accelerate ImageAtlas & FontAtlas Rasterization");
            this.TaskCount = this._activeTasks.Count;
            this.StatusMessage = $"Ready ({this.TaskCount} total tasks)";
        }
    }
}
