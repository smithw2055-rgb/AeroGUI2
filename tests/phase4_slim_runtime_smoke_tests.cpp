#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
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

    AERO_SLIM_CHECK(vb->HitTest(aero::Point{100.0f, 20.0f}) == rotBtn.Get(),
                    "Slim runtime HitTest inverts both Viewbox 2x scale and child 90-deg RotateTransform");
    AERO_SLIM_CHECK(vb->HitTest(aero::Point{65.0f, 50.0f}) == inner.Get(),
                    "Slim runtime HitTest misses pre-rotation horizontal wing");

    std::cout << "[PASS] TestSlimRuntimePhase5APanelsAndAffineTransformsNoRtti\n";
}

void TestSlimRuntimePhase5BInteractiveControlsAndRouterNoRtti() {
    auto cb = aero::CreateElementByTypeName("CheckBox");
    auto rb = aero::CreateElementByTypeName("RadioButton");
    auto sl = aero::CreateElementByTypeName("Slider");
    auto tb = aero::CreateElementByTypeName("TextBox");
    auto pb = aero::CreateElementByTypeName("PasswordBox");
    auto co = aero::CreateElementByTypeName("ComboBox");

    AERO_SLIM_CHECK(cb && cb->IsA<aero::CheckBox>() && cb->IsA<aero::Button>(),
                    "CheckBox Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(rb && rb->IsA<aero::RadioButton>() && rb->IsA<aero::Button>(),
                    "RadioButton Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(sl && sl->IsA<aero::Slider>(),
                    "Slider Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(tb && tb->IsA<aero::TextBox>() && tb->IsA<aero::TextBlock>(),
                    "TextBox Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(pb && pb->IsA<aero::PasswordBox>() && pb->IsA<aero::TextBox>(),
                    "PasswordBox Euler-tour RTTI under -fno-rtti");
    AERO_SLIM_CHECK(co && co->IsA<aero::ComboBox>() && co->IsA<aero::ItemsControl>(),
                    "ComboBox Euler-tour RTTI under -fno-rtti");

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    root->SetWidth(240.0f);
    root->SetHeight(160.0f);
    sl->SetWidth(200.0f);
    sl->SetHeight(24.0f);
    tb->SetWidth(200.0f);
    tb->SetHeight(28.0f);
    root->AddChild(cb.Get());
    root->AddChild(sl.Get());
    root->AddChild(tb.Get());

    root->Measure(aero::Size{240.0f, 160.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 240.0f, 160.0f});

    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(root.Get());

    aero::InputEventRouter router(root.Get());
    const aero::Rect slRect = sl->FinalRect();
    (void)router.DispatchPointerDown(aero::Point{slRect.x + 100.0f, slRect.y + 12.0f});
    (void)router.DispatchPointerUp(aero::Point{slRect.x + 100.0f, slRect.y + 12.0f});
    AERO_SLIM_CHECK(std::fabs(sl->DynamicCast<aero::Slider>()->GetSliderValue() - 50.0f) < 1.0f,
                    "Slim runtime Slider pointer drag updated value without C++ RTTI/exceptions");

    (void)router.SetFocus(tb.Get());
    (void)router.DispatchTextInput("Wasm输入");
    AERO_SLIM_CHECK(tb->DynamicCast<aero::TextBox>()->GetText() == "Wasm输入",
                    "Slim runtime TextBox UTF-8 text input succeeded");

    (void)renderTree.SyncVisualTree(root.Get());
    AERO_SLIM_CHECK(renderTree.NodeCount() >= 6u,
                    "Slim runtime PagedRenderTree synced Phase 5B interactive controls");

    std::cout << "[PASS] TestSlimRuntimePhase5BInteractiveControlsAndRouterNoRtti\n";
}

void TestSlimRuntimePhase5CStyleTriggerAndSpringAnimationNoRtti() {
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine);

    auto btn = aero::MakeIntrusive<aero::Button>();
    btn->SetWidth(140.0f);
    btn->SetHeight(40.0f);

    aero::AxbStyleDefinition st{};
    st.key = "SlimBtnStyle";
    st.targetTypeName = "Button";
    st.setters.push_back({"", aero::PropId_Opacity, aero::MakeFloat(0.85f)});
    aero::AxbStyleTrigger hover{};
    hover.conditions.push_back({aero::PropId_TriggerIsMouseOver, aero::UnifiedValue{true}});
    hover.setters.push_back({"", aero::PropId_Opacity, aero::MakeFloat(1.0f)});
    st.triggers.push_back(hover);

    AERO_SLIM_CHECK(styleEngine.AttachStyle(btn.Get(), st),
                    "Slim runtime attached StyleTriggerEngine without C++ RTTI/exceptions");
    (void)styleEngine.EvaluateAndApply();
    AERO_SLIM_CHECK(std::fabs(btn->GetOpacity() - 0.85f) < 0.01f, "Slim base style setter applied");

    btn->SetHovered(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_SLIM_CHECK(std::fabs(btn->GetOpacity() - 1.0f) < 0.01f, "Slim hover trigger applied");

    (void)animEngine.RetargetSpringFloat(btn.Get(), aero::PropId_AnimScaleX, 1.15f);
    for (int i = 0; i < 90 && animEngine.HasActiveAnimations(); ++i) {
        (void)animEngine.Tick(0.016f);
    }
    float sx = 1.0f;
    (void)aero::TryGetFloat(btn->GetValue(aero::PropId_AnimScaleX), sx);
    AERO_SLIM_CHECK(std::fabs(sx - 1.15f) < 0.01f && !animEngine.HasActiveAnimations(),
                    "Slim runtime spring animation converged cleanly without C++ RTTI/exceptions");

    std::cout << "[PASS] TestSlimRuntimePhase5CStyleTriggerAndSpringAnimationNoRtti\n";
}


void TestSlimRuntimePhase5DElementBindConverterAndEventTriggerNoRtti() {
    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    auto chk = aero::MakeIntrusive<aero::CheckBox>();
    chk->SetName("VisToggle");
    chk->SetIsChecked(false);
    auto badge = aero::MakeIntrusive<aero::Border>();
    badge->SetName("Badge");
    auto btn = aero::MakeIntrusive<aero::Button>();
    btn->SetName("FireBtn");
    root->AddChild(chk.Get());
    root->AddChild(badge.Get());
    root->AddChild(btn.Get());

    aero::AxbCompiledBindingChain chain{};
    chain.rawPath = "IsChecked";
    chain.elementName = "VisToggle";
    chain.targetPropertyId = aero::PropId_Visibility;
    chain.converter = aero::ValueConverterKind::BooleanToVisibility;
    chain.hops.push_back(aero::AxbCompiledHop{"IsChecked"});
    AERO_SLIM_CHECK(bindingEngine.BindElementProperty(chain, badge) != 0u,
                    "Slim runtime bound ElementName + BooleanToVisibility without C++ RTTI/exceptions");
    AERO_SLIM_CHECK(badge->GetVisibility() == aero::Visibility::Collapsed,
                    "BooleanToVisibility collapsed badge when unchecked");
    chk->SetIsChecked(true);
    (void)bindingEngine.FlushTrackA();
    AERO_SLIM_CHECK(badge->GetVisibility() == aero::Visibility::Visible,
                    "BooleanToVisibility revealed badge when checked");

    aero::AxbStyleDefinition btnSt{};
    btnSt.key = "SlimClickTrig";
    btnSt.targetTypeName = "Button";
    aero::AxbEventTrigger clickEt{};
    clickEt.eventKind = aero::RoutedTriggerEvent::Click;
    aero::AxbStoryboard sb{};
    aero::AxbAnimationTimeline tl{};
    tl.targetPropertyId = aero::PropId_Opacity;
    tl.hasFrom = true;
    tl.fromValue = aero::MakeFloat(0.4f);
    tl.hasTo = true;
    tl.toValue = aero::MakeFloat(1.0f);
    tl.durationSeconds = 0.05f;
    sb.timelines.push_back(tl);
    clickEt.storyboards.push_back(sb);
    btnSt.eventTriggers.push_back(clickEt);

    AERO_SLIM_CHECK(styleEngine.AttachStyle(btn.Get(), btnSt), "Attached slim EventTrigger style");
    (void)styleEngine.EvaluateAndApply();
    (void)btn->PerformClick();
    (void)styleEngine.EvaluateAndApply();
    AERO_SLIM_CHECK(animEngine.HasActiveAnimations(), "Slim Click EventTrigger started storyboard");
    for (int i = 0; i < 6; ++i) (void)animEngine.Tick(0.016f);
    AERO_SLIM_CHECK(std::fabs(btn->GetOpacity() - 1.0f) < 0.01f, "Slim Click EventTrigger completed");

    std::cout << "[PASS] TestSlimRuntimePhase5DElementBindConverterAndEventTriggerNoRtti\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();
    TestSlimRuntimeAxb4MountAndRenderWithoutCompilerFrontend();
    TestSlimRuntimePhase5APanelsAndAffineTransformsNoRtti();
    TestSlimRuntimePhase5BInteractiveControlsAndRouterNoRtti();
    TestSlimRuntimePhase5CStyleTriggerAndSpringAnimationNoRtti();
    TestSlimRuntimePhase5DElementBindConverterAndEventTriggerNoRtti();
    std::cout << "AeroGUI 2.0 Slim Runtime (-fno-rtti, 0 Compiler Frontend) Smoke Tests Passed 100%!\n";
    return 0;
}
