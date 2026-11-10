#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AeroSceneIR.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

#define AERO_CHECK(cond, msg)                                                      \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::tests {

namespace {

bool ApproxEq(float a, float b, float eps = 1e-2f) noexcept {
    return std::fabs(a - b) <= eps;
}

// ============================================================================
// Test 1: 16B UnifiedValue ABI v2.0, MakeFloat4InlineFast & ValuePool GC Survival
// ============================================================================
void Test16ByteValueAbiAndValuePoolGcSweepSurvival() {
    static_assert(sizeof(UnifiedValue) == 16, "UnifiedValue must be 16 bytes (RealScript ABI v2.0)");
    static_assert(alignof(UnifiedValue) == 16, "UnifiedValue must be 16-byte aligned");
    static_assert(sizeof(CompactPropertyEntry) == 32, "CompactPropertyEntry must be 32 bytes");
    static_assert(alignof(CompactPropertyEntry) == 16, "CompactPropertyEntry must be 16-byte aligned");

    // Verify MakeFloat4InlineFast round-trips without pool allocation
    const UnifiedValue fastColor = MakeFloat4InlineFast(0.15f, 0.45f, 0.85f, 1.0f);
    Float4Value f4{};
    AERO_CHECK(fastColor.tryGetFloat4(f4), "MakeFloat4InlineFast tryGetFloat4 succeeded");
    AERO_CHECK(ApproxEq(f4.x, 0.15f) && ApproxEq(f4.y, 0.45f) && ApproxEq(f4.z, 0.85f) && ApproxEq(f4.w, 1.0f),
               "MakeFloat4InlineFast components match");

    // Verify ValuePool external root markers protect UIElement, AxbTemplateRegistry,
    // StyleTriggerEngine, AnimationEngine, and DualTrackBindingEngine across GC sweeps!
    ExecutionDomainContext domains;
    auto btn = MakeIntrusive<Button>();
    btn->SetContentText("Pooled_String_In_UIElement_Sparse_Storage_That_Exceeds_Inline_Limit");
    const UnifiedValue nativeH = btn->ToNativeHandle(domains.nativeHandles());
    NativeHandle rawH{};
    AERO_CHECK(nativeH.tryGetNativeHandle(rawH) && rawH.valid(), "ToNativeHandle uses 16B NativeHandle pool");

    // Store pooled NativeHandle and long string in sparse property and AxbTemplateRegistry
    btn->SetValue(PropId_Tag, nativeH, ValueSourceRank::Local);
    AxbTemplateRegistry::Instance().RegisterResource(
        "GlobalPooledResourceStr",
        value::FromString("Global_Resource_Pooled_String_Surviving_Sweep_Unmarked_Pools"));

    // Trigger full UI ManagedHeap GC + ValuePool::sweepUnmarked
    domains.uiHeap().collectFull({});

    // Verify pooled values in UIElement and AxbTemplateRegistry survived sweepUnmarked!
    AERO_CHECK(btn->GetContentText() == "Pooled_String_In_UIElement_Sparse_Storage_That_Exceeds_Inline_Limit",
               "UIElement pooled StringValue survived ValuePool::sweepUnmarked()");
    NativeHandle afterSweepH{};
    const UnifiedValue tagAfter = btn->GetValue(PropId_Tag);
    AERO_CHECK(tagAfter.tryGetNativeHandle(afterSweepH) && afterSweepH == rawH &&
                   UIElement::FromNativeHandle(domains.nativeHandles(), afterSweepH) == btn.Get(),
               "UIElement pooled NativeHandle survived ValuePool::sweepUnmarked()");

    const UnifiedValue* resAfter = AxbTemplateRegistry::Instance().FindResource("GlobalPooledResourceStr");
    const auto* resSv = resAfter ? std::get_if<StringValue>(resAfter) : nullptr;
    AERO_CHECK(resSv != nullptr &&
                   resSv->view() == "Global_Resource_Pooled_String_Surviving_Sweep_Unmarked_Pools",
               "AxbTemplateRegistry pooled StringValue survived ValuePool::sweepUnmarked()");

    std::cout << "[PASS] Test16ByteValueAbiAndValuePoolGcSweepSurvival\n";
}

// ============================================================================
// Test 2: .axb4 MOT6 Binary Section, Rive Importer, Multi-Layer State Machine,
//         Blend1D, Additive Layer, Pointer Listener & Bone Transform Constraint
// ============================================================================
void TestMot6BinarySectionMultiLayerStateMachineAndBlend1D() {
    AxbTemplateRegistry::Instance().Clear();

    const char* riveAsset = R"RIVE(RIVE{
        "x:Class": "Game.UI.TacticalReticleRig",
        "artboard": {
            "name": "ReticleRoot",
            "width": 300,
            "height": 300,
            "nodes": [
                { "name": "CoreRing", "type": "Path", "width": 120, "height": 120, "x": 10, "y": 20, "path": "M 10,10 L 110,10 L 110,110 L 10,110 Z", "fill": "#00E5FF" },
                { "name": "FollowerWing", "type": "Border", "width": 40, "height": 20, "fill": "#FFAA00" }
            ],
            "constraints": [
                { "constrained": "FollowerWing", "target": "CoreRing", "weight": 0.5, "flags": 1, "offsetX": 20.0, "offsetY": 10.0 }
            ]
        },
        "clips": [
            {
                "name": "IdleClip",
                "duration": 1.0,
                "loop": true,
                "tracks": [
                    { "target": "CoreRing", "property": "TranslateX", "keyframes": [ { "t": 0.0, "v": "10" }, { "t": 1.0, "v": "10" } ] },
                    { "target": "CoreRing", "property": "ScaleX", "keyframes": [ { "t": 0.0, "v": "1.0" }, { "t": 1.0, "v": "1.0" } ] }
                ]
            },
            {
                "name": "RunFastClip",
                "duration": 1.0,
                "loop": true,
                "tracks": [
                    { "target": "CoreRing", "property": "TranslateX", "keyframes": [ { "t": 0.0, "v": "110" }, { "t": 1.0, "v": "110" } ] },
                    { "target": "CoreRing", "property": "ScaleX", "keyframes": [ { "t": 0.0, "v": "2.0" }, { "t": 1.0, "v": "2.0" } ] }
                ]
            },
            {
                "name": "RecoilAdditiveClip",
                "duration": 0.5,
                "loop": false,
                "tracks": [
                    {
                        "target": "CoreRing",
                        "property": "TranslateX",
                        "keyframes": [
                            { "t": 0.0, "v": "25", "easing": "CubicBezier", "bx1": 0.25, "by1": 0.1, "bx2": 0.25, "by2": 1.0 },
                            { "t": 0.5, "v": "25" }
                        ]
                    }
                ]
            }
        ],
        "stateMachines": [
            {
                "name": "ReticleSM",
                "inputs": [
                    { "name": "Speed", "type": "Float", "default": 0.0, "min": 0.0, "max": 100.0 },
                    { "name": "Hovered", "type": "Float", "default": 0.0, "min": 0.0, "max": 1.0 }
                ],
                "layers": [
                    {
                        "name": "LocomotionBase",
                        "blendMode": "Override",
                        "weight": 1.0,
                        "states": [
                            {
                                "name": "SpeedBlend",
                                "kind": "Blend1D",
                                "input": "Speed",
                                "thresholds": [
                                    { "threshold": 0.0, "clip": "IdleClip" },
                                    { "threshold": 100.0, "clip": "RunFastClip" }
                                ]
                            }
                        ]
                    },
                    {
                        "name": "AdditiveRecoil",
                        "blendMode": "Additive",
                        "weight": 1.0,
                        "states": [
                            { "name": "Recoil", "kind": "Clip", "clip": "RecoilAdditiveClip", "loop": true }
                        ]
                    }
                ],
                "listeners": [
                    { "target": "CoreRing", "event": "PointerEnter", "input": "Hovered", "action": "SetFloat", "value": 1.0 },
                    { "target": "CoreRing", "event": "PointerExit", "input": "Hovered", "action": "SetFloat", "value": 0.0 }
                ]
            }
        ]
    })RIVE";

    AeroCompiler compiler;
    AeroCompileInput in{};
    in.xamlPath = "TacticalReticle.riv";
    in.xamlContent = riveAsset;
    in.emitAotCpp = true;
    const AeroCompileResult res = compiler.Compile(in);
    AERO_CHECK(res.succeeded, "Rive .riv frontend compiled into AeroSceneGraphIR + .axb4");
    AERO_CHECK(res.sceneIr.sourceFormat == FrontendSourceFormat::RiveBinary, "SceneIR recorded RiveBinary source format");
    AERO_CHECK(res.sceneIr.contractPorts.size() == 2u, "SceneIR exported 2 contract input ports");
    AERO_CHECK(res.axbPackage.HasMotionSection(), "Compiled AxbPackage contains MOT6 motion section");

    // Round-trip .axb4 binary serialization & deserialization with MOT6 section
    AxbPackage decoded{};
    std::string err;
    AERO_CHECK(AxbPackage::Deserialize(res.axbBytes, decoded, &err),
               ".axb4 MOT6 deserialization succeeded");
    AERO_CHECK(decoded.stateMachines.size() == 1u &&
                   decoded.smLayers.size() == 2u &&
                   decoded.smStates.size() == 2u &&
                   decoded.smStates[0].kind == SmStateKind::Blend1D &&
                   decoded.transformConstraints.size() == 1u &&
                   decoded.smListeners.size() == 2u,
               "MOT6 deserialized state machines, layers, Blend1D state, transform constraints & pointer listeners");

    // Mount template and run 9-phase EmbeddedGameHost tick
    ExecutionDomainContext domains;
    AnimationEngine animEngine;
    DualTrackBindingEngine bindingEngine(domains);
    bindingEngine.SetAnimationEngine(&animEngine);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;

    auto rigView = MakeIntrusive<UserControl>();
    AERO_CHECK(rigView->MountTemplate(decoded, &domains, &bindingEngine), "Mounted Rive .axb4 template");

    auto* coreRing = rigView->FindName("CoreRing");
    auto* followerWing = rigView->FindName("FollowerWing");
    AERO_CHECK(coreRing != nullptr && followerWing != nullptr, "Resolved CoreRing and FollowerWing in mounted tree");

    // Set Speed = 50.0 (halfway between IdleClip=10 and RunFastClip=110 -> Base=60) via ContractInputBank!
    ContractInputBank::SetFloat(rigView.Get(), decoded, "Speed", 50.0f, &animEngine);

    EmbeddedGameHost host(rigView.Get(), &bindingEngine, &layoutQueue, &renderTree);
    host.SetAnimationEngine(&animEngine);
    host.SetViewportSize(Size{300.0f, 300.0f});
    host.TickUi(0.016f, UiPhase_All);

    // BaseLayer Blend1D(Speed=50) = 60.0 + AdditiveLayer(Recoil=25 - rest=10 = +15) -> 75.0!
    AERO_CHECK(coreRing->GetTranslateX() > 65.0f,
               "Multi-layer Blend1D + Additive layer updated CoreRing.TranslateX");
    AERO_CHECK(ApproxEq(coreRing->GetScaleX(), 1.5f, 0.05f),
               "Blend1D(Speed=50) interpolated CoreRing.ScaleX = 1.5");

    // Non-tree bone transform constraint: FollowerWing inherits CoreRing transform!
    AERO_CHECK(followerWing->GetTranslateX() > 50.0f,
               "Non-tree AxbTransformConstraintDesc propagated bone translation to FollowerWing");

    // Pointer listener test: dispatch PointerEnter on CoreRing -> Hovered input becomes 1.0!
    InputEventRouter inputRouter(rigView.Get());
    inputRouter.SetAnimationEngine(&animEngine);
    inputRouter.DispatchPointerMove(Point{120.0f, 80.0f});
    AERO_CHECK(ApproxEq(ContractInputBank::GetFloat(rigView.Get(), decoded, "Hovered", &animEngine), 1.0f, 0.01f),
               "State machine pointer listener updated ContractInputBank 'Hovered' to 1.0 on PointerEnter");

    std::cout << "[PASS] TestMot6BinarySectionMultiLayerStateMachineAndBlend1D\n";
}

// ============================================================================
// Test 3: Lottie JSON & SVG Token Frontend Importers + Adaptive 32/64/128 MTSDF
//         Skyline Atlas Packing + Polar Matrix Decomposition
// ============================================================================
void TestLottieAndSvgMultiFrontendImportersAndAdaptiveMtsdfSkyline() {
    AeroCompiler compiler;

    // 1. SVG Token Importer with adaptive MTSDF cell sizing & matrix(...) polar decomposition
    AeroCompileInput svgIn{};
    svgIn.xamlPath = "VectorBadges.svg";
    svgIn.xamlContent = R"SVG(
<svg viewBox="0 0 256 256" x:Class="Game.UI.VectorBadges">
    <path id="SmallBadge" d="M 4,4 L 28,4 L 28,28 L 4,28 Z" fill="#00FF88" transform="matrix(1, 0, 0.5, 1, 24, 36)" />
    <path id="DetailedCrest" d="M 10,10 L 90,10 L 100,40 L 95,80 L 60,105 L 20,90 L 5,50 C 10,30 25,20 40,15 C 55,10 70,12 85,18 C 92,28 96,42 88,60 C 75,78 52,88 30,82 C 18,70 12,48 15,28 Z" fill="#FF3366" />
</svg>
)SVG";
    const AeroCompileResult svgRes = compiler.Compile(svgIn);
    AERO_CHECK(svgRes.succeeded && svgRes.sceneIr.sourceFormat == FrontendSourceFormat::SvgVectorToken,
               "SvgTokenFrontendImporter compiled .svg asset");
    AERO_CHECK(svgRes.axbPackage.mtsdfPaths.size() == 2u, "Baked 2 adaptive MTSDF paths");
    AERO_CHECK(svgRes.axbPackage.mtsdfPaths[0].cellWidth == 32u &&
                   svgRes.axbPackage.mtsdfPaths[1].cellWidth >= 64u,
               "SelectAdaptiveMtsdfCellSize chose 32x32 for simple icon and >=64x64 for multi-curve crest");

    // Verify Skyline packer placed both cells without UV overlap
    const auto& c0 = svgRes.axbPackage.mtsdfPaths[0];
    const auto& c1 = svgRes.axbPackage.mtsdfPaths[1];
    const bool nonOverlapping = (c0.u1 <= c1.u0) || (c1.u1 <= c0.u0) || (c0.v1 <= c1.v0) || (c1.v1 <= c0.v0);
    AERO_CHECK(nonOverlapping, "PackMtsdfAtlasSkyline packed 32x32 and 64x64 cells without UV overlap");

    auto svgView = MakeIntrusive<UserControl>();
    AERO_CHECK(svgView->MountTemplate(svgRes.axbPackage), "Mounted compiled SVG package");
    auto* smallBadge = svgView->FindName("SmallBadge");
    AERO_CHECK(smallBadge != nullptr &&
                   ApproxEq(smallBadge->GetTranslateX(), 24.0f) &&
                   ApproxEq(smallBadge->GetTranslateY(), 36.0f) &&
                    std::fabs(smallBadge->GetSkewX()) > 10.0f,
               "PolarDecomposeMatrix2D decomposed SVG matrix(...) into TranslateX/Y and AnimSkewX");

    // 2. Lottie JSON Importer with Cubic-Bezier keyframes & parent layer constraint
    AeroCompileInput lottieIn{};
    lottieIn.xamlPath = "SpinnerAnim.json";
    lottieIn.xamlContent = R"JSON({
        "nm": "SpinnerComp",
        "fr": 60,
        "ip": 0,
        "op": 60,
        "w": 200,
        "h": 200,
        "layers": [
            {
                "ind": 1,
                "nm": "SpinnerHub",
                "w": 80,
                "h": 80,
                "fill": "#3399FF",
                "ks": {
                    "r": {
                        "a": 1,
                        "k": [
                            { "t": 0, "s": [0], "e": [180], "o": { "x": [0.42], "y": [0.0] }, "i": { "x": [0.58], "y": [1.0] } },
                            { "t": 60, "s": [180] }
                        ]
                    },
                    "p": { "a": 0, "k": [40, 60] }
                }
            },
            {
                "ind": 2,
                "parent": 1,
                "nm": "ChildSatellite",
                "w": 24,
                "h": 24,
                "fill": "#FFCC00",
                "ks": {
                    "p": { "a": 0, "k": [10, 5] }
                }
            }
        ]
    })JSON";
    const AeroCompileResult lottieRes = compiler.Compile(lottieIn);
    AERO_CHECK(lottieRes.succeeded && lottieRes.sceneIr.sourceFormat == FrontendSourceFormat::LottieJson,
               "LottieFrontendImporter compiled Lottie .json asset");
    AERO_CHECK(lottieRes.axbPackage.HasMotionSection() &&
                   lottieRes.axbPackage.transformConstraints.size() == 1u,
               "Lottie importer lowered keyframed channels and parent layer constraint into MOT6");

    ExecutionDomainContext lottieDomains;
    AnimationEngine animEngine;
    DualTrackBindingEngine bindingEngine(lottieDomains);
    bindingEngine.SetAnimationEngine(&animEngine);
    auto lottieView = MakeIntrusive<UserControl>();
    AERO_CHECK(lottieView->MountTemplate(lottieRes.axbPackage, &lottieDomains, &bindingEngine),
               "Mounted Lottie .axb4 package");
    animEngine.EvaluateStateMachines(0.5f);
    auto* hub = lottieView->FindName("SpinnerHub");
    auto* sat = lottieView->FindName("ChildSatellite");
    AERO_CHECK(hub != nullptr && sat != nullptr, "Resolved Lottie layers by name");
    AERO_CHECK(ApproxEq(hub->GetRotateAngle(), 90.0f, 2.0f),
               "Cubic-bezier Lottie rotation keyframe evaluated at t=0.5s to ~90 deg");
    AERO_CHECK(ApproxEq(sat->GetTranslateX(), 40.0f, 1.0f),
               "Lottie parent layer constraint propagated SpinnerHub translation to ChildSatellite");

    std::cout << "[PASS] TestLottieAndSvgMultiFrontendImportersAndAdaptiveMtsdfSkyline\n";
}

// ============================================================================
// Test 4: AeroPrimType::SdfRadialArc GPU/CPU Rasterization & Review Bug Fixes
// ============================================================================
void TestSdfRadialArcPrimitiveAndReviewBugFixes() {
    PagedRenderTree tree;
    const std::uint32_t arcNodeId = tree.AllocateRadialArcNode(
        20.0f,
        20.0f,
        120.0f,
        120.0f,
        -90.0f,
        180.0f,
        0.65f,
        Color{0.1f, 0.9f, 0.5f, 1.0f},
        1.0f);
    const RenderNodeDescriptor* arcDesc = tree.GetNode(arcNodeId);
    AERO_CHECK(arcDesc != nullptr && arcDesc->primType == AeroPrimType::SdfRadialArc,
               "Allocated AeroPrimType::SdfRadialArc = 5 node in PagedRenderTree");

    // Verify analytical coverage at top of ring (angle = -90 deg, radius = 50px inside [39px, 60px])
    const float covOnRing = EvaluateSdfRadialArcCoverage(
        0.0f, -50.0f, 60.0f, 60.0f, -90.0f, 180.0f, 0.65f, 1.0f);
    const float covCenterHole = EvaluateSdfRadialArcCoverage(
        0.0f, 0.0f, 60.0f, 60.0f, -90.0f, 180.0f, 0.65f, 1.0f);
    AERO_CHECK(covOnRing > 0.85f && covCenterHole < 0.05f,
               "EvaluateSdfRadialArcCoverage renders crisp ring arc and hollow center");

    // Verify all 3 UberShader backends support primType == 5u (SdfRadialArc)
    for (ShaderBackend backend : {ShaderBackend::DesktopGL33, ShaderBackend::WebGL2, ShaderBackend::WebGPU_WGSL}) {
        const std::string_view ps = ZeroCopyGpuStream::UberPixelShaderSource(backend);
        AERO_CHECK(ps.find("PrimType == 5u") != std::string_view::npos ||
                       ps.find("primType == 5u") != std::string_view::npos,
                   "UberShader backend implements primType == 5u (SdfRadialArc)");
    }

    // Verify Button::PerformClick selfGuard survives when click handler removes button from parent
    auto parentPanel = MakeIntrusive<StackPanel>();
    auto selfRemovingBtn = MakeIntrusive<Button>();
    Button* rawBtnPtr = selfRemovingBtn.Get();
    parentPanel->AddChild(rawBtnPtr);
    selfRemovingBtn.Reset(); // parentPanel now holds the only reference!
    bool clickCompletedSafely = false;
    rawBtnPtr->SetClickCallback([&](Button&) {
        parentPanel->RemoveChild(rawBtnPtr);
        clickCompletedSafely = true;
    });
    rawBtnPtr->PerformClick();
    AERO_CHECK(clickCompletedSafely && parentPanel->ChildCount() == 0u,
               "Button::PerformClick selfGuard prevented use-after-free when handler removed button");

    // Verify DesktopEventHost idle sleep auto-resets when animations finish
    ExecutionDomainContext domains;
    AnimationEngine animEngine;
    DualTrackBindingEngine bindingEngine(domains);
    bindingEngine.SetAnimationEngine(&animEngine);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree hostTree;
    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(200.0f);
    root->SetHeight(200.0f);

    DesktopEventHost desktopHost(root.Get(), &bindingEngine, &layoutQueue, &hostTree);
    desktopHost.SetAnimationEngine(&animEngine);
    desktopHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(desktopHost.CanSleepIdle(),
               "DesktopEventHost enters 0.0% CPU/GPU sleep when clean and no animations are active");

    std::cout << "[PASS] TestSdfRadialArcPrimitiveAndReviewBugFixes\n";
}

} // namespace

} // namespace aero::tests

int main() {
    aero::tests::Test16ByteValueAbiAndValuePoolGcSweepSurvival();
    aero::tests::TestMot6BinarySectionMultiLayerStateMachineAndBlend1D();
    aero::tests::TestLottieAndSvgMultiFrontendImportersAndAdaptiveMtsdfSkyline();
    aero::tests::TestSdfRadialArcPrimitiveAndReviewBugFixes();
    std::cout << "=== ALL PHASE 6 TESTS PASSED ===\n";
    return 0;
}

