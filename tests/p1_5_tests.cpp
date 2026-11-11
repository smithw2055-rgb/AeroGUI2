#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

using namespace aero;

namespace {

int gFailureCount = 0;

void CheckCondition(bool cond, const char* expr, int line, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] p1_5_tests.cpp:%d: %s (%s)\n", line, msg, expr);
        ++gFailureCount;
    }
}

#define AERO_P15_CHECK(cond, msg) CheckCondition((cond), #cond, __LINE__, (msg))

bool LoadFileBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return false;
    }
    const std::streamsize sz = ifs.tellg();
    if (sz <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(out.data()), sz));
}

struct RgbaSample {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

RgbaSample SamplePixel(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y) {
    const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4u;
    if (idx + 3u >= rgba.size()) {
        return RgbaSample{};
    }
    return RgbaSample{rgba[idx + 0u], rgba[idx + 1u], rgba[idx + 2u], rgba[idx + 3u]};
}

std::size_t CountNonClearPixels(
    const std::vector<std::uint8_t>& rgba,
    std::uint8_t clearR,
    std::uint8_t clearG,
    std::uint8_t clearB) {
    std::size_t diffCount = 0u;
    const std::size_t pxCount = rgba.size() / 4u;
    for (std::size_t i = 0u; i < pxCount; ++i) {
        const int dr = static_cast<int>(rgba[i * 4u + 0u]) - static_cast<int>(clearR);
        const int dg = static_cast<int>(rgba[i * 4u + 1u]) - static_cast<int>(clearG);
        const int db = static_cast<int>(rgba[i * 4u + 2u]) - static_cast<int>(clearB);
        if (std::abs(dr) > 2 || std::abs(dg) > 2 || std::abs(db) > 2) {
            ++diffCount;
        }
    }
    return diffCount;
}

// ============================================================================
// 0. Verify Exclusive BuiltinPropertyId Range [130..145]
// ============================================================================
void TestPropertyIdExclusiveRange() {
    const std::uint16_t ids[] = {
        PropId_Transform3DRotateXYZ,
        PropId_Transform3DTranslateXYZ,
        PropId_Transform3DScaleXYZ,
        PropId_Transform3DCenterXYZ,
        PropId_EffectKind,
        PropId_EffectParams,
        PropId_EffectColor,
        PropId_RiveSource,
        PropId_AnimRotation3DX,
        PropId_AnimRotation3DY,
        PropId_AnimRotation3DZ,
        PropId_AnimTranslate3DX,
        PropId_AnimTranslate3DY,
        PropId_AnimTranslate3DZ,
        PropId_EffectBlurRadius,
        PropId_EffectShadowDepth,
    };
    bool allInRange = true;
    for (std::uint16_t id : ids) {
        if (id < 130u || id > 145u) {
            allInRange = false;
        }
    }
    AERO_P15_CHECK(allInRange, "All P1-5 BuiltinPropertyIds strictly reside within [130..145]");
    if (gFailureCount == 0) {
        std::printf("[PASS] TestPropertyIdExclusiveRange\n");
    }
}

// ============================================================================
// 1. Real Rive v7 LEB128 Binary .riv Stream Decoder + RiveControl + JSON Compat
// ============================================================================
void TestRealRiveV7BinaryImportersAndRiveControl() {
    const int startFails = gFailureCount;
    const std::string riveDir = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Rive/Data/";
    RiveControl::ClearAssetSearchDirectories();
    RiveControl::AddAssetSearchDirectory(riveDir);

    const char* kSampleRiveFiles[] = {
        "logo.riv",
        "start.riv",
        "rewards.riv",
        "settings.riv",
        "background.riv",
        "leaves-tl.riv",
        "leaves-tr.riv",
        "leaves-bl.riv",
        "leaves-br.riv",
    };

    RiveFrontendImporter importer;
    for (const char* fileName : kSampleRiveFiles) {
        std::vector<std::uint8_t> bytes;
        const std::string fullPath = riveDir + fileName;
        AERO_P15_CHECK(LoadFileBytes(fullPath, bytes), fileName);
        AERO_P15_CHECK(IsRiveBinaryStream(bytes.data(), bytes.size()), "IsRiveBinaryStream detects v7 header");

        AeroSceneGraphIR ir{};
        std::vector<std::string> diags;
        const bool ok = importer.ImportToSceneIR(fullPath, bytes.data(), bytes.size(), ir, diags);
        AERO_P15_CHECK(ok && diags.empty(), "RiveFrontendImporter::ImportToSceneIR zero-error binary import");
        AERO_P15_CHECK(ir.sourceFormat == AeroFrontendFormat::RiveRiv, "SceneIR tagged as RiveRiv");
        AERO_P15_CHECK(ir.designWidth > 10.0f && ir.designHeight > 10.0f, "Artboard dimensions extracted");
        AERO_P15_CHECK(ir.nodes.size() >= 3u, "Non-empty visual node hierarchy extracted");
        AERO_P15_CHECK(!ir.vectorShapes.empty(), "Closed/open Bezier contours converted to SVG vector paths");
        AERO_P15_CHECK(!ir.loweredPackage.mtsdfPaths.empty(), "Vector contours baked into adaptive MTSDF atlas");
        AERO_P15_CHECK(ir.loweredPackage.HasMotionSection(), "Animations & state machines lowered into MOT6");

        // Verify full AXB binary serialization + deserialization round-trip (including MOT6 & MTSDF)
        const std::vector<std::uint8_t> axbWire = ir.loweredPackage.Serialize();
        AxbPackage reloadedPkg{};
        AERO_P15_CHECK(
            AxbPackage::Deserialize(axbWire, reloadedPkg),
            "Serialized Rive AXB package deserializes cleanly");

        // Verify RiveControl mounts, lays out, advances state machine, and rasterizes pixels
        auto rc = MakeIntrusive<RiveControl>();
        rc->SetWidth(240.0f);
        rc->SetHeight(160.0f);
        rc->SetStretch(Stretch::Uniform);
        rc->SetSource(fileName);
        AERO_P15_CHECK(rc->IsRiveLoaded(), "RiveControl::SetSource automatically decodes and mounts .riv");
        AERO_P15_CHECK(rc->ArtboardSize().width > 10.0f && rc->ArtboardSize().height > 10.0f,
                       "RiveControl reports native Artboard dimensions");

        // Advance animation / state machine pose
        (void)rc->SetInputBool("Hover", true);
        (void)rc->SetInputFloat("States", 1.0f);
        (void)rc->Advance(0.08f);

        rc->Measure(Size{240.0f, 160.0f});
        rc->Arrange(Rect{0.0f, 0.0f, 240.0f, 160.0f});

        PagedRenderTree tree;
        const std::size_t nodeCount = tree.BuildFromVisualTree(rc.Get());
        AERO_P15_CHECK(nodeCount > 0u, "RiveControl emits visual render nodes in PagedRenderTree");

        std::vector<std::uint8_t> rgba;
        ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 240u, 160u, rgba, Color{0.05f, 0.06f, 0.08f, 1.0f});
        const RgbaSample clr = SamplePixel(rgba, 240u, 0u, 0u);
        const std::size_t drawnPx = CountNonClearPixels(rgba, clr.r, clr.g, clr.b);
        AERO_P15_CHECK(drawnPx > 25u, "Rasterized RiveControl produces visible MTSDF vector pixels");

        std::printf(
            "  [Rive v7] %-14s -> artboard=%.0fx%.0f, irNodes=%zu, paths=%zu, clips=%zu, sm=%zu, pixels=%zu\n",
            fileName,
            ir.designWidth,
            ir.designHeight,
            ir.nodes.size(),
            ir.vectorShapes.size(),
            ir.loweredPackage.motionClips.size(),
            ir.loweredPackage.stateMachines.size(),
            drawnPx);
    }

    // Verify XAML <noesis:RiveControl Source="logo.riv" .../> compilation and runtime mounting
    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "RiveMountTest.xaml";
    xamlIn.xamlContent = R"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
      Width="400" Height="200">
    <noesis:RiveControl x:Name="LogoCtrl" Source="logo.riv" Width="320" Height="150" Stretch="Uniform">
        <noesis:RiveInput InputName="Hover" InputValue="1"/>
    </noesis:RiveControl>
</Grid>
)";
    const AeroCompileResult xamlRes = compiler.Compile(xamlIn);
    AERO_P15_CHECK(xamlRes.succeeded, "XAML containing <noesis:RiveControl Source='logo.riv'> compiles cleanly");

    auto rootGrid = CreateElementByTypeName(xamlRes.axbPackage.rootTypeName);
    AnimationEngine animEngine;
    AERO_P15_CHECK(
        MountAxbTemplate(
            rootGrid.Get(), xamlRes.axbPackage, nullptr, nullptr, ObjectRef{}, nullptr, nullptr, &animEngine),
        "Mounted XAML with RiveControl");
    auto* logoCtrl = rootGrid->FindName("LogoCtrl")
        ? rootGrid->FindName("LogoCtrl")->DynamicCast<RiveControl>()
        : nullptr;
    AERO_P15_CHECK(logoCtrl != nullptr && logoCtrl->IsRiveLoaded(),
                   "FindName locates mounted RiveControl with loaded logo.riv");

    // Verify 100% backward compatibility with Phase 6 JSON payloads ("RIVE{...}" and "{...}")
    const std::string legacyRiveJson = R"(RIVE{
      "artboard": {
        "name": "CompatBoard",
        "width": 200,
        "height": 100,
        "nodes": [
          { "name": "Badge", "type": "Path", "width": 180, "height": 80, "x": 10, "y": 10, "path": "M 10,10 L 190,10 L 190,90 L 10,90 Z", "fill": "#33AAFF" }
        ]
      },
      "clips": [
        {
          "name": "Idle",
          "duration": 1.0,
          "loop": true,
          "tracks": [
            { "target": "Badge", "property": "Opacity", "keyframes": [ { "t": 0.0, "v": "0.5" }, { "t": 1.0, "v": "1.0" } ] }
          ]
        }
      ],
      "stateMachines": [
        {
          "name": "MainSM",
          "inputs": [
            { "name": "Speed", "type": "Float", "default": 0.5, "min": 0.0, "max": 1.0 },
            { "name": "Armed", "type": "Bool", "default": 1.0 }
          ],
          "layers": [
            { "name": "Base", "blendMode": "Override", "weight": 1.0, "states": [ { "name": "IdleState", "kind": "Clip", "clip": "Idle", "loop": true } ] }
          ]
        }
      ]
    })";
    AeroSceneGraphIR jsonIr{};
    std::vector<std::string> jsonDiags;
    const bool jsonOk = importer.ImportToSceneIR(
        "legacy.riv",
        reinterpret_cast<const std::uint8_t*>(legacyRiveJson.data()),
        legacyRiveJson.size(),
        jsonIr,
        jsonDiags);
    AERO_P15_CHECK(jsonOk && jsonDiags.empty() && !jsonIr.vectorShapes.empty() &&
                       jsonIr.loweredPackage.contractInputs.size() == 2u,
                   "RiveFrontendImporter preserves 100% JSON payload compatibility");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestRealRiveV7BinaryImportersAndRiveControl\n");
    }
}

// ============================================================================
// 2. True 3D Perspective Matrix4x4 / CompositeTransform3D & Inverse Homography HitTest
// ============================================================================
void TestTrue3DPerspectiveTransformAndHitTest() {
    const int startFails = gFailureCount;

    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "Menu3DPerspectiveTest.xaml";
    xamlIn.xamlContent = R"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions"
        Width="500" Height="360" Background="#FF10141C">
    <Border x:Name="Card3D" Canvas.Left="100" Canvas.Top="90" Width="200" Height="120"
            CornerRadius="8" Background="#FF2478F0">
        <noesis:Element.Transform3D>
            <noesis:CompositeTransform3D RotationY="35"/>
        </noesis:Element.Transform3D>
    </Border>
    <Grid x:Name="MenuPanel3D" Canvas.Left="330" Canvas.Top="70" Width="140" Height="200">
        <noesis:Element.Transform3D>
            <noesis:CompositeTransform3D RotationY="-28" RotationX="10"/>
        </noesis:Element.Transform3D>
        <Button x:Name="PlayBtn3D" Width="120" Height="44" Content="START" Background="#FF2EB872"/>
    </Grid>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(xamlIn);
    AERO_P15_CHECK(res.succeeded && res.diagnostics.empty(),
                   "XAML with <noesis:Element.Transform3D><noesis:CompositeTransform3D RotationY='35'/> compiles");

    auto rootCanvas = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P15_CHECK(
        MountAxbTemplate(rootCanvas.Get(), res.axbPackage),
        "MountAxbTemplate succeeds for 3D scene");

    rootCanvas->Measure(Size{500.0f, 360.0f});
    rootCanvas->Arrange(Rect{0.0f, 0.0f, 500.0f, 360.0f});

    UIElement* card = rootCanvas->FindName("Card3D");
    UIElement* playBtn = rootCanvas->FindName("PlayBtn3D");
    AERO_P15_CHECK(card != nullptr && playBtn != nullptr, "Located Card3D and PlayBtn3D");
    AERO_P15_CHECK(card->HasActiveTransform3D(), "Card3D has active CompositeTransform3D(RotationY=35)");

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(rootCanvas.Get());

    const RenderNodeDescriptor* cardNode = tree.GetNode(card->RenderNodeId());
    AERO_P15_CHECK(cardNode != nullptr && cardNode->hasPerspective3D,
                   "Card3D RenderNodeDescriptor carries perspective 3D homography");

    const CommandPage& page = tree.GetPage(cardNode->pageIndex);
    const std::size_t vOff = cardNode->vertexOffsetInPage;
    const AeroVertex& v0 = page.vertices[vOff + 0u]; // top-left
    const AeroVertex& v1 = page.vertices[vOff + 1u]; // top-right
    const AeroVertex& v2 = page.vertices[vOff + 2u]; // bottom-right
    const AeroVertex& v3 = page.vertices[vOff + 3u]; // bottom-left

    const float leftEdgeH = v3.y - v0.y;
    const float rightEdgeH = v2.y - v1.y;
    const float projWidthTop = v1.x - v0.x;

    std::printf(
        "  [3D Quad] v0=(%.2f,%.2f) v1=(%.2f,%.2f) v2=(%.2f,%.2f) v3=(%.2f,%.2f) | leftH=%.2f rightH=%.2f topW=%.2f\n",
        v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, leftEdgeH, rightEdgeH, projWidthTop);

    // RotationY = +35 deg: one vertical edge approaches camera (> 120px) and the other recedes (< 120px)
    AERO_P15_CHECK(std::fabs(leftEdgeH - rightEdgeH) > 8.0f,
                   "CompositeTransform3D(RotationY=35) produces non-parallel trapezoid vertical edges");
    AERO_P15_CHECK((leftEdgeH > 120.5f && rightEdgeH < 119.5f) || (rightEdgeH > 120.5f && leftEdgeH < 119.5f),
                   "Near vertical edge expands >120px and far vertical edge shrinks <120px (true perspective)");
    AERO_P15_CHECK(projWidthTop > 120.0f && projWidthTop < 195.0f,
                   "Horizontal span foreshortens under 35-degree Y rotation");

    // Verify exact inverse homography ScreenToLocal round-trip & HitTest across the trapezoid
    const Matrix4x4 worldH = card->ComputeLocalToScreenMatrix3D();
    const Point testLocalPts[] = {
        Point{108.0f, 98.0f},   // near top-left corner
        Point{200.0f, 150.0f},  // exact center
        Point{292.0f, 100.0f},  // near top-right corner
        Point{292.0f, 202.0f},  // near bottom-right corner
        Point{108.0f, 202.0f},  // near bottom-left corner
    };
    for (const Point& origLocal : testLocalPts) {
        const Point screenPt = worldH.ProjectPoint2D(origLocal);
        const Point recoveredLocal = card->ScreenToLocal(screenPt);
        const float err = std::hypot(recoveredLocal.x - origLocal.x, recoveredLocal.y - origLocal.y);
        AERO_P15_CHECK(err < 0.05f, "ScreenToLocal inverse homography matches ProjectPoint2D within 0.05px");
        UIElement* hit = rootCanvas->HitTest(screenPt);
        AERO_P15_CHECK(hit == card, "UIElement::HitTest hits Card3D at 3D-projected interior point");
    }

    // Critical trapezoid slope test: pick a point near the shorter (far) edge's X coordinate whose Y is
    // inside the original unrotated [90..210] rect (and inside the 3D AABB!) yet ABOVE the shrunken far top edge!
    const bool rightIsFar = (rightEdgeH < leftEdgeH);
    const float farTopX = rightIsFar ? (v1.x - 2.0f) : (v0.x + 2.0f);
    const float farTopY = rightIsFar ? v1.y : v0.y; // > 90.0f because far edge shrunk inward!
    AERO_P15_CHECK(farTopY > 91.5f, "Far top corner Y shifted downward (> 91.5) due to perspective foreshortening");
    const Point outsideTrapezoidSlopePt{farTopX, farTopY - 2.5f}; // Y is > 90 (inside 2D rect) & inside AABB, but outside trapezoid!
    UIElement* slopeHit = rootCanvas->HitTest(outsideTrapezoidSlopePt);
    AERO_P15_CHECK(slopeHit != card,
                   "Inverse homography HitTest accurately rejects point inside 2D rect/AABB but outside 3D trapezoid slope");

    // Verify child Button inside 3D-rotated parent MenuPanel3D hits accurately at its 3D projected center
    const Rect btnRect = playBtn->FinalRect();
    const Matrix4x4 btnWorldH = playBtn->ComputeLocalToScreenMatrix3D();
    const Point btnScreenCenter = btnWorldH.ProjectPoint2D(
        Point{btnRect.x + btnRect.width * 0.5f, btnRect.y + btnRect.height * 0.5f});
    AERO_P15_CHECK(rootCanvas->HitTest(btnScreenCenter) == playBtn,
                   "Child Button inside 3D-rotated parent Grid hits accurately via inherited 3D homography");

    // Verify software rasterizer renders perspective trapezoid (pixels inside trapezoid colored, outside slope clear)
    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 500u, 360u, rgba, Color{0.06f, 0.08f, 0.11f, 1.0f});
    const Point insideFarCorner = worldH.ProjectPoint2D(
        Point{rightIsFar ? 292.0f : 108.0f, 150.0f});
    const RgbaSample insidePx = SamplePixel(
        rgba, 500u, static_cast<std::uint32_t>(insideFarCorner.x), static_cast<std::uint32_t>(insideFarCorner.y));
    const RgbaSample outsideSlopePx = SamplePixel(
        rgba,
        500u,
        static_cast<std::uint32_t>(outsideTrapezoidSlopePt.x),
        static_cast<std::uint32_t>(outsideTrapezoidSlopePt.y));
    AERO_P15_CHECK(insidePx.b > 180u && outsideSlopePx.b < 60u,
                   "RasterizeTreeToRgba8 rasterizes exact 3D trapezoid edge without affine parallelogram bleed");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestTrue3DPerspectiveTransformAndHitTest\n");
    }
}

// ============================================================================
// 3. DropShadowEffect & BlurEffect XAML Parsing + Offscreen / SDF Rasterization
// ============================================================================
void TestDropShadowAndBlurEffectsRasterization() {
    const int startFails = gFailureCount;

    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "EffectsTest.xaml";
    xamlIn.xamlContent = R"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="380" Height="240">
    <Canvas.Resources>
        <BlurEffect x:Key="SharedBlur" Radius="12"/>
    </Canvas.Resources>

    <Border x:Name="ShadowCard" Canvas.Left="40" Canvas.Top="30" Width="110" Height="70"
            CornerRadius="8" Background="#FF1E88E5">
        <Border.Effect>
            <DropShadowEffect BlurRadius="14" ShadowDepth="14" Direction="270" Color="#FF000000" Opacity="0.75"/>
        </Border.Effect>
    </Border>

    <Border x:Name="BlurCard" Canvas.Left="210" Canvas.Top="40" Width="100" Height="70"
            Background="#FFE53935">
        <Border.Effect>
            <BlurEffect Radius="10"/>
        </Border.Effect>
    </Border>

    <Border x:Name="StaticResBlurCard" Canvas.Left="210" Canvas.Top="150" Width="100" Height="50"
            Background="#FF00C853" Effect="{StaticResource SharedBlur}"/>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(xamlIn);
    AERO_P15_CHECK(res.succeeded && res.diagnostics.empty(),
                   "XAML with DropShadowEffect, BlurEffect, and ResourceDictionary BlurEffect compiles cleanly");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P15_CHECK(MountAxbTemplate(root.Get(), res.axbPackage),
                   "Mounted XAML Effects scene");

    UIElement* shadowCard = root->FindName("ShadowCard");
    UIElement* blurCard = root->FindName("BlurCard");
    UIElement* resBlurCard = root->FindName("StaticResBlurCard");
    AERO_P15_CHECK(shadowCard != nullptr && blurCard != nullptr && resBlurCard != nullptr,
                   "Found ShadowCard, BlurCard, and StaticResBlurCard");
    AERO_P15_CHECK(shadowCard->GetEffectKind() == UIEffectKind::DropShadow,
                   "ShadowCard carries UIEffectKind::DropShadow");
    AERO_P15_CHECK(blurCard->GetEffectKind() == UIEffectKind::Blur &&
                       std::fabs(blurCard->GetEffectDescriptor().blurRadius - 10.0f) < 0.01f,
                   "BlurCard carries UIEffectKind::Blur(Radius=10)");
    AERO_P15_CHECK(resBlurCard->GetEffectKind() == UIEffectKind::Blur &&
                       std::fabs(resBlurCard->GetEffectDescriptor().blurRadius - 12.0f) < 0.01f,
                   "StaticResBlurCard resolves SharedBlur(Radius=12) from ResourceDictionary");

    root->Measure(Size{380.0f, 240.0f});
    root->Arrange(Rect{0.0f, 0.0f, 380.0f, 240.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    AERO_P15_CHECK(tree.BlurLayerSpans().size() == 2u,
                   "PagedRenderTree records BlurLayerSpans for both BlurEffect elements");

    std::vector<std::uint8_t> rgba;
    const Color lightBg{0.94f, 0.95f, 0.97f, 1.0f}; // ~ (240, 242, 247)
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 380u, 240u, rgba, lightBg);

    const RgbaSample bgSample = SamplePixel(rgba, 380u, 5u, 5u);

    // 1. Verify DropShadowEffect below ShadowCard (y=30..100, Direction=270 shifts shadow down to y=44..114 + feather)
    const RgbaSample cardCenter = SamplePixel(rgba, 380u, 95u, 65u);
    const RgbaSample shadowCore = SamplePixel(rgba, 380u, 95u, 105u);  // 5px below card bottom, inside shadow core
    const RgbaSample shadowEdge = SamplePixel(rgba, 380u, 95u, 114u);  // on shifted shadow boundary (50% falloff)
    const RgbaSample shadowTail = SamplePixel(rgba, 380u, 95u, 120u);  // outer soft feather tail

    std::printf(
        "  [DropShadow] bgR=%u card=(%u,%u,%u) shCoreR=%u shEdgeR=%u shTailR=%u\n",
        bgSample.r, cardCenter.r, cardCenter.g, cardCenter.b,
        shadowCore.r, shadowEdge.r, shadowTail.r);

    AERO_P15_CHECK(cardCenter.b > 200u && cardCenter.r < 60u,
                   "ShadowCard foreground renders crisp blue above its pre-emitted shadow");
    AERO_P15_CHECK(shadowCore.r < bgSample.r - 80 && shadowCore.r > 20u,
                   "DropShadowEffect renders semi-transparent darkened shadow below card");
    AERO_P15_CHECK(shadowCore.r < shadowEdge.r && shadowEdge.r < shadowTail.r && shadowTail.r <= bgSample.r,
                   "DropShadowEffect alpha decays smoothly across feather band (core -> edge -> tail -> bg)");

    // 2. Verify BlurEffect on BlurCard (x=210..310, y=40..110, #E53935 red over light gray bg -> G/B channels dip where red blurs!)
    const RgbaSample blurFarOut = SamplePixel(rgba, 380u, 192u, 75u);  // 18px left of x=210
    const RgbaSample blurHalo   = SamplePixel(rgba, 380u, 205u, 75u);  // 5px OUTSIDE original x=210 edge!
    const RgbaSample blurBorder = SamplePixel(rgba, 380u, 210u, 75u);  // right on x=210 edge (~50% blend)
    const RgbaSample blurInner  = SamplePixel(rgba, 380u, 216u, 75u);  // 6px inside x=210 edge
    const RgbaSample blurCenter = SamplePixel(rgba, 380u, 260u, 75u);  // deep center

    std::printf(
        "  [BlurEffect] G-channel across x=210 edge: farOut=%u -> halo(x=205)=%u -> edge(x=210)=%u -> inner(x=216)=%u -> center=%u\n",
        blurFarOut.g, blurHalo.g, blurBorder.g, blurInner.g, blurCenter.g);

    AERO_P15_CHECK(blurHalo.g < bgSample.g - 15 && blurHalo.g > blurBorder.g,
                   "BlurEffect bleeds smooth semi-transparent halo outside original unblurred rect boundary");
    AERO_P15_CHECK(blurFarOut.g >= blurHalo.g && blurHalo.g > blurBorder.g &&
                       blurBorder.g > blurInner.g && blurInner.g > blurCenter.g,
                   "BlurEffect produces monotonic Gaussian-like transition across element boundary");

    // 3. Verify StaticResource BlurEffect also blurred across its top/left boundary (x=210, y=150..200)
    const RgbaSample resHalo = SamplePixel(rgba, 380u, 204u, 175u);
    const RgbaSample resCenter = SamplePixel(rgba, 380u, 260u, 175u);
    AERO_P15_CHECK(resHalo.r < bgSample.r - 15 && resHalo.r > resCenter.r,
                   "StaticResource SharedBlur produces smooth blurred halo outside StaticResBlurCard");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestDropShadowAndBlurEffectsRasterization\n");
    }
}

} // namespace

int main() {
    EnsureBuiltinTypesRegistered();
    EnsureControlTypesRegistered();

    std::printf("=== Running AeroGUI 2.0 P1-5 Acceptance Suite ===\n");
    TestPropertyIdExclusiveRange();
    TestRealRiveV7BinaryImportersAndRiveControl();
    TestTrue3DPerspectiveTransformAndHitTest();
    TestDropShadowAndBlurEffectsRasterization();

    if (gFailureCount != 0) {
        std::fprintf(stderr, "=== P1-5 TESTS FAILED (%d failures) ===\n", gFailureCount);
        return 1;
    }
    std::printf("=== ALL P1-5 TESTS PASSED 100%% ===\n");
    return 0;
}
