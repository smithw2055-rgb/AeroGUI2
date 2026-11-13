#include <fstream>
#include "aero/core/UiResourceContext.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/XamlApplication.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace fs = std::filesystem;

namespace {

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const char* expr, int line) {
    if (cond) {
        ++gPassed;
    } else {
        ++gFailed;
        std::cerr << "[FAIL] line " << line << ": " << expr << std::endl;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestThreeStateCheckBoxAndIndeterminateRender() {
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    auto cb = aero::MakeIntrusive<aero::CheckBox>();
    cb->SetContentText("TriState");
    cb->SetValue(aero::PropId_IsThreeState, aero::UnifiedValue{true}, aero::ValueSourceRank::Local);
    cb->SetIsChecked(false);
    root->AddChild(cb.Get());

    CHECK(!cb->IsChecked());
    cb->Toggle(); // false -> true
    CHECK(cb->IsChecked());
    cb->Toggle(); // true -> NullObject (Indeterminate)
    CHECK(std::holds_alternative<aero::NullObject>(cb->GetValue(aero::PropId_IsChecked)));
    cb->Toggle(); // NullObject -> false
    CHECK(!cb->IsChecked());
    CHECK(std::holds_alternative<bool>(cb->GetValue(aero::PropId_IsChecked)));
}

void TestDeclarativeTreeViewAndSelectedItemSubHopBinding() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="400" Height="300">
  <StackPanel>
    <TreeView x:Name="Tree1">
      <TreeViewItem x:Name="RootNode" Header="Root Category" IsExpanded="True">
        <TreeViewItem x:Name="LeafNode" Header="Child Item A"/>
      </TreeViewItem>
    </TreeView>
    <TextBlock x:Name="SelectedReadout" Text="{Binding SelectedItem.Header, ElementName=Tree1}"/>
  </StackPanel>
</Grid>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "TreeTest.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    auto root = aero::MakeIntrusive<aero::Grid>();
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));

    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    auto* tree = root->FindName("Tree1")->DynamicCast<aero::TreeView>();
    auto* leaf = root->FindName("LeafNode")->DynamicCast<aero::TreeViewItem>();
    auto* readout = root->FindName("SelectedReadout")->DynamicCast<aero::TextBlock>();
    CHECK(tree != nullptr && leaf != nullptr && readout != nullptr);

    tree->SelectContainer(leaf);
    (void)binding.FlushTrackA();
    if (readout->GetText() != "Child Item A") std::cerr << "  readout=" << readout->GetText() << std::endl;
    CHECK(readout->GetText() == "Child Item A");
}

void TestDynamicRunActualWidthAndGridSplitter() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="500" Height="200">
  <Grid.ColumnDefinitions>
    <ColumnDefinition Width="200"/>
    <ColumnDefinition Width="6"/>
    <ColumnDefinition Width="*"/>
  </Grid.ColumnDefinitions>
  <Border x:Name="LeftPane" Grid.Column="0"/>
  <GridSplitter x:Name="Splitter" Grid.Column="1" Width="6"/>
  <TextBlock x:Name="SizeLabel" Grid.Column="2">
    <Run Text="{Binding ActualWidth, ElementName=LeftPane, Mode=OneWay, StringFormat=F0}"/>x<Run Text="{Binding ActualHeight, ElementName=LeftPane, Mode=OneWay, StringFormat=F0}"/>
  </TextBlock>
</Grid>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "SplitterTest.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    auto root = aero::MakeIntrusive<aero::Grid>();
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
    root->Measure(aero::Size{500.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 200.0f});
    (void)binding.FlushTrackA();

    auto* label = root->FindName("SizeLabel")->DynamicCast<aero::TextBlock>();
    auto* splitter = root->FindName("Splitter")->DynamicCast<aero::GridSplitter>();
    CHECK(label != nullptr && splitter != nullptr);
    if (label->GetText() != "200x200") std::cerr << "  label(before)=" << label->GetText() << std::endl;
    CHECK(label->GetText() == "200x200");

    bool cap = false;
    CHECK(splitter->OnPointerDown(aero::Point{3.0f, 50.0f}, aero::PointerButton::Left, 0, cap));
    CHECK(splitter->OnPointerMove(aero::Point{43.0f, 50.0f}, 0));
    (void)splitter->OnPointerUp(aero::Point{43.0f, 50.0f}, aero::PointerButton::Left, 0, true);
    root->Measure(aero::Size{500.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 200.0f});
    (void)binding.FlushTrackA();
    if (label->GetText() != "240x200") std::cerr << "  label(after)=" << label->GetText() << std::endl;
    CHECK(label->GetText() == "240x200");
}

void TestContextMenuAndRemoveElementAction() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
      Width="400" Height="300">
  <StackPanel x:Name="CardsPanel">
    <Border x:Name="CardA" Width="200" Height="60" Background="#334455">
      <Border.ContextMenu>
        <ContextMenu>
          <MenuItem x:Name="DeleteMi" Header="Delete Card">
            <b:Interaction.Triggers>
              <b:EventTrigger EventName="Click">
                <b:RemoveElementAction TargetObject="{Binding PlacementTarget, RelativeSource={RelativeSource AncestorType=ContextMenu}}"/>
              </b:EventTrigger>
            </b:Interaction.Triggers>
          </MenuItem>
        </ContextMenu>
      </Border.ContextMenu>
    </Border>
  </StackPanel>
</Grid>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "CtxMenuTest.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::AnimationEngine anim;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    aero::StyleTriggerEngine style;
    style.SetAnimationEngine(&anim);
    style.SetBindingEngine(&binding);
    style.SetDomains(&domains, res.programImage.get(), nullptr);
    style.ImportRegistryStyles();
    style.RegisterPackageStyles(res.axbPackage);

    auto root = aero::MakeIntrusive<aero::Grid>();
    root->BindTwoWayEngine(&binding);
    root->BindStyleEngine(&style);
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding, {}, nullptr, nullptr, &anim));
    (void)style.BindSubtreeStyles(root.Get(), true);
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    aero::InputEventRouter router(root.Get());
    router.SetAnimationEngine(&anim);
    CHECK(router.DispatchPointerDown(aero::Point{200.0f, 30.0f}, aero::PointerButton::Right, 0));
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});
    (void)style.EvaluateAndApply();

    auto* rawDeleteMi = root->FindName("DeleteMi");
    auto* deleteMi = rawDeleteMi ? rawDeleteMi->DynamicCast<aero::MenuItem>() : nullptr;
    CHECK(deleteMi != nullptr);
    if (deleteMi) CHECK(deleteMi->ActivateFromInput());
    (void)style.EvaluateAndApply();

    auto* cardsPanel = root->FindName("CardsPanel");
    CHECK(cardsPanel != nullptr);
    CHECK(cardsPanel->FirstChild() == nullptr);
}

void TestWpfQuickStartEndToEndApplication() {
    const fs::path sampleDir = fs::path(AERO_SAMPLES_DIR) / "WpfQuickStart";
    aero::XamlApplication app;
    std::string err;
    CHECK(app.Load(sampleDir.string(), &err));
    if (!err.empty()) {
        std::cerr << "XamlApplication Load diagnostic: " << err << std::endl;
    }
    CHECK(app.StartupUri() == "MainWindow.xaml");
    CHECK(app.MainWindowElement() != nullptr);

    auto* banner = app.MainWindowElement()->FindName("HeaderBanner")->DynamicCast<aero::TextBlock>();
    auto* inputBox = app.MainWindowElement()->FindName("NewTaskInput")->DynamicCast<aero::TextBox>();
    auto* quickBtn = app.MainWindowElement()->FindName("QuickFillButton")->DynamicCast<aero::Button>();
    auto* addBtn = app.MainWindowElement()->FindName("AddTaskButton")->DynamicCast<aero::Button>();
    auto* statusItem = app.MainWindowElement()->FindName("StatusTextItem")->DynamicCast<aero::StatusBarItem>();
    auto* paneSize = app.MainWindowElement()->FindName("PaneSizeReadout")->DynamicCast<aero::TextBlock>();
    auto* tasksList = app.MainWindowElement()->FindName("TasksList")->DynamicCast<aero::ListBox>();

    CHECK(banner != nullptr && inputBox != nullptr && quickBtn != nullptr && addBtn != nullptr);
    CHECK(statusItem != nullptr && paneSize != nullptr && tasksList != nullptr);
    CHECK(paneSize->GetText().find("x") != std::string::npos);

    // Click QuickFillButton -> triggers C# (object sender, RoutedEventArgs e) + `sender is Button btn` + typed `NewTaskInput.Text = ...`!
    CHECK(quickBtn->ActivateFromInput());
    app.Tick(0.016f);
    CHECK(inputBox->GetText() == "Shipped via Quick Fill");

    // Click AddTaskButton -> invokes RelayCommand `AddTaskCommand` -> mutates ObservableCollection<string> `ActiveTasks` + `StatusMessage`!
    CHECK(addBtn->ActivateFromInput());
    app.Tick(0.016f);
    CHECK(statusItem->GetEffectiveText().find("4 total") != std::string::npos);

    // Verify ListView + GridView, RelayCommand<string> CommandParameter, KeyBinding (Ctrl+N), ToolBar.Header & MenuItem.IsCheckable
    auto* tasksGrid = app.MainWindowElement()->FindName("TasksGrid")->DynamicCast<aero::ListView>();
    auto* presetBtn = app.MainWindowElement()->FindName("QuickPresetButton")->DynamicCast<aero::Button>();
    auto* toolBar = app.MainWindowElement()->FindName("MainToolBar")->DynamicCast<aero::ToolBar>();
    auto* compactMi = app.MainWindowElement()->FindName("CompactModeMenuItem")->DynamicCast<aero::MenuItem>();
    CHECK(tasksGrid != nullptr && tasksGrid->HasGridViewHeader() && tasksGrid->Items().size() == 4u);
    CHECK(toolBar != nullptr && toolBar->GetHeaderText() == "Actions");
    CHECK(compactMi != nullptr && compactMi->IsCheckable() && !compactMi->IsChecked());

    if (compactMi) {
        CHECK(compactMi->ActivateFromInput());
        CHECK(compactMi->IsChecked());
    }
    if (presetBtn) {
        CHECK(presetBtn->ActivateFromInput());
        app.Tick(0.016f);
        CHECK(statusItem->GetEffectiveText().find("Urgent Hotfix") != std::string::npos);
        CHECK(statusItem->GetEffectiveText().find("5 total") != std::string::npos);
        if (tasksGrid) CHECK(tasksGrid->Items().size() == 5u);
    }

    // Trigger <Window.InputBindings><KeyBinding Key="N" Modifiers="Ctrl" Command="{Binding QuickAddCommand}" CommandParameter="Shortcut Task"/>
    CHECK(app.InputRouter().DispatchKeyDown(aero::KeyCode::N, aero::KeyMod_Ctrl));
    app.Tick(0.016f);
    CHECK(statusItem->GetEffectiveText().find("Shortcut Task") != std::string::npos);
    CHECK(statusItem->GetEffectiveText().find("6 total") != std::string::npos);
    if (tasksGrid) CHECK(tasksGrid->Items().size() == 6u);
}

void TestDeclarativeTreeViewItemIsSelectedOnMount() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="400" Height="300">
  <StackPanel>
    <TreeView x:Name="Tree1">
      <TreeViewItem x:Name="RootNode" Header="Root Category" IsExpanded="True">
        <TreeViewItem x:Name="PreSelectedLeaf" Header="Preselected Leaf" IsSelected="True"/>
      </TreeViewItem>
    </TreeView>
    <TextBlock x:Name="SelectedReadout" Text="{Binding SelectedItem.Header, ElementName=Tree1}"/>
    <TextBlock x:Name="DirectSelectedReadout" Text="{Binding SelectedItem, ElementName=Tree1}"/>
  </StackPanel>
</Grid>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "PreSelectedTreeTest.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    auto root = aero::MakeIntrusive<aero::Grid>();
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
    (void)binding.FlushTrackA();

    auto* tree = root->FindName("Tree1")->DynamicCast<aero::TreeView>();
    auto* leaf = root->FindName("PreSelectedLeaf")->DynamicCast<aero::TreeViewItem>();
    auto* readout = root->FindName("SelectedReadout")->DynamicCast<aero::TextBlock>();
    auto* directReadout = root->FindName("DirectSelectedReadout")->DynamicCast<aero::TextBlock>();
    CHECK(tree != nullptr && leaf != nullptr && readout != nullptr && directReadout != nullptr);
    CHECK(tree->SelectedContainer() == leaf);
    CHECK(readout->GetText() == "Preselected Leaf");
    CHECK(directReadout->GetText() == "Preselected Leaf");
}

void TestHierarchicalDataTemplateRuntime() {
    const char* xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="HierDemo.MainWindow"
        Width="400" Height="300">
  <Window.Resources>
    <HierarchicalDataTemplate x:Key="NodeTemplate" ItemsSource="{Binding Children}">
      <TextBlock Text="{Binding Title}"/>
    </HierarchicalDataTemplate>
  </Window.Resources>
  <StackPanel>
    <TreeView x:Name="OrgTree" ItemsSource="{Binding Roots}" ItemTemplate="{StaticResource NodeTemplate}"/>
    <TextBlock x:Name="SelectedNodeTitle" Text="{Binding SelectedItem.Title, ElementName=OrgTree}"/>
  </StackPanel>
</Window>
)XAML";

    const char* script = R"RS(
using System.Collections.ObjectModel;

namespace HierDemo
{
    public class TreeNode : ObservableObject
    {
        public string Title { get; set; } = "";
        public ObservableCollection<TreeNode> Children { get; set; } = new ObservableCollection<TreeNode>();

        public TreeNode() { }
        public TreeNode(string title)
        {
            Title = title;
        }
    }
}

namespace HierDemo
{
    public partial class MainWindow : Window
    {
        public ObservableCollection<TreeNode> Roots { get; set; } = new ObservableCollection<TreeNode>();

        public MainWindow()
        {
            InitializeComponent();
            TreeNode eng = new TreeNode("Engineering");
            TreeNode uiTeam = new TreeNode("UI Engine");
            uiTeam.Children.Add(new TreeNode("Compiler"));
            eng.Children.Add(uiTeam);
            Roots.Add(eng);
            SetDataContext(this);
        }
    }

    public MainWindow CreateMainWindow()
    {
        return new MainWindow();
    }
}
)RS";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "MainWindow.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back(aero::SourceFile{"MainWindow.xaml.cs", script});
    auto res = compiler.Compile(in);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) std::cerr << "  diag: " << d << std::endl;
    }
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    auto nativeBindings = std::make_shared<aero::BindingRegistry>();
    aero::ScriptUiBridge bridge(domains);
    bridge.RegisterCoreUiBindings(*nativeBindings);
    aero::Interpreter interp(res.programImage, domains.uiHeapPtr());
    interp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
    interp.setBindingRegistry(nativeBindings);
    aero::DualTrackBindingEngine binding(domains, res.programImage.get(), &interp);
    bridge.SetUnderlyingEngine(&binding);
    aero::StyleTriggerEngine style;

    auto createSym = res.programImage->findFunction("HierDemo::CreateMainWindow");
    CHECK(createSym.has_value());
    auto execRes = interp.invoke(*createSym, {});
    CHECK(execRes.succeeded);
    const auto* mainWinRef = std::get_if<aero::ObjectRef>(&execRes.value);
    CHECK(mainWinRef != nullptr && mainWinRef->valid());

    auto root = aero::MountAndWireMainWindow(
        res.axbPackage,
        domains,
        binding,
        style,
        *mainWinRef,
        &interp);
    CHECK(root.Get() != nullptr);

    auto* tree = root->FindName("OrgTree")->DynamicCast<aero::TreeView>();
    auto* selTitle = root->FindName("SelectedNodeTitle")->DynamicCast<aero::TextBlock>();
    CHECK(tree != nullptr && selTitle != nullptr);

    // Root level should have 1 TreeViewItem ("Engineering")
    aero::TreeViewItem* engItem = nullptr;
    for (aero::UIElement* ch = tree->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
        if (auto* tvi = ch->DynamicCast<aero::TreeViewItem>()) {
            engItem = tvi;
            break;
        }
    }
    CHECK(engItem != nullptr);
    if (engItem) {
        CHECK(engItem->GetHeader() == "Engineering");
        // Second level should have 1 child TreeViewItem ("UI Engine")
        aero::TreeViewItem* uiTeamItem = nullptr;
        for (aero::UIElement* ch = engItem->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
            if (auto* tvi = ch->DynamicCast<aero::TreeViewItem>()) {
                uiTeamItem = tvi;
                break;
            }
        }
        CHECK(uiTeamItem != nullptr);
        if (uiTeamItem) {
            CHECK(uiTeamItem->GetHeader() == "UI Engine");
            // Third level should have 1 child TreeViewItem ("Compiler")
            aero::TreeViewItem* compilerItem = nullptr;
            for (aero::UIElement* ch = uiTeamItem->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
                if (auto* tvi = ch->DynamicCast<aero::TreeViewItem>()) {
                    compilerItem = tvi;
                    break;
                }
            }
            CHECK(compilerItem != nullptr);
            if (compilerItem) {
                CHECK(compilerItem->GetHeader() == "Compiler");
                tree->SelectContainer(compilerItem);
                (void)binding.FlushTrackA();
                CHECK(selTitle->GetText() == "Compiler");
            }
        }
    }
}

void TestCtorNamedControlAccessAndCollectionMoveBounds() {
    const char* xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="CtorDemo.MainWindow"
        Width="400" Height="200">
  <StackPanel>
    <TextBlock x:Name="CtorBanner" Text="Default XAML Text"/>
    <ListBox x:Name="ItemsBox" ItemsSource="{Binding Labels}"/>
  </StackPanel>
</Window>
)XAML";

    // Standalone ViewModel using ONLY System.Collections.ObjectModel and multiple braced namespace blocks
    const char* vmScript = R"RS(
using System.Collections.ObjectModel;

namespace CtorDemo
{
    public class ItemsHelper
    {
        public static string FirstLabel() { return "First"; }
    }
}

namespace CtorDemo
{
    public class ItemsVm
    {
        public ObservableCollection<string> Labels { get; set; } = new ObservableCollection<string>();

        public ItemsVm()
        {
            Labels.Add(ItemsHelper.FirstLabel());
            Labels.Add("Second");
            // Out-of-bounds Move calls should be safely ignored without corrupting the collection
            Labels.Move(-1, 1);
            Labels.Move(0, 99);
            Labels.Move(99, 0);
            Labels.Move(0, 0);
            // Valid Move(0, 1) swaps "First" and "Second"
            Labels.Move(0, 1);
        }
    }
} // trailing comment with } brace
)RS";

    const char* winScript = R"RS(
using System.Windows;

namespace CtorDemo
{
    public partial class MainWindow : Window
    {
        public ItemsVm ViewModel { get; set; } = new ItemsVm();

        public MainWindow()
        {
            InitializeComponent();
            // Mutate x:Name control directly inside constructor right after InitializeComponent()!
            this.CtorBanner.Text = "Configured Inside Constructor";
            SetDataContext(this.ViewModel);
        }
    }

    public MainWindow CreateMainWindow()
    {
        return new MainWindow();
    }
}
)RS";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "MainWindow.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back(aero::SourceFile{"ItemsVm.cs", vmScript});
    in.scriptSources.push_back(aero::SourceFile{"MainWindow.xaml.cs", winScript});
    auto res = compiler.Compile(in);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) std::cerr << "  diag: " << d << std::endl;
    }
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    auto nativeBindings = std::make_shared<aero::BindingRegistry>();
    aero::ScriptUiBridge bridge(domains);
    bridge.RegisterCoreUiBindings(*nativeBindings);
    aero::Interpreter interp(res.programImage, domains.uiHeapPtr());
    interp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
    interp.setBindingRegistry(nativeBindings);
    aero::DualTrackBindingEngine binding(domains, res.programImage.get(), &interp);
    bridge.SetUnderlyingEngine(&binding);
    aero::StyleTriggerEngine style;

    auto createSym = res.programImage->findFunction("CtorDemo::CreateMainWindow");
    CHECK(createSym.has_value());
    auto execRes = interp.invoke(*createSym, {});
    CHECK(execRes.succeeded);
    const auto* mainWinRef = std::get_if<aero::ObjectRef>(&execRes.value);
    CHECK(mainWinRef != nullptr && mainWinRef->valid());

    auto root = aero::MountAndWireMainWindow(
        res.axbPackage,
        domains,
        binding,
        style,
        *mainWinRef,
        &interp);
    CHECK(root.Get() != nullptr);

    auto* banner = root->FindName("CtorBanner")->DynamicCast<aero::TextBlock>();
    auto* listBox = root->FindName("ItemsBox")->DynamicCast<aero::ListBox>();
    CHECK(banner != nullptr && listBox != nullptr);
    if (banner) {
        if (banner->GetText() != "Configured Inside Constructor") {
            std::cerr << "  banner=" << banner->GetText() << std::endl;
        }
        CHECK(banner->GetText() == "Configured Inside Constructor");
    }
    if (listBox) {
        CHECK(listBox->Items().size() == 2u);
        if (listBox->Items().size() == 2u) {
            const auto* s0 = std::get_if<aero::StringValue>(&listBox->Items()[0]);
            const auto* s1 = std::get_if<aero::StringValue>(&listBox->Items()[1]);
            CHECK(s0 != nullptr && s0->view() == "Second");
            CHECK(s1 != nullptr && s1->view() == "First");
        }
    }
}

void TestHorizontalGridSplitterAndStarPreservation() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="300" Height="400">
  <Grid.RowDefinitions>
    <RowDefinition Height="120"/>
    <RowDefinition Height="6"/>
    <RowDefinition Height="*"/>
  </Grid.RowDefinitions>
  <Border x:Name="TopPane" Grid.Row="0"/>
  <GridSplitter x:Name="RowSplitter" Grid.Row="1" Height="6" HorizontalAlignment="Stretch"/>
  <Border x:Name="BottomPane" Grid.Row="2"/>
</Grid>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "RowSplitterTest.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    auto root = aero::MakeIntrusive<aero::Grid>();
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
    root->Measure(aero::Size{300.0f, 400.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 400.0f});

    auto* splitter = root->FindName("RowSplitter")->DynamicCast<aero::GridSplitter>();
    auto* topPane = root->FindName("TopPane");
    auto* botPane = root->FindName("BottomPane");
    CHECK(splitter != nullptr && topPane != nullptr && botPane != nullptr);

    bool cap = false;
    CHECK(splitter->OnPointerDown(aero::Point{100.0f, 3.0f}, aero::PointerButton::Left, 0, cap));
    CHECK(splitter->OnPointerMove(aero::Point{100.0f, 33.0f}, 0)); // dy = +30
    (void)splitter->OnPointerUp(aero::Point{100.0f, 33.0f}, aero::PointerButton::Left, 0, true);

    root->Measure(aero::Size{300.0f, 400.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 400.0f});

    CHECK(std::fabs(topPane->FinalRect().height - 150.0f) < 1.0f);
    // Row 2 should remain Star-sized and adapt when the Grid height changes to 500!
    CHECK(root->Rows()[2].unit == aero::GridUnitType::Star);
    root->SetHeight(500.0f);
    root->Measure(aero::Size{300.0f, 500.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 500.0f});
    CHECK(std::fabs(botPane->FinalRect().height - (500.0f - 150.0f - 6.0f)) < 1.0f);
}

void TestConcurrentRasterizeTreeToRgba8() {
    constexpr int kThreads = 4;
    constexpr int kIters = 8;
    std::vector<std::thread> workers;
    std::atomic<int> failures{0};

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&failures, t, kIters]() {
            aero::PagedRenderTree tree;
            auto root = aero::MakeIntrusive<aero::Border>();
            root->SetWidth(512.0f);
            root->SetHeight(512.0f);
            root->SetBackground(aero::Color{0.2f, 0.4f, 0.8f, 1.0f});
            root->Measure(aero::Size{512.0f, 512.0f});
            root->Arrange(aero::Rect{0.0f, 0.0f, 512.0f, 512.0f});
            (void)tree.BuildFromVisualTree(root.Get());

            std::vector<std::uint8_t> pixels;
            for (int i = 0; i < kIters; ++i) {
                aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
                    tree,
                    512u,
                    512u,
                    pixels,
                    aero::Color{0.1f, 0.1f, 0.1f, 1.0f});
                if (pixels.size() != 512u * 512u * 4u ||
                    pixels[(256u * 512u + 256u) * 4u + 3u] != 255u) {
                    failures.fetch_add(1);
                }
            }
            (void)t;
        });
    }
    for (auto& th : workers) {
        th.join();
    }
    CHECK(failures.load() == 0);
}


void TestStrongTypedEventArgsAndMultiEventCheckBox() {
    const char* xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="TypedEventsDemo.MainWindow"
        Width="480" Height="320"
        KeyDown="Window_KeyDown">
  <StackPanel Spacing="8">
    <TextBox x:Name="SearchBox" Text="" TextChanged="SearchBox_TextChanged"/>
    <CheckBox x:Name="GateCheck" IsThreeState="True"
              Checked="Gate_Checked"
              Unchecked="Gate_Unchecked"
              Indeterminate="Gate_Indeterminate"/>
    <TextBlock x:Name="LogReadout" Text="{Binding LogText}"/>
  </StackPanel>
</Window>
)XAML";

    const char* script = R"CS(
using System;
using System.Windows;
using Aero.UI;

namespace TypedEventsDemo
{
    public partial class MainWindow : Window
    {
        public string LogText { get; set; } = "init";

        public MainWindow()
        {
            InitializeComponent();
            DataContext = this;
        }

        public void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            LogText = "txt:" + e.Text;
        }

        public void Window_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.IsCtrlDown)
            {
                LogText = "ctrl+" + e.Key;
            }
        }

        public void Gate_Checked(object sender, RoutedEventArgs e)
        {
            LogText = "state:checked";
        }

        public void Gate_Unchecked(object sender, RoutedEventArgs e)
        {
            LogText = "state:unchecked";
        }

        public void Gate_Indeterminate(object sender, RoutedEventArgs e)
        {
            LogText = "state:indeterminate";
        }
    }
}
)CS";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "MainWindow.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back(aero::SourceFile{"MainWindow.xaml.cs", script});
    auto res = compiler.Compile(in);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) std::cerr << "  diag: " << d << std::endl;
    }
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    auto nativeBindings = std::make_shared<aero::BindingRegistry>();
    aero::ScriptUiBridge bridge(domains);
    bridge.RegisterCoreUiBindings(*nativeBindings);
    aero::Interpreter interp(res.programImage, domains.uiHeapPtr());
    interp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
    interp.setBindingRegistry(nativeBindings);
    aero::DualTrackBindingEngine binding(domains, res.programImage.get(), &interp);
    bridge.SetUnderlyingEngine(&binding);
    aero::StyleTriggerEngine style;

    // Notice: MainWindow.xaml.cs did NOT define CreateMainWindow() — compiler auto-emitted it!
    auto createSym = res.programImage->findFunction("TypedEventsDemo::CreateMainWindow");
    CHECK(createSym.has_value());
    if (!createSym) return;
    auto execRes = interp.invoke(*createSym, {});
    CHECK(execRes.succeeded);
    const auto* mainWinRef = std::get_if<aero::ObjectRef>(&execRes.value);
    CHECK(mainWinRef != nullptr && mainWinRef->valid());
    auto pin = domains.uiHeap().retain(*mainWinRef);

    auto root = aero::MountAndWireMainWindow(
        res.axbPackage, domains, binding, style, *mainWinRef, &interp);
    CHECK(root.Get() != nullptr);
    root->Measure(aero::Size{480.0f, 320.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 480.0f, 320.0f});

    auto* searchBox = root->FindName("SearchBox")->DynamicCast<aero::TextBox>();
    auto* gateCheck = root->FindName("GateCheck")->DynamicCast<aero::CheckBox>();
    auto* logReadout = root->FindName("LogReadout")->DynamicCast<aero::TextBlock>();
    CHECK(searchBox != nullptr && gateCheck != nullptr && logReadout != nullptr);
    CHECK(logReadout->GetText() == "init");

    // 1. TextChangedEventArgs
    aero::InputEventRouter router(root.Get());
    CHECK(router.SetFocus(searchBox));
    CHECK(searchBox->OnTextInput("Aero"));
    (void)binding.FlushTrackA();
    CHECK(logReadout->GetText() == "txt:Aero");

    // 2. Multi-event CheckBox: Unchecked -> Checked -> Indeterminate -> Unchecked
    CHECK(gateCheck->ActivateFromInput());
    (void)binding.FlushTrackA();
    CHECK(logReadout->GetText() == "state:checked");

    CHECK(gateCheck->ActivateFromInput());
    (void)binding.FlushTrackA();
    CHECK(logReadout->GetText() == "state:indeterminate");

    CHECK(gateCheck->ActivateFromInput());
    (void)binding.FlushTrackA();
    CHECK(logReadout->GetText() == "state:unchecked");

    // 3. KeyEventArgs with Ctrl+K
    (void)router.DispatchKeyDown(aero::KeyCode::K, aero::KeyMod_Ctrl);
    (void)binding.FlushTrackA();
    CHECK(logReadout->GetText() == "ctrl+K");
}

void TestRepeatButtonAutoRepeatMenuAutoCloseAndTreeKeyboard() {
    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            Width="400" Height="300">
  <Menu x:Name="TopMenu" Height="28">
    <MenuItem x:Name="FileMenu" Header="File">
      <MenuItem Header="New"/>
    </MenuItem>
    <MenuItem x:Name="EditMenu" Header="Edit">
      <MenuItem Header="Copy"/>
    </MenuItem>
  </Menu>
  <RepeatButton x:Name="HoldBtn" Content="+" Width="80" Height="32" Delay="200" Interval="50" ToolTip="Hold to increment"/>
  <TreeView x:Name="KeyTree" Height="160">
    <TreeViewItem x:Name="NodeA" Header="Alpha" IsExpanded="False" IsSelected="True">
      <TreeViewItem x:Name="NodeA1" Header="Alpha-1"/>
    </TreeViewItem>
    <TreeViewItem x:Name="NodeB" Header="Beta"/>
  </TreeView>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "InteractiveControls.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine binding(domains, res.programImage.get());
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    auto* fileMenu = root->FindName("FileMenu")->DynamicCast<aero::MenuItem>();
    auto* editMenu = root->FindName("EditMenu")->DynamicCast<aero::MenuItem>();
    auto* holdBtn = root->FindName("HoldBtn")->DynamicCast<aero::RepeatButton>();
    auto* keyTree = root->FindName("KeyTree")->DynamicCast<aero::TreeView>();
    auto* nodeA = root->FindName("NodeA")->DynamicCast<aero::TreeViewItem>();
    auto* nodeA1 = root->FindName("NodeA1")->DynamicCast<aero::TreeViewItem>();
    auto* nodeB = root->FindName("NodeB")->DynamicCast<aero::TreeViewItem>();
    CHECK(fileMenu && editMenu && holdBtn && keyTree && nodeA && nodeA1 && nodeB);

    // 1. Sibling MenuItem auto-close when switching top-level menus
    CHECK(fileMenu->ActivateFromInput());
    CHECK(fileMenu->IsSubmenuOpen());
    CHECK(!editMenu->IsSubmenuOpen());
    CHECK(editMenu->ActivateFromInput());
    CHECK(editMenu->IsSubmenuOpen());
    CHECK(!fileMenu->IsSubmenuOpen());
    CHECK(editMenu->ActivateFromInput()); // Close Edit submenu before hit-testing RepeatButton below it
    CHECK(!editMenu->IsSubmenuOpen());

    // 2. RepeatButton press-hold Delay (200ms) + Interval (50ms) via InputEventRouter::Tick
    int repeatClicks = 0;
    holdBtn->SetClickCallback([&](aero::Button&) { ++repeatClicks; });
    aero::InputEventRouter router(root.Get());
    const aero::Rect btnWorld = holdBtn->FinalRect();
    const aero::Point btnCenter{btnWorld.x + btnWorld.width * 0.5f, btnWorld.y + btnWorld.height * 0.5f};
    (void)router.DispatchPointerMove(btnCenter);
    CHECK(router.HoveredElement() == holdBtn);
    CHECK(router.DispatchPointerDown(btnCenter, aero::PointerButton::Left));
    CHECK(repeatClicks == 1); // Initial press fires 1st click on RepeatButton
    router.Tick(0.15f);
    CHECK(repeatClicks == 1); // Still within 200ms Delay
    router.Tick(0.06f);       // Total 210ms >= 200ms Delay -> fires 2nd click!
    CHECK(repeatClicks == 2);
    router.Tick(0.055f);      // +55ms >= 50ms Interval -> fires 3rd click!
    CHECK(repeatClicks == 3);
    (void)router.DispatchPointerUp(btnCenter, aero::PointerButton::Left);
    router.Tick(0.10f);
    CHECK(repeatClicks == 3); // Stopped repeating on release!

    // 3. TreeView keyboard navigation: Right expands NodeA, Down selects NodeA1, Down selects NodeB
    CHECK(router.SetFocus(keyTree));
    CHECK(!nodeA->IsExpanded());
    CHECK(router.DispatchKeyDown(aero::KeyCode::Right));
    CHECK(nodeA->IsExpanded());
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});
    CHECK(router.DispatchKeyDown(aero::KeyCode::Down));
    CHECK(keyTree->SelectedContainer() == nodeA1);
    CHECK(router.DispatchKeyDown(aero::KeyCode::Down));
    CHECK(keyTree->SelectedContainer() == nodeB);
}

void TestGalleryEnhancedXamlSamples() {
    const fs::path galleryData = fs::path(AERO_SAMPLES_DIR) / "Gallery" / "Data";
    const fs::path samplesDir = galleryData / "Samples";
    aero::UiResourceContext::Current().ClearXamlSearchDirectories();
    aero::UiResourceContext::Current().AddXamlSearchDirectory(galleryData.string());
    aero::UiResourceContext::Current().AddXamlSearchDirectory(samplesDir.string());

    auto countType = [](auto& self, aero::UIElement* e, int& lvItems, int& icPop, int& tcMulti, int& tvPop) -> void {
        if (!e) return;
        if (auto* lv = e->DynamicCast<aero::ListView>()) {
            lvItems += static_cast<int>(lv->Items().size());
        } else if (auto* ic = e->DynamicCast<aero::ItemsControl>()) {
            if (!ic->Items().empty() || ic->FirstChild() != nullptr) ++icPop;
        }
        if (auto* tc = e->DynamicCast<aero::TabControl>()) {
            int ch = 0;
            for (auto* c = tc->FirstChild(); c; c = c->NextSibling()) ++ch;
            if (ch > 1 || !tc->Items().empty()) ++tcMulti;
        }
        if (auto* tv = e->DynamicCast<aero::TreeView>()) {
            if (tv->FirstChild() != nullptr) ++tvPop;
        }
        for (auto* c = e->FirstChild(); c; c = c->NextSibling()) {
            self(self, c, lvItems, icPop, tcMulti, tvPop);
        }
    };

    auto verifySample = [&](const char* fileName, int& lvItems, int& icPop, int& tcMulti, int& tvPop) {
        lvItems = icPop = tcMulti = tvPop = 0;
        std::ifstream ifs(samplesDir / fileName);
        std::string xaml((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        aero::AeroCompiler compiler;
        aero::AeroCompileInput in{};
        in.xamlPath = fileName;
        in.xamlContent = xaml;
        auto res = compiler.Compile(in);
        CHECK(res.succeeded);
        CHECK(res.diagnostics.empty());
        aero::ExecutionDomainContext domains;
        aero::DualTrackBindingEngine binding(domains, res.programImage.get());
        aero::StyleTriggerEngine style;
        style.SetBindingEngine(&binding);
        auto root = aero::MakeIntrusive<aero::Border>();
        CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
        (void)style.BindSubtreeStyles(root.Get(), true);
        (void)binding.FlushTrackA();
        (void)style.EvaluateAndApply();
        root->Measure(aero::Size{1280.0f, 720.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 1280.0f, 720.0f});
        countType(countType, root.Get(), lvItems, icPop, tcMulti, tvPop);
        root.Reset();
    };

    int lvItems = 0, icPop = 0, tcMulti = 0, tvPop = 0;
    verifySample("ListView.xaml", lvItems, icPop, tcMulti, tvPop);
    CHECK(lvItems == 15);
    verifySample("ItemsControl.xaml", lvItems, icPop, tcMulti, tvPop);
    CHECK(icPop >= 3);
    verifySample("ListBox.xaml", lvItems, icPop, tcMulti, tvPop);
    CHECK(icPop >= 3);
    verifySample("TabControl.xaml", lvItems, icPop, tcMulti, tvPop);
    CHECK(tcMulti >= 2);
    verifySample("TreeView.xaml", lvItems, icPop, tcMulti, tvPop);
    CHECK(tvPop >= 2);
}

void TestVisualResourceItemsSourceLifecycleAndLeakCheck() {
    const std::uint64_t baseLive = aero::UIElement::LiveInstanceCount();
    {
        const char* xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="400" Height="300">
  <Window.Resources>
    <StackPanel x:Key="ItemsA">
      <TextBlock Text="ItemA1"/>
      <TextBlock Text="ItemA2"/>
    </StackPanel>
    <StackPanel x:Key="ItemsB">
      <TextBlock Text="ItemB1"/>
    </StackPanel>
  </Window.Resources>
  <StackPanel>
    <TabControl x:Name="TC" ItemsSource="{StaticResource ItemsA}"/>
    <ItemsControl x:Name="IC" ItemsSource="{StaticResource ItemsA}"/>
    <ItemsControl x:Name="IC2" ItemsSource="{StaticResource ItemsB}"/>
  </StackPanel>
</Window>
)XAML";
        aero::AeroCompiler compiler;
        aero::AeroCompileInput in{};
        in.xamlPath = "VisualResLifecycle.xaml";
        in.xamlContent = xaml;
        auto res = compiler.Compile(in);
        CHECK(res.succeeded);
        if (!res.succeeded) return;

        aero::ExecutionDomainContext domains;
        aero::DualTrackBindingEngine binding(domains, res.programImage.get());
        aero::StyleTriggerEngine style;
        style.SetBindingEngine(&binding);
        auto root = aero::CreateElementByTypeName(res.axbPackage.rootTypeName);
        CHECK(root.Get() != nullptr);
        CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &binding));
        (void)style.BindSubtreeStyles(root.Get(), true);
        (void)binding.FlushTrackA();
        root->Measure(aero::Size{400.0f, 300.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

        auto* tc = dynamic_cast<aero::TabControl*>(root->FindName("TC"));
        auto* ic = dynamic_cast<aero::ItemsControl*>(root->FindName("IC"));
        auto* ic2 = dynamic_cast<aero::ItemsControl*>(root->FindName("IC2"));
        CHECK(tc != nullptr && ic != nullptr && ic2 != nullptr);
        if (tc && ic && ic2) {
            CHECK(tc->TabCount() == 2u);
            CHECK(ic->Items().size() == 2u);
            const aero::UnifiedValue itemsBVal = ic2->GetValue(aero::PropId_ItemsSource);
            tc->SetValue(aero::PropId_ItemsSource, itemsBVal, aero::ValueSourceRank::Local);
            ic->SetValue(aero::PropId_ItemsSource, itemsBVal, aero::ValueSourceRank::Local);
            CHECK(tc->TabCount() == 1u);
            CHECK(ic->Items().size() == 1u);

            ic->SetValue(aero::PropId_ItemsSource, aero::value::FromString("X|Y|Z"), aero::ValueSourceRank::Local);
            CHECK(ic->Items().size() == 3u);

            tc->SetValue(aero::PropId_ItemsSource, aero::UnifiedValue{aero::NullObject{}}, aero::ValueSourceRank::Local);
            ic->SetValue(aero::PropId_ItemsSource, aero::UnifiedValue{aero::NullObject{}}, aero::ValueSourceRank::Local);
            CHECK(tc->TabCount() == 0u);
            CHECK(ic->Items().size() == 0u);
        }
        root.Reset();
    }
    CHECK(aero::UIElement::LiveInstanceCount() == baseLive);
}

void TestListViewAutoWidthClickAndIncrementalSync() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="420" Height="220">
  <ListView x:Name="LV" ItemsSource="R0|R1|R2">
    <ListView.View>
      <GridView>
        <GridViewColumn Header="Name"/>
        <GridViewColumn Header="Kind" Width="Auto"/>
        <GridViewColumn Header="Val" Width="80"/>
      </GridView>
    </ListView.View>
  </ListView>
</Grid>
)XAML";
    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "ListViewAutoWidth.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    auto root = aero::CreateElementByTypeName(res.axbPackage.rootTypeName);
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage));
    root->Measure(aero::Size{420.0f, 220.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 420.0f, 220.0f});

    auto* lv = dynamic_cast<aero::ListView*>(root->FindName("LV"));
    CHECK(lv != nullptr);
    if (!lv) return;
    CHECK(lv->DesiredSize().width >= 200.0f);

    aero::InputEventRouter router(root.Get());
    // Click inside 26px GridView header should not select row 0
    (void)router.DispatchPointerDown(aero::Point{50.0f, 10.0f}, aero::PointerButton::Left);
    (void)router.DispatchPointerUp(aero::Point{50.0f, 10.0f}, aero::PointerButton::Left);
    CHECK(lv->GetSelectedIndex() == -1);

    // Click inside first data row below 26px header selects row 0
    (void)router.DispatchPointerDown(aero::Point{50.0f, 35.0f}, aero::PointerButton::Left);
    (void)router.DispatchPointerUp(aero::Point{50.0f, 35.0f}, aero::PointerButton::Left);
    CHECK(lv->GetSelectedIndex() == 0);

    aero::PagedRenderTree treeInc;
    const std::size_t beforeCount = treeInc.BuildFromVisualTree(root.Get());
    CHECK(beforeCount > 0u);

    lv->SetOpacity(0.85f);
    (void)treeInc.SyncVisualTree(root.Get());

    aero::PagedRenderTree treeFull;
    const std::size_t fullCount = treeFull.BuildFromVisualTree(root.Get());
    CHECK(treeInc.NodeCount() == fullCount);
}

void TestMenuItemIconIncrementalSyncMatchesFullBuild() {
    const char* xaml = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="320" Height="120">
  <Menu Height="32" VerticalAlignment="Top">
    <MenuItem x:Name="MI" Header="Open">
      <MenuItem.Icon>
        <Border Width="14" Height="14" Background="#FF3B82F6"/>
      </MenuItem.Icon>
      <MenuItem Header="Sub1"/>
    </MenuItem>
  </Menu>
</Grid>
)XAML";
    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "MenuIconInc.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    auto root = aero::CreateElementByTypeName(res.axbPackage.rootTypeName);
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage));
    root->Measure(aero::Size{320.0f, 120.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 120.0f});

    auto* mi = root->FindName("MI");
    CHECK(mi != nullptr);
    if (!mi) return;

    aero::PagedRenderTree treeInc;
    CHECK(treeInc.BuildFromVisualTree(root.Get()) > 0u);
    const std::uint32_t incStart = mi->RenderNodeId();
    const std::uint16_t incSpan = mi->RenderNodeSpan();
    CHECK(incStart != aero::UIElement::InvalidRenderNodeId && incSpan > 0u);

    mi->SetValue(
        aero::PropId_Background,
        aero::value::FromColor(aero::Color{0.25f, 0.35f, 0.55f, 1.0f}),
        aero::ValueSourceRank::Local);
    (void)treeInc.SyncVisualTree(root.Get());

    struct NodeGeom {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        aero::AeroPrimType prim = aero::AeroPrimType::SolidQuad;
    };
    std::vector<NodeGeom> incGeoms;
    for (std::uint16_t i = 0u; i < incSpan; ++i) {
        const auto* nd = treeInc.GetNode(incStart + i);
        CHECK(nd != nullptr);
        if (nd) incGeoms.push_back(NodeGeom{nd->x, nd->y, nd->width, nd->height, nd->primType});
    }

    aero::PagedRenderTree treeFull;
    CHECK(treeFull.BuildFromVisualTree(root.Get()) > 0u);
    CHECK(mi->RenderNodeSpan() == incSpan);
    for (std::uint16_t i = 0u; i < incGeoms.size() && i < mi->RenderNodeSpan(); ++i) {
        const auto* nd = treeFull.GetNode(mi->RenderNodeId() + i);
        CHECK(nd != nullptr);
        if (nd) {
            CHECK(std::fabs(nd->x - incGeoms[i].x) < 0.01f);
            CHECK(std::fabs(nd->y - incGeoms[i].y) < 0.01f);
            CHECK(std::fabs(nd->width - incGeoms[i].w) < 0.01f);
            CHECK(std::fabs(nd->height - incGeoms[i].h) < 0.01f);
            CHECK(nd->primType == incGeoms[i].prim);
        }
    }
}

void TestToolTipHoverStabilityAndLayoutIsolation() {
    const char* xaml = R"XAML(
<DockPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
           xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
           Width="400" Height="300" LastChildFill="True">
  <Grid x:Name="IsoGrid">
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <Border x:Name="Row0" Grid.Row="0" Height="25" Background="#FF334155"/>
    <Border x:Name="Row1" Grid.Row="1" Background="#FF1E293B"/>
    <ToolTip x:Name="DeclTip" Content="FloatTip"/>
  </Grid>
</DockPanel>
)XAML";
    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "ToolTipIsolation.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    auto root = aero::CreateElementByTypeName(res.axbPackage.rootTypeName);
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage));
    auto* row0 = root->FindName("Row0");
    auto* row1 = root->FindName("Row1");
    auto* declTip = dynamic_cast<aero::ToolTip*>(root->FindName("DeclTip"));
    CHECK(row0 != nullptr && row1 != nullptr && declTip != nullptr);
    if (!row0 || !row1 || !declTip) return;

    CHECK(!declTip->IsHitTestVisible());
    row0->SetValue(aero::PropId_ToolTipText, aero::value::FromString("Detailed Hover Tooltip"), aero::ValueSourceRank::Local);
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    CHECK(std::fabs(row0->FinalRect().height - 25.0f) < 0.01f);
    CHECK(std::fabs(row1->FinalRect().y - 25.0f) < 0.01f);
    CHECK(std::fabs(row1->FinalRect().height - 275.0f) < 0.01f);

    aero::InputEventRouter router(root.Get());
    (void)router.DispatchPointerMove(aero::Point{40.0f, 12.0f});
    router.Tick(0.6f);
    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    aero::ToolTip* autoTip = nullptr;
    for (aero::UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (auto* tt = c->DynamicCast<aero::ToolTip>(); tt && tt != declTip) {
            autoTip = tt;
            break;
        }
    }
    CHECK(autoTip != nullptr);
    if (autoTip) {
        CHECK(autoTip->IsOpen());
        CHECK(autoTip->GetVisibility() == aero::Visibility::Visible);
        CHECK(!autoTip->IsHitTestVisible());
        const aero::Rect tr = autoTip->FinalRect();
        CHECK(tr.x >= 0.0f && tr.y >= 0.0f && tr.x + tr.width <= 400.0f && tr.y + tr.height <= 300.0f);

        // Moving inside owner remains open instead of flickering
        (void)router.DispatchPointerMove(aero::Point{48.0f, 16.0f});
        router.Tick(0.05f);
        CHECK(autoTip->IsOpen());

        // Moving outside owner closes tooltip
        (void)router.DispatchPointerMove(aero::Point{100.0f, 200.0f});
        router.Tick(0.05f);
        CHECK(!autoTip->IsOpen());
    }
}

void TestNestedBlurAndBackdropBlurParallelRasterization() {
    const char* xaml = R"XAML(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions"
        Width="512" Height="512">
  <Border Canvas.Left="0" Canvas.Top="0" Width="512" Height="512" Background="#FF204080"/>
  <Border x:Name="OuterBlur" Canvas.Left="40" Canvas.Top="40" Width="320" Height="320" Background="#44102030">
    <Border x:Name="NestedBackdrop" Width="180" Height="180" Background="#66FFFFFF">
      <b:Interaction.Behaviors>
        <noesis:BackgroundEffectBehavior>
          <BlurEffect Radius="8"/>
        </noesis:BackgroundEffectBehavior>
      </b:Interaction.Behaviors>
      <Border Width="60" Height="60" Background="#FFE03030"/>
    </Border>
  </Border>
  <Border x:Name="TrailingBackdrop" Canvas.Left="280" Canvas.Top="280" Width="180" Height="180" Background="#5500FF80">
    <b:Interaction.Behaviors>
      <noesis:BackgroundEffectBehavior>
        <BlurEffect Radius="10"/>
      </noesis:BackgroundEffectBehavior>
    </b:Interaction.Behaviors>
    <Border Width="40" Height="40" Background="#FFFFFFFF"/>
  </Border>
</Canvas>
)XAML";
    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "NestedBlurBackdrop.xaml";
    in.xamlContent = xaml;
    auto res = compiler.Compile(in);
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    auto root = aero::CreateElementByTypeName(res.axbPackage.rootTypeName);
    CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage));
    auto* outerBlur = root->FindName("OuterBlur");
    CHECK(outerBlur != nullptr);
    if (outerBlur) {
        outerBlur->SetBlurEffect(6.0f);
    }

    root->Measure(aero::Size{512.0f, 512.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 512.0f, 512.0f});

    aero::PagedRenderTree tree;
    CHECK(tree.BuildFromVisualTree(root.Get()) > 0u);
    CHECK(tree.BlurLayerSpans().size() >= 3u);

    std::vector<std::uint8_t> pixels;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
        tree, 512u, 512u, pixels, aero::Color{0.05f, 0.05f, 0.05f, 1.0f});
    CHECK(pixels.size() == 512u * 512u * 4u);
    const std::size_t idxNested = (180u * 512u + 180u) * 4u;
    const std::size_t idxTrailing = (350u * 512u + 350u) * 4u;
    CHECK(pixels[idxNested + 3u] == 255u);
    CHECK(pixels[idxTrailing + 3u] == 255u);
    CHECK((pixels[idxTrailing] + pixels[idxTrailing + 1u] + pixels[idxTrailing + 2u]) > 0u);
}

void TestVerbatimStringsTypedCommandLoadedAndKeyDownHandled() {
    // Verify parameterized-only constructor code-behind compiles without bogus 0-arg factory error
    {
        const char* customCtorXaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="CustomCtorDemo.MainWindow"
        Width="200" Height="120">
  <TextBlock Text="Hi"/>
</Window>
)XAML";
        const char* customCtorCs = R"RS(
using System.Windows;
namespace CustomCtorDemo
{
    public partial class MainWindow : Window
    {
        public int Seed = 0;
        public MainWindow(int seed)
        {
            InitializeComponent();
            Seed = seed;
        }
    }
}
)RS";
        aero::AeroCompiler ctorCompiler;
        aero::AeroCompileInput ctorIn{};
        ctorIn.xamlPath = "CustomCtor.xaml";
        ctorIn.xamlContent = customCtorXaml;
        ctorIn.scriptSources.push_back(aero::SourceFile{"CustomCtor.xaml.cs", customCtorCs});
        auto ctorRes = ctorCompiler.Compile(ctorIn);
        CHECK(ctorRes.succeeded);
    }

    const char* xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="VerbatimDemo.MainWindow"
        Width="480" Height="320"
        Loaded="OnLoaded">
  <StackPanel Spacing="6">
    <TextBox x:Name="InputA" Text="alpha" KeyDown="InputA_KeyDown"/>
    <TextBox x:Name="InputB" Text="beta"/>
    <TextBlock x:Name="LoadedProbe" Text="probe_ready"/>
    <Button x:Name="Btn" Content="Run" Command="{Binding IntCmd}" CommandParameter="7"/>
    <TextBlock x:Name="Out" Text="{Binding Summary}"/>
  </StackPanel>
</Window>
)XAML";

    const char* script = R"CS(
using System;
using System.Windows;

namespace VerbatimDemo
{
    public class DemoVm : ViewModelBase
    {
        public string Summary { get; set; } = "init";
        // " unclosed quote in comment and RelayCommand<int> inside string below must not break rewrite:
        public string EscapedPath = "C:\\temp\\RelayCommand<int>\\";
        public RelayCommand<int> IntCmd { get; set; }

        public DemoVm()
        {
            IntCmd = new RelayCommand<int>(OnRun);
        }

        public void OnRun(int n)
        {
            Summary = "cmd:" + n;
        }
    }

    public partial class MainWindow : Window
    {
        public DemoVm Vm;

        public MainWindow()
        {
            InitializeComponent();
            Vm = new DemoVm();
            DataContext = Vm;
        }

        public void OnLoaded(object sender, RoutedEventArgs e)
        {
            Vm.Summary = "loaded:" + this.LoadedProbe.Text;
        }

        public void InputA_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == "Tab")
            {
                e.Handled = true;
            }
        }
    }
}
)CS";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "MainWindow.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back(aero::SourceFile{"MainWindow.xaml.cs", script});
    auto res = compiler.Compile(in);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) std::cerr << "  diag: " << d << std::endl;
    }
    CHECK(res.succeeded);
    if (!res.succeeded) return;

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    auto nativeBindings = std::make_shared<aero::BindingRegistry>();
    aero::ScriptUiBridge bridge(domains);
    bridge.RegisterCoreUiBindings(*nativeBindings);
    aero::Interpreter interp(res.programImage, domains.uiHeapPtr());
    interp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
    interp.setBindingRegistry(nativeBindings);
    aero::DualTrackBindingEngine binding(domains, res.programImage.get(), &interp);
    bridge.SetUnderlyingEngine(&binding);
    aero::StyleTriggerEngine style;
    style.SetBindingEngine(&binding);

    auto createSym = res.programImage->findFunction("VerbatimDemo::CreateMainWindow");
    CHECK(createSym.has_value());
    if (!createSym) return;
    auto execRes = interp.invoke(*createSym, {});
    CHECK(execRes.succeeded);
    const auto* mainWinRef = std::get_if<aero::ObjectRef>(&execRes.value);
    CHECK(mainWinRef != nullptr && mainWinRef->valid());
    auto pin = domains.uiHeap().retain(*mainWinRef);

    auto root = aero::MountAndWireMainWindow(
        res.axbPackage, domains, binding, style, *mainWinRef, &interp);
    CHECK(root.Get() != nullptr);
    if (!root) return;
    (void)binding.FlushTrackA();
    root->Measure(aero::Size{480.0f, 320.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 480.0f, 320.0f});

    auto* inputA = root->FindName("InputA");
    auto* btn = dynamic_cast<aero::Button*>(root->FindName("Btn"));
    auto* out = dynamic_cast<aero::TextBlock*>(root->FindName("Out"));
    CHECK(inputA != nullptr && btn != nullptr && out != nullptr);
    if (!inputA || !btn || !out) return;

    // OnLoaded ran after children mounted and saw LoadedProbe.Text
    CHECK(out->GetText() == "loaded:probe_ready");

    // KeyDown e.Handled = true suppresses Tab focus cycling
    aero::InputEventRouter router(root.Get());
    CHECK(router.SetFocus(inputA));
    CHECK(router.DispatchKeyDown(aero::KeyCode::Tab));
    CHECK(router.FocusedElement() == inputA);

    // Typed RelayCommand<int> executes after escaped/quoted strings
    CHECK(btn->ActivateFromInput());
    (void)binding.FlushTrackA();
    CHECK(out->GetText() == "cmd:7");
}

} // namespace


static void RunMilestone2And3VerificationSuite() {
    {
        // 1. Samples/Theme C# WPF Project (UTF-8 BOM, #region, App.xaml.cs, NoesisGUIExtensions DP shims,
        //    {x:Null} 3-State CheckBox, PasswordBox, Reversed/Ticked Slider, ListBox IsSelected)
        aero::XamlApplication themeApp;
        std::string themeErr;
        const bool themeLoaded = themeApp.Load(std::string(AERO_SAMPLES_DIR) + "/Theme", &themeErr);
        if (!themeLoaded) {
            std::fprintf(stderr, "Theme load error: %s\n", themeErr.c_str());
        }
        CHECK(themeLoaded);
        CHECK(themeApp.MainWindowElement() != nullptr);
        CHECK(themeApp.ApplicationObject().valid());
        std::vector<std::uint8_t> themeRgba;
        themeApp.RasterizeToRgba8(themeRgba);
        CHECK(themeRgba.size() == 1280u * 720u * 4u);
        CHECK(themeApp.RenderTree().NodeCount() > 40u);

        const std::size_t cleanSync = themeApp.RenderTree().SyncVisualTree(themeApp.MainWindowElement());
        CHECK(cleanSync == 0u);
    }
    aero::AxbTemplateRegistry::Instance().Clear();

    {
        // 2. Full WPF C# Ergonomics: UTF-8 BOM + #region + #if/#else + nameof(...) + Visibility enum
        //    + Code-behind += Method & Lambda subscriptions + CanExecuteChanged + Custom C# UserControl
        //    + Slider IsDirectionReversed/SnapToTick + PasswordBox PasswordChar/PasswordChanged
        //    + ComboBox IsEditable/Text + ListBox Extended SelectionMode + KeySpline Animation
        const std::filesystem::path tmpDir = std::filesystem::path(AERO_TEST_TEMP_DIR) / "wpf_csharp_ergonomics";
        std::filesystem::create_directories(tmpDir / "Controls");

        {
            std::ofstream ofs(tmpDir / "Controls/CounterCard.xaml", std::ios::binary);
            ofs << R"(<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="ErgonomicsDemo.CounterCard" Width="220" Height="44">
  <StackPanel Orientation="Horizontal" Spacing="8">
    <TextBlock x:Name="CardReadout" Text="Card:0"/>
    <Button x:Name="CardPingBtn" Content="Ping" Click="CardPingBtn_Click" Width="60" Height="28"/>
  </StackPanel>
</UserControl>)";
        }
        {
            std::ofstream ofs(tmpDir / "Controls/CounterCard.xaml.cs", std::ios::binary);
            ofs << "\xEF\xBB\xBF" << R"(using System;
using System.Windows;
using Aero.UI;

namespace ErgonomicsDemo
{
    public partial class CounterCard : UserControl
    {
        #region Fields
        private int _pings = 0;
        #endregion

        public CounterCard()
        {
            InitializeComponent();
            Loaded += (s, e) => { CardLoadedCount = CardLoadedCount + 1; };
            CardPingBtn.Loaded += (s, e) => { CardBtnLoadedCount = CardBtnLoadedCount + 1; };
            CardPingBtn.Click += (s, e) =>
            {
                _pings = _pings + 10;
                CardReadout.Text = $"Card:{_pings}";
            };
        }
        public int CardLoadedCount { get; set; } = 0;
        public int CardBtnLoadedCount { get; set; } = 0;

        public void CardPingBtn_Click(object sender, RoutedEventArgs e)
        {
            _pings = _pings + 1;
            CardReadout.Text = $"Card:{_pings}";
        }
    }
}
)";
        }
        {
            std::ofstream ofs(tmpDir / "App.xaml", std::ios::binary);
            ofs << R"(<Application x:Class="ErgonomicsDemo.App"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             StartupUri="MainWindow.xaml">
</Application>)";
        }
        {
            std::ofstream ofs(tmpDir / "App.xaml.cs", std::ios::binary);
            ofs << "\xEF\xBB\xBF" << R"(using System.Windows;
using Aero.UI;

namespace ErgonomicsDemo
{
    public partial class App : Application
    {
        public bool StartupCalled { get; set; } = false;
        public override void OnStartup(StartupEventArgs e)
        {
            StartupCalled = true;
        }
    }
}
)";
        }
        {
            std::ofstream ofs(tmpDir / "MainWindow.xaml", std::ios::binary);
            ofs << R"(<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:local="clr-namespace:ErgonomicsDemo"
        x:Class="ErgonomicsDemo.MainWindow"
        Width="800" Height="600">
  <Window.Resources>
    <Storyboard x:Key="SplineAnim">
      <DoubleAnimationUsingKeyFrames Storyboard.TargetName="AnimBox" Storyboard.TargetProperty="Opacity">
        <SplineDoubleKeyFrame KeyTime="0:0:0" Value="0.2"/>
        <SplineDoubleKeyFrame KeyTime="0:0:1" Value="1.0" KeySpline="0.4,0.0 0.2,1.0"/>
      </DoubleAnimationUsingKeyFrames>
    </Storyboard>
  </Window.Resources>
  <StackPanel Spacing="6">
    <Border x:Name="SecretPanel" Width="200" Height="24" Background="#223344"/>
    <Button x:Name="SubmitBtn" Content="Submit" Command="{Binding SubmitCommand}" Width="100" Height="30"/>
    <Button x:Name="ToggleVisBtn" Content="Toggle" Width="100" Height="30"/>
    <PasswordBox x:Name="PinBox" Password="12" PasswordChar="*" PasswordChanged="PinBox_PasswordChanged" Width="140" Height="28"/>
    <Slider x:Name="SnapSlider" Minimum="0" Maximum="100" TickFrequency="25" IsSnapToTickEnabled="True" IsDirectionReversed="True" TickPlacement="Both" Value="0" Width="200" Height="24"/>
    <ComboBox x:Name="EditCombo" IsEditable="True" Placeholder="Pick or type..." Width="180" Height="28">
      <ComboBoxItem Content="Alpha"/>
      <ComboBoxItem Content="Beta" IsSelected="True"/>
    </ComboBox>
    <ListBox x:Name="MultiList" SelectionMode="Extended" Width="180" Height="90">
      <ListBoxItem Content="Row0"/>
      <ListBoxItem Content="Row1"/>
      <ListBoxItem Content="Row2"/>
      <ListBoxItem Content="Row3" IsEnabled="False"/>
    </ListBox>
    <Border x:Name="AnimBox" Width="60" Height="20" Opacity="0.2" Background="#4488FF"/>
    <local:CounterCard x:Name="EmbeddedCard"/>
    <TextBlock x:Name="StatusOut" Text="{Binding LastChangedProp}"/>
  </StackPanel>
</Window>)";
        }
        {
            std::ofstream ofs(tmpDir / "MainWindow.xaml.cs", std::ios::binary);
            ofs << "\xEF\xBB\xBF" << R"(using System;
using System.ComponentModel;
using System.Windows;
using Aero.UI;

namespace ErgonomicsDemo
{
#if NOESIS
    public class IgnoredNoesisClass { invalid_syntax_if_compiled }
#else
    public class DemoVm : ViewModelBase
    {
        private string _draft = "";
        private RelayCommand _submitCommand;
        public string LastChangedProp { get; set; } = "Init";
        public int SubmitCount { get; set; } = 0;

        public RelayCommand SubmitCommand { get { return _submitCommand; } }

        public string Draft
        {
            get { return _draft; }
            set
            {
                if (_draft != value)
                {
                    _draft = value;
                    LastChangedProp = nameof(Draft);
                    OnPropertyChanged(nameof(Draft));
                    _submitCommand.RaiseCanExecuteChanged();
                }
            }
        }

        public DemoVm()
        {
            _submitCommand = new RelayCommand(DoSubmit, () => _draft != "");
        }

        private void DoSubmit()
        {
            SubmitCount = SubmitCount + 1;
            LastChangedProp = $"{nameof(SubmitCount)}:{SubmitCount}";
        }
    }

    public delegate int IntMapper(int x);

    public partial class MainWindow : Window
    {
        public DemoVm Vm { get; set; }
        public bool WindowLoadedFired { get; set; } = false;
        public int ChildBtnLoadedCount { get; set; } = 0;
        public int PinChangeEvents { get; set; } = 0;
        public float LastSliderVal { get; set; } = -1.0f;

        private int _secretBase = 40;
        private int ComputeSecret(int x) { return _secretBase + x; }

        public MainWindow()
        {
            InitializeComponent();
            Vm = new DemoVm();
            DataContext = Vm;

            Loaded += (s, e) => { WindowLoadedFired = true; };
            ToggleVisBtn.Loaded += (s, e) => { ChildBtnLoadedCount = ChildBtnLoadedCount + 1; };
            ToggleVisBtn.Click += OnToggleVisClicked;
            SnapSlider.ValueChanged += (s, e) => { LastSliderVal = e.NewValue; };
        }

        public string GetErgonomicsAudit()
        {
            IntMapper fn = (delta) => ComputeSecret(delta);
            return $"{WindowLoadedFired}|{ChildBtnLoadedCount}|{(int)LastSliderVal}|{fn(2)}";
        }

        private void OnToggleVisClicked(object sender, RoutedEventArgs e)
        {
            if (SecretPanel.Visibility == Visibility.Visible)
            {
                SecretPanel.Visibility = Visibility.Collapsed;
            }
            else
            {
                SecretPanel.Visibility = Visibility.Visible;
            }
        }

        public void PinBox_PasswordChanged(object sender, RoutedEventArgs e)
        {
            PinChangeEvents = PinChangeEvents + 1;
            Vm.Draft = PinBox.Password;
        }
    }
#endif
}
)";
        }

        aero::XamlApplication app;
        std::string err;
        const bool ok = app.Load(tmpDir.string(), &err);
        if (!ok) {
            std::fprintf(stderr, "ErgonomicsDemo load failed: %s\n", err.c_str());
        }
        CHECK(ok);
        auto* root = app.MainWindowElement();
        CHECK(root != nullptr);
        if (!ok || !root) {
            aero::AxbTemplateRegistry::Instance().Clear();
            return;
        }

        auto* secretPanel = root->FindName("SecretPanel");
        auto* submitBtn = root->FindName("SubmitBtn") ? root->FindName("SubmitBtn")->DynamicCast<aero::Button>() : nullptr;
        auto* toggleVisBtn = root->FindName("ToggleVisBtn") ? root->FindName("ToggleVisBtn")->DynamicCast<aero::Button>() : nullptr;
        auto* pinBox = root->FindName("PinBox") ? root->FindName("PinBox")->DynamicCast<aero::PasswordBox>() : nullptr;
        auto* snapSlider = root->FindName("SnapSlider") ? root->FindName("SnapSlider")->DynamicCast<aero::Slider>() : nullptr;
        auto* editCombo = root->FindName("EditCombo") ? root->FindName("EditCombo")->DynamicCast<aero::ComboBox>() : nullptr;
        auto* multiList = root->FindName("MultiList") ? root->FindName("MultiList")->DynamicCast<aero::ListBox>() : nullptr;
        auto* statusOut = root->FindName("StatusOut") ? root->FindName("StatusOut")->DynamicCast<aero::TextBlock>() : nullptr;
        auto* animBox = root->FindName("AnimBox");
        auto* embeddedCard = root->FindName("EmbeddedCard");
        CHECK(secretPanel && submitBtn && toggleVisBtn && pinBox && snapSlider && editCombo && multiList && statusOut && animBox && embeddedCard);

        CHECK(!submitBtn->IsEnabled());
        CHECK(secretPanel->GetVisibility() == aero::Visibility::Visible);
        (void)toggleVisBtn->PerformClick();
        app.Tick(0.016f);
        CHECK(secretPanel->GetVisibility() == aero::Visibility::Collapsed);
        (void)toggleVisBtn->PerformClick();
        app.Tick(0.016f);
        CHECK(secretPanel->GetVisibility() == aero::Visibility::Visible);

        CHECK(pinBox->GetDisplayString() == "**");
        pinBox->SetFocused(true);
        (void)pinBox->InsertText("9");
        app.Tick(0.016f);
        CHECK(pinBox->GetPassword() == "129");
        CHECK(pinBox->GetDisplayString() == "***");
        CHECK(statusOut->GetText() == "Draft");
        CHECK(submitBtn->IsEnabled());

        (void)submitBtn->PerformClick();
        app.Tick(0.016f);
        CHECK(statusOut->GetText() == "SubmitCount:1");

        (void)snapSlider->SetSliderValueInteractive(36.0f);
        app.Tick(0.016f);
        CHECK(std::fabs(snapSlider->GetSliderValue() - 25.0f) < 0.01f);
        CHECK(std::fabs(snapSlider->VisualNormalizedPosition() - 0.75f) < 0.01f);

        CHECK(editCombo->GetSelectedIndex() == 1);
        CHECK(editCombo->GetDisplayText() == "Beta");
        editCombo->SetFocused(true);
        (void)editCombo->OnTextInput("CustomTag");
        CHECK(editCombo->GetDisplayText().find("CustomTag") != std::string::npos);

        (void)multiList->SelectIndexWithModifiers(0, 0u);
        (void)multiList->SelectIndexWithModifiers(2, aero::KeyMod_Shift);
        CHECK(multiList->IsIndexSelected(0) && multiList->IsIndexSelected(1) && multiList->IsIndexSelected(2));
        (void)multiList->SelectIndexWithModifiers(1, aero::KeyMod_Ctrl);
        CHECK(multiList->IsIndexSelected(0) && !multiList->IsIndexSelected(1) && multiList->IsIndexSelected(2));

        auto* cardPingBtn = embeddedCard->FindName("CardPingBtn") ? embeddedCard->FindName("CardPingBtn")->DynamicCast<aero::Button>() : nullptr;
        auto* cardReadout = embeddedCard->FindName("CardReadout") ? embeddedCard->FindName("CardReadout")->DynamicCast<aero::TextBlock>() : nullptr;
        CHECK(cardPingBtn && cardReadout);
        (void)cardPingBtn->PerformClick();
        app.Tick(0.016f);
        CHECK(cardReadout->GetText() == "Card:11");

        // Verify Window.Loaded, child ToggleVisBtn.Loaded, Slider.ValueChanged -> LastSliderVal, and RS-1 private closure capture
        if (const auto auditSym = app.Program() ? app.Program()->findFunction("ErgonomicsDemo::MainWindow.GetErgonomicsAudit") : std::nullopt) {
            if (app.ScriptInterpreter() && app.CodeBehindObject().valid()) {
                const auto auditRes = app.ScriptInterpreter()->invoke(*auditSym, {aero::UnifiedValue(app.CodeBehindObject())});
                CHECK(auditRes.succeeded);
                const std::string auditStr = aero::rs_rt::valueToString(auditRes.value, app.ScriptInterpreter()->heap().get());
                CHECK(auditStr == "true|1|25|42" || auditStr == "True|1|25|42");
            }
        }
        // Verify UserControl CounterCard.Loaded & child CardPingBtn.Loaded
        auto* embeddedUc = embeddedCard->DynamicCast<aero::UserControl>();
        if (embeddedUc && embeddedUc->BoundScriptWrapper().valid() && app.Program() && app.ScriptInterpreter()) {
            const auto cardObj = embeddedUc->BoundScriptWrapper();
            const auto getCardLoaded = app.Program()->findFunction("ErgonomicsDemo::CounterCard.get_CardLoadedCount");
            const auto getBtnLoaded = app.Program()->findFunction("ErgonomicsDemo::CounterCard.get_CardBtnLoadedCount");
            if (getCardLoaded && getBtnLoaded) {
                const auto r1 = app.ScriptInterpreter()->invoke(*getCardLoaded, {aero::UnifiedValue(cardObj)});
                const auto r2 = app.ScriptInterpreter()->invoke(*getBtnLoaded, {aero::UnifiedValue(cardObj)});
                CHECK(r1.succeeded && std::holds_alternative<std::int64_t>(r1.value) && std::get<std::int64_t>(r1.value) == 1);
                CHECK(r2.succeeded && std::holds_alternative<std::int64_t>(r2.value) && std::get<std::int64_t>(r2.value) == 1);
            }
        }

        const auto* sb = app.StyleEngine().FindStoryboard("SplineAnim");
        if (!sb) sb = aero::AxbTemplateRegistry::Instance().FindStoryboard("SplineAnim");
        CHECK(sb != nullptr);
        if (sb) {
            (void)app.Animations().BeginStoryboard(root, *sb);
            app.Tick(0.5f);
            CHECK(animBox->GetOpacity() > 0.65f && animBox->GetOpacity() < 0.99f);
        }
    }
    aero::AxbTemplateRegistry::Instance().Clear();
}

static void TestPatchAuditRegressions() {
    // 1. AG-3 (compound #define/#undef/#if & verbatim #if 0) + AG-9.2 (System.String.Empty)
    //    + RS-1/RS-2/RS-3 (ternary/try/for/nested lambda captures & Action vs Func<int> overload)
    {
        const char* xaml = R"(<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="AuditDemo.MainWindow"
        Width="400" Height="240">
  <StackPanel>
    <TextBlock x:Name="EmptyStat" Text="{x:Static System.String.Empty}"/>
    <TextBlock x:Name="Out" Text="init"/>
  </StackPanel>
</Window>)";
        const char* script = R"CS(
#define FLAG_ON
#define FLAG_TEMP
#undef FLAG_TEMP
using System;
using Aero.UI;

namespace AuditDemo
{
    public delegate void IntAction(int x);
    public delegate int IntMapper(int x);
    public delegate int IntThunk();

    public partial class MainWindow : Window
    {
        private int _secret = 7;
        private int SecretMul(int k) { return _secret * k; }

        private int InvokeOverload(IntAction a) { a(3); return 10; }
        private int InvokeOverload(IntMapper f) { return f(4); }

        public MainWindow()
        {
            InitializeComponent();
        }

        public string RunAudit()
        {
#if (AERO && !NOESIS) && (defined(FLAG_ON) && !FLAG_TEMP) && !UNKNOWN_DEFINE
            string verb = @"#if 0
keep_literal";
            int total = 0;
            bool cond = true;
            int outerA = 5;
            int outerB = 9;
            IntMapper ternFn = (x) => cond ? (x + outerA) : outerB;
            total = total + ternFn(1);

            for (int i = 0; i < 2; i = i + 1)
            {
                int localStep = i + 1;
                IntThunk nested = () =>
                {
                    IntThunk inner = () => SecretMul(1) + localStep;
                    return inner();
                };
                total = total + nested();
            }

            try
            {
                int inTry = 100;
                IntThunk tryFn = () => outerA + inTry;
                total = total + (tryFn() - inTry);
            }
            catch
            {
                total = -999;
            }

            int ovAct = InvokeOverload((x) => { total = total + x; });
            int ovFunc = InvokeOverload((x) => { return x * 2; });
            return $"{total}|{ovAct}|{ovFunc}|{verb.Length}";
#else
            return "wrong_branch";
#endif
        }
    }
}
)CS";
        aero::AeroCompiler compiler;
        aero::AeroCompileInput in{};
        in.xamlPath = "MainWindow.xaml";
        in.xamlContent = xaml;
        in.scriptSources.push_back(aero::SourceFile{"MainWindow.xaml.cs", script});
        const auto res = compiler.Compile(in);
        if (!res.succeeded) {
            for (const auto& d : res.diagnostics) std::cerr << "  audit diag: " << d << std::endl;
        }
        CHECK(res.succeeded);
        if (res.succeeded) {
            aero::ExecutionDomainContext domains;
            domains.registerProgramObservableMetadata(*res.programImage);
            auto nativeBindings = std::make_shared<aero::BindingRegistry>();
            aero::ScriptUiBridge bridge(domains);
            bridge.RegisterCoreUiBindings(*nativeBindings);
            aero::Interpreter interp(res.programImage, domains.uiHeapPtr());
            interp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
            interp.setBindingRegistry(nativeBindings);
            aero::DualTrackBindingEngine binding(domains, res.programImage.get(), &interp);
            bridge.SetUnderlyingEngine(&binding);
            aero::StyleTriggerEngine style;
            style.SetBindingEngine(&binding);

            const auto createSym = res.programImage->findFunction("AuditDemo::CreateMainWindow");
            CHECK(createSym.has_value());
            if (createSym) {
                const auto execRes = interp.invoke(*createSym, {});
                CHECK(execRes.succeeded);
                if (const auto* mainWinRef = std::get_if<aero::ObjectRef>(&execRes.value); mainWinRef && mainWinRef->valid()) {
                    auto pin = domains.uiHeap().retain(*mainWinRef);
                    auto root = aero::MountAndWireMainWindow(res.axbPackage, domains, binding, style, *mainWinRef, &interp);
                    CHECK(root.Get() != nullptr);
                    if (root) {
                        auto* emptyStat = dynamic_cast<aero::TextBlock*>(root->FindName("EmptyStat"));
                        CHECK(emptyStat && emptyStat->GetText().empty());
                    }
                    const auto auditSym = res.programImage->findFunction("AuditDemo::MainWindow.RunAudit");
                    CHECK(auditSym.has_value());
                    if (auditSym) {
                        const auto runRes = interp.invoke(*auditSym, {aero::UnifiedValue(*mainWinRef)});
                        CHECK(runRes.succeeded);
                        // ternFn(1)=6; loop=(7+1)+(7+2)=17; try=5; ovAct sets total+=3 -> 31, ret 10; ovFunc ret 8; verb len=18 (\n) or 19 (\r\n)
                        const std::string val = aero::rs_rt::valueToString(runRes.value, interp.heap().get());
                        CHECK(val.rfind("31|10|8|", 0) == 0);
                    }
                }
            }
        }
    }

    // 2. AG-4 (ComboBox UTF-8 Backspace & SelectedIndex) + AG-8.4 (ListBox Extended Ctrl-toggle)
    //    + AG-8.5 (TextBox SelectAll) + AG-8.3 (PointerDown target before MouseDown bubble) + AG-7 (TextDecorations)
    {
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        root->SetOrientation(aero::Orientation::Vertical);
        root->SetWidth(400.0f);
        root->SetHeight(300.0f);

        // AG-4: ComboBox UTF-8 Backspace & SelectedIndex sync
        auto combo = aero::MakeIntrusive<aero::ComboBox>();
        combo->SetIsEditable(true);
        combo->SetItemsSource({
            aero::value::FromString("中"),
            aero::value::FromString("中文"),
            aero::value::FromString("Alpha")
        });
        combo->SetFocused(true);
        CHECK(combo->OnTextInput("中文"));
        CHECK(combo->GetSelectedIndex() == 1);
        CHECK(combo->OnKeyDown(aero::KeyCode::Backspace, 0u));
        CHECK(combo->GetDisplayText() == "中");
        CHECK(combo->GetSelectedIndex() == 0);
        CHECK(combo->OnKeyDown(aero::KeyCode::Backspace, 0u));
        CHECK(combo->GetDisplayText().empty());
        CHECK(combo->GetSelectedIndex() == -1);

        // AG-8.4: ListBox Extended Ctrl-toggle non-primary fires SelectionChanged
        auto lb = aero::MakeIntrusive<aero::ListBox>();
        lb->SetSelectionMode(2); // Extended
        lb->SetItemsSource({
            aero::value::FromString("A"),
            aero::value::FromString("B"),
            aero::value::FromString("C")
        });
        int lbNotifyCount = 0;
        lb->SetSelectionChangedCallback([&](aero::ListBox&, std::int32_t) { ++lbNotifyCount; });
        CHECK(lb->SelectIndexWithModifiers(0, 0u));
        CHECK(lb->SelectIndexWithModifiers(2, aero::KeyMod_Shift));
        const int beforeToggle = lbNotifyCount;
        CHECK(lb->SelectIndexWithModifiers(1, aero::KeyMod_Ctrl)); // unselects 1 while primary stays 0
        CHECK(lbNotifyCount == beforeToggle + 1);
        CHECK(lb->IsIndexSelected(0) && !lb->IsIndexSelected(1) && lb->IsIndexSelected(2));

        // AG-8.5: TextBox SelectAll replace & delete
        auto tbEdit = aero::MakeIntrusive<aero::TextBox>();
        tbEdit->SetText("Hello");
        tbEdit->SelectAll();
        CHECK(tbEdit->HasAllSelected());
        CHECK(tbEdit->InsertText("World"));
        CHECK(tbEdit->GetText() == "World");
        CHECK(!tbEdit->HasAllSelected());
        tbEdit->SelectAll();
        CHECK(tbEdit->OnKeyDown(aero::KeyCode::Backspace, 0u));
        CHECK(tbEdit->GetText().empty());
        tbEdit->SetText("Abc");
        tbEdit->SelectAll();
        CHECK(tbEdit->OnKeyDown(aero::KeyCode::Delete, 0u));
        CHECK(tbEdit->GetText().empty());

        // AG-8.3: DispatchPointerDown updates target state (OnPointerDown) before MouseDown callback runs
        auto tbClick = aero::MakeIntrusive<aero::TextBox>();
        tbClick->SetWidth(160.0f);
        tbClick->SetHeight(32.0f);
        tbClick->SetFontSize(14.0f);
        tbClick->SetText("HelloWorld");
        tbClick->SetCaretIndex(0u);
        std::size_t caretInMouseDown = 0u;
        tbClick->SetMouseDownCallback([&](aero::UIElement&) -> bool {
            caretInMouseDown = tbClick->GetCaretIndex();
            return true;
        });
        root->AddChild(tbClick.Get());

        // AG-7: TextDecorations bitmask & overline rendering
        const auto tdMask = aero::ParseBuiltinLiteralPropertyValue(
            aero::PropId_TextDecorations, "Underline, Strikethrough, Overline");
        CHECK(std::holds_alternative<std::int64_t>(tdMask) && std::get<std::int64_t>(tdMask) == 7);
        auto decoTb = aero::MakeIntrusive<aero::TextBlock>();
        decoTb->SetText("HelloDeco");
        decoTb->SetFontSize(16.0f);
        decoTb->SetValue(aero::PropId_TextDecorations, tdMask, aero::ValueSourceRank::Local);
        root->AddChild(decoTb.Get());

        root->Measure(aero::Size{400.0f, 300.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

        aero::InputEventRouter router(root.Get());
        const aero::Rect br = tbClick->FinalRect();
        CHECK(router.DispatchPointerDown(
            aero::Point{br.x + br.width - 8.0f, br.y + br.height * 0.5f},
            aero::PointerButton::Left,
            0u));
        CHECK(caretInMouseDown > 0u);

        aero::PagedRenderTree decoTree;
        decoTree.BuildFromVisualTree(decoTb.Get());
        CHECK(decoTree.NodeCount() >= 3u);
    }

    // 3. AG-1 & AG-2: Dynamic Button Command.CanExecute watcher + wrapper cleanup without crash/leak
    {
        aero::ExecutionDomainContext domains;
        aero::DualTrackBindingEngine engine(domains, nullptr, nullptr);
        int canExecCalls = 0;
        {
            auto dynBtn = aero::MakeIntrusive<aero::Button>();
            dynBtn->SetHasRegisteredScriptWrapper(true);
            engine.RegisterCommandCanExecuteWatcher(dynBtn.Get(), [&]() {
                ++canExecCalls;
            });
            engine.EvaluateCommandCanExecuteWatchers();
            CHECK(canExecCalls == 1);
            engine.UnbindSubtree(dynBtn.Get());
            engine.EvaluateCommandCanExecuteWatchers();
            CHECK(canExecCalls == 1);

            engine.RegisterCommandCanExecuteWatcher(dynBtn.Get(), [&]() {
                ++canExecCalls;
            });
        } // dynBtn destroyed here -> ~UIElement scrubs watcher & unregisters wrapper
        engine.EvaluateCommandCanExecuteWatchers();
        CHECK(canExecCalls == 1);
    }
}

int main() {
    RunMilestone2And3VerificationSuite();
    TestPatchAuditRegressions();

    TestThreeStateCheckBoxAndIndeterminateRender();
    TestDeclarativeTreeViewAndSelectedItemSubHopBinding();
    TestDeclarativeTreeViewItemIsSelectedOnMount();
    TestHierarchicalDataTemplateRuntime();
    TestCtorNamedControlAccessAndCollectionMoveBounds();
    TestDynamicRunActualWidthAndGridSplitter();
    TestHorizontalGridSplitterAndStarPreservation();
    TestContextMenuAndRemoveElementAction();
    TestConcurrentRasterizeTreeToRgba8();
    TestWpfQuickStartEndToEndApplication();
    TestStrongTypedEventArgsAndMultiEventCheckBox();
    TestRepeatButtonAutoRepeatMenuAutoCloseAndTreeKeyboard();
    TestGalleryEnhancedXamlSamples();
    TestVisualResourceItemsSourceLifecycleAndLeakCheck();
    TestListViewAutoWidthClickAndIncrementalSync();
    TestMenuItemIconIncrementalSyncMatchesFullBuild();
    TestToolTipHoverStabilityAndLayoutIsolation();
    TestNestedBlurAndBackdropBlurParallelRasterization();
    TestVerbatimStringsTypedCommandLoadedAndKeyDownHandled();

    std::cout << "wpf_controls_and_script_tests: " << gPassed << " passed, " << gFailed << " failed" << std::endl;
    return (gFailed == 0) ? 0 : 1;
}
