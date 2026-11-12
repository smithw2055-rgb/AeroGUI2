using System;
using System.Windows;
using Aero.UI;

namespace WpfQuickStart
{
    public partial class MainWindow : Window
    {
        public TaskBoardViewModel DataContext { get; set; } = null;
        public TaskBoardViewModel ViewModel { get { return this.DataContext; } }

        public MainWindow()
        {
            this.InitializeComponent();
            this.DataContext = new TaskBoardViewModel();
            this.SetDataContext<TaskBoardViewModel>(this.DataContext);
        }

        public void QuickFillButton_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn)
            {
                this.NewTaskInput.Text = "Shipped via Quick Fill";
                if (this.DataContext != null)
                {
                    this.DataContext.NewTaskTitle = "Shipped via Quick Fill";
                }
                e.Handled = true;
            }
        }
    }

    public MainWindow CreateMainWindow()
    {
        return new MainWindow();
    }
}
