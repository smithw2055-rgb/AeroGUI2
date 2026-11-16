#include "ControlsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/controls/AxbTemplateMounter.hpp"
#include "aero/controls/Control.hpp"
#include "aero/controls/Decorators.hpp"
#include "aero/controls/ItemsControls.hpp"
#include "aero/controls/Panels.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"
#include "realscript/bytecode/Bytecode.h"

namespace aero {
namespace {

[[nodiscard]] Color GetColorPropOrTransparent(const UIElement& elem, std::uint16_t propId) noexcept {
    if (auto c = value::TryGetColor(elem.GetValue(propId))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

[[nodiscard]] Thickness GetThicknessPropOrZero(const UIElement& elem, std::uint16_t propId) noexcept {
    if (auto t = value::TryGetThickness(elem.GetValue(propId))) {
        return *t;
    }
    return Thickness{};
}

[[nodiscard]] CornerRadius GetCornerRadiusPropOrZero(const UIElement& elem, std::uint16_t propId) noexcept {
    if (auto r = value::TryGetCornerRadius(elem.GetValue(propId))) {
        return *r;
    }
    return CornerRadius{};
}

[[nodiscard]] Size ShrinkByThickness(
    Size availableSize,
    const Thickness& t,
    float& outTotalW,
    float& outTotalH) noexcept {
    outTotalW = t.left + t.right;
    outTotalH = t.top + t.bottom;
    return Size{
        std::max(0.0f, availableSize.width - outTotalW),
        std::max(0.0f, availableSize.height - outTotalH)};
}

[[nodiscard]] Rect InsetRectByThickness(Rect finalRect, const Thickness& t) noexcept {
    return Rect{
        finalRect.x + t.left,
        finalRect.y + t.top,
        std::max(0.0f, finalRect.width - (t.left + t.right)),
        std::max(0.0f, finalRect.height - (t.top + t.bottom))};
}

[[nodiscard]] std::string ResolveInheritedFontFamily(const UIElement* start) {
    for (const UIElement* cur = start; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
        if (cur == start || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                const UnifiedValue tpVal = tp->GetValue(PropId_FontFamily);
                if (const auto* s = std::get_if<StringValue>(&tpVal)) {
                    if (!s->view().empty()) {
                        return s->str();
                    }
                }
            }
        }
    }
    return {};
}

[[nodiscard]] float ResolveInheritedFontSize(const UIElement* start) noexcept {
    float out = 14.0f;
    for (const UIElement* cur = start; cur != nullptr; cur = cur->Parent()) {
        if (TryGetFloat(cur->GetValue(PropId_FontSize), out)) {
            return out;
        }
        if (cur == start || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                if (TryGetFloat(tp->GetValue(PropId_FontSize), out)) {
                    return out;
                }
            }
        }
    }
    return 14.0f;
}

[[nodiscard]] Color ResolveInheritedForeground(const UIElement* start) noexcept {
    for (const UIElement* cur = start; cur != nullptr; cur = cur->Parent()) {
        if (auto c = value::TryGetColor(cur->GetValue(PropId_Foreground))) {
            return *c;
        }
        if (cur == start || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                if (auto c = value::TryGetColor(tp->GetValue(PropId_Foreground))) {
                    return *c;
                }
            }
        }
    }
    return Color{0.94f, 0.96f, 0.98f, 1.0f};
}

} // namespace

// ============================================================================
// Shared Internal Helpers (aero::detail)
// ============================================================================

namespace detail {

void MarkSubtreeRenderDirtyRecursive(UIElement* elem) noexcept {
    if (!elem) {
        return;
    }
    elem->InvalidateRender();
    for (UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        MarkSubtreeRenderDirtyRecursive(c);
    }
}

std::int32_t CoerceInt32Property(const UnifiedValue& v, std::int32_t fallbackVal) noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    float f = 0.0f;
    if (TryGetFloat(v, f) && std::isfinite(f)) {
        return static_cast<std::int32_t>(std::lround(f));
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_UniformColumns, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            return static_cast<std::int32_t>(*pi);
        }
    }
    return fallbackVal;
}

bool CoerceBoolProperty(const UnifiedValue& v, bool fallbackVal) noexcept {
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return *i != 0;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_IsExpanded, sv->view());
        if (const auto* pb = std::get_if<bool>(&parsed)) {
            return *pb;
        }
    }
    return fallbackVal;
}

std::uint32_t ExtractGradientBrushId(const UnifiedValue& v) noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i > 0) {
            return static_cast<std::uint32_t>(*i);
        }
    }
    return 0u;
}

bool PointInRect(const Rect& r, Point p) noexcept {
    return r.width > 0.0f && r.height > 0.0f &&
           p.x >= r.x && p.x <= r.x + r.width &&
           p.y >= r.y && p.y <= r.y + r.height;
}

void ComputeViewboxScaleFactors(
    float contentW,
    float contentH,
    float availW,
    float availH,
    bool hasW,
    bool hasH,
    Stretch stretch,
    StretchDirection dir,
    float& outSx,
    float& outSy) noexcept {
    outSx = 1.0f;
    outSy = 1.0f;
    if (contentW <= 1e-5f || contentH <= 1e-5f || stretch == Stretch::None) {
        return;
    }

    switch (stretch) {
        case Stretch::None:
            outSx = 1.0f;
            outSy = 1.0f;
            break;
        case Stretch::Fill:
            outSx = hasW ? (availW / contentW) : 1.0f;
            outSy = hasH ? (availH / contentH) : 1.0f;
            break;
        case Stretch::Uniform: {
            float s = 1.0f;
            if (hasW && hasH) {
                s = std::min(availW / contentW, availH / contentH);
            } else if (hasW) {
                s = availW / contentW;
            } else if (hasH) {
                s = availH / contentH;
            }
            outSx = s;
            outSy = s;
            break;
        }
        case Stretch::UniformToFill: {
            float s = 1.0f;
            if (hasW && hasH) {
                s = std::max(availW / contentW, availH / contentH);
            } else if (hasW) {
                s = availW / contentW;
            } else if (hasH) {
                s = availH / contentH;
            }
            outSx = s;
            outSy = s;
            break;
        }
    }

    outSx = std::max(0.0f, outSx);
    outSy = std::max(0.0f, outSy);

    if (dir == StretchDirection::UpOnly) {
        outSx = std::max(1.0f, outSx);
        outSy = std::max(1.0f, outSy);
    } else if (dir == StretchDirection::DownOnly) {
        outSx = std::min(1.0f, outSx);
        outSy = std::min(1.0f, outSy);
    }
}

void SyncHostContentTemplateChildImpl(
    UIElement* hostContainer,
    const UIElement* contentSourceHost,
    IntrusivePtr<UIElement>& trackedChild,
    std::string& activeKey,
    ObjectRef& activeObj,
    bool& hasActiveObj,
    bool& syncingGuard) {
    if (!hostContainer || syncingGuard) {
        return;
    }
    syncingGuard = true;

    auto findEngines = [&](ExecutionDomainContext*& outDom,
                           DualTrackBindingEngine*& outBind,
                           StyleTriggerEngine*& outStyle,
                           AnimationEngine*& outAnim,
                           const Interpreter*& outInterp,
                           ObjectRef& outRootWrapper) {
        outDom = nullptr;
        outBind = nullptr;
        outStyle = nullptr;
        outAnim = nullptr;
        outInterp = nullptr;
        outRootWrapper = ObjectRef{};
        for (const UIElement* cur = hostContainer;
             cur != nullptr;
             cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
            if (!outBind && cur->BoundTwoWayEngine()) {
                outBind = cur->BoundTwoWayEngine();
            }
            if (!outStyle && cur->BoundStyleEngine()) {
                outStyle = cur->BoundStyleEngine();
                if (!outBind && outStyle->GetBindingEngine()) {
                    outBind = outStyle->GetBindingEngine();
                }
            }
            if (!outAnim && cur->BoundAnimationEngine()) {
                outAnim = cur->BoundAnimationEngine();
            }
            if (cur->BoundScriptWrapper().valid()) {
                outRootWrapper = cur->BoundScriptWrapper();
            }
        }
        if (outBind) {
            outDom = &outBind->Domains();
            outInterp = outBind->UiInterpreter();
            if (!outAnim) {
                outAnim = outBind->GetAnimationEngine();
            }
        }
        if (!outAnim && outStyle) {
            outAnim = outStyle->GetAnimationEngine();
        }
    };

    auto clearTracked = [&]() {
        if (!trackedChild) {
            activeKey.clear();
            activeObj = ObjectRef{};
            hasActiveObj = false;
            return;
        }
        ExecutionDomainContext* dom = nullptr;
        DualTrackBindingEngine* bindEng = nullptr;
        StyleTriggerEngine* styleEng = nullptr;
        AnimationEngine* animEng = nullptr;
        const Interpreter* interp = nullptr;
        ObjectRef rootWrap{};
        findEngines(dom, bindEng, styleEng, animEng, interp, rootWrap);
        if (animEng) {
            (void)animEng->StopSubtreeAnimations(trackedChild.Get(), false);
        }
        if (bindEng) {
            (void)bindEng->UnbindSubtree(trackedChild.Get());
        }
        if (styleEng) {
            (void)styleEng->UnbindSubtreeStyles(trackedChild.Get());
        }
        NativeHandleRegistry* reg = dom ? &dom->nativeHandles() : nullptr;
        ManagedHeap* heap = dom ? &dom->uiHeap() : nullptr;
        (void)hostContainer->RemoveChild(trackedChild.Get(), reg, heap);
        trackedChild.Reset();
        activeKey.clear();
        activeObj = ObjectRef{};
        hasActiveObj = false;
    };

    bool isHeaderSource = false;
    if (const auto* asCp = hostContainer->DynamicCast<ContentPresenter>()) {
        isHeaderSource = (asCp->GetContentSource() == "Header");
    }

    std::string ctKey;
    const UnifiedValue hostCtVal = hostContainer->GetValue(PropId_ContentTemplate);
    if (const auto* sv = std::get_if<StringValue>(&hostCtVal);
        sv && !sv->view().empty()) {
        ctKey = sv->str();
    } else if (contentSourceHost && contentSourceHost != hostContainer) {
        const std::uint16_t hostTplProp = isHeaderSource ? PropId_HeaderTemplate : PropId_ContentTemplate;
        const UnifiedValue srcHostCtVal = contentSourceHost->GetValue(hostTplProp);
        if (const auto* svHost = std::get_if<StringValue>(&srcHostCtVal);
            svHost && !svHost->view().empty()) {
            ctKey = svHost->str();
        }
    }

    if (ctKey.empty()) {
        constexpr std::string_view kVisualResPrefix = "__aero_visual_res__:";
        auto matchVisualResKey = [&](const UIElement* el) -> const StringValue* {
            if (!el) {
                return nullptr;
            }
            const UnifiedValue rawContent = el->GetValue(PropId_Content);
            if (const auto* sv = std::get_if<StringValue>(&rawContent);
                sv && sv->view().rfind(kVisualResPrefix, 0) == 0) {
                return sv;
            }
            const UnifiedValue rawText = el->GetValue(PropId_Text);
            if (const auto* sv = std::get_if<StringValue>(&rawText);
                sv && sv->view().rfind(kVisualResPrefix, 0) == 0) {
                return sv;
            }
            if (isHeaderSource) {
                const UnifiedValue rawHeader = el->GetValue(PropId_Header);
                if (const auto* sv = std::get_if<StringValue>(&rawHeader);
                    sv && sv->view().rfind(kVisualResPrefix, 0) == 0) {
                    return sv;
                }
            }
            return nullptr;
        };

        const StringValue* svRes = matchVisualResKey(hostContainer);
        if (!svRes && contentSourceHost && contentSourceHost != hostContainer) {
            svRes = matchVisualResKey(contentSourceHost);
        }
        if (svRes) {
            const std::string resKey = svRes->str();
            if (trackedChild && activeKey == resKey) {
                syncingGuard = false;
                return;
            }
            clearTracked();
            if (const AxbPackage* vPkg = AxbTemplateRegistry::Instance().FindDataTemplate(resKey)) {
                ExecutionDomainContext* dom = nullptr;
                DualTrackBindingEngine* bindEng = nullptr;
                StyleTriggerEngine* styleEng = nullptr;
                AnimationEngine* animEng = nullptr;
                const Interpreter* interp = nullptr;
                ObjectRef rootWrap{};
                findEngines(dom, bindEng, styleEng, animEng, interp, rootWrap);
                const std::string_view rt = !vPkg->rootTypeName.empty()
                    ? std::string_view(vPkg->rootTypeName)
                    : std::string_view("Border");
                IntrusivePtr<UIElement> built = CreateElementByTypeName(rt);
                if (built) {
                    (void)MountAxbTemplate(
                        built.Get(), *vPkg, dom, bindEng, ObjectRef{}, interp, nullptr, animEng);
                    if (styleEng) {
                        styleEng->RegisterPackageStyles(*vPkg);
                        (void)styleEng->BindSubtreeStyles(built.Get());
                        (void)styleEng->EvaluateSubtree(built.Get());
                    }
                    NativeHandleRegistry* reg = dom ? &dom->nativeHandles() : nullptr;
                    ManagedHeap* heap = dom ? &dom->uiHeap() : nullptr;
                    hostContainer->AddChild(built.Get(), reg, heap);
                    trackedChild = built;
                    activeKey = resKey;
                }
            }
            syncingGuard = false;
            return;
        }
        clearTracked();
        syncingGuard = false;
        return;
    }

    const AxbPackage* dtPkg = AxbTemplateRegistry::Instance().FindDataTemplate(ctKey);
    if (!dtPkg) {
        syncingGuard = false;
        return;
    }

    bool hasExplicitContentEntry = false;
    bool explicitNullContent = false;
    ObjectRef itemObj{};

    auto inspectContentOn = [&](const UIElement* el) -> bool {
        if (!el) {
            return false;
        }
        const std::uint16_t p0 = isHeaderSource ? PropId_Header : PropId_Content;
        const std::uint16_t p1 = isHeaderSource ? PropId_Title : PropId_Text;
        for (const std::uint16_t pid : {p0, p1}) {
            if (const auto* entry = el->FindCompactEntry(pid)) {
                if (entry->sourceRank == static_cast<std::uint8_t>(ValueSourceRank::Default) &&
                    std::holds_alternative<std::monostate>(entry->effectiveValue)) {
                    continue;
                }
                if (std::holds_alternative<NullObject>(entry->effectiveValue)) {
                    hasExplicitContentEntry = true;
                    explicitNullContent = true;
                    return true;
                }
                if (const auto* ref = std::get_if<ObjectRef>(&entry->effectiveValue)) {
                    hasExplicitContentEntry = true;
                    if (ref->valid()) {
                        itemObj = *ref;
                    } else {
                        explicitNullContent = true;
                    }
                    return true;
                }
                if (std::holds_alternative<NativeHandle>(entry->effectiveValue)) {
                    hasExplicitContentEntry = true;
                    return true;
                }
                if (!std::holds_alternative<std::monostate>(entry->effectiveValue)) {
                    hasExplicitContentEntry = true;
                    return true;
                }
            }
        }
        return false;
    };

    if (!inspectContentOn(hostContainer) && contentSourceHost && contentSourceHost != hostContainer) {
        (void)inspectContentOn(contentSourceHost);
    }

    if (explicitNullContent) {
        clearTracked();
        syncingGuard = false;
        return;
    }

    if (!hasExplicitContentEntry) {
        for (const UIElement* cur = hostContainer;
             cur != nullptr;
             cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
            const UnifiedValue dcVal = cur->GetValue(PropId_DataContext);
            if (const auto* ref = std::get_if<ObjectRef>(&dcVal); ref && ref->valid()) {
                itemObj = *ref;
                break;
            }
        }
    }

    NativeHandle explicitNh{};
    bool hasExplicitNh = false;
    {
        const UnifiedValue hc = hostContainer->GetValue(isHeaderSource ? PropId_Header : PropId_Content);
        if (hc.tryGetNativeHandle(explicitNh)) {
            hasExplicitNh = true;
        } else if (contentSourceHost && contentSourceHost != hostContainer) {
            const UnifiedValue sc = contentSourceHost->GetValue(isHeaderSource ? PropId_Header : PropId_Content);
            if (sc.tryGetNativeHandle(explicitNh)) {
                hasExplicitNh = true;
            }
        }
    }
    if (!hasExplicitNh && trackedChild &&
        activeKey == ctKey &&
        hasActiveObj == hasExplicitContentEntry &&
        activeObj == itemObj) {
        syncingGuard = false;
        return;
    }

    clearTracked();

    ExecutionDomainContext* dom = nullptr;
    DualTrackBindingEngine* bindEng = nullptr;
    StyleTriggerEngine* styleEng = nullptr;
    AnimationEngine* animEng = nullptr;
    const Interpreter* interp = nullptr;
    ObjectRef rootWrap{};
    findEngines(dom, bindEng, styleEng, animEng, interp, rootWrap);

    AxbPackage boundCopy = *dtPkg;
    if (bindEng && bindEng->Program() && dom) {
        const ProgramImage* prog = bindEng->Program();
        if (boundCopy.className.empty() && rootWrap.valid()) {
            if (auto* wHeap = dom->heapForObject(rootWrap); wHeap && wHeap->isAlive(rootWrap)) {
                if (const auto wTid = wHeap->objectTypeId(rootWrap)) {
                    if (const auto* wTs = prog->resolveType(*wTid)) {
                        boundCopy.className = wTs->name;
                    }
                }
            }
        }
        if (itemObj.valid()) {
            if (auto* iHeap = dom->heapForObject(itemObj); iHeap && iHeap->isAlive(itemObj)) {
                if (const auto iTid = iHeap->objectTypeId(itemObj)) {
                    if (const auto* iTs = prog->resolveType(*iTid)) {
                        boundCopy.rootDataTypeName = iTs->name;
                    }
                }
            }
        }
        boundCopy.BindAgainstProgram(*prog);
    }

    IntrusivePtr<UIElement> dtRoot = !boundCopy.rootTypeName.empty()
        ? CreateElementByTypeName(boundCopy.rootTypeName)
        : MakeIntrusive<Grid>();
    if (!dtRoot) {
        dtRoot = MakeIntrusive<Grid>();
    }
    dtRoot->SetNamescopeBoundary(true);
    if (itemObj.valid()) {
        (void)dtRoot->SetValue(PropId_DataContext, itemObj, ValueSourceRank::Local);
    }

    NativeHandleRegistry* reg = dom ? &dom->nativeHandles() : nullptr;
    ManagedHeap* heap = dom ? &dom->uiHeap() : nullptr;
    (void)hostContainer->AddChild(dtRoot.Get(), reg, heap);

    (void)MountAxbTemplate(
        dtRoot.Get(),
        boundCopy,
        dom,
        bindEng,
        itemObj,
        interp,
        styleEng ? styleEng->GetAotExecutionContext() : nullptr,
        animEng);

    if (styleEng) {
        styleEng->RegisterPackageStyles(boundCopy);
        (void)styleEng->BindSubtreeStyles(dtRoot.Get(), true);
        (void)styleEng->EvaluateSubtree(dtRoot.Get());
    }

    trackedChild = std::move(dtRoot);
    activeKey = std::move(ctKey);
    activeObj = itemObj;
    hasActiveObj = hasExplicitContentEntry;
    syncingGuard = false;
}

} // namespace detail

// ============================================================================
// RTTI Type Registration (14 classes)
// ============================================================================

AERO_IMPLEMENT_CLASS(Control, UIElement, "Aero.UI.Control")
AERO_IMPLEMENT_CLASS(ContentControl, Control, "Aero.UI.ContentControl")
AERO_IMPLEMENT_CLASS(HeaderedContentControl, ContentControl, "Aero.UI.HeaderedContentControl")
AERO_IMPLEMENT_CLASS(GroupBox, HeaderedContentControl, "Aero.UI.GroupBox")
AERO_IMPLEMENT_CLASS(Expander, HeaderedContentControl, "Aero.UI.Expander")
AERO_IMPLEMENT_CLASS(Thumb, Control, "Aero.UI.Thumb")
AERO_IMPLEMENT_CLASS(Separator, Control, "Aero.UI.Separator")
AERO_IMPLEMENT_CLASS(Decorator, UIElement, "Aero.UI.Decorator")
AERO_IMPLEMENT_CLASS(Border, Decorator, "Aero.UI.Border")
AERO_IMPLEMENT_CLASS(BoxShadow, Decorator, "Aero.UI.BoxShadow")
AERO_IMPLEMENT_CLASS(Viewbox, Decorator, "Aero.UI.Viewbox")
AERO_IMPLEMENT_CLASS(LayoutScaler, Viewbox, "Aero.UI.LayoutScaler")
AERO_IMPLEMENT_CLASS(UserControl, ContentControl, "Aero.UI.UserControl")
AERO_IMPLEMENT_CLASS(Window, ContentControl, "Aero.UI.Window")

// ============================================================================
// Control
// ============================================================================

Control::Control() {
    EnsureControlTypesRegistered();
}

void Control::SetBackground(const Color& c) {
    (void)SetValue(PropId_Background, value::FromColor(c), ValueSourceRank::Local);
}

Color Control::GetBackground() const noexcept {
    return GetColorPropOrTransparent(*this, PropId_Background);
}

void Control::SetBorderBrush(const Color& c) {
    (void)SetValue(PropId_BorderBrush, value::FromColor(c), ValueSourceRank::Local);
}

Color Control::GetBorderBrush() const noexcept {
    return GetColorPropOrTransparent(*this, PropId_BorderBrush);
}

void Control::SetBorderThickness(const Thickness& t) {
    (void)SetValue(PropId_BorderThickness, value::FromThickness(t), ValueSourceRank::Local);
}

Thickness Control::GetBorderThickness() const noexcept {
    return GetThicknessPropOrZero(*this, PropId_BorderThickness);
}

void Control::SetCornerRadius(const CornerRadius& cr) {
    (void)SetValue(PropId_CornerRadius, value::FromCornerRadius(cr), ValueSourceRank::Local);
}

CornerRadius Control::GetCornerRadius() const noexcept {
    return GetCornerRadiusPropOrZero(*this, PropId_CornerRadius);
}

void Control::SetFontFamily(std::string_view fontFamily) {
    if (fontFamily.empty()) {
        (void)ClearValue(PropId_FontFamily, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_FontFamily, value::FromString(std::string(fontFamily)), ValueSourceRank::Local);
    }
}

std::string Control::GetFontFamily() const {
    return ResolveInheritedFontFamily(this);
}

void Control::SetFontSize(float fontSize) {
    (void)SetValue(PropId_FontSize, MakeFloat(fontSize), ValueSourceRank::Local);
}

float Control::GetFontSize() const noexcept {
    return ResolveInheritedFontSize(this);
}

void Control::SetForeground(const Color& c) {
    (void)SetValue(PropId_Foreground, value::FromColor(c), ValueSourceRank::Local);
}

Color Control::GetForeground() const noexcept {
    return ResolveInheritedForeground(this);
}

void Control::SetHorizontalContentAlignment(HorizontalAlignment align) {
    (void)SetValue(
        PropId_HorizontalContentAlignment,
        value::FromInt(static_cast<std::int64_t>(align)),
        ValueSourceRank::Local);
}

HorizontalAlignment Control::GetHorizontalContentAlignment() const noexcept {
    const UnifiedValue v = GetValue(PropId_HorizontalContentAlignment);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<HorizontalAlignment>(*i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_HorizontalContentAlignment, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi >= 0 && *pi <= 3) {
                return static_cast<HorizontalAlignment>(*pi);
            }
        }
    }
    return HorizontalAlignment::Left;
}

void Control::SetVerticalContentAlignment(VerticalAlignment align) {
    (void)SetValue(
        PropId_VerticalContentAlignment,
        value::FromInt(static_cast<std::int64_t>(align)),
        ValueSourceRank::Local);
}

VerticalAlignment Control::GetVerticalContentAlignment() const noexcept {
    const UnifiedValue v = GetValue(PropId_VerticalContentAlignment);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<VerticalAlignment>(*i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_VerticalContentAlignment, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi >= 0 && *pi <= 3) {
                return static_cast<VerticalAlignment>(*pi);
            }
        }
    }
    return VerticalAlignment::Center;
}

void Control::SetTabIndex(std::int32_t tabIndex) {
    (void)SetValue(
        PropId_TabIndex,
        value::FromInt(static_cast<std::int64_t>(tabIndex)),
        ValueSourceRank::Local);
}

std::int32_t Control::GetTabIndex() const noexcept {
    return detail::CoerceInt32Property(GetValue(PropId_TabIndex), 0);
}

Size Control::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    float borderW = 0.0f;
    float borderH = 0.0f;
    const Size innerAvail = ShrinkByThickness(availableSize, GetBorderThickness(), borderW, borderH);
    const Size contentSize = UIElement::MeasureCore(innerAvail);
    return Size{contentSize.width + borderW, contentSize.height + borderH};
}

void Control::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    UIElement::ArrangeCore(InsetRectByThickness(finalRect, GetBorderThickness()));
}

// ============================================================================
// ContentControl
// ============================================================================

ContentControl::ContentControl() {
    EnsureControlTypesRegistered();
}

void ContentControl::SetContentText(std::string_view text) {
    (void)SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string ContentControl::GetContentText() const {
    const UnifiedValue textVal = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&textVal)) {
        return s->str();
    }
    const UnifiedValue contentVal = GetValue(PropId_Content);
    if (const auto* s = std::get_if<StringValue>(&contentVal)) {
        return s->str();
    }
    const UnifiedValue titleVal = GetValue(PropId_Title);
    if (const auto* s = std::get_if<StringValue>(&titleVal)) {
        return s->str();
    }
    return {};
}

void ContentControl::SyncContentTemplateChild() {
    if (!HostsDirectContentTemplateChild() ||
        HasControlTemplate() ||
        !GetControlTemplateKey().empty()) {
        if (contentTemplateChild_) {
            // ControlTemplate's ContentPresenter or derived class template owns content presentation instead
            const UnifiedValue savedCt = GetValue(PropId_ContentTemplate);
            (void)ForceClearSparseEntry(PropId_ContentTemplate);
            detail::SyncHostContentTemplateChildImpl(
                this,
                this,
                contentTemplateChild_,
                activeContentTemplateKey_,
                activeContentTemplateObj_,
                hasActiveContentTemplateObj_,
                syncingContentTemplate_);
            if (!std::holds_alternative<std::monostate>(savedCt)) {
                (void)SetValue(PropId_ContentTemplate, savedCt, ValueSourceRank::ThemeStyle);
            }
        }
        return;
    }
    detail::SyncHostContentTemplateChildImpl(
        this,
        this,
        contentTemplateChild_,
        activeContentTemplateKey_,
        activeContentTemplateObj_,
        hasActiveContentTemplateObj_,
        syncingContentTemplate_);
}

void ContentControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (RefCount() > 0u &&
        (propId == PropId_ContentTemplate || propId == PropId_Content || propId == PropId_Text)) {
        if (HostsDirectContentTemplateChild() &&
            !HasControlTemplate() &&
            GetControlTemplateKey().empty() &&
            (BoundStyleEngine() != nullptr || GetStyleKey().empty())) {
            SyncContentTemplateChild();
        }
    }
}

void ContentControl::MarkExtraValuesInPools() const noexcept {
    Control::MarkExtraValuesInPools();
    if (hasActiveContentTemplateObj_ && activeContentTemplateObj_.valid()) {
        rs_rt::markValueInPools(UnifiedValue{activeContentTemplateObj_});
    }
}

Size ContentControl::MeasureCore(Size availableSize) noexcept {
    return Control::MeasureCore(availableSize);
}

void ContentControl::ArrangeCore(Rect finalRect) noexcept {
    Control::ArrangeCore(finalRect);
}

// ============================================================================
// HeaderedContentControl
// ============================================================================

HeaderedContentControl::HeaderedContentControl() {
    EnsureControlTypesRegistered();
}

void HeaderedContentControl::SetHeader(std::string_view text) {
    (void)SetValue(PropId_Header, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string HeaderedContentControl::GetHeader() const {
    const UnifiedValue v = GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    const UnifiedValue tv = GetValue(PropId_Title);
    if (const auto* sv = std::get_if<StringValue>(&tv)) {
        return sv->str();
    }
    return {};
}

void HeaderedContentControl::SetHeaderElement(UIElement* element) {
    if (headerElement_.Get() != element) {
        headerElement_.Reset(element);
        InvalidateLayout();
        InvalidateRender();
    }
}

UIElement* HeaderedContentControl::GetHeaderElement() const noexcept {
    if (headerElement_ &&
        (headerElement_->Parent() == this ||
         (HasControlTemplate() && headerElement_->Parent() != nullptr))) {
        return headerElement_.Get();
    }
    return nullptr;
}

bool HeaderedContentControl::HasHeader() const noexcept {
    return GetHeaderElement() != nullptr || !GetHeader().empty();
}

Size HeaderedContentControl::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* hdrElem = GetHeaderElement();
    float hdrW = 0.0f;
    float hdrH = 0.0f;
    if (hdrElem) {
        hdrElem->Measure(Size{innerAvailW, innerAvailH});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = hdrElem->DesiredSize().width;
            hdrH = std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = ts.width + 8.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, ts.height + 8.0f);
        }
    }

    const float bodyAvailH = std::max(0.0f, innerAvailH - hdrH);
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{innerAvailW, bodyAvailH});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width);
            bodyH = std::max(bodyH, c->DesiredSize().height);
        }
    }

    return Size{
        std::max(hdrW, bodyW) + padW,
        hdrH + bodyH + padH};
}

void HeaderedContentControl::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* hdrElem = GetHeaderElement();
    float hdrH = 0.0f;
    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrH = std::min(innerH, std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height));
            headerLocalRect_ = Rect{startX, startY, innerW, hdrH};
            hdrElem->Arrange(headerLocalRect_);
        } else {
            headerLocalRect_ = Rect{startX, startY, 0.0f, 0.0f};
            hdrElem->Arrange(headerLocalRect_);
        }
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        hdrH = std::min(innerH, std::max(kDefaultHeaderBarHeight, fontSize * 1.25f + 8.0f));
        headerLocalRect_ = Rect{startX, startY, innerW, hdrH};
    } else {
        headerLocalRect_ = Rect{startX, startY, 0.0f, 0.0f};
    }

    contentLocalRect_ = Rect{
        startX,
        startY + hdrH,
        innerW,
        std::max(0.0f, innerH - hdrH)};
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(contentLocalRect_);
    }
}

// ============================================================================
// GroupBox
// ============================================================================

GroupBox::GroupBox() {
    EnsureControlTypesRegistered();
}

Size GroupBox::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const float frameInsetX = (p.left + p.right > 0.0f) ? (p.left + p.right) : 16.0f;
    const float frameInsetY = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 16.0f;
    const float innerAvailW = std::max(0.0f, availableSize.width - frameInsetX);

    UIElement* hdrElem = GetHeaderElement();
    float hdrW = 0.0f;
    float hdrH = 0.0f;
    if (hdrElem) {
        hdrElem->Measure(Size{std::max(0.0f, availableSize.width - 8.0f), availableSize.height});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = hdrElem->DesiredSize().width + 12.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height + 4.0f);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = ts.width + 16.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, ts.height + 8.0f);
        }
    }

    const float bodyAvailH = std::max(0.0f, availableSize.height - hdrH - frameInsetY);
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{innerAvailW, bodyAvailH});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width);
            bodyH = std::max(bodyH, c->DesiredSize().height);
        }
    }

    return Size{
        std::max(hdrW, bodyW + frameInsetX),
        hdrH + bodyH + frameInsetY};
}

void GroupBox::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    const Thickness p = GetPadding();
    const float padL = (p.left > 0.0f) ? p.left : 8.0f;
    const float padT = (p.top > 0.0f) ? p.top : 8.0f;
    const float padR = (p.right > 0.0f) ? p.right : 8.0f;
    const float padB = (p.bottom > 0.0f) ? p.bottom : 8.0f;

    UIElement* hdrElem = GetHeaderElement();
    float hdrH = 0.0f;
    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrH = std::min(finalRect.height, std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height + 4.0f));
            headerLocalRect_ = Rect{finalRect.x + 6.0f, finalRect.y, std::max(0.0f, finalRect.width - 12.0f), hdrH};
            hdrElem->Arrange(headerLocalRect_);
        } else {
            headerLocalRect_ = Rect{finalRect.x, finalRect.y, 0.0f, 0.0f};
            hdrElem->Arrange(headerLocalRect_);
        }
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        hdrH = std::min(finalRect.height, std::max(kDefaultHeaderBarHeight, fontSize * 1.25f + 8.0f));
        headerLocalRect_ = Rect{finalRect.x + 6.0f, finalRect.y, std::max(0.0f, finalRect.width - 12.0f), hdrH};
    } else {
        headerLocalRect_ = Rect{finalRect.x, finalRect.y, 0.0f, 0.0f};
    }

    bodyBorderLocalRect_ = Rect{
        finalRect.x,
        finalRect.y + hdrH,
        finalRect.width,
        std::max(0.0f, finalRect.height - hdrH)};
    contentLocalRect_ = Rect{
        bodyBorderLocalRect_.x + padL,
        bodyBorderLocalRect_.y + padT,
        std::max(0.0f, bodyBorderLocalRect_.width - (padL + padR)),
        std::max(0.0f, bodyBorderLocalRect_.height - (padT + padB))};

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(contentLocalRect_);
    }
}

// ============================================================================
// Expander
// ============================================================================

Expander::Expander() {
    EnsureControlTypesRegistered();
}

void Expander::SetIsExpanded(bool expanded) {
    (void)SetValue(PropId_IsExpanded, value::FromBool(expanded), ValueSourceRank::Local);
}

bool Expander::IsExpanded() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsExpanded), false);
}

void Expander::ToggleExpanded() {
    const bool next = !IsExpanded();
    SetIsExpanded(next);
    (void)NotifyTwoWayPropertyMutated(PropId_IsExpanded);
    if (expandedChangedCallback_) {
        expandedChangedCallback_(*this, next);
    }
    (void)DispatchWrapperRoutedEvent(next ? "Expanded" : "Collapsed", value::FromBool(next), true);
}

void Expander::SetExpandDirection(ExpandDirection dir) {
    (void)SetValue(PropId_ExpandDirection, value::FromInt(static_cast<std::int64_t>(dir)), ValueSourceRank::Local);
}

ExpandDirection Expander::GetExpandDirection() const noexcept {
    const UnifiedValue v = GetValue(PropId_ExpandDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<ExpandDirection>(*i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_ExpandDirection, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi >= 0 && *pi <= 3) {
                return static_cast<ExpandDirection>(*pi);
            }
        }
    }
    return ExpandDirection::Down;
}

bool Expander::IsBodyChildHidden(const UIElement* child) const noexcept {
    if (HasControlTemplate() || !child || child->Parent() != this) {
        return false;
    }
    if (child == GetHeaderElement()) {
        return false;
    }
    return !IsExpanded();
}

void Expander::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    HeaderedContentControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsExpanded || propId == PropId_ExpandDirection) {
        InvalidateLayout();
        detail::MarkSubtreeRenderDirtyRecursive(this);
    }
}

bool Expander::ActivateFromInput(RuntimeError* /*error*/) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    ToggleExpanded();
    return true;
}

bool Expander::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    if (detail::PointInRect(headerLocalRect_, localPoint)) {
        ToggleExpanded();
        return true;
    }
    return false;
}

bool Expander::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        ToggleExpanded();
        return true;
    }
    return false;
}

Size Expander::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* hdrElem = GetHeaderElement();
    const ExpandDirection dir = GetExpandDirection();
    const bool horizontalExpand = (dir == ExpandDirection::Left || dir == ExpandDirection::Right);

    float hdrW = kDefaultExpanderHeaderHeight;
    float hdrH = kDefaultExpanderHeaderHeight;
    if (hdrElem) {
        hdrElem->Measure(Size{std::max(0.0f, innerAvailW - 22.0f), innerAvailH});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = std::max(hdrW, hdrElem->DesiredSize().width + 26.0f);
            hdrH = std::max(hdrH, hdrElem->DesiredSize().height + 6.0f);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = std::max(hdrW, ts.width + 28.0f);
            hdrH = std::max(hdrH, ts.height + 10.0f);
        }
    }

    if (!IsExpanded()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c != hdrElem) {
                c->Measure(Size{0.0f, 0.0f});
            }
        }
        return Size{hdrW + padW, hdrH + padH};
    }

    const Size bodyAvail = horizontalExpand
        ? Size{std::max(0.0f, innerAvailW - hdrW), innerAvailH}
        : Size{innerAvailW, std::max(0.0f, innerAvailH - hdrH)};
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    const float contentInset = 8.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{
            std::max(0.0f, bodyAvail.width - contentInset),
            std::max(0.0f, bodyAvail.height - contentInset)});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width + contentInset);
            bodyH = std::max(bodyH, c->DesiredSize().height + contentInset);
        }
    }

    if (horizontalExpand) {
        return Size{hdrW + bodyW + padW, std::max(hdrH, bodyH) + padH};
    }
    return Size{std::max(hdrW, bodyW) + padW, hdrH + bodyH + padH};
}

void Expander::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* hdrElem = GetHeaderElement();
    const ExpandDirection dir = GetExpandDirection();
    const bool expanded = IsExpanded();

    float hdrBarH = kDefaultExpanderHeaderHeight;
    float hdrBarW = kDefaultExpanderHeaderHeight;
    if (hdrElem && hdrElem->GetVisibility() != Visibility::Collapsed) {
        hdrBarH = std::max(hdrBarH, hdrElem->DesiredSize().height + 6.0f);
        hdrBarW = std::max(hdrBarW, hdrElem->DesiredSize().width + 26.0f);
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        const Size ts = MeasureUtf8TextRun(GetHeader(), fontSize);
        hdrBarH = std::max(hdrBarH, ts.height + 10.0f);
        hdrBarW = std::max(hdrBarW, ts.width + 28.0f);
    }

    switch (dir) {
        case ExpandDirection::Down: {
            const float hh = std::min(innerH, hdrBarH);
            headerLocalRect_ = Rect{startX, startY, innerW, hh};
            contentLocalRect_ = expanded
                ? Rect{startX, startY + hh, innerW, std::max(0.0f, innerH - hh)}
                : Rect{startX, startY + hh, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Up: {
            const float hh = std::min(innerH, hdrBarH);
            headerLocalRect_ = Rect{startX, startY + innerH - hh, innerW, hh};
            contentLocalRect_ = expanded
                ? Rect{startX, startY, innerW, std::max(0.0f, innerH - hh)}
                : Rect{startX, startY, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Right: {
            const float hw = std::min(innerW, hdrBarW);
            headerLocalRect_ = Rect{startX, startY, hw, innerH};
            contentLocalRect_ = expanded
                ? Rect{startX + hw, startY, std::max(0.0f, innerW - hw), innerH}
                : Rect{startX + hw, startY, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Left: {
            const float hw = std::min(innerW, hdrBarW);
            headerLocalRect_ = Rect{startX + innerW - hw, startY, hw, innerH};
            contentLocalRect_ = expanded
                ? Rect{startX, startY, std::max(0.0f, innerW - hw), innerH}
                : Rect{startX, startY, 0.0f, 0.0f};
            break;
        }
    }

    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrElem->Arrange(Rect{
                headerLocalRect_.x + 22.0f,
                headerLocalRect_.y + 3.0f,
                std::max(0.0f, headerLocalRect_.width - 26.0f),
                std::max(0.0f, headerLocalRect_.height - 6.0f)});
        } else {
            hdrElem->Arrange(Rect{headerLocalRect_.x, headerLocalRect_.y, 0.0f, 0.0f});
        }
    }

    const Rect bodySlot = expanded
        ? Rect{
              contentLocalRect_.x + 4.0f,
              contentLocalRect_.y + 4.0f,
              std::max(0.0f, contentLocalRect_.width - 8.0f),
              std::max(0.0f, contentLocalRect_.height - 8.0f)}
        : Rect{contentLocalRect_.x, contentLocalRect_.y, 0.0f, 0.0f};

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(bodySlot);
    }
}

// ============================================================================
// Thumb & Separator
// ============================================================================

Thumb::Thumb() {
    EnsureControlTypesRegistered();
}

Separator::Separator() {
    EnsureControlTypesRegistered();
    SetHeight(1.0f);
    (void)SetValue(PropId_Margin, value::FromThickness(Thickness{0.0f, 3.0f, 0.0f, 3.0f}), ValueSourceRank::Default);
    (void)SetValue(
        PropId_Background,
        value::FromColor(Color{0.24f, 0.27f, 0.34f, 0.85f}),
        ValueSourceRank::Default);
}

Size Separator::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const float w = (GetWidth() > 0.0f) ? GetWidth() : 0.0f;
    const float h = (GetHeight() > 0.0f) ? GetHeight() : 1.0f;
    return Size{w, h};
}

// ============================================================================
// Decorator, Border, BoxShadow
// ============================================================================

Decorator::Decorator() {
    EnsureControlTypesRegistered();
}

void Decorator::SetChild(UIElement* child, NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    if (FirstChild() == child && (child == nullptr || child->NextSibling() == nullptr)) {
        return;
    }
    while (UIElement* existing = FirstChild()) {
        (void)RemoveChild(existing, registry, uiHeap);
    }
    if (child != nullptr) {
        (void)AddChild(child, registry, uiHeap);
    }
}

void Decorator::SetBackground(const Color& color) {
    (void)SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Decorator::GetBackground() const noexcept {
    return GetColorPropOrTransparent(*this, PropId_Background);
}

void Decorator::SetBorderBrush(const Color& color) {
    (void)SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Decorator::GetBorderBrush() const noexcept {
    return GetColorPropOrTransparent(*this, PropId_BorderBrush);
}

void Decorator::SetBorderThickness(const Thickness& thickness) {
    (void)SetValue(PropId_BorderThickness, value::FromThickness(thickness), ValueSourceRank::Local);
}

Thickness Decorator::GetBorderThickness() const noexcept {
    return GetThicknessPropOrZero(*this, PropId_BorderThickness);
}

void Decorator::SetCornerRadius(const CornerRadius& radius) {
    (void)SetValue(PropId_CornerRadius, value::FromCornerRadius(radius), ValueSourceRank::Local);
}

CornerRadius Decorator::GetCornerRadius() const noexcept {
    return GetCornerRadiusPropOrZero(*this, PropId_CornerRadius);
}

Size Decorator::MeasureCore(Size availableSize) noexcept {
    float borderW = 0.0f;
    float borderH = 0.0f;
    const Size innerAvail = ShrinkByThickness(availableSize, GetBorderThickness(), borderW, borderH);
    const Size contentSize = UIElement::MeasureCore(innerAvail);
    return Size{contentSize.width + borderW, contentSize.height + borderH};
}

void Decorator::ArrangeCore(Rect finalRect) noexcept {
    UIElement::ArrangeCore(InsetRectByThickness(finalRect, GetBorderThickness()));
}

Border::Border() {
    EnsureControlTypesRegistered();
}

BoxShadow::BoxShadow() {
    EnsureControlTypesRegistered();
}

// ============================================================================
// Viewbox & LayoutScaler
// ============================================================================

Viewbox::Viewbox() {
    EnsureControlTypesRegistered();
}

void Viewbox::SetStretch(Stretch stretch) {
    (void)SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Viewbox::GetStretch() const noexcept {
    const auto v = GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

void Viewbox::SetStretchDirection(StretchDirection dir) {
    (void)SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection Viewbox::GetStretchDirection() const noexcept {
    const auto v = GetValue(PropId_StretchDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 2) {
            return static_cast<StretchDirection>(*i);
        }
    }
    return StretchDirection::Both;
}

Size Viewbox::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* child = FirstChild();
    if (!child) {
        return Size{padW, padH};
    }

    // Measure single child in unconstrained natural design space
    child->Measure(Size{detail::kUnconstrainedSpace, detail::kUnconstrainedSpace});
    for (UIElement* extra = child->NextSibling(); extra != nullptr; extra = extra->NextSibling()) {
        extra->Measure(Size{detail::kUnconstrainedSpace, detail::kUnconstrainedSpace});
    }

    if (child->GetVisibility() == Visibility::Collapsed) {
        return Size{padW, padH};
    }

    const Size natural = child->DesiredSize();
    const bool hasW =
        std::isfinite(availableSize.width) && (availableSize.width < detail::kUnconstrainedSpace * 0.5f);
    const bool hasH =
        std::isfinite(availableSize.height) && (availableSize.height < detail::kUnconstrainedSpace * 0.5f);

    float sx = 1.0f;
    float sy = 1.0f;
    detail::ComputeViewboxScaleFactors(
        natural.width,
        natural.height,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    return Size{
        natural.width * sx + padW,
        natural.height * sy + padH};
}

void Viewbox::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* child = FirstChild();
    if (!child || child->GetVisibility() == Visibility::Collapsed) {
        const bool resetChanged =
            (std::fabs(childScaleX_ - 1.0f) > 1e-5f) ||
            (std::fabs(childScaleY_ - 1.0f) > 1e-5f) ||
            (std::fabs(childOriginX_ - startX) > 1e-3f) ||
            (std::fabs(childOriginY_ - startY) > 1e-3f);
        childScaleX_ = 1.0f;
        childScaleY_ = 1.0f;
        childOriginX_ = startX;
        childOriginY_ = startY;
        for (UIElement* c = child; c != nullptr; c = c->NextSibling()) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
        }
        if (resetChanged) {
            InvalidateRender();
            for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
                detail::MarkSubtreeRenderDirtyRecursive(c);
            }
        }
        return;
    }

    const Size natural = child->DesiredSize();
    float sx = 1.0f;
    float sy = 1.0f;
    detail::ComputeViewboxScaleFactors(
        natural.width,
        natural.height,
        innerW,
        innerH,
        true,
        true,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    const float scaledW = natural.width * sx;
    const float scaledH = natural.height * sy;

    float offsetX = (innerW - scaledW) * 0.5f;
    switch (child->GetHorizontalAlignment()) {
        case HorizontalAlignment::Left:
            offsetX = 0.0f;
            break;
        case HorizontalAlignment::Right:
            offsetX = innerW - scaledW;
            break;
        case HorizontalAlignment::Center:
        case HorizontalAlignment::Stretch:
            offsetX = (innerW - scaledW) * 0.5f;
            break;
    }

    float offsetY = (innerH - scaledH) * 0.5f;
    switch (child->GetVerticalAlignment()) {
        case VerticalAlignment::Top:
            offsetY = 0.0f;
            break;
        case VerticalAlignment::Bottom:
            offsetY = innerH - scaledH;
            break;
        case VerticalAlignment::Center:
        case VerticalAlignment::Stretch:
            offsetY = (innerH - scaledH) * 0.5f;
            break;
    }

    const float newOriginX = startX + offsetX;
    const float newOriginY = startY + offsetY;
    const bool scaleChanged =
        (std::fabs(childScaleX_ - sx) > 1e-5f) ||
        (std::fabs(childScaleY_ - sy) > 1e-5f) ||
        (std::fabs(childOriginX_ - newOriginX) > 1e-3f) ||
        (std::fabs(childOriginY_ - newOriginY) > 1e-3f);

    childScaleX_ = sx;
    childScaleY_ = sy;
    childOriginX_ = newOriginX;
    childOriginY_ = newOriginY;

    child->Arrange(Rect{newOriginX, newOriginY, natural.width, natural.height});
    for (UIElement* extra = child->NextSibling(); extra != nullptr; extra = extra->NextSibling()) {
        const Size ed = extra->DesiredSize();
        extra->Arrange(Rect{newOriginX, newOriginY, ed.width, ed.height});
    }

    if (scaleChanged) {
        InvalidateRender();
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            detail::MarkSubtreeRenderDirtyRecursive(c);
        }
    }
}

LayoutScaler::LayoutScaler() {
    EnsureControlTypesRegistered();
}

void LayoutScaler::SetRefResolution(Size refRes) {
    (void)SetValue(
        PropId_RefResolution,
        UnifiedValue{Float4Value{std::max(1.0f, refRes.width), std::max(1.0f, refRes.height), 0.0f, 0.0f}},
        ValueSourceRank::Local);
}

Size LayoutScaler::GetRefResolution() const noexcept {
    const UnifiedValue v = GetValue(PropId_RefResolution);
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        if (f4->x >= 1.0f && f4->y >= 1.0f) {
            return Size{f4->x, f4->y};
        }
    }
    return Size{960.0f, 540.0f};
}

Size LayoutScaler::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    UIElement* child = FirstChild();
    if (!child) {
        return Size{padW, padH};
    }
    const Size refRes = GetRefResolution();
    const bool hasW =
        std::isfinite(availableSize.width) && (availableSize.width < detail::kUnconstrainedSpace * 0.5f);
    const bool hasH =
        std::isfinite(availableSize.height) && (availableSize.height < detail::kUnconstrainedSpace * 0.5f);
    const float availW = hasW ? std::max(1.0f, availableSize.width - padW) : refRes.width;
    const float availH = hasH ? std::max(1.0f, availableSize.height - padH) : refRes.height;
    float scale = std::min(availW / std::max(1.0f, refRes.width), availH / std::max(1.0f, refRes.height));
    if (!std::isfinite(scale) || scale <= 1e-4f) {
        scale = 1.0f;
    }
    const Size childAvail{availW / scale, availH / scale};
    for (UIElement* c = child; c != nullptr; c = c->NextSibling()) {
        c->Measure(childAvail);
    }
    if (child->GetVisibility() == Visibility::Collapsed) {
        return Size{padW, padH};
    }
    return Size{
        hasW ? (availW + padW) : (child->DesiredSize().width * scale + padW),
        hasH ? (availH + padH) : (child->DesiredSize().height * scale + padH)};
}

void LayoutScaler::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));
    UIElement* child = FirstChild();
    if (!child || child->GetVisibility() == Visibility::Collapsed || innerW <= 0.0f || innerH <= 0.0f) {
        const bool resetChanged =
            (std::fabs(childScaleX_ - 1.0f) > 1e-5f) ||
            (std::fabs(childScaleY_ - 1.0f) > 1e-5f) ||
            (std::fabs(childOriginX_ - startX) > 1e-3f) ||
            (std::fabs(childOriginY_ - startY) > 1e-3f);
        childScaleX_ = 1.0f;
        childScaleY_ = 1.0f;
        childOriginX_ = startX;
        childOriginY_ = startY;
        for (UIElement* c = child; c != nullptr; c = c->NextSibling()) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
        }
        if (resetChanged) {
            InvalidateRender();
            for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
                detail::MarkSubtreeRenderDirtyRecursive(c);
            }
        }
        return;
    }
    const Size refRes = GetRefResolution();
    float scale = std::min(innerW / std::max(1.0f, refRes.width), innerH / std::max(1.0f, refRes.height));
    if (!std::isfinite(scale) || scale <= 1e-4f) {
        scale = 1.0f;
    }
    const bool scaleChanged =
        (std::fabs(childScaleX_ - scale) > 1e-5f) ||
        (std::fabs(childScaleY_ - scale) > 1e-5f) ||
        (std::fabs(childOriginX_ - startX) > 1e-3f) ||
        (std::fabs(childOriginY_ - startY) > 1e-3f);
    childScaleX_ = scale;
    childScaleY_ = scale;
    childOriginX_ = startX;
    childOriginY_ = startY;
    const Rect logicalSlot{startX, startY, innerW / scale, innerH / scale};
    for (UIElement* c = child; c != nullptr; c = c->NextSibling()) {
        c->Arrange(logicalSlot);
    }
    if (scaleChanged) {
        InvalidateRender();
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            detail::MarkSubtreeRenderDirtyRecursive(c);
        }
    }
}

// ============================================================================
// UserControl & Window
// ============================================================================

UserControl::UserControl() {
    EnsureControlTypesRegistered();
}

UserControl::UserControl(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine)
    : controlClassName_(className) {
    EnsureControlTypesRegistered();
    const std::shared_ptr<UIResourceContext> domainCtx =
        (UIResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UIResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUIResourceContext domainScope(domainCtx);
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext,
            animationEngine);
    }
}

bool UserControl::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    const std::shared_ptr<UIResourceContext> domainCtx =
        (UIResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UIResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUIResourceContext domainScope(domainCtx);

    const Interpreter* effInterp = eventInterpreter
        ? eventInterpreter
        : (bindingEngine ? bindingEngine->UiInterpreter() : nullptr);
    const ProgramImage* prog = bindingEngine ? bindingEngine->Program() : nullptr;

    AxbPackage boundPkg;
    const AxbPackage* effPkg = &package;
    ObjectRef ucCodeBehind{};
    PersistentRoot ucCtorPin{};
    const rs_sem::TypeSymbol* ucClsSym = nullptr;

    if (!package.className.empty() && domains && prog) {
        std::string modPrefix;
        std::string shortCls = package.className;
        if (const auto sep = shortCls.find("::"); sep != std::string::npos) {
            modPrefix = shortCls.substr(0u, sep);
            shortCls = shortCls.substr(sep + 2u);
        } else if (const auto dot = shortCls.rfind('.'); dot != std::string::npos) {
            modPrefix = shortCls.substr(0u, dot);
            shortCls = shortCls.substr(dot + 1u);
        }
        for (const auto& mod : prog->modules()) {
            if (!modPrefix.empty() && mod.name != modPrefix) {
                continue;
            }
            for (const auto& ts : mod.types) {
                if (ts.name == shortCls) {
                    ucClsSym = &ts;
                    break;
                }
            }
            if (ucClsSym) {
                break;
            }
        }
        bool dcIsAlreadyUcType = false;
        if (ucClsSym && dataContext.valid()) {
            if (auto* dh = domains->heapForObject(dataContext); dh && dh->isAlive(dataContext)) {
                dcIsAlreadyUcType = (dh->objectTypeId(dataContext).value_or(0) == ucClsSym->id);
            }
        }
        if (ucClsSym && !BoundScriptWrapper().valid() && !dcIsAlreadyUcType) {
            RuntimeError allocErr{};
            const NativeHandle rootNh = ToNativeHandle(domains->nativeHandles());
            std::vector<UnifiedValue> initFields(ucClsSym->fields.size(), UnifiedValue(NullObject{}));
            std::vector<std::size_t> refFields;
            for (std::size_t fi = 0u; fi < ucClsSym->fields.size(); ++fi) {
                const auto fType = ucClsSym->fields[fi].type;
                if (fType == rs_sem::PrimitiveType::Int || fType == rs_sem::PrimitiveType::Enum) {
                    initFields[fi] = UnifiedValue(std::int64_t{0});
                } else if (fType == rs_sem::PrimitiveType::Float) {
                    initFields[fi] = UnifiedValue(0.0);
                } else if (fType == rs_sem::PrimitiveType::Bool) {
                    initFields[fi] = UnifiedValue(false);
                } else if (fType == rs_sem::PrimitiveType::Handle) {
                    initFields[fi] = UnifiedValue(NativeHandle{});
                }
                if (ucClsSym->fields[fi].name == "__nativeRootHandle") {
                    initFields[fi] = UnifiedValue(rootNh);
                } else if (ucClsSym->fields[fi].name == "__aero_premounted" ||
                           ucClsSym->fields[fi].name == "__aero_component_loaded") {
                    initFields[fi] = UnifiedValue(true);
                }
                if (fType == rs_sem::PrimitiveType::Object ||
                    fType == rs_sem::PrimitiveType::String ||
                    fType == rs_sem::PrimitiveType::Array) {
                    refFields.push_back(fi);
                }
            }
            if (auto objOpt = domains->uiHeap().allocateObject(
                    ucClsSym->id, std::move(initFields), std::move(refFields), &allocErr)) {
                ucCodeBehind = *objOpt;
                ucCtorPin = domains->uiHeap().retain(ucCodeBehind);
                (void)BindScriptWrapper(domains->nativeHandles(), domains->uiHeap(), ucCodeBehind);
                SetHasRegisteredScriptWrapper(true);
            }
        }
        boundPkg = package;
        std::string actualVmType;
        if (dataContext.valid()) {
            if (auto* vh = domains->heapForObject(dataContext); vh && vh->isAlive(dataContext)) {
                const SymbolId vmTid = vh->objectTypeId(dataContext).value_or(0);
                if (const auto* vmTs = (vmTid != 0) ? prog->resolveType(vmTid) : nullptr) {
                    actualVmType = vmTs->moduleName.empty() ? vmTs->name : (vmTs->moduleName + "::" + vmTs->name);
                }
            }
        }
        (void)boundPkg.BindAgainstProgram(*prog, actualVmType, domains);
        boundPkg.PinValuesInPools();
        effPkg = &boundPkg;
    }

    templateMounted_ = MountAxbTemplate(
        this,
        *effPkg,
        domains,
        bindingEngine,
        dataContext,
        effInterp,
        eventAotContext,
        animationEngine);

    if (templateMounted_ && ucCodeBehind.valid() && ucClsSym && (effInterp || eventAotContext)) {
        SymbolId ctorId = 0;
        for (const auto& c : ucClsSym->constructors) {
            const bool isZeroArgInstance =
                !c.staticMethod &&
                (c.parameters.empty() || (c.parameters.size() == 1u && c.parameters[0].name == "this"));
            if (isZeroArgInstance && c.id != 0) {
                ctorId = c.id;
                break;
            }
        }
        if (ctorId == 0) {
            for (const auto& m : ucClsSym->methods) {
                if ((m.name == ".ctor" || m.name == ucClsSym->name) && !m.staticMethod &&
                    (m.parameters.empty() || (m.parameters.size() == 1u && m.parameters[0].name == "this")) &&
                    m.id != 0) {
                    ctorId = m.id;
                    break;
                }
            }
        }
        if (ctorId != 0) {
            bool invoked = false;
            if (eventAotContext != nullptr) {
                UnifiedValue ctorArg(ucCodeBehind);
                UnifiedValue ctorOut{};
                invoked = eventAotContext->invoke(ctorId, &ctorArg, 1u, ctorOut);
            }
            if (!invoked && effInterp != nullptr) {
                rs_rt::ExecutionOptions execOpts{};
                execOpts.limits.instructionBudget = 5'000'000;
                (void)effInterp->invoke(ctorId, {ucCodeBehind}, execOpts);
            }
        }
        if (bindingEngine) {
            (void)bindingEngine->FlushTrackA();
        }
        (void)DispatchWrapperRoutedEvent("Loaded");
        DispatchLoadedSubtree(true);
    }

    return templateMounted_;
}

Window::Window() {
    EnsureControlTypesRegistered();
}

Window::Window(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine)
    : windowClassName_(className) {
    EnsureControlTypesRegistered();
    const std::shared_ptr<UIResourceContext> domainCtx =
        (UIResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UIResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUIResourceContext domainScope(domainCtx);
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext,
            animationEngine);
    }
}

bool Window::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    const std::shared_ptr<UIResourceContext> domainCtx =
        (UIResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UIResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUIResourceContext domainScope(domainCtx);
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
    return templateMounted_;
}

bool UserControl::MatchesXamlAncestorTypeAlias(std::string_view wantedType) const noexcept {
    if (wantedType == "Page") {
        return true;
    }
    return ContentControl::MatchesXamlAncestorTypeAlias(wantedType);
}

bool UserControl::MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept {
    if (targetType == "Page") {
        return true;
    }
    return ContentControl::MatchesBindingAncestorLegacyFallback(targetType);
}

void Window::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "dialog";
    ioInfo.focusable = true;
}

bool Window::MatchesXamlAncestorTypeAlias(std::string_view wantedType) const noexcept {
    if (wantedType == "Page") {
        return true;
    }
    return ContentControl::MatchesXamlAncestorTypeAlias(wantedType);
}

bool Window::MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept {
    if (targetType == "Page") {
        return true;
    }
    return ContentControl::MatchesBindingAncestorLegacyFallback(targetType);
}

} // namespace aero
