#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

class AnimationEngine;

struct ReactiveBindingInstance {
    std::uint16_t bindingSlotId = 0;
    std::string rawPath;
    BindingMode mode = BindingMode::OneWay;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    ObjectRef rootObject{};
    UnifiedValue fallbackValue{};
    std::vector<AxbCompiledHop> hops;
    std::vector<ObjectRef> resolvedHopObjects;
    IntrusivePtr<UIElement> targetElement{};
    std::uint16_t targetPropertyId = 0;
    ObjectRef syncedCollectionRef{};
    ObjectRef syncedCollectionWrapperRef{};
    ObjectRef syncedCollectionOwnerRef{};
    std::uint8_t syncedCollectionBitIndex = 0xFFu;
    std::uint64_t lastSeenMutationVersion = 0;
    bool initialized = false;
};

// Phase 5D + P1-4: Zero-allocation UIElement-to-UIElement (ElementName / RelativeSource) binding instance
struct ElementPropertyBindingInstance {
    std::uint16_t bindingSlotId = 0;
    BindingMode mode = BindingMode::OneWay;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    std::string elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    std::uint16_t sourcePropertyId = 0;
    std::uint16_t targetPropertyId = 0;
    UIElement* cachedSourceElement = nullptr;
    IntrusivePtr<UIElement> targetElement{};
    UnifiedValue fallbackValue{};
    UnifiedValue lastSyncedValue{};
    bool initialized = false;
};

struct MultiBindingInstance;

// P1-4: Individual source slot inside a MultiBindingInstance (supports DataContext hop chains,
// ElementName / RelativeSource properties, and nested sub-MultiBindings)
struct MultiBindingSourceSlot {
    bool isElementSource = false;
    std::string rawPath;
    std::string elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    std::uint16_t sourcePropertyId = 0;
    UIElement* cachedSourceElement = nullptr;
    std::vector<AxbCompiledHop> hops;
    std::vector<ObjectRef> resolvedHopObjects;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    UnifiedValue fallbackValue{};
    UnifiedValue lastEvaluatedValue{};
    std::shared_ptr<MultiBindingInstance> nestedMulti{};
};

struct MultiBindingInstance {
    std::uint16_t bindingSlotId = 0;
    BindingMode mode = BindingMode::OneWay;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    ObjectRef rootObject{};
    UnifiedValue fallbackValue{};
    UnifiedValue lastSyncedOutput{};
    std::vector<MultiBindingSourceSlot> sources;
    IntrusivePtr<UIElement> targetElement{};
    std::uint16_t targetPropertyId = 0;
    bool hasElementSource = false;
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
    using HostValueConvertFn = std::function<UnifiedValue(
        const UnifiedValue& value,
        std::string_view parameter)>;
    using HostMultiValueConvertFn = std::function<UnifiedValue(
        const UnifiedValue* values,
        std::size_t count,
        std::string_view parameter)>;

    explicit DualTrackBindingEngine(
        ExecutionDomainContext& domains,
        const ProgramImage* program = nullptr,
        const Interpreter* uiInterpreter = nullptr) noexcept;
    ~DualTrackBindingEngine() noexcept;

    void SetProgramImage(const ProgramImage* program) noexcept { program_ = program; }
    void SetUiInterpreter(const Interpreter* uiInterpreter) noexcept { uiInterpreter_ = uiInterpreter; }
    void SetAnimationEngine(AnimationEngine* animationEngine) noexcept { animationEngine_ = animationEngine; }
    void SetWakeHostCallback(std::function<void()> callback) { wakeHostCallback_ = std::move(callback); }

    [[nodiscard]] ExecutionDomainContext& Domains() noexcept { return domains_; }
    [[nodiscard]] const ExecutionDomainContext& Domains() const noexcept { return domains_; }
    [[nodiscard]] const ProgramImage* Program() const noexcept { return program_; }
    [[nodiscard]] const Interpreter* UiInterpreter() const noexcept { return uiInterpreter_; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }
    [[nodiscard]] bool HasPendingDirtyObjects() const noexcept;
    void MarkValuesInPools() const noexcept;

    // =========================================================================
    // P1-4: Named C++ & RealScript SymbolId IValueConverter / IMultiValueConverter Registry
    // =========================================================================
    void RegisterValueConverter(
        std::string name,
        HostValueConvertFn convertFn,
        HostValueConvertFn convertBackFn = {});

    void RegisterMultiValueConverter(
        std::string name,
        HostMultiValueConvertFn convertFn);

    void RegisterScriptValueConverter(
        std::string name,
        SymbolId convertSymbolId,
        SymbolId convertBackSymbolId = 0,
        ObjectRef receiverObject = ObjectRef{});

    [[nodiscard]] UnifiedValue ApplySingleValueConversion(
        ValueConverterKind kind,
        std::string_view converterKey,
        std::string_view converterParameter,
        SymbolId scriptConvertSym,
        SymbolId scriptConvertBackSym,
        const UnifiedValue& rawVal,
        bool inverseForTwoWay) const;

    [[nodiscard]] UnifiedValue ApplyMultiValueConversion(
        ValueConverterKind kind,
        std::string_view converterKey,
        std::string_view converterParameter,
        SymbolId scriptConvertSym,
        const UnifiedValue* values,
        std::size_t count) const;

    // =========================================================================
    // Track A: ReactiveSlotTrack (Multi-hop CompiledSlotChain, MultiBinding & ElementName)
    // =========================================================================
    std::uint16_t BindChain(
        ObjectRef rootObject,
        const AxbCompiledBindingChain& compiledChain,
        const IntrusivePtr<UIElement>& targetElement,
        RuntimeError* error = nullptr);

    std::uint16_t BindElementProperty(
        const AxbCompiledBindingChain& compiledChain,
        const IntrusivePtr<UIElement>& targetElement);

    std::uint16_t BindMultiChain(
        ObjectRef rootObject,
        const AxbCompiledMultiBinding& compiledMulti,
        const IntrusivePtr<UIElement>& targetElement,
        RuntimeError* error = nullptr);

    std::uint16_t BindPath(
        ObjectRef rootObject,
        std::string_view propertyPath,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        BindingMode mode = BindingMode::OneWay,
        RuntimeError* error = nullptr);

    // Phase 5D: Re-target all Track A & MultiBinding instances inside subtreeRoot to newRootObject in-place
    std::size_t RebindSubtreeRootObject(
        UIElement* subtreeRoot,
        ObjectRef newRootObject,
        RuntimeError* error = nullptr);

    // Detach and clean up Track A / MultiBinding / ElementName bindings targeting elements inside subtreeRoot.
    std::size_t UnbindSubtree(const UIElement* subtreeRoot) noexcept;

    // Detach a single binding instance by its bindingSlotId
    bool UnbindSlot(std::uint16_t bindingSlotId) noexcept;

    // Flush dirty [Observable] objects across SimHeap and UiHeap + MultiBindings + ElementName bindings,
    // re-connecting any intermediate hop whose reference changed, and writing terminal slots in O(1).
    std::size_t FlushTrackA(RuntimeError* error = nullptr);

    // Push a TwoWay-bound UIElement property change back to the terminal ViewModel slot
    // (or ElementName source property, or SimCommandEnvelope if marked [SimCommand]).
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
    [[nodiscard]] const std::vector<ElementPropertyBindingInstance>& ElementBindings() const noexcept {
        return elementBindings_;
    }
    [[nodiscard]] const std::vector<MultiBindingInstance>& MultiBindings() const noexcept {
        return multiBindings_;
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
    struct RegisteredConverterEntry {
        HostValueConvertFn convertFn{};
        HostValueConvertFn convertBackFn{};
        HostMultiValueConvertFn multiConvertFn{};
        SymbolId convertSymbolId = 0;
        SymbolId convertBackSymbolId = 0;
        ObjectRef receiverObject{};
    };

    [[nodiscard]] UnifiedValue InvokeScriptConverterSymbol(
        SymbolId fnSym,
        ObjectRef receiverObj,
        const UnifiedValue* values,
        std::size_t valueCount,
        std::string_view parameter) const;

    [[nodiscard]] UnifiedValue FinalizeTargetPropertyValue(
        std::uint16_t targetPropId,
        std::string_view stringFormat,
        const UnifiedValue& val) const;

    void PopulateMultiBindingFromCompiled(
        MultiBindingInstance& outInst,
        ObjectRef rootObject,
        const AxbCompiledMultiBinding& compiledMulti,
        const IntrusivePtr<UIElement>& targetElement) const;

    void ReleaseMultiBindingObservers(MultiBindingInstance& mb) noexcept;
    [[nodiscard]] UnifiedValue EvaluateMultiBindingValue(
        MultiBindingInstance& mb,
        RuntimeError* error,
        bool& outAnyValid);
    bool EvaluateAndSyncMultiBinding(MultiBindingInstance& mb, RuntimeError* error);

    void ReleaseCollectionObserver(ReactiveBindingInstance& binding) noexcept;
    void ApplyFallbackOrClear(ReactiveBindingInstance& binding) const;
    bool EvaluateAndSyncChain(
        ReactiveBindingInstance& binding,
        std::size_t startHopIndex,
        RuntimeError* error);
    bool EvaluateAndSyncElementBinding(ElementPropertyBindingInstance& eb);

    ExecutionDomainContext& domains_;
    const ProgramImage* program_ = nullptr;
    const Interpreter* uiInterpreter_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    std::function<void()> wakeHostCallback_{};
    std::uint16_t nextBindingSlotId_ = 1;
    std::uint64_t lastObservedGcEpoch_ = 0;
    std::unordered_map<std::string, RegisteredConverterEntry> registeredConverters_;
    std::vector<CollectionChangeRecord> scratchCollectionDeltas_;
    std::vector<ReactiveBindingInstance> trackABindings_;
    std::vector<ElementPropertyBindingInstance> elementBindings_;
    std::vector<MultiBindingInstance> multiBindings_;
    std::vector<EcsColumnBindingChannel> trackBChannels_;
};

} // namespace aero
