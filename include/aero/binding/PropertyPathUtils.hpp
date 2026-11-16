#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "aero/asset/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero::binding_util {

[[nodiscard]] inline const rs_sem::TypeSymbol* FindTypeSymbolByRawName(
    const ProgramImage* program,
    std::string_view rawTypeName) noexcept {
    if (!program || rawTypeName.empty()) {
        return nullptr;
    }
    std::string canonical;
    canonical.reserve(rawTypeName.size() + 4u);
    for (std::size_t i = 0u; i < rawTypeName.size(); ++i) {
        const char ch = rawTypeName[i];
        if (ch == '.' && (i + 1u >= rawTypeName.size() || rawTypeName[i + 1u] != '.')) {
            canonical += "::";
        } else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            canonical.push_back(ch);
        }
    }
    const rs_sem::TypeSymbol* shortMatch = nullptr;
    const std::string_view shortName = str::ShortTypeName(canonical);
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

inline void ResolveCompiledHopTargetType(
    const ProgramImage* program,
    SymbolId ownerTypeId,
    AxbCompiledHop& hop) noexcept {
    if (!program || ownerTypeId == 0 || hop.targetTypeId != 0) {
        return;
    }
    std::string memberTypeName;
    SymbolId curId = ownerTypeId;
    for (int depth = 0; depth < 16 && curId != 0; ++depth) {
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
        std::string_view elemTypeName;
        if (memberType) {
            for (const auto& f : memberType->fields) {
                if (f.name == "_items") {
                    if (str::EndsWith(f.typeName, "[]")) {
                        elemTypeName = std::string_view(f.typeName).substr(0u, f.typeName.size() - 2u);
                    }
                    break;
                }
            }
        }
        if (elemTypeName.empty()) {
            const std::string_view trimmed = str::Trim(memberTypeName);
            if (str::EndsWith(trimmed, "[]")) {
                elemTypeName = trimmed.substr(0u, trimmed.size() - 2u);
            } else {
                const auto lt = trimmed.find('<');
                const auto gt = trimmed.rfind('>');
                if (lt != std::string_view::npos && gt != std::string_view::npos && gt > lt + 1u) {
                    elemTypeName = trimmed.substr(lt + 1u, gt - lt - 1u);
                }
            }
        }
        memberType = FindTypeSymbolByRawName(program, elemTypeName);
    }
    if (memberType) {
        hop.targetTypeId = memberType->id;
        hop.targetIsEnum = (memberType->kind == rs_sem::TypeKind::Enum);
    }
}

[[nodiscard]] inline const UIElement* NextContextParent(const UIElement* cur) noexcept {
    if (!cur) {
        return nullptr;
    }
    return cur->Parent() ? cur->Parent() : cur->TemplatedParent();
}

[[nodiscard]] inline ObjectRef ResolveEffectiveDataContext(const UIElement* elem) noexcept {
    for (const UIElement* cur = elem; cur != nullptr; cur = NextContextParent(cur)) {
        const UnifiedValue dcVal = cur->GetValue(PropId_DataContext);
        if (const auto* ref = std::get_if<ObjectRef>(&dcVal)) {
            if (ref->valid()) {
                return *ref;
            }
        }
        if (cur->BoundScriptWrapper().valid()) {
            return cur->BoundScriptWrapper();
        }
    }
    return ObjectRef{};
}

[[nodiscard]] inline bool HasFocusedDescendant(const UIElement* elem) noexcept {
    if (!elem) {
        return false;
    }
    if (elem->IsFocused()) {
        return true;
    }
    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (HasFocusedDescendant(c)) {
            return true;
        }
    }
    return false;
}

} // namespace aero::binding_util
