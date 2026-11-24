#include "aero/compiler/MirReachabilityPruner.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "realscript/diagnostics/Diagnostic.h"

namespace aero {
namespace {

constexpr std::uint32_t kInvalidSlot = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool IsPrunableStdlibModule(std::string_view moduleName) noexcept {
    return moduleName == "Aero.UI" || moduleName == "Aero.Native" ||
           (moduleName.size() > 15u && moduleName.substr(0u, 15u) == "Aero.UI.__spec.");
}

[[nodiscard]] bool IsPrunableFunctionOnlyModule(std::string_view moduleName) noexcept {
    return moduleName == "RealScript.Math";
}

[[nodiscard]] bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0u, prefix.size()) == prefix;
}

[[nodiscard]] bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size(), suffix.size()) == suffix;
}

[[nodiscard]] std::string ExtractOwnerTypeFromFunctionName(std::string_view fnName) {
    if (EndsWith(fnName, "..ctor")) {
        return std::string(fnName.substr(0u, fnName.size() - 6u));
    }
    if (EndsWith(fnName, "..cctor")) {
        return std::string(fnName.substr(0u, fnName.size() - 7u));
    }
    std::size_t angleDepth = 0u;
    std::size_t lastDot = std::string_view::npos;
    for (std::size_t i = 0u; i < fnName.size(); ++i) {
        const char ch = fnName[i];
        if (ch == '<') {
            ++angleDepth;
        } else if (ch == '>' && angleDepth > 0u) {
            --angleDepth;
        } else if (ch == '.' && angleDepth == 0u) {
            lastDot = i;
        }
    }
    if (lastDot == std::string_view::npos || lastDot == 0u) {
        return {};
    }
    return std::string(fnName.substr(0u, lastDot));
}

constexpr std::array<std::string_view, 24> kColdSubsystemPropertyNames = {
    "ContextMenu",
    "ToolTip",
    "ToolTipBox",
    "HasToolTip",
    "Projection",
    "Transform3D",
    "LayoutTransform",
    "OpacityMask",
    "Effect",
    "InputBindings",
    "RiveSource",
    "LocSource",
    "LocalizationSource",
    "LocResources",
    "AllowDrop",
    "CommandTarget",
    "Uid",
    "Behaviors",
    "Triggers",
    "VisualStateGroups",
    "CacheMode",
    "ManipulationMode",
    "Cursor",
    "FocusVisualStyle",
};

[[nodiscard]] int FindColdSubsystemPropIndex(std::string_view propName) noexcept {
    for (std::size_t i = 0u; i < kColdSubsystemPropertyNames.size(); ++i) {
        if (kColdSubsystemPropertyNames[i] == propName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] bool IsOptionalUiElementPropertyName(
    std::string_view propName,
    std::uint32_t referencedColdPropMask) noexcept {
    const int idx = FindColdSubsystemPropIndex(propName);
    if (idx < 0) {
        return false;
    }
    return (referencedColdPropMask & (1u << static_cast<unsigned>(idx))) == 0u;
}

[[nodiscard]] bool IsOptionalUiElementAccessorName(
    std::string_view methodName,
    std::uint32_t referencedColdPropMask) noexcept {
    if (StartsWith(methodName, "get_") || StartsWith(methodName, "set_")) {
        const std::string_view propName = methodName.substr(4u);
        return IsOptionalUiElementPropertyName(propName, referencedColdPropMask);
    }
    return false;
}

[[nodiscard]] bool IsHostRootMethod(
    const rs_sem::TypeSymbol& owner,
    const rs_sem::FunctionSymbol& method,
    const std::unordered_map<SymbolId, const rs_sem::TypeSymbol*>& typeById,
    const std::unordered_map<SymbolId, SymbolId>& funcIdToDeclaringTypeId,
    std::uint32_t referencedColdPropMask) noexcept {
    if (method.constructor || method.name == ".ctor" || method.name == ".cctor" ||
        method.name == owner.name) {
        return true;
    }
    if (method.propertyGetter || method.propertySetter ||
        StartsWith(method.name, "get_") || StartsWith(method.name, "set_")) {
        bool declaredInAeroUi = (owner.moduleName == "Aero.UI");
        if (!declaredInAeroUi && method.id != 0u) {
            if (const auto dIt = funcIdToDeclaringTypeId.find(method.id); dIt != funcIdToDeclaringTypeId.end()) {
                if (const auto tIt = typeById.find(dIt->second); tIt != typeById.end() && tIt->second->moduleName == "Aero.UI") {
                    declaredInAeroUi = true;
                }
            }
        }
        if (declaredInAeroUi && IsOptionalUiElementAccessorName(method.name, referencedColdPropMask)) {
            return false;
        }
        return true;
    }
    if (owner.delegateType && method.name == "Invoke") {
        return true;
    }
    // Reflect_InvokeCommand dynamically invokes CanExecute / Execute on command objects.
    if (method.name == "CanExecute" || method.name == "Execute") {
        return true;
    }
    if (owner.moduleName == "Aero.UI") {
        if (owner.name == "InputDispatcher" || owner.name == "BindingEngine") {
            return true;
        }
        if (owner.name == "Window" && method.name == "Show") {
            return true;
        }
        if (owner.name == "UIElement" &&
            (method.name == "EnsureLoadedSubtree" || method.name == "ApplyTemplateSubtree")) {
            return true;
        }
        if (owner.name == "FrameworkElement" && method.name == "ApplyTemplate") {
            return true;
        }
        if (owner.name == "Application" && method.name == "Run") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ShouldRetainMethodOnType(
    const rs_sem::TypeSymbol& owner,
    const rs_sem::FunctionSymbol& method,
    const std::unordered_set<SymbolId>& liveFuncIds,
    const std::unordered_set<SymbolId>& dispatchSlotIds,
    bool isInstantiatedOwner,
    const std::unordered_map<SymbolId, const rs_sem::TypeSymbol*>& typeById,
    const std::unordered_map<SymbolId, SymbolId>& funcIdToDeclaringTypeId,
    std::uint32_t referencedColdPropMask) noexcept {
    if (owner.delegateType && method.name == "Invoke") {
        return true;
    }
    if (method.virtualMethod || method.overrideMethod || method.abstractMethod ||
        method.virtualSlot != kInvalidSlot ||
        method.interfaceMethod || method.interfaceSlot != kInvalidSlot) {
        return true;
    }
    if (method.id != 0u && dispatchSlotIds.count(method.id) != 0u) {
        return true;
    }
    if (isInstantiatedOwner &&
        IsHostRootMethod(owner, method, typeById, funcIdToDeclaringTypeId, referencedColdPropMask)) {
        return true;
    }
    if (liveFuncIds.count(method.id) != 0u) {
        const auto dtIt = funcIdToDeclaringTypeId.find(method.id);
        if (dtIt == funcIdToDeclaringTypeId.end() || dtIt->second == owner.id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::unordered_set<SymbolId> CollectDispatchSlotIds(
    const rs_sem::TypeSymbol& type) {
    std::unordered_set<SymbolId> slots;
    for (const SymbolId targetId : type.virtualDispatchTable) {
        if (targetId != 0u) {
            slots.insert(targetId);
        }
    }
    for (const auto& imap : type.interfaceDispatchMaps) {
        for (const SymbolId targetId : imap.slots) {
            if (targetId != 0u) {
                slots.insert(targetId);
            }
        }
    }
    return slots;
}

[[nodiscard]] std::size_t CountDistinctTypes(const std::vector<rs_mir::Module>& modules) {
    std::unordered_set<SymbolId> uniqueIds;
    for (const auto& mod : modules) {
        for (const auto& t : mod.types) {
            if (t.id != 0u) {
                uniqueIds.insert(t.id);
            }
        }
    }
    return uniqueIds.size();
}

[[nodiscard]] std::size_t CountTotalFunctions(const std::vector<rs_mir::Module>& modules) {
    std::size_t total = 0u;
    for (const auto& mod : modules) {
        total += mod.functions.size();
    }
    return total;
}

[[nodiscard]] bool ShouldKeepAttributeRecord(
    const rs_comp::LanguageAttributeRecord& attr,
    std::string_view moduleName,
    const std::unordered_set<std::string>& liveCanonicalTypes,
    const std::unordered_set<std::string>& retainedQualifiedFunctions) {
    if (attr.target.empty() || attr.target == moduleName) {
        return true;
    }
    const std::string fnPrefix = std::string(moduleName) + "::function:";
    if (StartsWith(attr.target, fnPrefix)) {
        std::string_view tail = std::string_view(attr.target).substr(fnPrefix.size());
        if (const std::size_t hashPos = tail.find('#'); hashPos != std::string_view::npos) {
            tail = tail.substr(0u, hashPos);
        }
        const std::string qualFn = std::string(moduleName) + "::" + std::string(tail);
        return retainedQualifiedFunctions.count(qualFn) != 0u;
    }
    const std::string modPrefix = std::string(moduleName) + "::";
    if (StartsWith(attr.target, modPrefix)) {
        std::string_view rest = std::string_view(attr.target).substr(modPrefix.size());
        if (const std::size_t subSep = rest.find("::"); subSep != std::string_view::npos) {
            rest = rest.substr(0u, subSep);
        }
        const std::string canonType = modPrefix + std::string(rest);
        return liveCanonicalTypes.count(canonType) != 0u;
    }
    return true;
}

} // namespace

bool PruneUnreachableStdlibMir(
    std::vector<rs_mir::Module>& mirModules,
    MirPruneStats* outStats) {
    MirPruneStats stats;
    stats.totalTypesBefore = CountDistinctTypes(mirModules);
    stats.totalFunctionsBefore = CountTotalFunctions(mirModules);

    const bool hasPrunableStdlib = std::any_of(
        mirModules.begin(),
        mirModules.end(),
        [](const rs_mir::Module& mod) {
            return IsPrunableStdlibModule(mod.name) || IsPrunableFunctionOnlyModule(mod.name);
        });
    if (!hasPrunableStdlib) {
        stats.retainedTypes = stats.totalTypesBefore;
        stats.retainedFunctions = stats.totalFunctionsBefore;
        if (outStats) {
            *outStats = stats;
        }
        return true;
    }

    struct ModuleMutationBackup final {
        std::size_t index = 0u;
        bool fullModule = false;
        rs_mir::Module moduleBackup;
        std::vector<rs_sem::TypeSymbol> typesBackup;
    };
    std::vector<ModuleMutationBackup> backups;
    backups.reserve(mirModules.size());
    for (std::size_t i = 0u; i < mirModules.size(); ++i) {
        const auto& mod = mirModules[i];
        if (IsPrunableStdlibModule(mod.name) || IsPrunableFunctionOnlyModule(mod.name)) {
            backups.push_back(ModuleMutationBackup{i, true, mod, {}});
        } else {
            const bool hasImportedStdlibTypes = std::any_of(
                mod.types.begin(),
                mod.types.end(),
                [&](const rs_sem::TypeSymbol& t) {
                    const std::string_view ownerMod =
                        !t.moduleName.empty() ? std::string_view(t.moduleName) : std::string_view(mod.name);
                    return IsPrunableStdlibModule(ownerMod);
                });
            if (hasImportedStdlibTypes) {
                backups.push_back(ModuleMutationBackup{i, false, {}, mod.types});
            }
        }
    }

    // 1. Index all types, array-to-element mappings, functions, and declaring types.
    std::unordered_map<SymbolId, const rs_sem::TypeSymbol*> typeById;
    std::unordered_map<std::string, SymbolId> typeIdByCanonicalName;
    for (const auto& mod : mirModules) {
        for (const auto& t : mod.types) {
            if (t.id == 0u) {
                continue;
            }
            if (t.moduleName == mod.name) {
                typeById[t.id] = &t;
            } else {
                typeById.emplace(t.id, &t);
            }
        }
    }
    for (const auto& [tid, tPtr] : typeById) {
        const std::string canon = rs_sem::canonicalTypeName(*tPtr);
        if (!canon.empty()) {
            typeIdByCanonicalName.emplace(canon, tid);
        }
        if (tPtr->moduleName.empty() && !tPtr->name.empty()) {
            typeIdByCanonicalName.emplace(tPtr->name, tid);
        }
    }

    std::unordered_map<SymbolId, SymbolId> arrayTypeIdToElementTypeId;
    for (const auto& [tid, tPtr] : typeById) {
        PrimitiveType elemPrim = PrimitiveType::Object;
        if (tPtr->kind == rs_sem::TypeKind::Struct) {
            elemPrim = PrimitiveType::Struct;
        } else if (tPtr->kind == rs_sem::TypeKind::Enum) {
            elemPrim = PrimitiveType::Enum;
        }
        const std::string canon = rs_sem::canonicalTypeName(*tPtr);
        const std::string arrName = rs_sem::arrayTypeName(elemPrim, canon);
        arrayTypeIdToElementTypeId.emplace(rs_sem::stableTypeId(arrName), tid);
    }

    std::unordered_map<SymbolId, const rs_mir::Function*> funcById;
    std::unordered_map<std::string, std::vector<SymbolId>> funcIdsByName;
    for (const auto& mod : mirModules) {
        for (const auto& fn : mod.functions) {
            if (fn.symbolId != 0u) {
                funcById.emplace(fn.symbolId, &fn);
            }
            if (!mod.name.empty()) {
                funcIdsByName[mod.name + "::" + fn.name].push_back(fn.symbolId);
            }
            if (!fn.moduleName.empty() && fn.moduleName != mod.name) {
                funcIdsByName[fn.moduleName + "::" + fn.name].push_back(fn.symbolId);
            }
            funcIdsByName[fn.name].push_back(fn.symbolId);
        }
    }

    // Pass 1: Authoritative owner resolution from lowered MIR function names ("Type.Method").
    // Inherited methods live inside derived TypeSymbol::methods with the SAME SymbolId,
    // so we MUST resolve declaring type from MIR function names before inspecting TypeSymbol::methods.
    std::unordered_map<SymbolId, SymbolId> funcIdToDeclaringTypeId;
    for (const auto& mod : mirModules) {
        for (const auto& fn : mod.functions) {
            if (fn.symbolId == 0u || funcIdToDeclaringTypeId.count(fn.symbolId) != 0u) {
                continue;
            }
            const std::string owner = ExtractOwnerTypeFromFunctionName(fn.name);
            if (owner.empty()) {
                continue;
            }
            const std::string_view modPrefix = !fn.moduleName.empty()
                ? std::string_view(fn.moduleName)
                : std::string_view(mod.name);
            const std::string canon = modPrefix.empty()
                ? owner
                : (std::string(modPrefix) + "::" + owner);
            if (const auto it = typeIdByCanonicalName.find(canon); it != typeIdByCanonicalName.end()) {
                funcIdToDeclaringTypeId.emplace(fn.symbolId, it->second);
            }
        }
    }
    // Pass 2: Fallback for constructors/accessors/methods without an extractable owner prefix.
    for (const auto& [tid, tPtr] : typeById) {
        for (const auto& c : tPtr->constructors) {
            if (c.id != 0u) {
                funcIdToDeclaringTypeId.emplace(c.id, tid);
            }
        }
        if (tPtr->staticConstructor.has_value() && tPtr->staticConstructor->id != 0u) {
            funcIdToDeclaringTypeId.emplace(tPtr->staticConstructor->id, tid);
        }
    }
    for (const auto& [tid, tPtr] : typeById) {
        for (const auto& m : tPtr->methods) {
            if (m.id != 0u && !m.overrideMethod) {
                funcIdToDeclaringTypeId.emplace(m.id, tid);
            }
        }
        for (const auto& p : tPtr->properties) {
            if (p.getter.has_value() && p.getter->id != 0u && !p.getter->overrideMethod) {
                funcIdToDeclaringTypeId.emplace(p.getter->id, tid);
            }
            if (p.setter.has_value() && p.setter->id != 0u && !p.setter->overrideMethod) {
                funcIdToDeclaringTypeId.emplace(p.setter->id, tid);
            }
        }
    }

    // 2. Worklists & reachability closure helpers.
    std::unordered_set<SymbolId> liveTypeIds;
    std::unordered_set<SymbolId> instantiatedTypeIds;
    std::unordered_set<SymbolId> liveFuncIds;
    std::vector<SymbolId> typeQueue;
    std::vector<SymbolId> funcQueue;

    auto markTypeId = [&](SymbolId id) {
        while (id != 0u) {
            if (typeById.find(id) != typeById.end()) {
                if (liveTypeIds.insert(id).second) {
                    typeQueue.push_back(id);
                }
                return;
            }
            const auto arrIt = arrayTypeIdToElementTypeId.find(id);
            if (arrIt == arrayTypeIdToElementTypeId.end()) {
                return;
            }
            id = arrIt->second;
        }
    };

    auto markInstantiatedTypeId = [&](SymbolId id) {
        while (id != 0u) {
            const auto tIt = typeById.find(id);
            if (tIt == typeById.end()) {
                const auto arrIt = arrayTypeIdToElementTypeId.find(id);
                if (arrIt == arrayTypeIdToElementTypeId.end()) {
                    return;
                }
                id = arrIt->second;
                continue;
            }
            markTypeId(id);
            if (instantiatedTypeIds.insert(id).second) {
                typeQueue.push_back(id);
            }
            id = tIt->second->baseTypeId;
        }
    };

    auto markTypeByName = [&](std::string_view name, std::string_view fallbackModule) {
        if (name.empty()) {
            return;
        }
        const std::string key(name);
        if (const auto it = typeIdByCanonicalName.find(key); it != typeIdByCanonicalName.end()) {
            markTypeId(it->second);
            return;
        }
        if (name.find("::") == std::string_view::npos) {
            if (!fallbackModule.empty()) {
                const std::string qualified = std::string(fallbackModule) + "::" + key;
                if (const auto it = typeIdByCanonicalName.find(qualified); it != typeIdByCanonicalName.end()) {
                    markTypeId(it->second);
                    return;
                }
            }
            const std::string uiQualified = "Aero.UI::" + key;
            if (const auto it = typeIdByCanonicalName.find(uiQualified); it != typeIdByCanonicalName.end()) {
                markTypeId(it->second);
                return;
            }
        }
        markTypeId(rs_sem::stableTypeId(key));
    };

    auto markTypeRef = [&](PrimitiveType prim, std::string_view typeName, std::string_view fallbackModule) {
        if (prim == PrimitiveType::Array && !typeName.empty()) {
            PrimitiveType elemPrim = PrimitiveType::Error;
            std::string elemTypeName;
            if (rs_sem::decodeArrayTypeName(std::string(typeName), elemPrim, elemTypeName) &&
                !elemTypeName.empty()) {
                markTypeByName(elemTypeName, fallbackModule);
            }
            return;
        }
        if ((rs_sem::isExactType(prim) || prim == PrimitiveType::Handle) && !typeName.empty()) {
            markTypeByName(typeName, fallbackModule);
        }
    };

    auto markFuncId = [&](SymbolId id) {
        if (id == 0u) {
            return;
        }
        if (funcById.find(id) != funcById.end() && liveFuncIds.insert(id).second) {
            funcQueue.push_back(id);
        }
    };

    auto markFuncByName = [&](std::string_view name) {
        if (name.empty()) {
            return;
        }
        if (const auto it = funcIdsByName.find(std::string(name)); it != funcIdsByName.end()) {
            for (const SymbolId id : it->second) {
                markFuncId(id);
            }
        }
    };

    // 3. Seed roots: all non-stdlib modules + collect user reflection/binding property tokens.
    std::unordered_set<std::string> userReferencedTokens;
    auto collectTokensFromString = [&](std::string_view sv) {
        std::size_t i = 0u;
        while (i < sv.size()) {
            while (i < sv.size() &&
                   !((sv[i] >= 'A' && sv[i] <= 'Z') || (sv[i] >= 'a' && sv[i] <= 'z') || sv[i] == '_')) {
                ++i;
            }
            const std::size_t start = i;
            while (i < sv.size() &&
                   ((sv[i] >= 'A' && sv[i] <= 'Z') || (sv[i] >= 'a' && sv[i] <= 'z') ||
                    (sv[i] >= '0' && sv[i] <= '9') || sv[i] == '_')) {
                ++i;
            }
            if (i > start && (i - start) <= 64u) {
                const std::string_view tok = sv.substr(start, i - start);
                userReferencedTokens.emplace(tok);
                if ((StartsWith(tok, "get_") || StartsWith(tok, "set_")) && tok.size() > 4u) {
                    userReferencedTokens.emplace(tok.substr(4u));
                }
            }
        }
    };

    for (const auto& mod : mirModules) {
        if (IsPrunableStdlibModule(mod.name) || IsPrunableFunctionOnlyModule(mod.name)) {
            continue;
        }
        for (const auto& fn : mod.functions) {
            markFuncId(fn.symbolId);
            for (const auto& bb : fn.blocks) {
                for (const auto& inst : bb.instructions) {
                    if (!inst.stringImmediate.empty()) {
                        collectTokensFromString(inst.stringImmediate);
                    }
                    if (!inst.symbolName.empty()) {
                        collectTokensFromString(inst.symbolName);
                    }
                }
            }
        }
        for (const auto& t : mod.types) {
            if (!IsPrunableStdlibModule(t.moduleName)) {
                markInstantiatedTypeId(t.id);
            }
        }
    }

    std::uint32_t referencedColdPropMask = 0u;
    for (std::size_t idx = 0u; idx < kColdSubsystemPropertyNames.size(); ++idx) {
        if (userReferencedTokens.count(std::string(kColdSubsystemPropertyNames[idx])) != 0u) {
            referencedColdPropMask |= (1u << idx);
        }
    }

    static constexpr std::array<const char*, 11> kHostRootTypes = {
        "Aero.UI::InputDispatcher",
        "Aero.UI::BindingEngine",
        "Aero.UI::DependencyObject",
        "Aero.UI::BoxedString",
        "Aero.UI::BoxedFloat",
        "Aero.UI::BoxedInt",
        "Aero.UI::BoxedBool",
        "Aero.UI::CollectionBox",
        "Aero.UI::NativeObjectBox",
        "Aero.UI::UIElement",
        "Aero.UI::FrameworkElement",
    };
    for (const char* rootType : kHostRootTypes) {
        if (const auto it = typeIdByCanonicalName.find(rootType); it != typeIdByCanonicalName.end()) {
            markInstantiatedTypeId(it->second);
        } else {
            markTypeByName(rootType, "Aero.UI");
        }
    }

    // Also seed free static methods of InputDispatcher and BindingEngine in case
    // any helper is emitted outside TypeSymbol::methods.
    for (const auto& mod : mirModules) {
        if (mod.name != "Aero.UI") {
            continue;
        }
        for (const auto& fn : mod.functions) {
            if (StartsWith(fn.name, "InputDispatcher.") ||
                StartsWith(fn.name, "BindingEngine.")) {
                markFuncId(fn.symbolId);
            }
        }
    }

    // 4. Fixed-point closure propagation.
    while (!typeQueue.empty() || !funcQueue.empty()) {
        while (!typeQueue.empty()) {
            const SymbolId tid = typeQueue.back();
            typeQueue.pop_back();
            const auto tIt = typeById.find(tid);
            if (tIt == typeById.end()) {
                continue;
            }
            const rs_sem::TypeSymbol& type = *tIt->second;

            markTypeId(type.baseTypeId);
            markTypeByName(type.baseTypeName, type.moduleName);

            for (const auto& ifaceName : type.baseInterfaceNames) {
                markTypeByName(ifaceName, type.moduleName);
            }
            for (const auto& imap : type.interfaceDispatchMaps) {
                markTypeId(imap.interfaceTypeId);
                for (const SymbolId targetId : imap.slots) {
                    markFuncId(targetId);
                }
            }
            for (const SymbolId targetId : type.virtualDispatchTable) {
                markFuncId(targetId);
            }
            for (const auto& field : type.fields) {
                markTypeRef(field.type, field.typeName, type.moduleName);
            }
            if (type.staticConstructor.has_value()) {
                markFuncId(type.staticConstructor->id);
            }
            const bool isInstantiated = instantiatedTypeIds.count(tid) != 0u;
            if (isInstantiated) {
                for (const auto& ctor : type.constructors) {
                    markFuncId(ctor.id);
                }
                auto isDeclaredInAeroUi = [&](SymbolId accId) noexcept -> bool {
                    if (type.moduleName == "Aero.UI") return true;
                    if (accId == 0u) return false;
                    const auto dIt = funcIdToDeclaringTypeId.find(accId);
                    if (dIt == funcIdToDeclaringTypeId.end()) return false;
                    const auto tIt = typeById.find(dIt->second);
                    return tIt != typeById.end() && tIt->second->moduleName == "Aero.UI";
                };
                for (const auto& prop : type.properties) {
                    const bool aeroUiProp =
                        (prop.getter.has_value() && isDeclaredInAeroUi(prop.getter->id)) ||
                        (prop.setter.has_value() && isDeclaredInAeroUi(prop.setter->id)) ||
                        (type.moduleName == "Aero.UI");
                    if (aeroUiProp && IsOptionalUiElementPropertyName(prop.name, referencedColdPropMask)) {
                        continue;
                    }
                    if (prop.getter.has_value()) {
                        markFuncId(prop.getter->id);
                    }
                    if (prop.setter.has_value()) {
                        markFuncId(prop.setter->id);
                    }
                }
            }
            for (const auto& method : type.methods) {
                const bool isDelegateInvoke = (type.delegateType && method.name == "Invoke");
                if ((isInstantiated &&
                     IsHostRootMethod(type, method, typeById, funcIdToDeclaringTypeId, referencedColdPropMask)) ||
                    isDelegateInvoke) {
                    markFuncId(method.id);
                }
                const bool isSlotOrVirtual =
                    isDelegateInvoke ||
                    method.virtualMethod || method.overrideMethod || method.abstractMethod ||
                    method.virtualSlot != kInvalidSlot ||
                    method.interfaceMethod || method.interfaceSlot != kInvalidSlot;
                if (isSlotOrVirtual) {
                    // Retain parameter & return types for virtual/interface slots that remain
                    // on the live TypeSymbol even when no concrete body exists (abstract/interface).
                    markTypeRef(
                        rs_sem::storageReturnTypeOf(method),
                        rs_sem::storageReturnTypeNameOf(method),
                        type.moduleName);
                    for (const auto& param : method.parameters) {
                        markTypeRef(
                            rs_sem::storageTypeOf(param),
                            rs_sem::storageTypeNameOf(param),
                            type.moduleName);
                    }
                }
            }
        }

        while (!funcQueue.empty()) {
            const SymbolId fid = funcQueue.back();
            funcQueue.pop_back();
            const auto fIt = funcById.find(fid);
            if (fIt == funcById.end()) {
                continue;
            }
            const rs_mir::Function& fn = *fIt->second;

            if (const auto dtIt = funcIdToDeclaringTypeId.find(fid); dtIt != funcIdToDeclaringTypeId.end()) {
                if (EndsWith(fn.name, "..ctor")) {
                    markInstantiatedTypeId(dtIt->second);
                } else {
                    markTypeId(dtIt->second);
                }
            }

            markTypeId(fn.returnTypeId);
            for (const SymbolId pid : fn.parameterTypeIds) {
                markTypeId(pid);
            }
            for (const SymbolId lid : fn.localTypeIds) {
                markTypeId(lid);
            }
            for (const auto& eh : fn.exceptionHandlers) {
                markTypeId(eh.catchTypeId);
            }

            for (const auto& bb : fn.blocks) {
                for (const auto& bp : bb.parameters) {
                    markTypeId(bp.typeId);
                }
                for (const auto& inst : bb.instructions) {
                    markTypeId(inst.resultTypeId);
                    markTypeId(inst.typeId);
                    markTypeId(inst.elementTypeId);
                    markTypeId(inst.interfaceTypeId);
                    for (const SymbolId pid : inst.parameterTypeIds) {
                        markTypeId(pid);
                    }
                    if (inst.opcode == rs_mir::Opcode::NewObject ||
                        inst.opcode == rs_mir::Opcode::NewStruct ||
                        inst.opcode == rs_mir::Opcode::NewDelegate) {
                        markInstantiatedTypeId(inst.typeId);
                        markInstantiatedTypeId(inst.resultTypeId);
                    }
                    if (inst.opcode == rs_mir::Opcode::ConstantTypeId && inst.integerImmediate != 0) {
                        markInstantiatedTypeId(static_cast<SymbolId>(inst.integerImmediate));
                    }
                    if (inst.symbolId != 0u) {
                        markFuncId(inst.symbolId);
                    }
                    if (!inst.symbolName.empty()) {
                        markFuncByName(inst.symbolName);
                    }
                }
            }
        }
    }

    // 5. Prune stdlib types (including imported copies across user modules),
    //    prune unreachable Aero.UI / Aero.Native functions, and clean stale metadata.
    std::unordered_set<std::string> liveCanonicalTypes;
    liveCanonicalTypes.reserve(liveTypeIds.size());
    for (const SymbolId tid : liveTypeIds) {
        if (const auto it = typeById.find(tid); it != typeById.end()) {
            liveCanonicalTypes.insert(rs_sem::canonicalTypeName(*it->second));
        }
    }

    for (auto& mod : mirModules) {
        // Prune unreachable Aero.UI / Aero.Native TypeSymbols and dead non-virtual methods
        // uniformly across ALL modules so ProgramImage::validateObjectModel sameTypeContract holds.
        mod.types.erase(
            std::remove_if(
                mod.types.begin(),
                mod.types.end(),
                [&](const rs_sem::TypeSymbol& t) {
                    const std::string_view ownerMod =
                        !t.moduleName.empty() ? std::string_view(t.moduleName) : std::string_view(mod.name);
                    return IsPrunableStdlibModule(ownerMod) && liveTypeIds.count(t.id) == 0u;
                }),
            mod.types.end());

        for (auto& t : mod.types) {
            const std::string_view ownerMod =
                !t.moduleName.empty() ? std::string_view(t.moduleName) : std::string_view(mod.name);
            if (!IsPrunableStdlibModule(ownerMod)) {
                continue;
            }
            const auto slotIds = CollectDispatchSlotIds(t);
            const bool isInstantiatedOwner = instantiatedTypeIds.count(t.id) != 0u;
            t.methods.erase(
                std::remove_if(
                    t.methods.begin(),
                    t.methods.end(),
                    [&](const rs_sem::FunctionSymbol& m) {
                        return !ShouldRetainMethodOnType(
                            t, m, liveFuncIds, slotIds, isInstantiatedOwner, typeById, funcIdToDeclaringTypeId, referencedColdPropMask);
                    }),
                t.methods.end());
            t.constructors.erase(
                std::remove_if(
                    t.constructors.begin(),
                    t.constructors.end(),
                    [&](const rs_sem::FunctionSymbol& ctor) {
                        return liveFuncIds.count(ctor.id) == 0u;
                    }),
                t.constructors.end());
            t.properties.erase(
                std::remove_if(
                    t.properties.begin(),
                    t.properties.end(),
                    [&](rs_sem::PropertySymbol& prop) {
                        if (prop.getter.has_value() && liveFuncIds.count(prop.getter->id) == 0u) {
                            prop.getter.reset();
                        }
                        if (prop.setter.has_value() && liveFuncIds.count(prop.setter->id) == 0u) {
                            prop.setter.reset();
                        }
                        return !prop.getter.has_value() && !prop.setter.has_value();
                    }),
                t.properties.end());
        }

        if (!IsPrunableStdlibModule(mod.name) && !IsPrunableFunctionOnlyModule(mod.name)) {
            // Shrink non-stdlib modules' imported Aero.UI / Aero.Native TypeSymbol copies
            // down to ONLY the structural closure required by this module's own code/types.
            std::unordered_map<SymbolId, const rs_sem::TypeSymbol*> modTypeById;
            std::unordered_map<std::string, SymbolId> modTypeIdByName;
            modTypeById.reserve(mod.types.size());
            modTypeIdByName.reserve(mod.types.size() * 2u);
            for (const auto& t : mod.types) {
                if (t.id == 0u) {
                    continue;
                }
                modTypeById[t.id] = &t;
                const std::string canon = rs_sem::canonicalTypeName(t);
                if (!canon.empty()) {
                    modTypeIdByName.emplace(canon, t.id);
                }
                if (t.moduleName.empty() && !t.name.empty()) {
                    modTypeIdByName.emplace(t.name, t.id);
                }
            }
            std::unordered_set<SymbolId> localLiveTypes;
            std::vector<SymbolId> localQueue;
            auto markLocalId = [&](SymbolId id) {
                while (id != 0u) {
                    if (modTypeById.find(id) != modTypeById.end()) {
                        if (localLiveTypes.insert(id).second) {
                            localQueue.push_back(id);
                        }
                        return;
                    }
                    const auto arrIt = arrayTypeIdToElementTypeId.find(id);
                    if (arrIt == arrayTypeIdToElementTypeId.end()) {
                        return;
                    }
                    id = arrIt->second;
                }
            };
            auto markLocalName = [&](std::string_view name, std::string_view fallbackMod) {
                if (name.empty()) {
                    return;
                }
                const std::string key(name);
                if (const auto it = modTypeIdByName.find(key); it != modTypeIdByName.end()) {
                    markLocalId(it->second);
                    return;
                }
                if (name.find("::") == std::string_view::npos && !fallbackMod.empty()) {
                    const std::string qual = std::string(fallbackMod) + "::" + key;
                    if (const auto it = modTypeIdByName.find(qual); it != modTypeIdByName.end()) {
                        markLocalId(it->second);
                        return;
                    }
                }
                markLocalId(rs_sem::stableTypeId(key));
            };
            auto markLocalRef = [&](PrimitiveType prim, std::string_view typeName, std::string_view fallbackMod) {
                if (prim == PrimitiveType::Array && !typeName.empty()) {
                    PrimitiveType elemPrim = PrimitiveType::Error;
                    std::string elemName;
                    if (rs_sem::decodeArrayTypeName(std::string(typeName), elemPrim, elemName) &&
                        !elemName.empty()) {
                        markLocalName(elemName, fallbackMod);
                    }
                    return;
                }
                if ((rs_sem::isExactType(prim) || prim == PrimitiveType::Handle) && !typeName.empty()) {
                    markLocalName(typeName, fallbackMod);
                }
            };

            for (const auto& t : mod.types) {
                const std::string_view ownerMod =
                    !t.moduleName.empty() ? std::string_view(t.moduleName) : std::string_view(mod.name);
                if (!IsPrunableStdlibModule(ownerMod)) {
                    markLocalId(t.id);
                }
            }
            for (const auto& fn : mod.functions) {
                markLocalId(fn.returnTypeId);
                for (const SymbolId pid : fn.parameterTypeIds) markLocalId(pid);
                for (const SymbolId lid : fn.localTypeIds) markLocalId(lid);
                for (const auto& eh : fn.exceptionHandlers) markLocalId(eh.catchTypeId);
                for (const auto& bb : fn.blocks) {
                    for (const auto& bp : bb.parameters) markLocalId(bp.typeId);
                    for (const auto& inst : bb.instructions) {
                        markLocalId(inst.resultTypeId);
                        markLocalId(inst.typeId);
                        markLocalId(inst.elementTypeId);
                        markLocalId(inst.interfaceTypeId);
                        for (const SymbolId pid : inst.parameterTypeIds) markLocalId(pid);
                        if (inst.opcode == rs_mir::Opcode::ConstantTypeId && inst.integerImmediate != 0) {
                            markLocalId(static_cast<SymbolId>(inst.integerImmediate));
                        }
                    }
                }
            }
            while (!localQueue.empty()) {
                const SymbolId tid = localQueue.back();
                localQueue.pop_back();
                const auto tIt = modTypeById.find(tid);
                if (tIt == modTypeById.end()) {
                    continue;
                }
                const rs_sem::TypeSymbol& t = *tIt->second;
                markLocalId(t.baseTypeId);
                markLocalName(t.baseTypeName, t.moduleName);
                for (const auto& ifaceName : t.baseInterfaceNames) {
                    markLocalName(ifaceName, t.moduleName);
                }
                for (const auto& imap : t.interfaceDispatchMaps) {
                    markLocalId(imap.interfaceTypeId);
                }
                for (const auto& f : t.fields) {
                    markLocalRef(f.type, f.typeName, t.moduleName);
                }
            }
            mod.types.erase(
                std::remove_if(
                    mod.types.begin(),
                    mod.types.end(),
                    [&](const rs_sem::TypeSymbol& t) {
                        const std::string_view ownerMod =
                            !t.moduleName.empty() ? std::string_view(t.moduleName) : std::string_view(mod.name);
                        return IsPrunableStdlibModule(ownerMod) && localLiveTypes.count(t.id) == 0u;
                    }),
                mod.types.end());
            continue;
        }

        mod.functions.erase(
            std::remove_if(
                mod.functions.begin(),
                mod.functions.end(),
                [&](const rs_mir::Function& fn) {
                    return liveFuncIds.count(fn.symbolId) == 0u;
                }),
            mod.functions.end());

        // Strip bulky statement-level debug sequence points/locals from pruned stdlib modules
        // (user modules retain full debugInfo).
        mod.sourceFiles.clear();
        for (auto& fn : mod.functions) {
            fn.debugInfo.sourceName.clear();
            fn.debugInfo.sourceFileId = 0u;
            fn.debugInfo.declaration = {};
            fn.debugInfo.body = {};
            fn.debugInfo.sequencePoints.clear();
            fn.debugInfo.locals.clear();
        }

        std::unordered_set<SymbolId> retainedFnIds;
        std::unordered_set<std::string> retainedQualifiedFunctions;
        retainedFnIds.reserve(mod.functions.size());
        retainedQualifiedFunctions.reserve(mod.functions.size());
        for (const auto& fn : mod.functions) {
            retainedFnIds.insert(fn.symbolId);
            retainedQualifiedFunctions.insert(mod.name + "::" + fn.name);
        }

        auto& meta = mod.languageMetadata;
        meta.nonPublicFunctions.erase(
            std::remove_if(
                meta.nonPublicFunctions.begin(),
                meta.nonPublicFunctions.end(),
                [&](const rs_comp::LanguageFunctionAccessibility& entry) {
                    return retainedFnIds.count(entry.symbolId) == 0u;
                }),
            meta.nonPublicFunctions.end());

        meta.functionInlineStamps.erase(
            std::remove_if(
                meta.functionInlineStamps.begin(),
                meta.functionInlineStamps.end(),
                [&](const rs_comp::LanguageFunctionInlineStamp& entry) {
                    return retainedFnIds.count(entry.symbolId) == 0u;
                }),
            meta.functionInlineStamps.end());

        meta.inlinedDependencies.erase(
            std::remove_if(
                meta.inlinedDependencies.begin(),
                meta.inlinedDependencies.end(),
                [&](const rs_comp::LanguageInlinedDependency& entry) {
                    return entry.callerSymbolId != 0u &&
                           retainedFnIds.count(entry.callerSymbolId) == 0u;
                }),
            meta.inlinedDependencies.end());

        if (IsPrunableFunctionOnlyModule(mod.name)) {
            continue;
        }

        meta.attributes.erase(
            std::remove_if(
                meta.attributes.begin(),
                meta.attributes.end(),
                [&](const rs_comp::LanguageAttributeRecord& entry) {
                    return !ShouldKeepAttributeRecord(
                        entry, mod.name, liveCanonicalTypes, retainedQualifiedFunctions);
                }),
            meta.attributes.end());

        meta.interfaces.erase(
            std::remove_if(
                meta.interfaces.begin(),
                meta.interfaces.end(),
                [&](const rs_comp::LanguageInterfaceImplementation& entry) {
                    return liveCanonicalTypes.count(entry.typeName) == 0u;
                }),
            meta.interfaces.end());

        meta.sequences.erase(
            std::remove_if(
                meta.sequences.begin(),
                meta.sequences.end(),
                [&](const rs_comp::LanguageSequenceRecord& entry) {
                    return liveCanonicalTypes.count(entry.typeName) == 0u;
                }),
            meta.sequences.end());

        meta.genericInstantiations.erase(
            std::remove_if(
                meta.genericInstantiations.begin(),
                meta.genericInstantiations.end(),
                [&](const rs_comp::LanguageGenericInstantiation& entry) {
                    const std::string genType = mod.name + "::" + entry.generatedName;
                    if (liveCanonicalTypes.count(genType) != 0u) {
                        return false;
                    }
                    const std::size_t dotPos = entry.genericName.find('.');
                    const std::string expectedMemberFn = (dotPos != std::string::npos)
                        ? (mod.name + "::" + entry.genericName.substr(0u, dotPos + 1u) + entry.generatedName)
                        : (mod.name + "::" + entry.generatedName);
                    return retainedQualifiedFunctions.count(expectedMemberFn) == 0u;
                }),
            meta.genericInstantiations.end());
    }

    // 6. Verify every modified module; fall back cleanly to unpruned MIR on any diagnostic.
    rs_diag::DiagnosticBag verifyDiag;
    for (const auto& b : backups) {
        const auto& mod = mirModules[b.index];
        if (!rs_mir::verifyModule(mod, verifyDiag) || verifyDiag.hasErrors()) {
            for (auto& restore : backups) {
                if (restore.fullModule) {
                    mirModules[restore.index] = std::move(restore.moduleBackup);
                } else {
                    mirModules[restore.index].types = std::move(restore.typesBackup);
                }
            }
            stats.fellBackToUnpruned = true;
            stats.retainedTypes = stats.totalTypesBefore;
            stats.prunedTypes = 0u;
            stats.retainedFunctions = stats.totalFunctionsBefore;
            stats.prunedFunctions = 0u;
            if (outStats) {
                *outStats = stats;
            }
            return false;
        }
    }

    stats.retainedTypes = CountDistinctTypes(mirModules);
    stats.prunedTypes = stats.totalTypesBefore >= stats.retainedTypes
        ? (stats.totalTypesBefore - stats.retainedTypes)
        : 0u;
    stats.retainedFunctions = CountTotalFunctions(mirModules);
    stats.prunedFunctions = stats.totalFunctionsBefore >= stats.retainedFunctions
        ? (stats.totalFunctionsBefore - stats.retainedFunctions)
        : 0u;
    if (outStats) {
        *outStats = stats;
    }
    return true;
}

} // namespace aero
