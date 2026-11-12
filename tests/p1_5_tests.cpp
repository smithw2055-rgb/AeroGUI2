#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

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
std::filesystem::path ResolveSamplesDir() {
    std::error_code ec;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        const std::filesystem::path p(envDir);
        if (std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
#ifdef AERO_SAMPLES_DIR
    {
        const std::filesystem::path p(AERO_SAMPLES_DIR);
        if (!p.empty() && std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
#endif
    constexpr const char* kCandidates[] = {
        "Samples",
        "samples",
        "../Samples",
        "../samples",
        "../../Samples",
        "../../samples",
        "../../../Samples",
        "../../../samples",
    };
    for (const char* cand : kCandidates) {
        const std::filesystem::path p(cand);
        if (std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
    return std::filesystem::path("Samples");
}

void TestRealRiveV7BinaryImportersAndRiveControl() {
    const int startFails = gFailureCount;
    const std::filesystem::path riveDirPath = ResolveSamplesDir() / "Rive" / "Data";
    const std::string riveDir = riveDirPath.string();
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
        const std::string fullPath = (riveDirPath / fileName).string();
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

// ============================================================================
// 4. Regression: Templated Label Layout Delegation & Literal Content Text
// ============================================================================
void TestTemplatedLabelLayoutAndLiteralContent() {
    const int startFails = gFailureCount;

    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "TemplatedLabelRegression.xaml";
    xamlIn.xamlContent = R"(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            xmlns:noesis="clr-namespace:NoesisGUIExtensions"
            Width="200">
    <StackPanel.Resources>
        <ControlTemplate x:Key="LblTpl" TargetType="{x:Type Label}">
            <Grid>
                <TextBlock TextWrapping="Wrap" FontSize="16"
                           noesis:RichText.Text="{TemplateBinding Content}"/>
            </Grid>
        </ControlTemplate>
        <Style TargetType="{x:Type Label}">
            <Setter Property="Template" Value="{StaticResource LblTpl}"/>
        </Style>
    </StackPanel.Resources>
    <Label x:Name="WrappedLbl" Width="70"
           Content="[b]Alpha[/b] Beta Gamma Delta Epsilon Zeta Eta"/>
</StackPanel>
)";
    const AeroCompileResult res = compiler.Compile(xamlIn);
    AERO_P15_CHECK(res.succeeded && res.diagnostics.empty(),
                   "Templated Label XAML compiles cleanly");

    AnimationEngine animEngine;
    StyleTriggerEngine styleEngine(&animEngine, nullptr);
    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P15_CHECK(MountAxbTemplate(root.Get(), res.axbPackage, nullptr, nullptr, ObjectRef{}, nullptr, nullptr, &animEngine),
                   "Mounted TemplatedLabelRegression scene");

    styleEngine.RegisterPackageStyles(res.axbPackage);
    (void)styleEngine.BindSubtreeStyles(root.Get(), true);
    (void)styleEngine.EvaluateAndApply();

    UIElement* wrappedLbl = root->FindName("WrappedLbl");
    AERO_P15_CHECK(wrappedLbl != nullptr && wrappedLbl->HasControlTemplate(),
                   "WrappedLbl mounted its ControlTemplate via implicit Label Style");

    root->Measure(Size{200.0f, 400.0f});
    root->Arrange(Rect{0.0f, 0.0f, 200.0f, 400.0f});
    AERO_P15_CHECK(wrappedLbl != nullptr && wrappedLbl->DesiredSize().height > 40.0f,
                   "Templated Label delegates MeasureCore to wrapped template child instead of single-line self");

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    const RenderNodeDescriptor* lblAnchor = wrappedLbl ? tree.GetNode(wrappedLbl->RenderNodeId()) : nullptr;
    AERO_P15_CHECK(wrappedLbl != nullptr && wrappedLbl->RenderNodeSpan() == 1u &&
                       lblAnchor != nullptr && lblAnchor->opacity == 0.0f &&
                       lblAnchor->primType != AeroPrimType::SdfTextGlyph,
                   "Templated Label emits only a 0-opacity anchor node and never double-renders SdfTextGlyph");

    // Untemplated Label with literal bracket text in Content must NOT strip BBCode tags
    auto plainLbl = MakeIntrusive<Label>();
    plainLbl->SetValue(PropId_Content, StringValue(std::string_view("[b]LiteralBracket[/b]")));
    AERO_P15_CHECK(plainLbl->GetText() == "[b]LiteralBracket[/b]",
                   "TextBlock::GetText preserves PropId_Content verbatim when PropId_RichText is unset");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestTemplatedLabelLayoutAndLiteralContent\n");
    }
}

// ============================================================================
// 5. Regression: RichText sourceCodePointIndex Style Alignment & Reactive [bind]
// ============================================================================
void TestRichTextGlyphStyleAlignmentAndReactiveBind() {
    const int startFails = gFailureCount;

    const char* rsCode = R"(
module Game.RichRegression;

public class RichVm
{
    [Observable] public int SoundLevel;
}
)";

    const char* xamlCode = R"(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            xmlns:noesis="clr-namespace:NoesisGUIExtensions"
            x:DataType="Game.RichRegression::RichVm"
            Width="220">
    <TextBlock x:Name="StyledWrapTb" Width="64" FontSize="16" TextWrapping="Wrap"
               Foreground="#FFFFFFFF"
               noesis:RichText.Text="Hello   [br/][color=#FF00FF00]World[/color]"/>
    <TextBlock x:Name="BoundRichTb" FontSize="16"
               noesis:RichText.Text="Vol: [bind format='{0:0}%']SoundLevel[/bind]"/>
</StackPanel>
)";

    AeroCompiler compiler;
    AeroCompileInput in{};
    in.xamlPath = "RichTextRegression.xaml";
    in.xamlContent = xamlCode;
    in.scriptSources.push_back({"RichVm.rs", rsCode});

    const AeroCompileResult compiled = compiler.Compile(in);
    AERO_P15_CHECK(compiled.succeeded && compiled.programImage != nullptr,
                   "RichText + RealScript ViewModel XAML compiled cleanly");
    if (!compiled.succeeded || !compiled.programImage) {
        return;
    }

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    auto uiHeapPtr = std::shared_ptr<ManagedHeap>(&domains.uiHeap(), [](ManagedHeap*) {});
    Interpreter uiInterp(compiled.programImage, uiHeapPtr);
    uiInterp.setExecutionDomainContext(&domains, DomainKind::Ui);

    const SymbolId vmTypeId = rs_sem::stableTypeId("Game.RichRegression::RichVm");
    const auto* vmTypeSym = compiled.programImage->resolveType(vmTypeId);
    auto vmObjOpt = domains.uiHeap().allocateObject(
        vmTypeId, std::vector<UnifiedValue>(vmTypeSym ? vmTypeSym->fields.size() : 1u), {});
    AERO_P15_CHECK(vmObjOpt.has_value(), "Allocated RichVm on UiHeap");
    const ObjectRef vmObj = *vmObjOpt;

    auto soundSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "SoundLevel");
    AERO_P15_CHECK(soundSlot.has_value(), "Resolved RichVm::SoundLevel slot");
    (void)domains.writePropertySlotFast(DomainKind::Ui, vmObj, *soundSlot, value::FromInt(25), &uiInterp);

    DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterp);
    auto root = CreateElementByTypeName(compiled.axbPackage.rootTypeName);
    root->SetValue(PropId_DataContext, vmObj, ValueSourceRank::Local);
    AERO_P15_CHECK(
        MountAxbTemplate(root.Get(), compiled.axbPackage, &domains, &bindingEngine, vmObj, &uiInterp),
        "Mounted RichTextRegression scene");

    auto* styledTb = root->FindName("StyledWrapTb")
        ? root->FindName("StyledWrapTb")->DynamicCast<TextBlock>()
        : nullptr;
    auto* boundTb = root->FindName("BoundRichTb")
        ? root->FindName("BoundRichTb")->DynamicCast<TextBlock>()
        : nullptr;
    AERO_P15_CHECK(styledTb != nullptr && boundTb != nullptr, "Located StyledWrapTb and BoundRichTb");

    root->Measure(Size{220.0f, 300.0f});
    root->Arrange(Rect{0.0f, 0.0f, 220.0f, 300.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());

    // 1. Verify wrapped spaces + [br/] do NOT shift RichGlyphStyle colors on "World"
    AERO_P15_CHECK(styledTb->RenderNodeSpan() == 1u,
                   "StyledWrapTb emits batched SdfTextGlyph chunk node");
    const std::uint32_t whitePacked = PackColorRgba8(Color{1.0f, 1.0f, 1.0f, 1.0f});
    const std::uint32_t greenPacked = PackColorRgba8(Color{0.0f, 1.0f, 0.0f, 1.0f});
    auto verifyGlyphColors = [&]() -> bool {
        const RenderNodeDescriptor* nd = tree.GetNode(styledTb->RenderNodeId());
        if (!nd || nd->primType != AeroPrimType::SdfTextGlyph || nd->vertexCount < 40u) {
            return false;
        }
        const CommandPage& pg = tree.GetPage(nd->pageIndex);
        for (std::size_t q = 0u; q < 10u; ++q) {
            const std::uint32_t expected = (q < 5u) ? whitePacked : greenPacked;
            if (pg.vertices[nd->vertexOffsetInPage + q * 4u].rgba != expected) {
                return false;
            }
        }
        return true;
    };
    AERO_P15_CHECK(verifyGlyphColors(),
                   "Initial build aligns RichGlyphStyle via sourceCodePointIndex ('Hello'=white, 'World'=green)");

    styledTb->InvalidateRender();
    AERO_P15_CHECK(tree.UpdateDirtyElement(styledTb) && verifyGlyphColors(),
                   "UpdateDirtyElement preserves sourceCodePointIndex RichGlyphStyle alignment");

    // 2. Verify reactive [bind] resolves real DataContext property and invalidates on Track A flush
    AERO_P15_CHECK(boundTb->GetText() == "Vol: 25%",
                   "RichText [bind] resolves live SoundLevel=25 from DataContext");
    boundTb->ClearLayoutDirtyFlags();
    (void)domains.writePropertySlotFast(DomainKind::Ui, vmObj, *soundSlot, value::FromInt(80), &uiInterp);
    AERO_P15_CHECK(bindingEngine.HasPendingDirtyObjects(),
                   "HasPendingDirtyObjects detects dirty RichText [bind] dependent");
    (void)bindingEngine.FlushTrackA();
    AERO_P15_CHECK(boundTb->GetText() == "Vol: 80%" &&
                       (boundTb->GetLayoutDirtyFlags() & RenderDirty) != 0u,
                   "FlushTrackA reactively updates RichText [bind] to 'Vol: 80%' without recursion");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestRichTextGlyphStyleAlignmentAndReactiveBind\n");
    }
}

// ============================================================================
// 6. Regression: BackdropBlur Foreground Preservation & GL Interleaved Execution
// ============================================================================
struct MockBlurGlSpy {
    AeroGLuint nextId = 1u;
    std::size_t readPixelsCalls = 0u;
    std::size_t texImage2DCalls = 0u;
    std::size_t texSubImage2DCalls = 0u;
    std::size_t drawElementsCalls = 0u;
    AeroGLenum lastBlendSrcRgb = 0u;
    AeroGLenum lastBlendDstRgb = 0u;
    AeroGLenum lastBlendSrcAlpha = 0u;
    AeroGLenum lastBlendDstAlpha = 0u;
};

MockBlurGlSpy* gBlurGlSpy = nullptr;

void AERO_GL_APIENTRY MockBlurViewport(AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei) {}
void AERO_GL_APIENTRY MockBlurScissor(AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei) {}
void AERO_GL_APIENTRY MockBlurEnable(AeroGLenum) {}
void AERO_GL_APIENTRY MockBlurDisable(AeroGLenum) {}
void AERO_GL_APIENTRY MockBlurBlendFuncSeparate(
    AeroGLenum srcRgb, AeroGLenum dstRgb, AeroGLenum srcA, AeroGLenum dstA) {
    gBlurGlSpy->lastBlendSrcRgb = srcRgb;
    gBlurGlSpy->lastBlendDstRgb = dstRgb;
    gBlurGlSpy->lastBlendSrcAlpha = srcA;
    gBlurGlSpy->lastBlendDstAlpha = dstA;
}
void AERO_GL_APIENTRY MockBlurClearColor(AeroGLfloat, AeroGLfloat, AeroGLfloat, AeroGLfloat) {}
void AERO_GL_APIENTRY MockBlurClear(AeroGLbitfield) {}
void AERO_GL_APIENTRY MockBlurGenIds(AeroGLsizei n, AeroGLuint* out) {
    for (AeroGLsizei i = 0; i < n; ++i) out[i] = gBlurGlSpy->nextId++;
}
void AERO_GL_APIENTRY MockBlurDeleteIds(AeroGLsizei, const AeroGLuint*) {}
void AERO_GL_APIENTRY MockBlurBindBuffer(AeroGLenum, AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurBufferData(AeroGLenum, AeroGLsizeiptr, const void*, AeroGLenum) {}
void AERO_GL_APIENTRY MockBlurBufferSubData(AeroGLenum, AeroGLintptr, AeroGLsizeiptr, const void*) {}
void AERO_GL_APIENTRY MockBlurBindVertexArray(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurEnableVertexAttribArray(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurVertexAttribPointer(
    AeroGLuint, AeroGLint, AeroGLenum, AeroGLboolean, AeroGLsizei, const void*) {}
void AERO_GL_APIENTRY MockBlurVertexAttribIPointer(
    AeroGLuint, AeroGLint, AeroGLenum, AeroGLsizei, const void*) {}
AeroGLuint AERO_GL_APIENTRY MockBlurCreateShader(AeroGLenum) { return gBlurGlSpy->nextId++; }
void AERO_GL_APIENTRY MockBlurDeleteShader(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurShaderSource(
    AeroGLuint, AeroGLsizei, const AeroGLchar* const*, const AeroGLint*) {}
void AERO_GL_APIENTRY MockBlurCompileShader(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurGetShaderiv(AeroGLuint, AeroGLenum pname, AeroGLint* params) {
    if (params) *params = (pname == 0x8B81u) ? 1 : 0;
}
void AERO_GL_APIENTRY MockBlurGetShaderInfoLog(
    AeroGLuint, AeroGLsizei, AeroGLsizei* len, AeroGLchar* log) {
    if (len) *len = 0;
    if (log) log[0] = '\0';
}
AeroGLuint AERO_GL_APIENTRY MockBlurCreateProgram() { return gBlurGlSpy->nextId++; }
void AERO_GL_APIENTRY MockBlurDeleteProgram(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurAttachShader(AeroGLuint, AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurLinkProgram(AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurGetProgramiv(AeroGLuint, AeroGLenum pname, AeroGLint* params) {
    if (params) *params = (pname == 0x8B82u) ? 1 : 0;
}
void AERO_GL_APIENTRY MockBlurGetProgramInfoLog(
    AeroGLuint, AeroGLsizei, AeroGLsizei* len, AeroGLchar* log) {
    if (len) *len = 0;
    if (log) log[0] = '\0';
}
void AERO_GL_APIENTRY MockBlurUseProgram(AeroGLuint) {}
AeroGLint AERO_GL_APIENTRY MockBlurGetUniformLocation(AeroGLuint, const AeroGLchar*) { return 0; }
void AERO_GL_APIENTRY MockBlurUniform1i(AeroGLint, AeroGLint) {}
void AERO_GL_APIENTRY MockBlurUniform2f(AeroGLint, AeroGLfloat, AeroGLfloat) {}
void AERO_GL_APIENTRY MockBlurActiveTexture(AeroGLenum) {}
void AERO_GL_APIENTRY MockBlurBindTexture(AeroGLenum, AeroGLuint) {}
void AERO_GL_APIENTRY MockBlurTexImage2D(
    AeroGLenum, AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei, AeroGLint, AeroGLenum, AeroGLenum, const void*) {
    ++gBlurGlSpy->texImage2DCalls;
}
void AERO_GL_APIENTRY MockBlurTexSubImage2D(
    AeroGLenum, AeroGLint, AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei, AeroGLenum, AeroGLenum, const void*) {
    ++gBlurGlSpy->texSubImage2DCalls;
}
void AERO_GL_APIENTRY MockBlurTexParameteri(AeroGLenum, AeroGLenum, AeroGLint) {}
void AERO_GL_APIENTRY MockBlurPixelStorei(AeroGLenum, AeroGLint) {}
void AERO_GL_APIENTRY MockBlurDrawElements(AeroGLenum, AeroGLsizei, AeroGLenum, const void*) {
    ++gBlurGlSpy->drawElementsCalls;
}
void AERO_GL_APIENTRY MockBlurReadPixels(
    AeroGLint, AeroGLint, AeroGLsizei w, AeroGLsizei h, AeroGLenum, AeroGLenum, void* data) {
    ++gBlurGlSpy->readPixelsCalls;
    if (data && w > 0 && h > 0) {
        std::memset(data, 0x80u, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    }
}
AeroGLenum AERO_GL_APIENTRY MockBlurGetError() { return 0u; }
const std::uint8_t* AERO_GL_APIENTRY MockBlurGetString(AeroGLenum) {
    return reinterpret_cast<const std::uint8_t*>("MockBlurGL 3.3");
}

GlFunctionTable MakeMockBlurGlTable() noexcept {
    GlFunctionTable t{};
    t.Viewport = &MockBlurViewport;
    t.Scissor = &MockBlurScissor;
    t.Enable = &MockBlurEnable;
    t.Disable = &MockBlurDisable;
    t.BlendFuncSeparate = &MockBlurBlendFuncSeparate;
    t.ClearColor = &MockBlurClearColor;
    t.Clear = &MockBlurClear;
    t.GenBuffers = &MockBlurGenIds;
    t.DeleteBuffers = &MockBlurDeleteIds;
    t.BindBuffer = &MockBlurBindBuffer;
    t.BufferData = &MockBlurBufferData;
    t.BufferSubData = &MockBlurBufferSubData;
    t.GenVertexArrays = &MockBlurGenIds;
    t.DeleteVertexArrays = &MockBlurDeleteIds;
    t.BindVertexArray = &MockBlurBindVertexArray;
    t.EnableVertexAttribArray = &MockBlurEnableVertexAttribArray;
    t.VertexAttribPointer = &MockBlurVertexAttribPointer;
    t.VertexAttribIPointer = &MockBlurVertexAttribIPointer;
    t.CreateShader = &MockBlurCreateShader;
    t.DeleteShader = &MockBlurDeleteShader;
    t.ShaderSource = &MockBlurShaderSource;
    t.CompileShader = &MockBlurCompileShader;
    t.GetShaderiv = &MockBlurGetShaderiv;
    t.GetShaderInfoLog = &MockBlurGetShaderInfoLog;
    t.CreateProgram = &MockBlurCreateProgram;
    t.DeleteProgram = &MockBlurDeleteProgram;
    t.AttachShader = &MockBlurAttachShader;
    t.LinkProgram = &MockBlurLinkProgram;
    t.GetProgramiv = &MockBlurGetProgramiv;
    t.GetProgramInfoLog = &MockBlurGetProgramInfoLog;
    t.UseProgram = &MockBlurUseProgram;
    t.GetUniformLocation = &MockBlurGetUniformLocation;
    t.Uniform1i = &MockBlurUniform1i;
    t.Uniform2f = &MockBlurUniform2f;
    t.GenTextures = &MockBlurGenIds;
    t.DeleteTextures = &MockBlurDeleteIds;
    t.ActiveTexture = &MockBlurActiveTexture;
    t.BindTexture = &MockBlurBindTexture;
    t.TexImage2D = &MockBlurTexImage2D;
    t.TexSubImage2D = &MockBlurTexSubImage2D;
    t.TexParameteri = &MockBlurTexParameteri;
    t.PixelStorei = &MockBlurPixelStorei;
    t.DrawElements = &MockBlurDrawElements;
    t.ReadPixels = &MockBlurReadPixels;
    t.GetError = &MockBlurGetError;
    t.GetString = &MockBlurGetString;
    return t;
}

void TestBackdropBlurForegroundAndGlInterleavedExecution() {
    const int startFails = gFailureCount;

    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "BackdropBlurRegression.xaml";
    xamlIn.xamlContent = R"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions"
        Width="200" Height="120">
    <Border Canvas.Left="0" Canvas.Top="0" Width="100" Height="120" Background="#FFFF0000"/>
    <Border Canvas.Left="100" Canvas.Top="0" Width="100" Height="120" Background="#FF0000FF"/>
    <Border x:Name="GlassPanel" Canvas.Left="60" Canvas.Top="20" Width="80" Height="80"
            Background="#6600FF00">
        <b:Interaction.Behaviors>
            <noesis:BackgroundEffectBehavior>
                <BlurEffect Radius="12"/>
            </noesis:BackgroundEffectBehavior>
        </b:Interaction.Behaviors>
        <Border x:Name="CrispChild" Width="24" Height="24"
                HorizontalAlignment="Center" VerticalAlignment="Center"
                Background="#FFFFFFFF"/>
    </Border>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(xamlIn);
    AERO_P15_CHECK(res.succeeded && res.diagnostics.empty(),
                   "BackdropBlur BackgroundEffectBehavior XAML compiles cleanly");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P15_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted BackdropBlur scene");

    UIElement* glassPanel = root->FindName("GlassPanel");
    AERO_P15_CHECK(glassPanel != nullptr && glassPanel->GetEffectKind() == UIEffectKind::BackdropBlur,
                   "GlassPanel carries UIEffectKind::BackdropBlur");

    root->Measure(Size{200.0f, 120.0f});
    root->Arrange(Rect{0.0f, 0.0f, 200.0f, 120.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    AERO_P15_CHECK(tree.BlurLayerSpans().size() == 1u &&
                       tree.BlurLayerSpans()[0].effectMode == UIEffectKind::BackdropBlur,
                   "PagedRenderTree records BackdropBlur BlurLayerSpan");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 120u, rgba, Color{0.0f, 0.0f, 0.0f, 1.0f});

    const RgbaSample unblurredLeft = SamplePixel(rgba, 200u, 96u, 8u);
    const RgbaSample unblurredRight = SamplePixel(rgba, 200u, 104u, 8u);
    const RgbaSample frostedSeam = SamplePixel(rgba, 200u, 99u, 28u);
    const RgbaSample crispChildPx = SamplePixel(rgba, 200u, 100u, 60u);

    AERO_P15_CHECK(unblurredLeft.r > 240u && unblurredLeft.b < 10u &&
                       unblurredRight.b > 240u && unblurredRight.r < 10u,
                   "Outside GlassPanel, red/blue background split remains sharp");
    AERO_P15_CHECK(frostedSeam.r > 35u && frostedSeam.b > 35u && frostedSeam.g > 80u,
                   "Inside GlassPanel, BackdropBlur blurs red/blue backdrop AND composites GlassPanel #6600FF00 tint");
    AERO_P15_CHECK(crispChildPx.r > 245u && crispChildPx.g > 245u && crispChildPx.b > 245u,
                   "BackdropBlur renders crisp foreground child [firstNodeId..endNodeId) on top of frosted backdrop");

    // Verify OpenGlDeviceBackend interleaved BackdropBlur execution & end-of-frame blend restoration
    MockBlurGlSpy spy{};
    gBlurGlSpy = &spy;
    OpenGlDeviceBackend glBackend(MakeMockBlurGlTable());
    GpuBackendConfig cfg{};
    cfg.initialViewportWidth = 200u;
    cfg.initialViewportHeight = 120u;
    AERO_P15_CHECK(glBackend.Initialize(cfg), "Initialized mock OpenGlDeviceBackend");
    spy.readPixelsCalls = 0u;
    spy.texImage2DCalls = 0u;
    spy.texSubImage2DCalls = 0u;
    spy.drawElementsCalls = 0u;

    ZeroCopyGpuStream stream(ShaderBackend::DesktopGL33);
    const GpuBackendFrameStats stats = glBackend.SubmitAndPresent(tree, stream, 200u, 120u);
    AERO_P15_CHECK(stats.succeeded, "OpenGlDeviceBackend::SubmitAndPresent succeeded on BackdropBlur scene");
    AERO_P15_CHECK(spy.readPixelsCalls >= 1u &&
                       (spy.texImage2DCalls + spy.texSubImage2DCalls) >= 1u &&
                       spy.drawElementsCalls >= 3u,
                   "OpenGlDeviceBackend captures backdrop via glReadPixels, uploads blurred patch, and draws foreground");
    // GL_ONE = 1 (premul UberShader output), GL_ONE_MINUS_SRC_ALPHA = 0x0303 (P2-2 Normal blend mode restored)
    AERO_P15_CHECK(spy.lastBlendSrcRgb == 1u && spy.lastBlendDstRgb == 0x0303u,
                   "OpenGlDeviceBackend restores BlendingMode::Normal at end of frame");
    glBackend.Shutdown();
    gBlurGlSpy = nullptr;

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestBackdropBlurForegroundAndGlInterleavedExecution\n");
    }
}

// ============================================================================
// 7. Regression: <Path> GradientBrush In-Place Update & WebGL2/WGSL UberShader
// ============================================================================
void TestVectorPathGradientInPlaceUpdateAndShaders() {
    const int startFails = gFailureCount;

    const std::string_view webgl2Ps = ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::WebGL2);
    const std::string_view wgslPs = ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::WebGPU_WGSL);
    const std::size_t webgl2Prim4 = webgl2Ps.find("v_PrimType == 4u");
    const std::size_t webgl2Prim5 = webgl2Ps.find("v_PrimType == 5u");
    AERO_P15_CHECK(
        webgl2Prim4 != std::string_view::npos && webgl2Prim5 != std::string_view::npos &&
            webgl2Ps.substr(webgl2Prim4, webgl2Prim5 - webgl2Prim4).find("sampleAtlasGradientBrush") !=
                std::string_view::npos,
        "WebGL2 UberShader MsdfVectorPath (PrimType == 4) samples sampleAtlasGradientBrush");

    const std::size_t wgslPrim4 = wgslPs.find("in.primType == 4u");
    const std::size_t wgslPrim5 = wgslPs.find("in.primType == 5u");
    AERO_P15_CHECK(
        wgslPrim4 != std::string_view::npos && wgslPrim5 != std::string_view::npos &&
            wgslPs.substr(wgslPrim4, wgslPrim5 - wgslPrim4).find("sampleAtlasGradientBrush") !=
                std::string_view::npos,
        "WebGPU WGSL UberShader MsdfVectorPath (primType == 4) samples sampleAtlasGradientBrush");

    AeroCompiler compiler;
    AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "PathGradientUpdateRegression.xaml";
    xamlIn.xamlContent = R"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Width="160" Height="100">
    <Path x:Name="GradPath" Canvas.Left="20" Canvas.Top="15" Width="120" Height="70"
          Stretch="Fill" Data="M 0,0 L 100,0 L 100,60 L 0,60 Z" StrokeThickness="4">
        <Path.Fill>
            <LinearGradientBrush StartPoint="0,0" EndPoint="1,0">
                <GradientStop Offset="0" Color="#FFFF1010"/>
                <GradientStop Offset="1" Color="#FF1060FF"/>
            </LinearGradientBrush>
        </Path.Fill>
        <Path.Stroke>
            <SolidColorBrush Color="#FFFFFFFF"/>
        </Path.Stroke>
    </Path>
</Canvas>
)";
    const AeroCompileResult res = compiler.Compile(xamlIn);
    AERO_P15_CHECK(res.succeeded && res.diagnostics.empty(),
                   "Path with LinearGradientBrush Fill and Stroke compiles cleanly");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_P15_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted PathGradientUpdateRegression");
    UIElement* gradPath = root->FindName("GradPath");
    AERO_P15_CHECK(gradPath != nullptr, "Located GradPath");

    root->Measure(Size{160.0f, 100.0f});
    root->Arrange(Rect{0.0f, 0.0f, 160.0f, 100.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    AERO_P15_CHECK(gradPath->RenderNodeSpan() == 2u,
                   "GradPath allocates separate fill + stroke MTSDF nodes");

    const RenderNodeDescriptor* beforeFill = tree.GetNode(gradPath->RenderNodeId());
    AERO_P15_CHECK(beforeFill != nullptr && beforeFill->gradientBrushId != 0u &&
                       static_cast<std::uint32_t>(beforeFill->encodedPrimExtra >> 4u) ==
                           beforeFill->gradientBrushId,
                   "Initial build encodes gradientBrushId on MsdfVectorPath fill node");

    gradPath->SetOpacity(0.85f);
    AERO_P15_CHECK(tree.UpdateDirtyElement(gradPath),
                   "PagedRenderTree::UpdateDirtyElement succeeds in-place for gradient Path");

    const RenderNodeDescriptor* afterFill = tree.GetNode(gradPath->RenderNodeId());
    AERO_P15_CHECK(afterFill != nullptr && afterFill->gradientBrushId != 0u &&
                       static_cast<std::uint32_t>(afterFill->encodedPrimExtra >> 4u) ==
                           afterFill->gradientBrushId,
                   "UpdateDirtyElement preserves gradientBrushId & encodedPrimExtra on MsdfVectorPath");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 160u, 100u, rgba, Color{0.0f, 0.0f, 0.0f, 1.0f});
    const RgbaSample leftSample = SamplePixel(rgba, 160u, 35u, 50u);
    const RgbaSample rightSample = SamplePixel(rgba, 160u, 125u, 50u);
    AERO_P15_CHECK(leftSample.r > 160u && leftSample.r > leftSample.b + 80 &&
                       rightSample.b > 160u && rightSample.b > rightSample.r + 80,
                   "Rasterized gradient Path retains red->blue ramp after UpdateDirtyElement");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestVectorPathGradientInPlaceUpdateAndShaders\n");
    }
}

// ============================================================================
// 8. Regression: BoxShadow Untinted Guard, Container DropShadow Z-Order & Mirror
//                Offset, ImageBrush Stretch Encoding, and LayoutScaler RefResolution
// ============================================================================
void TestBoxShadowContainerDropShadowAndImageBrushStretch() {
    const int startFails = gFailureCount;

    // 1. BoxShadow without Color must stay UIEffectKind::None (never UIEffectKind::Blur)
    auto untintedBoxShadow = MakeIntrusive<BoxShadow>();
    untintedBoxShadow->SetValue(PropId_EffectBlurRadius, value::FromFloat(14.0f));
    AERO_P15_CHECK(untintedBoxShadow->GetEffectKind() == UIEffectKind::None,
                   "BoxShadow with BlurRadius but no Color stays UIEffectKind::None instead of blurring children");

    // 2. Style TargetType=BoxShadow maps Color -> PropId_EffectColor; unrelated Style does NOT
    AeroCompiler compiler;
    AeroCompileInput styleIn{};
    styleIn.xamlPath = "BoxShadowAndImageBrush.xaml";
    styleIn.xamlContent = R"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions"
        Width="240" Height="140">
    <Canvas.Resources>
        <Style x:Key="ShadowStyle" TargetType="{x:Type noesis:BoxShadow}">
            <Setter Property="Color" Value="#80000000"/>
        </Style>
        <Style x:Key="TbStyle" TargetType="{x:Type TextBlock}">
            <Setter Property="Color" Value="#FFFF0000"/>
        </Style>
        <ImageBrush x:Key="FillBrush" ImageSource="Flag-en.png" Stretch="Fill"/>
        <ImageBrush x:Key="UniformBrush" ImageSource="Flag-en.png" Stretch="Uniform"/>
    </Canvas.Resources>

    <Grid x:Name="MirroredShadowGroup" Canvas.Left="80" Canvas.Top="30" Width="60" Height="60">
        <Grid.Effect>
            <DropShadowEffect Direction="0" ShadowDepth="18" BlurRadius="6" Color="#FF000000" Opacity="0.9"/>
        </Grid.Effect>
        <Border x:Name="ChildA" Width="60" Height="60" Background="#FF2090FF"
                RenderTransformOrigin="0.5,0.5">
            <Border.RenderTransform>
                <ScaleTransform ScaleX="-1" ScaleY="1"/>
            </Border.RenderTransform>
        </Border>
        <Border x:Name="ChildB" Width="30" Height="30" HorizontalAlignment="Left" VerticalAlignment="Top"
                Background="#FFFFD020"/>
    </Grid>
</Canvas>
)";
    const AeroCompileResult styleRes = compiler.Compile(styleIn);
    AERO_P15_CHECK(styleRes.succeeded, "BoxShadow style, ImageBrush Stretch, and mirrored DropShadow XAML compiled");

    bool foundShadowColorSetter = false;
    bool leakedGlobalEffectColorSetter = false;
    for (const auto& st : styleRes.axbPackage.styles) {
        for (const auto& setter : st.setters) {
            if (st.targetTypeName == "BoxShadow" && setter.propId == PropId_EffectColor) {
                foundShadowColorSetter = true;
            }
            if (st.targetTypeName == "TextBlock" && setter.propId == PropId_EffectColor) {
                leakedGlobalEffectColorSetter = true;
            }
        }
    }
    AERO_P15_CHECK(foundShadowColorSetter && !leakedGlobalEffectColorSetter,
                   "Color maps to PropId_EffectColor on BoxShadow Style without polluting global ResolveBuiltinPropertyId");

    // 3. Verify ImageBrush Stretch encoding ("Flag-en.png" for Fill vs "AERO_IMGBRUSH:2:Flag-en.png" for Uniform)
    const UnifiedValue* fillRes = nullptr;
    const UnifiedValue* uniformRes = nullptr;
    for (const auto& r : styleRes.axbPackage.resources) {
        if (r.first == "FillBrush") fillRes = &r.second;
        if (r.first == "UniformBrush") uniformRes = &r.second;
    }
    AERO_P15_CHECK(fillRes != nullptr && std::holds_alternative<StringValue>(*fillRes) &&
                       std::get<StringValue>(*fillRes).view() == "Flag-en.png",
                   "Default Stretch='Fill' ImageBrush stays unprefixed for resource compatibility");
    AERO_P15_CHECK(uniformRes != nullptr && std::holds_alternative<StringValue>(*uniformRes) &&
                       std::get<StringValue>(*uniformRes).view() == "AERO_IMGBRUSH:2:Flag-en.png",
                   "Stretch='Uniform' ImageBrush encodes AERO_IMGBRUSH:2:<uri>");

    auto img = MakeIntrusive<Image>();
    img->SetSource("AERO_IMGBRUSH:2:Flag-en.png");
    AERO_P15_CHECK(img->GetSource() == "Flag-en.png",
                   "Image::GetSource strips AERO_IMGBRUSH:<stretch>: prefix");

    // 4. Verify container DropShadowEffect pre-pass Z-order and world-space +X offset under ScaleX=-1
    auto root = CreateElementByTypeName(styleRes.axbPackage.rootTypeName);
    AERO_P15_CHECK(MountAxbTemplate(root.Get(), styleRes.axbPackage), "Mounted MirroredShadowGroup scene");
    root->Measure(Size{240.0f, 140.0f});
    root->Arrange(Rect{0.0f, 0.0f, 240.0f, 140.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 240u, 140u, rgba, Color{0.94f, 0.95f, 0.97f, 1.0f});
    const RgbaSample bgPx = SamplePixel(rgba, 240u, 5u, 5u);
    const RgbaSample rightWorldShadow = SamplePixel(rgba, 240u, 150u, 60u); // +X world side of x=80..140
    const RgbaSample leftUnmirroredBg = SamplePixel(rgba, 240u, 70u, 60u);  // -X world side must stay clear
    const RgbaSample childABlueSurface = SamplePixel(rgba, 240u, 95u, 75u); // inside ChildA, below ChildB: must NOT be darkened by ChildB's shadow!

    AERO_P15_CHECK(rightWorldShadow.r < bgPx.r - 80 && leftUnmirroredBg.r >= bgPx.r - 4,
                   "Container DropShadowEffect(Direction=0) casts shadow toward +X in world space despite ScaleX=-1");
    AERO_P15_CHECK(childABlueSurface.b > 235u && childABlueSurface.g > 130u,
                   "Two-pass container DropShadowEffect emits all child shadows behind ChildA foreground");

    // 5. Verify LayoutScaler::GetRefResolution & scaled ArrangeCore
    auto scaler = MakeIntrusive<LayoutScaler>();
    scaler->SetRefResolution(Size{1920.0f, 1080.0f});
    AERO_P15_CHECK(std::fabs(scaler->GetRefResolution().width - 1920.0f) < 0.01f &&
                       std::fabs(scaler->GetRefResolution().height - 1080.0f) < 0.01f,
                   "LayoutScaler::GetRefResolution reads PropId_RefResolution accurately");
    auto scalerChild = MakeIntrusive<Border>();
    scaler->AddChild(scalerChild.Get());
    scaler->Measure(Size{960.0f, 540.0f});
    scaler->Arrange(Rect{0.0f, 0.0f, 960.0f, 540.0f});
    AERO_P15_CHECK(std::fabs(scaler->ChildScaleX() - 0.5f) < 0.01f &&
                       std::fabs(scaler->ChildScaleY() - 0.5f) < 0.01f &&
                       std::fabs(scalerChild->FinalRect().width - 1920.0f) < 0.1f,
                   "LayoutScaler scales 1920x1080 RefResolution child by 0.5x into 960x540 viewport");

    if (gFailureCount == startFails) {
        std::printf("[PASS] TestBoxShadowContainerDropShadowAndImageBrushStretch\n");
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
    TestTemplatedLabelLayoutAndLiteralContent();
    TestRichTextGlyphStyleAlignmentAndReactiveBind();
    TestBackdropBlurForegroundAndGlInterleavedExecution();
    TestVectorPathGradientInPlaceUpdateAndShaders();
    TestBoxShadowContainerDropShadowAndImageBrushStretch();

    if (gFailureCount != 0) {
        std::fprintf(stderr, "=== P1-5 TESTS FAILED (%d failures) ===\n", gFailureCount);
        return 1;
    }
    std::printf("=== ALL P1-5 TESTS PASSED 100%% ===\n");
    return 0;
}
