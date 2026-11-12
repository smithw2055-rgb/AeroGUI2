module UserControl;

import Aero.UI;

[Observable]
public class ViewModel
{
    public int TotalBudget { get; set; } = 50;
    public int MinStat { get; set; } = 3;
    public int MaxStat { get; set; } = 20;
    public int Strength { get; set; } = 10;
    public int Agility { get; set; } = 14;
    public int Intelligence { get; set; } = 8;
    public int Charisma { get; set; } = 12;
    public int PointsAvailable { get; set; } = 6;

    public ViewModel()
    {
        this.TotalBudget = 50;
        this.MinStat = 3;
        this.MaxStat = 20;
        this.Strength = 10;
        this.Agility = 14;
        this.Intelligence = 8;
        this.Charisma = 12;
        this.PointsAvailable = 6;
    }

    private int ClampStat(int requested, int otherSum)
    {
        int maxAllowed = this.TotalBudget - otherSum;
        if (maxAllowed > this.MaxStat)
        {
            maxAllowed = this.MaxStat;
        }
        if (maxAllowed < this.MinStat)
        {
            maxAllowed = this.MinStat;
        }
        if (requested < this.MinStat)
        {
            return this.MinStat;
        }
        if (requested > maxAllowed)
        {
            return maxAllowed;
        }
        return requested;
    }

    public void RecalculatePoints()
    {
        this.PointsAvailable = this.TotalBudget - (this.Strength + this.Agility + this.Intelligence + this.Charisma);
    }

    public int SetStrength(int value)
    {
        int otherSum = this.Agility + this.Intelligence + this.Charisma;
        this.Strength = this.ClampStat(value, otherSum);
        this.RecalculatePoints();
        return this.Strength;
    }

    public int SetAgility(int value)
    {
        int otherSum = this.Strength + this.Intelligence + this.Charisma;
        this.Agility = this.ClampStat(value, otherSum);
        this.RecalculatePoints();
        return this.Agility;
    }

    public int SetIntelligence(int value)
    {
        int otherSum = this.Strength + this.Agility + this.Charisma;
        this.Intelligence = this.ClampStat(value, otherSum);
        this.RecalculatePoints();
        return this.Intelligence;
    }

    public int SetCharisma(int value)
    {
        int otherSum = this.Strength + this.Agility + this.Intelligence;
        this.Charisma = this.ClampStat(value, otherSum);
        this.RecalculatePoints();
        return this.Charisma;
    }

    public void Reset()
    {
        this.Strength = 10;
        this.Agility = 14;
        this.Intelligence = 8;
        this.Charisma = 12;
        this.RecalculatePoints();
    }
}

[Observable]
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; }

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
    }

    public void OnInitialized()
    {
        this.SyncFromControls();
    }

    public void SyncFromControls()
    {
        if (this.ViewModel == null)
        {
            return;
        }

        double rawStr = UIElement_GetDouble(this.StatStr, "Value");
        double rawAgi = UIElement_GetDouble(this.StatAgi, "Value");
        double rawInt = UIElement_GetDouble(this.StatInt, "Value");
        double rawCha = UIElement_GetDouble(this.StatCha, "Value");

        if (rawStr > 0.0)
        {
            this.ViewModel.Strength = (int)(rawStr + 0.5);
        }
        if (rawAgi > 0.0)
        {
            this.ViewModel.Agility = (int)(rawAgi + 0.5);
        }
        if (rawInt > 0.0)
        {
            this.ViewModel.Intelligence = (int)(rawInt + 0.5);
        }
        if (rawCha > 0.0)
        {
            this.ViewModel.Charisma = (int)(rawCha + 0.5);
        }

        this.ViewModel.RecalculatePoints();
    }

    public void SyncToControls()
    {
        if (this.ViewModel == null)
        {
            return;
        }

        UIElement_SetDouble(this.StatStr, "Value", (double)this.ViewModel.Strength);
        UIElement_SetDouble(this.StatAgi, "Value", (double)this.ViewModel.Agility);
        UIElement_SetDouble(this.StatInt, "Value", (double)this.ViewModel.Intelligence);
        UIElement_SetDouble(this.StatCha, "Value", (double)this.ViewModel.Charisma);
    }

    public void SetStrength(int v)
    {
        if (this.ViewModel == null)
        {
            return;
        }
        int actual = this.ViewModel.SetStrength(v);
        UIElement_SetDouble(this.StatStr, "Value", (double)actual);
        Storyboard_Begin(this.__nativeRootHandle, "UserControl.Anim.Str.ValueChanged");
    }

    public void SetAgility(int v)
    {
        if (this.ViewModel == null)
        {
            return;
        }
        int actual = this.ViewModel.SetAgility(v);
        UIElement_SetDouble(this.StatAgi, "Value", (double)actual);
        Storyboard_Begin(this.__nativeRootHandle, "UserControl.Anim.Agi.ValueChanged");
    }

    public void SetIntelligence(int v)
    {
        if (this.ViewModel == null)
        {
            return;
        }
        int actual = this.ViewModel.SetIntelligence(v);
        UIElement_SetDouble(this.StatInt, "Value", (double)actual);
        Storyboard_Begin(this.__nativeRootHandle, "UserControl.Anim.Int.ValueChanged");
    }

    public void SetCharisma(int v)
    {
        if (this.ViewModel == null)
        {
            return;
        }
        int actual = this.ViewModel.SetCharisma(v);
        UIElement_SetDouble(this.StatCha, "Value", (double)actual);
        Storyboard_Begin(this.__nativeRootHandle, "UserControl.Anim.Cha.ValueChanged");
    }

    public void IncrementStrength()
    {
        this.SyncFromControls();
        this.SetStrength(this.ViewModel.Strength + 1);
    }

    public void DecrementStrength()
    {
        this.SyncFromControls();
        this.SetStrength(this.ViewModel.Strength - 1);
    }

    public void IncrementAgility()
    {
        this.SyncFromControls();
        this.SetAgility(this.ViewModel.Agility + 1);
    }

    public void DecrementAgility()
    {
        this.SyncFromControls();
        this.SetAgility(this.ViewModel.Agility - 1);
    }

    public void IncrementIntelligence()
    {
        this.SyncFromControls();
        this.SetIntelligence(this.ViewModel.Intelligence + 1);
    }

    public void DecrementIntelligence()
    {
        this.SyncFromControls();
        this.SetIntelligence(this.ViewModel.Intelligence - 1);
    }

    public void IncrementCharisma()
    {
        this.SyncFromControls();
        this.SetCharisma(this.ViewModel.Charisma + 1);
    }

    public void DecrementCharisma()
    {
        this.SyncFromControls();
        this.SetCharisma(this.ViewModel.Charisma - 1);
    }

    public void ResetStats()
    {
        if (this.ViewModel == null)
        {
            return;
        }
        this.ViewModel.Reset();
        this.SyncToControls();
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
