module UserControls;

import Aero.UI;

public partial class NumericUpDown : UserControl
{
    private float _value = 0.0f;
    private int _minValue = 0;
    private int _maxValue = 255;
    private int _stepValue = 1;

    public event RoutedPropertyChangedEventHandler<float> ValueChanged;

    public float Value
    {
        get { return this._value; }
        set { this.ApplyValue(value); }
    }

    public int MinValue
    {
        get { return this._minValue; }
        set
        {
            if (this._minValue != value)
            {
                this._minValue = value;
                this.ApplyValue(this._value);
                BindingEngine.NotifyTargetPropertyChanged(this, "MinValue", DependencyObject.BoxInt(this._minValue));
                BindingEngine.Flush();
            }
        }
    }

    public int MaxValue
    {
        get { return this._maxValue; }
        set
        {
            if (this._maxValue != value)
            {
                this._maxValue = value;
                this.ApplyValue(this._value);
                BindingEngine.NotifyTargetPropertyChanged(this, "MaxValue", DependencyObject.BoxInt(this._maxValue));
                BindingEngine.Flush();
            }
        }
    }

    public int StepValue
    {
        get { return this._stepValue; }
        set { this._stepValue = value; }
    }

    public NumericUpDown() : base()
    {
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public override string GetTypeName()
    {
        return "NumericUpDown";
    }

    private int RoundToInt(float v)
    {
        return v >= 0.0f ? (int)(v + 0.5f) : (int)(v - 0.5f);
    }

    private float ClampValue(float requested)
    {
        int rounded = this.RoundToInt(requested);
        if (rounded < this._minValue) { rounded = this._minValue; }
        if (rounded > this._maxValue) { rounded = this._maxValue; }
        return (float)rounded;
    }

    private void ApplyValue(float requested)
    {
        float next = this.ClampValue(requested);
        if (this._value == next)
        {
            return;
        }
        float oldVal = this._value;
        this._value = next;
        this.OnValueChanged(new RoutedPropertyChangedEventArgs<float>(oldVal, next, this));
        BindingEngine.NotifyTargetPropertyChanged(this, "Value", DependencyObject.BoxFloat(this._value));
        BindingEngine.Flush();
    }

    protected virtual void OnValueChanged(RoutedPropertyChangedEventArgs<float> e)
    {
        ValueChanged(this, e);
    }

    public void UpButton_Click(object sender, RoutedEventArgs e)
    {
        this.Value = this.Value + (float)this.StepValue;
    }

    public void DownButton_Click(object sender, RoutedEventArgs e)
    {
        this.Value = this.Value - (float)this.StepValue;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value)
    {
        if (propName == "Value") { this.Value = DependencyObject.UnboxFloat(value, this._value); return true; }
        if (propName == "MinValue") { this.MinValue = DependencyObject.UnboxInt(value, this._minValue); return true; }
        if (propName == "MaxValue") { this.MaxValue = DependencyObject.UnboxInt(value, this._maxValue); return true; }
        if (propName == "StepValue") { this.StepValue = DependencyObject.UnboxInt(value, this._stepValue); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName)
    {
        if (propName == "Value") { return DependencyObject.BoxInt(this.RoundToInt(this._value)); }
        if (propName == "MinValue") { return DependencyObject.BoxInt(this._minValue); }
        if (propName == "MaxValue") { return DependencyObject.BoxInt(this._maxValue); }
        if (propName == "StepValue") { return DependencyObject.BoxInt(this._stepValue); }
        return base.GetPropertyValue(propName);
    }
}
