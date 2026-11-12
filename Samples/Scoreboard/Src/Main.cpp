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

#include "MainPage.xaml.bin.h"
#include "Cheboyga.ttf.bin.h"
#include "PERRYGOT.TTF.bin.h"


using namespace Scoreboard;
using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<EnumConverter<Scoreboard::Team>>();
        RegisterComponent<EnumConverter<Scoreboard::Class>>();
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
            { "Fonts", Cheboyga_ttf },
            { "Fonts", PERRYGOT_TTF }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    void OnAppStartUp() override
    {
        Noesis::Ptr<Game> game = *new Game();
        game->SetName("Silvershard Mines");
        game->SetElapsedTime(16);
        game->SetSelectedTeam(0);
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Mage, 96, 8134124, 1831, 43, "Nam cras aenean", 476, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Rogue, 98, 8324715, 2954, 79, "Sed class vestibulum", 414, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Hunter, 45, 797117, 2615, 99, "Curae praesent", 383, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Hunter, 93, 481757, 6353, 34, "Adipiscing dis quisque", 327, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Fighter, 82, 743715, 37415, 80, "Est donec vivamus", 289, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Rogue, 21, 383571, 82114, 90, "Duis leo curabitur", 265, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Cleric, 86, 441751, 255131, 37, "Mus etiam aliquam", 259, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Mage, 60, 201175, 4915, 63, "Nunc mauris accumsan", 225, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Fighter, 30, 271735, 6715, 20, "Phasellus nullam", 195, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Cleric, 18, 87537, 95717, 54, "Consequat bibendum", 180, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Fighter, 88, 534123, 1241, 66, "Kaelen Ironfist", 175, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Mage, 42, 652132, 2341, 22, "Gromdar Frostwolf", 160, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Rogue, 75, 742131, 8562, 45, "Elara Nightshade", 155, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Hunter, 91, 943211, 1124, 88, "Sylas Arrowhawk", 145, Team::Horde));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Cleric, 55, 123456, 456123, 12, "Aveline Dawnblessed", 130, Team::Alliance));
        game->GetPlayers()->Add(MakePtr<Player>(Scoreboard::Class::Fighter, 67, 432198, 3214, 59, "Durok Bloodrage", 115, Team::Horde));

        GetMainWindow()->SetDataContext(game);
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
