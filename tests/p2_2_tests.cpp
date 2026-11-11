#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace {

static_assert(sizeof(aero::rs_rt::Value) == 16, "realscript::runtime::Value ABI must be 16 bytes");
static_assert(sizeof(aero::UnifiedValue) == 16, "aero::UnifiedValue ABI must be 16 bytes");

int gPassed = 0;
int gFailed = 0;

void CheckImpl(bool cond, const char* expr, const char* msg, int line) {
    if (cond) {
        ++gPassed;
    } else {
        ++gFailed;
        std::fprintf(stderr, "[FAIL] p2_2_tests.cpp:%d: %s (%s)\n", line, msg, expr);
    }
}

#define AERO_CHECK(cond, msg) CheckImpl((cond), #cond, (msg), __LINE__)

[[nodiscard]] bool ApproxEq(float a, float b, float eps = 1e-3f) noexcept {
    return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool ColorApproxEq(const aero::Color& a, const aero::Color& b, float eps = 1e-2f) noexcept {
    return ApproxEq(a.r, b.r, eps) &&
           ApproxEq(a.g, b.g, eps) &&
           ApproxEq(a.b, b.b, eps) &&
           ApproxEq(a.a, b.a, eps);
}

// ============================================================================
// Suite 1: RealScript ManagedHeap & ObservableListHelper Delta Ring Unit Tests
// ============================================================================
void TestRealScriptDeltaRingAndRingOverflowFallback() {
    std::printf("--- [Suite 1] RealScript Array / ObservableList Delta Ring & Overflow Fallback ---\n");

    aero::ExecutionDomainContext domains;
    auto& heap = domains.uiHeap();

    // Allocate an int[] array and enable tracking via ObservableListHelper
    auto arrOpt = heap.allocateTypedArray(
        0, aero::PrimitiveType::Int, 0, 2u, aero::value::FromInt(0));
    AERO_CHECK(arrOpt.has_value(), "Allocated typed int array on UiHeap");
    const aero::ObjectRef arrRef = *arrOpt;
    const auto pinArr = heap.retain(arrRef);

    (void)heap.arraySet(arrRef, 0u, aero::value::FromInt(10));
    (void)heap.arraySet(arrRef, 1u, aero::value::FromInt(20));

    // Create a mock owner Record to observe dirty bit wake-ups
    const aero::SymbolId ownerTid = aero::rs_sem::stableTypeId("Test.P22::MockVm");
    heap.registerTypeObservableBits(ownerTid, {3u});
    auto ownerOpt = heap.allocateObject(ownerTid, {aero::UnifiedValue{arrRef}}, {0u});
    AERO_CHECK(ownerOpt.has_value(), "Allocated owner ViewModel holding array field");
    const aero::ObjectRef ownerRef = *ownerOpt;
    const auto pinOwner = heap.retain(ownerRef);

    // Clear initial dirty state
    (void)heap.consumeDirtyObjects();
    heap.clearDirtyFieldMask(ownerRef);
    const std::uint64_t v0 = aero::ObservableListHelper::MutationVersion(heap, arrRef);
    AERO_CHECK(v0 == 0ULL, "Initial mutationVersion is 0");

    // Perform Add, Insert, Replace, RemoveAt via ObservableListHelper
    AERO_CHECK(aero::ObservableListHelper::Add(heap, arrRef, aero::value::FromInt(30)), "Add(30)");
    AERO_CHECK(aero::ObservableListHelper::Insert(heap, arrRef, 1u, aero::value::FromInt(15)), "Insert(1, 15)");
    AERO_CHECK(aero::ObservableListHelper::Replace(heap, arrRef, 2u, aero::value::FromInt(25)), "Replace(2, 25)");
    aero::UnifiedValue removedVal{};
    AERO_CHECK(aero::ObservableListHelper::RemoveAt(heap, arrRef, 0u, &removedVal), "RemoveAt(0)");
    AERO_CHECK(std::holds_alternative<std::int64_t>(removedVal) && std::get<std::int64_t>(removedVal) == 10,
               "RemoveAt returned old element 10");

    // Verify array contents: [15, 25, 30]
    AERO_CHECK(heap.arrayLength(arrRef).value_or(0u) == 3u, "Array length is 3 after mutations");
    AERO_CHECK(std::get<std::int64_t>(*heap.arrayGet(arrRef, 0u)) == 15 &&
               std::get<std::int64_t>(*heap.arrayGet(arrRef, 1u)) == 25 &&
               std::get<std::int64_t>(*heap.arrayGet(arrRef, 2u)) == 30,
               "Array contents match [15, 25, 30]");

    // Verify owner dirty bit was automatically set!
    AERO_CHECK((heap.dirtyFieldMask(ownerRef) & (1ULL << 3u)) != 0ULL,
               "Array mutations automatically set owner ViewModel observable bit 3");
    const auto dirtyObjs = heap.consumeDirtyObjects();
    AERO_CHECK(dirtyObjs.size() == 1u && dirtyObjs[0] == ownerRef,
               "Owner ViewModel queued once in consumeDirtyObjects()");
    heap.clearDirtyFieldMask(ownerRef);

    // Read delta records since v0
    std::vector<aero::CollectionChangeRecord> deltas;
    std::uint64_t curVer = 0ULL;
    bool requiresReset = false;
    AERO_CHECK(
        aero::ObservableListHelper::ReadChangesSince(heap, arrRef, v0, deltas, curVer, requiresReset),
        "ReadChangesSince succeeded");
    AERO_CHECK(!requiresReset && curVer == 4ULL && deltas.size() == 4u,
               "Recorded exact 4 ordered delta records without overflow");
    AERO_CHECK(deltas[0].kind == aero::CollectionChangeKind::Add && deltas[0].index == 2u,
               "Delta[0] is Add at index 2");
    AERO_CHECK(deltas[1].kind == aero::CollectionChangeKind::Insert && deltas[1].index == 1u,
               "Delta[1] is Insert at index 1");
    AERO_CHECK(deltas[2].kind == aero::CollectionChangeKind::Replace && deltas[2].index == 2u &&
               std::get<std::int64_t>(deltas[2].oldValue) == 20 &&
               std::get<std::int64_t>(deltas[2].newValue) == 25,
               "Delta[2] is Replace at index 2 (20 -> 25)");
    AERO_CHECK(deltas[3].kind == aero::CollectionChangeKind::RemoveAt && deltas[3].index == 0u &&
               std::get<std::int64_t>(deltas[3].oldValue) == 10,
               "Delta[3] is RemoveAt at index 0 (old=10)");

    // Overflow ring (> 256 mutations without consuming) -> requiresReset == true
    for (std::size_t i = 0; i < aero::ManagedHeap::kCollectionDeltaRingCapacity + 40u; ++i) {
        (void)aero::ObservableListHelper::Add(heap, arrRef, aero::value::FromInt(static_cast<std::int64_t>(100 + i)));
    }
    std::uint64_t overflowVer = 0ULL;
    AERO_CHECK(
        aero::ObservableListHelper::ReadChangesSince(heap, arrRef, curVer, deltas, overflowVer, requiresReset) &&
        requiresReset && deltas.empty(),
        "Exceeding bounded delta ring capacity automatically degrades to Reset");

    // Subsequent single mutation after syncing to overflowVer resumes incremental delivery
    (void)aero::ObservableListHelper::Clear(heap, arrRef);
    std::uint64_t afterClearVer = 0ULL;
    AERO_CHECK(
        aero::ObservableListHelper::ReadChangesSince(heap, arrRef, overflowVer, deltas, afterClearVer, requiresReset) &&
        !requiresReset && deltas.size() == 1u && deltas[0].kind == aero::CollectionChangeKind::Clear &&
        heap.arrayLength(arrRef).value_or(99u) == 0u,
        "Clear recorded as single incremental Clear delta after resync");
}

// ============================================================================
// Helper to verify 100% parity between Incremental ListBox and Full-Rebuild Oracle
// ============================================================================
void AssertListBoxParityWithFullOracle(
    const aero::ScrollViewer* incSv,
    const aero::ListBox* incList,
    const aero::ScrollViewer* refSv,
    const aero::ListBox* refList,
    const char* stepName) {
    AERO_CHECK(incList->TotalItemCount() == refList->TotalItemCount(), stepName);
    AERO_CHECK(incList->RealizedItemCount() == refList->RealizedItemCount(), stepName);
    AERO_CHECK(incList->FirstRealizedItemIndex() == refList->FirstRealizedItemIndex(), stepName);
    AERO_CHECK(incList->RealizedWindowEndExclusive() == refList->RealizedWindowEndExclusive(), stepName);
    AERO_CHECK(ApproxEq(incSv->ExtentHeight(), refSv->ExtentHeight(), 0.05f), stepName);
    AERO_CHECK(ApproxEq(incSv->ScrollableHeight(), refSv->ScrollableHeight(), 0.05f), stepName);
    AERO_CHECK(incList->GetSelectedIndex() == refList->GetSelectedIndex(), stepName);
    AERO_CHECK(incList->GetSelectedItem() == refList->GetSelectedItem(), stepName);

    aero::UIElement* cInc = const_cast<aero::ListBox*>(incList)->FirstChild();
    aero::UIElement* cRef = const_cast<aero::ListBox*>(refList)->FirstChild();
    while (cInc != nullptr && cRef != nullptr) {
        const aero::Rect rInc = cInc->FinalRect();
        const aero::Rect rRef = cRef->FinalRect();
        AERO_CHECK(
            ApproxEq(rInc.x, rRef.x, 0.05f) &&
            ApproxEq(rInc.y, rRef.y, 0.05f) &&
            ApproxEq(rInc.width, rRef.width, 0.05f) &&
            ApproxEq(rInc.height, rRef.height, 0.05f),
            stepName);

        auto* titleInc = cInc->FindName("TitleText") ? cInc->FindName("TitleText")->DynamicCast<aero::TextBlock>() : nullptr;
        auto* titleRef = cRef->FindName("TitleText") ? cRef->FindName("TitleText")->DynamicCast<aero::TextBlock>() : nullptr;
        auto* powerInc = cInc->FindName("PowerText") ? cInc->FindName("PowerText")->DynamicCast<aero::TextBlock>() : nullptr;
        auto* powerRef = cRef->FindName("PowerText") ? cRef->FindName("PowerText")->DynamicCast<aero::TextBlock>() : nullptr;
        auto* cardInc = cInc->FindName("ItemCard") ? cInc->FindName("ItemCard")->DynamicCast<aero::Border>() : nullptr;
        auto* cardRef = cRef->FindName("ItemCard") ? cRef->FindName("ItemCard")->DynamicCast<aero::Border>() : nullptr;

        AERO_CHECK(titleInc && titleRef && titleInc->GetText() == titleRef->GetText(), stepName);
        AERO_CHECK(powerInc && powerRef && powerInc->GetText() == powerRef->GetText(), stepName);
        AERO_CHECK(cardInc && cardRef && ColorApproxEq(cardInc->GetBackground(), cardRef->GetBackground()), stepName);

        cInc = cInc->NextSibling();
        cRef = cRef->NextSibling();
    }
    AERO_CHECK(cInc == nullptr && cRef == nullptr, stepName);
}

// ============================================================================
// Suite 2 & 3: 10,000-Item Virtualized ListBox Bound to RealScript ObservableList
//              (5 Incremental Ops + 100% Full-Rebuild Parity + 0 UIElement Churn + Benchmark)
// ============================================================================
struct PerfMetrics {
    float outOfViewReplaceUs = 0.0f;
    float inViewReplaceUs = 0.0f;
    float outOfViewAddUs = 0.0f;
    float fullRebuildUs = 0.0f;
};

PerfMetrics Test10kVirtualizedListBoxIncrementalSyncAndBenchmark() {
    std::printf("--- [Suite 2 & 3] 10,000-Item Virtualized ListBox Incremental Sync, Zero-Alloc & Benchmark ---\n");
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Game.InventoryP22;

[Observable]
public class BagItemVm
{
    public string Title { get; set; } = "";
    public int Power { get; set; } = 0;
    public bool IsRare { get; set; } = false;
}

[Observable]
public partial class BagPanelVm
{
    public ObservableList<BagItemVm> Items { get; set; } = null;

    public void AppendItem(BagItemVm item)
    {
        Items.Add(item);
    }

    public void InsertItem(int index, BagItemVm item)
    {
        Items.Insert(index, item);
    }

    public void ReplaceItem(int index, BagItemVm item)
    {
        Items.Set(index, item);
    }

    public void RemoveItem(int index)
    {
        Items.RemoveAt(index);
    }

    public void ClearItems()
    {
        Items.Clear();
    }
}

public BagItemVm MakeBagItem(string title, int power, bool rare)
{
    BagItemVm it = new BagItemVm();
    it.Title = title;
    it.Power = power;
    it.IsRare = rare;
    return it;
}

public BagPanelVm Create10kBagVm()
{
    BagPanelVm vm = new BagPanelVm();
    vm.Items = new ObservableList<BagItemVm>(10016);
    int i = 0;
    while (i < 10000)
    {
        BagItemVm entry = new BagItemVm();
        entry.Title = "Blade_" + i;
        entry.Power = i * 10;
        entry.IsRare = (i % 5 == 0);
        vm.Items.Add(entry);
        i = i + 1;
    }
    return vm;
}
)RS";

    const char* xaml = R"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
              x:DataType="Game.InventoryP22::BagPanelVm"
              Width="360" Height="240">
    <ScrollViewer.Resources>
        <DataTemplate x:Key="BagRowTpl" DataType="Game.InventoryP22.BagItemVm">
            <Border x:Name="ItemCard" Height="30" Background="#FF1E293B">
                <StackPanel Orientation="Horizontal" Spacing="8">
                    <TextBlock x:Name="TitleText" Text="{Binding Title}"/>
                    <TextBlock x:Name="PowerText" Text="{Binding Power, StringFormat='PWR:{0}'}"/>
                </StackPanel>
            </Border>
            <DataTemplate.Triggers>
                <DataTrigger Binding="{Binding IsRare}" Value="True">
                    <Setter TargetName="ItemCard" Property="Background" Value="#FF7C3AED"/>
                </DataTrigger>
            </DataTemplate.Triggers>
        </DataTemplate>
    </ScrollViewer.Resources>

    <ListBox x:Name="BagList"
             IsVirtualizing="True"
             ItemHeight="30"
             ItemTemplate="{StaticResource BagRowTpl}"
             ItemsSource="{Binding Items}"/>
</ScrollViewer>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "BagPanel.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"BagPanelVm.rs", rsCode});

    const aero::AeroCompileResult res = compiler.Compile(input);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) {
            std::fprintf(stderr, "  compile diag: %s\n", d.c_str());
        }
    }
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled 10k BagPanel XAML + RealScript ObservableList");
    if (!res.succeeded || !res.programImage) {
        return {};
    }

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    aero::rs_rt::ExecutionOptions bigBudget{};
    bigBudget.limits.instructionBudget = 50'000'000;
    const auto vmExec = uiInterp.invoke(
        *res.programImage->findFunction("Game.InventoryP22::Create10kBagVm"),
        {},
        bigBudget);
    AERO_CHECK(vmExec.succeeded, "Constructed BagPanelVm with 10,000 items in RealScript");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const auto pinVm = domains.uiHeap().retain(vmRef);

    const aero::SymbolId vmTypeId = aero::rs_sem::stableTypeId("Game.InventoryP22::BagPanelVm");
    const auto itemsSlot = *domains.resolvePropertySlot(*res.programImage, vmTypeId, "Items");
    const aero::SymbolId makeItemSym = *res.programImage->findFunction("Game.InventoryP22::MakeBagItem");
    const aero::SymbolId appendSym = *res.programImage->findFunction("Game.InventoryP22::BagPanelVm.AppendItem");
    const aero::SymbolId insertSym = *res.programImage->findFunction("Game.InventoryP22::BagPanelVm.InsertItem");
    const aero::SymbolId replaceSym = *res.programImage->findFunction("Game.InventoryP22::BagPanelVm.ReplaceItem");
    const aero::SymbolId removeSym = *res.programImage->findFunction("Game.InventoryP22::BagPanelVm.RemoveItem");
    const aero::SymbolId clearSym = *res.programImage->findFunction("Game.InventoryP22::BagPanelVm.ClearItems");

    auto makeItem = [&](const std::string& title, std::int64_t power, bool rare) -> aero::ObjectRef {
        const auto sRef = *domains.uiHeap().allocateString(title);
        const auto r = uiInterp.invoke(makeItemSym, {sRef, aero::value::FromInt(power), aero::UnifiedValue{rare}});
        return std::get<aero::ObjectRef>(r.value);
    };

    // Clear initial setup dirty flags before mounting
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(vmRef);

    // 1. Mount Incremental Primary View (bound via DualTrackBindingEngine)
    aero::DualTrackBindingEngine incBindingEngine(domains, res.programImage.get(), &uiInterp);
    aero::StyleTriggerEngine incStyleEngine(nullptr, &incBindingEngine);
    incStyleEngine.RegisterPackageStyles(res.axbPackage);

    auto incSv = aero::MakeIntrusive<aero::ScrollViewer>();
    AERO_CHECK(
        aero::MountAxbTemplate(incSv.Get(), res.axbPackage, &domains, &incBindingEngine, vmRef, &uiInterp),
        "Mounted incremental ScrollViewer + ListBox");
    (void)incStyleEngine.BindSubtreeStyles(incSv.Get());
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();

    auto* incList = incSv->FindName("BagList")->DynamicCast<aero::ListBox>();
    AERO_CHECK(incList != nullptr && incList->IsVirtualizing(), "Resolved virtualized ListBox");
    AERO_CHECK(incList->TotalItemCount() == 10000u, "Initial TotalItemCount == 10000");
    AERO_CHECK(incList->RealizedItemCount() == 10u, "Viewport 240px / 30px + overscan 2 realized 10 slots");
    AERO_CHECK(ApproxEq(incSv->ExtentHeight(), 300000.0f), "Initial ExtentHeight == 300,000px");

    // 2. Mount Full-Rebuild Oracle View (unbound ItemsSource; manually fed full 10k vector on each check)
    aero::DualTrackBindingEngine refBindingEngine(domains, res.programImage.get(), &uiInterp);
    aero::StyleTriggerEngine refStyleEngine(nullptr, &refBindingEngine);
    refStyleEngine.RegisterPackageStyles(res.axbPackage);

    auto refSv = aero::MakeIntrusive<aero::ScrollViewer>();
    auto refListPtr = aero::MakeIntrusive<aero::ListBox>();
    refListPtr->SetName("BagList");
    refListPtr->SetIsVirtualizing(true);
    refListPtr->SetVirtualItemHeight(30.0f);
    refListPtr->SetItemTemplateKey("BagRowTpl");
    refListPtr->SetDomains(&domains);
    refListPtr->SetBindingEngine(&refBindingEngine);
    refListPtr->SetStyleEngine(&refStyleEngine);
    refSv->AddChild(refListPtr.Get(), &domains.nativeHandles(), &domains.uiHeap());

    auto syncFullOracle = [&]() {
        aero::UnifiedValue listObjVal{};
        (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmRef, itemsSlot, listObjVal, nullptr, &uiInterp);
        std::vector<aero::UnifiedValue> fullVec;
        if (const auto* lRef = std::get_if<aero::ObjectRef>(&listObjVal)) {
            const aero::ObjectRef backing = aero::ObservableListHelper::ResolveBackingArray(domains.uiHeap(), *lRef);
            if (backing.valid()) {
                const std::size_t n = domains.uiHeap().arrayLength(backing).value_or(0u);
                fullVec.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    fullVec.push_back(domains.uiHeap().arrayGet(backing, i).value_or(aero::UnifiedValue{}));
                }
            }
        }
        refListPtr->SetItemsSource(fullVec, &domains.nativeHandles(), &domains.uiHeap());
        refSv->Measure(aero::Size{360.0f, 240.0f});
        refSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
        (void)refStyleEngine.EvaluateAndApply();
    };

    syncFullOracle();
    // Set initial SelectedIndex = 4 on both views ("Blade_4")
    incList->SetSelectedIndex(4);
    refListPtr->SetSelectedIndex(4);
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "Initial 10,000 state");

    // ------------------------------------------------------------------------
    // Operation 1: Tail Add (Out-of-viewport Add at index 10,000)
    // Must create 0 UIElements and destroy 0 UIElements!
    // ------------------------------------------------------------------------
    const aero::ObjectRef tailItem = makeItem("Mythic_Tail_10000", 99999, true);
    const auto pinTail = domains.uiHeap().retain(tailItem);
    AERO_CHECK(uiInterp.invoke(appendSym, {vmRef, tailItem}).succeeded, "RealScript vm.AppendItem executed");
    AERO_CHECK(incBindingEngine.HasPendingDirtyObjects(), "ObservableList.Add automatically woke Track A dirty flag");

    const std::uint64_t cBeforeAdd = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dBeforeAdd = aero::UIElement::TotalDestroyedCount();
    const std::size_t updatedAdd = incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    const std::uint64_t cAfterAdd = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dAfterAdd = aero::UIElement::TotalDestroyedCount();

    AERO_CHECK(updatedAdd == 1u, "FlushTrackA processed incremental Add");
    AERO_CHECK((cAfterAdd - cBeforeAdd) == 0ULL && (dAfterAdd - dBeforeAdd) == 0ULL,
               "Out-of-viewport Add on 10,000 items constructed 0 and destroyed 0 UIElements");
    AERO_CHECK(incList->TotalItemCount() == 10001u && ApproxEq(incSv->ExtentHeight(), 300030.0f),
               "ExtentHeight updated to 300,030px in O(1)");

    syncFullOracle();
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After Tail Add");

    // ------------------------------------------------------------------------
    // Operation 2: Head & In-Viewport Insert (Insert at index 0 and index 2)
    // SelectedIndex (4) must shift -> 5 -> 6! Reuses scrolled-out slot (0 new UIElements!)
    // ------------------------------------------------------------------------
    const aero::ObjectRef headItem = makeItem("Inserted_Head_0", 777, true);
    const aero::ObjectRef midVisItem = makeItem("Inserted_Vis_2", 888, false);
    const auto pinHead = domains.uiHeap().retain(headItem);
    const auto pinMidVis = domains.uiHeap().retain(midVisItem);

    const std::uint64_t cBeforeIns = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dBeforeIns = aero::UIElement::TotalDestroyedCount();
    AERO_CHECK(uiInterp.invoke(insertSym, {vmRef, aero::value::FromInt(0), headItem}).succeeded, "Insert at 0");
    AERO_CHECK(uiInterp.invoke(insertSym, {vmRef, aero::value::FromInt(2), midVisItem}).succeeded, "Insert at 2");
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    const std::uint64_t cAfterIns = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dAfterIns = aero::UIElement::TotalDestroyedCount();

    AERO_CHECK((cAfterIns - cBeforeIns) == 0ULL && (dAfterIns - dBeforeIns) == 0ULL,
               "In-viewport Inserts recycled shifted-out containers with 0 UIElement allocations");
    AERO_CHECK(incList->GetSelectedIndex() == 6, "SelectedIndex shifted 4 -> 6 after two preceding inserts");

    syncFullOracle();
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After Head & Viewport Inserts");

    // ------------------------------------------------------------------------
    // Operation 3: Single Replace (In-Viewport index 1 AND Out-of-Viewport index 5000)
    // Both must construct 0 and destroy 0 UIElements!
    // ------------------------------------------------------------------------
    const aero::ObjectRef repVis = makeItem("Replaced_Slot_1", 54321, true);
    const aero::ObjectRef repFar = makeItem("Replaced_Far_5000", 12345, false);
    const auto pinRepVis = domains.uiHeap().retain(repVis);
    const auto pinRepFar = domains.uiHeap().retain(repFar);

    AERO_CHECK(uiInterp.invoke(replaceSym, {vmRef, aero::value::FromInt(1), repVis}).succeeded, "Replace at 1");
    const std::uint64_t cBeforeRepVis = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dBeforeRepVis = aero::UIElement::TotalDestroyedCount();
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    const std::uint64_t cAfterRepVis = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dAfterRepVis = aero::UIElement::TotalDestroyedCount();
    AERO_CHECK((cAfterRepVis - cBeforeRepVis) == 0ULL && (dAfterRepVis - dBeforeRepVis) == 0ULL,
               "In-viewport Replace on 10,000 items constructed 0 and destroyed 0 UIElements");

    AERO_CHECK(uiInterp.invoke(replaceSym, {vmRef, aero::value::FromInt(5000), repFar}).succeeded, "Replace at 5000");
    const std::uint64_t cBeforeRepFar = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dBeforeRepFar = aero::UIElement::TotalDestroyedCount();
    (void)incBindingEngine.FlushTrackA();
    const std::uint64_t cAfterRepFar = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dAfterRepFar = aero::UIElement::TotalDestroyedCount();
    AERO_CHECK((cAfterRepFar - cBeforeRepFar) == 0ULL && (dAfterRepFar - dBeforeRepFar) == 0ULL,
               "Out-of-viewport Replace on 10,000 items constructed 0 and destroyed 0 UIElements");

    syncFullOracle();
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After In-View & Out-of-View Replace");

    // ------------------------------------------------------------------------
    // Operation 4: RemoveAt (Before SelectedIndex -> shifts 6 to 5; then at SelectedIndex -> -1)
    // ------------------------------------------------------------------------
    const std::uint64_t cBeforeRem = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dBeforeRem = aero::UIElement::TotalDestroyedCount();
    AERO_CHECK(uiInterp.invoke(removeSym, {vmRef, aero::value::FromInt(2)}).succeeded, "RemoveAt(2)");
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    const std::uint64_t cAfterRem = aero::UIElement::TotalConstructedCount();
    const std::uint64_t dAfterRem = aero::UIElement::TotalDestroyedCount();

    AERO_CHECK((cAfterRem - cBeforeRem) == 0ULL && (dAfterRem - dBeforeRem) == 0ULL,
               "In-viewport RemoveAt recycled removed container for incoming bottom slot (0 UIElement churn)");
    AERO_CHECK(incList->GetSelectedIndex() == 5, "SelectedIndex shifted 6 -> 5 after RemoveAt(2)");

    syncFullOracle();
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After RemoveAt(2)");

    // Scroll both views to offset 15000px (around index 5000 / 30 = 500, let's check index 5000 at 150000px!)
    incSv->ScrollToVerticalOffset(149940.0f);
    refSv->ScrollToVerticalOffset(149940.0f);
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    refSv->Measure(aero::Size{360.0f, 240.0f});
    refSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)refStyleEngine.EvaluateAndApply();
    AssertListBoxParityWithFullOracle(
        incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "Scrolled to index ~5000 verifying Replaced_Far_5000");

    // Test Insert & RemoveAt BEFORE scrolled viewport (at index 10 while viewport is at ~4998..5010)
    incList->SetSelectedIndex(5002);
    refListPtr->SetSelectedIndex(5002);
    const aero::ObjectRef preViewItem = makeItem("Before_Viewport_10", 333, false);
    const auto pinPreView = domains.uiHeap().retain(preViewItem);
    AERO_CHECK(uiInterp.invoke(insertSym, {vmRef, aero::value::FromInt(10), preViewItem}).succeeded, "Insert before viewport");
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    syncFullOracle();
    AERO_CHECK(incList->GetSelectedIndex() == 5003, "SelectedIndex shifted 5002 -> 5003 on pre-viewport Insert");
    AssertListBoxParityWithFullOracle(
        incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After Insert(10) before scrolled viewport");

    // Delete the currently selected item itself (index 5003) -> SelectedIndex becomes -1
    AERO_CHECK(uiInterp.invoke(removeSym, {vmRef, aero::value::FromInt(5003)}).succeeded, "RemoveAt(5003)");
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    (void)incStyleEngine.EvaluateAndApply();
    syncFullOracle();
    AERO_CHECK(incList->GetSelectedIndex() == -1, "Removing selected item resets SelectedIndex to -1");
    AssertListBoxParityWithFullOracle(
        incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After removing selected item at 5003");

    // ------------------------------------------------------------------------
    // Suite 3: Microsecond Benchmark on 10,000-Item Virtualized ListBox
    //          (Measured BEFORE Clear while 10,002 items are live!)
    // ------------------------------------------------------------------------
    incSv->ScrollToVerticalOffset(0.0f);
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});

    aero::UnifiedValue listObjVal{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmRef, itemsSlot, listObjVal, nullptr, &uiInterp);
    const aero::ObjectRef backingArr = aero::ObservableListHelper::ResolveBackingArray(
        domains.uiHeap(), std::get<aero::ObjectRef>(listObjVal));

    const aero::ObjectRef benchItemA = makeItem("Bench_A", 101, true);
    const aero::ObjectRef benchItemB = makeItem("Bench_B", 202, false);
    const auto pinBenchA = domains.uiHeap().retain(benchItemA);
    const auto pinBenchB = domains.uiHeap().retain(benchItemB);

    constexpr int kIterations = 200;
    PerfMetrics metrics{};

    // (a) Out-of-viewport Replace on 10,000 items
    {
        const std::uint64_t c0 = aero::UIElement::TotalConstructedCount();
        const std::uint64_t d0 = aero::UIElement::TotalDestroyedCount();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            (void)aero::ObservableListHelper::Replace(
                domains.uiHeap(), backingArr, 8000u, (i & 1) ? benchItemA : benchItemB);
            (void)incBindingEngine.FlushTrackA();
        }
        const auto t1 = std::chrono::steady_clock::now();
        metrics.outOfViewReplaceUs =
            std::chrono::duration<float, std::micro>(t1 - t0).count() / static_cast<float>(kIterations);
        AERO_CHECK(
            aero::UIElement::TotalConstructedCount() == c0 &&
            aero::UIElement::TotalDestroyedCount() == d0,
            "200x Out-of-viewport Replace incurred 0 UIElement allocations");
    }

    // (b) In-viewport Replace on 10,000 items
    {
        const std::uint64_t c0 = aero::UIElement::TotalConstructedCount();
        const std::uint64_t d0 = aero::UIElement::TotalDestroyedCount();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            (void)aero::ObservableListHelper::Replace(
                domains.uiHeap(), backingArr, 3u, (i & 1) ? benchItemA : benchItemB);
            (void)incBindingEngine.FlushTrackA();
        }
        const auto t1 = std::chrono::steady_clock::now();
        metrics.inViewReplaceUs =
            std::chrono::duration<float, std::micro>(t1 - t0).count() / static_cast<float>(kIterations);
        AERO_CHECK(
            aero::UIElement::TotalConstructedCount() == c0 &&
            aero::UIElement::TotalDestroyedCount() == d0,
            "200x In-viewport Replace incurred 0 UIElement allocations");
    }

    // (c) Out-of-viewport Tail Add on 10,000 items (including Measure + Arrange extent update)
    {
        const std::uint64_t c0 = aero::UIElement::TotalConstructedCount();
        const std::uint64_t d0 = aero::UIElement::TotalDestroyedCount();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            (void)aero::ObservableListHelper::Add(domains.uiHeap(), backingArr, benchItemA);
            (void)incBindingEngine.FlushTrackA();
            incSv->Measure(aero::Size{360.0f, 240.0f});
            incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
        }
        const auto t1 = std::chrono::steady_clock::now();
        metrics.outOfViewAddUs =
            std::chrono::duration<float, std::micro>(t1 - t0).count() / static_cast<float>(kIterations);
        AERO_CHECK(
            aero::UIElement::TotalConstructedCount() == c0 &&
            aero::UIElement::TotalDestroyedCount() == d0,
            "200x Out-of-viewport Tail Add + Layout incurred 0 UIElement allocations");
    }

    // (d) Baseline Full-Copy Rebuild on 10,000+ items (extract 10k vector + SetItemsSource + Measure + Arrange)
    {
        constexpr int kFullIters = 40;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kFullIters; ++i) {
            (void)aero::ObservableListHelper::Replace(
                domains.uiHeap(), backingArr, 3u, (i & 1) ? benchItemA : benchItemB);
            syncFullOracle();
        }
        const auto t1 = std::chrono::steady_clock::now();
        metrics.fullRebuildUs =
            std::chrono::duration<float, std::micro>(t1 - t0).count() / static_cast<float>(kFullIters);
        // Sync incremental view with the final state of benchItemB at index 3
        (void)incBindingEngine.FlushTrackA();
        incSv->Measure(aero::Size{360.0f, 240.0f});
        incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
        (void)incStyleEngine.EvaluateAndApply();
        refSv->ScrollToVerticalOffset(0.0f);
        refSv->Measure(aero::Size{360.0f, 240.0f});
        refSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
        (void)refStyleEngine.EvaluateAndApply();
        AssertListBoxParityWithFullOracle(
            incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After 200x Benchmark Additions");
    }

    std::printf(
        "  [Perf 10k Items] Out-of-view Replace: %.2f us | In-view Replace: %.2f us | "
        "Out-of-view Add+Layout: %.2f us | Full Copy+Rebuild: %.2f us (Speedup: %.1fx - %.1fx)\n",
        metrics.outOfViewReplaceUs,
        metrics.inViewReplaceUs,
        metrics.outOfViewAddUs,
        metrics.fullRebuildUs,
        metrics.fullRebuildUs / std::max(0.01f, metrics.outOfViewAddUs),
        metrics.fullRebuildUs / std::max(0.01f, metrics.outOfViewReplaceUs));

    AERO_CHECK(metrics.outOfViewReplaceUs < 50.0f, "Out-of-view Replace is microsecond-level (< 50 us)");
    AERO_CHECK(metrics.inViewReplaceUs < 100.0f, "In-view Replace is microsecond-level (< 100 us)");
    AERO_CHECK(metrics.outOfViewAddUs < 50.0f, "Out-of-view Add+Layout is microsecond-level (< 50 us)");
    AERO_CHECK(metrics.outOfViewReplaceUs * 5.0f < metrics.fullRebuildUs &&
               metrics.outOfViewAddUs * 5.0f < metrics.fullRebuildUs,
               "Incremental sync is significantly faster than 10k full copy & rebuild");

    // ------------------------------------------------------------------------
    // Operation 5: Clear on 10,201 items -> TotalItemCount == 0, ExtentHeight == 0, SelectedIndex == -1
    // ------------------------------------------------------------------------
    incList->SetSelectedIndex(2);
    refListPtr->SetSelectedIndex(2);
    AERO_CHECK(uiInterp.invoke(clearSym, {vmRef}).succeeded, "RealScript vm.ClearItems() executed");
    (void)incBindingEngine.FlushTrackA();
    incSv->Measure(aero::Size{360.0f, 240.0f});
    incSv->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 240.0f});
    syncFullOracle();

    AERO_CHECK(incList->TotalItemCount() == 0u && incList->RealizedItemCount() == 0u &&
               incList->GetSelectedIndex() == -1 && ApproxEq(incSv->ExtentHeight(), 0.0f),
               "Clear emptied all 10k+ items, realized containers, ExtentHeight, and SelectedIndex");
    AssertListBoxParityWithFullOracle(incSv.Get(), incList, refSv.Get(), refListPtr.Get(), "After Clear()");

    return metrics;
}

// ============================================================================
// Suite 4: Native Array In-Place Methods (push/insert/removeAt/clear),
//          ComboBox & TabControl Incremental Sync, Ring Overflow & Cross-Collection Swap
// ============================================================================
void TestNativeArrayMethodsComboTabAndFallbackPaths() {
    std::printf("--- [Suite 4] Native Array push/insert/removeAt/clear, ComboBox, TabControl & Fallbacks ---\n");
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* rsCode = R"RS(
module Game.MultiControlsP22;

[Observable]
public class LobbyVm
{
    public string[] Channels { get; set; } = null;
    public string[] Tabs { get; set; } = null;

    public void PushChannel(string ch) { Channels.push(ch); }
    public void InsertChannel(int idx, string ch) { Channels.insert(idx, ch); }
    public void RemoveChannel(int idx) { Channels.removeAt(idx); }
    public void ReplaceChannel(int idx, string ch) { Channels[idx] = ch; }
    public void ClearChannels() { Channels.clear(); }

    public void PushTab(string t) { Tabs.push(t); }
    public void InsertTab(int idx, string t) { Tabs.insert(idx, t); }
    public void RemoveTab(int idx) { Tabs.removeAt(idx); }
}

public LobbyVm CreateLobbyVm()
{
    LobbyVm vm = new LobbyVm();
    vm.Channels = new string[2];
    vm.Channels[0] = "General";
    vm.Channels[1] = "Trade";

    vm.Tabs = new string[2];
    vm.Tabs[0] = "Inventory";
    vm.Tabs[1] = "Skills";
    return vm;
}
)RS";

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            x:DataType="Game.MultiControlsP22::LobbyVm"
            Width="320" Height="240">
    <ComboBox x:Name="ChannelCombo" SelectedIndex="1" ItemsSource="{Binding Channels}"/>
    <TabControl x:Name="MainTabs" SelectedIndex="1" ItemsSource="{Binding Tabs}"/>
    <ItemsControl x:Name="PlainList" ItemsSource="{Binding Channels}"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "Lobby.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back(aero::SourceFile{"LobbyVm.rs", rsCode});

    const aero::AeroCompileResult res = compiler.Compile(in);
    AERO_CHECK(res.succeeded && res.programImage != nullptr, "Compiled Lobby XAML + native array methods");
    if (!res.succeeded || !res.programImage) {
        return;
    }

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    aero::Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);

    const auto vmExec = uiInterp.invoke("Game.MultiControlsP22::CreateLobbyVm");
    AERO_CHECK(vmExec.succeeded, "Created LobbyVm");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const auto pinVm = domains.uiHeap().retain(vmRef);

    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(vmRef);

    aero::DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    AERO_CHECK(
        aero::MountAxbTemplate(root.Get(), res.axbPackage, &domains, &bindingEngine, vmRef, &uiInterp),
        "Mounted Lobby StackPanel");

    auto* combo = root->FindName("ChannelCombo")->DynamicCast<aero::ComboBox>();
    auto* tabs = root->FindName("MainTabs")->DynamicCast<aero::TabControl>();
    auto* plainList = root->FindName("PlainList")->DynamicCast<aero::ItemsControl>();
    AERO_CHECK(combo && tabs && plainList, "Resolved ComboBox, TabControl, ItemsControl");
    AERO_CHECK(combo->ComboItemCount() == 2u && combo->GetSelectedItemText() == "Trade",
               "Initial ComboBox SelectedIndex=1 ('Trade')");
    AERO_CHECK(tabs->TabCount() == 2u && tabs->GetSelectedIndex() == 1 && tabs->GetTabHeaderText(1) == "Skills",
               "Initial TabControl SelectedIndex=1 ('Skills')");
    AERO_CHECK(plainList->RealizedItemCount() == 2u, "Initial non-virtualized ItemsControl count == 2");

    // 1. Native array insert(0, "System") -> shifts ComboBox SelectedIndex 1 -> 2 ('Trade')!
    const auto sSystem = *domains.uiHeap().allocateString("System");
    const auto sGuild = *domains.uiHeap().allocateString("Guild");
    const auto sRaid = *domains.uiHeap().allocateString("Raid");
    AERO_CHECK(uiInterp.invoke("Game.MultiControlsP22::LobbyVm.InsertChannel", {vmRef, aero::value::FromInt(0), sSystem}).succeeded,
               "Channels.insert(0, 'System')");
    AERO_CHECK(uiInterp.invoke("Game.MultiControlsP22::LobbyVm.PushChannel", {vmRef, sGuild}).succeeded,
               "Channels.push('Guild')");
    (void)bindingEngine.FlushTrackA();

    AERO_CHECK(combo->ComboItemCount() == 4u && combo->GetSelectedIndex() == 2 &&
               combo->GetSelectedItemText() == "Trade",
               "ComboBox incrementally updated to 4 items and shifted SelectedIndex 1 -> 2");
    AERO_CHECK(plainList->RealizedItemCount() == 4u &&
               plainList->FirstChild()->DynamicCast<aero::TextBlock>()->GetText() == "System",
               "Non-virtualized ItemsControl incrementally inserted 'System' at head");

    // 2. Native array index assignment Channels[2] = "Raid" & removeAt(0)
    AERO_CHECK(uiInterp.invoke("Game.MultiControlsP22::LobbyVm.ReplaceChannel", {vmRef, aero::value::FromInt(2), sRaid}).succeeded,
               "Channels[2] = 'Raid'");
    AERO_CHECK(uiInterp.invoke("Game.MultiControlsP22::LobbyVm.RemoveChannel", {vmRef, aero::value::FromInt(0)}).succeeded,
               "Channels.removeAt(0)");
    (void)bindingEngine.FlushTrackA();

    AERO_CHECK(combo->ComboItemCount() == 3u && combo->GetSelectedIndex() == 1 &&
               combo->GetSelectedItemText() == "Raid",
               "ComboBox Replaced & shifted SelectedIndex 2 -> 1 ('Raid')");

    // 3. TabControl incremental insert(0, "Overview") -> SelectedIndex 1 -> 2 ("Skills")
    const auto sOverview = *domains.uiHeap().allocateString("Overview");
    AERO_CHECK(uiInterp.invoke("Game.MultiControlsP22::LobbyVm.InsertTab", {vmRef, aero::value::FromInt(0), sOverview}).succeeded,
               "Tabs.insert(0, 'Overview')");
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(tabs->TabCount() == 3u && tabs->GetSelectedIndex() == 2 &&
               tabs->GetTabHeaderText(0) == "Overview" && tabs->GetTabHeaderText(2) == "Skills",
               "TabControl incrementally inserted tab at 0 and shifted SelectedIndex 1 -> 2");

    // 4. Burst > 256 pushes onto Channels -> overflows delta ring -> DualTrackBindingEngine falls back cleanly!
    for (int i = 0; i < 300; ++i) {
        const auto sExtra = *domains.uiHeap().allocateString("Ch_" + std::to_string(i));
        (void)uiInterp.invoke("Game.MultiControlsP22::LobbyVm.PushChannel", {vmRef, sExtra});
    }
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(combo->ComboItemCount() == 303u && plainList->RealizedItemCount() == 303u,
               "Ring overflow (>256 ops) smoothly fell back to full sync (303 items)");

    // 5. Cross-collection replacement (vm.Channels = brandNewArray)
    const aero::SymbolId lobbyTid = aero::rs_sem::stableTypeId("Game.MultiControlsP22::LobbyVm");
    const auto chSlot = *domains.resolvePropertySlot(*res.programImage, lobbyTid, "Channels");
    const auto sFresh = *domains.uiHeap().allocateString("FreshOnly");
    const auto freshArr = *domains.uiHeap().allocateTypedArray(0, aero::PrimitiveType::String, 0, 1u, sFresh);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmRef, chSlot, freshArr, &uiInterp);
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(combo->ComboItemCount() == 1u && combo->GetComboItemText(0) == "FreshOnly" &&
               plainList->RealizedItemCount() == 1u,
               "Cross-collection property assignment cleanly switched array handle & rebuilt");
}

} // namespace

int main() {
    std::printf("=== Running AeroGUI 2.0 + RealScript P2-2 Incremental ObservableList Suite ===\n");
    TestRealScriptDeltaRingAndRingOverflowFallback();
    (void)Test10kVirtualizedListBoxIncrementalSyncAndBenchmark();
    TestNativeArrayMethodsComboTabAndFallbackPaths();

    std::printf("=== P2-2 Results: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
