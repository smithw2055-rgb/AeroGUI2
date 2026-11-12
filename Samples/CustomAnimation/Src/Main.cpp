////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/ReflectionImplementEmpty.h>
#include <NsCore/RegisterComponent.h>
#include <NsCore/EnumConverter.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EntryPoint.h>

#include "BounceAnimation.h"
#include "ElasticAnimation.h"
#include "ExponentialAnimation.h"
#include "BackAnimation.h"
#include "CircleAnimation.h"
#include "ShakeAnimation.h"

#include "MainPage.xaml.bin.h"
#include "Aero Matics Regular.ttf.bin.h"


using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<CustomAnimation::BounceAnimation>();
        RegisterComponent<CustomAnimation::ElasticAnimation>();
        RegisterComponent<CustomAnimation::ExponentialAnimation>();
        RegisterComponent<CustomAnimation::BackAnimation>();
        RegisterComponent<CustomAnimation::CircleAnimation>();
        RegisterComponent<CustomAnimation::ShakeAnimation>();

        RegisterComponent<EnumConverter<CustomAnimation::BounceAnimation::EdgeBehaviorEnum>>();
        RegisterComponent<EnumConverter<CustomAnimation::ElasticAnimation::EdgeBehaviorEnum>>();
        RegisterComponent<EnumConverter<CustomAnimation::ExponentialAnimation::EdgeBehaviorEnum>>();
        RegisterComponent<EnumConverter<CustomAnimation::BackAnimation::EdgeBehaviorEnum>>();
        RegisterComponent<EnumConverter<CustomAnimation::CircleAnimation::DirectionEnum>>();
    }

    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] = 
        {
            { "MainPage.xaml", MainPage_xaml }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] =
        {
            { "", Aero_Matics_Regular_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
int NsMain(int argc, char** argv)
{
    AppLauncher launcher;
    launcher.SetArguments(argc, argv);
    launcher.SetStartupUri("MainPage.xaml");
    return launcher.Run();
}
