#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aero/core/RsAliases.hpp"

namespace aero {

// P0-3: TypeClass uses 64-bit SymbolId (FNV-1a 64-bit hash)
// and Euler-tour intervals (treeIn / treeOut) for O(1) branchless IsDerivedFrom.
struct TypeClass {
    const char* typeName = nullptr;
    const TypeClass* baseClass = nullptr;
    SymbolId rsSymbolId = 0;
    std::uint16_t treeIn = 0;
    std::uint16_t treeOut = 0;
    std::uint16_t typeIndex = 0;
    std::uint16_t flags = 0;

    [[nodiscard]] constexpr bool IsDerivedFrom(const TypeClass* target) const noexcept {
        return target != nullptr &&
               treeIn >= target->treeIn &&
               treeOut <= target->treeOut;
    }
};

static_assert(
    sizeof(decltype(TypeClass{}.rsSymbolId)) == sizeof(std::uint64_t),
    "TypeClass::rsSymbolId must be 64-bit uint64_t to match RealScript SymbolId");

class TypeRegistry {
public:
    static TypeRegistry& Instance() noexcept {
        static TypeRegistry instance;
        return instance;
    }

    void RegisterType(
        TypeClass& typeClass,
        const char* canonicalName,
        const TypeClass* baseClass) {
        typeClass.typeName = canonicalName;
        typeClass.baseClass = baseClass;
        typeClass.rsSymbolId = rs_sem::stableTypeId(
            canonicalName ? std::string(canonicalName) : std::string{});
        for (auto* existing : types_) {
            if (existing == &typeClass) {
                RebuildEulerIntervals();
                return;
            }
        }
        typeClass.typeIndex = static_cast<std::uint16_t>(types_.size());
        types_.push_back(&typeClass);
        RebuildEulerIntervals();
    }

    void RebuildEulerIntervals() noexcept {
        std::uint16_t timer = 0;
        for (auto* t : types_) {
            if (t->baseClass == nullptr) {
                DfsEuler(t, timer);
            }
        }
    }

    [[nodiscard]] const TypeClass* FindBySymbolId(SymbolId symbolId) const noexcept {
        for (const auto* t : types_) {
            if (t->rsSymbolId == symbolId) {
                return t;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const TypeClass* FindByName(std::string_view name) const noexcept {
        for (const auto* t : types_) {
            if (t->typeName && std::string_view(t->typeName) == name) {
                return t;
            }
        }
        return nullptr;
    }

private:
    TypeRegistry() = default;

    void DfsEuler(TypeClass* node, std::uint16_t& timer) noexcept {
        ++timer;
        node->treeIn = timer;
        for (auto* candidate : types_) {
            if (candidate->baseClass == node) {
                DfsEuler(candidate, timer);
            }
        }
        node->treeOut = timer;
    }

    std::vector<TypeClass*> types_;
};

} // namespace aero
