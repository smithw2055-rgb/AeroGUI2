////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __GALLERY_MAINWINDOW_H__
#define __GALLERY_MAINWINDOW_H__


#include <NsCore/Noesis.h>
#include <NsCore/Hash.h>
#include <NsCore/String.h>
#include <NsCore/ReflectionDeclare.h>
#include <NsApp/Window.h>


namespace Noesis
{
class Panel;
class ToggleButton;
class ResourceDictionary;
}

namespace Gallery
{

////////////////////////////////////////////////////////////////////////////////////////////////////
/// Control Gallery Main Window code behind
////////////////////////////////////////////////////////////////////////////////////////////////////
class MainWindow final: public NoesisApp::Window
{
public:
    MainWindow();

    void UpdateSample(const char* sampleName);
    void UpdateTheme(const char* colorName, const char* accentName);

private:
    void InitializeComponent();
    bool ConnectField(BaseComponent* object, const char* name) override;
    Noesis::ResourceDictionary* GetTheme(const char* color, const char* accent);

private:
    Noesis::Panel* mLayoutRoot;
    Noesis::Panel* mWindowContent;

    Noesis::Panel* mSamplePanel;
    Noesis::ContentControl* mSampleContainer1;
    Noesis::ContentControl* mSampleContainer2;

    Noesis::FrameworkElement* mSampleOffset;
    Noesis::FrameworkElement* mSelectorBar;
    Noesis::ToggleButton* mSelectorExpanderButton;

    typedef Noesis::HashMap<Noesis::String, Noesis::Ptr<Noesis::ResourceDictionary>>
        ThemeDictionaries;
    ThemeDictionaries mThemeDictionaries;

    NS_DECLARE_REFLECTION(MainWindow, Window)
};

}


#endif
