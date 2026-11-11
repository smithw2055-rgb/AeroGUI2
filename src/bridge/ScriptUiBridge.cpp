#include "aero/bridge/ScriptUiBridge.hpp"

#include <string>
#include <utility>

#include "aero/core/UiResourceContext.hpp"

namespace aero {

ScriptUiBridge::ScriptUiBridge(
    ExecutionDomainContext& domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter)
    : domains_(domains)
    , ownedEngine_(std::make_unique<DualTrackBindingEngine>(domains, program, uiInterpreter))
    , engine_(ownedEngine_.get()) {}

ScriptUiBridge::ScriptUiBridge(DualTrackBindingEngine& externalEngine) noexcept
    : domains_(externalEngine.Domains())
    , ownedEngine_(nullptr)
    , engine_(&externalEngine) {}

void ScriptUiBridge::SetUnderlyingEngine(DualTrackBindingEngine* externalEngine) noexcept {
    if (externalEngine != nullptr) {
        engine_ = externalEngine;
    } else {
        if (!ownedEngine_) {
            ownedEngine_ = std::make_unique<DualTrackBindingEngine>(domains_);
        }
        engine_ = ownedEngine_.get();
    }
}

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
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle)) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
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
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle)) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            float w = 0.0f;
            if (TryGetFloat(args[1], w)) {
                elem->SetWidth(w);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(PropId_Width, &error);
                }
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
            NativeHandle handle{};
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!args[0].tryGetNativeHandle(handle) || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
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
            NativeHandle handle{};
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!args[0].tryGetNativeHandle(handle) || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int, Value)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            const auto targetProp = static_cast<std::uint16_t>(*propId);
            const bool ok = elem->SetValue(
                targetProp,
                args[2],
                ValueSourceRank::Local);
            if (ok && elem->BoundTwoWayEngine() != nullptr) {
                (void)elem->NotifyTwoWayPropertyMutated(targetProp, &error);
            }
            return UnifiedValue{ok};
        });
}

bool ScriptUiBridge::BindObservableProperty(
    ObjectRef sourceObject,
    PropertySlotHandle sourceSlot,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    return BindObservableProperty(
        sourceObject,
        sourceSlot,
        targetElement,
        targetPropertyId,
        BindingMode::OneWay,
        ValueConverterKind::None,
        {},
        {},
        error);
}

bool ScriptUiBridge::BindObservableProperty(
    ObjectRef sourceObject,
    PropertySlotHandle sourceSlot,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    BindingMode mode,
    ValueConverterKind converter,
    std::string_view converterKey,
    std::string_view converterParameter,
    RuntimeError* error) {
    if (!engine_ || !sourceSlot.valid() || !targetElement) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid source slot or null target UIElement";
        }
        return false;
    }

    auto* heap = engine_->Domains().heapForObject(sourceObject);
    if (!heap || !heap->isAlive(sourceObject)) {
        if (error) {
            error->code = ErrorCode::InvalidObjectReference;
            error->message = "observable source object is not alive in SimHeap or UiHeap";
        }
        return false;
    }

    AxbCompiledHop hop{};
    hop.declaringTypeId = sourceSlot.typeId;
    hop.fieldIndex = sourceSlot.backingFieldIndex;
    hop.observableBitIndex = sourceSlot.observableBitIndex;
    hop.getterSymbolId = sourceSlot.getterSymbolId;
    hop.setterSymbolId = sourceSlot.setterSymbolId;
    hop.isAutoProperty = sourceSlot.isAutoProperty;
    hop.isObservable = sourceSlot.isObservable;
    hop.isReadOnly = sourceSlot.isReadOnly;
    hop.isSimState = sourceSlot.isSimState;
    hop.isSimCommand = sourceSlot.isSimCommand;

    AxbCompiledBindingChain chain{};
    chain.targetPropertyId = targetPropertyId;
    chain.mode = mode;
    chain.converter = converter;
    chain.converterKey = std::string(converterKey);
    chain.converterParameter = std::string(converterParameter);
    chain.hops.push_back(std::move(hop));

    RuntimeError localErr{};
    RuntimeError* errOut = error ? error : &localErr;
    errOut->code = ErrorCode::None;
    errOut->message.clear();

    const std::uint16_t slotId = engine_->BindChain(sourceObject, chain, targetElement, errOut);
    if (slotId == 0u || errOut->code != ErrorCode::None) {
        if (slotId != 0u) {
            (void)engine_->UnbindSlot(slotId);
        }
        return false;
    }
    return true;
}

bool ScriptUiBridge::BindPath(
    ObjectRef rootObject,
    std::string_view propertyPath,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    BindingMode mode,
    RuntimeError* error) {
    if (!engine_) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "ScriptUiBridge has no underlying DualTrackBindingEngine";
        }
        return false;
    }
    RuntimeError localErr{};
    RuntimeError* errOut = error ? error : &localErr;
    errOut->code = ErrorCode::None;
    errOut->message.clear();
    const std::uint16_t slotId = engine_->BindPath(
        rootObject,
        propertyPath,
        targetElement,
        targetPropertyId,
        mode,
        errOut);
    if (slotId == 0u || errOut->code != ErrorCode::None) {
        if (slotId != 0u) {
            (void)engine_->UnbindSlot(slotId);
        }
        return false;
    }
    return true;
}

bool ScriptUiBridge::PushTwoWayToSource(
    const UIElement* targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    if (!engine_) {
        return false;
    }
    return engine_->PushTwoWayToSource(targetElement, targetPropertyId, error);
}

std::size_t ScriptUiBridge::FlushReactiveBindings(RuntimeError* error) {
    if (!engine_) {
        return 0u;
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        !UiResourceContext::HasActiveThreadOverride()
            ? UiResourceContext::FindForDomains(&domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));
    return engine_->FlushTrackA(error) + engine_->FlushTrackB();
}

std::size_t ScriptUiBridge::BindingCount() const noexcept {
    if (!engine_) {
        return 0u;
    }
    return engine_->ReactiveBindings().size() +
           engine_->ElementBindings().size() +
           engine_->MultiBindings().size();
}

std::vector<ReactivePropertyBinding> ScriptUiBridge::Bindings() const {
    std::vector<ReactivePropertyBinding> out;
    if (!engine_) {
        return out;
    }
    const auto& reactive = engine_->ReactiveBindings();
    out.reserve(reactive.size());
    for (const auto& inst : reactive) {
        ReactivePropertyBinding item{};
        item.sourceObject = (!inst.resolvedHopObjects.empty() && inst.resolvedHopObjects.back().valid())
            ? inst.resolvedHopObjects.back()
            : inst.rootObject;
        if (!inst.hops.empty()) {
            item.sourceSlot = inst.hops.back().ToSlotHandle();
        }
        item.targetElement = inst.targetElement;
        item.targetPropertyId = inst.targetPropertyId;
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace aero
