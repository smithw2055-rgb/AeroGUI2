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

} // namespace

int main() {
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

    std::cout << "wpf_controls_and_script_tests: " << gPassed << " passed, " << gFailed << " failed" << std::endl;
    return (gFailed == 0) ? 0 : 1;
}
