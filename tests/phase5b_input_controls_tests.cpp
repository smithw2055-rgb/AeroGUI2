#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

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

// ============================================================================
// 1. Memory Layout Invariants & Zero-Allocation InputEventRouter Steady State
// ============================================================================
void TestInlineInteractiveStatesAndZeroAllocRouter() {
    static_assert(sizeof(aero::UnifiedValue) == 16, "UnifiedValue == 16B");
    static_assert(sizeof(aero::Float4Value) == 16, "Float4Value == 16B");
    static_assert(sizeof(aero::CompactPropertyEntry) == 32, "CompactPropertyEntry == 32B");
    static_assert(sizeof(aero::FastInlineStorage) == 56, "FastInlineStorage == 56B");
    static_assert(sizeof(aero::AeroVertex) == 24, "AeroVertex == 24B");
    static_assert(sizeof(aero::CommandPageHeader) == 64, "CommandPageHeader == 64B");
    static_assert(sizeof(aero::CommandPage) == 4096, "CommandPage == 4096B");

    auto root = aero::MakeIntrusive<aero::Canvas>();
    root->SetWidth(400.0f);
    root->SetHeight(300.0f);

    auto btnA = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(btnA.Get(), 20.0f);
    aero::Canvas::SetTop(btnA.Get(), 20.0f);
    btnA->SetWidth(120.0f);
    btnA->SetHeight(36.0f);
    btnA->SetContentText("Alpha");
    root->AddChild(btnA.Get());

    auto btnB = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(btnB.Get(), 180.0f);
    aero::Canvas::SetTop(btnB.Get(), 20.0f);
    btnB->SetWidth(120.0f);
    btnB->SetHeight(36.0f);
    btnB->SetContentText("Beta");
    root->AddChild(btnB.Get());

    root->Measure(aero::Size{400.0f, 300.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 300.0f});

    // Setting IsEnabled and Focusable via SetValue uses FastInlineStorage::reserved (0 sparse entries!)
    auto probe = aero::MakeIntrusive<aero::UIElement>();
    AERO_CHECK(probe->IsEnabled() && !probe->IsFocusable(), "UIElement defaults to Enabled & non-Focusable");
    probe->SetValue(aero::PropId_IsEnabled, aero::UnifiedValue{false});
    probe->SetValue(aero::PropId_Focusable, aero::UnifiedValue{true});
    AERO_CHECK(!probe->IsEnabled() && probe->IsFocusable(), "Inline IsEnabled & Focusable mutated");
    AERO_CHECK(probe->SparsePropertyCount() == 0u, "IsEnabled & Focusable allocated 0 sparse property entries");

    int clicks = 0;
    btnA->SetClickCallback([&](aero::Button&) { ++clicks; });

    aero::InputEventRouter router(root.Get());
    // Warm up one pass, then enforce 0 heap allocations across 500 queued + flushed input cycles!
    (void)router.DispatchPointerMove(aero::Point{50.0f, 35.0f});
    AERO_CHECK(btnA->IsHovered() && !btnB->IsHovered(), "Hover entered btnA");

    gHeapAllocCount.store(0u, std::memory_order_seq_cst);
    gCountHeapAllocs.store(true, std::memory_order_seq_cst);

    for (int i = 0; i < 200; ++i) {
        (void)router.QueuePointerMove(aero::Point{60.0f, 35.0f});
        (void)router.QueuePointerMove(aero::Point{220.0f, 35.0f});
        (void)router.QueuePointerButton(aero::Point{60.0f, 35.0f}, aero::PointerButton::Left, true);
        (void)router.QueuePointerButton(aero::Point{60.0f, 35.0f}, aero::PointerButton::Left, false);
        (void)router.QueueKey(aero::KeyCode::Tab, true);
        (void)router.FlushQueuedEvents();
    }

    gCountHeapAllocs.store(false, std::memory_order_seq_cst);
    const std::size_t steadyAllocs = gHeapAllocCount.load(std::memory_order_seq_cst);
    AERO_CHECK(steadyAllocs == 0u, "Zero heap allocations across 200 steady-state InputEventRouter frames");
    AERO_CHECK(clicks == 200, "All 200 queued pointer clicks dispatched accurately");

    std::cout << "[PASS] TestInlineInteractiveStatesAndZeroAllocRouter\n";
}

// ============================================================================
// 2. Pointer Capture, Child-Icon Bubbling & Slider Out-of-Bounds Dragging
// ============================================================================
void TestPointerCaptureAndSliderDrag() {
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    root->SetWidth(300.0f);
    root->SetHeight(200.0f);
    root->SetSpacing(12.0f);

    // Button containing a child Path icon (verifies hit-testing child Path bubbles to parent Button!)
    auto iconBtn = aero::MakeIntrusive<aero::Button>();
    iconBtn->SetWidth(120.0f);
    iconBtn->SetHeight(40.0f);
    auto childPath = aero::MakeIntrusive<aero::Path>();
    childPath->SetData("M 0,0 L 24,12 L 0,24 Z");
    childPath->SetWidth(24.0f);
    childPath->SetHeight(24.0f);
    iconBtn->AddChild(childPath.Get());
    root->AddChild(iconBtn.Get());

    int btnActivated = 0;
    iconBtn->SetClickCallback([&](aero::Button&) { ++btnActivated; });

    auto slider = aero::MakeIntrusive<aero::Slider>();
    slider->SetWidth(200.0f);
    slider->SetHeight(24.0f);
    slider->SetMinimum(0.0f);
    slider->SetMaximum(100.0f);
    slider->SetSliderValue(20.0f);
    root->AddChild(slider.Get());

    root->Measure(aero::Size{300.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 200.0f});

    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(root.Get());
    const std::size_t initialNodeCount = renderTree.NodeCount();
    const std::uint32_t initialTreeId = renderTree.TreeId();

    aero::InputEventRouter router(root.Get());

    // Press on childPath inside iconBtn, drag OUTSIDE iconBtn, release outside -> must NOT fire click!
    const aero::Rect iconRect = childPath->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{iconRect.x + 5.0f, iconRect.y + 5.0f});
    AERO_CHECK(router.CapturedElement() == iconBtn.Get() && iconBtn->IsPressed(),
               "Clicking child Path inside Button captured & pressed parent Button");
    (void)router.DispatchPointerMove(aero::Point{290.0f, 190.0f});
    AERO_CHECK(!iconBtn->IsPressed(), "Moving captured pointer outside Button clears visual Pressed state");
    (void)router.DispatchPointerUp(aero::Point{290.0f, 190.0f});
    AERO_CHECK(btnActivated == 0 && router.CapturedElement() == nullptr,
               "Releasing pointer outside Button bounds cancelled click");

    // Press and release inside childPath -> fires Button click!
    (void)router.DispatchPointerDown(aero::Point{iconRect.x + 5.0f, iconRect.y + 5.0f});
    (void)router.DispatchPointerUp(aero::Point{iconRect.x + 5.0f, iconRect.y + 5.0f});
    AERO_CHECK(btnActivated == 1, "Press+release on child Path activated owning Button");

    // Drag Slider from 25% -> 75% -> +800px outside right -> -300px outside left while captured!
    const aero::Rect sr = slider->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{sr.x + sr.width * 0.25f, sr.y + sr.height * 0.5f});
    AERO_CHECK(router.CapturedElement() == slider.Get(), "Slider captured pointer on PointerDown");
    AERO_CHECK(ApproxEq(slider->GetSliderValue(), 25.0f, 0.5f), "Slider jumped to 25% on PointerDown");

    (void)router.DispatchPointerMove(aero::Point{sr.x + sr.width * 0.75f, sr.y + 180.0f});
    AERO_CHECK(ApproxEq(slider->GetSliderValue(), 75.0f, 0.5f),
               "Slider tracked horizontal drag even when pointer Y left Slider bounds");

    (void)router.DispatchPointerMove(aero::Point{sr.x + 900.0f, sr.y - 120.0f});
    AERO_CHECK(ApproxEq(slider->GetSliderValue(), 100.0f, 0.01f), "Slider clamped to Maximum=100 outside right");

    (void)router.DispatchPointerMove(aero::Point{sr.x - 400.0f, sr.y + 50.0f});
    AERO_CHECK(ApproxEq(slider->GetSliderValue(), 0.0f, 0.01f), "Slider clamped to Minimum=0 outside left");

    (void)router.DispatchPointerMove(aero::Point{sr.x + sr.width * 0.60f, sr.y + 10.0f});
    (void)router.DispatchPointerUp(aero::Point{sr.x + sr.width * 0.60f, sr.y + 10.0f});
    AERO_CHECK(router.CapturedElement() == nullptr && ApproxEq(slider->GetSliderValue(), 60.0f, 0.5f),
               "Slider released capture at 60%");

    // Verify O(1) in-place PagedRenderTree update (no full tree rebuild!)
    renderTree.ClearDirtyPages();
    (void)renderTree.SyncVisualTree(root.Get());
    AERO_CHECK(renderTree.TreeId() == initialTreeId && renderTree.NodeCount() == initialNodeCount,
               "Slider drag updated PagedRenderTree in-place in O(1) without rebuilding render tree");

    // Keyboard arrow step on focused Slider
    slider->SetSmallChange(2.5f);
    (void)router.DispatchKeyDown(aero::KeyCode::Right);
    AERO_CHECK(ApproxEq(slider->GetSliderValue(), 62.5f, 0.05f), "Slider Right arrow incremented by SmallChange");

    std::cout << "[PASS] TestPointerCaptureAndSliderDrag\n";
}

// ============================================================================
// 3. CheckBox Toggle & RadioButton Mutual Exclusion (Sibling + GroupName)
// ============================================================================
void TestCheckBoxAndRadioButtonGroups() {
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    root->SetWidth(320.0f);
    root->SetHeight(320.0f);

    auto cb = aero::MakeIntrusive<aero::CheckBox>();
    cb->SetContentText("Enable VSync");
    cb->SetHeight(26.0f);
    root->AddChild(cb.Get());

    // Unnamed sibling group inside panelA
    auto panelA = aero::MakeIntrusive<aero::StackPanel>();
    auto rb1 = aero::MakeIntrusive<aero::RadioButton>();
    rb1->SetContentText("Low");
    rb1->SetIsChecked(true);
    auto rb2 = aero::MakeIntrusive<aero::RadioButton>();
    rb2->SetContentText("High");
    panelA->AddChild(rb1.Get());
    panelA->AddChild(rb2.Get());
    root->AddChild(panelA.Get());

    // Named GroupName="QualityTier" split across two separate parent panels!
    auto boxLeft = aero::MakeIntrusive<aero::Border>();
    auto namedRbA = aero::MakeIntrusive<aero::RadioButton>();
    namedRbA->SetGroupName("QualityTier");
    namedRbA->SetContentText("Balanced");
    namedRbA->SetIsChecked(true);
    boxLeft->AddChild(namedRbA.Get());

    auto boxRight = aero::MakeIntrusive<aero::Border>();
    auto namedRbB = aero::MakeIntrusive<aero::RadioButton>();
    namedRbB->SetGroupName("QualityTier");
    namedRbB->SetContentText("Ultra");
    boxRight->AddChild(namedRbB.Get());

    root->AddChild(boxLeft.Get());
    root->AddChild(boxRight.Get());

    root->Measure(aero::Size{320.0f, 320.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 320.0f});

    aero::InputEventRouter router(root.Get());

    // Click CheckBox twice
    const aero::Rect cbR = cb->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{cbR.x + 8.0f, cbR.y + 8.0f});
    (void)router.DispatchPointerUp(aero::Point{cbR.x + 8.0f, cbR.y + 8.0f});
    AERO_CHECK(cb->IsChecked(), "CheckBox toggled ON");
    (void)router.DispatchKeyDown(aero::KeyCode::Space);
    AERO_CHECK(!cb->IsChecked(), "CheckBox toggled OFF via Space key");

    // Click rb2 -> unchecks sibling rb1 while leaving namedRbA untouched!
    const aero::Rect rb2R = rb2->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{rb2R.x + 8.0f, rb2R.y + 8.0f});
    (void)router.DispatchPointerUp(aero::Point{rb2R.x + 8.0f, rb2R.y + 8.0f});
    AERO_CHECK(rb2->IsChecked() && !rb1->IsChecked(), "Sibling RadioButton mutual exclusion unchecked rb1");
    AERO_CHECK(namedRbA->IsChecked(), "Unrelated GroupName RadioButton stayed checked");

    // Click namedRbB inside separate Border -> unchecks namedRbA across subtree!
    const aero::Rect nrbBR = namedRbB->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{nrbBR.x + 8.0f, nrbBR.y + 8.0f});
    (void)router.DispatchPointerUp(aero::Point{nrbBR.x + 8.0f, nrbBR.y + 8.0f});
    AERO_CHECK(namedRbB->IsChecked() && !namedRbA->IsChecked(),
               "Cross-panel GroupName RadioButton mutual exclusion unchecked namedRbA");
    AERO_CHECK(rb2->IsChecked(), "Unnamed sibling RadioButton group remained unaffected");

    std::cout << "[PASS] TestCheckBoxAndRadioButtonGroups\n";
}

// ============================================================================
// 4. TextBox & PasswordBox UTF-8 Editing, MaxLength, IsReadOnly & WebImeAdapter
// ============================================================================
void TestTextBoxUtf8EditingAndImeSync() {
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    root->SetWidth(320.0f);
    root->SetHeight(200.0f);
    root->SetSpacing(8.0f);

    auto tbox = aero::MakeIntrusive<aero::TextBox>();
    tbox->SetWidth(240.0f);
    tbox->SetHeight(32.0f);
    tbox->SetPlaceholder("输入代号...");
    tbox->SetMaxLength(7); // 7 UTF-8 codepoints max
    root->AddChild(tbox.Get());

    auto pwd = aero::MakeIntrusive<aero::PasswordBox>();
    pwd->SetWidth(240.0f);
    pwd->SetHeight(32.0f);
    root->AddChild(pwd.Get());

    root->Measure(aero::Size{320.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 200.0f});

    aero::WebImeAdapter ime;
    aero::InputEventRouter router(root.Get(), &ime);

    const aero::Rect tbR = tbox->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{tbR.x + 10.0f, tbR.y + 10.0f});
    (void)router.DispatchPointerUp(aero::Point{tbR.x + 10.0f, tbR.y + 10.0f});
    AERO_CHECK(tbox->IsFocused() && ime.IsImeActive(),
               "Focusing TextBox activated WebImeAdapter overlay");

    // Type ASCII + 3-byte UTF-8 CJK characters ("AB中文CD") -> 6 codepoints
    (void)router.DispatchTextInput("AB中文CD");
    AERO_CHECK(tbox->GetText() == "AB中文CD" && tbox->GetCaretIndex() == 6u,
               "UTF-8 mixed ASCII+CJK insertion & CaretIndex == 6 codepoints");

    // MaxLength=7 truncates "XYZ" to just 'X'
    (void)router.DispatchTextInput("XYZ");
    AERO_CHECK(tbox->GetText() == "AB中文CDX" && tbox->GetCaretIndex() == 7u,
               "MaxLength=7 enforced in UTF-8 codepoints");

    // Move caret left 4 codepoints (before '文') and Backspace '中' cleanly!
    (void)router.DispatchKeyDown(aero::KeyCode::Left); // 6
    (void)router.DispatchKeyDown(aero::KeyCode::Left); // 5
    (void)router.DispatchKeyDown(aero::KeyCode::Left); // 4 ('文'..'C' -> wait: A(0) B(1) 中(2) 文(3) C(4) D(5) X(6))
    (void)router.DispatchKeyDown(aero::KeyCode::Left); // 3 (between '中' and '文')
    (void)router.DispatchKeyDown(aero::KeyCode::Backspace); // deletes '中'!
    AERO_CHECK(tbox->GetText() == "AB文CDX" && tbox->GetCaretIndex() == 2u,
               "Backspace deleted entire 3-byte UTF-8 CJK codepoint '中' without corruption");

    // DeleteForward deletes '文'!
    (void)router.DispatchKeyDown(aero::KeyCode::Delete);
    AERO_CHECK(tbox->GetText() == "ABCDX" && tbox->GetCaretIndex() == 2u,
               "DeleteForward deleted 3-byte UTF-8 CJK codepoint '文'");

    // Insert '!' at caret index 2 -> "AB!CDX"
    (void)router.DispatchTextInput("!");
    AERO_CHECK(tbox->GetText() == "AB!CDX" && tbox->GetCaretIndex() == 3u,
               "Mid-string insertion at CaretIndex=2 succeeded");

    // IsReadOnly blocks edits
    tbox->SetIsReadOnly(true);
    AERO_CHECK(!router.DispatchTextInput("Q") && !router.DispatchKeyDown(aero::KeyCode::Backspace),
               "IsReadOnly TextBox rejected text insertion and Backspace");
    AERO_CHECK(tbox->GetText() == "AB!CDX", "ReadOnly text remained unchanged");

    // Tab to PasswordBox and verify display masking
    (void)router.DispatchKeyDown(aero::KeyCode::Tab);
    AERO_CHECK(pwd->IsFocused(), "Tab advanced focus to PasswordBox");
    (void)router.DispatchTextInput("密123");
    AERO_CHECK(pwd->GetText() == "密123" && pwd->GetDisplayString() == "****",
               "PasswordBox preserved raw UTF-8 secret while masking display to 4 asterisks");

    std::cout << "[PASS] TestTextBoxUtf8EditingAndImeSync\n";
}

// ============================================================================
// 5. ComboBox Popup Z-Order Over Sibling Controls, Selection & Dismissal
// ============================================================================
void TestComboBoxPopupOverlayAndSelection() {
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    root->SetWidth(300.0f);
    root->SetHeight(260.0f);
    root->SetSpacing(6.0f);

    auto combo = aero::MakeIntrusive<aero::ComboBox>();
    combo->SetWidth(180.0f);
    combo->SetHeight(30.0f);
    combo->SetItemsSource({
        aero::value::FromString("1280x720"),
        aero::value::FromString("1920x1080"),
        aero::value::FromString("2560x1440")});
    combo->SetSelectedIndex(0);
    root->AddChild(combo.Get());

    // Sibling Button directly underneath ComboBox (overlapped when dropdown opens!)
    auto occludedBtn = aero::MakeIntrusive<aero::Button>();
    occludedBtn->SetWidth(180.0f);
    occludedBtn->SetHeight(36.0f);
    occludedBtn->SetContentText("Apply Graphics");
    int occludedClicks = 0;
    occludedBtn->SetClickCallback([&](aero::Button&) { ++occludedClicks; });
    root->AddChild(occludedBtn.Get());

    root->Measure(aero::Size{300.0f, 260.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 260.0f});

    const aero::Rect occludedBefore = occludedBtn->FinalRect();
    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(root.Get());
    const std::size_t closedNodes = renderTree.NodeCount();

    aero::InputEventRouter router(root.Get());
    const aero::Rect cRect = combo->FinalRect();

    // Open ComboBox dropdown
    (void)router.DispatchPointerDown(aero::Point{cRect.x + 20.0f, cRect.y + 15.0f});
    (void)router.DispatchPointerUp(aero::Point{cRect.x + 20.0f, cRect.y + 15.0f});
    AERO_CHECK(combo->IsDropDownOpen() && router.ActiveOpenComboBox() == combo.Get(),
               "Clicking ComboBox header opened dropdown");

    // Verify opening ComboBox does NOT push down sibling occludedBtn in layout, and emits top-layer popup nodes!
    root->Measure(aero::Size{300.0f, 260.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 260.0f});
    AERO_CHECK(occludedBtn->FinalRect() == occludedBefore,
               "Opening ComboBox popup did not shift sibling StackPanel layout");
    (void)renderTree.SyncVisualTree(root.Get());
    AERO_CHECK(renderTree.NodeCount() > closedNodes,
               "PagedRenderTree emitted deferred Z-topmost ComboBox popup nodes");

    // Click item #2 ("2560x1440") whose popup Y overlaps occludedBtn!
    const aero::Rect popupRect = combo->DropDownPopupScreenRect();
    const aero::Point item2Pt{
        popupRect.x + 30.0f,
        popupRect.y + aero::ComboBox::kPopupRowHeight * 2.5f};
    (void)router.DispatchPointerDown(item2Pt);
    (void)router.DispatchPointerUp(item2Pt);

    AERO_CHECK(!combo->IsDropDownOpen(), "Selecting dropdown item closed ComboBox popup");
    AERO_CHECK(combo->GetSelectedIndex() == 2 && combo->GetSelectedItemText() == "2560x1440",
               "ComboBox SelectedIndex=2 and SelectedItemText='2560x1440' updated");
    AERO_CHECK(occludedClicks == 0,
               "Top-layer ComboBox popup intercepted click without triggering occluded Button underneath");

    // Re-open and dismiss via outside click
    (void)router.DispatchPointerDown(aero::Point{cRect.x + 20.0f, cRect.y + 15.0f});
    (void)router.DispatchPointerUp(aero::Point{cRect.x + 20.0f, cRect.y + 15.0f});
    AERO_CHECK(combo->IsDropDownOpen(), "ComboBox re-opened");
    (void)router.DispatchPointerDown(aero::Point{280.0f, 240.0f});
    (void)router.DispatchPointerUp(aero::Point{280.0f, 240.0f});
    AERO_CHECK(!combo->IsDropDownOpen(), "Clicking outside closed ComboBox popup");

    std::cout << "[PASS] TestComboBoxPopupOverlayAndSelection\n";
}

// ============================================================================
// 6. XAML + RealScript End-to-End TwoWay Auto-Push & [SimCommand] Marshaling
// ============================================================================
void TestTwoWayAutoWriteBackAndSimCommand() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* kRsCode = R"RS(
module Game.Settings;

[Observable]
public class AudioConfigVm
{
    public bool MuteAll { get; set; } = false;
    public float MasterVolume { get; set; } = 40.0;
    public string PlayerTag { get; set; } = "Pilot";
    public int PresetIndex { get; set; } = 0;
}

[SimState]
public class SimTacticalRules
{
    public float TargetThrottle { get; set; } = 10.0;

    [SimCommand]
    public void SetThrottle(float v)
    {
        this.TargetThrottle = v;
    }
}

public AudioConfigVm CreateAudioVm()
{
    return new AudioConfigVm();
}

public SimTacticalRules CreateSimRules()
{
    return new SimTacticalRules();
}
)RS";

    const char* kXaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            Width="360" Height="300" Spacing="8">
    <CheckBox x:Name="MuteCheck" Content="Mute Audio" IsChecked="{Binding MuteAll, Mode=TwoWay}" Height="26"/>
    <Slider x:Name="VolSlider" Minimum="0" Maximum="100" Value="{Binding MasterVolume, Mode=TwoWay}" Width="200" Height="24"/>
    <TextBox x:Name="TagBox" Text="{Binding PlayerTag, Mode=TwoWay}" Width="200" Height="30"/>
    <ComboBox x:Name="PresetCombo" SelectedIndex="{Binding PresetIndex, Mode=TwoWay}" ItemsSource="Stereo|Surround|Binaural" Width="200" Height="28"/>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "AudioPanel.xaml";
    input.xamlContent = kXaml;
    input.emitAotCpp = true;
    input.scriptSources.push_back(aero::SourceFile{"Settings.rs", kRsCode});

    const aero::AeroCompileResult compiled = compiler.Compile(input);
    AERO_CHECK(compiled.succeeded && compiled.programImage != nullptr,
               "Compiled Phase 5B interactive TwoWay XAML + RealScript");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    aero::Interpreter uiInterp(compiled.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
    aero::Interpreter simInterp(compiled.programImage, domains.simHeapPtr());
    simInterp.setExecutionDomainContext(&domains, aero::DomainKind::SimDeterministic);

    const auto vmExec = uiInterp.invoke("Game.Settings::CreateAudioVm");
    AERO_CHECK(vmExec.succeeded, "Created AudioConfigVm on UiHeap");
    const aero::ObjectRef vmRef = std::get<aero::ObjectRef>(vmExec.value);
    const auto vmPin = domains.uiHeap().retain(vmRef);

    aero::AxbPackage pkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(compiled.axbBytes, pkg), "Deserialized AudioPanel.axb4");
    (void)pkg.BindAgainstProgram(*compiled.programImage, "Game.Settings::AudioConfigVm", &domains);

    aero::DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterp);
    auto root = aero::MakeIntrusive<aero::StackPanel>();
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), pkg, &domains, &bindingEngine, vmRef, &uiInterp),
               "Mounted AudioPanel template onto StackPanel");

    // Also bind a second Slider TwoWay to a SimHeap [SimCommand] property!
    const auto simExec = simInterp.invoke("Game.Settings::CreateSimRules");
    AERO_CHECK(simExec.succeeded, "Created SimTacticalRules on SimHeap");
    const aero::ObjectRef simRef = std::get<aero::ObjectRef>(simExec.value);
    const auto simPin = domains.simHeap().retain(simRef);

    auto simSlider = aero::MakeIntrusive<aero::Slider>();
    simSlider->SetWidth(200.0f);
    simSlider->SetHeight(24.0f);
    simSlider->SetMinimum(0.0f);
    simSlider->SetMaximum(100.0f);
    root->AddChild(simSlider.Get());

    aero::AxbCompiledBindingChain simChain{};
    simChain.rawPath = "TargetThrottle";
    simChain.targetPropertyId = aero::PropId_Value;
    simChain.mode = aero::BindingMode::TwoWay;
    aero::AxbCompiledHop throttleHop{};
    throttleHop.memberName = "TargetThrottle";
    throttleHop.declaringTypeId = aero::rs_sem::stableTypeId("Game.Settings::SimTacticalRules");
    if (auto resolved = domains.resolvePropertySlot(*compiled.programImage, throttleHop.declaringTypeId, "TargetThrottle")) {
        throttleHop.declaringTypeId = resolved->typeId;
        throttleHop.fieldIndex = resolved->backingFieldIndex;
        throttleHop.observableBitIndex = resolved->observableBitIndex;
        throttleHop.getterSymbolId = resolved->getterSymbolId;
        throttleHop.isAutoProperty = resolved->isAutoProperty;
        throttleHop.isSimState = resolved->isSimState;
    }
    if (const auto* simTypeSym = compiled.programImage->resolveType(throttleHop.declaringTypeId)) {
        for (const auto& m : simTypeSym->methods) {
            if (m.name == "SetThrottle") {
                throttleHop.setterSymbolId = m.id;
                throttleHop.isSimCommand = true;
                break;
            }
        }
    }
    simChain.hops.push_back(throttleHop);
    (void)bindingEngine.BindChain(simRef, simChain, simSlider);

    root->Measure(aero::Size{360.0f, 340.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 360.0f, 340.0f});

    auto* muteCheck = root->FindName("MuteCheck")->DynamicCast<aero::CheckBox>();
    auto* volSlider = root->FindName("VolSlider")->DynamicCast<aero::Slider>();
    auto* tagBox = root->FindName("TagBox")->DynamicCast<aero::TextBox>();
    auto* presetCombo = root->FindName("PresetCombo")->DynamicCast<aero::ComboBox>();
    AERO_CHECK(muteCheck && volSlider && tagBox && presetCombo, "Resolved all 4 named Phase 5B controls");
    AERO_CHECK(!muteCheck->IsChecked() && ApproxEq(volSlider->GetSliderValue(), 40.0f) &&
               tagBox->GetText() == "Pilot" && presetCombo->GetSelectedIndex() == 0,
               "Initial OneWay/TwoWay values populated from AudioConfigVm");

    aero::InputEventRouter router(root.Get());

    // 1) User clicks MuteCheck -> TwoWay writes MuteAll = true into AudioConfigVm!
    (void)router.DispatchPointerDown(aero::Point{muteCheck->FinalRect().x + 8.0f, muteCheck->FinalRect().y + 8.0f});
    (void)router.DispatchPointerUp(aero::Point{muteCheck->FinalRect().x + 8.0f, muteCheck->FinalRect().y + 8.0f});

    // 2) User drags VolSlider to 85% -> TwoWay writes MasterVolume = 85.0 into AudioConfigVm!
    const aero::Rect vr = volSlider->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{vr.x + vr.width * 0.85f, vr.y + 10.0f});
    (void)router.DispatchPointerUp(aero::Point{vr.x + vr.width * 0.85f, vr.y + 10.0f});

    // 3) User types "-01" into TagBox -> TwoWay writes PlayerTag = "Pilot-01" into AudioConfigVm!
    const aero::Rect tr = tagBox->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{tr.x + tr.width - 4.0f, tr.y + 10.0f});
    (void)router.DispatchPointerUp(aero::Point{tr.x + tr.width - 4.0f, tr.y + 10.0f});
    (void)router.DispatchTextInput("-01");

    // 4) User selects index 2 ("Binaural") on PresetCombo -> TwoWay writes PresetIndex = 2!
    const aero::Rect pr = presetCombo->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{pr.x + 20.0f, pr.y + 12.0f});
    (void)router.DispatchPointerUp(aero::Point{pr.x + 20.0f, pr.y + 12.0f});
    const aero::Rect popR = presetCombo->DropDownPopupScreenRect();
    (void)router.DispatchPointerDown(aero::Point{popR.x + 20.0f, popR.y + aero::ComboBox::kPopupRowHeight * 2.5f});
    (void)router.DispatchPointerUp(aero::Point{popR.x + 20.0f, popR.y + aero::ComboBox::kPopupRowHeight * 2.5f});

    const auto vmTypeId = aero::rs_sem::stableTypeId("Game.Settings::AudioConfigVm");
    aero::UnifiedValue muteVal{}, volVal{}, tagVal{}, presetVal{};
    (void)domains.readPropertySlotFast(
        aero::DomainKind::Ui, vmRef, *domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "MuteAll"), muteVal, nullptr, &uiInterp);
    (void)domains.readPropertySlotFast(
        aero::DomainKind::Ui, vmRef, *domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "MasterVolume"), volVal, nullptr, &uiInterp);
    (void)domains.readPropertySlotFast(
        aero::DomainKind::Ui, vmRef, *domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "PlayerTag"), tagVal, nullptr, &uiInterp);
    (void)domains.readPropertySlotFast(
        aero::DomainKind::Ui, vmRef, *domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "PresetIndex"), presetVal, nullptr, &uiInterp);

    AERO_CHECK(std::holds_alternative<bool>(muteVal) && std::get<bool>(muteVal),
               "CheckBox TwoWay automatically pushed MuteAll=true to ViewModel");
    float vmVol = 0.0f;
    AERO_CHECK(aero::TryGetFloat(volVal, vmVol) && ApproxEq(vmVol, 85.0f, 0.5f),
               "Slider TwoWay automatically pushed MasterVolume=85 to ViewModel");
    const auto tagRef = std::get_if<aero::ObjectRef>(&tagVal);
    AERO_CHECK(tagRef && domains.stringViewAcrossDomains(*tagRef).value_or("") == "Pilot-01",
               "TextBox TwoWay automatically allocated & pushed 'Pilot-01' String to ViewModel");
    AERO_CHECK(std::holds_alternative<std::int64_t>(presetVal) && std::get<std::int64_t>(presetVal) == 2,
               "ComboBox TwoWay automatically pushed PresetIndex=2 to ViewModel");

    // 5) Drag simSlider to 75% -> enqueues [SimCommand] envelope onto ExecutionDomainContext!
    const aero::Rect ssr = simSlider->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{ssr.x + ssr.width * 0.75f, ssr.y + 10.0f});
    (void)router.DispatchPointerUp(aero::Point{ssr.x + ssr.width * 0.75f, ssr.y + 10.0f});
    AERO_CHECK(domains.pendingSimCommandCount() >= 1u,
               "Dragging SimHeap-bound Slider enqueued SimCommandEnvelope");
    const std::size_t flushedCmds = domains.flushSimCommands(simInterp);
    AERO_CHECK(flushedCmds >= 1u && domains.pendingSimCommandCount() == 0u,
               "Deterministic SimTick flushed queued Slider [SimCommand]");
    const auto throttleSlot = *domains.resolvePropertySlot(
        *compiled.programImage,
        aero::rs_sem::stableTypeId("Game.Settings::SimTacticalRules"),
        "TargetThrottle");
    aero::UnifiedValue simFieldVal{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, simRef, throttleSlot, simFieldVal);
    float simThrottle = 0.0f;
    AERO_CHECK(aero::TryGetFloat(simFieldVal, simThrottle) && ApproxEq(simThrottle, 75.0f, 0.5f),
               "Drained SimCommandEnvelope updated SimHeap TargetThrottle=75.0");

    std::cout << "[PASS] TestTwoWayAutoWriteBackAndSimCommand\n";
}

// ============================================================================
// 7. Tab / Shift+Tab, 2D Gamepad Spatial Focus & Nested ScrollViewer Wheel Bubbling
// ============================================================================
void TestFocusNavigationAndNestedWheelBubbling() {
    auto root = aero::MakeIntrusive<aero::Canvas>();
    root->SetWidth(400.0f);
    root->SetHeight(400.0f);

    // 2x2 grid of focusable buttons + 1 disabled button + 1 Focusable=false button
    auto bTL = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bTL.Get(), 20.0f);
    aero::Canvas::SetTop(bTL.Get(), 20.0f);
    bTL->SetWidth(80.0f);
    bTL->SetHeight(30.0f);

    auto bDisabled = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bDisabled.Get(), 110.0f);
    aero::Canvas::SetTop(bDisabled.Get(), 20.0f);
    bDisabled->SetWidth(80.0f);
    bDisabled->SetHeight(30.0f);
    bDisabled->SetIsEnabled(false);

    auto bTR = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bTR.Get(), 220.0f);
    aero::Canvas::SetTop(bTR.Get(), 20.0f);
    bTR->SetWidth(80.0f);
    bTR->SetHeight(30.0f);

    auto bNonFocusable = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bNonFocusable.Get(), 20.0f);
    aero::Canvas::SetTop(bNonFocusable.Get(), 70.0f);
    bNonFocusable->SetWidth(80.0f);
    bNonFocusable->SetHeight(30.0f);
    bNonFocusable->SetFocusable(false);

    auto bBL = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bBL.Get(), 20.0f);
    aero::Canvas::SetTop(bBL.Get(), 140.0f);
    bBL->SetWidth(80.0f);
    bBL->SetHeight(30.0f);

    auto bBR = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(bBR.Get(), 220.0f);
    aero::Canvas::SetTop(bBR.Get(), 140.0f);
    bBR->SetWidth(80.0f);
    bBR->SetHeight(30.0f);

    root->AddChild(bTL.Get());
    root->AddChild(bDisabled.Get());
    root->AddChild(bTR.Get());
    root->AddChild(bNonFocusable.Get());
    root->AddChild(bBL.Get());
    root->AddChild(bBR.Get());

    // Nested ScrollViewers at (20, 200) to test MouseWheel bubbling + boundary scroll-chaining!
    auto outerSv = aero::MakeIntrusive<aero::ScrollViewer>();
    aero::Canvas::SetLeft(outerSv.Get(), 20.0f);
    aero::Canvas::SetTop(outerSv.Get(), 200.0f);
    outerSv->SetWidth(200.0f);
    outerSv->SetHeight(120.0f);

    auto outerContent = aero::MakeIntrusive<aero::StackPanel>();
    auto innerSv = aero::MakeIntrusive<aero::ScrollViewer>();
    innerSv->SetWidth(180.0f);
    innerSv->SetHeight(80.0f);
    auto innerTall = aero::MakeIntrusive<aero::Border>();
    innerTall->SetWidth(160.0f);
    innerTall->SetHeight(180.0f); // ScrollableHeight = 100px
    innerSv->AddChild(innerTall.Get());

    auto spacer = aero::MakeIntrusive<aero::Border>();
    spacer->SetWidth(180.0f);
    spacer->SetHeight(240.0f); // Total outer height = 320px -> outer ScrollableHeight = 200px
    outerContent->AddChild(innerSv.Get());
    outerContent->AddChild(spacer.Get());
    outerSv->AddChild(outerContent.Get());
    root->AddChild(outerSv.Get());

    root->Measure(aero::Size{400.0f, 400.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 400.0f});

    aero::InputEventRouter router(root.Get());

    // Sequential Tab skips bDisabled and bNonFocusable!
    (void)router.DispatchKeyDown(aero::KeyCode::Tab);
    AERO_CHECK(router.FocusedElement() == bTL.Get(), "First Tab focused bTL");
    (void)router.DispatchKeyDown(aero::KeyCode::Tab);
    AERO_CHECK(router.FocusedElement() == bTR.Get(), "Second Tab skipped disabled button -> focused bTR");
    (void)router.DispatchKeyDown(aero::KeyCode::Tab);
    AERO_CHECK(router.FocusedElement() == bBL.Get(), "Third Tab skipped Focusable=false -> focused bBL");
    (void)router.DispatchKeyDown(aero::KeyCode::Tab, aero::KeyMod_Shift);
    AERO_CHECK(router.FocusedElement() == bTR.Get(), "Shift+Tab reversed focus to bTR");

    // 2D Spatial directional focus (Gamepad D-Pad / Arrow keys)
    (void)router.DispatchKeyDown(aero::KeyCode::Down);
    AERO_CHECK(router.FocusedElement() == bBR.Get(), "Spatial Down moved from bTR to bBR");
    (void)router.DispatchKeyDown(aero::KeyCode::Left);
    AERO_CHECK(router.FocusedElement() == bBL.Get(), "Spatial Left moved from bBR to bBL");
    (void)router.DispatchKeyDown(aero::KeyCode::Up);
    AERO_CHECK(router.FocusedElement() == bTL.Get(), "Spatial Up moved from bBL to bTL");
    (void)router.DispatchKeyDown(aero::KeyCode::Right);
    AERO_CHECK(router.FocusedElement() == bTR.Get(), "Spatial Right moved from bTL to bTR");

    // Nested ScrollViewer MouseWheel bubbling:
    // Wheel at (60, 230) hits innerTall -> scrolls innerSv first (0 -> 60 -> 100), then chains to outerSv!
    (void)router.DispatchMouseWheel(aero::Point{60.0f, 230.0f}, 0.0f, 60.0f);
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    AERO_CHECK(ApproxEq(innerSv->GetVerticalOffset(), 60.0f) && ApproxEq(outerSv->GetVerticalOffset(), 0.0f),
               "MouseWheel scrolled inner ScrollViewer first");

    (void)router.DispatchMouseWheel(aero::Point{60.0f, 230.0f}, 0.0f, 60.0f);
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    AERO_CHECK(ApproxEq(innerSv->GetVerticalOffset(), 100.0f) && ApproxEq(outerSv->GetVerticalOffset(), 0.0f),
               "Inner ScrollViewer clamped at ScrollableHeight=100");

    // Next wheel event while innerSv is already at bottom (100) chains seamlessly to outerSv!
    (void)router.DispatchMouseWheel(aero::Point{60.0f, 230.0f}, 0.0f, 45.0f);
    root->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    AERO_CHECK(ApproxEq(outerSv->GetVerticalOffset(), 45.0f),
               "MouseWheel boundary-chained to outer ScrollViewer once inner hit bottom");

    std::cout << "[PASS] TestFocusNavigationAndNestedWheelBubbling\n";
}

// ============================================================================
// 8. VirtualizingStackPanel / Virtualized ItemsControl (10,000 Items Recycling) & Host ARIA
// ============================================================================
void TestVirtualizedItemsControl10kAndHostIntegration() {
    constexpr std::size_t kItemCount = 10000u;
    std::vector<aero::UnifiedValue> items;
    items.reserve(kItemCount);
    for (std::size_t i = 0; i < kItemCount; ++i) {
        items.push_back(aero::value::FromString("Row #" + std::to_string(i)));
    }

    const char* kVirtXaml = R"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
              x:Name="MainScroll" Width="280" Height="120">
    <ItemsControl x:Name="BigList">
        <ItemsControl.ItemsPanel>
            <ItemsPanelTemplate>
                <VirtualizingStackPanel/>
            </ItemsPanelTemplate>
        </ItemsControl.ItemsPanel>
    </ItemsControl>
</ScrollViewer>
)XAML";

    aero::AeroCompileInput in{};
    in.xamlPath = "BigList.xaml";
    in.xamlContent = kVirtXaml;
    aero::AeroCompiler compiler;
    const auto comp = compiler.Compile(in);
    AERO_CHECK(comp.succeeded, "Compiled XAML with <VirtualizingStackPanel/> inside <ItemsControl.ItemsPanel>");

    aero::AxbPackage pkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(comp.axbBytes, pkg), "Deserialized BigList.axb4");

    auto scroll = aero::MakeIntrusive<aero::ScrollViewer>();
    AERO_CHECK(aero::MountAxbTemplate(scroll.Get(), pkg), "Mounted BigList onto ScrollViewer");

    auto* bigList = scroll->FindName("BigList")->DynamicCast<aero::ItemsControl>();
    AERO_CHECK(bigList != nullptr && bigList->IsVirtualizing(),
               "<VirtualizingStackPanel/> automatically enabled PropId_IsVirtualizing on ItemsControl");

    bigList->SetVirtualItemHeight(24.0f);
    bigList->SetItemsSource(items);

    aero::DepthBucketLayoutQueue layoutQueue;
    aero::PagedRenderTree renderTree;
    aero::InputEventRouter router(scroll.Get());
    aero::DesktopEventHost desktopHost(scroll.Get(), nullptr, &layoutQueue, &renderTree);
    desktopHost.SetViewportSize(aero::Size{280.0f, 120.0f});
    desktopHost.SetInputRouter(&router);
    desktopHost.NotifyDirtyLayout();
    desktopHost.TickUi(0.016f, aero::UiPhase_All);

    AERO_CHECK(bigList->TotalItemCount() == 10000u, "TotalItemCount == 10,000");
    AERO_CHECK(ApproxEq(scroll->ExtentHeight(), 240000.0f, 1.0f),
               "ScrollViewer ExtentHeight == 240,000px for 10,000 x 24px items");
    AERO_CHECK(bigList->RealizedItemCount() <= 12u,
               "Virtualized ItemsControl realized <= 12 UIElements instead of 10,000!");
    AERO_CHECK(renderTree.NodeCount() < 32u,
               "PagedRenderTree contains O(visible) nodes (< 32) instead of 10,000+");
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost returned to 0% CPU idle after layout");

    // Capture pointers of realized TextBlocks before scrolling to row 5000 (y = 120,000px)
    std::vector<const aero::UIElement*> initialPoolPtrs;
    for (const aero::UIElement* c = bigList->FirstChild(); c != nullptr; c = c->NextSibling()) {
        initialPoolPtrs.push_back(c);
    }

    // Queue MouseWheel + jump to middle of 10,000-item list via InputEventRouter -> wakes DesktopEventHost!
    (void)router.QueueMouseWheel(aero::Point{100.0f, 60.0f}, 0.0f, 120000.0f);
    AERO_CHECK(!desktopHost.CanSleepIdle(), "Queued input event woke DesktopEventHost from idle");
    desktopHost.TickUi(0.016f, aero::UiPhase_All);
    AERO_CHECK(desktopHost.CanSleepIdle(),
               "DesktopEventHost returned to 0% CPU idle after virtualized scroll recycling");

    AERO_CHECK(ApproxEq(scroll->GetVerticalOffset(), 120000.0f, 1.0f),
               "ScrollViewer scrolled to y = 120,000px (item #5000)");
    AERO_CHECK(bigList->FirstRealizedItemIndex() >= 4996u &&
               bigList->FirstRealizedItemIndex() <= 5000u &&
               bigList->RealizedItemCount() <= 12u,
               "Virtualized window shifted cleanly to [~4998 .. ~5007]");

    // Verify container recycling reused the exact same C++ TextBlock instances!
    std::size_t recycledMatches = 0u;
    bool foundRow5000 = false;
    for (const aero::UIElement* c = bigList->FirstChild(); c != nullptr; c = c->NextSibling()) {
        for (const auto* oldPtr : initialPoolPtrs) {
            if (c == oldPtr) {
                ++recycledMatches;
                break;
            }
        }
        if (const auto* tb = c->DynamicCast<aero::TextBlock>(); tb && tb->GetText() == "Row #5000") {
            foundRow5000 = true;
        }
    }
    AERO_CHECK(foundRow5000, "Realized recycled TextBlock displays 'Row #5000'");
    AERO_CHECK(recycledMatches >= initialPoolPtrs.size() - 1u,
               "Offscreen TextBlock containers were recycled in-place without reallocation");

    // Verify WebImeAdapter ARIA semantic roles for Phase 5B controls
    auto ariaRoot = aero::MakeIntrusive<aero::StackPanel>();
    ariaRoot->AddChild(aero::MakeIntrusive<aero::CheckBox>().Get());
    ariaRoot->AddChild(aero::MakeIntrusive<aero::RadioButton>().Get());
    ariaRoot->AddChild(aero::MakeIntrusive<aero::Slider>().Get());
    ariaRoot->AddChild(aero::MakeIntrusive<aero::TextBox>().Get());
    ariaRoot->AddChild(aero::MakeIntrusive<aero::ComboBox>().Get());
    aero::WebImeAdapter ariaBuilder;
    const aero::AriaShadowNode shadow = ariaBuilder.BuildAriaShadowTree(ariaRoot.Get());
    AERO_CHECK(shadow.children.size() == 5u &&
               shadow.children[0].role == "checkbox" &&
               shadow.children[1].role == "radio" &&
               shadow.children[2].role == "slider" &&
               shadow.children[3].role == "textbox" &&
               shadow.children[4].role == "combobox",
               "ARIA shadow tree emitted checkbox, radio, slider, textbox, and combobox roles");

    std::cout << "[PASS] TestVirtualizedItemsControl10kAndHostIntegration\n";
}

// ============================================================================
// 9. Edge-Case Regressions (Container Disable Sync, Popup Collapse, IME Insert,
//    ComboBoxItem Inner Text, CheckBox Child Arrange, TextBox Scroll & Web Wake)
// ============================================================================
void TestPhase5bEdgeCaseRegressions() {
    // 1) Parent container SetIsEnabled(false) syncs child Button/Slider visual opacity & resets Slider drag
    {
        auto panel = aero::MakeIntrusive<aero::StackPanel>();
        panel->SetWidth(240.0f);
        panel->SetHeight(160.0f);
        auto childBtn = aero::MakeIntrusive<aero::Button>();
        childBtn->SetWidth(120.0f);
        childBtn->SetHeight(32.0f);
        childBtn->SetContentText("Action");
        auto childSlider = aero::MakeIntrusive<aero::Slider>();
        childSlider->SetWidth(160.0f);
        childSlider->SetHeight(24.0f);
        auto childCombo = aero::MakeIntrusive<aero::ComboBox>();
        childCombo->SetWidth(160.0f);
        childCombo->SetHeight(28.0f);
        childCombo->SetItemsSource({aero::value::FromString("A"), aero::value::FromString("B")});
        panel->AddChild(childBtn.Get());
        panel->AddChild(childSlider.Get());
        panel->AddChild(childCombo.Get());

        panel->Measure(aero::Size{240.0f, 160.0f});
        panel->Arrange(aero::Rect{0.0f, 0.0f, 240.0f, 160.0f});

        aero::PagedRenderTree renderTree;
        (void)renderTree.BuildFromVisualTree(panel.Get());
        const auto* btnNodeBefore = renderTree.GetNode(childBtn->RenderNodeId());
        AERO_CHECK(btnNodeBefore && ApproxEq(btnNodeBefore->opacity, 1.0f),
                   "Child Button initially rendered with opacity 1.0");

        aero::InputEventRouter router(panel.Get());
        // Open ComboBox and start dragging Slider
        childCombo->SetIsDropDownOpen(true);
        (void)renderTree.SyncVisualTree(panel.Get());
        const std::size_t nodesWithPopup = renderTree.NodeCount();

        const aero::Rect sr = childSlider->FinalRect();
        (void)router.DispatchPointerDown(aero::Point{sr.x + 20.0f, sr.y + 10.0f});
        AERO_CHECK(childSlider->IsDraggingThumb(), "Slider is dragging before parent disable");

        // Disable parent container -> childSlider stops dragging, childCombo closes popup, childBtn dims to 0.55f!
        panel->SetIsEnabled(false);
        AERO_CHECK(!childSlider->IsDraggingThumb(),
                   "Disabling ancestor container reset Slider::IsDraggingThumb()");
        AERO_CHECK(!childCombo->IsDropDownOpen(),
                   "Disabling ancestor container closed child ComboBox dropdown");

        const std::size_t synced = renderTree.SyncVisualTree(panel.Get());
        AERO_CHECK(synced > 0u, "SyncVisualTree updated descendants when parent container was disabled");
        const auto* btnNodeAfter = renderTree.GetNode(childBtn->RenderNodeId());
        AERO_CHECK(btnNodeAfter && ApproxEq(btnNodeAfter->opacity, 0.55f, 0.02f),
                   "Child Button render node dimmed to 0.55 opacity when parent container was disabled");
        AERO_CHECK(renderTree.NodeCount() < nodesWithPopup,
                   "ComboBox top-layer popup nodes removed when ancestor container was disabled");

        panel->SetIsEnabled(true);
        (void)router.DispatchPointerDown(aero::Point{sr.x + 40.0f, sr.y + 10.0f});
        AERO_CHECK(childSlider->IsDraggingThumb(), "Slider dragging again after re-enabling");
        router.CancelPointerInteraction();
        AERO_CHECK(!childSlider->IsDraggingThumb(),
                   "CancelPointerInteraction() reset Slider::IsDraggingThumb()");
    }

    // 2) Collapsing ancestor of open ComboBox removes deferred top-layer popup nodes cleanly
    {
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        auto wrapper = aero::MakeIntrusive<aero::StackPanel>();
        auto combo = aero::MakeIntrusive<aero::ComboBox>();
        combo->SetWidth(150.0f);
        combo->SetHeight(28.0f);
        combo->SetItemsSource({aero::value::FromString("One"), aero::value::FromString("Two")});
        wrapper->AddChild(combo.Get());
        root->AddChild(wrapper.Get());

        root->Measure(aero::Size{200.0f, 200.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 200.0f, 200.0f});

        aero::PagedRenderTree tree;
        (void)tree.BuildFromVisualTree(root.Get());
        const std::size_t baseNodes = tree.NodeCount();

        combo->SetIsDropDownOpen(true);
        (void)tree.SyncVisualTree(root.Get());
        AERO_CHECK(tree.NodeCount() > baseNodes, "Open ComboBox added popup overlay nodes");

        wrapper->SetVisibility(aero::Visibility::Collapsed);
        (void)tree.SyncVisualTree(root.Get());
        AERO_CHECK(tree.NodeCount() <= baseNodes,
                   "Collapsing wrapper around open ComboBox removed top-layer popup nodes");
    }

    // 3) Non-destructive WebImeAdapter composition on TextBox + PasswordBox IME suppression + horizontal scroll
    {
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        auto tbox = aero::MakeIntrusive<aero::TextBox>();
        tbox->SetWidth(80.0f);
        tbox->SetHeight(28.0f);
        tbox->SetText("HelloWorld");
        tbox->SetCaretIndex(5u); // Between "Hello" and "World"

        auto pwd = aero::MakeIntrusive<aero::PasswordBox>();
        pwd->SetWidth(120.0f);
        pwd->SetHeight(28.0f);
        root->AddChild(tbox.Get());
        root->AddChild(pwd.Get());

        root->Measure(aero::Size{200.0f, 100.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 200.0f, 100.0f});

        aero::WebImeAdapter ime;
        aero::InputEventRouter router(root.Get(), &ime);
        (void)router.SetFocus(tbox.Get());
        AERO_CHECK(ime.IsImeActive(), "TextBox focus activated WebImeAdapter");

        ime.OnCompositionStart();
        ime.OnCompositionUpdate("zhong", 5u);
        AERO_CHECK(tbox->GetText() == "HellozhongWorld",
                   "WebImeAdapter live preedit spliced at caret without clobbering existing TextBox text");
        ime.OnCompositionEnd("中文");
        AERO_CHECK(tbox->GetText() == "Hello中文World" && tbox->GetCaretIndex() == 7u,
                   "WebImeAdapter committed text inserted at caret preserving prefix & suffix");

        // Focusing PasswordBox must blur IME
        (void)router.SetFocus(pwd.Get());
        AERO_CHECK(!ime.IsImeActive(), "PasswordBox focus suppressed/blurred IME host");

        // Narrow TextBox horizontal scroll keeps CaretLocalRect within box width
        tbox->SetText("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        tbox->MoveCaretEnd();
        const aero::Rect caretR = tbox->CaretLocalRect();
        const aero::Rect boxR = tbox->FinalRect();
        AERO_CHECK(tbox->HorizontalTextScrollOffset() > 0.0f &&
                   caretR.x + caretR.width <= boxR.x + boxR.width,
                   "TextBox HorizontalTextScrollOffset kept caret inside narrow TextBox bounds");
    }

    // 4) <ComboBoxItem>Text</ComboBoxItem> XAML inner text & CheckBox child Arrange offset + WebCanvasHost wake
    {
        const char* kXaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="240" Height="160">
    <ComboBox x:Name="QualityBox" SelectedIndex="1" Width="160" Height="28">
        <ComboBoxItem>Low</ComboBoxItem>
        <ComboBoxItem>Ultra</ComboBoxItem>
    </ComboBox>
    <CheckBox x:Name="CustomCheck" Width="160" Height="28">
        <TextBlock x:Name="CheckChildText" Text="Custom Child"/>
    </CheckBox>
</StackPanel>
)XAML";
        aero::AeroCompileInput in{};
        in.xamlPath = "ComboItems.xaml";
        in.xamlContent = kXaml;
        aero::AeroCompiler compiler;
        const auto comp = compiler.Compile(in);
        AERO_CHECK(comp.succeeded, "Compiled <ComboBoxItem> inner text XAML");

        aero::AxbPackage pkg{};
        AERO_CHECK(aero::AxbPackage::Deserialize(comp.axbBytes, pkg), "Deserialized ComboItems.axb4");
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        AERO_CHECK(aero::MountAxbTemplate(root.Get(), pkg), "Mounted ComboItems.axb4");

        aero::DepthBucketLayoutQueue layoutQueue;
        aero::PagedRenderTree renderTree;
        aero::InputEventRouter router(root.Get());
        aero::WebCanvasHost webHost(root.Get(), nullptr, &layoutQueue, aero::ShaderBackend::WebGL2, &renderTree);
        webHost.SetViewportSize(aero::Size{240.0f, 160.0f});
        webHost.SetInputRouter(&router);
        webHost.TickUi(0.016f, aero::UiPhase_All);

        auto* qbox = root->FindName("QualityBox")->DynamicCast<aero::ComboBox>();
        auto* chk = root->FindName("CustomCheck")->DynamicCast<aero::CheckBox>();
        auto* chkChild = root->FindName("CheckChildText");
        AERO_CHECK(qbox && qbox->ComboItemCount() == 2u &&
                   qbox->GetComboItemText(0) == "Low" &&
                   qbox->GetComboItemText(1) == "Ultra" &&
                   qbox->GetSelectedItemText() == "Ultra",
                   "<ComboBoxItem> inner text populated ComboBox items ('Low', 'Ultra')");
        AERO_CHECK(chk && chkChild &&
                   chkChild->FinalRect().x >= chk->FinalRect().x + 26.0f - 0.1f,
                   "CheckBox::ArrangeCore offset child element by 26px to clear indicator box");

        // Let WebCanvasHost suspend on a clean frame, then verify queued input wakes it!
        webHost.TickUi(0.016f, aero::UiPhase_All);
        AERO_CHECK(webHost.CanSleepIdle(), "WebCanvasHost suspended on clean frame");
        (void)router.QueuePointerButton(
            aero::Point{chk->FinalRect().x + 10.0f, chk->FinalRect().y + 10.0f},
            aero::PointerButton::Left,
            true);
        (void)router.QueuePointerButton(
            aero::Point{chk->FinalRect().x + 10.0f, chk->FinalRect().y + 10.0f},
            aero::PointerButton::Left,
            false);
        AERO_CHECK(!webHost.CanSleepIdle(), "Pending InputEventRouter queue woke WebCanvasHost::CanSleepIdle()");
        webHost.TickUi(0.016f, aero::UiPhase_All);
        AERO_CHECK(chk->IsChecked(), "WebCanvasHost woke from SuspendedIdle and dispatched queued CheckBox click");
    }

    std::cout << "[PASS] TestPhase5bEdgeCaseRegressions\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    TestInlineInteractiveStatesAndZeroAllocRouter();
    TestPointerCaptureAndSliderDrag();
    TestCheckBoxAndRadioButtonGroups();
    TestTextBoxUtf8EditingAndImeSync();
    TestComboBoxPopupOverlayAndSelection();
    TestTwoWayAutoWriteBackAndSimCommand();
    TestFocusNavigationAndNestedWheelBubbling();
    TestVirtualizedItemsControl10kAndHostIntegration();
    TestPhase5bEdgeCaseRegressions();

    std::cout << "All Phase 5B Input Router, Stateful Controls, TwoWay Auto-Push & Virtualized ItemsControl Tests Passed 100%!\n";
    return 0;
}
