#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/animation/AnimationEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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
                binding.initialized = true;
                ApplyFallbackOrClear(binding);
                return true;
            }
            currentObj = *nextRef;
        } else {
            if (std::holds_alternative<NullObject>(hopVal) &&
                binding.converter == ValueConverterKind::None) {
                binding.initialized = true;
                ApplyFallbackOrClear(binding);
                return true;
            }
            if (const auto* strRef = std::get_if<ObjectRef>(&hopVal)) {
                if (strRef->kind == ObjectKind::String) {
                    if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                        hopVal = value::FromString(std::string(*sv));
                    }
                }
            }
            if (binding.converter != ValueConverterKind::None) {
                hopVal = ApplyBuiltinValueConverter(binding.converter, hopVal, false);
            }
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
        val = ApplyBuiltinValueConverter(eb.converter, val, false);
    } else if ((std::holds_alternative<std::monostate>(val) || std::holds_alternative<NullObject>(val)) &&
               !std::holds_alternative<std::monostate>(eb.fallbackValue)) {
        val = eb.fallbackValue;
    }

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
        binding.rootObject = newRootObject;
        binding.initialized = false;
        if (newRootObject.valid()) {
            (void)EvaluateAndSyncChain(trackABindings_[bIdx], 0, error);
        } else {
            ApplyFallbackOrClear(trackABindings_[bIdx]);
        }
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
    return removedCount;
}

std::size_t DualTrackBindingEngine::FlushTrackA(RuntimeError* error) {
    if (trackABindings_.empty() && elementBindings_.empty()) {
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
        }
    };

    processHeap(domains_.simHeap());
    processHeap(domains_.uiHeap());

    // Sync ElementName / RelativeSource UIElement-to-UIElement bindings (0 heap allocs when clean)
    const std::size_t ebSnapshotCount = elementBindings_.size();
    for (std::size_t eIdx = 0; eIdx < ebSnapshotCount && eIdx < elementBindings_.size(); ++eIdx) {
        if (elementBindings_[eIdx].mode == BindingMode::OneTime && elementBindings_[eIdx].initialized) {
            continue;
        }
        if (EvaluateAndSyncElementBinding(elementBindings_[eIdx])) {
            ++updatedCount;
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
                uiVal = ApplyBuiltinValueConverter(eb.converter, uiVal, true);
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
            uiValue = ApplyBuiltinValueConverter(binding.converter, uiValue, true);
        }
        // Convert inline StringValue into a RealScript heap String ObjectRef when writing to a String slot/setter
        if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
            if (auto* heap = domains_.heapForObject(targetObj)) {
                bool shouldAllocateHeapString = (slot.setterSymbolId != 0 || slot.isSimCommand);
                if (!shouldAllocateHeapString && slot.valid()) {
                    UnifiedValue curSlotVal{};
                    if (domains_.readPropertySlotFast(
                            DomainKind::Ui, targetObj, slot, curSlotVal, nullptr, uiInterpreter_)) {
                        if (std::holds_alternative<ObjectRef>(curSlotVal) ||
                            std::holds_alternative<NullObject>(curSlotVal)) {
                            shouldAllocateHeapString = true;
                        }
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
