module CustomControl;

import Aero.UI;

public class DateTime : DateTimeControl
{
    private float _exactHour = 12.0f;
    private float _exactMinute = 0.0f;
    private float _exactSecond = 0.0f;
    private bool _updatingExact = false;

    public DateTime() : base()
    {
        this.UseSystemTime = true;
    }

    protected override void OnLiveSystemTimeSample(int yr, int mo, int dy, int hr, int mn, int sc)
    {
        this.OnLiveSystemTimeSampleMs(yr, mo, dy, hr, mn, sc, 0);
    }

    protected override void OnLiveSystemTimeSampleMs(int yr, int mo, int dy, int hr, int mn, int sc, int ms)
    {
        this._updatingExact = true;
        this._exactSecond = (float)sc + (float)ms / 1000.0f;
        this._exactMinute = (float)mn + this._exactSecond / 60.0f;
        this._exactHour = (float)hr + this._exactMinute / 60.0f;
        this.SetDateTime(yr, mo, dy, hr, mn, sc);
        this._updatingExact = false;
        BindingEngine.NotifyTargetPropertyChanged(this, "Hour", DependencyObject.BoxFloat(this._exactHour));
        BindingEngine.NotifyTargetPropertyChanged(this, "Minute", DependencyObject.BoxFloat(this._exactMinute));
        BindingEngine.NotifyTargetPropertyChanged(this, "Second", DependencyObject.BoxFloat(this._exactSecond));
        BindingEngine.NotifyTargetPropertyChanged(this, "Day", DependencyObject.BoxInt(this.Day));
        BindingEngine.NotifyTargetPropertyChanged(this, "Month", DependencyObject.BoxInt(this.Month));
        BindingEngine.NotifyTargetPropertyChanged(this, "Year", DependencyObject.BoxInt(this.Year));
    }

    protected override void OnTimeChanged()
    {
        if (!this._updatingExact)
        {
            this._exactSecond = (float)this.Second;
            this._exactMinute = (float)this.Minute + this._exactSecond / 60.0f;
            this._exactHour = (float)this.Hour + this._exactMinute / 60.0f;
        }
    }

    public override string GetTypeName()
    {
        return "DateTime";
    }

    public void SetExactTime(int year, int month, int day, float hour, float minute, float second)
    {
        this.UseSystemTime = false;
        this._updatingExact = true;
        this._exactHour = hour;
        this._exactMinute = minute;
        this._exactSecond = second;
        this.SetDateTime(year, month, day, (int)hour, (int)minute, (int)second);
        this._updatingExact = false;
        BindingEngine.NotifyTargetPropertyChanged(this, "Hour", DependencyObject.BoxFloat(this._exactHour));
        BindingEngine.NotifyTargetPropertyChanged(this, "Minute", DependencyObject.BoxFloat(this._exactMinute));
        BindingEngine.NotifyTargetPropertyChanged(this, "Second", DependencyObject.BoxFloat(this._exactSecond));
        BindingEngine.NotifyTargetPropertyChanged(this, "Day", DependencyObject.BoxInt(this.Day));
        BindingEngine.NotifyTargetPropertyChanged(this, "Month", DependencyObject.BoxInt(this.Month));
        BindingEngine.NotifyTargetPropertyChanged(this, "Year", DependencyObject.BoxInt(this.Year));
    }

    public void AdvanceTime(int deltaSeconds)
    {
        this.UseSystemTime = false;
        this._updatingExact = true;
        this.AdvanceSeconds(deltaSeconds);
        this._updatingExact = false;
        this._exactSecond = (float)this.Second;
        this._exactMinute = (float)this.Minute + this._exactSecond / 60.0f;
        this._exactHour = (float)this.Hour + this._exactMinute / 60.0f;
        BindingEngine.NotifyTargetPropertyChanged(this, "Hour", DependencyObject.BoxFloat(this._exactHour));
        BindingEngine.NotifyTargetPropertyChanged(this, "Minute", DependencyObject.BoxFloat(this._exactMinute));
        BindingEngine.NotifyTargetPropertyChanged(this, "Second", DependencyObject.BoxFloat(this._exactSecond));
    }

    public override bool SetPropertyValue(string propName, DependencyObject value)
    {
        if (propName == "Hour")
        {
            float hv = DependencyObject.UnboxFloat(value, 0.0f);
            this._updatingExact = true;
            this._exactHour = hv;
            this.Hour = (int)hv;
            this._updatingExact = false;
            return true;
        }
        if (propName == "Minute")
        {
            float mv = DependencyObject.UnboxFloat(value, 0.0f);
            this._updatingExact = true;
            this._exactMinute = mv;
            this.Minute = (int)mv;
            this._updatingExact = false;
            return true;
        }
        if (propName == "Second")
        {
            float sv = DependencyObject.UnboxFloat(value, 0.0f);
            this._updatingExact = true;
            this._exactSecond = sv;
            this.Second = (int)sv;
            this._updatingExact = false;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName)
    {
        if (propName == "Hour") { return DependencyObject.BoxFloat(this._exactHour > 0.0f || this.Hour == 0 ? this._exactHour : (float)this.Hour); }
        if (propName == "Minute") { return DependencyObject.BoxFloat(this._exactMinute > 0.0f || this.Minute == 0 ? this._exactMinute : (float)this.Minute); }
        if (propName == "Second") { return DependencyObject.BoxFloat(this._exactSecond > 0.0f || this.Second == 0 ? this._exactSecond : (float)this.Second); }
        return base.GetPropertyValue(propName);
    }
}

[Observable]
public partial class MainWindow : Page
{
    public MainWindow()
    {
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void AdvanceAllClocks(int deltaSeconds)
    {
        this.AdvanceClocksInSubtree(this, deltaSeconds);
    }

    private void AdvanceClocksInSubtree(UIElement node, int deltaSeconds)
    {
        if (node == null) { return; }
        DateTime dt = node as DateTime;
        if (dt != null)
        {
            dt.AdvanceTime(deltaSeconds);
        }
        int n = node.ChildCount();
        int i = 0;
        while (i < n)
        {
            this.AdvanceClocksInSubtree(node.ChildAt(i), deltaSeconds);
            i = i + 1;
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
