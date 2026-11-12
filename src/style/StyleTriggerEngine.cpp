#include "aero/style/StyleTriggerEngine.hpp"

#include <algorithm>
#include <mutex>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

#include "aero/controls/Controls.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/aot_cpp/AotRuntime.h"

namespace aero {

namespace {

[[nodiscard]] std::string_view ShortTypeNameView(std::string_view raw) noexcept {
    if (const auto colon = raw.rfind(':'); colon != std::string_view::npos) {
        raw = raw.substr(colon + 1u);
    }
    if (const auto dot = raw.rfind('.'); dot != std::string_view::npos) {
        raw = raw.substr(dot + 1u);
    }
    return raw;
}

[[nodiscard]] bool MatchesXamlAncestorType(
    const UIElement* anc,
    std::string_view wantedType) noexcept {
    if (!anc || wantedType.empty()) {
        return false;
    }
    if (!anc->DeclaredTypeName().empty() && anc->DeclaredTypeName() == wantedType) {
        return true;
    }
    for (const TypeClass* tc = anc->GetTypeClass(); tc != nullptr; tc = tc->baseClass) {
        if (tc->typeName && ShortTypeNameView(tc->typeName) == wantedType) {
            return true;
        }
    }
    if (wantedType == "ToggleButton") {
        return anc->IsA<CheckBox>() || anc->IsA<RadioButton>();
    }
    if (wantedType == "Page") {
        return anc->IsA<UserControl>() || anc->IsA<Window>();
    }
    if (wantedType == "ContentControl" ||
        wantedType == "ListBoxItem" ||
        wantedType == "ComboBoxItem" ||
        wantedType == "ListViewItem" ||
        wantedType == "TreeViewItem") {
        return anc->IsA<Border>() || anc->IsA<ContentPresenter>() || anc->IsA<UserControl>();
    }
    if (wantedType == "RepeatButton" || wantedType == "Hyperlink" || wantedType == "MenuItem") {
        return anc->IsA<Button>();
    }
    if (wantedType == "ListView") {
        return anc->IsA<ListBox>();
    }
    if (wantedType == "TreeView") {
        return anc->IsA<ItemsControl>();
    }
    if (wantedType == "FrameworkElement" || wantedType == "Control") {
        return true;
    }
    return false;
}

[[nodiscard]] bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0u; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool TryParseStackFloat(std::string_view s, float& outVal) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty() || s.size() >= 60u) return false;
    char buf[64]{};
    for (std::size_t i = 0u; i < s.size(); ++i) buf[i] = s[i];
    char* end = nullptr;
    const float v = std::strtof(buf, &end);
    if (end != buf + s.size()) return false;
    outVal = v;
    return true;
}

[[nodiscard]] bool TryExtractIntOrEnum(const UnifiedValue& v, std::int64_t& outInt) noexcept {
    if (const auto* iv = std::get_if<std::int64_t>(&v)) {
        outInt = *iv;
        return true;
    }
    EnumValue ev{};
    if (v.tryGetEnum(ev)) {
        outInt = ev.value;
        return true;
    }
    return false;
}

[[nodiscard]] bool UnifiedValuesEqual(const UnifiedValue& a, const UnifiedValue& b) noexcept {
    if (const auto* ab = std::get_if<bool>(&a)) {
        if (const auto* bb = std::get_if<bool>(&b)) return *ab == *bb;
        std::int64_t bi = 0;
        if (TryExtractIntOrEnum(b, bi)) return *ab == (bi != 0);
    }
    std::int64_t ai = 0;
    if (TryExtractIntOrEnum(a, ai)) {
        std::int64_t bi = 0;
        if (TryExtractIntOrEnum(b, bi)) return ai == bi;
        if (const auto* bb = std::get_if<bool>(&b)) return (ai != 0) == *bb;
    }
    float af = 0.0f;
    float bf = 0.0f;
    if (TryGetFloat(a, af) && TryGetFloat(b, bf) &&
        !std::holds_alternative<bool>(a) && !std::holds_alternative<bool>(b)) {
        return std::fabs(af - bf) <= 1e-4f;
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
    return a.index() == b.index() && std::holds_alternative<std::monostate>(a);
}

[[nodiscard]] bool TriggerValuesEqual(
    const UnifiedValue& actualVal,
    const UnifiedValue& expectedVal,
    bool expectNull,
    const ExecutionDomainContext* domains,
    const ProgramImage* program = nullptr) noexcept {
    const bool actualIsNull =
        std::holds_alternative<std::monostate>(actualVal) ||
        std::holds_alternative<NullObject>(actualVal) ||
        (std::holds_alternative<ObjectRef>(actualVal) && !std::get<ObjectRef>(actualVal).valid());

    if (expectNull) {
        return actualIsNull;
    }
    if (actualIsNull) {
        return false;
    }
    if (UnifiedValuesEqual(actualVal, expectedVal)) {
        return true;
    }

    // Zero-allocation cross-domain RealScript String ObjectRef comparison via string_view
    if (const auto* objRef = std::get_if<ObjectRef>(&actualVal)) {
        if (objRef->kind == ObjectKind::String && domains) {
            if (const auto actualSv = domains->stringViewAcrossDomains(*objRef)) {
                if (const auto* expSv = std::get_if<StringValue>(&expectedVal)) {
                    return *actualSv == expSv->view();
                }
            }
        }
        return false;
    }

    // Untyped XAML string literal comparison against bool, int64_t, float, or enum
    if (const auto* expStr = std::get_if<StringValue>(&expectedVal)) {
        const std::string_view sv = expStr->view();
        if (const auto* ab = std::get_if<bool>(&actualVal)) {
            if (EqualsAsciiIgnoreCase(sv, "true") || sv == "1") return *ab;
            if (EqualsAsciiIgnoreCase(sv, "false") || sv == "0") return !(*ab);
            return false;
        }
        std::int64_t ai = 0;
        if (TryExtractIntOrEnum(actualVal, ai)) {
            std::int64_t parsedInt = 0;
            const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsedInt);
            if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
                return ai == parsedInt;
            }
            if (EqualsAsciiIgnoreCase(sv, "Visible")) return ai == 0;
            if (EqualsAsciiIgnoreCase(sv, "Hidden")) return ai == 1;
            if (EqualsAsciiIgnoreCase(sv, "Collapsed")) return ai == 2;
            EnumValue ev{};
            if (actualVal.tryGetEnum(ev) && ev.typeId != 0 && program) {
                std::int64_t enumInt = 0;
                if (TryResolveEnumMemberByName(program, ev.typeId, sv, enumInt)) {
                    return ai == enumInt;
                }
            }
        }
        float af = 0.0f;
        float parsedFloat = 0.0f;
        if (TryGetFloat(actualVal, af) && TryParseStackFloat(sv, parsedFloat)) {
            return std::fabs(af - parsedFloat) <= 1e-4f;
        }
    }

    return false;
}

[[nodiscard]] bool TryExtractComparableFloat(
    const UnifiedValue& v,
    const ExecutionDomainContext* domains,
    float& outVal) noexcept {
    if (std::holds_alternative<bool>(v)) {
        outVal = std::get<bool>(v) ? 1.0f : 0.0f;
        return true;
    }
    EnumValue ev{};
    if (v.tryGetEnum(ev)) {
        outVal = static_cast<float>(ev.value);
        return true;
    }
    if (TryGetFloat(v, outVal)) {
        return true;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return TryParseStackFloat(sv->view(), outVal);
    }
    if (const auto* oref = std::get_if<ObjectRef>(&v)) {
        if (oref->kind == ObjectKind::String && domains) {
            if (const auto sv = domains->stringViewAcrossDomains(*oref)) {
                return TryParseStackFloat(*sv, outVal);
            }
        }
    }
    return false;
}

[[nodiscard]] bool TryExtractComparableString(
    const UnifiedValue& v,
    const ExecutionDomainContext* domains,
    std::string_view& outSv) noexcept {
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        outSv = sv->view();
        return true;
    }
    if (const auto* oref = std::get_if<ObjectRef>(&v)) {
        if (oref->kind == ObjectKind::String && domains) {
            if (const auto sv = domains->stringViewAcrossDomains(*oref)) {
                outSv = *sv;
                return true;
            }
        }
    }
    return false;
}

// Zero-allocation value matcher supporting Equal, NotEqual, LessThan, LessThanOrEqual,
// GreaterThan, and GreaterThanOrEqual across typed scalars, enums, and strings!
[[nodiscard]] bool TriggerConditionValueMatches(
    const UnifiedValue& actualVal,
    const UnifiedValue& expectedVal,
    bool expectNull,
    TriggerComparisonOp comparison,
    const ExecutionDomainContext* domains,
    const ProgramImage* program = nullptr) noexcept {
    const bool isEq = TriggerValuesEqual(actualVal, expectedVal, expectNull, domains, program);
    if (comparison == TriggerComparisonOp::Equal) {
        return isEq;
    }
    if (comparison == TriggerComparisonOp::NotEqual) {
        return !isEq;
    }

    const bool actualIsNull =
        std::holds_alternative<std::monostate>(actualVal) ||
        std::holds_alternative<NullObject>(actualVal) ||
        (std::holds_alternative<ObjectRef>(actualVal) && !std::get<ObjectRef>(actualVal).valid());
    if (actualIsNull || expectNull) {
        return false;
    }

    float lhsF = 0.0f;
    float rhsF = 0.0f;
    if (TryExtractComparableFloat(actualVal, domains, lhsF) &&
        TryExtractComparableFloat(expectedVal, domains, rhsF)) {
        switch (comparison) {
            case TriggerComparisonOp::LessThan:
                return lhsF < rhsF - 1e-6f;
            case TriggerComparisonOp::LessThanOrEqual:
                return lhsF <= rhsF + 1e-5f;
            case TriggerComparisonOp::GreaterThan:
                return lhsF > rhsF + 1e-6f;
            case TriggerComparisonOp::GreaterThanOrEqual:
                return lhsF + 1e-5f >= rhsF;
            default:
                return false;
        }
    }

    std::string_view lhsSv{};
    std::string_view rhsSv{};
    if (TryExtractComparableString(actualVal, domains, lhsSv) &&
        TryExtractComparableString(expectedVal, domains, rhsSv)) {
        switch (comparison) {
            case TriggerComparisonOp::LessThan:
                return lhsSv < rhsSv;
            case TriggerComparisonOp::LessThanOrEqual:
                return lhsSv <= rhsSv;
            case TriggerComparisonOp::GreaterThan:
                return lhsSv > rhsSv;
            case TriggerComparisonOp::GreaterThanOrEqual:
                return lhsSv >= rhsSv;
            default:
                return false;
        }
    }

    return false;
}

[[nodiscard]] const UIElement* NextContextParent(const UIElement* cur) noexcept {
    if (!cur) return nullptr;
    if (cur->Parent()) return cur->Parent();
    return cur->TemplatedParent();
}

[[nodiscard]] ObjectRef ResolveEffectiveDataContext(const UIElement* elem) noexcept {
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

[[nodiscard]] ObjectRef ResolveRootScriptWrapper(const UIElement* elem) noexcept {
    ObjectRef topWrapper{};
    for (const UIElement* cur = elem; cur != nullptr; cur = NextContextParent(cur)) {
        if (cur->BoundScriptWrapper().valid()) {
            topWrapper = cur->BoundScriptWrapper();
        }
    }
    return topWrapper;
}

void ResolveHopTargetType(
    const ProgramImage* program,
    SymbolId ownerTypeId,
    AxbCompiledHop& hop) noexcept {
    if (!program || ownerTypeId == 0 || hop.targetTypeId != 0) {
        return;
    }
    std::string rawTypeName;
    SymbolId curTypeId = ownerTypeId;
    for (int depth = 0; depth < 16 && curTypeId != 0; ++depth) {
        const auto* ts = program->resolveType(curTypeId);
        if (!ts) break;
        for (const auto& p : ts->properties) {
            if (p.name == hop.memberName) {
                rawTypeName = p.typeName;
                break;
            }
        }
        if (!rawTypeName.empty()) break;
        for (const auto& f : ts->fields) {
            if (f.name == hop.memberName) {
                rawTypeName = f.typeName;
                break;
            }
        }
        if (!rawTypeName.empty()) break;
        curTypeId = ts->baseTypeId;
    }
    if (rawTypeName.empty()) {
        return;
    }
    std::string lookupName = rawTypeName;
    if (hop.collectionIndex >= 0) {
        const auto lt = lookupName.find('<');
        const auto gt = lookupName.rfind('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt + 1u) {
            lookupName = lookupName.substr(lt + 1u, gt - lt - 1u);
        } else if (lookupName.size() > 2u && lookupName.compare(lookupName.size() - 2u, 2u, "[]") == 0) {
            lookupName.erase(lookupName.size() - 2u);
        }
    }
    std::string normalized;
    normalized.reserve(lookupName.size() + 2u);
    for (std::size_t i = 0; i < lookupName.size(); ++i) {
        if (lookupName[i] == '.' && (i + 1u >= lookupName.size() || lookupName[i + 1u] != '.')) {
            normalized += "::";
        } else if (!std::isspace(static_cast<unsigned char>(lookupName[i]))) {
            normalized.push_back(lookupName[i]);
        }
    }
    std::string_view shortTarget = ShortTypeNameView(normalized);
    const rs_sem::TypeSymbol* bestMatch = nullptr;
    for (const auto& mod : program->modules()) {
        for (const auto& cand : mod.types) {
            const std::string canon = rs_sem::canonicalTypeName(cand);
            if (canon == normalized || cand.name == normalized) {
                bestMatch = &cand;
                break;
            }
            if (!bestMatch && cand.name == shortTarget) {
                bestMatch = &cand;
            }
        }
        if (bestMatch && (rs_sem::canonicalTypeName(*bestMatch) == normalized || bestMatch->name == normalized)) {
            break;
        }
    }
    if (bestMatch) {
        hop.targetTypeId = bestMatch->id;
        hop.targetIsEnum = (bestMatch->kind == rs_sem::TypeKind::Enum);
    }
}

[[nodiscard]] bool ReadSingleHopOnObject(
    ExecutionDomainContext* domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter,
    ObjectRef curObj,
    AxbCompiledHop& hop,
    UnifiedValue& outVal) {
    if (!domains || !curObj.valid()) {
        return false;
    }
    auto* heap = domains->heapForObject(curObj);
    if (!heap || !heap->isAlive(curObj)) {
        return false;
    }
    const SymbolId runtimeTypeId = heap->objectTypeId(curObj).value_or(0);
    if (program && runtimeTypeId != 0) {
        if (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid()) {
            if (auto resolved = domains->resolvePropertySlot(*program, runtimeTypeId, hop.memberName)) {
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
        }
        ResolveHopTargetType(program, runtimeTypeId, hop);
    }

    const PropertySlotHandle slot = hop.ToSlotHandle();
    UnifiedValue hopVal{};
    if (slot.valid()) {
        if (!domains->readPropertySlotFast(DomainKind::Ui, curObj, slot, hopVal, nullptr, uiInterpreter)) {
            return false;
        }
    } else if (hop.fieldIndex != PropertySlotHandle::InvalidIndex) {
        if (!domains->readObjectField(DomainKind::Ui, curObj, hop.fieldIndex, hopVal, nullptr)) {
            return false;
        }
    } else {
        return false;
    }

    if (hop.collectionIndex >= 0) {
        const auto* collRef = std::get_if<ObjectRef>(&hopVal);
        if (!collRef || !collRef->valid()) {
            outVal = NullObject{};
            return true;
        }
        auto* collHeap = domains->heapForObject(*collRef);
        if (!collHeap || !collHeap->isAlive(*collRef)) {
            return false;
        }
        const ObjectRef backingArr = ObservableListHelper::ResolveBackingArray(*collHeap, *collRef);
        if (!backingArr.valid()) {
            outVal = NullObject{};
            return true;
        }
        auto* arrHeap = domains->heapForObject(backingArr);
        if (!arrHeap || !arrHeap->isAlive(backingArr)) {
            return false;
        }
        const std::size_t idx = static_cast<std::size_t>(hop.collectionIndex);
        if (idx >= arrHeap->arrayLength(backingArr).value_or(0u)) {
            outVal = NullObject{};
            return true;
        }
        if (auto elemOpt = arrHeap->arrayGet(backingArr, idx)) {
            outVal = *elemOpt;
        } else {
            outVal = NullObject{};
        }
        return true;
    }

    outVal = hopVal;
    return true;
}

[[nodiscard]] SymbolId FindMethodInHierarchy(
    const ProgramImage* program,
    SymbolId startTypeId,
    std::string_view leafName,
    bool& outIsSimCmd) {
    outIsSimCmd = false;
    if (!program || startTypeId == 0 || leafName.empty()) {
        return 0;
    }
    const std::string exact(leafName);
    const std::string withCommand = exact + "Command";
    const std::string withOn = "On" + exact;
    std::string strippedBase;
    std::string onStrippedBase;
    if (exact.size() > 7u && exact.compare(exact.size() - 7u, 7u, "Command") == 0) {
        strippedBase = exact.substr(0u, exact.size() - 7u);
        onStrippedBase = "On" + strippedBase;
    }

    SymbolId foundSym = 0;
    SymbolId curTypeId = startTypeId;
    for (int depth = 0; depth < 16 && curTypeId != 0 && foundSym == 0; ++depth) {
        const auto* ts = program->resolveType(curTypeId);
        if (!ts) break;
        for (const auto& m : ts->methods) {
            if (m.name == exact) {
                foundSym = m.id;
                break;
            }
        }
        if (foundSym == 0) {
            for (const auto& m : ts->methods) {
                if (m.name == withCommand || m.name == withOn ||
                    (!strippedBase.empty() && (m.name == strippedBase || m.name == onStrippedBase))) {
                    foundSym = m.id;
                    break;
                }
            }
        }
        curTypeId = ts->baseTypeId;
    }

    if (foundSym != 0) {
        if (const auto* loc = program->resolveFunction(foundSym)) {
            if (loc->function && loc->function->simCommand) {
                outIsSimCmd = true;
            }
        }
    }
    return foundSym;
}

[[nodiscard]] bool DoesTypeOwnOrInheritMethod(
    const ProgramImage* program,
    SymbolId startTypeId,
    SymbolId methodSym) noexcept {
    if (!program || startTypeId == 0 || methodSym == 0) {
        return false;
    }
    SymbolId curTypeId = startTypeId;
    for (int depth = 0; depth < 16 && curTypeId != 0; ++depth) {
        const auto* ts = program->resolveType(curTypeId);
        if (!ts) break;
        for (const auto& m : ts->methods) {
            if (m.id == methodSym) {
                return true;
            }
        }
        curTypeId = ts->baseTypeId;
    }
    return false;
}

[[nodiscard]] std::int16_t FindStateIndexByName(
    const AxbVisualStateGroup& group,
    std::string_view stateName) noexcept {
    for (std::size_t i = 0u; i < group.states.size(); ++i) {
        if (group.states[i].name == stateName) {
            return static_cast<std::int16_t>(i);
        }
    }
    return -1;
}

} // namespace

namespace {

template <typename Fn>
void VisitStoryboardValues(const AxbStoryboard& sb, Fn&& fn) noexcept {
    for (const auto& tl : sb.timelines) {
        if (tl.fromValue.isTraceable()) fn(tl.fromValue);
        if (tl.toValue.isTraceable()) fn(tl.toValue);
        for (const auto& kf : tl.keyFrames) {
            if (kf.value.isTraceable()) fn(kf.value);
        }
    }
}

template <typename Fn>
void VisitActionValues(const AxbInteractionAction& act, Fn&& fn) noexcept {
    if (act.value.isTraceable()) fn(act.value);
    if (act.valueBinding && act.valueBinding->fallbackValue.isTraceable()) {
        fn(act.valueBinding->fallbackValue);
    }
    if (act.valueMultiBinding) {
        if (act.valueMultiBinding->fallbackValue.isTraceable()) {
            fn(act.valueMultiBinding->fallbackValue);
        }
        for (const auto& cb : act.valueMultiBinding->sources) {
            if (cb.fallbackValue.isTraceable()) fn(cb.fallbackValue);
        }
    }
    if (act.commandParameter.isTraceable()) fn(act.commandParameter);
    if (act.commandParameterBinding.fallbackValue.isTraceable()) {
        fn(act.commandParameterBinding.fallbackValue);
    }
    VisitStoryboardValues(act.inlineStoryboard, fn);
}

template <typename Fn>
void VisitStyleValues(const AxbStyleDefinition& st, Fn&& fn) noexcept {
    for (const auto& s : st.setters) {
        if (s.value.isTraceable()) fn(s.value);
        if (s.binding && s.binding->fallbackValue.isTraceable()) {
            fn(s.binding->fallbackValue);
        }
    }
    for (const auto& trig : st.triggers) {
        for (const auto& cond : trig.conditions) {
            if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
        }
        for (const auto& s : trig.setters) {
            if (s.value.isTraceable()) fn(s.value);
            if (s.binding && s.binding->fallbackValue.isTraceable()) {
                fn(s.binding->fallbackValue);
            }
        }
        for (const auto& sb : trig.enterStoryboards) VisitStoryboardValues(sb, fn);
        for (const auto& sb : trig.exitStoryboards) VisitStoryboardValues(sb, fn);
        for (const auto& act : trig.enterActions) VisitActionValues(act, fn);
        for (const auto& act : trig.exitActions) VisitActionValues(act, fn);
    }
    for (const auto& et : st.eventTriggers) {
        for (const auto& sb : et.storyboards) VisitStoryboardValues(sb, fn);
        for (const auto& act : et.actions) VisitActionValues(act, fn);
    }
    for (const auto& vsg : st.visualStateGroups) {
        for (const auto& vs : vsg.states) {
            for (const auto& s : vs.setters) {
                if (s.value.isTraceable()) fn(s.value);
                if (s.binding && s.binding->fallbackValue.isTraceable()) {
                    fn(s.binding->fallbackValue);
                }
            }
            VisitStoryboardValues(vs.storyboard, fn);
        }
    }
    if (st.inlineControlTemplate) {
        const auto& ct = *st.inlineControlTemplate;
        for (const auto& trig : ct.triggers) {
            for (const auto& cond : trig.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            for (const auto& s : trig.setters) {
                if (s.value.isTraceable()) fn(s.value);
                if (s.binding && s.binding->fallbackValue.isTraceable()) {
                    fn(s.binding->fallbackValue);
                }
            }
            for (const auto& sb : trig.enterStoryboards) VisitStoryboardValues(sb, fn);
            for (const auto& sb : trig.exitStoryboards) VisitStoryboardValues(sb, fn);
            for (const auto& act : trig.enterActions) VisitActionValues(act, fn);
            for (const auto& act : trig.exitActions) VisitActionValues(act, fn);
        }
        for (const auto& et : ct.eventTriggers) {
            for (const auto& sb : et.storyboards) VisitStoryboardValues(sb, fn);
            for (const auto& act : et.actions) VisitActionValues(act, fn);
        }
        for (const auto& vsg : ct.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& s : vs.setters) {
                    if (s.value.isTraceable()) fn(s.value);
                    if (s.binding && s.binding->fallbackValue.isTraceable()) {
                        fn(s.binding->fallbackValue);
                    }
                }
                VisitStoryboardValues(vs.storyboard, fn);
            }
        }
    }
}

} // namespace

namespace {
struct LiveStyleEngineRegistry {
    std::mutex mutex;
    std::vector<const StyleTriggerEngine*> engines;

    static LiveStyleEngineRegistry& Instance() noexcept {
        static LiveStyleEngineRegistry reg;
        return reg;
    }

    void Register(const StyleTriggerEngine* eng) {
        if (!eng) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (std::find(engines.begin(), engines.end(), eng) == engines.end()) {
            engines.push_back(eng);
        }
    }

    void Unregister(const StyleTriggerEngine* eng) noexcept {
        if (!eng) return;
        std::lock_guard<std::mutex> lock(mutex);
        engines.erase(std::remove(engines.begin(), engines.end(), eng), engines.end());
    }

    [[nodiscard]] bool Contains(const StyleTriggerEngine* eng) noexcept {
        if (!eng) return false;
        std::lock_guard<std::mutex> lock(mutex);
        return std::find(engines.begin(), engines.end(), eng) != engines.end();
    }
};
} // namespace

StyleTriggerEngine::StyleTriggerEngine(
    AnimationEngine* animationEngine,
    DualTrackBindingEngine* bindingEngine)
    : animationEngine_(animationEngine) {
    LiveStyleEngineRegistry::Instance().Register(this);
    SetBindingEngine(bindingEngine);
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkValuesInPools();
    });
}

StyleTriggerEngine::~StyleTriggerEngine() noexcept {
    LiveStyleEngineRegistry::Instance().Unregister(this);
    rs_rt::unregisterExternalValuePoolRootMarker(this);
    for (auto& inst : instances_) {
        if (inst.element && inst.element->BoundStyleEngine() == this) {
            inst.element->BindStyleEngine(nullptr);
        }
    }
}

bool StyleTriggerEngine::IsLiveInstance(const StyleTriggerEngine* engine) noexcept {
    return LiveStyleEngineRegistry::Instance().Contains(engine);
}

void StyleTriggerEngine::MarkValuesInPools() const noexcept {
    auto markFn = [](const UnifiedValue& v) noexcept {
        rs_rt::markValueInPools(v);
    };
    for (const auto& kv : keyedStyles_) {
        VisitStyleValues(kv.second, markFn);
        if (kv.second.inlineControlTemplate && kv.second.inlineControlTemplate->templatePackage) {
            kv.second.inlineControlTemplate->templatePackage->MarkValuesInPools();
        }
    }
    for (const auto& kv : implicitTypeStyles_) {
        VisitStyleValues(kv.second, markFn);
        if (kv.second.inlineControlTemplate && kv.second.inlineControlTemplate->templatePackage) {
            kv.second.inlineControlTemplate->templatePackage->MarkValuesInPools();
        }
    }
    for (const auto& kv : keyedStoryboards_) {
        VisitStoryboardValues(kv.second, markFn);
    }
    for (const auto& inst : instances_) {
        VisitStyleValues(inst.baseFlattenedStyle, markFn);
        if (inst.baseFlattenedStyle.inlineControlTemplate &&
            inst.baseFlattenedStyle.inlineControlTemplate->templatePackage) {
            inst.baseFlattenedStyle.inlineControlTemplate->templatePackage->MarkValuesInPools();
        }
        VisitStyleValues(inst.resolvedStyle, markFn);
        if (inst.resolvedStyle.inlineControlTemplate &&
            inst.resolvedStyle.inlineControlTemplate->templatePackage) {
            inst.resolvedStyle.inlineControlTemplate->templatePackage->MarkValuesInPools();
        }
        for (const auto& slot : inst.propertySnapshots) {
            if (slot.captured && slot.hadBaseValue && slot.baseValue.isTraceable()) {
                rs_rt::markValueInPools(slot.baseValue);
            }
        }
        for (const auto& es : inst.eventStates) {
            if (es.prevWatchedValue.isTraceable()) {
                rs_rt::markValueInPools(es.prevWatchedValue);
            }
        }
    }
}

void StyleTriggerEngine::Clear() {
    for (auto& inst : instances_) {
        if (inst.element && inst.element->BoundStyleEngine() == this) {
            inst.element->BindStyleEngine(nullptr);
        }
    }
    keyedStyles_.clear();
    implicitTypeStyles_.clear();
    keyedStoryboards_.clear();
    instances_.clear();
}

void StyleTriggerEngine::RegisterStyle(AxbStyleDefinition style) {
    VisitStyleValues(style, [](const UnifiedValue& v) noexcept {
        rs_rt::pinValueInPools(v);
    });
    if (style.inlineControlTemplate && style.inlineControlTemplate->templatePackage) {
        style.inlineControlTemplate->templatePackage->PinValuesInPools();
    }
    style.targetTypeName = std::string(ShortTypeNameView(style.targetTypeName));
    if (!style.key.empty()) {
        keyedStyles_[style.key] = std::move(style);
    } else if (!style.targetTypeName.empty()) {
        implicitTypeStyles_[style.targetTypeName] = std::move(style);
    }
}

void StyleTriggerEngine::RegisterStoryboard(AxbStoryboard storyboard) {
    VisitStoryboardValues(storyboard, [](const UnifiedValue& v) noexcept {
        rs_rt::pinValueInPools(v);
    });
    if (!storyboard.key.empty()) {
        keyedStoryboards_[storyboard.key] = std::move(storyboard);
    }
}

void StyleTriggerEngine::RegisterPackageStyles(const AxbPackage& package) {
    for (const auto& sb : package.storyboards) {
        RegisterStoryboard(sb);
    }
    for (const auto& st : package.styles) {
        RegisterStyle(st);
    }
    for (const auto& dt : package.dataTemplates) {
        if (dt.templatePackage) {
            RegisterPackageStyles(*dt.templatePackage);
        }
    }
    for (const auto& ct : package.controlTemplates) {
        AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);
        if (ct.templatePackage) {
            RegisterPackageStyles(*ct.templatePackage);
        }
    }
}

void StyleTriggerEngine::ImportRegistryStyles() {
    for (const auto& sb : AxbTemplateRegistry::Instance().RegisteredStoryboards()) {
        RegisterStoryboard(sb);
    }
    for (const auto& st : AxbTemplateRegistry::Instance().RegisteredStyles()) {
        RegisterStyle(st);
    }
}

const AxbStyleDefinition* StyleTriggerEngine::FindStyle(std::string_view key) const noexcept {
    const auto it = keyedStyles_.find(std::string(key));
    if (it != keyedStyles_.end()) {
        return &it->second;
    }
    return AxbTemplateRegistry::Instance().FindStyle(key);
}

const AxbStyleDefinition* StyleTriggerEngine::FindImplicitStyle(
    std::string_view shortTypeName) const noexcept {
    const std::string_view stripped = ShortTypeNameView(shortTypeName);
    const auto it = implicitTypeStyles_.find(std::string(stripped));
    if (it != implicitTypeStyles_.end()) {
        return &it->second;
    }
    return AxbTemplateRegistry::Instance().FindImplicitStyle(stripped);
}

const AxbStoryboard* StyleTriggerEngine::FindStoryboard(std::string_view key) const noexcept {
    const auto it = keyedStoryboards_.find(std::string(key));
    if (it != keyedStoryboards_.end()) {
        return &it->second;
    }
    return AxbTemplateRegistry::Instance().FindStoryboard(key);
}

AxbStyleDefinition StyleTriggerEngine::FlattenStyleChain(const AxbStyleDefinition& leaf) const {
    if (leaf.basedOnKey.empty()) {
        return leaf;
    }

    std::vector<const AxbStyleDefinition*> chain;
    chain.push_back(&leaf);
    std::string curKey = leaf.basedOnKey;
    for (int depth = 0; depth < 8 && !curKey.empty(); ++depth) {
        const AxbStyleDefinition* base = FindStyle(curKey);
        if (!base) {
            break;
        }
        if (std::find(chain.begin(), chain.end(), base) != chain.end()) {
            break; // Guard against cyclic BasedOn
        }
        chain.push_back(base);
        curKey = base->basedOnKey;
    }

    AxbStyleDefinition merged{};
    merged.key = leaf.key;
    merged.targetTypeName = leaf.targetTypeName;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const AxbStyleDefinition* s = *it;
        if (merged.targetTypeName.empty() && !s->targetTypeName.empty()) {
            merged.targetTypeName = s->targetTypeName;
        }
        if (!s->controlTemplateKey.empty()) {
            merged.controlTemplateKey = s->controlTemplateKey;
        }
        if (s->inlineControlTemplate) {
            merged.inlineControlTemplate = s->inlineControlTemplate;
        }
        for (const auto& setter : s->setters) {
            bool replaced = false;
            for (auto& existing : merged.setters) {
                if (existing.propId == setter.propId && existing.targetName == setter.targetName) {
                    existing = setter;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                merged.setters.push_back(setter);
            }
        }
        for (const auto& trig : s->triggers) {
            merged.triggers.push_back(trig);
        }
        for (const auto& et : s->eventTriggers) {
            merged.eventTriggers.push_back(et);
        }
        for (const auto& vsg : s->visualStateGroups) {
            bool replacedGroup = false;
            for (auto& eg : merged.visualStateGroups) {
                if (eg.name == vsg.name) {
                    eg = vsg;
                    replacedGroup = true;
                    break;
                }
            }
            if (!replacedGroup) {
                merged.visualStateGroups.push_back(vsg);
            }
        }
    }
    return merged;
}

bool StyleTriggerEngine::IsFastInlineAtDefault(const UIElement* elem, std::uint16_t propId) noexcept {
    if (!elem) return true;
    return elem->GetPropertySourceRank(propId) == ValueSourceRank::Default;
}

void StyleTriggerEngine::EnsurePropertySnapshotSlot(
    StyledElementInstance& inst,
    UIElement* target,
    std::uint16_t propId) {
    if (!target) {
        return;
    }
    for (const auto& s : inst.propertySnapshots) {
        if (s.resolvedTarget.Get() == target && s.propId == propId) {
            return;
        }
    }
    PropertySnapshotSlot slot{};
    slot.resolvedTarget.Reset(target, true);
    slot.propId = propId;
    inst.propertySnapshots.push_back(std::move(slot));
}

void StyleTriggerEngine::CaptureSnapshotIfNeeded(PropertySnapshotSlot& slot) {
    if (slot.captured || !slot.resolvedTarget) {
        return;
    }
    slot.captured = true;
    UIElement* target = slot.resolvedTarget.Get();
    if (IsFastInlinePropertyId(slot.propId)) {
        const ValueSourceRank curRank = target->GetPropertySourceRank(slot.propId);
        slot.hadBaseValue = (curRank != ValueSourceRank::Default);
        slot.baseRank = curRank;
        slot.baseValue = target->GetValue(slot.propId);
        if (slot.baseValue.isTraceable()) {
            rs_rt::pinValueInPools(slot.baseValue);
        }
    } else if (const auto* entry = target->FindCompactEntry(slot.propId)) {
        if (!std::holds_alternative<std::monostate>(entry->effectiveValue) &&
            entry->sourceRank != static_cast<std::uint8_t>(ValueSourceRank::Default)) {
            slot.hadBaseValue = true;
            slot.baseRank = static_cast<ValueSourceRank>(entry->sourceRank);
            slot.baseValue = entry->effectiveValue;
            if (slot.baseValue.isTraceable()) {
                rs_rt::pinValueInPools(slot.baseValue);
            }
        } else {
            slot.hadBaseValue = false;
        }
    } else {
        slot.hadBaseValue = false;
    }
}

void StyleTriggerEngine::RestoreAllCapturedSnapshots(StyledElementInstance& inst) {
    for (auto& slot : inst.propertySnapshots) {
        if (!slot.captured || !slot.resolvedTarget) {
            continue;
        }
        if (animationEngine_) {
            (void)animationEngine_->StopPropertyAnimation(slot.resolvedTarget.Get(), slot.propId, false);
        }
        if (slot.hadBaseValue) {
            (void)slot.resolvedTarget->ForceEffectiveValue(
                slot.propId,
                slot.baseValue,
                slot.baseRank);
        } else if (IsFastInlinePropertyId(slot.propId)) {
            (void)slot.resolvedTarget->ClearValue(slot.propId, ValueSourceRank::Animation);
        } else {
            (void)slot.resolvedTarget->ForceClearSparseEntry(slot.propId);
        }
        if (UIElement* tp = slot.resolvedTarget->TemplatedParent()) {
            tp->SyncTemplateBindingsForTarget(slot.resolvedTarget.Get(), slot.propId);
        }
        slot.captured = false;
        slot.touchedThisPass = false;
    }
}

bool StyleTriggerEngine::AttachStyle(UIElement* element, const AxbStyleDefinition& rawStyle) {
    if (!element) {
        return false;
    }
    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }

    element->BindStyleEngine(this);
    if (animationEngine_ && !element->BoundAnimationEngine()) {
        element->BindAnimationEngine(animationEngine_);
    }

    AxbStyleDefinition style = FlattenStyleChain(rawStyle);
    AxbStyleDefinition baseFlattened = style;

    // 1a. Mount ControlTemplate first if defined by this Style (and not already overridden locally)
    {
        std::string tplKey = style.controlTemplateKey;
        for (const auto& setter : style.setters) {
            if (setter.propId == PropId_Template && setter.targetName.empty()) {
                if (const auto* sv = std::get_if<StringValue>(&setter.value)) {
                    tplKey = sv->str();
                }
            }
        }
        if (!tplKey.empty() || style.inlineControlTemplate) {
            if (element->GetPropertySourceRank(PropId_Template) < ValueSourceRank::Local) {
                const AxbControlTemplate* ct = !tplKey.empty()
                    ? AxbTemplateRegistry::Instance().FindControlTemplate(tplKey)
                    : nullptr;
                if (!ct && style.inlineControlTemplate) {
                    ct = style.inlineControlTemplate.get();
                }
                if (ct) {
                    const ObjectRef effDc = ResolveEffectiveDataContext(element);
                    const bool prevInAttach = inAttachStyle_;
                    inAttachStyle_ = true;
                    (void)element->ApplyControlTemplate(
                        *ct,
                        domains_,
                        bindingEngine_,
                        effDc,
                        uiInterpreter_,
                        aotContext_,
                        animationEngine_);
                    inAttachStyle_ = prevInAttach;
                }
            }
        }
    }

    // 1b. Apply base <Style><Setter> values at ValueSourceRank::ThemeStyle without overwriting explicit Local values!
    for (const auto& setter : style.setters) {
        if (setter.propId == PropId_Template && setter.targetName.empty()) {
            continue;
        }
        UIElement* target = setter.targetName.empty()
            ? element
            : element->FindName(setter.targetName);
        if (!target) {
            continue;
        }
        if (setter.hasBinding && setter.binding) {
            if (bindingEngine_ && target->GetPropertySourceRank(setter.propId) < ValueSourceRank::Local) {
                const auto& bc = *setter.binding;
                IntrusivePtr<UIElement> targetPtr(target, true);
                if (!bc.elementName.empty() || !bc.relativeAncestorType.empty() || bc.relativeSelf) {
                    (void)bindingEngine_->BindElementProperty(bc, targetPtr);
                } else {
                    (void)bindingEngine_->BindChain(ResolveEffectiveDataContext(target), bc, targetPtr);
                }
            }
            continue;
        }
        if (IsFastInlinePropertyId(setter.propId)) {
            if (IsFastInlineAtDefault(target, setter.propId)) {
                (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
            }
        } else {
            (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
        }
    }

    // 1c. Merge mounted ControlTemplate.Triggers, EventTriggers & VisualStateGroups onto resolvedStyle
    if (const AxbControlTemplate* mountedCt = element->MountedControlTemplate()) {
        for (auto ctTrig : mountedCt->triggers) {
            ctTrig.overrideLocalValues = true;
            style.triggers.push_back(std::move(ctTrig));
        }
        for (const auto& ctEt : mountedCt->eventTriggers) {
            style.eventTriggers.push_back(ctEt);
        }
        for (const auto& ctVsg : mountedCt->visualStateGroups) {
            bool exists = false;
            for (const auto& existingVsg : style.visualStateGroups) {
                if (existingVsg.name == ctVsg.name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                style.visualStateGroups.push_back(ctVsg);
            }
        }
    }

    // 2. Pre-allocate StyledElementInstance & snapshot slots so runtime EvaluateAndApply performs 0 heap allocations!
    StyledElementInstance* targetInst = nullptr;
    for (auto& existing : instances_) {
        if (existing.element.Get() == element) {
            RestoreAllCapturedSnapshots(existing);
            targetInst = &existing;
            break;
        }
    }
    if (!targetInst) {
        instances_.emplace_back();
        targetInst = &instances_.back();
    }

    targetInst->element.Reset(element, true);
    targetInst->baseFlattenedStyle = std::move(baseFlattened);
    targetInst->resolvedStyle = std::move(style);
    targetInst->activeTriggerMask = 0ULL;
    targetInst->initialized = false;
    targetInst->groupStates = {};
    targetInst->eventStates = {};
    targetInst->propertySnapshots.clear();

    auto reserveTarget = [&](const std::string& targetName, std::uint16_t propId) {
        UIElement* resolved = targetName.empty() ? element : element->FindName(targetName);
        if (resolved) {
            EnsurePropertySnapshotSlot(*targetInst, resolved, propId);
        }
    };

    for (const auto& trig : targetInst->resolvedStyle.triggers) {
        for (const auto& st : trig.setters) {
            reserveTarget(st.targetName, st.propId);
        }
        for (const auto& esb : trig.enterStoryboards) {
            for (const auto& tl : esb.timelines) {
                reserveTarget(tl.targetName, tl.targetPropertyId);
            }
        }
        for (const auto& xsb : trig.exitStoryboards) {
            for (const auto& tl : xsb.timelines) {
                reserveTarget(tl.targetName, tl.targetPropertyId);
            }
        }
    }
    for (const auto& vsg : targetInst->resolvedStyle.visualStateGroups) {
        for (const auto& vs : vsg.states) {
            for (const auto& st : vs.setters) {
                reserveTarget(st.targetName, st.propId);
            }
            for (const auto& tl : vs.storyboard.timelines) {
                reserveTarget(tl.targetName, tl.targetPropertyId);
            }
        }
    }

    return true;
}

bool StyleTriggerEngine::AttachStyleByKey(UIElement* element, std::string_view styleKey) {
    if (!element || styleKey.empty()) {
        return false;
    }
    if (const AxbStyleDefinition* found = FindStyle(styleKey)) {
        return AttachStyle(element, *found);
    }
    return false;
}

std::size_t StyleTriggerEngine::BindSubtreeStyles(
    UIElement* rootElement,
    bool applyImplicitTypeStyles) {
    if (!rootElement) {
        return 0u;
    }
    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        (domains_ && !UiResourceContext::HasActiveThreadOverride())
            ? UiResourceContext::FindForDomains(domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));

    rootElement->BindStyleEngine(this);
    if (animationEngine_ && !rootElement->BoundAnimationEngine()) {
        rootElement->BindAnimationEngine(animationEngine_);
    }
    if (auto* ic = rootElement->DynamicCast<ItemsControl>()) {
        ic->AssignStyleEnginePointer(this);
    } else if (auto* tc = rootElement->DynamicCast<TabControl>()) {
        tc->AssignStyleEnginePointer(this);
    }

    std::size_t attachedCount = 0u;
    bool attachedHere = false;
    const std::string explicitKey = rootElement->GetStyleKey();
    if (!explicitKey.empty()) {
        if (AttachStyleByKey(rootElement, explicitKey)) {
            ++attachedCount;
            attachedHere = true;
        }
    } else if (applyImplicitTypeStyles) {
        const AxbStyleDefinition* imp = nullptr;
        if (!rootElement->DeclaredTypeName().empty()) {
            imp = FindImplicitStyle(rootElement->DeclaredTypeName());
        }
        if (!imp) {
            if (const char* fullTypeName = rootElement->GetTypeClass()->typeName) {
                imp = FindImplicitStyle(ShortTypeNameView(fullTypeName));
            }
        }
        if (imp && AttachStyle(rootElement, *imp)) {
            ++attachedCount;
            attachedHere = true;
        }
    }

    // If a standalone ControlTemplate was attached directly via Template="{StaticResource ...}" without a Style,
    // bind its ControlTemplate.Triggers / EventTriggers / VisualStateGroups onto this host control!
    if (!attachedHere && rootElement->HasControlTemplate()) {
        if (const AxbControlTemplate* ct = rootElement->MountedControlTemplate()) {
            if (!ct->triggers.empty() || !ct->eventTriggers.empty() || !ct->visualStateGroups.empty()) {
                AxbStyleDefinition synthStyle{};
                if (const char* tn = rootElement->GetTypeClass()->typeName) {
                    synthStyle.targetTypeName = std::string(ShortTypeNameView(tn));
                }
                if (AttachStyle(rootElement, synthStyle)) {
                    ++attachedCount;
                }
            }
        }
    }

    for (UIElement* c = rootElement->FirstChild(); c != nullptr; c = c->NextSibling()) {
        attachedCount += BindSubtreeStyles(c, applyImplicitTypeStyles);
    }
    return attachedCount;
}

std::size_t StyleTriggerEngine::UnbindSubtreeStyles(const UIElement* subtreeRoot) noexcept {
    if (!subtreeRoot) {
        return 0u;
    }
    auto clearBoundRec = [this](auto& self, UIElement* node) noexcept -> void {
        if (!node) return;
        if (node->BoundStyleEngine() == this) {
            node->BindStyleEngine(nullptr);
        }
        for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
            self(self, c);
        }
    };
    clearBoundRec(clearBoundRec, const_cast<UIElement*>(subtreeRoot));

    auto isInSubtree = [&](const UIElement* candidate) noexcept -> bool {
        for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
            if (cur == subtreeRoot) {
                return true;
            }
        }
        return false;
    };

    std::size_t removed = 0u;
    std::size_t writeIdx = 0u;
    for (std::size_t i = 0u; i < instances_.size(); ++i) {
        if (instances_[i].element && isInSubtree(instances_[i].element.Get())) {
            RestoreAllCapturedSnapshots(instances_[i]);
            if (instances_[i].element->BoundStyleEngine() == this) {
                instances_[i].element->BindStyleEngine(nullptr);
            }
            ++removed;
            continue;
        }
        for (auto& rt : instances_[i].eventStates) {
            if (rt.cachedSource && isInSubtree(rt.cachedSource.Get())) {
                rt.cachedSource.Reset();
                rt.initialized = false;
            }
        }
        for (auto& slot : instances_[i].propertySnapshots) {
            if (slot.resolvedTarget && isInSubtree(slot.resolvedTarget.Get())) {
                slot.resolvedTarget.Reset();
                slot.captured = false;
            }
        }
        if (writeIdx != i) {
            instances_[writeIdx] = std::move(instances_[i]);
        }
        ++writeIdx;
    }
    instances_.resize(writeIdx);
    return removed;
}

void StyleTriggerEngine::OnElementControlTemplateChanging(
    UIElement* host,
    UIElement* oldTemplateRoot) noexcept {
    if (!host || !oldTemplateRoot) {
        return;
    }
    auto isInOldTpl = [oldTemplateRoot](const UIElement* candidate) noexcept -> bool {
        for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
            if (cur == oldTemplateRoot) {
                return true;
            }
        }
        return false;
    };
    for (auto& inst : instances_) {
        for (auto& slot : inst.propertySnapshots) {
            if (slot.resolvedTarget && isInOldTpl(slot.resolvedTarget.Get())) {
                if (slot.captured) {
                    if (animationEngine_) {
                        (void)animationEngine_->StopPropertyAnimation(
                            slot.resolvedTarget.Get(), slot.propId, false);
                    }
                    if (slot.hadBaseValue) {
                        (void)slot.resolvedTarget->ForceEffectiveValue(
                            slot.propId, slot.baseValue, slot.baseRank);
                    } else if (IsFastInlinePropertyId(slot.propId)) {
                        (void)slot.resolvedTarget->ClearValue(slot.propId, ValueSourceRank::Animation);
                    } else {
                        (void)slot.resolvedTarget->ForceClearSparseEntry(slot.propId);
                    }
                }
                slot.resolvedTarget.Reset();
                slot.captured = false;
                slot.touchedThisPass = false;
            }
        }
        for (auto& rt : inst.eventStates) {
            if (rt.cachedSource && isInOldTpl(rt.cachedSource.Get())) {
                rt.cachedSource.Reset();
                rt.initialized = false;
            }
        }
    }
}

void StyleTriggerEngine::OnElementControlTemplateCleared(UIElement* host) {
    if (inAttachStyle_ || !host) {
        return;
    }
    for (std::size_t i = 0u; i < instances_.size(); ++i) {
        if (instances_[i].element.Get() != host) {
            continue;
        }
        RestoreAllCapturedSnapshots(instances_[i]);
        instances_[i].resolvedStyle = instances_[i].baseFlattenedStyle;
        instances_[i].activeTriggerMask = 0ULL;
        instances_[i].initialized = false;
        instances_[i].groupStates = {};
        instances_[i].eventStates = {};
        instances_[i].propertySnapshots.clear();

        auto reserveTarget = [&](const std::string& targetName, std::uint16_t propId) {
            UIElement* resolved = targetName.empty() ? host : host->FindName(targetName);
            if (resolved) {
                EnsurePropertySnapshotSlot(instances_[i], resolved, propId);
            }
        };
        for (const auto& trig : instances_[i].resolvedStyle.triggers) {
            for (const auto& st : trig.setters) reserveTarget(st.targetName, st.propId);
            for (const auto& esb : trig.enterStoryboards) {
                for (const auto& tl : esb.timelines) reserveTarget(tl.targetName, tl.targetPropertyId);
            }
            for (const auto& xsb : trig.exitStoryboards) {
                for (const auto& tl : xsb.timelines) reserveTarget(tl.targetName, tl.targetPropertyId);
            }
        }
        for (const auto& vsg : instances_[i].resolvedStyle.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& st : vs.setters) reserveTarget(st.targetName, st.propId);
                for (const auto& tl : vs.storyboard.timelines) reserveTarget(tl.targetName, tl.targetPropertyId);
            }
        }
        (void)EvaluateSubtree(host, true);
        break;
    }
}

void StyleTriggerEngine::OnElementControlTemplateApplied(UIElement* host) {
    if (inAttachStyle_ || !host) {
        return;
    }
    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        (domains_ && !UiResourceContext::HasActiveThreadOverride())
            ? UiResourceContext::FindForDomains(domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));

    const bool prevInAttach = inAttachStyle_;
    inAttachStyle_ = true;

    bool foundExisting = false;
    for (std::size_t i = 0u; i < instances_.size(); ++i) {
        if (instances_[i].element.Get() == host) {
            AxbStyleDefinition savedBase = instances_[i].baseFlattenedStyle;
            (void)AttachStyle(host, savedBase);
            foundExisting = true;
            break;
        }
    }
    if (!foundExisting) {
        if (const AxbControlTemplate* ct = host->MountedControlTemplate()) {
            if (!ct->triggers.empty() || !ct->eventTriggers.empty() || !ct->visualStateGroups.empty()) {
                AxbStyleDefinition synthStyle{};
                if (const char* tn = host->GetTypeClass()->typeName) {
                    synthStyle.targetTypeName = std::string(ShortTypeNameView(tn));
                }
                (void)AttachStyle(host, synthStyle);
            }
        }
    }
    if (UIElement* tplRoot = host->TemplateRoot()) {
        (void)BindSubtreeStyles(tplRoot, true);
    }

    inAttachStyle_ = prevInAttach;
    (void)EvaluateSubtree(host, true);
}

bool StyleTriggerEngine::HasFocusedDescendant(const UIElement* elem) noexcept {
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

bool StyleTriggerEngine::EvaluateSingleCondition(
    UIElement* elem,
    AxbTriggerCondition& cond) {
    if (!elem) {
        return false;
    }

    // Pull latest context from attached DualTrackBindingEngine if present
    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }

    auto readElemPropOrPseudo = [](const UIElement* target, std::uint16_t propId) -> UnifiedValue {
        if (!target) return UnifiedValue{};
        switch (propId) {
            case PropId_TriggerIsMouseOver:
                return UnifiedValue{target->IsHovered()};
            case PropId_TriggerIsPressed:
                return UnifiedValue{target->IsPressed()};
            case PropId_TriggerIsFocused:
                return UnifiedValue{target->IsFocused()};
            case PropId_TriggerIsKeyboardFocusWithin:
                return UnifiedValue{StyleTriggerEngine::HasFocusedDescendant(target)};
            case PropId_IsEnabled:
                return UnifiedValue{target->IsEffectivelyEnabled()};
            default:
                return target->GetValue(propId);
        }
    };

    UnifiedValue actualVal{};

    switch (cond.sourceKind) {
        case TriggerConditionSource::SelfProperty: {
            if (cond.converter == ValueConverterKind::None && !cond.expectNull && cond.comparison == TriggerComparisonOp::Equal) {
                bool expectedBool = true;
                if (const auto* b = std::get_if<bool>(&cond.expectedValue)) {
                    expectedBool = *b;
                } else if (const auto* i = std::get_if<std::int64_t>(&cond.expectedValue)) {
                    expectedBool = (*i != 0);
                }
                switch (cond.propId) {
                    case PropId_TriggerIsMouseOver:
                        return elem->IsHovered() == expectedBool;
                    case PropId_TriggerIsPressed:
                        return elem->IsPressed() == expectedBool;
                    case PropId_TriggerIsFocused:
                        return elem->IsFocused() == expectedBool;
                    case PropId_TriggerIsKeyboardFocusWithin:
                        return HasFocusedDescendant(elem) == expectedBool;
                    case PropId_IsEnabled:
                        return elem->IsEffectivelyEnabled() == expectedBool;
                    default:
                        break;
                }
            }
            actualVal = readElemPropOrPseudo(elem, cond.propId);
            break;
        }

        case TriggerConditionSource::ElementNameProperty: {
            UIElement* srcElem = elem->FindName(cond.sourceName);
            if (!srcElem) {
                if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(elem)) {
                    srcElem = scopeRoot->FindName(cond.sourceName);
                }
            }
            if (!srcElem) {
                return false;
            }
            actualVal = readElemPropOrPseudo(srcElem, cond.propId);
            break;
        }

        case TriggerConditionSource::AncestorProperty: {
            const std::string_view wantedType = ShortTypeNameView(cond.sourceName);
            const UIElement* matchedAnc = nullptr;
            for (const UIElement* anc = elem; anc != nullptr; anc = NextContextParent(anc)) {
                if (MatchesXamlAncestorType(anc, wantedType)) {
                    matchedAnc = anc;
                    break;
                }
            }
            if (!matchedAnc) {
                return false;
            }
            actualVal = readElemPropOrPseudo(matchedAnc, cond.propId);
            break;
        }

        case TriggerConditionSource::DataContextBinding: {
            const ObjectRef dcObj = ResolveEffectiveDataContext(elem);
            if (cond.compiledHops.empty()) {
                // "{Binding}" empty path binds directly to DataContext itself (e.g. Value="{x:Null}")
                actualVal = dcObj.valid() ? UnifiedValue{dcObj} : UnifiedValue{NullObject{}};
                break;
            }
            if (!dcObj.valid() || !domains_) {
                actualVal = NullObject{};
                break;
            }

            ObjectRef curObj = dcObj;
            bool chainOk = true;
            for (std::size_t h = 0u; h < cond.compiledHops.size(); ++h) {
                auto& hop = cond.compiledHops[h];
                UnifiedValue hopVal{};
                if (!ReadSingleHopOnObject(domains_, program_, uiInterpreter_, curObj, hop, hopVal)) {
                    chainOk = false;
                    break;
                }

                if (h + 1u < cond.compiledHops.size()) {
                    const auto* nextRef = std::get_if<ObjectRef>(&hopVal);
                    if (!nextRef || !nextRef->valid()) {
                        actualVal = NullObject{};
                        chainOk = true;
                        break;
                    }
                    curObj = *nextRef;
                } else {
                    actualVal = hopVal;
                }
            }
            if (!chainOk) {
                return false;
            }
            break;
        }
    }

    if (cond.sourceKind != TriggerConditionSource::DataContextBinding &&
        !cond.compiledHops.empty()) {
        const auto* startRef = std::get_if<ObjectRef>(&actualVal);
        if (!startRef || !startRef->valid() || !domains_) {
            actualVal = NullObject{};
        } else {
            ObjectRef curObj = *startRef;
            for (std::size_t h = 0u; h < cond.compiledHops.size(); ++h) {
                auto& hop = cond.compiledHops[h];
                UnifiedValue hopVal{};
                if (!ReadSingleHopOnObject(domains_, program_, uiInterpreter_, curObj, hop, hopVal)) {
                    return false;
                }
                if (h + 1u < cond.compiledHops.size()) {
                    const auto* nextRef = std::get_if<ObjectRef>(&hopVal);
                    if (!nextRef || !nextRef->valid()) {
                        actualVal = NullObject{};
                        break;
                    }
                    curObj = *nextRef;
                } else {
                    actualVal = hopVal;
                }
            }
        }
    }

    UnifiedValue resolvedExpected = cond.expectedValue;
    if (cond.converter == ValueConverterKind::None && !cond.expectNull && program_) {
        SymbolId leafEnumTypeId = 0;
        if (!cond.compiledHops.empty() && cond.compiledHops.back().targetIsEnum) {
            leafEnumTypeId = cond.compiledHops.back().targetTypeId;
        }
        EnumValue ev{};
        if (actualVal.tryGetEnum(ev) && ev.typeId != 0) {
            leafEnumTypeId = ev.typeId;
        }
        if (leafEnumTypeId != 0) {
            if (const auto* expStr = std::get_if<StringValue>(&resolvedExpected)) {
                std::int64_t enumInt = 0;
                if (TryResolveEnumMemberByName(program_, leafEnumTypeId, expStr->view(), enumInt)) {
                    resolvedExpected = value::FromInt(enumInt);
                }
            }
        }
    }

    if (cond.converter != ValueConverterKind::None) {
        if (bindingEngine_) {
            actualVal = bindingEngine_->ApplySingleValueConversion(
                cond.converter,
                cond.converterKey,
                cond.converterParameter,
                cond.converterSymbolId,
                0,
                actualVal,
                false);
        } else if (cond.converter == ValueConverterKind::MathExpression) {
            float paramF = 0.0f;
            (void)TryParseStackFloat(cond.converterParameter, paramF);
            float mathOut = 0.0f;
            if (EvaluateMathConverterExpression(cond.converterKey, &actualVal, 1u, paramF, mathOut)) {
                actualVal = MakeFloat(mathOut);
            }
        } else {
            actualVal = ApplyBuiltinValueConverter(cond.converter, actualVal, false);
        }
    }

    return TriggerConditionValueMatches(
        actualVal,
        resolvedExpected,
        cond.expectNull,
        cond.comparison,
        domains_,
        program_);
}

namespace {

[[nodiscard]] bool EvaluateRuntimeBindingOnce(
    UIElement* elem,
    ExecutionDomainContext* domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter,
    DualTrackBindingEngine* bindingEngine,
    AxbCompiledBindingChain& bc,
    UnifiedValue& outVal) {
    if (!elem) {
        return false;
    }
    bool haveRaw = false;
    UnifiedValue rawVal{};

    if (!bc.elementName.empty() || !bc.relativeAncestorType.empty() || bc.relativeSelf) {
        UIElement* srcEl = nullptr;
        if (bc.relativeSelf) {
            srcEl = elem;
        } else if (!bc.relativeAncestorType.empty()) {
            const std::string_view wantedType = ShortTypeNameView(bc.relativeAncestorType);
            for (UIElement* anc = elem; anc != nullptr; anc = const_cast<UIElement*>(NextContextParent(anc))) {
                if (MatchesXamlAncestorType(anc, wantedType)) {
                    srcEl = anc;
                    break;
                }
            }
        } else {
            srcEl = elem->FindName(bc.elementName);
            if (!srcEl) {
                if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(elem)) {
                    srcEl = scopeRoot->FindName(bc.elementName);
                }
            }
        }
        if (!srcEl) {
            return false;
        }

        if ((bc.rawPath.empty() || bc.rawPath == ".") && bc.hops.empty()) {
            if (!domains) {
                return false;
            }
            rawVal = UnifiedValue{srcEl->ToNativeHandle(domains->nativeHandles())};
            haveRaw = true;
        } else if (bc.hops.size() > 1u && domains) {
            const std::uint16_t firstProp = ResolveTriggerConditionPropertyId(bc.hops.front().memberName);
            UnifiedValue firstVal = (firstProp != 0u) ? srcEl->GetValue(firstProp) : UnifiedValue{};
            const auto* curRef = std::get_if<ObjectRef>(&firstVal);
            if (curRef && curRef->valid()) {
                ObjectRef curObj = *curRef;
                bool ok = true;
                for (std::size_t h = 1u; h < bc.hops.size(); ++h) {
                    UnifiedValue hv{};
                    if (!ReadSingleHopOnObject(domains, program, uiInterpreter, curObj, bc.hops[h], hv)) {
                        ok = false;
                        break;
                    }
                    if (h + 1u < bc.hops.size()) {
                        const auto* nr = std::get_if<ObjectRef>(&hv);
                        if (!nr || !nr->valid()) {
                            rawVal = NullObject{};
                            haveRaw = true;
                            ok = false;
                            break;
                        }
                        curObj = *nr;
                    } else {
                        rawVal = hv;
                        haveRaw = true;
                    }
                }
                (void)ok;
            }
        } else {
            const std::uint16_t spId = ResolveTriggerConditionPropertyId(
                !bc.hops.empty() ? std::string_view(bc.hops.front().memberName) : std::string_view(bc.rawPath));
            if (spId != 0u) {
                rawVal = srcEl->GetValue(spId);
                haveRaw = !std::holds_alternative<std::monostate>(rawVal);
            }
        }
    } else {
        ObjectRef cur = ResolveEffectiveDataContext(elem);
        if (bc.hops.empty()) {
            if (cur.valid()) {
                rawVal = cur;
                haveRaw = true;
            }
        } else if (cur.valid() && domains) {
            for (std::size_t h = 0u; h < bc.hops.size(); ++h) {
                UnifiedValue hv{};
                if (!ReadSingleHopOnObject(domains, program, uiInterpreter, cur, bc.hops[h], hv)) {
                    break;
                }
                if (h + 1u < bc.hops.size()) {
                    const auto* nr = std::get_if<ObjectRef>(&hv);
                    if (!nr || !nr->valid()) {
                        rawVal = NullObject{};
                        haveRaw = true;
                        break;
                    }
                    cur = *nr;
                } else {
                    rawVal = hv;
                    haveRaw = true;
                }
            }
        }
    }

    if (!haveRaw) {
        if (!std::holds_alternative<std::monostate>(bc.fallbackValue)) {
            rawVal = bc.fallbackValue;
            haveRaw = true;
        } else {
            return false;
        }
    }

    if (bc.converter != ValueConverterKind::None) {
        if (bindingEngine) {
            rawVal = bindingEngine->ApplySingleValueConversion(
                bc.converter,
                bc.converterKey,
                bc.converterParameter,
                bc.converterSymbolId,
                bc.convertBackSymbolId,
                rawVal,
                false);
        } else if (bc.converter == ValueConverterKind::MathExpression) {
            float paramF = 0.0f;
            (void)TryParseStackFloat(bc.converterParameter, paramF);
            float mathOut = 0.0f;
            if (EvaluateMathConverterExpression(bc.converterKey, &rawVal, 1u, paramF, mathOut)) {
                rawVal = MakeFloat(mathOut);
            }
        } else {
            rawVal = ApplyBuiltinValueConverter(bc.converter, rawVal, false);
        }
    }

    outVal = rawVal;
    return true;
}

} // namespace

[[nodiscard]] bool TryInvokeCommandObjectOnOwner(
    ExecutionDomainContext* domains,
    const ProgramImage* prog,
    const Interpreter* interp,
    rs_aot::ExecutionContext* aotCtx,
    ObjectRef ownerObj,
    std::string_view propName,
    const UnifiedValue& rawParam,
    bool hasParam) {
    if (!domains || !prog || !ownerObj.valid() || propName.empty()) {
        return false;
    }
    auto* ownerHeap = domains->heapForObject(ownerObj);
    if (!ownerHeap || !ownerHeap->isAlive(ownerObj)) {
        return false;
    }
    const SymbolId ownerTid = ownerHeap->objectTypeId(ownerObj).value_or(0);
    if (ownerTid == 0) {
        return false;
    }

    ObjectRef cmdObj{};
    if (const auto slot = domains->resolvePropertySlot(*prog, ownerTid, propName)) {
        UnifiedValue v{};
        if (domains->readPropertySlotFast(DomainKind::Ui, ownerObj, *slot, v, nullptr, interp)) {
            if (const auto* r = std::get_if<ObjectRef>(&v); r && r->valid()) {
                cmdObj = *r;
            }
        }
    }
    if (!cmdObj.valid()) {
        SymbolId walkTid = ownerTid;
        for (int d = 0; d < 16 && walkTid != 0 && !cmdObj.valid(); ++d) {
            const auto* ts = prog->resolveType(walkTid);
            if (!ts) break;
            std::string privName = "_";
            privName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(propName[0]))));
            privName.append(propName.substr(1));
            for (const auto& f : ts->fields) {
                if (f.name == propName || f.name == privName ||
                    f.name == ("<" + std::string(propName) + ">k__BackingField")) {
                    UnifiedValue fv{};
                    if (domains->readObjectField(DomainKind::Ui, ownerObj, f.index, fv)) {
                        if (const auto* r = std::get_if<ObjectRef>(&fv); r && r->valid()) {
                            cmdObj = *r;
                            break;
                        }
                    }
                }
            }
            if (!cmdObj.valid()) {
                const std::string getterName = "get_" + std::string(propName);
                const std::string cGetterName = "Get" + std::string(propName);
                for (const auto& m : ts->methods) {
                    if ((m.name == getterName || m.name == cGetterName) && !m.staticMethod && m.parameters.size() == 1u && m.id != 0) {
                        if (interp) {
                            rs_rt::ExecutionOptions gOpts{};
                            gOpts.limits.instructionBudget = 1'000'000;
                            const auto gRes = interp->invoke(m.id, {ownerObj}, gOpts);
                            if (gRes.succeeded) {
                                if (const auto* r = std::get_if<ObjectRef>(&gRes.value); r && r->valid()) {
                                    cmdObj = *r;
                                    break;
                                }
                            }
                        } else if (aotCtx) {
                            UnifiedValue arg0 = ownerObj;
                            UnifiedValue ret{};
                            if (aotCtx->invoke(m.id, &arg0, 1u, ret)) {
                                if (const auto* r = std::get_if<ObjectRef>(&ret); r && r->valid()) {
                                    cmdObj = *r;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            walkTid = ts->baseTypeId;
        }
    }
    if (!cmdObj.valid()) {
        return false;
    }
    auto* cmdHeap = domains->heapForObject(cmdObj);
    if (!cmdHeap || !cmdHeap->isAlive(cmdObj)) {
        return false;
    }
    const SymbolId cmdTid = cmdHeap->objectTypeId(cmdObj).value_or(0);
    const auto* cmdTs = (cmdTid != 0) ? prog->resolveType(cmdTid) : nullptr;
    if (!cmdTs) {
        return false;
    }

    // Check which active delegate field is populated on DelegateCommand (if applicable)
    auto hasNonNullField = [&](std::string_view fn) -> bool {
        for (const auto& f : cmdTs->fields) {
            if (f.name == fn) {
                UnifiedValue v{};
                if (domains->readObjectField(DomainKind::Ui, cmdObj, f.index, v)) {
                    if (const auto* r = std::get_if<ObjectRef>(&v); r && r->valid() && cmdHeap->isAlive(*r)) {
                        return true;
                    }
                }
                return false;
            }
        }
        return false;
    };

    const bool hasExecInt = hasNonNullField("_execInt");
    const bool hasExecStr = hasNonNullField("_execStr");
    const bool hasExecHandle = hasNonNullField("_execHandle");
    const bool hasExec0 = hasNonNullField("_exec0");

    auto findExecMethod = [&](rs_sem::PrimitiveType expectedParamType, bool zeroArg) -> SymbolId {
        SymbolId walk = cmdTid;
        for (int d = 0; d < 16 && walk != 0; ++d) {
            const auto* ts = prog->resolveType(walk);
            if (!ts) break;
            for (const auto& m : ts->methods) {
                if (m.name != "Execute" || m.staticMethod || m.id == 0) continue;
                if (zeroArg && m.parameters.size() == 1u) return m.id;
                if (!zeroArg && m.parameters.size() == 2u && m.parameters[1].type == expectedParamType) {
                    return m.id;
                }
            }
            walk = ts->baseTypeId;
        }
        return 0;
    };

    auto invokeCmdMethod = [&](SymbolId mSym, const std::vector<UnifiedValue>& callArgs) -> bool {
        if (mSym == 0) return false;
        if (interp) {
            rs_rt::ExecutionOptions opts{};
            opts.limits.instructionBudget = 5'000'000;
            return interp->invoke(mSym, callArgs, opts).succeeded;
        }
        if (aotCtx) {
            UnifiedValue ret{};
            return aotCtx->invoke(mSym, callArgs.data(), callArgs.size(), ret);
        }
        return false;
    };

    auto coerceIntParam = [&](const UnifiedValue& p) -> std::int64_t {
        std::int64_t iv = 0;
        if (p.tryGetInt64(iv)) return iv;
        EnumValue ev{};
        if (p.tryGetEnum(ev)) return ev.value;
        float fv = 0.0f;
        if (TryGetFloat(p, fv)) return static_cast<std::int64_t>(std::llround(fv));
        if (const auto* sv = std::get_if<StringValue>(&p)) {
            const std::string_view v = sv->view();
            const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), iv);
            if (ec == std::errc{} && ptr == v.data() + v.size()) return iv;
            for (const auto& mod : prog->modules()) {
                for (const auto& ets : mod.types) {
                    if (ets.kind == rs_sem::TypeKind::Enum &&
                        TryResolveEnumMemberByName(prog, ets.id, v, iv)) {
                        return iv;
                    }
                }
            }
        }
        if (const auto* sref = std::get_if<ObjectRef>(&p); sref && sref->valid()) {
            if (const auto sv = domains->stringViewAcrossDomains(*sref)) {
                const auto [ptr, ec] = std::from_chars(sv->data(), sv->data() + sv->size(), iv);
                if (ec == std::errc{} && ptr == sv->data() + sv->size()) return iv;
            }
        }
        return 0;
    };

    if (hasExecInt) {
        const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Int, false);
        return invokeCmdMethod(mSym, {cmdObj, value::FromInt(hasParam ? coerceIntParam(rawParam) : 0)});
    }
    if (hasExecHandle) {
        const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Handle, false);
        NativeHandle nh{};
        if (hasParam) {
            (void)rawParam.tryGetNativeHandle(nh);
        }
        return invokeCmdMethod(mSym, {cmdObj, UnifiedValue{nh}});
    }
    if (hasExecStr) {
        const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::String, false);
        UnifiedValue strArg{};
        if (hasParam) {
            if (const auto* sref = std::get_if<ObjectRef>(&rawParam); sref && sref->valid() && sref->kind == ObjectKind::String) {
                strArg = *sref;
            } else {
                const std::string s = FormatBindingString({}, &rawParam, 1u, domains);
                if (auto alloc = domains->uiHeap().allocateString(s)) strArg = *alloc;
            }
        } else if (auto alloc = domains->uiHeap().allocateString("")) {
            strArg = *alloc;
        }
        return invokeCmdMethod(mSym, {cmdObj, strArg});
    }
    if (hasExec0) {
        const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Void, true);
        return invokeCmdMethod(mSym, {cmdObj});
    }

    // Custom ICommand implementation (not built-in DelegateCommand)
    if (hasParam) {
        NativeHandle nh{};
        if (rawParam.tryGetNativeHandle(nh)) {
            if (const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Handle, false)) {
                return invokeCmdMethod(mSym, {cmdObj, UnifiedValue{nh}});
            }
        }
        std::int64_t iv = 0;
        EnumValue ev{};
        if (rawParam.tryGetInt64(iv) || rawParam.tryGetEnum(ev)) {
            if (const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Int, false)) {
                return invokeCmdMethod(mSym, {cmdObj, value::FromInt(coerceIntParam(rawParam))});
            }
        }
        if (const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::String, false)) {
            const std::string s = FormatBindingString({}, &rawParam, 1u, domains);
            if (auto alloc = domains->uiHeap().allocateString(s)) {
                return invokeCmdMethod(mSym, {cmdObj, *alloc});
            }
        }
    }
    if (const SymbolId mSym = findExecMethod(rs_sem::PrimitiveType::Void, true)) {
        return invokeCmdMethod(mSym, {cmdObj});
    }
    return false;
}

void StyleTriggerEngine::ExecuteSingleInteractionAction(
    StyledElementInstance& inst,
    AxbInteractionAction& action) {
    if (!action.isEnabled) {
        return;
    }
    UIElement* elem = inst.element.Get();
    if (!elem) {
        return;
    }

    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }

    switch (action.kind) {
        case InteractionActionKind::ChangeProperty: {
            // Check if TargetName refers to a sibling InteractionAction's x:Name (e.g. toggling IsEnabled on PlaySoundAction)
            if (!action.targetName.empty() &&
                (action.propId == PropId_IsEnabled || EqualsAsciiIgnoreCase(action.propertyName, "IsEnabled"))) {
                bool matchedActionTarget = false;
                bool desiredEnabled = true;
                if (const auto* b = std::get_if<bool>(&action.value)) {
                    desiredEnabled = *b;
                } else if (const auto* sv = std::get_if<StringValue>(&action.value)) {
                    desiredEnabled = EqualsAsciiIgnoreCase(sv->view(), "true") || sv->view() == "1";
                }
                auto updateSiblingList = [&](std::vector<AxbInteractionAction>& list) {
                    for (auto& cand : list) {
                        if (cand.actionName == action.targetName) {
                            cand.isEnabled = desiredEnabled;
                            matchedActionTarget = true;
                        }
                    }
                };
                for (auto& et : inst.resolvedStyle.eventTriggers) {
                    updateSiblingList(et.actions);
                }
                for (auto& tr : inst.resolvedStyle.triggers) {
                    updateSiblingList(tr.enterActions);
                    updateSiblingList(tr.exitActions);
                }
                if (matchedActionTarget) {
                    break;
                }
            }

            UIElement* target = nullptr;
            if (action.targetName.empty()) {
                target = elem;
            } else {
                target = elem->FindName(action.targetName);
                if (!target) {
                    if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(elem)) {
                        target = scopeRoot->FindName(action.targetName);
                    }
                }
            }
            if (!target || action.propId == 0xFFFFu) {
                break;
            }
            if (animationEngine_) {
                (void)animationEngine_->StopPropertyAnimation(target, action.propId, false);
            }

            if (action.isNullValue) {
                if ((action.propId == PropId_Content || action.propId == PropId_Text) &&
                    (target->IsA<Border>() || target->IsA<ContentPresenter>() || target->IsA<UserControl>())) {
                    for (UIElement* ch = target->FirstChild(); ch != nullptr;) {
                        UIElement* next = ch->NextSibling();
                        if (ch != target->TemplateRoot()) {
                            if (StyleTriggerEngine* ste = ch->BoundStyleEngine()) {
                                (void)ste->UnbindSubtreeStyles(ch);
                            }
                            if (bindingEngine_) {
                                (void)bindingEngine_->UnbindSubtree(ch);
                            }
                            (void)target->RemoveChild(ch);
                        }
                        ch = next;
                    }
                    (void)target->ForceClearSparseEntry(PropId_Content);
                    (void)target->ForceClearSparseEntry(PropId_Text);
                    break;
                }
                if (IsFastInlinePropertyId(action.propId)) {
                    (void)target->ClearValue(action.propId, ValueSourceRank::Animation);
                } else {
                    (void)target->ForceClearSparseEntry(action.propId);
                }
                if (target->BoundTwoWayEngine() != nullptr) {
                    (void)target->NotifyTwoWayPropertyMutated(action.propId);
                }
                break;
            }

            UnifiedValue newVal = action.value;
            if (action.hasValueBinding) {
                if (action.valueMultiBinding) {
                    auto& mb = *action.valueMultiBinding;
                    std::vector<UnifiedValue> args;
                    args.reserve(mb.sources.size());
                    for (auto& childBc : mb.sources) {
                        UnifiedValue cv{};
                        if (!EvaluateRuntimeBindingOnce(
                                elem, domains_, program_, uiInterpreter_, bindingEngine_, childBc, cv)) {
                            cv = !std::holds_alternative<std::monostate>(childBc.fallbackValue)
                                ? childBc.fallbackValue
                                : MakeFloat(0.0f);
                        }
                        args.push_back(cv);
                    }
                    if (bindingEngine_) {
                        newVal = bindingEngine_->ApplyMultiValueConversion(
                            mb.converter,
                            mb.converterKey,
                            mb.converterParameter,
                            mb.converterSymbolId,
                            args.data(),
                            args.size());
                    } else if (mb.converter == ValueConverterKind::MathExpression) {
                        float paramF = 0.0f;
                        (void)TryParseStackFloat(mb.converterParameter, paramF);
                        float mathOut = 0.0f;
                        if (EvaluateMathConverterExpression(
                                mb.converterKey, args.data(), args.size(), paramF, mathOut)) {
                            newVal = MakeFloat(mathOut);
                        }
                    }
                } else if (action.valueBinding) {
                    UnifiedValue evalVal{};
                    if (!EvaluateRuntimeBindingOnce(
                            elem, domains_, program_, uiInterpreter_, bindingEngine_, *action.valueBinding, evalVal)) {
                        break;
                    }
                    newVal = evalVal;
                }
            }

            for (auto& sInst : instances_) {
                for (auto& slot : sInst.propertySnapshots) {
                    if (slot.resolvedTarget.Get() == target && slot.propId == action.propId) {
                        slot.baseValue = newVal;
                        slot.hadBaseValue = true;
                        slot.baseRank = ValueSourceRank::Local;
                    }
                }
            }
            (void)target->ForceEffectiveValue(action.propId, newVal, ValueSourceRank::Local);
            if (target->BoundTwoWayEngine() != nullptr) {
                (void)target->NotifyTwoWayPropertyMutated(action.propId);
            }
            break;
        }

        case InteractionActionKind::ControlStoryboard: {
            if (!animationEngine_) {
                break;
            }
            const AxbStoryboard* sb = nullptr;
            if (!action.inlineStoryboard.timelines.empty()) {
                sb = &action.inlineStoryboard;
            } else if (!action.storyboardKey.empty()) {
                sb = FindStoryboard(action.storyboardKey);
            }
            if (!sb) {
                break;
            }
            switch (action.storyboardOption) {
                case ControlStoryboardOption::Play:
                    (void)animationEngine_->BeginStoryboard(elem, *sb);
                    break;
                case ControlStoryboardOption::Stop:
                    (void)animationEngine_->StopStoryboard(elem, *sb, true);
                    break;
                case ControlStoryboardOption::TogglePlayPause:
                    (void)animationEngine_->TogglePlayPauseStoryboard(elem, *sb);
                    break;
            }
            break;
        }

        case InteractionActionKind::InvokeCommand: {
            const ObjectRef wrapperObj = ResolveRootScriptWrapper(elem);
            const ObjectRef dcObj = ResolveEffectiveDataContext(elem);

            if (action.commandPath.rfind("__NavigateTo:", 0) == 0) {
                const std::string destId = action.commandPath.substr(13u);
                // 1. Invoke MainWindow.NavigateTo<DestId>() if defined
                if (wrapperObj.valid() && domains_ && program_) {
                    if (auto* heap = domains_->heapForObject(wrapperObj); heap && heap->isAlive(wrapperObj)) {
                        const SymbolId wrapTid = heap->objectTypeId(wrapperObj).value_or(0);
                        const std::string navMethod = "NavigateTo" + destId;
                        bool isSim = false;
                        const SymbolId mSym = FindMethodInHierarchy(program_, wrapTid, navMethod, isSim);
                        if (mSym != 0) {
                            std::vector<UnifiedValue> args{wrapperObj};
                            if (uiInterpreter_) {
                                (void)uiInterpreter_->invoke(mSym, args);
                            } else if (aotContext_) {
                                UnifiedValue ret{};
                                (void)aotContext_->invoke(mSym, args.data(), args.size(), ret);
                            }
                        }
                    }
                }
                // 2. Switch NavigationRoot's Content + ContentTemplate to the matching destination
                UIElement* topRoot = elem;
                while (const UIElement* nxt = NextContextParent(topRoot)) {
                    topRoot = const_cast<UIElement*>(nxt);
                }
                UIElement* navRoot = topRoot ? topRoot->FindName("NavigationRoot") : nullptr;
                if (navRoot && domains_) {
                    ObjectRef rootVm{};
                    const UnifiedValue rootDc = topRoot->GetValue(PropId_DataContext);
                    if (const auto* rRef = std::get_if<ObjectRef>(&rootDc); rRef && rRef->valid()) {
                        rootVm = *rRef;
                    }
                    if (rootVm.valid()) {
                        AxbCompiledHop destHop{};
                        destHop.memberName = destId + "Menu";
                        UnifiedValue destMenuVal{};
                        if (ReadSingleHopOnObject(domains_, program_, uiInterpreter_, rootVm, destHop, destMenuVal)) {
                            (void)navRoot->SetValue(PropId_Content, destMenuVal, ValueSourceRank::Local);
                            (void)navRoot->SetValue(PropId_Text, destMenuVal, ValueSourceRank::Local);
                        }
                    }
                    const std::string tplKey = "Menu3D.Template." + destId + "Menu";
                    if (AxbTemplateRegistry::Instance().FindDataTemplate(tplKey) != nullptr) {
                        (void)navRoot->SetValue(
                            PropId_ContentTemplate,
                            value::FromString(tplKey),
                            ValueSourceRank::Local);
                    }
                }
                break;
            }

            std::string leafName = action.commandPath;
            if (const auto dot = leafName.rfind('.'); dot != std::string::npos) {
                leafName = leafName.substr(dot + 1u);
            }
            if (leafName.empty() && !action.commandHops.empty()) {
                leafName = action.commandHops.back().memberName;
            }

            ObjectRef targetObj{};
            SymbolId methodSym = 0;
            bool isSimCmd = false;

            auto resolveRuntimeTypeId = [&](ObjectRef obj) -> SymbolId {
                if (!domains_ || !obj.valid()) return 0;
                if (auto* heap = domains_->heapForObject(obj)) {
                    if (heap->isAlive(obj)) {
                        return heap->objectTypeId(obj).value_or(0);
                    }
                }
                return 0;
            };

            if (action.commandHops.size() <= 1u) {
                // 1. Check MainWindow (BoundScriptWrapper) FIRST when present!
                if (wrapperObj.valid()) {
                    const SymbolId wrapTypeId = resolveRuntimeTypeId(wrapperObj);
                    if (action.commandTargetsWrapper && action.commandMethodSymbolId != 0 &&
                        DoesTypeOwnOrInheritMethod(program_, wrapTypeId, action.commandMethodSymbolId)) {
                        targetObj = wrapperObj;
                        methodSym = action.commandMethodSymbolId;
                        isSimCmd = action.isSimCommand;
                    } else if (program_ && wrapTypeId != 0) {
                        bool sim = false;
                        const SymbolId found = FindMethodInHierarchy(program_, wrapTypeId, leafName, sim);
                        if (found != 0) {
                            targetObj = wrapperObj;
                            methodSym = found;
                            isSimCmd = sim;
                        }
                    }
                }

                // 2. Fall back to effective DataContext (ViewModel or DataTemplate item / sub-menu)
                if (methodSym == 0 && dcObj.valid()) {
                    const SymbolId dcTypeId = resolveRuntimeTypeId(dcObj);
                    if (!action.commandTargetsWrapper && action.commandMethodSymbolId != 0 &&
                        DoesTypeOwnOrInheritMethod(program_, dcTypeId, action.commandMethodSymbolId)) {
                        targetObj = dcObj;
                        methodSym = action.commandMethodSymbolId;
                        isSimCmd = action.isSimCommand;
                    } else if (program_ && dcTypeId != 0) {
                        bool sim = false;
                        const SymbolId found = FindMethodInHierarchy(program_, dcTypeId, leafName, sim);
                        if (found != 0) {
                            targetObj = dcObj;
                            methodSym = found;
                            isSimCmd = sim;
                        }
                    }
                }
            } else if (domains_) {
                auto stepPrefixFrom = [&](ObjectRef root) -> ObjectRef {
                    ObjectRef cur = root;
                    for (std::size_t h = 0u; h + 1u < action.commandHops.size() && cur.valid(); ++h) {
                        UnifiedValue nextVal{};
                        if (!ReadSingleHopOnObject(
                                domains_, program_, uiInterpreter_, cur, action.commandHops[h], nextVal)) {
                            return ObjectRef{};
                        }
                        const auto* nextRef = std::get_if<ObjectRef>(&nextVal);
                        cur = (nextRef && nextRef->valid()) ? *nextRef : ObjectRef{};
                    }
                    return cur;
                };

                targetObj = stepPrefixFrom(dcObj);
                if (!targetObj.valid() && wrapperObj.valid() && wrapperObj != dcObj) {
                    targetObj = stepPrefixFrom(wrapperObj);
                }
                if (targetObj.valid()) {
                    const SymbolId subTypeId = resolveRuntimeTypeId(targetObj);
                    if (action.commandMethodSymbolId != 0 &&
                        DoesTypeOwnOrInheritMethod(program_, subTypeId, action.commandMethodSymbolId)) {
                        methodSym = action.commandMethodSymbolId;
                        isSimCmd = action.isSimCommand;
                    } else if (program_ && subTypeId != 0) {
                        methodSym = FindMethodInHierarchy(program_, subTypeId, leafName, isSimCmd);
                    }
                }
            }

            if (!targetObj.valid() || methodSym == 0) {
                UnifiedValue earlyParamVal{};
                bool earlyHasParam = false;
                if (action.hasCommandParameterBinding) {
                    earlyHasParam = EvaluateRuntimeBindingOnce(
                        elem, domains_, program_, uiInterpreter_, bindingEngine_,
                        action.commandParameterBinding, earlyParamVal);
                } else if (action.hasCommandParameter) {
                    earlyParamVal = action.commandParameter;
                    earlyHasParam = true;
                } else {
                    const UnifiedValue hostParam = elem->GetValue(PropId_CommandParameter);
                    if (!std::holds_alternative<std::monostate>(hostParam)) {
                        earlyParamVal = hostParam;
                        earlyHasParam = true;
                    }
                }
                ObjectRef cmdOwner = (action.commandHops.size() > 1u) ? targetObj : dcObj;
                if (TryInvokeCommandObjectOnOwner(
                        domains_, program_, uiInterpreter_, aotContext_,
                        cmdOwner, leafName, earlyParamVal, earlyHasParam)) {
                    if (bindingEngine_ && DualTrackBindingEngine::IsLiveInstance(bindingEngine_)) {
                        (void)bindingEngine_->FlushTrackA();
                    }
                    break;
                }
                if (action.commandHops.size() <= 1u && wrapperObj.valid() && wrapperObj != cmdOwner &&
                    TryInvokeCommandObjectOnOwner(
                        domains_, program_, uiInterpreter_, aotContext_,
                        wrapperObj, leafName, earlyParamVal, earlyHasParam)) {
                    if (bindingEngine_ && DualTrackBindingEngine::IsLiveInstance(bindingEngine_)) {
                        (void)bindingEngine_->FlushTrackA();
                    }
                    break;
                }
                break;
            }

            // Resolve CommandParameter (binding, literal attribute, or host PropId_CommandParameter)
            UnifiedValue paramVal{};
            bool hasParam = false;
            if (action.hasCommandParameterBinding) {
                hasParam = EvaluateRuntimeBindingOnce(
                    elem,
                    domains_,
                    program_,
                    uiInterpreter_,
                    bindingEngine_,
                    action.commandParameterBinding,
                    paramVal);
            } else if (action.hasCommandParameter) {
                paramVal = action.commandParameter;
                hasParam = true;
            } else {
                const UnifiedValue hostParam = elem->GetValue(PropId_CommandParameter);
                if (!std::holds_alternative<std::monostate>(hostParam)) {
                    paramVal = hostParam;
                    hasParam = true;
                }
            }

            // Coerce CommandParameter (e.g. XAML string literal "50" or enum name) to match target method's parameter type
            if (hasParam && program_ && domains_) {
                if (const auto* loc = program_->resolveFunction(methodSym)) {
                    if (loc->function && loc->function->parameterTypes.size() >= 2u) {
                        const auto pt = loc->function->parameterTypes[1u];
                        if (pt == rs_sem::PrimitiveType::Int || pt == rs_sem::PrimitiveType::Long ||
                            pt == rs_sem::PrimitiveType::Short || pt == rs_sem::PrimitiveType::Byte ||
                            pt == rs_sem::PrimitiveType::Enum) {
                            if (const auto* sv = std::get_if<StringValue>(&paramVal)) {
                                std::int64_t iv = 0;
                                const std::string_view v = sv->view();
                                const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), iv);
                                if (ec == std::errc{} && ptr == v.data() + v.size()) {
                                    paramVal = value::FromInt(iv);
                                } else if (pt == rs_sem::PrimitiveType::Enum) {
                                    bool resolvedEnum = false;
                                    for (const auto& mod : program_->modules()) {
                                        for (const auto& ts : mod.types) {
                                            if (ts.kind == rs_sem::TypeKind::Enum &&
                                                TryResolveEnumMemberByName(program_, ts.id, v, iv)) {
                                                paramVal = value::FromInt(iv);
                                                resolvedEnum = true;
                                                break;
                                            }
                                        }
                                        if (resolvedEnum) {
                                            break;
                                        }
                                    }
                                }
                            } else if (const auto* fv = std::get_if<FloatValue>(&paramVal)) {
                                paramVal = value::FromInt(static_cast<std::int64_t>(std::llround(fv->value)));
                            } else {
                                EnumValue ev{};
                                if (paramVal.tryGetEnum(ev)) {
                                    paramVal = value::FromInt(ev.value);
                                }
                            }
                        } else if (pt == rs_sem::PrimitiveType::Float) {
                            if (const auto* sv = std::get_if<StringValue>(&paramVal)) {
                                float fv = 0.0f;
                                if (TryParseStackFloat(sv->view(), fv)) {
                                    paramVal = MakeFloat(fv);
                                }
                            } else if (const auto* iv = std::get_if<std::int64_t>(&paramVal)) {
                                paramVal = MakeFloat(static_cast<float>(*iv));
                            }
                        } else if (pt == rs_sem::PrimitiveType::Bool) {
                            if (const auto* sv = std::get_if<StringValue>(&paramVal)) {
                                paramVal = EqualsAsciiIgnoreCase(sv->view(), "true") || sv->view() == "1";
                            }
                        } else if (pt == rs_sem::PrimitiveType::String) {
                            if (const auto* sv = std::get_if<StringValue>(&paramVal)) {
                                if (auto strObj = domains_->uiHeap().allocateString(sv->str())) {
                                    paramVal = *strObj;
                                }
                            }
                        }
                    }
                }
            }

            const bool livesOnSimHeap =
                domains_ && (targetObj.heapId == static_cast<std::uint32_t>(domains_->simHeap().heapId()));
            if ((isSimCmd || livesOnSimHeap) && domains_) {
                std::vector<UnifiedValue> payload;
                if (hasParam) {
                    payload.push_back(paramVal);
                }
                domains_->enqueueSimCommand(SimCommandEnvelope{
                    methodSym,
                    targetObj,
                    std::move(payload),
                    0ULL});
                break;
            }

            if (uiInterpreter_) {
                std::vector<UnifiedValue> args{targetObj};
                if (hasParam) {
                    if (const auto* sv = std::get_if<StringValue>(&paramVal)) {
                        if (domains_) {
                            if (auto strObj = domains_->uiHeap().allocateString(sv->str())) {
                                args.push_back(*strObj);
                            } else {
                                args.push_back(paramVal);
                            }
                        } else {
                            args.push_back(paramVal);
                        }
                    } else {
                        args.push_back(paramVal);
                    }
                }
                auto res = uiInterpreter_->invoke(methodSym, args);
                if (!res.succeeded && res.error.code == ErrorCode::InvalidArguments &&
                    res.statistics.instructionsExecuted == 0u && args.size() > 1u) {
                    args.resize(1u);
                    (void)uiInterpreter_->invoke(methodSym, args);
                }
            } else if (aotContext_) {
                std::vector<UnifiedValue> args{targetObj};
                if (hasParam) {
                    args.push_back(paramVal);
                }
                UnifiedValue ret{};
                if (!aotContext_->invoke(methodSym, args.data(), args.size(), ret) && args.size() > 1u) {
                    (void)aotContext_->invoke(methodSym, args.data(), 1u, ret);
                }
            }
            break;
        }

        case InteractionActionKind::PlaySound: {
            if (soundActionCallback_) {
                soundActionCallback_(elem, action.soundSource, action.volume);
            }
            break;
        }
    }
}

void StyleTriggerEngine::ExecuteInteractionActions(
    StyledElementInstance& inst,
    std::vector<AxbInteractionAction>& actions) {
    for (auto& act : actions) {
        ExecuteSingleInteractionAction(inst, act);
    }
}

bool StyleTriggerEngine::EvaluateEventTriggers(StyledElementInstance& inst) {
    UIElement* elem = inst.element.Get();
    if (!elem || inst.resolvedStyle.eventTriggers.empty()) {
        return false;
    }

    bool anyFired = false;
    const std::size_t limit = std::min(
        inst.resolvedStyle.eventTriggers.size(),
        kMaxEventTriggersPerElement);

    for (std::size_t i = 0u; i < limit; ++i) {
        auto& et = inst.resolvedStyle.eventTriggers[i];
        auto& rt = inst.eventStates[i];

        if (et.eventKind == RoutedTriggerEvent::StoryboardCompleted) {
            const AxbStoryboard* watchedSb = !et.sourceName.empty() ? FindStoryboard(et.sourceName) : nullptr;
            bool curSbActive = false;
            if (animationEngine_ && watchedSb) {
                curSbActive = animationEngine_->IsStoryboardKeyActive(elem, et.sourceName, *watchedSb);
                if (!curSbActive) {
                    if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(elem); scopeRoot && scopeRoot != elem) {
                        curSbActive = animationEngine_->IsStoryboardKeyActive(scopeRoot, et.sourceName, *watchedSb);
                    }
                }
            }
            bool fire = false;
            if (!rt.initialized) {
                rt.initialized = true;
                rt.prevStoryboardActive = curSbActive;
            } else {
                if (rt.prevStoryboardActive && !curSbActive) {
                    fire = true;
                }
                rt.prevStoryboardActive = curSbActive;
            }
            if (fire && !et.conditions.empty()) {
                for (auto& cond : et.conditions) {
                    if (!EvaluateSingleCondition(elem, cond)) {
                        fire = false;
                        break;
                    }
                }
            }
            if (fire) {
                if (animationEngine_) {
                    for (const auto& sb : et.storyboards) {
                        (void)animationEngine_->BeginStoryboard(elem, sb);
                    }
                }
                if (!et.actions.empty()) {
                    ExecuteInteractionActions(inst, et.actions);
                }
                anyFired = true;
            }
            continue;
        }

        if (!rt.cachedSource) {
            std::string lookupName = et.sourceName;
            if (et.eventKind == RoutedTriggerEvent::PropertyChanged) {
                const auto bar = et.sourceName.find('|');
                if (bar != std::string::npos) {
                    lookupName = et.sourceName.substr(0u, bar);
                    int parsedPid = 0;
                    const std::string_view pidSv = std::string_view(et.sourceName).substr(bar + 1u);
                    for (char ch : pidSv) {
                        if (ch >= '0' && ch <= '9') parsedPid = parsedPid * 10 + (ch - '0');
                    }
                    rt.watchedPropId = static_cast<std::uint16_t>(parsedPid);
                }
            }
            if (lookupName.empty()) {
                rt.cachedSource.Reset(elem, true);
            } else {
                UIElement* found = elem->FindName(lookupName);
                if (!found) {
                    if (UIElement* scopeRoot = UIElement::FindNamescopeRoot(elem)) {
                        found = scopeRoot->FindName(lookupName);
                    }
                }
                rt.cachedSource.Reset(found, true);
            }
        }
        UIElement* src = rt.cachedSource.Get();
        if (!src) {
            continue;
        }

        const bool curHovered = src->IsHovered();
        bool curChecked = false;
        const UnifiedValue chkVal = src->GetValue(PropId_IsChecked);
        if (const auto* b = std::get_if<bool>(&chkVal)) {
            curChecked = *b;
        }
        std::uint32_t curClickGen = 0u;
        if (const auto* btn = src->DynamicCast<Button>()) {
            curClickGen = btn->ClickGeneration();
        }
        std::int64_t curSelectedIdx = -1;
        const UnifiedValue selVal = src->GetValue(PropId_SelectedIndex);
        if (const auto* selI = std::get_if<std::int64_t>(&selVal)) {
            curSelectedIdx = *selI;
        }
        float curNumVal = 0.0f;
        const std::uint16_t numericPid =
            (et.eventKind == RoutedTriggerEvent::PropertyChanged && rt.watchedPropId != 0xFFFFu)
                ? rt.watchedPropId
                : static_cast<std::uint16_t>(PropId_Value);
        const UnifiedValue curWatchedVal = src->GetValue(numericPid);
        (void)TryGetFloat(curWatchedVal, curNumVal);

        bool fire = false;
        if (!rt.initialized) {
            rt.initialized = true;
            rt.lastClickGen = curClickGen;
            rt.prevHovered = curHovered;
            rt.prevChecked = curChecked;
            rt.prevSelectedIndex = curSelectedIdx;
            rt.prevNumericValue = curNumVal;
            rt.prevWatchedValue = curWatchedVal;
            if (et.eventKind == RoutedTriggerEvent::Loaded) {
                fire = true;
            } else if (et.eventKind == RoutedTriggerEvent::PropertyChanged &&
                       std::fabs(curNumVal) > 1e-5f) {
                fire = true;
            }
        } else {
            switch (et.eventKind) {
                case RoutedTriggerEvent::Loaded:
                case RoutedTriggerEvent::StoryboardCompleted:
                    break;
                case RoutedTriggerEvent::Click:
                    if (curClickGen != rt.lastClickGen) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::MouseEnter:
                    if (curHovered && !rt.prevHovered) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::MouseLeave:
                    if (!curHovered && rt.prevHovered) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::Checked:
                    if (curChecked && !rt.prevChecked) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::Unchecked:
                    if (!curChecked && rt.prevChecked) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::SelectionChanged:
                    if (curSelectedIdx != rt.prevSelectedIndex) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::ValueChanged:
                    if (std::fabs(curNumVal - rt.prevNumericValue) > 1e-5f) {
                        fire = true;
                    }
                    break;
                case RoutedTriggerEvent::PropertyChanged:
                    if (!UnifiedValuesEqual(curWatchedVal, rt.prevWatchedValue)) {
                        fire = true;
                    }
                    break;
            }
            rt.lastClickGen = curClickGen;
            rt.prevHovered = curHovered;
            rt.prevChecked = curChecked;
            rt.prevSelectedIndex = curSelectedIdx;
            rt.prevNumericValue = curNumVal;
            rt.prevWatchedValue = curWatchedVal;
        }

        if (fire && !et.conditions.empty()) {
            for (auto& cond : et.conditions) {
                if (!EvaluateSingleCondition(elem, cond)) {
                    fire = false;
                    break;
                }
            }
        }

        if (fire) {
            if (animationEngine_) {
                for (const auto& sb : et.storyboards) {
                    (void)animationEngine_->BeginStoryboard(elem, sb);
                }
            }
            if (!et.actions.empty()) {
                ExecuteInteractionActions(inst, et.actions);
            }
            anyFired = true;
        }
    }

    return anyFired;
}

void StyleTriggerEngine::ReapplyActiveOverrides(
    StyledElementInstance& inst,
    std::uint64_t newTriggerMask,
    bool useTransitions,
    bool isInitialPass) {
    UIElement* elem = inst.element.Get();
    if (!elem) {
        return;
    }

    const std::uint64_t prevMask = inst.activeTriggerMask;
    const std::uint64_t exitedBits = prevMask & (~newTriggerMask);
    const std::uint64_t enteredBits = (~prevMask) & newTriggerMask;

    for (auto& slot : inst.propertySnapshots) {
        slot.touchedThisPass = false;
    }

    auto findSnapshotSlot = [&](UIElement* target, std::uint16_t propId) -> PropertySnapshotSlot* {
        for (auto& slot : inst.propertySnapshots) {
            if (slot.resolvedTarget.Get() == target && slot.propId == propId) {
                return &slot;
            }
        }
        return nullptr;
    };

    // 1. Apply active VisualStates across VisualStateGroups (preserving live values for smooth transitions!)
    const std::size_t groupLimit = std::min(
        inst.resolvedStyle.visualStateGroups.size(),
        kMaxVisualStateGroupsPerElement);
    for (std::size_t g = 0u; g < groupLimit; ++g) {
        const auto& vsg = inst.resolvedStyle.visualStateGroups[g];
        const std::int16_t stIdx = inst.groupStates[g].activeStateIndex;
        if (stIdx < 0 || static_cast<std::size_t>(stIdx) >= vsg.states.size()) {
            continue;
        }
        const auto& vs = vsg.states[static_cast<std::size_t>(stIdx)];
        for (const auto& setter : vs.setters) {
            UIElement* target = setter.targetName.empty() ? elem : elem->FindName(setter.targetName);
            if (!target) continue;
            if (PropertySnapshotSlot* slot = findSnapshotSlot(target, setter.propId)) {
                CaptureSnapshotIfNeeded(*slot);
                slot->touchedThisPass = true;
            }
            if (useTransitions && !isInitialPass && animationEngine_ &&
                vsg.defaultTransitionDuration > 1e-4f) {
                (void)animationEngine_->StartValueTransition(
                    target,
                    setter.propId,
                    setter.value,
                    vsg.defaultTransitionDuration,
                    vsg.defaultEasingKind,
                    vsg.defaultEasingMode);
            } else {
                if (animationEngine_) {
                    (void)animationEngine_->StopPropertyAnimation(target, setter.propId, false);
                }
                (void)target->ForceEffectiveValue(setter.propId, setter.value, ValueSourceRank::TemplateTrigger);
            }
        }
        if (!vs.storyboard.timelines.empty() && animationEngine_) {
            for (const auto& tl : vs.storyboard.timelines) {
                UIElement* target = tl.targetName.empty() ? elem : elem->FindName(tl.targetName);
                if (!target) continue;
                if (PropertySnapshotSlot* slot = findSnapshotSlot(target, tl.targetPropertyId)) {
                    CaptureSnapshotIfNeeded(*slot);
                    slot->touchedThisPass = true;
                }
                if (!useTransitions || isInitialPass) {
                    if (!tl.keyFrames.empty()) {
                        (void)target->ForceEffectiveValue(
                            tl.targetPropertyId,
                            tl.keyFrames.back().value,
                            ValueSourceRank::Animation);
                    } else if (tl.hasTo) {
                        (void)target->ForceEffectiveValue(
                            tl.targetPropertyId,
                            tl.toValue,
                            ValueSourceRank::Animation);
                    }
                }
            }
            if (useTransitions && !isInitialPass) {
                (void)animationEngine_->BeginStoryboard(elem, vs.storyboard);
            }
        }
    }

    // 2. Apply active Triggers in declaration order (last active trigger wins, matching WPF/Noesis!)
    const std::size_t trigLimit = std::min<std::size_t>(inst.resolvedStyle.triggers.size(), 64u);
    for (std::size_t t = 0u; t < trigLimit; ++t) {
        const std::uint64_t bit = (1ULL << t);
        auto& trig = inst.resolvedStyle.triggers[t];

        if ((newTriggerMask & bit) != 0ULL) {
            const ValueSourceRank trigRank = trig.overrideLocalValues
                ? ValueSourceRank::TemplateTrigger
                : ValueSourceRank::StyleTrigger;
            for (const auto& setter : trig.setters) {
                UIElement* target = setter.targetName.empty() ? elem : elem->FindName(setter.targetName);
                if (!target) continue;
                PropertySnapshotSlot* slot = findSnapshotSlot(target, setter.propId);
                if (slot) {
                    CaptureSnapshotIfNeeded(*slot);
                    if (!trig.overrideLocalValues && slot->hadBaseValue && slot->baseRank > trigRank) {
                        continue;
                    }
                    slot->touchedThisPass = true;
                }
                (void)target->ForceEffectiveValue(setter.propId, setter.value, trigRank);
            }
        }

        if (animationEngine_ && !isInitialPass) {
            if ((exitedBits & bit) != 0ULL) {
                for (const auto& sb : trig.exitStoryboards) {
                    (void)animationEngine_->BeginStoryboard(elem, sb);
                }
            }
            if ((enteredBits & bit) != 0ULL) {
                for (const auto& sb : trig.enterStoryboards) {
                    (void)animationEngine_->BeginStoryboard(elem, sb);
                }
            }
        }

        // Execute XAML Behaviors (<b:DataTrigger>) imperative actions on rising edge (including initial pass if matched!)
        if (!isInitialPass && (exitedBits & bit) != 0ULL && !trig.exitActions.empty()) {
            ExecuteInteractionActions(inst, trig.exitActions);
        }
        if ((enteredBits & bit) != 0ULL && !trig.enterActions.empty()) {
            ExecuteInteractionActions(inst, trig.enterActions);
        }
    }

    // 3. Restore any captured property snapshot that is no longer overridden by any active VisualState or Trigger
    for (auto& slot : inst.propertySnapshots) {
        if (!slot.captured || slot.touchedThisPass || !slot.resolvedTarget) {
            continue;
        }
        if (animationEngine_) {
            (void)animationEngine_->StopPropertyAnimation(slot.resolvedTarget.Get(), slot.propId, false);
        }
        if (slot.hadBaseValue) {
            (void)slot.resolvedTarget->ForceEffectiveValue(
                slot.propId,
                slot.baseValue,
                slot.baseRank);
        } else if (IsFastInlinePropertyId(slot.propId)) {
            (void)slot.resolvedTarget->ClearValue(slot.propId, ValueSourceRank::Animation);
        } else {
            (void)slot.resolvedTarget->ForceClearSparseEntry(slot.propId);
        }
        if (UIElement* tp = slot.resolvedTarget->TemplatedParent()) {
            tp->SyncTemplateBindingsForTarget(slot.resolvedTarget.Get(), slot.propId);
        }
        slot.captured = false;
    }

    inst.activeTriggerMask = newTriggerMask;
}

bool StyleTriggerEngine::GoToVisualState(
    UIElement* element,
    std::string_view stateName,
    bool useTransitions) {
    if (!element || stateName.empty()) {
        return false;
    }
    const std::size_t instSnapshot = instances_.size();
    for (std::size_t i = 0u; i < instSnapshot && i < instances_.size(); ++i) {
        if (instances_[i].element.Get() != element) {
            continue;
        }
        const std::size_t groupLimit = std::min(
            instances_[i].resolvedStyle.visualStateGroups.size(),
            kMaxVisualStateGroupsPerElement);
        for (std::size_t g = 0u; g < groupLimit; ++g) {
            const std::int16_t foundIdx = FindStateIndexByName(
                instances_[i].resolvedStyle.visualStateGroups[g],
                stateName);
            if (foundIdx >= 0) {
                instances_[i].groupStates[g].manualOverride = true;
                if (instances_[i].groupStates[g].activeStateIndex != foundIdx || !instances_[i].initialized) {
                    instances_[i].groupStates[g].activeStateIndex = foundIdx;
                    instances_[i].initialized = true;
                    ReapplyActiveOverrides(instances_[i], instances_[i].activeTriggerMask, useTransitions, false);
                }
                return true;
            }
        }
    }
    return false;
}

std::size_t StyleTriggerEngine::EvaluateAndApply() {
    return EvaluateSubtree(nullptr, false);
}

std::size_t StyleTriggerEngine::EvaluateSubtree(UIElement* subtreeRoot, bool resetEventStates) {
    if (bindingEngine_) {
        domains_ = &bindingEngine_->Domains();
        if (bindingEngine_->Program()) program_ = bindingEngine_->Program();
        if (bindingEngine_->UiInterpreter()) uiInterpreter_ = bindingEngine_->UiInterpreter();
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        (domains_ && !UiResourceContext::HasActiveThreadOverride())
            ? UiResourceContext::FindForDomains(domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));

    std::size_t changedElements = 0u;

    auto isInSubtree = [&](const UIElement* candidate) noexcept -> bool {
        if (!subtreeRoot) {
            return true;
        }
        for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
            if (cur == subtreeRoot) {
                return true;
            }
        }
        return false;
    };

    for (int settlePass = 0; settlePass < 3; ++settlePass) {
        bool anyCascadeMutation = false;
        const std::size_t instSnapshotCount = instances_.size();
        for (std::size_t instIdx = 0u; instIdx < instSnapshotCount && instIdx < instances_.size(); ++instIdx) {
            UIElement* elem = instances_[instIdx].element.Get();
            if (!elem || !isInSubtree(elem)) {
                continue;
            }
            if (settlePass == 0 && resetEventStates) {
                instances_[instIdx].eventStates = {};
            }

            // Note: do NOT skip Collapsed elements if a DataTrigger on Visibility itself could un-collapse them!
            const bool eventFired = EvaluateEventTriggers(instances_[instIdx]);
            if (eventFired) {
                anyCascadeMutation = true;
            }

        // 1. Compute 64-bit active trigger bitmask
        std::uint64_t nextMask = 0ULL;
        const std::size_t trigLimit = std::min<std::size_t>(instances_[instIdx].resolvedStyle.triggers.size(), 64u);
        for (std::size_t t = 0u; t < trigLimit; ++t) {
            auto& trig = instances_[instIdx].resolvedStyle.triggers[t];
            if (trig.conditions.empty()) {
                continue;
            }
            bool allMet = true;
            for (auto& cond : trig.conditions) {
                if (!EvaluateSingleCondition(elem, cond)) {
                    allMet = false;
                    break;
                }
            }
            if (allMet) {
                nextMask |= (1ULL << t);
            }
        }

        // 2. Compute automatic CommonStates / FocusStates / CheckStates indices
        bool anyGroupChanged = false;
        const std::size_t groupLimit = std::min(
            instances_[instIdx].resolvedStyle.visualStateGroups.size(),
            kMaxVisualStateGroupsPerElement);
        for (std::size_t g = 0u; g < groupLimit; ++g) {
            if (instances_[instIdx].groupStates[g].manualOverride) {
                continue;
            }
            const auto& vsg = instances_[instIdx].resolvedStyle.visualStateGroups[g];
            std::int16_t desiredIdx = instances_[instIdx].groupStates[g].activeStateIndex;

            if (vsg.name == "CommonStates" || vsg.name.empty()) {
                if (!elem->IsEffectivelyEnabled()) {
                    desiredIdx = FindStateIndexByName(vsg, "Disabled");
                } else if (elem->IsPressed()) {
                    desiredIdx = FindStateIndexByName(vsg, "Pressed");
                } else if (elem->IsHovered()) {
                    desiredIdx = FindStateIndexByName(vsg, "MouseOver");
                } else {
                    desiredIdx = FindStateIndexByName(vsg, "Normal");
                }
            } else if (vsg.name == "FocusStates") {
                desiredIdx = elem->IsFocused()
                    ? FindStateIndexByName(vsg, "Focused")
                    : FindStateIndexByName(vsg, "Unfocused");
            } else if (vsg.name == "CheckStates") {
                bool checked = false;
                const UnifiedValue chkVal = elem->GetValue(PropId_IsChecked);
                if (const auto* b = std::get_if<bool>(&chkVal)) {
                    checked = *b;
                }
                desiredIdx = checked
                    ? FindStateIndexByName(vsg, "Checked")
                    : FindStateIndexByName(vsg, "Unchecked");
            }

            if (desiredIdx != instances_[instIdx].groupStates[g].activeStateIndex) {
                instances_[instIdx].groupStates[g].activeStateIndex = desiredIdx;
                anyGroupChanged = true;
            }
        }

        // O(1) clean-frame short-circuit!
        if (instances_[instIdx].initialized && nextMask == instances_[instIdx].activeTriggerMask && !anyGroupChanged) {
            if (eventFired) {
                ++changedElements;
            }
            continue;
        }

            const bool firstPass = !instances_[instIdx].initialized;
            const std::uint64_t prevMaskForCascade = instances_[instIdx].activeTriggerMask;
            instances_[instIdx].initialized = true;
            ReapplyActiveOverrides(instances_[instIdx], nextMask, !firstPass, firstPass);
            ++changedElements;
            if (nextMask != prevMaskForCascade || firstPass) {
                anyCascadeMutation = true;
            }
        }
        if (!anyCascadeMutation) {
            break;
        }
    }

    return changedElements;
}

} // namespace aero
