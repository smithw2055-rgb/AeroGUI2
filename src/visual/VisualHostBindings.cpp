#include "aero/visual/VisualHostBindings.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

#include <realscript/aot_cpp/AotRuntime.h>
#include <realscript/semantic/Semantic.h>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"
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

[[nodiscard]] rs_rt::Value MakeFloat(float v) noexcept {
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

[[nodiscard]] bool TryExtractStringValue(
    const ReflectHostEnv& env,
    const rs_rt::Value& val,
    std::string& outStr) {
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&val)) {
        outStr = sv->str();
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

[[nodiscard]] bool ReadSingleMember(
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
        const rs_aot::ProgramDescriptor& prog = env.aotContext->program();
        const std::string backingName = std::string("$") + std::string(memberName);
        const std::string getterName = std::string("get_") + std::string(memberName);
        std::unordered_set<rs_sem::SymbolId> visited;
        const rs_aot::TypeDescriptor* cur = FindAotType(prog, typeId);
        while (cur && visited.insert(cur->id).second) {
            if (cur->fields) {
                for (std::uint32_t fi = 0u; fi < cur->fieldCount; ++fi) {
                    const auto& fd = cur->fields[fi];
                    if (fd.staticField || !fd.name) continue;
                    const std::string_view fname(fd.name);
                    if (fname == memberName || fname == backingName) {
                        if (const rs_rt::Value* fp = heap->fieldGetFast(obj, fd.index)) {
                            outValue = *fp;
                            return true;
                        }
                    }
                }
            }
            if (prog.functions && prog.functionCount > 0u) {
                const std::string fullType = AotTypeQualifiedName(*cur);
                const std::string_view shortType = cur->name ? std::string_view(cur->name) : std::string_view{};
                for (std::uint32_t fnIdx = 0u; fnIdx < prog.functionCount; ++fnIdx) {
                    const auto& fn = prog.functions[fnIdx];
                    if (!fn.name || fn.parameterCount != 1u) continue;
                    if (FunctionNameMatchesMember(fn.name, fullType, shortType, getterName) ||
                        FunctionNameMatchesMember(fn.name, fullType, shortType, memberName)) {
                        const rs_rt::Value selfArg{obj};
                        if (env.aotContext->invoke(fn.symbolId, &selfArg, 1u, outValue)) {
                            return true;
                        }
                    }
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

[[nodiscard]] bool WriteSingleMember(
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
        const rs_aot::ProgramDescriptor& prog = env.aotContext->program();
        const std::string backingName = std::string("$") + std::string(memberName);
        const std::string setterName = std::string("set_") + std::string(memberName);
        std::unordered_set<rs_sem::SymbolId> visited;
        const rs_aot::TypeDescriptor* cur = FindAotType(prog, typeId);
        while (cur && visited.insert(cur->id).second) {
            if (cur->fields) {
                for (std::uint32_t fi = 0u; fi < cur->fieldCount; ++fi) {
                    const auto& fd = cur->fields[fi];
                    if (fd.staticField || fd.constField || !fd.name) continue;
                    const std::string_view fname(fd.name);
                    if (fname == memberName || fname == backingName) {
                        rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
                        rs_rt::RuntimeError err{};
                        return heap->fieldSet(obj, fd.index, std::move(writeVal), &err);
                    }
                }
            }
            if (prog.functions && prog.functionCount > 0u) {
                const std::string fullType = AotTypeQualifiedName(*cur);
                const std::string_view shortType = cur->name ? std::string_view(cur->name) : std::string_view{};
                for (std::uint32_t fnIdx = 0u; fnIdx < prog.functionCount; ++fnIdx) {
                    const auto& fn = prog.functions[fnIdx];
                    if (!fn.name || fn.parameterCount != 2u) continue;
                    if (FunctionNameMatchesMember(fn.name, fullType, shortType, setterName)) {
                        rs_rt::Value writeVal = InternIfStringValueForHeap(*heap, value);
                        const rs_rt::Value callArgs[2]{rs_rt::Value{obj}, std::move(writeVal)};
                        rs_rt::Value unused{};
                        if (env.aotContext->invoke(fn.symbolId, callArgs, 2u, unused)) {
                            return true;
                        }
                    }
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
        if (!nextObj || !nextObj->valid() || nextObj->kind != rs_rt::ObjectKind::Record) {
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

    auto dispatchMatchedFn = [&](rs_sem::SymbolId fnId, bool takesParam, bool isStatic) -> bool {
        if (outInvokedMethod) *outInvokedMethod = true;
        std::vector<rs_rt::Value> args;
        if (!isStatic) {
            args.emplace_back(recvObj);
        }
        if (takesParam) {
            const std::string pStr = optStringParam ? *optStringParam : std::string{};
            if (const auto interned = heap->internString(pStr, nullptr)) {
                args.emplace_back(*interned);
            } else {
                args.emplace_back(rs_rt::StringValue{pStr});
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
            // Prefer exact parameter match (1 string param when provided cleanly, or 0 params).
            const rs_sem::FunctionSymbol* zeroArgMatch = nullptr;
            const rs_sem::FunctionSymbol* oneArgMatch = nullptr;
            for (const rs_sem::FunctionSymbol& fn : cur->methods) {
                if (fn.name != methodName) continue;
                if (fn.parameters.empty()) {
                    zeroArgMatch = &fn;
                } else if (fn.parameters.size() == 1u) {
                    oneArgMatch = &fn;
                }
            }
            if (optStringParam && !optStringParam->empty() && oneArgMatch) {
                return dispatchMatchedFn(oneArgMatch->id, true, oneArgMatch->staticMethod);
            }
            if (zeroArgMatch) {
                return dispatchMatchedFn(zeroArgMatch->id, false, zeroArgMatch->staticMethod);
            }
            if (oneArgMatch) {
                return dispatchMatchedFn(oneArgMatch->id, true, oneArgMatch->staticMethod);
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
                if (optStringParam && !optStringParam->empty() && oneInst) {
                    return dispatchMatchedFn(oneInst->symbolId, true, false);
                }
                if (zeroInst) {
                    return dispatchMatchedFn(zeroInst->symbolId, false, false);
                }
                if (oneInst) {
                    return dispatchMatchedFn(oneInst->symbolId, true, false);
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

[[nodiscard]] float ReadCurrentScalarProp(
    const VisualWorld& world,
    VisualNodeId id,
    VisualAnimProp prop) noexcept {
    const VisualNode* node = world.GetNode(id);
    if (!node) {
        return 0.0f;
    }
    switch (prop) {
        case VisualAnimProp::Opacity: return node->layout.opacity;
        case VisualAnimProp::Width: return std::isfinite(node->layout.width) ? node->layout.width : node->layout.layoutRect.width;
        case VisualAnimProp::Height: return std::isfinite(node->layout.height) ? node->layout.height : node->layout.layoutRect.height;
        case VisualAnimProp::TranslateX: return node->transform2D.tx;
        case VisualAnimProp::TranslateY: return node->transform2D.ty;
        case VisualAnimProp::ScaleX: return node->transform2D.sx;
        case VisualAnimProp::ScaleY: return node->transform2D.sy;
        case VisualAnimProp::RotateDeg: return node->transform2D.rotDeg;
        case VisualAnimProp::SkewX: return node->transform2D.skewX;
        case VisualAnimProp::SkewY: return node->transform2D.skewY;
        case VisualAnimProp::Rot3DX: return node->transform3D.rx;
        case VisualAnimProp::Rot3DY: return node->transform3D.ry;
        case VisualAnimProp::Rot3DZ: return node->transform3D.rz;
        case VisualAnimProp::Trans3DX: return node->transform3D.tx;
        case VisualAnimProp::Trans3DY: return node->transform3D.ty;
        case VisualAnimProp::Trans3DZ: return node->transform3D.tz;
        case VisualAnimProp::CornerRadius: return node->box.cornerRadius.topLeft;
        case VisualAnimProp::PathTrimStart: return node->vector.trimStart;
        case VisualAnimProp::PathTrimEnd: return node->vector.trimEnd;
        case VisualAnimProp::EffectRadius: return node->effect.radius;
        case VisualAnimProp::CustomScalar0: return node->customScalars[0];
        case VisualAnimProp::CustomScalar1: return node->customScalars[1];
        case VisualAnimProp::CustomScalar2: return node->customScalars[2];
        case VisualAnimProp::CustomScalar3: return node->customScalars[3];
        default: return 0.0f;
    }
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
            drawBuffer.ClearNode(id);
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
        (void)world.SetAlignment(
            ArgNode(args, 0, world),
            static_cast<HorizontalAlignment>(ArgInt(args, 1, 0)),
            static_cast<VerticalAlignment>(ArgInt(args, 2, 0)));
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
        return MakeFloat(val);
    });
    ok &= bindFn("Visual_ScreenToLocal", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const Point local = world.ScreenToLocal(ArgNode(args, 0, world), Point{ArgFloat(args, 1), ArgFloat(args, 2)});
        return MakeFloat((ArgInt(args, 3, 0) == 0) ? local.x : local.y);
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
        return MakeFloat((ArgInt(args, 5, 0) == 0) ? measured.width : measured.height);
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
    ok &= bindFn("Visual_InvalidateDraw", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.InvalidateDraw(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
    });

    // Animation / Host / Reflection
    ok &= bindFn("Anim_StartFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t tid = animEngine.StartFloat(
            ArgNode(args, 0, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 5, 2)),
            static_cast<EasingMode>(ArgInt(args, 6, 0)),
            ArgFloat(args, 7, 0.0f),
            ArgBool(args, 8, false),
            static_cast<std::int32_t>(ArgInt(args, 9, 1)));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StartColor", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t tid = animEngine.StartColor(
            ArgNode(args, 0, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1, 16)),
            ArgUInt(args, 2),
            ArgUInt(args, 3),
            ArgFloat(args, 4, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 5, 2)),
            static_cast<EasingMode>(ArgInt(args, 6, 0)));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_SpringFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId nodeId = ArgNode(args, 0, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1, 0));
        const std::int32_t tid = animEngine.SpringFloat(
            nodeId,
            prop,
            ReadCurrentScalarProp(world, nodeId, prop),
            ArgFloat(args, 2),
            ArgFloat(args, 3, 320.0f),
            ArgFloat(args, 4, 28.0f));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StopAll", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.StopAll(ArgNode(args, 0, world));
        return rs_rt::Value{std::int64_t{0}};
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
    ok &= bindFn("Host_CloseWindow", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->closeWindow) {
            platformHooks->closeWindow();
        }
        return rs_rt::Value{std::int64_t{0}};
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
            if (val.tryGetFloat(fv)) return MakeFloat(fv);
            double dv = 0.0;
            if (val.tryGetDouble(dv)) return MakeFloat(static_cast<float>(dv));
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return MakeFloat(static_cast<float>(iv));
            if (const auto* uv = std::get_if<rs_rt::UIntValue>(&val)) {
                return MakeFloat(static_cast<float>(uv->value));
            }
        }
        return MakeFloat(0.0f);
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
        return rs_rt::Value{WriteDotPathValue(reflectEnv, target, propPath, MakeFloat(ArgFloat(args, 2)))};
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
            const std::uint64_t mask = heap->peekDirtyMask(target);
            return rs_rt::Value{static_cast<std::int64_t>(mask)};
        }
        return rs_rt::Value{static_cast<std::int64_t>(0)};
    });
    ok &= bindFn("Color_ParseRgba", [reflectEnv](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        std::string rawColor;
        if (!TryExtractStringValue(reflectEnv, args.size() > 0u ? args[0] : rs_rt::Value{}, rawColor)) {
            rawColor = ArgString(args, 0);
        }
        return MakeUInt(ParseColorRgba32(rawColor));
    });

    return ok;
}

} // namespace aero
