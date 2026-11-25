#include "aero/runtime/AeroObservableList.hpp"

#include "AeroHeapHooksInternal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> ownerArrays;
    std::unordered_map<std::uint64_t, std::uint32_t> uiObserverCounts;
};

struct GlobalHookRegistry {
    std::mutex mutex;
    std::unordered_set<SymbolId> wrapperTypes;
    std::unordered_map<std::uint64_t, PerHeapState> heaps;
};

[[nodiscard]] constexpr std::uint64_t wrapperBloomMask(SymbolId id) noexcept {
    const std::uint64_t h0 = (static_cast<std::uint64_t>(id) * 11400714819323198485ull) >> 58u;
    const std::uint64_t h1 = (static_cast<std::uint64_t>(id) * 7046029254386353131ull) >> 58u;
    return (1ull << h0) | (1ull << h1);
}

std::atomic<std::uint64_t> gTrackedSlotEntries{0ull};
std::atomic<std::uint64_t> gWrapperTypeBloom{0ull};
std::atomic<bool> gHasWrapperTypes{false};

GlobalHookRegistry& hookRegistry() noexcept {
    static GlobalHookRegistry instance;
    return instance;
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

    std::vector<OwnerBinding> ownersCopy;
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto& hState = reg.heaps[heap.heapId()];
        const auto [aIt, inserted] = hState.arrays.try_emplace(slotKey(array));
        if (inserted) {
            gTrackedSlotEntries.fetch_add(1ull, std::memory_order_release);
        }
        auto& entry = aIt->second;
        entry.arrayRef = array;

        const std::uint64_t nextVer = ++entry.mutationVersion;
        if (changeKind == CollectionChangeKind::Reset) {
            for (auto& s : entry.ring) {
                s.record = {};
            }
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
        ownersCopy = entry.owners;
    }

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
    if (!sweptRef.valid() ||
        gTrackedSlotEntries.load(std::memory_order_acquire) == 0ull) {
        return;
    }
    const std::uint64_t key = slotKey(sweptRef);
    {
        auto& reg = hookRegistry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        const auto hIt = reg.heaps.find(heap.heapId());
        if (hIt == reg.heaps.end()) {
            return;
        }
        std::uint64_t removedEntries = 0ull;
        removedEntries += static_cast<std::uint64_t>(hIt->second.uiObserverCounts.erase(key));
        if (sweptRef.kind == ObjectKind::Record) {
            const auto oIt = hIt->second.ownerArrays.find(key);
            if (oIt != hIt->second.ownerArrays.end()) {
                for (const std::uint64_t aKey : oIt->second) {
                    const auto aIt = hIt->second.arrays.find(aKey);
                    if (aIt != hIt->second.arrays.end()) {
                        auto& owners = aIt->second.owners;
                        owners.erase(
                            std::remove_if(owners.begin(), owners.end(),
                                [&](const OwnerBinding& ob) { return slotKey(ob.ownerRef) == key; }),
                            owners.end());
                    }
                }
                hIt->second.ownerArrays.erase(oIt);
                ++removedEntries;
            }
        } else if (sweptRef.kind == ObjectKind::Array) {
            const auto aIt = hIt->second.arrays.find(key);
            if (aIt != hIt->second.arrays.end()) {
                for (const auto& ob : aIt->second.owners) {
                    const auto oIt = hIt->second.ownerArrays.find(slotKey(ob.ownerRef));
                    if (oIt != hIt->second.ownerArrays.end()) {
                        auto& vec = oIt->second;
                        vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
                        if (vec.empty()) {
                            hIt->second.ownerArrays.erase(oIt);
                            ++removedEntries;
                        }
                    }
                }
                hIt->second.arrays.erase(aIt);
                ++removedEntries;
            }
        }
        if (removedEntries > 0ull) {
            gTrackedSlotEntries.fetch_sub(removedEntries, std::memory_order_release);
        }
    }
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
            const std::uint64_t dropCount =
                static_cast<std::uint64_t>(hIt->second.arrays.size()) +
                static_cast<std::uint64_t>(hIt->second.ownerArrays.size()) +
                static_cast<std::uint64_t>(hIt->second.uiObserverCounts.size());
            if (dropCount > 0ull) {
                gTrackedSlotEntries.fetch_sub(dropCount, std::memory_order_release);
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
}

} // namespace detail

void ObservableListHelper::RegisterWrapperType(SymbolId wrapperTypeId) {
    if (wrapperTypeId == 0) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    if (reg.wrapperTypes.insert(wrapperTypeId).second) {
        gWrapperTypeBloom.fetch_or(wrapperBloomMask(wrapperTypeId), std::memory_order_release);
        gHasWrapperTypes.store(true, std::memory_order_release);
    }
}

bool ObservableListHelper::IsWrapperType(SymbolId typeId) noexcept {
    if (typeId == 0 || !gHasWrapperTypes.load(std::memory_order_acquire)) {
        return false;
    }
    const std::uint64_t reqMask = wrapperBloomMask(typeId);
    if ((gWrapperTypeBloom.load(std::memory_order_acquire) & reqMask) != reqMask) {
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
        const auto [aIt, inserted] = hState.arrays.try_emplace(slotKey(arrRef));
        if (inserted) {
            gTrackedSlotEntries.fetch_add(1ull, std::memory_order_release);
        }
        aIt->second.arrayRef = arrRef;
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

    auto sanitizeDeltaValue = [&](Value& val) {
        const auto* objRef = std::get_if<ObjectRef>(&val);
        if (objRef && objRef->valid() &&
            objRef->heapId == static_cast<std::uint32_t>(heap.heapId()) &&
            !heap.isAlive(*objRef)) {
            val = Value{};
        }
    };

    outDeltas.reserve(static_cast<std::size_t>(curVer - sinceVersion));
    for (std::uint64_t v = sinceVersion + 1ULL; v <= curVer; ++v) {
        const auto& slot = entry.ring[static_cast<std::size_t>(v % kDeltaRingCapacity)];
        if (slot.record.version != v || slot.record.kind == CollectionChangeKind::Reset) {
            outDeltas.clear();
            outRequiresReset = true;
            return true;
        }
        CollectionChangeRecord rec = slot.record;
        sanitizeDeltaValue(rec.oldValue);
        sanitizeDeltaValue(rec.newValue);
        outDeltas.push_back(std::move(rec));
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
    const std::uint64_t aKey = slotKey(arrRef);
    const std::uint64_t oKey = slotKey(ownerRef);
    const auto [aIt, aInserted] = hState.arrays.try_emplace(aKey);
    if (aInserted) {
        gTrackedSlotEntries.fetch_add(1ull, std::memory_order_release);
    }
    auto& entry = aIt->second;
    entry.arrayRef = arrRef;
    for (const auto& existing : entry.owners) {
        if (existing.ownerRef == ownerRef && existing.bitIndex == bitIndex) {
            return;
        }
    }
    entry.owners.push_back(OwnerBinding{ownerRef, bitIndex});
    const auto [oIt, oInserted] = hState.ownerArrays.try_emplace(oKey);
    if (oInserted) {
        gTrackedSlotEntries.fetch_add(1ull, std::memory_order_release);
    }
    auto& oVec = oIt->second;
    if (std::find(oVec.begin(), oVec.end(), aKey) == oVec.end()) {
        oVec.push_back(aKey);
    }
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
    auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return;
    }
    const std::uint64_t aKey = slotKey(arrRef);
    const auto aIt = hIt->second.arrays.find(aKey);
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
    bool stillBound = false;
    for (const auto& ob : owners) {
        if (ob.ownerRef == ownerRef) {
            stillBound = true;
            break;
        }
    }
    if (!stillBound) {
        const auto oIt = hIt->second.ownerArrays.find(slotKey(ownerRef));
        if (oIt != hIt->second.ownerArrays.end()) {
            auto& vec = oIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), aKey), vec.end());
            if (vec.empty()) {
                hIt->second.ownerArrays.erase(oIt);
                gTrackedSlotEntries.fetch_sub(1ull, std::memory_order_release);
            }
        }
    }
}

void ObservableListHelper::UnbindAllForOwnerField(
    ManagedHeap& heap,
    ObjectRef ownerRef,
    std::uint8_t bitIndex) noexcept {
    if (!ownerRef.valid() ||
        gTrackedSlotEntries.load(std::memory_order_acquire) == 0ull) {
        return;
    }
    auto& reg = hookRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto hIt = reg.heaps.find(heap.heapId());
    if (hIt == reg.heaps.end()) {
        return;
    }
    const std::uint64_t oKey = slotKey(ownerRef);
    const auto oIt = hIt->second.ownerArrays.find(oKey);
    if (oIt == hIt->second.ownerArrays.end()) {
        return;
    }
    auto& boundArrKeys = oIt->second;
    for (std::size_t i = boundArrKeys.size(); i > 0u; --i) {
        const std::uint64_t aKey = boundArrKeys[i - 1u];
        const auto aIt = hIt->second.arrays.find(aKey);
        if (aIt == hIt->second.arrays.end()) {
            boundArrKeys.erase(boundArrKeys.begin() + static_cast<std::ptrdiff_t>(i - 1u));
            continue;
        }
        auto& owners = aIt->second.owners;
        owners.erase(
            std::remove_if(owners.begin(), owners.end(),
                [&](const OwnerBinding& ob) {
                    return ob.ownerRef == ownerRef &&
                        (bitIndex >= 64u || ob.bitIndex == bitIndex);
                }),
            owners.end());
        bool anyRemainingForOwner = false;
        for (const auto& ob : owners) {
            if (ob.ownerRef == ownerRef) {
                anyRemainingForOwner = true;
                break;
            }
        }
        if (!anyRemainingForOwner) {
            boundArrKeys.erase(boundArrKeys.begin() + static_cast<std::ptrdiff_t>(i - 1u));
        }
    }
    if (boundArrKeys.empty()) {
        hIt->second.ownerArrays.erase(oIt);
        gTrackedSlotEntries.fetch_sub(1ull, std::memory_order_release);
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
        const auto [cIt, inserted] = hState.uiObserverCounts.try_emplace(slotKey(ref), 0u);
        if (inserted) {
            gTrackedSlotEntries.fetch_add(1ull, std::memory_order_release);
        }
        auto& count = cIt->second;
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
            gTrackedSlotEntries.fetch_sub(1ull, std::memory_order_release);
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
