using System;
using System.Windows;
using Aero.UI;

namespace WpfQuickStart
{
    public partial class MainWindow : Window
    {
        private TaskBoardViewModel _vm;
        public int ProgrammaticClickCount { get; set; } = 0;

        public MainWindow()
        {
            InitializeComponent();
            _vm = new TaskBoardViewModel();
            DataContext = _vm;

            QuickFillButton.Click += OnQuickFillProgrammatic;
        }

        private void OnQuickFillProgrammatic(object sender, RoutedEventArgs e)
        {
            ProgrammaticClickCount = ProgrammaticClickCount + 1;
        }

        public void ToggleNavVisibility(bool visible)
        {
            NavBorder.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
        }

        public void QuickFillButton_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn)
            {
                NewTaskInput.Text = "Shipped via Quick Fill";
                if (_vm != null)
                {
                    _vm.NewTaskTitle = "Shipped via Quick Fill";
                }
                e.Handled = true;
            }
        }

        public void TasksList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_vm != null && e != null && e.SelectedIndex >= 0)
            {
                _vm.SelectedTaskIndex = e.SelectedIndex;
            }
        }
    }
}
