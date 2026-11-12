#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/animation/AnimationEngine.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/UiResourceContext.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "realscript/bytecode/Bytecode.h"

namespace aero {

namespace {

struct LiveBindingEngineRegistry {
    std::mutex mutex;
    std::vector<DualTrackBindingEngine*> engines;

    static LiveBindingEngineRegistry& Instance() noexcept {
        static LiveBindingEngineRegistry reg;
        return reg;
    }

    void Register(DualTrackBindingEngine* eng) {
        if (!eng) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (std::find(engines.begin(), engines.end(), eng) == engines.end()) {
            engines.push_back(eng);
        }
    }

    void Unregister(DualTrackBindingEngine* eng) noexcept {
        if (!eng) return;
        std::lock_guard<std::mutex> lock(mutex);
        engines.erase(std::remove(engines.begin(), engines.end(), eng), engines.end());
    }

    [[nodiscard]] bool Contains(const DualTrackBindingEngine* eng) noexcept {
        if (!eng) return false;
        std::lock_guard<std::mutex> lock(mutex);
        return std::find(engines.begin(), engines.end(), eng) != engines.end();
    }

    template <typename Fn>
    void ForEachLocked(Fn&& fn) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        fn(engines);
    }
};

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
        case PropId_Content: {
            const UnifiedValue v = elem->GetValue(PropId_Content);
            if (!std::holds_alternative<std::monostate>(v)) {
                return v;
            }
            return elem->GetValue(PropId_Text);
        }
        case PropId_Text: {
            const UnifiedValue v = elem->GetValue(PropId_Text);
            if (!std::holds_alternative<std::monostate>(v)) {
                return v;
            }
            return elem->GetValue(PropId_Content);
        }
        case PropId_Header: {
            const UnifiedValue v = elem->GetValue(PropId_Header);
            if (std::holds_alternative<StringValue>(v)) {
                return v;
            }
            if (const auto* tvi = elem->DynamicCast<TreeViewItem>()) {
                const std::string hdr = tvi->GetHeader();
                if (!hdr.empty()) {
                    return value::FromString(hdr);
                }
            }
            return v;
        }
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
    EnumValue ae{};
    EnumValue be{};
    if (a.tryGetEnum(ae) && b.tryGetEnum(be)) {
        return ae == be;
    }
    if (const auto* ai = std::get_if<std::int64_t>(&a)) {
        if (const auto* bi = std::get_if<std::int64_t>(&b)) return *ai == *bi;
    }
    NativeHandle ah{};
    NativeHandle bh{};
    if (a.tryGetNativeHandle(ah) && b.tryGetNativeHandle(bh)) {
        return ah == bh;
    }
    float af = 0.0f;
    float bf = 0.0f;
    if (TryGetFloat(a, af) && TryGetFloat(b, bf) &&
        !std::holds_alternative<bool>(a) && !std::holds_alternative<bool>(b) &&
        !std::holds_alternative<std::int64_t>(a) && !std::holds_alternative<std::int64_t>(b) &&
        !std::holds_alternative<EnumValue>(a) && !std::holds_alternative<EnumValue>(b)) {
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

[[nodiscard]] const rs_sem::TypeSymbol* FindTypeSymbolByRawName(
    const ProgramImage* program,
    std::string_view rawTypeName) noexcept {
    if (!program || rawTypeName.empty()) {
        return nullptr;
    }
    std::string canonical(rawTypeName);
    for (std::size_t i = 0u; i < canonical.size(); ++i) {
        if (canonical[i] == '.') {
            canonical.replace(i, 1u, "::");
            ++i;
        }
    }
    const rs_sem::TypeSymbol* shortMatch = nullptr;
    const std::string_view shortName = ShortTypeNameOf(canonical);
    for (const auto& mod : program->modules()) {
        for (const auto& t : mod.types) {
            const std::string canon = rs_sem::canonicalTypeName(t);
            if (canon == canonical || t.name == canonical) {
                return &t;
            }
            if (!shortMatch && t.name == shortName) {
                shortMatch = &t;
            }
        }
    }
    return shortMatch;
}

void ResolveDynamicHopTargetType(
    const ProgramImage* program,
    SymbolId runtimeTypeId,
    AxbCompiledHop& hop) noexcept {
    if (!program || runtimeTypeId == 0 || hop.targetTypeId != 0) {
        return;
    }
    std::string memberTypeName;
    for (SymbolId curId = runtimeTypeId; curId != 0;) {
        const rs_sem::TypeSymbol* tSym = program->resolveType(curId);
        if (!tSym) {
            break;
        }
        for (const auto& p : tSym->properties) {
            if (p.name == hop.memberName) {
                memberTypeName = p.typeName;
                break;
            }
        }
        if (!memberTypeName.empty()) {
            break;
        }
        for (const auto& f : tSym->fields) {
            if (f.name == hop.memberName) {
                memberTypeName = f.typeName;
                break;
            }
        }
        if (!memberTypeName.empty()) {
            break;
        }
        curId = (tSym->baseTypeId != curId) ? tSym->baseTypeId : 0;
    }
    if (memberTypeName.empty()) {
        return;
    }
    const rs_sem::TypeSymbol* memberType = FindTypeSymbolByRawName(program, memberTypeName);
    if (hop.collectionIndex >= 0) {
        std::string elemTypeName;
        if (memberType) {
            for (const auto& f : memberType->fields) {
                if (f.name == "_items") {
                    if (f.typeName.size() > 2u &&
                        f.typeName.substr(f.typeName.size() - 2u) == "[]") {
                        elemTypeName = f.typeName.substr(0u, f.typeName.size() - 2u);
                    }
                    break;
                }
            }
        }
        if (elemTypeName.empty()) {
            if (memberTypeName.size() > 2u &&
                memberTypeName.substr(memberTypeName.size() - 2u) == "[]") {
                elemTypeName = memberTypeName.substr(0u, memberTypeName.size() - 2u);
            } else if (const auto lt = memberTypeName.find('<');
                       lt != std::string::npos && memberTypeName.back() == '>') {
                elemTypeName = memberTypeName.substr(lt + 1u, memberTypeName.size() - lt - 2u);
            }
        }
        memberType = FindTypeSymbolByRawName(program, elemTypeName);
    }
    if (memberType) {
        hop.targetTypeId = memberType->id;
        hop.targetIsEnum = (memberType->kind == rs_sem::TypeKind::Enum);
    }
}

void ReleaseIndexedArrayObserversFrom(
    ExecutionDomainContext& domains,
    std::vector<ObservedIndexedArray>& list,
    std::size_t fromHopIndex,
    bool observeEnabled) noexcept {
    std::size_t writeIdx = 0u;
    for (std::size_t i = 0u; i < list.size(); ++i) {
        auto& entry = list[i];
        if (entry.hopIndex >= fromHopIndex) {
            if (observeEnabled && entry.wrapperRef.valid()) {
                if (auto* heap = domains.heapForObject(entry.wrapperRef)) {
                    heap->removeUiObserver(entry.wrapperRef);
                }
            }
            continue;
        }
        if (writeIdx != i) {
            list[writeIdx] = entry;
        }
        ++writeIdx;
    }
    list.resize(writeIdx);
}

void UpsertIndexedArrayObserver(
    ExecutionDomainContext& domains,
    std::vector<ObservedIndexedArray>& list,
    std::size_t hopIndex,
    ObjectRef backingArr,
    ObjectRef wrapperRef,
    bool observeEnabled) {
    if (!backingArr.valid()) {
        ReleaseIndexedArrayObserversFrom(domains, list, hopIndex, observeEnabled);
        return;
    }
    auto* heap = domains.heapForObject(backingArr);
    if (!heap) {
        return;
    }
    if (observeEnabled) {
        heap->enableArrayChangeTracking(backingArr);
    }
    const std::uint64_t ver = heap->arrayMutationVersion(backingArr);
    for (auto& entry : list) {
        if (entry.hopIndex == hopIndex) {
            if (!(entry.wrapperRef == wrapperRef)) {
                if (observeEnabled && entry.wrapperRef.valid()) {
                    if (auto* oldHeap = domains.heapForObject(entry.wrapperRef)) {
                        oldHeap->removeUiObserver(entry.wrapperRef);
                    }
                }
                entry.wrapperRef = wrapperRef;
                if (observeEnabled && wrapperRef.valid()) {
                    if (auto* wHeap = domains.heapForObject(wrapperRef)) {
                        wHeap->addUiObserver(wrapperRef);
                    }
                }
            }
            entry.arrayRef = backingArr;
            entry.lastSeenVersion = ver;
            return;
        }
    }
    if (observeEnabled && wrapperRef.valid()) {
        if (auto* wHeap = domains.heapForObject(wrapperRef)) {
            wHeap->addUiObserver(wrapperRef);
        }
    }
    list.push_back(ObservedIndexedArray{backingArr, wrapperRef, hopIndex, ver});
}

[[nodiscard]] bool HasIndexedArrayMutation(
    const ExecutionDomainContext& domains,
    const std::vector<ObservedIndexedArray>& list,
    std::size_t* outEarliestHop = nullptr) noexcept {
    bool changed = false;
    std::size_t earliest = ~std::size_t{0};
    for (const auto& entry : list) {
        if (!entry.arrayRef.valid()) {
            continue;
        }
        if (const auto* heap = domains.heapForObject(entry.arrayRef);
            heap && heap->isAlive(entry.arrayRef)) {
            if (heap->arrayMutationVersion(entry.arrayRef) != entry.lastSeenVersion) {
                changed = true;
                if (entry.hopIndex < earliest) {
                    earliest = entry.hopIndex;
                }
            }
        }
    }
    if (changed && outEarliestHop) {
        *outEarliestHop = earliest;
    }
    return changed;
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
        for (const auto& idxArr : slot.indexedArrays) {
            if (idxArr.wrapperRef == dirtyObj) {
                return true;
            }
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
        if (HasIndexedArrayMutation(domains, slot.indexedArrays)) {
            return true;
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
    LiveBindingEngineRegistry::Instance().Register(this);
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkValuesInPools();
    });
}

bool DualTrackBindingEngine::IsLiveInstance(const DualTrackBindingEngine* engine) noexcept {
    return LiveBindingEngineRegistry::Instance().Contains(engine);
}

DualTrackBindingEngine::~DualTrackBindingEngine() noexcept {
    LiveBindingEngineRegistry::Instance().Unregister(this);
    rs_rt::unregisterExternalValuePoolRootMarker(this);
    for (auto& binding : trackABindings_) {
        if (binding.targetElement && binding.targetElement->BoundTwoWayEngine() == this) {
            binding.targetElement->BindTwoWayEngine(nullptr);
        }
        ReleaseCollectionObserver(binding);
        ReleaseIndexedArrayObserversFrom(
            domains_, binding.indexedArrays, 0u, binding.mode != BindingMode::OneTime);
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
        ReleaseIndexedArrayObserversFrom(
            domains_, eb.indexedArrays, 0u, eb.mode != BindingMode::OneTime);
        if (eb.mode != BindingMode::OneTime) {
            for (auto& hopRef : eb.resolvedHopObjects) {
                if (hopRef.valid()) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                    hopRef = ObjectRef{};
                }
            }
        }
    }
    for (auto& mb : multiBindings_) {
        if (mb.targetElement && mb.targetElement->BoundTwoWayEngine() == this) {
            mb.targetElement->BindTwoWayEngine(nullptr);
        }
        ReleaseMultiBindingObservers(mb);
    }
    for (auto& rt : richTextDependents_) {
        if (rt.targetElement && rt.targetElement->BoundTwoWayEngine() == this) {
            rt.targetElement->BindTwoWayEngine(nullptr);
        }
        if (rt.observedDcRef.valid()) {
            if (auto* heap = domains_.heapForObject(rt.observedDcRef)) {
                heap->removeUiObserver(rt.observedDcRef);
            }
            rt.observedDcRef = ObjectRef{};
        }
    }
    registeredConverters_.clear();
}

void DualTrackBindingEngine::NotifyElementDestroyedGlobal(const UIElement* element) noexcept {
    if (!element) {
        return;
    }
    LiveBindingEngineRegistry::Instance().ForEachLocked(
        [element](const std::vector<DualTrackBindingEngine*>& liveEngines) noexcept {
            for (DualTrackBindingEngine* eng : liveEngines) {
                if (eng) {
                    eng->ScrubDestroyedSourceElement(element);
                }
            }
        });
}

void DualTrackBindingEngine::ScrubDestroyedSourceElement(const UIElement* elem) noexcept {
    if (!elem) {
        return;
    }
    for (auto& eb : elementBindings_) {
        if (eb.cachedSourceElement == elem) {
            eb.cachedSourceElement = nullptr;
        }
    }
    auto scrubMultiRec = [elem](auto& self, MultiBindingInstance& mb) noexcept -> void {
        for (auto& sl : mb.sources) {
            if (sl.cachedSourceElement == elem) {
                sl.cachedSourceElement = nullptr;
            }
            if (sl.nestedMulti) {
                self(self, *sl.nestedMulti);
            }
        }
    };
    for (auto& mb : multiBindings_) {
        scrubMultiRec(scrubMultiRec, mb);
    }
}

bool DualTrackBindingEngine::HasAnyBindingTargeting(const UIElement* elem) const noexcept {
    if (!elem) {
        return false;
    }
    for (const auto& b : trackABindings_) {
        if (b.targetElement.Get() == elem) {
            return true;
        }
    }
    for (const auto& eb : elementBindings_) {
        if (eb.targetElement.Get() == elem) {
            return true;
        }
    }
    for (const auto& mb : multiBindings_) {
        if (mb.targetElement.Get() == elem) {
            return true;
        }
    }
    for (const auto& ch : trackBChannels_) {
        for (const auto& te : ch.targetElements) {
            if (te.Get() == elem) {
                return true;
            }
        }
    }
    return false;
}

bool DualTrackBindingEngine::HasPendingDirtyObjects() const noexcept {
    for (const auto& b : trackABindings_) {
        if (b.mode == BindingMode::OneTime && b.initialized) {
            continue;
        }
        if (b.syncedCollectionRef.valid()) {
            if (const auto* colHeap = domains_.heapForObject(b.syncedCollectionRef);
                colHeap && colHeap->isAlive(b.syncedCollectionRef)) {
                if (colHeap->arrayMutationVersion(b.syncedCollectionRef) != b.lastSeenMutationVersion) {
                    return true;
                }
            }
        }
        if (HasIndexedArrayMutation(domains_, b.indexedArrays)) {
            return true;
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
    for (const auto& eb : elementBindings_) {
        if (eb.mode == BindingMode::OneTime && eb.initialized) {
            continue;
        }
        if (HasIndexedArrayMutation(domains_, eb.indexedArrays)) {
            return true;
        }
        for (const auto& hopRef : eb.resolvedHopObjects) {
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
    for (const auto& rt : richTextDependents_) {
        if (rt.observedDcRef.valid()) {
            if (const auto* heap = domains_.heapForObject(rt.observedDcRef)) {
                if (heap->dirtyFieldMask(rt.observedDcRef) != 0ULL) {
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
    entry.receiverRoot.reset();
    if (receiverObject.valid()) {
        if (auto* heap = domains_.heapForObject(receiverObject); heap && heap->isAlive(receiverObject)) {
            entry.receiverRoot = std::make_shared<PersistentRoot>(heap->retain(receiverObject));
        }
    }
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
    EnumValue ev{};
    const bool isEnumVal = val.tryGetEnum(ev);
    if (isEnumVal && ev.typeId != 0 && program_ &&
        (targetPropId == PropId_Text || targetPropId == PropId_Title ||
         targetPropId == PropId_Placeholder || targetPropId == PropId_Header ||
         !stringFormat.empty())) {
        std::string enumName;
        if (TryFormatEnumValueName(program_, ev.typeId, ev.value, enumName)) {
            const UnifiedValue enumStrVal = value::FromString(std::move(enumName));
            if (!stringFormat.empty()) {
                UnifiedValue formatted = value::FromString(
                    FormatBindingString(stringFormat, &enumStrVal, 1u, &domains_));
                if (formatted.isTraceable()) {
                    rs_rt::pinValueInPools(formatted);
                }
                return formatted;
            }
            if (enumStrVal.isTraceable()) {
                rs_rt::pinValueInPools(enumStrVal);
            }
            return enumStrVal;
        }
    }
    if (!stringFormat.empty() &&
        !std::holds_alternative<std::monostate>(val) &&
        !std::holds_alternative<NullObject>(val)) {
        UnifiedValue formatted = value::FromString(FormatBindingString(stringFormat, &val, 1u, &domains_));
        if (formatted.isTraceable()) {
            rs_rt::pinValueInPools(formatted);
        }
        return formatted;
    }
    if (targetPropId != PropId_DataContext && targetPropId != PropId_SelectedItem) {
        if (const auto* strRef = std::get_if<ObjectRef>(&val)) {
            if (strRef->valid() && strRef->kind == ObjectKind::String) {
                if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                    UnifiedValue strVal = value::FromString(std::string(*sv));
                    if (strVal.isTraceable()) {
                        rs_rt::pinValueInPools(strVal);
                    }
                    return strVal;
                }
            }
        }
    }
    if ((targetPropId == PropId_Text || targetPropId == PropId_Title ||
         targetPropId == PropId_Placeholder || targetPropId == PropId_Header ||
         targetPropId == PropId_RichText) &&
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
        ReleaseIndexedArrayObserversFrom(
            domains_, binding.indexedArrays, fromHop, binding.mode != BindingMode::OneTime);
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
        if (program_ && runtimeTypeId != 0) {
            if (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid()) {
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
                hop.targetTypeId = 0;
                hop.targetIsEnum = false;
            }
            ResolveDynamicHopTargetType(program_, runtimeTypeId, hop);
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

        if (hop.collectionIndex >= 0) {
            UnifiedValue indexedVal = NullObject{};
            ObjectRef backingArr{};
            ObjectRef wrapperRef{};
            if (const auto* colObjRef = std::get_if<ObjectRef>(&hopVal); colObjRef && colObjRef->valid()) {
                if (auto* colHeap = domains_.heapForObject(*colObjRef);
                    colHeap && colHeap->isAlive(*colObjRef)) {
                    backingArr = ObservableListHelper::ResolveBackingArray(*colHeap, *colObjRef);
                    if (backingArr.valid()) {
                        wrapperRef = (colObjRef->kind == ObjectKind::Record && !(*colObjRef == backingArr))
                            ? *colObjRef
                            : ObjectRef{};
                        const auto arrLen = colHeap->arrayLength(backingArr).value_or(0u);
                        const auto idx = static_cast<std::size_t>(hop.collectionIndex);
                        if (idx < arrLen) {
                            if (auto elemOpt = colHeap->arrayGet(backingArr, idx)) {
                                indexedVal = *elemOpt;
                            }
                        }
                    }
                }
            }
            UpsertIndexedArrayObserver(
                domains_,
                binding.indexedArrays,
                h,
                backingArr,
                wrapperRef,
                binding.mode != BindingMode::OneTime);
            hopVal = std::move(indexedVal);
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
                            const ObjectRef wrapperRef =
                                (colObjRef->kind == ObjectKind::Record && !(*colObjRef == backingArr))
                                    ? *colObjRef
                                    : ObjectRef{};

                            if (binding.initialized && binding.syncedCollectionRef == backingArr) {
                                std::vector<CollectionChangeRecord> localDeltas;
                                localDeltas.swap(scratchCollectionDeltas_);
                                auto recycleLocalDeltas = [&]() noexcept {
                                    if (scratchCollectionDeltas_.capacity() == 0u &&
                                        localDeltas.capacity() > 0u) {
                                        localDeltas.clear();
                                        scratchCollectionDeltas_.swap(localDeltas);
                                    }
                                };

                                std::uint64_t latestVer = 0ULL;
                                bool requiresReset = false;
                                if (colHeap->readArrayChangesSince(
                                        backingArr,
                                        binding.lastSeenMutationVersion,
                                        localDeltas,
                                        latestVer,
                                        requiresReset)) {
                                    if (!requiresReset) {
                                        if (localDeltas.empty()) {
                                            recycleLocalDeltas();
                                            return false;
                                        }
                                        binding.lastSeenMutationVersion = latestVer;
                                        const IntrusivePtr<UIElement> targetElem = binding.targetElement;
                                        bool appliedIncrementally = false;
                                        if (auto* ic = targetElem->DynamicCast<ItemsControl>()) {
                                            appliedIncrementally = ic->ApplyIncrementalChanges(
                                                localDeltas.data(),
                                                localDeltas.size());
                                        } else if (auto* tc = targetElem->DynamicCast<TabControl>()) {
                                            appliedIncrementally = tc->ApplyIncrementalChanges(
                                                localDeltas.data(),
                                                localDeltas.size());
                                        }
                                        if (appliedIncrementally) {
                                            recycleLocalDeltas();
                                            return true;
                                        }
                                    }
                                    // Overflow, explicit Reset, or unsupported incremental change:
                                    // fall through to full collection sync!
                                    recycleLocalDeltas();
                                    binding.lastSeenMutationVersion = latestVer;
                                    hopVal = UnifiedValue{backingArr};
                                } else {
                                    recycleLocalDeltas();
                                }
                            } else {
                                ReleaseCollectionObserver(binding);
                                if (binding.mode != BindingMode::OneTime) {
                                    if (wrapperRef.valid()) {
                                        colHeap->addUiObserver(wrapperRef);
                                        binding.syncedCollectionWrapperRef = wrapperRef;
                                    }
                                    if (hop.observableBitIndex < 64u &&
                                        currentObj.valid() &&
                                        currentObj.heapId == backingArr.heapId) {
                                        colHeap->addArrayPropertyObserver(
                                            backingArr,
                                            currentObj,
                                            hop.observableBitIndex);
                                        binding.syncedCollectionOwnerRef = currentObj;
                                        binding.syncedCollectionBitIndex = hop.observableBitIndex;
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
            if (hop.targetIsEnum && hop.targetTypeId != 0 && program_ &&
                binding.converter == ValueConverterKind::None &&
                (binding.targetPropertyId == PropId_Text ||
                 binding.targetPropertyId == PropId_Title ||
                 binding.targetPropertyId == PropId_Placeholder ||
                 binding.targetPropertyId == PropId_Header ||
                 !binding.stringFormat.empty())) {
                if (const auto* iv = std::get_if<std::int64_t>(&hopVal)) {
                    std::string enumName;
                    if (TryFormatEnumValueName(program_, hop.targetTypeId, *iv, enumName)) {
                        hopVal = value::FromString(std::move(enumName));
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
            for (UIElement* anc = target; anc != nullptr; anc = anc->Parent()) {
                bool matched = false;
                for (const TypeClass* tc = anc->GetTypeClass(); tc != nullptr; tc = tc->baseClass) {
                    if (tc->typeName && ShortTypeNameOf(tc->typeName) == eb.relativeAncestorType) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    if (eb.relativeAncestorType == "ToggleButton") {
                        matched = anc->IsA<CheckBox>() || anc->IsA<RadioButton>();
                    } else if (eb.relativeAncestorType == "Page") {
                        matched = anc->IsA<UserControl>() || anc->IsA<Window>();
                    } else if (eb.relativeAncestorType == "ContentControl" ||
                               eb.relativeAncestorType == "ListBoxItem" ||
                               eb.relativeAncestorType == "ComboBoxItem" ||
                               eb.relativeAncestorType == "ListViewItem" ||
                               eb.relativeAncestorType == "TreeViewItem") {
                        matched = anc->IsA<Border>() || anc->IsA<ContentPresenter>() || anc->IsA<UserControl>();
                    } else if (eb.relativeAncestorType == "RepeatButton" ||
                               eb.relativeAncestorType == "Hyperlink" ||
                               eb.relativeAncestorType == "MenuItem") {
                        matched = anc->IsA<Button>();
                    } else if (eb.relativeAncestorType == "ListView") {
                        matched = anc->IsA<ListBox>();
                    } else if (eb.relativeAncestorType == "TreeView") {
                        matched = anc->IsA<ItemsControl>();
                    } else if (eb.relativeAncestorType == "FrameworkElement" ||
                               eb.relativeAncestorType == "Control") {
                        matched = true;
                    }
                }
                if (matched) {
                    eb.cachedSourceElement = anc;
                    break;
                }
            }
        } else if (!eb.elementName.empty()) {
            if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(target)) {
                eb.cachedSourceElement = scopeRoot->FindName(eb.elementName);
            }
        }
        if (eb.cachedSourceElement) {
            eb.cachedSourceElement->MarkReferencedAsElementSource();
        }
    }

    if (!eb.cachedSourceElement) {
        return false;
    }

    auto unbindEbRange = [&](std::size_t fromHop) {
        ReleaseIndexedArrayObserversFrom(
            domains_, eb.indexedArrays, fromHop, eb.mode != BindingMode::OneTime);
        for (std::size_t cIdx = fromHop; cIdx < eb.resolvedHopObjects.size(); ++cIdx) {
            const ObjectRef oldRef = eb.resolvedHopObjects[cIdx];
            if (oldRef.valid()) {
                if (eb.mode != BindingMode::OneTime) {
                    if (auto* oldHeap = domains_.heapForObject(oldRef)) {
                        oldHeap->removeUiObserver(oldRef);
                    }
                }
                eb.resolvedHopObjects[cIdx] = ObjectRef{};
            }
        }
    };

    UnifiedValue val{};
    if (eb.sourcePropertyId == 0xFFFFu) {
        val = UnifiedValue{eb.cachedSourceElement->ToNativeHandle(domains_.nativeHandles())};
    } else {
        val = ReadElementPropertyOrState(eb.cachedSourceElement, eb.sourcePropertyId);
        if (eb.subHops.empty()) {
            if (const auto* nh = std::get_if<NativeHandle>(&val)) {
                if (const UIElement* selElem = UIElement::FromNativeHandle(domains_.nativeHandles(), *nh)) {
                    if (const auto* tvi = selElem->DynamicCast<TreeViewItem>()) {
                        if (eb.targetPropertyId == PropId_Text ||
                            eb.targetPropertyId == PropId_Content ||
                            eb.targetPropertyId == PropId_Header ||
                            !eb.stringFormat.empty()) {
                            val = value::FromString(tvi->GetHeader());
                        }
                    }
                }
            }
        } else {
            if (eb.resolvedHopObjects.size() != eb.subHops.size()) {
                eb.resolvedHopObjects.assign(eb.subHops.size(), ObjectRef{});
            }
            const auto* startRef = std::get_if<ObjectRef>(&val);
            if (const auto* nh = std::get_if<NativeHandle>(&val)) {
                unbindEbRange(0u);
                UIElement* curElem = UIElement::FromNativeHandle(domains_.nativeHandles(), *nh);
                bool nhOk = (curElem != nullptr);
                for (std::size_t h = 0u; h < eb.subHops.size() && nhOk; ++h) {
                    const std::uint16_t subPid = ResolveTriggerConditionPropertyId(eb.subHops[h].memberName);
                    if (subPid == PropId_Tag && eb.subHops[h].memberName != "Tag") {
                        nhOk = false;
                        break;
                    }
                    val = ReadElementPropertyOrState(curElem, subPid);
                    if (h + 1u < eb.subHops.size()) {
                        const auto* nextNh = std::get_if<NativeHandle>(&val);
                        curElem = nextNh ? UIElement::FromNativeHandle(domains_.nativeHandles(), *nextNh) : nullptr;
                        nhOk = (curElem != nullptr);
                    }
                }
                if (!nhOk) {
                    val = UnifiedValue{};
                }
            } else if (!startRef || !startRef->valid()) {
                unbindEbRange(0u);
                val = UnifiedValue{};
            } else {
                ObjectRef curObj = *startRef;
                bool subValid = true;
                for (std::size_t h = 0u; h < eb.subHops.size(); ++h) {
                    auto* heap = domains_.heapForObject(curObj);
                    if (!heap || !heap->isAlive(curObj)) {
                        unbindEbRange(h);
                        subValid = false;
                        break;
                    }
                    if (!(eb.resolvedHopObjects[h] == curObj)) {
                        if (eb.resolvedHopObjects[h].valid() && eb.mode != BindingMode::OneTime) {
                            if (auto* oldHeap = domains_.heapForObject(eb.resolvedHopObjects[h])) {
                                oldHeap->removeUiObserver(eb.resolvedHopObjects[h]);
                            }
                        }
                        eb.resolvedHopObjects[h] = curObj;
                        if (eb.mode != BindingMode::OneTime) {
                            heap->addUiObserver(curObj);
                        }
                    }
                    auto& hop = eb.subHops[h];
                    const SymbolId runtimeTypeId = heap->objectTypeId(curObj).value_or(0);
                    if (program_ && runtimeTypeId != 0) {
                        if (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid()) {
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
                            hop.targetTypeId = 0;
                            hop.targetIsEnum = false;
                        }
                        ResolveDynamicHopTargetType(program_, runtimeTypeId, hop);
                    }
                    const PropertySlotHandle sh = hop.ToSlotHandle();
                    UnifiedValue hv{};
                    if (sh.valid()) {
                        if (!domains_.readPropertySlotFast(
                                DomainKind::Ui, curObj, sh, hv, nullptr, uiInterpreter_)) {
                            unbindEbRange(h + 1u);
                            subValid = false;
                            break;
                        }
                    } else if (hop.fieldIndex != PropertySlotHandle::InvalidIndex) {
                        if (!domains_.readObjectField(
                                DomainKind::Ui, curObj, hop.fieldIndex, hv, nullptr)) {
                            unbindEbRange(h + 1u);
                            subValid = false;
                            break;
                        }
                    } else {
                        unbindEbRange(h + 1u);
                        subValid = false;
                        break;
                    }
                    if (hop.collectionIndex >= 0) {
                        UnifiedValue indexedVal = NullObject{};
                        ObjectRef backingArr{};
                        ObjectRef wrapperRef{};
                        if (const auto* colRef = std::get_if<ObjectRef>(&hv); colRef && colRef->valid()) {
                            if (auto* colHeap = domains_.heapForObject(*colRef);
                                colHeap && colHeap->isAlive(*colRef)) {
                                backingArr = ObservableListHelper::ResolveBackingArray(*colHeap, *colRef);
                                if (backingArr.valid()) {
                                    wrapperRef = (colRef->kind == ObjectKind::Record && !(*colRef == backingArr))
                                        ? *colRef
                                        : ObjectRef{};
                                    const auto arrLen = colHeap->arrayLength(backingArr).value_or(0u);
                                    const auto idx = static_cast<std::size_t>(hop.collectionIndex);
                                    if (idx < arrLen) {
                                        if (auto elemOpt = colHeap->arrayGet(backingArr, idx)) {
                                            indexedVal = *elemOpt;
                                        }
                                    }
                                }
                            }
                        }
                        UpsertIndexedArrayObserver(
                            domains_, eb.indexedArrays, h, backingArr, wrapperRef, eb.mode != BindingMode::OneTime);
                        hv = std::move(indexedVal);
                    }
                    if (h + 1u < eb.subHops.size()) {
                        const auto* nextRef = std::get_if<ObjectRef>(&hv);
                        if (!nextRef || !nextRef->valid()) {
                            unbindEbRange(h + 1u);
                            subValid = false;
                            break;
                        }
                        curObj = *nextRef;
                    } else {
                        if (const auto* strRef = std::get_if<ObjectRef>(&hv);
                            strRef && strRef->kind == ObjectKind::String) {
                            if (const auto sv = domains_.stringViewAcrossDomains(*strRef)) {
                                hv = value::FromString(std::string(*sv));
                            }
                        } else if (hop.targetIsEnum && hop.targetTypeId != 0 && program_ &&
                                   eb.converter == ValueConverterKind::None &&
                                   (eb.targetPropertyId == PropId_Text ||
                                    eb.targetPropertyId == PropId_Title ||
                                    eb.targetPropertyId == PropId_Placeholder ||
                                    eb.targetPropertyId == PropId_Header ||
                                    !eb.stringFormat.empty())) {
                            if (const auto* iv = std::get_if<std::int64_t>(&hv)) {
                                std::string enumName;
                                if (TryFormatEnumValueName(program_, hop.targetTypeId, *iv, enumName)) {
                                    hv = value::FromString(std::move(enumName));
                                }
                            }
                        }
                        val = std::move(hv);
                    }
                }
                if (!subValid) {
                    val = UnifiedValue{};
                }
            }
        }
    }

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
        ? std::string_view(compiledChain.hops.front().memberName)
        : std::string_view(compiledChain.rawPath);
    std::uint16_t srcPropId = 0u;
    if (propName.empty() || propName == ".") {
        srcPropId = 0xFFFFu;
    } else {
        srcPropId = ResolveTriggerConditionPropertyId(propName);
        if (srcPropId == 0u) {
            return 0;
        }
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
    if (srcPropId != 0xFFFFu && compiledChain.hops.size() > 1u) {
        eb.subHops.assign(compiledChain.hops.begin() + 1, compiledChain.hops.end());
        eb.resolvedHopObjects.assign(eb.subHops.size(), ObjectRef{});
    }
    eb.targetElement = targetElement;
    eb.fallbackValue = compiledChain.fallbackValue;
    if (eb.fallbackValue.isTraceable()) {
        rs_rt::pinValueInPools(eb.fallbackValue);
    }

    if (eb.mode == BindingMode::TwoWay && srcPropId != 0xFFFFu && eb.subHops.empty()) {
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
            if (propLeaf.empty() || propLeaf == ".") {
                slot.sourcePropertyId = 0xFFFFu;
            } else {
                slot.sourcePropertyId = ResolveTriggerConditionPropertyId(propLeaf);
            }
            // Support multi-hop after ElementName property (e.g. Path="Content.Life" ElementName="SlotHead")
            if (slot.sourcePropertyId != 0xFFFFu && srcCh.hops.size() > 1u) {
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
                        std::string baseMember;
                        std::int32_t colIdx = -1;
                        if (SplitIndexedMemberName(seg, baseMember, colIdx)) {
                            h.memberName = std::move(baseMember);
                            h.collectionIndex = colIdx;
                        } else {
                            h.memberName = std::string(seg);
                        }
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
        ReleaseIndexedArrayObserversFrom(domains_, slot.indexedArrays, 0u, true);
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
            ReleaseIndexedArrayObserversFrom(
                domains_, slot.indexedArrays, fromHop, mb.mode != BindingMode::OneTime);
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
                if (program_ && runtimeTypeId != 0) {
                    if (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid()) {
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
                        hop.targetTypeId = 0;
                        hop.targetIsEnum = false;
                    }
                    ResolveDynamicHopTargetType(program_, runtimeTypeId, hop);
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
                if (hop.collectionIndex >= 0) {
                    UnifiedValue indexedVal = NullObject{};
                    ObjectRef backingArr{};
                    ObjectRef wrapperRef{};
                    if (const auto* colObjRef = std::get_if<ObjectRef>(&hv);
                        colObjRef && colObjRef->valid()) {
                        if (auto* colHeap = domains_.heapForObject(*colObjRef);
                            colHeap && colHeap->isAlive(*colObjRef)) {
                            backingArr = ObservableListHelper::ResolveBackingArray(*colHeap, *colObjRef);
                            if (backingArr.valid()) {
                                wrapperRef = (colObjRef->kind == ObjectKind::Record && !(*colObjRef == backingArr))
                                    ? *colObjRef
                                    : ObjectRef{};
                                const auto arrLen = colHeap->arrayLength(backingArr).value_or(0u);
                                const auto idx = static_cast<std::size_t>(hop.collectionIndex);
                                if (idx < arrLen) {
                                    if (auto elemOpt = colHeap->arrayGet(backingArr, idx)) {
                                        indexedVal = *elemOpt;
                                    }
                                }
                            }
                        }
                    }
                    UpsertIndexedArrayObserver(
                        domains_,
                        slot.indexedArrays,
                        h,
                        backingArr,
                        wrapperRef,
                        mb.mode != BindingMode::OneTime);
                    hv = std::move(indexedVal);
                }
                if (h + 1u < slot.hops.size()) {
                    const auto* nextRef = std::get_if<ObjectRef>(&hv);
                    if (!nextRef || !nextRef->valid()) {
                        unbindSlotRange(h + 1u);
                        return false;
                    }
                    curObj = *nextRef;
                } else {
                    if (hop.targetIsEnum && hop.targetTypeId != 0 && program_ &&
                        slot.converter == ValueConverterKind::None &&
                        (!slot.stringFormat.empty() ||
                         (mb.converter == ValueConverterKind::None &&
                          (!mb.stringFormat.empty() ||
                           mb.targetPropertyId == PropId_Text ||
                           mb.targetPropertyId == PropId_Title ||
                           mb.targetPropertyId == PropId_Placeholder ||
                           mb.targetPropertyId == PropId_Header)))) {
                        if (const auto* iv = std::get_if<std::int64_t>(&hv)) {
                            std::string enumName;
                            if (TryFormatEnumValueName(program_, hop.targetTypeId, *iv, enumName)) {
                                hv = value::FromString(std::move(enumName));
                            }
                        }
                    }
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
                    for (UIElement* anc = target; anc != nullptr; anc = anc->Parent()) {
                        bool matched = false;
                        for (const TypeClass* tc = anc->GetTypeClass(); tc != nullptr; tc = tc->baseClass) {
                            if (tc->typeName && ShortTypeNameOf(tc->typeName) == slot.relativeAncestorType) {
                                matched = true;
                                break;
                            }
                        }
                        if (!matched) {
                            if (slot.relativeAncestorType == "ToggleButton") {
                                matched = anc->IsA<CheckBox>() || anc->IsA<RadioButton>();
                            } else if (slot.relativeAncestorType == "Page") {
                                matched = anc->IsA<UserControl>() || anc->IsA<Window>();
                            } else if (slot.relativeAncestorType == "ContentControl" ||
                                       slot.relativeAncestorType == "ListBoxItem" ||
                                       slot.relativeAncestorType == "ComboBoxItem" ||
                                       slot.relativeAncestorType == "ListViewItem" ||
                                       slot.relativeAncestorType == "TreeViewItem") {
                                matched = anc->IsA<Border>() || anc->IsA<ContentPresenter>() || anc->IsA<UserControl>();
                            } else if (slot.relativeAncestorType == "RepeatButton" ||
                                       slot.relativeAncestorType == "Hyperlink" ||
                                       slot.relativeAncestorType == "MenuItem") {
                                matched = anc->IsA<Button>();
                            } else if (slot.relativeAncestorType == "ListView") {
                                matched = anc->IsA<ListBox>();
                            } else if (slot.relativeAncestorType == "TreeView") {
                                matched = anc->IsA<ItemsControl>();
                            } else if (slot.relativeAncestorType == "FrameworkElement" ||
                                       slot.relativeAncestorType == "Control") {
                                matched = true;
                            }
                        }
                        if (matched) {
                            slot.cachedSourceElement = anc;
                            break;
                        }
                    }
                } else if (!slot.elementName.empty()) {
                    if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(target)) {
                        slot.cachedSourceElement = scopeRoot->FindName(slot.elementName);
                    }
                }
                if (slot.cachedSourceElement) {
                    slot.cachedSourceElement->MarkReferencedAsElementSource();
                }
            }
            if (slot.cachedSourceElement && slot.sourcePropertyId == 0xFFFFu) {
                slotVal = UnifiedValue{slot.cachedSourceElement->ToNativeHandle(domains_.nativeHandles())};
                slotValid = true;
            } else if (slot.cachedSourceElement && slot.sourcePropertyId != 0u) {
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
        if (!std::holds_alternative<std::monostate>(mb.lastSyncedOutput)) {
            const IntrusivePtr<UIElement> targetElem = mb.targetElement;
            const std::uint16_t targetPropId = mb.targetPropertyId;
            mb.lastSyncedOutput = UnifiedValue{};
            mb.initialized = true;
            targetElem->ClearValue(targetPropId);
            return true;
        }
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
            std::string baseMember;
            std::int32_t colIdx = -1;
            if (SplitIndexedMemberName(segment, baseMember, colIdx)) {
                hop.memberName = std::move(baseMember);
                hop.collectionIndex = colIdx;
            } else {
                hop.memberName = std::string(segment);
            }
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
        ReleaseIndexedArrayObserversFrom(
            domains_, binding.indexedArrays, 0u, binding.mode != BindingMode::OneTime);
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

    std::vector<IntrusivePtr<UIElement>> unboundTargets;
    std::size_t removedCount = 0u;
    std::size_t writeIdx = 0u;
    for (std::size_t i = 0u; i < trackABindings_.size(); ++i) {
        auto& b = trackABindings_[i];
        if (b.targetElement && isInSubtree(b.targetElement.Get())) {
            if (b.targetElement->BoundTwoWayEngine() == this) {
                unboundTargets.push_back(b.targetElement);
            }
            ReleaseCollectionObserver(b);
            ReleaseIndexedArrayObserversFrom(
                domains_, b.indexedArrays, 0u, b.mode != BindingMode::OneTime);
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
                unboundTargets.push_back(eb.targetElement);
            }
            ReleaseIndexedArrayObserversFrom(
                domains_, eb.indexedArrays, 0u, eb.mode != BindingMode::OneTime);
            for (const auto& hopRef : eb.resolvedHopObjects) {
                if (hopRef.valid() && eb.mode != BindingMode::OneTime) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                }
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

    auto scrubMultiSubtreeRec = [&](auto& self, MultiBindingInstance& inst) noexcept -> void {
        for (auto& sl : inst.sources) {
            if (sl.cachedSourceElement && isInSubtree(sl.cachedSourceElement)) {
                sl.cachedSourceElement = nullptr;
            }
            if (sl.nestedMulti) {
                self(self, *sl.nestedMulti);
            }
        }
    };

    std::size_t mbWriteIdx = 0u;
    for (std::size_t i = 0u; i < multiBindings_.size(); ++i) {
        auto& mb = multiBindings_[i];
        if (mb.targetElement && isInSubtree(mb.targetElement.Get())) {
            if (mb.targetElement->BoundTwoWayEngine() == this) {
                unboundTargets.push_back(mb.targetElement);
            }
            ReleaseMultiBindingObservers(mb);
            ++removedCount;
            continue;
        }
        scrubMultiSubtreeRec(scrubMultiSubtreeRec, mb);
        if (mbWriteIdx != i) {
            multiBindings_[mbWriteIdx] = std::move(mb);
        }
        ++mbWriteIdx;
    }
    multiBindings_.resize(mbWriteIdx);

    std::size_t rtWriteIdx = 0u;
    for (std::size_t i = 0u; i < richTextDependents_.size(); ++i) {
        auto& rt = richTextDependents_[i];
        if (rt.targetElement && isInSubtree(rt.targetElement.Get())) {
            if (rt.observedDcRef.valid()) {
                if (auto* heap = domains_.heapForObject(rt.observedDcRef)) {
                    heap->removeUiObserver(rt.observedDcRef);
                }
            }
            ++removedCount;
            continue;
        }
        if (rtWriteIdx != i) {
            richTextDependents_[rtWriteIdx] = std::move(rt);
        }
        ++rtWriteIdx;
    }
    richTextDependents_.resize(rtWriteIdx);

    for (const auto& elem : unboundTargets) {
        if (elem && elem->BoundTwoWayEngine() == this && !HasAnyBindingTargeting(elem.Get())) {
            elem->BindTwoWayEngine(nullptr);
        }
    }
    return removedCount;
}

bool DualTrackBindingEngine::UnbindSlot(std::uint16_t bindingSlotId) noexcept {
    if (bindingSlotId == 0u) {
        return false;
    }
    for (auto it = trackABindings_.begin(); it != trackABindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            IntrusivePtr<UIElement> targetElem = it->targetElement;
            ReleaseCollectionObserver(*it);
            ReleaseIndexedArrayObserversFrom(
                domains_, it->indexedArrays, 0u, it->mode != BindingMode::OneTime);
            for (const auto& hopRef : it->resolvedHopObjects) {
                if (hopRef.valid() && it->mode != BindingMode::OneTime) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                }
            }
            trackABindings_.erase(it);
            if (targetElem && targetElem->BoundTwoWayEngine() == this &&
                !HasAnyBindingTargeting(targetElem.Get())) {
                targetElem->BindTwoWayEngine(nullptr);
            }
            return true;
        }
    }
    for (auto it = elementBindings_.begin(); it != elementBindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            IntrusivePtr<UIElement> targetElem = it->targetElement;
            ReleaseIndexedArrayObserversFrom(
                domains_, it->indexedArrays, 0u, it->mode != BindingMode::OneTime);
            for (const auto& hopRef : it->resolvedHopObjects) {
                if (hopRef.valid() && it->mode != BindingMode::OneTime) {
                    if (auto* heap = domains_.heapForObject(hopRef)) {
                        heap->removeUiObserver(hopRef);
                    }
                }
            }
            elementBindings_.erase(it);
            if (targetElem && targetElem->BoundTwoWayEngine() == this &&
                !HasAnyBindingTargeting(targetElem.Get())) {
                targetElem->BindTwoWayEngine(nullptr);
            }
            return true;
        }
    }
    for (auto it = multiBindings_.begin(); it != multiBindings_.end(); ++it) {
        if (it->bindingSlotId == bindingSlotId) {
            IntrusivePtr<UIElement> targetElem = it->targetElement;
            ReleaseMultiBindingObservers(*it);
            multiBindings_.erase(it);
            if (targetElem && targetElem->BoundTwoWayEngine() == this &&
                !HasAnyBindingTargeting(targetElem.Get())) {
                targetElem->BindTwoWayEngine(nullptr);
            }
            return true;
        }
    }
    return false;
}

void DualTrackBindingEngine::RegisterRichTextDependent(
    const IntrusivePtr<UIElement>& targetElement) {
    if (!targetElement) {
        return;
    }
    for (const auto& existing : richTextDependents_) {
        if (existing.targetElement.Get() == targetElement.Get()) {
            return;
        }
    }
    ObjectRef dcRef{};
    for (const UIElement* cur = targetElement.Get(); cur != nullptr;) {
        const UnifiedValue dcVal = cur->GetValue(PropId_DataContext);
        if (const auto* oref = std::get_if<ObjectRef>(&dcVal); oref && oref->valid()) {
            dcRef = *oref;
            break;
        }
        if (cur->Parent() != nullptr) {
            cur = cur->Parent();
        } else {
            cur = cur->TemplatedParent();
        }
    }
    if (dcRef.valid()) {
        if (auto* heap = domains_.heapForObject(dcRef)) {
            heap->addUiObserver(dcRef);
        }
    }
    RichTextDependentEntry entry{};
    entry.targetElement = targetElement;
    entry.observedDcRef = dcRef;
    richTextDependents_.push_back(std::move(entry));
    const std::size_t slotIdx = richTextDependents_.size() - 1u;
    if (const auto* tb = targetElement->DynamicCast<TextBlock>()) {
        std::string initialText = tb->GetText();
        if (slotIdx < richTextDependents_.size() &&
            richTextDependents_[slotIdx].targetElement.Get() == targetElement.Get()) {
            richTextDependents_[slotIdx].lastResolvedText = std::move(initialText);
        }
    }
}

std::size_t DualTrackBindingEngine::FlushTrackA(RuntimeError* error) {
    if (trackABindings_.empty() && elementBindings_.empty() &&
        multiBindings_.empty() && richTextDependents_.empty()) {
        return 0;
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        !UiResourceContext::HasActiveThreadOverride()
            ? UiResourceContext::FindForDomains(&domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));

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
                bool syncedThisBinding = false;
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
                        syncedThisBinding = true;
                        break;
                    }
                }
                if (!syncedThisBinding && bIdx < trackABindings_.size()) {
                    for (const auto& idxArr : trackABindings_[bIdx].indexedArrays) {
                        if (idxArr.wrapperRef == dirtyObj) {
                            if (EvaluateAndSyncChain(trackABindings_[bIdx], idxArr.hopIndex, error)) {
                                ++updatedCount;
                            }
                            break;
                        }
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

    // Sync any non-[Observable] or cross-heap ObservableList / array mutations tracked via arrayMutationVersion
    const std::size_t colSnapshotCount = trackABindings_.size();
    for (std::size_t bIdx = 0u; bIdx < colSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
        const auto& b = trackABindings_[bIdx];
        if ((b.mode == BindingMode::OneTime && b.initialized) || b.hops.empty()) {
            continue;
        }
        std::size_t earliestIndexedHop = ~std::size_t{0};
        if (HasIndexedArrayMutation(domains_, b.indexedArrays, &earliestIndexedHop)) {
            if (EvaluateAndSyncChain(trackABindings_[bIdx], earliestIndexedHop, error)) {
                ++updatedCount;
            }
            continue;
        }
        if (!b.syncedCollectionRef.valid()) {
            continue;
        }
        if (const auto* colHeap = domains_.heapForObject(b.syncedCollectionRef);
            colHeap && colHeap->isAlive(b.syncedCollectionRef)) {
            if (colHeap->arrayMutationVersion(b.syncedCollectionRef) != b.lastSeenMutationVersion) {
                if (EvaluateAndSyncChain(trackABindings_[bIdx], b.hops.size() - 1u, error)) {
                    ++updatedCount;
                }
            }
        }
    }

    auto multiHasIndexedChange = [&](auto& self, const MultiBindingInstance& mb) noexcept -> bool {
        for (const auto& sl : mb.sources) {
            if (sl.nestedMulti) {
                if (self(self, *sl.nestedMulti)) return true;
                continue;
            }
            if (HasIndexedArrayMutation(domains_, sl.indexedArrays)) {
                return true;
            }
        }
        return false;
    };
    const std::size_t mbIdxSnapshotCount = multiBindings_.size();
    for (std::size_t mIdx = 0u; mIdx < mbIdxSnapshotCount && mIdx < multiBindings_.size(); ++mIdx) {
        if (multiBindings_[mIdx].mode == BindingMode::OneTime && multiBindings_[mIdx].initialized) {
            continue;
        }
        if (multiHasIndexedChange(multiHasIndexedChange, multiBindings_[mIdx])) {
            if (EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error)) {
                ++updatedCount;
            }
        }
    }

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
                    ReleaseIndexedArrayObserversFrom(domains_, binding.indexedArrays, h, true);
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

        auto sweepMultiDeadHops = [&](auto& self, MultiBindingInstance& inst) noexcept -> bool {
            bool anyDeadHop = false;
            for (auto& sl : inst.sources) {
                if (sl.nestedMulti) {
                    if (self(self, *sl.nestedMulti)) {
                        anyDeadHop = true;
                    }
                    continue;
                }
                for (std::size_t h = 0u; h < sl.resolvedHopObjects.size(); ++h) {
                    const ObjectRef ref = sl.resolvedHopObjects[h];
                    if (!ref.valid()) {
                        continue;
                    }
                    auto* heap = domains_.heapForObject(ref);
                    if (!heap || !heap->isAlive(ref)) {
                        anyDeadHop = true;
                        ReleaseIndexedArrayObserversFrom(domains_, sl.indexedArrays, h, true);
                        for (std::size_t clearIdx = h; clearIdx < sl.resolvedHopObjects.size(); ++clearIdx) {
                            const ObjectRef oldRef = sl.resolvedHopObjects[clearIdx];
                            if (oldRef.valid()) {
                                if (auto* oldHeap = domains_.heapForObject(oldRef)) {
                                    oldHeap->removeUiObserver(oldRef);
                                }
                                sl.resolvedHopObjects[clearIdx] = ObjectRef{};
                            }
                        }
                        break;
                    }
                }
            }
            return anyDeadHop;
        };
        const std::size_t gcMultiSnapshot = multiBindings_.size();
        for (std::size_t mIdx = 0u; mIdx < gcMultiSnapshot && mIdx < multiBindings_.size(); ++mIdx) {
            auto& mb = multiBindings_[mIdx];
            if (!mb.initialized || !mb.targetElement || mb.mode == BindingMode::OneTime) {
                continue;
            }
            if (sweepMultiDeadHops(sweepMultiDeadHops, mb)) {
                if (EvaluateAndSyncMultiBinding(multiBindings_[mIdx], error)) {
                    ++updatedCount;
                }
            }
        }
    }

    if (!richTextDependents_.empty()) {
        const std::size_t rtSnapshotCount = richTextDependents_.size();
        for (std::size_t rIdx = 0u; rIdx < rtSnapshotCount && rIdx < richTextDependents_.size(); ++rIdx) {
            auto* tb = richTextDependents_[rIdx].targetElement
                ? richTextDependents_[rIdx].targetElement->DynamicCast<TextBlock>()
                : nullptr;
            if (!tb) {
                continue;
            }
            ObjectRef curDc{};
            for (const UIElement* cur = tb; cur != nullptr;) {
                const UnifiedValue dcVal = cur->GetValue(PropId_DataContext);
                if (const auto* oref = std::get_if<ObjectRef>(&dcVal); oref && oref->valid()) {
                    curDc = *oref;
                    break;
                }
                cur = (cur->Parent() != nullptr) ? cur->Parent() : cur->TemplatedParent();
            }
            if (!(curDc == richTextDependents_[rIdx].observedDcRef)) {
                if (richTextDependents_[rIdx].observedDcRef.valid()) {
                    if (auto* oldHeap = domains_.heapForObject(richTextDependents_[rIdx].observedDcRef)) {
                        oldHeap->removeUiObserver(richTextDependents_[rIdx].observedDcRef);
                    }
                }
                richTextDependents_[rIdx].observedDcRef = curDc;
                if (curDc.valid()) {
                    if (auto* newHeap = domains_.heapForObject(curDc)) {
                        newHeap->addUiObserver(curDc);
                    }
                }
            }
            std::string nextText = tb->GetText();
            if (rIdx < richTextDependents_.size() &&
                nextText != richTextDependents_[rIdx].lastResolvedText) {
                richTextDependents_[rIdx].lastResolvedText = std::move(nextText);
                tb->InvalidateMeasure();
                tb->InvalidateRender();
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
            eb.mode != BindingMode::TwoWay ||
            eb.sourcePropertyId == 0xFFFFu ||
            !eb.subHops.empty()) {
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

        if (lastHop.collectionIndex >= 0) {
            UnifiedValue colVal{};
            bool hasCol = false;
            if (slot.valid()) {
                hasCol = domains_.readPropertySlotFast(
                    DomainKind::Ui, targetObj, slot, colVal, nullptr, uiInterpreter_);
            } else if (lastHop.fieldIndex != PropertySlotHandle::InvalidIndex) {
                hasCol = domains_.readObjectField(
                    DomainKind::Ui, targetObj, lastHop.fieldIndex, colVal, nullptr);
            }
            const auto* colObjRef = hasCol ? std::get_if<ObjectRef>(&colVal) : nullptr;
            if (!colObjRef || !colObjRef->valid()) {
                continue;
            }
            auto* colHeap = domains_.heapForObject(*colObjRef);
            if (!colHeap || !colHeap->isAlive(*colObjRef)) {
                continue;
            }
            const ObjectRef backingArr = ObservableListHelper::ResolveBackingArray(*colHeap, *colObjRef);
            const auto idx = static_cast<std::size_t>(lastHop.collectionIndex);
            if (!backingArr.valid() || idx >= colHeap->arrayLength(backingArr).value_or(0u)) {
                continue;
            }
            UnifiedValue curElemVal{};
            bool hasCurElem = false;
            if (auto elemOpt = colHeap->arrayGet(backingArr, idx)) {
                curElemVal = *elemOpt;
                hasCurElem = true;
            }
            if (std::holds_alternative<std::monostate>(uiValue)) {
                uiValue = NullObject{};
            } else if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
                if (lastHop.targetIsEnum && lastHop.targetTypeId != 0 && program_) {
                    std::int64_t enumInt = 0;
                    if (TryResolveEnumMemberByName(program_, lastHop.targetTypeId, sv->view(), enumInt)) {
                        EnumValue curEv{};
                        if (hasCurElem && curElemVal.tryGetEnum(curEv)) {
                            uiValue = EnumValue{curEv.typeId, enumInt};
                        } else {
                            uiValue = value::FromInt(enumInt);
                        }
                    }
                } else if (hasCurElem && (std::holds_alternative<ObjectRef>(curElemVal) ||
                                          std::holds_alternative<NullObject>(curElemVal))) {
                    if (auto strObj = colHeap->allocateString(sv->str())) {
                        uiValue = *strObj;
                    }
                }
            }
            if (ObservableListHelper::Replace(*colHeap, *colObjRef, idx, uiValue, error)) {
                anyPushed = true;
            }
            continue;
        }

        // Coerce converted value to match target ViewModel slot's runtime storage kind when needed
        UnifiedValue curSlotVal{};
        const bool hasCurSlotVal = slot.valid() && domains_.readPropertySlotFast(
            DomainKind::Ui, targetObj, slot, curSlotVal, nullptr, uiInterpreter_);

        if (std::holds_alternative<std::monostate>(uiValue) && hasCurSlotVal &&
            (std::holds_alternative<ObjectRef>(curSlotVal) || std::holds_alternative<NullObject>(curSlotVal))) {
            uiValue = NullObject{};
        }

        if (hasCurSlotVal) {
            EnumValue curEv{};
            if (curSlotVal.tryGetEnum(curEv) && !std::holds_alternative<EnumValue>(uiValue)) {
                std::int64_t enumInt = curEv.value;
                if (const auto* iv = std::get_if<std::int64_t>(&uiValue)) {
                    enumInt = *iv;
                } else if (const auto* sv = std::get_if<StringValue>(&uiValue)) {
                    const SymbolId enumT = (curEv.typeId != 0) ? curEv.typeId : lastHop.targetTypeId;
                    if (!TryResolveEnumMemberByName(program_, enumT, sv->view(), enumInt)) {
                        enumInt = static_cast<std::int64_t>(std::llround(ParseStackFloat32(sv->view(), 0.0f)));
                    }
                } else {
                    float fv = 0.0f;
                    if (TryGetFloat(uiValue, fv)) {
                        enumInt = static_cast<std::int64_t>(std::llround(fv));
                    }
                }
                uiValue = EnumValue{curEv.typeId, enumInt};
            } else if (std::holds_alternative<FloatValue>(curSlotVal) && !std::holds_alternative<FloatValue>(uiValue)) {
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
                    std::int64_t enumInt = 0;
                    if (lastHop.targetIsEnum && lastHop.targetTypeId != 0 &&
                        TryResolveEnumMemberByName(program_, lastHop.targetTypeId, sv->view(), enumInt)) {
                        uiValue = value::FromInt(enumInt);
                    } else {
                        uiValue = value::FromInt(static_cast<std::int64_t>(std::llround(ParseStackFloat32(sv->view(), 0.0f))));
                    }
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

    for (auto& kv : registeredConverters_) {
        if (kv.second.receiverObject.valid() && mapObjectRef(kv.second.receiverObject)) {
            kv.second.receiverRoot.reset();
            if (kv.second.receiverObject.valid()) {
                if (auto* heap = domains_.heapForObject(kv.second.receiverObject);
                    heap && heap->isAlive(kv.second.receiverObject)) {
                    kv.second.receiverRoot = std::make_shared<PersistentRoot>(
                        heap->retain(kv.second.receiverObject));
                }
            }
        }
    }

    const std::size_t bSnapshotCount = trackABindings_.size();
    for (std::size_t bIdx = 0u; bIdx < bSnapshotCount && bIdx < trackABindings_.size(); ++bIdx) {
        auto& binding = trackABindings_[bIdx];
        ReleaseCollectionObserver(binding);
        ReleaseIndexedArrayObserversFrom(
            domains_, binding.indexedArrays, 0u, binding.mode != BindingMode::OneTime);
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
            hop.targetTypeId = 0;
            hop.targetIsEnum = false;
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
                    hop.targetTypeId = 0;
                    hop.targetIsEnum = false;
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
