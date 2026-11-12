////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "NumericUpDown.h"

#include <NsGui/Button.h>
#include <NsGui/UIElementData.h>
#include <NsGui/IntegrationAPI.h>


using namespace Noesis;
using namespace UserControls;


////////////////////////////////////////////////////////////////////////////////////////////////////
NumericUpDown::NumericUpDown()
{
    InitializeComponent();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int NumericUpDown::GetValue() const
{
    return DependencyObject::GetValue<int>(ValueProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::SetValue(int value)
{
    DependencyObject::SetValue<int>(ValueProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int NumericUpDown::GetMaxValue() const
{
    return DependencyObject::GetValue<int>(MaxValueProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::SetMaxValue(int value)
{
    DependencyObject::SetValue<int>(MaxValueProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int NumericUpDown::GetMinValue() const
{
    return DependencyObject::GetValue<int>(MinValueProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::SetMinValue(int value)
{
    DependencyObject::SetValue<int>(MinValueProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int NumericUpDown::GetStepValue() const
{
    return DependencyObject::GetValue<int>(StepValueProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::SetStepValue(int value)
{
    DependencyObject::SetValue<int>(StepValueProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
UIElement::RoutedEvent_<NumericUpDown::ValueChangedT> NumericUpDown::ValueChanged()
{
    return RoutedEvent_<ValueChangedT>(this, ValueChangedEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::InitializeComponent()
{
    GUI::LoadComponent(this, "NumericUpDown.xaml");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool NumericUpDown::ConnectEvent(BaseComponent* source, const char* event, const char* handler)
{
    NS_CONNECT_EVENT(Button, Click, UpButton_Click);
    NS_CONNECT_EVENT(Button, Click, DownButton_Click);

    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::OnValueChanged(const Noesis::RoutedPropertyChangedEventArgs<int>& args)
{
    RaiseEvent(args);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::UpButton_Click(BaseComponent*, const Noesis::RoutedEventArgs&)
{
    SetValue(GetValue() + GetStepValue());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void NumericUpDown::DownButton_Click(BaseComponent*, const Noesis::RoutedEventArgs&)
{
    SetValue(GetValue() - GetStepValue());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(NumericUpDown, "UserControls.NumericUpDown")
{
    auto OnValueChanged = [](DependencyObject* d, const DependencyPropertyChangedEventArgs& e)
    {
        NumericUpDown* control = DynamicCast<NumericUpDown*>(d);
        if (control != nullptr)
        {
            int oldValue = e.OldValue<int>();
            int newValue = e.NewValue<int>();

            RoutedPropertyChangedEventArgs<int> args(control, ValueChangedEvent, oldValue, newValue);
            control->OnValueChanged(args);
        }
    };

    auto OnCoercedValue = [](const DependencyObject* d, const void* value, void* coercedValue)
    {
        const NumericUpDown* control = DynamicCast<const NumericUpDown*>(d);
        if (control != nullptr)
        {
            int maxValue = control->GetMaxValue();
            int minValue = control->GetMinValue();

            int newValue = *(const int*)value;
            int& coerced = *(int*)coercedValue;

            coerced = Clip(newValue, minValue, maxValue);
            return true;
        }

        return false;
    };

    UIElementData* data = NsMeta<UIElementData>(TypeOf<SelfClass>());

    data->RegisterProperty<int>(ValueProperty, "Value", PropertyMetadata::Create(int(0),
        OnValueChanged, OnCoercedValue));

    data->RegisterProperty<int>(MaxValueProperty, "MaxValue", PropertyMetadata::Create(int(255),
        PropertyChangedCallback([](DependencyObject* d, const DependencyPropertyChangedEventArgs&)
        {
            d->CoerceValue(ValueProperty);
        })));
        
    data->RegisterProperty<int>(MinValueProperty, "MinValue", PropertyMetadata::Create(int(0),
        PropertyChangedCallback([](DependencyObject* d, const DependencyPropertyChangedEventArgs&)
        {
            d->CoerceValue(ValueProperty);
        })));

    data->RegisterProperty<int>(StepValueProperty, "StepValue", PropertyMetadata::Create(int(1)));

    data->RegisterEvent(ValueChangedEvent, "ValueChanged", RoutingStrategy_Bubble);
}

NS_END_COLD_REGION

////////////////////////////////////////////////////////////////////////////////////////////////////
const DependencyProperty* NumericUpDown::ValueProperty;
const DependencyProperty* NumericUpDown::MaxValueProperty;
const DependencyProperty* NumericUpDown::MinValueProperty;
const DependencyProperty* NumericUpDown::StepValueProperty;
const RoutedEvent* NumericUpDown::ValueChangedEvent;
