#include "aero/binding/DualTrackBindingEngine.hpp"

#include <algorithm>
#include <cstring>

namespace aero {

DualTrackBindingEngine::DualTrackBindingEngine(
    ExecutionDomainContext& domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter) noexcept
    : domains_(domains),
      program_(program),
      uiInterpreter_(uiInterpreter) {}

DualTrackBindingEngine::~DualTrackBindingEngine() noexcept {
    for (auto& binding : trackABindings_) {
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
}

void DualTrackBindingEngine::ApplyFallbackOrClear(ReactiveBindingInstance& binding) const {
    if (!binding.targetElement) {
        return;
    }
    std::uint16_t animTrack = 0xFFFFu;
    if (binding.targetPropertyId >= PropId_FirstSparse) {
        if (const auto* entry = binding.targetElement->FindCompactEntry(binding.targetPropertyId)) {
            animTrack = entry->animationTrackId;
        }
    }
    if (!std::holds_alternative<std::monostate>(binding.fallbackValue)) {
        binding.targetElement->SetValue(
            binding.targetPropertyId,
            binding.fallbackValue,
            ValueSourceRank::Local);
        if (binding.targetPropertyId >= PropId_FirstSparse) {
            binding.targetElement->SetPropertyMetadata(
                binding.targetPropertyId,
                binding.bindingSlotId,
                animTrack);
        }
    } else {
        binding.targetElement->ClearValue(binding.targetPropertyId);
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

    for (std::size_t h = startHopIndex; h < binding.hops.size(); ++h) {
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

        if (h + 1 < binding.hops.size()) {
            const auto* nextRef = std::get_if<ObjectRef>(&hopVal);
            if (!nextRef || !nextRef->valid()) {
                unbindHopRange(h + 1);
                ApplyFallbackOrClear(binding);
                binding.initialized = true;
                return true;
            }
            currentObj = *nextRef;
        } else {
            if (std::holds_alternative<NullObject>(hopVal)) {
                ApplyFallbackOrClear(binding);
                binding.initialized = true;
                return true;
            }
            if (const auto* strRef = std::get_if<ObjectRef>(&hopVal)) {
                if (strRef->kind == ObjectKind::String) {
                    if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                        hopVal = value::FromString(std::string(*sv));
                    }
                }
            }
            binding.targetElement->SetValue(
                binding.targetPropertyId,
                hopVal,
                ValueSourceRank::Local);
            if (binding.targetPropertyId >= PropId_FirstSparse) {
                const auto* entry = binding.targetElement->FindCompactEntry(binding.targetPropertyId);
                if (!entry || entry->bindingSlotId == 0xFFFFu) {
                    const std::uint16_t animTrack = entry ? entry->animationTrackId : 0xFFFFu;
                    binding.targetElement->SetPropertyMetadata(
                        binding.targetPropertyId,
                        binding.bindingSlotId,
                        animTrack);
                }
            }
            binding.initialized = true;
        }
    }

    return true;
}

std::uint16_t DualTrackBindingEngine::BindChain(
    ObjectRef rootObject,
    const AxbCompiledBindingChain& compiledChain,
    const IntrusivePtr<UIElement>& targetElement,
    RuntimeError* error) {
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
    instance.rootObject = rootObject;
    instance.fallbackValue = compiledChain.fallbackValue;
    instance.hops = compiledChain.hops;
    instance.resolvedHopObjects.assign(compiledChain.hops.size(), ObjectRef{});
    instance.targetElement = targetElement;
    instance.targetPropertyId = compiledChain.targetPropertyId;

    if (instance.targetPropertyId >= PropId_FirstSparse) {
        targetElement->SetPropertyMetadata(instance.targetPropertyId, slotId, 0xFFFFu);
    }

    (void)EvaluateAndSyncChain(instance, 0, error);
    trackABindings_.push_back(std::move(instance));
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

std::size_t DualTrackBindingEngine::FlushTrackA(RuntimeError* error) {
    std::size_t updatedCount = 0;

    auto processHeap = [&](ManagedHeap& heap) {
        auto dirtyObjects = heap.consumeDirtyObjects();
        for (const auto& dirtyObj : dirtyObjects) {
            const std::uint64_t mask = heap.dirtyFieldMask(dirtyObj);
            if (mask == 0ULL) {
                continue;
            }
            for (auto& binding : trackABindings_) {
                if (binding.mode == BindingMode::OneTime && binding.initialized) {
                    continue;
                }
                for (std::size_t h = 0; h < binding.hops.size(); ++h) {
                    if (h >= binding.resolvedHopObjects.size() ||
                        !(binding.resolvedHopObjects[h] == dirtyObj)) {
                        continue;
                    }
                    const std::uint8_t bitIdx = binding.hops[h].observableBitIndex;
                    const bool bitMatched = (bitIdx < 64u)
                        ? ((mask & (1ULL << bitIdx)) != 0ULL)
                        : true;
                    if (bitMatched) {
                        if (EvaluateAndSyncChain(binding, h, error)) {
                            ++updatedCount;
                        }
                        break;
                    }
                }
            }
            heap.clearDirtyFieldMask(dirtyObj);
        }
    };

    processHeap(domains_.simHeap());
    processHeap(domains_.uiHeap());

    // Only scan active bindings for GC-reclaimed objects when a GC cycle actually occurred,
    // preserving O(1) cost on clean frames while guaranteeing automatic fallback on object death.
    const auto& simStats = domains_.simHeap().statistics();
    const auto& uiStats = domains_.uiHeap().statistics();
    const std::uint64_t gcEpoch =
        simStats.objectsReclaimed + simStats.collectionsCompleted + simStats.youngCollections +
        uiStats.objectsReclaimed + uiStats.collectionsCompleted + uiStats.youngCollections;

    if (gcEpoch != lastObservedGcEpoch_) {
        lastObservedGcEpoch_ = gcEpoch;
        for (auto& binding : trackABindings_) {
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
                ApplyFallbackOrClear(binding);
                ++updatedCount;
            }
        }
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

    for (auto& binding : trackABindings_) {
        if (binding.targetElement.Get() != targetElement ||
            binding.targetPropertyId != targetPropertyId ||
            binding.mode != BindingMode::TwoWay ||
            binding.hops.empty()) {
            continue;
        }

        const std::size_t lastHopIdx = binding.hops.size() - 1;
        if (lastHopIdx >= binding.resolvedHopObjects.size()) {
            return false;
        }
        const ObjectRef targetObj = binding.resolvedHopObjects[lastHopIdx];
        if (!targetObj.valid()) {
            return false;
        }

        const UnifiedValue uiValue = targetElement->GetValue(targetPropertyId);
        const auto& lastHop = binding.hops[lastHopIdx];
        const PropertySlotHandle slot = lastHop.ToSlotHandle();

        // If the terminal property setter is marked [SimCommand] and targetObj lives on SimHeap,
        // automatically marshal as a SimCommandEnvelope into ExecutionDomainContext!
        if (slot.isSimCommand && slot.setterSymbolId != 0 &&
            targetObj.heapId == static_cast<std::uint32_t>(domains_.simHeap().heapId())) {
            domains_.enqueueSimCommand(SimCommandEnvelope{
                slot.setterSymbolId,
                targetObj,
                {uiValue},
                0ULL});
            return true;
        }

        return domains_.writePropertySlotFast(
            DomainKind::Ui,
            targetObj,
            slot,
            uiValue,
            uiInterpreter_,
            error);
    }

    return false;
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

    for (auto& binding : trackABindings_) {
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
        if (EvaluateAndSyncChain(binding, 0, error) || touched) {
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
