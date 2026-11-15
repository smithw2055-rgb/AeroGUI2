#include "aero/runtime/AeroObservableList.hpp"

#include "AeroHeapHooksInternal.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aero {
namespace {

using realscript::runtime::ArrayMutationOp;
using realscript::runtime::ManagedHeap;
using realscript::runtime::ObjectKind;
using realscript::runtime::ObjectRef;
using realscript::runtime::Value;
using realscript::semantic::SymbolId;

[[nodiscard]] constexpr std::uint64_t slotKey(ObjectRef ref) noexcept {
    return (static_cast<std::uint64_t>(ref.slot) << 32) |
        static_cast<std::uint64_t>(ref.generation);
}

struct DeltaRingSlot {
    CollectionChangeRecord record{};
    ManagedHeap::RootToken oldRoot = 0;
    ManagedHeap::RootToken newRoot = 0;
};

struct OwnerBinding {
    ObjectRef ownerRef{};
    std::uint8_t bitIndex = 0xFFu;
};

struct ArrayTrackEntry {
    ObjectRef arrayRef{};
    std::uint64_t mutationVersion = 0;
    std::uint64_t minAvailableVersion = 1;
    std::array<DeltaRingSlot, ObservableListHelper::kDeltaRingCapacity> ring{};
    std::vector<OwnerBinding> owners;
};

struct FieldListenerEntry {
    void* ownerKey = nullptr;
    detail::FieldMutationListenerFn callback = nullptr;
    void* context = nullptr;
};

struct PerHeapState {
    bool hooksInstalled = false;
    std::size_t attachedDomainUsers = 0;
    std::vector<FieldListenerEntry> fieldListeners;
    std::unordered_map<std::uint64_t, ArrayTrackEntry> arrays;
    std::unordered_map<std::uint64_t, std::uint32_t> uiObserverCounts;
};

struct GlobalHookRegistry {
    std::mutex mutex;
    std::unordered_set<SymbolId> wrapperTypes;
    std::unordered_map<std::uint64_t, PerHeapState> heaps;
};

GlobalHookRegistry& hookRegistry() noexcept {
    static GlobalHookRegistry instance;
    return instance;
}

[[nodiscard]] ManagedHeap::RootToken maybeRetainValue(
    ManagedHeap& heap,
    const Value& value) {
    const auto* objRef = std::get_if<ObjectRef>(&value);
    if (!objRef || !objRef->valid() ||
        objRef->heapId != static_cast<std::uint32_t>(heap.heapId())) {
        return 0;
    }
    return heap.addPersistentRoot(*objRef);
}

void collectEntryRoots(
    ArrayTrackEntry& entry,
    std::vector<ManagedHeap::RootToken>& outTokens) {
    for (auto& slot : entry.ring) {
        if (slot.oldRoot != 0) {
            outTokens.push_back(slot.oldRoot);
            slot.oldRoot = 0;
        }
        if (slot.newRoot != 0) {
            outTokens.push_back(slot.newRoot);
            slot.newRoot = 0;
        }
        slot.record = {};
    }
}

void releaseTokens(
    ManagedHeap& heap,
    const std::vector<ManagedHeap::RootToken>& tokens) noexcept {
    for (const auto token : tokens) {
        if (token != 0) {
            (void)heap.removePersistentRoot(token);
        }
    }
}

[[nodiscard]] bool isWrapperTypeLocked(
    const GlobalHookRegistry& reg,
    SymbolId typeId) noexcept {
    return typeId != 0 && reg.wrapperTypes.find(typeId) != reg.wrapperTypes.end();
}

void onHeapFieldMutated(
    void*,
    ManagedHeap& heap,
    ObjectRef object,
    std::size_t fieldIndex,
    std::uint8_t dirtyBit) {
    std::array<FieldListenerEntry, 8> inlineListeners{};
    std::size_t inlineCount = 0;
    std::vector<FieldListenerEntry> overflowListeners;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(heap.heapId());
        if (hIt != reg.heaps.end()) {
            const auto& src = hIt->second.fieldListeners;
            if (src.size() <= inlineListeners.size()) {
                inlineCount = src.size();
                for (std::size_t i = 0; i < inlineCount; ++i) {
                    inlineListeners[i] = src[i];
                }
            } else {
                overflowListeners = src;
            }
        }
    }
    if (inlineCount > 0) {
        for (std::size_t i = 0; i < inlineCount; ++i) {
            if (inlineListeners[i].callback) {
                inlineListeners[i].callback(
                    inlineListeners[i].context, heap, object, fieldIndex, dirtyBit);
            }
        }
    } else {
        for (const auto& listener : overflowListeners) {
            if (listener.callback) {
                listener.callback(listener.context, heap, object, fieldIndex, dirtyBit);
            }
        }
    }
}

void onHeapArrayMutated(
    void*,
    ManagedHeap& heap,
    ObjectRef array,
    ArrayMutationOp op,
    std::size_t index,
    const Value& oldValue,
    const Value& newValue) {
    if (!array.valid() || array.kind != ObjectKind::Array) {
        return;
    }

    CollectionChangeKind changeKind = CollectionChangeKind::Reset;
    switch (op) {
    case ArrayMutationOp::Set:
        changeKind = CollectionChangeKind::Replace;
        break;
    case ArrayMutationOp::Push:
        changeKind = CollectionChangeKind::Add;
        break;
    case ArrayMutationOp::Insert:
        changeKind = CollectionChangeKind::Insert;
        break;
    case ArrayMutationOp::RemoveAt:
        changeKind = CollectionChangeKind::RemoveAt;
        break;
    case ArrayMutationOp::Clear:
        changeKind = CollectionChangeKind::Clear;
        break;
    case ArrayMutationOp::Reset:
    default:
        changeKind = CollectionChangeKind::Reset;
        break;
    }

    ManagedHeap::RootToken newOldRoot = 0;
    ManagedHeap::RootToken newNewRoot = 0;
    if (changeKind != CollectionChangeKind::Reset && changeKind != CollectionChangeKind::Clear) {
        newOldRoot = maybeRetainValue(heap, oldValue);
        newNewRoot = maybeRetainValue(heap, newValue);
    }

    std::vector<ManagedHeap::RootToken> obsoleteTokens;
    std::vector<OwnerBinding> ownersCopy;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto& hState = reg.heaps[heap.heapId()];
        auto& entry = hState.arrays[slotKey(array)];
        entry.arrayRef = array;

        const std::uint64_t nextVer = ++entry.mutationVersion;
        if (changeKind == CollectionChangeKind::Reset) {
            collectEntryRoots(entry, obsoleteTokens);
            entry.minAvailableVersion = nextVer;
        } else {
            const std::uint64_t capacity = ObservableListHelper::kDeltaRingCapacity;
            if (nextVer > capacity) {
                const std::uint64_t minKeep = nextVer - capacity + 1ULL;
                if (entry.minAvailableVersion < minKeep) {
                    entry.minAvailableVersion = minKeep;
                }
            }
        }

        auto& slot = entry.ring[static_cast<std::size_t>(
            nextVer % ObservableListHelper::kDeltaRingCapacity)];
        if (slot.oldRoot != 0) {
            obsoleteTokens.push_back(slot.oldRoot);
        }
        if (slot.newRoot != 0) {
            obsoleteTokens.push_back(slot.newRoot);
        }
        slot.record.version = nextVer;
        slot.record.kind = changeKind;
        slot.record.index = index;
        slot.record.oldValue = (changeKind == CollectionChangeKind::Reset ||
                                changeKind == CollectionChangeKind::Clear)
            ? Value{}
            : oldValue;
        slot.record.newValue = (changeKind == CollectionChangeKind::Reset ||
                                changeKind == CollectionChangeKind::Clear)
            ? Value{}
            : newValue;
        slot.oldRoot = newOldRoot;
        slot.newRoot = newNewRoot;
        ownersCopy = entry.owners;
    }

    releaseTokens(heap, obsoleteTokens);

    bool needPrune = false;
    for (const auto& binding : ownersCopy) {
        if (!heap.isAlive(binding.ownerRef)) {
            needPrune = true;
            continue;
        }
        if (binding.bitIndex < 64u) {
            (void)heap.markDirtyBit(binding.ownerRef, binding.bitIndex);
        }
    }

    if (needPrune) {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(heap.heapId());
        if (hIt != reg.heaps.end()) {
            const auto aIt = hIt->second.arrays.find(slotKey(array));
            if (aIt != hIt->second.arrays.end()) {
                auto& owners = aIt->second.owners;
                owners.erase(
                    std::remove_if(owners.begin(), owners.end(),
                        [&](const OwnerBinding& ob) { return !heap.isAlive(ob.ownerRef); }),
                    owners.end());
            }
        }
    }
}

void onHeapSlotSwept(
    void*,
    ManagedHeap& heap,
    ObjectRef sweptRef) {
    if (!sweptRef.valid()) {
        return;
    }
    const std::uint64_t key = slotKey(sweptRef);
    std::vector<ManagedHeap::RootToken> obsoleteTokens;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(heap.heapId());
        if (hIt == reg.heaps.end()) {
            return;
        }
        hIt->second.uiObserverCounts.erase(key);
        if (sweptRef.kind == ObjectKind::Array) {
            const auto aIt = hIt->second.arrays.find(key);
            if (aIt != hIt->second.arrays.end()) {
                collectEntryRoots(aIt->second, obsoleteTokens);
                hIt->second.arrays.erase(aIt);
            }
        }
    }
    releaseTokens(heap, obsoleteTokens);
}

void installHooksLocked(ManagedHeap& heap, PerHeapState& state) {
    if (state.hooksInstalled) {
        return;
    }
    state.hooksInstalled = true;
    heap.setFieldMutationCallback(&onHeapFieldMutated, nullptr);
    heap.setArrayMutationCallback(&onHeapArrayMutated, nullptr);
    heap.setSlotSweepCallback(&onHeapSlotSwept, nullptr);
}

} // namespace

namespace detail {

void EnsureHeapHooksInstalled(ManagedHeap& heap) {
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto& hState = reg.heaps[heap.heapId()];
    installHooksLocked(heap, hState);
}

void RegisterFieldMutationListener(
    ManagedHeap& heap,
    void* ownerKey,
    FieldMutationListenerFn callback,
    void* context) {
    if (!ownerKey || !callback) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto& hState = reg.heaps[heap.heapId()];
    installHooksLocked(heap, hState);
    for (auto& existing : hState.fieldListeners) {
        if (existing.ownerKey == ownerKey) {
            existing.callback = callback;
            existing.context = context;
            return;
        }
    }
    hState.fieldListeners.push_back(FieldListenerEntry{ownerKey, callback, context});
}

void UnregisterFieldMutationListener(
    std::uint64_t heapId,
    void* ownerKey) noexcept {
    if (heapId == 0 || !ownerKey) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heapId);
    if (hIt == reg.heaps.end()) {
        return;
    }
    auto& listeners = hIt->second.fieldListeners;
    listeners.erase(
        std::remove_if(listeners.begin(), listeners.end(),
            [&](const FieldListenerEntry& e) { return e.ownerKey == ownerKey; }),
        listeners.end());
}

void RetainHeapHookUser(ManagedHeap& heap) {
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto& hState = reg.heaps[heap.heapId()];
    installHooksLocked(heap, hState);
    ++hState.attachedDomainUsers;
}

void ReleaseHeapHookUser(ManagedHeap* heap) noexcept {
    if (!heap) {
        return;
    }
    const std::uint64_t hId = heap->heapId();
    std::vector<ManagedHeap::RootToken> obsoleteTokens;
    bool uninstallCallbacks = false;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(hId);
        if (hIt == reg.heaps.end()) {
            return;
        }
        if (hIt->second.attachedDomainUsers > 0u) {
            --hIt->second.attachedDomainUsers;
        }
        if (hIt->second.attachedDomainUsers == 0u && hIt->second.fieldListeners.empty()) {
            for (auto& [k, entry] : hIt->second.arrays) {
                (void)k;
                collectEntryRoots(entry, obsoleteTokens);
            }
            reg.heaps.erase(hIt);
            uninstallCallbacks = true;
        }
    }
    if (uninstallCallbacks) {
        heap->setFieldMutationCallback(nullptr, nullptr);
        heap->setArrayMutationCallback(nullptr, nullptr);
        heap->setSlotSweepCallback(nullptr, nullptr);
    }
    releaseTokens(*heap, obsoleteTokens);
}

} // namespace detail

void ObservableListHelper::RegisterWrapperType(SymbolId wrapperTypeId) {
    if (wrapperTypeId == 0) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.wrapperTypes.insert(wrapperTypeId);
}

bool ObservableListHelper::IsWrapperType(SymbolId typeId) noexcept {
    if (typeId == 0) {
        return false;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return isWrapperTypeLocked(reg, typeId);
}

ObjectRef ObservableListHelper::ResolveBackingArray(
    const ManagedHeap& heap,
    ObjectRef listOrArrayRef) noexcept {
    if (!listOrArrayRef.valid() || !heap.isAlive(listOrArrayRef)) {
        return ObjectRef{};
    }
    if (listOrArrayRef.kind == ObjectKind::Array) {
        return listOrArrayRef;
    }
    if (listOrArrayRef.kind != ObjectKind::Record) {
        return ObjectRef{};
    }
    const SymbolId tid = heap.objectTypeIdFast(listOrArrayRef);
    if (!IsWrapperType(tid)) {
        return ObjectRef{};
    }
    const Value* field0 = heap.fieldGetFast(listOrArrayRef, 0u);
    if (!field0) {
        return ObjectRef{};
    }
    const auto* arrRef = std::get_if<ObjectRef>(field0);
    if (!arrRef || !arrRef->valid() || arrRef->kind != ObjectKind::Array ||
        !heap.isAlive(*arrRef)) {
        return ObjectRef{};
    }
    return *arrRef;
}

void ObservableListHelper::EnableTracking(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return;
    }
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto& hState = reg.heaps[heap.heapId()];
        installHooksLocked(heap, hState);
        auto& entry = hState.arrays[slotKey(arrRef)];
        entry.arrayRef = arrRef;
    }
    (void)heap.setArrayObserved(arrRef, true);
}

std::uint64_t ObservableListHelper::MutationVersion(
    const ManagedHeap& heap,
    ObjectRef listOrArrayRef) noexcept {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return 0ULL;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return 0ULL;
    }
    const auto aIt = hIt->second.arrays.find(slotKey(arrRef));
    if (aIt == hIt->second.arrays.end()) {
        return 0ULL;
    }
    return aIt->second.mutationVersion;
}

bool ObservableListHelper::ReadChangesSince(
    const ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    std::uint64_t sinceVersion,
    std::vector<CollectionChangeRecord>& outDeltas,
    std::uint64_t& outCurrentVersion,
    bool& outRequiresReset) {
    outDeltas.clear();
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        outCurrentVersion = 0ULL;
        outRequiresReset = false;
        return false;
    }

    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        outCurrentVersion = 0ULL;
        outRequiresReset = (sinceVersion != 0ULL);
        return true;
    }
    const auto aIt = hIt->second.arrays.find(slotKey(arrRef));
    if (aIt == hIt->second.arrays.end()) {
        outCurrentVersion = 0ULL;
        outRequiresReset = (sinceVersion != 0ULL);
        return true;
    }

    const auto& entry = aIt->second;
    const std::uint64_t curVer = entry.mutationVersion;
    outCurrentVersion = curVer;
    if (sinceVersion == curVer) {
        outRequiresReset = false;
        return true;
    }
    if (sinceVersion > curVer ||
        (curVer - sinceVersion) > kDeltaRingCapacity ||
        (sinceVersion + 1ULL) < entry.minAvailableVersion) {
        outRequiresReset = true;
        return true;
    }

    outDeltas.reserve(static_cast<std::size_t>(curVer - sinceVersion));
    for (std::uint64_t v = sinceVersion + 1ULL; v <= curVer; ++v) {
        const auto& slot = entry.ring[static_cast<std::size_t>(v % kDeltaRingCapacity)];
        if (slot.record.version != v || slot.record.kind == CollectionChangeKind::Reset) {
            outDeltas.clear();
            outRequiresReset = true;
            return true;
        }
        outDeltas.push_back(slot.record);
    }
    outRequiresReset = false;
    return true;
}

bool ObservableListHelper::Add(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    const Value& item) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return false;
    }
    EnableTracking(heap, arrRef);
    return heap.arrayPush(arrRef, item);
}

bool ObservableListHelper::Insert(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    std::size_t index,
    const Value& item) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return false;
    }
    EnableTracking(heap, arrRef);
    return heap.arrayInsert(arrRef, index, item);
}

bool ObservableListHelper::Replace(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    std::size_t index,
    const Value& item) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return false;
    }
    EnableTracking(heap, arrRef);
    return heap.arraySet(arrRef, index, item);
}

bool ObservableListHelper::RemoveAt(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    std::size_t index,
    Value* outRemoved) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return false;
    }
    EnableTracking(heap, arrRef);
    return heap.arrayRemoveAt(arrRef, index, outRemoved);
}

bool ObservableListHelper::Clear(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return false;
    }
    EnableTracking(heap, arrRef);
    return heap.arrayClear(arrRef);
}

void ObservableListHelper::RecordReset(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid()) {
        return;
    }
    EnableTracking(heap, arrRef);
    heap.recordArrayReset(arrRef);
}

void ObservableListHelper::BindOwnerProperty(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    ObjectRef ownerRef,
    std::uint8_t bitIndex) {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid() || !ownerRef.valid() || bitIndex >= 64u) {
        return;
    }
    EnableTracking(heap, arrRef);
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto& hState = reg.heaps[heap.heapId()];
    auto& entry = hState.arrays[slotKey(arrRef)];
    entry.arrayRef = arrRef;
    for (const auto& existing : entry.owners) {
        if (existing.ownerRef == ownerRef && existing.bitIndex == bitIndex) {
            return;
        }
    }
    entry.owners.push_back(OwnerBinding{ownerRef, bitIndex});
}

void ObservableListHelper::UnbindOwnerProperty(
    ManagedHeap& heap,
    ObjectRef listOrArrayRef,
    ObjectRef ownerRef,
    std::uint8_t bitIndex) noexcept {
    const ObjectRef arrRef = ResolveBackingArray(heap, listOrArrayRef);
    if (!arrRef.valid() || !ownerRef.valid()) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return;
    }
    const auto aIt = hIt->second.arrays.find(slotKey(arrRef));
    if (aIt == hIt->second.arrays.end()) {
        return;
    }
    auto& owners = aIt->second.owners;
    owners.erase(
        std::remove_if(owners.begin(), owners.end(),
            [&](const OwnerBinding& ob) {
                return ob.ownerRef == ownerRef && ob.bitIndex == bitIndex;
            }),
        owners.end());
}

void ObservableListHelper::UnbindAllForOwnerField(
    ManagedHeap& heap,
    ObjectRef ownerRef,
    std::uint8_t bitIndex) noexcept {
    if (!ownerRef.valid()) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return;
    }
    for (auto& [k, entry] : hIt->second.arrays) {
        (void)k;
        auto& owners = entry.owners;
        owners.erase(
            std::remove_if(owners.begin(), owners.end(),
                [&](const OwnerBinding& ob) {
                    return ob.ownerRef == ownerRef &&
                        (bitIndex >= 64u || ob.bitIndex == bitIndex);
                }),
            owners.end());
    }
}

void ObservableListHelper::AddUiObserver(
    ManagedHeap& heap,
    ObjectRef ref) {
    if (!ref.valid() || ref.kind != ObjectKind::Record || !heap.isAlive(ref)) {
        return;
    }
    bool firstObserver = false;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto& hState = reg.heaps[heap.heapId()];
        installHooksLocked(heap, hState);
        auto& count = hState.uiObserverCounts[slotKey(ref)];
        firstObserver = (count == 0u);
        ++count;
    }
    if (firstObserver) {
        (void)heap.setObjectObserved(ref, true);
    }
}

void ObservableListHelper::RemoveUiObserver(
    ManagedHeap& heap,
    ObjectRef ref) noexcept {
    if (!ref.valid() || ref.kind != ObjectKind::Record) {
        return;
    }
    bool lastObserver = false;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(heap.heapId());
        if (hIt == reg.heaps.end()) {
            return;
        }
        auto& counts = hIt->second.uiObserverCounts;
        const auto cIt = counts.find(slotKey(ref));
        if (cIt == counts.end()) {
            return;
        }
        if (cIt->second <= 1u) {
            counts.erase(cIt);
            lastObserver = true;
        } else {
            --(cIt->second);
        }
    }
    if (lastObserver && heap.isAlive(ref)) {
        (void)heap.setObjectObserved(ref, false);
    }
}

std::uint32_t ObservableListHelper::UiObserverCount(
    const ManagedHeap& heap,
    ObjectRef ref) noexcept {
    if (!ref.valid() || ref.kind != ObjectKind::Record || !heap.isAlive(ref)) {
        return 0u;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    const auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return 0u;
    }
    const auto cIt = hIt->second.uiObserverCounts.find(slotKey(ref));
    return (cIt != hIt->second.uiObserverCounts.end()) ? cIt->second : 0u;
}

} // namespace aero
