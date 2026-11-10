#include "aero/bridge/ScriptUiBridge.hpp"

namespace aero {

ScriptUiBridge::ScriptUiBridge(ExecutionDomainContext& domains)
    : domains_(domains) {}

void ScriptUiBridge::RegisterCoreUiBindings(BindingRegistry& bindings) {
    auto* handlesPtr = &domains_.nativeHandles();

    // UIElement.GetWidth(handle) -> float
    (void)bindings.bind(
        "Aero.UI::UIElement_GetWidth",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing NativeHandle argument";
                return std::nullopt;
            }
            const auto* handle = std::get_if<NativeHandle>(&args[0]);
            if (!handle) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, *handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            return MakeFloat(elem->GetWidth());
        });

    // UIElement.SetWidth(handle, width) -> bool
    (void)bindings.bind(
        "Aero.UI::UIElement_SetWidth",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_SetWidth";
                return std::nullopt;
            }
            const auto* handle = std::get_if<NativeHandle>(&args[0]);
            if (!handle) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, *handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            float w = 0.0f;
            if (TryGetFloat(args[1], w)) {
                elem->SetWidth(w);
            } else {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected float width";
                return std::nullopt;
            }
            return UnifiedValue{true};
        });

    // UIElement.GetProperty(handle, propId) -> Value
    (void)bindings.bind(
        "Aero.UI::UIElement_GetProperty",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_GetProperty";
                return std::nullopt;
            }
            const auto* handle = std::get_if<NativeHandle>(&args[0]);
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!handle || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, *handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            return elem->GetValue(static_cast<std::uint16_t>(*propId));
        });

    // UIElement.SetProperty(handle, propId, value) -> bool
    (void)bindings.bind(
        "Aero.UI::UIElement_SetProperty",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_SetProperty";
                return std::nullopt;
            }
            const auto* handle = std::get_if<NativeHandle>(&args[0]);
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!handle || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int, Value)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, *handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            const bool ok = elem->SetValue(
                static_cast<std::uint16_t>(*propId),
                args[2],
                ValueSourceRank::Local);
            return UnifiedValue{ok};
        });
}

bool ScriptUiBridge::BindObservableProperty(
    ObjectRef sourceObject,
    PropertySlotHandle sourceSlot,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    if (!sourceSlot.valid() || !targetElement) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid source slot or null target UIElement";
        }
        return false;
    }
    auto* heap = domains_.heapForObject(sourceObject);
    if (!heap || !heap->isAlive(sourceObject)) {
        if (error) {
            error->code = ErrorCode::InvalidObjectReference;
            error->message = "observable source object is not alive in SimHeap or UiHeap";
        }
        return false;
    }
    heap->addUiObserver(sourceObject);

    // Perform initial synchronous evaluation from source to target UIElement
    UnifiedValue initialValue{};
    if (!domains_.readPropertySlotFast(
            DomainKind::Ui,
            sourceObject,
            sourceSlot,
            initialValue,
            error)) {
        return false;
    }
    targetElement->SetValue(targetPropertyId, initialValue, ValueSourceRank::Local);

    const auto slotIndex = static_cast<std::uint16_t>(bindings_.size());
    if (!IsFastInlinePropertyId(targetPropertyId)) {
        targetElement->SetPropertyMetadata(targetPropertyId, slotIndex, 0xFFFFu);
    }

    ReactivePropertyBinding binding{};
    binding.sourceObject = sourceObject;
    binding.sourceSlot = sourceSlot;
    binding.targetElement = targetElement;
    binding.targetPropertyId = targetPropertyId;
    bindings_.push_back(std::move(binding));
    return true;
}

std::size_t ScriptUiBridge::FlushReactiveBindings(RuntimeError* error) {
    std::size_t updatedCount = 0;

    auto processHeapDirtyQueue = [&](ManagedHeap& heap) {
        auto dirtyObjects = heap.consumeDirtyObjects();
        for (const auto& objRef : dirtyObjects) {
            const std::uint64_t mask = heap.dirtyFieldMask(objRef);
            if (mask == 0) {
                continue;
            }
            for (const auto& b : bindings_) {
                if (!(b.sourceObject == objRef) || !b.targetElement) {
                    continue;
                }
                if (b.sourceSlot.observableBitIndex < 64u &&
                    (mask & (1ULL << b.sourceSlot.observableBitIndex)) != 0ULL) {
                    UnifiedValue latest{};
                    if (domains_.readPropertySlotFast(
                            DomainKind::Ui,
                            b.sourceObject,
                            b.sourceSlot,
                            latest,
                            error)) {
                        b.targetElement->SetValue(
                            b.targetPropertyId,
                            latest,
                            ValueSourceRank::Local);
                        ++updatedCount;
                    }
                }
            }
            heap.clearDirtyFieldMask(objRef);
        }
    };

    processHeapDirtyQueue(domains_.simHeap());
    processHeapDirtyQueue(domains_.uiHeap());
    return updatedCount;
}

} // namespace aero
