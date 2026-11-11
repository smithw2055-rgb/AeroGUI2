#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/animation/AnimationEngine.hpp"
#include "aero/controls/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "realscript/bytecode/Bytecode.h"

namespace aero {

namespace {

[[nodiscard]] std::string_view ShortTypeNameOf(std::string_view raw) noexcept {
    if (const auto colon = raw.rfind(':'); colon != std::string_view::npos) {
        raw = raw.substr(colon + 1u);
    }
    if (const auto dot = raw.rfind('.'); dot != std::string_view::npos) {
        raw = raw.substr(dot + 1u);
    }
    return raw;
}

[[nodiscard]] UIElement* FindVisualRoot(UIElement* elem) noexcept {
    if (!elem) {
        return nullptr;
    }
    UIElement* cur = elem;
    while (cur->Parent() != nullptr) {
        cur = cur->Parent();
    }
    return cur;
}

[[nodiscard]] bool IsDescendantOrSelf(const UIElement* candidate, const UIElement* ancestor) noexcept {
    for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
        if (cur == ancestor) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasFocusedChildRecursive(const UIElement* elem) noexcept {
    if (!elem) return false;
    if (elem->IsFocused()) return true;
    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (HasFocusedChildRecursive(c)) return true;
    }
    return false;
}

[[nodiscard]] UnifiedValue ReadElementPropertyOrState(const UIElement* elem, std::uint16_t propId) {
    if (!elem) {
        return UnifiedValue{};
    }
    switch (propId) {
        case PropId_TriggerIsMouseOver:
            return UnifiedValue{elem->IsHovered()};
        case PropId_TriggerIsPressed:
            return UnifiedValue{elem->IsPressed()};
        case PropId_TriggerIsFocused:
            return UnifiedValue{elem->IsFocused()};
        case PropId_TriggerIsKeyboardFocusWithin:
            return UnifiedValue{HasFocusedChildRecursive(elem)};
        default:
            return elem->GetValue(propId);
    }
}

[[nodiscard]] bool BindingValuesEqual(const UnifiedValue& a, const UnifiedValue& b) noexcept {
    if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<std::monostate>(b)) {
        return std::holds_alternative<std::monostate>(a) && std::holds_alternative<std::monostate>(b);
    }
    if (std::holds_alternative<NullObject>(a) || std::holds_alternative<NullObject>(b)) {
        return std::holds_alternative<NullObject>(a) && std::holds_alternative<NullObject>(b);
    }
    if (const auto* ab = std::get_if<bool>(&a)) {
        if (const auto* bb = std::get_if<bool>(&b)) return *ab == *bb;
    }
    if (const auto* ai = std::get_if<std::int64_t>(&a)) {
        if (const auto* bi = std::get_if<std::int64_t>(&b)) return *ai == *bi;
    }
    float af = 0.0f;
    float bf = 0.0f;
    if (TryGetFloat(a, af) && TryGetFloat(b, bf) &&
        !std::holds_alternative<bool>(a) && !std::holds_alternative<bool>(b) &&
        !std::holds_alternative<std::int64_t>(a) && !std::holds_alternative<std::int64_t>(b)) {
        return std::fabs(af - bf) <= 1e-5f;
    }
    Float4Value af4{};
    Float4Value bf4{};
    if (a.tryGetFloat4(af4) && b.tryGetFloat4(bf4)) {
        return af4 == bf4;
    }
    if (const auto* as = std::get_if<StringValue>(&a)) {
        if (const auto* bs = std::get_if<StringValue>(&b)) {
            return as->view() == bs->view();
        }
    }
    if (const auto* ao = std::get_if<ObjectRef>(&a)) {
        if (const auto* bo = std::get_if<ObjectRef>(&b)) {
            return *ao == *bo;
        }
    }
    return false;
}

[[nodiscard]] float ParseStackFloat32(std::string_view raw, float fallback = 0.0f) noexcept {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1u);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1u);
    if (raw.empty() || raw.size() >= 60u) {
        return fallback;
    }
    char buf[64]{};
    for (std::size_t i = 0u; i < raw.size(); ++i) buf[i] = raw[i];
    char* endPtr = nullptr;
    const float v = std::strtof(buf, &endPtr);
    return (endPtr != buf) ? v : fallback;
}

[[nodiscard]] bool MultiBindingMatchesDirtyObj(
    const MultiBindingInstance& mb,
    const ObjectRef& dirtyObj,
    std::uint64_t mask) noexcept {
    if (mb.mode == BindingMode::OneTime && mb.initialized) {
        return false;
    }
    for (const auto& slot : mb.sources) {
        if (slot.nestedMulti) {
            if (MultiBindingMatchesDirtyObj(*slot.nestedMulti, dirtyObj, mask)) {
                return true;
            }
            continue;
        }
        if (slot.isElementSource) {
            continue;
        }
        const std::size_t hopCount = slot.hops.size();
        for (std::size_t h = 0u; h < hopCount && h < slot.resolvedHopObjects.size(); ++h) {
            if (!(slot.resolvedHopObjects[h] == dirtyObj)) {
                continue;
            }
            const std::uint8_t bitIdx = slot.hops[h].observableBitIndex;
            const bool bitMatched = (bitIdx < 64u) ? ((mask & (1ULL << bitIdx)) != 0ULL) : true;
            if (bitMatched) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool MultiBindingHasDirtyHop(
    const MultiBindingInstance& mb,
    const ExecutionDomainContext& domains) noexcept {
    if (mb.mode == BindingMode::OneTime && mb.initialized) {
        return false;
    }
    for (const auto& slot : mb.sources) {
        if (slot.nestedMulti) {
            if (MultiBindingHasDirtyHop(*slot.nestedMulti, domains)) {
                return true;
            }
            continue;
        }
        for (const auto& hopRef : slot.resolvedHopObjects) {
            if (!hopRef.valid()) {
                continue;
            }
            if (const auto* heap = domains.heapForObject(hopRef)) {
                if (heap->dirtyFieldMask(hopRef) != 0ULL) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

DualTrackBindingEngine::DualTrackBindingEngine(
    ExecutionDomainContext& domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter) noexcept
    : domains_(domains),
      program_(program),
      uiInterpreter_(uiInterpreter) {
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkValuesInPools();
    });
}

DualTrackBindingEngine::~DualTrackBindingEngine() noexcept {
    rs_rt::unregisterExternalValuePoolRootMarker(this);
    for (auto& binding : trackABindings_) {
        if (binding.targetElement && binding.targetElement->BoundTwoWayEngine() == this) {
            binding.targetElement->BindTwoWayEngine(nullptr);
        }
        ReleaseCollectionObserver(binding);
        if (binding.mode == BindingMode::OneTime) {
            continue;
        }
        for (auto& hopRef : binding.resolvedHopObjects) {
            if (hopRef.valid()) {
                if (auto* heap = domains_.heapForObject(hopRef)) {
                    heap->removeUiObserver(hopRef);
                }
                hopRef = ObjectRef{};
            }
        }
    }
    for (auto& eb : elementBindings_) {
        if (eb.targetElement && eb.targetElement->BoundTwoWayEngine() == this) {
            eb.targetElement->BindTwoWayEngine(nullptr);
        }
    }
    for (auto& mb : multiBindings_) {
        if (mb.targetElement && mb.targetElement->BoundTwoWayEngine() == this) {
            mb.targetElement->BindTwoWayEngine(nullptr);
        }
        ReleaseMultiBindingObservers(mb);
    }
}

bool DualTrackBindingEngine::HasPendingDirtyObjects() const noexcept {
    for (const auto& b : trackABindings_) {
        if (b.mode == BindingMode::OneTime && b.initialized) {
            continue;
        }
        for (const auto& hopRef : b.resolvedHopObjects) {
            if (!hopRef.valid()) {
                continue;
            }
            if (const auto* heap = domains_.heapForObject(hopRef)) {
                if (heap->dirtyFieldMask(hopRef) != 0ULL) {
                    return true;
                }
            }
        }
    }
    for (const auto& mb : multiBindings_) {
        if (MultiBindingHasDirtyHop(mb, domains_)) {
            return true;
        }
    }
    return false;
}

void DualTrackBindingEngine::MarkValuesInPools() const noexcept {
    for (const auto& b : trackABindings_) {
        if (b.fallbackValue.isTraceable()) {
            rs_rt::markValueInPools(b.fallbackValue);
        }
    }
    for (const auto& eb : elementBindings_) {
        if (eb.fallbackValue.isTraceable()) {
            rs_rt::markValueInPools(eb.fallbackValue);
        }
        if (eb.lastSyncedValue.isTraceable()) {
            rs_rt::markValueInPools(eb.lastSyncedValue);
        }
    }
    auto markMultiRec = [](auto& self, const MultiBindingInstance& mb) noexcept -> void {
        if (mb.fallbackValue.isTraceable()) {
            rs_rt::markValueInPools(mb.fallbackValue);
        }
        if (mb.lastSyncedOutput.isTraceable()) {
            rs_rt::markValueInPools(mb.lastSyncedOutput);
        }
        for (const auto& slot : mb.sources) {
            if (slot.fallbackValue.isTraceable()) {
                rs_rt::markValueInPools(slot.fallbackValue);
            }
            if (slot.lastEvaluatedValue.isTraceable()) {
                rs_rt::markValueInPools(slot.lastEvaluatedValue);
            }
            if (slot.nestedMulti) {
                self(self, *slot.nestedMulti);
            }
        }
    };
    for (const auto& mb : multiBindings_) {
        markMultiRec(markMultiRec, mb);
    }
}

void DualTrackBindingEngine::RegisterValueConverter(
    std::string name,
    HostValueConvertFn convertFn,
    HostValueConvertFn convertBackFn) {
    if (name.empty()) {
        return;
    }
    auto& entry = registeredConverters_[name];
    entry.convertFn = std::move(convertFn);
    entry.convertBackFn = std::move(convertBackFn);
    const std::string shortName(ShortTypeNameOf(name));
    if (!shortName.empty() && shortName != name) {
        registeredConverters_[shortName] = entry;
    }
}

void DualTrackBindingEngine::RegisterMultiValueConverter(
    std::string name,
    HostMultiValueConvertFn convertFn) {
    if (name.empty()) {
        return;
    }
    auto& entry = registeredConverters_[name];
    entry.multiConvertFn = std::move(convertFn);
    const std::string shortName(ShortTypeNameOf(name));
    if (!shortName.empty() && shortName != name) {
        registeredConverters_[shortName] = entry;
    }
}

void DualTrackBindingEngine::RegisterScriptValueConverter(
    std::string name,
    SymbolId convertSymbolId,
    SymbolId convertBackSymbolId,
    ObjectRef receiverObject) {
    if (name.empty()) {
        return;
    }
    auto& entry = registeredConverters_[name];
    entry.convertSymbolId = convertSymbolId;
    entry.convertBackSymbolId = convertBackSymbolId;
    entry.receiverObject = receiverObject;
    const std::string shortName(ShortTypeNameOf(name));
    if (!shortName.empty() && shortName != name) {
        registeredConverters_[shortName] = entry;
    }
}

UnifiedValue DualTrackBindingEngine::InvokeScriptConverterSymbol(
    SymbolId fnSym,
    ObjectRef receiverObj,
    const UnifiedValue* values,
    std::size_t valueCount,
    std::string_view parameter) const {
    const UnifiedValue passthrough = (values && valueCount > 0u) ? values[0] : UnifiedValue{};
    if (!uiInterpreter_ || fnSym == 0) {
        return passthrough;
    }

    auto marshalToPrimitive = [&](const UnifiedValue& raw, rs_sem::PrimitiveType pt) -> UnifiedValue {
        if (pt == rs_sem::PrimitiveType::String) {
            if (const auto* oref = std::get_if<ObjectRef>(&raw)) {
                if (oref->kind == ObjectKind::String) {
                    return raw;
                }
            }
            std::string strText;
            if (const auto* sv = std::get_if<StringValue>(&raw)) {
                strText = sv->str();
            } else {
                strText = FormatBindingString({}, &raw, 1u, &domains_);
            }
            if (auto strObj = domains_.uiHeap().allocateString(strText)) {
                return *strObj;
            }
            return raw;
        }
        if (pt == rs_sem::PrimitiveType::Float) {
            float fv = 0.0f;
            if (TryGetFloat(raw, fv)) {
                return MakeFloat(fv);
            }
            if (const auto* sv = std::get_if<StringValue>(&raw)) {
                return MakeFloat(ParseStackFloat32(sv->view(), 0.0f));
            }
            return MakeFloat(0.0f);
        }
        if (pt == rs_sem::PrimitiveType::Bool) {
            if (const auto* b = std::get_if<bool>(&raw)) return *b;
            if (const auto* iv = std::get_if<std::int64_t>(&raw)) return *iv != 0;
            float fv = 0.0f;
            if (TryGetFloat(raw, fv)) return std::fabs(fv) > 1e-6f;
            if (const auto* sv = std::get_if<StringValue>(&raw)) {
                return sv->view() == "true" || sv->view() == "True" || sv->view() == "1";
            }
            return false;
        }
        if (pt == rs_sem::PrimitiveType::Int || pt == rs_sem::PrimitiveType::Long ||
            pt == rs_sem::PrimitiveType::Short || pt == rs_sem::PrimitiveType::Byte ||
            pt == rs_sem::PrimitiveType::SByte || pt == rs_sem::PrimitiveType::UShort ||
            pt == rs_sem::PrimitiveType::UInt || pt == rs_sem::PrimitiveType::ULong ||
            pt == rs_sem::PrimitiveType::Enum) {
            if (const auto* iv = std::get_if<std::int64_t>(&raw)) {
                return *iv;
            }
            float fv = 0.0f;
            if (TryGetFloat(raw, fv)) {
                return value::FromInt(static_cast<std::int64_t>(std::llround(fv)));
            }
            if (const auto* sv = std::get_if<StringValue>(&raw)) {
                std::string_view ts = sv->view();
                while (!ts.empty() && std::isspace(static_cast<unsigned char>(ts.front()))) ts.remove_prefix(1u);
                while (!ts.empty() && std::isspace(static_cast<unsigned char>(ts.back()))) ts.remove_suffix(1u);
                std::int64_t parsedInt = 0;
                const auto [ptr, ec] = std::from_chars(ts.data(), ts.data() + ts.size(), parsedInt);
                if (ec == std::errc{} && ptr == ts.data() + ts.size()) {
                    return value::FromInt(parsedInt);
                }
                return value::FromInt(static_cast<std::int64_t>(std::llround(ParseStackFloat32(ts, 0.0f))));
            }
            return value::FromInt(0);
        }
        if (const auto* sv = std::get_if<StringValue>(&raw)) {
            if (auto strObj = domains_.uiHeap().allocateString(sv->str())) {
                return *strObj;
            }
        }
        return raw;
    };

    std::vector<UnifiedValue> callArgs;
    const auto* fnLoc = program_ ? program_->resolveFunction(fnSym) : nullptr;
    if (fnLoc && fnLoc->function) {
        const auto& paramTypes = fnLoc->function->parameterTypes;
        std::size_t pIdx = 0u;
        if (receiverObj.valid() && pIdx < paramTypes.size() &&
            paramTypes[pIdx] == rs_sem::PrimitiveType::Object) {
            callArgs.push_back(receiverObj);
            ++pIdx;
        }
        for (std::size_t i = 0u; i < valueCount && pIdx < paramTypes.size(); ++i, ++pIdx) {
            callArgs.push_back(marshalToPrimitive(values[i], paramTypes[pIdx]));
        }
        if (pIdx < paramTypes.size()) {
            const UnifiedValue paramUv = value::FromString(std::string(parameter));
            callArgs.push_back(marshalToPrimitive(paramUv, paramTypes[pIdx]));
            ++pIdx;
        }
        while (pIdx < paramTypes.size()) {
            callArgs.push_back(marshalToPrimitive(UnifiedValue{}, paramTypes[pIdx]));
            ++pIdx;
        }
    } else {
        if (receiverObj.valid()) {
            callArgs.push_back(receiverObj);
        }
        for (std::size_t i = 0u; i < valueCount; ++i) {
            if (const auto* sv = std::get_if<StringValue>(&values[i])) {
                if (auto strObj = domains_.uiHeap().allocateString(sv->str())) {
                    callArgs.push_back(*strObj);
                    continue;
                }
            }
            callArgs.push_back(values[i]);
        }
        if (!parameter.empty()) {
            if (auto strObj = domains_.uiHeap().allocateString(std::string(parameter))) {
                callArgs.push_back(*strObj);
            }
        }
    }

    auto res = uiInterpreter_->invoke(fnSym, callArgs);
    if (!res.succeeded && res.error.code == ErrorCode::InvalidArguments &&
        res.statistics.instructionsExecuted == 0u && !parameter.empty() && !callArgs.empty()) {
        callArgs.pop_back();
        res = uiInterpreter_->invoke(fnSym, callArgs);
    }
    if (!res.succeeded) {
        return passthrough;
    }

    UnifiedValue out = res.value;
    if (const auto* strRef = std::get_if<ObjectRef>(&out)) {
        if (strRef->kind == ObjectKind::String) {
            if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                out = value::FromString(std::string(*sv));
            }
        }
    }
    if (out.isTraceable()) {
        rs_rt::pinValueInPools(out);
    }
    return out;
}

UnifiedValue DualTrackBindingEngine::ApplySingleValueConversion(
    ValueConverterKind kind,
    std::string_view converterKey,
    std::string_view converterParameter,
    SymbolId scriptConvertSym,
    SymbolId scriptConvertBackSym,
    const UnifiedValue& rawVal,
    bool inverseForTwoWay) const {
    if (kind == ValueConverterKind::None) {
        return rawVal;
    }
    if (kind == ValueConverterKind::MathExpression) {
        if (!inverseForTwoWay) {
            const float paramF = ParseStackFloat32(converterParameter, 0.0f);
            float evalOut = 0.0f;
            if (EvaluateMathConverterExpression(converterKey, &rawVal, 1u, paramF, evalOut)) {
                return MakeFloat(evalOut);
            }
        }
        return rawVal;
    }
    if (kind == ValueConverterKind::CustomScriptOrHost) {
        const RegisteredConverterEntry* found = nullptr;
        if (!converterKey.empty()) {
            const auto it = registeredConverters_.find(std::string(converterKey));
            if (it != registeredConverters_.end()) {
                found = &it->second;
            } else {
                const std::string shortKey(ShortTypeNameOf(converterKey));
                const auto itShort = registeredConverters_.find(shortKey);
                if (itShort != registeredConverters_.end()) {
                    found = &itShort->second;
                }
            }
        }

        if (!inverseForTwoWay) {
            if (found && found->convertFn) {
                UnifiedValue out = found->convertFn(rawVal, converterParameter);
                if (out.isTraceable()) rs_rt::pinValueInPools(out);
                return out;
            }
            const SymbolId sym = (found && found->convertSymbolId != 0)
                ? found->convertSymbolId
                : scriptConvertSym;
            const ObjectRef recv = found ? found->receiverObject : ObjectRef{};
            if (sym != 0) {
                return InvokeScriptConverterSymbol(sym, recv, &rawVal, 1u, converterParameter);
            }
        } else {
            if (found && found->convertBackFn) {
                UnifiedValue out = found->convertBackFn(rawVal, converterParameter);
                if (out.isTraceable()) rs_rt::pinValueInPools(out);
                return out;
            }
            const SymbolId sym = (found && found->convertBackSymbolId != 0)
                ? found->convertBackSymbolId
                : scriptConvertBackSym;
            const ObjectRef recv = found ? found->receiverObject : ObjectRef{};
            if (sym != 0) {
                return InvokeScriptConverterSymbol(sym, recv, &rawVal, 1u, converterParameter);
            }
        }
        return rawVal;
    }
    return ApplyBuiltinValueConverter(kind, rawVal, inverseForTwoWay);
}

UnifiedValue DualTrackBindingEngine::ApplyMultiValueConversion(
    ValueConverterKind kind,
    std::string_view converterKey,
    std::string_view converterParameter,
    SymbolId scriptConvertSym,
    const UnifiedValue* values,
    std::size_t count) const {
    if (kind == ValueConverterKind::MathExpression) {
        const float paramF = ParseStackFloat32(converterParameter, 0.0f);
        float evalOut = 0.0f;
        if (EvaluateMathConverterExpression(converterKey, values, count, paramF, evalOut)) {
            return MakeFloat(evalOut);
        }
        return MakeFloat(0.0f);
    }
    if (kind == ValueConverterKind::CustomScriptOrHost) {
        const RegisteredConverterEntry* found = nullptr;
        if (!converterKey.empty()) {
            const auto it = registeredConverters_.find(std::string(converterKey));
            if (it != registeredConverters_.end()) {
                found = &it->second;
            } else {
                const std::string shortKey(ShortTypeNameOf(converterKey));
                const auto itShort = registeredConverters_.find(shortKey);
                if (itShort != registeredConverters_.end()) {
                    found = &itShort->second;
                }
            }
        }
        if (found && found->multiConvertFn) {
            UnifiedValue out = found->multiConvertFn(values, count, converterParameter);
            if (out.isTraceable()) rs_rt::pinValueInPools(out);
            return out;
        }
        const SymbolId sym = (found && found->convertSymbolId != 0)
            ? found->convertSymbolId
            : scriptConvertSym;
        const ObjectRef recv = found ? found->receiverObject : ObjectRef{};
        if (sym != 0) {
            return InvokeScriptConverterSymbol(sym, recv, values, count, converterParameter);
        }
        if (found && found->convertFn && count > 0u) {
            UnifiedValue out = found->convertFn(values[0], converterParameter);
            if (out.isTraceable()) rs_rt::pinValueInPools(out);
            return out;
        }
        return (count > 0u) ? values[0] : UnifiedValue{};
    }
    if (kind != ValueConverterKind::None && count > 0u) {
        return ApplyBuiltinValueConverter(kind, values[0], false);
    }
    return (count > 0u) ? values[0] : UnifiedValue{};
}

UnifiedValue DualTrackBindingEngine::FinalizeTargetPropertyValue(
    std::uint16_t targetPropId,
    std::string_view stringFormat,
    const UnifiedValue& val) const {
    if (!stringFormat.empty() &&
        !std::holds_alternative<std::monostate>(val) &&
        !std::holds_alternative<NullObject>(val)) {
        UnifiedValue formatted = value::FromString(FormatBindingString(stringFormat, &val, 1u, &domains_));
        if (formatted.isTraceable()) {
            rs_rt::pinValueInPools(formatted);
        }
        return formatted;
    }
    if ((targetPropId == PropId_Text || targetPropId == PropId_Title || targetPropId == PropId_Placeholder) &&
        !std::holds_alternative<StringValue>(val) &&
        !std::holds_alternative<std::monostate>(val) &&
        !std::holds_alternative<NullObject>(val)) {
        UnifiedValue coerced = value::FromString(FormatBindingString({}, &val, 1u, &domains_));
        if (coerced.isTraceable()) {
            rs_rt::pinValueInPools(coerced);
        }
        return coerced;
    }
    if (val.isTraceable()) {
        rs_rt::pinValueInPools(val);
    }
    return val;
}

void DualTrackBindingEngine::ReleaseCollectionObserver(ReactiveBindingInstance& binding) noexcept {
    if (binding.syncedCollectionRef.valid()) {
        if (binding.mode != BindingMode::OneTime && binding.syncedCollectionOwnerRef.valid()) {
            if (auto* arrHeap = domains_.heapForObject(binding.syncedCollectionRef)) {
                arrHeap->removeArrayPropertyObserver(
                    binding.syncedCollectionRef,
                    binding.syncedCollectionOwnerRef,
                    binding.syncedCollectionBitIndex);
            }
        }
        binding.syncedCollectionRef = ObjectRef{};
    }
    if (binding.syncedCollectionWrapperRef.valid()) {
        if (binding.mode != BindingMode::OneTime) {
            if (auto* wrapHeap = domains_.heapForObject(binding.syncedCollectionWrapperRef)) {
                wrapHeap->removeUiObserver(binding.syncedCollectionWrapperRef);
            }
        }
        binding.syncedCollectionWrapperRef = ObjectRef{};
    }
    binding.syncedCollectionOwnerRef = ObjectRef{};
    binding.syncedCollectionBitIndex = 0xFFu;
    binding.lastSeenMutationVersion = 0ULL;
}

void DualTrackBindingEngine::ApplyFallbackOrClear(ReactiveBindingInstance& binding) const {
    const IntrusivePtr<UIElement> targetElem = binding.targetElement;
    if (!targetElem) {
        return;
    }
    const std::uint16_t targetPropId = binding.targetPropertyId;
    const std::uint16_t bindingSlotId = binding.bindingSlotId;
    const ValueConverterKind converter = binding.converter;
    const UnifiedValue fallbackVal = binding.fallbackValue;

    std::uint16_t animTrack = 0xFFFFu;
    if (!IsFastInlinePropertyId(targetPropId)) {
        if (const auto* entry = targetElem->FindCompactEntry(targetPropId)) {
            animTrack = entry->animationTrackId;
        }
    }
    if (!std::holds_alternative<std::monostate>(fallbackVal)) {
        targetElem->SetValue(
            targetPropId,
            fallbackVal,
            ValueSourceRank::Local);
        if (!IsFastInlinePropertyId(targetPropId)) {
            targetElem->SetPropertyMetadata(
                targetPropId,
                bindingSlotId,
                animTrack);
        }
    } else if (converter == ValueConverterKind::NullToCollapsed ||
               converter == ValueConverterKind::NullToBoolean ||
               converter == ValueConverterKind::BooleanToVisibility ||
               converter == ValueConverterKind::InverseBooleanToVisibility) {
        const UnifiedValue convertedNull = ApplyBuiltinValueConverter(converter, NullObject{}, false);
        targetElem->SetValue(
            targetPropId,
            convertedNull,
            ValueSourceRank::Local);
    } else {
        targetElem->ClearValue(targetPropId);
    }
}

bool DualTrackBindingEngine::EvaluateAndSyncChain(
    ReactiveBindingInstance& binding,
    std::size_t startHopIndex,
    RuntimeError* error) {
    if (!binding.targetElement || binding.hops.empty() || !binding.rootObject.valid()) {
        return false;
    }

    if (binding.resolvedHopObjects.size() != binding.hops.size()) {
        binding.resolvedHopObjects.assign(binding.hops.size(), ObjectRef{});
    }

    ObjectRef currentObj = (startHopIndex == 0)
        ? binding.rootObject
        : binding.resolvedHopObjects[startHopIndex];
    if (!currentObj.valid()) {
        return false;
    }

    auto unbindHopRange = [&](std::size_t fromHop) {
        for (std::size_t clearIdx = fromHop; clearIdx < binding.resolvedHopObjects.size(); ++clearIdx) {
            const ObjectRef oldRef = binding.resolvedHopObjects[clearIdx];
            if (oldRef.valid()) {
                if (binding.mode != BindingMode::OneTime) {
                    if (auto* oldHeap = domains_.heapForObject(oldRef)) {
                        oldHeap->removeUiObserver(oldRef);
                    }
                }
                binding.resolvedHopObjects[clearIdx] = ObjectRef{};
            }
        }
    };

    const std::size_t hopCount = binding.hops.size();
    for (std::size_t h = startHopIndex; h < hopCount; ++h) {
        auto* heap = domains_.heapForObject(currentObj);
        if (!heap || !heap->isAlive(currentObj)) {
            unbindHopRange(h);
            ReleaseCollectionObserver(binding);
            ApplyFallbackOrClear(binding);
            return false;
        }
        if (!(binding.resolvedHopObjects[h] == currentObj)) {
            if (binding.resolvedHopObjects[h].valid() && binding.mode != BindingMode::OneTime) {
                if (auto* oldHeap = domains_.heapForObject(binding.resolvedHopObjects[h])) {
                    oldHeap->removeUiObserver(binding.resolvedHopObjects[h]);
                }
            }
            binding.resolvedHopObjects[h] = currentObj;
            if (binding.mode != BindingMode::OneTime) {
                heap->addUiObserver(currentObj);
            }
        }

        auto& hop = binding.hops[h];
        const SymbolId runtimeTypeId = heap->objectTypeId(currentObj).value_or(0);
        if (program_ && runtimeTypeId != 0 &&
            (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid())) {
            if (auto resolved = domains_.resolvePropertySlot(
                    *program_, runtimeTypeId, hop.memberName)) {
                hop.declaringTypeId = resolved->typeId;
                hop.fieldIndex = resolved->backingFieldIndex;
                hop.observableBitIndex = resolved->observableBitIndex;
                hop.getterSymbolId = resolved->getterSymbolId;
                hop.setterSymbolId = resolved->setterSymbolId;
                hop.isAutoProperty = resolved->isAutoProperty;
                hop.isObservable = resolved->isObservable;
                hop.isReadOnly = resolved->isReadOnly;
                hop.isSimState = resolved->isSimState;
                hop.isSimCommand = resolved->isSimCommand;
            }
        }

        const PropertySlotHandle slot = hop.ToSlotHandle();
        UnifiedValue hopVal{};
        if (slot.valid()) {
            if (!domains_.readPropertySlotFast(
                    DomainKind::Ui,
                    currentObj,
                    slot,
                    hopVal,
                    error,
                    uiInterpreter_)) {
                unbindHopRange(h + 1);
                return false;
            }
        } else if (hop.fieldIndex != PropertySlotHandle::InvalidIndex) {
            if (!domains_.readObjectField(
                    DomainKind::Ui,
                    currentObj,
                    hop.fieldIndex,
                    hopVal,
                    error)) {
                unbindHopRange(h + 1);
                return false;
            }
        } else {
            unbindHopRange(h + 1);
            return false;
        }

        if (h + 1 < hopCount) {
            const auto* nextRef = std::get_if<ObjectRef>(&hopVal);
            if (!nextRef || !nextRef->valid()) {
                unbindHopRange(h + 1);
                ReleaseCollectionObserver(binding);
                binding.initialized = true;
                ApplyFallbackOrClear(binding);
                return true;
            }
            currentObj = *nextRef;
        } else {
            if ((std::holds_alternative<NullObject>(hopVal) ||
                 std::holds_alternative<NullArray>(hopVal)) &&
                binding.converter == ValueConverterKind::None) {
                ReleaseCollectionObserver(binding);
                binding.initialized = true;
                ApplyFallbackOrClear(binding);
                return true;
            }
            if (binding.targetPropertyId == PropId_ItemsSource &&
                binding.converter == ValueConverterKind::None &&
                binding.stringFormat.empty()) {
                if (const auto* colObjRef = std::get_if<ObjectRef>(&hopVal)) {
                    if (auto* colHeap = domains_.heapForObject(*colObjRef)) {
                        const ObjectRef backingArr =
                            ObservableListHelper::ResolveBackingArray(*colHeap, *colObjRef);
                        if (backingArr.valid()) {
                            const std::uint8_t effBit = (hop.observableBitIndex < 64u)
                                ? hop.observableBitIndex
                                : static_cast<std::uint8_t>(
                                      (hop.fieldIndex != PropertySlotHandle::InvalidIndex && hop.fieldIndex < 63u)
                                          ? hop.fieldIndex
                                          : 63u);
                            const ObjectRef wrapperRef =
                                (colObjRef->kind == ObjectKind::Record && !(*colObjRef == backingArr))
                                    ? *colObjRef
                                    : ObjectRef{};

                            if (binding.initialized && binding.syncedCollectionRef == backingArr) {
                                std::uint64_t latestVer = 0ULL;
                                bool requiresReset = false;
                                if (colHeap->readArrayChangesSince(
                                        backingArr,
                                        binding.lastSeenMutationVersion,
                                        scratchCollectionDeltas_,
                                        latestVer,
                                        requiresReset)) {
                                    if (!requiresReset) {
                                        if (scratchCollectionDeltas_.empty()) {
                                            return false;
                                        }
                                        binding.lastSeenMutationVersion = latestVer;
                                        const IntrusivePtr<UIElement> targetElem = binding.targetElement;
                                        if (auto* ic = targetElem->DynamicCast<ItemsControl>()) {
                                            return ic->ApplyIncrementalChanges(
                                                scratchCollectionDeltas_.data(),
                                                scratchCollectionDeltas_.size());
                                        }
                                        if (auto* tc = targetElem->DynamicCast<TabControl>()) {
                                            return tc->ApplyIncrementalChanges(
                                                scratchCollectionDeltas_.data(),
                                                scratchCollectionDeltas_.size());
                                        }
                                    }
                                    // Overflow or explicit Reset: fall through to full collection sync!
                                    binding.lastSeenMutationVersion = latestVer;
                                    hopVal = UnifiedValue{backingArr};
                                }
                            } else {
                                ReleaseCollectionObserver(binding);
                                if (binding.mode != BindingMode::OneTime) {
                                    if (wrapperRef.valid()) {
                                        colHeap->addUiObserver(wrapperRef);
                                        binding.syncedCollectionWrapperRef = wrapperRef;
                                    }
                                    if (currentObj.valid() &&
                                        currentObj.heapId == backingArr.heapId) {
                                        colHeap->addArrayPropertyObserver(backingArr, currentObj, effBit);
                                        binding.syncedCollectionOwnerRef = currentObj;
                                        binding.syncedCollectionBitIndex = effBit;
                                    } else {
                                        colHeap->enableArrayChangeTracking(backingArr);
                                    }
                                }
                                binding.syncedCollectionRef = backingArr;
                                binding.lastSeenMutationVersion = colHeap->arrayMutationVersion(backingArr);
                                hopVal = UnifiedValue{backingArr};
                            }
                        } else {
                            ReleaseCollectionObserver(binding);
                        }
                    } else {
                        ReleaseCollectionObserver(binding);
                    }
                } else {
                    ReleaseCollectionObserver(binding);
                }
            }
            if (const auto* strRef = std::get_if<ObjectRef>(&hopVal)) {
                if (strRef->kind == ObjectKind::String) {
                    if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                        hopVal = value::FromString(std::string(*sv));
                    }
                }
            }
            if (binding.converter != ValueConverterKind::None) {
                hopVal = ApplySingleValueConversion(
                    binding.converter,
                    binding.converterKey,
                    binding.converterParameter,
                    binding.converterSymbolId,
                    binding.convertBackSymbolId,
                    hopVal,
                    false);
            }
            hopVal = FinalizeTargetPropertyValue(
                binding.targetPropertyId,
                binding.stringFormat,
                hopVal);
            // Capture target/slot before SetValue because SetValue (e.g. ItemsControl.ItemsSource)
            // may instantiate nested DataTemplate bindings and reallocate trackABindings_!
            const IntrusivePtr<UIElement> targetElem = binding.targetElement;
            const std::uint16_t targetPropId = binding.targetPropertyId;
            const std::uint16_t bindingSlotId = binding.bindingSlotId;
            binding.initialized = true;

            targetElem->SetValue(
                targetPropId,
                hopVal,
                ValueSourceRank::Local);
            if (!IsFastInlinePropertyId(targetPropId)) {
                const auto* entry = targetElem->FindCompactEntry(targetPropId);
                if (!entry || entry->bindingSlotId == 0xFFFFu) {
                    const std::uint16_t animTrack = entry ? entry->animationTrackId : 0xFFFFu;
                    targetElem->SetPropertyMetadata(
                        targetPropId,
                        bindingSlotId,
                        animTrack);
                }
            }
            return true;
        }
    }

    return true;
}

bool DualTrackBindingEngine::EvaluateAndSyncElementBinding(ElementPropertyBindingInstance& eb) {
    UIElement* target = eb.targetElement.Get();
    if (!target) {
        return false;
    }

    if (!eb.cachedSourceElement) {
        if (eb.relativeSelf) {
            eb.cachedSourceElement = target;
        } else if (!eb.relativeAncestorType.empty()) {
            for (UIElement* anc = target->Parent(); anc != nullptr; anc = anc->Parent()) {
                const char* tn = anc->GetTypeClass()->typeName;
                if (tn && ShortTypeNameOf(tn) == eb.relativeAncestorType) {
                    eb.cachedSourceElement = anc;
                    break;
                }
            }
        } else if (!eb.elementName.empty()) {
            if (UIElement* root = FindVisualRoot(target)) {
                eb.cachedSourceElement = root->FindName(eb.elementName);
            }
        }
    }

    if (!eb.cachedSourceElement) {
        return false;
    }

    UnifiedValue val = ReadElementPropertyOrState(eb.cachedSourceElement, eb.sourcePropertyId);
    if (eb.converter != ValueConverterKind::None) {
        val = ApplySingleValueConversion(
            eb.converter,
            eb.converterKey,
            eb.converterParameter,
            eb.converterSymbolId,
            eb.convertBackSymbolId,
            val,
            false);
    } else if ((std::holds_alternative<std::monostate>(val) || std::holds_alternative<NullObject>(val)) &&
               !std::holds_alternative<std::monostate>(eb.fallbackValue)) {
        val = eb.fallbackValue;
    }
    val = FinalizeTargetPropertyValue(eb.targetPropertyId, eb.stringFormat, val);

    if (eb.initialized && BindingValuesEqual(val, eb.lastSyncedValue)) {
        return false;
    }

    if (val.isTraceable()) {
        rs_rt::pinValueInPools(val);
    }
    eb.lastSyncedValue = val;
    eb.initialized = true;
    if (!std::holds_alternative<std::monostate>(val) && !std::holds_alternative<NullObject>(val)) {
        (void)target->SetValue(eb.targetPropertyId, val, ValueSourceRank::Local);
        return true;
    }
    target->ClearValue(eb.targetPropertyId);
    return true;
}

std::uint16_t DualTrackBindingEngine::BindChain(
    ObjectRef rootObject,
    const AxbCompiledBindingChain& compiledChain,
    const IntrusivePtr<UIElement>& targetElement,
    RuntimeError* error) {
    if (!compiledChain.elementName.empty() ||
        !compiledChain.relativeAncestorType.empty() ||
        compiledChain.relativeSelf) {
        return BindElementProperty(compiledChain, targetElement);
    }

    if (!rootObject.valid() || !targetElement || compiledChain.hops.empty()) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid rootObject, targetElement, or empty binding chain";
        }
        return 0;
    }

    const std::uint16_t slotId = nextBindingSlotId_++;
    ReactiveBindingInstance instance{};
    instance.bindingSlotId = slotId;
    instance.rawPath = compiledChain.rawPath;
    instance.mode = compiledChain.mode;
    instance.converter = compiledChain.converter;
    instance.converterKey = compiledChain.converterKey;
    instance.converterParameter = compiledChain.converterParameter;
    instance.stringFormat = compiledChain.stringFormat;
    instance.converterSymbolId = compiledChain.converterSymbolId;
    instance.convertBackSymbolId = compiledChain.convertBackSymbolId;
    instance.rootObject = rootObject;
    instance.fallbackValue = compiledChain.fallbackValue;
    if (instance.fallbackValue.isTraceable()) {
        rs_rt::pinValueInPools(instance.fallbackValue);
    }
    instance.hops = compiledChain.hops;
    instance.resolvedHopObjects.assign(compiledChain.hops.size(), ObjectRef{});
    instance.targetElement = targetElement;
    instance.targetPropertyId = compiledChain.targetPropertyId;

    if (instance.mode == BindingMode::TwoWay) {
        targetElement->BindTwoWayEngine(this);
    }

    if (!IsFastInlinePropertyId(instance.targetPropertyId)) {
        targetElement->SetPropertyMetadata(instance.targetPropertyId, slotId, 0xFFFFu);
    }

    (void)EvaluateAndSyncChain(instance, 0, error);
    trackABindings_.push_back(std::move(instance));
    return slotId;
}

std::uint16_t DualTrackBindingEngine::BindElementProperty(
    const AxbCompiledBindingChain& compiledChain,
    const IntrusivePtr<UIElement>& targetElement) {
    if (!targetElement) {
        return 0;
    }
    const std::string_view propName = !compiledChain.hops.empty()
        ? std::string_view(compiledChain.hops.back().memberName)
        : std::string_view(compiledChain.rawPath);
    if (propName.empty()) {
        return 0;
    }
    const std::uint16_t srcPropId = ResolveTriggerConditionPropertyId(propName);
    if (srcPropId == 0u) {
        return 0;
    }

    const std::uint16_t slotId = nextBindingSlotId_++;
    ElementPropertyBindingInstance eb{};
    eb.bindingSlotId = slotId;
    eb.mode = compiledChain.mode;
    eb.converter = compiledChain.converter;
    eb.converterKey = compiledChain.converterKey;
    eb.converterParameter = compiledChain.converterParameter;
    eb.stringFormat = compiledChain.stringFormat;
    eb.converterSymbolId = compiledChain.converterSymbolId;
    eb.convertBackSymbolId = compiledChain.convertBackSymbolId;
    eb.elementName = compiledChain.elementName;
    eb.relativeAncestorType = std::string(ShortTypeNameOf(compiledChain.relativeAncestorType));
    eb.relativeSelf = compiledChain.relativeSelf;
    eb.sourcePropertyId = srcPropId;
    eb.targetPropertyId = compiledChain.targetPropertyId;
    eb.targetElement = targetElement;
    eb.fallbackValue = compiledChain.fallbackValue;
    if (eb.fallbackValue.isTraceable()) {
        rs_rt::pinValueInPools(eb.fallbackValue);
    }

    if (eb.mode == BindingMode::TwoWay) {
        targetElement->BindTwoWayEngine(this);
    }
    if (!IsFastInlinePropertyId(eb.targetPropertyId)) {
        targetElement->SetPropertyMetadata(eb.targetPropertyId, slotId, 0xFFFFu);
    }

    (void)EvaluateAndSyncElementBinding(eb);
    elementBindings_.push_back(std::move(eb));
    return slotId;
}

void DualTrackBindingEngine::PopulateMultiBindingFromCompiled(
    MultiBindingInstance& outInst,
    ObjectRef rootObject,
    const AxbCompiledMultiBinding& compiledMulti,
    const IntrusivePtr<UIElement>& targetElement) const {
    outInst.mode = compiledMulti.mode;
    outInst.converter = compiledMulti.converter;
    outInst.converterKey = compiledMulti.converterKey;
    outInst.converterParameter = compiledMulti.converterParameter;
    outInst.stringFormat = compiledMulti.stringFormat;
    outInst.converterSymbolId = compiledMulti.converterSymbolId;
    outInst.convertBackSymbolId = compiledMulti.convertBackSymbolId;
    outInst.rootObject = rootObject;
    outInst.fallbackValue = compiledMulti.fallbackValue;
    if (outInst.fallbackValue.isTraceable()) {
        rs_rt::pinValueInPools(outInst.fallbackValue);
    }
    outInst.targetElement = targetElement;
    outInst.targetPropertyId = compiledMulti.targetPropertyId;
    outInst.hasElementSource = false;
    outInst.sources.clear();
    outInst.sources.reserve(compiledMulti.sources.size());

    for (const auto& srcCh : compiledMulti.sources) {
        MultiBindingSourceSlot slot{};
        slot.rawPath = srcCh.rawPath;
        slot.converter = srcCh.converter;
        slot.converterKey = srcCh.converterKey;
        slot.converterParameter = srcCh.converterParameter;
        slot.stringFormat = srcCh.stringFormat;
        slot.converterSymbolId = srcCh.converterSymbolId;
        slot.convertBackSymbolId = srcCh.convertBackSymbolId;
        slot.fallbackValue = srcCh.fallbackValue;
        if (slot.fallbackValue.isTraceable()) {
            rs_rt::pinValueInPools(slot.fallbackValue);
        }

        if (srcCh.nestedMultiBinding) {
            slot.nestedMulti = std::make_shared<MultiBindingInstance>();
            PopulateMultiBindingFromCompiled(
                *slot.nestedMulti,
                rootObject,
                *srcCh.nestedMultiBinding,
                targetElement);
            if (slot.nestedMulti->hasElementSource) {
                outInst.hasElementSource = true;
            }
            outInst.sources.push_back(std::move(slot));
            continue;
        }

        const bool isElem = !srcCh.elementName.empty() ||
                            !srcCh.relativeAncestorType.empty() ||
                            srcCh.relativeSelf;
        slot.isElementSource = isElem;
        if (isElem) {
            outInst.hasElementSource = true;
            slot.elementName = srcCh.elementName;
            slot.relativeAncestorType = std::string(ShortTypeNameOf(srcCh.relativeAncestorType));
            slot.relativeSelf = srcCh.relativeSelf;
            const std::string_view propLeaf = !srcCh.hops.empty()
                ? std::string_view(srcCh.hops.front().memberName)
                : std::string_view(srcCh.rawPath);
            slot.sourcePropertyId = propLeaf.empty() ? 0u : ResolveTriggerConditionPropertyId(propLeaf);
            // Support multi-hop after ElementName property (e.g. Path="Content.Life" ElementName="SlotHead")
            if (srcCh.hops.size() > 1u) {
                slot.hops.assign(srcCh.hops.begin() + 1, srcCh.hops.end());
                slot.resolvedHopObjects.assign(slot.hops.size(), ObjectRef{});
            }
        } else {
            slot.hops = srcCh.hops;
            if (slot.hops.empty() && !srcCh.rawPath.empty() && srcCh.rawPath != ".") {
                std::size_t segPos = 0u;
                while (segPos < srcCh.rawPath.size()) {
                    const std::size_t dot = srcCh.rawPath.find('.', segPos);
                    const std::string_view seg = (dot == std::string_view::npos)
                        ? std::string_view(srcCh.rawPath).substr(segPos)
                        : std::string_view(srcCh.rawPath).substr(segPos, dot - segPos);
                    if (!seg.empty()) {
                        AxbCompiledHop h{};
                        h.memberName = std::string(seg);
                        slot.hops.push_back(std::move(h));
                    }
                    if (dot == std::string_view::npos) break;
                    segPos = dot + 1u;
                }
            }
            slot.resolvedHopObjects.assign(slot.hops.size(), ObjectRef{});
        }
        outInst.sources.push_back(std::move(slot));
    }
}

void DualTrackBindingEngine::ReleaseMultiBindingObservers(MultiBindingInstance& mb) noexcept {
    if (mb.mode == BindingMode::OneTime) {
        return;
    }
    for (auto& slot : mb.sources) {
        if (slot.nestedMulti) {
            ReleaseMultiBindingObservers(*slot.nestedMulti);
            continue;
        }
        for (auto& hopRef : slot.resolvedHopObjects) {
            if (hopRef.valid()) {
                if (auto* heap = domains_.heapForObject(hopRef)) {
                    heap->removeUiObserver(hopRef);
                }
                hopRef = ObjectRef{};
            }
        }
    }
}

UnifiedValue DualTrackBindingEngine::EvaluateMultiBindingValue(
    MultiBindingInstance& mb,
    RuntimeError* error,
    bool& outAnyValid) {
    outAnyValid = false;
    UIElement* target = mb.targetElement.Get();
    if (!target) {
        return UnifiedValue{};
    }

    std::vector<UnifiedValue> slotValues;
    slotValues.reserve(mb.sources.size());

    for (auto& slot : mb.sources) {
        if (slot.nestedMulti) {
            bool subValid = false;
            UnifiedValue subVal = EvaluateMultiBindingValue(*slot.nestedMulti, error, subValid);
            slot.nestedMulti->initialized = true;
            if (subValid) {
                outAnyValid = true;
            } else if (!std::holds_alternative<std::monostate>(slot.fallbackValue)) {
                subVal = slot.fallbackValue;
                outAnyValid = true;
            }
            if (slot.converter != ValueConverterKind::None) {
                subVal = ApplySingleValueConversion(
                    slot.converter,
                    slot.converterKey,
                    slot.converterParameter,
                    slot.converterSymbolId,
                    slot.convertBackSymbolId,
                    subVal,
                    false);
            }
            if (!slot.stringFormat.empty()) {
                subVal = value::FromString(FormatBindingString(slot.stringFormat, &subVal, 1u, &domains_));
            }
            if (subVal.isTraceable()) rs_rt::pinValueInPools(subVal);
            slot.lastEvaluatedValue = subVal;
            slotValues.push_back(std::move(subVal));
            continue;
        }

        auto unbindSlotRange = [&](std::size_t fromHop) {
            for (std::size_t cIdx = fromHop; cIdx < slot.resolvedHopObjects.size(); ++cIdx) {
                const ObjectRef oldRef = slot.resolvedHopObjects[cIdx];
                if (oldRef.valid()) {
                    if (mb.mode != BindingMode::OneTime) {
                        if (auto* oldHeap = domains_.heapForObject(oldRef)) {
                            oldHeap->removeUiObserver(oldRef);
                        }
                    }
                    slot.resolvedHopObjects[cIdx] = ObjectRef{};
                }
            }
        };

        auto evalHopsFromObject = [&](ObjectRef startObj, UnifiedValue& outHopVal) -> bool {
            if (!startObj.valid()) {
                unbindSlotRange(0u);
                return false;
            }
            if (slot.hops.empty()) {
                outHopVal = startObj;
                return true;
            }
            if (slot.resolvedHopObjects.size() != slot.hops.size()) {
                slot.resolvedHopObjects.assign(slot.hops.size(), ObjectRef{});
            }
            ObjectRef curObj = startObj;
            for (std::size_t h = 0u; h < slot.hops.size(); ++h) {
                auto* heap = domains_.heapForObject(curObj);
                if (!heap || !heap->isAlive(curObj)) {
                    unbindSlotRange(h);
                    return false;
                }
                if (!(slot.resolvedHopObjects[h] == curObj)) {
                    if (slot.resolvedHopObjects[h].valid() && mb.mode != BindingMode::OneTime) {
                        if (auto* oldHeap = domains_.heapForObject(slot.resolvedHopObjects[h])) {
                            oldHeap->removeUiObserver(slot.resolvedHopObjects[h]);
                        }
                    }
                    slot.resolvedHopObjects[h] = curObj;
                    if (mb.mode != BindingMode::OneTime) {
                        heap->addUiObserver(curObj);
                    }
                }
                auto& hop = slot.hops[h];
                const SymbolId runtimeTypeId = heap->objectTypeId(curObj).value_or(0);
                if (program_ && runtimeTypeId != 0 &&
                    (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid())) {
                    if (auto resolved = domains_.resolvePropertySlot(
                            *program_, runtimeTypeId, hop.memberName)) {
                        hop.declaringTypeId = resolved->typeId;
                        hop.fieldIndex = resolved->backingFieldIndex;
                        hop.observableBitIndex = resolved->observableBitIndex;
                        hop.getterSymbolId = resolved->getterSymbolId;
                        hop.setterSymbolId = resolved->setterSymbolId;
                        hop.isAutoProperty = resolved->isAutoProperty;
                        hop.isObservable = resolved->isObservable;
                        hop.isReadOnly = resolved->isReadOnly;
                        hop.isSimState = resolved->isSimState;
                        hop.isSimCommand = resolved->isSimCommand;
                    }
                }
                const PropertySlotHandle sh = hop.ToSlotHandle();
                UnifiedValue hv{};
                if (sh.valid()) {
                    if (!domains_.readPropertySlotFast(
                            DomainKind::Ui, curObj, sh, hv, error, uiInterpreter_)) {
                        unbindSlotRange(h + 1u);
                        return false;
                    }
                } else if (hop.fieldIndex != PropertySlotHandle::InvalidIndex) {
                    if (!domains_.readObjectField(
                            DomainKind::Ui, curObj, hop.fieldIndex, hv, error)) {
                        unbindSlotRange(h + 1u);
                        return false;
                    }
                } else {
                    unbindSlotRange(h + 1u);
                    return false;
                }
                if (h + 1u < slot.hops.size()) {
                    const auto* nextRef = std::get_if<ObjectRef>(&hv);
                    if (!nextRef || !nextRef->valid()) {
                        unbindSlotRange(h + 1u);
                        return false;
                    }
                    curObj = *nextRef;
                } else {
                    outHopVal = hv;
                }
            }
            return true;
        };

        UnifiedValue slotVal{};
        bool slotValid = false;

        if (slot.isElementSource) {
            if (!slot.cachedSourceElement) {
                if (slot.relativeSelf) {
                    slot.cachedSourceElement = target;
                } else if (!slot.relativeAncestorType.empty()) {
                    for (UIElement* anc = target->Parent(); anc != nullptr; anc = anc->Parent()) {
                        const char* tn = anc->GetTypeClass()->typeName;
                        if (tn && ShortTypeNameOf(tn) == slot.relativeAncestorType) {
                            slot.cachedSourceElement = anc;
                            break;
                        }
                    }
                } else if (!slot.elementName.empty()) {
                    if (UIElement* root = FindVisualRoot(target)) {
                        slot.cachedSourceElement = root->FindName(slot.elementName);
                    }
                }
            }
            if (slot.cachedSourceElement && slot.sourcePropertyId != 0u) {
                const UnifiedValue elemPropVal = ReadElementPropertyOrState(
                    slot.cachedSourceElement,
                    slot.sourcePropertyId);
                if (!slot.hops.empty()) {
                    if (const auto* oref = std::get_if<ObjectRef>(&elemPropVal)) {
                        slotValid = evalHopsFromObject(*oref, slotVal);
                    } else {
                        unbindSlotRange(0u);
                    }
                } else if (!std::holds_alternative<std::monostate>(elemPropVal) &&
                           !std::holds_alternative<NullObject>(elemPropVal)) {
                    slotVal = elemPropVal;
                    slotValid = true;
                }
            }
        } else {
            slotValid = evalHopsFromObject(mb.rootObject, slotVal);
            if (slotValid && std::holds_alternative<NullObject>(slotVal) &&
                slot.converter == ValueConverterKind::None) {
                slotValid = false;
            }
        }

        if (slotValid) {
            if (const auto* strRef = std::get_if<ObjectRef>(&slotVal)) {
                if (strRef->kind == ObjectKind::String) {
                    if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                        slotVal = value::FromString(std::string(*sv));
                    }
                }
            }
            outAnyValid = true;
        } else if (!std::holds_alternative<std::monostate>(slot.fallbackValue)) {
            slotVal = slot.fallbackValue;
            outAnyValid = true;
        } else {
            slotVal = MakeFloat(0.0f);
        }

        if (slot.converter != ValueConverterKind::None) {
            slotVal = ApplySingleValueConversion(
                slot.converter,
                slot.converterKey,
                slot.converterParameter,
                slot.converterSymbolId,
                slot.convertBackSymbolId,
                slotVal,
                false);
        }
        if (!slot.stringFormat.empty()) {
            slotVal = value::FromString(FormatBindingString(slot.stringFormat, &slotVal, 1u, &domains_));
        }
        if (slotVal.isTraceable()) {
            rs_rt::pinValueInPools(slotVal);
        }
        slot.lastEvaluatedValue = slotVal;
        slotValues.push_back(std::move(slotVal));
    }

    if (!outAnyValid && !std::holds_alternative<std::monostate>(mb.fallbackValue)) {
        outAnyValid = true;
        return mb.fallbackValue;
    }

    UnifiedValue combined{};
    if (mb.converter != ValueConverterKind::None) {
        combined = ApplyMultiValueConversion(
            mb.converter,
            mb.converterKey,
            mb.converterParameter,
            mb.converterSymbolId,
            slotValues.data(),
            slotValues.size());
        if (!mb.stringFormat.empty()) {
            combined = value::FromString(FormatBindingString(mb.stringFormat, &combined, 1u, &domains_));
        }
    } else if (!mb.stringFormat.empty()) {
        combined = value::FromString(
            FormatBindingString(mb.stringFormat, slotValues.data(), slotValues.size(), &domains_));
    } else if (!slotValues.empty()) {
        combined = slotValues[0];
    }

    return combined;
}

bool DualTrackBindingEngine::EvaluateAndSyncMultiBinding(
    MultiBindingInstance& mb,
    RuntimeError* error) {
    if (!mb.targetElement) {
        return false;
    }
    bool anyValid = false;
    UnifiedValue outVal = EvaluateMultiBindingValue(mb, error, anyValid);
    if (!anyValid && std::holds_alternative<std::monostate>(outVal)) {
        mb.initialized = true;
        return false;
    }
    outVal = FinalizeTargetPropertyValue(mb.targetPropertyId, {}, outVal);
    if (mb.initialized && BindingValuesEqual(outVal, mb.lastSyncedOutput)) {
        return false;
    }
    if (outVal.isTraceable()) {
        rs_rt::pinValueInPools(outVal);
    }
    const IntrusivePtr<UIElement> targetElem = mb.targetElement;
    const std::uint16_t targetPropId = mb.targetPropertyId;
    const std::uint16_t slotId = mb.bindingSlotId;
    mb.lastSyncedOutput = outVal;
    mb.initialized = true;

    targetElem->SetValue(targetPropId, outVal, ValueSourceRank::Local);
    if (!IsFastInlinePropertyId(targetPropId)) {
        const auto* entry = targetElem->FindCompactEntry(targetPropId);
        if (!entry || entry->bindingSlotId == 0xFFFFu) {
            const std::uint16_t animTrack = entry ? entry->animationTrackId : 0xFFFFu;
            targetElem->SetPropertyMetadata(targetPropId, slotId, animTrack);
        }
    }
    return true;
}

std::uint16_t DualTrackBindingEngine::BindMultiChain(
    ObjectRef rootObject,
    const AxbCompiledMultiBinding& compiledMulti,
    const IntrusivePtr<UIElement>& targetElement,
    RuntimeError* error) {
    if (!targetElement || compiledMulti.sources.empty()) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid targetElement or empty MultiBinding sources";
        }
        return 0u;
    }

    const std::uint16_t slotId = nextBindingSlotId_++;
    MultiBindingInstance mb{};
    mb.bindingSlotId = slotId;
    PopulateMultiBindingFromCompiled(mb, rootObject, compiledMulti, targetElement);

    if (!IsFastInlinePropertyId(mb.targetPropertyId)) {
        targetElement->SetPropertyMetadata(mb.targetPropertyId, slotId, 0xFFFFu);
    }

    (void)EvaluateAndSyncMultiBinding(mb, error);
    multiBindings_.push_back(std::move(mb));
    return slotId;
}

std::uint16_t DualTrackBindingEngine::BindPath(
    ObjectRef rootObject,
    std::string_view propertyPath,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    BindingMode mode,
    RuntimeError* error) {
    if (!rootObject.valid() || propertyPath.empty() || !targetElement) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid arguments to BindPath";
        }
        return 0;
    }

    AxbCompiledBindingChain chain{};
    chain.rawPath = std::string(propertyPath);
    chain.targetPropertyId = targetPropertyId;
    chain.mode = mode;

    std::size_t pos = 0;
    while (pos < propertyPath.size()) {
        const std::size_t dot = propertyPath.find('.', pos);
        const std::string_view segment = (dot == std::string_view::npos)
            ? propertyPath.substr(pos)
            : propertyPath.substr(pos, dot - pos);
        if (!segment.empty()) {
            AxbCompiledHop hop{};
            hop.memberName = std::string(segment);
            chain.hops.push_back(std::move(hop));
        }
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }

    return BindChain(rootObject, chain, targetElement, error);
}

std::size_t DualTrackBindingEngine::RebindSubtreeRootObject(
    UIElement* subtreeRoot,
    ObjectRef newRootObject,
    RuntimeError* error) {
    if (!subtreeRoot) {
        return 0u;
    }
    std::size_t reboundCount = 0u;
    const std::size_t snapshotCount = trackABindings_.size();
    for (std::size_t bIdx = 0u; bIdx < snapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
        auto& binding = trackABindings_[bIdx];
        if (!binding.targetElement ||
            !IsDescendantOrSelf(binding.targetElement.Get(), subtreeRoot)) {
            continue;
        }
        for (auto& hopRef : binding.resolvedHopObjects) {
            if (hopRef.valid()) {
                if (binding.mode != BindingMode::OneTime) {
                    if (auto* oldHeap = domains_.heapForObject(hopRef)) {
                        oldHeap->removeUiObserver(hopRef);
                    }
                }
                hopRef = ObjectRef{};
            }
        }
        ReleaseCollectionObserver(binding);
        binding.rootObject = newRootObject;
        binding.initialized = false;
        if (newRootObject.valid()) {
            (void)EvaluateAndSyncChain(trackABindings_[bIdx], 0, error);
        } else {
            ApplyFallbackOrClear(trackABindings_[bIdx]);
        }
        ++reboundCount;
    }
    const std::size_t mbSnapshot = multiBindings_.size();
    for (std::size_t mIdx = 0u; mIdx < mbSnapshot && mIdx < multiBindings_.size(); ++mIdx) {
        auto& mb = multiBindings_[mIdx];
        if (!mb.targetElement || !IsDescendantOrSelf(mb.targetElement.Get(), subtreeRoot)) {
            continue;
        }
        ReleaseMultiBindingObservers(mb);
        auto setRootRec = [](auto& self, MultiBindingInstance& inst, ObjectRef nr) noexcept -> void {
            inst.rootObject = nr;
            inst.initialized = false;
            inst.lastSyncedOutput = UnifiedValue{};
            for (auto& sl : inst.sources) {
                if (sl.nestedMulti) {
                    self(self, *sl.nestedMulti, nr);
                }
            }
        };
        setRootRec(setRootRec, mb, newRootObject);
        (void)EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error);
        ++reboundCount;
    }
    if (animationEngine_) {
        animationEngine_->OnSubtreeDataContextRebound(subtreeRoot);
    }
    return reboundCount;
}

std::size_t DualTrackBindingEngine::UnbindSubtree(const UIElement* subtreeRoot) noexcept {
    if (!subtreeRoot) {
        return 0u;
    }
    auto isInSubtree = [&](const UIElement* candidate) noexcept -> bool {
        for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
            if (cur == subtreeRoot) {
                return true;
            }
        }
        return false;
    };

    std::size_t removedCount = 0u;
    std::size_t writeIdx = 0u;
    for (std::size_t i = 0u; i < trackABindings_.size(); ++i) {
        auto& b = trackABindings_[i];
        if (b.targetElement && isInSubtree(b.targetElement.Get())) {
            if (b.targetElement->BoundTwoWayEngine() == this) {
                b.targetElement->BindTwoWayEngine(nullptr);
            }
            ReleaseCollectionObserver(b);
            for (const auto& hopRef : b.resolvedHopObjects) {
                if (hopRef.valid() && b.mode != BindingMode::OneTime) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                }
            }
            ++removedCount;
            continue;
        }
        if (writeIdx != i) {
            trackABindings_[writeIdx] = std::move(b);
        }
        ++writeIdx;
    }
    trackABindings_.resize(writeIdx);

    std::size_t ebWriteIdx = 0u;
    for (std::size_t i = 0u; i < elementBindings_.size(); ++i) {
        auto& eb = elementBindings_[i];
        if (eb.targetElement && isInSubtree(eb.targetElement.Get())) {
            if (eb.targetElement->BoundTwoWayEngine() == this) {
                eb.targetElement->BindTwoWayEngine(nullptr);
            }
            ++removedCount;
            continue;
        }
        if (eb.cachedSourceElement && isInSubtree(eb.cachedSourceElement)) {
            eb.cachedSourceElement = nullptr;
        }
        if (ebWriteIdx != i) {
            elementBindings_[ebWriteIdx] = std::move(eb);
        }
        ++ebWriteIdx;
    }
    elementBindings_.resize(ebWriteIdx);

    std::size_t mbWriteIdx = 0u;
    for (std::size_t i = 0u; i < multiBindings_.size(); ++i) {
        auto& mb = multiBindings_[i];
        if (mb.targetElement && isInSubtree(mb.targetElement.Get())) {
            if (mb.targetElement->BoundTwoWayEngine() == this) {
                mb.targetElement->BindTwoWayEngine(nullptr);
            }
            ReleaseMultiBindingObservers(mb);
            ++removedCount;
            continue;
        }
        for (auto& sl : mb.sources) {
            if (sl.cachedSourceElement && isInSubtree(sl.cachedSourceElement)) {
                sl.cachedSourceElement = nullptr;
            }
        }
        if (mbWriteIdx != i) {
            multiBindings_[mbWriteIdx] = std::move(mb);
        }
        ++mbWriteIdx;
    }
    multiBindings_.resize(mbWriteIdx);
    return removedCount;
}

bool DualTrackBindingEngine::UnbindSlot(std::uint16_t bindingSlotId) noexcept {
    if (bindingSlotId == 0u) {
        return false;
    }
    for (auto it = trackABindings_.begin(); it != trackABindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            if (it->targetElement && it->targetElement->BoundTwoWayEngine() == this) {
                it->targetElement->BindTwoWayEngine(nullptr);
            }
            for (const auto& hopRef : it->resolvedHopObjects) {
                if (hopRef.valid() && it->mode != BindingMode::OneTime) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                }
            }
            trackABindings_.erase(it);
            return true;
        }
    }
    for (auto it = elementBindings_.begin(); it != elementBindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            if (it->targetElement && it->targetElement->BoundTwoWayEngine() == this) {
                it->targetElement->BindTwoWayEngine(nullptr);
            }
            elementBindings_.erase(it);
            return true;
        }
    }
    for (auto it = multiBindings_.begin(); it != multiBindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            if (it->targetElement && it->targetElement->BoundTwoWayEngine() == this) {
                it->targetElement->BindTwoWayEngine(nullptr);
            }
            ReleaseMultiBindingObservers(*it);
            multiBindings_.erase(it);
            return true;
        }
    }
    return false;
}

std::size_t DualTrackBindingEngine::FlushTrackA(RuntimeError* error) {
    if (trackABindings_.empty() && elementBindings_.empty() && multiBindings_.empty()) {
        return 0;
    }
    std::size_t updatedCount = 0;

    auto processHeap = [&](ManagedHeap& heap) {
        auto dirtyObjects = heap.consumeDirtyObjects();
        for (const auto& dirtyObj : dirtyObjects) {
            const std::uint64_t mask = heap.dirtyFieldMask(dirtyObj);
            if (mask == 0ULL) {
                continue;
            }
            // Clear dirty mask BEFORE EvaluateAndSyncChain so any secondary mutations
            // triggered inside SetValue / OnPropertyChanged are preserved for subsequent flushes!
            heap.clearDirtyFieldMask(dirtyObj);
            // Index-based scan so nested BindChain calls during SetValue never invalidate iterators!
            const std::size_t bindingSnapshotCount = trackABindings_.size();
            for (std::size_t bIdx = 0; bIdx < bindingSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
                if (trackABindings_[bIdx].mode == BindingMode::OneTime &&
                    trackABindings_[bIdx].initialized) {
                    continue;
                }
                const std::size_t hopCount = trackABindings_[bIdx].hops.size();
                for (std::size_t h = 0; h < hopCount; ++h) {
                    if (h >= trackABindings_[bIdx].resolvedHopObjects.size() ||
                        !(trackABindings_[bIdx].resolvedHopObjects[h] == dirtyObj)) {
                        continue;
                    }
                    const std::uint8_t bitIdx = trackABindings_[bIdx].hops[h].observableBitIndex;
                    const bool bitMatched = (bitIdx < 64u)
                        ? ((mask & (1ULL << bitIdx)) != 0ULL)
                        : true;
                    if (bitMatched) {
                        if (EvaluateAndSyncChain(trackABindings_[bIdx], h, error)) {
                            ++updatedCount;
                        }
                        break;
                    }
                }
            }

            const std::size_t multiSnapshotCount = multiBindings_.size();
            for (std::size_t mIdx = 0u; mIdx < multiSnapshotCount && mIdx < multiBindings_.size(); ++mIdx) {
                if (MultiBindingMatchesDirtyObj(multiBindings_[mIdx], dirtyObj, mask)) {
                    if (EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error)) {
                        ++updatedCount;
                    }
                }
            }
        }
    };

    processHeap(domains_.simHeap());
    processHeap(domains_.uiHeap());

    // Sync ElementName / RelativeSource UIElement-to-UIElement bindings & MultiBindings with element sources
    for (int elemPass = 0; elemPass < 2; ++elemPass) {
        std::size_t passDelta = 0u;
        const std::size_t ebSnapshotCount = elementBindings_.size();
        for (std::size_t eIdx = 0; eIdx < ebSnapshotCount && eIdx < elementBindings_.size(); ++eIdx) {
            if (elementBindings_[eIdx].mode == BindingMode::OneTime && elementBindings_[eIdx].initialized) {
                continue;
            }
            if (EvaluateAndSyncElementBinding(elementBindings_[eIdx])) {
                ++updatedCount;
                ++passDelta;
            }
        }
        const std::size_t mbSnapshotCount = multiBindings_.size();
        for (std::size_t mIdx = 0u; mIdx < mbSnapshotCount && mIdx < multiBindings_.size(); ++mIdx) {
            if (!multiBindings_[mIdx].hasElementSource) {
                continue;
            }
            if (multiBindings_[mIdx].mode == BindingMode::OneTime && multiBindings_[mIdx].initialized) {
                continue;
            }
            if (EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error)) {
                ++updatedCount;
                ++passDelta;
            }
        }
        if (passDelta == 0u) {
            break;
        }
    }

    // Only scan active bindings for GC-reclaimed objects when a GC cycle actually occurred,
    // preserving O(1) cost on clean frames while guaranteeing automatic fallback on object death.
    const auto& simStats = domains_.simHeap().statistics();
    const auto& uiStats = domains_.uiHeap().statistics();
    const std::uint64_t gcEpoch =
        simStats.objectsReclaimed + simStats.collectionsCompleted + simStats.youngCollections +
        uiStats.objectsReclaimed + uiStats.collectionsCompleted + uiStats.youngCollections;

    if (gcEpoch != lastObservedGcEpoch_) {
        lastObservedGcEpoch_ = gcEpoch;
        const std::size_t gcSnapshotCount = trackABindings_.size();
        for (std::size_t bIdx = 0u; bIdx < gcSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
            auto& binding = trackABindings_[bIdx];
            if (!binding.initialized || !binding.targetElement ||
                binding.mode == BindingMode::OneTime) {
                continue;
            }
            bool anyDead = false;
            for (std::size_t h = 0; h < binding.resolvedHopObjects.size(); ++h) {
                const ObjectRef ref = binding.resolvedHopObjects[h];
                if (!ref.valid()) {
                    continue;
                }
                auto* heap = domains_.heapForObject(ref);
                if (!heap || !heap->isAlive(ref)) {
                    anyDead = true;
                    for (std::size_t clearIdx = h; clearIdx < binding.resolvedHopObjects.size(); ++clearIdx) {
                        const ObjectRef oldRef = binding.resolvedHopObjects[clearIdx];
                        if (oldRef.valid()) {
                            if (auto* oldHeap = domains_.heapForObject(oldRef)) {
                                oldHeap->removeUiObserver(oldRef);
                            }
                            binding.resolvedHopObjects[clearIdx] = ObjectRef{};
                        }
                    }
                    break;
                }
            }
            if (anyDead) {
                ReleaseCollectionObserver(trackABindings_[bIdx]);
                ApplyFallbackOrClear(trackABindings_[bIdx]);
                ++updatedCount;
            }
        }
    }

    if (updatedCount > 0u && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return updatedCount;
}

bool DualTrackBindingEngine::PushTwoWayToSource(
    const UIElement* targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    if (!targetElement) {
        return false;
    }

    bool anyPushed = false;

    // 1. Push TwoWay ElementName / RelativeSource UIElement-to-UIElement bindings
    const std::size_t ebSnapshotCount = elementBindings_.size();
    for (std::size_t eIdx = 0u; eIdx < ebSnapshotCount && eIdx < elementBindings_.size(); ++eIdx) {
        auto& eb = elementBindings_[eIdx];
        if (eb.targetElement.Get() != targetElement ||
            eb.targetPropertyId != targetPropertyId ||
            eb.mode != BindingMode::TwoWay) {
            continue;
        }
        if (!eb.cachedSourceElement) {
            (void)EvaluateAndSyncElementBinding(eb);
        }
        if (eb.cachedSourceElement && eb.cachedSourceElement != targetElement) {
            UnifiedValue uiVal = targetElement->GetValue(targetPropertyId);
            if (uiVal.isTraceable()) {
                rs_rt::pinValueInPools(uiVal);
            }
            eb.lastSyncedValue = uiVal;
            eb.initialized = true;
            if (eb.converter != ValueConverterKind::None) {
                uiVal = ApplySingleValueConversion(
                    eb.converter,
                    eb.converterKey,
                    eb.converterParameter,
                    eb.converterSymbolId,
                    eb.convertBackSymbolId,
                    uiVal,
                    true);
            }
            UIElement* cachedSrc = eb.cachedSourceElement;
            const std::uint16_t srcProp = eb.sourcePropertyId;
            const UnifiedValue curSrcVal = ReadElementPropertyOrState(cachedSrc, srcProp);
            if (!BindingValuesEqual(curSrcVal, uiVal) &&
                cachedSrc->SetValue(srcProp, uiVal, ValueSourceRank::Local)) {
                (void)cachedSrc->NotifyTwoWayPropertyMutated(srcProp, error);
                anyPushed = true;
            }
        }
    }

    // 2. Push TwoWay ViewModel ObjectRef bindings
    const std::size_t bSnapshotCount = trackABindings_.size();
    for (std::size_t bIdx = 0u; bIdx < bSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
        auto& binding = trackABindings_[bIdx];
        if (binding.targetElement.Get() != targetElement ||
            binding.targetPropertyId != targetPropertyId ||
            binding.mode != BindingMode::TwoWay ||
            binding.hops.empty()) {
            continue;
        }

        const std::size_t lastHopIdx = binding.hops.size() - 1;
        if (lastHopIdx >= binding.resolvedHopObjects.size()) {
            continue;
        }
        const ObjectRef targetObj = binding.resolvedHopObjects[lastHopIdx];
        if (!targetObj.valid()) {
            continue;
        }

        const auto& lastHop = binding.hops[lastHopIdx];
        const PropertySlotHandle slot = lastHop.ToSlotHandle();

        UnifiedValue uiValue = targetElement->GetValue(targetPropertyId);
        if (binding.converter != ValueConverterKind::None) {
            uiValue = ApplySingleValueConversion(
                binding.converter,
                binding.converterKey,
                binding.converterParameter,
                binding.converterSymbolId,
                binding.convertBackSymbolId,
                uiValue,
                true);
        }

        // Coerce converted value to match target ViewModel slot's runtime storage kind when needed
        UnifiedValue curSlotVal{};
        const bool hasCurSlotVal = slot.valid() && domains_.readPropertySlotFast(
            DomainKind::Ui, targetObj, slot, curSlotVal, nullptr, uiInterpreter_);

        if (hasCurSlotVal) {
            if (std::holds_alternative<FloatValue>(curSlotVal) && !std::holds_alternative<FloatValue>(uiValue)) {
                float fv = 0.0f;
                if (TryGetFloat(uiValue, fv)) {
                    uiValue = MakeFloat(fv);
                } else if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
                    uiValue = MakeFloat(ParseStackFloat32(sv->view(), 0.0f));
                }
            } else if (std::holds_alternative<std::int64_t>(curSlotVal) && !std::holds_alternative<std::int64_t>(uiValue)) {
                float fv = 0.0f;
                if (TryGetFloat(uiValue, fv)) {
                    uiValue = value::FromInt(static_cast<std::int64_t>(std::llround(fv)));
                } else if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
                    uiValue = value::FromInt(static_cast<std::int64_t>(std::llround(ParseStackFloat32(sv->view(), 0.0f))));
                }
            } else if (std::holds_alternative<bool>(curSlotVal) && !std::holds_alternative<bool>(uiValue)) {
                if (const auto* iv = std::get_if<std::int64_t>(&uiValue)) {
                    uiValue = (*iv != 0);
                } else {
                    float fv = 0.0f;
                    if (TryGetFloat(uiValue, fv)) uiValue = (std::fabs(fv) > 1e-6f);
                }
            }
        }

        // Convert inline StringValue into a RealScript heap String ObjectRef when writing to a String slot/setter
        if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
            if (auto* heap = domains_.heapForObject(targetObj)) {
                bool shouldAllocateHeapString = (slot.setterSymbolId != 0 || slot.isSimCommand);
                if (!shouldAllocateHeapString && hasCurSlotVal) {
                    if (std::holds_alternative<ObjectRef>(curSlotVal) ||
                        std::holds_alternative<NullObject>(curSlotVal)) {
                        shouldAllocateHeapString = true;
                    }
                }
                if (shouldAllocateHeapString) {
                    if (auto strObj = heap->allocateString(sv->str())) {
                        uiValue = *strObj;
                    }
                }
            }
        }

        // If the terminal property setter is marked [SimCommand] and targetObj lives on SimHeap,
        // automatically marshal as a SimCommandEnvelope into ExecutionDomainContext!
        if (slot.isSimCommand && slot.setterSymbolId != 0 &&
            targetObj.heapId == static_cast<std::uint32_t>(domains_.simHeap().heapId())) {
            domains_.enqueueSimCommand(SimCommandEnvelope{
                slot.setterSymbolId,
                targetObj,
                {uiValue},
                0ULL});
            anyPushed = true;
            continue;
        }

        if (domains_.writePropertySlotFast(
                DomainKind::Ui,
                targetObj,
                slot,
                uiValue,
                uiInterpreter_,
                error)) {
            anyPushed = true;
        }
    }

    if (anyPushed && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return anyPushed;
}

std::size_t DualTrackBindingEngine::ApplyHotReloadMigration(
    const ProgramImage& newProgram,
    const std::vector<HotReloadMigrationPair>& migratedRefs,
    RuntimeError* error) {
    program_ = &newProgram;
    domains_.registerProgramObservableMetadata(newProgram);
    std::size_t migratedCount = 0;

    auto mapObjectRef = [&](ObjectRef& ref) -> bool {
        for (const auto& pair : migratedRefs) {
            if (ref == pair.oldRef) {
                ref = pair.newRef;
                return true;
            }
        }
        return false;
    };

    const std::size_t bSnapshotCount = trackABindings_.size();
    for (std::size_t bIdx = 0u; bIdx < bSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
        auto& binding = trackABindings_[bIdx];
        ReleaseCollectionObserver(binding);
        binding.initialized = false;
        bool touched = mapObjectRef(binding.rootObject);
        // Unbind old hop observers and clear resolvedHopObjects BEFORE EvaluateAndSyncChain
        // so migrated newRef instances properly acquire addUiObserver() and oldRefs release theirs.
        for (auto& hopObj : binding.resolvedHopObjects) {
            if (hopObj.valid()) {
                for (const auto& pair : migratedRefs) {
                    if (hopObj == pair.oldRef) {
                        touched = true;
                        break;
                    }
                }
                if (binding.mode != BindingMode::OneTime) {
                    if (auto* oldHeap = domains_.heapForObject(hopObj)) {
                        oldHeap->removeUiObserver(hopObj);
                    }
                }
                hopObj = ObjectRef{};
            }
        }
        // Force re-resolution of all hops against newProgram
        for (auto& hop : binding.hops) {
            hop.declaringTypeId = 0;
        }
        if (EvaluateAndSyncChain(trackABindings_[bIdx], 0, error) || touched) {
            ++migratedCount;
        }
    }

    const std::size_t mbSnapshotCount = multiBindings_.size();
    for (std::size_t mIdx = 0u; mIdx < mbSnapshotCount && mIdx < multiBindings_.size(); ++mIdx) {
        auto& mb = multiBindings_[mIdx];
        ReleaseMultiBindingObservers(mb);
        auto migrateMultiRec = [&](auto& self, MultiBindingInstance& inst) -> bool {
            bool anyTouched = mapObjectRef(inst.rootObject);
            inst.initialized = false;
            inst.lastSyncedOutput = UnifiedValue{};
            for (auto& sl : inst.sources) {
                if (sl.nestedMulti) {
                    if (self(self, *sl.nestedMulti)) anyTouched = true;
                    continue;
                }
                for (auto& hop : sl.hops) {
                    hop.declaringTypeId = 0;
                }
            }
            return anyTouched;
        };
        const bool mbTouched = migrateMultiRec(migrateMultiRec, mb);
        if (EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error) || mbTouched) {
            ++migratedCount;
        }
    }

    return migratedCount;
}

const NativeRawColumnView* DualTrackBindingEngine::ResolveChannelView(
    const EcsColumnBindingChannel& ch) const noexcept {
    if (ch.columnArrayRef.valid()) {
        if (const auto* heap = domains_.heapForObject(ch.columnArrayRef)) {
            if (const auto* v = heap->rawColumnView(ch.columnArrayRef)) {
                return v;
            }
        }
    }
    if (ch.rawView.basePtr != nullptr) {
        return &ch.rawView;
    }
    return nullptr;
}

std::size_t DualTrackBindingEngine::BindEcsColumn(EcsColumnBindingChannel channel) {
    if (channel.componentTypeId == 0 && !channel.componentTypeName.empty()) {
        std::string canonical = channel.componentTypeName;
        if (canonical.find("::") == std::string::npos) {
            if (const std::size_t lastDot = canonical.rfind('.'); lastDot != std::string::npos) {
                canonical.replace(lastDot, 1, "::");
            }
        }
        channel.componentTypeId = rs_sem::stableTypeId(canonical);
    }

    const bool incomingHasDataView =
        channel.columnArrayRef.valid() || (channel.rawView.basePtr != nullptr);

    auto mergeIntoExisting = [&](EcsColumnBindingChannel& existing) {
        if (channel.columnArrayRef.valid()) existing.columnArrayRef = channel.columnArrayRef;
        if (channel.rawView.basePtr) existing.rawView = channel.rawView;
        if (channel.elementStride != 0) existing.elementStride = channel.elementStride;
        if (incomingHasDataView || channel.fieldByteOffset != 0) {
            existing.fieldByteOffset = channel.fieldByteOffset;
        }
        if (incomingHasDataView || channel.scalarKind != EcsFieldScalarKind::Float32) {
            existing.scalarKind = channel.scalarKind;
        }
        if (incomingHasDataView || channel.scaleFactor != 1.0f) {
            existing.scaleFactor = channel.scaleFactor;
        }
        if (!channel.componentTypeName.empty()) existing.componentTypeName = channel.componentTypeName;
        if (!channel.fieldName.empty()) existing.fieldName = channel.fieldName;
        if (!channel.targetElements.empty()) {
            if (existing.targetElements.size() < channel.targetElements.size()) {
                existing.targetElements.resize(channel.targetElements.size());
            }
            for (std::size_t r = 0; r < channel.targetElements.size(); ++r) {
                if (channel.targetElements[r]) {
                    existing.targetElements[r] = channel.targetElements[r];
                }
            }
        }
    };

    if (channel.componentTypeId != 0) {
        // Pass 1: exact fieldName match (or both unnamed with identical fieldByteOffset)
        for (std::size_t i = 0; i < trackBChannels_.size(); ++i) {
            auto& existing = trackBChannels_[i];
            if (existing.componentTypeId != channel.componentTypeId ||
                existing.targetPropertyId != channel.targetPropertyId) {
                continue;
            }
            if ((!existing.fieldName.empty() && existing.fieldName == channel.fieldName) ||
                (existing.fieldName.empty() && channel.fieldName.empty() &&
                 existing.fieldByteOffset == channel.fieldByteOffset)) {
                mergeIntoExisting(existing);
                return i;
            }
        }
        // Pass 2: pair an unnamed data-view registration with a XAML target-element placeholder
        // ONLY when one side has no data view yet (prevents colliding distinct fields on same component).
        for (std::size_t i = 0; i < trackBChannels_.size(); ++i) {
            auto& existing = trackBChannels_[i];
            if (existing.componentTypeId != channel.componentTypeId ||
                existing.targetPropertyId != channel.targetPropertyId) {
                continue;
            }
            const bool existingHasDataView =
                existing.columnArrayRef.valid() || (existing.rawView.basePtr != nullptr);
            if ((existing.fieldName.empty() || channel.fieldName.empty()) &&
                (existingHasDataView != incomingHasDataView)) {
                mergeIntoExisting(existing);
                return i;
            }
        }
    }

    const std::size_t idx = trackBChannels_.size();
    trackBChannels_.push_back(std::move(channel));
    return idx;
}

std::size_t DualTrackBindingEngine::BindEcsColumn(
    SymbolId componentTypeId,
    std::string_view fieldName,
    std::size_t rowIndex,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId) {
    // Pass 1: exact fieldName match
    for (std::size_t i = 0; i < trackBChannels_.size(); ++i) {
        auto& ch = trackBChannels_[i];
        if (ch.componentTypeId == componentTypeId &&
            ch.targetPropertyId == targetPropertyId &&
            ch.fieldName == fieldName) {
            if (rowIndex >= ch.targetElements.size()) {
                ch.targetElements.resize(rowIndex + 1);
            }
            ch.targetElements[rowIndex] = targetElement;
            return i;
        }
    }
    // Pass 2: attach to an unnamed C++ data-view channel only when unambiguous (single unnamed match)
    std::size_t unnamedMatchIdx = trackBChannels_.size();
    std::size_t unnamedMatchCount = 0;
    for (std::size_t i = 0; i < trackBChannels_.size(); ++i) {
        const auto& ch = trackBChannels_[i];
        if (ch.componentTypeId == componentTypeId &&
            ch.targetPropertyId == targetPropertyId &&
            (ch.fieldName.empty() || fieldName.empty())) {
            unnamedMatchIdx = i;
            ++unnamedMatchCount;
        }
    }
    if (unnamedMatchCount == 1u) {
        auto& ch = trackBChannels_[unnamedMatchIdx];
        if (ch.fieldName.empty() && !fieldName.empty()) {
            ch.fieldName = std::string(fieldName);
        }
        if (rowIndex >= ch.targetElements.size()) {
            ch.targetElements.resize(rowIndex + 1);
        }
        ch.targetElements[rowIndex] = targetElement;
        return unnamedMatchIdx;
    }

    EcsColumnBindingChannel channel{};
    channel.componentTypeId = componentTypeId;
    channel.fieldName = std::string(fieldName);
    channel.targetPropertyId = targetPropertyId;
    channel.targetElements.resize(rowIndex + 1);
    channel.targetElements[rowIndex] = targetElement;
    trackBChannels_.push_back(std::move(channel));
    return trackBChannels_.size() - 1;
}

std::size_t DualTrackBindingEngine::FlushTrackB() noexcept {
    std::size_t updatedElements = 0;

    for (auto& ch : trackBChannels_) {
        if (ch.targetElements.empty()) {
            continue;
        }

        const NativeRawColumnView* view = nullptr;
        if (ch.columnArrayRef.valid()) {
            if (const auto* heap = domains_.heapForObject(ch.columnArrayRef)) {
                view = heap->rawColumnView(ch.columnArrayRef);
            }
        }
        if (!view && ch.rawView.basePtr != nullptr && ch.rawView.count > 0) {
            view = &ch.rawView;
        }
        if (!view || !view->basePtr || view->count == 0) {
            continue;
        }

        const std::size_t stride = (ch.elementStride != 0)
            ? ch.elementStride
            : view->strideBytes;
        if (stride == 0) {
            continue;
        }

        const auto* baseBytes = static_cast<const std::uint8_t*>(view->basePtr);
        const std::size_t limit = std::min(view->count, ch.targetElements.size());

        for (std::size_t i = 0; i < limit; ++i) {
            UIElement* elem = ch.targetElements[i].Get();
            if (!elem) {
                continue;
            }
            const auto* fieldPtr = baseBytes + (i * stride) + ch.fieldByteOffset;

            switch (ch.scalarKind) {
                case EcsFieldScalarKind::Float32: {
                    float raw = 0.0f;
                    std::memcpy(&raw, fieldPtr, sizeof(float));
                    const float scaled = raw * ch.scaleFactor;
                    if (ch.targetPropertyId == PropId_Width) {
                        elem->SetWidth(scaled);
                    } else if (ch.targetPropertyId == PropId_Height) {
                        elem->SetHeight(scaled);
                    } else if (ch.targetPropertyId == PropId_Opacity) {
                        elem->SetOpacity(scaled);
                    } else {
                        elem->SetValue(ch.targetPropertyId, MakeFloat(scaled), ValueSourceRank::Local);
                    }
                    ++updatedElements;
                    break;
                }
                case EcsFieldScalarKind::Int32: {
                    std::int32_t raw = 0;
                    std::memcpy(&raw, fieldPtr, sizeof(std::int32_t));
                    elem->SetValue(
                        ch.targetPropertyId,
                        value::FromInt(static_cast<std::int64_t>(raw)),
                        ValueSourceRank::Local);
                    ++updatedElements;
                    break;
                }
                case EcsFieldScalarKind::Int64: {
                    std::int64_t raw = 0;
                    std::memcpy(&raw, fieldPtr, sizeof(std::int64_t));
                    elem->SetValue(
                        ch.targetPropertyId,
                        value::FromInt(raw),
                        ValueSourceRank::Local);
                    ++updatedElements;
                    break;
                }
                case EcsFieldScalarKind::Float4: {
                    Float4Value f4{};
                    std::memcpy(&f4, fieldPtr, sizeof(Float4Value));
                    elem->SetValue(
                        ch.targetPropertyId,
                        UnifiedValue{f4},
                        ValueSourceRank::Local);
                    ++updatedElements;
                    break;
                }
            }
        }
    }

    return updatedElements;
}

} // namespace aero
