#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace {

std::atomic<bool> gCountHeapAllocs{false};
std::atomic<std::size_t> gHeapAllocCount{0};

} // namespace

#if defined(__GNUC__) || defined(__clang__)
#define AERO_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define AERO_NOINLINE __declspec(noinline)
#else
#define AERO_NOINLINE
#endif

AERO_NOINLINE void* operator new(std::size_t size) {
    if (gCountHeapAllocs.load(std::memory_order_relaxed)) {
        gHeapAllocCount.fetch_add(1u, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

AERO_NOINLINE void operator delete(void* p) noexcept {
    std::free(p);
}

AERO_NOINLINE void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

#define AERO_CHECK(cond, msg)                                                      \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace {

bool ApproxEq(float a, float b, float eps = 1e-2f) noexcept {
    return std::fabs(a - b) <= eps;
}

bool ColorApproxEq(const aero::Color& a, const aero::Color& b, float eps = 0.02f) noexcept {
    return ApproxEq(a.r, b.r, eps) &&
           ApproxEq(a.g, b.g, eps) &&
           ApproxEq(a.b, b.b, eps) &&
           ApproxEq(a.a, b.a, eps);
}

aero::Color ReadElemBg(const aero::UIElement* elem) noexcept {
    const aero::UnifiedValue bgVal = elem->GetValue(aero::PropId_Background);
    if (auto c = aero::value::TryGetColor(bgVal)) {
        return *c;
    }
    return aero::Color{0.0f, 0.0f, 0.0f, 0.0f};
}

// ============================================================================
// 1. Zero-Allocation DataTrigger, MultiDataTrigger & {x:Null} Evaluation
// ============================================================================
void TestZeroAllocDataTriggerMultiDataTriggerAndNullConditions() {
    static_assert(sizeof(aero::UnifiedValue) == 16, "UnifiedValue == 16B");
    static_assert(sizeof(aero::Float4Value) == 16, "Float4Value == 16B");
    static_assert(sizeof(aero::CompactPropertyEntry) == 32, "CompactPropertyEntry == 32B");
    static_assert(sizeof(aero::FastInlineStorage) == 56, "FastInlineStorage == 56B");

    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Samples.Phase5D;

[Observable]
public class QuestNode
{
    public bool IsCompleted { get; set; } = false;
}

[Observable]
public partial class QuestHudView
{
    public QuestNode ActiveQuest { get; set; } = null;
    public string Faction { get; set; } = "Horde";
    public int Score { get; set; } = 50;
}

public QuestHudView CreateQuestHudVm()
{
    return new QuestHudView();
}

public QuestNode CreateQuestNode()
{
    return new QuestNode();
}
)RS";

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            x:Class="Samples.Phase5D.QuestHudView"
            Width="320" Height="200">
    <StackPanel.Resources>
        <Style x:Key="QuestDetailStyle" TargetType="Border">
            <Setter Property="Background" Value="#FF1E293B"/>
            <Setter Property="Visibility" Value="Visible"/>
            <Style.Triggers>
                <DataTrigger Binding="{Binding ActiveQuest}" Value="{x:Null}">
                    <Setter Property="Visibility" Value="Collapsed"/>
                </DataTrigger>
                <DataTrigger Binding="{Binding ActiveQuest.IsCompleted}" Value="True">
                    <Setter Property="Background" Value="#FF16A34A"/>
                    <Trigger.EnterActions>
                        <BeginStoryboard>
                            <Storyboard>
                                <DoubleAnimation Storyboard.TargetProperty="Opacity" To="0.88" Duration="0:0:0.08"/>
                            </Storyboard>
                        </BeginStoryboard>
                    </Trigger.EnterActions>
                    <Trigger.ExitActions>
                        <BeginStoryboard>
                            <Storyboard>
                                <DoubleAnimation Storyboard.TargetProperty="Opacity" To="1.0" Duration="0:0:0.08"/>
                            </Storyboard>
                        </BeginStoryboard>
                    </Trigger.ExitActions>
                </DataTrigger>
                <MultiDataTrigger>
                    <MultiDataTrigger.Conditions>
                        <Condition Binding="{Binding Faction}" Value="Horde"/>
                        <Condition Binding="{Binding Score}" Value="100"/>
                    </MultiDataTrigger.Conditions>
                    <Setter Property="Background" Value="#FFF59E0B"/>
                </MultiDataTrigger>
            </Style.Triggers>
        </Style>
    </StackPanel.Resources>

    <Border x:Name="DetailCard" Width="280" Height="64" Style="{StaticResource QuestDetailStyle}"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "QuestHudView.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"QuestHudView.rs", rsCode});

    const aero::AeroCompileResult res = compiler.Compile(input);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) {
            std::cerr << "Diag: " << d << "\n";
        }
    }
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled QuestHudView XAML + RealScript");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    const auto vmExec = uiInterp.invoke("Samples.Phase5D::CreateQuestHudVm");
    const auto questExec = uiInterp.invoke("Samples.Phase5D::CreateQuestNode");
    AERO_CHECK(vmExec.succeeded && questExec.succeeded, "Created QuestHudView and QuestNode on UiHeap");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const aero::ObjectRef questRef = std::get<aero::ObjectRef>(questExec.value);
    const auto pinVm = domains.uiHeap().retain(vmRef);
    const auto pinQuest = domains.uiHeap().retain(questRef);

    const aero::SymbolId vmTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::QuestHudView");
    const aero::SymbolId questTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::QuestNode");
    const auto slotActiveQuest = *domains.resolvePropertySlot(*res.programImage, vmTypeId, "ActiveQuest");
    const auto slotScore = *domains.resolvePropertySlot(*res.programImage, vmTypeId, "Score");
    const auto slotCompleted = *domains.resolvePropertySlot(*res.programImage, questTypeId, "IsCompleted");

    aero::DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    aero::AnimationEngine animEngine(16u);
    aero::StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine, vmRef, &uiInterp),
               "Mounted QuestHudView");
    root->SetValue(aero::PropId_DataContext, vmRef);
    styleEngine.RegisterPackageStyles(res.axbPackage);
    AERO_CHECK(styleEngine.BindSubtreeStyles(root.Get()) == 1u, "Attached QuestDetailStyle");

    auto* card = root->FindName("DetailCard")->DynamicCast<aero::Border>();
    AERO_CHECK(card != nullptr, "Resolved DetailCard");

    // Pass 1: ActiveQuest == null -> DataTrigger collapses DetailCard!
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(card->GetVisibility() == aero::Visibility::Collapsed,
               "DataTrigger Value='{x:Null}' collapsed DetailCard when ActiveQuest is null");

    // Assign ActiveQuest = questRef (IsCompleted == false) -> DetailCard becomes Visible with base #FF1E293B!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmRef, slotActiveQuest, questRef);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(card->GetVisibility() == aero::Visibility::Visible &&
                   ColorApproxEq(card->GetBackground(), aero::ParseXamlColorLiteral("#FF1E293B")),
               "Setting non-null ActiveQuest restored Visibility::Visible and base Background");

    // Set ActiveQuest.IsCompleted = true -> 2-hop DataTrigger turns card green #FF16A34A + runs EnterActions!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, questRef, slotCompleted, aero::UnifiedValue{true});
    (void)styleEngine.EvaluateAndApply();
    for (int i = 0; i < 8; ++i) (void)animEngine.Tick(0.016f);
    AERO_CHECK(ColorApproxEq(card->GetBackground(), aero::ParseXamlColorLiteral("#FF16A34A")) &&
                   ApproxEq(card->GetOpacity(), 0.88f, 0.01f),
               "Multi-hop DataTrigger ActiveQuest.IsCompleted==True applied green Background and EnterActions Opacity=0.88");

    // Set Score = 100 while Faction == "Horde" -> MultiDataTrigger wins with gold #FFF59E0B!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmRef, slotScore, aero::value::FromInt(100));
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(card->GetBackground(), aero::ParseXamlColorLiteral("#FFF59E0B")),
               "MultiDataTrigger (Faction=='Horde' && Score==100) overrode Background to gold");

    // Zero-allocation steady-state proof across 120 DataTrigger / MultiDataTrigger transitions!
    gHeapAllocCount.store(0u, std::memory_order_relaxed);
    gCountHeapAllocs.store(true, std::memory_order_seq_cst);

    for (int cycle = 0; cycle < 120; ++cycle) {
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmRef, slotScore, aero::value::FromInt(50));
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, questRef, slotCompleted, aero::UnifiedValue{false});
        (void)styleEngine.EvaluateAndApply();
        for (int s = 0; s < 6; ++s) (void)animEngine.Tick(0.016f);

        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, questRef, slotCompleted, aero::UnifiedValue{true});
        (void)styleEngine.EvaluateAndApply();
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmRef, slotScore, aero::value::FromInt(100));
        (void)styleEngine.EvaluateAndApply();
        for (int s = 0; s < 6; ++s) (void)animEngine.Tick(0.016f);
    }

    gCountHeapAllocs.store(false, std::memory_order_seq_cst);
    const std::size_t allocs = gHeapAllocCount.load(std::memory_order_relaxed);
    AERO_CHECK(allocs == 0u, "Zero heap allocations across 120 multi-hop DataTrigger & MultiDataTrigger cycles");

    std::cout << "[PASS] TestZeroAllocDataTriggerMultiDataTriggerAndNullConditions (allocs=" << allocs << ")\n";
}

// ============================================================================
// 2. ElementName (Forward Ref + TwoWay), RelativeSource & Built-in Value Converters
// ============================================================================
void TestElementNameRelativeSourceAndBuiltinValueConvertersTwoWay() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            Width="360" Height="260">
    <!-- Forward ElementName reference to ToggleDetails defined below! -->
    <Border x:Name="PreviewPanel" Width="200" Height="40"
            Visibility="{Binding IsChecked, ElementName=ToggleDetails, Converter={StaticResource boolToVisibility}}">
        <Border.Style>
            <Style TargetType="Border">
                <Setter Property="Background" Value="#FF1E40AF"/>
                <Style.Triggers>
                    <DataTrigger Binding="{Binding SelectedItem, ElementName=TeamCombo}" Value="Horde">
                        <Setter Property="Background" Value="#FFB91C1C"/>
                    </DataTrigger>
                </Style.Triggers>
            </Style>
        </Border.Style>
    </Border>

    <CheckBox x:Name="ToggleDetails" IsChecked="True" Content="Show Preview">
        <TextBlock x:Name="InvertedChild"
                   IsEnabled="{Binding IsChecked, RelativeSource={RelativeSource AncestorType={x:Type CheckBox}}, Converter={StaticResource InverseBooleanConverter}}"/>
    </CheckBox>

    <Slider x:Name="MasterSlider" Minimum="0" Maximum="200" Value="75" Width="200" Height="24"/>
    <Slider x:Name="MirrorSlider" Minimum="0" Maximum="200"
            Value="{Binding Value, ElementName=MasterSlider, Mode=TwoWay}" Width="200" Height="24"/>

    <ComboBox x:Name="TeamCombo" ItemsSource="Alliance|Horde" SelectedIndex="0" Width="160" Height="28"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "ElementBindView.xaml";
    input.xamlContent = xaml;
    const auto res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled ElementBindView XAML");

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::StyleTriggerEngine styleEngine(nullptr, &bindingEngine);

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine),
               "Mounted ElementBindView without any ViewModel DataContext");
    styleEngine.RegisterPackageStyles(res.axbPackage);
    (void)styleEngine.BindSubtreeStyles(root.Get());

    auto* preview = root->FindName("PreviewPanel")->DynamicCast<aero::Border>();
    auto* toggle = root->FindName("ToggleDetails")->DynamicCast<aero::CheckBox>();
    auto* invChild = root->FindName("InvertedChild");
    auto* masterSlider = root->FindName("MasterSlider")->DynamicCast<aero::Slider>();
    auto* mirrorSlider = root->FindName("MirrorSlider")->DynamicCast<aero::Slider>();
    auto* teamCombo = root->FindName("TeamCombo")->DynamicCast<aero::ComboBox>();
    AERO_CHECK(preview && toggle && invChild && masterSlider && mirrorSlider && teamCombo,
               "Resolved all controls in ElementBindView");

    // Flush Track A once after full tree mount so forward ElementName references connect!
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();

    AERO_CHECK(preview->GetVisibility() == aero::Visibility::Visible,
               "Forward ElementName + boolToVisibility resolved Visible when ToggleDetails.IsChecked==True");
    AERO_CHECK(!invChild->IsEnabled(),
               "RelativeSource AncestorType=CheckBox + InverseBooleanConverter set IsEnabled=false");
    AERO_CHECK(ApproxEq(mirrorSlider->GetSliderValue(), 75.0f, 0.01f),
               "MirrorSlider synced initial Value=75 from MasterSlider");
    AERO_CHECK(ColorApproxEq(preview->GetBackground(), aero::ParseXamlColorLiteral("#FF1E40AF")),
               "Alliance selection keeps default blue Background");

    // Uncheck ToggleDetails -> PreviewPanel collapses AND InvertedChild becomes enabled!
    toggle->SetIsChecked(false);
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(preview->GetVisibility() == aero::Visibility::Collapsed,
               "boolToVisibility collapsed PreviewPanel when ToggleDetails unchecked");
    AERO_CHECK(invChild->IsEnabled(),
               "InverseBooleanConverter enabled InvertedChild when ToggleDetails unchecked");

    // TwoWay ElementName push: mutating MirrorSlider interactively pushes back to MasterSlider!
    mirrorSlider->SetSliderValue(140.0f);
    (void)mirrorSlider->NotifyTwoWayPropertyMutated(aero::PropId_Value);
    AERO_CHECK(ApproxEq(masterSlider->GetSliderValue(), 140.0f, 0.01f),
               "TwoWay ElementName binding pushed MirrorSlider Value=140 back to MasterSlider");

    // Switch TeamCombo to index 1 ("Horde") -> ElementName DataTrigger switches PreviewPanel to crimson!
    (void)teamCombo->SelectIndexInteractive(1);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(preview->GetBackground(), aero::ParseXamlColorLiteral("#FFB91C1C")),
               "ElementName DataTrigger (SelectedItem=='Horde') switched PreviewPanel Background to crimson");

    std::cout << "[PASS] TestElementNameRelativeSourceAndBuiltinValueConvertersTwoWay\n";
}

// ============================================================================
// 3. Declarative <DataTemplate>, <DataTemplate.Triggers> & Inline <ItemsControl.ItemTemplate>
// ============================================================================
void TestDeclarativeDataTemplateAndItemsControlMounting() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Samples.Phase5D;

[Observable]
public class SquadMember
{
    public string Name { get; set; } = "";
    public float Hp { get; set; } = 100.0;
    public bool IsLeader { get; set; } = false;
}

[Observable]
public partial class SquadRosterView
{
    public SquadMember[] Members { get; set; } = null;
}

public SquadRosterView CreateSquadRosterVm()
{
    SquadRosterView vm = new SquadRosterView();
    SquadMember[] arr = new SquadMember[3];

    SquadMember m0 = new SquadMember();
    m0.Name = "Vanguard";
    m0.Hp = 100.0;
    m0.IsLeader = true;
    arr[0] = m0;

    SquadMember m1 = new SquadMember();
    m1.Name = "Ranger";
    m1.Hp = 82.0;
    m1.IsLeader = false;
    arr[1] = m1;

    SquadMember m2 = new SquadMember();
    m2.Name = "Medic";
    m2.Hp = 95.0;
    m2.IsLeader = false;
    arr[2] = m2;

    vm.Members = arr;
    return vm;
}
)RS";

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            x:Class="Samples.Phase5D.SquadRosterView"
            Width="320" Height="240">
    <StackPanel.Resources>
        <DataTemplate x:Key="MemberRowTemplate" DataType="Samples.Phase5D.SquadMember">
            <Border x:Name="RowCard" Height="32" Background="#FF1E293B">
                <StackPanel Orientation="Horizontal" Spacing="8">
                    <TextBlock x:Name="NameText" Text="{Binding Name}"/>
                    <Border x:Name="LeaderBadge" Width="16" Height="16" Background="#FFFACC15"
                            Visibility="{Binding IsLeader, Converter={StaticResource boolToVisibility}}"/>
                </StackPanel>
            </Border>
            <DataTemplate.Triggers>
                <DataTrigger Binding="{Binding IsLeader}" Value="True">
                    <Setter TargetName="RowCard" Property="Background" Value="#FF1D4ED8"/>
                </DataTrigger>
            </DataTemplate.Triggers>
        </DataTemplate>
    </StackPanel.Resources>

    <ItemsControl x:Name="RosterList"
                  ItemTemplate="{StaticResource MemberRowTemplate}"
                  ItemsSource="{Binding Members}"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "SquadRosterView.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"SquadRoster.rs", rsCode});

    const auto res = compiler.Compile(input);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) {
            std::cerr << "Diag: " << d << "\n";
        }
    }
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled SquadRosterView XAML + RealScript");
    AERO_CHECK(res.axbPackage.dataTemplates.size() == 1u, "Compiled MemberRowTemplate into dataTemplates");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    const auto vmExec = uiInterp.invoke("Samples.Phase5D::CreateSquadRosterVm");
    AERO_CHECK(vmExec.succeeded, "Executed CreateSquadRosterVm in RealScript");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const auto pinVm = domains.uiHeap().retain(vmRef);

    const aero::SymbolId rosterTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::SquadRosterView");
    const aero::SymbolId memberTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::SquadMember");
    const auto slotMembers = *domains.resolvePropertySlot(*res.programImage, rosterTypeId, "Members");
    const auto slotLeader = *domains.resolvePropertySlot(*res.programImage, memberTypeId, "IsLeader");

    aero::UnifiedValue arrVal{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmRef, slotMembers, arrVal, nullptr, &uiInterp);
    const aero::ObjectRef arrRef = std::get<aero::ObjectRef>(arrVal);
    const aero::ObjectRef m1 = std::get<aero::ObjectRef>(*domains.uiHeap().arrayGet(arrRef, 1u));

    aero::DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    aero::StyleTriggerEngine styleEngine(nullptr, &bindingEngine);

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    styleEngine.RegisterPackageStyles(res.axbPackage);
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine, vmRef, &uiInterp),
               "Mounted SquadRosterView");
    (void)styleEngine.BindSubtreeStyles(root.Get());
    (void)styleEngine.EvaluateAndApply();

    auto* roster = root->FindName("RosterList")->DynamicCast<aero::ItemsControl>();
    AERO_CHECK(roster != nullptr && roster->RealizedItemCount() == 3u,
               "ItemsControl materialized 3 DataTemplate rows");

    aero::UIElement* row0 = roster->FirstChild();
    aero::UIElement* row1 = row0->NextSibling();
    AERO_CHECK(row0->FindName("NameText")->DynamicCast<aero::TextBlock>()->GetText() == "Vanguard",
               "Row 0 bound Name == 'Vanguard'");
    AERO_CHECK(row1->FindName("NameText")->DynamicCast<aero::TextBlock>()->GetText() == "Ranger",
               "Row 1 bound Name == 'Ranger'");
    AERO_CHECK(row0->FindName("LeaderBadge")->GetVisibility() == aero::Visibility::Visible &&
                   row1->FindName("LeaderBadge")->GetVisibility() == aero::Visibility::Collapsed,
               "Per-row boolToVisibility converter toggled LeaderBadge accurately");
    AERO_CHECK(ColorApproxEq(ReadElemBg(row0), aero::ParseXamlColorLiteral("#FF1D4ED8")) &&
                   ColorApproxEq(ReadElemBg(row1), aero::ParseXamlColorLiteral("#FF1E293B")),
               "DataTemplate.Triggers applied blue highlight only to leader row 0");

    // Live per-item mutation: promote Ranger (m1) to IsLeader=true!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, m1, slotLeader, aero::UnifiedValue{true});
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(row1->FindName("LeaderBadge")->GetVisibility() == aero::Visibility::Visible &&
                   ColorApproxEq(ReadElemBg(row1), aero::ParseXamlColorLiteral("#FF1D4ED8")),
               "Live [Observable] item mutation updated both row1 binding and DataTemplate.Triggers");

    std::cout << "[PASS] TestDeclarativeDataTemplateAndItemsControlMounting\n";
}

// ============================================================================
// 4. Virtualized 1,000-Row ItemsControl + DataTemplate In-Place Slot Recycling
// ============================================================================
void TestVirtualizedItemsControlDataTemplateInPlaceRecycling() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Samples.Phase5D;

[Observable]
public class LeaderboardEntry
{
    public string PlayerTag { get; set; } = "";
}

[Observable]
public partial class LeaderboardView
{
    public LeaderboardEntry[] Entries { get; set; } = null;
}

public LeaderboardView CreateLeaderboardVm()
{
    LeaderboardView vm = new LeaderboardView();
    vm.Entries = new LeaderboardEntry[1000];
    return vm;
}

public LeaderboardEntry CreateLeaderboardEntry()
{
    return new LeaderboardEntry();
}
)RS";

    const char* xaml = R"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
              x:Class="Samples.Phase5D.LeaderboardView"
              Width="300" Height="120">
    <ItemsControl x:Name="BoardList" IsVirtualizing="True" ItemHeight="30"
                  ItemsSource="{Binding Entries}">
        <ItemsControl.ItemTemplate>
            <DataTemplate DataType="Samples.Phase5D.LeaderboardEntry">
                <Border Height="30" Background="#FF0F172A">
                    <TextBlock x:Name="TagLabel" Text="{Binding PlayerTag}"/>
                </Border>
            </DataTemplate>
        </ItemsControl.ItemTemplate>
    </ItemsControl>
</ScrollViewer>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "LeaderboardView.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"Leaderboard.rs", rsCode});

    const auto res = compiler.Compile(input);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) {
            std::cerr << "Diag: " << d << "\n";
        }
    }
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled virtualized LeaderboardView");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    const auto vmExec = uiInterp.invoke("Samples.Phase5D::CreateLeaderboardVm");
    AERO_CHECK(vmExec.succeeded, "Created LeaderboardView with 1,000-slot array");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const auto pinVm = domains.uiHeap().retain(vmRef);

    const aero::SymbolId viewTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::LeaderboardView");
    const aero::SymbolId entryTypeId = aero::rs_sem::stableTypeId("Samples.Phase5D::LeaderboardEntry");
    const auto slotEntries = *domains.resolvePropertySlot(*res.programImage, viewTypeId, "Entries");
    const auto slotTag = *domains.resolvePropertySlot(*res.programImage, entryTypeId, "PlayerTag");

    aero::UnifiedValue arrVal{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmRef, slotEntries, arrVal, nullptr, &uiInterp);
    const aero::ObjectRef arrRef = std::get<aero::ObjectRef>(arrVal);

    constexpr std::size_t kTotalRows = 1000u;
    for (std::size_t i = 0; i < kTotalRows; ++i) {
        const auto eExec = uiInterp.invoke("Samples.Phase5D::CreateLeaderboardEntry");
        const aero::ObjectRef eRef = std::get<aero::ObjectRef>(eExec.value);
        auto sRef = *domains.uiHeap().allocateString("Player_" + std::to_string(i));
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, eRef, slotTag, sRef);
        (void)domains.uiHeap().arraySet(arrRef, i, eRef);
    }

    aero::DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    auto rootSv = aero::MakeIntrusive<aero::ScrollViewer>();
    AERO_CHECK(aero::MountAxbTemplate(rootSv.Get(), res.axbPackage, &domains, &bindingEngine, vmRef, &uiInterp),
               "Mounted virtualized LeaderboardView");

    rootSv->Measure(aero::Size{300.0f, 120.0f});
    rootSv->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 120.0f});

    auto* board = rootSv->FindName("BoardList")->DynamicCast<aero::ItemsControl>();
    AERO_CHECK(board != nullptr && board->TotalItemCount() == kTotalRows, "TotalItemCount == 1000");
    AERO_CHECK(board->RealizedItemCount() <= 10u,
               "Only viewport + overscan DataTemplate rows realized for 1,000 items");

    const std::size_t bindingSlotsAfterInitialLayout = bindingEngine.ReactiveBindings().size();
    AERO_CHECK(bindingSlotsAfterInitialLayout <= 12u,
               "Bounded Track A binding slots created only for realized virtual containers");

    // Scroll into interior (offset 15000px -> index ~500, realizing full top+bottom overscan window),
    // then scroll across interior & bottom (7500px -> 22500px -> 29880px) verifying 0 new Track A slots.
    rootSv->ScrollToVerticalOffset(15000.0f);
    rootSv->Measure(aero::Size{300.0f, 120.0f});
    rootSv->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 120.0f});

    const std::size_t bindingSlotsAfterMidScroll = bindingEngine.ReactiveBindings().size();
    AERO_CHECK(bindingSlotsAfterMidScroll >= bindingSlotsAfterInitialLayout && bindingSlotsAfterMidScroll <= 12u,
               "Interior scroll window bounded by viewport + 2*overscan containers");

    rootSv->ScrollToVerticalOffset(7500.0f);
    rootSv->Measure(aero::Size{300.0f, 120.0f});
    rootSv->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 120.0f});

    rootSv->ScrollToVerticalOffset(22500.0f);
    rootSv->Measure(aero::Size{300.0f, 120.0f});
    rootSv->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 120.0f});

    rootSv->ScrollToVerticalOffset(29880.0f);
    rootSv->Measure(aero::Size{300.0f, 120.0f});
    rootSv->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 120.0f});

    AERO_CHECK(bindingEngine.ReactiveBindings().size() == bindingSlotsAfterMidScroll,
               "RebindSubtreeRootObject & recyclePool recycled Track A binding slots in-place (0 new slots allocated on scroll)");

    bool foundLastPlayer = false;
    for (aero::UIElement* c = board->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (auto* lbl = c->FindName("TagLabel")) {
            if (lbl->DynamicCast<aero::TextBlock>()->GetText() == "Player_999") {
                foundLastPlayer = true;
                break;
            }
        }
    }
    AERO_CHECK(foundLastPlayer, "Recycled DataTemplate container bound and rendered 'Player_999' at bottom of scroll");

    std::cout << "[PASS] TestVirtualizedItemsControlDataTemplateInPlaceRecycling\n";
}

// ============================================================================
// 5. Declarative <EventTrigger> (Loaded, Button.Click, ToggleButton.Checked)
// ============================================================================
void TestDeclarativeEventTriggersLoadedClickAndChecked() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            Width="300" Height="200">
    <Border x:Name="IntroCard" Width="200" Height="40" Opacity="0.1">
        <Border.Triggers>
            <EventTrigger RoutedEvent="FrameworkElement.Loaded">
                <BeginStoryboard>
                    <Storyboard>
                        <DoubleAnimation Storyboard.TargetProperty="Opacity" To="1.0" Duration="0:0:0.1"/>
                    </Storyboard>
                </BeginStoryboard>
            </EventTrigger>
        </Border.Triggers>
    </Border>

    <Button x:Name="PulseBtn" Content="Fire" Width="120" Height="36" RenderTransformOrigin="0.5,0.5">
        <Button.Triggers>
            <EventTrigger RoutedEvent="Button.Click">
                <BeginStoryboard>
                    <Storyboard>
                        <DoubleAnimation Storyboard.TargetProperty="(UIElement.RenderTransform).(ScaleTransform.ScaleX)"
                                         From="1.0" To="1.35" Duration="0:0:0.1"/>
                    </Storyboard>
                </BeginStoryboard>
            </EventTrigger>
        </Button.Triggers>
    </Button>

    <CheckBox x:Name="ArmToggle" Content="Arm" Width="120" Height="28">
        <CheckBox.Triggers>
            <EventTrigger RoutedEvent="ToggleButton.Checked">
                <BeginStoryboard>
                    <Storyboard>
                        <DoubleAnimation Storyboard.TargetProperty="Opacity" From="0.5" To="0.95" Duration="0:0:0.1"/>
                    </Storyboard>
                </BeginStoryboard>
            </EventTrigger>
        </CheckBox.Triggers>
    </CheckBox>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "EventTriggersView.xaml";
    input.xamlContent = xaml;
    const auto res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled EventTriggersView XAML");

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);

    AERO_CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine),
               "Mounted EventTriggersView");
    root->Measure(aero::Size{300.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 200.0f});

    styleEngine.RegisterPackageStyles(res.axbPackage);
    AERO_CHECK(styleEngine.BindSubtreeStyles(root.Get()) == 3u, "Bound inline EventTrigger styles");

    auto* introCard = root->FindName("IntroCard");
    auto* pulseBtn = root->FindName("PulseBtn")->DynamicCast<aero::Button>();
    auto* armToggle = root->FindName("ArmToggle")->DynamicCast<aero::CheckBox>();

    // First EvaluateAndApply fires FrameworkElement.Loaded on IntroCard!
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(animEngine.HasActiveAnimations(), "FrameworkElement.Loaded EventTrigger started IntroCard fade-in");
    for (int i = 0; i < 10; ++i) (void)animEngine.Tick(0.016f);
    AERO_CHECK(ApproxEq(introCard->GetOpacity(), 1.0f, 0.01f), "IntroCard Loaded storyboard reached Opacity=1.0");

    // Click PulseBtn via InputEventRouter -> fires Button.Click EventTrigger!
    aero::InputEventRouter router(root.Get());
    const aero::Rect btnRect = pulseBtn->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{btnRect.x + 20.0f, btnRect.y + 12.0f});
    (void)router.DispatchPointerUp(aero::Point{btnRect.x + 20.0f, btnRect.y + 12.0f});
    (void)styleEngine.EvaluateAndApply();
    for (int i = 0; i < 10; ++i) (void)animEngine.Tick(0.016f);
    float btnScale = 1.0f;
    (void)aero::TryGetFloat(pulseBtn->GetValue(aero::PropId_AnimScaleX), btnScale);
    AERO_CHECK(ApproxEq(btnScale, 1.35f, 0.01f),
               "Button.Click EventTrigger animated PulseBtn ScaleX to 1.35");

    // Toggle ArmToggle on -> fires ToggleButton.Checked EventTrigger!
    armToggle->SetIsChecked(true);
    (void)styleEngine.EvaluateAndApply();
    for (int i = 0; i < 10; ++i) (void)animEngine.Tick(0.016f);
    AERO_CHECK(ApproxEq(armToggle->GetOpacity(), 0.95f, 0.01f),
               "ToggleButton.Checked EventTrigger animated ArmToggle Opacity to 0.95");

    std::cout << "[PASS] TestDeclarativeEventTriggersLoadedClickAndChecked\n";
}

// ============================================================================
// 6. .axb4 "DTP5" Binary Round-Trip & --aot-cpp Registration Verification
// ============================================================================
void TestAxb4Dtp5RoundTripAndAotCppGenerationEndToEnd() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            x:Class="Samples.Phase5D.DtpRoundTrip">
    <StackPanel.Resources>
        <DataTemplate x:Key="BadgeTpl">
            <Border Height="24"><TextBlock Text="{Binding Label}"/></Border>
        </DataTemplate>
        <Style x:Key="CondCard" TargetType="Border">
            <Style.Triggers>
                <DataTrigger Binding="{Binding Status}" Value="Ready">
                    <Setter Property="Opacity" Value="0.9"/>
                </DataTrigger>
                <EventTrigger RoutedEvent="FrameworkElement.Loaded">
                    <BeginStoryboard>
                        <Storyboard>
                            <DoubleAnimation Storyboard.TargetProperty="Opacity" To="1.0" Duration="0:0:0.1"/>
                        </Storyboard>
                    </BeginStoryboard>
                </EventTrigger>
            </Style.Triggers>
        </Style>
    </StackPanel.Resources>
    <CheckBox x:Name="FlagBox" IsChecked="True"/>
    <Border x:Name="LinkedBorder"
            Visibility="{Binding IsChecked, ElementName=FlagBox, Converter={StaticResource boolToVisibility}}"
            Style="{StaticResource CondCard}"/>
    <ItemsControl ItemTemplate="{StaticResource BadgeTpl}"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "DtpRoundTrip.xaml";
    input.xamlContent = xaml;
    input.emitAotCpp = true;
    input.aotCppNamespace = "aero::test_phase5d_aot";

    const auto res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled DtpRoundTrip XAML + AOT C++");
    AERO_CHECK(res.aotCpp.source.find("RegisterDataTemplate") != std::string::npos &&
                   res.aotCpp.source.find("AxbEventTrigger") != std::string::npos &&
                   res.aotCpp.source.find("TriggerConditionSource") != std::string::npos,
               "--aot-cpp emitted RegisterDataTemplate, AxbEventTrigger, and DataTrigger condition code");

    aero::AxbPackage decoded{};
    std::string errMsg;
    AERO_CHECK(aero::AxbPackage::Deserialize(res.axbBytes, decoded, &errMsg),
               ".axb4 DTP5 binary section deserialized cleanly");
    AERO_CHECK(decoded.dataTemplates.size() == 1u &&
                   decoded.dataTemplates[0].key == "BadgeTpl" &&
                   decoded.dataTemplates[0].templatePackage != nullptr,
               "Decoded nested DataTemplate sub-package from DTP5 section");
    AERO_CHECK(decoded.bindingChains.size() == 1u &&
                   decoded.bindingChains[0].elementName == "FlagBox" &&
                   decoded.bindingChains[0].converter == aero::ValueConverterKind::BooleanToVisibility,
               "Decoded ElementName='FlagBox' and BooleanToVisibility converter from DTP5 section");
    AERO_CHECK(decoded.styles.size() == 1u &&
                   decoded.styles[0].eventTriggers.size() == 1u &&
                   decoded.styles[0].triggers.size() == 1u &&
                   decoded.styles[0].triggers[0].conditions[0].sourceKind ==
                       aero::TriggerConditionSource::DataContextBinding &&
                   decoded.styles[0].triggers[0].conditions[0].bindingPath == "Status",
               "Decoded DataTrigger bindingPath and EventTrigger from DTP5 section");

    std::cout << "[PASS] TestAxb4Dtp5RoundTripAndAotCppGenerationEndToEnd\n";
}

// ============================================================================
// 7. Phase 5D Review Regression Fixes (Multi-DataTemplate Triggers, Root Style
//    Chaining, Immediate Virtualized Trigger Evaluation & DTP5 Corrupt Enum Guard)
// ============================================================================
void TestPhase5DReviewRegressionFixes() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Samples.Phase5DReview;

[Observable]
public class RowItem
{
    public bool IsDanger { get; set; } = false;
    public string Title { get; set; } = "";
}

[Observable]
public partial class ReviewHostVm
{
    public RowItem[] Rows { get; set; } = null;
}

public ReviewHostVm CreateReviewHostVm()
{
    ReviewHostVm vm = new ReviewHostVm();
    vm.Rows = new RowItem[20];
    return vm;
}

public RowItem CreateRowItem()
{
    return new RowItem();
}
)RS";

    const char* xaml = R"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
              x:Class="Samples.Phase5DReview.ReviewHostVm"
              Width="240" Height="50">
    <ScrollViewer.Resources>
        <Style x:Key="BaseRowCard" TargetType="Border">
            <Setter Property="CornerRadius" Value="6"/>
            <Setter Property="Opacity" Value="0.6"/>
        </Style>

        <DataTemplate x:Key="FirstTpl" DataType="Samples.Phase5DReview.RowItem">
            <Border Style="{StaticResource BaseRowCard}" Height="25">
                <TextBlock Text="{Binding Title}"/>
            </Border>
            <DataTemplate.Triggers>
                <DataTrigger Binding="{Binding IsDanger}" Value="True">
                    <Setter Property="Opacity" Value="1.0"/>
                </DataTrigger>
            </DataTemplate.Triggers>
        </DataTemplate>

        <DataTemplate x:Key="SecondTpl" DataType="Samples.Phase5DReview.RowItem">
            <Border Style="{StaticResource BaseRowCard}" Height="25">
                <TextBlock Text="{Binding Title}"/>
            </Border>
            <DataTemplate.Triggers>
                <DataTrigger Binding="{Binding IsDanger}" Value="True">
                    <Setter Property="Opacity" Value="0.25"/>
                </DataTrigger>
            </DataTemplate.Triggers>
        </DataTemplate>
    </ScrollViewer.Resources>

    <ItemsControl x:Name="VirtRows"
                  IsVirtualizing="True"
                  ItemHeight="25"
                  ItemTemplate="{StaticResource FirstTpl}"
                  ItemsSource="{Binding Rows}"/>
</ScrollViewer>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "ReviewHostVm.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"ReviewHostVm.rs", rsCode});

    const auto res = compiler.Compile(input);
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled Phase 5D review regression XAML+RS");
    AERO_CHECK(res.axbPackage.dataTemplates.size() == 2u, "Compiled both FirstTpl and SecondTpl DataTemplates");
    AERO_CHECK(res.axbPackage.dataTemplates[0].templatePackage->styles.size() == 1u &&
                   res.axbPackage.dataTemplates[1].templatePackage->styles.size() == 1u &&
                   res.axbPackage.dataTemplates[0].templatePackage->styles[0].key !=
                       res.axbPackage.dataTemplates[1].templatePackage->styles[0].key &&
                   res.axbPackage.dataTemplates[0].templatePackage->styles[0].basedOnKey == "BaseRowCard",
               "Multiple <DataTemplate.Triggers> have distinct style keys and chain BasedOn to root Style");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    const auto vmExec = uiInterp.invoke("Samples.Phase5DReview::CreateReviewHostVm");
    AERO_CHECK(vmExec.succeeded, "Created ReviewHostVm with 20-slot array");
    const aero::ObjectRef vmObj = std::get<aero::ObjectRef>(vmExec.value);
    const auto pinVm = domains.uiHeap().retain(vmObj);

    const aero::SymbolId hostTypeId = aero::rs_sem::stableTypeId("Samples.Phase5DReview::ReviewHostVm");
    const aero::SymbolId rowTypeId = aero::rs_sem::stableTypeId("Samples.Phase5DReview::RowItem");
    const auto rowsSlot = *domains.resolvePropertySlot(*res.programImage, hostTypeId, "Rows");
    const auto dangerSlot = *domains.resolvePropertySlot(*res.programImage, rowTypeId, "IsDanger");
    const auto titleSlot = *domains.resolvePropertySlot(*res.programImage, rowTypeId, "Title");

    aero::UnifiedValue arrVal{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmObj, rowsSlot, arrVal, nullptr, &uiInterp);
    const aero::ObjectRef arrObj = std::get<aero::ObjectRef>(arrVal);

    for (std::size_t i = 0; i < 20u; ++i) {
        const auto rExec = uiInterp.invoke("Samples.Phase5DReview::CreateRowItem");
        const aero::ObjectRef row = std::get<aero::ObjectRef>(rExec.value);
        auto titleRef = *domains.uiHeap().allocateString("Row_" + std::to_string(i));
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, row, dangerSlot, aero::UnifiedValue{i >= 10u});
        (void)domains.writePropertySlotFast(aero::DomainKind::Ui, row, titleSlot, titleRef);
        (void)domains.uiHeap().arraySet(arrObj, i, row);
    }

    aero::DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);

    auto root = aero::MakeIntrusive<aero::ScrollViewer>();
    styleEngine.RegisterPackageStyles(res.axbPackage);
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine, vmObj, &uiInterp),
               "Mounted ReviewHostVm view");
    (void)styleEngine.BindSubtreeStyles(root.Get());

    auto* virtRows = root->FindName("VirtRows")->DynamicCast<aero::ItemsControl>();
    AERO_CHECK(virtRows != nullptr, "Resolved VirtRows ItemsControl");

    // Initial layout at scroll offset 0 (rows 0..1 have IsDanger == false -> Opacity 0.6, CornerRadius 6 from BaseRowCard)
    root->Measure(aero::Size{240.0f, 50.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 240.0f, 50.0f});
    AERO_CHECK(virtRows->RealizedItemCount() > 0u, "VirtRows realized initial virtualized containers");
    auto* firstBorder = virtRows->FirstChild()->DynamicCast<aero::Border>();
    AERO_CHECK(firstBorder != nullptr &&
                   ApproxEq(firstBorder->GetOpacity(), 0.6f) &&
                   ApproxEq(firstBorder->GetCornerRadius().topLeft, 6.0f),
               "Templated row inherited BaseRowCard CornerRadius=6 and Opacity=0.6 on initial Measure()");

    // Scroll directly to danger rows (offset 300px -> rows 12+ have IsDanger == true -> Opacity 1.0 immediately on Measure)
    root->ScrollToVerticalOffset(300.0f);
    root->Measure(aero::Size{240.0f, 50.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 240.0f, 50.0f});
    auto* recycledBorder = virtRows->FirstChild()->DynamicCast<aero::Border>();
    AERO_CHECK(recycledBorder != nullptr && ApproxEq(recycledBorder->GetOpacity(), 1.0f),
               "Recycled virtualized container immediately evaluated DataTemplate.Triggers on Measure()");

    std::cout << "[PASS] TestPhase5DReviewRegressionFixes\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    TestZeroAllocDataTriggerMultiDataTriggerAndNullConditions();
    TestElementNameRelativeSourceAndBuiltinValueConvertersTwoWay();
    TestDeclarativeDataTemplateAndItemsControlMounting();
    TestVirtualizedItemsControlDataTemplateInPlaceRecycling();
    TestDeclarativeEventTriggersLoadedClickAndChecked();
    TestAxb4Dtp5RoundTripAndAotCppGenerationEndToEnd();
    TestPhase5DReviewRegressionFixes();

    std::cout << "AeroGUI 2.0 Phase 5D DataTrigger, EventTrigger, ElementName/Converters & DataTemplate Tests Passed 100%!\n";
    return 0;
}
