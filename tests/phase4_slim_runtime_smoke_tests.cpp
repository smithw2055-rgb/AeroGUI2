#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#define AERO_SLIM_CHECK(cond, msg)                                                 \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace {

void TestSlimRuntimeAxb4MountAndRenderWithoutCompilerFrontend() {
    aero::AxbTemplateRegistry::Instance().Clear();

    constexpr const char* kShieldSvg = "M 4,4 L 28,4 L 28,18 L 16,28 L 4,18 Z";
    aero::AxbMtsdfPathEntry bakedShield = aero::BakeSvgPathToMtsdfEntry(kShieldSvg, 32u, 4.0f, 0u, 256u);
    AERO_SLIM_CHECK(bakedShield.pathHash != 0ULL, "Baked MTSDF shield hash is non-zero");
    AERO_SLIM_CHECK(bakedShield.mtsdfRgbaPixels.size() == 32u * 32u * 4u, "Baked 32x32 RGBA8 MTSDF cell");

    aero::AxbPackage pkg{};
    pkg.className = "Game.Slim.HudCard";
    pkg.rootTypeName = "StackPanel";
    pkg.mtsdfPaths.push_back(bakedShield);

    {
        aero::AxbInstruction beginRoot{};
        beginRoot.opcode = aero::AxbOpcode::BeginElement;
        beginRoot.strIdx0 = pkg.InternString("StackPanel");
        beginRoot.strIdx1 = pkg.InternString("RootStack");
        pkg.instructions.push_back(beginRoot);

        aero::AxbInstruction setW{};
        setW.opcode = aero::AxbOpcode::SetStaticProperty;
        setW.propId = aero::PropId_Width;
        setW.literalValue = aero::MakeFloat(320.0f);
        pkg.instructions.push_back(setW);

        aero::AxbInstruction setH{};
        setH.opcode = aero::AxbOpcode::SetStaticProperty;
        setH.propId = aero::PropId_Height;
        setH.literalValue = aero::MakeFloat(160.0f);
        pkg.instructions.push_back(setH);

        aero::AxbInstruction beginPath{};
        beginPath.opcode = aero::AxbOpcode::BeginElement;
        beginPath.strIdx0 = pkg.InternString("Path");
        beginPath.strIdx1 = pkg.InternString("Emblem");
        pkg.instructions.push_back(beginPath);

        aero::AxbInstruction pathW{aero::AxbOpcode::SetStaticProperty, aero::PropId_Width, 0, 0, 0, 0, aero::MakeFloat(48.0f)};
        aero::AxbInstruction pathH{aero::AxbOpcode::SetStaticProperty, aero::PropId_Height, 0, 0, 0, 0, aero::MakeFloat(48.0f)};
        aero::AxbInstruction pathD{aero::AxbOpcode::SetStaticProperty, aero::PropId_PathData, 0, 0, 0, 0, aero::value::FromString(kShieldSvg)};
        aero::AxbInstruction pathFill{
            aero::AxbOpcode::SetStaticProperty,
            aero::PropId_Background,
            0, 0, 0, 0,
            aero::value::FromColor(aero::ParseXamlColorLiteral("#22CC88"))};
        pkg.instructions.push_back(pathW);
        pkg.instructions.push_back(pathH);
        pkg.instructions.push_back(pathD);
        pkg.instructions.push_back(pathFill);
        pkg.instructions.push_back(aero::AxbInstruction{aero::AxbOpcode::EndElement});

        aero::AxbInstruction beginTb{};
        beginTb.opcode = aero::AxbOpcode::BeginElement;
        beginTb.strIdx0 = pkg.InternString("TextBlock");
        beginTb.strIdx1 = pkg.InternString("Title");
        pkg.instructions.push_back(beginTb);
        pkg.instructions.push_back(aero::AxbInstruction{
            aero::AxbOpcode::SetStaticProperty,
            aero::PropId_Text,
            0, 0, 0, 0,
            aero::value::FromString("Slim Runtime Wasm 就绪")});
        pkg.instructions.push_back(aero::AxbInstruction{aero::AxbOpcode::EndElement});

        aero::AxbInstruction beginBtn{};
        beginBtn.opcode = aero::AxbOpcode::BeginElement;
        beginBtn.strIdx0 = pkg.InternString("Button");
        beginBtn.strIdx1 = pkg.InternString("OkBtn");
        pkg.instructions.push_back(beginBtn);
        pkg.instructions.push_back(aero::AxbInstruction{
            aero::AxbOpcode::SetStaticProperty,
            aero::PropId_Text,
            0, 0, 0, 0,
            aero::value::FromString("Confirm")});
        pkg.instructions.push_back(aero::AxbInstruction{
            aero::AxbOpcode::SetStaticProperty,
            aero::PropId_Width,
            0, 0, 0, 0,
            aero::MakeFloat(120.0f)});
        pkg.instructions.push_back(aero::AxbInstruction{
            aero::AxbOpcode::SetStaticProperty,
            aero::PropId_Height,
            0, 0, 0, 0,
            aero::MakeFloat(32.0f)});
        pkg.instructions.push_back(aero::AxbInstruction{aero::AxbOpcode::EndElement});

        pkg.instructions.push_back(aero::AxbInstruction{aero::AxbOpcode::EndElement});
    }

    const std::vector<std::uint8_t> bytes = pkg.Serialize();
    aero::AxbPackage decoded{};
    std::string errMsg;
    AERO_SLIM_CHECK(aero::AxbPackage::Deserialize(bytes, decoded, &errMsg),
                    "Pure runtime Deserialize(.axb4) succeeded with MTSDF payload");
    AERO_SLIM_CHECK(decoded.mtsdfPaths.size() == 1u &&
                        decoded.mtsdfPaths[0].pathHash == bakedShield.pathHash,
                    "Deserialized .axb4 preserved baked MTSDF entry");

    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::DepthBucketLayoutQueue layoutQueue;
    aero::PagedRenderTree renderTree;
    aero::ZeroCopyGpuStream gpuStream(aero::ShaderBackend::WebGL2, 32);

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    AERO_SLIM_CHECK(aero::MountAxbTemplate(root.Get(), decoded, &domains, &bindingEngine),
                    "MountAxbTemplate succeeded in slim runtime without compiler frontend");

    auto* emblem = root->FindName("Emblem");
    AERO_SLIM_CHECK(emblem != nullptr && emblem->IsA<aero::Path>(),
                    "Euler-tour RTTI resolved Path under -fno-rtti");

    aero::WebCanvasHost webHost(root.Get(), &bindingEngine, &layoutQueue, aero::ShaderBackend::WebGL2, &renderTree);
    webHost.SetViewportSize(aero::Size{640.0f, 360.0f});
    webHost.TickUi(0.016f, aero::UiPhase_All);

    AERO_SLIM_CHECK(renderTree.NodeCount() >= 4u,
                    "WebCanvasHost::TickUi automatically synced visual tree into PagedRenderTree");
    const auto stats = webHost.RecordGpuCommands(renderTree, gpuStream);
    AERO_SLIM_CHECK(stats.drawCallCount == 1u && stats.dirtyPagesUploaded >= 1u,
                    "Single-draw-call WebGL2 submission succeeded in slim runtime");

    std::vector<std::uint8_t> fb;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 320u, 160u, fb);
    AERO_SLIM_CHECK(fb.size() == 320u * 160u * 4u, "Headless software UberShader rasterizer produced RGBA8 buffer");

    std::cout << "[PASS] TestSlimRuntimeAxb4MountAndRenderWithoutCompilerFrontend\n";
}

void TestSlimRuntimePhase5APanelsAndAffineTransformsNoRtti() {
    auto dock = aero::CreateElementByTypeName("DockPanel");
    auto wrap = aero::CreateElementByTypeName("WrapPanel");
    auto vbox = aero::CreateElementByTypeName("Viewbox");
    auto scroll = aero::CreateElementByTypeName("ScrollViewer");

    AERO_SLIM_CHECK(dock && dock->IsA<aero::DockPanel>(), "DockPanel Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(wrap && wrap->IsA<aero::WrapPanel>(), "WrapPanel Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(vbox && vbox->IsA<aero::Viewbox>(), "Viewbox Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(scroll && scroll->IsA<aero::ScrollViewer>(), "ScrollViewer Euler-tour RTTI under -fno-rtti");

    auto* vb = vbox->DynamicCast<aero::Viewbox>();
    vb->SetWidth(200.0f);
    vb->SetHeight(100.0f);
    auto inner = aero::MakeIntrusive<aero::Canvas>();
    inner->SetWidth(100.0f);
    inner->SetHeight(50.0f);
    auto rotBtn = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(rotBtn.Get(), 30.0f);
    aero::Canvas::SetTop(rotBtn.Get(), 15.0f);
    rotBtn->SetWidth(40.0f);
    rotBtn->SetHeight(20.0f);
    rotBtn->SetRenderTransformOrigin(aero::Point{0.5f, 0.5f});
    rotBtn->SetRenderTransform(aero::Matrix2D::Rotation(90.0f));
    inner->AddChild(rotBtn.Get());
    vb->AddChild(inner.Get());

    vb->Measure(aero::Size{200.0f, 100.0f});
    vb->Arrange(aero::Rect{0.0f, 0.0f, 200.0f, 100.0f});

    // Unscaled center is (50, 25), rotated 90 deg -> unscaled [40..60]x[5..45], scaled 2x by Viewbox -> [80..120]x[10..90]!
    AERO_SLIM_CHECK(vb->HitTest(aero::Point{100.0f, 20.0f}) == rotBtn.Get(),
                    "Slim runtime HitTest inverts both Viewbox 2x scale and child 90-deg RotateTransform");
    AERO_SLIM_CHECK(vb->HitTest(aero::Point{65.0f, 50.0f}) == inner.Get(),
                    "Slim runtime HitTest misses pre-rotation horizontal wing");

    std::cout << "[PASS] TestSlimRuntimePhase5APanelsAndAffineTransformsNoRtti\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();
    TestSlimRuntimeAxb4MountAndRenderWithoutCompilerFrontend();
    TestSlimRuntimePhase5APanelsAndAffineTransformsNoRtti();
    std::cout << "AeroGUI 2.0 Slim Runtime (-fno-rtti, 0 Compiler Frontend) Smoke Tests Passed 100%!\n";
    return 0;
}
