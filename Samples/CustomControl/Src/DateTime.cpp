////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#define _CRT_SECURE_NO_WARNINGS
#include "DateTime.h"

#include <NsCore/ReflectionImplement.h>
#include <NsGui/UIElementData.h>
#include <NsGui/StudioMeta.h>
#include <NsGui/IView.h>

#include <time.h>


using namespace Noesis;
using namespace CustomControl;


////////////////////////////////////////////////////////////////////////////////////////////////////
bool DateTime::GetUseSystemTime() const
{
    return GetValue<bool>(UseSystemTimeProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetUseSystemTime(bool use)
{
    SetValue<bool>(UseSystemTimeProperty, use);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int DateTime::GetDay() const
{
    return GetValue<int>(DayProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetDay(int day)
{
    SetValue<int>(DayProperty, day);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int DateTime::GetMonth() const
{
    return GetValue<int>(MonthProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetMonth(int month)
{
    SetValue<int>(MonthProperty, month);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int DateTime::GetYear() const
{
    return GetValue<int>(YearProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetYear(int year)
{
    SetValue<int>(YearProperty, year);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float DateTime::GetHour() const
{
    return GetValue<float>(HourProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetHour(float hour)
{
    SetValue<float>(HourProperty, hour);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float DateTime::GetMinute() const
{
    return GetValue<float>(MinuteProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetMinute(float minute)
{
    SetValue<float>(MinuteProperty, minute);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float DateTime::GetSecond() const
{
    return GetValue<float>(SecondProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::SetSecond(float second)
{
    SetValue<float>(SecondProperty, second);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::Tick(IView*)
{
    if (GetUseSystemTime())
    {
        struct timespec ts;

      #if defined(NS_PLATFORM_ANDROID) || defined(NS_PLATFORM_APPLE)
        clock_gettime(CLOCK_REALTIME, &ts);
      #else
        timespec_get(&ts, TIME_UTC);
      #endif

        time_t rawTime = ts.tv_sec;
        struct tm* localTime = localtime(&rawTime);

        if (localTime != nullptr)
        {
            float currentSecond = (float)localTime->tm_sec + ((float)ts.tv_nsec / 1e9f);
            float currentMinute = (float)localTime->tm_min + (currentSecond / 60.0f);
            float currentHour = (float)localTime->tm_hour + (currentMinute / 60.0f);

            SetSecond(currentSecond);
            SetMinute(currentMinute);
            SetHour(currentHour);

            SetYear(localTime->tm_year + 1900);
            SetMonth(localTime->tm_mon + 1);
            SetDay(localTime->tm_mday);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::OnConnectToView(IView* view)
{
    ParentClass::OnConnectToView(view);
    view->Rendering() += MakeDelegate(this, &DateTime::Tick);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void DateTime::OnDisconnectFromView()
{
    GetView()->Rendering() -= MakeDelegate(this, &DateTime::Tick);
    ParentClass::OnDisconnectFromView();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(DateTime, "CustomControl.DateTime")
{
  #ifdef NS_HAVE_STUDIO
    NsProp("UseSystemTime", &DateTime::GetUseSystemTime, &DateTime::SetUseSystemTime)
        .Meta<StudioOrder>(0);
    NsProp("Year", &DateTime::GetYear, &DateTime::SetYear)
        .Meta<StudioOrder>(1);
    NsProp("Month", &DateTime::GetMonth, &DateTime::SetMonth)
        .Meta<StudioOrder>(2);
    NsProp("Day", &DateTime::GetDay, &DateTime::SetDay)
        .Meta<StudioOrder>(3);
    NsProp("Hour", &DateTime::GetHour, &DateTime::SetHour)
        .Meta<StudioOrder>(4);
    NsProp("Minute", &DateTime::GetMinute, &DateTime::SetMinute)
        .Meta<StudioOrder>(5);
    NsProp("Second", &DateTime::GetSecond, &DateTime::SetSecond)
        .Meta<StudioOrder>(6);
  #endif

    UIElementData* data = NsMeta<UIElementData>(TypeOf<DateTime>());
    data->RegisterProperty<bool>(UseSystemTimeProperty, "UseSystemTime", PropertyMetadata::Create(true));
    data->RegisterProperty<int>(DayProperty, "Day", PropertyMetadata::Create(int(1)));
    data->RegisterProperty<int>(MonthProperty, "Month", PropertyMetadata::Create(int(1)));
    data->RegisterProperty<int>(YearProperty, "Year", PropertyMetadata::Create(int(2000)));
    data->RegisterProperty<float>(HourProperty, "Hour", PropertyMetadata::Create(0.0f));
    data->RegisterProperty<float>(MinuteProperty, "Minute", PropertyMetadata::Create(0.0f));
    data->RegisterProperty<float>(SecondProperty, "Second", PropertyMetadata::Create(0.0f));
}

NS_END_COLD_REGION

////////////////////////////////////////////////////////////////////////////////////////////////////
const DependencyProperty* DateTime::UseSystemTimeProperty;
const DependencyProperty* DateTime::DayProperty;
const DependencyProperty* DateTime::MonthProperty;
const DependencyProperty* DateTime::YearProperty;
const DependencyProperty* DateTime::HourProperty;
const DependencyProperty* DateTime::MinuteProperty;
const DependencyProperty* DateTime::SecondProperty;
