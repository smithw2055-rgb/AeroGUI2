#include "aero/core/TypeClass.hpp"

namespace aero {

TypeRegistry& TypeRegistry::Instance() noexcept {
    static TypeRegistry instance;
    return instance;
}

void TypeRegistry::RegisterType(
    TypeClass& typeClass,
    const char* canonicalName,
    const TypeClass* baseClass,
    TypeClass::TypeFactoryFn createInstance) {
    const SymbolId symId = rs_sem::stableTypeId(
        canonicalName ? std::string(canonicalName) : std::string{});
    std::lock_guard<std::mutex> lock(mutex_);
    typeClass.typeName = canonicalName;
    typeClass.baseClass = baseClass;
    typeClass.rsSymbolId = symId;
    typeClass.createInstance = createInstance;
    bool found = false;
    for (auto* existing : types_) {
        if (existing == &typeClass) {
            found = true;
            break;
        }
    }
    if (!found) {
        typeClass.typeIndex = static_cast<std::uint16_t>(types_.size());
        types_.push_back(&typeClass);
    }
    intervalsDirty_.store(true, std::memory_order_release);
}

void TypeRegistry::RegisterAlias(std::string_view aliasName, const TypeClass* target) {
    if (aliasName.empty() || target == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : aliases_) {
        if (entry.alias == aliasName) {
            entry.target = target;
            return;
        }
    }
    aliases_.push_back(TypeAliasEntry{std::string(aliasName), target});
}

void TypeRegistry::RebuildEulerIntervals() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    RebuildEulerIntervalsUnlocked();
}

void TypeRegistry::RebuildEulerIntervalsUnlocked() const noexcept {
    for (auto* t : types_) {
        t->treeIn = 0;
        t->treeOut = 0;
    }
    std::uint16_t timer = 0;
    for (auto* t : types_) {
        if (t->baseClass == nullptr && t->treeIn == 0) {
            DfsEuler(t, timer);
        }
    }
    // Fallback for any registered orphan whose baseClass was not registered.
    for (auto* t : types_) {
        if (t->treeIn == 0) {
            DfsEuler(t, timer);
        }
    }
    intervalsDirty_.store(false, std::memory_order_release);
}

void TypeRegistry::DfsEuler(TypeClass* node, std::uint16_t& timer) const noexcept {
    if (!node || node->treeIn != 0) {
        return;
    }
    ++timer;
    node->treeIn = timer;
    for (auto* candidate : types_) {
        if (candidate->baseClass == node && candidate->treeIn == 0) {
            DfsEuler(candidate, timer);
        }
    }
    node->treeOut = timer;
}

const TypeClass* TypeRegistry::FindBySymbolId(SymbolId symbolId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (intervalsDirty_.load(std::memory_order_relaxed)) {
        RebuildEulerIntervalsUnlocked();
    }
    for (const auto* t : types_) {
        if (t->rsSymbolId == symbolId) {
            return t;
        }
    }
    return nullptr;
}

const TypeClass* TypeRegistry::FindByName(std::string_view name) const noexcept {
    if (name.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (intervalsDirty_.load(std::memory_order_relaxed)) {
        RebuildEulerIntervalsUnlocked();
    }
    for (const auto* t : types_) {
        if (t->typeName && std::string_view(t->typeName) == name) {
            return t;
        }
    }
    for (const auto& entry : aliases_) {
        if (entry.alias == name) {
            return entry.target;
        }
    }
    return nullptr;
}

void TypeRegistry::BeginBatch() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++batchDepth_;
}

void TypeRegistry::EndBatch() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (batchDepth_ > 0u) {
        --batchDepth_;
    }
    if (batchDepth_ == 0u && intervalsDirty_.load(std::memory_order_relaxed)) {
        RebuildEulerIntervalsUnlocked();
    }
}

ScopedTypeBatch::ScopedTypeBatch() noexcept {
    TypeRegistry::Instance().BeginBatch();
}

ScopedTypeBatch::~ScopedTypeBatch() noexcept {
    TypeRegistry::Instance().EndBatch();
}

} // namespace aero
