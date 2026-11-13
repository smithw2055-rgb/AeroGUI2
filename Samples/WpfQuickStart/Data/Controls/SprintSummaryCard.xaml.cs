using System;
using System.Windows;
using Aero.UI;

namespace WpfQuickStart
{
    public partial class SprintSummaryCard : UserControl
    {
        private int _boostLevel = 0;

        public SprintSummaryCard()
        {
            InitializeComponent();
        }

        public void BoostBtn_Click(object sender, RoutedEventArgs e)
        {
            _boostLevel = _boostLevel + 1;
            BadgeLabel.Text = $"Sprint Velocity: x{_boostLevel + 1}";
            e.Handled = true;
        }
    }
}
