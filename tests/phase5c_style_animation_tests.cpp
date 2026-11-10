#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
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
#include "aero/style/StyleTriggerEngine.hpp"

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

bool ColorApproxEq(const aero::Color& a, const aero::Color& b, float eps = 0.02f) noexcept {
    return ApproxEq(a.r, b.r, eps) &&
           ApproxEq(a.g, b.g, eps) &&
           ApproxEq(a.b, b.b, eps) &&
           ApproxEq(a.a, b.a, eps);
}

aero::Color ReadElemBg(const aero::UIElement* elem) noexcept {
    if (auto c = aero::value::TryGetColor(elem->GetValue(aero::PropId_Background))) {
        return *c;
    }
    return aero::Color{0.0f, 0.0f, 0.0f, 0.0f};
}

// ============================================================================
// 1. ABI Layout Invariants, animationTrackId Linkage & Zero-Allocation Steady State
// ============================================================================
void TestZeroAllocStyleTriggerAndAnimationSteadyState() {
    static_assert(sizeof(aero::UnifiedValue) == 24, "UnifiedValue == 24B");
    static_assert(sizeof(aero::Float4Value) == 16, "Float4Value == 16B");
    static_assert(sizeof(aero::CompactPropertyEntry) == 32, "CompactPropertyEntry == 32B");
    static_assert(sizeof(aero::FastInlineStorage) == 56, "FastInlineStorage == 56B");
    static_assert(sizeof(aero::AeroVertex) == 24, "AeroVertex == 24B");
    static_assert(sizeof(aero::CommandPageHeader) == 64, "CommandPageHeader == 64B");
    static_assert(sizeof(aero::CommandPage) == 4096, "CommandPage == 4096B");

    auto root = aero::MakeIntrusive<aero::Canvas>();
    root->SetWidth(300.0f);
    root->SetHeight(200.0f);

    auto btn = aero::MakeIntrusive<aero::Button>();
    aero::Canvas::SetLeft(btn.Get(), 20.0f);
    aero::Canvas::SetTop(btn.Get(), 20.0f);
    btn->SetWidth(140.0f);
    btn->SetHeight(44.0f);
    btn->SetRenderTransformOrigin(aero::Point{0.5f, 0.5f});
    root->AddChild(btn.Get());

    root->Measure(aero::Size{300.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 200.0f});

    aero::AnimationEngine animEngine(16u);
    aero::StyleTriggerEngine styleEngine(&animEngine);
    aero::InputEventRouter router(root.Get());

    aero::AxbStyleDefinition btnStyle{};
    btnStyle.key = "ZeroAllocBtn";
    btnStyle.targetTypeName = "Button";
    btnStyle.setters.push_back(aero::AxbPropertySetter{
        "",
        aero::PropId_Background,
        aero::value::FromColor(aero::ParseXamlColorLiteral("#FF203040"))});

    aero::AxbStyleTrigger hoverTrig{};
    hoverTrig.conditions.push_back(aero::AxbTriggerCondition{
        aero::PropId_TriggerIsMouseOver,
        aero::UnifiedValue{true}});
    hoverTrig.setters.push_back(aero::AxbPropertySetter{
        "",
        aero::PropId_Background,
        aero::value::FromColor(aero::ParseXamlColorLiteral("#FF356595"))});
    {
        aero::AxbStoryboard enterSb{};
        aero::AxbAnimationTimeline tl{};
        tl.targetPropertyId = aero::PropId_AnimScaleX;
        tl.durationSeconds = 0.08f;
        tl.hasTo = true;
        tl.toValue = aero::MakeFloat(1.1f);
        tl.easingKind = aero::EasingFunctionKind::Cubic;
        tl.easingMode = aero::EasingMode::EaseOut;
        enterSb.timelines.push_back(tl);
        hoverTrig.enterStoryboards.push_back(enterSb);
    }
    {
        aero::AxbStoryboard exitSb{};
        aero::AxbAnimationTimeline tl{};
        tl.targetPropertyId = aero::PropId_AnimScaleX;
        tl.durationSeconds = 0.08f;
        tl.hasTo = true;
        tl.toValue = aero::MakeFloat(1.0f);
        tl.easingKind = aero::EasingFunctionKind::Cubic;
        tl.easingMode = aero::EasingMode::EaseOut;
        exitSb.timelines.push_back(tl);
        hoverTrig.exitStoryboards.push_back(exitSb);
    }
    btnStyle.triggers.push_back(hoverTrig);

    aero::AxbStyleTrigger pressTrig{};
    pressTrig.conditions.push_back(aero::AxbTriggerCondition{
        aero::PropId_TriggerIsPressed,
        aero::UnifiedValue{true}});
    pressTrig.setters.push_back(aero::AxbPropertySetter{
        "",
        aero::PropId_Background,
        aero::value::FromColor(aero::ParseXamlColorLiteral("#FF5599DD"))});
    btnStyle.triggers.push_back(pressTrig);

    AERO_CHECK(styleEngine.AttachStyle(btn.Get(), btnStyle), "AttachStyle succeeded");

    // Warm-up 1 cycle so sparse entry for PropId_AnimScaleX is materialized on btn
    (void)styleEngine.EvaluateAndApply();
    (void)router.DispatchPointerMove(aero::Point{50.0f, 35.0f});
    (void)styleEngine.EvaluateAndApply();

    // Verify CompactPropertyEntry::animationTrackId points directly to the active track!
    const auto* scaleEntry = btn->FindCompactEntry(aero::PropId_AnimScaleX);
    AERO_CHECK(scaleEntry != nullptr, "Sparse entry for PropId_AnimScaleX created on warmup");
    const std::uint16_t activeId = animEngine.FindActiveTrackId(btn.Get(), aero::PropId_AnimScaleX);
    AERO_CHECK(activeId != aero::AnimationEngine::InvalidTrackId &&
                   scaleEntry->animationTrackId == activeId,
               "CompactPropertyEntry::animationTrackId matches active AnimationEngine track slot");

    for (int i = 0; i < 8; ++i) {
        (void)animEngine.Tick(0.016f);
    }
    AERO_CHECK(!animEngine.HasActiveAnimations(), "Warmup hover animation completed");
    AERO_CHECK(btn->FindCompactEntry(aero::PropId_AnimScaleX)->animationTrackId ==
                   aero::AnimationEngine::InvalidTrackId,
               "CompactPropertyEntry::animationTrackId reset to InvalidTrackId on completion");

    (void)router.DispatchPointerMove(aero::Point{260.0f, 180.0f});
    (void)styleEngine.EvaluateAndApply();
    for (int i = 0; i < 8; ++i) {
        (void)animEngine.Tick(0.016f);
    }

    // Measure 120 full interactive hover/press/release/leave + trigger + animation cycles -> MUST be 0 heap allocs!
    gHeapAllocCount.store(0u, std::memory_order_relaxed);
    gCountHeapAllocs.store(true, std::memory_order_seq_cst);

    for (int cycle = 0; cycle < 120; ++cycle) {
        (void)router.DispatchPointerMove(aero::Point{60.0f, 40.0f});
        (void)styleEngine.EvaluateAndApply();
        (void)animEngine.Tick(0.016f);
        (void)router.DispatchPointerDown(aero::Point{60.0f, 40.0f});
        (void)styleEngine.EvaluateAndApply();
        (void)animEngine.Tick(0.016f);
        (void)router.DispatchPointerUp(aero::Point{60.0f, 40.0f});
        (void)styleEngine.EvaluateAndApply();
        (void)router.DispatchPointerMove(aero::Point{250.0f, 170.0f});
        (void)styleEngine.EvaluateAndApply();
        for (int step = 0; step < 6; ++step) {
            (void)animEngine.Tick(0.016f);
        }
    }

    gCountHeapAllocs.store(false, std::memory_order_seq_cst);
    const std::size_t totalAllocs = gHeapAllocCount.load(std::memory_order_relaxed);
    AERO_CHECK(totalAllocs == 0u,
               "Zero heap allocations across 120 hover/press/storyboard/track-recycle cycles");

    std::cout << "[PASS] TestZeroAllocStyleTriggerAndAnimationSteadyState (allocs=" << totalAllocs << ")\n";
}

// ============================================================================
// 2. Style BasedOn Inheritance, ValueSourceRank Precedence & MultiTrigger Restoration
// ============================================================================
void TestStyleBasedOnInheritanceTriggerPrecedenceAndMultiTriggerRestore() {
    aero::StyleTriggerEngine styleEngine;

    const aero::Color baseThemeBg = aero::ParseXamlColorLiteral("#FF1E293B");
    const aero::Color derivedThemeBg = aero::ParseXamlColorLiteral("#FF0F766E");
    const aero::Color explicitLocalBg = aero::ParseXamlColorLiteral("#FFB91C1C");
    const aero::Color hoverBg = aero::ParseXamlColorLiteral("#FF14B8A6");
    const aero::Color pressedBg = aero::ParseXamlColorLiteral("#FF2DD4BF");

    aero::AxbStyleDefinition baseStyle{};
    baseStyle.key = "BaseControlStyle";
    baseStyle.targetTypeName = "Button";
    baseStyle.setters.push_back({"", aero::PropId_Background, aero::value::FromColor(baseThemeBg)});
    baseStyle.setters.push_back({"", aero::PropId_CornerRadius, aero::value::FromCornerRadius(aero::CornerRadius(8.0f))});
    baseStyle.setters.push_back({"", aero::PropId_Opacity, aero::MakeFloat(0.92f)});

    aero::AxbStyleTrigger hoverTrig{};
    hoverTrig.overrideLocalValues = true; // ControlTemplate.Triggers semantics
    hoverTrig.conditions.push_back({aero::PropId_TriggerIsMouseOver, aero::UnifiedValue{true}});
    hoverTrig.setters.push_back({"", aero::PropId_Background, aero::value::FromColor(hoverBg)});
    hoverTrig.setters.push_back({"", aero::PropId_Opacity, aero::MakeFloat(1.0f)});
    baseStyle.triggers.push_back(hoverTrig);

    aero::AxbStyleDefinition derivedStyle{};
    derivedStyle.key = "DerivedAccentStyle";
    derivedStyle.targetTypeName = "Button";
    derivedStyle.basedOnKey = "BaseControlStyle";
    derivedStyle.setters.push_back({"", aero::PropId_Background, aero::value::FromColor(derivedThemeBg)});

    aero::AxbStyleTrigger pressedTrig{};
    pressedTrig.overrideLocalValues = true;
    pressedTrig.conditions.push_back({aero::PropId_TriggerIsPressed, aero::UnifiedValue{true}});
    pressedTrig.setters.push_back({"", aero::PropId_Background, aero::value::FromColor(pressedBg)});
    derivedStyle.triggers.push_back(pressedTrig);

    styleEngine.RegisterStyle(baseStyle);
    styleEngine.RegisterStyle(derivedStyle);

    auto btnStyled = aero::MakeIntrusive<aero::Button>();
    auto btnWithLocal = aero::MakeIntrusive<aero::Button>();
    btnWithLocal->SetValue(aero::PropId_Background, aero::value::FromColor(explicitLocalBg)); // Explicit Local value!

    AERO_CHECK(styleEngine.AttachStyleByKey(btnStyled.Get(), "DerivedAccentStyle"), "Attach DerivedAccentStyle");
    AERO_CHECK(styleEngine.AttachStyleByKey(btnWithLocal.Get(), "DerivedAccentStyle"), "Attach to Local-overridden btn");
    (void)styleEngine.EvaluateAndApply();

    // Base setters from BasedOn + derived override
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnStyled.Get()), derivedThemeBg),
               "Derived style overrode BaseControlStyle Background");
    AERO_CHECK(ApproxEq(aero::value::TryGetCornerRadius(btnStyled->GetValue(aero::PropId_CornerRadius))->topLeft, 8.0f),
               "Derived style inherited CornerRadius=8 from BaseControlStyle");
    AERO_CHECK(ApproxEq(btnStyled->GetOpacity(), 0.92f),
               "Derived style inherited Opacity=0.92 from BaseControlStyle");

    // Local value takes precedence over ThemeStyle setter!
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnWithLocal.Get()), explicitLocalBg),
               "ThemeStyle setter did NOT clobber explicit Local Background");

    // Hover + Press -> Last-active-trigger-wins (pressedTrig comes after hoverTrig)
    btnStyled->SetHovered(true);
    btnWithLocal->SetHovered(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnStyled.Get()), hoverBg) && ApproxEq(btnStyled->GetOpacity(), 1.0f),
               "Hover trigger applied hoverBg and Opacity=1.0");
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnWithLocal.Get()), hoverBg),
               "Template trigger temporarily overrode Local Background during hover");

    btnStyled->SetPressed(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnStyled.Get()), pressedBg),
               "Pressed trigger won over Hover trigger (WPF last-trigger-wins order)");

    // Release press while still hovered -> falls back to hoverBg!
    btnStyled->SetPressed(false);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnStyled.Get()), hoverBg),
               "Releasing press while hovered restored hoverBg");

    // Leave hover -> restores derivedThemeBg on btnStyled AND explicitLocalBg on btnWithLocal!
    btnStyled->SetHovered(false);
    btnWithLocal->SetHovered(false);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnStyled.Get()), derivedThemeBg) &&
                   ApproxEq(btnStyled->GetOpacity(), 0.92f),
               "Hover exit restored ThemeStyle Background and Opacity=0.92");
    AERO_CHECK(ColorApproxEq(ReadElemBg(btnWithLocal.Get()), explicitLocalBg),
               "Hover exit restored exact explicit Local Background on btnWithLocal");

    // MultiTrigger: IsMouseOver == true && IsChecked == true on CheckBox
    auto check = aero::MakeIntrusive<aero::CheckBox>();
    const aero::Color multiColor = aero::ParseXamlColorLiteral("#FFF59E0B");
    aero::AxbStyleDefinition chkStyle{};
    chkStyle.key = "GlowCheckStyle";
    chkStyle.targetTypeName = "CheckBox";
    chkStyle.setters.push_back({"", aero::PropId_BorderBrush, aero::value::FromColor(baseThemeBg)});
    aero::AxbStyleTrigger multi{};
    multi.conditions.push_back({aero::PropId_TriggerIsMouseOver, aero::UnifiedValue{true}});
    multi.conditions.push_back({aero::PropId_IsChecked, aero::UnifiedValue{true}});
    multi.setters.push_back({"", aero::PropId_BorderBrush, aero::value::FromColor(multiColor)});
    chkStyle.triggers.push_back(multi);

    AERO_CHECK(styleEngine.AttachStyle(check.Get(), chkStyle), "Attach GlowCheckStyle");
    (void)styleEngine.EvaluateAndApply();

    check->SetHovered(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(*aero::value::TryGetColor(check->GetValue(aero::PropId_BorderBrush)), baseThemeBg),
               "MultiTrigger stays inactive when only IsMouseOver is true");

    check->SetIsChecked(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(*aero::value::TryGetColor(check->GetValue(aero::PropId_BorderBrush)), multiColor),
               "MultiTrigger activates when BOTH IsMouseOver and IsChecked are true");

    check->SetHovered(false);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ColorApproxEq(*aero::value::TryGetColor(check->GetValue(aero::PropId_BorderBrush)), baseThemeBg),
               "MultiTrigger restores base BorderBrush as soon as one condition turns false");

    std::cout << "[PASS] TestStyleBasedOnInheritanceTriggerPrecedenceAndMultiTriggerRestore\n";
}

// ============================================================================
// 3. All 11 Easing Curves x 3 Modes, Tween Repeat/AutoReverse & FillBehavior::Stop
// ============================================================================
void TestAllElevenEasingCurvesTweenRepeatAutoReverseAndFillStop() {
    constexpr aero::EasingFunctionKind kAllKinds[] = {
        aero::EasingFunctionKind::Linear,
        aero::EasingFunctionKind::Quadratic,
        aero::EasingFunctionKind::Cubic,
        aero::EasingFunctionKind::Quartic,
        aero::EasingFunctionKind::Quintic,
        aero::EasingFunctionKind::Sine,
        aero::EasingFunctionKind::Circle,
        aero::EasingFunctionKind::Exponential,
        aero::EasingFunctionKind::Back,
        aero::EasingFunctionKind::Bounce,
        aero::EasingFunctionKind::Elastic,
    };
    constexpr aero::EasingMode kAllModes[] = {
        aero::EasingMode::EaseOut,
        aero::EasingMode::EaseIn,
        aero::EasingMode::EaseInOut,
    };

    for (const auto kind : kAllKinds) {
        for (const auto mode : kAllModes) {
            const float f0 = aero::EvaluateEasing(kind, mode, 0.0f);
            const float f1 = aero::EvaluateEasing(kind, mode, 1.0f);
            AERO_CHECK(ApproxEq(f0, 0.0f, 1e-4f), "Easing f(0) == 0");
            AERO_CHECK(ApproxEq(f1, 1.0f, 1e-4f), "Easing f(1) == 1");
            if (mode == aero::EasingMode::EaseInOut) {
                const float fMid = aero::EvaluateEasing(kind, mode, 0.5f);
                AERO_CHECK(ApproxEq(fMid, 0.5f, 1e-3f), "EaseInOut midpoint symmetry f(0.5) == 0.5");
            }
        }
    }

    // BackEase characteristic overshoot check
    AERO_CHECK(aero::EvaluateEasing(aero::EasingFunctionKind::Back, aero::EasingMode::EaseOut, 0.65f) > 1.02f,
               "BackEase EaseOut overshoots > 1.0");
    AERO_CHECK(aero::EvaluateEasing(aero::EasingFunctionKind::Back, aero::EasingMode::EaseIn, 0.35f) < -0.02f,
               "BackEase EaseIn anticipates < 0.0");

    aero::AnimationEngine animEngine;
    auto box = aero::MakeIntrusive<aero::Border>();
    box->SetOpacity(0.4f);

    // BeginTime=0.1s, Duration=0.2s, AutoReverse=true, RepeatCount=1 -> total active span after delay is 0.4s
    (void)animEngine.StartFloatTween(
        box.Get(),
        aero::PropId_Opacity,
        0.2f,
        1.0f,
        0.2f,
        aero::EasingFunctionKind::Linear,
        aero::EasingMode::EaseOut,
        aero::FillBehavior::Stop,
        0.1f,
        true,
        1);

    // During BeginTime delay (t = 0.05s < 0.1s), Opacity stays at pre-animation 0.4f!
    (void)animEngine.Tick(0.05f);
    AERO_CHECK(ApproxEq(box->GetOpacity(), 0.4f), "Opacity unchanged during BeginTime delay");

    // t = 0.20s (0.10s into forward leg of 0.2s) -> halfway between 0.2 and 1.0 == 0.6
    (void)animEngine.Tick(0.15f);
    AERO_CHECK(ApproxEq(box->GetOpacity(), 0.6f, 0.02f), "Forward tween leg at 50% == 0.6");

    // t = 0.30s (peak of AutoReverse at 0.2s into active phase) -> 1.0
    (void)animEngine.Tick(0.10f);
    AERO_CHECK(ApproxEq(box->GetOpacity(), 1.0f, 0.02f), "AutoReverse peak == 1.0");

    // t = 0.40s (halfway back on reverse leg) -> 0.6
    (void)animEngine.Tick(0.10f);
    AERO_CHECK(ApproxEq(box->GetOpacity(), 0.6f, 0.02f), "Reverse tween leg at 50% == 0.6");

    // t = 0.55s (completed with FillBehavior::Stop) -> restores pre-animation 0.4f!
    (void)animEngine.Tick(0.15f);
    AERO_CHECK(!animEngine.HasActiveAnimations(), "Tween finished after AutoReverse cycle");
    AERO_CHECK(ApproxEq(box->GetOpacity(), 0.4f, 0.01f),
               "FillBehavior::Stop restored pre-animation Opacity=0.4");

    std::cout << "[PASS] TestAllElevenEasingCurvesTweenRepeatAutoReverseAndFillStop\n";
}

// ============================================================================
// 4. Multi-KeyFrame Timeline + Non-Destructive Animated RenderTransform Channels
// ============================================================================
void TestKeyFramesAnimatedRenderTransformCompositionAndPagedRenderTree() {
    auto root = aero::MakeIntrusive<aero::Canvas>();
    root->SetWidth(300.0f);
    root->SetHeight(200.0f);

    auto badge = aero::MakeIntrusive<aero::Border>();
    aero::Canvas::SetLeft(badge.Get(), 50.0f);
    aero::Canvas::SetTop(badge.Get(), 40.0f);
    badge->SetWidth(100.0f);
    badge->SetHeight(60.0f);
    badge->SetBackground(aero::ParseXamlColorLiteral("#FF2563EB"));
    badge->SetRenderTransformOrigin(aero::Point{0.5f, 0.5f});
    // Static RenderTransform translation (+10, +20) that MUST NOT be clobbered by scale animation!
    badge->SetRenderTransform(aero::Matrix2D::Translation(10.0f, 20.0f));
    root->AddChild(badge.Get());

    root->Measure(aero::Size{300.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 300.0f, 200.0f});

    aero::PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(root.Get());

    aero::AnimationEngine animEngine;
    aero::AxbAnimationTimeline scaleTimeline{};
    scaleTimeline.targetPropertyId = aero::PropId_AnimScaleX;
    scaleTimeline.keyFrames.push_back(aero::AxbKeyFrame{
        0.0f, aero::MakeFloat(1.0f), aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, false});
    scaleTimeline.keyFrames.push_back(aero::AxbKeyFrame{
        0.2f, aero::MakeFloat(2.0f), aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, false});
    scaleTimeline.keyFrames.push_back(aero::AxbKeyFrame{
        0.4f, aero::MakeFloat(1.5f), aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, true});

    AERO_CHECK(animEngine.StartTimeline(badge.Get(), scaleTimeline) != aero::AnimationEngine::InvalidTrackId,
               "Started multi-keyframe ScaleX timeline");

    // Advance to t = 0.2s -> ScaleX == 2.0f!
    (void)animEngine.Tick(0.2f);
    AERO_CHECK((badge->GetLayoutDirtyFlags() & (aero::MeasureDirty | aero::ArrangeDirty)) == 0u,
               "Pure render-transform animation does NOT dirty Measure or Arrange layout passes");

    // Verify composed transform:
    // Badge FinalRect is [50, 40, 100x60], center is (100, 70).
    // Scaling X by 2.0 around center (100, 70) maps x in [50, 150] -> [0, 200],
    // PLUS static RenderTransform Translation(+10, +20) -> world bounds Rect{10, 60, 200, 60}!
    const aero::Rect worldBounds = badge->LocalToScreenBounds(badge->FinalRect());
    AERO_CHECK(ApproxEq(worldBounds.x, 10.0f, 0.05f) &&
                   ApproxEq(worldBounds.y, 60.0f, 0.05f) &&
                   ApproxEq(worldBounds.width, 200.0f, 0.05f) &&
                   ApproxEq(worldBounds.height, 60.0f, 0.05f),
               "ScaleX=2.0 around center + static Translate(10,20) mapped world bounds to [10, 60, 200x60]");

    // Discrete keyframe hold check at t = 0.3s (still 2.0f until discrete jump at 0.4s -> 1.5f)
    (void)animEngine.Tick(0.1f);
    float curScale = 0.0f;
    (void)aero::TryGetFloat(badge->GetValue(aero::PropId_AnimScaleX), curScale);
    AERO_CHECK(ApproxEq(curScale, 2.0f, 0.02f), "Discrete keyframe holds previous value=2.0 before KeyTime=0.4s");

    (void)animEngine.Tick(0.12f);
    (void)aero::TryGetFloat(badge->GetValue(aero::PropId_AnimScaleX), curScale);
    AERO_CHECK(ApproxEq(curScale, 1.5f, 0.02f), "Discrete keyframe snapped to 1.5 at KeyTime=0.4s");

    const std::size_t synced = renderTree.SyncVisualTree(root.Get());
    AERO_CHECK(synced >= 1u && renderTree.DirtyPageCount() >= 1u,
               "PagedRenderTree synced animated transform node onto dirty GPU command page");

    std::cout << "[PASS] TestKeyFramesAnimatedRenderTransformCompositionAndPagedRenderTree\n";
}

// ============================================================================
// 5. Second-Order C1 Velocity-Continuous Spring Mid-Flight Interruption
// ============================================================================
void TestVelocityContinuousSpringInterruptionAndConvergence() {
    aero::AnimationEngine animEngine;
    auto card = aero::MakeIntrusive<aero::Border>();

    aero::SpringConfig cfg{};
    cfg.stiffness = 300.0f;
    cfg.damping = 26.0f;
    cfg.mass = 1.0f;

    const std::uint16_t id0 = animEngine.RetargetSpringFloat(
        card.Get(), aero::PropId_AnimTranslateX, 100.0f, cfg);
    AERO_CHECK(id0 != aero::AnimationEngine::InvalidTrackId, "Started spring toward +100");

    // Advance 4 frames (64ms) -> card accelerates smoothly toward +100
    for (int i = 0; i < 4; ++i) {
        (void)animEngine.Tick(0.016f);
    }

    float posBeforeInterrupt = 0.0f;
    (void)aero::TryGetFloat(card->GetValue(aero::PropId_AnimTranslateX), posBeforeInterrupt);
    const float velBeforeInterrupt = animEngine.GetTrackVelocityX(id0);
    AERO_CHECK(posBeforeInterrupt > 8.0f && posBeforeInterrupt < 92.0f,
               "Spring is mid-flight between 0 and 100");
    AERO_CHECK(velBeforeInterrupt > 100.0f,
               "Spring has strong positive velocity mid-flight");

    // Interrupt mid-flight and retarget to -40.0f!
    const std::uint16_t id1 = animEngine.RetargetSpringFloat(
        card.Get(), aero::PropId_AnimTranslateX, -40.0f, cfg);
    AERO_CHECK(id1 == id0, "RetargetSpringFloat reused active track slot in-place");

    float posImmediatelyAfter = 0.0f;
    (void)aero::TryGetFloat(card->GetValue(aero::PropId_AnimTranslateX), posImmediatelyAfter);
    const float velImmediatelyAfter = animEngine.GetTrackVelocityX(id1);
    AERO_CHECK(ApproxEq(posImmediatelyAfter, posBeforeInterrupt, 1e-5f),
               "C0 position continuity preserved on mid-flight retarget (zero snap)");
    AERO_CHECK(ApproxEq(velImmediatelyAfter, velBeforeInterrupt, 1e-5f),
               "C1 velocity continuity preserved on mid-flight retarget");

    // Next small dt (4ms): because forward momentum was > +100 px/s, position briefly continues forward smoothly
    (void)animEngine.Tick(0.004f);
    float posAfter4ms = 0.0f;
    (void)aero::TryGetFloat(card->GetValue(aero::PropId_AnimTranslateX), posAfter4ms);
    AERO_CHECK(posAfter4ms > posBeforeInterrupt - 1.0f &&
                   animEngine.GetTrackVelocityX(id1) < velBeforeInterrupt,
               "Spring smoothly decelerates forward momentum toward reversed target");

    // Advance until settled (within 1.5s)
    for (int step = 0; step < 120 && animEngine.HasActiveAnimations(); ++step) {
        (void)animEngine.Tick(0.016f);
    }
    float settledPos = 0.0f;
    (void)aero::TryGetFloat(card->GetValue(aero::PropId_AnimTranslateX), settledPos);
    AERO_CHECK(!animEngine.HasActiveAnimations(), "Spring converged to rest and auto-released track");
    AERO_CHECK(ApproxEq(settledPos, -40.0f, 1e-3f), "Spring settled accurately at retargeted -40.0f");

    std::cout << "[PASS] TestVelocityContinuousSpringInterruptionAndConvergence\n";
}

// ============================================================================
// 6. End-to-End XAML -> .axb4 & --aot-cpp (<Style>, <Trigger>, VSM, Storyboard & Host Sleep/Wake)
// ============================================================================
void TestEndToEndXamlAxb4AotVisualStatesAndHostIdleSleepWake() {
    aero::AxbTemplateRegistry::Instance().Clear();

    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            x:Class="Samples.Phase5C.StyledHud"
            Width="360" Height="240" Spacing="10">
    <StackPanel.Resources>
        <SolidColorBrush x:Key="CardNormalBrush" Color="#FF1E293B"/>
        <SolidColorBrush x:Key="CardHoverBrush" Color="#FF2563EB"/>

        <Storyboard x:Key="BannerIntroSb">
            <DoubleAnimation Storyboard.TargetName="HeroBanner"
                             Storyboard.TargetProperty="Opacity"
                             From="0.25" To="1.0" Duration="0:0:0.16">
                <DoubleAnimation.EasingFunction>
                    <QuadraticEase EasingMode="EaseOut"/>
                </DoubleAnimation.EasingFunction>
            </DoubleAnimation>
        </Storyboard>

        <Style x:Key="BaseCardStyle" TargetType="{x:Type Border}">
            <Setter Property="Background" Value="{StaticResource CardNormalBrush}"/>
            <Setter Property="CornerRadius" Value="10"/>
        </Style>

        <Style x:Key="InteractiveCardStyle" TargetType="{x:Type Border}" BasedOn="{StaticResource BaseCardStyle}">
            <Style.Triggers>
                <Trigger Property="IsMouseOver" Value="True">
                    <Setter Property="Background" Value="{StaticResource CardHoverBrush}"/>
                    <Trigger.EnterActions>
                        <BeginStoryboard>
                            <Storyboard>
                                <DoubleAnimation Storyboard.TargetProperty="(UIElement.RenderTransform).(ScaleTransform.ScaleX)"
                                                 To="1.2" Duration="0:0:0.16">
                                    <DoubleAnimation.EasingFunction>
                                        <BackEase EasingMode="EaseOut"/>
                                    </DoubleAnimation.EasingFunction>
                                </DoubleAnimation>
                            </Storyboard>
                        </BeginStoryboard>
                    </Trigger.EnterActions>
                    <Trigger.ExitActions>
                        <BeginStoryboard>
                            <Storyboard>
                                <DoubleAnimation Storyboard.TargetProperty="(UIElement.RenderTransform).(ScaleTransform.ScaleX)"
                                                 To="1.0" Duration="0:0:0.12"/>
                            </Storyboard>
                        </BeginStoryboard>
                    </Trigger.ExitActions>
                </Trigger>
            </Style.Triggers>
        </Style>
    </StackPanel.Resources>

    <Border x:Name="HeroBanner" Width="320" Height="64"
            RenderTransformOrigin="0.5,0.5"
            Style="{StaticResource InteractiveCardStyle}"/>

    <Button x:Name="VsmBtn" Content="Launch" Width="160" Height="44" Background="#FF334155">
        <VisualStateManager.VisualStateGroups>
            <VisualStateGroup x:Name="CommonStates">
                <VisualStateGroup.Transitions>
                    <VisualTransition GeneratedDuration="0:0:0.12">
                        <VisualTransition.GeneratedEasingFunction>
                            <CubicEase EasingMode="EaseOut"/>
                        </VisualTransition.GeneratedEasingFunction>
                    </VisualTransition>
                </VisualStateGroup.Transitions>
                <VisualState x:Name="Normal">
                    <Storyboard>
                        <ColorAnimation Storyboard.TargetProperty="Background" To="#FF334155" Duration="0:0:0.12"/>
                    </Storyboard>
                </VisualState>
                <VisualState x:Name="MouseOver">
                    <Storyboard>
                        <ColorAnimation Storyboard.TargetProperty="Background" To="#FF0EA5E9" Duration="0:0:0.12"/>
                    </Storyboard>
                </VisualState>
                <VisualState x:Name="Pressed">
                    <Storyboard>
                        <ColorAnimation Storyboard.TargetProperty="Background" To="#FF38BDF8" Duration="0:0:0.06"/>
                    </Storyboard>
                </VisualState>
            </VisualStateGroup>
            <VisualStateGroup x:Name="ExpansionStates">
                <VisualState x:Name="Collapsed">
                    <VisualState.Setters>
                        <Setter Property="Width" Value="160"/>
                    </VisualState.Setters>
                </VisualState>
                <VisualState x:Name="Expanded">
                    <VisualState.Setters>
                        <Setter Property="Width" Value="240"/>
                    </VisualState.Setters>
                </VisualState>
            </VisualStateGroup>
        </VisualStateManager.VisualStateGroups>
    </Button>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "StyledHud.xaml";
    input.xamlContent = xaml;
    input.emitAotCpp = true;
    input.aotCppNamespace = "aero::test_phase5c_aot";

    const aero::AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "AeroCompiler compiled Phase 5C XAML cleanly");
    AERO_CHECK(res.axbPackage.storyboards.size() == 1u &&
                   res.axbPackage.storyboards[0].key == "BannerIntroSb",
               "Compiled standalone Storyboard resource into AxbPackage.storyboards");
    AERO_CHECK(res.axbPackage.styles.size() == 3u,
               "Compiled BaseCardStyle, InteractiveCardStyle, and inline VSM style into AxbPackage.styles");
    AERO_CHECK(res.aotCpp.source.find("RegisterStyle") != std::string::npos &&
                   res.aotCpp.source.find("RegisterStoryboard") != std::string::npos,
               "--aot-cpp generated C++17 style & storyboard registration");

    // Round-trip serialize & deserialize .axb4 binary ("STC5" section)
    aero::AxbPackage decoded{};
    std::string decodeErr;
    AERO_CHECK(aero::AxbPackage::Deserialize(res.axbBytes, decoded, &decodeErr),
               ".axb4 STC5 binary section round-trip deserialized cleanly");
    AERO_CHECK(decoded.styles.size() == 3u && decoded.storyboards.size() == 1u,
               "Deserialized .axb4 preserved all 3 styles and 1 storyboard");

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::DepthBucketLayoutQueue layoutQueue;
    aero::PagedRenderTree renderTree;
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine);

    aero::AxbTemplateRegistry::Instance().Clear();
    AERO_CHECK(aero::MountAxbTemplate(root.Get(), decoded, &domains, &bindingEngine),
               "MountAxbTemplate mounted decoded Phase 5C .axb4 package");
    styleEngine.RegisterPackageStyles(decoded);
    AERO_CHECK(styleEngine.BindSubtreeStyles(root.Get()) == 2u,
               "Bound styles onto HeroBanner and VsmBtn");

    auto* banner = root->FindName("HeroBanner")->DynamicCast<aero::Border>();
    auto* vsmBtn = root->FindName("VsmBtn")->DynamicCast<aero::Button>();
    AERO_CHECK(banner != nullptr && vsmBtn != nullptr, "Resolved HeroBanner and VsmBtn");

    // Wire DesktopEventHost & WebCanvasHost with StyleTriggerEngine + AnimationEngine + InputEventRouter
    aero::InputEventRouter router(root.Get());
    aero::DesktopEventHost desktopHost(root.Get(), &bindingEngine, &layoutQueue, &renderTree);
    desktopHost.SetStyleEngine(&styleEngine);
    desktopHost.SetAnimationEngine(&animEngine);
    desktopHost.SetViewportSize(aero::Size{360.0f, 240.0f});

    aero::WebCanvasHost webHost(root.Get(), &bindingEngine, &layoutQueue, aero::ShaderBackend::WebGL2, &renderTree);
    webHost.SetStyleEngine(&styleEngine);
    webHost.SetAnimationEngine(&animEngine);
    webHost.SetViewportSize(aero::Size{360.0f, 240.0f});

    // Initial frame settles layout + initial base styles -> host enters 0% CPU idle sleep!
    desktopHost.NotifyDirtyLayout();
    desktopHost.TickUi(0.016f, aero::UiPhase_All);
    webHost.TickUi(0.016f, aero::UiPhase_All);
    webHost.TickUi(0.016f, aero::UiPhase_All);
    AERO_CHECK(ColorApproxEq(banner->GetBackground(), aero::ParseXamlColorLiteral("#FF1E293B")),
               "HeroBanner received inherited BaseCardStyle Background");
    AERO_CHECK(desktopHost.CanSleepIdle(),
               "DesktopEventHost sleeps at 0% CPU when no animations or dirty flags are active");
    AERO_CHECK(webHost.RafState() == aero::WebRafState::SuspendedIdle,
               "WebCanvasHost suspends requestAnimationFrame when idle");

    // Trigger standalone storyboard from resources
    const aero::AxbStoryboard* introSb = styleEngine.FindStoryboard("BannerIntroSb");
    AERO_CHECK(introSb != nullptr, "Found BannerIntroSb");
    AERO_CHECK(animEngine.BeginStoryboard(root.Get(), *introSb) == 1u, "Started BannerIntroSb");
    AERO_CHECK(!desktopHost.CanSleepIdle(), "DesktopEventHost wakes while BannerIntroSb is active");
    for (int i = 0; i < 14; ++i) {
        desktopHost.TickUi(0.016f, aero::UiPhase_All);
    }
    AERO_CHECK(ApproxEq(banner->GetOpacity(), 1.0f, 0.01f) && desktopHost.CanSleepIdle(),
               "BannerIntroSb completed at Opacity=1.0 and DesktopEventHost returned to idle sleep");

    // Pointer hovers over HeroBanner -> Trigger sets hover Background + EnterActions BackEase ScaleX -> 1.2!
    const aero::Rect bannerRect = banner->FinalRect();
    (void)router.DispatchPointerMove(aero::Point{bannerRect.x + 40.0f, bannerRect.y + 20.0f});
    webHost.RequestWakeFrame();
    webHost.TickUi(0.016f, aero::UiPhase_All);

    AERO_CHECK(ColorApproxEq(banner->GetBackground(), aero::ParseXamlColorLiteral("#FF2563EB")),
               "IsMouseOver trigger applied CardHoverBrush");
    AERO_CHECK(webHost.RafState() == aero::WebRafState::ActiveTicking && !desktopHost.CanSleepIdle(),
               "Hosts remain awake while EnterActions BackEase ScaleX storyboard is running");

    bool sawBackOvershoot = false;
    for (int i = 0; i < 18; ++i) {
        webHost.TickUi(0.016f, aero::UiPhase_All);
        float sx = 1.0f;
        (void)aero::TryGetFloat(banner->GetValue(aero::PropId_AnimScaleX), sx);
        if (sx > 1.202f) {
            sawBackOvershoot = true;
        }
    }
    float settledHoverScale = 1.0f;
    (void)aero::TryGetFloat(banner->GetValue(aero::PropId_AnimScaleX), settledHoverScale);
    AERO_CHECK(sawBackOvershoot, "BackEase EnterActions storyboard exhibited characteristic >1.2x overshoot");
    AERO_CHECK(ApproxEq(settledHoverScale, 1.2f, 0.01f), "HeroBanner ScaleX settled at 1.2x");
    AERO_CHECK(webHost.RafState() == aero::WebRafState::SuspendedIdle && desktopHost.CanSleepIdle(),
               "WebCanvasHost & DesktopEventHost automatically returned to 0% CPU idle once hover animation settled");

    // Pointer moves onto VsmBtn -> HeroBanner runs ExitActions back to 1.0x AND VsmBtn transitions CommonStates -> MouseOver!
    const aero::Rect btnRect = vsmBtn->FinalRect();
    (void)router.DispatchPointerMove(aero::Point{btnRect.x + 30.0f, btnRect.y + 15.0f});
    webHost.RequestWakeFrame();
    for (int i = 0; i < 12; ++i) {
        webHost.TickUi(0.016f, aero::UiPhase_All);
    }
    float exitedScale = 0.0f;
    (void)aero::TryGetFloat(banner->GetValue(aero::PropId_AnimScaleX), exitedScale);
    AERO_CHECK(ApproxEq(exitedScale, 1.0f, 0.01f), "ExitActions restored HeroBanner ScaleX to 1.0x");
    AERO_CHECK(ColorApproxEq(banner->GetBackground(), aero::ParseXamlColorLiteral("#FF1E293B")),
               "HeroBanner restored CardNormalBrush on hover exit");
    AERO_CHECK(ColorApproxEq(ReadElemBg(vsmBtn), aero::ParseXamlColorLiteral("#FF0EA5E9")),
               "VsmBtn CommonStates.MouseOver ColorAnimation transitioned Background to #FF0EA5E9");

    // Imperative GoToVisualState("Expanded") on VsmBtn
    AERO_CHECK(styleEngine.GoToVisualState(vsmBtn, "Expanded", true),
               "GoToVisualState('Expanded') succeeded on VsmBtn");
    webHost.RequestWakeFrame();
    webHost.TickUi(0.016f, aero::UiPhase_All);
    AERO_CHECK(ApproxEq(vsmBtn->FinalRect().width, 240.0f, 0.1f),
               "VisualState 'Expanded' updated VsmBtn layout width to 240px");

    std::cout << "[PASS] TestEndToEndXamlAxb4AotVisualStatesAndHostIdleSleepWake\n";
}

// ============================================================================
// 7. Regression Tests for Phase 5C Review Edge Cases
// ============================================================================
void TestPhase5CReviewEdgeCasesAndRegressions() {
    aero::AxbTemplateRegistry::Instance().Clear();

    // 7A. Inline <Element.Style><Style>...</Style></Element.Style>, Style + VSM chaining,
    //     <DoubleAnimation By="...">, and <PowerEase Power="4"/>
    const char* xaml = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            Width="300" Height="200">
    <StackPanel.Resources>
        <Style x:Key="BaseChipStyle" TargetType="Border">
            <Setter Property="CornerRadius" Value="12"/>
        </Style>
        <Storyboard x:Key="ByDeltaSb">
            <DoubleAnimation Storyboard.TargetName="InlineStyledBox"
                             Storyboard.TargetProperty="Width"
                             From="100" By="50" Duration="0:0:0.1">
                <DoubleAnimation.EasingFunction>
                    <PowerEase Power="4" EasingMode="EaseIn"/>
                </DoubleAnimation.EasingFunction>
            </DoubleAnimation>
        </Storyboard>
    </StackPanel.Resources>

    <Border x:Name="InlineStyledBox" Width="80" Height="40">
        <Border.Style>
            <Style TargetType="Border" BasedOn="{StaticResource BaseChipStyle}">
                <Setter Property="Background" Value="#FF10B981"/>
            </Style>
        </Border.Style>
    </Border>

    <Border x:Name="UnstyledSibling" Width="80" Height="40" Background="#FF000000"/>

    <Border x:Name="VsmWithStyleBox" Width="120" Height="40" Style="{StaticResource BaseChipStyle}">
        <VisualStateManager.VisualStateGroups>
            <VisualStateGroup x:Name="SizeStates">
                <VisualStateGroup.Transitions>
                    <VisualTransition GeneratedDuration="0:0:0.2"/>
                </VisualStateGroup.Transitions>
                <VisualState x:Name="Collapsed">
                    <VisualState.Setters>
                        <Setter Property="Width" Value="160"/>
                    </VisualState.Setters>
                </VisualState>
                <VisualState x:Name="Expanded">
                    <VisualState.Setters>
                        <Setter Property="Width" Value="240"/>
                    </VisualState.Setters>
                </VisualState>
                <VisualState x:Name="DefaultSize"/>
            </VisualStateGroup>
        </VisualStateManager.VisualStateGroups>
    </Border>
</StackPanel>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "ReviewEdgeCases.xaml";
    input.xamlContent = xaml;
    input.emitAotCpp = true;

    const aero::AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled ReviewEdgeCases.xaml cleanly");
    AERO_CHECK(res.axbPackage.styles.size() == 3u,
               "Inline <Border.Style><Style> merged into synthetic style without leaking extra implicit style");

    aero::AxbPackage decoded{};
    std::string decodeErr;
    AERO_CHECK(aero::AxbPackage::Deserialize(res.axbBytes, decoded, &decodeErr),
               "Round-trip deserialized ReviewEdgeCases .axb4");

    auto root = aero::MakeIntrusive<aero::StackPanel>();
    aero::ExecutionDomainContext domains;
    aero::DualTrackBindingEngine bindingEngine(domains);
    aero::AnimationEngine animEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine);

    AERO_CHECK(aero::MountAxbTemplate(root.Get(), decoded, &domains, &bindingEngine),
               "Mounted ReviewEdgeCases .axb4");
    styleEngine.RegisterPackageStyles(decoded);
    (void)styleEngine.BindSubtreeStyles(root.Get());
    (void)styleEngine.EvaluateAndApply();

    auto* inlineBox = root->FindName("InlineStyledBox")->DynamicCast<aero::Border>();
    auto* sibling = root->FindName("UnstyledSibling")->DynamicCast<aero::Border>();
    auto* vsmBox = root->FindName("VsmWithStyleBox")->DynamicCast<aero::Border>();
    AERO_CHECK(inlineBox != nullptr && sibling != nullptr && vsmBox != nullptr,
               "Resolved InlineStyledBox, UnstyledSibling, VsmWithStyleBox");

    AERO_CHECK(ColorApproxEq(inlineBox->GetBackground(), aero::ParseXamlColorLiteral("#FF10B981")) &&
                   ApproxEq(inlineBox->GetCornerRadius().topLeft, 12.0f),
               "Inline <Border.Style><Style> applied both own Setter and BasedOn BaseChipStyle Setter");
    AERO_CHECK(ColorApproxEq(sibling->GetBackground(), aero::ParseXamlColorLiteral("#FF000000")) &&
                   ApproxEq(sibling->GetCornerRadius().topLeft, 0.0f),
               "UnstyledSibling was not polluted by inline <Border.Style>");
    AERO_CHECK(ApproxEq(vsmBox->GetCornerRadius().topLeft, 12.0f),
               "VsmWithStyleBox chained inline VSM synthetic style onto explicit Style='{StaticResource BaseChipStyle}'");

    // Verify By="50" with From="100" and PowerEase Power="4" (QuarticEase EaseIn)
    const aero::AxbStoryboard* bySb = styleEngine.FindStoryboard("ByDeltaSb");
    AERO_CHECK(bySb != nullptr && bySb->timelines.size() == 1u &&
                   bySb->timelines[0].isBy &&
                   bySb->timelines[0].easingKind == aero::EasingFunctionKind::Quartic,
               "ByDeltaSb preserved isBy=true and mapped PowerEase Power=4 to Quartic");
    (void)animEngine.BeginStoryboard(root.Get(), *bySb);
    for (int i = 0; i < 10; ++i) {
        (void)animEngine.Tick(0.016f);
    }
    AERO_CHECK(ApproxEq(inlineBox->GetWidth(), 150.0f, 0.05f),
               "DoubleAnimation From=100 By=50 converged to 150.0");

    // 7B. VisualTransition between two VisualState.Setters states without jumping back to base value,
    //     and with zero heap allocations
    AERO_CHECK(styleEngine.GoToVisualState(vsmBox, "Collapsed", false), "Entered Collapsed without transitions");
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(vsmBox->GetWidth(), 160.0f, 0.05f), "Collapsed state set Width=160");

    gHeapAllocCount.store(0u, std::memory_order_relaxed);
    gCountHeapAllocs.store(true, std::memory_order_relaxed);
    AERO_CHECK(styleEngine.GoToVisualState(vsmBox, "Expanded", true), "Transitioned Collapsed -> Expanded");
    (void)styleEngine.EvaluateAndApply();
    (void)animEngine.Tick(0.1f); // halfway through 0.2s linear transition: should be ~200, NOT jumping back to 120!
    gCountHeapAllocs.store(false, std::memory_order_relaxed);

    AERO_CHECK(gHeapAllocCount.load(std::memory_order_relaxed) == 0u,
               "VisualTransition setter interpolation incurred 0 heap allocations");
    AERO_CHECK(ApproxEq(vsmBox->GetWidth(), 200.0f, 2.0f),
               "VisualTransition interpolated smoothly from 160 -> 240 (midway ~200) instead of snapping to 120");

    // Switching to DefaultSize (no Width setter) while transition is mid-flight stops animation & restores base 120
    AERO_CHECK(styleEngine.GoToVisualState(vsmBox, "DefaultSize", false), "Switched to DefaultSize");
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(vsmBox->GetWidth(), 120.0f, 0.05f),
               "Switching to DefaultSize stopped mid-flight transition and restored base Width=120");

    // 7C. Fast-inline ValueSourceRank precedence (Local rank vs ThemeStyle & StyleTrigger)
    auto rankedBtn = aero::MakeIntrusive<aero::Button>();
    (void)rankedBtn->SetValue(aero::PropId_Opacity, aero::MakeFloat(0.9f), aero::ValueSourceRank::Local);
    (void)rankedBtn->SetValue(aero::PropId_Focusable, aero::UnifiedValue{false}, aero::ValueSourceRank::Local);

    aero::AxbStyleDefinition implicitBtnStyle{};
    implicitBtnStyle.targetTypeName = "Button";
    implicitBtnStyle.setters.push_back(aero::AxbPropertySetter{
        "", aero::PropId_Opacity, aero::MakeFloat(0.4f)});
    implicitBtnStyle.setters.push_back(aero::AxbPropertySetter{
        "", aero::PropId_Focusable, aero::UnifiedValue{true}});
    aero::AxbStyleTrigger hoverTrig{};
    hoverTrig.conditions.push_back(aero::AxbTriggerCondition{
        aero::PropId_TriggerIsMouseOver, aero::UnifiedValue{true}});
    hoverTrig.overrideLocalValues = false;
    hoverTrig.setters.push_back(aero::AxbPropertySetter{
        "", aero::PropId_Opacity, aero::MakeFloat(0.2f)});
    implicitBtnStyle.triggers.push_back(hoverTrig);

    aero::AxbTemplateRegistry::Instance().RegisterStyle(implicitBtnStyle);
    aero::StyleTriggerEngine globalLookupEngine(&animEngine);
    AERO_CHECK(globalLookupEngine.BindSubtreeStyles(rankedBtn.Get()) == 1u,
               "StyleTriggerEngine resolved implicit Button style from AxbTemplateRegistry");
    (void)globalLookupEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(rankedBtn->GetOpacity(), 0.9f, 1e-4f) && !rankedBtn->IsFocusable(),
               "Local fast-inline Opacity=0.9 and Focusable=false were not clobbered by ThemeStyle");
    rankedBtn->SetHovered(true);
    (void)globalLookupEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(rankedBtn->GetOpacity(), 0.9f, 1e-4f),
               "Local fast-inline Opacity=0.9 was not clobbered by StyleTrigger (overrideLocalValues=false)");

    // 7D. DesktopEventHost layout pass when renderTree == nullptr
    aero::DepthBucketLayoutQueue layoutQueue;
    auto hostRoot = aero::MakeIntrusive<aero::Border>();
    hostRoot->SetWidth(180.0f);
    hostRoot->SetHeight(90.0f);
    aero::DesktopEventHost headlessHost(hostRoot.Get(), nullptr, &layoutQueue, nullptr);
    headlessHost.SetViewportSize(aero::Size{400.0f, 300.0f});
    AERO_CHECK(!headlessHost.CanSleepIdle(),
               "Headless DesktopEventHost (renderTree=nullptr) detects rootVisual MeasureDirty");
    headlessHost.TickUi(0.016f, aero::UiPhase_All);
    AERO_CHECK(headlessHost.Telemetry().layoutPassesRun == 1u && headlessHost.CanSleepIdle(),
               "Headless DesktopEventHost executed layout pass and entered idle sleep");

    std::cout << "[PASS] TestPhase5CReviewEdgeCasesAndRegressions\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    TestZeroAllocStyleTriggerAndAnimationSteadyState();
    TestStyleBasedOnInheritanceTriggerPrecedenceAndMultiTriggerRestore();
    TestAllElevenEasingCurvesTweenRepeatAutoReverseAndFillStop();
    TestKeyFramesAnimatedRenderTransformCompositionAndPagedRenderTree();
    TestVelocityContinuousSpringInterruptionAndConvergence();
    TestEndToEndXamlAxb4AotVisualStatesAndHostIdleSleepWake();
    TestPhase5CReviewEdgeCasesAndRegressions();

    std::cout << "AeroGUI 2.0 Phase 5C Style/Trigger, VisualStateManager & Animation Tests Passed 100%!\n";
    return 0;
}
