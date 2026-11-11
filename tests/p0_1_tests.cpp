#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#define AERO_P01_CHECK(cond, msg)                                                  \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> "        \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::p0_1_tests {

struct Rgba8Sample {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

[[nodiscard]] Rgba8Sample SamplePixel(
    const std::vector<std::uint8_t>& fb,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y) noexcept {
    const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4u;
    return Rgba8Sample{fb[idx + 0u], fb[idx + 1u], fb[idx + 2u], fb[idx + 3u]};
}

[[nodiscard]] bool ColorNear(
    const Rgba8Sample& px,
    std::uint8_t er,
    std::uint8_t eg,
    std::uint8_t eb,
    int tol = 6) noexcept {
    return std::abs(static_cast<int>(px.r) - static_cast<int>(er)) <= tol &&
           std::abs(static_cast<int>(px.g) - static_cast<int>(eg)) <= tol &&
           std::abs(static_cast<int>(px.b) - static_cast<int>(eb)) <= tol;
}

// ============================================================================
// Test (a): Nested Unbacked Containers Stay Transparent (Never Occlude Red Backdrop)
// ============================================================================
void TestNestedUnbackedContainersTransparency() {
    auto rootWin = MakeIntrusive<Window>();
    rootWin->SetWidth(200.0f);
    rootWin->SetHeight(160.0f);

    auto rootCanvas = MakeIntrusive<Canvas>();
    rootCanvas->SetWidth(200.0f);
    rootCanvas->SetHeight(160.0f);
    rootWin->AddChild(rootCanvas.Get());

    // Bottom red backdrop node (#EE1A2B -> ~238, 26, 43)
    auto redBackdrop = MakeIntrusive<Border>();
    redBackdrop->SetWidth(200.0f);
    redBackdrop->SetHeight(160.0f);
    redBackdrop->SetBackground(Color{0.933f, 0.102f, 0.169f, 1.0f});
    rootCanvas->AddChild(redBackdrop.Get());

    // Deeply nested unbacked containers layered directly over redBackdrop:
    // UserControl -> Grid -> DockPanel -> WrapPanel -> ScrollViewer -> ItemsControl -> StackPanel -> Border
    auto userCtrl = MakeIntrusive<UserControl>();
    userCtrl->SetWidth(200.0f);
    userCtrl->SetHeight(160.0f);
    rootCanvas->AddChild(userCtrl.Get());

    auto outerGrid = MakeIntrusive<Grid>();
    outerGrid->SetWidth(200.0f);
    outerGrid->SetHeight(160.0f);
    userCtrl->AddChild(outerGrid.Get());

    auto dockPanel = MakeIntrusive<DockPanel>();
    dockPanel->SetWidth(200.0f);
    dockPanel->SetHeight(160.0f);
    outerGrid->AddChild(dockPanel.Get());

    auto wrapPanel = MakeIntrusive<WrapPanel>();
    wrapPanel->SetWidth(200.0f);
    wrapPanel->SetHeight(160.0f);
    dockPanel->AddChild(wrapPanel.Get());

    auto scrollViewer = MakeIntrusive<ScrollViewer>();
    scrollViewer->SetWidth(200.0f);
    scrollViewer->SetHeight(160.0f);
    wrapPanel->AddChild(scrollViewer.Get());

    auto itemsCtrl = MakeIntrusive<ItemsControl>();
    itemsCtrl->SetWidth(200.0f);
    itemsCtrl->SetHeight(160.0f);
    scrollViewer->AddChild(itemsCtrl.Get());

    auto stackPanel = MakeIntrusive<StackPanel>();
    stackPanel->SetWidth(200.0f);
    stackPanel->SetHeight(160.0f);
    // Explicitly test alpha == 0 Background as well
    stackPanel->SetValue(PropId_Background, value::FromColor(Color{0.2f, 0.8f, 0.4f, 0.0f}));
    itemsCtrl->AddChild(stackPanel.Get());

    auto unbackedBorder = MakeIntrusive<Border>();
    unbackedBorder->SetWidth(200.0f);
    unbackedBorder->SetHeight(160.0f);
    stackPanel->AddChild(unbackedBorder.Get());

    rootWin->Measure(Size{200.0f, 160.0f});
    rootWin->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(rootWin.Get());

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 160u, fb, Color{0.05f, 0.05f, 0.08f, 1.0f});

    const Rgba8Sample centerPx = SamplePixel(fb, 200u, 100u, 80u);
    AERO_P01_CHECK(
        ColorNear(centerPx, 238u, 26u, 43u, 4),
        "Nested unbacked Window/UserControl/Grid/DockPanel/WrapPanel/ScrollViewer/ItemsControl/Canvas/StackPanel/Border did NOT occlude red backdrop");

    // Verify incremental UpdateElementInPlace: temporarily set opaque blue on outerGrid, then clear back to unbacked!
    outerGrid->SetValue(PropId_Background, value::FromColor(Color{0.10f, 0.30f, 0.90f, 1.0f}));
    (void)tree.SyncVisualTree(rootWin.Get());
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 160u, fb);
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 200u, 100u, 80u), 26u, 77u, 230u, 5),
        "Setting explicit Background on Grid rendered opaque blue via SyncVisualTree");

    AERO_P01_CHECK(outerGrid->ClearValue(PropId_Background), "Cleared Background on Grid");
    (void)tree.SyncVisualTree(rootWin.Get());
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 160u, fb);
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 200u, 100u, 80u), 238u, 26u, 43u, 4),
        "Clearing Background on Grid restored transparency in UpdateElementInPlace without opaque fallback");

    std::cout << "[PASS] TestNestedUnbackedContainersTransparency\n";
}

// ============================================================================
// Test (b): PropId_Foreground on Containers Never Paints Container Background
// ============================================================================
void TestForegroundDoesNotPolluteContainerBackground() {
    auto rootWin = MakeIntrusive<Window>();
    rootWin->SetWidth(180.0f);
    rootWin->SetHeight(120.0f);
    // Set bright neon green Foreground on Window, UserControl, Grid, StackPanel, and Border
    const Color neonGreen{0.05f, 0.98f, 0.15f, 1.0f};
    rootWin->SetValue(PropId_Foreground, value::FromColor(neonGreen));

    auto canvas = MakeIntrusive<Canvas>();
    canvas->SetWidth(180.0f);
    canvas->SetHeight(120.0f);
    canvas->SetValue(PropId_Background, value::FromColor(Color{0.85f, 0.12f, 0.16f, 1.0f})); // crimson backdrop
    rootWin->AddChild(canvas.Get());

    auto uc = MakeIntrusive<UserControl>();
    uc->SetWidth(180.0f);
    uc->SetHeight(120.0f);
    uc->SetValue(PropId_Foreground, value::FromColor(neonGreen));
    canvas->AddChild(uc.Get());

    auto stack = MakeIntrusive<StackPanel>();
    stack->SetWidth(180.0f);
    stack->SetHeight(120.0f);
    stack->SetValue(PropId_Foreground, value::FromColor(neonGreen));
    uc->AddChild(stack.Get());

    auto borderBox = MakeIntrusive<Border>();
    borderBox->SetWidth(160.0f);
    borderBox->SetHeight(80.0f);
    borderBox->SetValue(PropId_Foreground, value::FromColor(neonGreen));
    stack->AddChild(borderBox.Get());

    rootWin->Measure(Size{180.0f, 120.0f});
    rootWin->Arrange(Rect{0.0f, 0.0f, 180.0f, 120.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(rootWin.Get());

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 180u, 120u, fb);

    // Sample inside StackPanel & Border: must be crimson (#D91F29 -> ~217, 31, 41), NEVER neonGreen!
    const Rgba8Sample p0 = SamplePixel(fb, 180u, 80u, 40u);
    AERO_P01_CHECK(
        ColorNear(p0, 217u, 31u, 41u, 5) && p0.g < 50u,
        "Foreground on StackPanel/Border/UserControl/Window did NOT paint container background green");

    // Mutate Foreground dynamically and sync via UpdateElementInPlace
    stack->SetValue(PropId_Foreground, value::FromColor(Color{0.1f, 0.9f, 1.0f, 1.0f}));
    borderBox->SetValue(PropId_Foreground, value::FromColor(Color{1.0f, 1.0f, 0.0f, 1.0f}));
    (void)tree.SyncVisualTree(rootWin.Get());
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 180u, 120u, fb);
    const Rgba8Sample p1 = SamplePixel(fb, 180u, 80u, 40u);
    AERO_P01_CHECK(
        ColorNear(p1, 217u, 31u, 41u, 5),
        "UpdateElementInPlace also kept Foreground-only StackPanel and Border completely transparent");

    std::cout << "[PASS] TestForegroundDoesNotPolluteContainerBackground\n";
}

// ============================================================================
// Test (c): Independent BorderBrush + 4-Side BorderThickness + Asymmetric CornerRadius(16, 0, 16, 0)
// ============================================================================
void TestIndependentBorderBrushAndAsymmetricCornerRadius() {
    // 1. Direct analytical coverage checks on EvaluateSdfRoundedRectCoverage(CornerRadius(16, 0, 16, 0))
    //    Box 100x80 -> halfWidth=50, halfHeight=40
    const CornerRadius asymCr{16.0f, 0.0f, 16.0f, 0.0f};
    // Top-Left (-48.5, -38.5) is outside r=16 arc -> ~0.0 coverage
    const float covTLOut = EvaluateSdfRoundedRectCoverage(-48.5f, -38.5f, 50.0f, 40.0f, asymCr, 1.0f);
    // Top-Right (+48.5, -38.5) has r=0 sharp corner -> 1.0 coverage
    const float covTRSharp = EvaluateSdfRoundedRectCoverage(48.5f, -38.5f, 50.0f, 40.0f, asymCr, 1.0f);
    // Bottom-Right (+48.5, +38.5) is outside r=16 arc -> ~0.0 coverage
    const float covBROut = EvaluateSdfRoundedRectCoverage(48.5f, 38.5f, 50.0f, 40.0f, asymCr, 1.0f);
    // Bottom-Left (-48.5, +38.5) has r=0 sharp corner -> 1.0 coverage
    const float covBLSharp = EvaluateSdfRoundedRectCoverage(-48.5f, 38.5f, 50.0f, 40.0f, asymCr, 1.0f);

    AERO_P01_CHECK(covTLOut < 0.01f, "Top-Left r=16 corner outside arc clipped to 0 coverage");
    AERO_P01_CHECK(covTRSharp > 0.99f, "Top-Right r=0 sharp corner has full 1.0 coverage");
    AERO_P01_CHECK(covBROut < 0.01f, "Bottom-Right r=16 corner outside arc clipped to 0 coverage");
    AERO_P01_CHECK(covBLSharp > 0.99f, "Bottom-Left r=0 sharp corner has full 1.0 coverage");

    // 2. End-to-end visual tree Border: Blue fill + Gold BorderBrush + BorderThickness=6 + CornerRadius(16, 0, 16, 0)
    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(140.0f);
    root->SetHeight(120.0f);

    const Color clearBg{0.05f, 0.06f, 0.08f, 1.0f};  // (13, 15, 20)
    const Color blueFill{0.10f, 0.35f, 0.90f, 1.0f}; // (26, 89, 230)
    const Color goldStroke{1.0f, 0.82f, 0.08f, 1.0f}; // (255, 209, 20)

    auto card = MakeIntrusive<Border>();
    Canvas::SetLeft(card.Get(), 20.0f);
    Canvas::SetTop(card.Get(), 20.0f);
    card->SetWidth(100.0f);
    card->SetHeight(80.0f); // occupies X=[20..120], Y=[20..100]
    card->SetBackground(blueFill);
    card->SetBorderBrush(goldStroke);
    card->SetBorderThickness(Thickness{6.0f});
    card->SetCornerRadius(asymCr);
    root->AddChild(card.Get());

    root->Measure(Size{140.0f, 120.0f});
    root->Arrange(Rect{0.0f, 0.0f, 140.0f, 120.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    AERO_P01_CHECK(card->RenderNodeSpan() == 2u, "Border with Background + BorderBrush bound 2-node span");

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 140u, 120u, fb, clearBg);

    // Center (70, 60): 100% pure Blue fill (no 0.35 border mask alpha dimming or gold mixing!)
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 70u, 60u), 26u, 89u, 230u, 4),
        "Card interior (70, 60) rendered pure unmixed Blue Background");

    // Top-Left corner (card local (1,1) -> screen (21,21)): outside r=16 arc -> clearBg!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 21u, 21u), 13u, 15u, 20u, 4),
        "Top-Left r=16 exterior (21, 21) stayed unlit clearBg");
    // Top-Left curved Gold border arc: arc center at (36, 36), radius in [10..16] -> (27, 27) has dist ~12.02 -> pure Gold!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 27u, 27u), 255u, 209u, 20u, 5),
        "Top-Left r=16 curved border ring (27, 27) rendered pure Gold BorderBrush");
    // Top-Left just inside curved border: (33, 33) has dist ~3.54 < 10 -> pure Blue!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 33u, 33u), 26u, 89u, 230u, 5),
        "Top-Left r=16 interior inside border arc (33, 33) rendered pure Blue Background");

    // Top-Right corner (r=0): outermost corner pixel (119, 20) and (118, 21) -> 100% pure Gold!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 119u, 20u), 255u, 209u, 20u, 5) &&
            ColorNear(SamplePixel(fb, 140u, 118u, 21u), 255u, 209u, 20u, 5),
        "Top-Right r=0 sharp corner (119, 20) rendered crisp 100% Gold BorderBrush");
    // Top-Right interior just inside 6px border (111, 28) -> pure Blue!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 111u, 28u), 26u, 89u, 230u, 5),
        "Top-Right r=0 inner corner (111, 28) rendered pure Blue Background");

    // Bottom-Right corner (r=16): outside arc (118, 98) -> clearBg; on arc (112, 92) -> pure Gold!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 118u, 98u), 13u, 15u, 20u, 4),
        "Bottom-Right r=16 exterior (118, 98) stayed unlit clearBg");
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 112u, 92u), 255u, 209u, 20u, 5),
        "Bottom-Right r=16 curved border ring (112, 92) rendered pure Gold BorderBrush");

    // Bottom-Left corner (r=0): outermost corner pixel (20, 99) and (21, 98) -> 100% pure Gold!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 20u, 99u), 255u, 209u, 20u, 5) &&
            ColorNear(SamplePixel(fb, 140u, 21u, 98u), 255u, 209u, 20u, 5),
        "Bottom-Left r=0 sharp corner (20, 99) rendered crisp 100% Gold BorderBrush");

    // 3. Test 4-side asymmetric BorderThickness(8, 2, 0, 4) + in-place SyncVisualTree update (span preserved!)
    card->SetCornerRadius(CornerRadius{0.0f});
    card->SetBorderThickness(Thickness{8.0f, 2.0f, 0.0f, 4.0f});
    const std::uint32_t prevTreeId = tree.TreeId();
    const std::size_t syncedCount = tree.SyncVisualTree(root.Get());
    AERO_P01_CHECK(
        syncedCount == 1u && tree.TreeId() == prevTreeId && card->RenderNodeSpan() == 2u,
        "Asymmetric 4-side BorderThickness updated in-place in O(1) with consistent RenderNodeSpan=2");

    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 140u, 120u, fb, clearBg);
    // Left border = 8px (X=[20..28] is Gold, X=29 is Blue)
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 26u, 60u), 255u, 209u, 20u, 5), "Left 8px border is Gold");
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 30u, 60u), 26u, 89u, 230u, 5), "Inside Left 8px border is Blue");
    // Top border = 2px (Y=[20..22] is Gold, Y=24 is Blue)
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 70u, 20u), 255u, 209u, 20u, 5), "Top 2px border is Gold");
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 70u, 24u), 26u, 89u, 230u, 5), "Below Top 2px border is Blue");
    // Right border = 0px (X=119 is pure Blue right up to the right edge!)
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 119u, 60u), 26u, 89u, 230u, 5), "Right 0px border has Blue to edge");
    // Bottom border = 4px (Y=[96..100] is Gold, Y=94 is Blue)
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 70u, 98u), 255u, 209u, 20u, 5), "Bottom 4px border is Gold");
    AERO_P01_CHECK(ColorNear(SamplePixel(fb, 140u, 70u, 94u), 26u, 89u, 230u, 5), "Above Bottom 4px border is Blue");

    // 4. Hollow Border (Background cleared -> transparent interior, Gold border remains!)
    AERO_P01_CHECK(card->ClearValue(PropId_Background), "Cleared Background on bordered card");
    (void)tree.SyncVisualTree(root.Get());
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 140u, 120u, fb, Color{0.80f, 0.10f, 0.15f, 1.0f});
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 70u, 60u), 204u, 26u, 38u, 5),
        "Hollow Border with cleared Background lets underlying backdrop show through interior");
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 140u, 24u, 60u), 255u, 209u, 20u, 5),
        "Hollow Border still renders crisp Gold BorderBrush frame");

    std::cout << "[PASS] TestIndependentBorderBrushAndAsymmetricCornerRadius\n";
}

// ============================================================================
// Test (d): Vertex Encoding Round-Trip, XAML 4-Value CornerRadius & UberShader Verification
// ============================================================================
void TestVertexPackingXamlAndUberShaders() {
    // 1. Vertex bitfield round-trip across all 4 SDF modes
    {
        std::uint16_t primExtra = 0u;
        std::uint16_t flags = 0u;
        CornerRadius outCr{};
        Thickness outBt{};
        bool outHollow = false;

        // Mode 0: Uniform fast path
        PackSdfRoundedRectVertexParams(CornerRadius{12.5f}, Thickness{2.0f}, false, primExtra, flags);
        AERO_P01_CHECK(primExtra == 0u, "Uniform fill uses compact fast path (primExtra == 0)");
        UnpackSdfRoundedRectVertexParams(
            static_cast<std::uint16_t>(1u | primExtra), flags, outCr, outBt, outHollow);
        AERO_P01_CHECK(!outHollow && IsUniformCornerRadius(outCr) &&
                           std::fabs(outCr.topLeft - 12.5f) < 0.01f &&
                           std::fabs(outBt.left - 2.0f) < 0.01f,
                       "Mode 0 round-trip exact");

        // Mode 1: Uniform radius + 4-side hollow border
        PackSdfRoundedRectVertexParams(
            CornerRadius{10.0f}, Thickness{4.0f, 2.0f, 6.0f, 8.0f}, true, primExtra, flags);
        UnpackSdfRoundedRectVertexParams(
            static_cast<std::uint16_t>(1u | primExtra), flags, outCr, outBt, outHollow);
        AERO_P01_CHECK(outHollow && std::fabs(outCr.topLeft - 10.0f) < 0.01f &&
                           std::fabs(outBt.left - 4.0f) < 0.01f &&
                           std::fabs(outBt.top - 2.0f) < 0.01f &&
                           std::fabs(outBt.right - 6.0f) < 0.01f &&
                           std::fabs(outBt.bottom - 8.0f) < 0.01f,
                       "Mode 1 uniform radius + 4-side hollow border round-trip exact");

        // Mode 2: Asymmetric 4-corner fill
        PackSdfRoundedRectVertexParams(
            CornerRadius{16.0f, 0.0f, 12.5f, 4.5f}, Thickness{}, false, primExtra, flags);
        UnpackSdfRoundedRectVertexParams(
            static_cast<std::uint16_t>(1u | primExtra), flags, outCr, outBt, outHollow);
        AERO_P01_CHECK(!outHollow &&
                           std::fabs(outCr.topLeft - 16.0f) < 0.01f &&
                           std::fabs(outCr.topRight - 0.0f) < 0.01f &&
                           std::fabs(outCr.bottomRight - 12.5f) < 0.01f &&
                           std::fabs(outCr.bottomLeft - 4.5f) < 0.01f,
                       "Mode 2 asymmetric 4-corner fill round-trip exact");

        // Mode 3: Asymmetric 4-corner hollow border
        PackSdfRoundedRectVertexParams(
            CornerRadius{16.0f, 0.0f, 16.0f, 0.0f}, Thickness{4.5f}, true, primExtra, flags);
        UnpackSdfRoundedRectVertexParams(
            static_cast<std::uint16_t>(1u | primExtra), flags, outCr, outBt, outHollow);
        AERO_P01_CHECK(outHollow &&
                           std::fabs(outCr.topLeft - 16.0f) < 0.01f &&
                           std::fabs(outCr.topRight - 0.0f) < 0.01f &&
                           std::fabs(outCr.bottomRight - 16.0f) < 0.01f &&
                           std::fabs(outCr.bottomLeft - 0.0f) < 0.01f &&
                           std::fabs(outBt.left - 4.5f) < 0.01f,
                       "Mode 3 asymmetric 4-corner hollow border round-trip exact");
    }

    // 2. Verify all 3 GPU UberShaders implement sdRoundedBox4(p, b, vec4(tr, br, tl, bl))
    for (ShaderBackend backend : {ShaderBackend::DesktopGL33, ShaderBackend::WebGL2, ShaderBackend::WebGPU_WGSL}) {
        const std::string_view vs = ZeroCopyGpuStream::UberVertexShaderSource(backend);
        const std::string_view ps = ZeroCopyGpuStream::UberPixelShaderSource(backend);
        AERO_P01_CHECK(
            vs.find("0x0007u") != std::string_view::npos &&
                vs.find("0xFFF8u") != std::string_view::npos,
            "UberVertexShader forwards extended SDF mode bits into v_Flags");
        AERO_P01_CHECK(
            ps.find("sdRoundedBox4") != std::string_view::npos &&
                ps.find("sdfMode") != std::string_view::npos,
            "UberPixelShader implements sdRoundedBox4 and multi-mode SDF evaluation");
    }

    // 3. XAML end-to-end compile + mount + Button with BorderBrush & CornerRadius="16,0,16,0"
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Grid Width="160" Height="100" Background="#CC1122">
    <StackPanel Foreground="#00FF44">
        <Border x:Name="XamlBadge" Width="120" Height="60"
                Background="#1A55E6" BorderBrush="#FFD700"
                BorderThickness="5" CornerRadius="16,0,16,0" />
        <Button x:Name="BorderedBtn" Width="120" Height="30"
                Background="#204080" BorderBrush="#00EEAA"
                BorderThickness="3" CornerRadius="8,0,8,0" Content="OK" />
    </StackPanel>
</Grid>
)";
    const AeroCompileResult res = compiler.Compile(input);
    AERO_P01_CHECK(res.succeeded, "Compiled XAML with 4-component CornerRadius and BorderBrush");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P01_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted XAML package");
    root->Measure(Size{160.0f, 100.0f});
    root->Arrange(Rect{0.0f, 0.0f, 160.0f, 100.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    auto* btn = root->FindName("BorderedBtn");
    AERO_P01_CHECK(btn != nullptr && btn->RenderNodeSpan() == 3u,
                   "Button with BorderBrush + label bound 3-node span (fill + borderRing + glyphRun)");

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 160u, 100u, fb);
    // Outside XamlBadge (at X=5, Y=50): StackPanel has Foreground="#00FF44" and no Background -> Grid's #CC1122 shows!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 160u, 5u, 50u), 204u, 17u, 34u, 5),
        "XAML StackPanel with Foreground=#00FF44 kept Grid #CC1122 visible underneath");
    // XamlBadge centered horizontally in 160px StackPanel -> X=[20..140], Y=[0..60]:
    // Top-Right sharp corner (139, 1) must be Gold (#FFD700 -> 255, 215, 0), Top-Left outside arc (21, 1) must be Grid #CC1122!
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 160u, 139u, 1u), 255u, 215u, 0u, 5),
        "XAML Border Top-Right sharp corner rendered Gold BorderBrush");
    AERO_P01_CHECK(
        ColorNear(SamplePixel(fb, 160u, 21u, 1u), 204u, 17u, 34u, 5),
        "XAML Border Top-Left r=16 cutout exposed underlying Grid #CC1122");

    std::cout << "[PASS] TestVertexPackingXamlAndUberShaders\n";
}

} // namespace aero::p0_1_tests

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    aero::p0_1_tests::TestNestedUnbackedContainersTransparency();
    aero::p0_1_tests::TestForegroundDoesNotPolluteContainerBackground();
    aero::p0_1_tests::TestIndependentBorderBrushAndAsymmetricCornerRadius();
    aero::p0_1_tests::TestVertexPackingXamlAndUberShaders();

    std::cout << "=== ALL P0-1 RENDER & SDF TESTS PASSED 100% ===\n";
    return 0;
}
