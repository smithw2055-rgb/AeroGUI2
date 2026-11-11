#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace {

std::atomic<bool> gTrackHeapAllocs{false};
std::atomic<std::size_t> gHeapAllocCount{0u};

} // namespace

void* operator new(std::size_t size) {
    if (gTrackHeapAllocs.load(std::memory_order_relaxed)) {
        gHeapAllocCount.fetch_add(1u, std::memory_order_relaxed);
    }
    void* p = std::malloc(size == 0u ? 1u : size);
    if (!p) {
        std::abort();
    }
    return p;
}

void* operator new[](std::size_t size) {
    if (gTrackHeapAllocs.load(std::memory_order_relaxed)) {
        gHeapAllocCount.fetch_add(1u, std::memory_order_relaxed);
    }
    void* p = std::malloc(size == 0u ? 1u : size);
    if (!p) {
        std::abort();
    }
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

#define AERO_CHECK(cond)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(                                                                    \
                stderr, "[FAIL] %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond);  \
            std::exit(1);                                                                    \
        }                                                                                    \
    } while (false)

[[nodiscard]] bool ColorNear(const aero::Color& a, const aero::Color& b, float eps = 0.02f) noexcept {
    return std::fabs(a.r - b.r) <= eps &&
           std::fabs(a.g - b.g) <= eps &&
           std::fabs(a.b - b.b) <= eps &&
           std::fabs(a.a - b.a) <= eps;
}

[[nodiscard]] std::string ReadFileText(const char* path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// (a) Style -> Setter(Template) -> ControlTemplate compile, .axb4 roundtrip & automatic mounting + PagedRenderTree switch
void TestStyleInlineControlTemplateAxb4RoundTripAndAutoMount() {
    aero::AxbTemplateRegistry::Instance().Clear();

    // Verify reserved P1-1 enum & opcode ranges strictly adhere to non-conflict contract
    AERO_CHECK(static_cast<std::uint16_t>(aero::PropId_Template) == 90u);
    AERO_CHECK(static_cast<std::uint16_t>(aero::PropId_TemplatedParent) == 91u);
    AERO_CHECK(static_cast<std::uint16_t>(aero::PropId_HorizontalContentAlignment) == 92u);
    AERO_CHECK(static_cast<std::uint16_t>(aero::PropId_VerticalContentAlignment) == 93u);
    AERO_CHECK(static_cast<std::uint16_t>(aero::PropId_ContentStringFormat) == 94u);
    AERO_CHECK(static_cast<std::uint8_t>(aero::AxbOpcode::BindTemplateParent) == 10u);

    const char* xaml = R"(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="400" Height="200">
    <Window.Resources>
        <Style TargetType="Button">
            <Setter Property="Background" Value="#FF1E5A96"/>
            <Setter Property="BorderBrush" Value="#FF63B3ED"/>
            <Setter Property="Foreground" Value="#FFF8FAFC"/>
            <Setter Property="Padding" Value="16,8,16,8"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="Bd"
                                Background="{TemplateBinding Background}"
                                BorderBrush="{TemplateBinding BorderBrush}"
                                BorderThickness="2"
                                CornerRadius="10"
                                Padding="{TemplateBinding Padding}">
                            <ContentPresenter x:Name="PART_ContentPresenter"/>
                        </Border>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
    </Window.Resources>
    <StackPanel Orientation="Vertical" Spacing="12">
        <Button x:Name="TemplatedBtn" Width="180" Height="44" Content="LAUNCH"/>
        <CheckBox x:Name="UntemplatedChk" Content="DefaultCheckBox" IsChecked="True"/>
    </StackPanel>
</Window>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "RoundTripTemplateView.xaml";
    in.xamlContent = xaml;
    aero::AeroCompileResult compRes = compiler.Compile(in);
    AERO_CHECK(compRes.succeeded);
    AERO_CHECK(compRes.axbPackage.controlTemplates.size() == 1u);
    AERO_CHECK(compRes.axbPackage.controlTemplates[0].templatePackage != nullptr);

    // Verify AxbOpcode::BindTemplateParent (10) was emitted inside the sub-package
    std::size_t bindTplParentCount = 0u;
    for (const auto& inst : compRes.axbPackage.controlTemplates[0].templatePackage->instructions) {
        if (inst.opcode == aero::AxbOpcode::BindTemplateParent) {
            ++bindTplParentCount;
        }
    }
    AERO_CHECK(bindTplParentCount == 3u);

    // Serialize -> Clear Registry -> Deserialize roundtrip
    const std::vector<std::uint8_t> axbBytes = compRes.axbPackage.Serialize();
    AERO_CHECK(!axbBytes.empty());
    aero::AxbTemplateRegistry::Instance().Clear();

    aero::AxbPackage roundTripPkg{};
    std::string errMsg;
    AERO_CHECK(aero::AxbPackage::Deserialize(axbBytes, roundTripPkg, &errMsg));
    AERO_CHECK(roundTripPkg.controlTemplates.size() == 1u);
    AERO_CHECK(roundTripPkg.styles.size() == 1u);
    AERO_CHECK(!roundTripPkg.styles[0].controlTemplateKey.empty());

    // Mount from deserialized package and verify automatic ControlTemplate instantiation
    auto win = aero::MakeIntrusive<aero::Window>();
    AERO_CHECK(aero::MountAxbTemplate(win.Get(), roundTripPkg));

    auto* btnElem = win->FindName("TemplatedBtn");
    AERO_CHECK(btnElem != nullptr && btnElem->IsA<aero::Button>());
    AERO_CHECK(btnElem->HasControlTemplate());

    auto* bd = btnElem->GetTemplateChild("Bd");
    auto* cp = btnElem->GetTemplateChild("PART_ContentPresenter");
    AERO_CHECK(bd != nullptr && bd->IsA<aero::Border>());
    AERO_CHECK(cp != nullptr && cp->IsA<aero::ContentPresenter>());
    AERO_CHECK(bd->TemplatedParent() == btnElem);
    AERO_CHECK(cp->TemplatedParent() == btnElem);

    auto* borderCtrl = bd->DynamicCast<aero::Border>();
    auto* cpCtrl = cp->DynamicCast<aero::ContentPresenter>();
    AERO_CHECK(ColorNear(borderCtrl->GetBackground(), aero::ParseXamlColorLiteral("#FF1E5A96")));
    AERO_CHECK(ColorNear(borderCtrl->GetBorderBrush(), aero::ParseXamlColorLiteral("#FF63B3ED")));
    AERO_CHECK(borderCtrl->GetPadding() == aero::Thickness(16.0f, 8.0f, 16.0f, 8.0f));
    AERO_CHECK(cpCtrl->GetEffectiveText() == "LAUNCH");

    // Layout & PagedRenderTree verification: templated Button skips hardcoded geometry (span=1 invisible anchor),
    // while untemplated CheckBox retains its default hardcoded indicator + label rendering!
    win->Measure(aero::Size{400.0f, 200.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 200.0f});

    aero::PagedRenderTree renderTree;
    const std::size_t nodeCount = renderTree.BuildFromVisualTree(win.Get());
    AERO_CHECK(nodeCount > 4u);
    AERO_CHECK(btnElem->RenderNodeSpan() == 1u);
    AERO_CHECK(renderTree.GetNode(btnElem->RenderNodeId())->opacity == 0.0f);
    AERO_CHECK(bd->RenderNodeSpan() == 2u); // Fill + BorderRing from template Border!
    AERO_CHECK(cp->RenderNodeSpan() >= 2u); // Anchor + SdfTextGlyph run for "LAUNCH"!

    auto* chkElem = win->FindName("UntemplatedChk");
    AERO_CHECK(chkElem != nullptr && !chkElem->HasControlTemplate());
    AERO_CHECK(chkElem->RenderNodeSpan() >= 3u); // Default outer box + check mark + label glyphs

    std::printf("[PASS] (a) TestStyleInlineControlTemplateAxb4RoundTripAndAutoMount\n");
}

// (b) Dynamic synchronization of {TemplateBinding} & <ContentPresenter/> with 0 heap allocations on hot path
void TestTemplateBindingAndContentPresenterZeroAllocSync() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
        Width="500" Height="240">
    <Window.Resources>
        <ControlTemplate x:Key="CustomBtnTpl" TargetType="Button">
            <Border x:Name="PART_Border"
                    Background="{TemplateBinding Background}"
                    BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="2"
                    Padding="{TemplateBinding Padding}"
                    CornerRadius="6">
                <StackPanel Orientation="Vertical">
                    <ContentPresenter x:Name="PART_Content"/>
                    <TextBlock x:Name="PART_SubHint"
                               Text="{Binding (noesis:Text.Placeholder), RelativeSource={RelativeSource TemplatedParent}}"/>
                </StackPanel>
            </Border>
        </ControlTemplate>
    </Window.Resources>
    <Button x:Name="DynBtn"
            Width="220"
            Height="64"
            Template="{StaticResource CustomBtnTpl}"
            Background="#FF223344"
            BorderBrush="#FF556677"
            Foreground="#FFE2E8F0"
            FontSize="16"
            Padding="10,4,10,4"
            noesis:Text.Placeholder="SubTitleInit"
            Content="InitialLabel"/>
</Window>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "ZeroAllocTemplateSync.xaml";
    in.xamlContent = xaml;
    aero::AeroCompileResult res = compiler.Compile(in);
    AERO_CHECK(res.succeeded);

    auto win = aero::MakeIntrusive<aero::Window>();
    AERO_CHECK(aero::MountAxbTemplate(win.Get(), res.axbPackage));
    win->Measure(aero::Size{500.0f, 240.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 240.0f});

    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(win.Get());
    renderTree.ClearDirtyPages();

    auto* btn = win->FindName("DynBtn")->DynamicCast<aero::Button>();
    AERO_CHECK(btn != nullptr && btn->HasControlTemplate());
    auto* partBorder = btn->GetTemplateChild("PART_Border")->DynamicCast<aero::Border>();
    auto* partContent = btn->GetTemplateChild("PART_Content")->DynamicCast<aero::ContentPresenter>();
    auto* partHint = btn->GetTemplateChild("PART_SubHint")->DynamicCast<aero::TextBlock>();
    AERO_CHECK(partBorder != nullptr && partContent != nullptr && partHint != nullptr);
    AERO_CHECK(partContent->GetEffectiveText() == "InitialLabel");
    AERO_CHECK(partHint->GetText() == "SubTitleInit");

    // Pre-create UnifiedValues outside hot-path loop to measure pure runtime propagation & render sync
    const aero::UnifiedValue bgA = aero::MakeFloat4InlineFast(0.85f, 0.20f, 0.25f, 1.0f);
    const aero::UnifiedValue bgB = aero::MakeFloat4InlineFast(0.15f, 0.65f, 0.45f, 1.0f);
    const aero::UnifiedValue brdA = aero::MakeFloat4InlineFast(0.95f, 0.80f, 0.20f, 1.0f);
    const aero::UnifiedValue fgA = aero::MakeFloat4InlineFast(1.0f, 0.95f, 0.80f, 1.0f);
    const aero::UnifiedValue contentA = aero::value::FromString("HotSyncA");
    const aero::UnifiedValue contentB = aero::value::FromString("HotSyncB");
    const aero::UnifiedValue hintNext = aero::value::FromString("UpdatedHint");

    // Warm up one pass so glyph capacity is settled, then assert strictly 0 heap allocations over 50 iterations!
    (void)btn->SetValue(aero::PropId_Content, contentA);
    (void)btn->SetValue(aero::PropId_Placeholder, hintNext);
    win->Measure(aero::Size{500.0f, 240.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 240.0f});
    (void)renderTree.SyncVisualTree(win.Get());
    renderTree.ClearDirtyPages();
    AERO_CHECK(partHint->GetText() == "UpdatedHint");

    gHeapAllocCount.store(0u, std::memory_order_relaxed);
    gTrackHeapAllocs.store(true, std::memory_order_seq_cst);

    std::size_t syncBindAllocs = 0u;
    std::size_t layoutRenderAllocs = 0u;
    for (int iter = 0; iter < 50; ++iter) {
        const bool odd = (iter & 1) != 0;
        const std::size_t a0 = gHeapAllocCount.load(std::memory_order_relaxed);
        (void)btn->SetValue(aero::PropId_Background, odd ? bgA : bgB);
        (void)btn->SetValue(aero::PropId_BorderBrush, brdA);
        (void)btn->SetValue(aero::PropId_Foreground, fgA);
        btn->SetPadding(odd ? aero::Thickness(12.0f, 6.0f, 12.0f, 6.0f)
                            : aero::Thickness(8.0f, 4.0f, 8.0f, 4.0f));
        (void)btn->SetValue(aero::PropId_Content, odd ? contentA : contentB);
        const std::size_t a1 = gHeapAllocCount.load(std::memory_order_relaxed);
        syncBindAllocs += (a1 - a0);
        win->Measure(aero::Size{500.0f, 240.0f});
        win->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 240.0f});
        (void)renderTree.SyncVisualTree(win.Get());
        renderTree.ClearDirtyPages();
        const std::size_t a2 = gHeapAllocCount.load(std::memory_order_relaxed);
        layoutRenderAllocs += (a2 - a1);
    }
    gTrackHeapAllocs.store(false, std::memory_order_seq_cst);

    const std::size_t hotAllocs = syncBindAllocs;
    AERO_CHECK(syncBindAllocs == 0u);

    // Also verify full end-to-end SetValue + PagedRenderTree::SyncVisualTree performs 0 heap allocations on color/border updates!
    gHeapAllocCount.store(0u, std::memory_order_relaxed);
    gTrackHeapAllocs.store(true, std::memory_order_seq_cst);
    for (int iter = 0; iter < 50; ++iter) {
        const bool odd = (iter & 1) != 0;
        (void)btn->SetValue(aero::PropId_Background, odd ? bgA : bgB);
        (void)btn->SetValue(aero::PropId_BorderBrush, brdA);
        (void)renderTree.SyncVisualTree(win.Get());
        renderTree.ClearDirtyPages();
    }
    gTrackHeapAllocs.store(false, std::memory_order_seq_cst);
    AERO_CHECK(gHeapAllocCount.load(std::memory_order_relaxed) == 0u);

    AERO_CHECK(ColorNear(partBorder->GetBackground(), aero::Color{0.85f, 0.20f, 0.25f, 1.0f}));
    AERO_CHECK(ColorNear(partBorder->GetBorderBrush(), aero::Color{0.95f, 0.80f, 0.20f, 1.0f}));
    AERO_CHECK(partBorder->GetPadding() == aero::Thickness(12.0f, 6.0f, 12.0f, 6.0f));
    AERO_CHECK(partContent->GetEffectiveText() == "HotSyncA");
    AERO_CHECK(ColorNear(partContent->GetEffectiveForeground(), aero::Color{1.0f, 0.95f, 0.80f, 1.0f}));

    std::printf("[PASS] (b) TestTemplateBindingAndContentPresenterZeroAllocSync (hotAllocs=%zu)\n", hotAllocs);
}

// (c) <ControlTemplate.Triggers> driven by InputEventRouter hover/press/check overriding TargetName elements & Storyboards
void TestControlTemplateTriggersHoverPressAndCheckViaInputRouter() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="400" Height="220">
    <Window.Resources>
        <Style TargetType="Button">
            <Setter Property="Background" Value="#FF2B3A4A"/>
            <Setter Property="BorderBrush" Value="#FF4A607A"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="Bd"
                                Background="{TemplateBinding Background}"
                                BorderBrush="{TemplateBinding BorderBrush}"
                                BorderThickness="2"
                                CornerRadius="8">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="Bd" Property="Background" Value="#FF2E86DE"/>
                                <Setter TargetName="Bd" Property="BorderBrush" Value="#FF74B9FF"/>
                            </Trigger>
                            <Trigger Property="IsPressed" Value="True">
                                <Setter TargetName="Bd" Property="Background" Value="#FF0984E3"/>
                                <Trigger.EnterActions>
                                    <BeginStoryboard>
                                        <Storyboard>
                                            <DoubleAnimation Storyboard.TargetName="Bd"
                                                             Storyboard.TargetProperty="Opacity"
                                                             To="0.75"
                                                             Duration="0:0:0.1"/>
                                        </Storyboard>
                                    </BeginStoryboard>
                                </Trigger.EnterActions>
                                <Trigger.ExitActions>
                                    <BeginStoryboard>
                                        <Storyboard>
                                            <DoubleAnimation Storyboard.TargetName="Bd"
                                                             Storyboard.TargetProperty="Opacity"
                                                             To="1.0"
                                                             Duration="0:0:0.1"/>
                                        </Storyboard>
                                    </BeginStoryboard>
                                </Trigger.ExitActions>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="Bd" Property="Background" Value="#FF555555"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>

        <ControlTemplate x:Key="CustomCheckTpl" TargetType="CheckBox">
            <StackPanel Orientation="Horizontal" Spacing="8">
                <Border x:Name="Box" Width="20" Height="20" Background="#FF1F2937" CornerRadius="4">
                    <Border x:Name="CheckMark" Width="12" Height="12" Background="#FF10B981" Visibility="Collapsed"/>
                </Border>
                <ContentPresenter VerticalAlignment="Center"/>
            </StackPanel>
            <ControlTemplate.Triggers>
                <Trigger Property="IsChecked" Value="True">
                    <Setter TargetName="CheckMark" Property="Visibility" Value="Visible"/>
                </Trigger>
            </ControlTemplate.Triggers>
        </ControlTemplate>
    </Window.Resources>

    <StackPanel Orientation="Vertical" Spacing="16">
        <Button x:Name="ActionBtn" Width="160" Height="48" HorizontalAlignment="Left" Content="SUBMIT"/>
        <CheckBox x:Name="AgreeChk" Width="160" Height="28" HorizontalAlignment="Left"
                  Template="{StaticResource CustomCheckTpl}" Content="Agree"/>
    </StackPanel>
</Window>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "TemplateTriggersTest.xaml";
    in.xamlContent = xaml;
    aero::AeroCompileResult res = compiler.Compile(in);
    AERO_CHECK(res.succeeded);

    auto win = aero::MakeIntrusive<aero::Window>();
    win->SetAsRootVisual(true);
    AERO_CHECK(aero::MountAxbTemplate(win.Get(), res.axbPackage));

    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, nullptr);
    styleEngine.RegisterPackageStyles(res.axbPackage);
    styleEngine.ImportRegistryStyles();
    AERO_CHECK(styleEngine.BindSubtreeStyles(win.Get()) >= 2u);

    win->Measure(aero::Size{400.0f, 220.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 400.0f, 220.0f});
    (void)styleEngine.EvaluateAndApply();

    auto* btn = win->FindName("ActionBtn")->DynamicCast<aero::Button>();
    auto* chk = win->FindName("AgreeChk")->DynamicCast<aero::CheckBox>();
    AERO_CHECK(btn != nullptr && chk != nullptr);

    auto* bd = btn->GetTemplateChild("Bd")->DynamicCast<aero::Border>();
    auto* checkMark = chk->GetTemplateChild("CheckMark")->DynamicCast<aero::Border>();
    AERO_CHECK(bd != nullptr && checkMark != nullptr);

    // Initial resting state
    AERO_CHECK(ColorNear(bd->GetBackground(), aero::ParseXamlColorLiteral("#FF2B3A4A")));
    AERO_CHECK(checkMark->GetVisibility() == aero::Visibility::Collapsed);

    aero::InputEventRouter router(win.Get());
    router.SetAnimationEngine(&animEngine);

    const aero::Rect btnRect = btn->FinalRect();
    const aero::Point btnCenter{btnRect.x + btnRect.width * 0.5f, btnRect.y + btnRect.height * 0.5f};

    // 1. Hover over ActionBtn via InputEventRouter
    (void)router.QueuePointerMove(btnCenter);
    (void)router.FlushQueuedEvents();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(btn->IsHovered());
    AERO_CHECK(ColorNear(bd->GetBackground(), aero::ParseXamlColorLiteral("#FF2E86DE")));
    AERO_CHECK(ColorNear(bd->GetBorderBrush(), aero::ParseXamlColorLiteral("#FF74B9FF")));

    // 2. Press ActionBtn -> IsPressed trigger + EnterActions Storyboard drives Bd.Opacity -> 0.75f
    (void)router.QueuePointerButton(btnCenter, aero::PointerButton::Left, true);
    (void)router.FlushQueuedEvents();
    (void)styleEngine.EvaluateAndApply();
    (void)animEngine.Tick(0.12f);
    AERO_CHECK(btn->IsPressed());
    AERO_CHECK(ColorNear(bd->GetBackground(), aero::ParseXamlColorLiteral("#FF0984E3")));
    AERO_CHECK(std::fabs(bd->GetOpacity() - 0.75f) <= 0.02f);

    // Mutate host Button's Background while hovered/pressed: TemplateTrigger (rank 4) stays active until exit!
    const aero::Color updatedRestBg = aero::ParseXamlColorLiteral("#FF3B1F5E");
    btn->SetValue(aero::PropId_Background, aero::value::FromColor(updatedRestBg));
    AERO_CHECK(ColorNear(bd->GetBackground(), aero::ParseXamlColorLiteral("#FF0984E3")));

    // 3. Release button & move pointer outside -> restores to updatedRestBg & ExitActions restores Opacity -> 1.0f
    (void)router.QueuePointerButton(btnCenter, aero::PointerButton::Left, false);
    (void)router.QueuePointerMove(aero::Point{380.0f, 200.0f});
    (void)router.FlushQueuedEvents();
    (void)styleEngine.EvaluateAndApply();
    (void)animEngine.Tick(0.12f);
    AERO_CHECK(!btn->IsHovered() && !btn->IsPressed());
    AERO_CHECK(ColorNear(bd->GetBackground(), updatedRestBg));
    AERO_CHECK(std::fabs(bd->GetOpacity() - 1.0f) <= 0.02f);

    // 4. Click templated CheckBox via InputEventRouter -> toggles IsChecked & reveals CheckMark
    const aero::Rect chkRect = chk->FinalRect();
    const aero::Point chkCenter{chkRect.x + 10.0f, chkRect.y + chkRect.height * 0.5f};
    (void)router.QueuePointerMove(chkCenter);
    (void)router.QueuePointerButton(chkCenter, aero::PointerButton::Left, true);
    (void)router.QueuePointerButton(chkCenter, aero::PointerButton::Left, false);
    (void)router.FlushQueuedEvents();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(chk->IsChecked());
    AERO_CHECK(checkMark->GetVisibility() == aero::Visibility::Visible);

    std::printf("[PASS] (c) TestControlTemplateTriggersHoverPressAndCheckViaInputRouter\n");
}

// (d) GetTemplateChild("PART_Track" / "PART_Indicator" / "PART_ContentHost") contract & Slider indicator sync
void TestPartContractLookupAndTemplatedSliderSync() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="320" Height="160">
    <Window.Resources>
        <ControlTemplate x:Key="ModernSliderTpl" TargetType="Slider">
            <Grid Height="24">
                <Border x:Name="PART_Track" Height="8" VerticalAlignment="Center"
                        Background="#FF1E293B" CornerRadius="4">
                    <Border x:Name="PART_Indicator" HorizontalAlignment="Left" Height="8"
                            Background="{TemplateBinding Foreground}" CornerRadius="4"/>
                </Border>
            </Grid>
        </ControlTemplate>

        <ControlTemplate x:Key="ModernTextBoxTpl" TargetType="TextBox">
            <Border x:Name="Bd" Background="{TemplateBinding Background}" CornerRadius="6" Padding="8,4,8,4">
                <ScrollViewer x:Name="PART_ContentHost"/>
            </Border>
        </ControlTemplate>
    </Window.Resources>

    <StackPanel Orientation="Vertical" Spacing="12">
        <Slider x:Name="VolSlider" Width="200" Height="24" HorizontalAlignment="Left"
                Minimum="0" Maximum="100" Value="25" Foreground="#FF38BDF8"
                Template="{StaticResource ModernSliderTpl}"/>
        <TextBox x:Name="SearchInput" Width="200" Height="32" HorizontalAlignment="Left"
                 Background="#FF0F172A" Template="{StaticResource ModernTextBoxTpl}"/>
        <Slider x:Name="PlainSlider" Width="200" Height="24" HorizontalAlignment="Left"/>
    </StackPanel>
</Window>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "PartContractTest.xaml";
    in.xamlContent = xaml;
    aero::AeroCompileResult res = compiler.Compile(in);
    AERO_CHECK(res.succeeded);

    auto win = aero::MakeIntrusive<aero::Window>();
    AERO_CHECK(aero::MountAxbTemplate(win.Get(), res.axbPackage));
    win->Measure(aero::Size{320.0f, 160.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 160.0f});

    auto* slider = win->FindName("VolSlider")->DynamicCast<aero::Slider>();
    auto* searchInput = win->FindName("SearchInput")->DynamicCast<aero::TextBox>();
    auto* plainSlider = win->FindName("PlainSlider")->DynamicCast<aero::Slider>();
    AERO_CHECK(slider != nullptr && searchInput != nullptr && plainSlider != nullptr);

    aero::UIElement* partTrack = slider->GetTemplateChild("PART_Track");
    aero::UIElement* partIndicator = slider->GetTemplateChild("PART_Indicator");
    aero::UIElement* partContentHost = searchInput->GetTemplateChild("PART_ContentHost");
    AERO_CHECK(partTrack != nullptr && partTrack->IsA<aero::Border>());
    AERO_CHECK(partIndicator != nullptr && partIndicator->IsA<aero::Border>());
    AERO_CHECK(partContentHost != nullptr && partContentHost->IsA<aero::ScrollViewer>());
    AERO_CHECK(slider->GetTemplateChild("PART_Missing") == nullptr);
    AERO_CHECK(plainSlider->GetTemplateChild("PART_Track") == nullptr);

    // At Value=25 on a 200px track, PART_Indicator width == 50px; updating Value=75 updates width to 150px
    AERO_CHECK(std::fabs(partIndicator->GetWidth() - 50.0f) <= 0.5f);
    slider->SetSliderValue(75.0f);
    win->Measure(aero::Size{320.0f, 160.0f});
    win->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 160.0f});
    AERO_CHECK(std::fabs(partIndicator->GetWidth() - 150.0f) <= 0.5f);

    // Verify PagedRenderTree skips hardcoded 3-quad Slider rendering for VolSlider, keeping it for PlainSlider
    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(win.Get());
    AERO_CHECK(slider->RenderNodeSpan() == 1u);
    AERO_CHECK(renderTree.GetNode(slider->RenderNodeId())->opacity == 0.0f);
    AERO_CHECK(plainSlider->RenderNodeSpan() == 3u);

    std::printf("[PASS] (d) TestPartContractLookupAndTemplatedSliderSync\n");
}

// (e) End-to-end compilation & mounting of official Samples/Buttons/Data/Resources.xaml + MainPage.xaml
void TestOfficialButtonsSampleResourcesAndMainPageEndToEnd() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const std::string resXaml = ReadFileText(
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Buttons/Data/Resources.xaml");
    const std::string pageXaml = ReadFileText(
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Buttons/Data/MainPage.xaml");
    AERO_CHECK(!resXaml.empty());
    AERO_CHECK(!pageXaml.empty());

    aero::AeroCompiler compiler;

    // 1. Compile Samples/Buttons/Data/Resources.xaml
    aero::AeroCompileInput resIn{};
    resIn.xamlPath = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Buttons/Data/Resources.xaml";
    resIn.xamlContent = resXaml;
    aero::AeroCompileResult resCompiled = compiler.Compile(resIn);
    AERO_CHECK(resCompiled.succeeded);

    const aero::AxbControlTemplate* btnTpl = resCompiled.axbPackage.FindControlTemplate("ButtonTemplate");
    AERO_CHECK(btnTpl != nullptr);
    AERO_CHECK(btnTpl->targetTypeName == "Button");
    AERO_CHECK(btnTpl->templatePackage != nullptr);
    AERO_CHECK(btnTpl->visualStateGroups.size() >= 2u); // CommonStates + FocusStates lifted from template root Viewbox!
    AERO_CHECK(aero::AxbTemplateRegistry::Instance().FindControlTemplate("ButtonTemplate") != nullptr);

    // 2. Compile Samples/Buttons/Data/MainPage.xaml & verify .axb4 serialization roundtrip of Resources.xaml
    const std::vector<std::uint8_t> resAxbBytes = resCompiled.axbPackage.Serialize();
    aero::AxbPackage deserResPkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(resAxbBytes, deserResPkg));
    AERO_CHECK(deserResPkg.FindControlTemplate("ButtonTemplate") != nullptr);

    aero::AeroCompileInput pageIn{};
    pageIn.xamlPath = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Buttons/Data/MainPage.xaml";
    pageIn.xamlContent = pageXaml;
    aero::AeroCompileResult pageCompiled = compiler.Compile(pageIn);
    AERO_CHECK(pageCompiled.succeeded);

    // 3. Mount MainPage onto a 1280x720 host surface
    auto pageRoot = aero::MakeIntrusive<aero::UserControl>();
    pageRoot->SetAsRootVisual(true);
    AERO_CHECK(aero::MountAxbTemplate(pageRoot.Get(), pageCompiled.axbPackage));

    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, nullptr);
    styleEngine.RegisterPackageStyles(deserResPkg);
    styleEngine.RegisterPackageStyles(pageCompiled.axbPackage);
    styleEngine.ImportRegistryStyles();
    (void)styleEngine.BindSubtreeStyles(pageRoot.Get());

    pageRoot->Measure(aero::Size{1280.0f, 720.0f});
    pageRoot->Arrange(aero::Rect{0.0f, 0.0f, 1280.0f, 720.0f});
    (void)styleEngine.EvaluateAndApply();
    (void)animEngine.Tick(1.05f); // Advance past OnLoaded Intro storyboard (ScaleY 0 -> 1, TranslateX -200 -> 0)

    // 4. Verify StartButton, SettingsButton & ExitButton all mounted ButtonTemplate and synced {TemplateBinding Content}
    const char* expectedButtons[3][2] = {
        {"StartButton", "START"},
        {"SettingsButton", "SETTINGS"},
        {"ExitButton", "EXIT"},
    };
    for (const auto& pair : expectedButtons) {
        auto* btnElem = pageRoot->FindName(pair[0]);
        AERO_CHECK(btnElem != nullptr && btnElem->IsA<aero::Button>());
        AERO_CHECK(btnElem->HasControlTemplate());

        auto* contentHost = btnElem->GetTemplateChild("ContentHost");
        auto* leftRect = btnElem->GetTemplateChild("LeftRect");
        auto* outerPath = btnElem->GetTemplateChild("BaseOuter");
        AERO_CHECK(contentHost != nullptr && contentHost->IsA<aero::TextBlock>());
        AERO_CHECK(leftRect != nullptr);
        AERO_CHECK(outerPath != nullptr && outerPath->IsA<aero::Path>());

        auto* tb = contentHost->DynamicCast<aero::TextBlock>();
        AERO_CHECK(tb->GetText() == pair[1]);
        AERO_CHECK(std::fabs(tb->GetFontSize() - 48.0f) <= 0.1f); // Inherited from <Style TargetType="{x:Type Button}">
    }

    // 5. Build PagedRenderTree & verify interactive hover transitions on StartButton's template VSM!
    aero::PagedRenderTree renderTree;
    const std::size_t totalRenderNodes = renderTree.BuildFromVisualTree(pageRoot.Get());
    AERO_CHECK(totalRenderNodes > 20u);

    auto* startBtn = pageRoot->FindName("StartButton")->DynamicCast<aero::Button>();
    AERO_CHECK(startBtn->RenderNodeSpan() == 1u); // Hardcoded Button box skipped; rendered via ButtonTemplate!
    auto* startContentHost = startBtn->GetTemplateChild("ContentHost")->DynamicCast<aero::TextBlock>();
    const aero::Color normalFg = startContentHost->GetForeground();

    aero::InputEventRouter router(pageRoot.Get());
    router.SetAnimationEngine(&animEngine);
    const aero::Rect startWorldBounds = startBtn->LocalToScreenBounds(startBtn->FinalRect());
    const aero::Point startCenter{
        startWorldBounds.x + startWorldBounds.width * 0.5f,
        startWorldBounds.y + startWorldBounds.height * 0.5f};

    (void)router.QueuePointerMove(startCenter);
    (void)router.QueuePointerButton(startCenter, aero::PointerButton::Left, true);
    (void)router.FlushQueuedEvents();
    (void)styleEngine.EvaluateAndApply();
    (void)animEngine.Tick(0.02f);
    AERO_CHECK(startBtn->IsHovered() && startBtn->IsFocused() && startBtn->IsPressed());
    // In ButtonTemplate Focused & Pressed VSM states, ContentHost Foreground animates from #FF488EB5 to White (#FFFFFFFF)
    // and LeftRect ScaleX animates above 1.0f!
    const aero::Color focusedFg = startContentHost->GetForeground();
    AERO_CHECK(focusedFg.r > normalFg.r + 0.25f && focusedFg.g > normalFg.g + 0.25f);
    AERO_CHECK(startBtn->GetTemplateChild("LeftRect")->GetScaleX() > 1.5f);

    std::printf(
        "[PASS] (e) TestOfficialButtonsSampleResourcesAndMainPageEndToEnd (renderNodes=%zu)\n",
        totalRenderNodes);
}


// Bonus verification: Login/Data/Resources.xaml, Gallery/Data/Samples/Button.xaml & CheckBox.xaml, and AOT C++ codegen
void TestOfficialLoginAndGalleryTemplatesAndAotCodegen() {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::AeroCompiler compiler;

    // 1. Login/Data/Resources.xaml (TextBox & PasswordBox ControlTemplates with RelativeSource TemplatedParent & MultiDataTrigger)
    const std::string loginResXaml = ReadFileText(
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Login/Data/Resources.xaml");
    AERO_CHECK(!loginResXaml.empty());
    aero::AeroCompileInput loginIn{};
    loginIn.xamlPath = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Login/Data/Resources.xaml";
    loginIn.xamlContent = loginResXaml;
    loginIn.emitAotCpp = true;
    aero::AeroCompileResult loginRes = compiler.Compile(loginIn);
    AERO_CHECK(loginRes.succeeded);
    AERO_CHECK(!loginRes.axbPackage.controlTemplates.empty());
    AERO_CHECK(loginRes.aotCpp.source.find("RegisterControlTemplate") != std::string::npos);

    // 2. Gallery/Data/Samples/Button.xaml (inline <Setter Property="Template"><Setter.Value><ControlTemplate>...)
    const std::string galleryBtnXaml = ReadFileText(
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/Button.xaml");
    AERO_CHECK(!galleryBtnXaml.empty());
    aero::AeroCompileInput galBtnIn{};
    galBtnIn.xamlPath = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/Button.xaml";
    galBtnIn.xamlContent = galleryBtnXaml;
    aero::AeroCompileResult galBtnRes = compiler.Compile(galBtnIn);
    AERO_CHECK(galBtnRes.succeeded);
    AERO_CHECK(!galBtnRes.axbPackage.controlTemplates.empty());

    auto galBtnRoot = aero::MakeIntrusive<aero::UserControl>();
    AERO_CHECK(aero::MountAxbTemplate(galBtnRoot.Get(), galBtnRes.axbPackage));
    auto* customStyledBtn = galBtnRoot->FindName("CustomButton");
    if (customStyledBtn) {
        AERO_CHECK(customStyledBtn->HasControlTemplate());
    }

    // 3. Gallery/Data/Samples/CheckBox.xaml
    const std::string galleryChkXaml = ReadFileText(
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/CheckBox.xaml");
    AERO_CHECK(!galleryChkXaml.empty());
    aero::AeroCompileInput galChkIn{};
    galChkIn.xamlPath = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/CheckBox.xaml";
    galChkIn.xamlContent = galleryChkXaml;
    aero::AeroCompileResult galChkRes = compiler.Compile(galChkIn);
    AERO_CHECK(galChkRes.succeeded);

    std::printf(
        "[PASS] (f) TestOfficialLoginAndGalleryTemplatesAndAotCodegen (loginTemplates=%zu, galleryBtnTemplates=%zu)\n",
        loginRes.axbPackage.controlTemplates.size(),
        galBtnRes.axbPackage.controlTemplates.size());
}

} // namespace

int main() {
    std::printf("=== AeroGUI 2.0 P1-1 ControlTemplate, TemplateBinding & PART_* Contract Tests ===\n");
    TestStyleInlineControlTemplateAxb4RoundTripAndAutoMount();
    TestTemplateBindingAndContentPresenterZeroAllocSync();
    TestControlTemplateTriggersHoverPressAndCheckViaInputRouter();
    TestPartContractLookupAndTemplatedSliderSync();
    TestOfficialButtonsSampleResourcesAndMainPageEndToEnd();
    TestOfficialLoginAndGalleryTemplatesAndAotCodegen();
    std::printf("=== ALL P1-1 ACCEPTANCE TESTS PASSED 100%% ===\n");
    return 0;
}
