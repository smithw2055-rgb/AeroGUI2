#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

struct ReactiveBindingInstance {
    std::uint16_t bindingSlotId = 0;
    std::string rawPath;
    BindingMode mode = BindingMode::OneWay;
    ObjectRef rootObject{};
    UnifiedValue fallbackValue{};
    std::vector<AxbCompiledHop> hops;
    std::vector<ObjectRef> resolvedHopObjects;
    IntrusivePtr<UIElement> targetElement{};
    std::uint16_t targetPropertyId = 0;
    bool initialized = false;
};

enum class EcsFieldScalarKind : std::uint8_t {
    Float32 = 0,
    Int32 = 1,
    Int64 = 2,
    Float4 = 3,
};

struct EcsColumnBindingChannel {
    std::string componentTypeName;
    std::string fieldName;
    SymbolId componentTypeId = 0;
    ObjectRef columnArrayRef{};
    NativeRawColumnView rawView{};
    std::size_t elementStride = 0;
    std::size_t fieldByteOffset = 0;
    EcsFieldScalarKind scalarKind = EcsFieldScalarKind::Float32;
    std::uint16_t targetPropertyId = PropId_Width;
    float scaleFactor = 1.0f;
    std::vector<IntrusivePtr<UIElement>> targetElements;
};

struct HotReloadMigrationPair {
    ObjectRef oldRef{};
    ObjectRef newRef{};
};

class DualTrackBindingEngine {
public:
    explicit DualTrackBindingEngine(
        ExecutionDomainContext& domains,
        const ProgramImage* program = nullptr,
        const Interpreter* uiInterpreter = nullptr) noexcept;
    ~DualTrackBindingEngine() noexcept;

    void SetProgramImage(const ProgramImage* program) noexcept { program_ = program; }
    void SetUiInterpreter(const Interpreter* uiInterpreter) noexcept { uiInterpreter_ = uiInterpreter; }

    // =========================================================================
    // Track A: ReactiveSlotTrack (Multi-hop CompiledSlotChain + dirtyFieldMask)
    // =========================================================================
    std::uint16_t BindChain(
        ObjectRef rootObject,
        const AxbCompiledBindingChain& compiledChain,
        const IntrusivePtr<UIElement>& targetElement,
        RuntimeError* error = nullptr);

    std::uint16_t BindPath(
        ObjectRef rootObject,
        std::string_view propertyPath,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        BindingMode mode = BindingMode::OneWay,
        RuntimeError* error = nullptr);

    // Flush dirty [Observable] objects across SimHeap and UiHeap, re-connecting
    // any intermediate hop whose reference changed, and writing terminal slots in O(1).
    std::size_t FlushTrackA(RuntimeError* error = nullptr);

    // Push a TwoWay-bound UIElement property change back to the terminal ViewModel slot
    // (or enqueue a SimCommandEnvelope if the setter is marked [SimCommand]).
    bool PushTwoWayToSource(
        const UIElement* targetElement,
        std::uint16_t targetPropertyId,
        RuntimeError* error = nullptr);

    // Sub-5ms Hot Reload state migration: patch root/hop ObjectRefs and re-resolve
    // PropertySlotHandles against the new ProgramImage without rebuilding UIElements.
    std::size_t ApplyHotReloadMigration(
        const ProgramImage& newProgram,
        const std::vector<HotReloadMigrationPair>& migratedRefs,
        RuntimeError* error = nullptr);

    [[nodiscard]] const std::vector<ReactiveBindingInstance>& ReactiveBindings() const noexcept {
        return trackABindings_;
    }

    // =========================================================================
    // Track B: EcsColumnSimdTrack (Zero-copy NativeRawColumnView batch scan)
    // =========================================================================
    std::size_t BindEcsColumn(EcsColumnBindingChannel channel);

    std::size_t BindEcsColumn(
        SymbolId componentTypeId,
        std::string_view fieldName,
        std::size_t rowIndex,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId);

    // Batch-scan all bound ECS contiguous columns from SimHeap/UiHeap and write
    // directly into UIElement fast inline or sparse slots with 0 heap allocations.
    std::size_t FlushTrackB() noexcept;

    [[nodiscard]] const std::vector<EcsColumnBindingChannel>& EcsChannels() const noexcept {
        return trackBChannels_;
    }

    [[nodiscard]] const NativeRawColumnView* ResolveChannelView(
        const EcsColumnBindingChannel& channel) const noexcept;

private:
    void ApplyFallbackOrClear(ReactiveBindingInstance& binding) const;
    bool EvaluateAndSyncChain(
        ReactiveBindingInstance& binding,
        std::size_t startHopIndex,
        RuntimeError* error);

    ExecutionDomainContext& domains_;
    const ProgramImage* program_ = nullptr;
    const Interpreter* uiInterpreter_ = nullptr;
    std::uint16_t nextBindingSlotId_ = 1;
    std::uint64_t lastObservedGcEpoch_ = 0;
    std::vector<ReactiveBindingInstance> trackABindings_;
    std::vector<EcsColumnBindingChannel> trackBChannels_;
};

} // namespace aero
