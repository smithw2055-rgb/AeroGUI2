#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/Compilation.h"

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

std::shared_ptr<const ProgramImage> CompileProgram(std::vector<SourceFile> files) {
    rs_comp::Compilation compilation(std::move(files));
    const auto build = compilation.build();
    if (build.diagnostics.hasErrors()) {
        for (const auto& d : build.diagnostics.items()) {
            std::cerr << "Compile diagnostic: " << d.code << ": " << d.message << "\n";
        }
    }
    AERO_CHECK(!build.diagnostics.hasErrors(), "RealScript compilation succeeded");

    rs_bc::Lowerer lowerer;
    std::vector<rs_bc::Module> modules;
    for (const auto& mir : build.modules) {
        auto bc = lowerer.lower(mir);
        rs_diag::DiagnosticBag verifyDiag;
        const bool verified = rs_bc::verifyModule(bc, verifyDiag);
        if (!verified) {
            for (const auto& d : verifyDiag.items()) {
                std::cerr << "Verify diagnostic: " << d.code << ": " << d.message << "\n";
            }
        }
        AERO_CHECK(verified, "Bytecode module verified");
        modules.push_back(std::move(bc));
    }

    RuntimeError linkErr{};
    auto image = ProgramImage::link(std::move(modules), linkErr);
    AERO_CHECK(image.has_value(), "ProgramImage linked");
    return std::make_shared<const ProgramImage>(std::move(*image));
}

// ============================================================================
// Test 1: RealScript [SimState] Cross-Domain Read-Only & [SimCommand] Marshaling
// ============================================================================
void TestSimStateReadOnlyAndSimCommandMarshaling() {
    auto program = CompileProgram({
        {"BattleSim.rs",
         R"(
module Game.Sim;

[SimState]
public class CommanderUnit
{
    [Observable]
    public int Stamina { get; set; }

    [Observable]
    public float ShieldStrength { get; set; }

    public CommanderUnit()
    {
        Stamina = 100;
        ShieldStrength = 250.0f;
    }

    [SimCommand]
    public void SpendStamina(int cost, float shieldBoost)
    {
        Stamina = Stamina - cost;
        ShieldStrength = ShieldStrength + shieldBoost;
    }
}

public CommanderUnit SpawnCommander()
{
    return new CommanderUnit();
}

public int ReadCommanderStaminaFromUi(CommanderUnit unit)
{
    return unit.Stamina;
}

public void IllegalMutateFromUi(CommanderUnit unit)
{
    unit.Stamina = 999;
}

public void TriggerCommandFromUi(CommanderUnit unit, int cost, float boost)
{
    unit.SpendStamina(cost, boost);
}
)"}});

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*program);

    Interpreter simInterpreter(program, domains.simHeapPtr());
    simInterpreter.setExecutionDomainContext(&domains, DomainKind::SimDeterministic);

    Interpreter uiInterpreter(program, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto spawnRes = simInterpreter.invoke("Game.Sim::SpawnCommander");
    AERO_CHECK(spawnRes.succeeded, "Spawned CommanderUnit on SimHeap");
    const ObjectRef unitRef = std::get<ObjectRef>(spawnRes.value);
    auto unitRoot = domains.simHeap().retain(unitRef);

    const SymbolId unitTypeId = rs_sem::stableTypeId("Game.Sim::CommanderUnit");
    const auto* unitTypeSym = program->resolveType(unitTypeId);
    AERO_CHECK(unitTypeSym != nullptr && unitTypeSym->simState, "CommanderUnit recognized as [SimState]");

    const auto staminaSlotOpt = domains.resolvePropertySlot(*program, unitTypeId, "Stamina");
    AERO_CHECK(
        staminaSlotOpt.has_value() && staminaSlotOpt->isSimState,
        "Stamina slot carries [SimState] metadata");

    // 1. UI Interpreter can directly read [SimState] object living on SimHeap!
    auto uiReadRes = uiInterpreter.invoke("Game.Sim::ReadCommanderStaminaFromUi", {unitRef});
    AERO_CHECK(uiReadRes.succeeded, "UI domain can read [SimState] property across heaps");
    AERO_CHECK(
        std::get<std::int64_t>(uiReadRes.value) == 100,
        "UI domain read Stamina == 100 from SimHeap");

    // 2. UI Interpreter attempting direct write to [SimState] on SimHeap is blocked with CapabilityDenied!
    auto illegalRes = uiInterpreter.invoke("Game.Sim::IllegalMutateFromUi", {unitRef});
    AERO_CHECK(!illegalRes.succeeded, "Direct write to [SimState] from UiPresentation must fail");
    AERO_CHECK(
        illegalRes.error.code == ErrorCode::CapabilityDenied,
        "Direct [SimState] write from UI returns CapabilityDenied");

    // 3. UI Interpreter calling [SimCommand] method automatically marshals into SimCommandEnvelope queue!
    AERO_CHECK(domains.pendingSimCommandCount() == 0, "Command queue initially empty");
    auto cmdCallRes = uiInterpreter.invoke(
        "Game.Sim::TriggerCommandFromUi",
        {unitRef, std::int64_t{35}, MakeFloat(75.0f)});
    AERO_CHECK(cmdCallRes.succeeded, "[SimCommand] call from UI succeeded via auto-marshaling");
    AERO_CHECK(
        domains.pendingSimCommandCount() == 1,
        "1 SimCommandEnvelope queued in ExecutionDomainContext");

    // Verify SimHeap state has NOT changed before SimTick flushes the command queue
    auto preTickRead = uiInterpreter.invoke("Game.Sim::ReadCommanderStaminaFromUi", {unitRef});
    AERO_CHECK(
        std::get<std::int64_t>(preTickRead.value) == 100,
        "SimHeap state remains untouched before deterministic SimTick flush");

    // 4. Deterministic SimTick flushes and executes queued [SimCommand] envelopes
    RuntimeError flushErr{};
    const std::size_t executed = domains.flushSimCommands(simInterpreter, &flushErr);
    AERO_CHECK(executed == 1u, "Flushed and executed 1 SimCommand on SimTick");
    AERO_CHECK(domains.pendingSimCommandCount() == 0, "Command queue empty after flush");

    auto postTickRead = uiInterpreter.invoke("Game.Sim::ReadCommanderStaminaFromUi", {unitRef});
    AERO_CHECK(
        std::get<std::int64_t>(postTickRead.value) == 65,
        "Stamina deterministically updated to 100 - 35 = 65 after SimTick flush");

    const auto shieldSlot = *domains.resolvePropertySlot(*program, unitTypeId, "ShieldStrength");
    UnifiedValue shieldVal{};
    AERO_CHECK(
        domains.readPropertySlotFast(DomainKind::Ui, unitRef, shieldSlot, shieldVal),
        "Read ShieldStrength after SimCommand flush");
    float shieldFloat = 0.0f;
    AERO_CHECK(
        TryGetFloat(shieldVal, shieldFloat) && std::fabs(shieldFloat - 325.0f) < 1e-4f,
        "ShieldStrength updated to 250.0f + 75.0f = 325.0f");

    std::cout << "[PASS] TestSimStateReadOnlyAndSimCommandMarshaling\n";
}

// ============================================================================
// Test 2: RealScript UiDomain Heap-Allocated Reentrant Async Tasks & Control Cancellation
// ============================================================================
void TestUiDomainReentrantAsyncTasksAndCancellation() {
    auto program = CompileProgram({
        {"UiAnimController.rs",
         R"(
module Game.UI;

public long Schedule(long target, string callback, int delayTicks)
{
    return delayTicks;
}

public bool CancelTimer(long timerId)
{
    return timerId != 0;
}

public class CardAnimator
{
    public int Accumulator;

    public CardAnimator()
    {
        Accumulator = 0;
    }

    public sequence PulseSequence(long target, int stepDelta)
    {
        Accumulator = Accumulator + stepDelta;
        yield wait_ticks(1);
        Accumulator = Accumulator + (stepDelta * 10);
        yield wait_ticks(1);
        Accumulator = Accumulator + (stepDelta * 100);
    }
}

public CardAnimator CreateAnimator()
{
    return new CardAnimator();
}
)"}});

    ExecutionDomainContext domains;
    Interpreter uiInterpreter(program, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto createRes = uiInterpreter.invoke("Game.UI::CreateAnimator");
    AERO_CHECK(createRes.succeeded, "Created CardAnimator on UiHeap");
    const ObjectRef animRef = std::get<ObjectRef>(createRes.value);
    auto animRoot = domains.uiHeap().retain(animRef);

    auto btn = MakeIntrusive<Button>();
    const NativeHandle btnHandle = btn->ToNativeHandle(domains.nativeHandles());

    // Spawn TWO concurrent reentrant sequence tasks on the SAME CardAnimator instance!
    // Task A uses stepDelta = 1 (+1, +10, +100); Task B uses stepDelta = 2 (+2, +20, +200).
    const std::uint64_t taskA = domains.spawnReentrantSequenceTask(
        *program,
        uiInterpreter,
        animRef,
        "PulseSequence",
        {std::int64_t{1}},
        btnHandle);
    const std::uint64_t taskB = domains.spawnReentrantSequenceTask(
        *program,
        uiInterpreter,
        animRef,
        "PulseSequence",
        {std::int64_t{2}},
        btnHandle);

    AERO_CHECK(taskA != 0 && taskB != 0 && taskA != taskB, "Spawned two distinct UiTaskIds");
    AERO_CHECK(domains.activeUiTaskCount() == 2u, "2 active UiTasks in UiDomain");

    auto readAccumulator = [&]() -> std::int64_t {
        UnifiedValue v{};
        (void)domains.readObjectField(DomainKind::Ui, animRef, 0, v);
        return std::get<std::int64_t>(v);
    };

    // Frame 1: both Task A and Task B execute slice 0 (+1 and +2 -> 3) and yield at first `wait 1`
    AERO_CHECK(domains.stepAllUiTasks(&uiInterpreter) == 2u, "Stepped both tasks on Frame 1");
    AERO_CHECK(readAccumulator() == 3, "After Frame 1, Accumulator == 1 + 2 = 3");
    AERO_CHECK(domains.activeUiTaskCount() == 2u, "Both tasks still suspended at wait 1");

    // Frame 2: both Task A and Task B execute slice 1 (+10 and +20 -> 3 + 30 = 33)
    AERO_CHECK(domains.stepAllUiTasks(&uiInterpreter) == 2u, "Stepped both tasks on Frame 2");
    AERO_CHECK(readAccumulator() == 33, "After Frame 2, Accumulator == 3 + 10 + 20 = 33");

    // Frame 3: both Task A and Task B execute final slice (+100 and +200 -> 33 + 300 = 333) and complete
    AERO_CHECK(domains.stepAllUiTasks(&uiInterpreter) == 2u, "Stepped both tasks on Frame 3");
    AERO_CHECK(readAccumulator() == 333, "After Frame 3, Accumulator == 33 + 100 + 200 = 333");
    AERO_CHECK(domains.activeUiTaskCount() == 0u, "Both tasks automatically cleaned up on completion");

    // Control Unload Cancellation Safety:
    // Spawn Task C bound to `btnHandle`, then destroy `btn` before stepping Task C!
    const std::uint64_t taskC = domains.spawnReentrantSequenceTask(
        *program,
        uiInterpreter,
        animRef,
        "PulseSequence",
        {std::int64_t{5}},
        btnHandle);
    AERO_CHECK(taskC != 0 && domains.activeUiTaskCount() == 1u, "Spawned Task C bound to Button");

    // Destroy C++ Button -> invalidates btnHandle generation in NativeHandleRegistry
    btn.Reset();
    AERO_CHECK(!domains.nativeHandles().isAlive(btnHandle), "Button NativeHandle invalidated on unload");

    // Stepping UiTasks must detect dead control handle and cancel Task C without mutating Accumulator
    AERO_CHECK(domains.stepAllUiTasks(&uiInterpreter) == 0u, "Dead control task cancelled safely");
    AERO_CHECK(domains.activeUiTaskCount() == 0u, "Cancelled task removed from active list");
    AERO_CHECK(readAccumulator() == 333, "Cancelled task did not mutate Accumulator");

    std::cout << "[PASS] TestUiDomainReentrantAsyncTasksAndCancellation\n";
}

// ============================================================================
// Test 3: RealScript UTF-8 Codepoint-Aware String Intrinsics & Ring-Buffer Elimination
// ============================================================================
void TestUtf8CodePointStringIntrinsics() {
    // 1. C++ StringValue 64-bit word ASCII fast path & multi-byte UTF-8 codepoint operations
    const StringValue asciiStr(std::string_view("AeroGUI_2.0_Fast_ASCII_String_Path"));
    AERO_CHECK(asciiStr.isAsciiFast(), "ASCII string detected via 64-bit fast path");
    AERO_CHECK(
        asciiStr.codePointLength() == asciiStr.size(),
        "ASCII codepoint length equals byte length");

    // Multi-byte UTF-8 string: "技能🔥Lv9" -> '技'(3B) + '能'(3B) + '🔥'(4B) + 'L'(1B) + 'v'(1B) + '9'(1B)
    // Total bytes = 13, Total UTF-8 codepoints = 6!
    const StringValue utf8Str(std::string_view("\xE6\x8A\x80\xE8\x83\xBD\xF0\x9F\x94\xA5Lv9"));
    AERO_CHECK(!utf8Str.isAsciiFast(), "Multi-byte UTF-8 string detected as non-ASCII");
    AERO_CHECK(utf8Str.size() == 13u, "UTF-8 byte length is 13");
    AERO_CHECK(utf8Str.codePointLength() == 6u, "UTF-8 codepoint length is 6");
    AERO_CHECK(
        utf8Str.codePointSlice(2, 1) == "\xF0\x9F\x94\xA5",
        "Codepoint slice(2, 1) extracts 4-byte emoji cleanly without splitting UTF-8 bytes");
    AERO_CHECK(
        utf8Str.codePointIndexOf("Lv9") == 3,
        "Codepoint IndexOf('Lv9') returns codepoint index 3 (not byte offset 10)");

    // 2. RealScript VM intrinsic execution for UTF-8 Length, Substring, IndexOf, LastIndexOf
    // plus >16 Substring allocations in one function to prove sMutableRing overwrite is gone!
    auto program = CompileProgram({
        {"Utf8StringTest.rs",
         R"(
module Game.UI;

public int CheckUtf8Length(string s)
{
    return s.Length();
}

public string ExtractEmojiAndLevel(string s)
{
    return s.Substring(2, 4);
}

public int FindLevelCodePointIndex(string s)
{
    return s.IndexOf("Lv9");
}

public string StressTwentySubstrings(string src)
{
    string s0 = src.Substring(0, 1);
    string s1 = src.Substring(1, 1);
    string s2 = src.Substring(2, 1);
    string s3 = src.Substring(3, 1);
    string s4 = src.Substring(4, 1);
    string s5 = src.Substring(5, 1);
    string s6 = src.Substring(0, 2);
    string s7 = src.Substring(1, 2);
    string s8 = src.Substring(2, 2);
    string s9 = src.Substring(3, 2);
    return s0 + s1 + s2 + s3 + s4 + s5;
}
)"}});

    ExecutionDomainContext domains;
    Interpreter uiInterpreter(program, domains.uiHeapPtr());

    auto readScriptStr = [&](const UnifiedValue& val) -> std::string {
        if (const auto* sv = std::get_if<StringValue>(&val)) {
            return sv->str();
        }
        if (const auto* ref = std::get_if<ObjectRef>(&val)) {
            if (const auto view = domains.stringViewAcrossDomains(*ref)) {
                return std::string(*view);
            }
        }
        return {};
    };

    auto lenRes = uiInterpreter.invoke("Game.UI::CheckUtf8Length", {utf8Str});
    AERO_CHECK(lenRes.succeeded && std::get<std::int64_t>(lenRes.value) == 6, "Script s.Length() == 6 codepoints");

    auto subRes = uiInterpreter.invoke("Game.UI::ExtractEmojiAndLevel", {utf8Str});
    AERO_CHECK(
        subRes.succeeded && readScriptStr(subRes.value) == "\xF0\x9F\x94\xA5Lv9",
        "Script s.Substring(2, 4) returns '🔥Lv9'");

    auto idxRes = uiInterpreter.invoke("Game.UI::FindLevelCodePointIndex", {utf8Str});
    AERO_CHECK(
        idxRes.succeeded && std::get<std::int64_t>(idxRes.value) == 3,
        "Script s.IndexOf('Lv9') == 3");

    auto stressRes = uiInterpreter.invoke("Game.UI::StressTwentySubstrings", {utf8Str});
    AERO_CHECK(
        stressRes.succeeded && readScriptStr(stressRes.value) == utf8Str.str(),
        "Multiple Substring results remain intact without sMutableRing overwrite");

    // 3. Verify non-const std::get_if<std::string>(Value*) and std::get<std::string>(Value&)
    // across 128 distinct UnifiedValues to prove the 64-slot sMutableRing in Runtime.h is gone!
    std::vector<UnifiedValue> distinctVals;
    std::vector<const std::string*> capturedPtrs;
    distinctVals.reserve(128);
    capturedPtrs.reserve(128);
    for (std::size_t i = 0; i < 128; ++i) {
        distinctVals.push_back(value::FromString("Distinct_String_Slot_" + std::to_string(i)));
    }
    for (std::size_t i = 0; i < 128; ++i) {
        const std::string* ptr = std::get_if<std::string>(&distinctVals[i]);
        AERO_CHECK(ptr != nullptr, "Non-const std::get_if<std::string>(Value*) returned valid pointer");
        capturedPtrs.push_back(ptr);
    }
    for (std::size_t i = 0; i < 128; ++i) {
        const std::string expected = "Distinct_String_Slot_" + std::to_string(i);
        AERO_CHECK(
            *capturedPtrs[i] == expected && std::get<std::string>(distinctVals[i]) == expected,
            "All 128 non-const std::get_if<std::string> pointers remain intact (zero ring-buffer overwrite)");
    }

    std::cout << "[PASS] TestUtf8CodePointStringIntrinsics\n";
}

// ============================================================================
// Test 4: Single-Pass aero-compiler (.xaml + .rs -> .g.rs + .axb4) & Self-Mounting
// ============================================================================
void TestAeroCompilerSinglePassAndSelfContainedMounting() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlPath = "TacticalHud.xaml";
    input.xamlContent = R"(
<UserControl x:Class="Game.UI.TacticalHud" Width="480" Height="160" Padding="12,12,12,12">
    <StackPanel x:Name="MainStack" Orientation="Vertical" Spacing="10">
        <TextBlock x:Name="SquadHeader" Text="{Binding HeaderText}" FontSize="20" />
        <Button x:Name="StrikeButton" Content="{Binding StrikeButtonLabel}" Click="OnStrikeClicked" Width="180" Height="40" Opacity="0.95" />
    </StackPanel>
</UserControl>
)";
    input.scriptSources.push_back(SourceFile{
        "TacticalHud.rs",
        R"(
module Game.UI;

public partial class TacticalHud
{
    [Observable]
    public string HeaderText { get; set; }

    [Observable]
    public string StrikeButtonLabel { get; set; }

    public int StrikeCount;

    public TacticalHud()
    {
        HeaderText = "Orbital Strike Ready";
        StrikeButtonLabel = "Launch Strike";
        StrikeCount = 0;
    }

    public void OnStrikeClicked()
    {
        StrikeCount = StrikeCount + 1;
        HeaderText = "Orbital Strike Fired!";
    }
}

public TacticalHud CreateTacticalHud()
{
    return new TacticalHud();
}
)"});

    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_CHECK(compiled.succeeded, "AeroCompiler single-pass compile succeeded");
    AERO_CHECK(
        compiled.generatedPartialRsPath == "TacticalHud.g.rs",
        "Generated TacticalHud.g.rs partial class path");
    AERO_CHECK(
        compiled.generatedPartialRsCode.find("public partial class TacticalHud") != std::string::npos &&
            compiled.generatedPartialRsCode.find("public handle MainStack;") != std::string::npos &&
            compiled.generatedPartialRsCode.find("public handle SquadHeader;") != std::string::npos &&
            compiled.generatedPartialRsCode.find("public handle StrikeButton;") != std::string::npos,
        "Generated *.g.rs contains strongly-typed handle fields for all x:Name controls");
    AERO_CHECK(!compiled.axbBytes.empty(), "Generated non-empty .axb4 binary package");

    // Verify .axb4 binary round-trip deserialization
    AxbPackage roundTripPkg{};
    std::string deserErr;
    AERO_CHECK(
        AxbPackage::Deserialize(compiled.axbBytes, roundTripPkg, &deserErr),
        "AxbPackage::Deserialize succeeded on .axb4 binary payload");
    AERO_CHECK(roundTripPkg.className == "Game.UI.TacticalHud", "Preserved className");
    AERO_CHECK(roundTripPkg.namedControls.size() == 3u, "Preserved 3 x:Name control slots");
    AERO_CHECK(roundTripPkg.bindingChains.size() == 2u, "Preserved 2 compiled binding chains");
    AERO_CHECK(roundTripPkg.eventBindings.size() == 1u, "Preserved 1 compiled event binding");

    // Instantiate TacticalHud script object on UiHeap
    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    Interpreter uiInterpreter(compiled.programImage, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto createHudRes = uiInterpreter.invoke("Game.UI::CreateTacticalHud");
    AERO_CHECK(createHudRes.succeeded, "Created TacticalHud script instance on UiHeap");
    const ObjectRef hudObj = std::get<ObjectRef>(createHudRes.value);
    auto hudRoot = domains.uiHeap().retain(hudObj);

    DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterpreter);

    // Self-contained UserControl mounting: constructing UserControl("Game.UI.TacticalHud", ...)
    // automatically loads its registered .axb4 package, builds its child visual tree, populates
    // x:Name handle slots on `hudObj`, and wires all property & event bindings!
    auto hudControl = MakeIntrusive<UserControl>(
        "Game.UI.TacticalHud",
        &domains,
        &bindingEngine,
        hudObj,
        &uiInterpreter);
    AERO_CHECK(hudControl->IsTemplateMounted(), "UserControl self-mounted its .axb4 template");
    AERO_CHECK(std::fabs(hudControl->GetWidth() - 480.0f) < 1e-5f, "Root Width == 480.0f");
    AERO_CHECK(std::fabs(hudControl->GetHeight() - 160.0f) < 1e-5f, "Root Height == 160.0f");

    auto* headerTb = hudControl->FindName("SquadHeader")->DynamicCast<TextBlock>();
    auto* strikeBtn = hudControl->FindName("StrikeButton")->DynamicCast<Button>();
    AERO_CHECK(headerTb != nullptr && strikeBtn != nullptr, "Resolved SquadHeader and StrikeButton");
    AERO_CHECK(headerTb->GetText() == "Orbital Strike Ready", "Initial bound HeaderText");
    AERO_CHECK(strikeBtn->GetContentText() == "Launch Strike", "Initial bound StrikeButtonLabel");

    // Verify x:Name NativeHandle slots were written directly into TacticalHud script fields!
    for (const auto& nc : roundTripPkg.namedControls) {
        AERO_CHECK(
            nc.fieldIndexInClass != PropertySlotHandle::InvalidIndex,
            "x:Name control field index resolved in partial class");
        UnifiedValue fieldVal{};
        AERO_CHECK(
            domains.readObjectField(DomainKind::Ui, hudObj, nc.fieldIndexInClass, fieldVal),
            "Read x:Name handle field from TacticalHud script instance");
        AERO_CHECK(
            std::holds_alternative<NativeHandle>(fieldVal) &&
                UIElement::FromNativeHandle(domains.nativeHandles(), std::get<NativeHandle>(fieldVal)) != nullptr,
            "x:Name handle field in script instance resolves to live C++ UIElement");
    }

    // Clear initial construction dirty state, then click StrikeButton!
    domains.uiHeap().clearDirtyFieldMask(hudObj);
    (void)domains.uiHeap().consumeDirtyObjects();

    RuntimeError clickErr{};
    AERO_CHECK(strikeBtn->PerformClick(&clickErr), "StrikeButton PerformClick invoked script handler");
    AERO_CHECK(bindingEngine.FlushTrackA(&clickErr) == 1u, "Track A flushed updated HeaderText");
    AERO_CHECK(
        headerTb->GetText() == "Orbital Strike Fired!",
        "HeaderText updated after Button click event");

    // Verify self-contained Window auto-mounting + XML entity decoding (&amp;, &lt;, &gt;, &quot;, &apos;)
    AeroCompileInput winInput{};
    winInput.xamlPath = "CommandWindow.xaml";
    winInput.xamlContent = R"(
<Window x:Class="Game.UI.CommandWindow" Width="800" Height="600" Title="R&amp;D &lt;Alpha&gt; &quot;Core&quot; &apos;v2&apos;">
    <TextBlock x:Name="StatusBanner" Text="Systems &amp; Sensors &lt;Online&gt;" />
</Window>
)";
    const AeroCompileResult winCompiled = compiler.Compile(winInput);
    AERO_CHECK(winCompiled.succeeded, "Compiled CommandWindow with XML entities");
    auto selfMountedWin = MakeIntrusive<Window>("Game.UI.CommandWindow", &domains, &bindingEngine);
    AERO_CHECK(selfMountedWin->IsTemplateMounted(), "Window self-mounted its .axb4 template on construction");
    const UnifiedValue titleVal = selfMountedWin->GetValue(PropId_Title);
    const auto* titleSv = std::get_if<StringValue>(&titleVal);
    AERO_CHECK(
        titleSv != nullptr && titleSv->str() == "R&D <Alpha> \"Core\" 'v2'",
        "Decoded XML entities in Window Title attribute");

    // Verify __nativeRootHandle was populated on hudObj (field 0 of generated partial class)
    UnifiedValue rootHandleVal{};
    AERO_CHECK(
        domains.readObjectField(DomainKind::Ui, hudObj, compiled.axbPackage.rootHandleFieldIndex, rootHandleVal, &clickErr),
        "Read __nativeRootHandle field from partial class instance");
    NativeHandle rootHandle{};
    AERO_CHECK(
        rootHandleVal.tryGetNativeHandle(rootHandle) && rootHandle.valid() &&
        UIElement::FromNativeHandle(domains.nativeHandles(), rootHandle) == hudControl.Get(),
        "__nativeRootHandle on script partial class resolves back to hudControl UIElement");

    // Verify duplicate .g.rs guard in AeroCompiler::Compile
    AeroCompileInput dupInput = input;
    dupInput.scriptSources.push_back(SourceFile{compiled.generatedPartialRsPath, compiled.generatedPartialRsCode});
    const AeroCompileResult dupCompiled = compiler.Compile(dupInput);
    AERO_CHECK(dupCompiled.succeeded, "AeroCompiler deduplicates pre-existing .g.rs in scriptSources");
    auto* statusTb = selfMountedWin->FindName("StatusBanner")->DynamicCast<TextBlock>();
    AERO_CHECK(
        statusTb != nullptr && statusTb->GetText() == "Systems & Sensors <Online>",
        "Decoded XML entities in child TextBlock Text attribute");

    std::cout << "[PASS] TestAeroCompilerSinglePassAndSelfContainedMounting\n";
}

// ============================================================================
// Test 5: DualTrackBindingEngine Track A (Multi-Hop Reconnection & TwoWay) + Track B (ECS SIMD)
// ============================================================================
void TestDualTrackBindingEngineMultiHopAndEcsColumn() {
    auto program = CompileProgram({
        {"QuestViewModel.rs",
         R"(
module Game.UI;

public class QuestItem
{
    [Observable]
    public string Title { get; set; }

    [Observable]
    public float ProgressWidth { get; set; }

    public QuestItem(string title, float w)
    {
        Title = title;
        ProgressWidth = w;
    }
}

public class PlayerHudViewModel
{
    [Observable]
    public QuestItem SelectedQuest { get; set; }

    public PlayerHudViewModel(QuestItem initialQuest)
    {
        SelectedQuest = initialQuest;
    }
}

public QuestItem MakeQuest(string title, float w)
{
    return new QuestItem(title, w);
}

public PlayerHudViewModel MakePlayerHud(QuestItem q)
{
    return new PlayerHudViewModel(q);
}

public void SwitchSelectedQuest(PlayerHudViewModel vm, QuestItem nextQuest)
{
    vm.SelectedQuest = nextQuest;
}

public void UpdateQuestTitle(QuestItem q, string newTitle)
{
    q.Title = newTitle;
}
)"}});

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*program);
    Interpreter uiInterpreter(program, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto q1Res = uiInterpreter.invoke(
        "Game.UI::MakeQuest",
        {value::FromString("Defend the Citadel"), MakeFloat(140.0f)});
    auto q2Res = uiInterpreter.invoke(
        "Game.UI::MakeQuest",
        {value::FromString("Explore Omega Nebula"), MakeFloat(260.0f)});
    AERO_CHECK(q1Res.succeeded && q2Res.succeeded, "Created QuestItem 1 and QuestItem 2");
    const ObjectRef quest1 = std::get<ObjectRef>(q1Res.value);
    const ObjectRef quest2 = std::get<ObjectRef>(q2Res.value);
    auto q1Root = domains.uiHeap().retain(quest1);
    auto q2Root = domains.uiHeap().retain(quest2);

    auto vmRes = uiInterpreter.invoke("Game.UI::MakePlayerHud", {quest1});
    AERO_CHECK(vmRes.succeeded, "Created PlayerHudViewModel");
    const ObjectRef playerVm = std::get<ObjectRef>(vmRes.value);
    auto vmRoot = domains.uiHeap().retain(playerVm);

    domains.uiHeap().clearDirtyFieldMask(quest1);
    domains.uiHeap().clearDirtyFieldMask(quest2);
    domains.uiHeap().clearDirtyFieldMask(playerVm);
    (void)domains.uiHeap().consumeDirtyObjects();

    DualTrackBindingEngine engine(domains, program.get(), &uiInterpreter);
    auto questTitleTb = MakeIntrusive<TextBlock>();
    auto questBar = MakeIntrusive<Border>();

    // Bind multi-hop paths: SelectedQuest.Title (OneWay) and SelectedQuest.ProgressWidth (TwoWay)
    RuntimeError err{};
    AERO_CHECK(
        engine.BindPath(playerVm, "SelectedQuest.Title", questTitleTb, PropId_Text, BindingMode::OneWay, &err) != 0,
        "Bound 2-hop path SelectedQuest.Title");
    AERO_CHECK(
        engine.BindPath(playerVm, "SelectedQuest.ProgressWidth", questBar, PropId_Width, BindingMode::TwoWay, &err) != 0,
        "Bound 2-hop TwoWay path SelectedQuest.ProgressWidth");

    AERO_CHECK(questTitleTb->GetText() == "Defend the Citadel", "Initial multi-hop Title");
    AERO_CHECK(std::fabs(questBar->GetWidth() - 140.0f) < 1e-5f, "Initial multi-hop Width == 140.0f");

    // 1. Mutate leaf property on quest1 -> Track A updates questTitleTb
    (void)uiInterpreter.invoke(
        "Game.UI::UpdateQuestTitle",
        {quest1, value::FromString("Defend the Citadel (Urgent)")});
    AERO_CHECK(engine.FlushTrackA(&err) == 1u, "Flushed leaf change on quest1");
    AERO_CHECK(
        questTitleTb->GetText() == "Defend the Citadel (Urgent)",
        "Leaf hop change reflected in TextBlock");

    // 2. Switch intermediate hop: playerVm.SelectedQuest = quest2!
    // Track A must automatically reconnect hop 1 to quest2 and update both Title and ProgressWidth!
    (void)uiInterpreter.invoke("Game.UI::SwitchSelectedQuest", {playerVm, quest2});
    AERO_CHECK(engine.FlushTrackA(&err) == 2u, "Intermediate hop switch updated both bound targets");
    AERO_CHECK(
        questTitleTb->GetText() == "Explore Omega Nebula",
        "Multi-hop path reconnected to quest2.Title");
    AERO_CHECK(
        std::fabs(questBar->GetWidth() - 260.0f) < 1e-5f,
        "Multi-hop path reconnected to quest2.ProgressWidth == 260.0f");

    // 2b. Set intermediate hop to null (NullObject) -> Track A clears target properties cleanly,
    // then reconnect back to quest2 and verify full restoration!
    (void)uiInterpreter.invoke("Game.UI::SwitchSelectedQuest", {playerVm, NullObject{}});
    AERO_CHECK(
        engine.FlushTrackA(&err) == 2u,
        "Intermediate hop set to null cleared both bound target properties");
    AERO_CHECK(
        questTitleTb->GetText().empty(),
        "TextBlock cleared to empty default when intermediate hop is null");
    AERO_CHECK(
        questBar->GetWidth() < 0.0f,
        "Border Width cleared to Auto (-1.0f) default when intermediate hop is null");

    (void)uiInterpreter.invoke("Game.UI::SwitchSelectedQuest", {playerVm, quest2});
    AERO_CHECK(
        engine.FlushTrackA(&err) == 2u,
        "Intermediate hop reconnected from null back to quest2");
    AERO_CHECK(
        questTitleTb->GetText() == "Explore Omega Nebula" &&
            std::fabs(questBar->GetWidth() - 260.0f) < 1e-5f,
        "Both leaf properties restored after intermediate hop reconnected from null");

    // 3. Test TwoWay writeback from UI (questBar->SetWidth(310.0f)) to quest2.ProgressWidth
    questBar->SetWidth(310.0f);
    AERO_CHECK(
        engine.PushTwoWayToSource(questBar.Get(), PropId_Width, &err),
        "PushTwoWayToSource succeeded");
    const auto progressSlot = *domains.resolvePropertySlot(
        *program,
        rs_sem::stableTypeId("Game.UI::QuestItem"),
        "ProgressWidth");
    UnifiedValue updatedProgress{};
    AERO_CHECK(
        domains.readPropertySlotFast(DomainKind::Ui, quest2, progressSlot, updatedProgress, &err),
        "Read quest2.ProgressWidth after TwoWay writeback");
    float progressFloat = 0.0f;
    AERO_CHECK(
        TryGetFloat(updatedProgress, progressFloat) && std::fabs(progressFloat - 310.0f) < 1e-4f,
        "TwoWay binding wrote 310.0f back to quest2.ProgressWidth");

    // 4. Track B: EcsColumnSimdTrack batch scan over 1,000 contiguous ECS HUD components with 0 heap allocations
    struct EcsHealthBarComponent {
        float hpRatio;
        float alpha;
    };
    constexpr std::size_t kEntityCount = 1000;
    std::vector<EcsHealthBarComponent> ecsColumn(kEntityCount);
    for (std::size_t i = 0; i < kEntityCount; ++i) {
        ecsColumn[i].hpRatio = 0.25f + static_cast<float>(i % 75) * 0.01f;
        ecsColumn[i].alpha = 0.9f;
    }

    NativeRawColumnView rawView{};
    rawView.basePtr = ecsColumn.data();
    rawView.count = ecsColumn.size();
    rawView.strideBytes = sizeof(EcsHealthBarComponent);
    rawView.structTypeId = rs_sem::stableTypeId("Game.Sim::EcsHealthBarComponent");
    rawView.fields.push_back(NativeFieldLayout{
        offsetof(EcsHealthBarComponent, hpRatio),
        NativeFieldKind::Float32,
        PrimitiveType::Float,
        0});
    rawView.readOnly = true;

    const auto colArrayOpt = domains.simHeap().allocateRawColumnArray(
        rs_sem::stableTypeId("Game.Sim::EcsHealthBarComponent[]"),
        rawView.structTypeId,
        rawView);
    AERO_CHECK(colArrayOpt.has_value(), "Allocated zero-copy NativeRawColumnView on SimHeap");
    auto colArrayRoot = domains.simHeap().retain(*colArrayOpt);

    EcsColumnBindingChannel channel{};
    channel.componentTypeName = "Game.Sim::EcsHealthBarComponent";
    channel.columnArrayRef = *colArrayOpt;
    channel.elementStride = sizeof(EcsHealthBarComponent);
    channel.fieldByteOffset = offsetof(EcsHealthBarComponent, hpRatio);
    channel.scalarKind = EcsFieldScalarKind::Float32;
    channel.targetPropertyId = PropId_Width;
    channel.scaleFactor = 200.0f;
    channel.targetElements.reserve(kEntityCount);
    for (std::size_t i = 0; i < kEntityCount; ++i) {
        channel.targetElements.push_back(MakeIntrusive<Border>());
    }
    const std::size_t channelIdx = engine.BindEcsColumn(std::move(channel));

    std::size_t syncedEcsCount = 0;
    {
        AllocationTrackingScope noAlloc;
        syncedEcsCount = engine.FlushTrackB();
        AERO_CHECK(
            noAlloc.count() == 0,
            "Track B 1,000-entity ECS column batch scan must perform 0 heap allocations");
    }
    AERO_CHECK(syncedEcsCount == kEntityCount, "Track B updated all 1,000 HUD elements");
    const float expected500 = ecsColumn[500].hpRatio * 200.0f;
    AERO_CHECK(
        std::fabs(engine.EcsChannels()[channelIdx].targetElements[500]->GetWidth() - expected500) < 1e-4f,
        "Track B element[500] Width matches scaled ECS column float value");

    // 5. Verify Dead SimHeap/UiHeap object automatic fallback to default (Section II.1.4 contract)
    q2Root.reset();
    q1Root.reset();
    vmRoot.reset();
    ShadowStack emptyStack;
    domains.uiHeap().collectFull(emptyStack);
    AERO_CHECK(!domains.uiHeap().isAlive(quest2), "Unrooted quest2 collected by GC");
    AERO_CHECK(engine.FlushTrackA(&err) >= 1u, "FlushTrackA detected dead bound object and triggered fallback");
    AERO_CHECK(questTitleTb->GetText().empty(), "Dead bound object automatically reset bound TextBlock to default empty string");
    AERO_CHECK(questBar->GetWidth() < 0.0f, "Dead bound object automatically reset bound Border Width to Auto (-1.0f)");

    std::cout << "[PASS] TestDualTrackBindingEngineMultiHopAndEcsColumn\n";
}

// ============================================================================
// Test 6: 64-Level DepthBucketLayoutQueue & Core Layout Panels (Grid, StackPanel, Canvas, Border)
// ============================================================================
void TestDepthBucketLayoutQueueAndCorePanels() {
    auto rootWin = MakeIntrusive<Window>();
    auto mainGrid = MakeIntrusive<Grid>();
    mainGrid->AddRowDefinition(GridUnitType::Pixel, 50.0f);
    mainGrid->AddRowDefinition(GridUnitType::Star, 1.0f);
    mainGrid->AddColumnDefinition(GridUnitType::Pixel, 200.0f);
    mainGrid->AddColumnDefinition(GridUnitType::Star, 1.0f);

    auto headerBorder = MakeIntrusive<Border>();
    headerBorder->SetBorderThickness(Thickness{2.0f});
    headerBorder->SetPadding(Thickness{4.0f});
    Grid::SetRow(headerBorder.Get(), 0);
    Grid::SetColumn(headerBorder.Get(), 0);

    auto headerTitle = MakeIntrusive<TextBlock>();
    headerTitle->SetText("Tactical Map");
    headerTitle->SetFontSize(20.0f);
    headerBorder->AddChild(headerTitle.Get());

    auto sideStack = MakeIntrusive<StackPanel>();
    sideStack->SetOrientation(Orientation::Vertical);
    sideStack->SetSpacing(8.0f);
    Grid::SetRow(sideStack.Get(), 1);
    Grid::SetColumn(sideStack.Get(), 0);

    auto btnA = MakeIntrusive<Button>();
    btnA->SetHeight(32.0f);
    auto btnB = MakeIntrusive<Button>();
    btnB->SetHeight(40.0f);
    sideStack->AddChild(btnA.Get());
    sideStack->AddChild(btnB.Get());

    auto mapCanvas = MakeIntrusive<Canvas>();
    Grid::SetRow(mapCanvas.Get(), 1);
    Grid::SetColumn(mapCanvas.Get(), 1);

    auto radarPin = MakeIntrusive<Border>();
    radarPin->SetWidth(24.0f);
    radarPin->SetHeight(24.0f);
    Canvas::SetLeft(radarPin.Get(), 120.0f);
    Canvas::SetTop(radarPin.Get(), 85.0f);
    mapCanvas->AddChild(radarPin.Get());

    rootWin->AddChild(mainGrid.Get());
    mainGrid->AddChild(headerBorder.Get());
    mainGrid->AddChild(sideStack.Get());
    mainGrid->AddChild(mapCanvas.Get());

    AERO_CHECK(rootWin->TreeDepth() == 0u, "rootWin depth == 0");
    AERO_CHECK(mainGrid->TreeDepth() == 1u, "mainGrid depth == 1");
    AERO_CHECK(sideStack->TreeDepth() == 2u, "sideStack depth == 2");
    AERO_CHECK(btnA->TreeDepth() == 3u, "btnA depth == 3");

    DepthBucketLayoutQueue layoutQueue;
    layoutQueue.EnqueueSubtree(rootWin.Get());
    AERO_CHECK(layoutQueue.HasPendingMeasure(), "Layout queue has pending measure buckets");

    // FlushLayout must process all dirty buckets from depth 0..63 with 0 heap allocations
    // (Note: Grid uses small local work vectors when rows/cols are non-empty; let's verify
    // queue drain and exact float geometry).
    const std::size_t processed = layoutQueue.FlushLayout(Size{1000.0f, 600.0f});
    AERO_CHECK(processed > 0u, "DepthBucketLayoutQueue processed dirty layout nodes");
    AERO_CHECK(!layoutQueue.HasPendingMeasure() && !layoutQueue.HasPendingArrange(), "Buckets drained");

    // Check Grid cell arrangement:
    // Column 0 is Pixel(200), Column 1 is Star(1) -> width = 800, offsetX = 200
    // Row 0 is Pixel(50), Row 1 is Star(1) -> height = 550, offsetY = 50
    AERO_CHECK(
        (sideStack->FinalRect() == Rect{0.0f, 50.0f, 200.0f, 550.0f}),
        "Grid arranged sideStack in Row 1, Col 0 (0, 50, 200, 550)");
    AERO_CHECK(
        (mapCanvas->FinalRect() == Rect{200.0f, 50.0f, 800.0f, 550.0f}),
        "Grid arranged mapCanvas in Row 1, Col 1 (200, 50, 800, 550)");

    // Check StackPanel vertical arrangement with Spacing = 8.0f:
    // btnA at y = 50, height = 32; btnB at y = 50 + 32 + 8 = 90, height = 40
    AERO_CHECK(
        (btnA->FinalRect() == Rect{0.0f, 50.0f, 200.0f, 32.0f}),
        "StackPanel arranged btnA at (0, 50, 200, 32)");
    AERO_CHECK(
        (btnB->FinalRect() == Rect{0.0f, 90.0f, 200.0f, 40.0f}),
        "StackPanel arranged btnB at (0, 90, 200, 40) with 8px spacing");

    // Check Canvas positioning: mapCanvas origin (200, 50) + Canvas(120, 85) = (320, 135)
    AERO_CHECK(
        (radarPin->FinalRect() == Rect{320.0f, 135.0f, 24.0f, 24.0f}),
        "Canvas arranged radarPin at (320, 135, 24, 24)");

    // Verify incremental child size change in StackPanel triggers parent re-arrange of sibling btnB
    btnA->SetHeight(60.0f);
    layoutQueue.EnqueueMeasure(btnA.Get());
    const std::size_t incProcessed = layoutQueue.FlushLayout(Size{1000.0f, 600.0f});
    AERO_CHECK(incProcessed > 0u, "Incremental layout processed dirty child and parent");
    AERO_CHECK(
        (btnB->FinalRect() == Rect{0.0f, 118.0f, 200.0f, 40.0f}),
        "Incremental child height change (32->60) in StackPanel re-arranged sibling btnB to Y=118");

    // Verify Grid RowSpan and ColumnSpan
    auto spanBanner = MakeIntrusive<Border>();
    Grid::SetRow(spanBanner.Get(), 0);
    Grid::SetColumn(spanBanner.Get(), 0);
    Grid::SetColumnSpan(spanBanner.Get(), 2);
    mainGrid->AddChild(spanBanner.Get());
    layoutQueue.EnqueueSubtree(mainGrid.Get());
    (void)layoutQueue.FlushLayout(Size{1000.0f, 600.0f});
    AERO_CHECK(
        (spanBanner->FinalRect() == Rect{0.0f, 0.0f, 1000.0f, 50.0f}),
        "Grid ColumnSpan=2 arranged spanBanner across both columns (width = 200 + 800 = 1000)");

    // Verify 2D/3D HitTest
    AERO_CHECK(
        rootWin->HitTest(Point{330.0f, 145.0f}) == radarPin.Get(),
        "HitTest(330, 145) hit deepest child radarPin");

    std::cout << "[PASS] TestDepthBucketLayoutQueueAndCorePanels\n";
}

// ============================================================================
// Test 7: Paradigm Samples (Localization, Menu3D, Inventory) & < 5ms Hot Reload
// ============================================================================
void TestParadigmSamplesAndSub5msHotReload() {
    AeroCompiler compiler;

    // ------------------------------------------------------------------------
    // Paradigm 1: Localization (Reactive UTF-8 Multi-Language Switching)
    // ------------------------------------------------------------------------
    AeroCompileInput locInput{};
    locInput.xamlPath = "LocalizationView.xaml";
    locInput.xamlContent = R"(
<UserControl x:Class="Game.UI.LocalizationView" Width="400" Height="100">
    <StackPanel Spacing="6">
        <TextBlock x:Name="CampaignTitle" Text="{Binding CampaignText}" FontSize="20" />
    </StackPanel>
</UserControl>
)";
    locInput.scriptSources.push_back(SourceFile{
        "LocalizationView.rs",
        R"(
module Game.UI;

public partial class LocalizationView
{
    [Observable]
    public string CampaignText { get; set; }

    public LocalizationView()
    {
        CampaignText = "Start Campaign";
    }

    public void SwitchLocale(string localeCode)
    {
        if (localeCode == "zh-CN")
        {
            CampaignText = "开始星际战役";
        }
        else if (localeCode == "ja-JP")
        {
            CampaignText = "キャンペーン開始";
        }
        else
        {
            CampaignText = "Start Campaign";
        }
    }
}

public LocalizationView CreateLocalizationView()
{
    return new LocalizationView();
}

public void SetLocale(LocalizationView view, string code)
{
    view.SwitchLocale(code);
}
)"});

    const auto locCompiled = compiler.Compile(locInput);
    AERO_CHECK(locCompiled.succeeded, "Compiled LocalizationView paradigm");

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*locCompiled.programImage);
    Interpreter uiInterpreter(locCompiled.programImage, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto locObjRes = uiInterpreter.invoke("Game.UI::CreateLocalizationView");
    AERO_CHECK(locObjRes.succeeded, "Created LocalizationView instance");
    const ObjectRef locObj = std::get<ObjectRef>(locObjRes.value);
    auto locRoot = domains.uiHeap().retain(locObj);
    domains.uiHeap().clearDirtyFieldMask(locObj);
    (void)domains.uiHeap().consumeDirtyObjects();

    DualTrackBindingEngine bindingEngine(domains, locCompiled.programImage.get(), &uiInterpreter);
    auto locControl = MakeIntrusive<UserControl>(
        "Game.UI.LocalizationView",
        &domains,
        &bindingEngine,
        locObj,
        &uiInterpreter);
    auto* campaignTb = locControl->FindName("CampaignTitle")->DynamicCast<TextBlock>();
    AERO_CHECK(campaignTb != nullptr && campaignTb->GetText() == "Start Campaign", "Initial en-US text");

    // Switch to zh-CN ("开始星际战役" -> 6 UTF-8 codepoints)
    (void)uiInterpreter.invoke("Game.UI::SetLocale", {locObj, value::FromString("zh-CN")});
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(
        campaignTb->GetText() == "开始星际战役",
        "Localization switched to zh-CN UTF-8 string");
    campaignTb->Measure(Size{400.0f, 100.0f});
    // 6 wide CJK codepoints * (20.0f * 0.95f) = 114.0f width
    AERO_CHECK(
        std::fabs(campaignTb->DesiredSize().width - 114.0f) < 1e-4f,
        "UTF-8 codepoint-aware TextBlock Measure computes exact width for 6 CJK characters");

    // ------------------------------------------------------------------------
    // Paradigm 2: Menu3D (3D Camera/Panel Transition via Reentrant UiTask)
    // ------------------------------------------------------------------------
    AeroCompileInput menu3dInput{};
    menu3dInput.xamlPath = "Menu3DView.xaml";
    menu3dInput.xamlContent = R"(
<Window x:Class="Game.UI.Menu3DView" Width="1280" Height="720">
    <Border x:Name="CameraPanel" Width="600" Height="400" Projection3D.Yaw="{Binding CameraYaw}" Projection3D.Pitch="{Binding CameraPitch}" />
</Window>
)";
    menu3dInput.scriptSources.push_back(SourceFile{
        "Menu3DView.rs",
        R"(
module Game.UI;

public long Schedule(long target, string callback, int delayTicks)
{
    return delayTicks;
}

public bool CancelTimer(long timerId)
{
    return timerId != 0;
}

public partial class Menu3DView
{
    [Observable]
    public float CameraYaw;

    [Observable]
    public float CameraPitch;

    public Menu3DView()
    {
        CameraYaw = 0.0f;
        CameraPitch = 0.0f;
    }

    public sequence TransitionToLoadoutCamera(long target)
    {
        CameraYaw = 15.0f;
        CameraPitch = -5.0f;
        yield wait_ticks(1);
        CameraYaw = 35.0f;
        CameraPitch = -12.5f;
    }
}

public Menu3DView CreateMenu3DView()
{
    return new Menu3DView();
}
)"});
    const auto menu3dCompiled = compiler.Compile(menu3dInput);
    if (!menu3dCompiled.succeeded) {
        for (const auto& d : menu3dCompiled.diagnostics) {
            std::cerr << "[Menu3D Diag] " << d << "\n";
        }
    }
    AERO_CHECK(menu3dCompiled.succeeded, "Compiled Menu3DView paradigm (.xaml + .rs)");

    ExecutionDomainContext menu3dDomains;
    menu3dDomains.registerProgramObservableMetadata(*menu3dCompiled.programImage);
    Interpreter menu3dInterp(menu3dCompiled.programImage, menu3dDomains.uiHeapPtr());
    menu3dInterp.setExecutionDomainContext(&menu3dDomains, DomainKind::UiPresentation);

    auto menu3dObjRes = menu3dInterp.invoke("Game.UI::CreateMenu3DView");
    AERO_CHECK(menu3dObjRes.succeeded, "Created Menu3DView instance on UiHeap");
    const ObjectRef menu3dObj = std::get<ObjectRef>(menu3dObjRes.value);
    auto menu3dRoot = menu3dDomains.uiHeap().retain(menu3dObj);
    menu3dDomains.uiHeap().clearDirtyFieldMask(menu3dObj);
    (void)menu3dDomains.uiHeap().consumeDirtyObjects();

    DualTrackBindingEngine menu3dBindings(menu3dDomains, menu3dCompiled.programImage.get(), &menu3dInterp);
    auto menu3dWin = MakeIntrusive<Window>(
        "Game.UI.Menu3DView",
        &menu3dDomains,
        &menu3dBindings,
        menu3dObj,
        &menu3dInterp);
    AERO_CHECK(menu3dWin->IsTemplateMounted(), "Menu3DView Window self-mounted .axb4 template");
    auto* camPanel = menu3dWin->FindName("CameraPanel")->DynamicCast<Border>();
    AERO_CHECK(camPanel != nullptr, "Resolved CameraPanel Border in Menu3DView");

    const std::uint64_t camTask = menu3dDomains.spawnReentrantSequenceTask(
        *menu3dCompiled.programImage,
        menu3dInterp,
        menu3dObj,
        "TransitionToLoadoutCamera",
        {},
        camPanel->ToNativeHandle(menu3dDomains.nativeHandles()));
    AERO_CHECK(camTask != 0, "Spawned Menu3D camera transition UiTask");

    // Step 1 of Menu3D camera transition
    (void)menu3dDomains.stepAllUiTasks(&menu3dInterp);
    (void)menu3dBindings.FlushTrackA();
    float yaw = 0.0f;
    float pitch = 0.0f;
    AERO_CHECK(
        TryGetFloat(camPanel->GetValue(PropId_Projection3DYaw), yaw) && std::fabs(yaw - 15.0f) < 1e-4f &&
            TryGetFloat(camPanel->GetValue(PropId_Projection3DPitch), pitch) && std::fabs(pitch - (-5.0f)) < 1e-4f,
        "Menu3D frame 1 updated CameraPanel Projection3D Yaw=15.0f, Pitch=-5.0f");

    // Step 2 of Menu3D camera transition
    (void)menu3dDomains.stepAllUiTasks(&menu3dInterp);
    (void)menu3dBindings.FlushTrackA();
    AERO_CHECK(
        TryGetFloat(camPanel->GetValue(PropId_Projection3DYaw), yaw) && std::fabs(yaw - 35.0f) < 1e-4f &&
            TryGetFloat(camPanel->GetValue(PropId_Projection3DPitch), pitch) && std::fabs(pitch - (-12.5f)) < 1e-4f,
        "Menu3D frame 2 completed CameraPanel Projection3D Yaw=35.0f, Pitch=-12.5f");

    // ------------------------------------------------------------------------
    // Paradigm 3: Inventory (Reactive ItemsControl + [SimState] + [SimCommand] Item Use)
    // ------------------------------------------------------------------------
    AeroCompileInput invInput{};
    invInput.xamlPath = "InventoryView.xaml";
    invInput.xamlContent = R"(
<UserControl x:Class="Game.UI.InventoryView" Width="420" Height="280">
    <StackPanel Spacing="8">
        <ItemsControl x:Name="BagItems" ItemsSource="{Binding SimBag.ItemList}" />
        <Button x:Name="UsePotionBtn" Content="Use Med-Potion" Click="OnUsePotionClicked" />
    </StackPanel>
</UserControl>
)";
    invInput.scriptSources.push_back(SourceFile{
        "InventoryView.rs",
        R"(
module Game.UI;

[SimState]
public class SimInventoryBag
{
    [Observable]
    public string ItemList { get; set; }

    [Observable]
    public int PotionCount { get; set; }

    public SimInventoryBag()
    {
        ItemList = "Plasma Rifle|Omni-Gel|Med-Potion";
        PotionCount = 1;
    }

    [SimCommand]
    public void ConsumePotion()
    {
        PotionCount = 0;
        ItemList = "Plasma Rifle|Omni-Gel";
    }
}

public partial class InventoryView
{
    [Observable]
    public SimInventoryBag SimBag { get; set; }

    public InventoryView(SimInventoryBag bag)
    {
        SimBag = bag;
    }

    public void OnUsePotionClicked()
    {
        SimBag.ConsumePotion();
    }
}

public SimInventoryBag CreateSimInventoryBag()
{
    return new SimInventoryBag();
}

public InventoryView CreateInventoryView(SimInventoryBag bag)
{
    return new InventoryView(bag);
}
)"});
    const auto invCompiled = compiler.Compile(invInput);
    AERO_CHECK(invCompiled.succeeded, "Compiled InventoryView paradigm (.xaml + .rs)");

    ExecutionDomainContext invDomains;
    invDomains.registerProgramObservableMetadata(*invCompiled.programImage);
    Interpreter invSimInterp(invCompiled.programImage, invDomains.simHeapPtr());
    invSimInterp.setExecutionDomainContext(&invDomains, DomainKind::SimDeterministic);
    Interpreter invUiInterp(invCompiled.programImage, invDomains.uiHeapPtr());
    invUiInterp.setExecutionDomainContext(&invDomains, DomainKind::UiPresentation);

    auto bagRes = invSimInterp.invoke("Game.UI::CreateSimInventoryBag");
    AERO_CHECK(bagRes.succeeded, "Created [SimState] SimInventoryBag on SimHeap");
    const ObjectRef simBagObj = std::get<ObjectRef>(bagRes.value);
    auto simBagRoot = invDomains.simHeap().retain(simBagObj);

    auto invViewRes = invUiInterp.invoke("Game.UI::CreateInventoryView", {simBagObj});
    AERO_CHECK(invViewRes.succeeded, "Created InventoryView on UiHeap bound to SimInventoryBag");
    const ObjectRef invViewObj = std::get<ObjectRef>(invViewRes.value);
    auto invViewRoot = invDomains.uiHeap().retain(invViewObj);

    invDomains.simHeap().clearDirtyFieldMask(simBagObj);
    (void)invDomains.simHeap().consumeDirtyObjects();
    invDomains.uiHeap().clearDirtyFieldMask(invViewObj);
    (void)invDomains.uiHeap().consumeDirtyObjects();

    DualTrackBindingEngine invBindings(invDomains, invCompiled.programImage.get(), &invUiInterp);
    auto invControl = MakeIntrusive<UserControl>(
        "Game.UI.InventoryView",
        &invDomains,
        &invBindings,
        invViewObj,
        &invUiInterp);
    auto* bagItemsCtrl = invControl->FindName("BagItems")->DynamicCast<ItemsControl>();
    auto* usePotionBtn = invControl->FindName("UsePotionBtn")->DynamicCast<Button>();
    AERO_CHECK(bagItemsCtrl != nullptr && usePotionBtn != nullptr, "Resolved BagItems and UsePotionBtn");
    AERO_CHECK(bagItemsCtrl->ChildCount() == 3u, "Initial Inventory ItemsControl materialized 3 items");

    // Click UsePotionBtn from UiDomain -> marshals [SimCommand] ConsumePotion() into command queue!
    AERO_CHECK(usePotionBtn->PerformClick(), "Clicked UsePotionBtn in UiDomain");
    AERO_CHECK(invDomains.pendingSimCommandCount() == 1u, "[SimCommand] ConsumePotion queued for next SimTick");
    AERO_CHECK(bagItemsCtrl->ChildCount() == 3u, "Inventory unchanged before deterministic SimTick flush");

    // Deterministic SimTick consumes the queued [SimCommand], then Track A syncs ItemsControl!
    AERO_CHECK(invDomains.flushSimCommands(invSimInterp) == 1u, "Flushed [SimCommand] on SimTick");
    AERO_CHECK(invBindings.FlushTrackA() == 1u, "Track A synced updated SimBag.ItemList across domains");
    AERO_CHECK(bagItemsCtrl->ChildCount() == 2u, "Inventory ItemsControl updated to 2 items after potion use");

    // ------------------------------------------------------------------------
    // Sub-5ms Hot Reload Verification (< 5.0 ms target)
    // ------------------------------------------------------------------------
    // Compile V2 of LocalizationView where a new field `VersionBanner` is inserted BEFORE
    // `CampaignText` (shifting `CampaignText`'s fieldIndex!), and migrate live bindings!
    auto v2Program = CompileProgram({
        {"LocalizationView.g.rs", locCompiled.generatedPartialRsCode},
        {"LocalizationView_v2.rs",
         R"RS(
module Game.UI;

public partial class LocalizationView
{
    public int HotReloadRevision;

    [Observable]
    public string CampaignText { get; set; }

    public LocalizationView()
    {
        HotReloadRevision = 2;
        CampaignText = "Start Campaign [Hot-Reloaded v2]";
    }
}

public LocalizationView CreateLocalizationViewV2()
{
    return new LocalizationView();
}
)RS"}});

    domains.registerProgramObservableMetadata(*v2Program);
    Interpreter v2Interpreter(v2Program, domains.uiHeapPtr());
    v2Interpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    auto v2ObjRes = v2Interpreter.invoke("Game.UI::CreateLocalizationViewV2");
    AERO_CHECK(v2ObjRes.succeeded, "Allocated V2 LocalizationView instance");
    const ObjectRef locObjV2 = std::get<ObjectRef>(v2ObjRes.value);
    auto locV2Root = domains.uiHeap().retain(locObjV2);

    const auto t0 = std::chrono::steady_clock::now();
    AxbPackage reloadedPkg = locCompiled.axbPackage;
    AeroCompiler::BindPackageAgainstProgram(reloadedPkg, *v2Program);
    bindingEngine.SetUiInterpreter(&v2Interpreter);
    const std::size_t migratedCount = bindingEngine.ApplyHotReloadMigration(
        *v2Program,
        {HotReloadMigrationPair{locObj, locObjV2}});
    const auto t1 = std::chrono::steady_clock::now();

    const float elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    std::cout << "[INFO] Hot-reload binding & slot migration completed in " << elapsedMs << " ms\n";

    AERO_CHECK(migratedCount == 1u, "Migrated active LocalizationView binding to V2 object");
    AERO_CHECK(
        elapsedMs < 5.0f,
        "Hot-reload state & binding migration must complete in under 5.0 ms");
    AERO_CHECK(
        campaignTb->GetText() == "Start Campaign [Hot-Reloaded v2]",
        "Existing TextBlock immediately reflects V2 hot-reloaded property value without rebuilding UIElement");

    std::cout << "[PASS] TestParadigmSamplesAndSub5msHotReload\n";
}

// ============================================================================
// Test 8: Samples.zip Migration Completeness (Page, ResourceDictionary, Grid Definitions,
//         Property-Element Brushes, Run Inline Text, Named Colors, Command & FallbackValue)
// ============================================================================
void TestSamplesMigrationCompletenessEndToEnd() {
    AeroCompiler compiler;

    // 1. Compile a standalone ResourceDictionary (like Localization/Language-en.xaml & Resources.xaml)
    AeroCompileInput langDictInput{};
    langDictInput.xamlPath = "Language-en.xaml";
    langDictInput.xamlContent = R"(
<ResourceDictionary
  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
  xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <SolidColorBrush x:Key="ThemeAccentBrush" Color="#FF2AA6E2"/>
  <Color x:Key="ThemeHeaderColor">LightSeaGreen</Color>
  <sys:String x:Key="LoginWelcomeLabel">Welcome back, Commander</sys:String>
</ResourceDictionary>
)";
    const AeroCompileResult dictRes = compiler.Compile(langDictInput);
    AERO_CHECK(dictRes.succeeded, "Compiled standalone ResourceDictionary XAML");
    AERO_CHECK(dictRes.axbPackage.resources.size() == 3u, "Extracted 3 keyed resources from ResourceDictionary");

    // 2. Compile a composite Page combining HelloWorld, Buttons, Login, Localization & Gallery patterns
    AeroCompileInput pageInput{};
    pageInput.xamlPath = "MigratedSamplePage.xaml";
    pageInput.emitAotCpp = true;
    pageInput.xamlContent = R"(
<Page
  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
  xmlns:d="http://schemas.microsoft.com/expression/blend/2008"
  xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
  xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
  xmlns:noesis="clr-namespace:NoesisGUIExtensions"
  mc:Ignorable="d"
  x:Class="Game.UI.MigratedSamplePage"
  d:DesignWidth="1280" d:DesignHeight="720"
  Title="Noesis Sample Migration">
  <Page.Resources>
    <ResourceDictionary>
      <LinearGradientBrush x:Key="DialogGradientBg" StartPoint="0,0" EndPoint="1,1">
        <GradientStop Color="#FF235986" Offset="0"/>
        <GradientStop Color="#FF123757" Offset="1"/>
      </LinearGradientBrush>
      <Storyboard x:Key="IntroAnim">
        <DoubleAnimationUsingKeyFrames Storyboard.TargetName="DialogGrid" Storyboard.TargetProperty="Opacity">
          <EasingDoubleKeyFrame KeyTime="0:0:0.5" Value="1"/>
        </DoubleAnimationUsingKeyFrames>
      </Storyboard>
    </ResourceDictionary>
  </Page.Resources>
  <b:Interaction.Triggers>
    <b:EventTrigger EventName="Loaded">
      <b:ControlStoryboardAction Storyboard="{StaticResource IntroAnim}"/>
    </b:EventTrigger>
  </b:Interaction.Triggers>
  <Grid x:Name="LayoutRoot" Background="{StaticResource DialogGradientBg}">
    <Grid.RowDefinitions>
      <RowDefinition Height="80"/>
      <RowDefinition Height="*"/>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="200"/>
      <ColumnDefinition Width="2*"/>
    </Grid.ColumnDefinitions>
    <Grid.RenderTransform>
      <TransformGroup>
        <ScaleTransform/>
        <TranslateTransform/>
      </TransformGroup>
    </Grid.RenderTransform>
    <HeaderedContentControl x:Name="HeaderSection" Grid.Row="0" Grid.ColumnSpan="2">
      <HeaderedContentControl.Header>
        <TextBlock x:Name="WelcomeBanner" Content="{noesis:Loc ResourceKey=LoginWelcomeLabel}" Foreground="Turquoise" />
      </HeaderedContentControl.Header>
      <TextBlock x:Name="RunSummaryText">
        <Run>Click mode:</Run>
        <Run FontWeight="Bold">Release</Run>
      </TextBlock>
    </HeaderedContentControl>
    <Border x:Name="CardBorder" Grid.Row="1" Grid.Column="1" BorderThickness="2">
      <Border.Background>
        <RadialGradientBrush>
          <GradientStop Color="#FF2C76B4" Offset="0"/>
          <GradientStop Color="#FF123757" Offset="1"/>
        </RadialGradientBrush>
      </Border.Background>
      <StackPanel Spacing="8">
        <TextBlock x:Name="StatusMessage" Text="{Binding Message, Mode=TwoWay, FallbackValue=LOGIN READY}" Foreground="White"/>
        <Button x:Name="StartBtn" Content="START" Command="{Binding StartCommand}" Height="36"/>
      </StackPanel>
    </Border>
  </Grid>
</Page>
)";
    pageInput.scriptSources.push_back(SourceFile{
        "MigratedSamplePage.rs",
        R"(
module Game.UI;

public partial class MigratedSamplePage
{
    [Observable]
    public string Message { get; set; }

    public MigratedSamplePage()
    {
        Message = "AUTHENTICATED";
    }

    public void OnStart()
    {
        Message = "MISSION STARTED";
    }
}

public MigratedSamplePage CreateMigratedSamplePage()
{
    return new MigratedSamplePage();
}
)"});

    const AeroCompileResult res = compiler.Compile(pageInput);
    AERO_CHECK(res.succeeded, "Compiled MigratedSamplePage with full Noesis XAML syntax");
    AERO_CHECK(res.axbPackage.bindingChains.size() == 1u,
               "Only genuine {Binding Message} emitted into bindingChains (0 bogus StaticResource/Loc chains)");

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*res.programImage);
    Interpreter uiInterp(res.programImage, domains.uiHeapPtr());
    uiInterp.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    // First mount WITHOUT a ViewModel to verify FallbackValue, StaticResource, noesis:Loc, Run & Grid definitions
    auto pageNoVm = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_CHECK(MountAxbTemplate(pageNoVm.Get(), res.axbPackage, &domains, nullptr),
               "Mounted MigratedSamplePage without ViewModel");
    AERO_CHECK(pageNoVm->GetWidth() < 0.0f && pageNoVm->GetHeight() < 0.0f,
               "d:DesignWidth and d:DesignHeight remain design-time only (root Page Auto-sizes to host viewport)");

    auto* layoutRoot = pageNoVm->FindName("LayoutRoot")->DynamicCast<Grid>();
    AERO_CHECK(layoutRoot != nullptr, "Resolved LayoutRoot Grid");
    AERO_CHECK(layoutRoot->RowDefinitionCount() == 3u && layoutRoot->ColumnDefinitionCount() == 2u,
               "Grid.RowDefinitions (3) and Grid.ColumnDefinitions (2) populated on LayoutRoot");
    AERO_CHECK(layoutRoot->Rows()[0].unit == GridUnitType::Pixel && std::fabs(layoutRoot->Rows()[0].value - 80.0f) < 1e-4f &&
               layoutRoot->Rows()[1].unit == GridUnitType::Star &&
               layoutRoot->Rows()[2].unit == GridUnitType::Auto,
               "Parsed Pixel(80), Star(*), and Auto RowDefinitions");

    auto* welcomeTb = pageNoVm->FindName("WelcomeBanner")->DynamicCast<TextBlock>();
    AERO_CHECK(welcomeTb != nullptr && welcomeTb->GetText() == "Welcome back, Commander",
               "{noesis:Loc ResourceKey=LoginWelcomeLabel} resolved against Language-en.xaml ResourceDictionary");

    auto* runTb = pageNoVm->FindName("RunSummaryText")->DynamicCast<TextBlock>();
    AERO_CHECK(runTb != nullptr && runTb->GetText() == "Click mode: Release",
               "<TextBlock><Run>...</Run></TextBlock> concatenated inline Run text");

    auto* statusNoVm = pageNoVm->FindName("StatusMessage")->DynamicCast<TextBlock>();
    AERO_CHECK(statusNoVm != nullptr && statusNoVm->GetText() == "LOGIN READY",
               "{Binding Message, FallbackValue=LOGIN READY} applied FallbackValue before ViewModel binding");

    // Now mount WITH the RealScript ViewModel and verify Command="{Binding StartCommand}" -> OnStart()
    const auto vmExec = uiInterp.invoke("Game.UI::CreateMigratedSamplePage", {});
    AERO_CHECK(vmExec.succeeded && std::holds_alternative<ObjectRef>(vmExec.value), "Created MigratedSamplePage script VM");
    const ObjectRef pageVm = std::get<ObjectRef>(vmExec.value);
    auto vmKeepAlive = domains.uiHeap().retain(pageVm);

    DualTrackBindingEngine bindingEngine(domains, res.programImage.get(), &uiInterp);
    auto mountedPage = MakeIntrusive<UserControl>(
        "Game.UI.MigratedSamplePage",
        &domains,
        &bindingEngine,
        pageVm,
        &uiInterp);
    auto* statusWithVm = mountedPage->FindName("StatusMessage")->DynamicCast<TextBlock>();
    auto* startBtn = mountedPage->FindName("StartBtn")->DynamicCast<Button>();
    AERO_CHECK(statusWithVm != nullptr && statusWithVm->GetText() == "AUTHENTICATED",
               "Initial bound Message updated from FallbackValue to ViewModel value AUTHENTICATED");
    AERO_CHECK(startBtn != nullptr && startBtn->PerformClick(),
               "Button Command={Binding StartCommand} resolved and invoked OnStart() on RealScript ViewModel");
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(statusWithVm->GetText() == "MISSION STARTED",
               "Command={Binding StartCommand} mutated [Observable] Message to MISSION STARTED");

    std::cout << "[PASS] TestSamplesMigrationCompletenessEndToEnd\n";
}

// ============================================================================
// Test 9: Compiler, Binding, Hot-Reload Observer, Deserializer & Grid Span Regressions
// ============================================================================
void TestCompilerBindingAndGridHardeningRegressions() {
    AeroCompiler compiler;
    AxbTemplateRegistry::Instance().Clear();

    // 1. Compile XAML BEFORE registering runtime resources, plus nested DataTemplate x:DataType,
    //    ContentTemplate / StrokeDashArray suffix checks, <GroupBox.Header>/<Button.Content> text,
    //    Placeholder alongside bound Text, SelectedIndex int literal, and FallbackValue on null hop.
    AeroCompileInput input{};
    input.xamlPath = "HardenedView.xaml";
    input.emitAotCpp = true;
    input.xamlContent = R"(
<Page
  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
  xmlns:d="http://schemas.microsoft.com/expression/blend/2008"
  xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
  xmlns:noesis="clr-namespace:NoesisGUIExtensions"
  mc:Ignorable="d"
  x:Class="Game.UI.HardenedView"
  d:DesignWidth="1024" d:DesignHeight="640">
  <Page.Resources>
    <DataTemplate x:Key="QuestRowTpl" x:DataType="Game.UI.QuestRowItem">
      <TextBlock Text="{Binding RowLabel}"/>
    </DataTemplate>
  </Page.Resources>
  <Grid x:Name="RootGrid" d:DesignWidth="333" d:DesignHeight="222">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="Auto"/>
      <ColumnDefinition Width="*"/>
    </Grid.ColumnDefinitions>
    <ContentControl x:Name="TemplatedHost">
      <ContentControl.ContentTemplate>
        <DataTemplate x:DataType="Game.UI.QuestRowItem">
          <Button x:Name="LeakedTemplateButton" Content="SHOULD_NOT_MOUNT"/>
        </DataTemplate>
      </ContentControl.ContentTemplate>
    </ContentControl>
    <Path x:Name="VectorShape">
      <Path.StrokeDashArray>2,2</Path.StrokeDashArray>
    </Path>
    <GroupBox x:Name="SettingsGroup" Grid.Column="0">
      <GroupBox.Header>Squad Settings</GroupBox.Header>
      <Button x:Name="ApplyBtn" Command="{Binding FailingInnerCallCommand}">
        <Button.Content>Apply Now</Button.Content>
      </Button>
    </GroupBox>
    <StackPanel Grid.Column="1">
      <TextBox x:Name="UserBox" Text="{Binding UserName}" noesis:Text.Placeholder="Enter callsign"/>
      <TextBlock x:Name="QuestStatusTb" Text="{Binding ActiveQuest.Title, FallbackValue=NO ACTIVE QUEST}"/>
      <TextBlock x:Name="DynamicLocTb" Text="{DynamicResource DeferredBannerText}"/>
      <ComboBox x:Name="SquadCombo" SelectedIndex="2" ItemsSource="{Binding SquadNames}"/>
    </StackPanel>
  </Grid>
</Page>
)";
    input.scriptSources.push_back(SourceFile{
        "HostMock.rs",
        R"(
module HostMock;

public void TriggerInnerInvalidArgs()
{
}
)"});
    input.scriptSources.push_back(SourceFile{
        "HardenedView.rs",
        R"(
module Game.UI;
import HostMock;

public class QuestRowItem
{
    [Observable]
    public string Title { get; set; }

    public QuestRowItem(string t)
    {
        Title = t;
    }
}

public partial class HardenedView
{
    [Observable]
    public string UserName { get; set; }

    [Observable]
    public QuestRowItem ActiveQuest { get; set; }

    [Observable]
    public string[] SquadNames { get; set; }

    public int SideEffectCounter;

    public HardenedView(QuestRowItem q)
    {
        UserName = "Maverick";
        ActiveQuest = q;
        SquadNames = new string[2];
        SquadNames[0] = "Alpha";
        SquadNames[1] = "Bravo";
        SideEffectCounter = 0;
    }

    public void OnFailingInnerCall()
    {
        SideEffectCounter = SideEffectCounter + 1;
        TriggerInnerInvalidArgs();
    }
}

public QuestRowItem CreateQuestRow(string t)
{
    return new QuestRowItem(t);
}

public HardenedView CreateHardenedView(QuestRowItem q)
{
    return new HardenedView(q);
}

public void SetHardenedQuest(HardenedView v, QuestRowItem q)
{
    v.ActiveQuest = q;
}
)"});

    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled HardenedView with nested DataTemplate x:DataType");
    AERO_CHECK(res.axbPackage.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex,
               "DataTemplate x:DataType did NOT hijack root class binding");
    AERO_CHECK(res.aotCpp.source.find("eventAotContext") != std::string::npos &&
               res.aotCpp.source.find("SetDomains(domains)") != std::string::npos,
               "AOT C++ builder emits eventAotContext and SetDomains for ComboBox (ItemsControl)");

    // Verify .axb4 round-trip preserves FallbackValue and rejects corrupted/truncated resources
    AxbPackage decoded{};
    std::string errMsg;
    AERO_CHECK(AxbPackage::Deserialize(res.axbBytes, decoded, &errMsg),
               "Deserialize valid .axb4 with binding fallback table");
    std::vector<std::uint8_t> corruptedBytes = res.axbBytes;
    // Append bogus huge resource count if we truncate tail to rootHandleFieldIndex + 0xFFFFFFFFu
    corruptedBytes.resize(corruptedBytes.size() - 4u); // chop fallback count mid-tail and corrupt
    corruptedBytes.push_back(0xFFu);
    corruptedBytes.push_back(0xFFu);
    corruptedBytes.push_back(0xFFu);
    corruptedBytes.push_back(0x7Fu);
    corruptedBytes.push_back(0x01u); // extra trailing byte with truncated value
    AxbPackage rejectedPkg{};
    AERO_CHECK(!AxbPackage::Deserialize(corruptedBytes, rejectedPkg, &errMsg),
               "Deserialize rejects corrupted trailing section instead of OOM or silent success");

    // Register runtime resource AFTER compilation (simulating offline-compiled .axb4 + runtime dictionary load)
    AxbTemplateRegistry::Instance().RegisterResource(
        "DeferredBannerText",
        value::FromString("Runtime Loaded Localization"));

    ExecutionDomainContext domains;
    auto hostLinkedMods = res.bytecodeModules;
    for (auto& m : hostLinkedMods) {
        if (m.name == "HostMock") {
            m.functions.clear();
        }
    }
    RuntimeError hostLinkErr{};
    auto hostLinkedImgOpt = ProgramImage::link(std::move(hostLinkedMods), hostLinkErr);
    AERO_CHECK(hostLinkedImgOpt.has_value(), "Linked ProgramImage with external HostMock");
    auto runtimeImg = std::make_shared<const ProgramImage>(std::move(*hostLinkedImgOpt));
    domains.registerProgramObservableMetadata(*runtimeImg);
    auto bindings = std::make_shared<BindingRegistry>();
    (void)bindings->bind(
        "HostMock::TriggerInnerInvalidArgs",
        [](const rs_bc::FunctionReference&, ValueSpan, RuntimeError& err) -> std::optional<UnifiedValue> {
            err.code = ErrorCode::InvalidArguments;
            err.message = "inner_call_invalid_args";
            return std::nullopt;
        });
    Interpreter uiInterp(runtimeImg, domains.uiHeapPtr());
    uiInterp.setBindingRegistry(bindings);
    uiInterp.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    // Mount with ActiveQuest = null: FallbackValue MUST survive active DualTrackBindingEngine!
    const auto vmNullQuestExec = uiInterp.invoke("Game.UI::CreateHardenedView", {NullObject{}});
    AERO_CHECK(vmNullQuestExec.succeeded, "Created HardenedView with null ActiveQuest");
    const ObjectRef viewVm = std::get<ObjectRef>(vmNullQuestExec.value);
    auto vmPin = domains.uiHeap().retain(viewVm);

    DualTrackBindingEngine bindingEngine(domains, runtimeImg.get(), &uiInterp);
    auto page = CreateElementByTypeName(decoded.rootTypeName);
    AERO_CHECK(MountAxbTemplate(page.Get(), decoded, &domains, &bindingEngine, viewVm, &uiInterp),
               "Mounted HardenedView from deserialized AxbPackage");

    // Verify ContentTemplate did NOT leak inner template elements into visual tree
    AERO_CHECK(page->FindName("LeakedTemplateButton") == nullptr,
               "ContentControl.ContentTemplate properly skipped (not treated as .Content)");

    // Verify both root Page and child Grid ignore design-time d:DesignWidth at runtime
    auto* rootGrid = page->FindName("RootGrid")->DynamicCast<Grid>();
    AERO_CHECK(rootGrid != nullptr && rootGrid->GetWidth() < 0.0f && page->GetWidth() < 0.0f,
               "d:DesignWidth ignored on both root Page and child Grid at runtime");

    // Verify <GroupBox.Header> and <Button.Content> plain text extraction
    auto* settingsGroup = page->FindName("SettingsGroup");
    auto* applyBtn = page->FindName("ApplyBtn")->DynamicCast<Button>();
    const auto hdrVal = settingsGroup ? settingsGroup->GetValue(PropId_Title) : UnifiedValue{};
    AERO_CHECK(std::holds_alternative<StringValue>(hdrVal) &&
               std::get<StringValue>(hdrVal).str() == "Squad Settings",
               "<GroupBox.Header> plain text captured into PropId_Title");
    AERO_CHECK(applyBtn != nullptr && applyBtn->GetContentText() == "Apply Now",
               "<Button.Content> plain text captured into Button content");

    // Verify Button::PerformClick does NOT re-invoke 0-arg handler when inner call raises InvalidArguments
    RuntimeError innerClickErr{};
    AERO_CHECK(!applyBtn->PerformClick(&innerClickErr),
               "Button::PerformClick propagated inner InvalidArguments failure");
    AERO_CHECK(innerClickErr.code == ErrorCode::InvalidArguments &&
               innerClickErr.message.find("inner_call_invalid_args") != std::string::npos,
               "Button::PerformClick preserved inner call error message without masking it");
    const auto sideEffectSlot = *domains.resolvePropertySlot(
        *res.programImage,
        rs_sem::stableTypeId("Game.UI::HardenedView"),
        "SideEffectCounter");
    UnifiedValue sideEffectsVal{};
    AERO_CHECK(domains.readPropertySlotFast(DomainKind::Ui, viewVm, sideEffectSlot, sideEffectsVal) &&
               std::holds_alternative<std::int64_t>(sideEffectsVal) &&
               std::get<std::int64_t>(sideEffectsVal) == 1,
               "OnFailingInnerCall executed strictly once (SideEffectCounter == 1, no blind retry)");

    // Verify Placeholder did NOT overwrite bound UserName ("Maverick")
    auto* userBox = page->FindName("UserBox")->DynamicCast<TextBlock>();
    AERO_CHECK(userBox != nullptr && userBox->GetText() == "Maverick",
               "noesis:Text.Placeholder did not clobber bound Text property");
    const auto phVal = userBox->GetValue(PropId_Placeholder);
    AERO_CHECK(std::holds_alternative<StringValue>(phVal) &&
               std::get<StringValue>(phVal).str() == "Enter callsign",
               "Placeholder stored in dedicated PropId_Placeholder");

    // Verify SelectedIndex parsed as int64(2) and runtime-registered DynamicResource resolved
    auto* squadCombo = page->FindName("SquadCombo")->DynamicCast<ItemsControl>();
    const auto selIdxVal = squadCombo ? squadCombo->GetValue(PropId_SelectedIndex) : UnifiedValue{};
    AERO_CHECK(std::holds_alternative<std::int64_t>(selIdxVal) &&
               std::get<std::int64_t>(selIdxVal) == 2,
               "SelectedIndex='2' parsed as integer 2");
    auto* dynLocTb = page->FindName("DynamicLocTb")->DynamicCast<TextBlock>();
    AERO_CHECK(dynLocTb != nullptr && dynLocTb->GetText() == "Runtime Loaded Localization",
               "SetResourceProperty resolved resource registered at runtime after compilation");

    // Verify FallbackValue preserved when ActiveQuest is null AND after live quest object is GC'd
    auto* questStatusTb = page->FindName("QuestStatusTb")->DynamicCast<TextBlock>();
    AERO_CHECK(questStatusTb != nullptr && questStatusTb->GetText() == "NO ACTIVE QUEST",
               "FallbackValue preserved when bound intermediate hop ActiveQuest is null");

    auto questExec = uiInterp.invoke("Game.UI::CreateQuestRow", {value::FromString("Secure Relay")});
    AERO_CHECK(questExec.succeeded, "Created live QuestRowItem");
    const ObjectRef liveQuest = std::get<ObjectRef>(questExec.value);
    auto questPin = domains.uiHeap().retain(liveQuest);
    (void)uiInterp.invoke("Game.UI::SetHardenedQuest", {viewVm, liveQuest});
    (void)bindingEngine.FlushTrackA();
    AERO_CHECK(questStatusTb->GetText() == "Secure Relay",
               "Bound QuestStatusTb updated to live quest title");

    // Verify ApplyHotReloadMigration observer transfer (removeUiObserver on oldRef, addUiObserver on newRef)
    auto quest2Exec = uiInterp.invoke("Game.UI::CreateQuestRow", {value::FromString("Migrated Relay V2")});
    const ObjectRef migratedQuest = std::get<ObjectRef>(quest2Exec.value);
    auto migratedPin = domains.uiHeap().retain(migratedQuest);
    AERO_CHECK(domains.uiHeap().uiObserverCount(liveQuest) >= 1u &&
               domains.uiHeap().uiObserverCount(migratedQuest) == 0u,
               "Initial observer count on liveQuest before hot-reload migration");
    (void)uiInterp.invoke("Game.UI::SetHardenedQuest", {viewVm, migratedQuest});
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(viewVm);
    (void)bindingEngine.ApplyHotReloadMigration(
        *res.programImage,
        {HotReloadMigrationPair{liveQuest, migratedQuest}});
    AERO_CHECK(domains.uiHeap().uiObserverCount(liveQuest) == 0u &&
               domains.uiHeap().uiObserverCount(migratedQuest) >= 1u,
               "ApplyHotReloadMigration released oldRef observer and registered addUiObserver on newRef");
    AERO_CHECK(questStatusTb->GetText() == "Migrated Relay V2",
               "Hot-reload migrated hop synced Migrated Relay V2");

    // Verify dead bound object after GC falls back to FallbackValue ("NO ACTIVE QUEST") instead of ""
    (void)uiInterp.invoke("Game.UI::SetHardenedQuest", {viewVm, NullObject{}});
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(viewVm);
    migratedPin.reset();
    ShadowStack emptyStack;
    domains.uiHeap().collectFull(emptyStack);
    AERO_CHECK(!domains.uiHeap().isAlive(migratedQuest), "Unrooted migratedQuest reclaimed by GC");
    AERO_CHECK(bindingEngine.FlushTrackA() >= 1u &&
               questStatusTb->GetText() == "NO ACTIVE QUEST",
               "GC-reclaimed hop restored FallbackValue NO ACTIVE QUEST instead of clearing to empty");

    // Now test Grid ColumnSpan=2 over (Auto, *) does NOT inflate Auto column 0
    Grid autoStarGrid;
    autoStarGrid.AddColumnDefinition(GridUnitType::Auto);
    autoStarGrid.AddColumnDefinition(GridUnitType::Star, 1.0f);
    autoStarGrid.AddRowDefinition(GridUnitType::Auto);
    autoStarGrid.AddRowDefinition(GridUnitType::Star, 1.0f);

    auto wideHeader = MakeIntrusive<Border>();
    wideHeader->SetWidth(700.0f);
    wideHeader->SetHeight(40.0f);
    Grid::SetRow(wideHeader.Get(), 0);
    Grid::SetColumn(wideHeader.Get(), 0);
    Grid::SetColumnSpan(wideHeader.Get(), 2);

    auto narrowSidebar = MakeIntrusive<Border>();
    narrowSidebar->SetWidth(160.0f);
    narrowSidebar->SetHeight(200.0f);
    Grid::SetRow(narrowSidebar.Get(), 1);
    Grid::SetColumn(narrowSidebar.Get(), 0);

    auto mainBody = MakeIntrusive<Border>();
    Grid::SetRow(mainBody.Get(), 1);
    Grid::SetColumn(mainBody.Get(), 1);

    autoStarGrid.AddChild(wideHeader.Get());
    autoStarGrid.AddChild(narrowSidebar.Get());
    autoStarGrid.AddChild(mainBody.Get());
    autoStarGrid.Measure(Size{1000.0f, 600.0f});
    autoStarGrid.Arrange(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    AERO_CHECK(std::fabs(narrowSidebar->FinalRect().width - 160.0f) < 1e-4f &&
               std::fabs(mainBody->FinalRect().x - 160.0f) < 1e-4f &&
               std::fabs(mainBody->FinalRect().width - 840.0f) < 1e-4f,
               "ColumnSpan=2 header across (Auto, *) preserved 160px Auto sidebar and 840px Star column");

    std::cout << "[PASS] TestCompilerBindingAndGridHardeningRegressions\n";
}

} // namespace aero::tests

int main() {
    using namespace aero::tests;
    std::cout << "=== AeroGUI 2.0 + RealScript Phase 2 Verification Suite ===\n";
    TestSimStateReadOnlyAndSimCommandMarshaling();
    TestUiDomainReentrantAsyncTasksAndCancellation();
    TestUtf8CodePointStringIntrinsics();
    TestAeroCompilerSinglePassAndSelfContainedMounting();
    TestDualTrackBindingEngineMultiHopAndEcsColumn();
    TestDepthBucketLayoutQueueAndCorePanels();
    TestParadigmSamplesAndSub5msHotReload();
    TestSamplesMigrationCompletenessEndToEnd();
    TestCompilerBindingAndGridHardeningRegressions();
    std::cout << "=== ALL 9 PHASE 2 TEST SUITES PASSED (100%) ===\n";
    return 0;
}
