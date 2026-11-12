////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/ReflectionImplement.h>
#include <NsCore/RegisterComponent.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/EmbeddedTextureProvider.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EntryPoint.h>
#include <NsApp/Window.h>

#include <NsGui/BitmapImage.h>
#include <NsGui/ObservableCollection.h>

#include "MainPage.xaml.bin.h"
#include "Roboto-Regular.ttf.bin.h"
#include "Roboto-Bold.ttf.bin.h"
#include "Earth.png.bin.h"
#include "Jupiter.png.bin.h"
#include "Mars.png.bin.h"
#include "Mercury.png.bin.h"
#include "Neptune.png.bin.h"
#include "Pluto.png.bin.h"
#include "Saturn.png.bin.h"
#include "Space.png.bin.h"
#include "Sun.png.bin.h"
#include "Uranus.png.bin.h"
#include "Venus.png.bin.h"


using namespace Noesis;
using namespace NoesisApp;


namespace DataBinding
{

NS_WARNING_PUSH
NS_CLANG_WARNING_DISABLE("-Wunused-private-field")

////////////////////////////////////////////////////////////////////////////////////////////////////
class SolarSystemObject: public BaseComponent
{
public:
    SolarSystemObject(const char* name, float orbit, float diameter, Noesis::Ptr<ImageSource> image,
        const char* details): _name(name), _orbit(orbit), _diameter(diameter), _details(details),
        _image(Noesis::MoveArg(image))
    {
    }

private:
    String _name;
    float _orbit;
    float _diameter;
    String _details;
    Noesis::Ptr<ImageSource> _image;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(SolarSystemObject, BaseComponent)
    {
        NsProp("Name", &SolarSystemObject::_name);
        NsProp("Orbit", &SolarSystemObject::_orbit);
        NsProp("Diameter", &SolarSystemObject::_diameter);
        NsProp("Details", &SolarSystemObject::_details);
        NsProp("Image", &SolarSystemObject::_image);
    }
};

NS_WARNING_POP

////////////////////////////////////////////////////////////////////////////////////////////////////
class SolarSystem: public BaseComponent
{
public:
    SolarSystem()
    {
        _solarSystemObjects = MakePtr<ObservableCollection<SolarSystemObject>>();

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Sun", 0.0f, 1380000.0f, MakePtr<BitmapImage>("Images/Sun.png"),
            "The yellow dwarf star in the center of our solar system."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Mercury", 0.38f, 4880.0f, MakePtr<BitmapImage>("Images/Mercury.png"),
            "The small and rocky planet Mercury is the closest planet to the Sun."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Venus", 0.72f, 12103.6f, MakePtr<BitmapImage>("Images/Venus.png"),
            "At first glance, if Earth had a twin, it would be Venus."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Earth", 1.0f, 12756.3f, MakePtr<BitmapImage>("Images/Earth.png"),
            "Earth, our home planet, is the only planet in our solar system known to harbor life."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
                "Mars", 1.52f, 6794.0f, MakePtr<BitmapImage>("Images/Mars.png"),
                "The red planet Mars has inspired wild flights of imagination over the centuries."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Jupiter", 5.20f, 142984.0f, MakePtr<BitmapImage>("Images/Jupiter.png"),
            "With its numerous moons and several rings, the Jupiter system is a 'mini-solar system'."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Saturn", 9.54f, 120536.0f, MakePtr<BitmapImage>("Images/Saturn.png"),
            "Saturn is the most distant of the five planets known to ancient stargazers."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Uranus", 19.218f, 51118.0f, MakePtr<BitmapImage>("Images/Uranus.png"),
            "Uranus gets its blue-green color from methane gas above the deeper cloud layers."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Neptune", 30.06f, 49532.0f, MakePtr<BitmapImage>("Images/Neptune.png"),
            "Neptune was the first planet located through mathematical predictions."
        ));

        _solarSystemObjects->Add(MakePtr<SolarSystemObject>(
            "Pluto", 39.5f, 2274.0f, MakePtr<BitmapImage>("Images/Pluto.png"),
            "Long considered to be the smallest, coldest, and most distant planet from the Sun."
        ));
    }

private:
    Noesis::Ptr<ObservableCollection<SolarSystemObject>> _solarSystemObjects;

    NS_IMPLEMENT_INLINE_REFLECTION(SolarSystem, BaseComponent)
    {
        NsProp("SolarSystemObjects", &SolarSystem::_solarSystemObjects);
    }
};

}

////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
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
            { "", Roboto_Regular_ttf },
            { "", Roboto_Bold_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    Noesis::Ptr<TextureProvider> GetTextureProvider() const override
    {
        EmbeddedTexture textures[] =
        {
            { "Images/Earth.png", Earth_png },
            { "Images/Jupiter.png", Jupiter_png },
            { "Images/Mars.png", Mars_png },
            { "Images/Mercury.png", Mercury_png },
            { "Images/Neptune.png", Neptune_png },
            { "Images/Pluto.png", Pluto_png },
            { "Images/Saturn.png", Saturn_png },
            { "Images/Space.png", Space_png },
            { "Images/Sun.png", Sun_png },
            { "Images/Uranus.png", Uranus_png },
            { "Images/Venus.png", Venus_png }
        };

        return *new EmbeddedTextureProvider(textures);
    }

    void OnAppStartUp() override
    {
        GetMainWindow()->SetDataContext(MakePtr<DataBinding::SolarSystem>());
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
