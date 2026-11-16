module WpfQuickStart;

import Aero.UI;

public partial class SprintSummaryCard : UserControl
{
    private int _boostLevel = 0;

    public SprintSummaryCard() : base()
    {
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void BoostBtn_Click(object sender, RoutedEventArgs e)
    {
        this._boostLevel = this._boostLevel + 1;
        if (this.BadgeLabel != null)
        {
            this.BadgeLabel.Text = "Sprint Velocity: x" + (this._boostLevel + 1);
        }
        if (e != null)
        {
            e.Handled = true;
        }
    }
}
