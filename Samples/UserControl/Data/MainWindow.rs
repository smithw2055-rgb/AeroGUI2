module UserControl;

import Aero.UI;
import UserControls;

[Observable]
public class ViewModel : ViewModelBase
{
    private int _totalBudget = 50;
    private int _minStat = 3;
    private int _maxStat = 20;
    private int _strength = 10;
    private int _agility = 14;
    private int _intelligence = 8;
    private int _charisma = 12;
    private int _pointsAvailable = 6;

    public int TotalBudget
    {
        get { return this._totalBudget; }
        set { this.SetProperty(ref this._totalBudget, value, "TotalBudget"); }
    }

    public int MinStat
    {
        get { return this._minStat; }
        set { this.SetProperty(ref this._minStat, value, "MinStat"); }
    }

    public int MaxStat
    {
        get { return this._maxStat; }
        set { this.SetProperty(ref this._maxStat, value, "MaxStat"); }
    }

    public int Strength
    {
        get { return this._strength; }
        set { this.SetProperty(ref this._strength, value, "Strength"); }
    }

    public int Agility
    {
        get { return this._agility; }
        set { this.SetProperty(ref this._agility, value, "Agility"); }
    }

    public int Intelligence
    {
        get { return this._intelligence; }
        set { this.SetProperty(ref this._intelligence, value, "Intelligence"); }
    }

    public int Charisma
    {
        get { return this._charisma; }
        set { this.SetProperty(ref this._charisma, value, "Charisma"); }
    }

    public int PointsAvailable
    {
        get { return this._pointsAvailable; }
        set { this.SetProperty(ref this._pointsAvailable, value, "PointsAvailable"); }
    }

    public ViewModel()
    {
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

public partial class MainWindow : Page
{
    private ViewModel _vm;

    public ViewModel VM { get { return this._vm; } }
    public ViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new ViewModel();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
        this.SyncFromControls();
    }

    public void SyncFromControls()
    {
        if (this.VM == null)
        {
            return;
        }

        if (this.StatStr != null) { this.VM.Strength = this.StatStr.Value; }
        if (this.StatAgi != null) { this.VM.Agility = this.StatAgi.Value; }
        if (this.StatInt != null) { this.VM.Intelligence = this.StatInt.Value; }
        if (this.StatCha != null) { this.VM.Charisma = this.StatCha.Value; }

        this.VM.RecalculatePoints();
    }

    public void SyncToControls()
    {
        if (this.VM == null)
        {
            return;
        }

        if (this.StatStr != null) { this.StatStr.Value = this.VM.Strength; }
        if (this.StatAgi != null) { this.StatAgi.Value = this.VM.Agility; }
        if (this.StatInt != null) { this.StatInt.Value = this.VM.Intelligence; }
        if (this.StatCha != null) { this.StatCha.Value = this.VM.Charisma; }
    }

    public void SetStrength(int v)
    {
        if (this.VM == null)
        {
            return;
        }
        int actual = this.VM.SetStrength(v);
        if (this.StatStr != null) { this.StatStr.Value = actual; }
        this.BeginStoryboard("UserControl.Anim.Str.ValueChanged");
    }

    public void SetAgility(int v)
    {
        if (this.VM == null)
        {
            return;
        }
        int actual = this.VM.SetAgility(v);
        if (this.StatAgi != null) { this.StatAgi.Value = actual; }
        this.BeginStoryboard("UserControl.Anim.Agi.ValueChanged");
    }

    public void SetIntelligence(int v)
    {
        if (this.VM == null)
        {
            return;
        }
        int actual = this.VM.SetIntelligence(v);
        if (this.StatInt != null) { this.StatInt.Value = actual; }
        this.BeginStoryboard("UserControl.Anim.Int.ValueChanged");
    }

    public void SetCharisma(int v)
    {
        if (this.VM == null)
        {
            return;
        }
        int actual = this.VM.SetCharisma(v);
        if (this.StatCha != null) { this.StatCha.Value = actual; }
        this.BeginStoryboard("UserControl.Anim.Cha.ValueChanged");
    }

    public void IncrementStrength()
    {
        this.SyncFromControls();
        this.SetStrength(this.VM.Strength + 1);
    }

    public void DecrementStrength()
    {
        this.SyncFromControls();
        this.SetStrength(this.VM.Strength - 1);
    }

    public void IncrementAgility()
    {
        this.SyncFromControls();
        this.SetAgility(this.VM.Agility + 1);
    }

    public void DecrementAgility()
    {
        this.SyncFromControls();
        this.SetAgility(this.VM.Agility - 1);
    }

    public void IncrementIntelligence()
    {
        this.SyncFromControls();
        this.SetIntelligence(this.VM.Intelligence + 1);
    }

    public void DecrementIntelligence()
    {
        this.SyncFromControls();
        this.SetIntelligence(this.VM.Intelligence - 1);
    }

    public void IncrementCharisma()
    {
        this.SyncFromControls();
        this.SetCharisma(this.VM.Charisma + 1);
    }

    public void DecrementCharisma()
    {
        this.SyncFromControls();
        this.SetCharisma(this.VM.Charisma - 1);
    }

    public void ResetStats()
    {
        if (this.VM == null)
        {
            return;
        }
        this.VM.Reset();
        this.SyncToControls();
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
