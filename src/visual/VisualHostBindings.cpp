#include "aero/visual/VisualHostBindings.hpp"
#include "aero/runtime/AeroObservableList.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <realscript/aot_cpp/AotRuntime.h>
#include <realscript/semantic/Semantic.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"

namespace aero {

namespace {

[[nodiscard]] VisualNodeId ArgNode(rs_rt::ValueSpan args, std::size_t idx, const VisualWorld& world) noexcept {
    if (idx >= args.size()) return VisualNodeId{};
    rs_rt::NativeHandle nh{};
    if (!args[idx].tryGetNativeHandle(nh)) return VisualNodeId{};
    return world.ResolveHandle(nh);
}

[[nodiscard]] float ArgFloat(rs_rt::ValueSpan args, std::size_t idx, float def = 0.0f) noexcept {
    if (idx >= args.size()) return def;
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) return fv;
    double dv = 0.0;
    if (args[idx].tryGetDouble(dv)) return static_cast<float>(dv);
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) return static_cast<float>(iv);
    return def;
}

[[nodiscard]] std::int64_t ArgInt(rs_rt::ValueSpan args, std::size_t idx, std::int64_t def = 0) noexcept {
    if (idx >= args.size()) return def;
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) return iv;
    rs_rt::EnumValue ev{};
    if (args[idx].tryGetEnum(ev)) return ev.value;
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) return static_cast<std::int64_t>(fv);
    return def;
}

[[nodiscard]] std::uint32_t ArgUInt(rs_rt::ValueSpan args, std::size_t idx, std::uint32_t def = 0u) noexcept {
    if (idx >= args.size()) return def;
    if (const auto* uv = std::get_if<rs_rt::UIntValue>(&args[idx])) {
        return uv->value;
    }
    return static_cast<std::uint32_t>(ArgInt(args, idx, static_cast<std::int64_t>(def)));
}

[[nodiscard]] bool ArgBool(rs_rt::ValueSpan args, std::size_t idx, bool def = false) noexcept {
    if (idx >= args.size()) return def;
    bool bv = false;
    if (args[idx].tryGetBool(bv)) return bv;
    return ArgInt(args, idx, def ? 1 : 0) != 0;
}

[[nodiscard]] std::string ArgString(rs_rt::ValueSpan args, std::size_t idx) {
    if (idx >= args.size()) return {};
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&args[idx])) {
        return sv->str();
    }
    return {};
}

[[nodiscard]] std::string ArgString(
    const ExecutionDomainContext& domains,
    rs_rt::ValueSpan args,
    std::size_t idx) {
    if (idx >= args.size()) return {};
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&args[idx])) {
        return sv->str();
    }
    if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&args[idx]); ov && ov->valid()) {
        if (const auto view = domains.stringViewAcrossDomains(*ov)) {
            return std::string(*view);
        }
    }
    return {};
}

[[nodiscard]] rs_rt::ObjectRef ArgObject(rs_rt::ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) return rs_rt::ObjectRef{};
    if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&args[idx])) {
        if (ov->valid() && ov->kind == rs_rt::ObjectKind::Record) {
            return *ov;
        }
    }
    return rs_rt::ObjectRef{};
}

[[nodiscard]] rs_rt::Value MakeRtFloat(float v) noexcept {
    return rs_rt::Value{rs_rt::FloatValue{v}};
}

[[nodiscard]] rs_rt::Value MakeUInt(std::uint32_t v) noexcept {
    return rs_rt::Value{rs_rt::UIntValue{v}};
}

void ParseStopsSpec(std::string_view spec, GradientBrushDesc& desc) {
    while (!spec.empty()) {
        const std::size_t semi = spec.find(';');
        const std::string_view token = (semi == std::string_view::npos) ? spec : spec.substr(0u, semi);
        if (semi == std::string_view::npos) {
            spec = {};
        } else {
            spec.remove_prefix(semi + 1u);
        }
        const std::size_t colon = token.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        GradientStop stop{};
        stop.offset = str::ParseFloat32Or(token.substr(0u, colon), 0.0f);
        stop.color = ParseColorLiteral(token.substr(colon + 1u));
        desc.stops.push_back(stop);
    }
}

struct ReflectHostEnv {
    ExecutionDomainContext* domains = nullptr;
    const ProgramImage* programImage = nullptr;
    const Interpreter* interpreter = nullptr;
    rs_aot::ExecutionContext* aotContext = nullptr;
};

[[nodiscard]] rs_rt::ManagedHeap* ResolveTargetHeap(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef target) noexcept {
    if (!target.valid()) {
        return nullptr;
    }
    if (env.domains) {
        if (rs_rt::ManagedHeap* h = env.domains->heapForObject(target)) {
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

[[nodiscard]] const rs_aot::TypeDescriptor* FindAotType(
    const rs_aot::ProgramDescriptor& prog,
    rs_sem::SymbolId typeId) noexcept {
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

[[nodiscard]] bool TryExtractStringValue(
    const ReflectHostEnv& env,
    const rs_rt::Value& val,
    std::string& outStr) {
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&val)) {
        outStr = sv->str();
        return true;
    }
    rs_rt::EnumValue ev{};
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
        if (env.aotContext && ev.typeId != 0u) {
            if (const rs_aot::TypeDescriptor* td = FindAotType(env.aotContext->program(), ev.typeId)) {
                if (td->enumMembers) {
                    for (std::uint32_t i = 0u; i < td->enumMemberCount; ++i) {
                        if (td->enumMembers[i].value == ev.value && td->enumMembers[i].name) {
                            outStr = td->enumMembers[i].name;
                            return true;
                        }
                    }
                }
            }
        }
        outStr = std::to_string(ev.value);
        return true;
    }
    rs_rt::ObjectRef ref{};
    if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&val)) {
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
    if (rs_rt::ManagedHeap* h = ResolveTargetHeap(env, ref)) {
        if (const auto view = h->stringView(ref)) {
            outStr.assign(*view);
            return true;
        }
    }
    return false;
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

[[nodiscard]] rs_rt::Value InternIfStringValueForHeap(
    rs_rt::ManagedHeap& heap,
    const rs_rt::Value& val) {
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&val)) {
        if (const auto interned = heap.internString(sv->str(), nullptr)) {
            return rs_rt::Value{*interned};
        }
    }
    return val;
}

struct AotMemberSlot {
    bool hasReadField = false;
    std::uint32_t readFieldIndex = 0u;
    rs_sem::SymbolId getterSym = 0u;
    bool hasWriteField = false;
    std::uint32_t writeFieldIndex = 0u;
    rs_sem::SymbolId setterSym = 0u;
};

struct AotNamedMemberEntry {
    std::string memberName;
    AotMemberSlot slot;
};

struct AotProgramReflectionCache {
    std::uint32_t typeCount = 0u;
    std::uint32_t functionCount = 0u;
    std::unordered_map<rs_sem::SymbolId, std::vector<AotNamedMemberEntry>> byType;
};

[[nodiscard]] AotMemberSlot ResolveAotMemberSlot(
    const rs_aot::ProgramDescriptor& prog,
    rs_sem::SymbolId typeId,
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
    std::unordered_set<rs_sem::SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = FindAotType(prog, typeId);
    while (cur && visited.insert(cur->id).second) {
        if (cur->fields) {
            for (std::uint32_t fi = 0u; fi < cur->fieldCount; ++fi) {
                const auto& fd = cur->fields[fi];
                if (fd.staticField || !fd.name) continue;
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
                    if (!fn.name) continue;
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
        cur = FindAotType(prog, cur->baseTypeId);
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

[[nodiscard]] rs_rt::ObjectRef ArgCollectionOrArray(rs_rt::ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) return rs_rt::ObjectRef{};
    if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&args[idx])) {
        if (ov->valid() &&
            (ov->kind == rs_rt::ObjectKind::Record || ov->kind == rs_rt::ObjectKind::Array)) {
            return *ov;
        }
    }
    return rs_rt::ObjectRef{};
}

[[nodiscard]] bool CanMutateCollection(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef colObj,
    rs_rt::ManagedHeap*& outHeap) noexcept {
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

[[nodiscard]] rs_rt::ObjectRef ResolveBackingArrayFallback(
    rs_rt::ManagedHeap& heap,
    rs_rt::ObjectRef colObj) noexcept {
    rs_rt::ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(heap, colObj);
    if (!arrRef.valid() && colObj.kind == rs_rt::ObjectKind::Record) {
        if (const rs_rt::Value* f0 = heap.fieldGetFast(colObj, 0u)) {
            if (const auto* ov = std::get_if<rs_rt::ObjectRef>(f0);
                ov && ov->valid() && ov->kind == rs_rt::ObjectKind::Array) {
                arrRef = *ov;
            }
        }
    }
    return arrRef;
}

[[nodiscard]] bool ReadIndexedElement(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef colObj,
    std::size_t idx,
    rs_rt::Value& outValue) {
    rs_rt::ManagedHeap* heap = ResolveTargetHeap(env, colObj);
    if (!heap || !colObj.valid()) {
        return false;
    }
    rs_rt::ObjectRef arrRef{};
    if (colObj.kind == rs_rt::ObjectKind::Array) {
        arrRef = colObj;
    } else if (colObj.kind == rs_rt::ObjectKind::Record) {
        arrRef = ObservableListHelper::ResolveBackingArray(*heap, colObj);
        if (!arrRef.valid()) {
            if (const rs_rt::Value* f0 = heap->fieldGetFast(colObj, 0u)) {
                if (const auto* ov = std::get_if<rs_rt::ObjectRef>(f0);
                    ov && ov->valid() && ov->kind == rs_rt::ObjectKind::Array) {
                    arrRef = *ov;
                }
            }
        }
    }
    if (!arrRef.valid() || arrRef.kind != rs_rt::ObjectKind::Array) {
        return false;
    }
    const auto elemOpt = heap->arrayGet(arrRef, idx);
    if (!elemOpt.has_value()) {
        return false;
    }
    outValue = *elemOpt;
    return true;
}

[[nodiscard]] bool WriteIndexedElement(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef colObj,
    std::size_t idx,
    const rs_rt::Value& value) {
    rs_rt::ManagedHeap* heap = ResolveTargetHeap(env, colObj);
    if (!heap || !colObj.valid()) {
        return false;
    }
    if (env.domains &&
        env.domains->activeDomain() == DomainKind::UiPresentation &&
        env.domains->isSimObject(colObj)) {
        return false;
    }
    rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
    if (colObj.kind == rs_rt::ObjectKind::Record) {
        if (ObservableListHelper::Replace(*heap, colObj, idx, writeVal)) {
            return true;
        }
        if (const rs_rt::Value* f0 = heap->fieldGetFast(colObj, 0u)) {
            if (const auto* ov = std::get_if<rs_rt::ObjectRef>(f0);
                ov && ov->valid() && ov->kind == rs_rt::ObjectKind::Array) {
                return heap->arraySet(*ov, idx, std::move(writeVal));
            }
        }
        return false;
    }
    if (colObj.kind == rs_rt::ObjectKind::Array) {
        return heap->arraySet(colObj, idx, std::move(writeVal));
    }
    return false;
}

[[nodiscard]] bool ReadSingleMemberCore(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    rs_rt::Value& outValue);

[[nodiscard]] bool ReadSingleMember(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    rs_rt::Value& outValue) {
    std::string_view baseMember;
    std::size_t bracketIdx = 0u;
    if (TryParseBracketSegment(memberName, baseMember, bracketIdx)) {
        rs_rt::ObjectRef colObj = obj;
        if (!baseMember.empty()) {
            rs_rt::Value colVal{};
            if (!ReadSingleMemberCore(env, obj, baseMember, colVal)) {
                return false;
            }
            const auto* ov = std::get_if<rs_rt::ObjectRef>(&colVal);
            if (!ov || !ov->valid()) {
                return false;
            }
            colObj = *ov;
        }
        return ReadIndexedElement(env, colObj, bracketIdx, outValue);
    }
    return ReadSingleMemberCore(env, obj, memberName, outValue);
}

[[nodiscard]] bool ReadSingleMemberCore(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    rs_rt::Value& outValue) {
    rs_rt::ManagedHeap* heap = ResolveTargetHeap(env, obj);
    if (!heap || memberName.empty()) {
        return false;
    }
    rs_sem::SymbolId typeId = heap->objectTypeIdFast(obj);
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
                rs_rt::RuntimeError err{};
                if (env.domains->readPropertySlotFast(
                        env.domains->activeDomain(), obj, *slot, outValue, &err, env.interpreter)) {
                    return true;
                }
            }
            if (slot->isAutoProperty && slot->backingFieldIndex != PropertySlotHandle::InvalidIndex) {
                if (const rs_rt::Value* fp = heap->fieldGetFast(obj, slot->backingFieldIndex)) {
                    outValue = *fp;
                    return true;
                }
            }
            if (slot->getterSymbolId != 0u) {
                const rs_rt::Value selfArg{obj};
                if (env.interpreter) {
                    const auto res = env.interpreter->invoke(slot->getterSymbolId, {selfArg});
                    if (res.succeeded) {
                        outValue = res.value;
                        return true;
                    }
                }
                if (env.aotContext) {
                    if (env.aotContext->invoke(slot->getterSymbolId, &selfArg, 1u, outValue)) {
                        return true;
                    }
                }
            }
        }
    }

    if (env.aotContext) {
        const AotMemberSlot slot = ResolveAotMemberSlot(env.aotContext->program(), typeId, memberName);
        if (slot.hasReadField) {
            if (const rs_rt::Value* fp = heap->fieldGetFast(obj, slot.readFieldIndex)) {
                outValue = *fp;
                return true;
            }
        }
        if (slot.getterSym != 0u) {
            const rs_rt::Value selfArg{obj};
            if (env.aotContext->invoke(slot.getterSym, &selfArg, 1u, outValue)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool WriteSingleMemberCore(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    const rs_rt::Value& value);

[[nodiscard]] bool WriteSingleMember(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    const rs_rt::Value& value) {
    std::string_view baseMember;
    std::size_t bracketIdx = 0u;
    if (TryParseBracketSegment(memberName, baseMember, bracketIdx)) {
        rs_rt::ObjectRef colObj = obj;
        if (!baseMember.empty()) {
            rs_rt::Value colVal{};
            if (!ReadSingleMemberCore(env, obj, baseMember, colVal)) {
                return false;
            }
            const auto* ov = std::get_if<rs_rt::ObjectRef>(&colVal);
            if (!ov || !ov->valid()) {
                return false;
            }
            colObj = *ov;
        }
        return WriteIndexedElement(env, colObj, bracketIdx, value);
    }
    return WriteSingleMemberCore(env, obj, memberName, value);
}

[[nodiscard]] bool WriteSingleMemberCore(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef obj,
    std::string_view memberName,
    const rs_rt::Value& value) {
    rs_rt::ManagedHeap* heap = ResolveTargetHeap(env, obj);
    if (!heap || memberName.empty()) {
        return false;
    }
    rs_sem::SymbolId typeId = heap->objectTypeIdFast(obj);
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
                rs_rt::RuntimeError err{};
                if (env.domains->writePropertySlotFast(
                        env.domains->activeDomain(), obj, *slot, value, env.interpreter, &err)) {
                    return true;
                }
                // Cross-heap / read-only policy failures should not bypass domain checks via fallback.
                if (err.code == rs_rt::ErrorCode::CapabilityDenied) {
                    return false;
                }
            }
            if (slot->isReadOnly) {
                return false;
            }
            rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
            if (slot->isAutoProperty && slot->backingFieldIndex != PropertySlotHandle::InvalidIndex) {
                rs_rt::RuntimeError err{};
                return heap->fieldSet(obj, slot->backingFieldIndex, std::move(writeVal), &err);
            }
            if (slot->setterSymbolId != 0u) {
                const rs_rt::Value callArgs[2]{rs_rt::Value{obj}, std::move(writeVal)};
                if (env.interpreter) {
                    const auto res = env.interpreter->invoke(
                        slot->setterSymbolId, std::vector<rs_rt::Value>(callArgs, callArgs + 2u));
                    if (res.succeeded) {
                        return true;
                    }
                }
                if (env.aotContext) {
                    rs_rt::Value unused{};
                    if (env.aotContext->invoke(slot->setterSymbolId, callArgs, 2u, unused)) {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    if (env.aotContext) {
        if (env.domains &&
            env.domains->activeDomain() == DomainKind::UiPresentation &&
            (env.domains->isSimObject(obj) || env.domains->isTypeSimState(typeId))) {
            return false;
        }
        const AotMemberSlot slot = ResolveAotMemberSlot(env.aotContext->program(), typeId, memberName);
        if (slot.hasWriteField) {
            rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
            rs_rt::RuntimeError err{};
            return heap->fieldSet(obj, slot.writeFieldIndex, std::move(writeVal), &err);
        }
        if (slot.setterSym != 0u) {
            rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
            const rs_rt::Value callArgs[2]{rs_rt::Value{obj}, std::move(writeVal)};
            rs_rt::Value unused{};
            if (env.aotContext->invoke(slot.setterSym, callArgs, 2u, unused)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool ResolveDotPathTerminal(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef rootTarget,
    std::string_view propPath,
    rs_rt::ObjectRef& outParentObj,
    std::string_view& outLeafMember) {
    if (!rootTarget.valid() || propPath.empty()) {
        return false;
    }
    rs_rt::ObjectRef cur = rootTarget;
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
        rs_rt::Value hopVal{};
        if (!ReadSingleMember(env, cur, seg, hopVal)) {
            return false;
        }
        const auto* nextObj = std::get_if<rs_rt::ObjectRef>(&hopVal);
        if (!nextObj || !nextObj->valid() ||
            (nextObj->kind != rs_rt::ObjectKind::Record && nextObj->kind != rs_rt::ObjectKind::Array)) {
            return false;
        }
        cur = *nextObj;
        rem.remove_prefix(dot + 1u);
    }
}

[[nodiscard]] bool ReadDotPathValue(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef rootTarget,
    std::string_view propPath,
    rs_rt::Value& outValue) {
    rs_rt::ObjectRef parent{};
    std::string_view leaf;
    if (!ResolveDotPathTerminal(env, rootTarget, propPath, parent, leaf)) {
        return false;
    }
    return ReadSingleMember(env, parent, leaf, outValue);
}

[[nodiscard]] bool WriteDotPathValue(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef rootTarget,
    std::string_view propPath,
    const rs_rt::Value& inValue) {
    rs_rt::ObjectRef parent{};
    std::string_view leaf;
    if (!ResolveDotPathTerminal(env, rootTarget, propPath, parent, leaf)) {
        return false;
    }
    return WriteSingleMember(env, parent, leaf, inValue);
}

[[nodiscard]] bool TryInvokeObjectMethod(
    const ReflectHostEnv& env,
    rs_rt::ObjectRef recvObj,
    std::string_view methodName,
    const std::string* optStringParam,
    rs_rt::Value& outResult,
    bool* outInvokedMethod = nullptr) {
    if (outInvokedMethod) *outInvokedMethod = false;
    rs_rt::ManagedHeap* heap = ResolveTargetHeap(env, recvObj);
    if (!heap || methodName.empty()) {
        return false;
    }
    rs_sem::SymbolId typeId = heap->objectTypeIdFast(recvObj);
    if (typeId == 0u) {
        if (const auto tid = heap->objectTypeId(recvObj)) {
            typeId = *tid;
        }
    }
    if (typeId == 0u) {
        return false;
    }

    auto dispatchMatchedFn = [&](
        rs_sem::SymbolId fnId,
        bool takesParam,
        bool isStatic,
        rs_sem::PrimitiveType expectedParamType = rs_sem::PrimitiveType::String) -> bool {
        if (outInvokedMethod) *outInvokedMethod = true;
        std::vector<rs_rt::Value> args;
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
                    args.emplace_back(MakeRtFloat((endPtr != nullptr && endPtr != pStr.c_str()) ? fv : 0.0f));
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
                        args.emplace_back(rs_rt::StringValue{pStr});
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
            cmd.targetObject = isStatic ? rs_rt::ObjectRef{} : recvObj;
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
        if (env.aotContext) {
            if (env.aotContext->invoke(fnId, args.data(), args.size(), outResult)) {
                return true;
            }
        }
        return false;
    };

    if (env.programImage) {
        std::unordered_set<rs_sem::SymbolId> visited;
        const rs_sem::TypeSymbol* cur = env.programImage->resolveType(typeId);
        while (cur && visited.insert(cur->id).second) {
            // Prefer exact parameter match (1 param when provided cleanly, or 0 params).
            const rs_sem::FunctionSymbol* zeroArgMatch = nullptr;
            const rs_sem::FunctionSymbol* oneArgMatch = nullptr;
            std::size_t oneArgParamIdx = 0u;
            for (const rs_sem::FunctionSymbol& fn : cur->methods) {
                if (fn.name != methodName) continue;
                const bool hasImplicitSelf =
                    fn.method && !fn.staticMethod && !fn.parameters.empty();
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

    if (env.aotContext) {
        const rs_aot::ProgramDescriptor& prog = env.aotContext->program();
        std::unordered_set<rs_sem::SymbolId> visited;
        const rs_aot::TypeDescriptor* cur = FindAotType(prog, typeId);
        while (cur && visited.insert(cur->id).second) {
            if (prog.functions && prog.functionCount > 0u) {
                const std::string fullType = AotTypeQualifiedName(*cur);
                const std::string_view shortType = cur->name ? std::string_view(cur->name) : std::string_view{};
                const rs_aot::FunctionDescriptor* zeroInst = nullptr;
                const rs_aot::FunctionDescriptor* oneInst = nullptr;
                const rs_aot::FunctionDescriptor* zeroStatic = nullptr;
                for (std::uint32_t fi = 0u; fi < prog.functionCount; ++fi) {
                    const auto& fn = prog.functions[fi];
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
            cur = FindAotType(prog, cur->baseTypeId);
        }
    }

    return false;
}

struct FastXmlAttr {
    std::string name;
    std::string value;
};

struct FastXmlNode {
    std::string rawName;
    std::string localName;
    std::string text;
    std::vector<FastXmlAttr> attrs;
    std::int32_t parentId = -1;
    std::int32_t childCount = 0;
    bool lastChildIsLineBreak = false;
    bool hasPrefix = false;
};

struct FastXmlScratch {
    std::vector<FastXmlNode> nodes;
};

[[nodiscard]] FastXmlScratch& ThreadXmlScratch() noexcept {
    static thread_local FastXmlScratch tlsXmlScratch;
    return tlsXmlScratch;
}

} // namespace

bool RegisterVisualHostBindings(
    BindingRegistry& bindings,
    VisualWorld& world,
    VisualAnimationEngine& animEngine,
    VisualInputRouter& inputRouter,
    ExecutionDomainContext& domains,
    const ProgramImage* programImage,
    const Interpreter* interpreter,
    rs_aot::ExecutionContext* aotContext,
    UIHostPlatformHooks* platformHooks) {
    static DrawingCommandBuffer sDefaultDrawBuffer;
    return RegisterVisualHostBindings(
        bindings,
        world,
        sDefaultDrawBuffer,
        animEngine,
        inputRouter,
        domains,
        programImage,
        interpreter,
        aotContext,
        platformHooks);
}

bool RegisterVisualHostBindings(
    BindingRegistry& bindings,
    VisualWorld& world,
    DrawingCommandBuffer& drawBuffer,
    VisualAnimationEngine& animEngine,
    VisualInputRouter& inputRouter,
    ExecutionDomainContext& domains,
    const ProgramImage* programImage,
    const Interpreter* interpreter,
    rs_aot::ExecutionContext* aotContext,
    UIHostPlatformHooks* platformHooks) {
    const ReflectHostEnv reflectEnv{&domains, programImage, interpreter, aotContext};

    auto bindFn = [&](const char* shortName, auto fn) -> bool {
        const std::string bare(shortName);
        const std::string canonical = "Aero.Native::" + bare;
        const std::string nativeBare = "__rs_native_" + bare;
        const std::string nativeCanonical = "Aero.Native::__rs_native_" + bare;
        const bool okCanonical = bindings.bind(
            canonical, fn, rs_rt::BindingDeterminism::NonDeterministic);
        const bool okNativeCanonical = bindings.bind(
            nativeCanonical, fn, rs_rt::BindingDeterminism::NonDeterministic);
        const bool okNativeBare = bindings.bind(
            nativeBare, fn, rs_rt::BindingDeterminism::NonDeterministic);
        const bool okBare = bindings.bind(
            bare, std::move(fn), rs_rt::BindingDeterminism::NonDeterministic);
        return okCanonical && okNativeCanonical && okNativeBare && okBare;
    };

    bool ok = true;

    // Tree & lifecycle
    ok &= bindFn("Visual_Create", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto kind = static_cast<VisualKind>(std::clamp<std::int64_t>(ArgInt(args, 0, 0), 0, 4));
        const VisualNodeId id = world.CreateNode(kind);
        return rs_rt::Value{world.HandleFor(id)};
    });
    ok &= bindFn("Visual_Destroy", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId id = ArgNode(args, 0, world);
        if (id.IsValid()) {
            std::vector<VisualNodeId> queue;
            queue.push_back(id);
            const std::size_t limit = world.LiveNodeCount() + 1u;
            for (std::size_t qi = 0u; qi < queue.size() && qi < limit; ++qi) {
                const VisualNodeId cur = queue[qi];
                drawBuffer.ClearNode(cur);
                if (const VisualNode* node = world.GetNode(cur)) {
                    for (const VisualNodeId childId : node->children) {
                        if (childId.IsValid()) {
                            queue.push_back(childId);
                        }
                    }
                }
            }
            (void)world.DestroyNode(id);
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_AddChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.AddChild(ArgNode(args, 0, world), ArgNode(args, 1, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_InsertChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.InsertChild(ArgNode(args, 0, world), static_cast<std::int32_t>(ArgInt(args, 1, 0)), ArgNode(args, 2, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_RemoveChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.RemoveChild(ArgNode(args, 0, world), ArgNode(args, 1, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_ClearChildren", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.ClearChildren(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_BindOwner", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.BindOwner(ArgNode(args, 0, world), ArgObject(args, 1), static_cast<std::uint32_t>(ArgInt(args, 2, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_GetOwner", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef obj = world.GetOwner(ArgNode(args, 0, world));
        return (obj.valid() && obj.kind == rs_rt::ObjectKind::Record)
            ? rs_rt::Value{obj}
            : rs_rt::Value{rs_rt::NullObject{}};
    });

    // Geometry & Layout
    ok &= bindFn("Visual_SetSize", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetSize(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetMinMaxSize", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetMinMaxSize(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetMargin", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetMargin(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetPadding", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetPadding(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetAlignment", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nid = ArgNode(args, 0, world);
        const std::int64_t rawH = ArgInt(args, 1, 0);
        const std::int64_t rawV = ArgInt(args, 2, 0);
        (void)world.SetAlignment(
            nid,
            static_cast<HorizontalAlignment>(rawH & 0xFF),
            static_cast<VerticalAlignment>(rawV & 0xFF));
        if (VisualNode* node = world.GetNode(nid)) {
            const bool hasFd = (rawH & 0x100) != 0;
            const bool isRtl = (rawH & 0x200) != 0;
            if (node->layout.hasExplicitFlowDirection != hasFd || node->layout.explicitRtl != isRtl) {
                node->layout.hasExplicitFlowDirection = hasFd;
                node->layout.explicitRtl = isRtl;
                node->layout.hasValidMeasure = false;
                node->layout.hasValidArrange = false;
                world.MarkDirty(nid, VisualDirty_All);
            }
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetVisibility", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVisibility(ArgNode(args, 0, world), static_cast<Visibility>(ArgInt(args, 1, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetOpacity", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetOpacity(ArgNode(args, 0, world), ArgFloat(args, 1, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetZIndex", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetZIndex(ArgNode(args, 0, world), static_cast<std::int32_t>(ArgInt(args, 1, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetClipToBounds", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetClipToBounds(ArgNode(args, 0, world), ArgBool(args, 1, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetInteractiveFlags", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetInteractiveFlags(ArgNode(args, 0, world), ArgBool(args, 1, true), ArgBool(args, 2, true), ArgBool(args, 3, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetLayoutMode", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetLayoutMode(
            ArgNode(args, 0, world),
            static_cast<LayoutMode>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            static_cast<std::int32_t>(ArgInt(args, 4, 0)),
            static_cast<std::int32_t>(ArgInt(args, 5, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_GridClearDefs", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridClearDefs(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_GridAddRowDef", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridAddRowDef(ArgNode(args, 0, world), static_cast<GridUnitType>(ArgInt(args, 1, 2)), ArgFloat(args, 2, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_GridAddColDef", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridAddColDef(ArgNode(args, 0, world), static_cast<GridUnitType>(ArgInt(args, 1, 2)), ArgFloat(args, 2, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetGridCell", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetGridCell(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, 0)),
            static_cast<std::int32_t>(ArgInt(args, 2, 0)),
            static_cast<std::int32_t>(ArgInt(args, 3, 1)),
            static_cast<std::int32_t>(ArgInt(args, 4, 1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetDock", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetDock(ArgNode(args, 0, world), static_cast<Dock>(ArgInt(args, 1, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetCanvasOffsets", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetCanvasOffsets(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetScrollOffsets", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetScrollOffsets(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_GetLayoutRect", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const float val = world.GetLayoutRectScalar(
            ArgNode(args, 0, world),
            static_cast<VisualLayoutRectQuery>(ArgInt(args, 1, 0)));
        return MakeRtFloat(val);
    });
    ok &= bindFn("Visual_ScreenToLocal", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const Point local = world.ScreenToLocal(ArgNode(args, 0, world), Point{ArgFloat(args, 1), ArgFloat(args, 2)});
        return MakeRtFloat((ArgInt(args, 3, 0) == 0) ? local.x : local.y);
    });

    // Appearance / Transforms / Effects
    ok &= bindFn("Visual_SetBoxStyle", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxStyle(
            ArgNode(args, 0, world),
            ArgUInt(args, 1),
            ArgUInt(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7),
            ArgFloat(args, 8),
            ArgFloat(args, 9),
            ArgFloat(args, 10));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetBoxGradients", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxGradients(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, -1)),
            static_cast<std::int32_t>(ArgInt(args, 2, -1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetBoxBrushOpacity", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxBrushOpacity(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetBoxShadow", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxShadow(
            ArgNode(args, 0, world),
            ArgBool(args, 1, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_RegisterLinearGradient", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        GradientBrushDesc desc{};
        desc.kind = GradientBrushKind::Linear;
        desc.startPoint = Point{ArgFloat(args, 0, 0.0f), ArgFloat(args, 1, 0.0f)};
        desc.endPoint = Point{ArgFloat(args, 2, 1.0f), ArgFloat(args, 3, 1.0f)};
        desc.spreadMethod = static_cast<GradientSpreadMethod>(ArgInt(args, 4, 0));
        desc.mappingMode = static_cast<BrushMappingMode>(ArgInt(args, 5, 0));
        ParseStopsSpec(ArgString(domains, args, 6), desc);
        const std::uint32_t id = world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc));
        return rs_rt::Value{static_cast<std::int64_t>(id)};
    });
    ok &= bindFn("Visual_RegisterRadialGradient", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        GradientBrushDesc desc{};
        desc.kind = GradientBrushKind::Radial;
        desc.center = Point{ArgFloat(args, 0, 0.5f), ArgFloat(args, 1, 0.5f)};
        desc.gradientOrigin = Point{ArgFloat(args, 2, 0.5f), ArgFloat(args, 3, 0.5f)};
        desc.radiusX = ArgFloat(args, 4, 0.5f);
        desc.radiusY = ArgFloat(args, 5, 0.5f);
        desc.spreadMethod = static_cast<GradientSpreadMethod>(ArgInt(args, 6, 0));
        desc.mappingMode = static_cast<BrushMappingMode>(ArgInt(args, 7, 0));
        ParseStopsSpec(ArgString(domains, args, 8), desc);
        const std::uint32_t id = world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc));
        return rs_rt::Value{static_cast<std::int64_t>(id)};
    });
    ok &= bindFn("Visual_RegisterConicGradient", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        GradientBrushDesc desc{};
        desc.kind = GradientBrushKind::Conic;
        desc.center = Point{ArgFloat(args, 0, 0.5f), ArgFloat(args, 1, 0.5f)};
        desc.spreadMethod = static_cast<GradientSpreadMethod>(ArgInt(args, 2, 0));
        desc.mappingMode = static_cast<BrushMappingMode>(ArgInt(args, 3, 0));
        ParseStopsSpec(ArgString(domains, args, 4), desc);
        const std::uint32_t id = world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc));
        return rs_rt::Value{static_cast<std::int64_t>(id)};
    });
    ok &= bindFn("Visual_SetTransform2D", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTransform2D(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3, 1.0f),
            ArgFloat(args, 4, 1.0f),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7),
            ArgFloat(args, 8, 0.5f),
            ArgFloat(args, 9, 0.5f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetLayoutTransform", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetLayoutTransform(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 1.0f),
            ArgFloat(args, 2, 1.0f),
            ArgFloat(args, 3, 0.0f),
            ArgBool(args, 4, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTransform3D", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTransform3D(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTransform3DEx", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTransform3DEx(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7, 1.0f),
            ArgFloat(args, 8, 1.0f),
            ArgFloat(args, 9, 1.0f),
            ArgFloat(args, 10, 0.0f),
            ArgFloat(args, 11, 0.0f),
            ArgFloat(args, 12, 0.0f),
            ArgFloat(args, 13, 0.0f),
            ArgBool(args, 14, true));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetFocus", [&world, &inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nid = ArgNode(args, 0, world);
        (void)inputRouter.SetFocus(nid);
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetEffect", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetEffect(
            ArgNode(args, 0, world),
            static_cast<UIEffectKind>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgUInt(args, 5),
            ArgFloat(args, 6, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetOpacityMask", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetOpacityMask(
            ArgNode(args, 0, world),
            ArgBool(args, 1, false),
            static_cast<std::int32_t>(ArgInt(args, 2, -1)),
            ArgUInt(args, 3, 0xFFFFFFFFu),
            ArgFloat(args, 4, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetBlendMode", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const int rawMode = static_cast<int>(ArgInt(args, 1, 0));
        const BlendingMode mode = (rawMode >= 0 && rawMode <= 3)
            ? static_cast<BlendingMode>(rawMode)
            : BlendingMode::Normal;
        (void)world.SetBlendMode(ArgNode(args, 0, world), mode);
        return rs_rt::Value{std::int64_t{0}};
    });

    // Text / Image / Vector
    ok &= bindFn("Visual_SetText", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetText(
            ArgNode(args, 0, world),
            ArgString(domains, args, 1),
            ArgString(domains, args, 2),
            ArgFloat(args, 3, 14.0f),
            ArgUInt(args, 4, 0xFF000000u),
            static_cast<TextWrapping>(ArgInt(args, 5, 0)),
            static_cast<TextAlignment>(ArgInt(args, 6, 0)),
            static_cast<TextTrimming>(ArgInt(args, 7, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTextMetrics", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextMetrics(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            static_cast<std::int32_t>(ArgInt(args, 3, 0)),
            static_cast<std::int32_t>(ArgInt(args, 4, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTextStyle", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextStyle(
            ArgNode(args, 0, world),
            static_cast<int>(ArgInt(args, 1, 400)),
            ArgBool(args, 2, false),
            static_cast<std::int32_t>(ArgInt(args, 3, -1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTextBrushOpacity", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextBrushOpacity(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_ClearTextSpans", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.ClearTextSpans(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_AddTextSpan", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.AddTextSpan(
            ArgNode(args, 0, world),
            ArgString(domains, args, 1),
            ArgString(domains, args, 2),
            ArgFloat(args, 3, 0.0f),
            static_cast<int>(ArgInt(args, 4, 0)),
            static_cast<std::int8_t>(ArgInt(args, 5, -1)),
            ArgUInt(args, 6, 0u),
            ArgBool(args, 7, false),
            static_cast<std::int32_t>(ArgInt(args, 8, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetTextSelection", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextSelection(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, 0)),
            static_cast<std::int32_t>(ArgInt(args, 2, 0)),
            static_cast<std::int32_t>(ArgInt(args, 3, -1)),
            ArgBool(args, 4, false),
            ArgUInt(args, 5),
            ArgUInt(args, 6));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_MeasureText", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const Size measured = world.MeasureText(
            ArgString(domains, args, 0),
            ArgString(domains, args, 1),
            ArgFloat(args, 2, 14.0f),
            ArgFloat(args, 3, 1000000.0f),
            static_cast<TextWrapping>(ArgInt(args, 4, 0)));
        return MakeRtFloat((ArgInt(args, 5, 0) == 0) ? measured.width : measured.height);
    });
    ok &= bindFn("Visual_HitTestTextCp", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t cp = world.HitTestTextCodePoint(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return rs_rt::Value{static_cast<std::int64_t>(cp)};
    });
    ok &= bindFn("Visual_SetImage", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetImage(
            ArgNode(args, 0, world),
            ArgString(domains, args, 1),
            static_cast<Stretch>(ArgInt(args, 2, 2)),
            static_cast<StretchDirection>(ArgInt(args, 3, 2)),
            ArgUInt(args, 4, 0xFFFFFFFFu));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetImageViewbox", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetImageViewbox(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 0.0f),
            ArgFloat(args, 2, 0.0f),
            ArgFloat(args, 3, 1.0f),
            ArgFloat(args, 4, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetImageViewport", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetImageViewport(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 0.0f),
            ArgFloat(args, 2, 0.0f),
            ArgFloat(args, 3, 1.0f),
            ArgFloat(args, 4, 1.0f),
            ArgBool(args, 5, false),
            ArgBool(args, 6, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetVisualBrushSource", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVisualBrushSource(
            ArgNode(args, 0, world),
            ArgNode(args, 1, world),
            static_cast<int>(ArgInt(args, 2, 1)),
            static_cast<int>(ArgInt(args, 3, 1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetImageTile", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetImageTileMode(ArgNode(args, 0, world), ArgBool(args, 1, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetVectorShape", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVectorShape(
            ArgNode(args, 0, world),
            static_cast<VectorShapeKind>(ArgInt(args, 1, 0)),
            ArgString(domains, args, 2),
            ArgUInt(args, 3),
            static_cast<std::int32_t>(ArgInt(args, 4, -1)),
            ArgUInt(args, 5),
            static_cast<std::int32_t>(ArgInt(args, 6, -1)),
            ArgFloat(args, 7, 1.0f),
            static_cast<Stretch>(ArgInt(args, 8, 2)),
            ArgFloat(args, 9, 0.0f),
            ArgFloat(args, 10, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetVectorBrushOpacity", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVectorBrushOpacity(
            ArgNode(args, 0, world),
            ArgFloat(args, 1, 1.0f),
            ArgFloat(args, 2, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_SetVectorStrokeDash", [&world, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVectorStrokeDash(
            ArgNode(args, 0, world),
            ArgString(domains, args, 1),
            static_cast<std::int32_t>(ArgInt(args, 2, 0)),
            static_cast<std::int32_t>(ArgInt(args, 3, 0)),
            static_cast<std::int32_t>(ArgInt(args, 4, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });

    // Retained DrawingContext
    ok &= bindFn("Draw_Begin", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.Begin(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_End", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId id = ArgNode(args, 0, world);
        drawBuffer.End(id);
        world.MarkDirty(id, VisualDirty_Visual);
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_FillRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), static_cast<std::int32_t>(ArgInt(args, 6, -1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_StrokeRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_FillRoundedRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillRoundedRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgFloat(args, 6), ArgUInt(args, 7), static_cast<std::int32_t>(ArgInt(args, 8, -1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_StrokeRoundedRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawRoundedRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgFloat(args, 6), ArgUInt(args, 7), ArgFloat(args, 8, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_FillEllipse", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillEllipse(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), static_cast<std::int32_t>(ArgInt(args, 6, -1)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_StrokeEllipse", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawEllipse(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_Line", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawLine(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_Path", [&world, &drawBuffer, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawPath(ArgNode(args, 0, world), ArgString(domains, args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgUInt(args, 6), ArgUInt(args, 7), ArgFloat(args, 8, 1.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_Text", [&world, &drawBuffer, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawText(
            ArgNode(args, 0, world),
            ArgString(domains, args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgString(domains, args, 6),
            ArgFloat(args, 7, 14.0f),
            ArgUInt(args, 8, 0xFF000000u),
            static_cast<HorizontalAlignment>(ArgInt(args, 9, 0)),
            static_cast<VerticalAlignment>(ArgInt(args, 10, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_PushClip", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PushClip(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_PopClip", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PopClip(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_PushTransform", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PushTransform(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3, 1.0f), ArgFloat(args, 4, 1.0f), ArgFloat(args, 5));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Draw_PopTransform", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PopTransform(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Visual_InvalidateDraw", [&world, platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (world.InvalidateDraw(ArgNode(args, 0, world))) {
            if (platformHooks && platformHooks->requestWake) {
                platformHooks->requestWake();
            }
        }
        return rs_rt::Value{std::int64_t{0}};
    });

    // Animation / Host / Reflection
    ok &= bindFn("Anim_StartFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        const float fromVal = ArgFloat(args, 2);
        const float toVal = ArgFloat(args, 3);
        const float durationSec = ArgFloat(args, 4, 0.25f);
        const float delaySec = ArgFloat(args, 7, 0.0f);
        const std::int32_t tid = animEngine.StartFloat(
            nodeId,
            prop,
            fromVal,
            toVal,
            durationSec,
            static_cast<EasingFunctionKind>(ArgInt(args, 5, 2)),
            static_cast<EasingMode>(ArgInt(args, 6, 0)),
            delaySec,
            ArgBool(args, 8, false),
            static_cast<std::int32_t>(ArgInt(args, 9, 1)));
        if (tid > 0 && delaySec <= 0.0f && durationSec > 1e-5f) {
            VisualAnimationEngine::ApplyScalarPropImmediate(world, nodeId, prop, fromVal);
        }
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StartFloatTo", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        const std::int32_t tid = animEngine.StartFloat(
            nodeId,
            prop,
            VisualAnimationEngine::ReadCurrentScalarProp(world, nodeId, prop),
            ArgFloat(args, 2),
            ArgFloat(args, 3, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 4, 2)),
            static_cast<EasingMode>(ArgInt(args, 5, 0)),
            ArgFloat(args, 6, 0.0f),
            ArgBool(args, 7, false),
            static_cast<std::int32_t>(ArgInt(args, 8, 1)));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    auto startColorCommon = [&world, &animEngine](rs_rt::ValueSpan args, bool hasAutoRepeat, bool readCurrentFrom) -> std::int64_t {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 16));
        const std::uint32_t fromRgba = readCurrentFrom
            ? VisualAnimationEngine::ReadCurrentColorProp(world, nodeId, prop)
            : ArgUInt(args, 2);
        const std::size_t off = readCurrentFrom ? 2u : 3u;
        const std::uint32_t toRgba = ArgUInt(args, off);
        const float durationSec = ArgFloat(args, off + 1u, 0.25f);
        const auto easeKind = static_cast<EasingFunctionKind>(ArgInt(args, off + 2u, 2));
        const auto easeMode = static_cast<EasingMode>(ArgInt(args, off + 3u, 0));
        const float delaySec = ArgFloat(args, off + 4u, 0.0f);
        const bool autoReverse = hasAutoRepeat ? ArgBool(args, off + 5u, false) : false;
        const std::int32_t repeatCount = hasAutoRepeat ? static_cast<std::int32_t>(ArgInt(args, off + 6u, 1)) : 1;
        const std::int32_t tid = animEngine.StartColor(
            nodeId,
            prop,
            fromRgba,
            toRgba,
            durationSec,
            easeKind,
            easeMode,
            delaySec,
            autoReverse,
            repeatCount);
        if (!readCurrentFrom && tid > 0 && delaySec <= 0.0f && durationSec > 1e-5f &&
            prop != VisualAnimProp::ManagedCustom) {
            VisualAnimationEngine::ApplyColorPropImmediate(world, nodeId, prop, fromRgba);
        }
        return static_cast<std::int64_t>(tid);
    };
    auto startColorHandler = [startColorCommon](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{startColorCommon(args, false, false)};
    };
    ok &= bindFn("Anim_StartColor", startColorHandler);
    ok &= bindFn("Anim_StartColorDelayed", startColorHandler);
    ok &= bindFn("Anim_StartColorEx", [startColorCommon](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{startColorCommon(args, true, false)};
    });
    ok &= bindFn("Anim_StartColorTo", [startColorCommon](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{startColorCommon(args, false, true)};
    });
    ok &= bindFn("Anim_StartColorToEx", [startColorCommon](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{startColorCommon(args, true, true)};
    });
    ok &= bindFn("Anim_ReadCurrentFloat", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        return MakeRtFloat(VisualAnimationEngine::ReadCurrentScalarProp(world, nodeId, prop));
    });
    ok &= bindFn("Anim_ReadCurrentColor", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 16));
        return MakeUInt(VisualAnimationEngine::ReadCurrentColorProp(world, nodeId, prop));
    });
    ok &= bindFn("Anim_SpringFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        const std::int32_t tid = animEngine.SpringFloat(
            nodeId,
            prop,
            VisualAnimationEngine::ReadCurrentScalarProp(world, nodeId, prop),
            ArgFloat(args, 2),
            ArgFloat(args, 3, 320.0f),
            ArgFloat(args, 4, 28.0f));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StopAll", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.StopAll(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_StopTrack", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.StopTrack(static_cast<std::int32_t>(ArgInt(args, 0, 0)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_SetTrackEasingParams", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.SetTrackEasingParams(
            static_cast<std::int32_t>(ArgInt(args, 0, 0)),
            ArgFloat(args, 1, 0.0f),
            ArgFloat(args, 2, 0.0f),
            static_cast<std::uint8_t>(std::clamp<std::int64_t>(ArgInt(args, 3, 3), 0, 255)));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_BeginKeyFrames", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        const float delaySec = ArgFloat(args, 2, 0.0f);
        const bool autoReverse = ArgBool(args, 3, false);
        const auto repeatCount = static_cast<std::int32_t>(ArgInt(args, 4, 1));
        const std::int32_t tid = animEngine.BeginKeyFramesTrack(
            nodeId, prop, delaySec, autoReverse, repeatCount);
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_AddKeyFrameFloat", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.AddKeyFrameFloat(
            static_cast<std::int32_t>(ArgInt(args, 0, 0)),
            ArgFloat(args, 1, 0.0f),
            ArgFloat(args, 2, 0.0f),
            static_cast<EasingFunctionKind>(ArgInt(args, 3, 0)),
            static_cast<EasingMode>(ArgInt(args, 4, 0)),
            ArgFloat(args, 5, 0.0f),
            ArgFloat(args, 6, 0.0f),
            static_cast<std::uint8_t>(std::clamp<std::int64_t>(ArgInt(args, 7, 0), 0, 255)),
            ArgBool(args, 8, false));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_ApplyFloat", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        VisualAnimationEngine::ApplyScalarPropImmediate(world, nodeId, prop, ArgFloat(args, 2, 0.0f));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_ApplyColor", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 16));
        VisualAnimationEngine::ApplyColorPropImmediate(world, nodeId, prop, ArgUInt(args, 2, 0u));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Anim_IsTrackActive", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{animEngine.HasTrack(static_cast<std::int32_t>(ArgInt(args, 0, 0)))};
    });
    ok &= bindFn("Anim_IsTrackStarted", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{animEngine.IsTrackStarted(static_cast<std::int32_t>(ArgInt(args, 0, 0)))};
    });
    ok &= bindFn("Anim_ReadTrackFloat", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return MakeRtFloat(animEngine.ReadTrackFloat(
            static_cast<std::int32_t>(ArgInt(args, 0, 0)),
            ArgFloat(args, 1, 0.0f)));
    });
    ok &= bindFn("Anim_ReadTrackColor", [&animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return MakeUInt(animEngine.ReadTrackColor(
            static_cast<std::int32_t>(ArgInt(args, 0, 0)),
            ArgUInt(args, 1, 0u)));
    });
    ok &= bindFn("Host_SetPointerCapture", [&world, &inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SetPointerCapture(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_ReleasePointerCapture", [&world, &inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.ReleasePointerCapture(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_SyncImeRect", [&inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SyncImeCaretRect(Rect{ArgFloat(args, 0), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3)}, ArgBool(args, 4, true));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_GetClipboardText", [&inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{rs_rt::StringValue{inputRouter.GetClipboardText()}};
    });
    ok &= bindFn("Host_SetClipboardText", [&inputRouter, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SetClipboardText(ArgString(domains, args, 0));
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_RequestWake", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->requestWake) {
            platformHooks->requestWake();
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_RequestWakeAfterMs", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t delayMs = static_cast<std::int32_t>(std::clamp<std::int64_t>(ArgInt(args, 0, 16), 1, 60000));
        if (platformHooks) {
            if (platformHooks->requestWakeAfterMs) {
                platformHooks->requestWakeAfterMs(delayMs);
            } else if (platformHooks->requestWake) {
                platformHooks->requestWake();
            }
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_CloseWindow", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->closeWindow) {
            platformHooks->closeWindow();
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_PlaySound", [platformHooks, &domains](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->playSound) {
            platformHooks->playSound(ArgString(domains, args, 0), ArgFloat(args, 1, 1.0f));
        }
        return rs_rt::Value{std::int64_t{0}};
    });
    ok &= bindFn("Host_GetLocalTimePart", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const int part = static_cast<int>(ArgInt(args, 0, 0));
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &tt);
#else
        localtime_r(&tt, &localTm);
#endif
        std::int64_t out = 0;
        switch (part) {
        case 0: out = static_cast<std::int64_t>(localTm.tm_year + 1900); break;
        case 1: out = static_cast<std::int64_t>(localTm.tm_mon + 1); break;
        case 2: out = static_cast<std::int64_t>(localTm.tm_mday); break;
        case 3: out = static_cast<std::int64_t>(localTm.tm_hour); break;
        case 4: out = static_cast<std::int64_t>(localTm.tm_min); break;
        case 5: out = static_cast<std::int64_t>(localTm.tm_sec); break;
        case 6: {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            out = static_cast<std::int64_t>((ms % 1000 + 1000) % 1000);
            break;
        }
        default: out = 0; break;
        }
        return rs_rt::Value{out};
    });

    // Reflection & color utilities
    ok &= bindFn("Reflect_GetPropString", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (ReadDotPathValue(reflectEnv, target, propPath, val)) {
            std::string outStr;
            if (TryExtractStringValue(reflectEnv, val, outStr)) {
                return rs_rt::Value{rs_rt::StringValue{std::move(outStr)}};
            }
        }
        return rs_rt::Value{rs_rt::StringValue{std::string{}}};
    });
    ok &= bindFn("Reflect_GetPropFloat", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (ReadDotPathValue(reflectEnv, target, propPath, val)) {
            float fv = 0.0f;
            if (val.tryGetFloat(fv)) return MakeRtFloat(fv);
            double dv = 0.0;
            if (val.tryGetDouble(dv)) return MakeRtFloat(static_cast<float>(dv));
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return MakeRtFloat(static_cast<float>(iv));
            if (const auto* uv = std::get_if<rs_rt::UIntValue>(&val)) {
                return MakeRtFloat(static_cast<float>(uv->value));
            }
        }
        return MakeRtFloat(0.0f);
    });
    ok &= bindFn("Reflect_GetPropInt", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (ReadDotPathValue(reflectEnv, target, propPath, val)) {
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return rs_rt::Value{iv};
            rs_rt::EnumValue ev{};
            if (val.tryGetEnum(ev)) return rs_rt::Value{ev.value};
            if (const auto* uv = std::get_if<rs_rt::UIntValue>(&val)) {
                return rs_rt::Value{static_cast<std::int64_t>(uv->value)};
            }
            bool bv = false;
            if (val.tryGetBool(bv)) return rs_rt::Value{static_cast<std::int64_t>(bv ? 1 : 0)};
            float fv = 0.0f;
            if (val.tryGetFloat(fv)) return rs_rt::Value{static_cast<std::int64_t>(fv)};
        }
        return rs_rt::Value{static_cast<std::int64_t>(0)};
    });
    ok &= bindFn("Reflect_ResolveEnumOrdinal", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        constexpr std::int64_t kNotFound = std::int64_t{-2147483647};
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        std::string memberName;
        if (!TryExtractStringValue(reflectEnv, args.size() > 2u ? args[2] : rs_rt::Value{}, memberName)) {
            memberName = ArgString(args, 2);
        }
        rs_rt::Value val{};
        rs_rt::EnumValue ev{};
        if (memberName.empty() || !ReadDotPathValue(reflectEnv, target, propPath, val) || !val.tryGetEnum(ev) || ev.typeId == 0u) {
            return rs_rt::Value{kNotFound};
        }
        if (reflectEnv.programImage) {
            if (const rs_sem::TypeSymbol* ts = reflectEnv.programImage->resolveType(ev.typeId)) {
                for (const auto& em : ts->enumMembers) {
                    if (em.name == memberName) {
                        return rs_rt::Value{em.value};
                    }
                }
            }
        }
        if (reflectEnv.aotContext) {
            if (const rs_aot::TypeDescriptor* td = FindAotType(reflectEnv.aotContext->program(), ev.typeId)) {
                if (td->enumMembers) {
                    for (std::uint32_t i = 0u; i < td->enumMemberCount; ++i) {
                        if (td->enumMembers[i].name && memberName == td->enumMembers[i].name) {
                            return rs_rt::Value{td->enumMembers[i].value};
                        }
                    }
                }
            }
        }
        return rs_rt::Value{kNotFound};
    });
    ok &= bindFn("Reflect_GetPropBool", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (ReadDotPathValue(reflectEnv, target, propPath, val)) {
            bool bv = false;
            if (val.tryGetBool(bv)) return rs_rt::Value{bv};
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return rs_rt::Value{iv != 0};
        }
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_GetPropObject", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (ReadDotPathValue(reflectEnv, target, propPath, val)) {
            if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&val);
                ov && ov->valid() && ov->kind == rs_rt::ObjectKind::Record) {
                return rs_rt::Value{*ov};
            }
        }
        return rs_rt::Value{rs_rt::NullObject{}};
    });
    ok &= bindFn("Reflect_GetPropTypeKind", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        rs_rt::Value val{};
        if (!ReadDotPathValue(reflectEnv, target, propPath, val)) {
            return rs_rt::Value{std::int64_t{0}};
        }
        switch (val.rawTag()) {
            case rs_rt::ValueTag::Bool:
                return rs_rt::Value{std::int64_t{1}};
            case rs_rt::ValueTag::Byte:
            case rs_rt::ValueTag::SByte:
            case rs_rt::ValueTag::Short:
            case rs_rt::ValueTag::UShort:
            case rs_rt::ValueTag::Int64:
            case rs_rt::ValueTag::UInt:
            case rs_rt::ValueTag::Long:
            case rs_rt::ValueTag::ULong:
                return rs_rt::Value{std::int64_t{2}};
            case rs_rt::ValueTag::Float:
            case rs_rt::ValueTag::Double:
                return rs_rt::Value{std::int64_t{3}};
            case rs_rt::ValueTag::String:
            case rs_rt::ValueTag::EnumInline:
            case rs_rt::ValueTag::EnumArena:
                return rs_rt::Value{std::int64_t{4}};
            case rs_rt::ValueTag::ObjectRef: {
                if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&val); ov && ov->valid()) {
                    if (ov->kind == rs_rt::ObjectKind::String) {
                        return rs_rt::Value{std::int64_t{4}};
                    }
                    return rs_rt::Value{std::int64_t{5}};
                }
                return rs_rt::Value{std::int64_t{0}};
            }
            default:
                return rs_rt::Value{std::int64_t{0}};
        }
    });
    ok &= bindFn("Reflect_SetPropString", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        std::string newVal;
        if (!TryExtractStringValue(reflectEnv, args.size() > 2u ? args[2] : rs_rt::Value{}, newVal)) {
            newVal = ArgString(args, 2);
        }
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, rs_rt::Value{rs_rt::StringValue{std::move(newVal)}})};
    });
    ok &= bindFn("Reflect_SetPropFloat", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, MakeRtFloat(ArgFloat(args, 2)))};
    });
    ok &= bindFn("Reflect_SetPropInt", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, rs_rt::Value{ArgInt(args, 2)})};
    });
    ok &= bindFn("Reflect_SetPropBool", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, rs_rt::Value{ArgBool(args, 2)})};
    });
    ok &= bindFn("Reflect_SetPropObject", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        const rs_rt::Value objVal = (args.size() > 2u) ? args[2] : rs_rt::Value{rs_rt::NullObject{}};
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, objVal)};
    });
    ok &= bindFn("Reflect_ListSet", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef colObj = ArgCollectionOrArray(args, 0);
        const std::int64_t rawIdx = ArgInt(args, 1, -1);
        if (!colObj.valid() || rawIdx < 0) {
            return rs_rt::Value{false};
        }
        const rs_rt::Value itemVal = (args.size() > 2u) ? args[2] : rs_rt::Value{rs_rt::NullObject{}};
        return rs_rt::Value{WriteIndexedElement(reflectEnv, colObj, static_cast<std::size_t>(rawIdx), itemVal)};
    });
    ok &= bindFn("Reflect_ListAdd", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef colObj = ArgCollectionOrArray(args, 0);
        rs_rt::ManagedHeap* heap = nullptr;
        if (!CanMutateCollection(reflectEnv, colObj, heap)) {
            return rs_rt::Value{false};
        }
        const rs_rt::Value itemVal = (args.size() > 1u) ? args[1] : rs_rt::Value{rs_rt::NullObject{}};
        const rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, itemVal);
        if (ObservableListHelper::Add(*heap, colObj, writeVal)) {
            return rs_rt::Value{true};
        }
        const rs_rt::ObjectRef arrRef = ResolveBackingArrayFallback(*heap, colObj);
        return rs_rt::Value{arrRef.valid() && heap->arrayPush(arrRef, writeVal)};
    });
    ok &= bindFn("Reflect_ListInsert", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef colObj = ArgCollectionOrArray(args, 0);
        const std::int64_t rawIdx = ArgInt(args, 1, -1);
        rs_rt::ManagedHeap* heap = nullptr;
        if (rawIdx < 0 || !CanMutateCollection(reflectEnv, colObj, heap)) {
            return rs_rt::Value{false};
        }
        const auto idx = static_cast<std::size_t>(rawIdx);
        const rs_rt::Value itemVal = (args.size() > 2u) ? args[2] : rs_rt::Value{rs_rt::NullObject{}};
        const rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, itemVal);
        if (ObservableListHelper::Insert(*heap, colObj, idx, writeVal)) {
            return rs_rt::Value{true};
        }
        const rs_rt::ObjectRef arrRef = ResolveBackingArrayFallback(*heap, colObj);
        return rs_rt::Value{arrRef.valid() && heap->arrayInsert(arrRef, idx, writeVal)};
    });
    ok &= bindFn("Reflect_ListRemoveAt", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef colObj = ArgCollectionOrArray(args, 0);
        const std::int64_t rawIdx = ArgInt(args, 1, -1);
        rs_rt::ManagedHeap* heap = nullptr;
        if (rawIdx < 0 || !CanMutateCollection(reflectEnv, colObj, heap)) {
            return rs_rt::Value{false};
        }
        const auto idx = static_cast<std::size_t>(rawIdx);
        if (ObservableListHelper::RemoveAt(*heap, colObj, idx)) {
            return rs_rt::Value{true};
        }
        const rs_rt::ObjectRef arrRef = ResolveBackingArrayFallback(*heap, colObj);
        return rs_rt::Value{arrRef.valid() && heap->arrayRemoveAt(arrRef, idx)};
    });
    ok &= bindFn("Reflect_InvokeCommand", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        std::string propPath;
        if (!TryExtractStringValue(reflectEnv, args.size() > 1u ? args[1] : rs_rt::Value{}, propPath)) {
            propPath = ArgString(args, 1);
        }
        std::string param;
        if (!TryExtractStringValue(reflectEnv, args.size() > 2u ? args[2] : rs_rt::Value{}, param)) {
            param = ArgString(args, 2);
        }
        rs_rt::ObjectRef recvObj{};
        std::string_view leafName;
        if (!ResolveDotPathTerminal(reflectEnv, target, propPath, recvObj, leafName)) {
            return rs_rt::Value{false};
        }

        // Case 1: Path resolves to a command object exposing CanExecute / Execute (ICommand / RelayCommand).
        rs_rt::Value propVal{};
        if (ReadSingleMember(reflectEnv, recvObj, leafName, propVal)) {
            if (const auto* cmdRef = std::get_if<rs_rt::ObjectRef>(&propVal);
                cmdRef && cmdRef->valid() && cmdRef->kind == rs_rt::ObjectKind::Record) {
                rs_rt::Value canExecRes{};
                bool hasCanExec = false;
                const bool canExecOk = TryInvokeObjectMethod(
                    reflectEnv, *cmdRef, "CanExecute", &param, canExecRes, &hasCanExec);
                if (hasCanExec && canExecOk) {
                    bool allowed = true;
                    if (canExecRes.tryGetBool(allowed) && !allowed) {
                        return rs_rt::Value{false};
                    }
                }
                rs_rt::Value execRes{};
                bool hasExec = false;
                const bool execOk = TryInvokeObjectMethod(
                    reflectEnv, *cmdRef, "Execute", &param, execRes, &hasExec);
                if (hasExec) {
                    return rs_rt::Value{execOk};
                }
            }
        }

        // Case 2: Direct command/action method on target (or nested sub-object) or [SimCommand].
        rs_rt::Value directRes{};
        return rs_rt::Value{TryInvokeObjectMethod(
            reflectEnv, recvObj, leafName, &param, directRes)};
    });
    ok &= bindFn("Reflect_GetDirtyMask", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef target = ArgObject(args, 0);
        if (rs_rt::ManagedHeap* heap = ResolveTargetHeap(reflectEnv, target)) {
            rs_sem::SymbolId typeId = heap->objectTypeIdFast(target);
            if (typeId == 0u) {
                if (const auto tidOpt = heap->objectTypeId(target)) {
                    typeId = *tidOpt;
                }
            }
            if (typeId == 0u || !reflectEnv.domains || !reflectEnv.domains->isTypeObservable(typeId)) {
                return rs_rt::Value{static_cast<std::int64_t>(-1)};
            }
            const std::uint64_t mask = heap->peekDirtyMask(target);
            return rs_rt::Value{static_cast<std::int64_t>(mask)};
        }
        return rs_rt::Value{static_cast<std::int64_t>(-1)};
    });
    ok &= bindFn("Color_ParseRgba", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        std::string rawColor;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, rawColor)) {
            rawColor = ArgString(args, 0);
        }
        return MakeUInt(ParseColorRgba32(rawColor));
    });

    ok &= bindFn("Host_ReadTextAsset", [&world, &domains, reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        namespace fs = std::filesystem;
        std::string rawUri;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, rawUri)) {
            rawUri = ArgString(domains, args, 0);
        }

        auto isSpace = [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };
        while (!rawUri.empty() && isSpace(static_cast<unsigned char>(rawUri.front()))) {
            rawUri.erase(0u, 1u);
        }
        while (!rawUri.empty() && isSpace(static_cast<unsigned char>(rawUri.back()))) {
            rawUri.pop_back();
        }
        if (rawUri.empty()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }

        std::replace(rawUri.begin(), rawUri.end(), '\\', '/');
        std::string lowerUri = rawUri;
        for (char& ch : lowerUri) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }

        std::string directPath = rawUri;
        std::string relPath = rawUri;
        const std::size_t compPos = lowerUri.find(";component/");
        if (compPos != std::string::npos) {
            relPath = rawUri.substr(compPos + 11u);
        } else if (lowerUri.rfind("pack://", 0u) == 0u) {
            const std::size_t tripleCommaPos = lowerUri.find(",,,/", 7u);
            if (tripleCommaPos != std::string::npos) {
                relPath = rawUri.substr(tripleCommaPos + 4u);
            } else {
                const std::size_t slashPos = rawUri.find('/', 7u);
                relPath = slashPos != std::string::npos ? rawUri.substr(slashPos + 1u) : rawUri.substr(7u);
            }
        } else if (lowerUri.rfind("file:///", 0u) == 0u) {
            directPath = rawUri.substr(8u);
            relPath = directPath;
        } else if (lowerUri.rfind("file://", 0u) == 0u) {
            directPath = rawUri.substr(7u);
            relPath = directPath;
        }

        while (!relPath.empty()) {
            if (relPath[0] == '/') {
                relPath.erase(0u, 1u);
            } else if (relPath.size() >= 2u && relPath[0] == '.' && relPath[1] == '/') {
                relPath.erase(0u, 2u);
            } else {
                break;
            }
        }

        std::string text;
        auto registerLoadedAssetDir = [&world](const fs::path& loadedFile) {
            std::error_code dirEc;
            const fs::path absFile = loadedFile.is_absolute() ? loadedFile : fs::absolute(loadedFile, dirEc);
            const fs::path parentDir = (!dirEc && !absFile.empty()) ? absFile.parent_path() : loadedFile.parent_path();
            if (parentDir.empty()) {
                return;
            }
            const std::string dirStr = parentDir.generic_string();
            if (UIResourceStore* mutStore = world.ResourceStore()) {
                mutStore->AddAssetSearchDirectory(dirStr);
                mutStore->Images().AddSearchDirectory(dirStr);
            } else {
                ImageAtlas::Instance().AddSearchDirectory(dirStr);
            }
        };
        auto tryReadFile = [&text, &registerLoadedAssetDir](const fs::path& candidate) -> bool {
            if (candidate.empty()) {
                return false;
            }
            std::error_code ec;
            if (!fs::is_regular_file(candidate, ec) || ec) {
                return false;
            }
            std::ifstream ifs(candidate, std::ios::binary);
            if (!ifs.is_open()) {
                return false;
            }
            std::string loaded((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (ifs.bad()) {
                return false;
            }
            if (loaded.size() >= 3u &&
                static_cast<unsigned char>(loaded[0]) == 0xEFu &&
                static_cast<unsigned char>(loaded[1]) == 0xBBu &&
                static_cast<unsigned char>(loaded[2]) == 0xBFu) {
                loaded.erase(0u, 3u);
            }
            text = std::move(loaded);
            registerLoadedAssetDir(candidate);
            return true;
        };

        if (tryReadFile(fs::path(directPath)) || (!relPath.empty() && tryReadFile(fs::path(relPath)))) {
            return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
        }
        if (relPath.empty()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }

        const fs::path relFs(relPath);
        auto probeWithSampleLayouts = [&](const fs::path& base) -> bool {
            return tryReadFile(base / relFs) ||
                   tryReadFile(base / "Samples" / "Gallery" / "Data" / relFs) ||
                   tryReadFile(base / "Samples" / "Gallery" / "Data" / "Samples" / relFs) ||
                   tryReadFile(base / "Samples" / "Theme" / "Data" / relFs) ||
                   tryReadFile(base / "Samples" / "Theme" / "Data" / "Theme" / relFs);
        };
        auto probeAncestors = [&](const fs::path& startDir) -> bool {
            if (startDir.empty()) {
                return false;
            }
            std::error_code ec;
            fs::path probe = startDir.is_absolute() ? startDir : fs::absolute(startDir, ec);
            if (ec || probe.empty()) {
                probe = startDir;
            }
            for (int up = 0; up < 6 && !probe.empty(); ++up) {
                if (probeWithSampleLayouts(probe)) {
                    return true;
                }
                const fs::path parent = probe.parent_path();
                if (parent == probe) {
                    break;
                }
                probe = parent;
            }
            return false;
        };

        if (const UIResourceStore* store = world.ResourceStore()) {
            for (const std::string& searchDir : store->AssetSearchDirectoriesSnapshot()) {
                if (searchDir.empty()) {
                    continue;
                }
                const fs::path dirPath(searchDir);
                if (tryReadFile(dirPath / relFs)) {
                    return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
                }
                const fs::path parentDir = dirPath.parent_path();
                if (!parentDir.empty() && tryReadFile(parentDir / relFs)) {
                    return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
                }
            }
            if (const ImageAtlas* storeAtlas = store->TryImages()) {
                if (probeAncestors(fs::path(storeAtlas->GetBaseDirectory()))) {
                    return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
                }
            }
        }

        if (probeAncestors(fs::path(ImageAtlas::Instance().GetBaseDirectory()))) {
            return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
        }

        std::error_code cwdEc;
        if (probeAncestors(fs::current_path(cwdEc))) {
            return rs_rt::Value{rs_rt::StringValue{std::move(text)}};
        }

        return rs_rt::Value{rs_rt::StringValue{std::string{}}};
    });

    ok &= bindFn("Utf8_CodePointCount", [&domains, reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        std::string s;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, s)) {
            s = ArgString(domains, args, 0);
        }
        std::int64_t count = 0;
        for (std::size_t i = 0u; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            std::size_t adv = 1u;
            if ((c & 0x80u) == 0u) adv = 1u;
            else if ((c & 0xE0u) == 0xC0u) adv = 2u;
            else if ((c & 0xF0u) == 0xE0u) adv = 3u;
            else if ((c & 0xF8u) == 0xF0u) adv = 4u;
            i += std::min(adv, s.size() - i);
            ++count;
        }
        return rs_rt::Value{count};
    });

    ok &= bindFn("Utf8_SliceCodePoints", [&domains, reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        std::string s;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, s)) {
            s = ArgString(domains, args, 0);
        }
        const std::int32_t startCp = std::max<std::int32_t>(0, ArgInt(args, 1));
        const std::int32_t countCp = ArgInt(args, 2);
        if (countCp <= 0 || s.empty()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        const std::int64_t endCp = static_cast<std::int64_t>(startCp) + static_cast<std::int64_t>(countCp);
        std::size_t byteStart = s.size();
        std::size_t byteEnd = s.size();
        std::int64_t cpIdx = 0;
        for (std::size_t i = 0u; i < s.size();) {
            if (cpIdx == startCp) {
                byteStart = i;
            }
            if (cpIdx == endCp) {
                byteEnd = i;
                break;
            }
            const unsigned char c = static_cast<unsigned char>(s[i]);
            std::size_t adv = 1u;
            if ((c & 0x80u) == 0u) adv = 1u;
            else if ((c & 0xE0u) == 0xC0u) adv = 2u;
            else if ((c & 0xF0u) == 0xE0u) adv = 3u;
            else if ((c & 0xF8u) == 0xF0u) adv = 4u;
            i += std::min(adv, s.size() - i);
            ++cpIdx;
        }
        if (byteStart >= s.size() || byteEnd <= byteStart) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{s.substr(byteStart, byteEnd - byteStart)}};
    });

    ok &= bindFn("Utf8_FromCodePoint", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto cpRaw = ArgInt(args, 0);
        if (cpRaw <= 0 || cpRaw > 0x10FFFF || (cpRaw >= 0xD800 && cpRaw <= 0xDFFF)) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        const auto cp = static_cast<std::uint32_t>(cpRaw);
        std::string out;
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        return rs_rt::Value{rs_rt::StringValue{std::move(out)}};
    });

    auto isXmlSpace = [](char c) noexcept -> bool {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    auto trimView = [&](std::string_view sv) noexcept -> std::string_view {
        std::size_t b = 0u;
        std::size_t e = sv.size();
        while (b < e && isXmlSpace(sv[b])) ++b;
        while (e > b && isXmlSpace(sv[e - 1u])) --e;
        return sv.substr(b, e - b);
    };
    auto stripPrefixView = [](std::string_view sv, bool& outHasPrefix) noexcept -> std::string_view {
        const std::size_t colon = sv.rfind(':');
        if (colon != std::string_view::npos && colon + 1u < sv.size()) {
            outHasPrefix = true;
            return sv.substr(colon + 1u);
        }
        outHasPrefix = false;
        return sv;
    };
    auto appendUtf8Cp = [](std::string& dst, std::uint32_t cp) {
        if (cp == 0u || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return;
        if (cp <= 0x7Fu) {
            dst.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            dst.push_back(static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            dst.push_back(static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            dst.push_back(static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    };
    auto decodeXmlEntities = [appendUtf8Cp](std::string_view raw) -> std::string {
        const std::size_t firstAmp = raw.find('&');
        if (firstAmp == std::string_view::npos) {
            return std::string(raw);
        }
        std::string out;
        out.reserve(raw.size());
        std::size_t pos = 0u;
        while (pos < raw.size()) {
            const std::size_t amp = raw.find('&', pos);
            if (amp == std::string_view::npos) {
                out.append(raw.data() + pos, raw.size() - pos);
                break;
            }
            out.append(raw.data() + pos, amp - pos);
            const std::size_t semi = raw.find(';', amp + 1u);
            if (semi != std::string_view::npos && semi > amp + 1u && semi - amp <= 12u) {
                const std::string_view ent = raw.substr(amp + 1u, semi - (amp + 1u));
                if (ent == "lt") { out.push_back('<'); pos = semi + 1u; continue; }
                if (ent == "gt") { out.push_back('>'); pos = semi + 1u; continue; }
                if (ent == "quot") { out.push_back('"'); pos = semi + 1u; continue; }
                if (ent == "apos") { out.push_back('\''); pos = semi + 1u; continue; }
                if (ent == "amp") { out.push_back('&'); pos = semi + 1u; continue; }
                if (ent.size() > 1u && ent[0] == '#') {
                    std::uint32_t cp = 0u;
                    bool valid = true;
                    if (ent.size() > 2u && (ent[1] == 'x' || ent[1] == 'X')) {
                        for (std::size_t k = 2u; k < ent.size(); ++k) {
                            const char hc = ent[k];
                            std::uint32_t d = 16u;
                            if (hc >= '0' && hc <= '9') d = static_cast<std::uint32_t>(hc - '0');
                            else if (hc >= 'a' && hc <= 'f') d = static_cast<std::uint32_t>(10 + (hc - 'a'));
                            else if (hc >= 'A' && hc <= 'F') d = static_cast<std::uint32_t>(10 + (hc - 'A'));
                            if (d >= 16u) { valid = false; break; }
                            cp = cp * 16u + d;
                            if (cp > 0x10FFFFu) { valid = false; break; }
                        }
                    } else {
                        for (std::size_t k = 1u; k < ent.size(); ++k) {
                            const char dc = ent[k];
                            if (dc < '0' || dc > '9') { valid = false; break; }
                            cp = cp * 10u + static_cast<std::uint32_t>(dc - '0');
                            if (cp > 0x10FFFFu) { valid = false; break; }
                        }
                    }
                    if (valid && cp > 0u && cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                        appendUtf8Cp(out, cp);
                        pos = semi + 1u;
                        continue;
                    }
                }
            }
            out.push_back('&');
            pos = amp + 1u;
        }
        return out;
    };

    ok &= bindFn("Xml_ParseDocument", [&domains, reflectEnv, isXmlSpace, trimView, stripPrefixView, decodeXmlEntities](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        auto& scratch = ThreadXmlScratch();
        scratch.nodes.clear();
        std::string storage;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, storage)) {
            storage = ArgString(domains, args, 0);
        }
        std::string_view src(storage);
        if (src.size() >= 3u &&
            static_cast<unsigned char>(src[0]) == 0xEFu &&
            static_cast<unsigned char>(src[1]) == 0xBBu &&
            static_cast<unsigned char>(src[2]) == 0xBFu) {
            src.remove_prefix(3u);
        }
        const std::size_t firstLt = src.find('<');
        if (firstLt == std::string_view::npos) {
            return rs_rt::Value{std::int64_t{0}};
        }
        src.remove_prefix(firstLt);
        scratch.nodes.reserve(std::max<std::size_t>(32u, src.size() / 64u));

        auto appendChildNode = [&](std::int32_t parentId, std::string raw, std::string local, bool hasPfx, std::string txt) -> std::int32_t {
            const auto id = static_cast<std::int32_t>(scratch.nodes.size());
            FastXmlNode n;
            n.rawName = std::move(raw);
            n.localName = std::move(local);
            n.text = std::move(txt);
            n.parentId = parentId;
            n.hasPrefix = hasPfx;
            if (parentId >= 0 && static_cast<std::size_t>(parentId) < scratch.nodes.size()) {
                auto& p = scratch.nodes[static_cast<std::size_t>(parentId)];
                p.childCount += 1;
                p.lastChildIsLineBreak = (n.localName == "LineBreak");
            }
            scratch.nodes.push_back(std::move(n));
            return id;
        };

        auto appendTextSegment = [&](std::int32_t parentId, std::string_view rawSeg) {
            if (parentId < 0 || rawSeg.empty()) return;
            std::string decoded = decodeXmlEntities(rawSeg);
            if (decoded.empty()) return;
            const std::string_view tr = trimView(decoded);
            const std::string_view pLocal = scratch.nodes[static_cast<std::size_t>(parentId)].localName;
            const bool isInline = (pLocal == "TextBlock" || pLocal == "Span" || pLocal == "Bold" ||
                                   pLocal == "Italic" || pLocal == "Underline" || pLocal == "Hyperlink");
            if (isInline) {
                if (tr.empty()) {
                    (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
                } else {
                    const bool hasLead = isXmlSpace(decoded.front());
                    const bool hasTrail = isXmlSpace(decoded.back());
                    const auto& pRef = scratch.nodes[static_cast<std::size_t>(parentId)];
                    if (hasLead && pRef.childCount > 0 && !pRef.lastChildIsLineBreak) {
                        (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
                    }
                    const std::string trCopy(tr);
                    (void)appendChildNode(parentId, "#text", "#text", false, trCopy);
                    scratch.nodes[static_cast<std::size_t>(parentId)].text += decoded;
                    if (hasTrail) {
                        (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
                    }
                }
                return;
            }
            if (!tr.empty()) {
                if (pLocal == "ItemsControl" || pLocal == "ListBox" || pLocal == "ListView" || pLocal == "ComboBox") {
                    const std::string trCopy(tr);
                    (void)appendChildNode(parentId, "#text", "#text", false, trCopy);
                }
                scratch.nodes[static_cast<std::size_t>(parentId)].text += decoded;
            }
        };

        std::vector<std::int32_t> stack;
        stack.reserve(32u);
        std::size_t maxAttrs = 0u;
        std::size_t pos = 0u;
        const std::size_t len = src.size();
        while (pos < len) {
            if (scratch.nodes.size() >= 262143u || maxAttrs >= 1024u) {
                std::vector<FastXmlNode>().swap(scratch.nodes);
                return rs_rt::Value{std::int64_t{-1}};
            }
            const std::size_t lt = src.find('<', pos);
            if (lt == std::string_view::npos) {
                if (!stack.empty() && pos < len) {
                    appendTextSegment(stack.back(), src.substr(pos));
                }
                break;
            }
            if (lt > pos && !stack.empty()) {
                appendTextSegment(stack.back(), src.substr(pos, lt - pos));
            }
            if (lt + 1u >= len) break;

            // Comments <!-- ... -->
            if (lt + 4u <= len && src.substr(lt, 4u) == "<!--") {
                const std::size_t endCom = src.find("-->", lt + 4u);
                if (endCom == std::string_view::npos) break;
                pos = endCom + 3u;
                continue;
            }
            // CDATA <![CDATA[ ... ]]>
            if (lt + 9u <= len && src.substr(lt, 9u) == "<![CDATA[") {
                const std::size_t endCd = src.find("]]>", lt + 9u);
                if (endCd == std::string_view::npos) break;
                if (!stack.empty()) {
                    scratch.nodes[static_cast<std::size_t>(stack.back())].text.append(
                        src.data() + lt + 9u, endCd - (lt + 9u));
                }
                pos = endCd + 3u;
                continue;
            }
            // PI <?...?> or <!...>
            const char c1 = src[lt + 1u];
            if (c1 == '?' || c1 == '!') {
                const std::size_t gt = src.find('>', lt + 2u);
                if (gt == std::string_view::npos) break;
                pos = gt + 1u;
                continue;
            }

            // Find '>' outside quotes
            std::size_t gt = std::string_view::npos;
            char quote = '\0';
            for (std::size_t k = lt + 1u; k < len; ++k) {
                const char ch = src[k];
                if (quote != '\0') {
                    if (ch == quote) quote = '\0';
                } else {
                    if (ch == '"' || ch == '\'') quote = ch;
                    else if (ch == '>') { gt = k; break; }
                }
            }
            if (gt == std::string_view::npos) break;

            std::string_view header = trimView(src.substr(lt + 1u, gt - (lt + 1u)));
            pos = gt + 1u;
            if (header.empty()) continue;

            // Closing tag </...>
            if (header.front() == '/') {
                const std::string_view closeRaw = trimView(header.substr(1u));
                bool dummyPfx = false;
                const std::string_view closeLocal = stripPrefixView(closeRaw, dummyPfx);
                while (!stack.empty()) {
                    const std::int32_t poppedId = stack.back();
                    stack.pop_back();
                    const auto& popped = scratch.nodes[static_cast<std::size_t>(poppedId)];
                    if (popped.rawName == closeRaw || popped.localName == closeLocal) {
                        break;
                    }
                }
                continue;
            }

            bool selfClosing = false;
            if (!header.empty() && header.back() == '/') {
                selfClosing = true;
                header = trimView(header.substr(0u, header.size() - 1u));
            }
            std::size_t nameEnd = 0u;
            while (nameEnd < header.size() && !isXmlSpace(header[nameEnd])) {
                ++nameEnd;
            }
            const std::string_view rawNameSv = header.substr(0u, nameEnd);
            bool hasPfx = false;
            const std::string_view localNameSv = stripPrefixView(rawNameSv, hasPfx);
            const std::int32_t parentId = stack.empty() ? -1 : stack.back();
            const std::int32_t nodeId = appendChildNode(
                parentId, std::string(rawNameSv), std::string(localNameSv), hasPfx, std::string{});

            // Parse attributes
            if (nameEnd < header.size()) {
                std::string_view attrText = header.substr(nameEnd);
                std::size_t ai = 0u;
                const std::size_t alen = attrText.size();
                auto& attrVec = scratch.nodes[static_cast<std::size_t>(nodeId)].attrs;
                while (ai < alen) {
                    while (ai < alen && isXmlSpace(attrText[ai])) ++ai;
                    if (ai >= alen) break;
                    const std::size_t aStart = ai;
                    while (ai < alen && attrText[ai] != '=' && !isXmlSpace(attrText[ai])) ++ai;
                    const std::string_view aName = trimView(attrText.substr(aStart, ai - aStart));
                    while (ai < alen && isXmlSpace(attrText[ai])) ++ai;
                    if (ai >= alen || attrText[ai] != '=') {
                        if (!aName.empty()) {
                            attrVec.push_back(FastXmlAttr{std::string(aName), std::string{}});
                        }
                        continue;
                    }
                    ++ai; // skip '='
                    while (ai < alen && isXmlSpace(attrText[ai])) ++ai;
                    if (ai >= alen) break;
                    std::string aVal;
                    const char aq = attrText[ai];
                    if (aq == '"' || aq == '\'') {
                        ++ai;
                        const std::size_t vStart = ai;
                        while (ai < alen && attrText[ai] != aq) ++ai;
                        aVal = decodeXmlEntities(attrText.substr(vStart, ai - vStart));
                        if (ai < alen) ++ai;
                    } else {
                        const std::size_t vStart = ai;
                        while (ai < alen && !isXmlSpace(attrText[ai])) ++ai;
                        aVal = decodeXmlEntities(attrText.substr(vStart, ai - vStart));
                    }
                    if (!aName.empty()) {
                        attrVec.push_back(FastXmlAttr{std::string(aName), std::move(aVal)});
                    }
                }
                maxAttrs = std::max(maxAttrs, attrVec.size());
            }

            if (!selfClosing) {
                stack.push_back(nodeId);
            }
        }
        if (scratch.nodes.size() >= 262143u || maxAttrs >= 1024u) {
            std::vector<FastXmlNode>().swap(scratch.nodes);
            return rs_rt::Value{std::int64_t{-1}};
        }
        return rs_rt::Value{static_cast<std::int64_t>(scratch.nodes.size())};
    });

    ok &= bindFn("Xml_ClearDocument", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        std::vector<FastXmlNode>().swap(ThreadXmlScratch().nodes);
        return rs_rt::Value{std::int64_t{0}};
    });

    ok &= bindFn("Xml_NodeMeta", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{std::int64_t{0}};
        }
        const auto& n = nodes[static_cast<std::size_t>(idx)];
        const std::int64_t parentPlus1 = static_cast<std::int64_t>(n.parentId + 1) & 0x3FFFFLL;
        const std::int64_t attrCount = (static_cast<std::int64_t>(n.attrs.size()) & 0x3FFLL) << 18u;
        const std::int64_t hasText = (!n.text.empty() ? 1LL : 0LL) << 28u;
        const std::int64_t hasPfx = (n.hasPrefix ? 1LL : 0LL) << 29u;
        return rs_rt::Value{parentPlus1 | attrCount | hasText | hasPfx};
    });

    ok &= bindFn("Xml_NodeRawName", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{nodes[static_cast<std::size_t>(idx)].rawName}};
    });

    ok &= bindFn("Xml_NodeLocalName", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{nodes[static_cast<std::size_t>(idx)].localName}};
    });

    ok &= bindFn("Xml_NodeText", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{nodes[static_cast<std::size_t>(idx)].text}};
    });

    ok &= bindFn("Xml_AttrName", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        const auto aIdx = ArgInt(args, 1);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        const auto& attrs = nodes[static_cast<std::size_t>(idx)].attrs;
        if (aIdx < 0 || static_cast<std::size_t>(aIdx) >= attrs.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{attrs[static_cast<std::size_t>(aIdx)].name}};
    });

    ok &= bindFn("Xml_AttrValue", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgInt(args, 0);
        const auto aIdx = ArgInt(args, 1);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        const auto& attrs = nodes[static_cast<std::size_t>(idx)].attrs;
        if (aIdx < 0 || static_cast<std::size_t>(aIdx) >= attrs.size()) {
            return rs_rt::Value{rs_rt::StringValue{std::string{}}};
        }
        return rs_rt::Value{rs_rt::StringValue{attrs[static_cast<std::size_t>(aIdx)].value}};
    });

    return ok;
}

} // namespace aero
