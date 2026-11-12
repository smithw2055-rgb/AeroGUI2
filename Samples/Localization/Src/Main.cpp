////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/EmbeddedTextureProvider.h>
#include <NsApp/Window.h>
#include <NsApp/EntryPoint.h>

#include "ViewModel.h"

#include "MainPage.xaml.bin.h"
#include "Resources.xaml.bin.h"
#include "Language-en.xaml.bin.h"
#include "Language-fr.xaml.bin.h"
#include "Language-jp.xaml.bin.h"
#include "Language-ar.xaml.bin.h"
#include "Flag-en.png.bin.h"
#include "Flag-fr.png.bin.h"
#include "Flag-jp.png.bin.h"
#include "Flag-ar.png.bin.h"
#include "PTSans-Bold.ttf.bin.h"
#include "PTSans-BoldItalic.ttf.bin.h"
#include "PTSans-Italic.ttf.bin.h"
#include "PTSans-Regular.ttf.bin.h"
#include "FontopoNIHONGO.otf.bin.h"
#include "A_Nefel_Sereke.ttf.bin.h"


using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] = 
        {
            { "MainPage.xaml", MainPage_xaml },
            { "Resources.xaml", Resources_xaml },
            { "Language-en.xaml", Language_en_xaml },
            { "Language-fr.xaml", Language_fr_xaml },
            { "Language-jp.xaml", Language_jp_xaml },
            { "Language-ar.xaml", Language_ar_xaml }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<TextureProvider> GetTextureProvider() const override
    {
        EmbeddedTexture textures[] =
        {
            { "Flag-en.png", Flag_en_png },
            { "Flag-fr.png", Flag_fr_png },
            { "Flag-jp.png", Flag_jp_png },
            { "Flag-ar.png", Flag_ar_png }
        };

        return *new EmbeddedTextureProvider(textures);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] = 
        {
            { "", PTSans_Bold_ttf },
            { "", PTSans_BoldItalic_ttf },
            { "", PTSans_Italic_ttf },
            { "", PTSans_Regular_ttf },
            { "", FontopoNIHONGO_otf },
            { "", A_Nefel_Sereke_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    Uri GetResources(bool /*darkTheme*/) const override
    {
        return "Resources.xaml";
    }

    void OnAppStartUp() override
    {
        GetMainWindow()->SetDataContext(MakePtr<Localization::ViewModel>());
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
