#pragma once

#include "realscript/runtime/Runtime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace realscript::aot {
struct ProgramDescriptor;
class ExecutionContext;
} // namespace realscript::aot

namespace aero {

enum class DomainKind : std::uint8_t {
    SimDeterministic = 0,
    UiPresentation = 1,
    Sim = SimDeterministic,
    Ui = UiPresentation,
};

struct PropertySlotHandle {
    static constexpr std::uint32_t InvalidIndex = 0xffffffffu;

    realscript::semantic::SymbolId typeId = 0;
    std::uint32_t backingFieldIndex = InvalidIndex;
    realscript::semantic::SymbolId getterSymbolId = 0;
    realscript::semantic::SymbolId setterSymbolId = 0;
    std::uint8_t observableBitIndex = 0xffu;
    bool isAutoProperty = false;
    bool isObservable = false;
    bool isReadOnly = false;
    bool isSimState = false;
    bool isSimCommand = false;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return typeId != 0 &&
            (backingFieldIndex != InvalidIndex || getterSymbolId != 0 || setterSymbolId != 0);
    }

    [[nodiscard]] constexpr bool isAutoBacked() const noexcept {
        return isAutoProperty && backingFieldIndex != InvalidIndex;
    }
};

struct SimCommandEnvelope {
    realscript::semantic::SymbolId commandTypeId = 0;
    realscript::runtime::ObjectRef targetObject{};
    std::vector<realscript::runtime::Value> payload;
    std::uint64_t clientFrameTick = 0;
};

using UiTaskId = std::uint64_t;

enum class UiTaskStatus : std::uint8_t {
    Pending = 0,
    Suspended = 1,
    Completed = 2,
    Cancelled = 3,
    Faulted = 4,
};

using UiAsyncStepCallback = std::function<bool(std::uint32_t stepIndex, realscript::runtime::Value& inOutResult)>;

class ExecutionDomainContext {
public:
    ExecutionDomainContext();
    ExecutionDomainContext(
        std::shared_ptr<realscript::runtime::ManagedHeap> simHeap,
        std::shared_ptr<realscript::runtime::ManagedHeap> uiHeap,
        std::shared_ptr<realscript::runtime::NativeHandleRegistry> nativeHandles = nullptr);
    ~ExecutionDomainContext();

    ExecutionDomainContext(const ExecutionDomainContext&) = delete;
    ExecutionDomainContext& operator=(const ExecutionDomainContext&) = delete;

    [[nodiscard]] realscript::runtime::ManagedHeap& simHeap() noexcept { return *simHeap_; }
    [[nodiscard]] const realscript::runtime::ManagedHeap& simHeap() const noexcept { return *simHeap_; }
    [[nodiscard]] std::shared_ptr<realscript::runtime::ManagedHeap> simHeapPtr() const noexcept { return simHeap_; }

    [[nodiscard]] realscript::runtime::ManagedHeap& uiHeap() noexcept { return *uiHeap_; }
    [[nodiscard]] const realscript::runtime::ManagedHeap& uiHeap() const noexcept { return *uiHeap_; }
    [[nodiscard]] std::shared_ptr<realscript::runtime::ManagedHeap> uiHeapPtr() const noexcept { return uiHeap_; }

    [[nodiscard]] realscript::runtime::NativeHandleRegistry& nativeHandles() noexcept { return *nativeHandles_; }
    [[nodiscard]] const realscript::runtime::NativeHandleRegistry& nativeHandles() const noexcept { return *nativeHandles_; }
    [[nodiscard]] std::shared_ptr<realscript::runtime::NativeHandleRegistry> nativeHandlesPtr() const noexcept {
        return nativeHandles_;
    }

    [[nodiscard]] DomainKind activeDomain() const noexcept { return activeDomain_; }
    void setActiveDomain(DomainKind domain) noexcept { activeDomain_ = domain; }

    [[nodiscard]] bool isSimObject(realscript::runtime::ObjectRef ref) const noexcept {
        return ref.valid() && simHeap_ &&
            ref.heapId == static_cast<std::uint32_t>(simHeap_->heapId());
    }
    [[nodiscard]] bool isUiObject(realscript::runtime::ObjectRef ref) const noexcept {
        return ref.valid() && uiHeap_ &&
            ref.heapId == static_cast<std::uint32_t>(uiHeap_->heapId());
    }

    [[nodiscard]] realscript::runtime::ManagedHeap* heapForObject(
        realscript::runtime::ObjectRef ref) const noexcept;
    [[nodiscard]] std::optional<std::string_view> stringViewAcrossDomains(
        realscript::runtime::ObjectRef ref) const;

    void attachInterpreter(realscript::runtime::Interpreter& interpreter, DomainKind domain);
    void detachInterpreter(realscript::runtime::Interpreter& interpreter) noexcept;
    void attachAotContext(realscript::aot::ExecutionContext& ctx, DomainKind domain);
    void detachAotContext(realscript::aot::ExecutionContext& ctx) noexcept;

    void registerProgramObservableMetadata(const realscript::runtime::ProgramImage& program);
    void registerAotProgramAttributes(const realscript::aot::ProgramDescriptor& program);

    [[nodiscard]] bool isTypeSimState(realscript::semantic::SymbolId typeId) const noexcept;
    [[nodiscard]] bool isTypeObservable(realscript::semantic::SymbolId typeId) const noexcept;
    [[nodiscard]] bool isSimCommandFunction(realscript::semantic::SymbolId functionId) const noexcept;
    [[nodiscard]] std::uint64_t globalObservableEpoch() const noexcept;
    [[nodiscard]] std::int32_t getOrAssignObservableToken(realscript::runtime::ObjectRef ref);
    [[nodiscard]] std::int32_t beginConsumeDirtyObservableTokens();
    [[nodiscard]] std::int32_t getConsumedDirtyObservableToken(std::size_t idx) const noexcept;

    [[nodiscard]] std::optional<realscript::runtime::Value> readCrossHeapFieldFast(
        realscript::runtime::ObjectRef simObject,
        std::size_t fieldIndex,
        realscript::runtime::RuntimeError* error = nullptr) const noexcept;

    bool readObjectField(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        std::size_t fieldIndex,
        realscript::runtime::Value& outValue,
        realscript::runtime::RuntimeError* error = nullptr) const noexcept;

    bool writeObjectField(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        std::size_t fieldIndex,
        realscript::runtime::Value newValue,
        realscript::runtime::RuntimeError* error = nullptr);

    [[nodiscard]] std::optional<PropertySlotHandle> resolvePropertySlot(
        const realscript::runtime::ProgramImage& program,
        realscript::semantic::SymbolId typeId,
        std::string_view memberName) const noexcept;

    [[nodiscard]] std::optional<PropertySlotHandle> resolvePropertySlot(
        const realscript::runtime::ProgramImage& program,
        std::string_view canonicalTypeName,
        std::string_view memberName) const noexcept;

    [[nodiscard]] std::optional<realscript::runtime::Value> readPropertySlotFast(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        const PropertySlotHandle& slot,
        const realscript::runtime::Interpreter* interpreter = nullptr,
        realscript::runtime::RuntimeError* error = nullptr) const;

    bool readPropertySlotFast(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        const PropertySlotHandle& slot,
        realscript::runtime::Value& outValue,
        realscript::runtime::RuntimeError* error = nullptr,
        const realscript::runtime::Interpreter* interpreter = nullptr) const;

    bool writePropertySlotFast(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        const PropertySlotHandle& slot,
        const realscript::runtime::Value& value,
        const realscript::runtime::Interpreter* interpreter = nullptr,
        realscript::runtime::RuntimeError* error = nullptr);

    bool writePropertySlotFast(
        DomainKind callerDomain,
        realscript::runtime::ObjectRef target,
        const PropertySlotHandle& slot,
        const realscript::runtime::Value& value,
        realscript::runtime::RuntimeError* error);

    void enqueueSimCommand(SimCommandEnvelope command);
    [[nodiscard]] std::vector<SimCommandEnvelope> consumeSimCommands();
    [[nodiscard]] std::size_t pendingSimCommandCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return pendingSimCommands_.size();
    }
    std::size_t flushSimCommands(
        const realscript::runtime::Interpreter& simInterpreter,
        realscript::runtime::RuntimeError* error = nullptr);
    std::size_t flushSimCommands(
        realscript::aot::ExecutionContext& simAotContext,
        realscript::runtime::RuntimeError* error = nullptr);

    UiTaskId spawnReentrantSequenceTask(
        const realscript::runtime::ProgramImage& program,
        const realscript::runtime::Interpreter& uiInterpreter,
        realscript::runtime::ObjectRef targetObject,
        std::string_view sequenceName,
        std::vector<realscript::runtime::Value> arguments = {},
        realscript::runtime::NativeHandle boundControl = {},
        UiTaskId awaitPrerequisite = 0);

    UiTaskId spawnCallbackTask(
        UiAsyncStepCallback stepCallback,
        realscript::runtime::NativeHandle boundControl = {},
        UiTaskId awaitPrerequisite = 0);

    bool stepUiTask(UiTaskId taskId, const realscript::runtime::Interpreter* uiInterpreter = nullptr);
    std::size_t stepAllUiTasks(const realscript::runtime::Interpreter* uiInterpreter = nullptr);
    bool cancelUiTask(UiTaskId taskId) noexcept;
    std::size_t cancelUiTasksForControl(realscript::runtime::NativeHandle boundControl) noexcept;
    [[nodiscard]] UiTaskStatus uiTaskStatus(UiTaskId taskId) const noexcept;
    [[nodiscard]] std::optional<realscript::runtime::Value> uiTaskResult(UiTaskId taskId) const;
    [[nodiscard]] std::size_t activeUiTaskCount() const noexcept;

    std::size_t stepUiIdleGc(
        const realscript::runtime::ShadowStack& shadowStack,
        double maxMilliseconds);
    std::size_t stepUiIdleGc(double maxMilliseconds);

    // Optional desktop-only fast-path method invoker hook (e.g. backed by RealScript::StencilJit).
    // Returns true if the target function was handled by the hook (check outSucceeded/outError),
    // or false to fall back cleanly to the Interpreter / SlimRuntime path.
    using DesktopJitInvokerFn = std::function<bool(
        DomainKind domain,
        realscript::semantic::SymbolId functionId,
        const realscript::runtime::Value* arguments,
        std::size_t argumentCount,
        bool& outSucceeded,
        realscript::runtime::Value& outResult,
        realscript::runtime::RuntimeError& outError)>;

    void setDesktopJitInvoker(DesktopJitInvokerFn invoker);
    void clearDesktopJitInvoker() noexcept;
    [[nodiscard]] bool hasDesktopJitInvoker() const noexcept;
    [[nodiscard]] bool hasSimCommands() const noexcept {
        return hasSimCommands_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool hasSimStateTypes() const noexcept {
        return hasSimStateTypes_.load(std::memory_order_acquire);
    }

    [[nodiscard]] realscript::runtime::IExecutionPolicy* executionPolicy(DomainKind domain) noexcept {
        return (domain == DomainKind::SimDeterministic)
            ? static_cast<realscript::runtime::IExecutionPolicy*>(&simPolicy_)
            : static_cast<realscript::runtime::IExecutionPolicy*>(&uiPolicy_);
    }

    [[nodiscard]] bool tryInvokeDesktopJit(
        DomainKind domain,
        realscript::semantic::SymbolId functionId,
        const realscript::runtime::Value* arguments,
        std::size_t argumentCount,
        realscript::runtime::ExecutionResult& outResult) const;

    [[nodiscard]] realscript::runtime::ExecutionResult invokeMethod(
        DomainKind domain,
        const realscript::runtime::Interpreter* interpreter,
        realscript::semantic::SymbolId functionId,
        const realscript::runtime::Value* arguments,
        std::size_t argumentCount,
        realscript::runtime::ExecutionOptions options = {}) const;

    [[nodiscard]] realscript::runtime::ExecutionResult invokeMethod(
        DomainKind domain,
        const realscript::runtime::Interpreter* interpreter,
        realscript::semantic::SymbolId functionId,
        const std::vector<realscript::runtime::Value>& arguments,
        realscript::runtime::ExecutionOptions options = {}) const {
        return invokeMethod(
            domain,
            interpreter,
            functionId,
            arguments.data(),
            arguments.size(),
            std::move(options));
    }

    [[nodiscard]] std::uint64_t desktopJitInvokeHits() const noexcept {
        return desktopJitInvokeHits_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t desktopJitFallbackHits() const noexcept {
        return desktopJitFallbackHits_.load(std::memory_order_relaxed);
    }
    void resetDesktopJitCounters() noexcept {
        desktopJitInvokeHits_.store(0u, std::memory_order_relaxed);
        desktopJitFallbackHits_.store(0u, std::memory_order_relaxed);
    }

private:
    class DomainPolicy final : public realscript::runtime::IExecutionPolicy {
    public:
        DomainPolicy(ExecutionDomainContext* owner, DomainKind domain) noexcept
            : owner_(owner), domain_(domain) {}

        [[nodiscard]] bool interceptsCalls() const noexcept override;
        [[nodiscard]] bool interceptsSameHeapWrites() const noexcept override;

        [[nodiscard]] realscript::runtime::ManagedHeap* resolveHeap(
            realscript::runtime::ObjectRef reference,
            realscript::runtime::ManagedHeap* defaultHeap) const noexcept override;

        [[nodiscard]] realscript::runtime::CrossHeapAccessPolicy crossHeapAccessPolicy(
            const realscript::runtime::ManagedHeap* activeHeap,
            const realscript::runtime::ManagedHeap* targetHeap) const noexcept override;

        bool checkFieldWrite(
            const realscript::runtime::ManagedHeap* activeHeap,
            const realscript::runtime::ManagedHeap* targetHeap,
            realscript::runtime::ObjectRef target,
            realscript::semantic::SymbolId declaringTypeId,
            std::size_t fieldIndex,
            std::string_view fieldName,
            const realscript::runtime::Value& newValue,
            realscript::runtime::RuntimeError& outError) const override;

        bool checkArrayWrite(
            const realscript::runtime::ManagedHeap* activeHeap,
            const realscript::runtime::ManagedHeap* targetHeap,
            realscript::runtime::ObjectRef target,
            const realscript::runtime::Value& newValue,
            realscript::runtime::RuntimeError& outError) const override;

        bool interceptCall(
            realscript::semantic::SymbolId functionId,
            realscript::runtime::ValueSpan arguments,
            bool& outIntercepted,
            realscript::runtime::Value& outResult,
            realscript::runtime::RuntimeError& outError) override;

    private:
        ExecutionDomainContext* owner_ = nullptr;
        DomainKind domain_ = DomainKind::UiPresentation;
    };

    struct TypeObservableMeta {
        bool isObservable = false;
        bool isSimState = false;
        bool isCollectionWrapper = false;
        std::vector<std::uint8_t> fieldToBitMap;
        std::unordered_map<std::string, std::uint8_t> customPropBits;
        std::unordered_map<std::string, bool> customPropSimState;
        std::unordered_map<std::string, bool> customPropSimCommand;
    };

    struct WrapperOwnerLink {
        realscript::runtime::ObjectRef ownerRef{};
        std::uint8_t bitIndex = 0xffu;
    };

    struct UiAsyncTaskFrame {
        UiTaskId taskId = 0;
        UiTaskStatus status = UiTaskStatus::Pending;
        UiTaskId prerequisiteTaskId = 0;
        realscript::runtime::WeakRef targetWeakRef{};
        realscript::runtime::NativeHandle boundControl{};
        std::uint32_t stepIndex = 0;
        realscript::runtime::Value result{};
        realscript::runtime::RuntimeError error{};
        bool isSequenceTask = false;
        realscript::semantic::SymbolId sequenceEntrySymbolId = 0;
        realscript::semantic::SymbolId sequenceStepSymbolId = 0;
        std::int64_t sequenceTargetId = 0;
        std::vector<realscript::runtime::Value> sequenceEntryArgs;
        std::size_t stateFieldIndex = PropertySlotHandle::InvalidIndex;
        std::size_t completedFieldIndex = PropertySlotHandle::InvalidIndex;
        std::size_t resultFieldIndex = PropertySlotHandle::InvalidIndex;
        std::vector<std::pair<std::size_t, realscript::runtime::Value>> isolatedSequenceFields;
        UiAsyncStepCallback stepCallback;
    };

    static void onHeapFieldMutatedCallback(
        void* context,
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef object,
        std::size_t fieldIndex,
        std::uint8_t dirtyBit);

    static void onObservableListOwnerDirtyCallback(
        void* context,
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ownerRef,
        std::uint8_t bitIndex);

    void handleFieldMutated(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef object,
        std::size_t fieldIndex,
        std::uint8_t dirtyBit);

    void noteSubscribedObjectMutated(
        const realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef object);

    void ensureProgramMetadataComputedLocked(
        const realscript::runtime::ProgramImage& program,
        bool pushToHeaps) const;

    std::shared_ptr<realscript::runtime::ManagedHeap> simHeap_;
    std::shared_ptr<realscript::runtime::ManagedHeap> uiHeap_;
    std::uint64_t simHeapId_ = 0;
    std::uint64_t uiHeapId_ = 0;
    std::shared_ptr<realscript::runtime::NativeHandleRegistry> nativeHandles_;
    DomainKind activeDomain_ = DomainKind::UiPresentation;

    mutable DomainPolicy simPolicy_;
    mutable DomainPolicy uiPolicy_;

    struct OwnerFieldRefKey {
        std::uint64_t ownerKey = 0;
        std::uint8_t dirtyBit = 0;
        bool operator==(const OwnerFieldRefKey& other) const noexcept {
            return ownerKey == other.ownerKey && dirtyBit == other.dirtyBit;
        }
    };
    struct OwnerFieldRefKeyHash {
        std::size_t operator()(const OwnerFieldRefKey& k) const noexcept {
            std::size_t h = std::hash<std::uint64_t>{}(k.ownerKey);
            h ^= std::hash<std::uint8_t>{}(k.dirtyBit) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct CachedPropertySlotEntry {
        std::uint64_t nameHash = 0;
        std::string name;
        std::optional<PropertySlotHandle> handle;
    };

    mutable std::mutex mutex_;
    mutable std::unordered_map<realscript::semantic::SymbolId, TypeObservableMeta> typeMeta_;
    mutable std::unordered_map<realscript::semantic::SymbolId, bool> simCommandFunctions_;
    mutable std::atomic<std::uint64_t> simCommandBloom_{0ull};
    mutable std::atomic<std::uint64_t> simStateTypeBloom_{0ull};
    mutable std::atomic<std::uint64_t> observableTypeBloom_{0ull};
    mutable std::unordered_set<const void*> registeredImages_;
    mutable std::unordered_map<
        const void*,
        std::unordered_map<realscript::semantic::SymbolId, std::vector<CachedPropertySlotEntry>>>
        propSlotCache_;

    // Keyed by ((heapId << 48) ^ ((slot << 32) | generation)) -> owners watching wrapper field 0
    std::unordered_map<std::uint64_t, std::unordered_map<std::uint64_t, std::vector<WrapperOwnerLink>>>
        wrapperOwnersByHeap_;
    std::unordered_map<
        std::uint64_t,
        std::unordered_map<OwnerFieldRefKey, std::vector<std::uint64_t>, OwnerFieldRefKeyHash>>
        ownerFieldToWrappersByHeap_;
    std::size_t wrapperOwnerLinkCount_ = 0u;
    std::atomic<bool> hasWrapperOwners_{false};

    std::vector<SimCommandEnvelope> pendingSimCommands_;
    UiTaskId nextUiTaskId_ = 1;
    std::vector<std::unique_ptr<UiAsyncTaskFrame>> uiTasks_;
    std::unordered_set<std::uint64_t> cancelledControlKeys_;
    std::shared_ptr<const DesktopJitInvokerFn> desktopJitInvoker_{};
    std::atomic<bool> hasDesktopJitInvoker_{false};
    mutable std::atomic<bool> hasSimCommands_{false};
    mutable std::atomic<bool> hasSimStateTypes_{false};
    mutable std::atomic<bool> hasObservableTypes_{false};
    std::unordered_map<std::uint64_t, std::int32_t> objectTokensByRefKey_;
    std::int32_t nextObjectToken_ = 1;
    std::vector<std::int32_t> dirtyObservableTokens_;
    std::unordered_set<std::int32_t> dirtyObservableTokenSet_;
    std::vector<std::int32_t> consumedDirtyTokens_;
    bool dirtyObservableOverflow_ = false;
    std::atomic<bool> hasSubscribedTokens_{false};

    mutable std::atomic<std::uint64_t> globalObservableEpoch_{1u};
    mutable std::atomic<std::uint64_t> desktopJitInvokeHits_{0u};
    mutable std::atomic<std::uint64_t> desktopJitFallbackHits_{0u};
};

} // namespace aero
