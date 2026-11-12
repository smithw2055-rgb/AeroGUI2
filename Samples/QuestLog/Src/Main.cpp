////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/RegisterComponent.h>
#include <NsCore/EnumConverter.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/EmbeddedTextureProvider.h>
#include <NsApp/EntryPoint.h>
#include <NsApp/Window.h>

#include "ViewModel.h"

#include "Resources.xaml.bin.h"
#include "MainPage.xaml.bin.h"
#include "Dungeon.ttf.bin.h"
#include "GentiumBookBasic.ttf.bin.h"
#include "Background.png.bin.h"
#include "Image0.png.bin.h"
#include "Image1.png.bin.h"
#include "Image2.png.bin.h"
#include "WaterDropSmall.mp3.bin.h"


using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<EnumConverter<QuestLog::QuestDifficulty>>();
    }

    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] = 
        {
            { "Resources.xaml", Resources_xaml },
            { "MainPage.xaml", MainPage_xaml },
            { "Sounds/WaterDropSmall.mp3", WaterDropSmall_mp3 }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] = 
        {
            { "", Dungeon_ttf },
            { "", GentiumBookBasic_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    Noesis::Ptr<TextureProvider> GetTextureProvider() const override
    {
        EmbeddedTexture textures[] = 
        {
            { "Images/Background.png", Background_png },
            { "Images/Image0.png", Image0_png },
            { "Images/Image1.png", Image1_png },
            { "Images/Image2.png", Image2_png }
        };

        return *new EmbeddedTextureProvider(textures);
    }

    Uri GetResources(bool /*darkTheme*/) const override
    {
        return "Resources.xaml";
    }

    void OnAppStartUp() override
    {
        GetMainWindow()->SetDataContext(MakePtr<QuestLog::ViewModel>());
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
