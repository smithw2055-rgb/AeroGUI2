////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __CUSTOMANIMATION_SHAKEANIMATION_H__
#define __CUSTOMANIMATION_SHAKEANIMATION_H__


#include <NsCore/Noesis.h>
#include <NsGui/DoubleAnimationBase.h>


namespace CustomAnimation
{

////////////////////////////////////////////////////////////////////////////////////////////////////
class ShakeAnimation final: public Noesis::DoubleAnimationBase
{
public:
    /// Specifies the starting value of the animation
    //@{
    Noesis::Nullable<float> GetFrom() const;
    void SetFrom(Noesis::Nullable<float> value);
    //@}

    /// Specifies the intensity/distance of the shake
    //@{
    float GetAmplitude() const;
    void SetAmplitude(float value);
    //@}

    /// Specifies the number of vibrations
    //@{
    float GetOscillations() const;
    void SetOscillations(float value);
    //@}

public:
    static const Noesis::DependencyProperty* FromProperty;
    static const Noesis::DependencyProperty* AmplitudeProperty;
    static const Noesis::DependencyProperty* OscillationsProperty;

private:
    float GetCurrentValueCore(float defaultOrigin, float defaultDestination,
        Noesis::AnimationClock* clock) override;
    Noesis::Ptr<Freezable> CreateInstanceCore() const override;

private:
    NS_DECLARE_REFLECTION(ShakeAnimation, DoubleAnimationBase)
};

}

#endif
