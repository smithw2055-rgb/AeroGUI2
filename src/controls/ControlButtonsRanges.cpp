#include "ControlsInternal.hpp"

#include "aero/asset/AxbFormat.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aero {

AERO_IMPLEMENT_CLASS(ButtonBase, ContentControl, "Aero.UI.ButtonBase")
AERO_IMPLEMENT_CLASS(ToggleButton, ButtonBase, "Aero.UI.ToggleButton")
AERO_IMPLEMENT_CLASS(CheckBox, ToggleButton, "Aero.UI.CheckBox")
AERO_IMPLEMENT_CLASS(RadioButton, ToggleButton, "Aero.UI.RadioButton")
AERO_IMPLEMENT_CLASS(Button, ButtonBase, "Aero.UI.Button")
AERO_IMPLEMENT_CLASS(RepeatButton, ButtonBase, "Aero.UI.RepeatButton")
AERO_IMPLEMENT_CLASS(Hyperlink, ButtonBase, "Aero.UI.Hyperlink")

AERO_IMPLEMENT_CLASS(RangeBase, Control, "Aero.UI.RangeBase")
AERO_IMPLEMENT_CLASS(Slider, RangeBase, "Aero.UI.Slider")
AERO_IMPLEMENT_CLASS(ProgressBar, RangeBase, "Aero.UI.ProgressBar")
AERO_IMPLEMENT_CLASS(GridSplitter, Thumb, "Aero.UI.GridSplitter")

namespace {

Size MeasureToggleIndicatorControl(ContentControl& ctrl, Size availableSize) noexcept {
    if (ctrl.HasControlTemplate()) {
        return ctrl.UIElement::MeasureCore(availableSize);
    }
    const Thickness p = ctrl.GetPadding();
    if (ctrl.FirstChild()) {
        const Size childAvail{
            std::max(0.0f, availableSize.width - (26.0f + p.left + p.right)),
            std::max(0.0f, availableSize.height - (p.top + p.bottom))};
        const Size childSize = ctrl.UIElement::MeasureCore(childAvail);
        return Size{
            childSize.width + 26.0f + p.left + p.right,
            std::max(22.0f, childSize.height) + p.top + p.bottom};
    }
    float fontSize = 14.0f;
    (void)TryGetFloat(ctrl.GetValue(PropId_FontSize), fontSize);
    const std::string label = ctrl.GetContentText();
    const Size labelMetrics = MeasureUtf8TextRun(label, fontSize);
    const float indicatorW = 18.0f;
    const float gap = label.empty() ? 0.0f : 8.0f;
    return Size{
        indicatorW + gap + labelMetrics.width + p.left + p.right,
        std::max(22.0f, labelMetrics.height) + p.top + p.bottom};
}

void ArrangeToggleIndicatorControl(UIElement& ctrl, Rect finalRect) noexcept {
    if (ctrl.HasControlTemplate()) {
        ctrl.UIElement::ArrangeCore(finalRect);
        return;
    }
    if (!ctrl.FirstChild()) {
        return;
    }
    const Thickness p = ctrl.GetPadding();
    const float startX = finalRect.x + p.left + 26.0f;
    const float startY = finalRect.y + p.top;
    const float contentW = std::max(0.0f, finalRect.width - (p.left + p.right + 26.0f));
    const float contentH = std::max(0.0f, finalRect.height - (p.top + p.bottom));
    for (UIElement* c = ctrl.FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{startX, startY, contentW, contentH});
    }
}

void ResolveRoutedCommandTargets(
    const UIElement* anc,
    ObjectRef& outWrapObj,
    ObjectRef& outDataContextObj) noexcept {
    outWrapObj = anc ? anc->BoundScriptWrapper() : ObjectRef{};
    outDataContextObj = ObjectRef{};
    for (const UIElement* w = anc; w != nullptr; w = w->Parent() ? w->Parent() : w->TemplatedParent()) {
        if (!outWrapObj.valid() && w->BoundScriptWrapper().valid()) {
            outWrapObj = w->BoundScriptWrapper();
        }
        if (!outDataContextObj.valid()) {
            const UnifiedValue dcv = w->GetValue(PropId_DataContext);
            if (const auto* r = std::get_if<ObjectRef>(&dcv); r && r->valid()) {
                outDataContextObj = *r;
            }
        }
    }
}

void AppendTraceableFieldIndex(
    rs_sem::PrimitiveType type,
    std::size_t index,
    std::vector<std::size_t>& outRefIndices) {
    if (type == rs_sem::PrimitiveType::Object ||
        type == rs_sem::PrimitiveType::String ||
        type == rs_sem::PrimitiveType::Array) {
        outRefIndices.push_back(index);
    }
}

UnifiedValue AllocateRoutedCommandSender(
    ExecutionDomainContext& domains,
    const rs_sem::TypeSymbol* feSym,
    NativeHandle nativeHandle,
    RuntimeError& err) {
    if (!feSym) {
        return UnifiedValue{NullObject{}};
    }
    std::vector<UnifiedValue> sf(feSym->fields.size(), UnifiedValue(NullObject{}));
    std::vector<std::size_t> srf;
    for (std::size_t fi = 0u; fi < feSym->fields.size(); ++fi) {
        if (feSym->fields[fi].name == "__nativeRootHandle") {
            sf[fi] = UnifiedValue(nativeHandle);
        }
        AppendTraceableFieldIndex(feSym->fields[fi].type, fi, srf);
    }
    if (auto sObj = domains.uiHeap().allocateObject(feSym->id, std::move(sf), std::move(srf), &err)) {
        return *sObj;
    }
    return UnifiedValue{NullObject{}};
}

std::optional<ObjectRef> AllocateRoutedEventArgsObject(
    ExecutionDomainContext& domains,
    const rs_sem::TypeSymbol* argsSym,
    NativeHandle nativeHandle,
    std::string_view commandName,
    const UnifiedValue& parameter,
    bool hasParameter,
    bool isCanExecute,
    std::size_t* outCanExecIdx,
    RuntimeError& err) {
    if (outCanExecIdx) {
        *outCanExecIdx = static_cast<std::size_t>(-1);
    }
    if (!argsSym) {
        return std::nullopt;
    }
    std::vector<UnifiedValue> ef(argsSym->fields.size(), UnifiedValue(NullObject{}));
    std::vector<std::size_t> erf;
    for (std::size_t fi = 0u; fi < argsSym->fields.size(); ++fi) {
        const std::string& fn = argsSym->fields[fi].name;
        if (fn == "OriginalSource") {
            ef[fi] = UnifiedValue(nativeHandle);
        } else if (fn == "Handled") {
            ef[fi] = UnifiedValue(false);
        } else if (isCanExecute && fn == "CanExecute") {
            ef[fi] = UnifiedValue(true);
            if (outCanExecIdx) {
                *outCanExecIdx = fi;
            }
        } else if (fn == "Command") {
            if (auto sr = domains.uiHeap().allocateString(std::string(commandName), &err)) {
                ef[fi] = *sr;
            }
        } else if (fn == "Parameter") {
            const std::string ps = hasParameter
                ? FormatBindingString({}, &parameter, 1u, &domains)
                : std::string();
            if (auto sr = domains.uiHeap().allocateString(ps, &err)) {
                ef[fi] = *sr;
            }
        }
        AppendTraceableFieldIndex(argsSym->fields[fi].type, fi, erf);
    }
    return domains.uiHeap().allocateObject(argsSym->id, std::move(ef), std::move(erf), &err);
}

void FindAeroUiRoutedTypes(
    const ProgramImage& prog,
    std::string_view argsTypeName,
    const rs_sem::TypeSymbol*& outArgsSym,
    const rs_sem::TypeSymbol*& outFeSym) noexcept {
    outArgsSym = nullptr;
    outFeSym = nullptr;
    for (const auto& mod : prog.modules()) {
        if (mod.name != "Aero.UI") {
            continue;
        }
        for (const auto& ts : mod.types) {
            if (ts.name == argsTypeName) {
                outArgsSym = &ts;
            } else if (ts.name == "FrameworkElement") {
                outFeSym = &ts;
            }
        }
    }
}

void AdjustGridTrackPair(
    GridUnitType& firstUnit,
    float& firstVal,
    float firstComputed,
    GridUnitType& secondUnit,
    float& secondVal,
    float secondComputed,
    float delta,
    float fallbackPx) noexcept {
    const bool firstWasStar = (firstUnit == GridUnitType::Star);
    const bool secondWasStar = (secondUnit == GridUnitType::Star);
    float firstPx = (firstUnit == GridUnitType::Pixel)
        ? firstVal
        : ((firstComputed > 0.0f) ? firstComputed : fallbackPx);
    float secondPx = (secondUnit == GridUnitType::Pixel)
        ? secondVal
        : ((secondComputed > 0.0f) ? secondComputed : fallbackPx);
    const float totalPairPx = firstPx + secondPx;
    firstPx = std::clamp(firstPx + delta, 20.0f, std::max(20.0f, totalPairPx - 20.0f));
    secondPx = std::max(20.0f, totalPairPx - firstPx);
    if (!firstWasStar && secondWasStar) {
        firstUnit = GridUnitType::Pixel;
        firstVal = firstPx;
    } else if (firstWasStar && !secondWasStar) {
        secondUnit = GridUnitType::Pixel;
        secondVal = secondPx;
    } else {
        firstUnit = GridUnitType::Pixel;
        firstVal = firstPx;
        secondUnit = GridUnitType::Pixel;
        secondVal = secondPx;
    }
}

} // namespace

// ============================================================================
// aero::detail Helpers
// ============================================================================

namespace detail {

void UncheckNamedRadioGroupRecursive(
    UIElement* node,
    const RadioButton* activeRadio,
    std::string_view groupName,
    bool pushTwoWay) {
    if (!node) {
        return;
    }
    if (auto* rb = node->DynamicCast<RadioButton>()) {
        if (rb != activeRadio && rb->GetGroupName() == groupName && rb->IsChecked()) {
            rb->SetIsChecked(false);
            if (pushTwoWay) {
                (void)rb->NotifyTwoWayPropertyMutated(PropId_IsChecked);
            }
        }
    }
    for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
        UncheckNamedRadioGroupRecursive(c, activeRadio, groupName, pushTwoWay);
    }
}

std::string_view RoutedCommandLeafName(std::string_view raw) noexcept {
    if (const auto colon = raw.rfind(':'); colon != std::string_view::npos) {
        raw = raw.substr(colon + 1u);
    }
    if (const auto dot = raw.rfind('.'); dot != std::string_view::npos) {
        raw = raw.substr(dot + 1u);
    }
    return raw;
}

DualTrackBindingEngine* FindEngineNear(const UIElement* start) noexcept {
    for (const UIElement* a = start; a != nullptr; a = a->Parent() ? a->Parent() : a->TemplatedParent()) {
        if (a->BoundTwoWayEngine()) {
            return a->BoundTwoWayEngine();
        }
    }
    return nullptr;
}

StyleTriggerEngine* FindStyleEngineNear(const UIElement* start) noexcept {
    for (const UIElement* a = start; a != nullptr; a = a->Parent() ? a->Parent() : a->TemplatedParent()) {
        if (a->BoundStyleEngine()) {
            return a->BoundStyleEngine();
        }
    }
    return nullptr;
}

SymbolId FindMethodIdByNameOnObj(
    ExecutionDomainContext& domains,
    const ProgramImage& prog,
    ObjectRef obj,
    std::string_view methodName) noexcept {
    if (!obj.valid() || methodName.empty()) {
        return 0;
    }
    const auto* heap = domains.heapForObject(obj);
    if (!heap || !heap->isAlive(obj)) {
        return 0;
    }
    SymbolId tid = heap->objectTypeId(obj).value_or(0);
    for (int d = 0; d < 16 && tid != 0; ++d) {
        const auto* ts = prog.resolveType(tid);
        if (!ts) {
            break;
        }
        for (const auto& m : ts->methods) {
            if (m.name == methodName && !m.staticMethod && m.id != 0) {
                return m.id;
            }
        }
        tid = ts->baseTypeId;
    }
    return 0;
}

} // namespace detail

// ============================================================================
// RoutedCommand Bubble Query & Execution
// ============================================================================

bool QueryRoutedCommandCanExecuteBubble(
    const UIElement* source,
    std::string_view commandName,
    const UnifiedValue& parameter,
    bool hasParameter,
    bool& outHandled) {
    outHandled = false;
    if (!source || commandName.empty()) {
        return true;
    }
    const std::string_view wantLeaf = detail::RoutedCommandLeafName(commandName);
    DualTrackBindingEngine* be = detail::FindEngineNear(source);
    StyleTriggerEngine* se = detail::FindStyleEngineNear(source);
    if (!be || !be->Program()) {
        return true;
    }
    const ProgramImage* prog = be->Program();
    const Interpreter* interp = be->UiInterpreter();
    rs_aot::ExecutionContext* aotCtx = se ? se->GetAotExecutionContext() : nullptr;

    for (const UIElement* anc = source; anc != nullptr; anc = anc->Parent() ? anc->Parent() : anc->TemplatedParent()) {
        const UnifiedValue specVal = anc->GetValue(PropId_CommandBindingsSpec);
        const auto* sv = std::get_if<StringValue>(&specVal);
        if (!sv || sv->view().empty()) {
            continue;
        }

        ObjectRef wrapObj{};
        ObjectRef dcObj{};
        ResolveRoutedCommandTargets(anc, wrapObj, dcObj);

        std::string_view lines = sv->view();
        std::size_t pos = 0u;
        while (pos < lines.size()) {
            const std::size_t nl = lines.find('\n', pos);
            const std::string_view rec = (nl == std::string_view::npos) ? lines.substr(pos) : lines.substr(pos, nl - pos);
            pos = (nl == std::string_view::npos) ? lines.size() : (nl + 1u);
            const std::size_t p0 = rec.find('|');
            if (p0 == std::string_view::npos) {
                continue;
            }
            const std::size_t p1 = rec.find('|', p0 + 1u);
            if (p1 == std::string_view::npos) {
                continue;
            }
            const std::string_view recCmd = rec.substr(0u, p0);
            if (recCmd != commandName && detail::RoutedCommandLeafName(recCmd) != wantLeaf) {
                continue;
            }

            outHandled = true;
            const std::string_view canExecName = rec.substr(p1 + 1u);
            if (canExecName.empty()) {
                return true;
            }

            ObjectRef target = wrapObj;
            SymbolId mSym = detail::FindMethodIdByNameOnObj(be->Domains(), *prog, wrapObj, canExecName);
            if (mSym == 0 && dcObj.valid()) {
                mSym = detail::FindMethodIdByNameOnObj(be->Domains(), *prog, dcObj, canExecName);
                target = dcObj;
            }
            if (mSym == 0 || !target.valid()) {
                return true;
            }

            const auto* fnLoc = prog->resolveFunction(mSym);
            const std::size_t pCount = (fnLoc && fnLoc->function) ? fnLoc->function->parameterTypes.size() : 1u;
            if (pCount <= 2u) {
                std::vector<UnifiedValue> args{target};
                if (pCount == 2u) {
                    const std::string pStr = hasParameter
                        ? FormatBindingString({}, &parameter, 1u, &be->Domains())
                        : std::string();
                    if (auto sr = be->Domains().uiHeap().allocateString(pStr)) {
                        args.push_back(*sr);
                    }
                }
                if (interp) {
                    const auto res = interp->invoke(mSym, args);
                    if (res.succeeded && std::holds_alternative<bool>(res.value)) {
                        return std::get<bool>(res.value);
                    }
                } else if (aotCtx) {
                    UnifiedValue ret{};
                    if (aotCtx->invoke(mSym, args.data(), args.size(), ret) && std::holds_alternative<bool>(ret)) {
                        return std::get<bool>(ret);
                    }
                }
                return true;
            }

            const rs_sem::TypeSymbol* ceSym = nullptr;
            const rs_sem::TypeSymbol* feSym = nullptr;
            FindAeroUiRoutedTypes(*prog, "CanExecuteRoutedEventArgs", ceSym, feSym);
            if (!ceSym) {
                return true;
            }
            RuntimeError aErr{};
            const NativeHandle nh = const_cast<UIElement*>(source)->ToNativeHandle(be->Domains().nativeHandles());
            const UnifiedValue senderVal = AllocateRoutedCommandSender(be->Domains(), feSym, nh, aErr);
            std::size_t canExecIdx = static_cast<std::size_t>(-1);
            auto eObj = AllocateRoutedEventArgsObject(
                be->Domains(),
                ceSym,
                nh,
                commandName,
                parameter,
                hasParameter,
                true,
                &canExecIdx,
                aErr);
            if (!eObj) {
                return true;
            }
            if (interp) {
                (void)interp->invoke(mSym, {target, senderVal, *eObj});
            } else if (aotCtx) {
                UnifiedValue callArgs[3]{target, senderVal, *eObj};
                UnifiedValue ret{};
                (void)aotCtx->invoke(mSym, callArgs, 3u, ret);
            }
            if (canExecIdx != static_cast<std::size_t>(-1) && be->Domains().uiHeap().isAlive(*eObj)) {
                if (const auto cv = be->Domains().uiHeap().fieldGet(*eObj, canExecIdx)) {
                    if (const auto* cb = std::get_if<bool>(&*cv)) {
                        return *cb;
                    }
                }
            }
            return true;
        }
    }
    return true;
}

bool TryExecuteRoutedCommandBubble(
    UIElement* source,
    std::string_view commandName,
    const UnifiedValue& parameter,
    bool hasParameter) {
    if (!source || commandName.empty()) {
        return false;
    }
    bool canHandled = false;
    if (!QueryRoutedCommandCanExecuteBubble(source, commandName, parameter, hasParameter, canHandled)) {
        return false;
    }
    const std::string_view wantLeaf = detail::RoutedCommandLeafName(commandName);
    DualTrackBindingEngine* be = detail::FindEngineNear(source);
    StyleTriggerEngine* se = detail::FindStyleEngineNear(source);
    if (!be || !be->Program()) {
        return false;
    }
    const ProgramImage* prog = be->Program();
    const Interpreter* interp = be->UiInterpreter();
    rs_aot::ExecutionContext* aotCtx = se ? se->GetAotExecutionContext() : nullptr;

    for (UIElement* anc = source; anc != nullptr; anc = anc->Parent() ? anc->Parent() : anc->TemplatedParent()) {
        const UnifiedValue specVal = anc->GetValue(PropId_CommandBindingsSpec);
        const auto* sv = std::get_if<StringValue>(&specVal);
        if (!sv || sv->view().empty()) {
            continue;
        }

        ObjectRef wrapObj{};
        ObjectRef dcObj{};
        ResolveRoutedCommandTargets(anc, wrapObj, dcObj);

        std::string_view lines = sv->view();
        std::size_t pos = 0u;
        while (pos < lines.size()) {
            const std::size_t nl = lines.find('\n', pos);
            const std::string_view rec = (nl == std::string_view::npos) ? lines.substr(pos) : lines.substr(pos, nl - pos);
            pos = (nl == std::string_view::npos) ? lines.size() : (nl + 1u);
            const std::size_t p0 = rec.find('|');
            if (p0 == std::string_view::npos) {
                continue;
            }
            const std::size_t p1 = rec.find('|', p0 + 1u);
            if (p1 == std::string_view::npos) {
                continue;
            }
            const std::string_view recCmd = rec.substr(0u, p0);
            if (recCmd != commandName && detail::RoutedCommandLeafName(recCmd) != wantLeaf) {
                continue;
            }

            const std::string_view execName = rec.substr(p0 + 1u, p1 - (p0 + 1u));
            if (execName.empty()) {
                continue;
            }

            ObjectRef target = wrapObj;
            SymbolId mSym = detail::FindMethodIdByNameOnObj(be->Domains(), *prog, wrapObj, execName);
            if (mSym == 0 && dcObj.valid()) {
                mSym = detail::FindMethodIdByNameOnObj(be->Domains(), *prog, dcObj, execName);
                target = dcObj;
            }
            if (mSym == 0 || !target.valid()) {
                continue;
            }

            const auto* fnLoc = prog->resolveFunction(mSym);
            const std::size_t pCount = (fnLoc && fnLoc->function) ? fnLoc->function->parameterTypes.size() : 1u;
            if (pCount == 1u) {
                if (interp) {
                    (void)interp->invoke(mSym, {target});
                } else if (aotCtx) {
                    UnifiedValue a[1]{target};
                    UnifiedValue ret{};
                    (void)aotCtx->invoke(mSym, a, 1u, ret);
                }
            } else if (pCount == 2u) {
                const std::string ps = hasParameter
                    ? FormatBindingString({}, &parameter, 1u, &be->Domains())
                    : std::string();
                UnifiedValue pArg{NullObject{}};
                if (auto sr = be->Domains().uiHeap().allocateString(ps)) {
                    pArg = *sr;
                }
                if (interp) {
                    (void)interp->invoke(mSym, {target, pArg});
                } else if (aotCtx) {
                    UnifiedValue a[2]{target, pArg};
                    UnifiedValue ret{};
                    (void)aotCtx->invoke(mSym, a, 2u, ret);
                }
            } else {
                const rs_sem::TypeSymbol* exSym = nullptr;
                const rs_sem::TypeSymbol* feSym = nullptr;
                FindAeroUiRoutedTypes(*prog, "ExecutedRoutedEventArgs", exSym, feSym);
                if (exSym) {
                    RuntimeError aErr{};
                    const NativeHandle nh = source->ToNativeHandle(be->Domains().nativeHandles());
                    const UnifiedValue senderVal = AllocateRoutedCommandSender(be->Domains(), feSym, nh, aErr);
                    if (auto eObj = AllocateRoutedEventArgsObject(
                            be->Domains(),
                            exSym,
                            nh,
                            commandName,
                            parameter,
                            hasParameter,
                            false,
                            nullptr,
                            aErr)) {
                        if (interp) {
                            (void)interp->invoke(mSym, {target, senderVal, *eObj});
                        } else if (aotCtx) {
                            UnifiedValue a[3]{target, senderVal, *eObj};
                            UnifiedValue ret{};
                            (void)aotCtx->invoke(mSym, a, 3u, ret);
                        }
                    }
                }
            }
            if (DualTrackBindingEngine::IsLiveInstance(be)) {
                (void)be->FlushTrackA();
            }
            return true;
        }
    }
    return false;
}

// ============================================================================
// ButtonBase
// ============================================================================

ButtonBase::ButtonBase() {
    EnsureControlTypesRegistered();
}

namespace detail {

bool DispatchClickCommandAndScript(
    UIElement& owner,
    const ScriptClickBinding& clickBinding,
    RuntimeError* error) {
    (void)owner.DispatchWrapperRoutedEvent("Click");
    const UnifiedValue routedCmdVal = owner.GetValue(PropId_RoutedCommandName);
    if (const auto* rcmdSv = std::get_if<StringValue>(&routedCmdVal);
        rcmdSv && !rcmdSv->view().empty()) {
        const UnifiedValue cp = owner.GetValue(PropId_CommandParameter);
        const bool hasCp = !std::holds_alternative<std::monostate>(cp);
        (void)TryExecuteRoutedCommandBubble(&owner, rcmdSv->view(), cp, hasCp);
    }
    return clickBinding.Invoke(owner, error);
}

} // namespace detail

bool ScriptClickBinding::Invoke(const UIElement& owner, RuntimeError* error) const {
    if (!targetObject.valid() || methodId == 0) {
        return true;
    }
    std::vector<UnifiedValue> args;
    args.push_back(targetObject);
    const UnifiedValue cmdParam = owner.GetValue(PropId_CommandParameter);
    if (!std::holds_alternative<std::monostate>(cmdParam)) {
        args.push_back(cmdParam);
    }
    if (interpreter) {
        auto res = interpreter->invoke(methodId, args);
        // Only retry for arity mismatch if 0 bytecode instructions ran (entry signature check),
        // never re-entering a handler that already started executing and failed internally.
        if (!res.succeeded &&
            res.error.code == ErrorCode::InvalidArguments &&
            res.statistics.instructionsExecuted == 0u) {
            if (args.size() == 1u) {
                args.push_back(UnifiedValue{NullObject{}});
                args.push_back(UnifiedValue{NullObject{}});
                res = interpreter->invoke(methodId, args);
                if (!res.succeeded &&
                    res.error.code == ErrorCode::InvalidArguments &&
                    res.statistics.instructionsExecuted == 0u) {
                    args.resize(1u);
                    args.push_back(value::FromInt(0));
                    res = interpreter->invoke(methodId, args);
                }
            } else if (args.size() > 1u) {
                args.resize(1u);
                res = interpreter->invoke(methodId, args);
                if (!res.succeeded &&
                    res.error.code == ErrorCode::InvalidArguments &&
                    res.statistics.instructionsExecuted == 0u) {
                    args.push_back(UnifiedValue{NullObject{}});
                    args.push_back(UnifiedValue{NullObject{}});
                    res = interpreter->invoke(methodId, args);
                }
            }
        }
        if (!res.succeeded) {
            if (error) {
                *error = res.error;
            }
            return false;
        }
    } else if (aotContext) {
        const auto& prog = aotContext->program();
        for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
            const auto& fn = prog.functions[i];
            if (fn.symbolId == methodId) {
                if (args.size() < fn.parameterCount && fn.parameterTypes && fn.parameterTypeIds) {
                    while (args.size() < fn.parameterCount) {
                        const std::size_t pIdx = args.size();
                        args.push_back(aotContext->defaultValue(
                            fn.parameterTypes[pIdx],
                            fn.parameterTypeIds[pIdx]));
                    }
                } else if (args.size() > fn.parameterCount && fn.parameterCount >= 1u) {
                    args.resize(fn.parameterCount);
                }
                break;
            }
        }
        UnifiedValue outVal{};
        const bool ok = aotContext->invoke(
            methodId,
            args.data(),
            args.size(),
            outVal);
        if (!ok) {
            if (error) {
                *error = aotContext->error();
            }
            return false;
        }
    }
    return true;
}

bool ButtonBase::PerformClick(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    IntrusivePtr<ButtonBase> selfGuard(this, true);
    ++clickGeneration_;
    NotifyBubbledClick();
    if (clickCallback_) {
        clickCallback_(*this);
    }
    return detail::DispatchClickCommandAndScript(*this, clickBinding_, error);
}

bool ButtonBase::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    return PerformClick(error);
}

bool ButtonBase::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)localPoint;
    (void)modifiers;
    if (button == PointerButton::Left && IsEffectivelyEnabled()) {
        outRequestCapture = true;
        const UnifiedValue cmVal = GetValue(PropId_ClickMode);
        if (const auto* iv = std::get_if<std::int64_t>(&cmVal); iv && *iv == 1) {
            (void)ActivateFromInput();
        }
        return true;
    }
    outRequestCapture = false;
    return false;
}

bool ButtonBase::OnPointerUp(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool insideBounds) {
    (void)localPoint;
    (void)modifiers;
    if (button == PointerButton::Left && insideBounds && IsEffectivelyEnabled()) {
        const UnifiedValue cmVal = GetValue(PropId_ClickMode);
        if (const auto* iv = std::get_if<std::int64_t>(&cmVal); iv && (*iv == 1 || *iv == 2)) {
            return true;
        }
        return ActivateFromInput();
    }
    return false;
}

bool ButtonBase::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        return ActivateFromInput();
    }
    return false;
}

Size ButtonBase::MeasureCore(Size availableSize) noexcept {
    if (FirstChild()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const auto text = GetContentText();
    const std::size_t codePoints = rs_rt::utf8CodePointLength(text);
    const float textW = static_cast<float>(codePoints) * 8.0f + 16.0f;
    const float textH = 24.0f;
    return Size{
        textW + p.left + p.right,
        textH + p.top + p.bottom};
}

// ============================================================================
// ToggleButton, CheckBox, RadioButton
// ============================================================================

ToggleButton::ToggleButton() {
    EnsureControlTypesRegistered();
}

void ToggleButton::SetIsChecked(bool checked) {
    SetValue(PropId_IsChecked, UnifiedValue{checked}, ValueSourceRank::Local);
}

bool ToggleButton::IsChecked() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsChecked);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return *i != 0;
    }
    return false;
}

void ToggleButton::SetIsThreeState(bool threeState) {
    SetValue(PropId_IsThreeState, UnifiedValue{threeState}, ValueSourceRank::Local);
}

bool ToggleButton::IsThreeState() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsThreeState), false);
}

void ToggleButton::Toggle() {
    const UnifiedValue curVal = GetValue(PropId_IsChecked);
    if (IsThreeState()) {
        if (std::holds_alternative<NullObject>(curVal)) {
            SetIsChecked(false);
        } else if (IsChecked()) {
            SetValue(PropId_IsChecked, UnifiedValue{NullObject{}}, ValueSourceRank::Local);
        } else {
            SetIsChecked(true);
        }
    } else {
        SetIsChecked(!IsChecked());
    }
    const bool isInd = std::holds_alternative<NullObject>(GetValue(PropId_IsChecked));
    const char* evN = isInd ? "Indeterminate" : (IsChecked() ? "Checked" : "Unchecked");
    (void)DispatchWrapperRoutedEvent(evN, value::FromBool(IsChecked()), true);
    (void)NotifyTwoWayPropertyMutated(PropId_IsChecked);
}

bool ToggleButton::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    Toggle();
    return ButtonBase::ActivateFromInput(error);
}

CheckBox::CheckBox() {
    EnsureControlTypesRegistered();
}

void CheckBox::Toggle() {
    ToggleButton::Toggle();
    const bool checked = IsChecked();
    if (checkedChangedCallback_) {
        checkedChangedCallback_(*this, checked);
    }
    for (const auto& extraCb : extraCheckedCallbacks_) {
        if (extraCb) {
            extraCb(*this, checked);
        }
    }
}

bool CheckBox::ActivateFromInput(RuntimeError* error) {
    return ToggleButton::ActivateFromInput(error);
}

Size CheckBox::MeasureCore(Size availableSize) noexcept {
    return MeasureToggleIndicatorControl(*this, availableSize);
}

void CheckBox::ArrangeCore(Rect finalRect) noexcept {
    ArrangeToggleIndicatorControl(*this, finalRect);
}

RadioButton::RadioButton() {
    EnsureControlTypesRegistered();
}

void RadioButton::SetIsChecked(bool checked) {
    ToggleButton::SetIsChecked(checked);
}

void RadioButton::SetGroupName(std::string_view groupName) {
    SetValue(PropId_GroupName, value::FromString(std::string(groupName)), ValueSourceRank::Local);
}

std::string RadioButton::GetGroupName() const {
    const UnifiedValue v = GetValue(PropId_GroupName);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void RadioButton::UncheckMutualExclusionPeers(bool pushTwoWayOnUncheckedPeers) {
    if (uncheckingPeers_) {
        return;
    }
    uncheckingPeers_ = true;
    const std::string group = GetGroupName();
    if (group.empty()) {
        if (UIElement* parent = Parent()) {
            for (UIElement* sib = parent->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                if (auto* rb = sib->DynamicCast<RadioButton>()) {
                    if (rb != this && rb->GetGroupName().empty() && rb->IsChecked()) {
                        rb->SetIsChecked(false);
                        if (pushTwoWayOnUncheckedPeers) {
                            (void)rb->NotifyTwoWayPropertyMutated(PropId_IsChecked);
                        }
                    }
                }
            }
        }
    } else {
        UIElement* root = this;
        while (root->Parent() != nullptr) {
            root = root->Parent();
        }
        detail::UncheckNamedRadioGroupRecursive(root, this, group, pushTwoWayOnUncheckedPeers);
    }
    uncheckingPeers_ = false;
}

void RadioButton::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ToggleButton::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsChecked && IsChecked() && !uncheckingPeers_) {
        UncheckMutualExclusionPeers(false);
    }
}

bool RadioButton::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (!IsChecked()) {
        uncheckingPeers_ = true;
        SetIsChecked(true);
        uncheckingPeers_ = false;
        UncheckMutualExclusionPeers(true);
        if (checkedChangedCallback_) {
            checkedChangedCallback_(*this, true);
        }
        (void)NotifyTwoWayPropertyMutated(PropId_IsChecked);
    }
    return ButtonBase::ActivateFromInput(error);
}

Size RadioButton::MeasureCore(Size availableSize) noexcept {
    return MeasureToggleIndicatorControl(*this, availableSize);
}

void RadioButton::ArrangeCore(Rect finalRect) noexcept {
    ArrangeToggleIndicatorControl(*this, finalRect);
}

// ============================================================================
// Button, RepeatButton, Hyperlink
// ============================================================================

Button::Button() {
    EnsureControlTypesRegistered();
}

RepeatButton::RepeatButton() {
    EnsureControlTypesRegistered();
    SetValue(PropId_ClickMode, value::FromInt(1), ValueSourceRank::Default);
}

bool RepeatButton::ActivateFromInput(RuntimeError* error) {
    const bool ok = ButtonBase::ActivateFromInput(error);
    const UnifiedValue cmdVal = GetValue(PropId_RoutedCommandName);
    if (const auto* sv = std::get_if<StringValue>(&cmdVal); sv && !sv->view().empty()) {
        const std::string_view cmd = sv->view();
        ScrollViewer* targetSv = nullptr;
        for (UIElement* anc = Parent(); anc != nullptr && !targetSv; anc = anc->Parent()) {
            targetSv = anc->DynamicCast<ScrollViewer>();
            if (!targetSv) {
                for (UIElement* sib = anc->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                    if (auto* found = sib->DynamicCast<ScrollViewer>()) {
                        targetSv = found;
                        break;
                    }
                }
            }
        }
        if (targetSv) {
            if (cmd.find("LineLeft") != std::string_view::npos) {
                targetSv->ScrollBy(-24.0f, 0.0f);
            } else if (cmd.find("LineRight") != std::string_view::npos) {
                targetSv->ScrollBy(24.0f, 0.0f);
            } else if (cmd.find("LineUp") != std::string_view::npos) {
                targetSv->ScrollBy(0.0f, -24.0f);
            } else if (cmd.find("LineDown") != std::string_view::npos) {
                targetSv->ScrollBy(0.0f, 24.0f);
            } else if (cmd.find("PageLeft") != std::string_view::npos) {
                targetSv->ScrollBy(-120.0f, 0.0f);
            } else if (cmd.find("PageRight") != std::string_view::npos) {
                targetSv->ScrollBy(120.0f, 0.0f);
            } else if (cmd.find("PageUp") != std::string_view::npos) {
                targetSv->ScrollBy(0.0f, -120.0f);
            } else if (cmd.find("PageDown") != std::string_view::npos) {
                targetSv->ScrollBy(0.0f, 120.0f);
            }
        }
    }
    return ok;
}

bool RepeatButton::OnPointerMove(Point localPoint, std::uint8_t modifiers) {
    (void)localPoint;
    (void)modifiers;
    const UnifiedValue cmVal = GetValue(PropId_ClickMode);
    if (const auto* iv = std::get_if<std::int64_t>(&cmVal); iv && *iv == 2 && IsEffectivelyEnabled()) {
        (void)ActivateFromInput();
        return true;
    }
    return false;
}

Hyperlink::Hyperlink() {
    EnsureControlTypesRegistered();
    (void)SetValue(PropId_Foreground, value::FromColor(Color{0.31f, 0.68f, 0.98f, 1.0f}), ValueSourceRank::Default);
    (void)SetValue(PropId_Background, value::FromColor(Color{0.0f, 0.0f, 0.0f, 0.0f}), ValueSourceRank::Default);
    (void)SetValue(PropId_BorderThickness, value::FromThickness(Thickness{0.0f}), ValueSourceRank::Default);
    (void)SetValue(PropId_Padding, value::FromThickness(Thickness{2.0f, 1.0f, 2.0f, 1.0f}), ValueSourceRank::Default);
    (void)SetValue(PropId_TextDecorations, value::FromInt(1), ValueSourceRank::Default);
}

void Hyperlink::SetNavigateUri(std::string_view uri) {
    SetValue(PropId_NavigateUri, value::FromString(std::string(uri)), ValueSourceRank::Local);
}

std::string Hyperlink::GetNavigateUri() const {
    const UnifiedValue v = GetValue(PropId_NavigateUri);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

bool Hyperlink::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const bool clicked = ButtonBase::ActivateFromInput(error);
    const std::string uri = GetNavigateUri();
    if (requestNavigateCallback_) {
        requestNavigateCallback_(*this, uri);
    }
    (void)DispatchWrapperRoutedEvent("RequestNavigate", value::FromString(uri), true);
    return clicked || !uri.empty();
}

// ============================================================================
// RangeBase
// ============================================================================

RangeBase::RangeBase() {
    EnsureControlTypesRegistered();
}

void RangeBase::SetMinimum(float minVal) {
    SetValue(PropId_Minimum, MakeFloat(minVal), ValueSourceRank::Local);
}

float RangeBase::GetMinimum() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Minimum), out) && std::isfinite(out)) {
        return out;
    }
    return 0.0f;
}

void RangeBase::SetMaximum(float maxVal) {
    SetValue(PropId_Maximum, MakeFloat(maxVal), ValueSourceRank::Local);
}

float RangeBase::GetMaximum() const noexcept {
    float out = 100.0f;
    if (TryGetFloat(GetValue(PropId_Maximum), out) && std::isfinite(out)) {
        return std::max(GetMinimum(), out);
    }
    return std::max(GetMinimum(), 100.0f);
}

void RangeBase::SetRangeValue(float val) {
    SetValue(PropId_Value, MakeFloat(val), ValueSourceRank::Local);
}

float RangeBase::ClampValueToRange(float rawVal) const noexcept {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    if (!std::isfinite(rawVal)) {
        return minV;
    }
    return std::clamp(rawVal, minV, maxV);
}

float RangeBase::GetRangeValue() const noexcept {
    const float minV = GetMinimum();
    float out = minV;
    if (TryGetFloat(GetValue(PropId_Value), out) && std::isfinite(out)) {
        return ClampValueToRange(out);
    }
    return minV;
}

void RangeBase::SetSmallChange(float step) {
    SetValue(PropId_SmallChange, MakeFloat(std::max(1e-5f, step)), ValueSourceRank::Local);
}

float RangeBase::GetSmallChange() const noexcept {
    float out = 1.0f;
    if (TryGetFloat(GetValue(PropId_SmallChange), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    const float span = GetMaximum() - GetMinimum();
    return (span > 0.0f && span <= 2.0f) ? 0.05f : 1.0f;
}

void RangeBase::SetLargeChange(float step) {
    SetValue(PropId_LargeChange, MakeFloat(std::max(1e-5f, step)), ValueSourceRank::Local);
}

float RangeBase::GetLargeChange() const noexcept {
    float out = 10.0f;
    if (TryGetFloat(GetValue(PropId_LargeChange), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    return GetSmallChange() * 10.0f;
}

void RangeBase::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Value || propId == PropId_Minimum || propId == PropId_Maximum) {
        const float newVal = GetRangeValue();
        if (std::fabs(newVal - lastNotifiedValue_) > 1e-6f) {
            const float oldVal = lastNotifiedValue_;
            lastNotifiedValue_ = newVal;
            OnValueChanged(oldVal, newVal);
        }
    }
}

void RangeBase::OnValueChanged(float oldVal, float newVal) {
    (void)oldVal;
    (void)newVal;
}

// ============================================================================
// Slider
// ============================================================================

Slider::Slider() {
    EnsureControlTypesRegistered();
}

void Slider::SetSliderValue(float val) {
    SetRangeValue(val);
}

float Slider::GetSliderValue() const noexcept {
    return GetRangeValue();
}

bool Slider::SetSliderValueInteractive(float val) {
    float clamped = ClampValueToRange(val);
    if (IsSnapToTickEnabled()) {
        clamped = SnapValueToTicks(clamped);
    }
    const float prev = GetSliderValue();
    SetSliderValue(clamped);
    if (std::fabs(clamped - prev) > 1e-6f) {
        if (valueChangedCallback_) {
            valueChangedCallback_(*this, clamped);
        }
        (void)DispatchWrapperRoutedEvent("ValueChanged", MakeFloat(clamped), true);
        (void)NotifyTwoWayPropertyMutated(PropId_Value);
        return true;
    }
    return false;
}

void Slider::SetOrientation(Orientation o) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(o)),
        ValueSourceRank::Local);
}

Orientation Slider::GetOrientation() const noexcept {
    if (const auto* entry = FindCompactEntry(PropId_Orientation)) {
        if (const auto* i = std::get_if<std::int64_t>(&entry->effectiveValue)) {
            return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
        }
    }
    return Orientation::Horizontal;
}

void Slider::SetIsDirectionReversed(bool reversed) {
    SetValue(PropId_IsDirectionReversed, UnifiedValue{reversed}, ValueSourceRank::Local);
}

bool Slider::IsDirectionReversed() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsDirectionReversed);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void Slider::SetIsSnapToTickEnabled(bool snap) {
    SetValue(PropId_IsSnapToTickEnabled, UnifiedValue{snap}, ValueSourceRank::Local);
}

bool Slider::IsSnapToTickEnabled() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsSnapToTickEnabled);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void Slider::SetTickFrequency(float freq) {
    SetValue(PropId_TickFrequency, MakeFloat(std::max(0.0f, freq)), ValueSourceRank::Local);
}

float Slider::GetTickFrequency() const noexcept {
    float out = 1.0f;
    if (TryGetFloat(GetValue(PropId_TickFrequency), out) && std::isfinite(out) && out > 0.0f) {
        return out;
    }
    return 1.0f;
}

void Slider::SetTickPlacement(std::int32_t placement) {
    SetValue(PropId_TickPlacement, value::FromInt(std::clamp<std::int32_t>(placement, 0, 3)), ValueSourceRank::Local);
}

std::int32_t Slider::GetTickPlacement() const noexcept {
    const UnifiedValue v = GetValue(PropId_TickPlacement);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return std::clamp<std::int32_t>(static_cast<std::int32_t>(*i), 0, 3);
    }
    return 0;
}

void Slider::SetTicks(std::string_view ticksSpec) {
    SetValue(PropId_Ticks, value::FromString(std::string(ticksSpec)), ValueSourceRank::Local);
}

std::string Slider::GetTicks() const {
    const UnifiedValue v = GetValue(PropId_Ticks);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void Slider::CollectNormalizedTicks(std::vector<float>& outTicks) const {
    outTicks.clear();
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float span = maxV - minV;
    if (span <= 1e-6f) {
        return;
    }
    const bool rev = IsDirectionReversed();

    const UnifiedValue ticksVal = GetValue(PropId_Ticks);
    if (const auto* sv = std::get_if<StringValue>(&ticksVal); sv && !sv->view().empty()) {
        std::string buf(sv->view());
        for (char& ch : buf) {
            if (ch == ',' || ch == ';') {
                ch = ' ';
            }
        }
        std::istringstream iss(buf);
        float tv = 0.0f;
        while (iss >> tv) {
            if (std::isfinite(tv) && tv >= minV - 1e-4f && tv <= maxV + 1e-4f) {
                const float norm = std::clamp((tv - minV) / span, 0.0f, 1.0f);
                outTicks.push_back(rev ? (1.0f - norm) : norm);
            }
        }
        if (!outTicks.empty()) {
            return;
        }
    }

    const float freq = GetTickFrequency();
    if (freq > 1e-5f) {
        const std::size_t maxSteps = std::min<std::size_t>(128u, static_cast<std::size_t>(std::floor(span / freq)) + 2u);
        outTicks.reserve(maxSteps);
        for (std::size_t k = 0u; k < maxSteps; ++k) {
            const float tv = minV + static_cast<float>(k) * freq;
            if (tv > maxV + 1e-4f) {
                break;
            }
            const float norm = std::clamp((tv - minV) / span, 0.0f, 1.0f);
            outTicks.push_back(rev ? (1.0f - norm) : norm);
        }
        const float lastNorm = rev ? 0.0f : 1.0f;
        if (outTicks.empty() || std::fabs(outTicks.back() - lastNorm) > 1e-3f) {
            outTicks.push_back(lastNorm);
        }
    }
}

void Slider::SetIsMoveToPointEnabled(bool enabled) {
    SetValue(PropId_IsMoveToPointEnabled, UnifiedValue{enabled}, ValueSourceRank::Local);
}

bool Slider::IsMoveToPointEnabled() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsMoveToPointEnabled);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return true;
}

float Slider::SnapValueToTicks(float rawVal) const noexcept {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float clamped = std::clamp(rawVal, minV, maxV);
    const UnifiedValue ticksVal = GetValue(PropId_Ticks);
    if (const auto* sv = std::get_if<StringValue>(&ticksVal); sv && !sv->view().empty()) {
        std::string buf(sv->view());
        for (char& ch : buf) {
            if (ch == ',' || ch == ';') {
                ch = ' ';
            }
        }
        std::istringstream iss(buf);
        float best = clamped;
        float bestDist = 1e30f;
        float tv = 0.0f;
        while (iss >> tv) {
            if (std::isfinite(tv)) {
                const float tc = std::clamp(tv, minV, maxV);
                const float d = std::fabs(tc - clamped);
                if (d < bestDist) {
                    bestDist = d;
                    best = tc;
                }
            }
        }
        if (bestDist < 1e29f) {
            return best;
        }
    }
    const float freq = GetTickFrequency();
    if (freq > 1e-5f) {
        const float steps = std::round((clamped - minV) / freq);
        const float snapped = minV + steps * freq;
        if (std::fabs(maxV - clamped) < std::fabs(snapped - clamped)) {
            return maxV;
        }
        return std::clamp(snapped, minV, maxV);
    }
    return clamped;
}

float Slider::NormalizedPosition() const noexcept {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float span = maxV - minV;
    if (span <= 1e-6f) {
        return 0.0f;
    }
    return std::clamp((GetSliderValue() - minV) / span, 0.0f, 1.0f);
}

float Slider::VisualNormalizedPosition() const noexcept {
    const float t = NormalizedPosition();
    return IsDirectionReversed() ? (1.0f - t) : t;
}

bool Slider::ShouldSkipControlTemplatePackage(const AxbPackage& package) const noexcept {
    bool hasTrack = false;
    bool hasIndicatorOrThumb = false;
    for (const auto& s : package.stringTable) {
        if (s == "Track") {
            hasTrack = true;
        } else if (s == "PART_Indicator" || s == "PART_Thumb") {
            hasIndicatorOrThumb = true;
            break;
        }
    }
    return hasTrack && !hasIndicatorOrThumb;
}

bool Slider::UpdateValueFromLocalPoint(Point localPt) {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const Orientation orient = GetOrientation();
    float t = 0.0f;
    if (orient == Orientation::Horizontal) {
        const float trackX = r.x + p.left;
        const float trackW = std::max(1.0f, r.width - (p.left + p.right));
        t = std::clamp((localPt.x - trackX) / trackW, 0.0f, 1.0f);
    } else {
        const float trackY = r.y + p.top;
        const float trackH = std::max(1.0f, r.height - (p.top + p.bottom));
        // Standard vertical slider: bottom = Minimum (t=0), top = Maximum (t=1)
        t = std::clamp(1.0f - ((localPt.y - trackY) / trackH), 0.0f, 1.0f);
    }
    if (IsDirectionReversed()) {
        t = 1.0f - t;
    }
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    return SetSliderValueInteractive(minV + t * (maxV - minV));
}

bool Slider::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        outRequestCapture = false;
        return false;
    }
    const UnifiedValue explicitMoveVal = GetValue(PropId_IsMoveToPointEnabled);
    const bool explicitlyDisabledMoveToPoint =
        std::holds_alternative<bool>(explicitMoveVal) && !std::get<bool>(explicitMoveVal);
    if (explicitlyDisabledMoveToPoint) {
        const Rect r = FinalRect();
        const float w = (r.width > 0.0f) ? r.width : std::max(60.0f, GetWidth());
        const float h = (r.height > 0.0f) ? r.height : std::max(20.0f, GetHeight());
        const float normPos = VisualNormalizedPosition();
        const bool isVert = (GetOrientation() == Orientation::Vertical);
        const float thumbCenter = isVert
            ? (r.y + 8.0f + (1.0f - normPos) * std::max(0.0f, h - 16.0f))
            : (r.x + 8.0f + normPos * std::max(0.0f, w - 16.0f));
        const float clickCoord = isVert ? localPoint.y : localPoint.x;
        if (std::fabs(clickCoord - thumbCenter) > 10.0f) {
            float dir = (clickCoord > thumbCenter) ? 1.0f : -1.0f;
            if (isVert) {
                dir = -dir;
            }
            if (IsDirectionReversed()) {
                dir = -dir;
            }
            (void)SetSliderValueInteractive(GetSliderValue() + dir * GetLargeChange());
            isDragging_ = false;
            outRequestCapture = false;
            InvalidateRender();
            return true;
        }
    }
    SetPressed(true);
    isDragging_ = true;
    outRequestCapture = true;
    (void)UpdateValueFromLocalPoint(localPoint);
    InvalidateRender();
    return true;
}

bool Slider::OnPointerMove(Point localPoint, std::uint8_t modifiers) {
    (void)modifiers;
    if (!isDragging_ || !IsPressed() || !IsEffectivelyEnabled()) {
        isDragging_ = false;
        return false;
    }
    (void)UpdateValueFromLocalPoint(localPoint);
    return true;
}

bool Slider::OnPointerUp(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool insideBounds) {
    (void)modifiers;
    (void)insideBounds;
    if (button == PointerButton::Left && isDragging_) {
        (void)UpdateValueFromLocalPoint(localPoint);
        isDragging_ = false;
        InvalidateRender();
        return true;
    }
    return false;
}

void Slider::OnPointerCaptureLost() noexcept {
    if (isDragging_) {
        isDragging_ = false;
        InvalidateRender();
    }
}

void Slider::OnEnabledStateChanged(bool effectivelyEnabled) {
    if (!effectivelyEnabled && isDragging_) {
        isDragging_ = false;
        InvalidateRender();
    }
}

bool Slider::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const float cur = GetSliderValue();
    const Orientation orient = GetOrientation();
    const float dirSign = IsDirectionReversed() ? -1.0f : 1.0f;
    const float step = (IsSnapToTickEnabled() && GetTickFrequency() > 1e-5f) ? GetTickFrequency() : GetSmallChange();
    switch (key) {
        case KeyCode::Right:
            if (orient == Orientation::Horizontal) {
                (void)SetSliderValueInteractive(cur + dirSign * step);
                return true;
            }
            return false;
        case KeyCode::Left:
            if (orient == Orientation::Horizontal) {
                (void)SetSliderValueInteractive(cur - dirSign * step);
                return true;
            }
            return false;
        case KeyCode::Up:
            if (orient == Orientation::Vertical) {
                (void)SetSliderValueInteractive(cur + dirSign * step);
                return true;
            }
            return false;
        case KeyCode::Down:
            if (orient == Orientation::Vertical) {
                (void)SetSliderValueInteractive(cur - dirSign * step);
                return true;
            }
            return false;
        case KeyCode::PageUp:
            (void)SetSliderValueInteractive(cur + GetLargeChange());
            return true;
        case KeyCode::PageDown:
            (void)SetSliderValueInteractive(cur - GetLargeChange());
            return true;
        case KeyCode::Home:
            (void)SetSliderValueInteractive(GetMinimum());
            return true;
        case KeyCode::End:
            (void)SetSliderValueInteractive(GetMaximum());
            return true;
        default:
            return false;
    }
}

void Slider::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    RangeBase::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Value || propId == PropId_Minimum || propId == PropId_Maximum ||
        propId == PropId_IsDirectionReversed) {
        SyncTemplateParts();
    }
}

void Slider::SyncTemplateParts() noexcept {
    if (!HasControlTemplate()) {
        return;
    }
    UIElement* indicator = GetTemplateChild("PART_Indicator");
    UIElement* thumb = GetTemplateChild("PART_Thumb");
    if (!indicator && !thumb) {
        return;
    }
    UIElement* track = GetTemplateChild("PART_Track");
    const float t = VisualNormalizedPosition();
    if (GetOrientation() == Orientation::Vertical) {
        float fullH = 0.0f;
        if (track && track->FinalRect().height > 0.0f) {
            fullH = track->FinalRect().height;
        } else if (track && track->GetHeight() > 0.0f) {
            fullH = track->GetHeight();
        } else if (FinalRect().height > 0.0f) {
            fullH = FinalRect().height;
        } else if (GetHeight() > 0.0f) {
            fullH = GetHeight();
        }
        if (fullH > 0.0f) {
            if (indicator) {
                (void)indicator->SetValue(
                    PropId_VerticalAlignment,
                    value::FromInt(static_cast<std::int64_t>(VerticalAlignment::Bottom)),
                    ValueSourceRank::ThemeStyle);
                if (!indicator->SetValue(PropId_Height, MakeFloat(fullH * t), ValueSourceRank::ThemeStyle)) {
                    (void)indicator->SetValue(PropId_Height, MakeFloat(fullH * t), ValueSourceRank::Local);
                }
            }
            if (thumb) {
                float thumbH = 0.0f;
                if (thumb->GetHeight() > 0.0f) {
                    thumbH = thumb->GetHeight();
                } else if (thumb->FinalRect().height > 0.0f) {
                    thumbH = thumb->FinalRect().height;
                } else {
                    thumbH = thumb->DesiredSize().height;
                }

                const float trackOffsetY =
                    (track && track->FinalRect().height > 0.0f &&
                     thumb->Parent() && thumb->Parent() != track && thumb->Parent()->FinalRect().height > 0.0f)
                        ? std::max(0.0f, track->FinalRect().y - thumb->Parent()->FinalRect().y)
                        : 0.0f;
                const float offsetTop = trackOffsetY + (1.0f - t) * std::max(0.0f, fullH - thumbH);

                if (thumb->Parent() && thumb->Parent()->IsA<Canvas>()) {
                    if (!thumb->SetValue(PropId_CanvasTop, MakeFloat(offsetTop), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_CanvasTop, MakeFloat(offsetTop), ValueSourceRank::Local);
                    }
                } else {
                    (void)thumb->SetValue(
                        PropId_VerticalAlignment,
                        value::FromInt(static_cast<std::int64_t>(VerticalAlignment::Top)),
                        ValueSourceRank::ThemeStyle);
                    Thickness m = thumb->GetMargin();
                    m.top = offsetTop;
                    m.bottom = 0.0f;
                    if (!thumb->SetValue(PropId_Margin, value::FromThickness(m), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_AnimTranslateY, MakeFloat(offsetTop), ValueSourceRank::ThemeStyle);
                    }
                }
            }
        }
    } else {
        float fullW = 0.0f;
        if (track && track->FinalRect().width > 0.0f) {
            fullW = track->FinalRect().width;
        } else if (track && track->GetWidth() > 0.0f) {
            fullW = track->GetWidth();
        } else if (FinalRect().width > 0.0f) {
            fullW = FinalRect().width;
        } else if (GetWidth() > 0.0f) {
            fullW = GetWidth();
        }
        if (fullW > 0.0f) {
            if (indicator) {
                (void)indicator->SetValue(
                    PropId_HorizontalAlignment,
                    value::FromInt(static_cast<std::int64_t>(HorizontalAlignment::Left)),
                    ValueSourceRank::ThemeStyle);
                if (!indicator->SetValue(PropId_Width, MakeFloat(fullW * t), ValueSourceRank::ThemeStyle)) {
                    (void)indicator->SetValue(PropId_Width, MakeFloat(fullW * t), ValueSourceRank::Local);
                }
            }
            if (thumb) {
                float thumbW = 0.0f;
                if (thumb->GetWidth() > 0.0f) {
                    thumbW = thumb->GetWidth();
                } else if (thumb->FinalRect().width > 0.0f) {
                    thumbW = thumb->FinalRect().width;
                } else {
                    thumbW = thumb->DesiredSize().width;
                }

                const float trackOffsetX =
                    (track && track->FinalRect().width > 0.0f &&
                     thumb->Parent() && thumb->Parent() != track && thumb->Parent()->FinalRect().width > 0.0f)
                        ? std::max(0.0f, track->FinalRect().x - thumb->Parent()->FinalRect().x)
                        : 0.0f;
                const float offsetLeft = trackOffsetX + t * std::max(0.0f, fullW - thumbW);

                if (thumb->Parent() && thumb->Parent()->IsA<Canvas>()) {
                    if (!thumb->SetValue(PropId_CanvasLeft, MakeFloat(offsetLeft), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_CanvasLeft, MakeFloat(offsetLeft), ValueSourceRank::Local);
                    }
                } else {
                    (void)thumb->SetValue(
                        PropId_HorizontalAlignment,
                        value::FromInt(static_cast<std::int64_t>(HorizontalAlignment::Left)),
                        ValueSourceRank::ThemeStyle);
                    Thickness m = thumb->GetMargin();
                    m.left = offsetLeft;
                    m.right = 0.0f;
                    if (!thumb->SetValue(PropId_Margin, value::FromThickness(m), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_AnimTranslateX, MakeFloat(offsetLeft), ValueSourceRank::ThemeStyle);
                    }
                }
            }
        }
    }
}

Size Slider::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        const Size tplSize = UIElement::MeasureCore(availableSize);
        if (tplSize.width > 0.0f || tplSize.height > 0.0f) {
            return tplSize;
        }
    }
    const Thickness p = GetPadding();
    if (GetOrientation() == Orientation::Vertical) {
        return Size{24.0f + p.left + p.right, 120.0f + p.top + p.bottom};
    }
    return Size{120.0f + p.left + p.right, 24.0f + p.top + p.bottom};
}

void Slider::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        SyncTemplateParts();
        if (UIElement* tplRoot = TemplateRoot()) {
            tplRoot->Measure(Size{finalRect.width, finalRect.height});
            tplRoot->Arrange(finalRect);
        }
    }
}

// ============================================================================
// ProgressBar
// ============================================================================

ProgressBar::ProgressBar() {
    EnsureControlTypesRegistered();
}

void ProgressBar::SetProgressValue(float val) {
    SetRangeValue(val);
}

float ProgressBar::GetProgressValue() const noexcept {
    return GetRangeValue();
}

void ProgressBar::SetOrientation(Orientation o) {
    SetValue(PropId_Orientation, value::FromInt(static_cast<std::int64_t>(o)), ValueSourceRank::Local);
}

Orientation ProgressBar::GetOrientation() const noexcept {
    const UnifiedValue v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_Orientation, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            return (*pi == 0) ? Orientation::Vertical : Orientation::Horizontal;
        }
    }
    return Orientation::Horizontal;
}

void ProgressBar::SetIsIndeterminate(bool indeterminate) {
    SetValue(PropId_IsIndeterminate, value::FromBool(indeterminate), ValueSourceRank::Local);
}

bool ProgressBar::IsIndeterminate() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsIndeterminate), false);
}

float ProgressBar::NormalizedProgress() const noexcept {
    if (IsIndeterminate()) {
        return 0.35f;
    }
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float curV = GetProgressValue();
    if (maxV <= minV) {
        return 0.0f;
    }
    return std::clamp((curV - minV) / (maxV - minV), 0.0f, 1.0f);
}

Rect ProgressBar::ComputeFillBarLocalRect() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float innerX = r.x + p.left;
    const float innerY = r.y + p.top;
    const float innerW = std::max(0.0f, r.width - (p.left + p.right));
    const float innerH = std::max(0.0f, r.height - (p.top + p.bottom));
    const float ratio = NormalizedProgress();

    if (IsIndeterminate()) {
        if (GetOrientation() == Orientation::Vertical) {
            const float segH = innerH * 0.35f;
            return Rect{innerX, innerY + (innerH - segH) * 0.5f, innerW, segH};
        }
        const float segW = innerW * 0.35f;
        return Rect{innerX + (innerW - segW) * 0.5f, innerY, segW, innerH};
    }

    if (GetOrientation() == Orientation::Vertical) {
        const float fillH = innerH * ratio;
        return Rect{innerX, innerY + (innerH - fillH), innerW, fillH};
    }
    return Rect{innerX, innerY, innerW * ratio, innerH};
}

Size ProgressBar::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    if (GetOrientation() == Orientation::Vertical) {
        return Size{16.0f + p.left + p.right, 120.0f + p.top + p.bottom};
    }
    return Size{120.0f + p.left + p.right, 16.0f + p.top + p.bottom};
}

// ============================================================================
// GridSplitter
// ============================================================================

GridSplitter::GridSplitter() {
    EnsureControlTypesRegistered();
    (void)SetValue(PropId_Background, value::FromColor(Color{0.26f, 0.30f, 0.39f, 0.90f}), ValueSourceRank::Default);
}

Size GridSplitter::MeasureCore(Size availableSize) noexcept {
    const float w = (GetWidth() > 0.0f) ? GetWidth() : std::min(5.0f, availableSize.width);
    const float h = (GetHeight() > 0.0f) ? GetHeight() : std::min(5.0f, availableSize.height);
    return Size{w, h};
}

bool GridSplitter::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    if (button == PointerButton::Left && IsEffectivelyEnabled()) {
        dragging_ = true;
        const Rect sb = LocalToScreenBounds(Rect{localPoint.x, localPoint.y, 0.0f, 0.0f});
        dragStartScreen_ = Point{sb.x, sb.y};
        outRequestCapture = true;
        return true;
    }
    outRequestCapture = false;
    return false;
}

bool GridSplitter::OnPointerMove(Point localPoint, std::uint8_t modifiers) {
    (void)modifiers;
    if (!dragging_) {
        return false;
    }
    Grid* grid = Parent() ? Parent()->DynamicCast<Grid>() : nullptr;
    if (!grid) {
        return false;
    }
    const Rect sb = LocalToScreenBounds(Rect{localPoint.x, localPoint.y, 0.0f, 0.0f});
    const Point curScreen{sb.x, sb.y};
    const float dx = curScreen.x - dragStartScreen_.x;
    const float dy = curScreen.y - dragStartScreen_.y;
    if (std::fabs(dx) < 0.5f && std::fabs(dy) < 0.5f) {
        return true;
    }
    dragStartScreen_ = curScreen;

    auto& cols = const_cast<std::vector<ColumnDefinition>&>(grid->Columns());
    auto& rows = const_cast<std::vector<RowDefinition>&>(grid->Rows());
    const int myCol = static_cast<int>(Grid::GetColumn(this));
    const int myRow = static_cast<int>(Grid::GetRow(this));

    const Rect r = FinalRect();
    const bool verticalBar = (r.height >= r.width) && (cols.size() >= 2u);
    if (verticalBar) {
        const int leftIdx = (myCol > 0) ? (myCol - 1) : 0;
        const int rightIdx = std::min<int>(
            static_cast<int>(cols.size()) - 1,
            (myCol > 0 && myCol + 1 < static_cast<int>(cols.size())) ? (myCol + 1) : (leftIdx + 1));
        if (leftIdx >= 0 && rightIdx > leftIdx && rightIdx < static_cast<int>(cols.size())) {
            AdjustGridTrackPair(
                cols[leftIdx].unit,
                cols[leftIdx].value,
                cols[leftIdx].computedWidth,
                cols[rightIdx].unit,
                cols[rightIdx].value,
                cols[rightIdx].computedWidth,
                dx,
                std::max(40.0f, grid->FinalRect().width * 0.45f));
            grid->InvalidateMeasure();
        }
    } else if (rows.size() >= 2u) {
        const int topIdx = (myRow > 0) ? (myRow - 1) : 0;
        const int botIdx = std::min<int>(
            static_cast<int>(rows.size()) - 1,
            (myRow > 0 && myRow + 1 < static_cast<int>(rows.size())) ? (myRow + 1) : (topIdx + 1));
        if (topIdx >= 0 && botIdx > topIdx && botIdx < static_cast<int>(rows.size())) {
            AdjustGridTrackPair(
                rows[topIdx].unit,
                rows[topIdx].value,
                rows[topIdx].computedHeight,
                rows[botIdx].unit,
                rows[botIdx].value,
                rows[botIdx].computedHeight,
                dy,
                std::max(30.0f, grid->FinalRect().height * 0.45f));
            grid->InvalidateMeasure();
        }
    }
    for (UIElement* sib = grid->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
        (void)sib->NotifyTwoWayPropertyMutated(PropId_ActualWidth);
        (void)sib->NotifyTwoWayPropertyMutated(PropId_ActualHeight);
    }
    return true;
}

bool GridSplitter::OnPointerUp(Point localPoint, PointerButton button, std::uint8_t modifiers, bool insideBounds) {
    (void)localPoint;
    (void)modifiers;
    (void)insideBounds;
    if (button == PointerButton::Left && dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

void ButtonBase::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "button";
    ioInfo.value = GetContentText();
}

bool ButtonBase::MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept {
    if (targetType == "ButtonBase" ||
        targetType == "RepeatButton" ||
        targetType == "Hyperlink" ||
        targetType == "MenuItem") {
        return true;
    }
    return ContentControl::MatchesBindingAncestorLegacyFallback(targetType);
}

void ToggleButton::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "button";
    ioInfo.value = IsChecked() ? "true" : "false";
    if (ioInfo.label.empty()) {
        ioInfo.label = GetContentText();
    }
}

void CheckBox::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    ToggleButton::PopulateAriaInfo(ioInfo);
    ioInfo.role = "checkbox";
}

void RadioButton::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    ToggleButton::PopulateAriaInfo(ioInfo);
    ioInfo.role = "radio";
}

void Slider::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "slider";
    ioInfo.value = rs_rt::valueToString(MakeFloat(GetSliderValue()));
}

} // namespace aero
