#include "aero/style/StyleTriggerEngine.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

#include "aero/controls/Controls.hpp"

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

[[nodiscard]] bool UnifiedValuesEqual(const UnifiedValue& a, const UnifiedValue& b) noexcept {
    if (const auto* ab = std::get_if<bool>(&a)) {
        if (const auto* bb = std::get_if<bool>(&b)) return *ab == *bb;
        if (const auto* bi = std::get_if<std::int64_t>(&b)) return *ab == (*bi != 0);
    }
    if (const auto* ai = std::get_if<std::int64_t>(&a)) {
        if (const auto* bi = std::get_if<std::int64_t>(&b)) return *ai == *bi;
        if (const auto* bb = std::get_if<bool>(&b)) return (*ai != 0) == *bb;
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
    return a.index() == b.index() && std::holds_alternative<std::monostate>(a);
}

// Zero-allocation value matcher supporting typed values, {x:Null}, heap ObjectRef(String),
// and untyped XAML string literals ("True", "100", "Visible", "Horde")!
[[nodiscard]] bool TriggerConditionValueMatches(
    const UnifiedValue& actualVal,
    const AxbTriggerCondition& cond,
    const ExecutionDomainContext* domains) noexcept {
    const bool actualIsNull =
        std::holds_alternative<std::monostate>(actualVal) ||
        std::holds_alternative<NullObject>(actualVal) ||
        (std::holds_alternative<ObjectRef>(actualVal) && !std::get<ObjectRef>(actualVal).valid());

    if (cond.expectNull) {
        return actualIsNull;
    }
    if (actualIsNull) {
        return false;
    }
    if (UnifiedValuesEqual(actualVal, cond.expectedValue)) {
        return true;
    }

    // Zero-allocation cross-domain RealScript String ObjectRef comparison via string_view
    if (const auto* objRef = std::get_if<ObjectRef>(&actualVal)) {
        if (objRef->kind == ObjectKind::String && domains) {
            if (const auto actualSv = domains->stringViewAcrossDomains(*objRef)) {
                if (const auto* expSv = std::get_if<StringValue>(&cond.expectedValue)) {
                    return *actualSv == expSv->view();
                }
            }
        }
        return false;
    }

    // Untyped XAML string literal comparison against bool, int64_t, float, or enum
    if (const auto* expStr = std::get_if<StringValue>(&cond.expectedValue)) {
        const std::string_view sv = expStr->view();
        if (const auto* ab = std::get_if<bool>(&actualVal)) {
            if (EqualsAsciiIgnoreCase(sv, "true") || sv == "1") return *ab;
            if (EqualsAsciiIgnoreCase(sv, "false") || sv == "0") return !(*ab);
            return false;
        }
        if (const auto* ai = std::get_if<std::int64_t>(&actualVal)) {
            std::int64_t parsedInt = 0;
            const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsedInt);
            if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
                return *ai == parsedInt;
            }
            if (EqualsAsciiIgnoreCase(sv, "Visible")) return *ai == 0;
            if (EqualsAsciiIgnoreCase(sv, "Hidden")) return *ai == 1;
            if (EqualsAsciiIgnoreCase(sv, "Collapsed")) return *ai == 2;
        }
        float af = 0.0f;
        float parsedFloat = 0.0f;
        if (TryGetFloat(actualVal, af) && TryParseStackFloat(sv, parsedFloat)) {
            return std::fabs(af - parsedFloat) <= 1e-4f;
        }
    }

    return false;
}

[[nodiscard]] UIElement* FindTreeRoot(UIElement* elem) noexcept {
    UIElement* cur = elem;
    while (cur && cur->Parent() != nullptr) {
        cur = cur->Parent();
    }
    return cur;
}

[[nodiscard]] ObjectRef ResolveEffectiveDataContext(const UIElement* elem) noexcept {
    for (const UIElement* cur = elem; cur != nullptr; cur = cur->Parent()) {
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
void VisitStyleValues(const AxbStyleDefinition& st, Fn&& fn) noexcept {
    for (const auto& s : st.setters) {
        if (s.value.isTraceable()) fn(s.value);
    }
    for (const auto& trig : st.triggers) {
        for (const auto& cond : trig.conditions) {
            if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
        }
        for (const auto& s : trig.setters) {
            if (s.value.isTraceable()) fn(s.value);
        }
        for (const auto& sb : trig.enterStoryboards) VisitStoryboardValues(sb, fn);
        for (const auto& sb : trig.exitStoryboards) VisitStoryboardValues(sb, fn);
    }
    for (const auto& et : st.eventTriggers) {
        for (const auto& sb : et.storyboards) VisitStoryboardValues(sb, fn);
    }
    for (const auto& vsg : st.visualStateGroups) {
        for (const auto& vs : vsg.states) {
            for (const auto& s : vs.setters) {
                if (s.value.isTraceable()) fn(s.value);
            }
            VisitStoryboardValues(vs.storyboard, fn);
        }
    }
}

} // namespace

StyleTriggerEngine::StyleTriggerEngine(
    AnimationEngine* animationEngine,
    DualTrackBindingEngine* bindingEngine)
    : animationEngine_(animationEngine) {
    SetBindingEngine(bindingEngine);
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkValuesInPools();
    });
}

StyleTriggerEngine::~StyleTriggerEngine() noexcept {
    rs_rt::unregisterExternalValuePoolRootMarker(this);
}

void StyleTriggerEngine::MarkValuesInPools() const noexcept {
    auto markFn = [](const UnifiedValue& v) noexcept {
        rs_rt::markValueInPools(v);
    };
    for (const auto& kv : keyedStyles_) {
        VisitStyleValues(kv.second, markFn);
    }
    for (const auto& kv : implicitTypeStyles_) {
        VisitStyleValues(kv.second, markFn);
    }
    for (const auto& kv : keyedStoryboards_) {
        VisitStoryboardValues(kv.second, markFn);
    }
    for (const auto& inst : instances_) {
        VisitStyleValues(inst.resolvedStyle, markFn);
        for (const auto& slot : inst.propertySnapshots) {
            if (slot.captured && slot.hadBaseValue && slot.baseValue.isTraceable()) {
                rs_rt::markValueInPools(slot.baseValue);
            }
        }
    }
}

void StyleTriggerEngine::Clear() {
    keyedStyles_.clear();
    implicitTypeStyles_.clear();
    keyedStoryboards_.clear();
    instances_.clear();
}

void StyleTriggerEngine::RegisterStyle(AxbStyleDefinition style) {
    VisitStyleValues(style, [](const UnifiedValue& v) noexcept {
        rs_rt::pinValueInPools(v);
    });
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
        for (const auto& setter : s->setters) {
            bool replaced = false;
            for (auto& existing : merged.setters) {
                if (existing.propId == setter.propId && existing.targetName == setter.targetName) {
                    existing.value = setter.value;
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
        if (s.resolvedTarget == target && s.propId == propId) {
            return;
        }
    }
    PropertySnapshotSlot slot{};
    slot.resolvedTarget = target;
    slot.propId = propId;
    inst.propertySnapshots.push_back(std::move(slot));
}

void StyleTriggerEngine::CaptureSnapshotIfNeeded(PropertySnapshotSlot& slot) {
    if (slot.captured || !slot.resolvedTarget) {
        return;
    }
    slot.captured = true;
    UIElement* target = slot.resolvedTarget;
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
            (void)animationEngine_->StopPropertyAnimation(slot.resolvedTarget, slot.propId, false);
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
        slot.captured = false;
        slot.touchedThisPass = false;
    }
}

bool StyleTriggerEngine::AttachStyle(UIElement* element, const AxbStyleDefinition& rawStyle) {
    if (!element) {
        return false;
    }

    AxbStyleDefinition style = FlattenStyleChain(rawStyle);

    // 1. Apply base <Style><Setter> values at ValueSourceRank::ThemeStyle without overwriting explicit Local values!
    for (const auto& setter : style.setters) {
        UIElement* target = setter.targetName.empty()
            ? element
            : element->FindName(setter.targetName);
        if (!target) {
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
    if (auto* ic = rootElement->DynamicCast<ItemsControl>()) {
        ic->AssignStyleEnginePointer(this);
    }

    std::size_t attachedCount = 0u;
    const std::string explicitKey = rootElement->GetStyleKey();
    if (!explicitKey.empty()) {
        if (AttachStyleByKey(rootElement, explicitKey)) {
            ++attachedCount;
        }
    } else if (applyImplicitTypeStyles) {
        const char* fullTypeName = rootElement->GetTypeClass()->typeName;
        if (fullTypeName) {
            if (const AxbStyleDefinition* imp = FindImplicitStyle(ShortTypeNameView(fullTypeName))) {
                if (AttachStyle(rootElement, *imp)) {
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
            ++removed;
            continue;
        }
        for (auto& rt : instances_[i].eventStates) {
            if (rt.cachedSource && isInSubtree(rt.cachedSource)) {
                rt.cachedSource = nullptr;
            }
        }
        for (auto& slot : instances_[i].propertySnapshots) {
            if (slot.resolvedTarget && isInSubtree(slot.resolvedTarget)) {
                slot.resolvedTarget = nullptr;
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
            if (cond.converter == ValueConverterKind::None && !cond.expectNull) {
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
            UIElement* root = FindTreeRoot(elem);
            UIElement* srcElem = root ? root->FindName(cond.sourceName) : nullptr;
            if (!srcElem) {
                return false;
            }
            actualVal = readElemPropOrPseudo(srcElem, cond.propId);
            break;
        }

        case TriggerConditionSource::AncestorProperty: {
            const UIElement* matchedAnc = nullptr;
            for (const UIElement* anc = elem->Parent(); anc != nullptr; anc = anc->Parent()) {
                const char* tn = anc->GetTypeClass()->typeName;
                if (tn && ShortTypeNameView(tn) == cond.sourceName) {
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
                auto* heap = domains_->heapForObject(curObj);
                if (!heap || !heap->isAlive(curObj)) {
                    chainOk = false;
                    break;
                }
                auto& hop = cond.compiledHops[h];
                const SymbolId runtimeTypeId = heap->objectTypeId(curObj).value_or(0);
                if (program_ && runtimeTypeId != 0 &&
                    (hop.declaringTypeId != runtimeTypeId || !hop.ToSlotHandle().valid())) {
                    if (auto resolved = domains_->resolvePropertySlot(
                            *program_, runtimeTypeId, hop.memberName)) {
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

                const PropertySlotHandle slot = hop.ToSlotHandle();
                UnifiedValue hopVal{};
                if (slot.valid()) {
                    if (!domains_->readPropertySlotFast(
                            DomainKind::Ui, curObj, slot, hopVal, nullptr, uiInterpreter_)) {
                        chainOk = false;
                        break;
                    }
                } else if (hop.fieldIndex != PropertySlotHandle::InvalidIndex) {
                    if (!domains_->readObjectField(
                            DomainKind::Ui, curObj, hop.fieldIndex, hopVal, nullptr)) {
                        chainOk = false;
                        break;
                    }
                } else {
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

    if (cond.converter != ValueConverterKind::None) {
        actualVal = ApplyBuiltinValueConverter(cond.converter, actualVal, false);
    }

    return TriggerConditionValueMatches(actualVal, cond, domains_);
}

bool StyleTriggerEngine::EvaluateEventTriggers(StyledElementInstance& inst) {
    UIElement* elem = inst.element.Get();
    if (!elem || !animationEngine_ || inst.resolvedStyle.eventTriggers.empty()) {
        return false;
    }

    bool anyFired = false;
    const std::size_t limit = std::min(
        inst.resolvedStyle.eventTriggers.size(),
        kMaxEventTriggersPerElement);

    for (std::size_t i = 0u; i < limit; ++i) {
        const auto& et = inst.resolvedStyle.eventTriggers[i];
        auto& rt = inst.eventStates[i];

        if (!rt.cachedSource) {
            if (et.sourceName.empty()) {
                rt.cachedSource = elem;
            } else {
                rt.cachedSource = elem->FindName(et.sourceName);
                if (!rt.cachedSource) {
                    if (UIElement* root = FindTreeRoot(elem)) {
                        rt.cachedSource = root->FindName(et.sourceName);
                    }
                }
            }
        }
        UIElement* src = rt.cachedSource;
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

        bool fire = false;
        if (!rt.initialized) {
            rt.initialized = true;
            rt.lastClickGen = curClickGen;
            rt.prevHovered = curHovered;
            rt.prevChecked = curChecked;
            if (et.eventKind == RoutedTriggerEvent::Loaded) {
                fire = true;
            }
        } else {
            switch (et.eventKind) {
                case RoutedTriggerEvent::Loaded:
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
            }
            rt.lastClickGen = curClickGen;
            rt.prevHovered = curHovered;
            rt.prevChecked = curChecked;
        }

        if (fire) {
            for (const auto& sb : et.storyboards) {
                (void)animationEngine_->BeginStoryboard(elem, sb);
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
            if (slot.resolvedTarget == target && slot.propId == propId) {
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
        const auto& trig = inst.resolvedStyle.triggers[t];

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
    }

    // 3. Restore any captured property snapshot that is no longer overridden by any active VisualState or Trigger
    for (auto& slot : inst.propertySnapshots) {
        if (!slot.captured || slot.touchedThisPass || !slot.resolvedTarget) {
            continue;
        }
        if (animationEngine_) {
            (void)animationEngine_->StopPropertyAnimation(slot.resolvedTarget, slot.propId, false);
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

    const std::size_t instSnapshotCount = instances_.size();
    for (std::size_t instIdx = 0u; instIdx < instSnapshotCount && instIdx < instances_.size(); ++instIdx) {
        UIElement* elem = instances_[instIdx].element.Get();
        if (!elem || !isInSubtree(elem)) {
            continue;
        }
        if (resetEventStates) {
            instances_[instIdx].eventStates = {};
        }

        // Note: do NOT skip Collapsed elements if a DataTrigger on Visibility itself could un-collapse them!
        const bool eventFired = EvaluateEventTriggers(instances_[instIdx]);

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
        instances_[instIdx].initialized = true;
        ReapplyActiveOverrides(instances_[instIdx], nextMask, !firstPass, firstPass);
        ++changedElements;
    }

    return changedElements;
}

} // namespace aero
