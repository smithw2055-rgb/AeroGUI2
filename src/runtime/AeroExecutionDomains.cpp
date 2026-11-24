#include "aero/runtime/AeroExecutionDomains.hpp"

#include "aero/runtime/AeroObservableList.hpp"
#include "realscript/aot_cpp/AotRuntime.h"
#include "AeroHeapHooksInternal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace aero {
namespace {

using realscript::runtime::CrossHeapAccessPolicy;
using realscript::runtime::ErrorCode;
using realscript::runtime::ExecutionResult;
using realscript::runtime::Interpreter;
using realscript::runtime::ManagedHeap;
using realscript::runtime::NativeHandle;
using realscript::runtime::NativeHandleRegistry;
using realscript::runtime::NullString;
using realscript::runtime::ObjectKind;
using realscript::runtime::ObjectRef;
using realscript::runtime::ProgramImage;
using realscript::runtime::RuntimeError;
using realscript::runtime::ShadowStack;
using realscript::runtime::StringValue;
using realscript::runtime::Value;
using realscript::runtime::ValueSpan;
using realscript::runtime::WeakRef;
using realscript::semantic::SymbolId;
using realscript::semantic::TypeKind;
using realscript::semantic::TypeSymbol;

void setDomainError(RuntimeError* error, ErrorCode code, std::string message) {
    if (!error) return;
    error->code = code;
    error->message = std::move(message);
}

void setDomainErrorRef(RuntimeError& error, ErrorCode code, std::string message) {
    error.code = code;
    error.message = std::move(message);
}

[[nodiscard]] constexpr std::uint64_t refKey(ObjectRef ref) noexcept {
    return (static_cast<std::uint64_t>(ref.slot) << 32) |
        static_cast<std::uint64_t>(ref.generation);
}

[[nodiscard]] Value sanitizeCrossHeapValue(const ManagedHeap& sourceHeap, const Value& val) {
    if (const auto* objRef = std::get_if<ObjectRef>(&val)) {
        if (objRef->kind == ObjectKind::String) {
            if (const auto sv = sourceHeap.stringView(*objRef)) {
                return Value{StringValue(std::string(*sv))};
            }
            return Value{NullString{}};
        }
    }
    return val;
}

[[nodiscard]] bool valuesEqualForDirtyCheck(
    const ManagedHeap& heap,
    const Value& current,
    const Value& incoming) noexcept {
    if (current == incoming) {
        return true;
    }
    if (const auto* lhsRef = std::get_if<ObjectRef>(&current)) {
        if (const auto* rhsRef = std::get_if<ObjectRef>(&incoming)) {
            if (lhsRef->kind == ObjectKind::String &&
                rhsRef->kind == ObjectKind::String &&
                lhsRef->heapId == static_cast<std::uint32_t>(heap.heapId()) &&
                rhsRef->heapId == static_cast<std::uint32_t>(heap.heapId())) {
                const auto lsv = heap.stringView(*lhsRef);
                const auto rsv = heap.stringView(*rhsRef);
                return lsv.has_value() && rsv.has_value() && *lsv == *rsv;
            }
        }
    }
    return false;
}

enum class DomainAttrFlag : std::uint8_t {
    None = 0,
    Observable = 1 << 0,
    SimState = 1 << 1,
    SimCommand = 1 << 2,
};

[[nodiscard]] std::uint8_t classifyDomainAttribute(std::string_view name) noexcept {
    constexpr std::string_view suffix = "Attribute";
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.remove_suffix(suffix.size());
    }
    if (name == "Observable") return static_cast<std::uint8_t>(DomainAttrFlag::Observable);
    if (name == "SimState") return static_cast<std::uint8_t>(DomainAttrFlag::SimState);
    if (name == "SimCommand") return static_cast<std::uint8_t>(DomainAttrFlag::SimCommand);
    return static_cast<std::uint8_t>(DomainAttrFlag::None);
}

struct ParsedTargetAttrs {
    std::unordered_map<std::string, std::uint8_t> typeFlags;
    // Keyed by "Owner::memberName" (or full target string for overloaded methods)
    std::unordered_map<std::string, std::uint8_t> fieldFlags;
    std::unordered_map<std::string, std::uint8_t> propFlags;
    std::unordered_map<std::string, std::uint8_t> methodOrFnExactFlags;
    std::unordered_map<std::string, std::uint8_t> methodOrFnStemFlags;
};

void addAttrFlag(
    std::unordered_map<std::string, std::uint8_t>& map,
    std::string key,
    std::uint8_t flag) {
    if (key.empty() || flag == 0) return;
    map[std::move(key)] |= flag;
}

void recordParsedAttribute(
    ParsedTargetAttrs& out,
    std::string_view target,
    std::string_view attrName) {
    const std::uint8_t flag = classifyDomainAttribute(attrName);
    if (flag == 0 || target.empty()) {
        return;
    }

    // Standard L0 targets produced by memberAttributeTarget():
    //   Type:     "<Mod>::<Type>" or "<Type>"
    //   Field:    "<Owner>::field:<FieldName>"
    //   Property: "<Owner>::property:<PropName>"
    //   Method:   "<Owner>::method:<MethodName>#<arity>"
    //   Function: "<Mod>::function:<FuncName>#<arity>"
    // Shorthand targets accepted for hand-crafted descriptors:
    //   "<Owner>.<MemberName>" or "<Mod>::<FuncName>"
    const auto fieldPos = target.rfind("::field:");
    if (fieldPos != std::string_view::npos) {
        std::string key(target.substr(0, fieldPos));
        key.append("::");
        key.append(target.substr(fieldPos + 8u));
        addAttrFlag(out.fieldFlags, std::move(key), flag);
        return;
    }

    const auto propPos = target.rfind("::property:");
    if (propPos != std::string_view::npos) {
        std::string key(target.substr(0, propPos));
        key.append("::");
        key.append(target.substr(propPos + 11u));
        addAttrFlag(out.propFlags, std::move(key), flag);
        return;
    }

    const auto methodPos = target.rfind("::method:");
    if (methodPos != std::string_view::npos) {
        const std::string_view owner = target.substr(0, methodPos);
        const std::string_view tail = target.substr(methodPos + 9u);
        const auto hashPos = tail.find('#');
        const std::string_view methodName = (hashPos == std::string_view::npos)
            ? tail
            : tail.substr(0, hashPos);
        std::string stemKey(owner);
        stemKey.push_back('.');
        stemKey.append(methodName);
        if (hashPos != std::string_view::npos) {
            addAttrFlag(out.methodOrFnExactFlags, stemKey + std::string(tail.substr(hashPos)), flag);
        }
        addAttrFlag(out.methodOrFnStemFlags, std::move(stemKey), flag);
        return;
    }

    const auto fnPos = target.rfind("::function:");
    if (fnPos != std::string_view::npos) {
        const std::string_view modName = target.substr(0, fnPos);
        const std::string_view tail = target.substr(fnPos + 11u);
        const auto hashPos = tail.find('#');
        const std::string_view fnName = (hashPos == std::string_view::npos)
            ? tail
            : tail.substr(0, hashPos);
        std::string stemKey(modName);
        stemKey.append("::");
        stemKey.append(fnName);
        if (hashPos != std::string_view::npos) {
            addAttrFlag(out.methodOrFnExactFlags, stemKey + std::string(tail.substr(hashPos)), flag);
        }
        addAttrFlag(out.methodOrFnStemFlags, std::move(stemKey), flag);
        return;
    }

    // Fallback: record both as type target and member target if dot-qualified
    addAttrFlag(out.typeFlags, std::string(target), flag);
    const auto sep = target.rfind("::");
    const auto afterMod = (sep == std::string_view::npos) ? 0u : (sep + 2u);
    const auto dotPos = target.find('.', afterMod);
    if (dotPos != std::string_view::npos) {
        const std::string_view owner = target.substr(0, dotPos);
        const std::string_view member = target.substr(dotPos + 1u);
        addAttrFlag(out.fieldFlags, std::string(owner) + "::" + std::string(member), flag);
        addAttrFlag(out.propFlags, std::string(owner) + "::" + std::string(member), flag);
        addAttrFlag(out.methodOrFnStemFlags, std::string(target), flag);
    } else if (sep != std::string_view::npos) {
        addAttrFlag(out.methodOrFnStemFlags, std::string(target), flag);
    }
}

[[nodiscard]] std::uint8_t lookupFlag(
    const std::unordered_map<std::string, std::uint8_t>& map,
    const std::string& key) noexcept {
    const auto it = map.find(key);
    return it != map.end() ? it->second : 0u;
}

[[nodiscard]] bool isCollectionWrapperShortName(std::string_view name) noexcept {
    return name.rfind("ObservableList__", 0) == 0 ||
        name.rfind("ObservableCollection__", 0) == 0;
}

[[nodiscard]] bool isImplicitlyObservableTypeName(std::string_view name) noexcept {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1u);
    }
    if (const auto dot = name.rfind('.'); dot != std::string_view::npos) {
        name = name.substr(dot + 1u);
    }
    return name == "INotifyPropertyChanged" ||
        name == "ObservableObject" ||
        name == "ViewModelBase" ||
        name == "BindableBase";
}

[[nodiscard]] std::string makeCanonicalTypeName(
    std::string_view moduleName,
    std::string_view typeName) {
    if (moduleName.empty()) {
        return std::string(typeName);
    }
    std::string out(moduleName);
    out.append("::");
    out.append(typeName);
    return out;
}

[[nodiscard]] std::uint8_t queryFunctionCommandFlags(
    const ParsedTargetAttrs& attrs,
    std::string_view ownerOrMod,
    std::string_view funcLocalName,
    std::size_t explicitArity) {
    // Methods use "<Owner>.<Method>", free functions use "<Mod>::<Func>"
    std::string dotKey;
    std::uint8_t flags = 0;
    if (!ownerOrMod.empty()) {
        dotKey.assign(ownerOrMod);
        dotKey.push_back('.');
        dotKey.append(funcLocalName);
        flags |= lookupFlag(
            attrs.methodOrFnExactFlags,
            dotKey + "#" + std::to_string(explicitArity));
        flags |= lookupFlag(attrs.methodOrFnStemFlags, dotKey);

        std::string colonKey(ownerOrMod);
        colonKey.append("::");
        colonKey.append(funcLocalName);
        flags |= lookupFlag(
            attrs.methodOrFnExactFlags,
            colonKey + "#" + std::to_string(explicitArity));
        flags |= lookupFlag(attrs.methodOrFnStemFlags, colonKey);
    }
    return flags;
}

void markPropertyIfAutoOrCustom(
    const TypeSymbol& type,
    const ParsedTargetAttrs& attrs,
    const std::unordered_map<SymbolId, bool>& classObservableByTypeId,
    const std::unordered_map<SymbolId, bool>& classSimStateByTypeId,
    const std::unordered_map<SymbolId, const TypeSymbol*>& typeById,
    std::vector<std::uint8_t>& outFieldToBitMap,
    std::unordered_map<std::string, std::uint8_t>& outCustomPropBits,
    std::unordered_map<std::string, bool>& outCustomPropSimState,
    std::unordered_map<std::string, bool>& outCustomPropSimCommand,
    std::unordered_map<SymbolId, bool>& outSimCommands) {
    const std::string selfCanon = realscript::semantic::canonicalTypeName(type);
    const auto selfObsIt = classObservableByTypeId.find(type.id);
    const bool selfClassObs = (selfObsIt != classObservableByTypeId.end() && selfObsIt->second);
    const auto selfSimIt = classSimStateByTypeId.find(type.id);
    const bool selfClassSim = (selfSimIt != classSimStateByTypeId.end() && selfSimIt->second);

    auto resolveOwnerNames = [&](SymbolId declTid, std::string_view declName,
                                 std::vector<std::string>& outNames,
                                 bool& outDeclClassObs,
                                 bool& outDeclClassSim) {
        outDeclClassObs = selfClassObs;
        outDeclClassSim = selfClassSim;
        outNames.clear();
        if (!selfCanon.empty()) outNames.push_back(selfCanon);
        if (!declName.empty() && declName != selfCanon) {
            outNames.emplace_back(declName);
        }
        if (declTid != 0) {
            if (const auto obsIt = classObservableByTypeId.find(declTid);
                obsIt != classObservableByTypeId.end() && obsIt->second) {
                outDeclClassObs = true;
            }
            if (const auto simIt = classSimStateByTypeId.find(declTid);
                simIt != classSimStateByTypeId.end() && simIt->second) {
                outDeclClassSim = true;
            }
            if (const auto tIt = typeById.find(declTid); tIt != typeById.end() && tIt->second) {
                const std::string dc = realscript::semantic::canonicalTypeName(*tIt->second);
                if (!dc.empty() && std::find(outNames.begin(), outNames.end(), dc) == outNames.end()) {
                    outNames.push_back(dc);
                }
            }
        }
    };

    outFieldToBitMap.assign(type.fields.size(), 0xffu);
    std::uint32_t nextBit = 0u;
    std::vector<std::string> ownerNames;

    for (std::size_t i = 0; i < type.fields.size(); ++i) {
        const auto& field = type.fields[i];
        if (field.staticField || field.constField) {
            continue;
        }
        bool declObs = false;
        bool declSim = false;
        resolveOwnerNames(field.declaringTypeId, field.declaringTypeName, ownerNames, declObs, declSim);

        bool observable = false;
        if (!field.synthetic) {
            observable = declObs;
            if (!observable) {
                for (const auto& owner : ownerNames) {
                    const std::uint8_t flags = lookupFlag(attrs.fieldFlags, owner + "::" + field.name);
                    if ((flags & static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u) {
                        observable = true;
                        break;
                    }
                }
            }
        } else if (!field.name.empty() && field.name[0] == '$') {
            // Auto-property backing field "$PropName"
            const std::string propName = field.name.substr(1u);
            if (propName.rfind("sequence_", 0) != 0) {
                observable = declObs;
                if (!observable) {
                    for (const auto& owner : ownerNames) {
                        const std::uint8_t flags = lookupFlag(attrs.propFlags, owner + "::" + propName);
                        if ((flags & static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u) {
                            observable = true;
                            break;
                        }
                    }
                }
            }
        }

        if (observable) {
            const std::uint8_t bit = static_cast<std::uint8_t>(nextBit < 63u ? nextBit++ : 63u);
            outFieldToBitMap[i] = bit;
        }
    }

    // Properties (auto or custom getter/setter) + [SimCommand] property setters
    for (const auto& prop : type.properties) {
        bool declObs = false;
        bool declSim = false;
        resolveOwnerNames(prop.declaringTypeId, prop.declaringTypeName, ownerNames, declObs, declSim);

        std::uint8_t propFlags = 0u;
        for (const auto& owner : ownerNames) {
            propFlags |= lookupFlag(attrs.propFlags, owner + "::" + prop.name);
        }
        if ((propFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u &&
            prop.setter.has_value() && prop.setter->id != 0) {
            outSimCommands[prop.setter->id] = true;
        }
        if (prop.setter.has_value() && prop.setter->id != 0) {
            const std::size_t explicitParams =
                (prop.setter->method && !prop.setter->staticMethod && !prop.setter->parameters.empty())
                    ? (prop.setter->parameters.size() - 1u)
                    : prop.setter->parameters.size();
            for (const auto& owner : ownerNames) {
                const std::uint8_t setterFlags = queryFunctionCommandFlags(
                    attrs, owner, prop.setter->name, explicitParams);
                if ((setterFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u) {
                    outSimCommands[prop.setter->id] = true;
                    break;
                }
            }
        }

        const bool isCustom = prop.backingFieldIndex >= type.fields.size();
        const bool obs = declObs ||
            ((propFlags & static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u);
        const bool simState = declSim ||
            ((propFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimState)) != 0u);
        const bool simCmd = ((propFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u) ||
            (prop.setter.has_value() && outSimCommands.count(prop.setter->id) != 0u);

        if (isCustom && obs) {
            const std::uint8_t bit = static_cast<std::uint8_t>(nextBit < 63u ? nextBit++ : 63u);
            outCustomPropBits[prop.name] = bit;
        }
        if (simState) {
            outCustomPropSimState[prop.name] = true;
        }
        if (simCmd) {
            outCustomPropSimCommand[prop.name] = true;
        }
    }

    // Type methods for [SimCommand]
    for (const auto& method : type.methods) {
        if (method.id == 0) continue;
        bool declObs = false;
        bool declSim = false;
        resolveOwnerNames(method.declaringTypeId, method.declaringTypeName, ownerNames, declObs, declSim);
        const std::size_t explicitParams =
            (method.method && !method.staticMethod && !method.parameters.empty())
                ? (method.parameters.size() - 1u)
                : method.parameters.size();
        for (const auto& owner : ownerNames) {
            const std::uint8_t fnFlags = queryFunctionCommandFlags(
                attrs, owner, method.name, explicitParams);
            if ((fnFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u) {
                outSimCommands[method.id] = (method.method && !method.staticMethod);
                break;
            }
        }
    }
}

} // namespace

// ============================================================================
// DomainPolicy
// ============================================================================

ManagedHeap* ExecutionDomainContext::DomainPolicy::resolveHeap(
    ObjectRef reference,
    ManagedHeap* defaultHeap) const noexcept {
    if (!owner_) return defaultHeap;
    if (auto* h = owner_->heapForObject(reference)) {
        return h;
    }
    return defaultHeap;
}

CrossHeapAccessPolicy ExecutionDomainContext::DomainPolicy::crossHeapAccessPolicy(
    const ManagedHeap* activeHeap,
    const ManagedHeap* targetHeap) const noexcept {
    if (!owner_ || !targetHeap) {
        return CrossHeapAccessPolicy::AllowReadWrite;
    }
    if (activeHeap == targetHeap) {
        return CrossHeapAccessPolicy::AllowReadWrite;
    }
    if (domain_ == DomainKind::UiPresentation &&
        activeHeap == owner_->uiHeap_.get() &&
        targetHeap == owner_->simHeap_.get()) {
        return CrossHeapAccessPolicy::ReadOnlySanitizeStrings;
    }
    return CrossHeapAccessPolicy::Deny;
}

bool ExecutionDomainContext::DomainPolicy::checkFieldWrite(
    const ManagedHeap* activeHeap,
    const ManagedHeap* targetHeap,
    ObjectRef target,
    SymbolId declaringTypeId,
    std::size_t,
    std::string_view fieldName,
    const Value& newValue,
    RuntimeError& outError) const {
    if (!owner_) return true;

    if (targetHeap && activeHeap && targetHeap != activeHeap) {
        if (domain_ == DomainKind::UiPresentation && targetHeap == owner_->simHeap_.get()) {
            setDomainErrorRef(
                outError,
                ErrorCode::CapabilityDenied,
                "CrossHeapWriteViolation: UI presentation domain cannot mutate [SimState] or SimHeap field '" +
                    std::string(fieldName) + "'");
        } else {
            setDomainErrorRef(
                outError,
                ErrorCode::CapabilityDenied,
                "CrossHeapWriteViolation: domain cannot mutate foreign heap field '" +
                    std::string(fieldName) + "'");
        }
        return false;
    }

    if (domain_ == DomainKind::UiPresentation) {
        SymbolId runtimeTypeId = 0;
        if (targetHeap) {
            if (const auto tidOpt = targetHeap->objectTypeId(target)) {
                runtimeTypeId = *tidOpt;
            }
        }
        if (owner_->isTypeSimState(declaringTypeId) ||
            (runtimeTypeId != 0 && owner_->isTypeSimState(runtimeTypeId))) {
            setDomainErrorRef(
                outError,
                ErrorCode::CapabilityDenied,
                "CrossHeapWriteViolation: UI presentation domain cannot mutate [SimState] field '" +
                    std::string(fieldName) + "'");
            return false;
        }
    }

    if (const auto* valRef = std::get_if<ObjectRef>(&newValue)) {
        if (valRef->valid() && !valRef->isStackSlotRef()) {
            ManagedHeap* valHeap = owner_->heapForObject(*valRef);
            if (targetHeap == owner_->simHeap_.get() && valHeap == owner_->uiHeap_.get()) {
                setDomainErrorRef(
                    outError,
                    ErrorCode::CapabilityDenied,
                    "CrossHeapReferenceViolation: SimDeterministic heap object cannot hold a reference to a UiHeap object");
                return false;
            }
        }
    }

    return true;
}

bool ExecutionDomainContext::DomainPolicy::checkArrayWrite(
    const ManagedHeap* activeHeap,
    const ManagedHeap* targetHeap,
    ObjectRef,
    const Value& newValue,
    RuntimeError& outError) const {
    if (!owner_) return true;

    if (targetHeap && activeHeap && targetHeap != activeHeap) {
        if (domain_ == DomainKind::UiPresentation && targetHeap == owner_->simHeap_.get()) {
            setDomainErrorRef(
                outError,
                ErrorCode::CapabilityDenied,
                "CrossHeapWriteViolation: UiPresentation domain cannot mutate SimHeap array");
        } else {
            setDomainErrorRef(
                outError,
                ErrorCode::CapabilityDenied,
                "CrossHeapWriteViolation: domain cannot mutate foreign heap array");
        }
        return false;
    }

    if (const auto* valRef = std::get_if<ObjectRef>(&newValue)) {
        if (valRef->valid() && !valRef->isStackSlotRef()) {
            ManagedHeap* valHeap = owner_->heapForObject(*valRef);
            if (targetHeap == owner_->simHeap_.get() && valHeap == owner_->uiHeap_.get()) {
                setDomainErrorRef(
                    outError,
                    ErrorCode::CapabilityDenied,
                    "CrossHeapReferenceViolation: SimDeterministic heap array cannot store a reference to a UiHeap object");
                return false;
            }
        }
    }

    return true;
}

bool ExecutionDomainContext::DomainPolicy::interceptsCalls() const noexcept {
    return domain_ == DomainKind::UiPresentation &&
           owner_ != nullptr &&
           owner_->hasSimCommands_.load(std::memory_order_relaxed);
}

bool ExecutionDomainContext::DomainPolicy::interceptsSameHeapWrites() const noexcept {
    return domain_ != DomainKind::UiPresentation ||
           (owner_ != nullptr && owner_->hasSimStateTypes_.load(std::memory_order_relaxed));
}

bool ExecutionDomainContext::DomainPolicy::interceptCall(
    SymbolId functionId,
    ValueSpan arguments,
    bool& outIntercepted,
    Value& outResult,
    RuntimeError&) {
    outIntercepted = false;
    if (!owner_ || domain_ != DomainKind::UiPresentation) {
        return true;
    }

    bool isInstanceMethod = false;
    bool knownCommand = false;
    {
        std::lock_guard<std::mutex> lock(owner_->mutex_);
        const auto it = owner_->simCommandFunctions_.find(functionId);
        if (it != owner_->simCommandFunctions_.end()) {
            knownCommand = true;
            isInstanceMethod = it->second;
        }
    }
    if (!knownCommand) {
        return true;
    }

    SimCommandEnvelope envelope;
    envelope.commandTypeId = functionId;
    std::size_t startIdx = 0;
    if (isInstanceMethod && !arguments.empty()) {
        if (const auto* recvRef = std::get_if<ObjectRef>(&arguments[0])) {
            if (recvRef->valid() && recvRef->kind == ObjectKind::Record) {
                envelope.targetObject = *recvRef;
                startIdx = 1u;
            }
        }
    }
    envelope.payload.reserve(arguments.size() > startIdx ? (arguments.size() - startIdx) : 0u);
    for (std::size_t i = startIdx; i < arguments.size(); ++i) {
        envelope.payload.push_back(arguments[i]);
    }
    owner_->enqueueSimCommand(std::move(envelope));
    outIntercepted = true;
    outResult = Value{};
    return true;
}

// ============================================================================
// ExecutionDomainContext
// ============================================================================

[[nodiscard]] std::shared_ptr<ManagedHeap> MakeDefaultDomainHeap() {
    realscript::runtime::HeapConfig cfg{};
    cfg.initialCollectionThresholdBytes = 8u * 1024u * 1024u;
    cfg.maximumHeapBytes = 256u * 1024u * 1024u;
    return std::make_shared<ManagedHeap>(cfg);
}

ExecutionDomainContext::ExecutionDomainContext()
    : simHeap_(MakeDefaultDomainHeap()),
      uiHeap_(MakeDefaultDomainHeap()),
      simHeapId_(simHeap_->heapId()),
      uiHeapId_(uiHeap_->heapId()),
      nativeHandles_(std::make_shared<NativeHandleRegistry>()),
      simPolicy_(this, DomainKind::SimDeterministic),
      uiPolicy_(this, DomainKind::UiPresentation) {
    detail::RetainHeapHookUser(*simHeap_);
    detail::RetainHeapHookUser(*uiHeap_);
    detail::RegisterFieldMutationListener(*simHeap_, this, &onHeapFieldMutatedCallback, this);
    detail::RegisterFieldMutationListener(*uiHeap_, this, &onHeapFieldMutatedCallback, this);
}

ExecutionDomainContext::ExecutionDomainContext(
    std::shared_ptr<ManagedHeap> simHeap,
    std::shared_ptr<ManagedHeap> uiHeap,
    std::shared_ptr<NativeHandleRegistry> nativeHandles)
    : simHeap_(simHeap ? std::move(simHeap) : MakeDefaultDomainHeap()),
      uiHeap_(uiHeap ? std::move(uiHeap) : MakeDefaultDomainHeap()),
      simHeapId_(simHeap_->heapId()),
      uiHeapId_(uiHeap_->heapId()),
      nativeHandles_(nativeHandles ? std::move(nativeHandles) : std::make_shared<NativeHandleRegistry>()),
      simPolicy_(this, DomainKind::SimDeterministic),
      uiPolicy_(this, DomainKind::UiPresentation) {
    detail::RetainHeapHookUser(*simHeap_);
    detail::RetainHeapHookUser(*uiHeap_);
    detail::RegisterFieldMutationListener(*simHeap_, this, &onHeapFieldMutatedCallback, this);
    detail::RegisterFieldMutationListener(*uiHeap_, this, &onHeapFieldMutatedCallback, this);
}

ExecutionDomainContext::~ExecutionDomainContext() {
    if (simHeapId_ != 0) {
        detail::UnregisterFieldMutationListener(simHeapId_, this);
    }
    if (uiHeapId_ != 0) {
        detail::UnregisterFieldMutationListener(uiHeapId_, this);
    }
    detail::ReleaseHeapHookUser(simHeap_.get());
    detail::ReleaseHeapHookUser(uiHeap_.get());
}

void ExecutionDomainContext::onHeapFieldMutatedCallback(
    void* context,
    ManagedHeap& heap,
    ObjectRef object,
    std::size_t fieldIndex,
    std::uint8_t dirtyBit) {
    if (auto* self = static_cast<ExecutionDomainContext*>(context)) {
        self->handleFieldMutated(heap, object, fieldIndex, dirtyBit);
    }
}

void ExecutionDomainContext::handleFieldMutated(
    ManagedHeap& heap,
    ObjectRef object,
    std::size_t fieldIndex,
    std::uint8_t dirtyBit) {
    if (!object.valid() || object.kind != ObjectKind::Record) {
        return;
    }

    const std::uint64_t heapKey = heap.heapId();
    const std::uint64_t objKey = refKey(object);
    const auto typeIdOpt = heap.objectTypeId(object);
    const bool isWrapper = typeIdOpt.has_value() && ObservableListHelper::IsWrapperType(*typeIdOpt);

    if (isWrapper) {
        if (fieldIndex == 0u) {
            if (dirtyBit < 64u) {
                ObservableListHelper::UnbindAllForOwnerField(heap, object, dirtyBit);
            }
            const Value* field0 = heap.fieldGetFast(object, 0u);
            if (field0) {
                if (const auto* arrRef = std::get_if<ObjectRef>(field0);
                    arrRef && arrRef->valid() && arrRef->kind == ObjectKind::Array &&
                    arrRef->heapId == static_cast<std::uint32_t>(heapKey)) {
                    ObservableListHelper::EnableTracking(heap, *arrRef);
                    if (dirtyBit < 64u) {
                        ObservableListHelper::BindOwnerProperty(heap, *arrRef, object, dirtyBit);
                    }
                    std::vector<WrapperOwnerLink> ownersCopy;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto hIt = wrapperOwnersByHeap_.find(heapKey);
                        if (hIt != wrapperOwnersByHeap_.end()) {
                            const auto wIt = hIt->second.find(objKey);
                            if (wIt != hIt->second.end()) {
                                ownersCopy = wIt->second;
                            }
                        }
                    }
                    for (const auto& link : ownersCopy) {
                        if (link.ownerRef.valid() && link.bitIndex < 64u) {
                            ObservableListHelper::BindOwnerProperty(
                                heap, *arrRef, link.ownerRef, link.bitIndex);
                        }
                    }
                }
            }
        }
        return;
    }

    if (dirtyBit >= 64u) {
        return;
    }

    ObservableListHelper::UnbindAllForOwnerField(heap, object, dirtyBit);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto hIt = wrapperOwnersByHeap_.find(heapKey);
        if (hIt != wrapperOwnersByHeap_.end()) {
            for (auto wIt = hIt->second.begin(); wIt != hIt->second.end();) {
                auto& links = wIt->second;
                links.erase(
                    std::remove_if(
                        links.begin(),
                        links.end(),
                        [&](const WrapperOwnerLink& link) {
                            return link.ownerRef == object && link.bitIndex == dirtyBit;
                        }),
                    links.end());
                if (links.empty()) {
                    wIt = hIt->second.erase(wIt);
                } else {
                    ++wIt;
                }
            }
        }
    }

    const Value* fieldVal = heap.fieldGetFast(object, fieldIndex);
    if (!fieldVal) {
        return;
    }
    const auto* valRef = std::get_if<ObjectRef>(fieldVal);
    if (!valRef || !valRef->valid() || valRef->heapId != static_cast<std::uint32_t>(heapKey)) {
        return;
    }

    if (valRef->kind == ObjectKind::Array) {
        ObservableListHelper::EnableTracking(heap, *valRef);
        ObservableListHelper::BindOwnerProperty(heap, *valRef, object, dirtyBit);
        return;
    }

    if (valRef->kind == ObjectKind::Record) {
        const auto valTypeId = heap.objectTypeId(*valRef);
        if (valTypeId && ObservableListHelper::IsWrapperType(*valTypeId)) {
            const std::uint64_t wrapperKey = refKey(*valRef);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto& links = wrapperOwnersByHeap_[heapKey][wrapperKey];
                const bool exists = std::any_of(
                    links.begin(),
                    links.end(),
                    [&](const WrapperOwnerLink& link) {
                        return link.ownerRef == object && link.bitIndex == dirtyBit;
                    });
                if (!exists) {
                    links.push_back(WrapperOwnerLink{object, dirtyBit});
                }
            }
            const ObjectRef backingArr = ObservableListHelper::ResolveBackingArray(heap, *valRef);
            if (backingArr.valid() && backingArr.kind == ObjectKind::Array &&
                backingArr.heapId == static_cast<std::uint32_t>(heapKey)) {
                ObservableListHelper::EnableTracking(heap, backingArr);
                ObservableListHelper::BindOwnerProperty(heap, backingArr, object, dirtyBit);
            }
        }
    }
}

ManagedHeap* ExecutionDomainContext::heapForObject(ObjectRef ref) const noexcept {
    if (!ref.valid()) return nullptr;
    if (uiHeap_ && ref.heapId == static_cast<std::uint32_t>(uiHeap_->heapId())) {
        return uiHeap_.get();
    }
    if (simHeap_ && ref.heapId == static_cast<std::uint32_t>(simHeap_->heapId())) {
        return simHeap_.get();
    }
    return nullptr;
}

std::optional<std::string_view> ExecutionDomainContext::stringViewAcrossDomains(
    ObjectRef ref) const {
    if (const auto* heap = heapForObject(ref)) {
        return heap->stringView(ref);
    }
    return std::nullopt;
}

void ExecutionDomainContext::attachInterpreter(Interpreter& interpreter, DomainKind domain) {
    interpreter.setExecutionPolicy(domain == DomainKind::SimDeterministic ? &simPolicy_ : &uiPolicy_);
}

void ExecutionDomainContext::detachInterpreter(Interpreter& interpreter) noexcept {
    interpreter.setExecutionPolicy(nullptr);
}

void ExecutionDomainContext::attachAotContext(
    realscript::aot::ExecutionContext& ctx,
    DomainKind domain) {
    registerAotProgramAttributes(ctx.program());
    ctx.setExecutionPolicy(domain == DomainKind::SimDeterministic ? &simPolicy_ : &uiPolicy_);
}

void ExecutionDomainContext::detachAotContext(
    realscript::aot::ExecutionContext& ctx) noexcept {
    ctx.setExecutionPolicy(nullptr);
}

void ExecutionDomainContext::ensureProgramMetadataComputedLocked(
    const ProgramImage& program,
    bool pushToHeaps) const {
    const void* imageKey = &program.modules();
    if (!pushToHeaps && registeredImages_.find(imageKey) != registeredImages_.end()) {
        return;
    }

    ParsedTargetAttrs attrs;
    std::unordered_map<SymbolId, const TypeSymbol*> typeById;
    std::vector<const TypeSymbol*> orderedTypes;

    for (const auto& mod : program.modules()) {
        for (const auto& attr : mod.languageMetadata.attributes) {
            recordParsedAttribute(attrs, attr.target, attr.name);
        }
        for (const auto& type : mod.types) {
            if (type.id != 0 && typeById.find(type.id) == typeById.end()) {
                typeById.emplace(type.id, &type);
                orderedTypes.push_back(&type);
            }
        }
    }

    // Propagate class-level [Observable] and [SimState] from root base classes down
    std::unordered_map<SymbolId, bool> classObs;
    std::unordered_map<SymbolId, bool> classSim;
    auto computeClassFlags = [&](auto& self, const TypeSymbol* type, std::unordered_set<SymbolId>& visiting) -> std::pair<bool, bool> {
        if (!type || type->id == 0) return {false, false};
        if (const auto it = classObs.find(type->id); it != classObs.end()) {
            return {it->second, classSim[type->id]};
        }
        if (!visiting.insert(type->id).second) {
            return {false, false};
        }

        const std::string canon = realscript::semantic::canonicalTypeName(*type);
        const std::uint8_t ownFlags = lookupFlag(attrs.typeFlags, canon);
        bool obs = (ownFlags & static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u;
        bool sim = (ownFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimState)) != 0u;

        if (!obs && isImplicitlyObservableTypeName(type->name)) {
            obs = true;
        }

        if (!obs) {
            for (const auto& iface : type->baseInterfaceNames) {
                if (isImplicitlyObservableTypeName(iface)) {
                    obs = true;
                    break;
                }
            }
        }

        for (const auto& imap : type->interfaceDispatchMaps) {
            if (imap.interfaceTypeId == 0) continue;
            if (const auto iIt = typeById.find(imap.interfaceTypeId); iIt != typeById.end()) {
                const auto [iObs, iSim] = self(self, iIt->second, visiting);
                obs = obs || iObs;
                sim = sim || iSim;
            } else if (!obs &&
                       (imap.interfaceTypeId == realscript::semantic::stableTypeId("INotifyPropertyChanged") ||
                        imap.interfaceTypeId == realscript::semantic::stableTypeId("Aero.UI::INotifyPropertyChanged"))) {
                obs = true;
            }
        }

        if (type->baseTypeId != 0 && type->baseTypeId != type->id) {
            if (const auto bIt = typeById.find(type->baseTypeId); bIt != typeById.end()) {
                const auto [bObs, bSim] = self(self, bIt->second, visiting);
                obs = obs || bObs;
                sim = sim || bSim;
            } else if (!obs && isImplicitlyObservableTypeName(type->baseTypeName)) {
                obs = true;
            }
        }
        visiting.erase(type->id);
        classObs[type->id] = obs;
        classSim[type->id] = sim;
        return {obs, sim};
    };

    for (const TypeSymbol* type : orderedTypes) {
        std::unordered_set<SymbolId> visiting;
        (void)computeClassFlags(computeClassFlags, type, visiting);
    }

    // Collect type method / property accessor symbol IDs so free module function lookup skips methods
    std::unordered_set<SymbolId> typeMethodIds;
    for (const TypeSymbol* type : orderedTypes) {
        for (const auto& m : type->methods) {
            if (m.id != 0) typeMethodIds.insert(m.id);
        }
        for (const auto& p : type->properties) {
            if (p.getter.has_value() && p.getter->id != 0) typeMethodIds.insert(p.getter->id);
            if (p.setter.has_value() && p.setter->id != 0) typeMethodIds.insert(p.setter->id);
        }
    }

    // Free module functions for [SimCommand]
    for (const auto& mod : program.modules()) {
        for (const auto& fn : mod.functions) {
            if (fn.symbolId == 0 || typeMethodIds.count(fn.symbolId) != 0u) continue;
            const std::uint8_t flags = queryFunctionCommandFlags(
                attrs, mod.name, fn.name, fn.parameterTypes.size());
            if ((flags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u) {
                simCommandFunctions_[fn.symbolId] = false;
            }
        }
    }

    std::vector<std::pair<SymbolId, std::vector<std::uint8_t>>> dirtyBatch;
    if (pushToHeaps) {
        dirtyBatch.reserve(orderedTypes.size());
    }
    bool sawAnySimState = false;

    for (const TypeSymbol* type : orderedTypes) {
        if (type->id == 0) continue;

        if (pushToHeaps && type->baseTypeId != 0 && type->baseTypeId != type->id) {
            if (simHeap_) simHeap_->registerTypeParent(type->id, type->baseTypeId);
            if (uiHeap_) uiHeap_->registerTypeParent(type->id, type->baseTypeId);
        }

        const bool isWrapper = type->kind == TypeKind::Class &&
            type->fields.size() == 1u &&
            isCollectionWrapperShortName(type->name);
        if (isWrapper) {
            ObservableListHelper::RegisterWrapperType(type->id);
            if (pushToHeaps) {
                dirtyBatch.emplace_back(type->id, std::vector<std::uint8_t>{0u});
            }
            TypeObservableMeta meta;
            meta.isObservable = true;
            meta.isCollectionWrapper = true;
            meta.fieldToBitMap = {0u};
            typeMeta_[type->id] = std::move(meta);
            continue;
        }

        TypeObservableMeta meta;
        meta.isSimState = classSim[type->id];
        meta.isObservable = classObs[type->id];

        markPropertyIfAutoOrCustom(
            *type,
            attrs,
            classObs,
            classSim,
            typeById,
            meta.fieldToBitMap,
            meta.customPropBits,
            meta.customPropSimState,
            meta.customPropSimCommand,
            simCommandFunctions_);

        if (meta.isSimState || !meta.customPropSimState.empty()) {
            sawAnySimState = true;
        }

        bool anyDirtyBit = false;
        for (const std::uint8_t bit : meta.fieldToBitMap) {
            if (bit != 0xffu) {
                anyDirtyBit = true;
                break;
            }
        }
        if (!meta.customPropBits.empty()) {
            anyDirtyBit = true;
        }
        if (anyDirtyBit) {
            meta.isObservable = true;
        }

        if (pushToHeaps && anyDirtyBit && !meta.fieldToBitMap.empty()) {
            dirtyBatch.emplace_back(type->id, meta.fieldToBitMap);
        }

        typeMeta_[type->id] = std::move(meta);
    }

    if (pushToHeaps && !dirtyBatch.empty()) {
        if (simHeap_) simHeap_->enableTypeDirtyTrackingBatch(dirtyBatch);
        if (uiHeap_) uiHeap_->enableTypeDirtyTrackingBatch(std::move(dirtyBatch));
    }
    if (pushToHeaps) {
        registeredImages_.insert(imageKey);
    }
    if (sawAnySimState) {
        hasSimStateTypes_.store(true, std::memory_order_release);
    }
    if (!simCommandFunctions_.empty()) {
        hasSimCommands_.store(true, std::memory_order_release);
    }
}

void ExecutionDomainContext::registerProgramObservableMetadata(const ProgramImage& program) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureProgramMetadataComputedLocked(program, true);
}

void ExecutionDomainContext::registerAotProgramAttributes(
    const realscript::aot::ProgramDescriptor& program) {
    ParsedTargetAttrs attrs;
    if (program.attributes && program.attributeCount > 0u) {
        for (std::uint32_t i = 0; i < program.attributeCount; ++i) {
            const auto& attr = program.attributes[i];
            if (attr.target && attr.name) {
                recordParsedAttribute(attrs, attr.target, attr.name);
            }
        }
    }

    std::unordered_map<SymbolId, const realscript::aot::TypeDescriptor*> typeById;
    if (program.types && program.typeCount > 0u) {
        for (std::uint32_t i = 0; i < program.typeCount; ++i) {
            const auto& td = program.types[i];
            if (td.id != 0) {
                typeById.emplace(td.id, &td);
            }
        }
    }

    std::unordered_map<SymbolId, bool> classObs;
    std::unordered_map<SymbolId, bool> classSim;
    auto resolveTypeFlags = [&](auto& self, const realscript::aot::TypeDescriptor* td, std::unordered_set<SymbolId>& visiting) -> std::pair<bool, bool> {
        if (!td || td->id == 0) return {false, false};
        if (const auto it = classObs.find(td->id); it != classObs.end()) {
            return {it->second, classSim[td->id]};
        }
        if (!visiting.insert(td->id).second) {
            return {false, false};
        }
        const std::string canon = makeCanonicalTypeName(
            td->moduleName ? td->moduleName : "",
            td->name ? td->name : "");
        const std::uint8_t ownFlags = lookupFlag(attrs.typeFlags, canon);
        bool obs = (ownFlags & static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u;
        bool sim = (ownFlags & static_cast<std::uint8_t>(DomainAttrFlag::SimState)) != 0u;
        if (!obs && td->name && isImplicitlyObservableTypeName(td->name)) {
            obs = true;
        }
        if (td->interfaceDispatchMaps && td->interfaceDispatchMapCount > 0u) {
            for (std::uint32_t k = 0; k < td->interfaceDispatchMapCount; ++k) {
                const SymbolId ifaceId = td->interfaceDispatchMaps[k].interfaceTypeId;
                if (ifaceId == 0) continue;
                if (const auto iIt = typeById.find(ifaceId); iIt != typeById.end()) {
                    const auto [iObs, iSim] = self(self, iIt->second, visiting);
                    obs = obs || iObs;
                    sim = sim || iSim;
                } else if (!obs &&
                           (ifaceId == realscript::semantic::stableTypeId("INotifyPropertyChanged") ||
                            ifaceId == realscript::semantic::stableTypeId("Aero.UI::INotifyPropertyChanged"))) {
                    obs = true;
                }
            }
        }
        if (td->baseTypeId != 0 && td->baseTypeId != td->id) {
            if (const auto bIt = typeById.find(td->baseTypeId); bIt != typeById.end()) {
                const auto [bObs, bSim] = self(self, bIt->second, visiting);
                obs = obs || bObs;
                sim = sim || bSim;
            }
        }
        visiting.erase(td->id);
        classObs[td->id] = obs;
        classSim[td->id] = sim;
        return {obs, sim};
    };

    if (program.types && program.typeCount > 0u) {
        for (std::uint32_t i = 0; i < program.typeCount; ++i) {
            std::unordered_set<SymbolId> visiting;
            (void)resolveTypeFlags(resolveTypeFlags, &program.types[i], visiting);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (program.types && program.typeCount > 0u) {
        for (std::uint32_t ti = 0; ti < program.typeCount; ++ti) {
            const auto& td = program.types[ti];
            if (td.id == 0) continue;

            if (td.baseTypeId != 0 && td.baseTypeId != td.id) {
                if (simHeap_) simHeap_->registerTypeParent(td.id, td.baseTypeId);
                if (uiHeap_) uiHeap_->registerTypeParent(td.id, td.baseTypeId);
            }

            const std::string_view shortName = td.name ? std::string_view(td.name) : std::string_view{};
            const bool isWrapper = td.kind == TypeKind::Class &&
                td.fieldCount == 1u &&
                isCollectionWrapperShortName(shortName);
            if (isWrapper) {
                ObservableListHelper::RegisterWrapperType(td.id);
                if (simHeap_) simHeap_->enableTypeDirtyTracking(td.id, {0u});
                if (uiHeap_) uiHeap_->enableTypeDirtyTracking(td.id, {0u});
                TypeObservableMeta meta;
                meta.isObservable = true;
                meta.isCollectionWrapper = true;
                meta.fieldToBitMap = {0u};
                typeMeta_[td.id] = std::move(meta);
                continue;
            }

            const std::string canon = makeCanonicalTypeName(
                td.moduleName ? td.moduleName : "",
                shortName);
            const bool typeObs = classObs[td.id];
            const bool typeSim = classSim[td.id];
            if (typeSim) {
                hasSimStateTypes_.store(true, std::memory_order_release);
            }

            std::vector<std::uint8_t> bitMap(td.fieldCount, 0xffu);
            std::uint32_t nextBit = 0u;
            bool anyFieldBit = false;
            if (td.fields && td.fieldCount > 0u) {
                for (std::uint32_t fi = 0; fi < td.fieldCount; ++fi) {
                    const auto& fd = td.fields[fi];
                    if (fd.staticField || fd.constField) {
                        continue;
                    }
                    const std::string_view fname = fd.name ? std::string_view(fd.name) : std::string_view{};
                    bool observable = false;
                    if (!fd.synthetic) {
                        observable = typeObs ||
                            ((lookupFlag(attrs.fieldFlags, canon + "::" + std::string(fname)) &
                              static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u);
                    } else if (!fname.empty() && fname[0] == '$') {
                        const std::string propName(fname.substr(1u));
                        if (propName.rfind("sequence_", 0) != 0) {
                            observable = typeObs ||
                                ((lookupFlag(attrs.propFlags, canon + "::" + propName) &
                                  static_cast<std::uint8_t>(DomainAttrFlag::Observable)) != 0u);
                        }
                    }
                    if (observable) {
                        bitMap[fi] = static_cast<std::uint8_t>(nextBit < 63u ? nextBit++ : 63u);
                        anyFieldBit = true;
                    }
                }
            }

            if (anyFieldBit) {
                if (simHeap_) simHeap_->enableTypeDirtyTracking(td.id, bitMap);
                if (uiHeap_) uiHeap_->enableTypeDirtyTracking(td.id, bitMap);
            }

            TypeObservableMeta meta;
            meta.isObservable = typeObs || anyFieldBit;
            meta.isSimState = typeSim;
            meta.fieldToBitMap = std::move(bitMap);
            typeMeta_[td.id] = std::move(meta);
        }
    }

    if (program.functions && program.functionCount > 0u) {
        for (std::uint32_t fi = 0; fi < program.functionCount; ++fi) {
            const auto& fn = program.functions[fi];
            if (fn.symbolId == 0 || !fn.name) continue;
            const std::string_view fullName(fn.name);
            // Possible forms:
            //   "<Mod>::<Type>.<Method>"
            //   "<Mod>::<Type>::<Method>"
            //   "<Mod>::<Func>"
            const auto modSep = fullName.find("::");
            const std::size_t afterMod = (modSep == std::string_view::npos) ? 0u : (modSep + 2u);
            const auto dotSep = fullName.rfind('.');
            bool matched = false;
            bool isInstance = false;

            auto tryCheck = [&](std::string_view owner, std::string_view localName, std::size_t arity, bool inst) {
                const std::uint8_t flags = queryFunctionCommandFlags(attrs, owner, localName, arity);
                if ((flags & static_cast<std::uint8_t>(DomainAttrFlag::SimCommand)) != 0u) {
                    matched = true;
                    isInstance = inst;
                    return true;
                }
                return false;
            };

            if (dotSep != std::string_view::npos && dotSep >= afterMod) {
                const std::string_view owner = fullName.substr(0, dotSep);
                const std::string_view method = fullName.substr(dotSep + 1u);
                const bool hasRecvArg = fn.parameterCount > 0u &&
                    fn.parameterTypes &&
                    fn.parameterTypes[0] == realscript::semantic::PrimitiveType::Object;
                const std::size_t instArity = hasRecvArg ? (fn.parameterCount - 1u) : fn.parameterCount;
                if (!(hasRecvArg && tryCheck(owner, method, instArity, true))) {
                    (void)tryCheck(owner, method, fn.parameterCount, false);
                }
            } else {
                const auto lastColon = fullName.rfind("::");
                if (lastColon != std::string_view::npos) {
                    const std::string_view prefix = fullName.substr(0, lastColon);
                    const std::string_view local = fullName.substr(lastColon + 2u);
                    const bool hasRecvArg = fn.parameterCount > 0u &&
                        fn.parameterTypes &&
                        fn.parameterTypes[0] == realscript::semantic::PrimitiveType::Object;
                    if (!tryCheck(prefix, local, fn.parameterCount, false) && hasRecvArg) {
                        (void)tryCheck(prefix, local, fn.parameterCount - 1u, true);
                    }
                }
            }

            if (matched) {
                simCommandFunctions_[fn.symbolId] = isInstance;
            }
        }
    }
    if (!simCommandFunctions_.empty()) {
        hasSimCommands_.store(true, std::memory_order_release);
    }
}

bool ExecutionDomainContext::isTypeSimState(SymbolId typeId) const noexcept {
    if (typeId == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = typeMeta_.find(typeId);
    return it != typeMeta_.end() && it->second.isSimState;
}

bool ExecutionDomainContext::isTypeObservable(SymbolId typeId) const noexcept {
    if (typeId == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = typeMeta_.find(typeId);
    return it != typeMeta_.end() && it->second.isObservable;
}

bool ExecutionDomainContext::isSimCommandFunction(SymbolId functionId) const noexcept {
    if (functionId == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return simCommandFunctions_.find(functionId) != simCommandFunctions_.end();
}

std::optional<Value> ExecutionDomainContext::readCrossHeapFieldFast(
    ObjectRef simObject,
    std::size_t fieldIndex,
    RuntimeError* error) const noexcept {
    if (!isSimObject(simObject)) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "cross-heap read requires a valid SimHeap ObjectRef");
        return std::nullopt;
    }
    const Value* rawVal = simHeap_->fieldGetFast(simObject, fieldIndex);
    if (!rawVal) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "SimHeap object is dead or fieldIndex is out of range");
        return std::nullopt;
    }
    return sanitizeCrossHeapValue(*simHeap_, *rawVal);
}

bool ExecutionDomainContext::readObjectField(
    DomainKind callerDomain,
    ObjectRef target,
    std::size_t fieldIndex,
    Value& outValue,
    RuntimeError* error) const noexcept {
    ManagedHeap* targetHeap = heapForObject(target);
    if (!targetHeap) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "target object does not belong to SimHeap or UiHeap");
        return false;
    }
    const Value* rawVal = targetHeap->fieldGetFast(target, fieldIndex);
    if (!rawVal) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "target object is dead or fieldIndex is out of range");
        return false;
    }
    const bool crossHeapRead =
        (callerDomain == DomainKind::UiPresentation && targetHeap == simHeap_.get()) ||
        (callerDomain == DomainKind::SimDeterministic && targetHeap == uiHeap_.get());
    outValue = crossHeapRead ? sanitizeCrossHeapValue(*targetHeap, *rawVal) : *rawVal;
    return true;
}

bool ExecutionDomainContext::writeObjectField(
    DomainKind callerDomain,
    ObjectRef target,
    std::size_t fieldIndex,
    Value newValue,
    RuntimeError* error) {
    ManagedHeap* targetHeap = heapForObject(target);
    if (!targetHeap) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "target object does not belong to SimHeap or UiHeap");
        return false;
    }
    if (callerDomain == DomainKind::UiPresentation && targetHeap == simHeap_.get()) {
        setDomainError(
            error,
            ErrorCode::CapabilityDenied,
            "CrossHeapWriteViolation: UI presentation domain cannot mutate SimDeterministic heap object directly");
        return false;
    }
    if (callerDomain == DomainKind::SimDeterministic && targetHeap == uiHeap_.get()) {
        setDomainError(
            error,
            ErrorCode::CapabilityDenied,
            "CrossHeapWriteViolation: SimDeterministic domain cannot mutate UiHeap object directly");
        return false;
    }
    if (const auto* valRef = std::get_if<ObjectRef>(&newValue)) {
        if (targetHeap == simHeap_.get() && isUiObject(*valRef)) {
            setDomainError(
                error,
                ErrorCode::CapabilityDenied,
                "CrossHeapReferenceViolation: SimDeterministic heap object cannot hold a reference to a UiHeap object");
            return false;
        }
        if (valRef->kind == ObjectKind::String &&
            valRef->heapId != static_cast<std::uint32_t>(targetHeap->heapId())) {
            if (const auto sv = stringViewAcrossDomains(*valRef)) {
                const auto interned = targetHeap->internString(*sv, error);
                if (!interned) return false;
                newValue = Value{*interned};
            } else {
                setDomainError(
                    error,
                    ErrorCode::InvalidObjectReference,
                    "cross-heap string reference is invalid or collected");
                return false;
            }
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&newValue)) {
        const auto interned = targetHeap->internString(sv->str(), error);
        if (!interned) return false;
        newValue = Value{*interned};
    }
    return targetHeap->fieldSet(target, fieldIndex, std::move(newValue), error);
}

std::optional<PropertySlotHandle> ExecutionDomainContext::resolvePropertySlot(
    const ProgramImage& program,
    SymbolId typeId,
    std::string_view memberName) const noexcept {
    const TypeSymbol* rootType = program.resolveType(typeId);
    if (!rootType) return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex_);
    if (registeredImages_.find(&program.modules()) == registeredImages_.end()) {
        ensureProgramMetadataComputedLocked(program, true);
    }

    auto findTypeMeta = [&](SymbolId tid) -> const TypeObservableMeta* {
        const auto it = typeMeta_.find(tid);
        return it != typeMeta_.end() ? &it->second : nullptr;
    };

    const TypeObservableMeta* rootMeta = findTypeMeta(rootType->id);
    const TypeSymbol* cur = rootType;
    std::unordered_set<SymbolId> visitedTypes;
    while (cur && visitedTypes.insert(cur->id).second) {
        const TypeObservableMeta* curMeta = findTypeMeta(cur->id);
        for (const auto& prop : cur->properties) {
            if (prop.name != memberName) continue;

            PropertySlotHandle handle;
            handle.typeId = rootType->id;
            handle.isReadOnly = !prop.setter.has_value();
            handle.isSimState = (rootMeta && rootMeta->isSimState) ||
                (curMeta && curMeta->isSimState);
            if (curMeta) {
                if (const auto sIt = curMeta->customPropSimState.find(prop.name);
                    sIt != curMeta->customPropSimState.end() && sIt->second) {
                    handle.isSimState = true;
                }
                if (const auto cIt = curMeta->customPropSimCommand.find(prop.name);
                    cIt != curMeta->customPropSimCommand.end() && cIt->second) {
                    handle.isSimCommand = true;
                }
                if (const auto bIt = curMeta->customPropBits.find(prop.name);
                    bIt != curMeta->customPropBits.end() && bIt->second < 64u) {
                    handle.isObservable = true;
                    handle.observableBitIndex = bIt->second;
                }
            }
            if (prop.getter) handle.getterSymbolId = prop.getter->id;
            if (prop.setter) {
                handle.setterSymbolId = prop.setter->id;
                if (simCommandFunctions_.find(prop.setter->id) != simCommandFunctions_.end()) {
                    handle.isSimCommand = true;
                }
            }

            std::size_t resolvedFieldIdx = prop.backingFieldIndex;
            if (resolvedFieldIdx >= cur->fields.size() || cur->id != rootType->id) {
                const std::string expectedBacking = "$" + prop.name;
                for (std::size_t fi = 0; fi < rootType->fields.size(); ++fi) {
                    if (rootType->fields[fi].synthetic &&
                        rootType->fields[fi].name == expectedBacking) {
                        resolvedFieldIdx = fi;
                        break;
                    }
                }
            }
            if (resolvedFieldIdx < rootType->fields.size()) {
                handle.backingFieldIndex = static_cast<std::uint32_t>(resolvedFieldIdx);
                handle.isAutoProperty = true;
                if (rootMeta && resolvedFieldIdx < rootMeta->fieldToBitMap.size()) {
                    const std::uint8_t bit = rootMeta->fieldToBitMap[resolvedFieldIdx];
                    if (bit < 64u) {
                        handle.isObservable = true;
                        handle.observableBitIndex = bit;
                    }
                }
            }
            return handle;
        }
        if (cur->baseTypeId == 0 || cur->baseTypeId == cur->id) {
            break;
        }
        cur = program.resolveType(cur->baseTypeId);
    }

    for (const auto& field : rootType->fields) {
        if (field.name == memberName) {
            PropertySlotHandle handle;
            handle.typeId = rootType->id;
            handle.backingFieldIndex = static_cast<std::uint32_t>(field.index);
            handle.isAutoProperty = true;
            if (rootMeta && field.index < rootMeta->fieldToBitMap.size()) {
                const std::uint8_t bit = rootMeta->fieldToBitMap[field.index];
                if (bit < 64u) {
                    handle.isObservable = true;
                    handle.observableBitIndex = bit;
                }
            }
            handle.isReadOnly = field.readonlyField || field.constField;
            handle.isSimState = rootMeta && rootMeta->isSimState;
            return handle;
        }
    }

    return std::nullopt;
}

std::optional<PropertySlotHandle> ExecutionDomainContext::resolvePropertySlot(
    const ProgramImage& program,
    std::string_view canonicalTypeName,
    std::string_view memberName) const noexcept {
    return resolvePropertySlot(
        program,
        realscript::semantic::stableTypeId(std::string(canonicalTypeName)),
        memberName);
}

std::optional<Value> ExecutionDomainContext::readPropertySlotFast(
    DomainKind callerDomain,
    ObjectRef target,
    const PropertySlotHandle& slot,
    const Interpreter* interpreter,
    RuntimeError* error) const {
    if (!slot.valid()) {
        setDomainError(error, ErrorCode::InvalidArguments, "PropertySlotHandle is invalid");
        return std::nullopt;
    }
    ManagedHeap* targetHeap = heapForObject(target);
    if (!targetHeap) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "target object does not belong to SimHeap or UiHeap");
        return std::nullopt;
    }
    const bool crossHeapRead =
        (callerDomain == DomainKind::UiPresentation && targetHeap == simHeap_.get()) ||
        (callerDomain == DomainKind::SimDeterministic && targetHeap == uiHeap_.get());

    if (slot.isAutoProperty && slot.backingFieldIndex != PropertySlotHandle::InvalidIndex) {
        const Value* val = targetHeap->fieldGetFast(target, slot.backingFieldIndex);
        if (!val) {
            setDomainError(
                error,
                ErrorCode::InvalidObjectReference,
                "target object is dead or slot index is out of range");
            return std::nullopt;
        }
        return crossHeapRead ? sanitizeCrossHeapValue(*targetHeap, *val) : *val;
    }

    if (slot.getterSymbolId != 0 && (interpreter || hasDesktopJitInvoker())) {
        const Value arg0{target};
        const auto exec = invokeMethod(callerDomain, interpreter, slot.getterSymbolId, &arg0, 1u);
        if (!exec.succeeded) {
            if (error) *error = exec.error;
            return std::nullopt;
        }
        return crossHeapRead ? sanitizeCrossHeapValue(*targetHeap, exec.value) : exec.value;
    }

    setDomainError(
        error,
        ErrorCode::FunctionNotFound,
        "property slot has no accessible backing field or getter");
    return std::nullopt;
}

bool ExecutionDomainContext::readPropertySlotFast(
    DomainKind callerDomain,
    ObjectRef target,
    const PropertySlotHandle& slot,
    Value& outValue,
    RuntimeError* error,
    const Interpreter* interpreter) const {
    auto res = readPropertySlotFast(callerDomain, target, slot, interpreter, error);
    if (!res) return false;
    outValue = std::move(*res);
    return true;
}

bool ExecutionDomainContext::writePropertySlotFast(
    DomainKind callerDomain,
    ObjectRef target,
    const PropertySlotHandle& slot,
    const Value& value,
    const Interpreter* interpreter,
    RuntimeError* error) {
    if (!slot.valid()) {
        setDomainError(error, ErrorCode::InvalidArguments, "PropertySlotHandle is invalid");
        return false;
    }
    ManagedHeap* targetHeap = heapForObject(target);
    if (!targetHeap) {
        setDomainError(
            error,
            ErrorCode::InvalidObjectReference,
            "target object does not belong to SimHeap or UiHeap");
        return false;
    }
    if (callerDomain == DomainKind::UiPresentation && slot.isSimCommand && slot.setterSymbolId != 0) {
        SimCommandEnvelope cmd;
        cmd.commandTypeId = slot.setterSymbolId;
        cmd.targetObject = target;
        cmd.payload.push_back(value);
        enqueueSimCommand(std::move(cmd));
        return true;
    }
    if (callerDomain == DomainKind::UiPresentation &&
        (targetHeap == simHeap_.get() || slot.isSimState)) {
        setDomainError(
            error,
            ErrorCode::CapabilityDenied,
            "CrossHeapWriteViolation: UI presentation domain cannot mutate SimDeterministic heap or [SimState] property directly; dispatch a SimCommandEnvelope instead");
        return false;
    }
    if (callerDomain == DomainKind::SimDeterministic && targetHeap == uiHeap_.get()) {
        setDomainError(
            error,
            ErrorCode::CapabilityDenied,
            "CrossHeapWriteViolation: SimDeterministic domain cannot mutate UiHeap object directly");
        return false;
    }
    if (slot.isReadOnly) {
        setDomainError(error, ErrorCode::CapabilityDenied, "cannot write to a read-only property or field slot");
        return false;
    }

    Value writeVal = value;
    if (const auto* valRef = std::get_if<ObjectRef>(&writeVal)) {
        if (targetHeap == simHeap_.get() && isUiObject(*valRef)) {
            setDomainError(
                error,
                ErrorCode::CapabilityDenied,
                "CrossHeapReferenceViolation: SimDeterministic heap object cannot hold a reference to a UiHeap object");
            return false;
        }
        if (valRef->kind == ObjectKind::String &&
            valRef->heapId != static_cast<std::uint32_t>(targetHeap->heapId())) {
            if (const auto sv = stringViewAcrossDomains(*valRef)) {
                const auto interned = targetHeap->internString(*sv, error);
                if (!interned) return false;
                writeVal = Value{*interned};
            } else {
                setDomainError(
                    error,
                    ErrorCode::InvalidObjectReference,
                    "cross-heap string reference is invalid or collected");
                return false;
            }
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&writeVal)) {
        const auto interned = targetHeap->internString(sv->str(), error);
        if (!interned) return false;
        writeVal = Value{*interned};
    }

    if (slot.isAutoProperty && slot.backingFieldIndex != PropertySlotHandle::InvalidIndex) {
        if (const Value* currentVal = targetHeap->fieldGetFast(target, slot.backingFieldIndex)) {
            if (valuesEqualForDirtyCheck(*targetHeap, *currentVal, writeVal)) {
                return true;
            }
        }
        return targetHeap->fieldSet(target, slot.backingFieldIndex, std::move(writeVal), error);
    }

    if (slot.setterSymbolId != 0 && (interpreter || hasDesktopJitInvoker())) {
        const Value callArgs[2]{Value{target}, std::move(writeVal)};
        const auto exec = invokeMethod(callerDomain, interpreter, slot.setterSymbolId, callArgs, 2u);
        if (!exec.succeeded) {
            if (error) *error = exec.error;
            return false;
        }
        return true;
    }

    setDomainError(
        error,
        ErrorCode::FunctionNotFound,
        "property slot has no accessible backing field or setter");
    return false;
}

bool ExecutionDomainContext::writePropertySlotFast(
    DomainKind callerDomain,
    ObjectRef target,
    const PropertySlotHandle& slot,
    const Value& value,
    RuntimeError* error) {
    return writePropertySlotFast(callerDomain, target, slot, value, nullptr, error);
}

void ExecutionDomainContext::enqueueSimCommand(SimCommandEnvelope command) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingSimCommands_.push_back(std::move(command));
}

std::vector<SimCommandEnvelope> ExecutionDomainContext::consumeSimCommands() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SimCommandEnvelope> out;
    out.swap(pendingSimCommands_);
    return out;
}

std::size_t ExecutionDomainContext::flushSimCommands(
    const Interpreter& simInterpreter,
    RuntimeError* error) {
    auto commands = consumeSimCommands();
    std::size_t executedCount = 0;
    for (auto& cmd : commands) {
        std::vector<Value> args;
        args.reserve(cmd.payload.size() + (cmd.targetObject.valid() ? 1u : 0u));
        if (cmd.targetObject.valid()) {
            args.push_back(Value{cmd.targetObject});
        }
        for (auto& val : cmd.payload) {
            if (simHeap_) {
                if (const auto* strRef = std::get_if<ObjectRef>(&val)) {
                    if (strRef->kind == ObjectKind::String &&
                        strRef->heapId != static_cast<std::uint32_t>(simHeap_->heapId())) {
                        if (const auto sv = stringViewAcrossDomains(*strRef)) {
                            RuntimeError strErr;
                            if (const auto interned = simHeap_->internString(*sv, &strErr)) {
                                args.push_back(Value{*interned});
                                continue;
                            }
                        }
                    }
                } else if (const auto* sv = std::get_if<StringValue>(&val)) {
                    RuntimeError strErr;
                    if (const auto interned = simHeap_->internString(sv->str(), &strErr)) {
                        args.push_back(Value{*interned});
                        continue;
                    }
                }
            }
            args.push_back(std::move(val));
        }
        const auto exec = invokeMethod(
            DomainKind::SimDeterministic,
            &simInterpreter,
            cmd.commandTypeId,
            args.data(),
            args.size());
        if (!exec.succeeded) {
            if (error && error->code == ErrorCode::None) {
                *error = exec.error;
            }
            continue;
        }
        ++executedCount;
    }
    return executedCount;
}

std::size_t ExecutionDomainContext::flushSimCommands(
    realscript::aot::ExecutionContext& simAotContext,
    RuntimeError* error) {
    auto commands = consumeSimCommands();
    std::size_t executedCount = 0;
    for (auto& cmd : commands) {
        std::vector<Value> args;
        args.reserve(cmd.payload.size() + (cmd.targetObject.valid() ? 1u : 0u));
        if (cmd.targetObject.valid()) {
            args.push_back(Value{cmd.targetObject});
        }
        for (auto& val : cmd.payload) {
            if (simHeap_) {
                if (const auto* strRef = std::get_if<ObjectRef>(&val)) {
                    if (strRef->kind == ObjectKind::String &&
                        strRef->heapId != static_cast<std::uint32_t>(simHeap_->heapId())) {
                        if (const auto sv = stringViewAcrossDomains(*strRef)) {
                            RuntimeError strErr;
                            if (const auto interned = simHeap_->internString(*sv, &strErr)) {
                                args.push_back(Value{*interned});
                                continue;
                            }
                        }
                    }
                } else if (const auto* sv = std::get_if<StringValue>(&val)) {
                    RuntimeError strErr;
                    if (const auto interned = simHeap_->internString(sv->str(), &strErr)) {
                        args.push_back(Value{*interned});
                        continue;
                    }
                }
            }
            args.push_back(std::move(val));
        }
        Value outResult{};
        if (!simAotContext.invoke(cmd.commandTypeId, args.data(), args.size(), outResult)) {
            if (error && error->code == ErrorCode::None) {
                *error = simAotContext.error();
            }
            continue;
        }
        ++executedCount;
    }
    return executedCount;
}

UiTaskId ExecutionDomainContext::spawnReentrantSequenceTask(
    const ProgramImage& program,
    const Interpreter& uiInterpreter,
    ObjectRef targetObject,
    std::string_view sequenceName,
    std::vector<Value> arguments,
    NativeHandle boundControl,
    UiTaskId awaitPrerequisite) {
    auto frame = std::make_unique<UiAsyncTaskFrame>();
    frame->taskId = nextUiTaskId_++;
    frame->prerequisiteTaskId = awaitPrerequisite;
    frame->targetWeakRef = WeakRef(targetObject);
    frame->boundControl = boundControl;
    frame->isSequenceTask = true;

    ManagedHeap* targetHeap = heapForObject(targetObject);
    if (!targetHeap || !targetHeap->isAlive(targetObject)) {
        frame->status = UiTaskStatus::Faulted;
        const UiTaskId id = frame->taskId;
        uiTasks_.push_back(std::move(frame));
        return id;
    }
    if (boundControl.valid() &&
        (cancelledControlSlots_.count(boundControl.slot) != 0 ||
         (nativeHandles_ && nativeHandles_->resolveRawFast(boundControl) == nullptr))) {
        frame->status = UiTaskStatus::Cancelled;
        const UiTaskId id = frame->taskId;
        uiTasks_.push_back(std::move(frame));
        return id;
    }

    const auto typeIdOpt = targetHeap->objectTypeId(targetObject);
    const auto* typeDesc = typeIdOpt ? program.resolveType(*typeIdOpt) : nullptr;
    if (!typeDesc) {
        frame->status = UiTaskStatus::Faulted;
        const UiTaskId id = frame->taskId;
        uiTasks_.push_back(std::move(frame));
        return id;
    }

    const std::string seqStr(sequenceName);
    const std::string stateName = "$sequence_state_" + seqStr;
    const std::string completedName = "$sequence_completed_" + seqStr;
    const std::string resultName = "$sequence_result_" + seqStr;
    const std::string targetName = "$sequence_target_" + seqStr;
    const std::string timerName = "$sequence_timer_" + seqStr;
    const std::string paramPrefix = "$sequence_parameter_" + seqStr + "_";
    const std::string localPrefix = "$sequence_local_" + seqStr + "_";
    const std::string foreachPrefix = "$sequence_foreach_" + seqStr + "_";

    std::vector<std::pair<std::size_t, Value>> backupFields;
    for (const auto& field : typeDesc->fields) {
        if (!field.synthetic) continue;
        const bool belongsToSequence =
            field.name == stateName ||
            field.name == completedName ||
            field.name == resultName ||
            field.name == targetName ||
            field.name == timerName ||
            field.name.rfind(paramPrefix, 0) == 0 ||
            field.name.rfind(localPrefix, 0) == 0 ||
            field.name.rfind(foreachPrefix, 0) == 0;
        if (!belongsToSequence) continue;

        if (field.name == stateName) frame->stateFieldIndex = field.index;
        if (field.name == completedName) frame->completedFieldIndex = field.index;
        if (field.name == resultName) frame->resultFieldIndex = field.index;

        if (const Value* cur = targetHeap->fieldGetFast(targetObject, field.index)) {
            backupFields.emplace_back(field.index, *cur);
        }
    }

    (void)uiInterpreter;
    SymbolId entrySymbolId = 0;
    std::size_t expectedArgsAfterThis = 1;
    const std::string stepMethodName = "$sequence_" + seqStr + "_1";
    for (const auto& method : typeDesc->methods) {
        if (method.name == seqStr) {
            entrySymbolId = method.id;
            if (!method.parameters.empty()) {
                expectedArgsAfterThis = method.parameters.size() - 1u;
            }
        } else if (method.name == stepMethodName) {
            frame->sequenceStepSymbolId = method.id;
        }
    }
    if (frame->sequenceStepSymbolId == 0) {
        const std::string qualStep = realscript::semantic::canonicalTypeName(*typeDesc) + "." + stepMethodName;
        if (const auto foundStep = program.findFunction(qualStep)) {
            frame->sequenceStepSymbolId = *foundStep;
        }
    }
    if (entrySymbolId == 0) {
        const std::string qualEntry = realscript::semantic::canonicalTypeName(*typeDesc) + "." + seqStr;
        if (const auto foundEntry = program.findFunction(qualEntry)) {
            entrySymbolId = *foundEntry;
        }
    }
    if (entrySymbolId == 0) {
        frame->status = UiTaskStatus::Faulted;
        const UiTaskId id = frame->taskId;
        uiTasks_.push_back(std::move(frame));
        return id;
    }

    std::vector<Value> callArgs;
    callArgs.reserve(arguments.size() + 2u);
    callArgs.push_back(Value{targetObject});
    frame->sequenceTargetId = static_cast<std::int64_t>(frame->taskId);
    const bool omittedTargetParam = (arguments.size() + 1u == expectedArgsAfterThis) ||
        arguments.empty() ||
        (!std::holds_alternative<realscript::runtime::LongValue>(arguments[0]) &&
         !std::holds_alternative<std::int64_t>(arguments[0]));
    if (omittedTargetParam) {
        callArgs.push_back(Value{realscript::runtime::LongValue{frame->sequenceTargetId}});
        for (auto& arg : arguments) {
            callArgs.push_back(std::move(arg));
        }
    } else {
        if (const auto* lv = std::get_if<realscript::runtime::LongValue>(&arguments[0])) {
            frame->sequenceTargetId = lv->value;
        } else if (const auto* iv = std::get_if<std::int64_t>(&arguments[0])) {
            frame->sequenceTargetId = *iv;
            arguments[0] = Value{realscript::runtime::LongValue{*iv}};
        }
        for (auto& arg : arguments) {
            callArgs.push_back(std::move(arg));
        }
    }

    frame->sequenceEntrySymbolId = entrySymbolId;
    frame->sequenceEntryArgs = std::move(callArgs);
    frame->isolatedSequenceFields.reserve(backupFields.size());
    for (const auto& [idx, oldVal] : backupFields) {
        if (idx == frame->stateFieldIndex) {
            frame->isolatedSequenceFields.emplace_back(idx, Value{std::int64_t{0}});
        } else if (idx == frame->completedFieldIndex) {
            frame->isolatedSequenceFields.emplace_back(idx, Value{false});
        } else {
            frame->isolatedSequenceFields.emplace_back(idx, oldVal);
        }
    }
    frame->status = UiTaskStatus::Suspended;

    const UiTaskId id = frame->taskId;
    uiTasks_.push_back(std::move(frame));
    return id;
}

UiTaskId ExecutionDomainContext::spawnCallbackTask(
    UiAsyncStepCallback stepCallback,
    NativeHandle boundControl,
    UiTaskId awaitPrerequisite) {
    auto frame = std::make_unique<UiAsyncTaskFrame>();
    frame->taskId = nextUiTaskId_++;
    frame->prerequisiteTaskId = awaitPrerequisite;
    frame->boundControl = boundControl;
    frame->isSequenceTask = false;
    frame->stepCallback = std::move(stepCallback);
    if (boundControl.valid() &&
        (cancelledControlSlots_.count(boundControl.slot) != 0 ||
         (nativeHandles_ && nativeHandles_->resolveRawFast(boundControl) == nullptr))) {
        frame->status = UiTaskStatus::Cancelled;
    } else {
        frame->status = UiTaskStatus::Suspended;
    }
    const UiTaskId id = frame->taskId;
    uiTasks_.push_back(std::move(frame));
    return id;
}

bool ExecutionDomainContext::stepUiTask(UiTaskId taskId, const Interpreter* uiInterpreter) {
    UiAsyncTaskFrame* frame = nullptr;
    for (const auto& item : uiTasks_) {
        if (item->taskId == taskId) {
            frame = item.get();
            break;
        }
    }
    if (!frame) return false;
    if (frame->status == UiTaskStatus::Completed ||
        frame->status == UiTaskStatus::Cancelled ||
        frame->status == UiTaskStatus::Faulted) {
        return false;
    }

    if (frame->boundControl.valid()) {
        if (cancelledControlSlots_.count(frame->boundControl.slot) != 0 ||
            (nativeHandles_ && nativeHandles_->resolveRawFast(frame->boundControl) == nullptr)) {
            frame->status = UiTaskStatus::Cancelled;
            return false;
        }
    }

    if (frame->prerequisiteTaskId != 0) {
        const auto prereqStatus = uiTaskStatus(frame->prerequisiteTaskId);
        if (prereqStatus == UiTaskStatus::Cancelled || prereqStatus == UiTaskStatus::Faulted) {
            frame->status = UiTaskStatus::Cancelled;
            return false;
        }
        if (prereqStatus != UiTaskStatus::Completed) {
            return true;
        }
    }

    if (!frame->isSequenceTask) {
        if (!frame->stepCallback) {
            frame->status = UiTaskStatus::Completed;
            return false;
        }
        const bool finished = frame->stepCallback(frame->stepIndex++, frame->result);
        if (frame->boundControl.valid() &&
            (cancelledControlSlots_.count(frame->boundControl.slot) != 0 ||
             (nativeHandles_ && nativeHandles_->resolveRawFast(frame->boundControl) == nullptr))) {
            frame->status = UiTaskStatus::Cancelled;
            return false;
        }
        if (finished) {
            frame->status = UiTaskStatus::Completed;
            return false;
        }
        frame->status = UiTaskStatus::Suspended;
        return true;
    }

    if (!uiInterpreter) return false;
    const ObjectRef targetObject = frame->targetWeakRef.ref;
    ManagedHeap* targetHeap = heapForObject(targetObject);
    if (!targetHeap || !targetHeap->isAlive(targetObject)) {
        frame->status = UiTaskStatus::Cancelled;
        return false;
    }

    std::vector<std::pair<std::size_t, Value>> backupFields;
    backupFields.reserve(frame->isolatedSequenceFields.size());
    for (const auto& [idx, taskVal] : frame->isolatedSequenceFields) {
        if (const Value* cur = targetHeap->fieldGetFast(targetObject, idx)) {
            backupFields.emplace_back(idx, *cur);
        }
        if (idx == frame->stateFieldIndex &&
            std::holds_alternative<std::int64_t>(taskVal) &&
            std::get<std::int64_t>(taskVal) < 0) {
            (void)targetHeap->fieldSet(
                targetObject, idx, Value{-std::get<std::int64_t>(taskVal)});
        } else {
            (void)targetHeap->fieldSet(targetObject, idx, taskVal);
        }
    }

    ExecutionResult exec{};
    if (frame->stepIndex == 0 && frame->sequenceEntrySymbolId != 0) {
        if (!frame->sequenceEntryArgs.empty()) {
            frame->sequenceEntryArgs[0] = Value{targetObject};
        }
        exec = invokeMethod(
            DomainKind::UiPresentation,
            uiInterpreter,
            frame->sequenceEntrySymbolId,
            frame->sequenceEntryArgs.data(),
            frame->sequenceEntryArgs.size());
    } else {
        const Value arg0{targetObject};
        exec = invokeMethod(
            DomainKind::UiPresentation,
            uiInterpreter,
            frame->sequenceStepSymbolId,
            &arg0,
            1u);
    }
    ++frame->stepIndex;

    for (auto& [idx, taskVal] : frame->isolatedSequenceFields) {
        if (const Value* updated = targetHeap->fieldGetFast(targetObject, idx)) {
            taskVal = *updated;
        }
    }
    for (const auto& [idx, oldVal] : backupFields) {
        (void)targetHeap->fieldSet(targetObject, idx, oldVal);
    }

    if (!exec.succeeded) {
        frame->status = UiTaskStatus::Faulted;
        frame->error = exec.error;
        return false;
    }

    bool completed = false;
    std::int64_t stateVal = 0;
    for (const auto& [idx, val] : frame->isolatedSequenceFields) {
        if (idx == frame->completedFieldIndex && std::holds_alternative<bool>(val)) {
            completed = std::get<bool>(val);
        } else if (idx == frame->stateFieldIndex && std::holds_alternative<std::int64_t>(val)) {
            stateVal = std::get<std::int64_t>(val);
        } else if (idx == frame->resultFieldIndex) {
            frame->result = val;
        }
    }
    if (completed || stateVal == 0) {
        frame->status = UiTaskStatus::Completed;
        return false;
    }
    frame->status = UiTaskStatus::Suspended;
    return true;
}

std::size_t ExecutionDomainContext::stepAllUiTasks(const Interpreter* uiInterpreter) {
    std::size_t steppedCount = 0;
    const std::size_t count = uiTasks_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const UiTaskId id = uiTasks_[i]->taskId;
        const auto beforeStatus = uiTasks_[i]->status;
        if (beforeStatus == UiTaskStatus::Suspended || beforeStatus == UiTaskStatus::Pending) {
            (void)stepUiTask(id, uiInterpreter);
            const auto afterStatus = uiTasks_[i]->status;
            if (afterStatus != UiTaskStatus::Cancelled && afterStatus != UiTaskStatus::Faulted) {
                ++steppedCount;
            }
        }
    }
    return steppedCount;
}

bool ExecutionDomainContext::cancelUiTask(UiTaskId taskId) noexcept {
    for (const auto& item : uiTasks_) {
        if (item->taskId == taskId) {
            if (item->status != UiTaskStatus::Completed) {
                item->status = UiTaskStatus::Cancelled;
                return true;
            }
            return false;
        }
    }
    return false;
}

std::size_t ExecutionDomainContext::cancelUiTasksForControl(NativeHandle boundControl) noexcept {
    if (!boundControl.valid()) return 0;
    cancelledControlSlots_.insert(boundControl.slot);
    std::size_t cancelledCount = 0;
    for (const auto& item : uiTasks_) {
        if (item->boundControl.valid() &&
            item->boundControl.slot == boundControl.slot &&
            item->boundControl.generation == boundControl.generation &&
            item->status != UiTaskStatus::Completed &&
            item->status != UiTaskStatus::Cancelled) {
            item->status = UiTaskStatus::Cancelled;
            ++cancelledCount;
        }
    }
    return cancelledCount;
}

UiTaskStatus ExecutionDomainContext::uiTaskStatus(UiTaskId taskId) const noexcept {
    for (const auto& item : uiTasks_) {
        if (item->taskId == taskId) {
            return item->status;
        }
    }
    return UiTaskStatus::Faulted;
}

std::optional<Value> ExecutionDomainContext::uiTaskResult(UiTaskId taskId) const {
    for (const auto& item : uiTasks_) {
        if (item->taskId == taskId && item->status == UiTaskStatus::Completed) {
            return item->result;
        }
    }
    return std::nullopt;
}

std::size_t ExecutionDomainContext::activeUiTaskCount() const noexcept {
    std::size_t active = 0;
    for (const auto& item : uiTasks_) {
        if (item->status == UiTaskStatus::Pending || item->status == UiTaskStatus::Suspended) {
            ++active;
        }
    }
    return active;
}

std::size_t ExecutionDomainContext::stepUiIdleGc(
    const ShadowStack& shadowStack,
    double maxMilliseconds) {
    if (!uiHeap_) return 0;
    const double clampedMs = std::max(0.0, maxMilliseconds);
    const std::uint64_t budgetUs = static_cast<std::uint64_t>(clampedMs * 1000.0);
    std::size_t totalWork = 0;
    if (uiHeap_->nurserySize() > 0u) {
        totalWork += uiHeap_->collectYoung(shadowStack, 64u);
    }
    if (budgetUs > 0u || uiHeap_->collectionRequested()) {
        const auto stats = uiHeap_->collectIncrementalWithBudgetUs(
            shadowStack,
            budgetUs > 0u ? budgetUs : 1u,
            32u,
            false);
        totalWork += stats.totalWork();
    }
    return totalWork;
}

std::size_t ExecutionDomainContext::stepUiIdleGc(double maxMilliseconds) {
    ShadowStack emptyStack;
    return stepUiIdleGc(emptyStack, maxMilliseconds);
}

void ExecutionDomainContext::setDesktopJitInvoker(DesktopJitInvokerFn invoker) {
    if (invoker) {
        auto holder = std::make_shared<const DesktopJitInvokerFn>(std::move(invoker));
        std::atomic_store_explicit(&desktopJitInvoker_, std::move(holder), std::memory_order_release);
        hasDesktopJitInvoker_.store(true, std::memory_order_release);
    } else {
        clearDesktopJitInvoker();
    }
}

void ExecutionDomainContext::clearDesktopJitInvoker() noexcept {
    hasDesktopJitInvoker_.store(false, std::memory_order_release);
    std::atomic_store_explicit(
        &desktopJitInvoker_,
        std::shared_ptr<const DesktopJitInvokerFn>{},
        std::memory_order_release);
}

bool ExecutionDomainContext::hasDesktopJitInvoker() const noexcept {
    return hasDesktopJitInvoker_.load(std::memory_order_acquire);
}

bool ExecutionDomainContext::tryInvokeDesktopJit(
    DomainKind domain,
    SymbolId functionId,
    const Value* arguments,
    std::size_t argumentCount,
    ExecutionResult& outResult) const {
    if (functionId == 0 || !hasDesktopJitInvoker_.load(std::memory_order_acquire)) {
        return false;
    }
    const auto invokerHolder = std::atomic_load_explicit(&desktopJitInvoker_, std::memory_order_acquire);
    if (!invokerHolder || !(*invokerHolder)) {
        return false;
    }
    if (domain == DomainKind::UiPresentation &&
        hasSimCommands_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (simCommandFunctions_.find(functionId) != simCommandFunctions_.end()) {
            return false;
        }
    }

    bool outSucceeded = false;
    Value outVal{};
    RuntimeError outErr{};
    if ((*invokerHolder)(domain, functionId, arguments, argumentCount, outSucceeded, outVal, outErr)) {
        desktopJitInvokeHits_.fetch_add(1u, std::memory_order_relaxed);
        outResult = ExecutionResult{};
        outResult.succeeded = outSucceeded;
        outResult.value = std::move(outVal);
        outResult.error = std::move(outErr);
        return true;
    }

    desktopJitFallbackHits_.fetch_add(1u, std::memory_order_relaxed);
    return false;
}

ExecutionResult ExecutionDomainContext::invokeMethod(
    DomainKind domain,
    const Interpreter* interpreter,
    SymbolId functionId,
    const Value* arguments,
    std::size_t argumentCount,
    realscript::runtime::ExecutionOptions options) const {
    if (functionId == 0) {
        ExecutionResult fail{};
        fail.error.code = ErrorCode::FunctionNotFound;
        fail.error.message = "cannot invoke functionId 0";
        return fail;
    }

    if (domain == DomainKind::UiPresentation && isSimCommandFunction(functionId)) {
        bool intercepted = false;
        Value interceptedVal{};
        RuntimeError interceptErr{};
        if (uiPolicy_.interceptCall(
                functionId,
                ValueSpan{arguments, argumentCount},
                intercepted,
                interceptedVal,
                interceptErr) &&
            intercepted) {
            ExecutionResult res{};
            res.succeeded = true;
            res.value = std::move(interceptedVal);
            return res;
        }
    }

    ExecutionResult jitExec{};
    if (tryInvokeDesktopJit(domain, functionId, arguments, argumentCount, jitExec)) {
        return jitExec;
    }

    if (interpreter) {
        if (options.limits.instructionBudget == 1'000'000u) {
            options.limits.instructionBudget = 128'000'000u;
            options.limits.gcWorkBudget = 0u;
        }
        std::vector<Value> argVec;
        if (arguments && argumentCount > 0u) {
            argVec.assign(arguments, arguments + argumentCount);
        }
        return interpreter->invoke(functionId, argVec, std::move(options));
    }

    ExecutionResult missing{};
    missing.error.code = ErrorCode::FunctionNotFound;
    missing.error.message = "no desktop JIT or interpreter available for invokeMethod";
    return missing;
}

} // namespace aero
