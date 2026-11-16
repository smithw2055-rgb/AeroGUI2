#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "aero/core/RsAliases.hpp"

namespace aero {

class AeroObject;
template <typename T>
class IntrusivePtr;

// P0-3: TypeClass uses 64-bit SymbolId (FNV-1a 64-bit hash)
// and Euler-tour intervals (treeIn / treeOut) for O(1) branchless IsDerivedFrom.
struct TypeClass {
    using TypeFactoryFn = IntrusivePtr<AeroObject> (*)();

    const char* typeName = nullptr;
    const TypeClass* baseClass = nullptr;
    SymbolId rsSymbolId = 0;
    TypeFactoryFn createInstance = nullptr;
    std::uint16_t treeIn = 0;
    std::uint16_t treeOut = 0;
    std::uint16_t typeIndex = 0;
    std::uint16_t flags = 0;

    [[nodiscard]] constexpr bool IsDerivedFrom(const TypeClass* target) const noexcept {
        return target != nullptr &&
               treeIn != 0 &&
               target->treeIn != 0 &&
               treeIn >= target->treeIn &&
               treeOut <= target->treeOut;
    }
};

static_assert(
    sizeof(decltype(TypeClass{}.rsSymbolId)) == sizeof(std::uint64_t),
    "TypeClass::rsSymbolId must be 64-bit uint64_t to match RealScript SymbolId");

class TypeRegistry {
public:
    static TypeRegistry& Instance() noexcept;

    void RegisterType(
        TypeClass& typeClass,
        const char* canonicalName,
        const TypeClass* baseClass,
        TypeClass::TypeFactoryFn createInstance = nullptr);

    void RegisterAlias(std::string_view aliasName, const TypeClass* target);

    [[nodiscard]] bool AreIntervalsDirty() const noexcept {
        return intervalsDirty_.load(std::memory_order_acquire);
    }

    void EnsureIntervalsUpToDate() const noexcept {
        if (!intervalsDirty_.load(std::memory_order_acquire)) {
            return;
        }
        RebuildEulerIntervals();
    }

    void RebuildEulerIntervals() const noexcept;

    [[nodiscard]] const TypeClass* FindBySymbolId(SymbolId symbolId) const noexcept;
    [[nodiscard]] const TypeClass* FindByName(std::string_view name) const noexcept;

private:
    friend class ScopedTypeBatch;

    struct TypeAliasEntry {
        std::string alias;
        const TypeClass* target = nullptr;
    };

    TypeRegistry() = default;

    void BeginBatch() noexcept;
    void EndBatch() noexcept;
    void RebuildEulerIntervalsUnlocked() const noexcept;
    void DfsEuler(TypeClass* node, std::uint16_t& timer) const noexcept;

    mutable std::mutex mutex_;
    std::vector<TypeClass*> types_;
    std::vector<TypeAliasEntry> aliases_;
    std::size_t batchDepth_ = 0u;
    mutable std::atomic<bool> intervalsDirty_{false};
};

// Defers Euler-tour interval rebuilding across bulk registrations until the outermost batch exits.
class ScopedTypeBatch {
public:
    ScopedTypeBatch() noexcept;
    ~ScopedTypeBatch() noexcept;

    ScopedTypeBatch(const ScopedTypeBatch&) = delete;
    ScopedTypeBatch& operator=(const ScopedTypeBatch&) = delete;
    ScopedTypeBatch(ScopedTypeBatch&&) = delete;
    ScopedTypeBatch& operator=(ScopedTypeBatch&&) = delete;
};

} // namespace aero

#define AERO_DECLARE_CLASS(Self, Base)                                              \
public:                                                                             \
    using SelfClass = Self;                                                         \
    using ParentClass = Base;                                                       \
    static const ::aero::TypeClass* StaticTypeClass() noexcept;                     \
    [[nodiscard]] const ::aero::TypeClass* GetTypeClass() const noexcept override { \
        return StaticTypeClass();                                                   \
    }

#define AERO_IMPLEMENT_CLASS(Self, Base, CanonicalName)                             \
    const ::aero::TypeClass* Self::StaticTypeClass() noexcept {                     \
        static ::aero::TypeClass sTypeClass{};                                      \
        static const bool sRegistered = []() {                                      \
            ::aero::TypeClass::TypeFactoryFn factory = nullptr;                     \
            if constexpr (std::is_default_constructible_v<Self> &&                  \
                          !std::is_abstract_v<Self>) {                              \
                factory = +[]() -> ::aero::IntrusivePtr<::aero::AeroObject> {       \
                    return ::aero::IntrusivePtr<::aero::AeroObject>(                \
                        new Self(), true);                                          \
                };                                                                  \
            }                                                                       \
            ::aero::TypeRegistry::Instance().RegisterType(                          \
                sTypeClass, CanonicalName, Base::StaticTypeClass(), factory);       \
            return true;                                                            \
        }();                                                                        \
        (void)sRegistered;                                                          \
        return &sTypeClass;                                                         \
    }
