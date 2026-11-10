#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
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
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace {

std::atomic<bool> gTrackAllocations{false};
std::atomic<std::size_t> gAllocationCount{0};

struct AllocationTrackingScope {
    AllocationTrackingScope() noexcept {
        gAllocationCount.store(0, std::memory_order_seq_cst);
        gTrackAllocations.store(true, std::memory_order_seq_cst);
    }
    ~AllocationTrackingScope() noexcept {
        gTrackAllocations.store(false, std::memory_order_seq_cst);
    }
    [[nodiscard]] std::size_t count() const noexcept {
        return gAllocationCount.load(std::memory_order_seq_cst);
    }
};

} // namespace

#if defined(__GNUC__) || defined(__clang__)
#define AERO_TEST_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define AERO_TEST_NOINLINE __declspec(noinline)
#else
#define AERO_TEST_NOINLINE
#endif

AERO_TEST_NOINLINE void* operator new(std::size_t size) {
    if (gTrackAllocations.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

AERO_TEST_NOINLINE void* operator new[](std::size_t size) {
    if (gTrackAllocations.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

AERO_TEST_NOINLINE void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

#define AERO_CHECK(cond, msg)                                                      \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::tests {

[[nodiscard]] bool ApproxEq(float a, float b, float eps = 1e-3f) noexcept {
    return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool ApproxRect(const Rect& a, const Rect& b, float eps = 1e-3f) noexcept {
    return ApproxEq(a.x, b.x, eps) &&
           ApproxEq(a.y, b.y, eps) &&
           ApproxEq(a.width, b.width, eps) &&
           ApproxEq(a.height, b.height, eps);
}

// ============================================================================
// Test 1: HorizontalAlignment / VerticalAlignment (0-Alloc Inline) & MaxWidth / MaxHeight
// ============================================================================
void TestAlignmentAndMaxDimensionsZeroAlloc() {
    static_assert(sizeof(UnifiedValue) == 16, "UnifiedValue must remain 16B");
    static_assert(sizeof(Float4Value) == 16, "Float4Value must remain 16B");
    static_assert(sizeof(CompactPropertyEntry) == 32, "CompactPropertyEntry must remain 32B");
    static_assert(sizeof(FastInlineStorage) == 56, "FastInlineStorage must remain 56B");
    static_assert(sizeof(AeroVertex) == 24, "AeroVertex must remain 24B");
    static_assert(sizeof(CommandPage) == 4096, "CommandPage must remain 4096B");

    auto card = MakeIntrusive<Border>();
    AERO_CHECK(card->GetHorizontalAlignment() == HorizontalAlignment::Stretch &&
                   card->GetVerticalAlignment() == VerticalAlignment::Stretch,
               "Default HorizontalAlignment and VerticalAlignment are Stretch");
    AERO_CHECK(std::isinf(card->GetMaxWidth()) && std::isinf(card->GetMaxHeight()),
               "Default MaxWidth and MaxHeight are +infinity");

    // Verify 0 heap allocations when mutating alignment via both C++ methods and SetValue/GetValue
    {
        AllocationTrackingScope noAlloc;
        card->SetHorizontalAlignment(HorizontalAlignment::Center);
        card->SetVerticalAlignment(VerticalAlignment::Bottom);
        AERO_CHECK(card->GetHorizontalAlignment() == HorizontalAlignment::Center &&
                       card->GetVerticalAlignment() == VerticalAlignment::Bottom,
                   "Inline alignment flags stored Center + Bottom");
        card->SetValue(PropId_HorizontalAlignment, value::FromInt(3)); // Right
        card->SetValue(PropId_VerticalAlignment, value::FromInt(1));   // Top
        AERO_CHECK(std::get<std::int64_t>(card->GetValue(PropId_HorizontalAlignment)) == 3 &&
                       std::get<std::int64_t>(card->GetValue(PropId_VerticalAlignment)) == 1,
                   "GetValue/SetValue round-trip on inline alignment properties");
        AERO_CHECK(noAlloc.count() == 0u,
                   "HorizontalAlignment and VerticalAlignment perform 0 heap allocations");
    }
    AERO_CHECK(card->SparsePropertyCount() == 0u,
               "Alignment flags are packed in 56B FastInlineStorage without growing sparseProps_");

    // Compile XAML exercising Left/Center/Right/Stretch, Top/Center/Bottom/Stretch, Margin, MaxWidth & MaxHeight
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Grid Width="400" Height="240">
    <Border x:Name="TopLeftBox" Width="60" Height="30" Margin="10,8,10,8"
            HorizontalAlignment="Left" VerticalAlignment="Top" />
    <Border x:Name="CenterBox" Width="80" Height="40" Margin="10,0,10,0"
            HorizontalAlignment="Center" VerticalAlignment="Center" />
    <Border x:Name="BottomRightBox" Width="50" Height="24" Margin="0,0,12,14"
            HorizontalAlignment="Right" VerticalAlignment="Bottom" />
    <Border x:Name="MaxClampedStretch" MaxWidth="180" MaxHeight="90"
            HorizontalAlignment="Stretch" VerticalAlignment="Stretch" />
    <TextBlock x:Name="AutoSizedRight" Text="ABCD" FontSize="20"
               HorizontalAlignment="Right" VerticalAlignment="Bottom" Margin="0,0,20,10" />
</Grid>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled alignment & MaxWidth/MaxHeight XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted alignment XAML");

    DepthBucketLayoutQueue layoutQueue;
    layoutQueue.EnqueueSubtree(root.Get());
    (void)layoutQueue.FlushLayout(Size{400.0f, 240.0f});

    auto* tl = root->FindName("TopLeftBox");
    auto* cc = root->FindName("CenterBox");
    auto* br = root->FindName("BottomRightBox");
    auto* mc = root->FindName("MaxClampedStretch");
    auto* ar = root->FindName("AutoSizedRight");

    AERO_CHECK(tl && ApproxRect(tl->FinalRect(), Rect{10.0f, 8.0f, 60.0f, 30.0f}),
               "Left + Top alignment with Margin=(10,8,10,8) placed at (10, 8, 60, 30)");
    AERO_CHECK(cc && ApproxRect(cc->FinalRect(), Rect{160.0f, 100.0f, 80.0f, 40.0f}),
               "Center + Center alignment placed at (160, 100, 80, 40)");
    AERO_CHECK(br && ApproxRect(br->FinalRect(), Rect{338.0f, 202.0f, 50.0f, 24.0f}),
               "Right + Bottom alignment with Margin=(0,0,12,14) placed at (338, 202, 50, 24)");
    AERO_CHECK(mc && ApproxRect(mc->FinalRect(), Rect{110.0f, 75.0f, 180.0f, 90.0f}),
               "Stretch element with MaxWidth=180, MaxHeight=90 clamped to 180x90 and centered at (110, 75)");
    AERO_CHECK(ar && ApproxRect(ar->FinalRect(), Rect{332.0f, 205.0f, 48.0f, 25.0f}),
               "Auto-sized TextBlock with Right+Bottom aligned to natural desired size (48x25) instead of stretching");

    // Verify incremental re-arrange on CenterBox with Margin preserves parent 400x240 slot (no double-margin shift!)
    cc->SetHorizontalAlignment(HorizontalAlignment::Right);
    layoutQueue.EnqueueDirtySubtree(root.Get());
    (void)layoutQueue.FlushLayout(Size{400.0f, 240.0f});
    AERO_CHECK(ApproxRect(cc->FinalRect(), Rect{310.0f, 100.0f, 80.0f, 40.0f}),
               "Incremental alignment change to Right placed CenterBox at x=310 without double-applying Margin");

    std::cout << "[PASS] TestAlignmentAndMaxDimensionsZeroAlloc\n";
}

// ============================================================================
// Test 2: DockPanel (Left/Top/Right/Bottom, LastChildFill, Collapsed Dock Collapse)
// ============================================================================
void TestDockPanelLayoutAndLastChildFill() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<DockPanel x:Name="RootDock" Width="400" Height="300" LastChildFill="True">
    <Border x:Name="TopBar" DockPanel.Dock="Top" Height="40" Background="#224466" />
    <Border x:Name="BottomStatus" DockPanel.Dock="Bottom" Height="30" Background="#1A2636" />
    <Border x:Name="LeftNav" DockPanel.Dock="Left" Width="80" Background="#283848" />
    <Border x:Name="RightInspector" DockPanel.Dock="Right" Width="60" Background="#283848" />
    <Border x:Name="WorkspaceCenter" Background="#101822" />
</DockPanel>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled DockPanel XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    auto* dock = root ? root->DynamicCast<DockPanel>() : nullptr;
    AERO_CHECK(dock != nullptr && MountAxbTemplate(dock, res.axbPackage),
               "Created and mounted dedicated DockPanel");

    dock->Measure(Size{400.0f, 300.0f});
    dock->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});

    auto* topBar = dock->FindName("TopBar");
    auto* bottomBar = dock->FindName("BottomStatus");
    auto* leftNav = dock->FindName("LeftNav");
    auto* rightInsp = dock->FindName("RightInspector");
    auto* center = dock->FindName("WorkspaceCenter");

    AERO_CHECK(ApproxRect(topBar->FinalRect(), Rect{0.0f, 0.0f, 400.0f, 40.0f}), "TopBar docked at (0, 0, 400, 40)");
    AERO_CHECK(ApproxRect(bottomBar->FinalRect(), Rect{0.0f, 270.0f, 400.0f, 30.0f}), "BottomStatus docked at (0, 270, 400, 30)");
    AERO_CHECK(ApproxRect(leftNav->FinalRect(), Rect{0.0f, 40.0f, 80.0f, 230.0f}), "LeftNav docked at (0, 40, 80, 230)");
    AERO_CHECK(ApproxRect(rightInsp->FinalRect(), Rect{340.0f, 40.0f, 60.0f, 230.0f}), "RightInspector docked at (340, 40, 60, 230)");
    AERO_CHECK(ApproxRect(center->FinalRect(), Rect{80.0f, 40.0f, 260.0f, 230.0f}),
               "LastChildFill stretched WorkspaceCenter to remaining (80, 40, 260, 230)");

    leftNav->SetVisibility(Visibility::Collapsed);
    dock->Measure(Size{400.0f, 300.0f});
    dock->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    AERO_CHECK(ApproxRect(center->FinalRect(), Rect{0.0f, 40.0f, 340.0f, 230.0f}),
               "Collapsing LeftNav reclaimed 80px into WorkspaceCenter (0, 40, 340, 230)");

    center->SetWidth(100.0f);
    dock->SetLastChildFill(false);
    dock->Measure(Size{400.0f, 300.0f});
    dock->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    AERO_CHECK(ApproxRect(center->FinalRect(), Rect{0.0f, 40.0f, 100.0f, 230.0f}),
               "LastChildFill=false docked last child at its 100px width instead of filling 340px");

    std::cout << "[PASS] TestDockPanelLayoutAndLastChildFill\n";
}

// ============================================================================
// Test 3: WrapPanel (Horizontal & Vertical Line Breaking, Spacing, ItemWidth/ItemHeight)
// ============================================================================
void TestWrapPanelFlowAndUniformItems() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<WrapPanel x:Name="TagWrap" Width="260" Spacing="10">
    <Border x:Name="Tag0" Width="80" Height="28" />
    <Border x:Name="Tag1" Width="90" Height="32" />
    <Border x:Name="Tag2" Width="70" Height="24" />
    <Border x:Name="Tag3" Width="120" Height="30" />
    <Border x:Name="Tag4" Width="150" Height="26" />
</WrapPanel>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled WrapPanel XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    auto* wrap = root ? root->DynamicCast<WrapPanel>() : nullptr;
    AERO_CHECK(wrap != nullptr && MountAxbTemplate(wrap, res.axbPackage),
               "Created and mounted dedicated WrapPanel");

    wrap->Measure(Size{260.0f, 500.0f});
    AERO_CHECK(ApproxEq(wrap->DesiredSize().width, 260.0f) &&
                   ApproxEq(wrap->DesiredSize().height, 108.0f),
               "WrapPanel MeasureCore wrapped 5 items into 3 rows (260 x 108)");

    wrap->Arrange(Rect{0.0f, 0.0f, 260.0f, 108.0f});
    AERO_CHECK(ApproxEq(wrap->FindName("Tag2")->FinalRect().x, 190.0f) &&
                   ApproxEq(wrap->FindName("Tag2")->FinalRect().y, 4.0f),
               "Tag2 arranged at end of row 0 (x=190, vertically centered in 32px row -> y=4)");
    AERO_CHECK(ApproxEq(wrap->FindName("Tag3")->FinalRect().x, 0.0f) &&
                   ApproxEq(wrap->FindName("Tag3")->FinalRect().y, 42.0f),
               "Tag3 wrapped to start of row 1 (x=0, y=32+10=42)");
    AERO_CHECK(ApproxEq(wrap->FindName("Tag4")->FinalRect().x, 0.0f) &&
                   ApproxEq(wrap->FindName("Tag4")->FinalRect().y, 82.0f),
               "Tag4 wrapped to start of row 2 (x=0, y=42+30+10=82)");

    // Clear explicit item sizes to Auto and apply uniform ItemWidth=60, ItemHeight=60 in 200px width -> 3x2 grid
    for (UIElement* c = wrap->FirstChild(); c != nullptr; c = c->NextSibling()) {
        (void)c->ClearValue(PropId_Width);
        (void)c->ClearValue(PropId_Height);
    }
    wrap->SetSpacing(0.0f);
    wrap->SetWidth(200.0f);
    wrap->SetItemWidth(60.0f);
    wrap->SetItemHeight(60.0f);
    wrap->Measure(Size{200.0f, 500.0f});
    wrap->Arrange(Rect{0.0f, 0.0f, 200.0f, 120.0f});
    AERO_CHECK(ApproxEq(wrap->DesiredSize().height, 120.0f) &&
                   ApproxRect(wrap->FindName("Tag3")->FinalRect(), Rect{0.0f, 60.0f, 60.0f, 60.0f}) &&
                   ApproxRect(wrap->FindName("Tag4")->FinalRect(), Rect{60.0f, 60.0f, 60.0f, 60.0f}),
               "WrapPanel ItemWidth=60, ItemHeight=60 arranged uniform 3x2 grid");

    // Also test Orientation="Vertical" column-wrapping in 140px height (2 items per column -> 3 columns, width = 180)
    wrap->SetOrientation(Orientation::Vertical);
    wrap->ClearValue(PropId_Width);
    wrap->SetHeight(140.0f);
    wrap->Measure(Size{500.0f, 140.0f});
    wrap->Arrange(Rect{0.0f, 0.0f, 180.0f, 140.0f});
    AERO_CHECK(ApproxEq(wrap->DesiredSize().width, 180.0f) &&
                   ApproxRect(wrap->FindName("Tag2")->FinalRect(), Rect{60.0f, 0.0f, 60.0f, 60.0f}) &&
                   ApproxRect(wrap->FindName("Tag4")->FinalRect(), Rect{120.0f, 0.0f, 60.0f, 60.0f}),
               "WrapPanel Orientation=Vertical wrapped items into 3 columns");

    std::cout << "[PASS] TestWrapPanelFlowAndUniformItems\n";
}

// ============================================================================
// Test 4: Viewbox (Uniform / Fill / DownOnly, Child Transform Scaling, HitTest & Golden Image)
// ============================================================================
void TestViewboxStretchModesHitTestAndGoldenImage() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Canvas Width="400" Height="300" Background="#0A0E14">
    <Viewbox x:Name="HudViewbox" Width="400" Height="300" Stretch="Uniform">
        <Canvas x:Name="DesignCanvas" Width="200" Height="100" Background="#182230">
            <Button x:Name="ScaledLaunchBtn" Canvas.Left="60" Canvas.Top="20"
                    Width="80" Height="30" Background="#EE3344" Content="GO" />
        </Canvas>
    </Viewbox>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled Viewbox XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted Viewbox XAML");
    root->Measure(Size{400.0f, 300.0f});
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});

    auto* vb = root->FindName("HudViewbox")->DynamicCast<Viewbox>();
    auto* launchBtn = root->FindName("ScaledLaunchBtn")->DynamicCast<Button>();
    AERO_CHECK(vb != nullptr && launchBtn != nullptr, "Resolved Viewbox and ScaledLaunchBtn");

    AERO_CHECK(ApproxEq(vb->ChildScaleX(), 2.0f) &&
                   ApproxEq(vb->ChildScaleY(), 2.0f) &&
                   ApproxEq(vb->ChildOriginX(), 0.0f) &&
                   ApproxEq(vb->ChildOriginY(), 50.0f),
               "Viewbox computed 2.0x uniform scale with 50px vertical letterbox offset");

    AERO_CHECK(root->HitTest(Point{200.0f, 120.0f}) == launchBtn,
               "HitTest at scaled screen point (200, 120) inverted Viewbox 2x scale and hit ScaledLaunchBtn");
    AERO_CHECK(root->HitTest(Point{200.0f, 20.0f}) == vb,
               "HitTest in top letterbox band (200, 20) missed DesignCanvas and fell back to Viewbox");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 400u, 300u, rgba);

    const std::size_t btnCenterIdx = (120u * 400u + 140u) * 4u;
    const std::size_t letterboxIdx = (20u * 400u + 200u) * 4u;
    AERO_CHECK(rgba[btnCenterIdx + 0u] > 200u && rgba[btnCenterIdx + 1u] < 80u,
               "Golden Image rendered 2x-scaled red ScaledLaunchBtn inside Viewbox");
    AERO_CHECK(rgba[letterboxIdx + 0u] < 25u && rgba[letterboxIdx + 2u] < 35u,
               "Unbacked Viewbox stays transparent in letterbox bands without painting opaque backdrop");

    vb->SetStretchDirection(StretchDirection::DownOnly);
    root->Measure(Size{400.0f, 300.0f});
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    AERO_CHECK(ApproxEq(vb->ChildScaleX(), 1.0f) &&
                   ApproxEq(vb->ChildOriginX(), 100.0f) &&
                   ApproxEq(vb->ChildOriginY(), 100.0f),
               "StretchDirection::DownOnly kept 1.0x scale and centered 200x100 child at (100, 100)");

    std::cout << "[PASS] TestViewboxStretchModesHitTestAndGoldenImage\n";
}

// ============================================================================
// Test 5: ScrollViewer (Extent Measurement, Clamped Offsets, Scissor Clip & Live Scroll Sync)
// ============================================================================
void TestScrollViewerOffsetsClipAndLiveSync() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<ScrollViewer x:Name="QuestScroll" Width="200" Height="100"
              VerticalScrollBarVisibility="Auto" Background="#121822">
    <StackPanel Spacing="10">
        <Border x:Name="Row0" Height="40" Background="#2255AA" />
        <Border x:Name="Row1" Height="40" Background="#2255AA" />
        <Border x:Name="Row2" Height="40" Background="#2255AA" />
        <Border x:Name="Row3" Height="40" Background="#2255AA" />
        <Border x:Name="Row4" Height="40" Background="#22DD66" />
        <Border x:Name="Row5" Height="40" Background="#2255AA" />
    </StackPanel>
</ScrollViewer>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled ScrollViewer XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    auto* sv = root ? root->DynamicCast<ScrollViewer>() : nullptr;
    AERO_CHECK(sv != nullptr && MountAxbTemplate(sv, res.axbPackage),
               "Created and mounted dedicated ScrollViewer");

    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    EmbeddedGameHost host(sv, nullptr, &layoutQueue, &renderTree);
    host.SetViewportSize(Size{200.0f, 100.0f});
    host.TickUi(0.016f, UiPhase_All);

    AERO_CHECK(ApproxEq(sv->ExtentHeight(), 290.0f) &&
                   ApproxEq(sv->ViewportHeight(), 100.0f) &&
                   ApproxEq(sv->ScrollableHeight(), 190.0f),
               "ScrollViewer measured unconstrained 290px ExtentHeight inside 100px ViewportHeight");

    auto* row0 = sv->FindName("Row0");
    auto* row4 = sv->FindName("Row4");
    AERO_CHECK(sv->HitTest(Point{100.0f, 20.0f}) == row0,
               "At VerticalOffset=0, HitTest(100, 20) hits Row0");
    AERO_CHECK(sv->HitTest(Point{100.0f, 150.0f}) == nullptr,
               "HitTest(100, 150) below 100px ScrollViewer viewport is clipped");

    const std::size_t initialNodeCount = renderTree.NodeCount();

    sv->ScrollToVerticalOffset(180.0f);
    host.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(renderTree.NodeCount() == initialNodeCount,
               "Scrolling updated PagedRenderTree vertices in-place without rebuilding nodes");
    AERO_CHECK(sv->HitTest(Point{100.0f, 35.0f}) == row4,
               "At VerticalOffset=180, HitTest(100, 35) hits green Row4");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 200u, 140u, rgba, Color{0.02f, 0.02f, 0.03f, 1.0f});
    const std::size_t row4VisIdx = (35u * 200u + 100u) * 4u;
    const std::size_t belowClipIdx = (115u * 200u + 100u) * 4u;
    AERO_CHECK(rgba[row4VisIdx + 1u] > 190u && rgba[row4VisIdx + 0u] < 60u,
               "Golden Image at (100, 35) rendered scrolled-in green Row4");
    AERO_CHECK(rgba[belowClipIdx + 1u] < 20u && rgba[belowClipIdx + 2u] < 20u,
               "Golden Image at (100, 115) strictly clipped Row5 below 100px ScrollViewer scissor");

    sv->ScrollToVerticalOffset(5000.0f);
    AERO_CHECK(ApproxEq(sv->GetVerticalOffset(), 190.0f),
               "ScrollToVerticalOffset clamped overscroll to ScrollableHeight=190.0f");

    std::cout << "[PASS] TestScrollViewerOffsetsClipAndLiveSync\n";
}

// ============================================================================
// Test 6: Full 2D Affine Transforms (Rotate/Scale/Translate/Skew/Matrix/TransformGroup + Origin + AOT)
// ============================================================================
void TestFull2DAffineTransformsXamlAotHitTestAndGoldenRaster() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.emitAotCpp = true;
    input.xamlContent = R"(
<Canvas Width="320" Height="240" Background="#0C1018">
    <Border x:Name="RotatedBar" Canvas.Left="100" Canvas.Top="100" Width="100" Height="40"
            Background="#FF4433" RenderTransformOrigin="0.5,0.5">
        <Border.RenderTransform>
            <RotateTransform Angle="90" />
        </Border.RenderTransform>
    </Border>

    <Border x:Name="GroupCard" Canvas.Left="10" Canvas.Top="10" Width="30" Height="20"
            Background="#22CCFF">
        <Border.RenderTransform>
            <TransformGroup>
                <ScaleTransform ScaleX="2" ScaleY="1" />
                <SkewTransform AngleX="45" AngleY="0" />
                <TranslateTransform X="30" Y="10" />
            </TransformGroup>
        </Border.RenderTransform>
    </Border>

    <Border x:Name="MatrixBadge" Canvas.Left="220" Canvas.Top="20" Width="40" Height="30"
            Background="#44EE88">
        <Border.RenderTransform>
            <MatrixTransform Matrix="1 0 0 1 20 15" />
        </Border.RenderTransform>
    </Border>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled full 2D affine RenderTransform XAML");
    AERO_CHECK(res.aotCpp.source.find("SetValue(47u") != std::string::npos &&
                   res.aotCpp.source.find("SetValue(49u") != std::string::npos,
               "AOT C++17 view builder emitted PropId_RenderTransform and PropId_RenderTransformOrigin");

    AxbPackage decoded{};
    AERO_CHECK(AxbPackage::Deserialize(res.axbBytes, decoded), "Deserialized .axb4 with 2D affine transforms");

    auto root = CreateElementByTypeName(decoded.rootTypeName);
    AERO_CHECK(MountAxbTemplate(root.Get(), decoded), "Mounted 2D affine transform XAML");
    root->Measure(Size{320.0f, 240.0f});
    root->Arrange(Rect{0.0f, 0.0f, 320.0f, 240.0f});

    auto* rotBar = root->FindName("RotatedBar");
    auto* grpCard = root->FindName("GroupCard");
    auto* matBadge = root->FindName("MatrixBadge");
    AERO_CHECK(rotBar && grpCard && matBadge, "Found all 3 transformed elements");

    AERO_CHECK(root->HitTest(Point{150.0f, 80.0f}) == rotBar,
               "HitTest(150, 80) hit 90-degree rotated top arm of RotatedBar");
    AERO_CHECK(root->HitTest(Point{150.0f, 160.0f}) == rotBar,
               "HitTest(150, 160) hit 90-degree rotated bottom arm of RotatedBar");
    AERO_CHECK(root->HitTest(Point{110.0f, 120.0f}) == root.Get(),
               "HitTest(110, 120) missed pre-rotation horizontal wing of RotatedBar");

    AERO_CHECK(root->HitTest(Point{80.0f, 30.0f}) == grpCard,
               "HitTest(80, 30) inverted Scale->Skew->Translate TransformGroup accurately");
    AERO_CHECK(root->HitTest(Point{15.0f, 15.0f}) == root.Get(),
               "HitTest(15, 15) missed unshifted GroupCard origin");

    AERO_CHECK(root->HitTest(Point{260.0f, 50.0f}) == matBadge,
               "HitTest(260, 50) hit MatrixTransform-shifted MatrixBadge");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());

    const RenderNodeDescriptor* rotNode = tree.GetNode(rotBar->RenderNodeId());
    AERO_CHECK(rotNode != nullptr, "Resolved RotatedBar render node");
    const CommandPage& page = tree.GetPage(rotNode->pageIndex);
    const AeroVertex& v0 = page.vertices[rotNode->vertexOffsetInPage + 0u];
    const AeroVertex& v1 = page.vertices[rotNode->vertexOffsetInPage + 1u];
    const AeroVertex& v2 = page.vertices[rotNode->vertexOffsetInPage + 2u];
    AERO_CHECK(ApproxEq(v0.x, 170.0f, 0.05f) && ApproxEq(v0.y, 70.0f, 0.05f) &&
                   ApproxEq(v1.x, 170.0f, 0.05f) && ApproxEq(v1.y, 170.0f, 0.05f) &&
                   ApproxEq(v2.x, 130.0f, 0.05f) && ApproxEq(v2.y, 170.0f, 0.05f),
               "PagedRenderTree emitted exact 90-degree rotated quad vertices around RenderTransformOrigin(0.5,0.5)");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 320u, 240u, rgba);
    const std::size_t rotTopArmIdx = (80u * 320u + 150u) * 4u;
    const std::size_t unrotMissIdx = (120u * 320u + 110u) * 4u;
    const std::size_t skewHitIdx = (30u * 320u + 80u) * 4u;
    AERO_CHECK(rgba[rotTopArmIdx + 0u] > 220u && rgba[rotTopArmIdx + 2u] < 80u,
               "Golden Image rasterized bright red pixels in rotated top arm (150, 80)");
    AERO_CHECK(rgba[unrotMissIdx + 0u] < 30u && rgba[unrotMissIdx + 2u] < 40u,
               "Golden Image left pre-rotation wing (110, 120) unlit");
    AERO_CHECK(rgba[skewHitIdx + 2u] > 200u && rgba[skewHitIdx + 1u] > 150u,
               "Golden Image rasterized cyan skewed parallelogram at (80, 30)");

    (void)tree.SyncVisualTree(root.Get());
    rotBar->SetRenderTransform(Matrix2D::Rotation(45.0f));
    {
        AllocationTrackingScope noAlloc;
        const std::size_t synced = tree.SyncVisualTree(root.Get());
        AERO_CHECK(synced == 1u && noAlloc.count() == 0u,
                   "Live 45-degree rotation update synced in O(1) with 0 heap allocations");
    }

    std::cout << "[PASS] TestFull2DAffineTransformsXamlAotHitTestAndGoldenRaster\n";
}

// ============================================================================
// Test 7: Nested Container Rotation + UTF-8/CJK Rotated Glyph Emission
// ============================================================================
void TestNestedContainerRotationAndRotatedUtf8Glyphs() {
    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(300.0f);
    root->SetHeight(300.0f);

    auto dial = MakeIntrusive<Canvas>();
    Canvas::SetLeft(dial.Get(), 100.0f);
    Canvas::SetTop(dial.Get(), 100.0f);
    dial->SetWidth(100.0f);
    dial->SetHeight(100.0f);
    dial->SetRenderTransformOrigin(Point{0.5f, 0.5f});
    dial->SetRenderTransform(Matrix2D::Rotation(90.0f));

    auto label = MakeIntrusive<TextBlock>();
    Canvas::SetLeft(label.Get(), 50.0f);
    Canvas::SetTop(label.Get(), 10.0f);
    label->SetFontSize(16.0f);
    label->SetText("北N");
    dial->AddChild(label.Get());

    auto childBtn = MakeIntrusive<Button>();
    Canvas::SetLeft(childBtn.Get(), 35.0f);
    Canvas::SetTop(childBtn.Get(), 5.0f);
    childBtn->SetWidth(30.0f);
    childBtn->SetHeight(20.0f);
    dial->AddChild(childBtn.Get());

    root->AddChild(dial.Get());
    root->Measure(Size{300.0f, 300.0f});
    root->Arrange(Rect{0.0f, 0.0f, 300.0f, 300.0f});

    AERO_CHECK(root->HitTest(Point{185.0f, 150.0f}) == childBtn.Get(),
               "Nested child Button inside 90-degree rotated container hit accurately at (185, 150)");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    const RenderNodeDescriptor* lblNode = tree.GetNode(label->RenderNodeId());
    AERO_CHECK(lblNode != nullptr && lblNode->primType == AeroPrimType::SdfTextGlyph,
               "Rotated UTF-8/CJK TextBlock emitted SdfTextGlyph run");
    const AeroVertex& gv0 = tree.GetPage(lblNode->pageIndex).vertices[lblNode->vertexOffsetInPage];
    AERO_CHECK(ApproxEq(gv0.x, 190.0f, 0.05f) && ApproxEq(gv0.y, 150.0f, 0.05f),
               "Parent 90-degree rotation around (150,150) mapped child glyph origin (150,110) -> (190,150)");

    std::cout << "[PASS] TestNestedContainerRotationAndRotatedUtf8Glyphs\n";
}

// ============================================================================
// Test 8: Phase 5A Review Hardening Regressions
//   - Viewbox 2x SdfRoundedRect CornerRadius & <Path> StrokeThickness CPU/GPU scale symmetry
//   - ScrollViewer pre-arrange overscroll clamping, zero-dead-scroll ScrollBy, Padding viewport clip & Arrange-only dirty
//   - RenderTransform 6-float literal translation preservation & override cleanup
//   - Inline alignment binding keeping SparsePropertyCount == 0 + string enum coercion
//   - LayoutTransform real layout-box AABB sizing + StaticResource/Binding transform diagnostics
//   - ECS Track B rotated RenderTransformOrigin width pivot update
// ============================================================================
void TestPhase5AReviewHardeningRegressions() {
    // 1. Viewbox 2x SdfRoundedRect CornerRadius=16 (-> 32px on screen) & <Path StrokeThickness=3> (-> 6px on screen)
    {
        auto root = MakeIntrusive<Canvas>();
        root->SetWidth(240.0f);
        root->SetHeight(160.0f);
        root->SetValue(PropId_Background, value::FromColor(Color{0.02f, 0.02f, 0.03f, 1.0f}));

        auto vb = MakeIntrusive<Viewbox>();
        vb->SetWidth(200.0f);
        vb->SetHeight(120.0f);
        vb->SetStretch(Stretch::Uniform);

        auto card = MakeIntrusive<Border>();
        card->SetWidth(100.0f);
        card->SetHeight(60.0f);
        card->SetCornerRadius(CornerRadius{16.0f});
        card->SetBackground(Color{0.95f, 0.25f, 0.20f, 1.0f});

        auto icon = MakeIntrusive<Path>();
        icon->SetWidth(40.0f);
        icon->SetHeight(40.0f);
        icon->SetData("M 4,20 L 36,20");
        icon->SetFill(Color{0.0f, 0.0f, 0.0f, 0.0f});
        icon->SetStroke(Color{0.10f, 0.95f, 0.45f, 1.0f});
        icon->SetStrokeThickness(3.0f);
        card->AddChild(icon.Get());

        vb->AddChild(card.Get());
        root->AddChild(vb.Get());
        root->Measure(Size{240.0f, 160.0f});
        root->Arrange(Rect{0.0f, 0.0f, 240.0f, 160.0f});

        PagedRenderTree tree;
        (void)tree.SyncVisualTree(root.Get());
        std::vector<std::uint8_t> fb;
        ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 240u, 160u, fb);

        // At 2x scale, CornerRadius=16 becomes 32px radius! Sample (4, 4) is outside 32px circle arc (dist from (32,32) = 39.6 > 32),
        // whereas unscaled 16px bug had (4,4) well inside (dist from (16,16) = 16.97 -> partial/lit) or (6, 6) fully red!
        const std::size_t outsideScaledCornerIdx = (5u * 240u + 5u) * 4u;
        const std::size_t insideCenterIdx = (60u * 240u + 20u) * 4u;
        AERO_CHECK(fb[outsideScaledCornerIdx + 0u] < 25u,
                   "2x Viewbox scaled SdfRoundedRect CornerRadius=16 to 32px on CPU rasterizer (pixel (5,5) stays unlit)");
        AERO_CHECK(fb[insideCenterIdx + 0u] > 220u,
                   "2x Viewbox SdfRoundedRect interior rendered bright red");

        // <Path StrokeThickness=3> at 2x scale -> 6px stroke centered at Y=60 (covers Y=[57..63], so Y=58 must be bright green!)
        const std::size_t scaledStrokeOuterBandIdx = (58u * 240u + 100u) * 4u;
        AERO_CHECK(fb[scaledStrokeOuterBandIdx + 1u] > 180u,
                   "2x Viewbox scaled <Path StrokeThickness=3> to 6px screen stroke");
    }

    // 2. ScrollViewer pre-arrange overscroll, zero-dead-scroll ScrollBy, Padding viewport clip & Arrange-only dirty
    {
        auto sv = MakeIntrusive<ScrollViewer>();
        sv->SetWidth(200.0f);
        sv->SetHeight(120.0f);
        sv->SetPadding(Thickness{10.0f, 12.0f, 10.0f, 12.0f}); // inner viewport = [10..190] x [12..108] (180x96)
        sv->SetValue(PropId_Background, value::FromColor(Color{0.08f, 0.10f, 0.14f, 1.0f}));

        auto stack = MakeIntrusive<StackPanel>();
        for (int i = 0; i < 5; ++i) {
            auto row = MakeIntrusive<Border>();
            row->SetHeight(50.0f);
            row->SetBackground(Color{0.90f, 0.30f, 0.20f, 1.0f});
            stack->AddChild(row.Get());
        }
        sv->AddChild(stack.Get());

        // Overscroll BEFORE first Arrange -> ArrangeCore must coerce stored offset to maxScrollY (250 - 96 = 154)
        sv->ScrollToVerticalOffset(5000.0f);
        sv->Measure(Size{200.0f, 120.0f});
        sv->Arrange(Rect{0.0f, 0.0f, 200.0f, 120.0f});
        AERO_CHECK(ApproxEq(sv->GetVerticalOffset(), 154.0f),
                   "Pre-arrange overscroll coerced to exact ScrollableHeight (154px) during ArrangeCore");

        // Immediate upward ScrollBy(0, -24) must move directly to 130 (0 dead scroll!) and mark ArrangeDirty ONLY (0 MeasureDirty!)
        sv->ScrollBy(0.0f, -24.0f);
        AERO_CHECK(ApproxEq(sv->GetVerticalOffset(), 130.0f),
                   "ScrollBy(0, -24) immediately stepped from 154 to 130 with zero dead scroll");
        AERO_CHECK((sv->GetLayoutDirtyFlags() & MeasureDirty) == 0u &&
                       (sv->GetLayoutDirtyFlags() & ArrangeDirty) != 0u,
                   "Scrolling marked ArrangeDirty + RenderDirty without marking MeasureDirty");

        DepthBucketLayoutQueue queue;
        queue.EnqueueDirtySubtree(sv.Get());
        AERO_CHECK(!queue.HasPendingMeasure() && queue.HasPendingArrange(),
                   "DepthBucketLayoutQueue enqueued Arrange-only pass (0 Measure passes) for scroll tick");
        (void)queue.FlushLayout(Size{200.0f, 120.0f});

        // HitTest in top 12px Padding band (100, 6) must NOT hit scrolled red row, must return ScrollViewer itself!
        AERO_CHECK(sv->HitTest(Point{100.0f, 6.0f}) == sv.Get(),
                   "HitTest inside ScrollViewer top Padding band clipped child rows and hit ScrollViewer");
        AERO_CHECK(sv->HitTest(Point{100.0f, 30.0f}) != sv.Get() && sv->HitTest(Point{100.0f, 30.0f}) != nullptr,
                   "HitTest inside padded viewport hit child row");

        PagedRenderTree tree;
        (void)tree.SyncVisualTree(sv.Get());
        std::vector<std::uint8_t> fb;
        ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 120u, fb);
        const std::size_t topPadPixelIdx = (6u * 200u + 100u) * 4u;
        const std::size_t innerViewportPixelIdx = (30u * 200u + 100u) * 4u;
        AERO_CHECK(fb[topPadPixelIdx + 0u] < 40u && fb[innerViewportPixelIdx + 0u] > 200u,
                   "ScrollViewer scissor strictly clipped scrolled children out of Padding band");
    }

    // 3. RenderTransform 6-float literal translation preservation + XAML override cleanup
    {
        auto elem = MakeIntrusive<Border>();
        const UnifiedValue parsed6 = ParseBuiltinLiteralPropertyValue(PropId_RenderTransform, "1, 0, 0, 1, 35, -12");
        AERO_CHECK(elem->SetValue(PropId_RenderTransform, parsed6),
                   "UIElement::SetValue accepted 6-float RenderTransform literal with translation");
        AERO_CHECK(ApproxEq(elem->GetRenderTransform().tx, 35.0f) &&
                       ApproxEq(elem->GetRenderTransform().ty, -12.0f),
                   "ParseBuiltinLiteralPropertyValue + SetValue preserved (35, -12) translation");

        AeroCompiler compiler;
        AeroCompileInput overrideInput{};
        overrideInput.xamlContent = R"(
<Border Width="60" Height="30" RenderTransform="1,0,0,1,40,25">
    <Border.RenderTransform>
        <RotateTransform Angle="90" />
    </Border.RenderTransform>
</Border>
)";
        const AeroCompileResult ovRes = compiler.Compile(overrideInput);
        AERO_CHECK(ovRes.succeeded, "Compiled RenderTransform property-element override XAML");
        auto ovRoot = CreateElementByTypeName(ovRes.axbPackage.rootTypeName);
        AERO_CHECK(MountAxbTemplate(ovRoot.Get(), ovRes.axbPackage), "Mounted override XAML");
        AERO_CHECK(ApproxEq(ovRoot->GetRenderTransform().tx, 0.0f) &&
                       ApproxEq(ovRoot->GetRenderTransform().ty, 0.0f) &&
                       ApproxEq(ovRoot->GetRenderTransform().m10, 1.0f),
                   "<Border.RenderTransform> cleanly reset prior inline translation (40, 25) to (0, 0)");
    }

    // 4. Inline alignment string coercion & 0-sparse-entry metadata guard
    {
        auto box = MakeIntrusive<Border>();
        AERO_CHECK(box->SetValue(PropId_HorizontalAlignment, value::FromString("Right")) &&
                       box->SetValue(PropId_VerticalAlignment, value::FromString("Bottom")),
                   "SetValue coerced StringValue 'Right'/'Bottom' on inline alignment properties");
        AERO_CHECK(box->GetHorizontalAlignment() == HorizontalAlignment::Right &&
                       box->GetVerticalAlignment() == VerticalAlignment::Bottom,
                   "String alignment coercion updated inline alignmentFlags");
        AERO_CHECK(!box->SetPropertyMetadata(PropId_HorizontalAlignment, 1u, 0xFFFFu) &&
                       box->SparsePropertyCount() == 0u,
                   "Inline alignment properties never allocate orphan CompactPropertyEntry in sparseProps_");
    }

    // 5. LayoutTransform real layout-box AABB + StaticResource in Transform + Binding diagnostic
    {
        AeroCompiler compiler;
        AeroCompileInput ltInput{};
        ltInput.xamlContent = R"(
<StackPanel Width="160">
    <StackPanel.Resources>
        <Double x:Key="TwiceScale">2.0</Double>
    </StackPanel.Resources>
    <Border x:Name="RotatedLayoutItem" Width="100" Height="30" HorizontalAlignment="Left" Background="#EE4433">
        <Border.LayoutTransform>
            <RotateTransform Angle="90" />
        </Border.LayoutTransform>
    </Border>
    <Border x:Name="ScaledByResItem" Width="40" Height="20" HorizontalAlignment="Left" Background="#22AAFF">
        <Border.RenderTransform>
            <ScaleTransform ScaleX="{StaticResource TwiceScale}" ScaleY="1" />
        </Border.RenderTransform>
        <Border.LayoutTransform>
            <RotateTransform Angle="{Binding UnsupportedDynamicAngle}" />
        </Border.LayoutTransform>
    </Border>
</StackPanel>
)";
        const AeroCompileResult ltRes = compiler.Compile(ltInput);
        AERO_CHECK(ltRes.succeeded, "Compiled LayoutTransform + StaticResource transform XAML");
        bool sawTransformBindingDiag = false;
        for (const auto& d : ltRes.diagnostics) {
            if (d.find("AERO_XAML_TRANSFORM_BINDING") != std::string::npos) {
                sawTransformBindingDiag = true;
                break;
            }
        }
        AERO_CHECK(sawTransformBindingDiag,
                   "Compiler emitted AERO_XAML_TRANSFORM_BINDING diagnostic on unsupported child transform {Binding}");

        auto ltRoot = CreateElementByTypeName(ltRes.axbPackage.rootTypeName);
        AERO_CHECK(MountAxbTemplate(ltRoot.Get(), ltRes.axbPackage), "Mounted LayoutTransform XAML");
        ltRoot->Measure(Size{160.0f, 400.0f});
        ltRoot->Arrange(Rect{0.0f, 0.0f, 160.0f, 200.0f});

        auto* rotItem = ltRoot->FindName("RotatedLayoutItem");
        auto* nextItem = ltRoot->FindName("ScaledByResItem");
        AERO_CHECK(rotItem && nextItem, "Found RotatedLayoutItem and ScaledByResItem");
        AERO_CHECK(ApproxEq(rotItem->DesiredSize().width, 30.0f) &&
                       ApproxEq(rotItem->DesiredSize().height, 100.0f),
                   "LayoutTransform RotateTransform(90) transformed 100x30 DesiredSize into 30x100 layout box");
        AERO_CHECK(ApproxEq(nextItem->FinalRect().y, 100.0f),
                   "Sibling in StackPanel positioned at Y=100 below rotated 30x100 LayoutTransform box (0 overlap)");
        AERO_CHECK(ApproxEq(nextItem->GetRenderTransform().m00, 2.0f),
                   "<ScaleTransform ScaleX='{StaticResource TwiceScale}'/> resolved Double resource 2.0");
        AERO_CHECK(ltRoot->HitTest(Point{15.0f, 85.0f}) == rotItem,
                   "HitTest(15, 85) accurately hit 90-degree LayoutTransform item in [0..30]x[0..100]");
    }

    // 6. ECS Track B BatchUpdateWidthsFromRawColumn with RenderTransformOrigin(0.5,0.5) + RotateTransform(90)
    {
        auto root = MakeIntrusive<Canvas>();
        root->SetWidth(200.0f);
        root->SetHeight(200.0f);
        auto bar = MakeIntrusive<Border>();
        Canvas::SetLeft(bar.Get(), 60.0f);
        Canvas::SetTop(bar.Get(), 90.0f);
        bar->SetWidth(40.0f);
        bar->SetHeight(20.0f);
        bar->SetRenderTransformOrigin(Point{0.5f, 0.5f});
        bar->SetRenderTransform(Matrix2D::Rotation(90.0f));
        root->AddChild(bar.Get());
        root->Measure(Size{200.0f, 200.0f});
        root->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});

        PagedRenderTree tree;
        (void)tree.SyncVisualTree(root.Get());
        const float newWidthCol[1] = {80.0f};
        AERO_CHECK(tree.BatchUpdateWidthsFromRawColumn(bar->RenderNodeId(), 1u, newWidthCol, sizeof(float), 0u, 1.0f) == 1u,
                   "BatchUpdateWidthsFromRawColumn updated rotated bar width from 40 to 80");
        // With x=60, y=90, w=80, h=20, origin=(0.5,0.5) -> pivot=(100,100); rotated 90 deg -> X=[90..110], Y=[60..140]!
        const RenderNodeDescriptor* nd = tree.GetNode(bar->RenderNodeId());
        const AeroVertex& v0 = tree.GetPage(nd->pageIndex).vertices[nd->vertexOffsetInPage + 0u];
        const AeroVertex& v1 = tree.GetPage(nd->pageIndex).vertices[nd->vertexOffsetInPage + 1u];
        AERO_CHECK(ApproxEq(v0.x, 110.0f, 0.05f) && ApproxEq(v0.y, 60.0f, 0.05f) &&
                       ApproxEq(v1.x, 110.0f, 0.05f) && ApproxEq(v1.y, 140.0f, 0.05f),
                   "ECS Track B width update recomputed RenderTransformOrigin(0.5,0.5) pivot accurately");
    }

    std::cout << "[PASS] TestPhase5AReviewHardeningRegressions\n";
}

} // namespace aero::tests

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();
    using namespace aero::tests;
    std::cout << "=== AeroGUI 2.0 Phase 5A Layout & Full 2D Affine Transform Suite ===\n";
    TestAlignmentAndMaxDimensionsZeroAlloc();
    TestDockPanelLayoutAndLastChildFill();
    TestWrapPanelFlowAndUniformItems();
    TestViewboxStretchModesHitTestAndGoldenImage();
    TestScrollViewerOffsetsClipAndLiveSync();
    TestFull2DAffineTransformsXamlAotHitTestAndGoldenRaster();
    TestNestedContainerRotationAndRotatedUtf8Glyphs();
    TestPhase5AReviewHardeningRegressions();
    std::cout << "=== ALL 8 PHASE 5A TEST SUITES PASSED (100%) ===\n";
    return 0;
}
