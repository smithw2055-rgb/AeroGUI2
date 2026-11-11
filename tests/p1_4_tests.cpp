#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace {

int gChecksPassed = 0;
int gChecksFailed = 0;

void CheckImpl(bool cond, const char* expr, const char* msg, int line) {
    if (cond) {
        ++gChecksPassed;
    } else {
        ++gChecksFailed;
        std::fprintf(stderr, "[FAIL] Line %d: %s (%s)\n", line, msg, expr);
    }
}

#define AERO_CHECK(cond, msg) CheckImpl((cond), #cond, (msg), __LINE__)

[[nodiscard]] bool ApproxEq(float a, float b, float eps = 1e-3f) noexcept {
    return std::fabs(a - b) <= eps;
}

std::string ReadFileUtf8(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

template <typename T>
T* FindControlAs(aero::UIElement* root, std::string_view name) noexcept {
    if (!root) return nullptr;
    aero::UIElement* found = root->FindName(name);
    return found ? found->DynamicCast<T>() : nullptr;
}

// ============================================================================
// Suite (a): <b:Interaction.Triggers> (EventTrigger, DataTrigger w/ GreaterThan,
//            ChangePropertyAction, ControlStoryboardAction Play/Stop/TogglePlayPause,
//            InvokeCommandAction [UI & SimCommand], PlaySoundAction)
// ============================================================================
void TestInteractionTriggersAndActionsEndToEnd() {
    std::printf("--- [Suite A] Testing <b:Interaction.Triggers> & Core Actions End-to-End ---\n");

    // Verify reserved enum allocations strictly match P1-4 specification
    static_assert(static_cast<std::uint8_t>(aero::AxbOpcode::BindMultiProperty) == 11u);
    static_assert(static_cast<std::uint8_t>(aero::ValueConverterKind::MathExpression) == 6u);
    static_assert(static_cast<std::uint8_t>(aero::ValueConverterKind::CustomScriptOrHost) == 7u);
    AERO_CHECK(
        static_cast<std::uint8_t>(aero::AxbOpcode::BindMultiProperty) == 11u &&
        static_cast<std::uint8_t>(aero::ValueConverterKind::MathExpression) == 6u &&
        static_cast<std::uint8_t>(aero::ValueConverterKind::CustomScriptOrHost) == 7u,
        "Reserved P1-4 enum values match specification");

    const char* rsCode = R"(
module Game.InteractionTest;

public class BattleActionVm
{
    [Observable] public int Rage;
    [Observable] public int ComboCount;
    [Observable] public int LastCommandBonus;

    public void AddCombo(int delta)
    {
        ComboCount = ComboCount + delta;
        LastCommandBonus = delta;
    }

    [SimCommand]
    public void TriggerUltimate(int power)
    {
        Rage = 0;
    }
}
)";

    const char* xaml = R"(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
             x:DataType="Game.InteractionTest::BattleActionVm"
             Width="400" Height="300">
  <UserControl.Resources>
    <Storyboard x:Key="PulseAnim">
      <DoubleAnimation Storyboard.TargetName="BannerBox"
                       Storyboard.TargetProperty="Opacity"
                       From="0.2" To="1.0" Duration="0:0:1"/>
    </Storyboard>
  </UserControl.Resources>

  <StackPanel x:Name="RootPanel">
    <Border x:Name="BannerBox" Width="200" Height="40" Opacity="0.5">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Loaded">
          <b:ChangePropertyAction PropertyName="Tag" Value="LoadedSelfOk"/>
          <b:PlaySoundAction Source="Audio/UiReady.wav" Volume="0.4"/>
        </b:EventTrigger>
        <b:EventTrigger EventName="MouseEnter">
          <b:ChangePropertyAction PropertyName="Width" Value="260"/>
        </b:EventTrigger>
        <b:EventTrigger EventName="MouseLeave">
          <b:ChangePropertyAction PropertyName="Width" Value="200"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
      <TextBlock x:Name="AlertLabel" Text="CALM"/>
    </Border>

    <CheckBox x:Name="MuteToggle" Content="Mute">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Checked">
          <b:ChangePropertyAction TargetName="AlertLabel" PropertyName="Text" Value="MUTED"/>
        </b:EventTrigger>
        <b:EventTrigger EventName="Unchecked">
          <b:ChangePropertyAction TargetName="AlertLabel" PropertyName="Text" Value="UNMUTED"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </CheckBox>

    <Button x:Name="PlayBtn" Content="Play">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Click">
          <b:ControlStoryboardAction Storyboard="{StaticResource PulseAnim}" ControlStoryboardOption="Play"/>
          <b:InvokeCommandAction Command="{Binding AddCombo}" CommandParameter="5"/>
          <b:PlaySoundAction Source="Audio/Click.wav" Volume="0.75"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </Button>

    <Button x:Name="TogglePauseBtn" Content="PauseResume">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Click">
          <b:ControlStoryboardAction Storyboard="{StaticResource PulseAnim}" ControlStoryboardOption="TogglePlayPause"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </Button>

    <Button x:Name="StopBtn" Content="Stop">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Click">
          <b:ControlStoryboardAction Storyboard="{StaticResource PulseAnim}" ControlStoryboardOption="Stop"/>
          <b:InvokeCommandAction Command="{Binding TriggerUltimate}" CommandParameter="99"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </Button>

    <TextBlock x:Name="RageStatus" Text="NORMAL">
      <b:Interaction.Triggers>
        <b:DataTrigger Binding="{Binding Rage}" Comparison="GreaterThan" Value="50">
          <b:ChangePropertyAction PropertyName="Text" Value="OVERDRIVE!"/>
          <b:ChangePropertyAction TargetName="BannerBox" PropertyName="Width" Value="320"/>
          <b:PlaySoundAction Source="Audio/Overdrive.ogg" Volume="0.9"/>
        </b:DataTrigger>
        <b:DataTrigger Binding="{Binding Rage}" Comparison="LessThanOrEqual" Value="10">
          <b:ChangePropertyAction PropertyName="Text" Value="COOLING"/>
        </b:DataTrigger>
      </b:Interaction.Triggers>
    </TextBlock>
  </StackPanel>
</UserControl>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "BattleInteraction.xaml";
    in.xamlContent = xaml;
    in.scriptSources.push_back({"BattleActionVm.rs", rsCode});

    const aero::AeroCompileResult compiled = compiler.Compile(in);
    AERO_CHECK(compiled.succeeded && compiled.programImage != nullptr, "BattleInteraction XAML+RS compiled");
    if (!compiled.succeeded || !compiled.programImage) {
        for (const auto& d : compiled.diagnostics) {
            std::fprintf(stderr, "  diag: %s\n", d.c_str());
        }
        return;
    }

    // Verify full .axb4 binary Serialize/Deserialize round-trip of AP14 section
    aero::AxbPackage roundTripPkg{};
    std::string deserErr;
    AERO_CHECK(
        aero::AxbPackage::Deserialize(compiled.axbBytes, roundTripPkg, &deserErr),
        "AP14 binary section deserializes cleanly");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    auto uiHeapPtr = std::shared_ptr<aero::ManagedHeap>(&domains.uiHeap(), [](aero::ManagedHeap*) {});
    aero::Interpreter uiInterp(compiled.programImage, uiHeapPtr);
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::Ui);

    const aero::SymbolId vmTypeId = aero::rs_sem::stableTypeId("Game.InteractionTest::BattleActionVm");
    const auto* vmTypeSym = compiled.programImage->resolveType(vmTypeId);
    AERO_CHECK(vmTypeSym != nullptr, "Resolved BattleActionVm TypeSymbol");
    auto vmObjOpt = domains.uiHeap().allocateObject(vmTypeId, std::vector<aero::UnifiedValue>(vmTypeSym ? vmTypeSym->fields.size() : 3u), {});
    AERO_CHECK(vmObjOpt.has_value(), "Allocated BattleActionVm on UiHeap");
    const aero::ObjectRef vmObj = *vmObjOpt;

    auto rageSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "Rage");
    auto comboSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "ComboCount");
    AERO_CHECK(rageSlot.has_value() && comboSlot.has_value(), "Resolved Rage & ComboCount slots");
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *rageSlot, aero::value::FromInt(25), &uiInterp);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *comboSlot, aero::value::FromInt(0), &uiInterp);

    aero::AnimationEngine animEngine;
    aero::DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterp);
    bindingEngine.SetAnimationEngine(&animEngine);
    aero::StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);

    struct SoundEventRecord {
        std::string uri;
        float volume = 0.0f;
    };
    std::vector<SoundEventRecord> playedSounds;
    styleEngine.SetSoundActionCallback([&](aero::UIElement*, std::string_view uri, float vol) {
        playedSounds.push_back({std::string(uri), vol});
    });

    auto root = aero::MakeIntrusive<aero::UserControl>();
    root->SetValue(aero::PropId_DataContext, vmObj, aero::ValueSourceRank::Local);
    AERO_CHECK(
        aero::MountAxbTemplate(
            root.Get(),
            roundTripPkg,
            &domains,
            &bindingEngine,
            vmObj,
            &uiInterp,
            nullptr,
            &animEngine),
        "Mounted round-tripped BattleInteraction package");

    styleEngine.RegisterPackageStyles(roundTripPkg);
    (void)styleEngine.BindSubtreeStyles(root.Get());

    auto* banner = FindControlAs<aero::Border>(root.Get(), "BannerBox");
    auto* alertLabel = FindControlAs<aero::TextBlock>(root.Get(), "AlertLabel");
    auto* muteToggle = FindControlAs<aero::CheckBox>(root.Get(), "MuteToggle");
    auto* playBtn = FindControlAs<aero::Button>(root.Get(), "PlayBtn");
    auto* togglePauseBtn = FindControlAs<aero::Button>(root.Get(), "TogglePauseBtn");
    auto* stopBtn = FindControlAs<aero::Button>(root.Get(), "StopBtn");
    auto* rageStatus = FindControlAs<aero::TextBlock>(root.Get(), "RageStatus");
    AERO_CHECK(
        banner && alertLabel && muteToggle && playBtn && togglePauseBtn && stopBtn && rageStatus,
        "Resolved all named controls in BattleInteraction");

    // 1. Initial EvaluateAndApply: fires Loaded EventTrigger (self ChangePropertyAction + PlaySoundAction)
    (void)styleEngine.EvaluateAndApply();
    const auto tagVal = banner->GetValue(aero::PropId_Tag);
    AERO_CHECK(
        std::holds_alternative<aero::StringValue>(tagVal) &&
        std::get<aero::StringValue>(tagVal).view() == "LoadedSelfOk",
        "Loaded EventTrigger ChangePropertyAction without TargetName mutated host element itself");
    AERO_CHECK(
        playedSounds.size() == 1u && playedSounds[0].uri == "Audio/UiReady.wav" &&
        ApproxEq(playedSounds[0].volume, 0.4f),
        "Loaded EventTrigger PlaySoundAction invoked SoundActionCallback");
    AERO_CHECK(rageStatus->GetText() == "NORMAL", "Rage=25 matches neither >50 nor <=10 initially");

    // 2. MouseEnter / MouseLeave & Checked / Unchecked EventTriggers
    banner->SetHovered(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(banner->GetWidth(), 260.0f), "MouseEnter EventTrigger updated Width to 260");
    banner->SetHovered(false);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(ApproxEq(banner->GetWidth(), 200.0f), "MouseLeave EventTrigger updated Width to 200");

    muteToggle->SetIsChecked(true);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(alertLabel->GetText() == "MUTED", "Checked EventTrigger updated target TextBlock");
    muteToggle->SetIsChecked(false);
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(alertLabel->GetText() == "UNMUTED", "Unchecked EventTrigger updated target TextBlock");

    // 3. Click PlayBtn -> ControlStoryboardAction(Play) + InvokeCommandAction(AddCombo, 5) + PlaySoundAction
    (void)playBtn->PerformClick();
    (void)styleEngine.EvaluateAndApply();
    const aero::AxbStoryboard* pulseSb = styleEngine.FindStoryboard("PulseAnim");
    AERO_CHECK(pulseSb != nullptr && animEngine.IsStoryboardActive(playBtn, *pulseSb), "PulseAnim started playing");
    aero::UnifiedValue comboNow{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmObj, *comboSlot, comboNow, nullptr, &uiInterp);
    AERO_CHECK(
        std::holds_alternative<std::int64_t>(comboNow) && std::get<std::int64_t>(comboNow) == 5,
        "InvokeCommandAction invoked RealScript AddCombo(5) on UiHeap ViewModel");
    AERO_CHECK(
        playedSounds.size() == 2u && playedSounds[1].uri == "Audio/Click.wav" &&
        ApproxEq(playedSounds[1].volume, 0.75f),
        "Click PlaySoundAction fired with Volume=0.75");

    // Advance storyboard halfway (0.5s -> Opacity between 0.2 and 1.0)
    (void)animEngine.Tick(0.5f);
    const float midOpacity = banner->GetOpacity();
    AERO_CHECK(midOpacity > 0.45f && midOpacity < 0.95f, "PulseAnim interpolated BannerBox Opacity");

    // 4. Click TogglePauseBtn -> pauses storyboard; Tick(0.4f) must keep Opacity frozen!
    (void)togglePauseBtn->PerformClick();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(animEngine.IsStoryboardPaused(togglePauseBtn, *pulseSb), "TogglePlayPause paused active storyboard");
    (void)animEngine.Tick(0.4f);
    AERO_CHECK(ApproxEq(banner->GetOpacity(), midOpacity, 1e-4f), "Paused storyboard holds exact frozen value");

    // Click TogglePauseBtn again -> resumes storyboard; Tick(0.25f) advances Opacity further!
    (void)togglePauseBtn->PerformClick();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(!animEngine.IsStoryboardPaused(togglePauseBtn, *pulseSb), "Second TogglePlayPause resumed storyboard");
    (void)animEngine.Tick(0.25f);
    AERO_CHECK(banner->GetOpacity() > midOpacity + 0.05f, "Resumed storyboard advances BannerBox Opacity");

    // 5. Click StopBtn -> stops storyboard (restoring pre-animation base Opacity 0.5) & enqueues [SimCommand]
    (void)stopBtn->PerformClick();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(!animEngine.IsStoryboardActive(stopBtn, *pulseSb), "ControlStoryboardOption::Stop stopped storyboard");
    AERO_CHECK(ApproxEq(banner->GetOpacity(), 0.5f, 1e-3f), "Stopped storyboard restored pre-animation base Opacity");
    auto simCmds = domains.consumeSimCommands();
    AERO_CHECK(
        simCmds.size() == 1u && simCmds[0].payload.size() == 1u &&
        std::holds_alternative<std::int64_t>(simCmds[0].payload[0]) &&
        std::get<std::int64_t>(simCmds[0].payload[0]) == 99,
        "InvokeCommandAction marshaled [SimCommand] TriggerUltimate(99) into SimCommandEnvelope");

    // 6. DataTrigger with Comparison="GreaterThan" (Rage > 50) & imperative persistence
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *rageSlot, aero::value::FromInt(75), &uiInterp);
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(rageStatus->GetText() == "OVERDRIVE!", "b:DataTrigger Comparison=GreaterThan fired at Rage=75");
    AERO_CHECK(ApproxEq(banner->GetWidth(), 320.0f), "b:DataTrigger ChangePropertyAction mutated BannerBox.Width=320");
    AERO_CHECK(
        playedSounds.size() == 3u && playedSounds[2].uri == "Audio/Overdrive.ogg",
        "b:DataTrigger PlaySoundAction fired on entering Rage > 50");

    // When Rage drops to 30 (neither >50 nor <=10), imperative ChangePropertyAction persists ("OVERDRIVE!")
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *rageSlot, aero::value::FromInt(30), &uiInterp);
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(
        rageStatus->GetText() == "OVERDRIVE!",
        "Imperative ChangePropertyAction value persists when b:DataTrigger exits");

    // When Rage drops to 8 (<= 10), second inequality DataTrigger updates text to "COOLING"
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *rageSlot, aero::value::FromInt(8), &uiInterp);
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();
    AERO_CHECK(rageStatus->GetText() == "COOLING", "b:DataTrigger Comparison=LessThanOrEqual fired at Rage=8");
}

// ============================================================================
// Suite (b): <MultiBinding StringFormat="HP: {0:F0}/{1:F0} ({2:0}%)"> + MathConverter
//            & Single-Binding StringFormat Live Reactive Evaluation
// ============================================================================
void TestMultiBindingStringFormatAndMathConverter() {
    std::printf("--- [Suite B] Testing MultiBinding + StringFormat + MathConverter Live Updates ---\n");

    // Direct unit checks on recursive-descent MathConverter & zero-exception FormatBindingString
    {
        const aero::UnifiedValue vals[3] = {
            aero::MakeFloat(75.0f),
            aero::MakeFloat(150.0f),
            aero::MakeFloat(-12.4f)};
        float r = 0.0f;
        AERO_CHECK(
            aero::EvaluateMathConverterExpression("clamp(round(({0}/{1})*100), 0, 100)", vals, 3u, 0.0f, r) &&
            ApproxEq(r, 50.0f),
            "MathConverter evaluates nested functions & arithmetic");
        AERO_CHECK(
            aero::EvaluateMathConverterExpression("@VALUE * 2 + abs({2}) > 160 ? max(10, 25) : min(5, 9)", vals, 3u, 0.0f, r) &&
            ApproxEq(r, 25.0f),
            "MathConverter evaluates @VALUE, abs, relational & ternary operator");
        AERO_CHECK(
            aero::EvaluateMathConverterExpression("{}pow({0}/75, 2) * 10 + (17 % 5)", vals, 3u, 0.0f, r) &&
            ApproxEq(r, 12.0f),
            "MathConverter evaluates {} escape prefix, pow(), and modulo %");
        AERO_CHECK(
            aero::EvaluateMathConverterExpression("{0} / 0", vals, 3u, 0.0f, r) &&
            ApproxEq(r, 0.0f),
            "MathConverter handles division by zero safely without NaN or exception");

        const aero::UnifiedValue fmtArgs[2] = {aero::MakeFloat(0.856f), aero::value::FromInt(42)};
        AERO_CHECK(
            aero::FormatBindingString("Crit: {0:P0} | {1:000}", fmtArgs, 2u) == "Crit: 86% | 042",
            "FormatBindingString formats P0 and 000 specifiers");
        AERO_CHECK(
            aero::FormatBindingString("Val={0:F2}", fmtArgs, 2u) == "Val=0.86",
            "FormatBindingString formats F2 specifier");
    }

    const char* rsHud = R"(
module Game.HudTest;

public class PlayerHudVm
{
    [Observable] public float Hp;
    [Observable] public float MaxHp;
    [Observable] public int Score;
}
)";

    const char* xamlHud = R"XAML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
             x:DataType="Game.HudTest::PlayerHudVm"
             Width="400" Height="200">
  <UserControl.Resources>
    <noesis:MathConverter x:Key="BarWidthConverter" Expression="clamp(({0}/{1})*240, 0, 240)"/>
  </UserControl.Resources>
  <StackPanel>
    <TextBlock x:Name="ScoreLabel" Text="{Binding Score, StringFormat='Score: {0:000}'}"/>
    <TextBlock x:Name="HpStatusText">
      <TextBlock.Text>
        <MultiBinding StringFormat="HP: {0:F0}/{1:F0} ({2:0}%)">
          <Binding Path="Hp"/>
          <Binding Path="MaxHp"/>
          <MultiBinding>
            <MultiBinding.Converter>
              <noesis:MathConverter Expression="clamp(({0}/{1})*100, 0, 100)"/>
            </MultiBinding.Converter>
            <Binding Path="Hp"/>
            <Binding Path="MaxHp"/>
          </MultiBinding>
        </MultiBinding>
      </TextBlock.Text>
    </TextBlock>
    <Border x:Name="HpFillBar" Height="16">
      <Border.Width>
        <MultiBinding Converter="{StaticResource BarWidthConverter}">
          <Binding Path="Hp"/>
          <Binding Path="MaxHp"/>
        </MultiBinding>
      </Border.Width>
    </Border>
  </StackPanel>
</UserControl>
)XAML";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "PlayerHud.xaml";
    in.xamlContent = xamlHud;
    in.scriptSources.push_back({"PlayerHudVm.rs", rsHud});

    const aero::AeroCompileResult compiled = compiler.Compile(in);
    AERO_CHECK(compiled.succeeded && compiled.programImage != nullptr, "PlayerHud XAML+RS compiled");
    if (!compiled.succeeded || !compiled.programImage) {
        return;
    }

    bool hasBindMultiOpcode = false;
    for (const auto& inst : compiled.axbPackage.instructions) {
        if (inst.opcode == aero::AxbOpcode::BindMultiProperty) {
            hasBindMultiOpcode = true;
            break;
        }
    }
    AERO_CHECK(hasBindMultiOpcode && compiled.axbPackage.multiBindings.size() == 2u,
               "Compiler emitted BindMultiProperty (11) for both MultiBindings");

    aero::AxbPackage decodedPkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(compiled.axbBytes, decodedPkg), "Deserialized MultiBinding package");

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    auto uiHeapPtr = std::shared_ptr<aero::ManagedHeap>(&domains.uiHeap(), [](aero::ManagedHeap*) {});
    aero::Interpreter uiInterp(compiled.programImage, uiHeapPtr);
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::Ui);

    const aero::SymbolId vmTypeId = aero::rs_sem::stableTypeId("Game.HudTest::PlayerHudVm");
    const auto* vmTypeSym = compiled.programImage->resolveType(vmTypeId);
    auto vmObjOpt = domains.uiHeap().allocateObject(vmTypeId, std::vector<aero::UnifiedValue>(vmTypeSym ? vmTypeSym->fields.size() : 3u), {});
    AERO_CHECK(vmObjOpt.has_value(), "Allocated PlayerHudVm");
    const aero::ObjectRef vmObj = *vmObjOpt;

    auto hpSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "Hp");
    auto maxHpSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "MaxHp");
    auto scoreSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "Score");
    AERO_CHECK(hpSlot && maxHpSlot && scoreSlot, "Resolved Hp, MaxHp, Score slots");

    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *hpSlot, aero::MakeFloat(75.0f), &uiInterp);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *maxHpSlot, aero::MakeFloat(100.0f), &uiInterp);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *scoreSlot, aero::value::FromInt(42), &uiInterp);
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(vmObj);

    aero::DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterp);
    auto root = aero::MakeIntrusive<aero::UserControl>();
    AERO_CHECK(
        aero::MountAxbTemplate(root.Get(), decodedPkg, &domains, &bindingEngine, vmObj, &uiInterp),
        "Mounted PlayerHud package");

    auto* scoreLabel = FindControlAs<aero::TextBlock>(root.Get(), "ScoreLabel");
    auto* hpStatusText = FindControlAs<aero::TextBlock>(root.Get(), "HpStatusText");
    auto* hpFillBar = FindControlAs<aero::Border>(root.Get(), "HpFillBar");
    AERO_CHECK(scoreLabel && hpStatusText && hpFillBar, "Found ScoreLabel, HpStatusText, HpFillBar");

    AERO_CHECK(scoreLabel->GetText() == "Score: 042", "Initial single-binding StringFormat='Score: {0:000}'");
    AERO_CHECK(hpStatusText->GetText() == "HP: 75/100 (75%)", "Initial MultiBinding + MathConverter text");
    AERO_CHECK(ApproxEq(hpFillBar->GetWidth(), 180.0f), "Initial Resource MathConverter width (75/100 * 240 = 180)");

    // Mutate Hp -> 36, Score -> 7 -> FlushTrackA must recompute both!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *hpSlot, aero::MakeFloat(36.0f), &uiInterp);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *scoreSlot, aero::value::FromInt(7), &uiInterp);
    const std::size_t flushed1 = bindingEngine.FlushTrackA();
    AERO_CHECK(flushed1 >= 3u, "FlushTrackA updated dirty single & multi bindings");
    AERO_CHECK(scoreLabel->GetText() == "Score: 007", "Reactive StringFormat updated Score to 'Score: 007'");
    AERO_CHECK(hpStatusText->GetText() == "HP: 36/100 (36%)", "Reactive MultiBinding updated on Hp mutation");
    AERO_CHECK(ApproxEq(hpFillBar->GetWidth(), 86.4f, 0.05f), "Reactive MathConverter updated Width to 86.4");

    // Mutate MaxHp -> 150 -> FlushTrackA must recompute percentage (36/150 = 24%) and bar width (57.6)!
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *maxHpSlot, aero::MakeFloat(150.0f), &uiInterp);
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(hpStatusText->GetText() == "HP: 36/150 (24%)", "Reactive MultiBinding updated on MaxHp mutation");
    AERO_CHECK(ApproxEq(hpFillBar->GetWidth(), 57.6f, 0.05f), "Reactive MathConverter updated Width on MaxHp mutation");
}

// ============================================================================
// Suite (c): Custom RealScript SymbolId & C++ Callback IValueConverter
//            Forward Convert and TwoWay ConvertBack
// ============================================================================
void TestCustomScriptAndHostValueConvertersTwoWay() {
    std::printf("--- [Suite C] Testing Custom RealScript & C++ IValueConverter (Convert & TwoWay ConvertBack) ---\n");

    const char* rsConv = R"(
module Game.Converters;

public class SettingsVm
{
    [Observable] public float Celsius;
    [Observable] public int RankLevel;
}

public class ThermalConverters
{
    public static float CelsiusToFahrenheit(float c)
    {
        return c * 1.8f + 32.0f;
    }

    public static float FahrenheitToCelsius(float f)
    {
        return (f - 32.0f) / 1.8f;
    }
}
)";

    const char* xamlConv = R"(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:DataType="Game.Converters::SettingsVm"
             Width="320" Height="160">
  <StackPanel>
    <Slider x:Name="TempSlider" Minimum="0" Maximum="300"
            Value="{Binding Celsius, Mode=TwoWay, Converter={StaticResource TempScriptConv}}"/>
    <TextBox x:Name="RankBox"
             Text="{Binding RankLevel, Mode=TwoWay, Converter={StaticResource RankHostConv}}"/>
  </StackPanel>
</UserControl>
)";

    aero::AeroCompiler compiler;
    aero::AeroCompileInput in{};
    in.xamlPath = "ConverterSettings.xaml";
    in.xamlContent = xamlConv;
    in.scriptSources.push_back({"Converters.rs", rsConv});

    const aero::AeroCompileResult compiled = compiler.Compile(in);
    AERO_CHECK(compiled.succeeded && compiled.programImage != nullptr, "ConverterSettings XAML+RS compiled");
    if (!compiled.succeeded || !compiled.programImage) {
        for (const auto& d : compiled.diagnostics) {
            std::fprintf(stderr, "  diag: %s\n", d.c_str());
        }
        return;
    }

    AERO_CHECK(
        compiled.axbPackage.bindingChains.size() == 2u &&
        compiled.axbPackage.bindingChains[0].converter == aero::ValueConverterKind::CustomScriptOrHost &&
        compiled.axbPackage.bindingChains[1].converter == aero::ValueConverterKind::CustomScriptOrHost,
        "Bindings extracted as ValueConverterKind::CustomScriptOrHost (7)");

    aero::SymbolId fwdSym = 0;
    aero::SymbolId backSym = 0;
    for (const auto& mod : compiled.programImage->modules()) {
        for (const auto& fn : mod.functions) {
            if (fn.name.find("CelsiusToFahrenheit") != std::string::npos) {
                fwdSym = fn.symbolId;
            } else if (fn.name.find("FahrenheitToCelsius") != std::string::npos) {
                backSym = fn.symbolId;
            }
        }
    }
    AERO_CHECK(fwdSym != 0 && backSym != 0, "Resolved RealScript converter function SymbolIds");
    if (fwdSym == 0 || backSym == 0) {
        return;
    }

    aero::ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    auto uiHeapPtr = std::shared_ptr<aero::ManagedHeap>(&domains.uiHeap(), [](aero::ManagedHeap*) {});
    aero::Interpreter uiInterp(compiled.programImage, uiHeapPtr);
    uiInterp.setExecutionDomainContext(&domains, aero::DomainKind::Ui);

    const aero::SymbolId vmTypeId = aero::rs_sem::stableTypeId("Game.Converters::SettingsVm");
    const auto* vmTypeSym = compiled.programImage->resolveType(vmTypeId);
    auto vmObjOpt = domains.uiHeap().allocateObject(vmTypeId, std::vector<aero::UnifiedValue>(vmTypeSym ? vmTypeSym->fields.size() : 2u), {});
    AERO_CHECK(vmObjOpt.has_value(), "Allocated SettingsVm");
    const aero::ObjectRef vmObj = *vmObjOpt;

    auto celsiusSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "Celsius");
    auto rankSlot = domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "RankLevel");
    AERO_CHECK(celsiusSlot && rankSlot, "Resolved Celsius and RankLevel slots");

    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *celsiusSlot, aero::MakeFloat(25.0f), &uiInterp);
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *rankSlot, aero::value::FromInt(2), &uiInterp);
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(vmObj);

    aero::DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterp);

    // 1. Register RealScript SymbolId converter (forward + TwoWay reverse)
    bindingEngine.RegisterScriptValueConverter("TempScriptConv", fwdSym, backSym);

    // 2. Register C++ callback converter (forward + TwoWay reverse)
    bindingEngine.RegisterValueConverter(
        "RankHostConv",
        [](const aero::UnifiedValue& v, std::string_view) -> aero::UnifiedValue {
            std::int64_t code = 0;
            if (const auto* iv = std::get_if<std::int64_t>(&v)) code = *iv;
            if (code == 1) return aero::value::FromString("Bronze");
            if (code == 2) return aero::value::FromString("Silver");
            if (code >= 3) return aero::value::FromString("Gold");
            return aero::value::FromString("Unranked");
        },
        [](const aero::UnifiedValue& v, std::string_view) -> aero::UnifiedValue {
            if (const auto* sv = std::get_if<aero::StringValue>(&v)) {
                if (sv->view() == "Bronze") return aero::value::FromInt(1);
                if (sv->view() == "Silver") return aero::value::FromInt(2);
                if (sv->view() == "Gold") return aero::value::FromInt(3);
            }
            return aero::value::FromInt(0);
        });

    auto root = aero::MakeIntrusive<aero::UserControl>();
    AERO_CHECK(
        aero::MountAxbTemplate(root.Get(), compiled.axbPackage, &domains, &bindingEngine, vmObj, &uiInterp),
        "Mounted ConverterSettings package");

    auto* tempSlider = FindControlAs<aero::Slider>(root.Get(), "TempSlider");
    auto* rankBox = FindControlAs<aero::TextBox>(root.Get(), "RankBox");
    AERO_CHECK(tempSlider && rankBox, "Resolved TempSlider and RankBox");

    // Verify initial forward Convert (25 C -> 77 F; Rank 2 -> "Silver")
    AERO_CHECK(ApproxEq(tempSlider->GetSliderValue(), 77.0f, 0.05f),
               "RealScript SymbolId forward Convert: 25 C -> 77 F");
    AERO_CHECK(rankBox->GetText() == "Silver",
               "C++ callback forward Convert: RankLevel 2 -> 'Silver'");

    // Mutate ViewModel Celsius = 100.0f -> FlushTrackA -> Slider becomes 212.0f
    (void)domains.writePropertySlotFast(aero::DomainKind::Ui, vmObj, *celsiusSlot, aero::MakeFloat(100.0f), &uiInterp);
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(ApproxEq(tempSlider->GetSliderValue(), 212.0f, 0.05f),
               "RealScript SymbolId reactive forward Convert: 100 C -> 212 F");

    // Mutate UI Slider to 68.0f (Fahrenheit) -> TwoWay ConvertBack writes 20.0f (Celsius) back to ViewModel!
    tempSlider->SetSliderValue(68.0f);
    (void)tempSlider->NotifyTwoWayPropertyMutated(aero::PropId_Value);
    aero::UnifiedValue celsiusBack{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmObj, *celsiusSlot, celsiusBack, nullptr, &uiInterp);
    float celsiusNow = -1.0f;
    (void)aero::TryGetFloat(celsiusBack, celsiusNow);
    AERO_CHECK(ApproxEq(celsiusNow, 20.0f, 0.05f),
               "RealScript SymbolId TwoWay ConvertBack: 68 F -> 20 C in ViewModel");

    // Mutate UI TextBox to "Gold" -> TwoWay C++ ConvertBack writes RankLevel = 3 back to ViewModel!
    rankBox->SetText("Gold");
    (void)rankBox->NotifyTwoWayPropertyMutated(aero::PropId_Text);
    aero::UnifiedValue rankBack{};
    (void)domains.readPropertySlotFast(aero::DomainKind::Ui, vmObj, *rankSlot, rankBack, nullptr, &uiInterp);
    AERO_CHECK(
        std::holds_alternative<std::int64_t>(rankBack) && std::get<std::int64_t>(rankBack) == 3,
        "C++ callback TwoWay ConvertBack: 'Gold' -> RankLevel 3 in ViewModel");
}

// ============================================================================
// Suite (d): Compile Official Samples (TicTacToe/Data/MainPage.xaml &
//            Scoreboard/Data/MainPage.xaml + Inventory MultiBinding MathConverter)
// ============================================================================
void TestOfficialSamplesCompilationAndExtraction() {
    std::printf("--- [Suite D] Compiling Official XAML Samples (TicTacToe & Scoreboard) ---\n");

    const std::string samplesDir = "/working_dir/c_1a21a30fb41e1c84/scratch/samples";
    aero::AeroCompiler compiler;

    // 1. Official Sample: TicTacToe/Data/MainPage.xaml
    {
        const std::string tttXaml = ReadFileUtf8(samplesDir + "/TicTacToe/Data/MainPage.xaml");
        AERO_CHECK(!tttXaml.empty(), "Loaded Samples/TicTacToe/Data/MainPage.xaml");

        aero::AeroCompileInput in{};
        in.xamlPath = "Samples/TicTacToe/Data/MainPage.xaml";
        in.xamlContent = tttXaml;
        const aero::AeroCompileResult res = compiler.Compile(in);
        AERO_CHECK(res.succeeded, "Compiled TicTacToe/Data/MainPage.xaml cleanly");

        aero::AxbPackage decoded{};
        AERO_CHECK(
            aero::AxbPackage::Deserialize(res.axbBytes, decoded),
            "TicTacToe .axb4 serialized & deserialized cleanly");

        std::size_t interDataTriggers = 0u;
        std::size_t interEventTriggers = 0u;
        std::size_t changePropActions = 0u;
        std::size_t controlSbActions = 0u;
        std::size_t playSoundActions = 0u;
        std::size_t inequalityConds = 0u;

        auto countActions = [&](const std::vector<aero::AxbInteractionAction>& acts) {
            for (const auto& a : acts) {
                if (a.kind == aero::InteractionActionKind::ChangeProperty) ++changePropActions;
                else if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++controlSbActions;
                else if (a.kind == aero::InteractionActionKind::PlaySound) ++playSoundActions;
            }
        };

        for (const auto& st : decoded.styles) {
            for (const auto& tr : st.triggers) {
                if (tr.isInteractionTrigger) {
                    ++interDataTriggers;
                }
                for (const auto& c : tr.conditions) {
                    if (c.comparison != aero::TriggerComparisonOp::Equal) {
                        ++inequalityConds;
                    }
                }
                countActions(tr.enterActions);
            }
            for (const auto& et : st.eventTriggers) {
                if (!et.actions.empty()) {
                    ++interEventTriggers;
                    countActions(et.actions);
                }
            }
        }

        std::printf(
            "  [TicTacToe] Extracted: %zu b:DataTriggers (%zu inequality conds), %zu b:EventTriggers, "
            "%zu ChangePropertyActions, %zu ControlStoryboardActions, %zu PlaySoundActions\n",
            interDataTriggers,
            inequalityConds,
            interEventTriggers,
            changePropActions,
            controlSbActions,
            playSoundActions);

        AERO_CHECK(interDataTriggers >= 15u, "TicTacToe extracted >= 15 b:DataTriggers");
        AERO_CHECK(inequalityConds >= 6u, "TicTacToe extracted GreaterThanOrEqual & NotEqual conditions");
        AERO_CHECK(interEventTriggers >= 3u, "TicTacToe extracted Loaded & Click b:EventTriggers");
        AERO_CHECK(changePropActions >= 8u, "TicTacToe extracted ChangePropertyActions");
        AERO_CHECK(controlSbActions >= 14u, "TicTacToe extracted ControlStoryboardActions");
        AERO_CHECK(playSoundActions >= 4u, "TicTacToe extracted PlaySoundActions");
    }

    // 2. Official Sample: Scoreboard/Data/MainPage.xaml
    {
        const std::string sbXaml = ReadFileUtf8(samplesDir + "/Scoreboard/Data/MainPage.xaml");
        AERO_CHECK(!sbXaml.empty(), "Loaded Samples/Scoreboard/Data/MainPage.xaml");

        aero::AeroCompileInput in{};
        in.xamlPath = "Samples/Scoreboard/Data/MainPage.xaml";
        in.xamlContent = sbXaml;
        const aero::AeroCompileResult res = compiler.Compile(in);
        AERO_CHECK(res.succeeded, "Compiled Scoreboard/Data/MainPage.xaml cleanly");

        aero::AxbPackage decoded{};
        AERO_CHECK(
            aero::AxbPackage::Deserialize(res.axbBytes, decoded),
            "Scoreboard .axb4 serialized & deserialized cleanly");

        std::size_t totalControlSbActions = 0u;
        std::size_t totalChangePropActions = 0u;
        std::size_t totalStringFormats = 0u;
        std::size_t totalCustomConverters = 0u;
        std::string sampleDamageFormat;

        auto inspectPkg = [&](auto& self, const aero::AxbPackage& pkg) -> void {
            for (const auto& ch : pkg.bindingChains) {
                if (!ch.stringFormat.empty()) {
                    ++totalStringFormats;
                    if (ch.rawPath == "Damage") {
                        sampleDamageFormat = ch.stringFormat;
                    }
                }
                if (ch.converter == aero::ValueConverterKind::CustomScriptOrHost) {
                    ++totalCustomConverters;
                }
            }
            for (const auto& st : pkg.styles) {
                for (const auto& tr : st.triggers) {
                    for (const auto& a : tr.enterActions) {
                        if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++totalControlSbActions;
                        if (a.kind == aero::InteractionActionKind::ChangeProperty) ++totalChangePropActions;
                    }
                }
                for (const auto& et : st.eventTriggers) {
                    for (const auto& a : et.actions) {
                        if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++totalControlSbActions;
                        if (a.kind == aero::InteractionActionKind::ChangeProperty) ++totalChangePropActions;
                    }
                }
            }
            for (const auto& dt : pkg.dataTemplates) {
                if (dt.templatePackage) {
                    self(self, *dt.templatePackage);
                }
            }
            for (const auto& ct : pkg.controlTemplates) {
                for (const auto& trig : ct.triggers) {
                    for (const auto& a : trig.enterActions) {
                        if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++totalControlSbActions;
                        if (a.kind == aero::InteractionActionKind::ChangeProperty) ++totalChangePropActions;
                    }
                    for (const auto& a : trig.exitActions) {
                        if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++totalControlSbActions;
                        if (a.kind == aero::InteractionActionKind::ChangeProperty) ++totalChangePropActions;
                    }
                }
                for (const auto& et : ct.eventTriggers) {
                    for (const auto& a : et.actions) {
                        if (a.kind == aero::InteractionActionKind::ControlStoryboard) ++totalControlSbActions;
                        if (a.kind == aero::InteractionActionKind::ChangeProperty) ++totalChangePropActions;
                    }
                }
                if (ct.templatePackage) {
                    self(self, *ct.templatePackage);
                }
            }
        };
        inspectPkg(inspectPkg, decoded);

        std::printf(
            "  [Scoreboard] Extracted: %zu ControlStoryboardActions, %zu ChangePropertyActions, "
            "%zu StringFormats (Damage fmt='%s'), %zu CustomConverters\n",
            totalControlSbActions,
            totalChangePropActions,
            totalStringFormats,
            sampleDamageFormat.c_str(),
            totalCustomConverters);

        AERO_CHECK(totalControlSbActions >= 3u, "Scoreboard extracted ControlStoryboardActions (incl. DataTemplate OverOn/OverOff)");
        AERO_CHECK(totalChangePropActions >= 1u, "Scoreboard extracted ChangePropertyAction on SelectionChanged");
        AERO_CHECK(totalStringFormats >= 2u && sampleDamageFormat == "{0:#,.##} K",
                   "Scoreboard extracted Damage/Heal StringFormat='{0:#,.##} K'");
        const aero::UnifiedValue dmgArg = aero::value::FromInt(12500);
        AERO_CHECK(
            aero::FormatBindingString(sampleDamageFormat, &dmgArg, 1u) == "12.5 K",
            "Scoreboard '{0:#,.##} K' formats 12500 as '12.5 K'");
    }

    // 3. Bonus Official Sample Check: Inventory/Data/MainPage.xaml (9-source MultiBinding + MathConverter)
    {
        const std::string invXaml = ReadFileUtf8(samplesDir + "/Inventory/Data/MainPage.xaml");
        if (!invXaml.empty()) {
            aero::AeroCompileInput in{};
            in.xamlPath = "Samples/Inventory/Data/MainPage.xaml";
            in.xamlContent = invXaml;
            const aero::AeroCompileResult res = compiler.Compile(in);
            AERO_CHECK(res.succeeded && res.axbPackage.multiBindings.size() >= 4u,
                       "Inventory/Data/MainPage.xaml extracted 4 nine-input MultiBindings with MathConverter");
            if (!res.axbPackage.multiBindings.empty()) {
                const auto& mb0 = res.axbPackage.multiBindings[0];
                AERO_CHECK(
                    mb0.converter == aero::ValueConverterKind::MathExpression &&
                    mb0.sources.size() == 9u,
                    "Inventory MultiBinding has 9 sources and MathExpression converter");
            }
        }
    }

    // 4. Official Sample Check: DataBinding/Data/MainPage.xaml (pow() MathConverter + Custom Converter)
    {
        const std::string dbXaml = ReadFileUtf8(samplesDir + "/DataBinding/Data/MainPage.xaml");
        AERO_CHECK(!dbXaml.empty(), "Loaded Samples/DataBinding/Data/MainPage.xaml");
        aero::AeroCompileInput in{};
        in.xamlPath = "Samples/DataBinding/Data/MainPage.xaml";
        in.xamlContent = dbXaml;
        const aero::AeroCompileResult res = compiler.Compile(in);
        AERO_CHECK(res.succeeded, "Compiled DataBinding/Data/MainPage.xaml cleanly");

        std::size_t mathConverters = 0u;
        std::size_t customConverters = 0u;
        auto scanPkg = [&](auto& self, const aero::AxbPackage& pkg) -> void {
            for (const auto& ch : pkg.bindingChains) {
                if (ch.converter == aero::ValueConverterKind::MathExpression) ++mathConverters;
                if (ch.converter == aero::ValueConverterKind::CustomScriptOrHost) ++customConverters;
            }
            for (const auto& dt : pkg.dataTemplates) {
                if (dt.templatePackage) self(self, *dt.templatePackage);
            }
        };
        scanPkg(scanPkg, res.axbPackage);
        std::size_t stopSbActions = 0u;
        for (const auto& st : res.axbPackage.styles) {
            for (const auto& et : st.eventTriggers) {
                for (const auto& a : et.actions) {
                    if (a.kind == aero::InteractionActionKind::ControlStoryboard &&
                        a.storyboardOption == aero::ControlStoryboardOption::Stop) {
                        ++stopSbActions;
                    }
                }
            }
            for (const auto& tr : st.triggers) {
                for (const auto& a : tr.enterActions) {
                    if (a.kind == aero::InteractionActionKind::ControlStoryboard &&
                        a.storyboardOption == aero::ControlStoryboardOption::Stop) {
                        ++stopSbActions;
                    }
                }
            }
        }
        std::printf(
            "  [DataBinding] Extracted: %zu DataTemplate pow() MathConverters, %zu ControlStoryboardOption::Stop actions\n",
            mathConverters,
            stopSbActions);
        AERO_CHECK(mathConverters == 4u && stopSbActions >= 2u,
                   "DataBinding extracted all 4 DataTemplate pow() MathConverters & StyleInteraction Stop actions");
    }

    // 5. Official Sample Check: QuestLog/Data/MainPage.xaml & Gallery/Data/Samples/Animation.xaml
    {
        const std::string qlXaml = ReadFileUtf8(samplesDir + "/QuestLog/Data/MainPage.xaml");
        const std::string animXaml = ReadFileUtf8(samplesDir + "/Gallery/Data/Samples/Animation.xaml");
        AERO_CHECK(!qlXaml.empty() && !animXaml.empty(), "Loaded QuestLog & Gallery/Animation samples");

        aero::AeroCompileInput qlIn{};
        qlIn.xamlPath = "Samples/QuestLog/Data/MainPage.xaml";
        qlIn.xamlContent = qlXaml;
        const aero::AeroCompileResult qlRes = compiler.Compile(qlIn);
        AERO_CHECK(qlRes.succeeded, "Compiled QuestLog/Data/MainPage.xaml cleanly");

        aero::AeroCompileInput animIn{};
        animIn.xamlPath = "Samples/Gallery/Data/Samples/Animation.xaml";
        animIn.xamlContent = animXaml;
        const aero::AeroCompileResult animRes = compiler.Compile(animIn);
        AERO_CHECK(animRes.succeeded, "Compiled Gallery/Data/Samples/Animation.xaml cleanly");

        std::size_t f1Formats = 0u;
        for (const auto& ch : animRes.axbPackage.bindingChains) {
            if (ch.stringFormat == "F1" || ch.stringFormat == "F0") {
                ++f1Formats;
            }
        }
        AERO_CHECK(f1Formats >= 7u, "Gallery/Animation extracted standalone F0/F1 StringFormats");
    }
}

} // namespace

int main() {
    std::printf("=== AeroGUI 2.0 P1-4 Acceptance Test Suite ===\n");
    TestInteractionTriggersAndActionsEndToEnd();
    TestMultiBindingStringFormatAndMathConverter();
    TestCustomScriptAndHostValueConvertersTwoWay();
    TestOfficialSamplesCompilationAndExtraction();

    std::printf(
        "=== P1-4 Summary: %d passed, %d failed ===\n",
        gChecksPassed,
        gChecksFailed);
    return (gChecksFailed == 0) ? 0 : 1;
}
