#pragma once

#include "realscript/runtime/Runtime.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aero {

enum class CollectionChangeKind : std::uint8_t {
    Add = 0,
    Insert = 1,
    Replace = 2,
    RemoveAt = 3,
    Clear = 4,
    Reset = 5,
};

struct CollectionChangeRecord {
    std::uint64_t version = 0;
    CollectionChangeKind kind = CollectionChangeKind::Reset;
    std::size_t index = 0;
    realscript::runtime::Value oldValue{};
    realscript::runtime::Value newValue{};
};

class ObservableListHelper final {
public:
    static constexpr std::size_t kDeltaRingCapacity = 256u;

    static void RegisterWrapperType(realscript::semantic::SymbolId wrapperTypeId);
    [[nodiscard]] static bool IsWrapperType(realscript::semantic::SymbolId typeId) noexcept;

    [[nodiscard]] static realscript::runtime::ObjectRef ResolveBackingArray(
        const realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef) noexcept;

    static void EnableTracking(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef);

    [[nodiscard]] static std::uint64_t MutationVersion(
        const realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef) noexcept;

    static bool ReadChangesSince(
        const realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        std::uint64_t sinceVersion,
        std::vector<CollectionChangeRecord>& outDeltas,
        std::uint64_t& outCurrentVersion,
        bool& outRequiresReset);

    static bool Add(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        const realscript::runtime::Value& item);

    static bool Insert(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        std::size_t index,
        const realscript::runtime::Value& item);

    static bool Replace(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        std::size_t index,
        const realscript::runtime::Value& item);

    static bool RemoveAt(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        std::size_t index,
        realscript::runtime::Value* outRemoved = nullptr);

    static bool Clear(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef);

    static void RecordReset(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef);

    static void BindOwnerProperty(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        realscript::runtime::ObjectRef ownerRef,
        std::uint8_t bitIndex);

    static void UnbindOwnerProperty(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef listOrArrayRef,
        realscript::runtime::ObjectRef ownerRef,
        std::uint8_t bitIndex) noexcept;

    static void UnbindAllForOwnerField(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ownerRef,
        std::uint8_t bitIndex) noexcept;

    static void AddUiObserver(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ref);

    static void RemoveUiObserver(
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ref) noexcept;

    [[nodiscard]] static std::uint32_t UiObserverCount(
        const realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ref) noexcept;

    using OwnerDirtyListenerFn = void(*)(
        void* context,
        realscript::runtime::ManagedHeap& heap,
        realscript::runtime::ObjectRef ownerRef,
        std::uint8_t bitIndex);

    static void RegisterOwnerDirtyListener(
        std::uint64_t heapId,
        void* ownerKey,
        OwnerDirtyListenerFn callback,
        void* context);

    static void UnregisterOwnerDirtyListener(
        std::uint64_t heapId,
        void* ownerKey) noexcept;

    [[nodiscard]] static std::uint64_t GlobalMutationEpoch() noexcept;
};

} // namespace aero
