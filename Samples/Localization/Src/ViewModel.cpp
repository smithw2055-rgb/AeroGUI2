////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ViewModel.h"

#include <NsCore/ReflectionImplement.h>
#include <NsGui/ObservableCollection.h>


using namespace Noesis;
using namespace Localization;


////////////////////////////////////////////////////////////////////////////////////////////////////
Language::Language(const char* name, const char* sourcePath, Noesis::FlowDirection flow):
    _name(name), _source(sourcePath), _flow(flow)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const char* Language::GetName() const
{
    return _name.Str();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const Uri& Language::GetSource() const
{
    return _source;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Noesis::FlowDirection Language::GetFlowDirection() const
{
    return _flow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
ViewModel::ViewModel()
{
    _languages = *new ObservableCollection<Language>();
    _languages->Add(MakePtr<Language>("English", "Language-en.xaml", FlowDirection_LeftToRight));
    _languages->Add(MakePtr<Language>("Français", "Language-fr.xaml", FlowDirection_LeftToRight));
    _languages->Add(MakePtr<Language>("日本語", "Language-jp.xaml", FlowDirection_LeftToRight));
    _languages->Add(MakePtr<Language>("عربي", "Language-ar.xaml", FlowDirection_RightToLeft));

    SetSelectedLanguage(_languages->Get(0));

    SetSoundLevel(100.0f);
    SetMusicLevel(70.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
ObservableCollection<Language>* ViewModel::GetLanguages() const
{
    return _languages;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetSelectedLanguage(Language* language)
{
    if (_selectedLanguage != language)
    {
        _selectedLanguage = language;
        OnPropertyChanged("SelectedLanguage");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Language* ViewModel::GetSelectedLanguage() const
{
    return _selectedLanguage;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetSoundLevel(float level)
{
    if (_soundLevel != level)
    {
        _soundLevel = level;
        OnPropertyChanged("SoundLevel");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float ViewModel::GetSoundLevel() const
{
    return _soundLevel;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetMusicLevel(float level)
{
    if (_musicLevel != level)
    {
        _musicLevel = level;
        OnPropertyChanged("MusicLevel");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float ViewModel::GetMusicLevel() const
{
    return _musicLevel;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(ViewModel)
{
    NsProp("Languages", &ViewModel::GetLanguages);
    NsProp("SelectedLanguage", &ViewModel::GetSelectedLanguage, &ViewModel::SetSelectedLanguage);
    NsProp("SoundLevel", &ViewModel::GetSoundLevel, &ViewModel::SetSoundLevel);
    NsProp("MusicLevel", &ViewModel::GetMusicLevel, &ViewModel::SetMusicLevel);
}

NS_IMPLEMENT_REFLECTION(Language)
{
    NsProp("Name", &Language::GetName);
    NsProp("Source", &Language::GetSource);
    NsProp("FlowDirection", &Language::GetFlowDirection);
}

NS_END_COLD_REGION
