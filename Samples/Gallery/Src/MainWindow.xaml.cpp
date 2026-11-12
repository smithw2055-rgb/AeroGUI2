////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "MainWindow.xaml.h"

#include <NsGui/IntegrationAPI.h>
#include <NsGui/Panel.h>
#include <NsGui/ToggleButton.h>
#include <NsGui/ScaleTransform.h>
#include <NsGui/ResourceDictionary.h>
#include <NsGui/Collection.h>
#include <NsGui/UICollection.h>
#include <NsGui/UIElementData.h>
#include <NsGui/PropertyMetadata.h>
#include <NsCore/Nullable.h>
#include <NsApp/Application.h>
#include <NsDrawing/Color.h>


using namespace Noesis;
using namespace NoesisApp;
using namespace Gallery;


namespace Gallery
{

////////////////////////////////////////////////////////////////////////////////////////////////////
struct Sample: public BaseComponent
{
    Sample(const char* name_, const char* icon_) : name(name_), icon(icon_)
    {
    }

    String name;
    String icon;

    NS_IMPLEMENT_INLINE_REFLECTION(Sample, BaseComponent, "Gallery.Sample")
    {
        NsProp("Name", &Sample::name);
        NsProp("Icon", &Sample::icon);
    }
};

typedef Collection<Sample> SampleCollection;

////////////////////////////////////////////////////////////////////////////////////////////////////
struct Category: public BaseComponent
{
    Category(const char* name_) : name(name_)
    {
    }

    const SampleCollection* GetSamples() const
    {
        return &samples;
    }

    SampleCollection samples;
    String name;

    NS_IMPLEMENT_INLINE_REFLECTION(Category, BaseComponent, "Gallery.Category")
    {
        NsProp("Name", &Category::name);
        NsProp("Samples", &Category::GetSamples);
    }
};

typedef Collection<Category> CategoryCollection;

////////////////////////////////////////////////////////////////////////////////////////////////////
struct ColorItem: public Noesis::BaseComponent
{
    ColorItem(const char* name_, const Color& color_) : name(name_), color(color_)
    {
    }

    String name;
    Color color;

    NS_IMPLEMENT_INLINE_REFLECTION(ColorItem, BaseComponent, "Gallery.ColorItem")
    {
        NsProp("Name", &ColorItem::name);
        NsProp("Color", &ColorItem::color);
    }
};

typedef Noesis::Collection<ColorItem> ColorItemCollection;

////////////////////////////////////////////////////////////////////////////////////////////////////
struct ViewModel: public BaseComponent
{
    ViewModel(MainWindow* window_): window(window_),
        selectedSample(0), selectedThemeColor(0), selectedThemeAccent(0)
    {
        NS_ASSERT(window);

        {
            Ptr<Category> category = MakePtr<Category>("Basic Input");
            category->samples.Add(MakePtr<Sample>("Button", "A"));
            category->samples.Add(MakePtr<Sample>("RepeatButton", "B"));
            category->samples.Add(MakePtr<Sample>("ToggleButton", "C"));
            category->samples.Add(MakePtr<Sample>("CheckBox", "D"));
            category->samples.Add(MakePtr<Sample>("RadioButton", "E"));
            category->samples.Add(MakePtr<Sample>("Slider", "F"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Text");
            category->samples.Add(MakePtr<Sample>("TextBlock", "G"));
            category->samples.Add(MakePtr<Sample>("TextBox", "H"));
            category->samples.Add(MakePtr<Sample>("PasswordBox", "I"));
            category->samples.Add(MakePtr<Sample>("Hyperlink", "J"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Collections");
            category->samples.Add(MakePtr<Sample>("ItemsControl", "K"));
            category->samples.Add(MakePtr<Sample>("ComboBox", "L"));
            category->samples.Add(MakePtr<Sample>("ListBox", "M"));
            category->samples.Add(MakePtr<Sample>("ListView", "N"));
            category->samples.Add(MakePtr<Sample>("TreeView", "O"));
            category->samples.Add(MakePtr<Sample>("TabControl", "P"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Layout");
            category->samples.Add(MakePtr<Sample>("Canvas", "Q"));
            category->samples.Add(MakePtr<Sample>("StackPanel", "R"));
            category->samples.Add(MakePtr<Sample>("WrapPanel", "S"));
            category->samples.Add(MakePtr<Sample>("DockPanel", "T"));
            category->samples.Add(MakePtr<Sample>("Grid", "U"));
            category->samples.Add(MakePtr<Sample>("UniformGrid", "V"));
            category->samples.Add(MakePtr<Sample>("GroupBox", "W"));
            category->samples.Add(MakePtr<Sample>("Expander", "X"));
            category->samples.Add(MakePtr<Sample>("ScrollViewer", "Y"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Menus and Toolbars");
            category->samples.Add(MakePtr<Sample>("Menu", "Z"));
            category->samples.Add(MakePtr<Sample>("ContextMenu", "Z"));
            category->samples.Add(MakePtr<Sample>("ToolBar", "a"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Status and Info");
            category->samples.Add(MakePtr<Sample>("StatusBar", "b"));
            category->samples.Add(MakePtr<Sample>("ProgressBar", "c"));
            category->samples.Add(MakePtr<Sample>("ToolTip", "d"));
            categories.Add(category);
        }
        {
            Ptr<Category> category = MakePtr<Category>("Media");
            category->samples.Add(MakePtr<Sample>("Brushes", "e"));
            category->samples.Add(MakePtr<Sample>("Image", "f"));
            category->samples.Add(MakePtr<Sample>("Effects", "g"));
            category->samples.Add(MakePtr<Sample>("Blending", "w"));
            category->samples.Add(MakePtr<Sample>("Animation", "h"));
            categories.Add(category);
        }

        themeColors.Add(MakePtr<ColorItem>("Dark", Color(16, 20, 24)));
        themeColors.Add(MakePtr<ColorItem>("Light", Color(246, 247, 248)));

        themeAccents.Add(MakePtr<ColorItem>("Red", Color(222, 43, 16)));
        themeAccents.Add(MakePtr<ColorItem>("Green", Color(42, 166, 12)));
        themeAccents.Add(MakePtr<ColorItem>("Blue", Color(16, 152, 223)));
        themeAccents.Add(MakePtr<ColorItem>("Orange", Color(223, 153, 16)));
        themeAccents.Add(MakePtr<ColorItem>("Emerald", Color(16, 222, 98)));
        themeAccents.Add(MakePtr<ColorItem>("Purple", Color(137, 78, 237)));
        themeAccents.Add(MakePtr<ColorItem>("Crimson", Color(222, 16, 67)));
        themeAccents.Add(MakePtr<ColorItem>("Lime", Color(153, 223, 16)));
        themeAccents.Add(MakePtr<ColorItem>("Aqua", Color(16, 221, 223)));

        selectedThemeColor = themeColors.Get(1);
        selectedThemeAccent = themeAccents.Get(2);

        window->UpdateTheme(selectedThemeColor->name.Str(), selectedThemeAccent->name.Str());
    }

    const CategoryCollection* GetCategories() const
    {
        return &categories;
    }

    Sample* GetSelectedSample() const
    {
        return selectedSample;
    }
    void SetSelectedSample(Sample* value)
    {
        selectedSample = value;
        window->UpdateSample(selectedSample->name.Str());
    }

    const ColorItemCollection* GetThemeColors() const
    {
        return &themeColors;
    }

    const ColorItemCollection* GetThemeAccents() const
    {
        return &themeAccents;
    }

    ColorItem* GetSelectedThemeColor() const
    {
        return selectedThemeColor;
    }

    void SetSelectedThemeColor(ColorItem* value)
    {
        selectedThemeColor = value;
        window->UpdateTheme(selectedThemeColor->name.Str(), selectedThemeAccent->name.Str());
    }

    ColorItem* GetSelectedThemeAccent() const
    {
        return selectedThemeAccent;
    }

    void SetSelectedThemeAccent(ColorItem* value)
    {
        selectedThemeAccent = value;
        window->UpdateTheme(selectedThemeColor->name.Str(), selectedThemeAccent->name.Str());
    }

private:
    MainWindow* window;

    CategoryCollection categories;
    Sample* selectedSample;

    ColorItemCollection themeColors;
    ColorItemCollection themeAccents;
    ColorItem* selectedThemeColor;
    ColorItem* selectedThemeAccent;

    NS_IMPLEMENT_INLINE_REFLECTION(ViewModel, BaseComponent)
    {
        NsProp("Categories", &ViewModel::GetCategories);
        NsProp("SelectedSample", &ViewModel::GetSelectedSample, &ViewModel::SetSelectedSample);
        NsProp("ThemeColors", &ViewModel::GetThemeColors);
        NsProp("ThemeAccents", &ViewModel::GetThemeAccents);
        NsProp("SelectedThemeColor", &ViewModel::GetSelectedThemeColor, &ViewModel::SetSelectedThemeColor);
        NsProp("SelectedThemeAccent", &ViewModel::GetSelectedThemeAccent, &ViewModel::SetSelectedThemeAccent);
    }
};

}

////////////////////////////////////////////////////////////////////////////////////////////////////
MainWindow::MainWindow(): mLayoutRoot(0), mWindowContent(0),
    mSamplePanel(0), mSampleContainer1(0), mSampleContainer2(0),
    mSampleOffset(0), mSelectorBar(0), mSelectorExpanderButton(0)
{
    InitializeComponent();

    SetDataContext(MakePtr<ViewModel>(this));

    mWindowContent->SizeChanged() += [this](BaseComponent*, const SizeChangedEventArgs& e)
    {
        float scale;

        if (GetActualWidth() > GetActualHeight())
        {
            mSampleOffset->SetWidth(mSelectorBar->GetWidth());
            mSelectorExpanderButton->SetVisibility(Visibility_Hidden);
            mSelectorExpanderButton->SetIsChecked(false);
            mSelectorExpanderButton->SetIsChecked(true);

            scale = Max(1.0f, e.newSize.width / 1280.0f);
        }
        else
        {
            mSampleOffset->SetWidth(0.0f);
            mSelectorExpanderButton->SetIsChecked(true);
            mSelectorExpanderButton->SetIsChecked(false);
            mSelectorExpanderButton->SetVisibility(Visibility_Visible);

            scale = Max(1.0f, e.newSize.height / 720.0f);
        }

        ScaleTransform* rootScale = (ScaleTransform*)mLayoutRoot->GetLayoutTransform();
        rootScale->SetScaleX(scale);
        rootScale->SetScaleY(scale);
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void MainWindow::InitializeComponent()
{
    GUI::LoadComponent(this, "MainWindow.xaml");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool MainWindow::ConnectField(BaseComponent* object, const char* name)
{
    NS_CONNECT_FIELD(mLayoutRoot, "LayoutRoot");
    NS_CONNECT_FIELD(mWindowContent, "WindowContent");

    NS_CONNECT_FIELD(mSamplePanel, "SamplePanel");
    NS_CONNECT_FIELD(mSampleContainer1, "SampleContainer1");
    NS_CONNECT_FIELD(mSampleContainer2, "SampleContainer2");

    NS_CONNECT_FIELD(mSampleOffset, "SampleOffset");
    NS_CONNECT_FIELD(mSelectorBar, "SelectorBar");
    NS_CONNECT_FIELD(mSelectorExpanderButton, "SelectorExpanderButton");

    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void MainWindow::UpdateSample(const char* sampleName)
{
    String sampleUri(String::VarArgs(), "Samples/%s.xaml", sampleName);
    Ptr<FrameworkElement> sample = GUI::LoadXaml<FrameworkElement>(sampleUri.Str());

    if (sample != nullptr)
    {
        if (mSampleContainer1->GetContent() == nullptr)
        {
            mSampleContainer1->SetContent(sample);
        }
        else
        {
            mSampleContainer2->SetContent(sample);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void MainWindow::UpdateTheme(const char* colorName, const char* accentName)
{
    ResourceDictionary* theme = GetTheme(colorName, accentName);

    ResourceDictionary* resources = mSamplePanel->GetResources();
    resources->GetMergedDictionaries()->Clear();
    resources->GetMergedDictionaries()->Add(theme);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
ResourceDictionary* MainWindow::GetTheme(const char* color, const char* accent)
{
    String themeName = Uri::Pack("Noesis.GUI.Extensions", "Theme/").Str();
    themeName.AppendFormat("NoesisTheme.Brushes.%s%s.xaml", color, accent);

    ThemeDictionaries::Iterator it = mThemeDictionaries.Find(themeName);
    if (it != mThemeDictionaries.End())
    {
        return it->value;
    }

    Ptr<ResourceDictionary> theme = GUI::LoadXaml<ResourceDictionary>(themeName.Str());
    mThemeDictionaries.Insert(themeName, theme);
    return theme;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION_(Gallery::MainWindow, "Gallery.MainWindow")

NS_END_COLD_REGION
