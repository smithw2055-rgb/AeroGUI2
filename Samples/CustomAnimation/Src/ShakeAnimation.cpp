////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ShakeAnimation.h"

#include <NsCore/ReflectionImplement.h>
#include <NsCore/Nullable.h>
#include <NsGui/UIElementData.h>
#include <NsGui/PropertyMetadata.h>
#include <NsGui/AnimationClock.h>


using namespace CustomAnimation;
using namespace Noesis;


////////////////////////////////////////////////////////////////////////////////////////////////////
Nullable<float> ShakeAnimation::GetFrom() const
{
    return GetValue<Nullable<float>>(FromProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ShakeAnimation::SetFrom(Nullable<float> value)
{
    SetValue<Nullable<float>>(FromProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float ShakeAnimation::GetAmplitude() const
{
    return GetValue<float>(AmplitudeProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ShakeAnimation::SetAmplitude(float value)
{
    SetValue<float>(AmplitudeProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float ShakeAnimation::GetOscillations() const
{
    return GetValue<float>(OscillationsProperty);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ShakeAnimation::SetOscillations(float value)
{
    SetValue<float>(OscillationsProperty, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float ShakeAnimation::GetCurrentValueCore(float defaultOrigin, float, AnimationClock* clock)
{
    float start = GetFrom().HasValue() ? GetFrom().GetValue() : defaultOrigin;
    float t = clock->GetCurrentProgress();
    float amplitude = GetAmplitude();
    float oscillations = GetOscillations();

    // Linear decay so the shake violently starts and smoothly settles to 0
    float decay = 1.0f - t; 

    // High frequency sine wave scaled by the decaying amplitude
    float val = sinf(TwoPi * t * oscillations) * amplitude * decay;

    return start + val;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Ptr<Freezable> ShakeAnimation::CreateInstanceCore() const
{
    return *new ShakeAnimation();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(CustomAnimation::ShakeAnimation, "CustomAnimation.ShakeAnimation")
{
    UIElementData* data = NsMeta<UIElementData>(TypeOf<SelfClass>());

    data->RegisterProperty<Nullable<float>>(FromProperty, "From", PropertyMetadata::Create(Nullable<float>()));
    data->RegisterProperty<float>(AmplitudeProperty, "Amplitude", PropertyMetadata::Create(15.0f));
    data->RegisterProperty<float>(OscillationsProperty, "Oscillations", PropertyMetadata::Create(5.0f));
}

NS_END_COLD_REGION

////////////////////////////////////////////////////////////////////////////////////////////////////
const DependencyProperty* ShakeAnimation::FromProperty;
const DependencyProperty* ShakeAnimation::AmplitudeProperty;
const DependencyProperty* ShakeAnimation::OscillationsProperty;
