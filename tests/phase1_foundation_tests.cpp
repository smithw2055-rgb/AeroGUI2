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

#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/Compilation.h"
#include "realscript/game/GameScripting.h"

namespace {

// Global allocation tracker for verifying 0-heap-allocation hot paths
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

std::shared_ptr<const ProgramImage> CompileAndLinkProgram(
    std::vector<rs_comp::SourceFile> files) {
    rs_comp::Compilation compilation(std::move(files));
    const auto build = compilation.build();
    if (build.diagnostics.hasErrors()) {
        for (const auto& d : build.diagnostics.items()) {
            std::cerr << "Compile diagnostic: " << d.code << ": " << d.message << "\n";
        }
    }
    AERO_CHECK(!build.diagnostics.hasErrors(), "Source compilation succeeded without errors");

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
        if (bc.name == "Aero.UI") {
            bc.functions.clear();
        }
        modules.push_back(std::move(bc));
    }

    RuntimeError linkErr{};
    auto image = ProgramImage::link(std::move(modules), linkErr);
    if (!image) {
        std::cerr << "Link error: " << linkErr.message << "\n";
    }
    AERO_CHECK(image.has_value(), "ProgramImage linked");
    return std::make_shared<const ProgramImage>(std::move(*image));
}

// ============================================================================
// Test 1: 24B UnifiedValue & Float4Value UI Primitives (P0-2)
// ============================================================================
void TestUnifiedValueAndFloat4() {
    static_assert(sizeof(UnifiedValue) == 24, "UnifiedValue must be 24 bytes");
    static_assert(sizeof(Float4Value) == 16, "Float4Value must be 16 bytes");

    const Color c{0.25f, 0.5f, 0.75f, 1.0f};
    const Rect r{10.0f, 20.0f, 300.0f, 180.0f};
    const Thickness t{4.0f, 8.0f, 12.0f, 16.0f};
    const CornerRadius cr{6.0f, 6.0f, 6.0f, 6.0f};

    UnifiedValue vc;
    UnifiedValue vr;
    UnifiedValue vt;
    UnifiedValue vcr;
    {
        AllocationTrackingScope noAlloc;
        vc = value::FromColor(c);
        vr = value::FromRect(r);
        vt = value::FromThickness(t);
        vcr = value::FromCornerRadius(cr);

        AERO_CHECK(value::TryGetColor(vc) == c, "Color round-trip mismatch");
        AERO_CHECK(value::TryGetRect(vr) == r, "Rect round-trip mismatch");
        AERO_CHECK(value::TryGetThickness(vt) == t, "Thickness round-trip mismatch");
        AERO_CHECK(value::TryGetCornerRadius(vcr) == cr, "CornerRadius round-trip mismatch");
        AERO_CHECK(
            noAlloc.count() == 0,
            "Float4Value / UnifiedValue conversions must perform 0 heap allocations");
    }

    AERO_CHECK(
        rs_rt::valueType(vc) == PrimitiveType::Struct,
        "Float4Value should report PrimitiveType::Struct");
    AERO_CHECK(
        rs_rt::valueToString(vc).find("<Float4:") == 0,
        "Float4Value valueToString formatting");

    std::cout << "[PASS] TestUnifiedValueAndFloat4\n";
}

// ============================================================================
// Test 2: Static TypeClass, 64-Bit SymbolId, and O(1) Euler-Tour RTTI (P0-3)
// ============================================================================
void TestTypeClassEulerTourRtti() {
    EnsureBuiltinTypesRegistered();

    const auto* uiElemType = UIElement::StaticTypeClass();
    const auto* panelType = Panel::StaticTypeClass();
    const auto* windowType = Window::StaticTypeClass();
    const auto* buttonType = Button::StaticTypeClass();

    AERO_CHECK(uiElemType != nullptr && panelType != nullptr, "Builtin types non-null");
    AERO_CHECK(
        uiElemType->rsSymbolId == rs_sem::stableTypeId("Aero.UI.UIElement"),
        "64-bit FNV-1a SymbolId matches RealScript stableTypeId");
    AERO_CHECK(
        windowType->rsSymbolId == rs_sem::stableTypeId("Aero.UI.Window"),
        "Window 64-bit SymbolId matches");

    // Euler-tour interval O(1) hierarchy checks
    AERO_CHECK(windowType->IsDerivedFrom(windowType), "Window IsDerivedFrom Window");
    AERO_CHECK(windowType->IsDerivedFrom(panelType), "Window IsDerivedFrom Panel");
    AERO_CHECK(windowType->IsDerivedFrom(uiElemType), "Window IsDerivedFrom UIElement");
    AERO_CHECK(buttonType->IsDerivedFrom(uiElemType), "Button IsDerivedFrom UIElement");
    AERO_CHECK(!buttonType->IsDerivedFrom(panelType), "Button is NOT derived from Panel");
    AERO_CHECK(!panelType->IsDerivedFrom(windowType), "Panel is NOT derived from Window");
    AERO_CHECK(!uiElemType->IsDerivedFrom(buttonType), "UIElement is NOT derived from Button");

    auto win = MakeIntrusive<Window>();
    UIElement* basePtr = win.Get();
    AERO_CHECK(basePtr->DynamicCast<Window>() == win.Get(), "DynamicCast to Window");
    AERO_CHECK(basePtr->DynamicCast<Panel>() == win.Get(), "DynamicCast to Panel");
    AERO_CHECK(basePtr->DynamicCast<Button>() == nullptr, "DynamicCast to Button is null");

    std::cout << "[PASS] TestTypeClassEulerTourRtti\n";
}

// ============================================================================
// Test 3: Intrusive UIElement Visual Tree, 8 Fast Inline Properties & 32B Entry (P1-4)
// ============================================================================
void TestUIElementVisualTreeAndProperties() {
    static_assert(
        sizeof(CompactPropertyEntry) == 32,
        "CompactPropertyEntry must be 32 bytes");
    static_assert(
        alignof(CompactPropertyEntry) == 8,
        "CompactPropertyEntry must be 8-byte aligned");

    auto window = MakeIntrusive<Window>();
    auto panel = MakeIntrusive<Panel>();
    auto btn1 = MakeIntrusive<Button>();
    auto btn2 = MakeIntrusive<Button>();

    window->SetAsRootVisual(true);
    AERO_CHECK(window->IsConnectedToRoot(), "Window is root visual");

    AERO_CHECK(window->AddChild(panel.Get()), "Add panel to window");
    AERO_CHECK(panel->AddChild(btn1.Get()), "Add btn1 to panel");
    AERO_CHECK(panel->AddChild(btn2.Get()), "Add btn2 to panel");
    AERO_CHECK(!btn1->AddChild(window.Get()), "Cycle detection prevents adding ancestor as child");

    AERO_CHECK(panel->ChildCount() == 2, "Panel has 2 children");
    AERO_CHECK(btn1->IsConnectedToRoot() && btn2->IsConnectedToRoot(), "Children connected to root");
    AERO_CHECK(btn1->NextSibling() == btn2.Get(), "Intrusive sibling link forward");
    AERO_CHECK(btn2->PrevSibling() == btn1.Get(), "Intrusive sibling link backward");

    // 8 Fast Inline Properties must NOT allocate in sparseProps_
    btn1->ClearLayoutDirtyFlags();
    {
        AllocationTrackingScope noAlloc;
        btn1->SetValue(PropId_Width, MakeFloat(160.0f));
        btn1->SetValue(PropId_Height, MakeFloat(44.0f));
        btn1->SetValue(PropId_MinWidth, MakeFloat(80.0f));
        btn1->SetValue(PropId_MinHeight, MakeFloat(24.0f));
        btn1->SetValue(PropId_Margin, value::FromThickness(Thickness{4, 6, 4, 6}));
        btn1->SetValue(PropId_Padding, value::FromThickness(Thickness{8, 4, 8, 4}));
        btn1->SetValue(PropId_Opacity, MakeFloat(0.85f));
        btn1->SetValue(
            PropId_Visibility,
            value::FromInt(static_cast<std::int64_t>(Visibility::Hidden)));

        AERO_CHECK(noAlloc.count() == 0, "Fast inline properties must perform 0 heap allocations");
    }
    AERO_CHECK(btn1->SparsePropertyCount() == 0, "Fast inline properties do not touch sparse vector");
    AERO_CHECK(std::fabs(btn1->GetWidth() - 160.0f) < 1e-5f, "Fast inline Width");
    AERO_CHECK(std::fabs(btn1->GetHeight() - 44.0f) < 1e-5f, "Fast inline Height");
    AERO_CHECK(btn1->GetMargin() == Thickness(4, 6, 4, 6), "Fast inline Margin");
    AERO_CHECK(btn1->GetPadding() == Thickness(8, 4, 8, 4), "Fast inline Padding");
    AERO_CHECK(std::fabs(btn1->GetOpacity() - 0.85f) < 1e-5f, "Fast inline Opacity");
    AERO_CHECK(btn1->GetVisibility() == Visibility::Hidden, "Fast inline Visibility");
    AERO_CHECK(
        (btn1->GetLayoutDirtyFlags() & LayoutAllDirty) == LayoutAllDirty,
        "Layout dirty flags updated by fast inline properties");

    // Sparse 32B CompactPropertyEntry & ValueSourceRank priority checks
    const Color localBg{0.2f, 0.3f, 0.8f, 1.0f};
    const Color styleBg{0.5f, 0.5f, 0.5f, 1.0f};
    const Color animBg{1.0f, 0.0f, 0.0f, 1.0f};

    AERO_CHECK(
        btn1->SetValue(PropId_Background, value::FromColor(localBg), ValueSourceRank::Local),
        "Set Local Background");
    AERO_CHECK(
        !btn1->SetValue(
            PropId_Background,
            value::FromColor(styleBg),
            ValueSourceRank::StyleTrigger),
        "Lower rank StyleTrigger must not overwrite Local value");
    AERO_CHECK(
        value::TryGetColor(btn1->GetValue(PropId_Background)) == localBg,
        "Effective Background remains Local");

    AERO_CHECK(
        btn1->SetValue(
            PropId_Background,
            value::FromColor(animBg),
            ValueSourceRank::Animation),
        "Higher rank Animation overwrites Local value");
    AERO_CHECK(
        value::TryGetColor(btn1->GetValue(PropId_Background)) == animBg,
        "Effective Background updated to Animation");

    // Verify explicit 4-byte padding utilization (bindingSlotId & animationTrackId)
    AERO_CHECK(
        btn1->SetPropertyMetadata(PropId_Background, 7u, 19u),
        "SetPropertyMetadata on 32B entry");
    const auto* entry = btn1->FindCompactEntry(PropId_Background);
    AERO_CHECK(entry != nullptr, "FindCompactEntry returns entry");
    AERO_CHECK(entry->bindingSlotId == 7u, "bindingSlotId stored in 32B entry");
    AERO_CHECK(entry->animationTrackId == 19u, "animationTrackId stored in 32B entry");

    // Verify ClearValue on both fast inline properties and sparse properties
    AERO_CHECK(
        btn1->ClearValue(PropId_Width, ValueSourceRank::Local),
        "ClearValue on fast inline PropId_Width");
    AERO_CHECK(
        std::fabs(btn1->GetWidth() - (-1.0f)) < 1e-5f,
        "ClearValue resets Width to -1.0f (Auto)");
    AERO_CHECK(
        btn1->ClearValue(PropId_Opacity, ValueSourceRank::Local),
        "ClearValue on fast inline PropId_Opacity");
    AERO_CHECK(
        std::fabs(btn1->GetOpacity() - 1.0f) < 1e-5f,
        "ClearValue resets Opacity to 1.0f");
    AERO_CHECK(
        btn1->ClearValue(PropId_Visibility, ValueSourceRank::Local),
        "ClearValue on fast inline PropId_Visibility");
    AERO_CHECK(
        btn1->GetVisibility() == Visibility::Visible,
        "ClearValue resets Visibility to Visible");
    AERO_CHECK(
        !btn1->ClearValue(PropId_Background, ValueSourceRank::Local),
        "ClearValue with rank Local cannot clear Animation-ranked sparse entry");
    AERO_CHECK(
        btn1->ClearValue(PropId_Background, ValueSourceRank::Animation),
        "ClearValue with rank Animation clears Animation-ranked sparse entry");
    AERO_CHECK(
        btn1->SparsePropertyCount() == 0,
        "Sparse property removed after ClearValue");

    std::cout << "[PASS] TestUIElementVisualTreeAndProperties\n";
}

// ============================================================================
// Test 4: Zero-Hash NativeHandle Bridge, Generational Invalidation & 0-Alloc Fast Path
// ============================================================================
void TestNativeHandleZeroHashAndGenerationalInvalidation() {
    NativeHandleRegistry registry;
    auto btn = MakeIntrusive<Button>();
    btn->SetWidth(220.0f);

    // First ToNativeHandle registers borrowed pointer slot
    const auto handle = btn->ToNativeHandle(registry);
    AERO_CHECK(handle.valid(), "ToNativeHandle returns valid handle");
    AERO_CHECK(
        handle.typeId == Button::StaticTypeClass()->rsSymbolId,
        "NativeHandle carries 64-bit Button SymbolId");

    // Hot loop: 10,000 cross-boundary NativeHandle resolutions & fast property reads/writes
    {
        AllocationTrackingScope noAlloc;
        for (int i = 0; i < 10000; ++i) {
            const auto cachedHandle = btn->ToNativeHandle(registry);
            auto* resolved = UIElement::FromNativeHandle(registry, cachedHandle);
            AERO_CHECK(resolved == btn.Get(), "Zero-hash O(1) FromNativeHandle resolution");
            resolved->SetValue(PropId_Width, MakeFloat(220.0f + static_cast<float>(i & 3)));
            const auto v = resolved->GetValue(PropId_Width);
            AERO_CHECK(std::holds_alternative<FloatValue>(v), "Fast inline width returns FloatValue");
        }
        AERO_CHECK(
            noAlloc.count() == 0,
            "10,000 C++ <-> NativeHandle fast-path iterations must perform 0 heap allocations");
    }

    // Generational invalidation when C++ UIElement is destroyed
    btn.Reset();
    AERO_CHECK(!registry.isAlive(handle), "Registry reports handle dead after ~UIElement()");
    AERO_CHECK(
        UIElement::FromNativeHandle(registry, handle) == nullptr,
        "FromNativeHandle returns nullptr after ~UIElement()");

    // Allocate a new Button that reuses the freed slot with incremented generation
    auto btn2 = MakeIntrusive<Button>();
    const auto handle2 = btn2->ToNativeHandle(registry);
    AERO_CHECK(handle2.slot == handle.slot, "Freelist reuses slot index");
    AERO_CHECK(handle2.generation > handle.generation, "Generation incremented on slot reuse");
    AERO_CHECK(
        UIElement::FromNativeHandle(registry, handle) == nullptr,
        "Stale handle with old generation still resolves to nullptr");
    AERO_CHECK(
        UIElement::FromNativeHandle(registry, handle2) == btn2.Get(),
        "New generation handle resolves to btn2");

    std::cout << "[PASS] TestNativeHandleZeroHashAndGenerationalInvalidation\n";
}

// ============================================================================
// Test 5: Visual Tree Derived Control Wrapper GC Root Keep-Alive in UiHeap (P1-4)
// ============================================================================
void TestVisualTreeDerivedWrapperGcKeepAlive() {
    ExecutionDomainContext domains;
    auto& registry = domains.nativeHandles();
    auto& uiHeap = domains.uiHeap();

    auto window = MakeIntrusive<Window>();
    window->SetAsRootVisual(true, &registry, &uiHeap);

    auto button = MakeIntrusive<Button>();
    window->AddChild(button.Get(), &registry, &uiHeap);

    // Allocate a script wrapper object on UiHeap representing a derived script control
    // (e.g., class MyCustomButton : Button) and bind it to `button` without holding
    // any external PersistentRoot in test code.
    ObjectRef wrapperRef{};
    {
        const auto nativeHandle = button->ToNativeHandle(registry);
        auto allocated = uiHeap.allocateObject(
            rs_sem::stableTypeId("App.UI::MyCustomButton"),
            {nativeHandle, std::int64_t{42}},
            {});
        AERO_CHECK(allocated.has_value(), "Allocated script wrapper on UiHeap");
        wrapperRef = *allocated;
        AERO_CHECK(
            button->BindScriptWrapper(registry, uiHeap, wrapperRef),
            "BindScriptWrapper succeeded");
    }

    AERO_CHECK(
        button->HasPinnedScriptWrapper(),
        "Mounted visual-tree element pins its script wrapper in UiHeap");

    // Run full GC on UiHeap: because `button` is mounted on the active root visual tree,
    // its script wrapper MUST survive GC even though no script stack/local root holds it!
    ShadowStack emptyStack;
    uiHeap.collectFull(emptyStack);
    AERO_CHECK(
        uiHeap.isAlive(wrapperRef),
        "Mounted visual-tree control's script wrapper must survive UiHeap full GC");

    // Now detach `button` from the visual tree and run full GC again:
    // the pin must be automatically released and the wrapper collected.
    AERO_CHECK(window->RemoveChild(button.Get(), &registry, &uiHeap), "Removed button from Window");
    AERO_CHECK(
        !button->HasPinnedScriptWrapper(),
        "Detached element releases its visual-tree GC root pin");

    uiHeap.collectFull(emptyStack);
    AERO_CHECK(
        !uiHeap.isAlive(wrapperRef),
        "Detached control's unreferenced script wrapper is collected by UiHeap full GC");

    std::cout << "[PASS] TestVisualTreeDerivedWrapperGcKeepAlive\n";
}

// ============================================================================
// Test 6: ExecutionDomainContext Dual-Heap Isolation & Cross-Heap Read-Only (P0-1 & P0-5)
// ============================================================================
void TestExecutionDomainDualHeapAndCrossHeapReadOnly() {
    ExecutionDomainContext domains;
    auto& simHeap = domains.simHeap();
    auto& uiHeap = domains.uiHeap();

    // P0-1: Verify dynamic heapIds are distinct and non-zero
    AERO_CHECK(simHeap.heapId() != 0 && uiHeap.heapId() != 0, "Non-zero dynamic heapIds");
    AERO_CHECK(simHeap.heapId() != uiHeap.heapId(), "SimHeap and UiHeap have distinct heapIds");

    // Allocate a script string on SimHeap (produces ObjectRef(ObjectKind::String))
    const auto simStrOpt = simHeap.allocateString("CommanderShepard");
    AERO_CHECK(simStrOpt.has_value(), "Allocated string on SimHeap");
    const auto simStrRef = *simStrOpt;
    AERO_CHECK(
        simStrRef.heapId == static_cast<std::uint32_t>(simHeap.heapId()),
        "String allocated on SimHeap");

    // P0-5: Allocate a small object (<= 4 fields -> uses HeapObject::inlineValues) on SimHeap
    auto simSmallObjOpt = simHeap.allocateObject(
        rs_sem::stableTypeId("Game.Sim::PlayerState"),
        {std::int64_t{95}, simStrRef, Color{0.1f, 0.8f, 0.2f, 1.0f}.ToFloat4()},
        {1u});
    AERO_CHECK(simSmallObjOpt.has_value(), "Allocated small SimHeap object");
    const auto simSmallObj = *simSmallObjOpt;
    AERO_CHECK(
        simHeap.isInlineRecordObject(simSmallObj),
        "3-field object uses HeapObject::inlineValues storage");
    auto simRoot = simHeap.retain(simSmallObj);

    // 1. Cross-heap read from UI domain into SimHeap small object (inlineValues)
    UnifiedValue hpVal{};
    RuntimeError err{};
    AERO_CHECK(
        domains.readObjectField(DomainKind::Ui, simSmallObj, 0, hpVal, &err),
        "UI domain can read SimHeap inline field 0");
    AERO_CHECK(
        std::holds_alternative<std::int64_t>(hpVal) && std::get<std::int64_t>(hpVal) == 95,
        "Cross-heap inline int field matches 95");

    // 2. Cross-heap read of ObjectKind::String from SimHeap into UI domain:
    // must automatically convert to self-contained StringValue!
    UnifiedValue nameVal{};
    AERO_CHECK(
        domains.readObjectField(DomainKind::Ui, simSmallObj, 1, nameVal, &err),
        "UI domain can read SimHeap string field 1");
    AERO_CHECK(
        std::holds_alternative<StringValue>(nameVal) &&
            std::get<StringValue>(nameVal).str() == "CommanderShepard",
        "Cross-heap ObjectKind::String automatically converted to StringValue");

    // 3. Cross-Heap Read-Only enforcement: UI domain attempting to mutate SimHeap object
    // MUST fail with ErrorCode::CapabilityDenied!
    err = {};
    const bool illegalWrite = domains.writeObjectField(
        DomainKind::Ui,
        simSmallObj,
        0,
        std::int64_t{999},
        &err);
    AERO_CHECK(!illegalWrite, "UI domain write to SimHeap must be rejected");
    AERO_CHECK(
        err.code == ErrorCode::CapabilityDenied,
        "Cross-heap write violation returns ErrorCode::CapabilityDenied");

    // Confirm SimHeap field was untouched
    hpVal = {};
    (void)domains.readObjectField(DomainKind::Ui, simSmallObj, 0, hpVal, &err);
    AERO_CHECK(std::get<std::int64_t>(hpVal) == 95, "SimHeap field remains 95 after blocked write");

    // 4. Legal mutation via SimCommandEnvelope -> Sim domain tick
    domains.enqueueSimCommand(SimCommandEnvelope{
        rs_sem::stableTypeId("Game.Sim::ApplyHeal"),
        simSmallObj,
        {std::int64_t{100}},
        1ULL});
    AERO_CHECK(domains.pendingSimCommandCount() == 1, "1 pending SimCommandEnvelope");

    auto cmds = domains.consumeSimCommands();
    AERO_CHECK(cmds.size() == 1 && domains.pendingSimCommandCount() == 0, "Consumed command queue");
    err = {};
    AERO_CHECK(
        domains.writeObjectField(
            DomainKind::Sim,
            cmds[0].targetObject,
            0,
            cmds[0].payload[0],
            &err),
        "Sim domain write to SimHeap succeeds");

    hpVal = {};
    (void)domains.readObjectField(DomainKind::Ui, simSmallObj, 0, hpVal, &err);
    AERO_CHECK(std::get<std::int64_t>(hpVal) == 100, "UI domain observes updated SimHeap value 100");

    // 5. Cross-Heap Reference Isolation: even when callerDomain == DomainKind::Sim,
    // writing a UiHeap ObjectRef into a SimHeap object MUST be rejected!
    auto uiObjOpt = uiHeap.allocateObject(
        rs_sem::stableTypeId("Aero.UI::WidgetState"),
        {std::int64_t{77}},
        {});
    AERO_CHECK(uiObjOpt.has_value(), "Allocated UiHeap object");
    auto uiObjRoot = uiHeap.retain(*uiObjOpt);
    err = {};
    const bool crossHeapRefWrite = domains.writeObjectField(
        DomainKind::Sim,
        simSmallObj,
        0,
        *uiObjOpt,
        &err);
    AERO_CHECK(
        !crossHeapRefWrite && err.code == ErrorCode::CapabilityDenied,
        "Writing a UiHeap ObjectRef into a SimHeap object is blocked with CapabilityDenied");

    // 6. Incremental idle GC on UiHeap
    const auto steps = domains.stepUiIdleGc(0.25);
    (void)steps;

    std::cout << "[PASS] TestExecutionDomainDualHeapAndCrossHeapReadOnly\n";
}

// ============================================================================
// Test 7: partial class AST Merging Across Source Units in a Module (P1-2)
// ============================================================================
void TestPartialClassMergingAndDiagnostics() {
    // 1. Compile a multi-file module where HudWindow is split across .generated.rs and .logic.rs
    auto program = CompileAndLinkProgram({
        {"HudWindow.generated.rs",
         R"(
module Aero.Hud;

public partial class HudWindow
{
    public int baseWidth;
    public string headerTitle;
}
)"},
        {"HudWindow.logic.rs",
         R"(
module Aero.Hud;

public partial class HudWindow
{
    public int clickCounter;

    [Observable]
    public int AlertLevel { get; set; }

    public HudWindow()
    {
        baseWidth = 400;
        headerTitle = "Tactical HUD";
        clickCounter = 2;
        AlertLevel = 5;
    }

    public int ComputeTotalScore(int bonus)
    {
        clickCounter = clickCounter + 1;
        AlertLevel = AlertLevel + bonus;
        return baseWidth + clickCounter + AlertLevel;
    }
}

public int RunPartialTest()
{
    HudWindow win = new HudWindow();
    return win.ComputeTotalScore(10);
}
)"}});

    Interpreter interpreter(program);
    auto exec = interpreter.invoke("Aero.Hud::RunPartialTest");
    AERO_CHECK(exec.succeeded, "RunPartialTest execution succeeded");
    // 400 + (2 + 1) + (5 + 10) = 418
    AERO_CHECK(
        std::holds_alternative<std::int64_t>(exec.value) &&
            std::get<std::int64_t>(exec.value) == 418,
        "partial class merged fields, [Observable] prop, and methods evaluated to 418");

    // 2. Multi-level partial class inheritance with [Observable] & custom attribute arguments across files
    rs_comp::Compilation multiPartialComp({
        {"01_BaseView.a.rs",
         R"(
module Aero.HudMulti;

public partial class BaseView
{
    [Observable]
    public int BaseCounter;
}
)"},
        {"02_BaseView.b.rs",
         R"(
module Aero.HudMulti;

public partial class BaseView
{
    [Observable]
    public int BaseScore { get; set; }
}
)"},
        {"03_DerivedView.a.rs",
         R"(
module Aero.HudMulti;

public partial class DerivedView : BaseView
{
    [CustomMeta("DerivedFieldTagValue")]
    [Observable]
    public int DerivedExtra;
}
)"},
        {"04_DerivedView.b.rs",
         R"(
module Aero.HudMulti;

public partial class DerivedView
{
    [CustomMeta("DerivedPropTagValue")]
    [Observable]
    public int DerivedBonus { get; set; }
}
)"}});
    const auto multiBuild = multiPartialComp.build();
    AERO_CHECK(
        !multiBuild.diagnostics.hasErrors(),
        "Multi-level partial class inheritance with attributes compiled cleanly");
    bool sawFieldTag = false;
    bool sawPropTag = false;
    for (const auto& attr : multiBuild.nativeAttributes) {
        if (attr.name == "CustomMeta" && !attr.arguments.empty()) {
            if (attr.arguments[0].value == "\"DerivedFieldTagValue\"") sawFieldTag = true;
            if (attr.arguments[0].value == "\"DerivedPropTagValue\"") sawPropTag = true;
        }
    }
    AERO_CHECK(
        sawFieldTag && sawPropTag,
        "Attribute argument values on merged partial class members preserved across files without span corruption");

    // 3. Negative check: duplicate non-partial classes in the same module MUST still fail with RS4004
    rs_comp::Compilation badComp({
        {"DupA.rs", "module Aero.Bad;\nclass DupControl { int a; }\n"},
        {"DupB.rs", "module Aero.Bad;\nclass DupControl { int b; }\n"}});
    const auto badBuild = badComp.build();
    AERO_CHECK(badBuild.diagnostics.hasErrors(), "Non-partial duplicate class must fail compilation");
    bool sawRS4004 = false;
    for (const auto& d : badBuild.diagnostics.items()) {
        if (d.code == "RS4004") {
            sawRS4004 = true;
            break;
        }
    }
    AERO_CHECK(sawRS4004, "Non-partial duplicate class reports diagnostic RS4004");

    std::cout << "[PASS] TestPartialClassMergingAndDiagnostics\n";
}

// ============================================================================
// Test 8: [Observable] 64-Bit dirtyFieldMask, >64 Field Overflow Protection & ScriptUiBridge (P1-1 & P1-3)
// ============================================================================
void TestObservableDirtyMaskAndReactiveUiBridge() {
    // Generate a ViewModel class with 68 non-observable fields (pad0 .. pad67) BEFORE three
    // [Observable] properties (HealthWidth, ShieldOpacity, SquadTitle).
    // Without P1-1's dedicated observableBitIndex (0..63), HealthWidth and ShieldOpacity
    // would have fieldIndex >= 68 (>= 64), causing 1ULL << fieldIndex UB!
    std::string source = "module Game.UI;\n\npublic class HugeSquadViewModel\n{\n";
    for (int i = 0; i < 68; ++i) {
        source += "    public int pad" + std::to_string(i) + ";\n";
    }
    source += R"(
    [Observable]
    public float HealthWidth { get; set; }

    [Observable]
    public float ShieldOpacity { get; set; }

    [Observable]
    public string SquadTitle { get; set; }

    public HugeSquadViewModel()
    {
        HealthWidth = 120.0f;
        ShieldOpacity = 0.5f;
        SquadTitle = "Alpha Squad";
    }

    public void ApplyDamage(float newWidth, float newOpacity)
    {
        HealthWidth = newWidth;
        ShieldOpacity = newOpacity;
    }
}

public HugeSquadViewModel CreateViewModel()
{
    return new HugeSquadViewModel();
}

public void MutateViewModel(HugeSquadViewModel vm, float newWidth, float newOpacity)
{
    vm.ApplyDamage(newWidth, newOpacity);
}
)";

    auto program = CompileAndLinkProgram({{"HugeSquadViewModel.rs", source}});

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*program);

    // Instantiate HugeSquadViewModel on SimHeap
    Interpreter simInterpreter(program, domains.simHeapPtr());
    auto createRes = simInterpreter.invoke("Game.UI::CreateViewModel");
    AERO_CHECK(createRes.succeeded, "Created HugeSquadViewModel on SimHeap");
    const auto vmRef = std::get<ObjectRef>(createRes.value);
    auto vmRoot = domains.simHeap().retain(vmRef);

    const auto vmTypeId = rs_sem::stableTypeId("Game.UI::HugeSquadViewModel");

    // P1-3: Resolve O(1) PropertySlotHandles for HealthWidth, ShieldOpacity, SquadTitle
    const auto widthSlotOpt = domains.resolvePropertySlot(*program, vmTypeId, "HealthWidth");
    const auto opacitySlotOpt = domains.resolvePropertySlot(*program, vmTypeId, "ShieldOpacity");
    const auto titleSlotOpt = domains.resolvePropertySlot(*program, vmTypeId, "SquadTitle");

    AERO_CHECK(
        widthSlotOpt.has_value() && opacitySlotOpt.has_value() && titleSlotOpt.has_value(),
        "Resolved all three PropertySlotHandles");
    const auto widthSlot = *widthSlotOpt;
    const auto opacitySlot = *opacitySlotOpt;
    const auto titleSlot = *titleSlotOpt;

    AERO_CHECK(widthSlot.valid() && widthSlot.isAutoBacked(), "HealthWidth slot is auto-backed");
    AERO_CHECK(widthSlot.isObservable, "HealthWidth slot is marked [Observable]");
    AERO_CHECK(
        widthSlot.backingFieldIndex >= 68u,
        "HealthWidth backingFieldIndex is >= 68 (past 64-field boundary)");
    AERO_CHECK(
        widthSlot.observableBitIndex == 0u,
        "HealthWidth assigned independent observableBitIndex 0 (< 64)");
    AERO_CHECK(
        opacitySlot.observableBitIndex == 1u,
        "ShieldOpacity assigned independent observableBitIndex 1 (< 64)");
    AERO_CHECK(
        titleSlot.observableBitIndex == 2u,
        "SquadTitle assigned independent observableBitIndex 2 (< 64)");

    // Clear initial construction dirty state before binding
    domains.simHeap().clearDirtyFieldMask(vmRef);
    (void)domains.simHeap().consumeDirtyObjects();

    // Bind HugeSquadViewModel properties on SimHeap to an AeroGUI Button on the UI side
    auto healthBarBtn = MakeIntrusive<Button>();
    ScriptUiBridge bridge(domains);
    RuntimeError err{};

    AERO_CHECK(
        bridge.BindObservableProperty(vmRef, widthSlot, healthBarBtn, PropId_Width, &err),
        "Bound HealthWidth -> Button.Width");
    AERO_CHECK(
        bridge.BindObservableProperty(vmRef, opacitySlot, healthBarBtn, PropId_Opacity, &err),
        "Bound ShieldOpacity -> Button.Opacity");
    AERO_CHECK(
        bridge.BindObservableProperty(vmRef, titleSlot, healthBarBtn, PropId_Title, &err),
        "Bound SquadTitle -> Button.Title");

    // Verify initial synchronous evaluation populated Button properties
    AERO_CHECK(std::fabs(healthBarBtn->GetWidth() - 120.0f) < 1e-5f, "Initial Button.Width == 120");
    AERO_CHECK(
        std::fabs(healthBarBtn->GetOpacity() - 0.5f) < 1e-5f,
        "Initial Button.Opacity == 0.5");
    AERO_CHECK(
        std::get<StringValue>(healthBarBtn->GetValue(PropId_Title)).str() == "Alpha Squad",
        "Initial Button.Title == 'Alpha Squad'");

    // Equality short-circuit test: writing the exact same value (120.0f) must NOT set dirty bit
    // or enqueue vmRef into dirtyObjects!
    AERO_CHECK(
        domains.writePropertySlotFast(DomainKind::Sim, vmRef, widthSlot, MakeFloat(120.0f), &err),
        "Wrote identical value 120.0f to HealthWidth");
    AERO_CHECK(
        domains.simHeap().dirtyFieldMask(vmRef) == 0ULL,
        "Equality short-circuit keeps dirtyFieldMask == 0");
    AERO_CHECK(
        bridge.FlushReactiveBindings(&err) == 0u,
        "0 reactive bindings flushed when value did not change");

    // Mutate HealthWidth and ShieldOpacity from RealScript method MutateViewModel(vm, 280.0f, 0.9f)
    auto dmgRes = simInterpreter.invoke(
        "Game.UI::MutateViewModel",
        {vmRef, MakeFloat(280.0f), MakeFloat(0.9f)});
    AERO_CHECK(dmgRes.succeeded, "Invoke MutateViewModel(vm, 280.0f, 0.9f) on SimHeap ViewModel");

    // Verify bits 0 and 1 are set in dirtyFieldMask
    const std::uint64_t mask = domains.simHeap().dirtyFieldMask(vmRef);
    AERO_CHECK(
        mask == ((1ULL << 0u) | (1ULL << 1u)),
        "dirtyFieldMask has exact bits 0 and 1 set after script property mutation");

    // Flush reactive bindings into UIElement
    const std::size_t updated = bridge.FlushReactiveBindings(&err);
    AERO_CHECK(updated == 2u, "Exactly 2 bound UI properties updated from dirtyFieldMask");
    AERO_CHECK(std::fabs(healthBarBtn->GetWidth() - 280.0f) < 1e-5f, "Button.Width updated to 280");
    AERO_CHECK(
        std::fabs(healthBarBtn->GetOpacity() - 0.9f) < 1e-5f,
        "Button.Opacity updated to 0.9");
    AERO_CHECK(
        domains.simHeap().dirtyFieldMask(vmRef) == 0ULL,
        "dirtyFieldMask cleared after FlushReactiveBindings");

    // Verify .rsbc binary serialization/deserialization round-trip preserves [Observable] bit indices
    const auto encodedBytes = rs_bc::encodeModule(program->modules().front());
    rs_bc::Module decodedMod;
    rs_diag::DiagnosticBag decodeDiags;
    AERO_CHECK(
        rs_bc::decodeModule(encodedBytes, decodedMod, decodeDiags) && !decodeDiags.hasErrors(),
        "decodeModule succeeded on [Observable] bytecode module");
    RuntimeError linkErr{};
    auto roundTripPkg = ProgramImage::link({std::move(decodedMod)}, linkErr);
    AERO_CHECK(roundTripPkg.has_value(), "Linked decoded .rsbc module");
    const auto decodedWidthSlot = domains.resolvePropertySlot(*roundTripPkg, vmTypeId, "HealthWidth");
    const auto decodedOpacitySlot = domains.resolvePropertySlot(*roundTripPkg, vmTypeId, "ShieldOpacity");
    const auto decodedTitleSlot = domains.resolvePropertySlot(*roundTripPkg, vmTypeId, "SquadTitle");
    AERO_CHECK(
        decodedWidthSlot && decodedWidthSlot->isObservable && decodedWidthSlot->observableBitIndex == 0u &&
            decodedOpacitySlot && decodedOpacitySlot->isObservable && decodedOpacitySlot->observableBitIndex == 1u &&
            decodedTitleSlot && decodedTitleSlot->isObservable && decodedTitleSlot->observableBitIndex == 2u,
        ".rsbc round-trip preserves [Observable] flags and exact 0..63 observableBitIndex values");

    std::cout << "[PASS] TestObservableDirtyMaskAndReactiveUiBridge\n";
}

// ============================================================================
// Test 9: Generational WeakRef & WeakDelegate Multicast Auto-Pruning (P0-4)
// ============================================================================
void TestWeakRefAndWeakDelegateMulticastPruning() {
    auto program = CompileAndLinkProgram({
        {"WeakDelegateTest.rs",
         R"(
module Aero.Events;

public delegate int ClickCallback(int delta);

public class ButtonController
{
    public int weight;

    public ButtonController(int w)
    {
        weight = w;
    }

    public int OnClick(int delta)
    {
        return weight + delta;
    }
}

public ClickCallback MakeStrongDelegate(ButtonController ctrl)
{
    return ctrl.OnClick;
}

public ClickCallback CombineCallbacks(ClickCallback a, ClickCallback b)
{
    return a + b;
}

public int FireCallback(ClickCallback cb, int arg)
{
    return cb(arg);
}
)"}});

    auto heap = std::make_shared<ManagedHeap>();
    Interpreter interpreter(program, heap);

    const auto ctrlTypeId = rs_sem::stableTypeId("Aero.Events::ButtonController");
    const auto* ctrlType = program->resolveType(ctrlTypeId);
    AERO_CHECK(ctrlType != nullptr, "Resolved ButtonController TypeSymbol");
    SymbolId onClickSymbolId = 0;
    for (const auto& m : ctrlType->methods) {
        if (m.name == "OnClick") {
            onClickSymbolId = m.id;
            break;
        }
    }
    AERO_CHECK(onClickSymbolId != 0, "Resolved ButtonController.OnClick SymbolId");
    const auto delegateTypeId = rs_sem::stableTypeId("Aero.Events::ClickCallback");

    // Allocate two ButtonController objects:
    // - ctrlAlive (weight = 100): kept rooted via PersistentRoot
    // - ctrlEphemeral (weight = 500): NOT rooted, subscribed via WeakDelegate
    auto ctrlAliveOpt = heap->allocateObject(ctrlTypeId, {std::int64_t{100}}, {});
    auto ctrlEphemeralOpt = heap->allocateObject(ctrlTypeId, {std::int64_t{500}}, {});
    AERO_CHECK(ctrlAliveOpt && ctrlEphemeralOpt, "Allocated two ButtonController instances");

    const auto ctrlAlive = *ctrlAliveOpt;
    const auto ctrlEphemeral = *ctrlEphemeralOpt;
    auto aliveRoot = heap->retain(ctrlAlive);

    // Test WeakRef generational liveness
    const WeakRef weakToEphemeral(ctrlEphemeral);
    AERO_CHECK(weakToEphemeral.lock(*heap).has_value(), "WeakRef alive before GC");

    // Create a WeakDelegate targeting ctrlEphemeral and a strong delegate targeting ctrlAlive
    auto weakDelOpt = heap->allocateWeakDelegate(
        delegateTypeId,
        ctrlEphemeral,
        onClickSymbolId);
    AERO_CHECK(weakDelOpt.has_value(), "Allocated WeakDelegate");

    auto strongDelRes = interpreter.invoke("Aero.Events::MakeStrongDelegate", {ctrlAlive});
    AERO_CHECK(strongDelRes.succeeded, "Created strong delegate to ctrlAlive");
    const auto strongDelRef = std::get<ObjectRef>(strongDelRes.value);

    // Combine weakDelegate (left) + strongDelegate (right) into a multicast delegate tree
    auto combineRes = interpreter.invoke(
        "Aero.Events::CombineCallbacks",
        {*weakDelOpt, strongDelRef});
    AERO_CHECK(combineRes.succeeded, "Combined WeakDelegate and StrongDelegate into multicast chain");
    const auto multicastRef = std::get<ObjectRef>(combineRes.value);
    auto multicastRoot = heap->retain(multicastRef);

    // Before GC: both ctrlEphemeral and ctrlAlive are alive; invoking multicast returns last result (100 + 7 = 107)
    auto fireBeforeGc = interpreter.invoke(
        "Aero.Events::FireCallback",
        {multicastRef, std::int64_t{7}});
    AERO_CHECK(fireBeforeGc.succeeded, "Multicast invocation before GC succeeded");
    AERO_CHECK(std::get<std::int64_t>(fireBeforeGc.value) == 107, "Returned 107 before GC");

    // Run full GC! Because weakDelOpt was created via allocateWeakDelegate (referenceFields = {7u}),
    // multicastRoot keeps the combine tree and strongDelRef/ctrlAlive alive, but MUST NOT keep
    // ctrlEphemeral alive!
    ShadowStack emptyStack;
    heap->collectFull(emptyStack);
    AERO_CHECK(
        !weakToEphemeral.lock(*heap).has_value(),
        "Unrooted WeakDelegate target (ctrlEphemeral) was collected by GC");
    AERO_CHECK(
        heap->isAlive(ctrlAlive),
        "Strong delegate target (ctrlAlive) remained alive across GC");

    // Invoke multicast delegate AFTER ctrlEphemeral was collected:
    // Interpreter must automatically skip & prune the dead WeakDelegate leaf without error!
    auto fireAfterGc = interpreter.invoke(
        "Aero.Events::FireCallback",
        {multicastRef, std::int64_t{25}});
    AERO_CHECK(
        fireAfterGc.succeeded,
        "Multicast invocation after WeakDelegate target collection succeeded without error");
    AERO_CHECK(
        std::get<std::int64_t>(fireAfterGc.value) == 125,
        "Alive listener in multicast chain still executed and returned 100 + 25 = 125");

    // Also test a multicast delegate where ALL leaves are WeakDelegates whose targets are collected:
    // invoking it MUST succeed and return the return type's defaultValue (0), NOT TypeMismatch!
    auto eph1Opt = heap->allocateObject(ctrlTypeId, {std::int64_t{10}}, {});
    auto eph2Opt = heap->allocateObject(ctrlTypeId, {std::int64_t{20}}, {});
    AERO_CHECK(eph1Opt && eph2Opt, "Allocated two ephemeral controllers");
    auto weak1Opt = heap->allocateWeakDelegate(delegateTypeId, *eph1Opt, onClickSymbolId);
    auto weak2Opt = heap->allocateWeakDelegate(delegateTypeId, *eph2Opt, onClickSymbolId);
    AERO_CHECK(weak1Opt && weak2Opt, "Allocated two WeakDelegates");
    auto allWeakMulticastRes = interpreter.invoke(
        "Aero.Events::CombineCallbacks",
        {*weak1Opt, *weak2Opt});
    AERO_CHECK(allWeakMulticastRes.succeeded, "Combined two WeakDelegates");
    const auto allWeakMulticastRef = std::get<ObjectRef>(allWeakMulticastRes.value);
    auto allWeakRoot = heap->retain(allWeakMulticastRef);

    // Collect both eph1 and eph2
    heap->collectFull(emptyStack);
    AERO_CHECK(
        !heap->isAlive(*eph1Opt) && !heap->isAlive(*eph2Opt),
        "Both ephemeral targets collected by GC");

    auto fireAllDead = interpreter.invoke(
        "Aero.Events::FireCallback",
        {allWeakMulticastRef, std::int64_t{99}});
    AERO_CHECK(
        fireAllDead.succeeded,
        "Multicast invocation when all WeakDelegate leaves are dead succeeded cleanly");
    AERO_CHECK(
        std::holds_alternative<std::int64_t>(fireAllDead.value) &&
            std::get<std::int64_t>(fireAllDead.value) == 0,
        "Multicast invocation when all WeakDelegate leaves are dead returns default int value 0");

    std::cout << "[PASS] TestWeakRefAndWeakDelegateMulticastPruning\n";
}

// ============================================================================
// Test 10: End-to-End RealScript <-> C++ UIElement Bridge, Inheritance Partial & Benchmark
// ============================================================================
void TestScriptToCppZeroHashUiBindingAndBenchmark() {
    ExecutionDomainContext domains;
    ScriptUiBridge bridge(domains);
    auto bindings = std::make_shared<BindingRegistry>();
    bridge.RegisterCoreUiBindings(*bindings);

    auto program = CompileAndLinkProgram({
        {"AeroUiSdk.rs",
         R"(
module Aero.UI;

public float UIElement_GetWidth(handle elem) { return 0.0f; }
public bool UIElement_SetWidth(handle elem, float width) { return false; }
public float UIElement_GetProperty(handle elem, int propId) { return 0.0f; }
public bool UIElement_SetProperty(handle elem, int propId, float val) { return false; }
)"},
        {"CustomButton.generated.rs",
         R"(
module App.UI;
import Aero.UI;

public class BaseControl
{
    public int basePadding;
    public BaseControl()
    {
        basePadding = 12;
    }
}

public partial class CustomButton : BaseControl
{
    public handle nativeElem;
    public float defaultWidth;
}
)"},
        {"CustomButton.logic.rs",
         R"(
module App.UI;
import Aero.UI;

public partial class CustomButton
{
    public CustomButton(handle h)
    {
        nativeElem = h;
        defaultWidth = 300.0f;
    }

    public float ResizeAndReadBack(float bonus)
    {
        float target = defaultWidth + bonus + basePadding;
        UIElement_SetWidth(nativeElem, target);
        UIElement_SetProperty(nativeElem, 6, 0.75f);
        return UIElement_GetWidth(nativeElem);
    }
}

public float DriveButtonFromScript(handle h, float bonus)
{
    CustomButton btn = new CustomButton(h);
    return btn.ResizeAndReadBack(bonus);
}

public float ReadWidthDirect(handle h)
{
    return UIElement_GetWidth(h);
}
)"}});

    Interpreter uiInterpreter(program, domains.uiHeapPtr());
    uiInterpreter.setBindingRegistry(bindings);

    auto cppButton = MakeIntrusive<Button>();
    cppButton->SetWidth(100.0f);

    const auto rawHandle = cppButton->ToNativeHandle(domains.nativeHandles());
    const auto exec = uiInterpreter.invoke(
        "App.UI::DriveButtonFromScript",
        {rawHandle, MakeFloat(50.0f)});
    if (!exec.succeeded) {
        std::cerr << "DriveButtonFromScript error: " << exec.error.message << "\n";
    }
    AERO_CHECK(exec.succeeded, "DriveButtonFromScript succeeded");
    // 300.0f + 50.0f + 12 (inherited from BaseControl across partial class!) = 362.0f
    float returnedWidth = 0.0f;
    AERO_CHECK(
        std::holds_alternative<FloatValue>(exec.value) &&
            TryGetFloat(exec.value, returnedWidth) &&
            std::fabs(returnedWidth - 362.0f) < 1e-5f,
        "Script partial class with inheritance mutated C++ Button.Width to 362.0f");
    AERO_CHECK(
        std::fabs(cppButton->GetWidth() - 362.0f) < 1e-5f,
        "C++ Button.GetWidth() reflects 362.0 set by RealScript");
    AERO_CHECK(
        std::fabs(cppButton->GetOpacity() - 0.75f) < 1e-5f,
        "C++ Button.GetOpacity() reflects 0.75 set by RealScript");

    // Destroy C++ Button and verify script access traps with InvalidObjectReference
    cppButton.Reset();
    const auto deadExec = uiInterpreter.invoke(
        "App.UI::ReadWidthDirect",
        {rawHandle});
    AERO_CHECK(
        !deadExec.succeeded &&
            deadExec.error.code == ErrorCode::InvalidObjectReference,
        "Script access to destroyed C++ UIElement cleanly traps with InvalidObjectReference");

    std::cout << "[PASS] TestScriptToCppZeroHashUiBindingAndBenchmark\n";
}

} // namespace aero::tests

int main() {
    using namespace aero::tests;
    std::cout << "=== AeroGUI 2.0 + RealScript Phase 1 Foundation Verification Suite ===\n";
    TestUnifiedValueAndFloat4();
    TestTypeClassEulerTourRtti();
    TestUIElementVisualTreeAndProperties();
    TestNativeHandleZeroHashAndGenerationalInvalidation();
    TestVisualTreeDerivedWrapperGcKeepAlive();
    TestExecutionDomainDualHeapAndCrossHeapReadOnly();
    TestPartialClassMergingAndDiagnostics();
    TestObservableDirtyMaskAndReactiveUiBridge();
    TestWeakRefAndWeakDelegateMulticastPruning();
    TestScriptToCppZeroHashUiBindingAndBenchmark();
    std::cout << "=== ALL 10 PHASE 1 FOUNDATION TEST SUITES PASSED (100%) ===\n";
    return 0;
}
