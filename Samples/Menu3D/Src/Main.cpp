////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/RegisterComponent.h>
#include <NsCore/ReflectionImplement.h>
#include <NsCore/ReflectionImplementEnum.h>
#include <NsCore/EnumConverter.h>
#include <NsCore/Log.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/Application.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedTextureProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/DelegateCommand.h>
#include <NsApp/Window.h>
#include <NsApp/EntryPoint.h>

#include "MainPage.xaml.bin.h"
#include "Resources.xaml.bin.h"
#include "BackClouds.png.bin.h"
#include "FrontClouds.png.bin.h"
#include "FrontTrees.png.bin.h"
#include "LakeAndMountains.png.bin.h"
#include "SkyAndSun.jpg.bin.h"
#include "weblysleekuisb.ttf.bin.h"
#include "weblysleekuisl.ttf.bin.h"
#include "WaterDropBig.mp3.bin.h"
#include "WaterDropSmall.mp3.bin.h"


using namespace Noesis;
using namespace NoesisApp;


namespace Menu3D
{

////////////////////////////////////////////////////////////////////////////////////////////////////
class MenuDescription: public BaseComponent
{
public:
    MenuDescription(const char* desc, const char* accept, const char* cancel) : _description(desc),
        _acceptText(accept), _cancelText(cancel)
    {
    }

private:
    String _description;
    String _acceptText;
    String _cancelText;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(MenuDescription, BaseComponent)
    {
        NsProp("Description", &MenuDescription::_description);
        NsProp("AcceptText", &MenuDescription::_acceptText);
        NsProp("CancelText", &MenuDescription::_cancelText);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
class MainMenu: public BaseComponent
{
public:
    MainMenu()
    {
        _menuDescription = MakePtr<MenuDescription>("MAIN MENU", "ACCEPT", "EXIT");
        _exit = MakePtr<DelegateCommand>(MakeDelegate(this, &MainMenu::OnExit));
    }

private:
    void OnExit(BaseComponent*)
    {
        NS_LOG_INFO("Exiting game");
        Application::Current()->Shutdown();
    }

private:
    Noesis::Ptr<MenuDescription> _menuDescription;
    Noesis::Ptr<DelegateCommand> _exit;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(MainMenu, BaseComponent)
    {
        NsProp("Description", &MainMenu::_menuDescription);
        NsProp("Exit", &MainMenu::_exit);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
class SettingsMenu: public BaseComponent
{
public:
    SettingsMenu()
    {
        _menuDescription = MakePtr<MenuDescription>("SETTINGS", "ACCEPT", "BACK");
    }

private:
    Noesis::Ptr<MenuDescription> _menuDescription;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(SettingsMenu, BaseComponent)
    {
        NsProp("Description", &SettingsMenu::_menuDescription);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
class StartMenu: public BaseComponent
{
public:
    StartMenu()
    {
        _menuDescription = MakePtr<MenuDescription>("SELECT DIFFICULTY", "ACCEPT", "BACK");
        _startCasual = MakePtr<DelegateCommand>(MakeDelegate(this, &StartMenu::OnStartCasual));
        _startNormal = MakePtr<DelegateCommand>(MakeDelegate(this, &StartMenu::OnStartNormal));
        _startExpert = MakePtr<DelegateCommand>(MakeDelegate(this, &StartMenu::OnStartExpert));
    }

private:
    void OnStartCasual(BaseComponent*)
    {
        NS_LOG_INFO("Start Casual");
    }

    void OnStartNormal(BaseComponent*)
    {
        NS_LOG_INFO("Start Normal");
    }

    void OnStartExpert(BaseComponent*)
    {
        NS_LOG_INFO("Start Expert");
    }

private:
    Noesis::Ptr<MenuDescription> _menuDescription;
    Noesis::Ptr<DelegateCommand> _startCasual;
    Noesis::Ptr<DelegateCommand> _startNormal;
    Noesis::Ptr<DelegateCommand> _startExpert;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(StartMenu, BaseComponent)
    {
        NsProp("Description", &StartMenu::_menuDescription);
        NsProp("StartCasual", &StartMenu::_startCasual);
        NsProp("StartNormal", &StartMenu::_startNormal);
        NsProp("StartExpert", &StartMenu::_startExpert);
    }
};

enum class Platform
{
    PC,
    PS4,
    XBOX
};

////////////////////////////////////////////////////////////////////////////////////////////////////
class ViewModel: public BaseComponent
{
public:
    ViewModel()
    {
        _mainMenu = MakePtr<MainMenu>();
        _startMenu = MakePtr<StartMenu>();
        _settingsMenu = MakePtr<SettingsMenu>();

      #if defined(NS_PLATFORM_GAME_CORE) || defined(NS_PLATFORM_NX)
        _platform = Platform::XBOX;
      #elif defined(NS_PLATFORM_SCE)
        _platform = Platform::PS4;
      #else
        _platform = Platform::PC;
      #endif
    }

private:
    Noesis::Ptr<MainMenu> _mainMenu;
    Noesis::Ptr<StartMenu> _startMenu;
    Noesis::Ptr<SettingsMenu> _settingsMenu;
    Platform _platform;

private:
    NS_IMPLEMENT_INLINE_REFLECTION(ViewModel, BaseComponent)
    {
        NsProp("MainMenu", &ViewModel::_mainMenu);
        NsProp("StartMenu", &ViewModel::_startMenu);
        NsProp("SettingsMenu", &ViewModel::_settingsMenu);
        NsProp("Platform", &ViewModel::_platform);
    }
};

}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_IMPLEMENT_INLINE_REFLECTION_ENUM(Menu3D::Platform, "Platform")
{
    NsVal("PC", Menu3D::Platform::PC);
    NsVal("PS4", Menu3D::Platform::PS4);
    NsVal("XBOX", Menu3D::Platform::XBOX);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<EnumConverter<Menu3D::Platform>>();
    }

    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] =
        {
            { "MainPage.xaml", MainPage_xaml },
            { "Resources.xaml", Resources_xaml },
            { "Sounds/WaterDropBig.mp3", WaterDropBig_mp3 },
            { "Sounds/WaterDropSmall.mp3", WaterDropSmall_mp3 }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] = 
        {
            { "Fonts", weblysleekuisb_ttf },
            { "Fonts", weblysleekuisl_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }

    Noesis::Ptr<TextureProvider> GetTextureProvider() const override
    {
        EmbeddedTexture textures[] = 
        {
            { "Images/BackClouds.png", BackClouds_png },
            { "Images/FrontClouds.png", FrontClouds_png },
            { "Images/FrontTrees.png", FrontTrees_png },
            { "Images/LakeAndMountains.png", LakeAndMountains_png },
            { "Images/SkyAndSun.jpg", SkyAndSun_jpg }
        };

        return *new EmbeddedTextureProvider(textures);
    }

    Uri GetResources(bool /*darkTheme*/) const override
    {
        return "Resources.xaml";
    }

    void OnAppStartUp() override
    {
        GetMainWindow()->SetDataContext(MakePtr<Menu3D::ViewModel>());
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
