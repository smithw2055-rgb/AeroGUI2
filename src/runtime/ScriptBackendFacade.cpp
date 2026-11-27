#include "aero/runtime/ScriptBackendFacade.hpp"

#include "aero/runtime/AeroObservableList.hpp"

#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aero {

namespace {

[[nodiscard]] std::string NormalizeDoubleColonToDot(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0u; i < s.size(); ++i) {
        if (i + 1u < s.size() && s[i] == ':' && s[i + 1u] == ':') {
            out.push_back('.');
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

[[nodiscard]] bool EndsWithSymbol(std::string_view full, std::string_view suffix) noexcept {
    if (full.size() < suffix.size() || suffix.empty()) {
        return false;
    }
    if (full == suffix) {
        return true;
    }
    if (full.compare(full.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    if (suffix.front() == ':' || suffix.front() == '.') {
        return true;
    }
    const char prev = full[full.size() - suffix.size() - 1u];
    return prev == '.' || prev == ':';
}

[[nodiscard]] std::string AotTypeQualifiedName(const rs_aot::TypeDescriptor& td) {
    std::string full;
    if (td.moduleName && td.moduleName[0] != '\0') {
        full.append(td.moduleName);
        full.append("::");
    }
    if (td.name) {
        full.append(td.name);
    }
    return full;
}

[[nodiscard]] bool FunctionNameMatchesMember(
    std::string_view fullName,
    std::string_view typeFullName,
    std::string_view typeShortName,
    std::string_view memberName) noexcept {
    const auto dotSep = fullName.rfind('.');
    if (dotSep != std::string_view::npos) {
        const std::string_view owner = fullName.substr(0u, dotSep);
        const std::string_view method = fullName.substr(dotSep + 1u);
        if (method != memberName) {
            return false;
        }
        return owner == typeFullName || owner == typeShortName;
    }
    const auto colSep = fullName.rfind("::");
    if (colSep != std::string_view::npos) {
        const std::string_view owner = fullName.substr(0u, colSep);
        const std::string_view method = fullName.substr(colSep + 2u);
        if (method != memberName) {
            return false;
        }
        return owner == typeFullName || owner == typeShortName;
    }
    return false;
}

[[nodiscard]] UnifiedValue InternIfStringValueForHeap(
    ManagedHeap& heap,
    const UnifiedValue& val) {
    if (const auto* sv = std::get_if<StringValue>(&val)) {
        if (const auto interned = heap.internString(sv->str(), nullptr)) {
            return UnifiedValue{*interned};
        }
    }
    return val;
}

struct AotMemberSlot {
    bool hasReadField = false;
    std::uint32_t readFieldIndex = 0u;
    SymbolId getterSym = 0u;
    bool hasWriteField = false;
    std::uint32_t writeFieldIndex = 0u;
    SymbolId setterSym = 0u;
};

struct AotNamedMemberEntry {
    std::string memberName;
    AotMemberSlot slot;
};

struct AotProgramReflectionCache {
    std::uint32_t typeCount = 0u;
    std::uint32_t functionCount = 0u;
    std::unordered_map<SymbolId, std::vector<AotNamedMemberEntry>> byType;
};

[[nodiscard]] AotMemberSlot ResolveAotMemberSlot(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId,
    std::string_view memberName) {
    static std::mutex sCacheMutex;
    static std::unordered_map<const rs_aot::ProgramDescriptor*, AotProgramReflectionCache> sCaches;

    {
        std::lock_guard<std::mutex> lock(sCacheMutex);
        const auto progIt = sCaches.find(&prog);
        if (progIt != sCaches.end() &&
            progIt->second.typeCount == prog.typeCount &&
            progIt->second.functionCount == prog.functionCount) {
            const auto tIt = progIt->second.byType.find(typeId);
            if (tIt != progIt->second.byType.end()) {
                for (const auto& entry : tIt->second) {
                    if (entry.memberName == memberName) {
                        return entry.slot;
                    }
                }
            }
        }
    }

    AotMemberSlot resolved{};
    const std::string backingName = std::string("$") + std::string(memberName);
    const std::string getterName = std::string("get_") + std::string(memberName);
    const std::string setterName = std::string("set_") + std::string(memberName);
    std::unordered_set<SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = FindAotTypeById(prog, typeId);
    while (cur && visited.insert(cur->id).second) {
        if (cur->fields) {
            for (std::uint32_t fi = 0u; fi < cur->fieldCount; ++fi) {
                const auto& fd = cur->fields[fi];
                if (fd.staticField || !fd.name) {
                    continue;
                }
                const std::string_view fname(fd.name);
                if (fname == memberName || fname == backingName) {
                    if (!resolved.hasReadField) {
                        resolved.hasReadField = true;
                        resolved.readFieldIndex = fd.index;
                    }
                    if (!fd.constField && !resolved.hasWriteField) {
                        resolved.hasWriteField = true;
                        resolved.writeFieldIndex = fd.index;
                    }
                }
            }
        }
        if ((!resolved.hasReadField && resolved.getterSym == 0u) ||
            (!resolved.hasWriteField && resolved.setterSym == 0u)) {
            if (prog.functions && prog.functionCount > 0u) {
                const std::string fullType = AotTypeQualifiedName(*cur);
                const std::string_view shortType = cur->name ? std::string_view(cur->name) : std::string_view{};
                for (std::uint32_t fnIdx = 0u; fnIdx < prog.functionCount; ++fnIdx) {
                    const auto& fn = prog.functions[fnIdx];
                    if (!fn.name) {
                        continue;
                    }
                    if (fn.parameterCount == 1u && resolved.getterSym == 0u) {
                        if (FunctionNameMatchesMember(fn.name, fullType, shortType, getterName) ||
                            FunctionNameMatchesMember(fn.name, fullType, shortType, memberName)) {
                            resolved.getterSym = fn.symbolId;
                        }
                    } else if (fn.parameterCount == 2u && resolved.setterSym == 0u) {
                        if (FunctionNameMatchesMember(fn.name, fullType, shortType, setterName)) {
                            resolved.setterSym = fn.symbolId;
                        }
                    }
                    if (resolved.getterSym != 0u && resolved.setterSym != 0u) {
                        break;
                    }
                }
            }
        }
        if (resolved.hasReadField && resolved.hasWriteField) {
            break;
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = FindAotTypeById(prog, cur->baseTypeId);
    }

    {
        std::lock_guard<std::mutex> lock(sCacheMutex);
        if (sCaches.size() > 16u && sCaches.find(&prog) == sCaches.end()) {
            sCaches.clear();
        }
        auto& progCache = sCaches[&prog];
        if (progCache.typeCount != prog.typeCount ||
            progCache.functionCount != prog.functionCount) {
            progCache.byType.clear();
            progCache.typeCount = prog.typeCount;
            progCache.functionCount = prog.functionCount;
        }
        auto& entries = progCache.byType[typeId];
        bool updated = false;
        for (auto& existing : entries) {
            if (existing.memberName == memberName) {
                existing.slot = resolved;
                updated = true;
                break;
            }
        }
        if (!updated) {
            entries.push_back(AotNamedMemberEntry{std::string(memberName), resolved});
        }
    }
    return resolved;
}

[[nodiscard]] bool TryParseBracketSegment(
    std::string_view seg,
    std::string_view& outBaseMember,
    std::size_t& outIndex) noexcept {
    const std::size_t openPos = seg.find('[');
    if (openPos == std::string_view::npos || seg.back() != ']') {
        return false;
    }
    const std::string_view idxStr = seg.substr(openPos + 1u, seg.size() - openPos - 2u);
    if (idxStr.empty()) {
        return false;
    }
    std::size_t val = 0u;
    for (char ch : idxStr) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (val > (std::numeric_limits<std::size_t>::max() - digit) / 10u) {
            return false;
        }
        val = val * 10u + digit;
    }
    outBaseMember = seg.substr(0u, openPos);
    outIndex = val;
    return true;
}

[[nodiscard]] bool CanMutateCollection(
    const ScriptBackendView& env,
    ObjectRef colObj,
    ManagedHeap*& outHeap) noexcept {
    outHeap = ResolveTargetHeap(env, colObj);
    if (!outHeap || !colObj.valid()) {
        return false;
    }
    if (env.domains &&
        env.domains->activeDomain() == DomainKind::UiPresentation &&
        env.domains->isSimObject(colObj)) {
        return false;
    }
    return true;
}

[[nodiscard]] ObjectRef ResolveCollectionBackingArray(
    const ScriptBackendView& env,
    ManagedHeap& heap,
    ObjectRef colObj) noexcept {
    if (!colObj.valid() || !heap.isAlive(colObj)) {
        return ObjectRef{};
    }
    if (colObj.kind == ObjectKind::Array) {
        return colObj;
    }
    if (colObj.kind != ObjectKind::Record) {
        return ObjectRef{};
    }
    ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(heap, colObj);
    if (arrRef.valid()) {
        return arrRef;
    }

    // Safe schema-guided lookup for unregistered collection wrappers (_items / items).
    SymbolId tid = heap.objectTypeIdFast(colObj);
    if (tid == 0u) {
        if (const auto optTid = heap.objectTypeId(colObj)) {
            tid = *optTid;
        }
    }
    if (tid != 0u && env.programImage) {
        if (const rs_sem::TypeSymbol* ts = env.programImage->resolveType(tid)) {
            for (const auto& f : ts->fields) {
                if (!f.staticField && (f.name == "_items" || f.name == "items")) {
                    UnifiedValue fv{};
                    if (heap.fieldGetInto(colObj, f.index, fv)) {
                        if (const auto* ov = std::get_if<ObjectRef>(&fv);
                            ov && ov->valid() && ov->kind == ObjectKind::Array) {
                            return *ov;
                        }
                    }
                }
            }
        }
    }
    if (const rs_aot::ProgramDescriptor* aotProg = env.EffectiveAotProgram(); tid != 0u && aotProg) {
        if (const rs_aot::TypeDescriptor* td = FindAotTypeById(*aotProg, tid); td && td->fields) {
            for (std::uint32_t fi = 0u; fi < td->fieldCount; ++fi) {
                const auto& fd = td->fields[fi];
                if (!fd.staticField && fd.name &&
                    (std::string_view(fd.name) == "_items" || std::string_view(fd.name) == "items")) {
                    UnifiedValue fv{};
                    if (heap.fieldGetInto(colObj, fd.index, fv)) {
                        if (const auto* ov = std::get_if<ObjectRef>(&fv);
                            ov && ov->valid() && ov->kind == ObjectKind::Array) {
                            return *ov;
                        }
                    }
                }
            }
        }
    }
    return ObjectRef{};
}

[[nodiscard]] bool ReadIndexedElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx,
    UnifiedValue& outValue) {
    ManagedHeap* heap = ResolveTargetHeap(env, colObj);
    if (!heap || !colObj.valid()) {
        return false;
    }
    const ObjectRef arrRef = ResolveCollectionBackingArray(env, *heap, colObj);
    if (!arrRef.valid() || arrRef.kind != ObjectKind::Array) {
        return false;
    }
    const auto elemOpt = heap->arrayGet(arrRef, idx);
    if (!elemOpt.has_value()) {
        return false;
    }
    outValue = *elemOpt;
    return true;
}

[[nodiscard]] bool ReadSingleMemberCore(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    UnifiedValue& outValue) {
    ManagedHeap* heap = ResolveTargetHeap(env, obj);
    if (!heap || memberName.empty()) {
        return false;
    }
    SymbolId typeId = heap->objectTypeIdFast(obj);
    if (typeId == 0u) {
        if (const auto tid = heap->objectTypeId(obj)) {
            typeId = *tid;
        }
    }
    if (typeId == 0u) {
        return false;
    }

    if (env.programImage) {
        if (env.domains) {
            if (const auto slot = env.domains->resolvePropertySlot(*env.programImage, typeId, memberName)) {
                if (env.domains->heapForObject(obj) != nullptr) {
                    RuntimeError err{};
                    if (env.domains->readPropertySlotFast(
                            env.domains->activeDomain(), obj, *slot, outValue, &err, env.interpreter)) {
                        return true;
                    }
                }
                if (slot->isAutoProperty && slot->backingFieldIndex != PropertySlotHandle::InvalidIndex) {
                    UnifiedValue fp{};
                    if (heap->fieldGetInto(obj, slot->backingFieldIndex, fp)) {
                        outValue = std::move(fp);
                        return true;
                    }
                }
                if (slot->getterSymbolId != 0u) {
                    const UnifiedValue selfArg{obj};
                    if (env.domains && (env.interpreter || env.domains->hasDesktopJitInvoker())) {
                        const auto exec = env.domains->invokeMethod(
                            env.domains->activeDomain(), env.interpreter, slot->getterSymbolId, &selfArg, 1u);
                        if (exec.succeeded) {
                            outValue = exec.value;
                            return true;
                        }
                    }
                    if (env.interpreter) {
                        const auto res = env.interpreter->invoke(slot->getterSymbolId, {selfArg});
                        if (res.succeeded) {
                            outValue = res.value;
                            return true;
                        }
                    }
                    if (env.aotContext && env.aotContext->invoke(slot->getterSymbolId, &selfArg, 1u, outValue)) {
                        return true;
                    }
                }
            }
        }

        // Fallback for non-public / internal fields (e.g. _isDisposedTree) or standalone ProgramImage without domains.
        const std::string backing = "$" + std::string(memberName);
        std::unordered_set<SymbolId> visited;
        const rs_sem::TypeSymbol* cur = env.programImage->resolveType(typeId);
        while (cur && visited.insert(cur->id).second) {
            for (const auto& f : cur->fields) {
                if (!f.staticField && (f.name == memberName || f.name == backing)) {
                    UnifiedValue fp{};
                    if (heap->fieldGetInto(obj, f.index, fp)) {
                        outValue = std::move(fp);
                        return true;
                    }
                }
            }
            if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
                break;
            }
            cur = env.programImage->resolveType(cur->baseTypeId);
        }

        const SymbolId getterSym = FindBytecodeClassMethod(
            *env.programImage, typeId, "get_" + std::string(memberName), 0u);
        if (getterSym != 0u) {
            const UnifiedValue selfArg{obj};
            if (env.domains && (env.interpreter || env.domains->hasDesktopJitInvoker())) {
                const auto exec = env.domains->invokeMethod(
                    env.domains->activeDomain(), env.interpreter, getterSym, &selfArg, 1u);
                if (exec.succeeded) {
                    outValue = exec.value;
                    return true;
                }
            }
            if (env.interpreter) {
                const auto exec = env.interpreter->invoke(getterSym, {selfArg});
                if (exec.succeeded) {
                    outValue = exec.value;
                    return true;
                }
            }
        }
    }

    if (const rs_aot::ProgramDescriptor* aotProg = env.EffectiveAotProgram()) {
        const AotMemberSlot slot = ResolveAotMemberSlot(*aotProg, typeId, memberName);
        if (slot.hasReadField) {
            UnifiedValue fp{};
            if (heap->fieldGetInto(obj, slot.readFieldIndex, fp)) {
                outValue = std::move(fp);
                return true;
            }
        }
        if (slot.getterSym != 0u && env.aotContext) {
            const UnifiedValue selfArg{obj};
            if (env.aotContext->invoke(slot.getterSym, &selfArg, 1u, outValue)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool WriteSingleMemberCore(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    const UnifiedValue& value) {
    ManagedHeap* heap = ResolveTargetHeap(env, obj);
    if (!heap || memberName.empty()) {
        return false;
    }
    SymbolId typeId = heap->objectTypeIdFast(obj);
    if (typeId == 0u) {
        if (const auto tid = heap->objectTypeId(obj)) {
            typeId = *tid;
        }
    }
    if (typeId == 0u) {
        return false;
    }

    if (env.programImage && env.domains) {
        if (const auto slot = env.domains->resolvePropertySlot(*env.programImage, typeId, memberName)) {
            if (env.domains->heapForObject(obj) != nullptr) {
                RuntimeError err{};
                if (env.domains->writePropertySlotFast(
                        env.domains->activeDomain(), obj, *slot, value, env.interpreter, &err)) {
                    return true;
                }
                if (err.code == ErrorCode::CapabilityDenied) {
                    return false;
                }
            }
            if (slot->isReadOnly) {
                return false;
            }
            UnifiedValue writeVal = InternIfStringValueForHeap(*heap, value);
            if (slot->isAutoProperty && slot->backingFieldIndex != PropertySlotHandle::InvalidIndex) {
                RuntimeError err{};
                return heap->fieldSet(obj, slot->backingFieldIndex, std::move(writeVal), &err);
            }
            if (slot->setterSymbolId != 0u) {
                const UnifiedValue callArgs[2]{UnifiedValue{obj}, std::move(writeVal)};
                if (env.domains && (env.interpreter || env.domains->hasDesktopJitInvoker())) {
                    const auto res = env.domains->invokeMethod(
                        env.domains->activeDomain(), env.interpreter, slot->setterSymbolId, callArgs, 2u);
                    if (res.succeeded) {
                        return true;
                    }
                }
                if (env.interpreter) {
                    const auto res = env.interpreter->invoke(
                        slot->setterSymbolId, std::vector<UnifiedValue>(callArgs, callArgs + 2u));
                    if (res.succeeded) {
                        return true;
                    }
                }
                if (env.aotContext) {
                    UnifiedValue unused{};
                    if (env.aotContext->invoke(slot->setterSymbolId, callArgs, 2u, unused)) {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    if (const rs_aot::ProgramDescriptor* aotProg = env.EffectiveAotProgram()) {
        if (env.domains &&
            env.domains->activeDomain() == DomainKind::UiPresentation &&
            (env.domains->isSimObject(obj) || env.domains->isTypeSimState(typeId))) {
            return false;
        }
        const AotMemberSlot slot = ResolveAotMemberSlot(*aotProg, typeId, memberName);
        if (slot.hasWriteField) {
            UnifiedValue writeVal = InternIfStringValueForHeap(*heap, value);
            RuntimeError err{};
            return heap->fieldSet(obj, slot.writeFieldIndex, std::move(writeVal), &err);
        }
        if (slot.setterSym != 0u && env.aotContext) {
            UnifiedValue writeVal = InternIfStringValueForHeap(*heap, value);
            const UnifiedValue callArgs[2]{UnifiedValue{obj}, std::move(writeVal)};
            UnifiedValue unused{};
            if (env.aotContext->invoke(slot.setterSym, callArgs, 2u, unused)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool TryInvokeObjectMethod(
    const ScriptBackendView& env,
    ObjectRef recvObj,
    std::string_view methodName,
    const std::string* optStringParam,
    UnifiedValue& outResult,
    bool* outInvokedMethod = nullptr) {
    if (outInvokedMethod) {
        *outInvokedMethod = false;
    }
    ManagedHeap* heap = ResolveTargetHeap(env, recvObj);
    if (!heap || methodName.empty()) {
        return false;
    }
    SymbolId typeId = heap->objectTypeIdFast(recvObj);
    if (typeId == 0u) {
        if (const auto tid = heap->objectTypeId(recvObj)) {
            typeId = *tid;
        }
    }
    if (typeId == 0u) {
        return false;
    }

    auto dispatchMatchedFn = [&](
        SymbolId fnId,
        bool takesParam,
        bool isStatic,
        rs_sem::PrimitiveType expectedParamType = rs_sem::PrimitiveType::String) -> bool {
        if (outInvokedMethod) {
            *outInvokedMethod = true;
        }
        std::vector<UnifiedValue> args;
        if (!isStatic) {
            args.emplace_back(recvObj);
        }
        if (takesParam) {
            const std::string pStr = optStringParam ? *optStringParam : std::string{};
            switch (expectedParamType) {
                case rs_sem::PrimitiveType::Bool: {
                    const bool bv = (pStr == "true" || pStr == "True" || pStr == "TRUE" || pStr == "1");
                    args.emplace_back(bv);
                    break;
                }
                case rs_sem::PrimitiveType::Byte:
                case rs_sem::PrimitiveType::SByte:
                case rs_sem::PrimitiveType::Short:
                case rs_sem::PrimitiveType::UShort:
                case rs_sem::PrimitiveType::Int:
                case rs_sem::PrimitiveType::UInt:
                case rs_sem::PrimitiveType::Long:
                case rs_sem::PrimitiveType::ULong: {
                    char* endPtr = nullptr;
                    const long long parsed = std::strtoll(pStr.c_str(), &endPtr, 10);
                    if (endPtr != nullptr && endPtr != pStr.c_str()) {
                        if (expectedParamType == rs_sem::PrimitiveType::UInt) {
                            args.emplace_back(rs_rt::UIntValue{static_cast<std::uint32_t>(parsed)});
                        } else if (expectedParamType == rs_sem::PrimitiveType::ULong) {
                            args.emplace_back(rs_rt::ULongValue{static_cast<std::uint64_t>(parsed)});
                        } else if (expectedParamType == rs_sem::PrimitiveType::Long) {
                            args.emplace_back(rs_rt::LongValue{static_cast<std::int64_t>(parsed)});
                        } else {
                            args.emplace_back(static_cast<std::int64_t>(parsed));
                        }
                    } else {
                        args.emplace_back(std::int64_t{0});
                    }
                    break;
                }
                case rs_sem::PrimitiveType::Float: {
                    char* endPtr = nullptr;
                    const float fv = std::strtof(pStr.c_str(), &endPtr);
                    args.emplace_back(FloatValue{(endPtr != nullptr && endPtr != pStr.c_str()) ? fv : 0.0f});
                    break;
                }
                case rs_sem::PrimitiveType::Double: {
                    char* endPtr = nullptr;
                    const double dv = std::strtod(pStr.c_str(), &endPtr);
                    args.emplace_back((endPtr != nullptr && endPtr != pStr.c_str()) ? dv : 0.0);
                    break;
                }
                default: {
                    if (const auto interned = heap->internString(pStr, nullptr)) {
                        args.emplace_back(*interned);
                    } else {
                        args.emplace_back(StringValue{pStr});
                    }
                    break;
                }
            }
        }
        if (env.domains &&
            env.domains->activeDomain() == DomainKind::UiPresentation &&
            env.domains->isSimCommandFunction(fnId)) {
            SimCommandEnvelope cmd;
            cmd.commandTypeId = fnId;
            cmd.targetObject = isStatic ? ObjectRef{} : recvObj;
            if (takesParam) {
                cmd.payload.push_back(args.back());
            }
            env.domains->enqueueSimCommand(std::move(cmd));
            return true;
        }
        if (env.domains && (env.interpreter || env.domains->hasDesktopJitInvoker())) {
            const auto exec = env.domains->invokeMethod(
                env.domains->activeDomain(), env.interpreter, fnId, args);
            if (exec.succeeded) {
                outResult = exec.value;
                return true;
            }
        }
        if (env.interpreter) {
            const auto exec = env.interpreter->invoke(fnId, args);
            if (exec.succeeded) {
                outResult = exec.value;
                return true;
            }
        }
        if (env.aotContext && env.aotContext->invoke(fnId, args.data(), args.size(), outResult)) {
            return true;
        }
        return false;
    };

    if (env.programImage) {
        std::unordered_set<SymbolId> visited;
        const rs_sem::TypeSymbol* cur = env.programImage->resolveType(typeId);
        while (cur && visited.insert(cur->id).second) {
            const rs_sem::FunctionSymbol* zeroArgMatch = nullptr;
            const rs_sem::FunctionSymbol* oneArgMatch = nullptr;
            std::size_t oneArgParamIdx = 0u;
            for (const rs_sem::FunctionSymbol& fn : cur->methods) {
                if (fn.name != methodName) {
                    continue;
                }
                const bool hasImplicitSelf = fn.method && !fn.staticMethod && !fn.parameters.empty();
                const std::size_t explicitParamCount = hasImplicitSelf
                    ? (fn.parameters.size() - 1u)
                    : fn.parameters.size();
                if (explicitParamCount == 0u) {
                    zeroArgMatch = &fn;
                } else if (explicitParamCount == 1u) {
                    oneArgMatch = &fn;
                    oneArgParamIdx = hasImplicitSelf ? 1u : 0u;
                }
            }
            const rs_sem::PrimitiveType oneArgType =
                (oneArgMatch && oneArgParamIdx < oneArgMatch->parameters.size())
                    ? oneArgMatch->parameters[oneArgParamIdx].type
                    : rs_sem::PrimitiveType::String;
            if (optStringParam && !optStringParam->empty() && oneArgMatch) {
                return dispatchMatchedFn(oneArgMatch->id, true, oneArgMatch->staticMethod, oneArgType);
            }
            if (zeroArgMatch) {
                return dispatchMatchedFn(zeroArgMatch->id, false, zeroArgMatch->staticMethod);
            }
            if (oneArgMatch) {
                return dispatchMatchedFn(oneArgMatch->id, true, oneArgMatch->staticMethod, oneArgType);
            }
            if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
                break;
            }
            cur = env.programImage->resolveType(cur->baseTypeId);
        }
    }

    if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram()) {
        std::unordered_set<SymbolId> visited;
        const rs_aot::TypeDescriptor* cur = FindAotTypeById(*prog, typeId);
        while (cur && visited.insert(cur->id).second) {
            if (prog->functions && prog->functionCount > 0u) {
                const std::string fullType = AotTypeQualifiedName(*cur);
                const std::string_view shortType = cur->name ? std::string_view(cur->name) : std::string_view{};
                const rs_aot::FunctionDescriptor* zeroInst = nullptr;
                const rs_aot::FunctionDescriptor* oneInst = nullptr;
                const rs_aot::FunctionDescriptor* zeroStatic = nullptr;
                for (std::uint32_t fi = 0u; fi < prog->functionCount; ++fi) {
                    const auto& fn = prog->functions[fi];
                    if (!fn.name || !FunctionNameMatchesMember(fn.name, fullType, shortType, methodName)) {
                        continue;
                    }
                    const bool hasObjZero = fn.parameterCount > 0u &&
                        fn.parameterTypes &&
                        fn.parameterTypes[0] == rs_sem::PrimitiveType::Object;
                    if (fn.parameterCount == 1u && hasObjZero) {
                        zeroInst = &fn;
                    } else if (fn.parameterCount == 2u && hasObjZero) {
                        oneInst = &fn;
                    } else if (fn.parameterCount == 0u) {
                        zeroStatic = &fn;
                    }
                }
                const rs_sem::PrimitiveType oneInstType =
                    (oneInst && oneInst->parameterTypes && oneInst->parameterCount >= 2u)
                        ? oneInst->parameterTypes[1]
                        : rs_sem::PrimitiveType::String;
                if (optStringParam && !optStringParam->empty() && oneInst) {
                    return dispatchMatchedFn(oneInst->symbolId, true, false, oneInstType);
                }
                if (zeroInst) {
                    return dispatchMatchedFn(zeroInst->symbolId, false, false);
                }
                if (oneInst) {
                    return dispatchMatchedFn(oneInst->symbolId, true, false, oneInstType);
                }
                if (zeroStatic) {
                    return dispatchMatchedFn(zeroStatic->symbolId, false, true);
                }
            }
            if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
                break;
            }
            cur = FindAotTypeById(*prog, cur->baseTypeId);
        }
    }

    return false;
}

} // namespace

bool IsStdlibModuleName(std::string_view moduleName) noexcept {
    return moduleName.rfind("Aero.", 0u) == 0u ||
           moduleName.rfind("RealScript.", 0u) == 0u;
}

bool NameMatchesTypeSpec(
    std::string_view moduleName,
    std::string_view typeName,
    std::string_view spec) noexcept {
    if (spec.empty() || typeName.empty()) {
        return false;
    }
    if (typeName == spec) {
        return true;
    }
    std::string dotQualified;
    std::string colonQualified;
    if (!moduleName.empty()) {
        dotQualified.reserve(moduleName.size() + 1u + typeName.size());
        dotQualified.append(moduleName);
        dotQualified.push_back('.');
        dotQualified.append(typeName);
        colonQualified.reserve(moduleName.size() + 2u + typeName.size());
        colonQualified.append(moduleName);
        colonQualified.append("::");
        colonQualified.append(typeName);
    }
    if (!dotQualified.empty() && (dotQualified == spec || colonQualified == spec)) {
        return true;
    }
    const std::string normalized = NormalizeDoubleColonToDot(spec);
    return normalized == dotQualified || normalized == typeName;
}

UnifiedValue MakeDefaultFieldValue(rs_sem::PrimitiveType prim, SymbolId typeId) noexcept {
    switch (prim) {
        case rs_sem::PrimitiveType::Bool:
            return UnifiedValue{false};
        case rs_sem::PrimitiveType::Byte:
            return UnifiedValue{rs_rt::ByteValue{0u}};
        case rs_sem::PrimitiveType::SByte:
            return UnifiedValue{rs_rt::SByteValue{0}};
        case rs_sem::PrimitiveType::Short:
            return UnifiedValue{rs_rt::ShortValue{0}};
        case rs_sem::PrimitiveType::UShort:
            return UnifiedValue{rs_rt::UShortValue{0u}};
        case rs_sem::PrimitiveType::Int:
            return UnifiedValue{std::int64_t{0}};
        case rs_sem::PrimitiveType::UInt:
            return UnifiedValue{rs_rt::UIntValue{0u}};
        case rs_sem::PrimitiveType::Long:
            return UnifiedValue{rs_rt::LongValue{0}};
        case rs_sem::PrimitiveType::ULong:
            return UnifiedValue{rs_rt::ULongValue{0u}};
        case rs_sem::PrimitiveType::Float:
            return UnifiedValue{FloatValue{0.0f}};
        case rs_sem::PrimitiveType::Double:
            return UnifiedValue{0.0};
        case rs_sem::PrimitiveType::Char:
            return UnifiedValue{rs_rt::CharValue{0u}};
        case rs_sem::PrimitiveType::String:
            return UnifiedValue{NullString{}};
        case rs_sem::PrimitiveType::Object:
            return UnifiedValue{NullObject{}};
        case rs_sem::PrimitiveType::Array:
            return UnifiedValue{NullArray{}};
        case rs_sem::PrimitiveType::Handle: {
            NativeHandle nh{};
            nh.typeId = typeId;
            return UnifiedValue{nh};
        }
        case rs_sem::PrimitiveType::Enum: {
            EnumValue ev{};
            ev.typeId = typeId;
            ev.value = 0;
            return UnifiedValue{ev};
        }
        case rs_sem::PrimitiveType::Struct:
            if (const auto inlineStruct = rs_rt::makeDefaultInlineStructValue(typeId)) {
                return *inlineStruct;
            }
            return UnifiedValue{};
        default:
            return UnifiedValue{};
    }
}

const rs_aot::TypeDescriptor* FindAotTypeById(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId) noexcept {
    if (!prog.types || prog.typeCount == 0u || typeId == 0u) {
        return nullptr;
    }
    for (std::uint32_t i = 0u; i < prog.typeCount; ++i) {
        if (prog.types[i].id == typeId) {
            return &prog.types[i];
        }
    }
    return nullptr;
}

bool IsBytecodeTypeDerivedFrom(
    const ProgramImage& image,
    const rs_sem::TypeSymbol* type,
    std::string_view baseShortName) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_sem::TypeSymbol* cur = type;
    while (cur && visited.insert(cur->id).second) {
        if (cur->name == baseShortName) {
            return true;
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = image.resolveType(cur->baseTypeId);
    }
    return false;
}

bool IsAotTypeDerivedFrom(
    const rs_aot::ProgramDescriptor& prog,
    const rs_aot::TypeDescriptor* type,
    std::string_view baseShortName) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = type;
    while (cur && visited.insert(cur->id).second) {
        if (cur->name && std::string_view(cur->name) == baseShortName) {
            return true;
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = FindAotTypeById(prog, cur->baseTypeId);
    }
    return false;
}

bool IsTypeDerivedFrom(
    const ScriptBackendView& env,
    SymbolId typeId,
    std::string_view baseShortName) noexcept {
    if (typeId == 0u || baseShortName.empty()) {
        return false;
    }
    if (env.programImage) {
        const rs_sem::TypeSymbol* ts = env.programImage->resolveType(typeId);
        return ts && IsBytecodeTypeDerivedFrom(*env.programImage, ts, baseShortName);
    }
    if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram()) {
        const rs_aot::TypeDescriptor* td = FindAotTypeById(*prog, typeId);
        return td && IsAotTypeDerivedFrom(*prog, td, baseShortName);
    }
    return false;
}

SymbolId FindBytecodeFunctionSymbol(
    const ProgramImage& image,
    std::string_view exactOrSuffix,
    bool excludeStdlibModules) noexcept {
    if (exactOrSuffix.empty()) {
        return 0u;
    }
    if (!excludeStdlibModules) {
        if (const auto direct = image.findFunction(std::string(exactOrSuffix))) {
            return *direct;
        }
    }
    const std::string dotQuery = NormalizeDoubleColonToDot(exactOrSuffix);
    std::string_view bareQuery = exactOrSuffix;
    if (bareQuery.rfind("::", 0u) == 0u) {
        bareQuery.remove_prefix(2u);
    }
    if (!excludeStdlibModules && dotQuery != exactOrSuffix) {
        if (const auto directDot = image.findFunction(dotQuery)) {
            return *directDot;
        }
    }
    for (const auto& mod : image.modules()) {
        if (excludeStdlibModules && IsStdlibModuleName(mod.name)) {
            continue;
        }
        for (const auto& fn : mod.functions) {
            if (fn.name == exactOrSuffix || fn.name == bareQuery || fn.name == dotQuery) {
                return fn.symbolId;
            }
            const std::string qualColon = mod.name.empty() ? fn.name : (mod.name + "::" + fn.name);
            const std::string qualDot = mod.name.empty() ? fn.name : (mod.name + "." + fn.name);
            if (EndsWithSymbol(fn.name, exactOrSuffix) ||
                EndsWithSymbol(fn.name, dotQuery) ||
                EndsWithSymbol(qualColon, exactOrSuffix) ||
                EndsWithSymbol(qualDot, dotQuery)) {
                return fn.symbolId;
            }
        }
    }
    return 0u;
}

SymbolId FindAotFunctionSymbol(
    const rs_aot::ProgramDescriptor& prog,
    std::string_view exactOrSuffix,
    bool excludeStdlibPrefixes) noexcept {
    if (!prog.functions || exactOrSuffix.empty()) {
        return 0u;
    }
    const std::string dotQuery = NormalizeDoubleColonToDot(exactOrSuffix);
    std::string_view bareQuery = exactOrSuffix;
    if (bareQuery.rfind("::", 0u) == 0u) {
        bareQuery.remove_prefix(2u);
    }
    auto isStdlibFn = [](std::string_view fname) noexcept {
        return fname.rfind("Aero.", 0u) == 0u ||
               fname.rfind("RealScript.", 0u) == 0u ||
               fname.rfind("Aero::", 0u) == 0u ||
               fname.rfind("RealScript::", 0u) == 0u;
    };
    for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
        const auto& fn = prog.functions[i];
        if (!fn.name) {
            continue;
        }
        const std::string_view fname(fn.name);
        if (excludeStdlibPrefixes && isStdlibFn(fname)) {
            continue;
        }
        if (fname == exactOrSuffix || fname == dotQuery || fname == bareQuery) {
            return fn.symbolId;
        }
    }
    for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
        const auto& fn = prog.functions[i];
        if (!fn.name) {
            continue;
        }
        const std::string_view fname(fn.name);
        if (excludeStdlibPrefixes && isStdlibFn(fname)) {
            continue;
        }
        if (EndsWithSymbol(fname, exactOrSuffix) ||
            EndsWithSymbol(fname, dotQuery) ||
            EndsWithSymbol(fname, bareQuery)) {
            return fn.symbolId;
        }
    }
    return 0u;
}

SymbolId FindBackendFunctionSymbol(
    const ScriptBackendView& env,
    std::string_view exactOrSuffix,
    bool excludeStdlib) noexcept {
    if (env.programImage) {
        return FindBytecodeFunctionSymbol(*env.programImage, exactOrSuffix, excludeStdlib);
    }
    if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram()) {
        return FindAotFunctionSymbol(*prog, exactOrSuffix, excludeStdlib);
    }
    return 0u;
}

SymbolId FindBytecodeClassMethod(
    const ProgramImage& image,
    SymbolId typeId,
    std::string_view methodName,
    std::size_t nonSelfArgCount) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_sem::TypeSymbol* cur = image.resolveType(typeId);
    while (cur && visited.insert(cur->id).second) {
        const std::string qualColon =
            (cur->moduleName.empty() ? std::string{} : (cur->moduleName + ".")) +
            cur->name + "::" + std::string(methodName);
        if (const auto direct = image.findFunction(qualColon)) {
            return *direct;
        }
        const std::string shortDot = cur->name + "." + std::string(methodName);
        for (const auto& mod : image.modules()) {
            if (!cur->moduleName.empty() && mod.name != cur->moduleName) {
                continue;
            }
            for (const auto& fn : mod.functions) {
                if (fn.name == shortDot || EndsWithSymbol(fn.name, shortDot)) {
                    return fn.symbolId;
                }
            }
        }
        for (const auto& m : cur->methods) {
            const std::size_t explicitParams =
                (m.method && !m.staticMethod && !m.parameters.empty())
                    ? (m.parameters.size() - 1u)
                    : m.parameters.size();
            if (m.name == methodName && explicitParams == nonSelfArgCount) {
                return m.id;
            }
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = image.resolveType(cur->baseTypeId);
    }
    return 0u;
}

SymbolId FindAotClassMethod(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId,
    std::string_view methodName,
    std::uint32_t totalArgCountIncludingSelf) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = FindAotTypeById(prog, typeId);
    while (cur && visited.insert(cur->id).second) {
        std::string fullPrefix;
        if (cur->moduleName && cur->moduleName[0] != '\0') {
            fullPrefix.append(cur->moduleName);
            fullPrefix.push_back('.');
        }
        if (cur->name) {
            fullPrefix.append(cur->name);
        }
        const std::string shortName(cur->name ? cur->name : "");
        const std::string shortSuffix = shortName + "::" + std::string(methodName);
        const std::string dotSuffix = shortName + "." + std::string(methodName);
        const std::string ctorAltSuffix = shortName + ".." + std::string(methodName);
        const std::string fullColon = fullPrefix + "::" + std::string(methodName);
        const std::string fullDot = fullPrefix + "." + std::string(methodName);
        const std::string fullCtorAlt = fullPrefix + ".." + std::string(methodName);

        if (prog.functions) {
            for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
                const auto& fn = prog.functions[i];
                if (!fn.name || fn.parameterCount != totalArgCountIncludingSelf) {
                    continue;
                }
                const std::string_view fname(fn.name);
                if (fname == fullColon || fname == fullDot || fname == fullCtorAlt ||
                    EndsWithSymbol(fname, shortSuffix) ||
                    EndsWithSymbol(fname, dotSuffix) ||
                    EndsWithSymbol(fname, ctorAltSuffix)) {
                    return fn.symbolId;
                }
            }
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = FindAotTypeById(prog, cur->baseTypeId);
    }
    return 0u;
}

SymbolId FindBackendClassMethod(
    const ScriptBackendView& env,
    SymbolId typeId,
    std::string_view methodName,
    std::size_t nonSelfArgCount) noexcept {
    if (env.programImage) {
        return FindBytecodeClassMethod(*env.programImage, typeId, methodName, nonSelfArgCount);
    }
    if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram()) {
        return FindAotClassMethod(
            *prog, typeId, methodName, static_cast<std::uint32_t>(nonSelfArgCount + 1u));
    }
    return 0u;
}

bool AllocateBytecodeObjectWithDefaults(
    const ProgramImage& image,
    ManagedHeap& heap,
    const rs_sem::TypeSymbol& rawTs,
    ObjectRef& outObj,
    std::string* outError) {
    const rs_sem::TypeSymbol* resolved = image.resolveType(rawTs.id);
    const rs_sem::TypeSymbol& ts = resolved ? *resolved : rawTs;

    auto findFieldTypeId = [&](std::string_view typeSpec) noexcept -> SymbolId {
        if (typeSpec.empty()) {
            return 0u;
        }
        for (const auto& m : image.modules()) {
            for (const auto& t : m.types) {
                if (NameMatchesTypeSpec(t.moduleName, t.name, typeSpec)) {
                    return t.id;
                }
            }
        }
        return 0u;
    };

    std::vector<UnifiedValue> fields;
    std::vector<std::size_t> refFields;
    fields.reserve(ts.fields.size());
    for (std::size_t fi = 0u; fi < ts.fields.size(); ++fi) {
        const auto& f = ts.fields[fi];
        const SymbolId fTypeId = findFieldTypeId(f.typeName);
        fields.push_back(MakeDefaultFieldValue(f.type, fTypeId));
        if (f.type == rs_sem::PrimitiveType::String ||
            f.type == rs_sem::PrimitiveType::Object ||
            f.type == rs_sem::PrimitiveType::Array ||
            (f.type == rs_sem::PrimitiveType::Struct &&
             !rs_rt::makeDefaultInlineStructValue(fTypeId).has_value())) {
            refFields.push_back(fi);
        }
    }
    RuntimeError allocErr{};
    const auto alloc = heap.allocateObject(ts.id, std::move(fields), std::move(refFields), &allocErr);
    if (!alloc) {
        if (outError) {
            *outError = "allocateObject failed: " + allocErr.message;
        }
        return false;
    }
    outObj = *alloc;
    return true;
}

SymbolId FindBytecodeDefaultConstructor(
    const ProgramImage& image,
    const rs_sem::TypeSymbol& ts) noexcept {
    for (const auto& c : ts.constructors) {
        if (!c.staticMethod && c.parameters.empty()) {
            return c.id;
        }
    }
    const std::string qName = ts.moduleName.empty() ? ts.name : (ts.moduleName + "." + ts.name);
    if (const SymbolId ctorId = FindBytecodeFunctionSymbol(image, qName + "::.ctor"); ctorId != 0u) {
        return ctorId;
    }
    return FindBytecodeFunctionSymbol(image, ts.name + "..ctor");
}

ManagedHeap* ResolveTargetHeap(const ScriptBackendView& env, ObjectRef target) noexcept {
    if (!target.valid()) {
        return nullptr;
    }
    if (env.domains) {
        if (ManagedHeap* h = env.domains->heapForObject(target)) {
            if (h->isAlive(target)) {
                return h;
            }
        }
    }
    if (env.interpreter && env.interpreter->heap() && env.interpreter->heap()->isAlive(target)) {
        return env.interpreter->heap().get();
    }
    if (env.aotContext && env.aotContext->heap() && env.aotContext->heap()->isAlive(target)) {
        return env.aotContext->heap().get();
    }
    return nullptr;
}

bool TryExtractStringValue(
    const ScriptBackendView& env,
    const UnifiedValue& val,
    std::string& outStr) {
    if (const auto* sv = std::get_if<StringValue>(&val)) {
        outStr = sv->str();
        return true;
    }
    EnumValue ev{};
    if (val.tryGetEnum(ev)) {
        if (env.programImage && ev.typeId != 0u) {
            if (const rs_sem::TypeSymbol* ts = env.programImage->resolveType(ev.typeId)) {
                for (const auto& em : ts->enumMembers) {
                    if (em.value == ev.value) {
                        outStr = em.name;
                        return true;
                    }
                }
            }
        }
        if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram(); prog && ev.typeId != 0u) {
            if (const rs_aot::TypeDescriptor* td = FindAotTypeById(*prog, ev.typeId); td && td->enumMembers) {
                for (std::uint32_t i = 0u; i < td->enumMemberCount; ++i) {
                    if (td->enumMembers[i].value == ev.value && td->enumMembers[i].name) {
                        outStr = td->enumMembers[i].name;
                        return true;
                    }
                }
            }
        }
        outStr = std::to_string(ev.value);
        return true;
    }
    ObjectRef ref{};
    if (const auto* ov = std::get_if<ObjectRef>(&val)) {
        ref = *ov;
    }
    if (!ref.valid()) {
        return false;
    }
    if (env.domains) {
        if (const auto view = env.domains->stringViewAcrossDomains(ref)) {
            outStr.assign(*view);
            return true;
        }
    }
    if (ManagedHeap* h = ResolveTargetHeap(env, ref)) {
        if (const auto view = h->stringView(ref)) {
            outStr.assign(*view);
            return true;
        }
    }
    return false;
}

std::string_view ExtractStringArgView(
    const ScriptBackendView& env,
    ValueSpan args,
    std::size_t idx,
    std::string& scratch) {
    if (idx >= args.size()) {
        return {};
    }
    if (const auto* sv = std::get_if<StringValue>(&args[idx])) {
        return sv->str();
    }
    if (const auto* ov = std::get_if<ObjectRef>(&args[idx]); ov && ov->valid()) {
        if (env.domains) {
            if (const auto view = env.domains->stringViewAcrossDomains(*ov)) {
                return *view;
            }
        }
        if (ManagedHeap* h = ResolveTargetHeap(env, *ov)) {
            if (const auto view = h->stringView(*ov)) {
                return *view;
            }
        }
    }
    if (TryExtractStringValue(env, args[idx], scratch)) {
        return scratch;
    }
    return {};
}

bool ResolveEnumOrdinal(
    const ScriptBackendView& env,
    SymbolId enumTypeId,
    std::string_view memberName,
    std::int64_t& outValue) noexcept {
    if (enumTypeId == 0u || memberName.empty()) {
        return false;
    }
    if (env.programImage) {
        if (const rs_sem::TypeSymbol* ts = env.programImage->resolveType(enumTypeId)) {
            for (const auto& em : ts->enumMembers) {
                if (em.name == memberName) {
                    outValue = em.value;
                    return true;
                }
            }
        }
    }
    if (const rs_aot::ProgramDescriptor* prog = env.EffectiveAotProgram()) {
        if (const rs_aot::TypeDescriptor* td = FindAotTypeById(*prog, enumTypeId); td && td->enumMembers) {
            for (std::uint32_t i = 0u; i < td->enumMemberCount; ++i) {
                if (td->enumMembers[i].name && memberName == td->enumMembers[i].name) {
                    outValue = td->enumMembers[i].value;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ReadSingleMember(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    UnifiedValue& outValue) {
    std::string_view baseMember;
    std::size_t bracketIdx = 0u;
    if (TryParseBracketSegment(memberName, baseMember, bracketIdx)) {
        ObjectRef colObj = obj;
        if (!baseMember.empty()) {
            UnifiedValue colVal{};
            if (!ReadSingleMemberCore(env, obj, baseMember, colVal)) {
                return false;
            }
            const auto* ov = std::get_if<ObjectRef>(&colVal);
            if (!ov || !ov->valid()) {
                return false;
            }
            colObj = *ov;
        }
        return ReadIndexedElement(env, colObj, bracketIdx, outValue);
    }
    return ReadSingleMemberCore(env, obj, memberName, outValue);
}

bool WriteIndexedCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx,
    const UnifiedValue& value) {
    ManagedHeap* heap = nullptr;
    if (!CanMutateCollection(env, colObj, heap)) {
        return false;
    }
    UnifiedValue writeVal = InternIfStringValueForHeap(*heap, value);
    if (colObj.kind == ObjectKind::Record) {
        if (ObservableListHelper::Replace(*heap, colObj, idx, writeVal)) {
            return true;
        }
        const ObjectRef arrRef = ResolveCollectionBackingArray(env, *heap, colObj);
        return arrRef.valid() && heap->arraySet(arrRef, idx, std::move(writeVal));
    }
    if (colObj.kind == ObjectKind::Array) {
        return heap->arraySet(colObj, idx, std::move(writeVal));
    }
    return false;
}

bool WriteSingleMember(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    const UnifiedValue& value) {
    std::string_view baseMember;
    std::size_t bracketIdx = 0u;
    if (TryParseBracketSegment(memberName, baseMember, bracketIdx)) {
        ObjectRef colObj = obj;
        if (!baseMember.empty()) {
            UnifiedValue colVal{};
            if (!ReadSingleMemberCore(env, obj, baseMember, colVal)) {
                return false;
            }
            const auto* ov = std::get_if<ObjectRef>(&colVal);
            if (!ov || !ov->valid()) {
                return false;
            }
            colObj = *ov;
        }
        return WriteIndexedCollectionElement(env, colObj, bracketIdx, value);
    }
    return WriteSingleMemberCore(env, obj, memberName, value);
}

bool ResolveDotPathTerminal(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    ObjectRef& outParentObj,
    std::string_view& outLeafMember) {
    if (!rootTarget.valid() || propPath.empty()) {
        return false;
    }
    ObjectRef cur = rootTarget;
    std::string_view rem = propPath;
    for (;;) {
        const std::size_t dot = rem.find('.');
        if (dot == std::string_view::npos) {
            if (rem.empty()) {
                return false;
            }
            outParentObj = cur;
            outLeafMember = rem;
            return true;
        }
        const std::string_view seg = rem.substr(0u, dot);
        if (seg.empty()) {
            return false;
        }
        UnifiedValue hopVal{};
        if (!ReadSingleMember(env, cur, seg, hopVal)) {
            return false;
        }
        const auto* nextObj = std::get_if<ObjectRef>(&hopVal);
        if (!nextObj || !nextObj->valid() ||
            (nextObj->kind != ObjectKind::Record && nextObj->kind != ObjectKind::Array)) {
            return false;
        }
        cur = *nextObj;
        rem.remove_prefix(dot + 1u);
    }
}

bool ReadDotPathValue(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    UnifiedValue& outValue) {
    ObjectRef parent{};
    std::string_view leaf;
    if (!ResolveDotPathTerminal(env, rootTarget, propPath, parent, leaf)) {
        return false;
    }
    return ReadSingleMember(env, parent, leaf, outValue);
}

bool WriteDotPathValue(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    const UnifiedValue& inValue) {
    ObjectRef parent{};
    std::string_view leaf;
    if (!ResolveDotPathTerminal(env, rootTarget, propPath, parent, leaf)) {
        return false;
    }
    return WriteSingleMember(env, parent, leaf, inValue);
}

bool AppendCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    const UnifiedValue& item) {
    ManagedHeap* heap = nullptr;
    if (!CanMutateCollection(env, colObj, heap)) {
        return false;
    }
    const UnifiedValue writeVal = InternIfStringValueForHeap(*heap, item);
    if (ObservableListHelper::Add(*heap, colObj, writeVal)) {
        return true;
    }
    const ObjectRef arrRef = ResolveCollectionBackingArray(env, *heap, colObj);
    return arrRef.valid() && heap->arrayPush(arrRef, writeVal);
}

bool InsertCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx,
    const UnifiedValue& item) {
    ManagedHeap* heap = nullptr;
    if (!CanMutateCollection(env, colObj, heap)) {
        return false;
    }
    const UnifiedValue writeVal = InternIfStringValueForHeap(*heap, item);
    if (ObservableListHelper::Insert(*heap, colObj, idx, writeVal)) {
        return true;
    }
    const ObjectRef arrRef = ResolveCollectionBackingArray(env, *heap, colObj);
    return arrRef.valid() && heap->arrayInsert(arrRef, idx, writeVal);
}

bool RemoveCollectionElementAt(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx) {
    ManagedHeap* heap = nullptr;
    if (!CanMutateCollection(env, colObj, heap)) {
        return false;
    }
    if (ObservableListHelper::RemoveAt(*heap, colObj, idx)) {
        return true;
    }
    const ObjectRef arrRef = ResolveCollectionBackingArray(env, *heap, colObj);
    return arrRef.valid() && heap->arrayRemoveAt(arrRef, idx);
}

bool InvokeCommandOrMethodByPath(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    const std::string* optStringParam) {
    ObjectRef recvObj{};
    std::string_view leafName;
    if (!ResolveDotPathTerminal(env, rootTarget, propPath, recvObj, leafName)) {
        return false;
    }

    // Case 1: Path resolves to a command object exposing CanExecute / Execute (ICommand / RelayCommand).
    UnifiedValue propVal{};
    if (ReadSingleMember(env, recvObj, leafName, propVal)) {
        if (const auto* cmdRef = std::get_if<ObjectRef>(&propVal);
            cmdRef && cmdRef->valid() && cmdRef->kind == ObjectKind::Record) {
            UnifiedValue canExecRes{};
            bool hasCanExec = false;
            const bool canExecOk = TryInvokeObjectMethod(
                env, *cmdRef, "CanExecute", optStringParam, canExecRes, &hasCanExec);
            if (hasCanExec && canExecOk) {
                bool allowed = true;
                if (canExecRes.tryGetBool(allowed) && !allowed) {
                    return false;
                }
            }
            UnifiedValue execRes{};
            bool hasExec = false;
            const bool execOk = TryInvokeObjectMethod(
                env, *cmdRef, "Execute", optStringParam, execRes, &hasExec);
            if (hasExec) {
                return execOk;
            }
        }
    }

    // Case 2: Direct command/action method on target (or nested sub-object) or [SimCommand].
    UnifiedValue directRes{};
    return TryInvokeObjectMethod(env, recvObj, leafName, optStringParam, directRes);
}

} // namespace aero
