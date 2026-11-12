////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/RegisterComponent.h>
#include <NsCore/EnumConverter.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedTextureProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/Window.h>
#include <NsApp/EntryPoint.h>

#include "ViewModel.h"

#include "MainPage.xaml.bin.h"
#include "Resources.xaml.bin.h"
#include "Gidole-Regular.ttf.bin.h"
#include "Sequel_Demo.ttf.bin.h"
#include "Background.jpg.bin.h"
#include "Character.png.bin.h"
#include "Axe.png.bin.h"
#include "BattleAxe.png.bin.h"
#include "Boots.png.bin.h"
#include "Breacan.png.bin.h"
#include "Breeches.png.bin.h"
#include "Club.png.bin.h"
#include "Cowl.png.bin.h"
#include "DwarvenRing.png.bin.h"
#include "ElvenRing.png.bin.h"
#include "EnchantedRing.png.bin.h"
#include "Frock.png.bin.h"
#include "GoldRing.png.bin.h"
#include "GuardianRing.png.bin.h"
#include "Halberd.png.bin.h"
#include "Hammer.png.bin.h"
#include "Hood.png.bin.h"
#include "Jerkin.png.bin.h"
#include "KnightHelmet.png.bin.h"
#include "LeatherGloves.png.bin.h"
#include "LeatherVambrace.png.bin.h"
#include "Mace.png.bin.h"
#include "MailChemise.png.bin.h"
#include "MailGreaves.png.bin.h"
#include "MailHood.png.bin.h"
#include "MailSkirt.png.bin.h"
#include "MailVambrace.png.bin.h"
#include "Mallet.png.bin.h"
#include "MetalShield.png.bin.h"
#include "MorningStar.png.bin.h"
#include "OuroborosRing.png.bin.h"
#include "PaddedGreaves.png.bin.h"
#include "Pantaloons.png.bin.h"
#include "PlateArmor.png.bin.h"
#include "PlateCuisses.png.bin.h"
#include "PlateGreaves.png.bin.h"
#include "PlateVambrace.png.bin.h"
#include "SageRing.png.bin.h"
#include "Sandals.png.bin.h"
#include "ShortSword.png.bin.h"
#include "SilverRing.png.bin.h"
#include "Sword.png.bin.h"
#include "Vest.png.bin.h"
#include "WarHammer.png.bin.h"
#include "WoodenShield.png.bin.h"
#include "WoolShirt.png.bin.h"


using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<EnumConverter<Inventory::Platform>>();
        RegisterComponent<EnumConverter<Inventory::ItemCategory>>();
    }

    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] = 
        {
            { "MainPage.xaml", MainPage_xaml },
            { "Resources.xaml", Resources_xaml }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] = 
        {
            { "Fonts", Gidole_Regular_ttf },
            { "Fonts", Sequel_Demo_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    Noesis::Ptr<TextureProvider> GetTextureProvider() const override
    {
        EmbeddedTexture textures[] = 
        {
            { "Images/Background.jpg", Background_jpg },
            { "Images/Character.png", Character_png },
            { "Images/Items/Axe.png", Axe_png},
            { "Images/Items/BattleAxe.png", BattleAxe_png},
            { "Images/Items/Boots.png", Boots_png},
            { "Images/Items/Breacan.png", Breacan_png},
            { "Images/Items/Breeches.png", Breeches_png},
            { "Images/Items/Club.png", Club_png},
            { "Images/Items/Cowl.png", Cowl_png},
            { "Images/Items/DwarvenRing.png", DwarvenRing_png},
            { "Images/Items/ElvenRing.png", ElvenRing_png},
            { "Images/Items/EnchantedRing.png", EnchantedRing_png},
            { "Images/Items/Frock.png", Frock_png},
            { "Images/Items/GoldRing.png", GoldRing_png},
            { "Images/Items/GuardianRing.png", GuardianRing_png},
            { "Images/Items/Halberd.png", Halberd_png},
            { "Images/Items/Hammer.png", Hammer_png},
            { "Images/Items/Hood.png", Hood_png},
            { "Images/Items/Jerkin.png", Jerkin_png},
            { "Images/Items/KnightHelmet.png", KnightHelmet_png},
            { "Images/Items/LeatherGloves.png", LeatherGloves_png},
            { "Images/Items/LeatherVambrace.png", LeatherVambrace_png},
            { "Images/Items/Mace.png", Mace_png},
            { "Images/Items/MailChemise.png", MailChemise_png},
            { "Images/Items/MailGreaves.png", MailGreaves_png},
            { "Images/Items/MailHood.png", MailHood_png},
            { "Images/Items/MailSkirt.png", MailSkirt_png},
            { "Images/Items/MailVambrace.png", MailVambrace_png},
            { "Images/Items/Mallet.png", Mallet_png},
            { "Images/Items/MetalShield.png", MetalShield_png},
            { "Images/Items/MorningStar.png", MorningStar_png},
            { "Images/Items/OuroborosRing.png", OuroborosRing_png},
            { "Images/Items/PaddedGreaves.png", PaddedGreaves_png},
            { "Images/Items/Pantaloons.png", Pantaloons_png},
            { "Images/Items/PlateArmor.png", PlateArmor_png},
            { "Images/Items/PlateCuisses.png", PlateCuisses_png},
            { "Images/Items/PlateGreaves.png", PlateGreaves_png},
            { "Images/Items/PlateVambrace.png", PlateVambrace_png},
            { "Images/Items/SageRing.png", SageRing_png},
            { "Images/Items/Sandals.png", Sandals_png},
            { "Images/Items/ShortSword.png", ShortSword_png},
            { "Images/Items/SilverRing.png", SilverRing_png},
            { "Images/Items/Sword.png", Sword_png},
            { "Images/Items/Vest.png", Vest_png},
            { "Images/Items/WarHammer.png", WarHammer_png},
            { "Images/Items/WoodenShield.png", WoodenShield_png},
            { "Images/Items/WoolShirt.png", WoolShirt_png }
        };

        return *new EmbeddedTextureProvider(textures);
    }

    Uri GetResources(bool /*darkTheme*/) const override
    {
        return "Resources.xaml";
    }

    void OnAppStartUp() override
    {
        GetMainWindow()->SetDataContext(MakePtr<Inventory::ViewModel>());
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
