#pragma once

#include "realscript/runtime/Runtime.h"

#include <cstddef>
#include <cstdint>

namespace aero::detail {

using FieldMutationListenerFn = void (*)(
    void* context,
    realscript::runtime::ManagedHeap& heap,
    realscript::runtime::ObjectRef object,
    std::size_t fieldIndex,
    std::uint8_t dirtyBit);

void EnsureHeapHooksInstalled(realscript::runtime::ManagedHeap& heap);

void RegisterFieldMutationListener(
    realscript::runtime::ManagedHeap& heap,
    void* ownerKey,
    FieldMutationListenerFn callback,
    void* context);

void UnregisterFieldMutationListener(
    std::uint64_t heapId,
    void* ownerKey) noexcept;

void RetainHeapHookUser(realscript::runtime::ManagedHeap& heap);

void ReleaseHeapHookUser(realscript::runtime::ManagedHeap* heap) noexcept;

} // namespace aero::detail
