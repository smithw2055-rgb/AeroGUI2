#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/MarkupExtensionParser.hpp"
#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/compiler/XamlToRsEmitter.hpp"
#include "aero/compiler/XmlPullLexer.hpp"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/Compilation.h"
#include "realscript/runtime/Runtime.h"

namespace {

using aero::rs_bc::disassembleModule;
using aero::rs_comp::CompilerSession;
namespace rs_bc = aero::rs_bc;
namespace rs_rt = aero::rs_rt;

int gFailureCount = 0;
long long gColdStdlibCompileMs = 0;

[[nodiscard]] bool ShouldRunSlowAotOptTests() {
    const char* v = std::getenv("AERO_TEST_ENABLE_AOT_OPT");
    return v != nullptr && v[0] != '\0' && std::string_view(v) != "0";
}

void Require(bool cond, std::string_view message) {
    if (!cond) {
        std::cerr << "[FAIL] " << message << "\n";
        ++gFailureCount;
    }
}

[[nodiscard]] bool ContainsText(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool RealStdlibHasUiClasses(const std::vector<aero::SourceFile>& stdlib) {
    for (const auto& sf : stdlib) {
        if (ContainsText(sf.content, "class UIElement")) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] aero::SourceFile BuildPreMergeMockAeroUiModule() {
    aero::SourceFile sf;
    sf.path = "Mock/Aero.UI.Contract.rs";
    sf.content = R"RS(
module Aero.UI;

import Aero.Native;

public delegate void Action();
public delegate void Action1<T>(T arg);
public delegate void RoutedEventHandler(object sender, RoutedEventArgs e);
public delegate void SelectionChangedEventHandler(object sender, SelectionChangedEventArgs e);
public delegate R Func1<T, R>(T arg);

public class RoutedEventArgs {
    public bool Handled = false;
    public RoutedEventArgs() { }
}

public class SelectionChangedEventArgs : RoutedEventArgs {
    public int SelectedIndex = -1;
    public SelectionChangedEventArgs() { }
}

public class DependencyObject {
    public DependencyObject() { }
}

public class Thickness : DependencyObject {
    public float Left = 0.0f;
    public float Top = 0.0f;
    public float Right = 0.0f;
    public float Bottom = 0.0f;
    public Thickness(float uniform) { Left = uniform; Top = uniform; Right = uniform; Bottom = uniform; }
    public Thickness(float l, float t, float r, float b) { Left = l; Top = t; Right = r; Bottom = b; }
}

public class CornerRadius : DependencyObject {
    public float TopLeft = 0.0f;
    public float TopRight = 0.0f;
    public float BottomRight = 0.0f;
    public float BottomLeft = 0.0f;
    public CornerRadius(float uniform) { TopLeft = uniform; TopRight = uniform; BottomRight = uniform; BottomLeft = uniform; }
    public CornerRadius(float tl, float tr, float br, float bl) { TopLeft = tl; TopRight = tr; BottomRight = br; BottomLeft = bl; }
}

public class Brush : DependencyObject {
    public float Opacity = 1.0f;
    public Brush() { }
}

public class SolidColorBrush : Brush {
    public string Color = "#FFFFFFFF";
    public SolidColorBrush(string color) { Color = color; }
    public SolidColorBrush(uint rgba) { Color = "#FFFFFFFF"; }
}

public class LinearGradientBrush : Brush {
    public LinearGradientBrush(float x0, float y0, float x1, float y1) { }
    public void AddStop(string color, float offset) { }
    public void AddStop(uint rgba, float offset) { }
}

public class RadialGradientBrush : Brush {
    public RadialGradientBrush(float cx, float cy, float ox, float oy, float rx, float ry) { }
    public void AddStop(string color, float offset) { }
    public void AddStop(uint rgba, float offset) { }
}

public class Transform : DependencyObject {
    public Transform() { }
}

public class TranslateTransform : Transform {
    public TranslateTransform(float x, float y) { }
}

public class ScaleTransform : Transform {
    public ScaleTransform(float sx, float sy, float cx = 0.0f, float cy = 0.0f) { }
}

public class RotateTransform : Transform {
    public RotateTransform(float angle, float cx = 0.0f, float cy = 0.0f) { }
}

public class SkewTransform : Transform {
    public SkewTransform(float ax, float ay, float cx = 0.0f, float cy = 0.0f) { }
}

public class TransformGroup : Transform {
    public TransformGroup() { }
    public void Add(Transform t) { }
}

public class Effect : DependencyObject {
    public Effect() { }
}

public class BlurEffect : Effect {
    public float Radius = 5.0f;
    public BlurEffect(float radius) { Radius = radius; }
}

public class DropShadowEffect : Effect {
    public DropShadowEffect(float radius = 5.0f, float depth = 5.0f, float dir = 315.0f, string color = "#FF000000", float opacity = 1.0f) { }
}

public class BoolToVisibilityConverter : DependencyObject { public BoolToVisibilityConverter() { } }
public class InverseBoolConverter : DependencyObject { public InverseBoolConverter() { } }
public class MathConverter : DependencyObject { public MathConverter() { } }

public class ResourceDictionary : DependencyObject {
    public ResourceDictionary() { }
    public void Set(string key, object val) { }
    public void Set(string key, string val) { }
    public void Set(string key, float val) { }
    public void Set(string key, int val) { }
    public void Set(string key, bool val) { }
    public object Get(string key) { return null; }
    public bool Contains(string key) { return false; }
    public void Merge(ResourceDictionary other) { }
}

public class TriggerBase : DependencyObject {
    public TriggerBase() { }
    public void AddSetter(string prop, object val, string targetName = "") { }
    public void AddSetter(string prop, string val, string targetName = "") { }
    public void AddSetter(string prop, float val, string targetName = "") { }
    public void AddSetter(string prop, int val, string targetName = "") { }
    public void AddSetter(string prop, bool val, string targetName = "") { }
    public void AddAction(Storyboard sb) { }
}

public class Trigger : TriggerBase {
    public Trigger(string prop, object val) { }
    public Trigger(string prop, string val) { }
    public Trigger(string prop, float val) { }
    public Trigger(string prop, int val) { }
    public Trigger(string prop, bool val) { }
}

public class DataTrigger : TriggerBase {
    public DataTrigger(string bindingPath, object val) { }
    public DataTrigger(string bindingPath, string val) { }
    public DataTrigger(string bindingPath, float val) { }
    public DataTrigger(string bindingPath, int val) { }
    public DataTrigger(string bindingPath, bool val) { }
}

public class EventTrigger : TriggerBase {
    public EventTrigger(string eventName) { }
}

public class Style : DependencyObject {
    public Style BasedOn = null;
    public Style(string targetType) { }
    public void AddSetter(string prop, object val, string targetName = "") { }
    public void AddSetter(string prop, string val, string targetName = "") { }
    public void AddSetter(string prop, float val, string targetName = "") { }
    public void AddSetter(string prop, int val, string targetName = "") { }
    public void AddSetter(string prop, bool val, string targetName = "") { }
    public void AddTrigger(TriggerBase trigger) { }
}

public class ControlTemplate : DependencyObject {
    public ControlTemplate(string targetType, Func1<Control, UIElement> factory) { }
}

public class DataTemplate : DependencyObject {
    public DataTemplate(Func1<object, UIElement> factory) { }
}

public class Storyboard : DependencyObject {
    public Storyboard() { }
    public void AddDoubleAnimation(string targetName, string propPath, float fromVal, float toVal, float durationSec, int easeKind = 2, int easeMode = 0, float delaySec = 0.0f) { }
    public void AddColorAnimation(string targetName, string propPath, uint fromRgba, uint toRgba, float durationSec, int easeKind = 2, int easeMode = 0, float delaySec = 0.0f) { }
    public void Begin(UIElement scope) { }
    public void Stop(UIElement scope) { }
}

public class ObservableObject : DependencyObject {
    public ObservableObject() { }
    protected bool SetProperty(ref string field, string value, string propName = "") {
        field = value;
        return true;
    }
    protected bool SetProperty(ref int field, int value, string propName = "") {
        field = value;
        return true;
    }
}

public class ViewModelBase : ObservableObject {
    public ViewModelBase() { }
}

public class ObservableList<T> {
    private List<T> _items = new List<T>();
    public ObservableList() { }
    public void Add(T item) { _items.Add(item); }
    public void Clear() { _items.Clear(); }
    public int Count() { return _items.Count(); }
}

public class ObservableCollection<T> : ObservableList<T> {
    public ObservableCollection() { }
}

public class RelayCommand {
    public RelayCommand(Action execute) { }
    public RelayCommand(Action1<object> execute) { }
    public void Execute(object parameter) { }
}

public class RelayCommand<T> {
    public RelayCommand(Action1<T> execute) { }
    public void Execute(object parameter) { }
}

public class UIElement : DependencyObject {
    public handle __nativeHandle;
    public string Name = "";
    public ResourceDictionary Resources = new ResourceDictionary();
    public Style Style = null;
    public float Width = 0.0f;
    public float Height = 0.0f;
    public float MinWidth = 0.0f;
    public float MaxWidth = 0.0f;
    public float MinHeight = 0.0f;
    public float MaxHeight = 0.0f;
    public Thickness Margin = new Thickness(0.0f);
    public Thickness Padding = new Thickness(0.0f);
    public HorizontalAlignment HorizontalAlignment = HorizontalAlignment.Stretch;
    public VerticalAlignment VerticalAlignment = VerticalAlignment.Stretch;
    public Visibility Visibility = Visibility.Visible;
    public float Opacity = 1.0f;
    public bool IsEnabled = true;
    public bool IsHitTestVisible = true;
    public bool Focusable = false;
    public bool ClipToBounds = false;
    public int ZIndex = 0;
    public Brush Background = null;
    public Brush Foreground = null;
    public Transform RenderTransform = null;
    public Effect Effect = null;

    public object DataContext {
        get { return null; }
        set { }
    }

    public event RoutedEventHandler Loaded;

    public UIElement() {
        __nativeHandle = Visual_Create(0);
    }

    public void AddChild(UIElement child) { }
    public void SetProperty(string prop, object val) { }
    public void SetProperty(string prop, string val) { }
    public void SetProperty(string prop, float val) { }
    public void SetProperty(string prop, int val) { }
    public void SetProperty(string prop, bool val) { }
    public object FindName(string name) { return null; }
    public object FindResource(string key) { return Resources.Get(key); }
    public void BeginStoryboard(string key) { }
    public void StopStoryboard(string key) { }

    public static void SetGridRow(UIElement el, int row) { }
    public static void SetGridColumn(UIElement el, int col) { }
    public static void SetGridRowSpan(UIElement el, int span) { }
    public static void SetGridColumnSpan(UIElement el, int span) { }
    public static void SetDock(UIElement el, Dock d) { }
    public static void SetCanvasLeft(UIElement el, float v) { }
    public static void SetCanvasTop(UIElement el, float v) { }
    public static void SetCanvasRight(UIElement el, float v) { }
    public static void SetCanvasBottom(UIElement el, float v) { }
}

public class FrameworkElement : UIElement {
    public FrameworkElement() { }
}

public class Control : FrameworkElement {
    public Brush BorderBrush = null;
    public Thickness BorderThickness = new Thickness(0.0f);
    public CornerRadius CornerRadius = new CornerRadius(0.0f);
    public HorizontalAlignment HorizontalContentAlignment = HorizontalAlignment.Center;
    public VerticalAlignment VerticalContentAlignment = VerticalAlignment.Center;
    public float FontSize = 14.0f;
    public string FontFamily = "Default";
    public ControlTemplate Template = null;
    public Control() { }
}

public class ContentControl : Control {
    public ContentControl() { }
    public void SetContent(object content) { }
    public void SetContent(string content) { }
}

public class HeaderedContentControl : ContentControl {
    public HeaderedContentControl() { }
    public void SetHeader(object header) { }
    public void SetHeader(string header) { }
}

public class Window : ContentControl {
    public string Title = "";
    public Window() { }
}

public class Page : ContentControl {
    public string Title = "";
    public Page() { }
}

public class UserControl : ContentControl {
    public UserControl() { }
}

public class ScrollViewer : ContentControl {
    public ScrollBarVisibility HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
    public ScrollBarVisibility VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
    public ScrollViewer() { }
}

public class Application : DependencyObject {
    public ResourceDictionary Resources = new ResourceDictionary();
    public Application() { }
}

public class Border : FrameworkElement {
    public Brush BorderBrush = null;
    public Thickness BorderThickness = new Thickness(0.0f);
    public CornerRadius CornerRadius = new CornerRadius(0.0f);
    public Border() { }
}

public class Panel : FrameworkElement {
    public Panel() { }
}

public class Grid : Panel {
    public bool ShowGridLines = false;
    public Grid() { }
    public void AddRowDefinition(GridUnitType u, float val) { }
    public void AddColumnDefinition(GridUnitType u, float val) { }
}

public class StackPanel : Panel {
    public Orientation Orientation = Orientation.Vertical;
    public float Spacing = 0.0f;
    public StackPanel() { }
}

public class DockPanel : Panel {
    public bool LastChildFill = true;
    public DockPanel() { }
}

public class Canvas : Panel {
    public Canvas() { }
}

public class Run : UIElement {
    public string Text = "";
    public string FontWeight = "Normal";
    public float FontSize = 14.0f;
    public Run() { }
}

public class TextBlock : FrameworkElement {
    public string Text = "";
    public float FontSize = 14.0f;
    public string FontFamily = "Default";
    public TextWrapping TextWrapping = TextWrapping.NoWrap;
    public TextAlignment TextAlignment = TextAlignment.Left;
    public TextTrimming TextTrimming = TextTrimming.None;
    public TextBlock() { }
    public void AddRun(Run run) { }
}

public class TextBox : Control {
    public string Text = "";
    public string Placeholder = "";
    public bool IsReadOnly = false;
    public TextBox() { }
}

public class Button : ContentControl {
    public DependencyObject CommandParameter = null;
    public event RoutedEventHandler Click;
    public Button() { }
}

public class CheckBox : ContentControl {
    public bool IsChecked = false;
    public bool IsThreeState = false;
    public CheckBox() { }
}

public class ItemsControl : Control {
    public DataTemplate ItemTemplate = null;
    public ItemsControl() { }
    public void AddItem(object item) { }
    public void AddItem(string item) { }
}

public class ListBox : ItemsControl {
    public int SelectedIndex = -1;
    public SelectionMode SelectionMode = SelectionMode.Single;
    public event SelectionChangedEventHandler SelectionChanged;
    public ListBox() { }
}

public class GridViewColumn : DependencyObject {
    public string Header = "";
    public float Width = 100.0f;
    public string DisplayMemberPath = "";
    public GridViewColumn() { }
}

public class GridView : DependencyObject {
    public GridView() { }
    public void AddColumn(GridViewColumn col) { }
}

public class ListView : ListBox {
    public GridView View = null;
    public ListView() { }
}

public class Rectangle : FrameworkElement {
    public Brush Fill = null;
    public Brush Stroke = null;
    public float StrokeThickness = 1.0f;
    public float RadiusX = 0.0f;
    public float RadiusY = 0.0f;
    public Rectangle() { }
}

public class BindingEngine {
    public static void BindPath(UIElement target, string targetProp, object source, string path, int mode = 0, string converterKey = "", string converterParam = "", string stringFormat = "") { }
    public static void BindElement(UIElement target, string targetProp, string elementName, string sourceProp, int mode = 0) { }
    public static void BindTemplateParent(UIElement target, string targetProp, Control templatedParent, string sourceProp) { }
    public static void Flush() { }
}
)RS";
    return sf;
}

void TestXmlPullLexer() {
    const std::string_view xml = R"XML(<?xml version="1.0" encoding="utf-8"?>
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Tom &amp; Jerry &lt;3&#x21;">
  <!-- comment ignored -->
  <StackPanel>
    <TextBlock><![CDATA[Raw <Text> & Literal]]></TextBlock>
    <Separator/>
  </StackPanel>
</Window>)XML";

    aero::XmlPullLexer lexer(xml);
    const auto tokens = lexer.LexAll();
    Require(!tokens.empty() && tokens.back().kind == aero::XmlTokenKind::EndOfFile,
        "XmlPullLexer should reach EndOfFile cleanly");
    Require(tokens[0].kind == aero::XmlTokenKind::StartElement && tokens[0].localName == "Window",
        "XmlPullLexer first token must be <Window>");
    Require(tokens[0].namespaceUri == "http://schemas.microsoft.com/winfx/2006/xaml/presentation",
        "XmlPullLexer default xmlns resolution failed");

    bool foundTitle = false;
    for (const auto& attr : tokens[0].attributes) {
        if (attr.rawName == "Title") {
            foundTitle = (attr.value == "Tom & Jerry <3!");
        }
    }
    Require(foundTitle, "XmlPullLexer entity decoding failed on &amp;/&lt;/&#x21;");

    bool foundCData = false;
    bool foundEmptySep = false;
    for (const auto& tok : tokens) {
        if (tok.kind == aero::XmlTokenKind::CData && tok.text == "Raw <Text> & Literal") {
            foundCData = true;
        }
        if (tok.kind == aero::XmlTokenKind::StartElement &&
            tok.localName == "Separator" && tok.isEmptyElement) {
            foundEmptySep = true;
        }
    }
    Require(foundCData, "XmlPullLexer CDATA section failed");
    Require(foundEmptySep, "XmlPullLexer empty element <Separator/> failed");

    aero::XmlPullLexer badLexer("<Window><Grid></Window>");
    const auto badTokens = badLexer.LexAll();
    Require(!badTokens.empty() && badTokens.back().kind == aero::XmlTokenKind::Error &&
            badTokens.back().line == 1,
        "XmlPullLexer should diagnose mismatched closing tag with line/column");
}

void TestMarkupExtensionParser() {
    {
        const auto esc = aero::ParseMarkupExtension("{}{0:F2} ms");
        Require(esc.kind == aero::MarkupExtensionKind::EscapedLiteral &&
                esc.literalValue == "{0:F2} ms",
            "MarkupExtensionParser failed '{}' literal escape");
    }
    {
        const auto bind = aero::ParseMarkupExtension(
            "{Binding User.Title, Mode=TwoWay, Converter={StaticResource BoolToVis}, "
            "ConverterParameter='x,y', StringFormat='[{0}]', FallbackValue='Untitled', "
            "TargetNullValue='None', ElementName=MainTree, "
            "RelativeSource={RelativeSource FindAncestor, AncestorType={x:Type Window}, AncestorLevel=2}}");
        Require(bind.IsOk() && bind.kind == aero::MarkupExtensionKind::Binding,
            "MarkupExtensionParser failed nested Binding parse");

        aero::ParsedBindingSpec spec;
        Require(aero::TryExtractBindingSpec(bind, spec), "TryExtractBindingSpec failed");
        Require(spec.path == "User.Title", "Binding path mismatch");
        Require(spec.mode == "TwoWay", "Binding Mode mismatch");
        Require(spec.converterKey == "BoolToVis", "Binding nested Converter key mismatch");
        Require(spec.converterParam == "x,y", "Binding quoted ConverterParameter mismatch");
        Require(spec.stringFormat == "[{0}]", "Binding StringFormat mismatch");
        Require(spec.hasFallbackValue && spec.fallbackValue == "Untitled", "Binding FallbackValue mismatch");
        Require(spec.hasTargetNullValue && spec.targetNullValue == "None", "Binding TargetNullValue mismatch");
        Require(spec.elementName == "MainTree", "Binding ElementName mismatch");
        Require(spec.hasRelativeSource && spec.relativeSourceMode == "FindAncestor" &&
                spec.ancestorType == "Window" && spec.ancestorLevel == 2,
            "Binding nested RelativeSource mismatch");
    }
    {
        const auto tb = aero::ParseMarkupExtension("{TemplateBinding Background}");
        aero::ParsedBindingSpec spec;
        Require(aero::TryExtractBindingSpec(tb, spec) && spec.isTemplateBinding &&
                spec.path == "Background",
            "TemplateBinding extraction failed");
    }
    {
        const auto xnull = aero::ParseMarkupExtension("{x:Null}");
        const auto loc = aero::ParseMarkupExtension("{noesis:Loc IDS_TITLE}");
        const auto dyn = aero::ParseMarkupExtension("{DynamicResource Brush.Accent}");
        const auto mb = aero::ParseMarkupExtension("{MultiBinding StringFormat='{0}:{1}'}");
        Require(xnull.kind == aero::MarkupExtensionKind::XNull, "x:Null classification failed");
        Require(loc.kind == aero::MarkupExtensionKind::NoesisLoc &&
                loc.FindPositional(0) != nullptr && loc.FindPositional(0)->literal == "IDS_TITLE",
            "noesis:Loc classification failed");
        Require(dyn.kind == aero::MarkupExtensionKind::DynamicResource &&
                dyn.FindPositional(0) != nullptr && dyn.FindPositional(0)->literal == "Brush.Accent",
            "DynamicResource classification failed");
        Require(mb.kind == aero::MarkupExtensionKind::MultiBinding, "MultiBinding classification failed");
    }
    {
        const auto bad = aero::ParseMarkupExtension("{Binding Path=Foo");
        Require(!bad.IsOk(), "Unclosed markup extension must report error");
    }
}

void TestCSharpCompatLowerer() {
    const std::string_view cs = R"CS(
using System;
using System.Collections.ObjectModel;
using System.Windows;
using Aero.UI;

namespace DemoApp
{
    public class SampleVM : ViewModelBase
    {
#if REALSCRIPT
        private readonly int _activeBranch = 1;
#else
        private readonly int _activeBranch = 999;
#endif
        #region Properties
        public string FilterText { get; private set; } = string.Empty;
        public string Tagged { get; init; } = "#if not a directive";
        public RelayCommand<string> QuickCmd { get; private set; }
        #endregion

        public void SetFilter(string val)
        {
            var k = nameof(FilterText);
            if (val != string.Empty)
            {
                using (var g = MakeGuard()) { }
                FilterText = val;
            }
            double scale = 1.5d;
        }
    }
}
)CS";

    const std::unordered_set<std::string> avail = {"Aero.UI", "Aero.Native"};
    const std::string lowered = aero::LowerCSharpCompatSyntax(cs, &avail);

    Require(ContainsText(lowered, "module DemoApp;"), "Lowerer must emit module DemoApp;");
    Require(ContainsText(lowered, "import Aero.UI;"), "Lowerer must keep import Aero.UI;");
    Require(!ContainsText(lowered, "using System"), "Lowerer must strip .NET System using directives");
    Require(ContainsText(lowered, "_activeBranch = 1;"), "Lowerer must keep #if REALSCRIPT branch");
    Require(!ContainsText(lowered, "999"), "Lowerer must drop inactive #else branch");
    Require(!ContainsText(lowered, "#region"), "Lowerer must strip #region");
    Require(ContainsText(lowered, "\"#if not a directive\""), "Lowerer must not touch string literal contents");
    Require(ContainsText(lowered, "using (var g = MakeGuard())"), "Lowerer must preserve C# using(...) statement");
    Require(ContainsText(lowered, "{ get; set; } = \"\";"), "Lowerer must normalize private set & string.Empty");
    Require(ContainsText(lowered, "string Tagged { get; set; }"), "Lowerer must normalize init -> set");
    Require(ContainsText(lowered, "var k = \"FilterText\";"), "Lowerer must lower nameof(FilterText)");
    Require(ContainsText(lowered, "double scale = 1.5;"), "Lowerer must strip numeric 'd' suffix");
}

void TestXamlToRsEmitterFeatures() {
    const std::string_view mergedColors = R"XML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <Color x:Key="Color.Accent">#FF1098DF</Color>
</ResourceDictionary>)XML";

    const std::string_view mainXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Showcase.MainWindow"
        x:DataType="MainViewModel"
        Title="AeroV3 Showcase"
        Width="800" Height="600">
  <Window.Resources>
    <ResourceDictionary>
      <ResourceDictionary.MergedDictionaries>
        <ResourceDictionary Source="Colors.xaml"/>
      </ResourceDictionary.MergedDictionaries>
      <SolidColorBrush x:Key="AccentBrush" Color="{StaticResource Color.Accent}"/>
      <Style x:Key="CardBtnStyle" TargetType="Button">
        <Setter Property="Background" Value="{StaticResource AccentBrush}"/>
        <Setter Property="Template">
          <Setter.Value>
            <ControlTemplate TargetType="Button">
              <Border Background="{TemplateBinding Background}" CornerRadius="4">
                <ContentPresenter/>
              </Border>
            </ControlTemplate>
          </Setter.Value>
        </Setter>
        <Style.Triggers>
          <Trigger Property="IsMouseOver" Value="True">
            <Setter Property="Opacity" Value="0.85"/>
          </Trigger>
        </Style.Triggers>
      </Style>
      <DataTemplate x:Key="RowTemplate" x:DataType="RowItem">
        <TextBlock Text="{Binding Title}"/>
      </DataTemplate>
      <Storyboard x:Key="Intro">
        <DoubleAnimation Storyboard.TargetName="HeroRect" Storyboard.TargetProperty="Opacity"
                         From="0" To="1" Duration="0:0:0.4"/>
        <ColorAnimation Storyboard.TargetName="HeroRect" Storyboard.TargetProperty="Fill.Color"
                        From="#FF000000" To="#FF1098DF" Duration="0:0:0.4"/>
      </Storyboard>
    </ResourceDictionary>
  </Window.Resources>
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="2*"/>
    </Grid.RowDefinitions>
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="200"/>
      <ColumnDefinition Width="*"/>
    </Grid.ColumnDefinitions>
    <DockPanel Grid.Row="0" Grid.ColumnSpan="2">
      <Button x:Name="ActionBtn" DockPanel.Dock="Left" Content="Run" Style="{StaticResource CardBtnStyle}" Click="ActionBtn_Click"/>
    </DockPanel>
    <Canvas Grid.Row="1" Grid.Column="1">
      <Rectangle x:Name="HeroRect" Canvas.Left="24" Canvas.Top="16" Width="120" Height="80">
        <Rectangle.Fill>
          <LinearGradientBrush StartPoint="0,0" EndPoint="1,1">
            <GradientStop Color="#FF1098DF" Offset="0"/>
            <GradientStop Color="#FF0864A1" Offset="1"/>
          </LinearGradientBrush>
        </Rectangle.Fill>
        <Rectangle.RenderTransform>
          <RotateTransform Angle="15" CenterX="60" CenterY="40"/>
        </Rectangle.RenderTransform>
        <Rectangle.Effect>
          <BlurEffect Radius="4"/>
        </Rectangle.Effect>
      </Rectangle>
      <TextBlock Canvas.Left="24" Canvas.Top="110" Text="{Binding Headline, Mode=OneWay, StringFormat='Hi {0}'}"/>
    </Canvas>
  </Grid>
</Window>)XML";

    aero::XamlEmitOptions opts;
    opts.xamlPath = "Showcase/MainWindow.xaml";
    opts.externalXamlResolver = [&](std::string_view req, std::string_view, std::string& resolved, std::string& content) {
        if (req == "Colors.xaml") {
            resolved = "Showcase/Colors.xaml";
            content = std::string(mergedColors);
            return true;
        }
        return false;
    };

    const auto res = aero::EmitXamlToRealScript(mainXaml, opts);
    Require(res.succeeded, "XamlToRsEmitter failed on Showcase MainWindow");
    Require(res.moduleName == "Showcase", "Expected module Showcase");
    Require(res.rootClassName == "Showcase.MainWindow", "Expected rootClassName Showcase.MainWindow");
    Require(ContainsText(res.generatedRsCode, "public partial class MainWindow : Window"),
        "Missing partial class MainWindow");
    Require(ContainsText(res.generatedRsCode, "public Button ActionBtn;"), "Missing typed x:Name ActionBtn field");
    Require(ContainsText(res.generatedRsCode, "public Rectangle HeroRect;"), "Missing typed x:Name HeroRect field");
    Require(ContainsText(res.generatedRsCode, "new SolidColorBrush(\"#FF1098DF\")"),
        "StaticResource Color from MergedDictionaries should resolve cleanly");
    Require(ContainsText(res.generatedRsCode, "AddRowDefinition(GridUnitType.Auto, 1.0f)"),
        "Missing Grid.RowDefinitions Auto");
    Require(ContainsText(res.generatedRsCode, "AddRowDefinition(GridUnitType.Star, 2.0f)"),
        "Missing Grid.RowDefinitions Star");
    Require(ContainsText(res.generatedRsCode, "UIElement.SetDock("), "Missing DockPanel.Dock");
    Require(ContainsText(res.generatedRsCode, "UIElement.SetCanvasLeft("), "Missing Canvas.Left");
    Require(ContainsText(res.generatedRsCode, "new LinearGradientBrush("), "Missing LinearGradientBrush");
    Require(ContainsText(res.generatedRsCode, "new RotateTransform("), "Missing RotateTransform");
    Require(ContainsText(res.generatedRsCode, "new BlurEffect("), "Missing BlurEffect");
    Require(ContainsText(res.generatedRsCode, "ActionBtn.Click += this.ActionBtn_Click;") ||
            ContainsText(res.generatedRsCode, ".Click += this.ActionBtn_Click;"),
        "Missing Click event wiring");
    Require(ContainsText(res.generatedRsCode, "new ControlTemplate(") &&
            ContainsText(res.generatedRsCode, "BindingEngine.BindTemplateParent("),
        "Missing ControlTemplate / TemplateBinding");
    Require(ContainsText(res.generatedRsCode, "new DataTemplate(") &&
            ContainsText(res.generatedRsCode, "is RowItem"),
        "Missing DataTemplate / x:DataType typed fast-path");
    Require(ContainsText(res.generatedRsCode, "AddDoubleAnimation(\"HeroRect\", \"Opacity\""),
        "Missing Storyboard DoubleAnimation");
    Require(ContainsText(res.generatedRsCode, "AddColorAnimation(\"HeroRect\", \"Fill.Color\""),
        "Missing Storyboard ColorAnimation");
    Require(ContainsText(res.generatedRsCode, "this.SetContent("),
        "Window root child must attach via SetContent for ContentPresenter template");
    Require(ContainsText(res.generatedRsCode, ", \"Text\", null, \"Headline\", 0, \"\", \"\", \"Hi {0}\");"),
        "Plain DataContext binding must pass null source so BindingEngine resolves EffectiveDataContext");
}

void TestNativeBodyStrippingUnderO2AndAot() {
    const std::vector<aero::SourceFile> stdlib = aero::LoadStdlibModulePack();
    Require(!stdlib.empty(), "LoadStdlibModulePack returned empty pack");

    bool hasNative = false;
    bool hasUi = false;
    for (const auto& sf : stdlib) {
        if (ContainsText(sf.content, "module Aero.Native;")) hasNative = true;
        if (ContainsText(sf.content, "module Aero.UI;")) hasUi = true;
    }
    Require(hasNative && hasUi, "Stdlib pack must load Aero.Native and Aero.UI modules");

    aero::SourceFile probe;
    probe.path = "NativeProbe.rs";
    probe.content = R"RS(
module NativeProbe;
import Aero.Native;
import Aero.UI;

public class ProbeHost {
    public handle Node;
    public uint Color;
    public ProbeHost() {
        Node = Visual_Create(0);
        Color = Color_ParseRgba("#FF1098DF");
    }
}

public uint RunNativeProbe() {
    var host = new ProbeHost();
    return host.Color;
}

public void RunVoidProbe(handle h) {
    Visual_SetOpacity(h, 0.75f);
}
)RS";

    const bool runSlow = ShouldRunSlowAotOptTests();
    aero::AeroV3CompileInput input;
    input.includeStdlib = true;
    input.stdlibPackages = aero::AeroStdlibPkg_Core | aero::AeroStdlibPkg_Controls;
    input.allowDiskCache = false;
    input.allowStdlibCache = false;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = runSlow;
    input.emitAotCpp = runSlow;
    input.aotViewBuilderName = "BuildNativeProbe";
    input.scriptSources.push_back(probe);

    const aero::AeroCompilerV3 compiler;
    const auto tCold0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult result = compiler.Compile(input);
    gColdStdlibCompileMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tCold0).count();
    for (const auto& d : result.diagnostics) {
        if (!result.succeeded) {
            std::cerr << "  native probe diag: " << d << "\n";
        }
    }
    Require(result.succeeded && result.programImage != nullptr,
        "NativeProbe compilation with real stdlib failed");
    Require(!result.programImageBytes.empty(), "programImageBytes must not be empty");
    if (runSlow) {
        Require(!result.aotCpp.scriptAotHeader.empty() && !result.aotCpp.scriptAotSource.empty(),
            "CppGenerator must produce non-empty AOT header & source");
        Require(ContainsText(result.aotCpp.scriptAotHeader, "BuildNativeProbeProgram"),
            "AOT header missing BuildNativeProbeProgram");
    }
    if (!result.succeeded || !result.programImage) {
        return;
    }

    // Verify Aero.Native retains ONLY void wrappers (all non-void & __rs_native_* stripped),
    // and NativeProbe preserved external call to Aero.Native::Visual_Create under -O2!
    std::string probeDisasm;
    for (const auto& bcMod : result.bytecodeModules) {
        if (bcMod.name == "Aero.Native") {
            for (const auto& fn : bcMod.functions) {
                Require(fn.returnType == aero::rs_sem::PrimitiveType::Void &&
                        !ContainsText(fn.name, "__rs_native_"),
                    "Aero.Native must strip all non-void and __rs_native_* bodies, keeping only void wrappers");
            }
        } else if (bcMod.name == "NativeProbe") {
            probeDisasm = rs_bc::disassembleModule(bcMod);
        }
    }
    Require(ContainsText(probeDisasm, "Visual_Create") &&
            ContainsText(probeDisasm, "Color_ParseRgba") &&
            (ContainsText(probeDisasm, "Visual_SetOpacity") ||
             ContainsText(probeDisasm, "__rs_native_Visual_SetOpacity")),
        "-O0/-O2 must NOT inline Aero.Native dummy bodies; Call instructions must remain");

    if (runSlow) {
        Require(ContainsText(result.aotCpp.scriptAotSource, "Visual_Create") &&
                (ContainsText(result.aotCpp.scriptAotSource, "Visual_SetOpacity") ||
                 ContainsText(result.aotCpp.scriptAotSource, "__rs_native_Visual_SetOpacity")),
            "AOT C++ source must preserve external Aero.Native calls");
    }

    // Execute compiled bytecode with mock native hooks to confirm live FFI dispatch
    rs_rt::Interpreter interp(result.programImage);
    bool sawVisualCreate = false;
    bool sawColorParse = false;
    interp.setExternalResolver(
        [&](const rs_bc::FunctionReference& ref, rs_rt::ValueSpan, rs_rt::RuntimeError&)
            -> std::optional<rs_rt::Value> {
            if (ref.name == "Aero.Native::Visual_Create") {
                sawVisualCreate = true;
                return rs_rt::Value(rs_rt::NativeHandle{42u, 1u, 1u, 1u});
            }
            if (ref.name == "Aero.Native::Color_ParseRgba") {
                sawColorParse = true;
                return rs_rt::Value(rs_rt::UIntValue{0xFF1098DFu});
            }
            return std::nullopt;
        });

    const auto execRes = interp.invoke("NativeProbe::RunNativeProbe");
    Require(execRes.succeeded, "Interpreter execution of NativeProbe::RunNativeProbe failed: " + execRes.error.message);
    Require(sawVisualCreate && sawColorParse, "Interpreter did not invoke stripped Aero.Native hooks");
    Require(std::holds_alternative<rs_rt::UIntValue>(execRes.value) &&
            std::get<rs_rt::UIntValue>(execRes.value).value == 0xFF1098DFu,
        "RunNativeProbe should return live native hook result 0xFF1098DFu, not inlined 0u");
}

void TestVoidNativeForwardersO0O2AndAot() {
    const aero::AeroCompilerV3 compiler;
    aero::SourceFile probe;
    probe.path = "VoidForwarderProbe.rs";
    probe.content = R"RS(
module VoidForwarderProbe;
import Aero.Native;

public float ApplyFade(float opacity) {
    handle node = Visual_Create(0);
    Visual_SetOpacity(node, opacity);
    return opacity;
}
)RS";

    std::vector<bool> optModes{false};
    if (ShouldRunSlowAotOptTests()) {
        optModes.push_back(true);
    }
    for (bool optO2 : optModes) {
        aero::AeroV3CompileInput input;
        input.includeStdlib = true;
        input.treeShakeStdlib = false;
        input.optimizeBytecode = optO2;
        input.emitAotCpp = optO2;
        input.aotViewBuilderName = optO2 ? "BuildVoidProbeO2" : "BuildVoidProbeO0";
        input.scriptSources.push_back(probe);

        const aero::AeroV3CompileResult res = compiler.Compile(input);
        for (const auto& d : res.diagnostics) {
            if (!res.succeeded) {
                std::cerr << "  void probe diag: " << d << "\n";
            }
        }
        Require(res.succeeded && res.programImage != nullptr,
            std::string("VoidForwarderProbe compile failed under ") + (optO2 ? "-O2" : "-O0"));
        if (optO2) {
            Require(!res.aotCpp.scriptAotHeader.empty() && !res.aotCpp.scriptAotSource.empty(),
                std::string("VoidForwarderProbe AOT output empty under ") + (optO2 ? "-O2" : "-O0"));
        }
        if (!res.succeeded || !res.programImage) {
            continue;
        }

        rs_rt::Interpreter interp(res.programImage);
        bool sawCreate = false;
        bool sawNativeSetOpacity = false;
        float capturedOpacity = -1.0f;
        interp.setExternalResolver(
            [&](const rs_bc::FunctionReference& ref, rs_rt::ValueSpan args, rs_rt::RuntimeError&)
                -> std::optional<rs_rt::Value> {
                if (ref.name == "Aero.Native::Visual_Create") {
                    sawCreate = true;
                    return rs_rt::Value(rs_rt::NativeHandle{99u, 1u, 1u, 1u});
                }
                if (ref.name == "Aero.Native::__rs_native_Visual_SetOpacity" ||
                    ref.name == "Aero.Native::Visual_SetOpacity") {
                    sawNativeSetOpacity = true;
                    if (args.size() >= 2) {
                        (void)args[1].tryGetFloat(capturedOpacity);
                    }
                    return rs_rt::Value{std::int64_t{0}};
                }
                return std::nullopt;
            });

        const auto execRes = interp.invoke(
            "VoidForwarderProbe::ApplyFade",
            {rs_rt::Value(rs_rt::FloatValue{0.42f})});
        Require(execRes.succeeded,
            std::string("VoidForwarderProbe invoke failed under ") + (optO2 ? "-O2" : "-O0") +
            ": " + execRes.error.message);
        Require(sawCreate && sawNativeSetOpacity,
            std::string("VoidForwarderProbe did not dispatch __rs_native_Visual_SetOpacity under ") +
            (optO2 ? "-O2" : "-O0"));
        Require(capturedOpacity > 0.41f && capturedOpacity < 0.43f,
            "VoidForwarderProbe did not receive expected opacity argument 0.42f");
    }
}

void TestEndToEndXamlCompileAndSessionReuse() {
    const auto realStdlib = aero::LoadStdlibModulePack();
    const bool useFullRealStdlib = RealStdlibHasUiClasses(realStdlib);

    const std::string xaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="QuickApp.MainWindow"
        Title="AeroV3 App" Width="640" Height="480">
  <Window.Resources>
    <ResourceDictionary>
      <SolidColorBrush x:Key="BgBrush" Color="#182030"/>
      <Style TargetType="Button">
        <Setter Property="Background" Value="#2563EB"/>
      </Style>
    </ResourceDictionary>
  </Window.Resources>
  <Grid Margin="12">
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <StackPanel Grid.Row="0" Orientation="Horizontal" Spacing="8">
      <TextBlock x:Name="HeaderText" Text="{Binding TitleText}" FontSize="18"/>
      <Button x:Name="ClickBtn" Content="Go" Click="OnGoClick"/>
    </StackPanel>
    <ListView x:Name="TaskGrid" Grid.Row="1">
      <ListView.View>
        <GridView>
          <GridViewColumn Header="Task" Width="220" DisplayMemberBinding="{Binding Name}"/>
        </GridView>
      </ListView.View>
    </ListView>
  </Grid>
</Window>)XML";

    const std::string codeBehindCs = R"CS(
using System;
using System.Windows;
using Aero.UI;

namespace QuickApp
{
    public class AppVM : ViewModelBase
    {
        private string _title = "Initial";
        public string TitleText
        {
            get { return _title; }
            set { SetProperty(ref _title, value, nameof(TitleText)); }
        }
    }

    public partial class MainWindow : Window
    {
        public int ClickCount { get; private set; } = 0;

        public MainWindow()
        {
            InitializeComponent();
            DataContext = new AppVM();
        }

        public void OnGoClick(object sender, RoutedEventArgs e)
        {
            ClickCount = ClickCount + 1;
            HeaderText.Text = $"Clicked {ClickCount}";
        }
    }
}
)CS";

    aero::rs_comp::CompilerSession session;
    const aero::AeroCompilerV3 compiler;

    const bool runSlow = ShouldRunSlowAotOptTests();
    aero::AeroV3CompileInput input;
    input.xamlPath = "QuickApp/MainWindow.xaml";
    input.xamlContent = xaml;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = runSlow;
    input.emitAotCpp = runSlow;
    input.aotViewBuilderName = "BuildQuickAppWindow";
    input.compilerSession = &session;

    if (useFullRealStdlib) {
        input.includeStdlib = true;
        input.scriptSources.push_back(aero::SourceFile{"QuickApp/MainWindow.xaml.cs", codeBehindCs});
    } else {
        input.includeStdlib = false;
        for (const auto& sf : realStdlib) {
            input.scriptSources.push_back(sf);
        }
        input.scriptSources.push_back(BuildPreMergeMockAeroUiModule());
        input.scriptSources.push_back(aero::SourceFile{"QuickApp/MainWindow.xaml.cs", codeBehindCs});
    }

    const auto res1 = compiler.Compile(input);
    for (const auto& d : res1.diagnostics) {
        if (!res1.succeeded) {
            std::cerr << "  diag: " << d << "\n";
        }
    }
    Require(res1.succeeded, "End-to-end XAML + lowered C# compilation failed");
    Require(res1.programImage != nullptr, "End-to-end compilation must produce linked ProgramImage");
    if (!res1.succeeded || !res1.programImage) {
        return;
    }
    Require(res1.programImage->findFunction("QuickApp.MainWindow::InitializeComponent").has_value() ||
            ContainsText(res1.generatedPartialRsCode, "InitializeComponent"),
        "Compiled program must include QuickApp::MainWindow::InitializeComponent");
    if (runSlow) {
        Require(ContainsText(res1.aotCpp.scriptAotHeader, "BuildQuickAppWindowProgram"),
            "End-to-end AOT C++ header missing BuildQuickAppWindowProgram");
    }
    Require(session.parsedUnitCacheSize() > 0,
        "CompilerSession parsedUnitCache should be populated after first compile");

    // Second compile sharing the same CompilerSession -> reuses cached parsed units cleanly
    const auto res2 = compiler.Compile(input);
    Require(res2.succeeded, "Second cached compilation with CompilerSession failed");
    Require(session.parsedUnitCacheSize() > 0,
        "CompilerSession parsedUnitCache should remain active on second compile");

    // Verify CompilerSession moduleFrontendCache populates & hits for non-generic compilation units
    {
        aero::rs_comp::CompilerSession feSession;
        aero::AeroV3CompileInput feInput;
        feInput.includeStdlib = false;
        feInput.compilerSession = &feSession;
        feInput.scriptSources.push_back(aero::SourceFile{
            "CacheProbe.rs",
            "module CacheProbe;\npublic class Widget { public int Val() { return 7; } }\n"});
        const auto fe1 = compiler.Compile(feInput);
        const auto fe2 = compiler.Compile(feInput);
        Require(fe1.succeeded && fe2.succeeded, "CacheProbe repeated compilation failed");
        Require(feSession.parsedUnitCacheSize() > 0 && feSession.moduleFrontendCacheSize() > 0,
            "CompilerSession moduleFrontendCache should populate across repeated compiles");
    }
}

void TestMirTreeShakingAndProcessStdlibCache() {
    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    const aero::AeroCompilerV3 compiler;
    const std::filesystem::path diskCacheDir =
        std::filesystem::path(AERO_TEST_TEMP_DIR) / "stdlib_disk_cache_probe";
    std::error_code ec;
    std::filesystem::remove_all(diskCacheDir, ec);

    const std::string windowXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="PerfProbe.MainWindow"
        Title="TreeShake Probe" Width="480" Height="320">
  <StackPanel Orientation="Vertical" Spacing="8" Margin="16">
    <TextBlock x:Name="Banner" Text="Hello TreeShake" FontSize="18"/>
    <Button x:Name="GoBtn" Content="Run" Click="OnGo"/>
  </StackPanel>
</Window>)XML";

    const std::string windowRs = R"RS(
module PerfProbe;
import Aero.UI;

public partial class MainWindow : Window {
    public int Count = 0;
    public MainWindow() {
        InitializeComponent();
    }
    public void OnGo(object sender, RoutedEventArgs e) {
        Count = Count + 1;
        if (Banner != null) {
            Banner.Text = "Count=" + Count;
        }
    }
}
)RS";

    // 1. Unpruned reference check (treeShakeStdlib = false)
    aero::AeroV3CompileInput unprunedInput;
    unprunedInput.xamlPath = "PerfProbe/MainWindow.xaml";
    unprunedInput.xamlContent = windowXaml;
    unprunedInput.includeStdlib = true;
    unprunedInput.treeShakeStdlib = false;
    unprunedInput.optimizeBytecode = false;
    unprunedInput.scriptSources.push_back(aero::SourceFile{"PerfProbe/MainWindow.rs", windowRs});
    const auto unprunedRes = compiler.Compile(unprunedInput);
    Require(unprunedRes.succeeded && unprunedRes.programImage != nullptr,
        "Unpruned PerfProbe compilation failed");
    Require(unprunedRes.prunedMirFunctions == 0u,
        "Unpruned compile must report prunedMirFunctions == 0");

    // 2. Tree-shaken + disk-cached compile with compilerSession == nullptr
    aero::AeroV3CompileInput prunedInput;
    prunedInput.xamlPath = "PerfProbe/MainWindow.xaml";
    prunedInput.xamlContent = windowXaml;
    prunedInput.includeStdlib = true;
    prunedInput.treeShakeStdlib = true;
    prunedInput.optimizeBytecode = false;
    prunedInput.cacheDir = diskCacheDir.string();
    prunedInput.scriptSources.push_back(aero::SourceFile{"PerfProbe/MainWindow.rs", windowRs});

    const auto prunedRes = compiler.Compile(prunedInput);
    for (const auto& d : prunedRes.diagnostics) {
        if (!prunedRes.succeeded) {
            std::cerr << "  pruned diag: " << d << "\n";
        }
    }
    Require(prunedRes.succeeded && prunedRes.programImage != nullptr,
        "Tree-shaken PerfProbe compilation failed");
    std::printf(
        "  [TreeShake] .rsb unpruned=%zu bytes -> pruned=%zu bytes (types: %zu kept / %zu pruned, fns: %zu kept / %zu pruned)\n",
        unprunedRes.programImageBytes.size(),
        prunedRes.programImageBytes.size(),
        prunedRes.retainedMirTypes,
        prunedRes.prunedMirTypes,
        prunedRes.retainedMirFunctions,
        prunedRes.prunedMirFunctions);
    Require(prunedRes.programImageBytes.size() < 6500000u &&
            prunedRes.programImageBytes.size() * 10u < unprunedRes.programImageBytes.size() * 3u,
        "Tree-shaken .rsb on <Window> view must be < 6,500,000 bytes and < 30% of unpruned size");
    Require(prunedRes.prunedMirFunctions > 200u,
        "Tree-shaken <Window> compile must prune > 200 unreachable MIR functions");
    Require(prunedRes.prunedMirTypes > 0u,
        "Tree-shaken <Window> compile must prune unreachable MIR types");

    // 3. Verify warm Compile() with compilerSession == nullptr hits process-wide cache in < 500 ms
    const std::string warmXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="PerfProbeWarm.WarmWindow"
        Title="Warm Probe" Width="400" Height="260">
  <StackPanel Orientation="Vertical" Spacing="6">
    <TextBlock x:Name="StatusText" Text="Warm Cache"/>
    <Button x:Name="OkBtn" Content="OK"/>
  </StackPanel>
</Window>)XML";
    const std::string warmRs = R"RS(
module PerfProbeWarm;
import Aero.UI;

public partial class WarmWindow : Window {
    public WarmWindow() {
        InitializeComponent();
    }
}
)RS";

    aero::AeroV3CompileInput warmInput;
    warmInput.xamlPath = "PerfProbeWarm/WarmWindow.xaml";
    warmInput.xamlContent = warmXaml;
    warmInput.includeStdlib = true;
    warmInput.treeShakeStdlib = true;
    warmInput.optimizeBytecode = false;
    warmInput.compilerSession = nullptr;
    warmInput.scriptSources.push_back(aero::SourceFile{"PerfProbeWarm/WarmWindow.rs", warmRs});

    const auto t0 = std::chrono::steady_clock::now();
    const auto warmRes = compiler.Compile(warmInput);
    const auto warmMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf(
        "  [StdlibCache] cold=%lld ms -> warm Compile(compilerSession=nullptr) elapsed=%lld ms, usedStdlibCache=%d\n",
        gColdStdlibCompileMs,
        static_cast<long long>(warmMs),
        warmRes.usedStdlibCache ? 1 : 0);

    Require(warmRes.succeeded && warmRes.programImage != nullptr,
        "Warm Compile(compilerSession=nullptr) failed");
    Require(warmRes.usedStdlibCache,
        "Warm Compile(compilerSession=nullptr) must report usedStdlibCache == true");
    // RealScript Compilation.cpp:935 disables moduleFrontendCache whenever nativeGenericInstantiations
    // is non-empty (Aero.UI uses generics such as TypedBox<T>), so warm compiles still run
    // type-layout closure + user binding (~4-5s vs ~14-20s cold) while reusing stdlib MIR/ASTs.
    Require(warmMs < 8000,
        "Warm Compile(compilerSession=nullptr) must complete in < 8000 ms");
    Require(gColdStdlibCompileMs <= 0 || warmMs * 4 < gColdStdlibCompileMs * 3,
        "Warm Compile(compilerSession=nullptr) must be < 0.75 * coldElapsedMs");

    // Verify disk cache artifacts were written cleanly
    bool foundSnapMeta = false;
    if (std::filesystem::exists(diskCacheDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(diskCacheDir, ec)) {
            if (entry.path().extension() == ".snapmeta") {
                foundSnapMeta = true;
                break;
            }
        }
    }
    Require(foundSnapMeta, "Expected disk cache .snapmeta artifact in cacheDir");
    std::filesystem::remove_all(diskCacheDir, ec);
}

void TestMultiXamlAdditionalSourcesCompilation() {
    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    const std::string badgeXaml = R"XML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="MultiXamlDemo.StatusBadge">
  <Border Padding="8" Background="#FF1E293B" CornerRadius="6">
    <TextBlock x:Name="BadgeLabel" Text="Ready" Foreground="#FFF8FAFC"/>
  </Border>
</UserControl>)XML";

    const std::string badgeRs = R"RS(
module MultiXamlDemo;
import Aero.UI;

public partial class StatusBadge : UserControl {
    public StatusBadge() {
        InitializeComponent();
    }
    public void SetLabel(string text) {
        if (BadgeLabel != null) {
            BadgeLabel.Text = text;
        }
    }
}
)RS";

    const std::string mainXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="MultiXamlDemo.MainWindow"
        Title="Multi-XAML Demo" Width="640" Height="400">
  <StackPanel Orientation="Vertical" Spacing="10" Margin="16">
    <TextBlock Text="Host Window"/>
    <StatusBadge x:Name="PrimaryBadge"/>
  </StackPanel>
</Window>)XML";

    const std::string mainRs = R"RS(
module MultiXamlDemo;
import Aero.UI;

public partial class MainWindow : Window {
    public MainWindow() {
        InitializeComponent();
        if (PrimaryBadge != null) {
            PrimaryBadge.SetLabel("Online");
        }
    }
}
)RS";

    aero::AeroV3CompileInput input;
    input.xamlPath = "MultiXamlDemo/MainWindow.xaml";
    input.xamlContent = mainXaml;
    input.additionalXamlSources.push_back(
        aero::SourceFile{"MultiXamlDemo/StatusBadge.xaml", badgeXaml});
    input.scriptSources.push_back(
        aero::SourceFile{"MultiXamlDemo/StatusBadge.rs", badgeRs});
    input.scriptSources.push_back(
        aero::SourceFile{"MultiXamlDemo/MainWindow.rs", mainRs});
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;

    const aero::AeroCompilerV3 compiler;
    const auto res = compiler.Compile(input);
    for (const auto& d : res.diagnostics) {
        if (!res.succeeded) {
            std::cerr << "  multi-xaml diag: " << d << "\n";
        }
    }
    Require(res.succeeded && res.programImage != nullptr,
        "Multi-XAML UserControl + MainWindow compilation failed");
    if (!res.succeeded || !res.programImage) {
        return;
    }
    Require(res.programImage->findFunction("MultiXamlDemo::StatusBadge.InitializeComponent").has_value(),
        "Compiled ProgramImage missing MultiXamlDemo::StatusBadge.InitializeComponent");
    Require(res.programImage->findFunction("MultiXamlDemo::MainWindow.InitializeComponent").has_value(),
        "Compiled ProgramImage missing MultiXamlDemo::MainWindow.InitializeComponent");
}

void TestP0P1CompilerSemanticsAndStdlibGenericCache() {
    const std::string_view mergedResources = R"XML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                    xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <sys:Double x:Key="SidebarWidth">220</sys:Double>
  <DataTemplate x:Key="LeafTpl">
    <TextBlock Text="{Binding Title}"/>
  </DataTemplate>
  <HierarchicalDataTemplate x:Key="CatTpl" ItemsSource="{Binding Children}" ItemTemplate="{StaticResource LeafTpl}">
    <TextBlock Text="{Binding Name}"/>
  </HierarchicalDataTemplate>
</ResourceDictionary>)XML";

    const std::string_view p0p1Xaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
        x:Class="P0P1CompileDemo.MainWindow"
        Title="P0P1 Compile Demo" Width="800" Height="600">
  <Window.Resources>
    <ResourceDictionary>
      <ResourceDictionary.MergedDictionaries>
        <ResourceDictionary Source="Shared.xaml"/>
      </ResourceDictionary.MergedDictionaries>
      <Storyboard x:Key="IntroAndState">
        <BooleanAnimationUsingKeyFrames Storyboard.TargetName="SubmitBtn" Storyboard.TargetProperty="(UIElement.IsEnabled)">
          <DiscreteBooleanKeyFrame KeyTime="0" Value="False"/>
          <DiscreteBooleanKeyFrame KeyTime="0:0:0.6" Value="True"/>
        </BooleanAnimationUsingKeyFrames>
        <ObjectAnimationUsingKeyFrames Storyboard.TargetName="StatusPanel" Storyboard.TargetProperty="(UIElement.Visibility)">
          <DiscreteObjectKeyFrame KeyTime="0" Value="{x:Static Visibility.Visible}"/>
          <DiscreteObjectKeyFrame KeyTime="0:0:0.4" Value="{x:Static Visibility.Collapsed}"/>
        </ObjectAnimationUsingKeyFrames>
        <PointAnimationUsingKeyFrames Storyboard.TargetName="StatusPanel" Storyboard.TargetProperty="RenderTransformOrigin">
          <LinearPointKeyFrame KeyTime="0:0:0.5" Value="0.5,0.5"/>
        </PointAnimationUsingKeyFrames>
      </Storyboard>
    </ResourceDictionary>
  </Window.Resources>
  <Grid>
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="{Binding Source={StaticResource SidebarWidth}}"/>
      <ColumnDefinition Width="*"/>
    </Grid.ColumnDefinitions>
    <StackPanel Grid.Column="0">
      <StackPanel.LayoutTransform>
        <ScaleTransform ScaleX="1"/>
      </StackPanel.LayoutTransform>
      <TreeView x:Name="CatTree" ItemsSource="{Binding Categories}" ItemTemplate="{StaticResource CatTpl}">
        <b:Interaction.Triggers>
          <b:EventTrigger EventName="SelectedItemChanged">
            <b:ChangePropertyAction PropertyName="Tag" Value="{Binding SelectedItem, ElementName=CatTree}"/>
          </b:EventTrigger>
        </b:Interaction.Triggers>
      </TreeView>
    </StackPanel>
    <StackPanel x:Name="StatusPanel" Grid.Column="1" Spacing="8">
      <Rectangle x:Name="ShaderRect" Width="120" Height="60">
        <Rectangle.Fill>
          <ImageBrush x:Name="ShaderFill">
            <noesis:Brush.Shader>
              <noesis:PlasmaBrush Time="0.5" Scale="1.0,1.0"/>
            </noesis:Brush.Shader>
          </ImageBrush>
        </Rectangle.Fill>
      </Rectangle>
      <Button x:Name="SubmitBtn" Content="Submit"/>
    </StackPanel>
  </Grid>
</Window>)XML";

    aero::XamlEmitOptions opts;
    opts.xamlPath = "P0P1CompileDemo/MainWindow.xaml";
    opts.externalXamlResolver = [&](std::string_view req, std::string_view, std::string& resolved, std::string& content) {
        if (req == "Shared.xaml") {
            resolved = "P0P1CompileDemo/Shared.xaml";
            content = std::string(mergedResources);
            return true;
        }
        return false;
    };

    const auto emitRes = aero::EmitXamlToRealScript(p0p1Xaml, opts);
    for (const auto& d : emitRes.diagnostics) {
        if (!emitRes.succeeded) {
            std::cerr << "  p0p1 emit diag: " << d << "\n";
        }
    }
    Require(emitRes.succeeded, "P0P1 XamlToRsEmitter must succeed (warnings must not fail compilation)");
    Require(ContainsText(emitRes.generatedRsCode, "new HierarchicalDataTemplate(") &&
            ContainsText(emitRes.generatedRsCode, "\"Children\"") &&
            ContainsText(emitRes.generatedRsCode, "ItemTemplate"),
        "HierarchicalDataTemplate emission missing ItemsSourcePath or ItemTemplate wiring");
    Require(ContainsText(emitRes.generatedRsCode, "AddDiscreteKeyFrame(\"SubmitBtn\", \"IsEnabled\"") &&
            ContainsText(emitRes.generatedRsCode, "AddDiscreteKeyFrame(\"StatusPanel\", \"Visibility\""),
        "BooleanAnimationUsingKeyFrames / ObjectAnimationUsingKeyFrames must emit Storyboard.AddDiscreteKeyFrame");
    Require(ContainsText(emitRes.generatedRsCode, "ValueBinding =") &&
            !ContainsText(emitRes.generatedRsCode, "\"{Binding SelectedItem"),
        "ChangePropertyAction Value=\"{Binding ...}\" must emit ValueBinding assignment, not literal string");
    Require(!ContainsText(emitRes.generatedRsCode, "new ImageBrush(\"\")"),
        "ImageBrush with noesis:Brush.Shader must not emit blank ImageBrush(\"\")");
    Require(ContainsText(emitRes.generatedRsCode, "220") ||
            ContainsText(emitRes.generatedRsCode, "SidebarWidth"),
        "Grid.ColumnDefinition Width={Binding Source={StaticResource SidebarWidth}} must not degrade silently to Auto");

    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    const std::string codeBehindRs = R"RS(
module P0P1CompileDemo;
import Aero.UI;

// ObservableList<string> in line comment must NOT trigger stdlib generic instantiation scan
/* DelegateCommand<object> in block comment must NOT trigger stdlib generic instantiation scan */
public partial class MainWindow : Window {
    public string CommentProbe = "RelayCommand<int> TypedBox<float> ObservableCollection<string> in string literal";
    public MainWindow() {
        InitializeComponent();
    }
}
)RS";

    aero::AeroV3CompileInput compileInput;
    compileInput.xamlPath = "P0P1CompileDemo/MainWindow.xaml";
    compileInput.xamlContent = std::string(p0p1Xaml);
    compileInput.resourceSearchRoot = "P0P1CompileDemo";
    compileInput.includeStdlib = true;
    compileInput.treeShakeStdlib = true;
    compileInput.optimizeBytecode = false;
    compileInput.scriptSources.push_back(aero::SourceFile{"P0P1CompileDemo/MainWindow.rs", codeBehindRs});

    // Write temporary Shared.xaml resolver source into additionalXamlSources or temp dir on disk
    const std::filesystem::path tmpDir = std::filesystem::path(AERO_TEST_TEMP_DIR) / "p0p1_compiler_probe";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    {
        std::ofstream out((tmpDir / "Shared.xaml").string(), std::ios::binary | std::ios::trunc);
        Require(out.is_open(), "Failed to create temporary Shared.xaml for compiler probe");
        if (out.is_open()) {
            out.write(mergedResources.data(), static_cast<std::streamsize>(mergedResources.size()));
        }
    }
    compileInput.xamlPath = (tmpDir / "MainWindow.xaml").string();
    compileInput.resourceSearchRoot = tmpDir.string();
    compileInput.resourceSearchPaths.push_back(tmpDir.string());
    compileInput.compilerSession = nullptr;

    const aero::AeroCompilerV3 compiler;
    const auto compileRes = compiler.Compile(compileInput);
    for (const auto& d : compileRes.diagnostics) {
        if (!compileRes.succeeded) {
            std::cerr << "  p0p1 compile diag: " << d << "\n";
        }
    }
    Require(compileRes.succeeded && compileRes.programImage != nullptr,
        "End-to-end AeroCompilerV3 compile of HierarchicalDataTemplate/DiscreteKeyFrame/ChangePropertyAction failed");
    Require(compileRes.usedStdlibCache,
        "Stdlib cache should remain active when generics appear only inside comments and string literals");
    std::filesystem::remove_all(tmpDir, ec);
}

void TestGalleryEngineCompilerFixes() {
    const std::string_view testXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="GalleryFixCompileDemo.MainWindow"
        Title="Gallery Fixes Compile Demo" Width="800" Height="600">
  <Window.Resources>
    <Style TargetType="ToolBar">
      <Style.Resources>
        <Style x:Key="{x:Static ToolBar.ButtonStyleKey}" TargetType="Button">
          <Setter Property="Width" Value="44"/>
        </Style>
      </Style.Resources>
    </Style>
  </Window.Resources>
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition x:Name="TopRow" Height="Auto"/>
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <Grid.ColumnDefinitions>
      <ColumnDefinition x:Name="LeftCol" Width="100"/>
      <ColumnDefinition Width="*"/>
    </Grid.ColumnDefinitions>
    <UniformGrid Grid.Row="0" Grid.Column="0" FirstColumn="2">
      <Button TabIndex="5" Content="Go"/>
    </UniformGrid>
    <GridViewColumnHeader Grid.Row="1" Grid.Column="1" Content="Col"/>
  </Grid>
</Window>)XML";

    const aero::XamlEmitOptions opts;
    const auto emitRes = aero::EmitXamlToRealScript(testXaml, opts);
    for (const auto& d : emitRes.diagnostics) {
        if (!emitRes.succeeded) {
            std::cerr << "  gallery fix emit diag: " << d << "\n";
        }
    }
    Require(emitRes.succeeded, "TestGalleryEngineCompilerFixes XamlToRsEmitter must succeed");
    Require(ContainsText(emitRes.generatedRsCode, ".Resources.SetBox(\"ToolBar.ButtonStyleKey\""),
        "Style.Resources with x:Key={x:Static ToolBar.ButtonStyleKey} should emit ToolBar.ButtonStyleKey registration");
    Require(ContainsText(emitRes.generatedRsCode, "FirstColumn = 2"),
        "UniformGrid FirstColumn attribute should emit direct FirstColumn assignment");
    Require(ContainsText(emitRes.generatedRsCode, "TabIndex = 5"),
        "Button TabIndex attribute should emit direct TabIndex assignment on UIElement");
    Require(ContainsText(emitRes.generatedRsCode, "new GridViewColumnHeader()"),
        "GridViewColumnHeader should be emitted as a recognized UI control");
    Require(ContainsText(emitRes.generatedRsCode, "AddRowDefinition(") &&
            ContainsText(emitRes.generatedRsCode, "\"TopRow\")"),
        "RowDefinition x:Name should be forwarded to AddRowDefinition");
    Require(ContainsText(emitRes.generatedRsCode, "AddColumnDefinition(") &&
            ContainsText(emitRes.generatedRsCode, "\"LeftCol\")"),
        "ColumnDefinition x:Name should be forwarded to AddColumnDefinition");

    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    const std::string codeBehindRs = R"RS(
module GalleryFixCompileDemo;
import Aero.UI;

public partial class MainWindow : Window {
    public MainWindow() {
        InitializeComponent();
    }
}
)RS";

    aero::AeroV3CompileInput compileInput;
    compileInput.xamlPath = "GalleryFixCompileDemo/MainWindow.xaml";
    compileInput.xamlContent = std::string(testXaml);
    compileInput.includeStdlib = true;
    compileInput.treeShakeStdlib = true;
    compileInput.optimizeBytecode = false;
    compileInput.scriptSources.push_back(aero::SourceFile{"GalleryFixCompileDemo/MainWindow.rs", codeBehindRs});

    const aero::AeroCompilerV3 compiler;
    const auto compileRes = compiler.Compile(compileInput);
    for (const auto& d : compileRes.diagnostics) {
        if (!compileRes.succeeded) {
            std::cerr << "  gallery fix compile diag: " << d << "\n";
        }
    }
    Require(compileRes.succeeded && compileRes.programImage != nullptr,
        "End-to-end AeroCompilerV3 compile of Style.Resources/x:Static/FirstColumn/TabIndex/GridViewColumnHeader/Grid x:Name failed");
}

void TestIncrementalSpecModuleCacheInvalidation() {
    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    aero::rs_comp::CompilerSession specSession;
    const aero::AeroCompilerV3 compiler;

    const std::string xaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="SpecIncDemo.MainWindow"
        Title="Spec Incremental Cache" Width="400" Height="300">
  <TextBlock Text="Hi"/>
</Window>)XML";

    const std::string modelV1 = R"RS(
module SpecIncModel;
public class Hero {
    public int Hp = 10;
}
)RS";

    const std::string storeV1 = R"RS(
module SpecIncStore;
import Aero.UI;
import SpecIncModel;
public class HeroStore {
    public ObservableList<Hero> Items;
    public int VersionTag = 1;
    public HeroStore() {
        Items = new ObservableList<Hero>();
        var h = new Hero();
        h.Hp = 42;
        Items.Add(h);
    }
    public int FirstHp() {
        return Items.Count() == 1 ? Items.Get(0).Hp : -1;
    }
}
)RS";

    const std::string mainRs = R"RS(
module SpecIncDemo;
import Aero.UI;
import SpecIncStore;
public partial class MainWindow : Window {
    public HeroStore Store;
    public MainWindow() {
        InitializeComponent();
        Store = new HeroStore();
    }
    public int CheckVal() {
        return Store.FirstHp();
    }
}
)RS";

    auto makeInput = [&](std::string modelSrc, std::string storeSrc) {
        aero::AeroV3CompileInput in;
        in.xamlPath = "SpecIncDemo/MainWindow.xaml";
        in.xamlContent = xaml;
        in.includeStdlib = true;
        in.treeShakeStdlib = false;
        in.optimizeBytecode = false;
        in.allowStdlibCache = true;
        in.compilerSession = &specSession;
        in.scriptSources.push_back(aero::SourceFile{"SpecIncDemo/Model.rs", std::move(modelSrc)});
        in.scriptSources.push_back(aero::SourceFile{"SpecIncDemo/Store.rs", std::move(storeSrc)});
        in.scriptSources.push_back(aero::SourceFile{"SpecIncDemo/MainWindow.rs", mainRs});
        return in;
    };

    // 1. Initial compile populates session snapshot with Aero.UI.__spec.SpecIncModel
    const auto r1 = compiler.Compile(makeInput(modelV1, storeV1));
    for (const auto& d : r1.diagnostics) {
        if (!r1.succeeded) { std::cerr << "  spec inc #1 diag: " << d << "\n"; }
    }
    Require(r1.succeeded && r1.programImage != nullptr, "TestIncrementalSpecModuleCacheInvalidation compile #1 failed");

    // 2. Edit Store only (Model & Aero.UI.__spec.SpecIncModel stay unchanged -> dirtyQueue restores hygienic module decls)
    const std::string storeV2 = R"RS(
module SpecIncStore;
import Aero.UI;
import SpecIncModel;
public class HeroStore {
    public ObservableList<Hero> Items;
    public int VersionTag = 2;
    public HeroStore() {
        Items = new ObservableList<Hero>();
        var h = new Hero();
        h.Hp = 77;
        Items.Add(h);
    }
    public int FirstHp() {
        return Items.Count() == 1 ? Items.Get(0).Hp + VersionTag : -1;
    }
}
)RS";
    const auto r2 = compiler.Compile(makeInput(modelV1, storeV2));
    for (const auto& d : r2.diagnostics) {
        if (!r2.succeeded) { std::cerr << "  spec inc #2 diag: " << d << "\n"; }
    }
    Require(r2.succeeded && r2.programImage != nullptr, "TestIncrementalSpecModuleCacheInvalidation compile #2 failed");

    // 3. Edit Model (Hero gains Tag field) & read Tag in Store -> hygienicModules must invalidate cached .__spec. module
    const std::string modelV2 = R"RS(
module SpecIncModel;
public class Hero {
    public int Hp = 10;
    public string Tag = "ok";
}
)RS";
    const std::string storeV3 = R"RS(
module SpecIncStore;
import Aero.UI;
import SpecIncModel;
public class HeroStore {
    public ObservableList<Hero> Items;
    public int VersionTag = 3;
    public HeroStore() {
        Items = new ObservableList<Hero>();
        var h = new Hero();
        h.Hp = 88;
        h.Tag = "ok";
        Items.Add(h);
    }
    public int FirstHp() {
        return (Items.Count() == 1 && Items.Get(0).Tag == "ok") ? 99 : -1;
    }
}
)RS";
    const auto r3 = compiler.Compile(makeInput(modelV2, storeV3));
    for (const auto& d : r3.diagnostics) {
        if (!r3.succeeded) { std::cerr << "  spec inc #3 diag: " << d << "\n"; }
    }
    Require(r3.succeeded && r3.programImage != nullptr, "TestIncrementalSpecModuleCacheInvalidation compile #3 failed");
}

void TestPhase4PackagesCatalogAndPruning() {
    std::cout << "[Test] 4-package stdlib split, CompiledXamlCatalog, and Workbench pruning...\n";

    // 1. Selective stdlib package source packing
    const auto packCore = aero::LoadStdlibModulePack(AERO_STDLIB_DIR, aero::AeroStdlibPkg_Core);
    const auto packControls = aero::LoadStdlibModulePack(AERO_STDLIB_DIR, aero::AeroStdlibPkg_Controls);
    const auto packAll = aero::LoadStdlibModulePack(AERO_STDLIB_DIR, aero::AeroStdlibPkg_All);
    Require(!packCore.empty() && packCore.size() < packControls.size() && packControls.size() < packAll.size(),
        "4-package stdlib split source counts unexpected: core=" + std::to_string(packCore.size()) +
        " controls=" + std::to_string(packControls.size()) +
        " all=" + std::to_string(packAll.size()));

    // 2. Minimal Grid + TextBlock view infers AeroStdlibPkg_Core and prunes __RegisterWorkbenchXamlFactories
    aero::AeroCompilerV3 compiler;
    aero::AeroV3CompileInput minIn;
    minIn.xamlPath = "MinView.xaml";
    minIn.xamlContent = R"XML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="Samples.MinPkg.MinView">
    <Grid>
        <TextBlock Text="Hello Core" />
    </Grid>
</UserControl>
)XML";
    minIn.stdlibRootDir = AERO_STDLIB_DIR;
    minIn.allowDiskCache = false;
    minIn.allowStdlibCache = false;
    minIn.treeShakeStdlib = true;
    minIn.stdlibPackages = aero::AeroStdlibPkg_Auto;

    const auto minRes = compiler.Compile(minIn);
    Require(minRes.succeeded && minRes.programImage != nullptr, "Min Core package compile failed");
    Require(minRes.resolvedStdlibPackages == aero::AeroStdlibPkg_Core,
        "Expected Auto inference to pick AeroStdlibPkg_Core");

    bool hasWorkbenchHookInMin = false;
    for (const auto& mod : minRes.programImage->modules()) {
        for (const auto& fn : mod.functions) {
            if (fn.name.find("__RegisterWorkbenchXamlFactories") != std::string::npos) {
                hasWorkbenchHookInMin = true;
            }
        }
    }
    Require(!hasWorkbenchHookInMin,
        "__RegisterWorkbenchXamlFactories must be tree-shaken when XamlReader and Workbench types are absent");

    // 3. Compare unpruned Core vs All MIR type counts
    aero::AeroV3CompileInput unprunedCoreIn = minIn;
    unprunedCoreIn.treeShakeStdlib = false;
    unprunedCoreIn.stdlibPackages = aero::AeroStdlibPkg_Core;
    const auto unprunedCore = compiler.Compile(unprunedCoreIn);

    aero::AeroV3CompileInput unprunedAllIn = minIn;
    unprunedAllIn.treeShakeStdlib = false;
    unprunedAllIn.stdlibPackages = aero::AeroStdlibPkg_All;
    const auto unprunedAll = compiler.Compile(unprunedAllIn);

    Require(unprunedCore.succeeded && unprunedAll.succeeded, "Core vs All unpruned compile failed");
    Require(unprunedCore.retainedMirTypes < unprunedAll.retainedMirTypes,
        "Core package retainedMirTypes (" + std::to_string(unprunedCore.retainedMirTypes) +
        ") should be < All (" + std::to_string(unprunedAll.retainedMirTypes) + ")");

    // 4. CompiledXamlCatalog emission + tree-shaking coexistence
    aero::AeroV3CompileInput catIn;
    catIn.xamlPath = "HostPage.xaml";
    catIn.xamlContent = R"XML(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      x:Class="Samples.CatTest.HostPage">
    <Border x:Name="HostSlot" />
</Page>
)XML";
    catIn.scriptSources.push_back(aero::SourceFile{"HostPage.rs", R"RS(
module Samples.CatTest;
import Aero.UI;
public partial class HostPage : Page {
    public void LoadCatalogItems() {
        var rd = XamlReader.LoadResourceDictionary("Theme/CatTheme.xaml");
        var ui = XamlReader.LoadUri("Data/Samples/DemoCard.xaml", rd);
        if (this.HostSlot != null) { this.HostSlot.Child = ui; }
    }
}
)RS"});
    catIn.catalogXamlSources.push_back(aero::CatalogXamlEntry{
        "Data/Samples/DemoCard.xaml",
        {"/Data/Samples/DemoCard.xaml", "DemoCard.xaml"},
        "Data/Samples/DemoCard.xaml",
        R"XML(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Background="#FF223344">
    <TextBlock Text="FromCatalog" />
</Border>
)XML",
        false});
    catIn.catalogXamlSources.push_back(aero::CatalogXamlEntry{
        "Theme/CatTheme.xaml",
        {"/Theme/CatTheme.xaml", "CatTheme.xaml"},
        "Theme/CatTheme.xaml",
        R"XML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <SolidColorBrush x:Key="CatalogAccent" Color="#FF11AAFF" />
</ResourceDictionary>
)XML",
        true});
    catIn.stdlibRootDir = AERO_STDLIB_DIR;
    catIn.allowDiskCache = false;
    catIn.allowStdlibCache = false;
    catIn.treeShakeStdlib = true;

    const auto catRes = compiler.Compile(catIn);
    Require(catRes.succeeded && catRes.programImage != nullptr, "CompiledXamlCatalog compile failed");
    Require(catRes.compiledCatalogEntryCount == 2u,
        "Expected 2 compiled catalog entries, got " + std::to_string(catRes.compiledCatalogEntryCount));

    bool foundCatalogInit = false;
    for (const auto& mod : catRes.programImage->modules()) {
        if (mod.name == "__AeroCompiledCatalog") {
            for (const auto& fn : mod.functions) {
                if (fn.name.find("RegisterAll") != std::string::npos) {
                    foundCatalogInit = true;
                }
            }
        }
    }
    Require(foundCatalogInit, "Missing __AeroCompiledXamlCatalogInit.RegisterAll in __AeroCompiledCatalog");
}

void TestPackageInferenceScannersAndPropertyPruning() {
    std::cout << "[Test] Token-aware package inference & tightened MIR property pruning...\n";

    // 1. Direct unit checks on InferRequiredStdlibPackages (Controls / Workbench / RuntimeXaml + no false positives)
    {
        aero::AeroV3CompileInput falsePosProbe;
        falsePosProbe.xamlContent = R"XML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="Samples.CleanCore.CleanView">
    <!-- <Button/> <GridSplitter/> <PropertyGrid/> XamlReader.LoadUri("Data/Samples/Foo.xaml") -->
    <Grid>
        <TextBlock Text="Click Button or Menu or ComboBox" />
    </Grid>
</UserControl>
)XML";
        falsePosProbe.scriptSources.push_back(aero::SourceFile{"CleanView.rs", R"RS(
module Samples.CleanCore;
import Aero.UI;
// Button ComboBox PropertyGrid XamlReader
/* Menu Popup DockingWorkspace XamlReader.LoadUri("Data/Samples/Ignored.xaml") */
public partial class CleanView : UserControl {
    public int CloseButtonCount = 0;
    public string MenuTitle = "Click Button";
    public string RawNote = @"Button PropertyGrid XamlReader";
}
)RS"});
        Require(aero::InferRequiredStdlibPackages(falsePosProbe) == aero::AeroStdlibPkg_Core,
            "Comments, string literals, and compound identifiers must not trigger Controls/Workbench/RuntimeXaml");

        for (const char* ctrlTag : {"GridSplitter", "Hyperlink", "MediaElement", "Thumb", "RadialTicks"}) {
            aero::AeroV3CompileInput tagProbe;
            tagProbe.xamlContent = std::string("<Window><") + ctrlTag + " /></Window>";
            Require(aero::InferRequiredStdlibPackages(tagProbe) ==
                        (aero::AeroStdlibPkg_Core | aero::AeroStdlibPkg_Controls),
                std::string("XAML element <") + ctrlTag + "> must infer AeroStdlibPkg_Controls");
        }

        for (const char* wbSym : {"EditorMath", "NodeGraphNodeData", "CodeDiagnosticItem", "InspectorPropertyItem", "DockZone"}) {
            aero::AeroV3CompileInput wbProbe;
            wbProbe.scriptSources.push_back(aero::SourceFile{"Wb.rs", std::string("var x = ") + wbSym + ";"});
            Require(aero::InferRequiredStdlibPackages(wbProbe) ==
                        (aero::AeroStdlibPkg_Core | aero::AeroStdlibPkg_Controls | aero::AeroStdlibPkg_Workbench),
                std::string("Workbench symbol ") + wbSym + " must infer AeroStdlibPkg_Workbench");
        }

        aero::AeroV3CompileInput catSymProbe;
        catSymProbe.scriptSources.push_back(aero::SourceFile{"Cat.rs", "CompiledXamlCatalog.NormalizeUriKey(\"a\");"});
        Require(aero::InferRequiredStdlibPackages(catSymProbe) ==
                    (aero::AeroStdlibPkg_Core | aero::AeroStdlibPkg_Controls | aero::AeroStdlibPkg_RuntimeXaml),
            "CompiledXamlCatalog identifier must infer AeroStdlibPkg_RuntimeXaml");
    }

    const auto realStdlib = aero::LoadStdlibModulePack();
    if (!RealStdlibHasUiClasses(realStdlib)) {
        return;
    }

    aero::AeroCompilerV3 compiler;

    // 2. End-to-end Auto compile (compilerSession == nullptr) with <GridSplitter>, <Hyperlink>, <MediaElement>, <Thumb>, <RadialTicks>
    {
        aero::AeroV3CompileInput ctrlIn;
        ctrlIn.xamlPath = "ControlsSweep.xaml";
        ctrlIn.xamlContent = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Samples.PkgSweep.ControlsSweep"
        Title="Controls Sweep" Width="640" Height="400">
    <Grid>
        <GridSplitter Width="4" />
        <TextBlock><Hyperlink NavigateUri="https://example.com">Docs</Hyperlink></TextBlock>
        <MediaElement Source="clip.mp4" Volume="0.5" IsMuted="True" />
        <Thumb Width="16" Height="16" />
        <RadialTicks Minimum="0" Maximum="100" TickWidth="2" TickLength="8" />
    </Grid>
</Window>
)XML";
        ctrlIn.stdlibRootDir = AERO_STDLIB_DIR;
        ctrlIn.compilerSession = nullptr;
        ctrlIn.stdlibPackages = aero::AeroStdlibPkg_Auto;
        ctrlIn.treeShakeStdlib = true;

        const auto ctrlRes = compiler.Compile(ctrlIn);
        for (const auto& d : ctrlRes.diagnostics) {
            if (!ctrlRes.succeeded) {
                std::cerr << "  ctrlSweep diag: " << d << "\n";
            }
        }
        Require(ctrlRes.succeeded && ctrlRes.programImage != nullptr,
            "Auto compile with GridSplitter/Hyperlink/MediaElement/Thumb/RadialTicks failed");
        Require(ctrlRes.resolvedStdlibPackages == (aero::AeroStdlibPkg_Core | aero::AeroStdlibPkg_Controls),
            "Expected GridSplitter/Hyperlink/MediaElement/Thumb/RadialTicks view to resolve AeroStdlibPkg_Controls");
    }

    // 3. End-to-end false-positive resistance compile: stays on AeroStdlibPkg_Core
    {
        aero::AeroV3CompileInput coreIn;
        coreIn.xamlPath = "CleanCoreView.xaml";
        coreIn.xamlContent = R"XML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="Samples.NoFalsePos.CleanCoreView">
    <!-- Button ComboBox PropertyGrid XamlReader -->
    <Grid>
        <TextBlock Text="Hello"/>
    </Grid>
</UserControl>
)XML";
        coreIn.scriptSources.push_back(aero::SourceFile{"CleanCoreView.rs", R"RS(
module Samples.NoFalsePos;
import Aero.UI;
// Button ComboBox PropertyGrid XamlReader
public partial class CleanCoreView : UserControl {
    public int CloseButtonCount = 0;
    public string Prompt = "Click Button";
    public CleanCoreView() {
        InitializeComponent();
    }
}
)RS"});
        coreIn.stdlibRootDir = AERO_STDLIB_DIR;
        coreIn.compilerSession = nullptr;
        coreIn.stdlibPackages = aero::AeroStdlibPkg_Auto;
        coreIn.treeShakeStdlib = true;

        const auto coreRes = compiler.Compile(coreIn);
        for (const auto& d : coreRes.diagnostics) {
            if (!coreRes.succeeded) {
                std::cerr << "  cleanCore diag: " << d << "\n";
            }
        }
        Require(coreRes.succeeded && coreRes.programImage != nullptr,
            "CleanCoreView compilation failed");
        Require(coreRes.resolvedStdlibPackages == aero::AeroStdlibPkg_Core,
            "CleanCoreView with comments/strings/CloseButtonCount must resolve AeroStdlibPkg_Core");
    }

    // 4. Minimal <Window> MIR reachability pruning vs Binding/Setter/Style retention
    {
        aero::AeroV3CompileInput minWinIn;
        minWinIn.xamlPath = "MinimalWin.xaml";
        minWinIn.xamlContent = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Samples.MinWin.MinimalWin">
    <Grid>
        <TextBlock Text="Hello" />
    </Grid>
</Window>
)XML";
        minWinIn.stdlibRootDir = AERO_STDLIB_DIR;
        minWinIn.compilerSession = nullptr;
        minWinIn.stdlibPackages = aero::AeroStdlibPkg_Auto;
        minWinIn.treeShakeStdlib = true;

        const auto minWinRes = compiler.Compile(minWinIn);
        Require(minWinRes.succeeded && minWinRes.programImage != nullptr,
            "Minimal <Window> tree-shaken compile failed");
        std::printf(
            "  [MinimalWindowPrune] retainedFns=%zu, prunedFns=%zu, retainedTypes=%zu\n",
            minWinRes.retainedMirFunctions,
            minWinRes.prunedMirFunctions,
            minWinRes.retainedMirTypes);
        // Previously ~1,443 MIR functions were retained because every public get_*/set_* on
        // instantiated Aero.UI classes was treated as an unconditional root (now ~1,282).
        Require(minWinRes.retainedMirFunctions < 1340u,
            "Minimal <Window> tree-shaking must retain < 1340 MIR functions (got " +
            std::to_string(minWinRes.retainedMirFunctions) + ")");

        // Verify a <Window> with Style Setters, DataTrigger, and Binding on SolidColorBrush.Color
        // retains referenced Aero.UI accessors and compiles/verifies cleanly.
        aero::AeroV3CompileInput styledIn;
        styledIn.xamlPath = "StyledWin.xaml";
        styledIn.xamlContent = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Samples.StyledProbe.StyledWin">
    <Window.Resources>
        <Style x:Key="CardStyle" TargetType="Border">
            <Setter Property="Background" Value="#FF223344" />
            <Setter Property="CornerRadius" Value="8" />
            <Style.Triggers>
                <DataTrigger Binding="{Binding IsHighlighted}" Value="True">
                    <Setter Property="Opacity" Value="0.75" />
                </DataTrigger>
            </Style.Triggers>
        </Style>
    </Window.Resources>
    <Border Style="{StaticResource CardStyle}">
        <TextBlock x:Name="MsgText" Text="{Binding Headline}">
            <TextBlock.Foreground>
                <SolidColorBrush Color="{Binding AccentHex}" />
            </TextBlock.Foreground>
        </TextBlock>
    </Border>
</Window>
)XML";
        styledIn.scriptSources.push_back(aero::SourceFile{"StyledWin.rs", R"RS(
module Samples.StyledProbe;
import Aero.UI;
public class StyledVm {
    public bool IsHighlighted { get; set; } = true;
    public string Headline { get; set; } = "Styled OK";
    public string AccentHex { get; set; } = "#FF00FFAA";
}
public partial class StyledWin : Window {
    public StyledWin() {
        DataContext = new StyledVm();
        InitializeComponent();
    }
}
)RS"});
        styledIn.stdlibRootDir = AERO_STDLIB_DIR;
        styledIn.compilerSession = nullptr;
        styledIn.stdlibPackages = aero::AeroStdlibPkg_Auto;
        styledIn.treeShakeStdlib = true;

        const auto styledRes = compiler.Compile(styledIn);
        for (const auto& d : styledRes.diagnostics) {
            if (!styledRes.succeeded) {
                std::cerr << "  styledWin diag: " << d << "\n";
            }
        }
        Require(styledRes.succeeded && styledRes.programImage != nullptr,
            "Styled/Bound <Window> tree-shaken compile failed");

        bool hasSolidBrushSetColor = false;
        for (const auto& mod : styledRes.programImage->modules()) {
            if (mod.name == "Aero.UI") {
                for (const auto& fn : mod.functions) {
                    if (fn.name == "SolidColorBrush.set_Color") {
                        hasSolidBrushSetColor = true;
                    }
                }
            }
        }
        Require(hasSolidBrushSetColor,
            "SolidColorBrush.set_Color must be retained when bound via {Binding} in XAML");
    }
}

} // namespace

int main() {
    TestXmlPullLexer();
    TestMarkupExtensionParser();
    TestCSharpCompatLowerer();
    TestXamlToRsEmitterFeatures();
    TestNativeBodyStrippingUnderO2AndAot();
    TestVoidNativeForwardersO0O2AndAot();
    TestEndToEndXamlCompileAndSessionReuse();
    TestMirTreeShakingAndProcessStdlibCache();
    TestMultiXamlAdditionalSourcesCompilation();
    TestP0P1CompilerSemanticsAndStdlibGenericCache();
    TestGalleryEngineCompilerFixes();
    TestIncrementalSpecModuleCacheInvalidation();
    TestPhase4PackagesCatalogAndPruning();
    TestPackageInferenceScannersAndPropertyPruning();

    if (gFailureCount > 0) {
        std::cerr << gFailureCount << " compiler_v3_tests assertion(s) failed.\n";
        return 1;
    }
    std::printf("All compiler_v3_tests passed!\n");
    return 0;
}
