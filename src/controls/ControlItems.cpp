#include "ControlsInternal.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/runtime/AeroObservableList.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"
#include "realscript/bytecode/Bytecode.h"

namespace aero {

AERO_IMPLEMENT_CLASS(ContentPresenter, Control, "Aero.UI.ContentPresenter")
AERO_IMPLEMENT_CLASS(ItemsControl, Control, "Aero.UI.ItemsControl")
AERO_IMPLEMENT_CLASS(HeaderedItemsControl, ItemsControl, "Aero.UI.HeaderedItemsControl")
AERO_IMPLEMENT_CLASS(MenuBase, ItemsControl, "Aero.UI.MenuBase")
AERO_IMPLEMENT_CLASS(Selector, ItemsControl, "Aero.UI.Selector")
AERO_IMPLEMENT_CLASS(ListBox, Selector, "Aero.UI.ListBox")
AERO_IMPLEMENT_CLASS(ListView, ListBox, "Aero.UI.ListView")
AERO_IMPLEMENT_CLASS(ComboBox, Selector, "Aero.UI.ComboBox")
AERO_IMPLEMENT_CLASS(ListBoxItem, ContentControl, "Aero.UI.ListBoxItem")
AERO_IMPLEMENT_CLASS(ComboBoxItem, ContentControl, "Aero.UI.ComboBoxItem")
AERO_IMPLEMENT_CLASS(ListViewItem, ListBoxItem, "Aero.UI.ListViewItem")
AERO_IMPLEMENT_CLASS(TabItem, HeaderedContentControl, "Aero.UI.TabItem")
AERO_IMPLEMENT_CLASS(TabControl, Selector, "Aero.UI.TabControl")
AERO_IMPLEMENT_CLASS(StatusBar, ItemsControl, "Aero.UI.StatusBar")
AERO_IMPLEMENT_CLASS(StatusBarItem, ContentControl, "Aero.UI.StatusBarItem")
AERO_IMPLEMENT_CLASS(ToolBar, HeaderedItemsControl, "Aero.UI.ToolBar")
AERO_IMPLEMENT_CLASS(Menu, MenuBase, "Aero.UI.Menu")
AERO_IMPLEMENT_CLASS(ContextMenu, MenuBase, "Aero.UI.ContextMenu")
AERO_IMPLEMENT_CLASS(MenuItem, HeaderedItemsControl, "Aero.UI.MenuItem")
AERO_IMPLEMENT_CLASS(TreeViewItem, HeaderedItemsControl, "Aero.UI.TreeViewItem")
AERO_IMPLEMENT_CLASS(TreeView, ItemsControl, "Aero.UI.TreeView")

// ============================================================================
// Shared Item, Display-Text & Panel Detail Helpers (aero::detail)
// ============================================================================

namespace detail {

std::string ExtractElementDisplayText(const UIElement* elem) {
    if (!elem) {
        return {};
    }
    if (const auto* tb = elem->DynamicCast<TextBlock>()) {
        std::string t = tb->GetText();
        if (!t.empty()) {
            return t;
        }
    }
    if (const auto* lbl = elem->DynamicCast<Label>()) {
        std::string t = lbl->GetText();
        if (!t.empty()) {
            return t;
        }
        t = lbl->GetContentText();
        if (!t.empty()) {
            return t;
        }
    }
    if (const auto* tbb = elem->DynamicCast<TextBoxBase>()) {
        std::string t = tbb->GetText();
        if (!t.empty()) {
            return t;
        }
    }
    if (const auto* bb = elem->DynamicCast<ButtonBase>()) {
        std::string t = bb->GetContentText();
        if (!t.empty()) {
            return t;
        }
    }
    if (const auto* mi = elem->DynamicCast<MenuItem>()) {
        std::string t = mi->GetContentText();
        if (!t.empty()) {
            return t;
        }
        t = mi->GetHeader();
        if (!t.empty()) {
            return t;
        }
    }
    if (const auto* cc = elem->DynamicCast<ContentControl>()) {
        std::string t = cc->GetContentText();
        if (!t.empty()) {
            return t;
        }
    }
    for (const auto propId : {PropId_Text, PropId_Content, PropId_Header, PropId_Title}) {
        const UnifiedValue v = elem->GetValue(propId);
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            if (!sv->view().empty()) {
                return sv->str();
            }
        }
    }
    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        std::string sub = ExtractElementDisplayText(c);
        if (!sub.empty()) {
            return sub;
        }
    }
    return {};
}

std::string ExtractSubtreeDisplayText(const UIElement* el) {
    if (!el) {
        return {};
    }
    if (const auto* tb = el->DynamicCast<TextBlock>()) {
        return tb->GetText();
    }
    for (const std::uint16_t pid : {PropId_Text, PropId_Content, PropId_Header}) {
        const UnifiedValue v = el->GetValue(pid);
        if (const auto* sv = std::get_if<StringValue>(&v); sv && !sv->view().empty()) {
            return sv->str();
        }
    }
    std::string combined;
    for (const UIElement* ch = el->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
        const std::string sub = ExtractSubtreeDisplayText(ch);
        if (!sub.empty()) {
            if (!combined.empty()) {
                combined.push_back(' ');
            }
            combined += sub;
        }
    }
    return combined;
}

bool IsTreeHeaderSlotChild(const UIElement* c) noexcept {
    if (!c) {
        return false;
    }
    const UnifiedValue hv = c->GetValue(PropId_Header);
    return std::holds_alternative<bool>(hv) && std::get<bool>(hv);
}

bool CollectionItemValuesEqual(const UnifiedValue& a, const UnifiedValue& b) noexcept {
    if (a.rawTag() != b.rawTag()) {
        return false;
    }
    return UnifiedValueEquals(a, b) || a == b;
}

std::string ResolveSmartObjectDisplayText(
    const UIElement* hostCtrl,
    ExecutionDomainContext* dom,
    DualTrackBindingEngine* bindEng,
    const UnifiedValue& itemVal) {
    if (const auto* sv = std::get_if<StringValue>(&itemVal)) {
        return sv->str();
    }
    const auto* oref = std::get_if<ObjectRef>(&itemVal);
    if (!oref || !oref->valid()) {
        return rs_rt::valueToString(itemVal);
    }
    if (!bindEng && hostCtrl) {
        for (const UIElement* cur = hostCtrl; cur != nullptr && !bindEng;
             cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
            bindEng = cur->BoundTwoWayEngine();
            if (!bindEng && cur->BoundStyleEngine()) {
                bindEng = cur->BoundStyleEngine()->GetBindingEngine();
            }
        }
    }
    if (!dom && bindEng) {
        dom = &bindEng->Domains();
    }
    if (dom && oref->kind == ObjectKind::String) {
        if (const auto sv = dom->stringViewAcrossDomains(*oref)) {
            return std::string(*sv);
        }
    }
    if (dom && bindEng && bindEng->Program()) {
        const ProgramImage* prog = bindEng->Program();
        if (const auto* heap = dom->heapForObject(*oref); heap && heap->isAlive(*oref)) {
            const SymbolId tid = heap->objectTypeId(*oref).value_or(0);
            if (tid != 0) {
                if (hostCtrl) {
                    const UnifiedValue dmpVal = hostCtrl->GetValue(PropId_DisplayMemberPath);
                    if (const auto* dmpSv = std::get_if<StringValue>(&dmpVal);
                        dmpSv && !dmpSv->view().empty()) {
                        if (auto slot = dom->resolvePropertySlot(*prog, tid, std::string(dmpSv->view()))) {
                            UnifiedValue fv{};
                            if (dom->readPropertySlotFast(
                                    DomainKind::Ui,
                                    *oref,
                                    *slot,
                                    fv,
                                    nullptr,
                                    bindEng->UiInterpreter())) {
                                return FormatBindingString({}, &fv, 1u, dom);
                            }
                        }
                    }
                }
                for (const char* cand :
                     {"Name", "Title", "Header", "Label", "Text", "Description", "Key"}) {
                    if (auto slot = dom->resolvePropertySlot(*prog, tid, cand)) {
                        UnifiedValue fv{};
                        if (dom->readPropertySlotFast(
                                DomainKind::Ui,
                                *oref,
                                *slot,
                                fv,
                                nullptr,
                                bindEng->UiInterpreter())) {
                            const std::string formatted = FormatBindingString({}, &fv, 1u, dom);
                            if (!formatted.empty() && formatted.rfind("<object:", 0) != 0) {
                                return formatted;
                            }
                        }
                    }
                }
                if (const auto* ts = prog->resolveType(tid)) {
                    for (const auto& f : ts->fields) {
                        if (!f.staticField) {
                            if (auto fv = heap->fieldGet(*oref, f.index)) {
                                if (std::holds_alternative<StringValue>(*fv) ||
                                    (std::holds_alternative<ObjectRef>(*fv) &&
                                     std::get<ObjectRef>(*fv).kind == ObjectKind::String)) {
                                    const std::string formatted =
                                        FormatBindingString({}, &(*fv), 1u, dom);
                                    if (!formatted.empty()) {
                                        return formatted;
                                    }
                                }
                            }
                        }
                    }
                    if (!ts->name.empty()) {
                        const std::size_t col = ts->name.rfind("::");
                        return (col != std::string::npos) ? ts->name.substr(col + 2u) : ts->name;
                    }
                }
            }
        }
    }
    return rs_rt::valueToString(itemVal);
}

ParsedItemsPanelConfig ResolveItemsPanelConfig(const ItemsControl* ic) noexcept {
    ParsedItemsPanelConfig cfg{};
    if (!ic) {
        return cfg;
    }
    std::string spec;
    const UnifiedValue raw = ic->GetValue(PropId_ItemsPanel);
    if (const auto* sv = std::get_if<StringValue>(&raw); sv && !sv->view().empty()) {
        spec = sv->str();
        if (spec.rfind("AERO_PANEL:", 0) != 0) {
            if (const UnifiedValue* res = AxbTemplateRegistry::Instance().FindResourceForElement(spec, ic)) {
                if (const auto* rsv = std::get_if<StringValue>(res)) {
                    spec = rsv->str();
                }
            }
        }
    }
    if (spec.rfind("AERO_PANEL:", 0) != 0) {
        return cfg;
    }
    std::string_view rest = std::string_view(spec).substr(11u);
    auto nextTok = [&]() -> std::string_view {
        const std::size_t c = rest.find(':');
        if (c == std::string_view::npos) {
            std::string_view t = rest;
            rest = {};
            return t;
        }
        std::string_view t = rest.substr(0u, c);
        rest = rest.substr(c + 1u);
        return t;
    };
    const std::string_view kind = nextTok();
    if (kind == "Canvas") {
        cfg.mode = ItemsPanelMode::Canvas;
    } else if (kind == "UniformGrid") {
        cfg.mode = ItemsPanelMode::UniformGrid;
        cfg.rows = std::max<std::int32_t>(
            0, CoerceInt32Property(value::FromString(std::string(nextTok())), 0));
        cfg.cols = std::max<std::int32_t>(
            0, CoerceInt32Property(value::FromString(std::string(nextTok())), 0));
        cfg.firstCol = std::max<std::int32_t>(
            0, CoerceInt32Property(value::FromString(std::string(nextTok())), 0));
    } else if (kind == "WrapPanel") {
        const std::string_view ori = nextTok();
        cfg.mode = (ori == "Vertical" || ori == "vertical")
            ? ItemsPanelMode::WrapVertical
            : ItemsPanelMode::WrapHorizontal;
        (void)TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Width, nextTok()), cfg.itemWidth);
        (void)TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Height, nextTok()), cfg.itemHeight);
    } else if (kind == "StackPanel" || kind == "VirtualizingStackPanel") {
        const std::string_view ori = nextTok();
        if (ori == "Horizontal" || ori == "horizontal") {
            cfg.mode = ItemsPanelMode::HorizontalStack;
        }
        const std::string_view sp = nextTok();
        if (!sp.empty()) {
            (void)TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Spacing, sp), cfg.spacing);
        }
    }
    return cfg;
}

UnifiedValue ResolveSelectedValueForItem(
    const UIElement* selector,
    ExecutionDomainContext* domains,
    const DualTrackBindingEngine* bindingEngine,
    const UnifiedValue& itemVal) {
    if (!selector || std::holds_alternative<std::monostate>(itemVal) ||
        std::holds_alternative<NullObject>(itemVal) ||
        std::holds_alternative<NullString>(itemVal)) {
        return UnifiedValue{};
    }
    const UnifiedValue svp = selector->GetValue(PropId_SelectedValuePath);
    const auto* svpStr = std::get_if<StringValue>(&svp);
    if (!svpStr || svpStr->view().empty()) {
        return itemVal;
    }
    const std::string_view path = svpStr->view();
    if (const auto* nh = std::get_if<NativeHandle>(&itemVal)) {
        const UIElement* itemEl = domains
            ? UIElement::FromNativeHandle(domains->nativeHandles(), *nh)
            : nullptr;
        if (!itemEl) {
            itemEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), *nh);
        }
        if (itemEl) {
            const std::uint16_t spid = ResolveTriggerConditionPropertyId(path);
            const UnifiedValue sub = itemEl->GetValue(spid);
            if (!std::holds_alternative<std::monostate>(sub)) {
                return sub;
            }
        }
    } else if (const auto* oref = std::get_if<ObjectRef>(&itemVal);
               oref && oref->valid() && domains && bindingEngine && bindingEngine->Program()) {
        if (const auto* oh = domains->heapForObject(*oref); oh && oh->isAlive(*oref)) {
            const SymbolId tid = oh->objectTypeId(*oref).value_or(0);
            if (auto slot = domains->resolvePropertySlot(*bindingEngine->Program(), tid, path)) {
                UnifiedValue sub{};
                if (domains->readPropertySlotFast(
                        DomainKind::Ui,
                        *oref,
                        *slot,
                        sub,
                        nullptr,
                        bindingEngine->UiInterpreter())) {
                    if (const auto* sr = std::get_if<ObjectRef>(&sub);
                        sr && sr->kind == ObjectKind::String) {
                        if (const auto sv = domains->stringViewAcrossDomains(*sr)) {
                            sub = value::FromString(std::string(*sv));
                        }
                    }
                    return sub;
                }
            }
        }
    }
    return itemVal;
}

} // namespace detail

namespace {

[[nodiscard]] bool IsMenuSubItemElement(const UIElement* elem) noexcept {
    return elem != nullptr && (elem->IsA<MenuItem>() || elem->IsA<Separator>());
}

[[nodiscard]] bool HasMenuSubItems(const UIElement* menuItem) noexcept {
    if (!menuItem) {
        return false;
    }
    for (const UIElement* c = menuItem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (IsMenuSubItemElement(c)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Size MeasureStackChildren(
    UIElement* host,
    Orientation orient,
    float spacing,
    Size availableSize) noexcept {
    const Thickness p = host->GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    float accumMain = 0.0f;
    float maxCross = 0.0f;
    std::size_t visibleCount = 0u;

    for (UIElement* c = host->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Measure(childAvail);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Measure(childAvail);
            continue;
        }
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            accumMain += d.height;
            maxCross = std::max(maxCross, d.width);
        } else {
            accumMain += d.width;
            maxCross = std::max(maxCross, d.height);
        }
        ++visibleCount;
    }

    if (visibleCount > 1u) {
        accumMain += spacing * static_cast<float>(visibleCount - 1u);
    }

    if (orient == Orientation::Vertical) {
        return Size{maxCross + padW, accumMain + padH};
    }
    return Size{accumMain + padW, maxCross + padH};
}

void ArrangeStackChildren(
    UIElement* host,
    Orientation orient,
    float spacing,
    Rect finalRect) noexcept {
    const Thickness p = host->GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    float cursor = (orient == Orientation::Vertical) ? startY : startX;
    for (UIElement* c = host->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Arrange(finalRect);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
            continue;
        }
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            c->Arrange(Rect{startX, cursor, innerW, d.height});
            cursor += d.height + spacing;
        } else {
            c->Arrange(Rect{cursor, startY, d.width, innerH});
            cursor += d.width + spacing;
        }
    }
}

void ReleaseItemsChildSubtreeResources(
    UIElement* ch,
    DualTrackBindingEngine* bindingEngine,
    StyleTriggerEngine* styleEngine,
    AnimationEngine* animationEngine) {
    if (!ch) {
        return;
    }
    DualTrackBindingEngine* liveBind =
        DualTrackBindingEngine::IsLiveInstance(bindingEngine) ? bindingEngine : nullptr;
    StyleTriggerEngine* liveStyle =
        StyleTriggerEngine::IsLiveInstance(styleEngine) ? styleEngine : nullptr;
    AnimationEngine* candAnim = animationEngine
        ? animationEngine
        : (liveBind ? liveBind->GetAnimationEngine() : nullptr);
    AnimationEngine* effAnim =
        AnimationEngine::IsLiveInstance(candAnim) ? candAnim : nullptr;
    if (effAnim) {
        effAnim->StopSubtreeAnimations(ch, false);
    }
    if (liveBind) {
        (void)liveBind->UnbindSubtree(ch);
    }
    if (liveStyle) {
        (void)liveStyle->UnbindSubtreeStyles(ch);
    }
}

[[nodiscard]] UIElement* GetTabItemHeaderElement(UIElement* tabElem) noexcept {
    if (auto* hcc = tabElem ? tabElem->DynamicCast<HeaderedContentControl>() : nullptr) {
        return hcc->GetHeaderElement();
    }
    return nullptr;
}

void ArrangeTabItemHeaderElement(UIElement* tabElem, const Rect& tabRect) noexcept {
    if (UIElement* hdrElem = GetTabItemHeaderElement(tabElem)) {
        hdrElem->Arrange(Rect{
            tabRect.x + 8.0f,
            tabRect.y + 4.0f,
            std::max(0.0f, tabRect.width - 16.0f),
            std::max(0.0f, tabRect.height - 8.0f)});
    }
}

} // namespace

// ============================================================================
// ContentPresenter
// ============================================================================

ContentPresenter::ContentPresenter() {
    EnsureControlTypesRegistered();
}

std::string ContentPresenter::GetContentSource() const {
    const UnifiedValue v = GetValue(PropId_ContentSource);
    if (const auto* sv = std::get_if<StringValue>(&v); sv && !sv->view().empty()) {
        return sv->str();
    }
    return "Content";
}

std::string ContentPresenter::GetEffectiveText() const {
    if (FirstChild() != nullptr) {
        return {};
    }
    auto tryExtractScalarOrString = [](const UnifiedValue& val, std::string& out) -> bool {
        if (const auto* sv = std::get_if<StringValue>(&val)) {
            out = sv->str();
            return true;
        }
        if (std::holds_alternative<std::int64_t>(val) ||
            std::holds_alternative<FloatValue>(val) ||
            std::holds_alternative<bool>(val)) {
            out = rs_rt::valueToString(val);
            return true;
        }
        return false;
    };
    std::string resolved;
    for (const std::uint16_t selfProp : {PropId_Content, PropId_Text}) {
        if (const auto* entry = FindCompactEntry(selfProp)) {
            if (tryExtractScalarOrString(entry->effectiveValue, resolved)) {
                return resolved;
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        const std::string cs = GetContentSource();
        if (cs == "Header") {
            for (const std::uint16_t hostProp : {PropId_Header, PropId_Title}) {
                if (const auto* entry = tp->FindCompactEntry(hostProp)) {
                    if (tryExtractScalarOrString(entry->effectiveValue, resolved)) {
                        return resolved;
                    }
                }
            }
            return {};
        }
        if (cs == "SelectionBoxItem") {
            if (const auto* combo = tp->DynamicCast<ComboBox>()) {
                return combo->GetSelectedItemText();
            }
        }
        for (const std::uint16_t hostProp : {PropId_Content, PropId_Text, PropId_Title}) {
            if (const auto* entry = tp->FindCompactEntry(hostProp)) {
                if (tryExtractScalarOrString(entry->effectiveValue, resolved)) {
                    return resolved;
                }
            }
        }
    }
    return {};
}

Color ContentPresenter::GetEffectiveForeground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *c;
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (auto c = value::TryGetColor(tp->GetValue(PropId_Foreground))) {
            return *c;
        }
    }
    return Color{0.96f, 0.98f, 1.0f, 1.0f};
}

float ContentPresenter::GetEffectiveFontSize() const noexcept {
    float out = 14.0f;
    if (TryGetFloat(GetValue(PropId_FontSize), out) && out > 0.0f) {
        return out;
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (TryGetFloat(tp->GetValue(PropId_FontSize), out) && out > 0.0f) {
            return out;
        }
    }
    return 14.0f;
}

std::string ContentPresenter::GetEffectiveFontFamily() const {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        const UnifiedValue v = tp->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
    }
    return {};
}

TextWrapping ContentPresenter::GetEffectiveTextWrapping() const noexcept {
    auto tryReadWrap = [](const UIElement* el, TextWrapping& outWrap) noexcept -> bool {
        if (!el) {
            return false;
        }
        const UnifiedValue v = el->GetValue(PropId_TextWrapping);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i == 1) outWrap = TextWrapping::Wrap;
            else if (*i == 2) outWrap = TextWrapping::WrapWithOverflow;
            else outWrap = TextWrapping::NoWrap;
            return true;
        }
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_TextWrapping, sv->view());
            if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
                if (*pi == 1) outWrap = TextWrapping::Wrap;
                else if (*pi == 2) outWrap = TextWrapping::WrapWithOverflow;
                else outWrap = TextWrapping::NoWrap;
                return true;
            }
        }
        return false;
    };
    TextWrapping w = TextWrapping::NoWrap;
    if (tryReadWrap(this, w)) return w;
    if (tryReadWrap(TemplatedParent(), w)) return w;
    return TextWrapping::NoWrap;
}

HorizontalAlignment ContentPresenter::GetEffectiveHorizontalContentAlignment() const noexcept {
    for (const UIElement* src :
         {static_cast<const UIElement*>(this), static_cast<const UIElement*>(TemplatedParent())}) {
        if (!src) continue;
        const UnifiedValue v = src->GetValue(PropId_HorizontalContentAlignment);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i >= 0 && *i <= 3) {
                return static_cast<HorizontalAlignment>(*i);
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (tp->IsA<Button>() && !tp->IsA<CheckBox>() && !tp->IsA<RadioButton>()) {
            return HorizontalAlignment::Center;
        }
    }
    return HorizontalAlignment::Left;
}

VerticalAlignment ContentPresenter::GetEffectiveVerticalContentAlignment() const noexcept {
    for (const UIElement* src :
         {static_cast<const UIElement*>(this), static_cast<const UIElement*>(TemplatedParent())}) {
        if (!src) continue;
        const UnifiedValue v = src->GetValue(PropId_VerticalContentAlignment);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i >= 0 && *i <= 3) {
                return static_cast<VerticalAlignment>(*i);
            }
        }
    }
    return VerticalAlignment::Center;
}

Point ContentPresenter::ComputeTextOrigin(const Rect& boxRect, Size textSize) const noexcept {
    const Thickness pad = GetPadding();
    const float innerX = boxRect.x + pad.left;
    const float innerY = boxRect.y + pad.top;
    const float innerW = std::max(0.0f, boxRect.width - (pad.left + pad.right));
    const float innerH = std::max(0.0f, boxRect.height - (pad.top + pad.bottom));

    float tx = innerX;
    switch (GetEffectiveHorizontalContentAlignment()) {
        case HorizontalAlignment::Left:
        case HorizontalAlignment::Stretch:
            tx = innerX;
            break;
        case HorizontalAlignment::Center:
            tx = innerX + std::max(0.0f, (innerW - textSize.width) * 0.5f);
            break;
        case HorizontalAlignment::Right:
            tx = innerX + std::max(0.0f, innerW - textSize.width);
            break;
    }

    float ty = innerY;
    switch (GetEffectiveVerticalContentAlignment()) {
        case VerticalAlignment::Top:
        case VerticalAlignment::Stretch:
            ty = innerY;
            break;
        case VerticalAlignment::Center:
            ty = innerY + std::max(0.0f, (innerH - textSize.height) * 0.5f);
            break;
        case VerticalAlignment::Bottom:
            ty = innerY + std::max(0.0f, innerH - textSize.height);
            break;
    }
    return Point{tx, ty};
}

void ContentPresenter::SyncContentTemplateChild() {
    detail::SyncHostContentTemplateChildImpl(
        this,
        TemplatedParent(),
        contentTemplateChild_,
        activeContentTemplateKey_,
        activeContentTemplateObj_,
        hasActiveContentTemplateObj_,
        syncingContentTemplate_);
}

void ContentPresenter::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (RefCount() > 0u &&
        (propId == PropId_ContentTemplate || propId == PropId_Content || propId == PropId_Text)) {
        SyncContentTemplateChild();
    }
}

Size ContentPresenter::MeasureCore(Size availableSize) noexcept {
    if (FirstChild() != nullptr) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const std::string text = GetEffectiveText();
    if (text.empty()) {
        return Size{padW, padH};
    }
    const float fontSize = GetEffectiveFontSize();
    const std::string family = GetEffectiveFontFamily();
    const TextWrapping wrapMode = GetEffectiveTextWrapping();
    const bool hasFiniteW =
        std::isfinite(availableSize.width) && (availableSize.width < detail::kUnconstrainedSpace * 0.5f);
    const float maxTextW = (wrapMode != TextWrapping::NoWrap && hasFiniteW)
        ? std::max(1.0f, availableSize.width - padW)
        : detail::kUnconstrainedSpace;
    const Size metrics = MeasureUtf8TextRun(text, fontSize, family, maxTextW, wrapMode);
    return Size{metrics.width + padW, metrics.height + padH};
}

// ============================================================================
// HeaderedItemsControl, MenuBase, Selector
// ============================================================================

HeaderedItemsControl::HeaderedItemsControl() {
    EnsureControlTypesRegistered();
}

void HeaderedItemsControl::SetHeader(std::string_view text) {
    SetValue(PropId_Header, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string HeaderedItemsControl::GetHeader() const {
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

void HeaderedItemsControl::SetHeaderTemplateKey(std::string_view key) {
    if (key.empty()) {
        (void)ClearValue(PropId_HeaderTemplate, ValueSourceRank::Local);
    } else {
        SetValue(PropId_HeaderTemplate, value::FromString(std::string(key)), ValueSourceRank::Local);
    }
}

std::string HeaderedItemsControl::GetHeaderTemplateKey() const {
    const UnifiedValue v = GetValue(PropId_HeaderTemplate);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void HeaderedItemsControl::SetHeaderElement(UIElement* element) {
    if (headerElement_.Get() != element) {
        headerElement_.Reset(element);
        InvalidateLayout();
        InvalidateRender();
    }
}

UIElement* HeaderedItemsControl::GetHeaderElement() const noexcept {
    if (headerElement_ &&
        (headerElement_->Parent() == this ||
         (HasControlTemplate() && headerElement_->Parent() != nullptr))) {
        return headerElement_.Get();
    }
    return nullptr;
}

bool HeaderedItemsControl::HasHeader() const noexcept {
    return GetHeaderElement() != nullptr || !GetHeader().empty();
}

MenuBase::MenuBase() {
    EnsureControlTypesRegistered();
}

void MenuBase::SetOrientation(Orientation orientation) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(orientation)),
        ValueSourceRank::Local);
}

Orientation MenuBase::GetOrientation() const noexcept {
    const UnifiedValue v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 1) ? Orientation::Horizontal : Orientation::Vertical;
    }
    return Orientation::Vertical;
}

void MenuBase::SetSpacing(float spacing) {
    SetValue(PropId_Spacing, MakeFloat(spacing), ValueSourceRank::Local);
}

float MenuBase::GetSpacing() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Spacing), out)) {
        return out;
    }
    return 0.0f;
}

Size MenuBase::MeasureCore(Size availableSize) noexcept {
    return MeasureStackChildren(this, GetOrientation(), GetSpacing(), availableSize);
}

void MenuBase::ArrangeCore(Rect finalRect) noexcept {
    ArrangeStackChildren(this, GetOrientation(), GetSpacing(), finalRect);
}

Selector::Selector() {
    EnsureControlTypesRegistered();
}

void Selector::SetSelectedValue(const UnifiedValue& value) {
    if (std::holds_alternative<std::monostate>(value) ||
        std::holds_alternative<NullObject>(value) ||
        std::holds_alternative<NullString>(value)) {
        (void)ClearValue(PropId_SelectedValue, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_SelectedValue, value, ValueSourceRank::Local);
    }
}

UnifiedValue Selector::GetSelectedValue() const {
    return GetValue(PropId_SelectedValue);
}

void Selector::SetSelectedValuePath(std::string_view path) {
    if (path.empty()) {
        (void)ClearValue(PropId_SelectedValuePath, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_SelectedValuePath, value::FromString(std::string(path)), ValueSourceRank::Local);
    }
}

std::string Selector::GetSelectedValuePath() const {
    const UnifiedValue v = GetValue(PropId_SelectedValuePath);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

bool Selector::SelectIndexInteractive(std::int32_t index) {
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    if (clamped != prev) {
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
        (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(clamped), true);
        return true;
    }
    return false;
}

// ============================================================================
// StatusBar, StatusBarItem, ToolBar, Menu, ContextMenu, MenuItem
// ============================================================================

StatusBar::StatusBar() {
    EnsureControlTypesRegistered();
    SetLastChildFill(false);
    (void)SetValue(
        PropId_Padding,
        value::FromThickness(Thickness{6.0f, 2.0f, 6.0f, 2.0f}),
        ValueSourceRank::Default);
    (void)SetValue(
        PropId_Background,
        value::FromColor(Color{0.13f, 0.15f, 0.20f, 0.95f}),
        ValueSourceRank::Default);
}

void StatusBar::SetLastChildFill(bool lastChildFill) {
    SetValue(PropId_LastChildFill, value::FromBool(lastChildFill), ValueSourceRank::Local);
}

bool StatusBar::GetLastChildFill() const noexcept {
    const UnifiedValue v = GetValue(PropId_LastChildFill);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return true;
}

Size StatusBar::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    const bool lastFill = GetLastChildFill();
    UIElement* lastVisible = nullptr;
    if (lastFill) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (!detail::IsFloatingPopupOverlay(c) && c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    float accumW = 0.0f;
    float accumH = 0.0f;
    float parentW = 0.0f;
    float parentH = 0.0f;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Measure(Size{innerAvailW, innerAvailH});
            continue;
        }
        const Size childConstraint{
            std::max(0.0f, innerAvailW - accumW),
            std::max(0.0f, innerAvailH - accumH)};
        c->Measure(childConstraint);
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Size d = c->DesiredSize();
        if (lastFill && c == lastVisible) {
            parentW = std::max(parentW, accumW + d.width);
            parentH = std::max(parentH, accumH + d.height);
        } else {
            switch (DockPanel::GetDock(c)) {
                case Dock::Left:
                case Dock::Right:
                    parentH = std::max(parentH, accumH + d.height);
                    accumW += d.width;
                    break;
                case Dock::Top:
                case Dock::Bottom:
                    parentW = std::max(parentW, accumW + d.width);
                    accumH += d.height;
                    break;
            }
        }
    }

    parentW = std::max(parentW, accumW);
    parentH = std::max(parentH, accumH);
    return Size{parentW + padW, parentH + padH};
}

void StatusBar::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    float left = finalRect.x + p.left;
    float top = finalRect.y + p.top;
    float right = left + std::max(0.0f, finalRect.width - (p.left + p.right));
    float bottom = top + std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const bool lastFill = GetLastChildFill();
    UIElement* lastVisible = nullptr;
    if (lastFill) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (!detail::IsFloatingPopupOverlay(c) && c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Arrange(finalRect);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{left, top, 0.0f, 0.0f});
            continue;
        }
        const float remW = std::max(0.0f, right - left);
        const float remH = std::max(0.0f, bottom - top);
        if (lastFill && c == lastVisible) {
            c->Arrange(Rect{left, top, remW, remH});
        } else {
            const Size d = c->DesiredSize();
            switch (DockPanel::GetDock(c)) {
                case Dock::Left: {
                    const float w = std::min(d.width, remW);
                    c->Arrange(Rect{left, top, w, remH});
                    left += w;
                    break;
                }
                case Dock::Right: {
                    const float w = std::min(d.width, remW);
                    c->Arrange(Rect{right - w, top, w, remH});
                    right -= w;
                    break;
                }
                case Dock::Top: {
                    const float h = std::min(d.height, remH);
                    c->Arrange(Rect{left, top, remW, h});
                    top += h;
                    break;
                }
                case Dock::Bottom: {
                    const float h = std::min(d.height, remH);
                    c->Arrange(Rect{left, bottom - h, remW, h});
                    bottom -= h;
                    break;
                }
            }
        }
    }
}

StatusBarItem::StatusBarItem() {
    EnsureControlTypesRegistered();
    (void)SetValue(
        PropId_Padding,
        value::FromThickness(Thickness{6.0f, 2.0f, 6.0f, 2.0f}),
        ValueSourceRank::Default);
}

std::string StatusBarItem::GetEffectiveText() const {
    if (std::string text = GetContentText(); !text.empty()) {
        return text;
    }
    for (const std::uint16_t propId : {PropId_Content, PropId_Text, PropId_Title}) {
        const UnifiedValue val = GetValue(propId);
        if (std::holds_alternative<std::int64_t>(val) ||
            std::holds_alternative<FloatValue>(val) ||
            std::holds_alternative<bool>(val)) {
            return rs_rt::valueToString(val);
        }
    }
    if (FirstChild() != nullptr) {
        return detail::ExtractElementDisplayText(this);
    }
    return {};
}

ToolBar::ToolBar() {
    EnsureControlTypesRegistered();
    SetOrientation(Orientation::Horizontal);
    (void)SetValue(PropId_Spacing, MakeFloat(4.0f), ValueSourceRank::Default);
    (void)SetValue(
        PropId_Padding,
        value::FromThickness(Thickness{4.0f, 2.0f, 4.0f, 2.0f}),
        ValueSourceRank::Default);
}

void ToolBar::SetOrientation(Orientation orientation) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(orientation)),
        ValueSourceRank::Local);
}

Orientation ToolBar::GetOrientation() const noexcept {
    const UnifiedValue v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
    }
    return Orientation::Horizontal;
}

void ToolBar::SetSpacing(float spacing) {
    SetValue(PropId_Spacing, MakeFloat(spacing), ValueSourceRank::Local);
}

float ToolBar::GetSpacing() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Spacing), out)) {
        return out;
    }
    return 0.0f;
}

std::string ToolBar::GetHeaderText() const {
    const UnifiedValue hv = GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&hv)) {
        return sv->str();
    }
    return {};
}

Size ToolBar::MeasureCore(Size availableSize) noexcept {
    const std::string hdr = GetHeaderText();
    float hdrW = 0.0f;
    float hdrH = 0.0f;
    if (!hdr.empty()) {
        float fs = 13.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fs);
        const Size tm = MeasureUtf8TextRun(hdr, fs);
        hdrW = tm.width + 12.0f;
        hdrH = std::max(24.0f, tm.height + 6.0f);
    }
    const Size inner = MeasureStackChildren(
        this,
        GetOrientation(),
        GetSpacing(),
        Size{std::max(0.0f, availableSize.width - hdrW), availableSize.height});
    return Size{inner.width + hdrW, std::max(inner.height, hdrH)};
}

void ToolBar::ArrangeCore(Rect finalRect) noexcept {
    const std::string hdr = GetHeaderText();
    float hdrW = 0.0f;
    if (!hdr.empty()) {
        float fs = 13.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fs);
        hdrW = MeasureUtf8TextRun(hdr, fs).width + 12.0f;
    }
    ArrangeStackChildren(
        this,
        GetOrientation(),
        GetSpacing(),
        Rect{
            finalRect.x + hdrW,
            finalRect.y,
            std::max(0.0f, finalRect.width - hdrW),
            finalRect.height});
}

Menu::Menu() {
    EnsureControlTypesRegistered();
    SetOrientation(Orientation::Horizontal);
    (void)SetValue(PropId_Spacing, MakeFloat(2.0f), ValueSourceRank::Default);
}

ContextMenu::ContextMenu() {
    EnsureControlTypesRegistered();
    SetOrientation(Orientation::Vertical);
    (void)SetValue(PropId_Spacing, MakeFloat(2.0f), ValueSourceRank::Default);
    (void)SetValue(
        PropId_Padding,
        value::FromThickness(Thickness{4.0f, 4.0f, 4.0f, 4.0f}),
        ValueSourceRank::Default);
    (void)SetValue(
        PropId_Background,
        value::FromColor(Color{0.15f, 0.17f, 0.22f, 0.98f}),
        ValueSourceRank::Default);
    (void)SetValue(
        PropId_BorderBrush,
        value::FromColor(Color{0.28f, 0.32f, 0.42f, 1.0f}),
        ValueSourceRank::Default);
    (void)SetValue(PropId_BorderThickness, value::FromThickness(Thickness{1.0f}), ValueSourceRank::Default);
    (void)SetValue(PropId_CornerRadius, value::FromCornerRadius(CornerRadius{6.0f}), ValueSourceRank::Default);
}

void ContextMenu::SetIsOpen(bool open) {
    SetValue(PropId_IsOpen, value::FromBool(open), ValueSourceRank::Local);
}

bool ContextMenu::IsOpen() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsOpen), false);
}

void ContextMenu::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    MenuBase::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsOpen ||
        propId == PropId_HorizontalOffset ||
        propId == PropId_VerticalOffset) {
        InvalidateLayout();
        detail::MarkSubtreeRenderDirtyRecursive(this);
    }
}

Size ContextMenu::MeasureCore(Size availableSize) noexcept {
    if (!IsOpen()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Measure(Size{0.0f, 0.0f});
        }
        return Size{0.0f, 0.0f};
    }
    return MenuBase::MeasureCore(availableSize);
}

void ContextMenu::ArrangeCore(Rect finalRect) noexcept {
    if (!IsOpen()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Arrange(Rect{finalRect.x, finalRect.y, 0.0f, 0.0f});
        }
        return;
    }
    float hOff = 0.0f;
    float vOff = 0.0f;
    (void)TryGetFloat(GetValue(PropId_HorizontalOffset), hOff);
    (void)TryGetFloat(GetValue(PropId_VerticalOffset), vOff);
    const Rect shifted{
        finalRect.x + hOff,
        finalRect.y + vOff,
        finalRect.width,
        finalRect.height};
    MenuBase::ArrangeCore(shifted);
}

MenuItem::MenuItem() {
    EnsureControlTypesRegistered();
    (void)SetValue(
        PropId_Padding,
        value::FromThickness(Thickness{10.0f, 4.0f, 10.0f, 4.0f}),
        ValueSourceRank::Default);
    (void)SetValue(PropId_HorizontalContentAlignment, value::FromInt(1), ValueSourceRank::Default);
}

MenuItem::~MenuItem() {
    if (submenuCounted_) {
        UIElement::DecrementOpenMenuSubmenuCount();
        submenuCounted_ = false;
    }
}

bool MenuItem::HasAnyOpenSubmenu() noexcept {
    return UIElement::HasAnyOpenMenuSubmenu();
}

void MenuItem::SetContentText(std::string_view text) {
    (void)SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string MenuItem::GetContentText() const {
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

bool MenuItem::PerformClick(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    IntrusivePtr<MenuItem> selfGuard(this, true);
    ++clickGeneration_;
    NotifyBubbledClick();
    if (clickCallback_) {
        clickCallback_(*this);
    }
    return detail::DispatchClickCommandAndScript(*this, clickBinding_, error);
}

bool MenuItem::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (HasMenuSubItems(this)) {
        const UnifiedValue ov = GetValue(PropId_IsSubmenuOpen);
        const bool nextOpen = !(std::holds_alternative<bool>(ov) && std::get<bool>(ov));
        if (nextOpen && Parent()) {
            for (UIElement* sib = Parent()->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                if (sib != this && sib->IsA<MenuItem>()) {
                    sib->SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
                }
            }
        }
        SetValue(PropId_IsSubmenuOpen, UnifiedValue{nextOpen}, ValueSourceRank::Local);
        InvalidateMeasure();
        return PerformClick(error);
    }
    const UnifiedValue chkVal = GetValue(PropId_IsCheckable);
    if (std::holds_alternative<bool>(chkVal) && std::get<bool>(chkVal)) {
        const UnifiedValue ic = GetValue(PropId_IsChecked);
        const bool next = !(std::holds_alternative<bool>(ic) && std::get<bool>(ic));
        SetValue(PropId_IsChecked, UnifiedValue{next}, ValueSourceRank::Local);
        (void)NotifyTwoWayPropertyMutated(PropId_IsChecked);
    }
    const bool ok = PerformClick(error);
    for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (anc->IsA<MenuItem>()) {
            anc->SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
        }
        if (anc->IsA<ContextMenu>()) {
            anc->SetValue(PropId_IsOpen, UnifiedValue{false}, ValueSourceRank::Local);
            break;
        }
        if (anc->IsA<Menu>()) {
            for (UIElement* topMi = anc->FirstChild(); topMi != nullptr; topMi = topMi->NextSibling()) {
                if (topMi->IsA<MenuItem>()) {
                    topMi->SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
                }
            }
            break;
        }
    }
    return ok;
}

void MenuItem::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    HeaderedItemsControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsSubmenuOpen) {
        bool isOpen = false;
        (void)newValue.tryGetBool(isOpen);
        if (isOpen && !submenuCounted_) {
            submenuCounted_ = true;
            UIElement::IncrementOpenMenuSubmenuCount();
        } else if (!isOpen && submenuCounted_) {
            submenuCounted_ = false;
            UIElement::DecrementOpenMenuSubmenuCount();
        }
    }
    if (propId == PropId_Header ||
        propId == PropId_InputGestureText ||
        propId == PropId_IsChecked ||
        propId == PropId_IsCheckable) {
        std::string label;
        const UnifiedValue hv = GetValue(PropId_Header);
        if (const auto* sv = std::get_if<StringValue>(&hv)) {
            label = sv->str();
        }
        const UnifiedValue chkVal = GetValue(PropId_IsCheckable);
        if (std::holds_alternative<bool>(chkVal) && std::get<bool>(chkVal)) {
            const UnifiedValue ic = GetValue(PropId_IsChecked);
            const bool checked = std::holds_alternative<bool>(ic) && std::get<bool>(ic);
            label = (checked ? "[x] " : "[ ] ") + label;
        }
        const UnifiedValue gv = GetValue(PropId_InputGestureText);
        if (const auto* gs = std::get_if<StringValue>(&gv); gs && !gs->view().empty()) {
            label += "    " + gs->str();
        }
        if (!label.empty()) {
            (void)SetValue(PropId_Text, value::FromString(label), ValueSourceRank::Default);
        }
        InvalidateMeasure();
    }
}

bool MenuItem::OnPointerDown(
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

bool MenuItem::OnPointerUp(
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

bool MenuItem::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const bool hasSubItems = HasMenuSubItems(this);
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        return ActivateFromInput();
    }
    if (key == KeyCode::Escape) {
        bool closedAny = false;
        if (IsSubmenuOpen()) {
            SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
            closedAny = true;
        }
        for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
            if (anc->IsA<MenuItem>()) {
                anc->SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
                closedAny = true;
            } else if (anc->IsA<ContextMenu>()) {
                anc->SetValue(PropId_IsOpen, UnifiedValue{false}, ValueSourceRank::Local);
                closedAny = true;
                break;
            }
        }
        if (closedAny) {
            InvalidateMeasure();
        }
        return closedAny;
    }
    if ((key == KeyCode::Right || (key == KeyCode::Down && Parent() && Parent()->IsA<Menu>())) &&
        hasSubItems && !IsSubmenuOpen()) {
        SetValue(PropId_IsSubmenuOpen, UnifiedValue{true}, ValueSourceRank::Local);
        InvalidateMeasure();
        return true;
    }
    if (key == KeyCode::Left && IsSubmenuOpen()) {
        SetValue(PropId_IsSubmenuOpen, UnifiedValue{false}, ValueSourceRank::Local);
        InvalidateMeasure();
        return true;
    }
    return HeaderedItemsControl::OnKeyDown(key, modifiers);
}

Size MenuItem::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return Control::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    std::string label = GetContentText();
    if (label.empty()) {
        label = GetHeader();
    }
    const Size tm = MeasureUtf8TextRun(label, fontSize);
    float w = tm.width + p.left + p.right + 12.0f;
    float h = std::max(24.0f, tm.height + p.top + p.bottom);

    const UnifiedValue ov = GetValue(PropId_IsSubmenuOpen);
    const bool subOpen = std::holds_alternative<bool>(ov) && std::get<bool>(ov);
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (IsMenuSubItemElement(c)) {
            if (subOpen) {
                c->Measure(availableSize);
                w = std::max(w, c->DesiredSize().width + 12.0f);
                h += c->DesiredSize().height;
            } else {
                c->Measure(Size{0.0f, 0.0f});
            }
        } else {
            c->Measure(availableSize);
            w = std::max(w, c->DesiredSize().width + p.left + p.right);
            h = std::max(h, c->DesiredSize().height + p.top + p.bottom);
        }
    }
    return Size{w, h};
}

void MenuItem::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        Control::ArrangeCore(finalRect);
        return;
    }
    const UnifiedValue ov = GetValue(PropId_IsSubmenuOpen);
    const bool subOpen = std::holds_alternative<bool>(ov) && std::get<bool>(ov);
    const bool nestedInMenuItem = Parent() != nullptr && Parent()->IsA<MenuItem>();
    const float subX = nestedInMenuItem
        ? (finalRect.x + std::max(108.0f, finalRect.width - 4.0f))
        : (finalRect.x + 8.0f);
    float curY = nestedInMenuItem ? finalRect.y : (finalRect.y + 26.0f);
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (IsMenuSubItemElement(c)) {
            if (subOpen) {
                const float ch = c->DesiredSize().height;
                const float subW = std::max(132.0f, c->DesiredSize().width);
                c->Arrange(Rect{subX, curY, subW, ch});
                curY += ch;
            } else {
                c->Arrange(Rect{finalRect.x, finalRect.y, 0.0f, 0.0f});
            }
        } else {
            const float iconSz = std::min(18.0f, std::max(12.0f, finalRect.height - 6.0f));
            c->Arrange(Rect{
                finalRect.x + 6.0f,
                finalRect.y + std::max(2.0f, (24.0f - iconSz) * 0.5f),
                iconSz,
                iconSz});
        }
    }
}

// ============================================================================
// TreeViewItem & TreeView
// ============================================================================

TreeViewItem::TreeViewItem() {
    EnsureControlTypesRegistered();
}

void TreeViewItem::SetHeader(std::string_view header) {
    SetValue(PropId_Header, value::FromString(std::string(header)), ValueSourceRank::Local);
}

std::string TreeViewItem::GetHeader() const {
    const UnifiedValue hv = GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&hv)) {
        return sv->str();
    }
    const UnifiedValue tv = GetValue(PropId_Text);
    if (const auto* sv = std::get_if<StringValue>(&tv)) {
        return sv->str();
    }
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c)) {
            const std::string extracted = detail::ExtractSubtreeDisplayText(c);
            if (!extracted.empty()) {
                return extracted;
            }
        }
    }
    const UnifiedValue dcVal = GetValue(PropId_DataContext);
    if (const auto* sv = std::get_if<StringValue>(&dcVal)) {
        return sv->str();
    }
    if (const auto* objRef = std::get_if<ObjectRef>(&dcVal); objRef && objRef->valid()) {
        ExecutionDomainContext* dom = Domains();
        DualTrackBindingEngine* be = BindingEngine();
        for (const UIElement* anc = Parent(); (!dom || !be) && anc != nullptr; anc = anc->Parent()) {
            if (!be) {
                be = anc->BoundTwoWayEngine();
            }
            if (!dom && be) {
                dom = &be->Domains();
            }
        }
        return detail::ResolveSmartObjectDisplayText(this, dom, be, dcVal);
    }
    return {};
}

void TreeViewItem::SetIsExpanded(bool expanded) {
    SetValue(PropId_IsExpanded, UnifiedValue{expanded}, ValueSourceRank::Local);
}

bool TreeViewItem::IsExpanded() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsExpanded);
    return std::holds_alternative<bool>(v) && std::get<bool>(v);
}

void TreeViewItem::SetIsSelected(bool selected) {
    SetValue(PropId_IsChecked, UnifiedValue{selected}, ValueSourceRank::Local);
}

bool TreeViewItem::IsSelected() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsChecked);
    return std::holds_alternative<bool>(v) && std::get<bool>(v);
}

TreeView* TreeViewItem::FindAncestorTreeView() noexcept {
    for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (auto* tv = anc->DynamicCast<TreeView>()) {
            return tv;
        }
    }
    return nullptr;
}

void TreeViewItem::SyncHasItemsFlag() noexcept {
    bool hasAny = false;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (!detail::IsTreeHeaderSlotChild(c) && c != TemplateRoot()) {
            hasAny = true;
            break;
        }
    }
    (void)SetValue(PropId_HasItems, UnifiedValue{hasAny}, ValueSourceRank::Default);
}

void TreeViewItem::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    HeaderedItemsControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsExpanded || propId == PropId_Header || propId == PropId_IsChecked) {
        SyncHasItemsFlag();
        InvalidateMeasure();
        if (UIElement* p = Parent()) {
            p->InvalidateMeasure();
        }
    }
    if (propId == PropId_IsChecked) {
        if (TreeView* tv = FindAncestorTreeView()) {
            if (IsSelected()) {
                if (tv->SelectedContainer() != this) {
                    tv->SelectContainer(this);
                }
            } else if (tv->SelectedContainer() == this) {
                tv->SelectContainer(nullptr);
            }
        }
    } else if ((propId == PropId_Header || propId == PropId_Text || propId == PropId_DataContext) &&
               IsSelected()) {
        if (TreeView* tv = FindAncestorTreeView()) {
            if (tv->SelectedContainer() == this) {
                tv->SelectContainer(this);
            }
        }
    }
}

bool TreeViewItem::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    if (localPoint.y > 28.0f && IsExpanded() && FirstChild() != nullptr && !HasControlTemplate()) {
        return false;
    }
    SyncHasItemsFlag();
    const UnifiedValue hiVal = GetValue(PropId_HasItems);
    const bool hasChildren = std::holds_alternative<bool>(hiVal) && std::get<bool>(hiVal);
    if (hasChildren && localPoint.x <= 24.0f) {
        SetIsExpanded(!IsExpanded());
        (void)NotifyTwoWayPropertyMutated(PropId_IsExpanded);
    }
    if (TreeView* tv = FindAncestorTreeView()) {
        tv->SelectContainer(this);
    } else {
        SetIsSelected(true);
    }
    return true;
}

Size TreeViewItem::MeasureCore(Size availableSize) noexcept {
    SyncHasItemsFlag();
    if (HasControlTemplate()) {
        return HeaderedItemsControl::MeasureCore(availableSize);
    }
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const std::string hdr = GetHeader();
    const Size hdrMetrics = MeasureUtf8TextRun(hdr, fontSize);
    float headerW = 22.0f + hdrMetrics.width + 12.0f;
    float headerH = std::max(24.0f, hdrMetrics.height + 6.0f);

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c)) {
            c->Measure(availableSize);
            headerW = std::max(headerW, 22.0f + c->DesiredSize().width + 8.0f);
            headerH = std::max(headerH, c->DesiredSize().height + 4.0f);
        }
    }

    float maxW = headerW;
    float totalH = headerH;
    const bool expanded = IsExpanded();
    constexpr float kIndent = 18.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c) || c == TemplateRoot()) {
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (expanded) {
            c->Measure(Size{std::max(0.0f, availableSize.width - kIndent), availableSize.height});
            maxW = std::max(maxW, kIndent + c->DesiredSize().width);
            totalH += c->DesiredSize().height;
        } else {
            c->Measure(Size{0.0f, 0.0f});
        }
    }
    return Size{maxW, totalH};
}

void TreeViewItem::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        HeaderedItemsControl::ArrangeCore(finalRect);
        return;
    }
    float headerH = 24.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c)) {
            headerH = std::max(headerH, c->DesiredSize().height + 4.0f);
            c->Arrange(Rect{
                finalRect.x + 22.0f,
                finalRect.y + 2.0f,
                std::max(0.0f, finalRect.width - 22.0f),
                headerH - 4.0f});
        }
    }
    float curY = finalRect.y + headerH;
    const bool expanded = IsExpanded();
    constexpr float kIndent = 18.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c) || c == TemplateRoot()) {
            continue;
        }
        if (!expanded || c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{finalRect.x, curY, 0.0f, 0.0f});
            continue;
        }
        const float ch = c->DesiredSize().height;
        c->Arrange(Rect{finalRect.x + kIndent, curY, std::max(0.0f, finalRect.width - kIndent), ch});
        curY += ch;
    }
}

TreeView::TreeView() {
    EnsureControlTypesRegistered();
}

void TreeView::SelectContainer(TreeViewItem* item) {
    if (selecting_) {
        return;
    }
    selecting_ = true;
    TreeViewItem* const prevSelected = selectedContainer_;
    if (selectedContainer_ && selectedContainer_ != item) {
        selectedContainer_->SetIsSelected(false);
    }
    selectedContainer_ = item;
    DualTrackBindingEngine* be = BoundTwoWayEngine();
    for (UIElement* anc = Parent(); !be && anc != nullptr; anc = anc->Parent()) {
        be = anc->BoundTwoWayEngine();
    }
    if (selectedContainer_) {
        if (!selectedContainer_->IsSelected()) {
            selectedContainer_->SetIsSelected(true);
        }
        const UnifiedValue dcVal = selectedContainer_->GetValue(PropId_DataContext);
        if (const auto* objRef = std::get_if<ObjectRef>(&dcVal); objRef && objRef->valid()) {
            SetValue(PropId_SelectedItem, UnifiedValue{*objRef}, ValueSourceRank::Local);
        } else if (be) {
            SetValue(
                PropId_SelectedItem,
                UnifiedValue{selectedContainer_->ToNativeHandle(be->Domains().nativeHandles())},
                ValueSourceRank::Local);
        } else {
            SetValue(PropId_SelectedItem, value::FromString(selectedContainer_->GetHeader()), ValueSourceRank::Local);
        }
    } else {
        SetValue(PropId_SelectedItem, UnifiedValue{NullObject{}}, ValueSourceRank::Local);
    }
    selecting_ = false;
    (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
    if (be) {
        (void)be->FlushTrackA();
    }
    if (prevSelected != selectedContainer_) {
        if (selectedItemChangedCallback_) {
            selectedItemChangedCallback_(*this, selectedContainer_);
        }
        (void)DispatchWrapperRoutedEvent("SelectedItemChanged", value::FromString(GetSelectedHeader()), true);
    }
}

std::string TreeView::GetSelectedHeader() const {
    if (selectedContainer_) {
        return selectedContainer_->GetHeader();
    }
    return {};
}

bool TreeView::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    std::vector<TreeViewItem*> visibleItems;
    auto collectVisible = [&](auto& self, UIElement* node) -> void {
        for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() == Visibility::Collapsed) {
                continue;
            }
            if (auto* tvi = c->DynamicCast<TreeViewItem>()) {
                visibleItems.push_back(tvi);
                if (tvi->IsExpanded()) {
                    self(self, tvi);
                }
            }
        }
    };
    collectVisible(collectVisible, this);
    if (visibleItems.empty()) {
        return false;
    }
    std::ptrdiff_t curIdx = -1;
    for (std::size_t i = 0u; i < visibleItems.size(); ++i) {
        if (visibleItems[i] == selectedContainer_) {
            curIdx = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }
    if (key == KeyCode::Down) {
        const std::size_t nextIdx = (curIdx < 0)
            ? 0u
            : std::min<std::size_t>(visibleItems.size() - 1u, static_cast<std::size_t>(curIdx + 1));
        SelectContainer(visibleItems[nextIdx]);
        return true;
    }
    if (key == KeyCode::Up) {
        const std::size_t prevIdx = (curIdx <= 0) ? 0u : static_cast<std::size_t>(curIdx - 1);
        SelectContainer(visibleItems[prevIdx]);
        return true;
    }
    if (key == KeyCode::Right && selectedContainer_) {
        selectedContainer_->SyncHasItemsFlag();
        const UnifiedValue hi = selectedContainer_->GetValue(PropId_HasItems);
        if (std::holds_alternative<bool>(hi) && std::get<bool>(hi) && !selectedContainer_->IsExpanded()) {
            selectedContainer_->SetIsExpanded(true);
            return true;
        }
    }
    if (key == KeyCode::Left && selectedContainer_) {
        if (selectedContainer_->IsExpanded()) {
            selectedContainer_->SetIsExpanded(false);
            return true;
        }
        if (auto* parentTvi = selectedContainer_->Parent()
                ? selectedContainer_->Parent()->DynamicCast<TreeViewItem>()
                : nullptr) {
            SelectContainer(parentTvi);
            return true;
        }
    }
    return false;
}

// ============================================================================
// ItemsControl (with Viewport-Aware VirtualizingStackPanel Recycling)
// ============================================================================

ItemsControl::ItemsControl() {
    EnsureControlTypesRegistered();
}

ItemsControl::~ItemsControl() {
    ClearRecyclePool();
}

bool ItemsControl::ShouldSkipControlTemplatePackage(const AxbPackage& package) const noexcept {
    if (IsA<ComboBox>()) {
        return false;
    }
    for (const std::string& s : package.stringTable) {
        if (s == "ItemsPresenter" || s == "ScrollViewer") {
            return true;
        }
    }
    return false;
}

void ItemsControl::SetIsVirtualizing(bool enabled) {
    SetValue(PropId_IsVirtualizing, UnifiedValue{enabled}, ValueSourceRank::Local);
}

bool ItemsControl::IsVirtualizing() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsVirtualizing);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void ItemsControl::SetVirtualItemHeight(float itemHeight) {
    if (itemHeight > 0.0f && std::isfinite(itemHeight)) {
        SetValue(PropId_ItemHeight, MakeFloat(itemHeight), ValueSourceRank::Local);
    } else {
        (void)ClearValue(PropId_ItemHeight, ValueSourceRank::Local);
    }
}

float ItemsControl::GetVirtualItemHeight() const noexcept {
    float h = 24.0f;
    if (TryGetFloat(GetValue(PropId_ItemHeight), h) && h > 1.0f && std::isfinite(h)) {
        return h;
    }
    return 24.0f;
}

const ScrollViewer* ItemsControl::FindAncestorScrollViewer() const noexcept {
    for (const UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (const auto* sv = anc->DynamicCast<ScrollViewer>()) {
            return sv;
        }
    }
    return nullptr;
}

bool ItemsControl::ComputeLiveScrollViewport(float& outViewTop, float& outViewH) const noexcept {
    const ScrollViewer* sv = FindAncestorScrollViewer();
    if (!sv) {
        return false;
    }
    const Thickness p = GetPadding();
    const Rect clip = sv->GetLocalClipBounds();
    if (clip.height > 0.0f) {
        outViewH = clip.height;
    } else if (sv->ViewportHeight() > 0.0f) {
        outViewH = sv->ViewportHeight();
    } else if (sv->GetHeight() > 0.0f) {
        const Thickness svPad = sv->GetPadding();
        outViewH = std::max(0.0f, sv->GetHeight() - (svPad.top + svPad.bottom));
    } else {
        outViewH = 240.0f;
    }

    const UIElement* svChild = this;
    while (svChild->Parent() != nullptr && svChild->Parent() != sv) {
        svChild = svChild->Parent();
    }

    float staticRowStartInContent = GetMargin().top + p.top;
    if (this != svChild) {
        if (FinalRect().height > 0.0f || svChild->FinalRect().height > 0.0f) {
            staticRowStartInContent =
                (FinalRect().y - (svChild->FinalRect().y - svChild->GetMargin().top)) + p.top;
        } else {
            for (const UIElement* cur = Parent(); cur != nullptr && cur != sv; cur = cur->Parent()) {
                staticRowStartInContent += cur->GetMargin().top + cur->GetPadding().top;
            }
        }
    }

    const float liveScrollY = std::max(0.0f, sv->GetVerticalOffset());
    outViewTop = std::max(0.0f, liveScrollY - staticRowStartInContent);
    return true;
}

ExecutionDomainContext* ItemsControl::EffectiveDomains() const noexcept {
    if (domains_) {
        return domains_;
    }
    for (const UIElement* cur = this; cur != nullptr;
         cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
        if (const UIViewContext* vc = cur->ViewContext()) {
            if (ExecutionDomainContext* dom = vc->GetDomains()) {
                return dom;
            }
        }
    }
    if (DualTrackBindingEngine* be = EffectiveBindingEngine()) {
        return &be->Domains();
    }
    return nullptr;
}

DualTrackBindingEngine* ItemsControl::EffectiveBindingEngine() const noexcept {
    if (bindingEngine_) {
        return bindingEngine_;
    }
    if (DualTrackBindingEngine* bound = BoundTwoWayEngine()) {
        return bound;
    }
    if (StyleTriggerEngine* se = EffectiveStyleEngine()) {
        return se->GetBindingEngine();
    }
    return nullptr;
}

StyleTriggerEngine* ItemsControl::EffectiveStyleEngine() const noexcept {
    return styleEngine_ ? styleEngine_ : BoundStyleEngine();
}

AnimationEngine* ItemsControl::EffectiveAnimationEngine() const noexcept {
    if (animationEngine_) {
        return animationEngine_;
    }
    if (AnimationEngine* bound = BoundAnimationEngine()) {
        return bound;
    }
    if (const DualTrackBindingEngine* be = EffectiveBindingEngine()) {
        if (AnimationEngine* eng = be->GetAnimationEngine()) {
            return eng;
        }
    }
    if (const StyleTriggerEngine* se = EffectiveStyleEngine()) {
        return se->GetAnimationEngine();
    }
    return nullptr;
}

void ItemsControl::SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept {
    if (styleEngine_ == styleEngine) {
        return;
    }
    styleEngine_ = styleEngine;
    if (styleEngine_) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            (void)styleEngine_->BindSubtreeStyles(c);
            (void)styleEngine_->EvaluateSubtree(c);
        }
    }
}

void ItemsControl::SetItemTemplateKey(std::string_view key) {
    if (key.empty()) {
        (void)ClearValue(PropId_ItemTemplate, ValueSourceRank::Local);
    } else {
        SetValue(PropId_ItemTemplate, value::FromString(std::string(key)), ValueSourceRank::Local);
    }
}

std::string ItemsControl::GetItemTemplateKey() const {
    const UnifiedValue v = GetValue(PropId_ItemTemplate);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

std::size_t ItemsControl::FirstItemChildOffset() const noexcept {
    if (!IsA<TreeViewItem>()) {
        return 0u;
    }
    std::size_t offset = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsTreeHeaderSlotChild(c)) {
            ++offset;
        } else {
            break;
        }
    }
    return offset;
}

const AxbDataTemplate* ItemsControl::ResolveActiveDataTemplateEntry(
    const UnifiedValue& sampleItem) const noexcept {
    const std::string key = GetItemTemplateKey();
    if (!key.empty()) {
        if (const AxbDataTemplate* found = AxbTemplateRegistry::Instance().FindDataTemplateEntry(key)) {
            return found;
        }
    }
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    if (const auto* nh = std::get_if<NativeHandle>(&sampleItem)) {
        const UIElement* itemEl = effDomains
            ? UIElement::FromNativeHandle(effDomains->nativeHandles(), *nh)
            : nullptr;
        if (!itemEl) {
            itemEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), *nh);
        }
        if (itemEl && !itemEl->DeclaredTypeName().empty()) {
            if (const AxbDataTemplate* found =
                    AxbTemplateRegistry::Instance().FindImplicitDataTemplateEntry(
                        itemEl->DeclaredTypeName())) {
                return found;
            }
        }
    }
    if (const auto* objRef = std::get_if<ObjectRef>(&sampleItem)) {
        if (objRef->valid() && effDomains && effBind && effBind->Program()) {
            if (auto* heap = effDomains->heapForObject(*objRef)) {
                if (const auto tid = heap->objectTypeId(*objRef)) {
                    if (const auto* tSym = effBind->Program()->resolveType(*tid)) {
                        if (const AxbDataTemplate* found =
                                AxbTemplateRegistry::Instance().FindImplicitDataTemplateEntry(tSym->name)) {
                            return found;
                        }
                    }
                }
            }
        }
    }
    if (IsA<TreeViewItem>()) {
        for (const UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
            if (const auto* ancIc = anc->DynamicCast<ItemsControl>()) {
                const std::string ancKey = ancIc->GetItemTemplateKey();
                if (!ancKey.empty()) {
                    if (const AxbDataTemplate* found =
                            AxbTemplateRegistry::Instance().FindDataTemplateEntry(ancKey)) {
                        return found;
                    }
                }
            }
            if (anc->IsA<TreeView>()) {
                break;
            }
        }
    }
    return nullptr;
}

const AxbPackage* ItemsControl::ResolveActiveItemTemplate(
    const UnifiedValue& sampleItem) const noexcept {
    if (const AxbDataTemplate* dtEntry = ResolveActiveDataTemplateEntry(sampleItem);
        dtEntry && dtEntry->templatePackage) {
        return dtEntry->templatePackage.get();
    }
    const std::string key = GetItemTemplateKey();
    if (!key.empty()) {
        return AxbTemplateRegistry::Instance().FindDataTemplate(key);
    }
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    if (const auto* objRef = std::get_if<ObjectRef>(&sampleItem)) {
        if (objRef->valid() && effDomains && effBind && effBind->Program()) {
            if (auto* heap = effDomains->heapForObject(*objRef)) {
                if (const auto tid = heap->objectTypeId(*objRef)) {
                    if (const auto* tSym = effBind->Program()->resolveType(*tid)) {
                        return AxbTemplateRegistry::Instance().FindImplicitDataTemplate(tSym->name);
                    }
                }
            }
        }
    }
    return nullptr;
}

IntrusivePtr<UIElement> ItemsControl::InstantiateTemplatedItem(
    const AxbPackage& tplPkg,
    const UnifiedValue& itemVal) {
    const std::string_view rootType = !tplPkg.rootTypeName.empty()
        ? std::string_view(tplPkg.rootTypeName)
        : std::string_view("Border");
    IntrusivePtr<UIElement> created = CreateElementByTypeName(rootType);
    if (!created) {
        return {};
    }
    created->SetNamescopeBoundary(true);
    created->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
    ObjectRef itemObj{};
    if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
        itemObj = *ref;
    }
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    StyleTriggerEngine* effStyle = EffectiveStyleEngine();
    AnimationEngine* effAnimEngine = EffectiveAnimationEngine();
    const Interpreter* effInterp = effBind ? effBind->UiInterpreter() : nullptr;
    (void)MountAxbTemplate(
        created.Get(),
        tplPkg,
        effDomains,
        effBind,
        itemObj,
        effInterp,
        nullptr,
        effAnimEngine);
    if (effStyle) {
        effStyle->RegisterPackageStyles(tplPkg);
        (void)effStyle->BindSubtreeStyles(created.Get());
        (void)effStyle->EvaluateSubtree(created.Get());
    }
    return created;
}

void ItemsControl::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t ItemsControl::GetSelectedIndex() const noexcept {
    const UnifiedValue v = GetValue(PropId_SelectedIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    return -1;
}

UnifiedValue ItemsControl::GetSelectedItem() const {
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < items_.size()) {
        return items_[static_cast<std::size_t>(idx)];
    }
    const UnifiedValue explicitItem = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(explicitItem)) {
        return explicitItem;
    }
    return UnifiedValue{};
}

UnifiedValue ItemsControl::NormalizeItemValue(const UnifiedValue& raw) {
    UnifiedValue out = raw;
    if (const auto* strRef = std::get_if<ObjectRef>(&out)) {
        if (strRef->kind == ObjectKind::String) {
            if (ExecutionDomainContext* effDomains = EffectiveDomains()) {
                if (const auto sv = effDomains->stringViewAcrossDomains(*strRef)) {
                    out = value::FromString(std::string(*sv));
                }
            }
        }
    }
    if (out.isTraceable()) {
        rs_rt::pinValueInPools(out);
        EnsureRegisteredInValuePoolSet();
    }
    return out;
}

std::int32_t ItemsControl::FindMatchingItemIndex(const UnifiedValue& target) const {
    if (std::holds_alternative<std::monostate>(target) ||
        std::holds_alternative<NullObject>(target)) {
        return -1;
    }
    UnifiedValue comparable = target;
    if (const auto* strRef = std::get_if<ObjectRef>(&comparable)) {
        if (strRef->kind == ObjectKind::String) {
            if (ExecutionDomainContext* effDomains = EffectiveDomains()) {
                if (const auto sv = effDomains->stringViewAcrossDomains(*strRef)) {
                    comparable = value::FromString(std::string(*sv));
                }
            }
        }
    }
    for (std::size_t i = 0u; i < items_.size(); ++i) {
        if (detail::CollectionItemValuesEqual(items_[i], comparable)) {
            return static_cast<std::int32_t>(i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&comparable)) {
        for (std::size_t i = 0u; i < items_.size(); ++i) {
            if (rs_rt::valueToString(items_[i]) == sv->view()) {
                return static_cast<std::int32_t>(i);
            }
        }
    }
    return -1;
}

void ItemsControl::ReleaseChildSubtreeResources(UIElement* ch) {
    ReleaseItemsChildSubtreeResources(
        ch,
        EffectiveBindingEngine(),
        EffectiveStyleEngine(),
        EffectiveAnimationEngine());
}

void ItemsControl::ClearRecyclePool(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    ExecutionDomainContext* effDomains = EffectiveDomains();
    NativeHandleRegistry* effReg = registry ? registry : (effDomains ? &effDomains->nativeHandles() : nullptr);
    ManagedHeap* effHeap = uiHeap ? uiHeap : (effDomains ? &effDomains->uiHeap() : nullptr);
    for (auto& slot : recyclePool_) {
        if (slot.element) {
            ReleaseChildSubtreeResources(slot.element.Get());
            if (slot.element->Parent() == this) {
                RemoveChild(slot.element.Get(), effReg, effHeap);
            }
        }
    }
    recyclePool_.clear();
}

void ItemsControl::MoveVirtualSlotsToRecyclePool(
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    NativeHandleRegistry* effReg = registry ? registry : (effDomains ? &effDomains->nativeHandles() : nullptr);
    ManagedHeap* effHeap = uiHeap ? uiHeap : (effDomains ? &effDomains->uiHeap() : nullptr);
    for (auto& slot : virtualSlots_) {
        if (!slot.element) {
            continue;
        }
        if (slot.element->Parent() == this) {
            RemoveChild(slot.element.Get(), effReg, effHeap);
        }
        if (recyclePool_.size() < detail::kMaxVirtualRecyclePoolSize) {
            (void)slot.element->ClearValue(PropId_DataContext, ValueSourceRank::Local);
            if (effBind) {
                (void)effBind->RebindSubtreeRootObject(slot.element.Get(), ObjectRef{});
            }
            slot.itemIndex = detail::kInvalidVirtualSlotIndex;
            recyclePool_.push_back(std::move(slot));
        } else {
            ReleaseChildSubtreeResources(slot.element.Get());
        }
    }
    virtualSlots_.clear();
    firstRealizedIndex_ = 0u;
    realizedEndExclusive_ = 0u;
    while (UIElement* ch = FirstChild()) {
        ReleaseChildSubtreeResources(ch);
        RemoveChild(ch, effReg, effHeap);
    }
}

IntrusivePtr<UIElement> ItemsControl::CreateMaterializedItemVisual(
    std::size_t index,
    const UnifiedValue& itemVal,
    const AxbPackage*& outAppliedTemplate) {
    outAppliedTemplate = nullptr;
    if (itemGenerator_) {
        return itemGenerator_(index, itemVal);
    }

    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    StyleTriggerEngine* effStyle = EffectiveStyleEngine();
    AnimationEngine* effAnim = EffectiveAnimationEngine();

    IntrusivePtr<UIElement> innerVisual;
    const UnifiedValue gvVal = GetValue(PropId_GridViewColumnsSpec);
    if (const auto* gvSv = std::get_if<StringValue>(&gvVal); gvSv && !gvSv->view().empty()) {
        auto rowGrid = MakeIntrusive<Grid>();
        rowGrid->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
        rowGrid->SetPadding(Thickness{4.0f, 3.0f, 4.0f, 3.0f});
        NativeHandleRegistry* reg = effDomains ? &effDomains->nativeHandles() : nullptr;
        ManagedHeap* heap = effDomains ? &effDomains->uiHeap() : nullptr;
        const std::string_view allCols = gvSv->view();
        std::size_t pos = 0u;
        std::int32_t colIdx = 0;
        while (pos <= allCols.size()) {
            const std::size_t recEnd = allCols.find('\x1e', pos);
            const std::string_view rec = (recEnd == std::string_view::npos)
                ? allCols.substr(pos)
                : allCols.substr(pos, recEnd - pos);
            if (!rec.empty()) {
                std::string_view fields[5]{};
                std::size_t fp = 0u;
                for (int fi = 0; fi < 5; ++fi) {
                    const std::size_t fn = rec.find('\x1f', fp);
                    if (fn == std::string_view::npos) {
                        fields[fi] = rec.substr(fp);
                        break;
                    }
                    fields[fi] = rec.substr(fp, fn - fp);
                    fp = fn + 1u;
                }
                float colW = 100.0f;
                if (!fields[1].empty() && fields[1] != "Auto" && fields[1] != "NaN") {
                    (void)TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Width, fields[1]), colW);
                }
                rowGrid->AddColumnDefinition(GridUnitType::Pixel, std::max(24.0f, colW));

                IntrusivePtr<UIElement> cellEl;
                if (!fields[4].empty()) {
                    if (const AxbPackage* cellDt = AxbTemplateRegistry::Instance().FindDataTemplate(fields[4])) {
                        cellEl = InstantiateTemplatedItem(*cellDt, itemVal);
                    }
                }
                if (!cellEl) {
                    auto cellTb = MakeIntrusive<TextBlock>();
                    std::string cellText;
                    if (!fields[2].empty()) {
                        if (const auto* nh = std::get_if<NativeHandle>(&itemVal)) {
                            const UIElement* rowEl = effDomains
                                ? UIElement::FromNativeHandle(effDomains->nativeHandles(), *nh)
                                : nullptr;
                            if (!rowEl) {
                                rowEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), *nh);
                            }
                            if (rowEl) {
                                const std::uint16_t pid = ResolveTriggerConditionPropertyId(fields[2]);
                                const UnifiedValue fv = rowEl->GetValue(pid);
                                if (!std::holds_alternative<std::monostate>(fv)) {
                                    cellText = FormatBindingString(fields[3], &fv, 1u, effDomains);
                                }
                            }
                        } else if (const auto* oref = std::get_if<ObjectRef>(&itemVal);
                                   oref && oref->valid() && effDomains && effBind &&
                                   effBind->Program()) {
                            if (const auto* oh = effDomains->heapForObject(*oref)) {
                                const SymbolId tid = oh->objectTypeId(*oref).value_or(0);
                                if (auto slot = effDomains->resolvePropertySlot(
                                        *effBind->Program(),
                                        tid,
                                        std::string(fields[2]))) {
                                    UnifiedValue fv{};
                                    if (effDomains->readPropertySlotFast(
                                            DomainKind::Ui,
                                            *oref,
                                            *slot,
                                            fv,
                                            nullptr,
                                            effBind->UiInterpreter())) {
                                        cellText = FormatBindingString(fields[3], &fv, 1u, effDomains);
                                    }
                                    AxbCompiledHop hop{};
                                    hop.memberName = std::string(fields[2]);
                                    hop.declaringTypeId = slot->typeId;
                                    hop.fieldIndex = slot->backingFieldIndex;
                                    hop.observableBitIndex = slot->observableBitIndex;
                                    hop.getterSymbolId = slot->getterSymbolId;
                                    hop.setterSymbolId = slot->setterSymbolId;
                                    hop.isAutoProperty = slot->isAutoProperty;
                                    hop.isObservable = slot->isObservable;
                                    hop.isReadOnly = slot->isReadOnly;
                                    AxbCompiledBindingChain cellChain{};
                                    cellChain.rawPath = std::string(fields[2]);
                                    cellChain.targetPropertyId = PropId_Text;
                                    cellChain.mode = BindingMode::OneWay;
                                    cellChain.stringFormat = std::string(fields[3]);
                                    cellChain.hops.push_back(std::move(hop));
                                    cellTb->SetText(cellText);
                                    cellTb->SetVerticalAlignment(VerticalAlignment::Center);
                                    (void)effBind->BindChain(*oref, cellChain, cellTb);
                                    cellEl = cellTb;
                                }
                            }
                        }
                    } else {
                        cellText = detail::ResolveSmartObjectDisplayText(this, effDomains, effBind, itemVal);
                    }
                    if (!cellEl) {
                        cellTb->SetText(cellText);
                        cellTb->SetVerticalAlignment(VerticalAlignment::Center);
                        cellEl = cellTb;
                    }
                }
                if (cellEl) {
                    Grid::SetColumn(cellEl.Get(), colIdx);
                    rowGrid->AddChild(cellEl.Get(), reg, heap);
                }
                ++colIdx;
            }
            if (recEnd == std::string_view::npos) {
                break;
            }
            pos = recEnd + 1u;
        }
        innerVisual = rowGrid;
    } else if (const AxbPackage* tplPkg = ResolveActiveItemTemplate(itemVal)) {
        outAppliedTemplate = tplPkg;
        innerVisual = InstantiateTemplatedItem(*tplPkg, itemVal);
    } else {
        auto tb = MakeIntrusive<TextBlock>();
        tb->SetText(detail::ResolveSmartObjectDisplayText(this, effDomains, effBind, itemVal));
        innerVisual = tb;
    }

    if (!innerVisual) {
        return {};
    }

    const AxbDataTemplate* dtEntry = !itemGenerator_ ? ResolveActiveDataTemplateEntry(itemVal) : nullptr;
    if (IsA<TreeView>() || IsA<TreeViewItem>() || (dtEntry && !dtEntry->itemsSourcePath.empty())) {
        IntrusivePtr<TreeViewItem> tvi;
        if (auto* existingTvi = innerVisual->DynamicCast<TreeViewItem>()) {
            tvi = IntrusivePtr<TreeViewItem>(existingTvi, true);
        } else {
            tvi = MakeIntrusive<TreeViewItem>();
            innerVisual->SetValue(PropId_Header, UnifiedValue{true}, ValueSourceRank::Default);
            NativeHandleRegistry* reg = effDomains ? &effDomains->nativeHandles() : nullptr;
            ManagedHeap* heap = effDomains ? &effDomains->uiHeap() : nullptr;
            tvi->AddChild(innerVisual.Get(), reg, heap);
        }
        tvi->SetDomains(effDomains);
        tvi->SetBindingEngine(effBind);
        if (effBind) {
            tvi->BindTwoWayEngine(effBind);
        }
        tvi->SetStyleEngine(effStyle);
        tvi->SetAnimationEngine(effAnim);
        if (effAnim) {
            tvi->BindAnimationEngine(effAnim);
        }
        tvi->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
        if (tvi->GetPropertySourceRank(PropId_IsExpanded) == ValueSourceRank::Default) {
            tvi->SetValue(PropId_IsExpanded, UnifiedValue{true}, ValueSourceRank::Default);
        }
        if (dtEntry && !dtEntry->childItemTemplateKey.empty()) {
            tvi->SetItemTemplateKey(dtEntry->childItemTemplateKey);
        } else if (dtEntry && !dtEntry->key.empty()) {
            tvi->SetItemTemplateKey(dtEntry->key);
        } else if (!GetItemTemplateKey().empty()) {
            tvi->SetItemTemplateKey(GetItemTemplateKey());
        }
        if (dtEntry && !dtEntry->itemsSourcePath.empty()) {
            if (const auto* nh = std::get_if<NativeHandle>(&itemVal);
                nh && dtEntry->itemsSourcePath == "Children") {
                UIElement* nodeEl = effDomains
                    ? UIElement::FromNativeHandle(effDomains->nativeHandles(), *nh)
                    : nullptr;
                if (!nodeEl) {
                    nodeEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), *nh);
                }
                if (nodeEl && nodeEl->FirstChild()) {
                    NativeHandleRegistry& regRef =
                        effDomains ? effDomains->nativeHandles() : FallbackVisualHandleRegistry();
                    std::vector<UnifiedValue> subItems;
                    for (UIElement* c = nodeEl->FirstChild(); c != nullptr; c = c->NextSibling()) {
                        subItems.push_back(UnifiedValue{c->ToNativeHandle(regRef)});
                    }
                    tvi->SetItemsSource(
                        subItems,
                        effDomains ? &effDomains->nativeHandles() : nullptr,
                        effDomains ? &effDomains->uiHeap() : nullptr);
                }
            } else if (effBind) {
                if (const auto* itemObj = std::get_if<ObjectRef>(&itemVal); itemObj && itemObj->valid()) {
                    (void)effBind->BindPath(
                        *itemObj,
                        dtEntry->itemsSourcePath,
                        IntrusivePtr<UIElement>(tvi.Get(), true),
                        PropId_ItemsSource,
                        BindingMode::OneWay);
                }
            }
        }
        tvi->SyncHasItemsFlag();
        innerVisual = tvi;
    }

    const std::int32_t altCount = detail::CoerceInt32Property(GetValue(PropId_AlternationCount), 0);
    const std::int64_t altIdx = (altCount > 1)
        ? static_cast<std::int64_t>(index % static_cast<std::size_t>(altCount))
        : 0;
    if (altCount > 1) {
        innerVisual->SetValue(PropId_AlternationIndex, value::FromInt(altIdx), ValueSourceRank::Local);
    }

    const UnifiedValue icsVal = GetValue(PropId_ItemContainerStyle);
    if (const auto* icsSv = std::get_if<StringValue>(&icsVal); icsSv && !icsSv->view().empty()) {
        const std::string styleKey = icsSv->str();
        if (const AxbStyleDefinition* stDef = AxbTemplateRegistry::Instance().FindStyle(styleKey)) {
            IntrusivePtr<ContentControl> container;
            if (IsA<ListView>()) {
                container = MakeIntrusive<ListViewItem>();
                container->SetDeclaredTypeName("ListViewItem");
            } else if (IsA<ListBox>()) {
                container = MakeIntrusive<ListBoxItem>();
                container->SetDeclaredTypeName("ListBoxItem");
            } else if (IsA<ComboBox>()) {
                container = MakeIntrusive<ComboBoxItem>();
                container->SetDeclaredTypeName("ComboBoxItem");
            } else {
                container = MakeIntrusive<ContentControl>();
                container->SetDeclaredTypeName("ContentControl");
            }
            container->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
            container->SetValue(PropId_Style, value::FromString(styleKey), ValueSourceRank::Local);
            if (altCount > 1) {
                container->SetValue(PropId_AlternationIndex, value::FromInt(altIdx), ValueSourceRank::Local);
            }
            if (effBind) container->BindTwoWayEngine(effBind);
            if (effAnim) container->BindAnimationEngine(effAnim);
            NativeHandleRegistry* reg = effDomains ? &effDomains->nativeHandles() : nullptr;
            ManagedHeap* heap = effDomains ? &effDomains->uiHeap() : nullptr;
            container->AddChild(innerVisual.Get(), reg, heap);

            ObjectRef itemObj{};
            if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) itemObj = *ref;
            if (!effStyle) {
                const AxbControlTemplate* ctToMount = !stDef->controlTemplateKey.empty()
                    ? AxbTemplateRegistry::Instance().FindControlTemplate(stDef->controlTemplateKey)
                    : stDef->inlineControlTemplate.get();
                if (ctToMount) {
                    (void)container->ApplyControlTemplate(
                        *ctToMount,
                        effDomains,
                        effBind,
                        itemObj,
                        effBind ? effBind->UiInterpreter() : nullptr,
                        nullptr,
                        effAnim);
                }
            }
            for (const auto& setter : stDef->setters) {
                if (setter.propId == PropId_Template) continue;
                if (setter.hasBinding && setter.binding && effBind && itemObj.valid()) {
                    AxbCompiledBindingChain bc = *setter.binding;
                    if (effBind->Program()) {
                        AxbPackage tmpPkg{};
                        if (auto* ih = effDomains ? effDomains->heapForObject(itemObj) : nullptr;
                            ih && ih->isAlive(itemObj)) {
                            if (const auto tid = ih->objectTypeId(itemObj)) {
                                if (const auto* ts = effBind->Program()->resolveType(*tid)) {
                                    tmpPkg.rootDataTypeName = ts->name;
                                }
                            }
                        }
                        tmpPkg.bindingChains.push_back(bc);
                        tmpPkg.BindAgainstProgram(*effBind->Program());
                        bc = std::move(tmpPkg.bindingChains[0]);
                    }
                    if (!bc.hops.empty()) {
                        (void)effBind->BindChain(itemObj, bc, container);
                    } else if (!bc.rawPath.empty()) {
                        (void)effBind->BindPath(itemObj, bc.rawPath, container, setter.propId, bc.mode);
                    }
                } else if (setter.hasBinding && setter.binding && std::holds_alternative<NativeHandle>(itemVal)) {
                    const auto nh = std::get<NativeHandle>(itemVal);
                    const UIElement* nhEl = effDomains
                        ? UIElement::FromNativeHandle(effDomains->nativeHandles(), nh)
                        : nullptr;
                    if (!nhEl) {
                        nhEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), nh);
                    }
                    if (nhEl) {
                        const std::uint16_t spid = ResolveTriggerConditionPropertyId(setter.binding->rawPath);
                        UnifiedValue sv = nhEl->GetValue(spid);
                        if (setter.binding->converter != ValueConverterKind::None && effBind) {
                            sv = effBind->ApplySingleValueConversion(
                                setter.binding->converter,
                                setter.binding->converterKey,
                                setter.binding->converterParameter,
                                setter.binding->converterSymbolId,
                                setter.binding->convertBackSymbolId,
                                sv,
                                false);
                        }
                        if (!std::holds_alternative<std::monostate>(sv)) {
                            (void)container->SetValue(setter.propId, sv, ValueSourceRank::ThemeStyle);
                        }
                    }
                } else if (!setter.hasBinding && !effStyle) {
                    (void)container->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
                }
            }
            if (effStyle) {
                (void)effStyle->BindSubtreeStyles(container.Get());
                (void)effStyle->EvaluateSubtree(container.Get());
            }
            return container;
        }
    }

    return innerVisual;
}

bool ItemsControl::RebindMaterializedItemInPlace(
    UIElement* elem,
    const AxbPackage* appliedTpl,
    std::size_t index,
    const UnifiedValue& itemVal) {
    if (!elem) {
        return false;
    }
    if (elem->GetVisibility() != Visibility::Visible) {
        elem->SetVisibility(Visibility::Visible);
    }
    const std::int32_t altCount = detail::CoerceInt32Property(GetValue(PropId_AlternationCount), 0);
    if (altCount > 1) {
        const auto altIdx = static_cast<std::int64_t>(index % static_cast<std::size_t>(altCount));
        elem->SetValue(PropId_AlternationIndex, value::FromInt(altIdx), ValueSourceRank::Local);
        for (UIElement* ch = elem->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
            ch->SetValue(PropId_AlternationIndex, value::FromInt(altIdx), ValueSourceRank::Local);
        }
    }
    if (itemBinder_) {
        itemBinder_(index, itemVal, *elem);
        return true;
    }
    if (!itemGenerator_) {
        ExecutionDomainContext* effDomains = EffectiveDomains();
        DualTrackBindingEngine* effBind = EffectiveBindingEngine();
        StyleTriggerEngine* effStyle = EffectiveStyleEngine();
        const AxbPackage* tplPkg = ResolveActiveItemTemplate(itemVal);
        if (tplPkg != nullptr && appliedTpl == tplPkg) {
            elem->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
            ObjectRef itemObj{};
            if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
                itemObj = *ref;
            }
            if (effBind) {
                (void)effBind->RebindSubtreeRootObject(elem, itemObj);
            }
            if (effStyle) {
                (void)effStyle->EvaluateSubtree(elem, true);
            }
            return true;
        }
        if (tplPkg == nullptr && appliedTpl == nullptr) {
            if (auto* tb = elem->DynamicCast<TextBlock>()) {
                tb->SetText(detail::ResolveSmartObjectDisplayText(this, effDomains, effBind, itemVal));
                return true;
            }
        }
    }
    return false;
}

void ItemsControl::SyncSelectionMetadataAfterMutation(std::int32_t nextSelectedIndex) {
    if (syncingItemsSelection_) {
        return;
    }
    const std::int32_t curSel = GetSelectedIndex();
    const UnifiedValue prevItemVal = GetValue(PropId_SelectedItem);
    UnifiedValue nextItemVal{};
    const bool hasNextItem =
        (nextSelectedIndex >= 0 && static_cast<std::size_t>(nextSelectedIndex) < items_.size());
    if (hasNextItem) {
        nextItemVal = items_[static_cast<std::size_t>(nextSelectedIndex)];
    }

    syncingItemsSelection_ = true;
    if (nextSelectedIndex != curSel) {
        deferSelectionNotify_ = true;
        if (hasNextItem) {
            SetValue(PropId_SelectedItem, nextItemVal, ValueSourceRank::Local);
        } else if (nextSelectedIndex < 0) {
            (void)ClearValue(PropId_SelectedItem, ValueSourceRank::Local);
        }
        deferSelectionNotify_ = false;
        SetSelectedIndex(nextSelectedIndex);
    } else {
        if (hasNextItem) {
            SetValue(PropId_SelectedItem, nextItemVal, ValueSourceRank::Local);
        } else if (nextSelectedIndex < 0) {
            (void)ClearValue(PropId_SelectedItem, ValueSourceRank::Local);
        }
    }
    syncingItemsSelection_ = false;

    if (nextSelectedIndex != curSel) {
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
    }
    if (!detail::CollectionItemValuesEqual(nextItemVal, prevItemVal)) {
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
    }
    {
        const UnifiedValue curSelItem = GetSelectedItem();
        const UnifiedValue resolvedSv = detail::ResolveSelectedValueForItem(
            this,
            EffectiveDomains(),
            EffectiveBindingEngine(),
            curSelItem);
        const bool prevSyncFlag = syncingItemsSelection_;
        syncingItemsSelection_ = true;
        if (!std::holds_alternative<std::monostate>(resolvedSv) &&
            !std::holds_alternative<NullObject>(resolvedSv)) {
            SetValue(PropId_SelectedValue, resolvedSv, ValueSourceRank::Local);
        } else {
            (void)ClearValue(PropId_SelectedValue, ValueSourceRank::Local);
        }
        syncingItemsSelection_ = prevSyncFlag;
        if (!deferSelectionNotify_) {
            (void)NotifyTwoWayPropertyMutated(PropId_SelectedValue);
        }
    }
}

void ItemsControl::ComputeCurrentVirtualWindow(
    Size fallbackChildAvail,
    std::size_t& outFirstIdx,
    std::size_t& outEndExclusive,
    Size& outMeasureConstraint) const noexcept {
    const float itemH = GetVirtualItemHeight();
    const Thickness p = GetPadding();
    const float availW =
        (fallbackChildAvail.width > 0.0f && fallbackChildAvail.width < detail::kUnconstrainedSpace * 0.5f)
        ? fallbackChildAvail.width
        : std::max(0.0f, FinalRect().width - (p.left + p.right));

    float viewTop = 0.0f;
    float viewH = 240.0f;
    if (!ComputeLiveScrollViewport(viewTop, viewH)) {
        if (fallbackChildAvail.height < detail::kUnconstrainedSpace * 0.5f &&
            fallbackChildAvail.height > 0.0f) {
            viewH = fallbackChildAvail.height;
        } else if (FinalRect().height > 0.0f) {
            viewH = std::max(itemH, FinalRect().height - (p.top + p.bottom));
        }
    }

    const float totalLogicalH = static_cast<float>(items_.size()) * itemH;
    const float maxScrollTop = std::max(0.0f, totalLogicalH - viewH);
    viewTop = std::min(viewTop, maxScrollTop);

    constexpr std::size_t kOverscan = 2u;
    const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(viewTop / itemH)));
    outFirstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
    const auto rawEnd = static_cast<std::size_t>(std::ceil((viewTop + viewH) / itemH)) + kOverscan;
    outEndExclusive = items_.empty()
        ? 0u
        : std::min(items_.size(), std::max(outFirstIdx + 1u, rawEnd));
    outMeasureConstraint = Size{availW, itemH};
}

void ItemsControl::MarkExtraValuesInPools() const noexcept {
    for (const auto& item : items_) {
        if (item.isTraceable()) {
            rs_rt::markValueInPools(item);
        }
    }
}

void ItemsControl::RebuildAllMaterializedItems(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    ClearRecyclePool(registry, uiHeap);
    virtualSlots_.clear();
    firstRealizedIndex_ = 0u;
    realizedEndExclusive_ = items_.size();

    const std::size_t childOffset = FirstItemChildOffset();
    while (ChildCount() > childOffset && LastChild() != nullptr) {
        UIElement* ch = LastChild();
        ReleaseChildSubtreeResources(ch);
        RemoveChild(ch, registry, uiHeap);
    }
    for (std::size_t i = 0u; i < items_.size(); ++i) {
        const AxbPackage* appliedTpl = nullptr;
        IntrusivePtr<UIElement> itemVisual = CreateMaterializedItemVisual(i, items_[i], appliedTpl);
        if (itemVisual) {
            AddChild(itemVisual.Get(), registry, uiHeap);
        }
    }
    if (auto* tvi = DynamicCast<TreeViewItem>()) {
        tvi->SyncHasItemsFlag();
    }
}

void ItemsControl::SyncVirtualizedWindow(
    std::size_t targetFirstIdx,
    std::size_t targetEndExclusive,
    Size measureConstraint) {
    const std::size_t total = items_.size();
    targetFirstIdx = std::min(targetFirstIdx, total);
    targetEndExclusive = std::clamp(targetEndExclusive, targetFirstIdx, total);

    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;

    std::vector<RealizedVirtualSlot> kept;
    std::vector<RealizedVirtualSlot> recyclable;
    kept.reserve(targetEndExclusive - targetFirstIdx);
    recyclable.reserve(virtualSlots_.size());

    for (auto& slot : virtualSlots_) {
        if (slot.element &&
            slot.itemIndex != detail::kInvalidVirtualSlotIndex &&
            slot.itemIndex >= targetFirstIdx &&
            slot.itemIndex < targetEndExclusive) {
            kept.push_back(std::move(slot));
        } else if (slot.element) {
            recyclable.push_back(std::move(slot));
        }
    }

    auto bindOrCreateSlot = [&](std::size_t idx, const AxbPackage*& outTpl) -> IntrusivePtr<UIElement> {
        const AxbPackage* tplPkg = !itemGenerator_ ? ResolveActiveItemTemplate(items_[idx]) : nullptr;
        outTpl = tplPkg;
        while (!recyclable.empty()) {
            RealizedVirtualSlot recycledSlot = std::move(recyclable.back());
            recyclable.pop_back();
            IntrusivePtr<UIElement> reused = std::move(recycledSlot.element);
            if (RebindMaterializedItemInPlace(
                    reused.Get(), recycledSlot.appliedTemplate, idx, items_[idx])) {
                outTpl = tplPkg;
                if (reused->Parent() != this) {
                    AddChild(reused.Get(), registry, uiHeap);
                }
                return reused;
            }
            ReleaseChildSubtreeResources(reused.Get());
            RemoveChild(reused.Get(), registry, uiHeap);
        }

        while (!recyclePool_.empty()) {
            RealizedVirtualSlot pooledSlot = std::move(recyclePool_.back());
            recyclePool_.pop_back();
            IntrusivePtr<UIElement> reused = std::move(pooledSlot.element);
            if (!reused) {
                continue;
            }
            if (RebindMaterializedItemInPlace(
                    reused.Get(), pooledSlot.appliedTemplate, idx, items_[idx])) {
                outTpl = tplPkg;
                if (reused->Parent() != this) {
                    AddChild(reused.Get(), registry, uiHeap);
                }
                return reused;
            }
            ReleaseChildSubtreeResources(reused.Get());
            if (reused->Parent() == this) {
                RemoveChild(reused.Get(), registry, uiHeap);
            }
        }

        IntrusivePtr<UIElement> created = CreateMaterializedItemVisual(idx, items_[idx], outTpl);
        if (created) {
            AddChild(created.Get(), registry, uiHeap);
        }
        return created;
    };

    std::vector<RealizedVirtualSlot> nextWindow;
    nextWindow.reserve(targetEndExclusive - targetFirstIdx);

    for (std::size_t idx = targetFirstIdx; idx < targetEndExclusive; ++idx) {
        IntrusivePtr<UIElement> matched{};
        const AxbPackage* matchedTpl = nullptr;
        for (auto& k : kept) {
            if (k.itemIndex == idx && k.element) {
                matched = std::move(k.element);
                matchedTpl = k.appliedTemplate;
                if (itemBinder_) {
                    itemBinder_(idx, items_[idx], *matched);
                }
                break;
            }
        }
        if (!matched) {
            matched = bindOrCreateSlot(idx, matchedTpl);
        }
        if (matched) {
            matched->Measure(measureConstraint);
            estimatedMaxItemWidth_ = std::max(estimatedMaxItemWidth_, matched->DesiredSize().width);
            nextWindow.push_back(RealizedVirtualSlot{idx, std::move(matched), matchedTpl});
        }
    }

    for (auto& surplus : recyclable) {
        if (surplus.element) {
            if (surplus.element->Parent() == this) {
                RemoveChild(surplus.element.Get(), registry, uiHeap);
            }
            if (recyclePool_.size() < detail::kMaxVirtualRecyclePoolSize) {
                (void)surplus.element->ClearValue(PropId_DataContext, ValueSourceRank::Local);
                if (effBind) {
                    (void)effBind->RebindSubtreeRootObject(surplus.element.Get(), ObjectRef{});
                }
                surplus.itemIndex = detail::kInvalidVirtualSlotIndex;
                recyclePool_.push_back(std::move(surplus));
            } else {
                ReleaseChildSubtreeResources(surplus.element.Get());
            }
        }
    }

    if (nextWindow.size() > 1u) {
        std::vector<UIElement*> orderedPtrs;
        orderedPtrs.reserve(nextWindow.size());
        for (const auto& s : nextWindow) {
            orderedPtrs.push_back(s.element.Get());
        }
        ReorderExistingChildren(orderedPtrs.data(), orderedPtrs.size());
    }

    virtualSlots_ = std::move(nextWindow);
    firstRealizedIndex_ = targetFirstIdx;
    realizedEndExclusive_ = targetEndExclusive;
}

void ItemsControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    hasBoundItemsSource_ = true;
    const bool alreadyRealized =
        IsVirtualizing() ? !virtualSlots_.empty() : (ChildCount() > FirstItemChildOffset());
    if (!IsA<ComboBox>() && alreadyRealized && items.size() == items_.size()) {
        bool allSame = true;
        for (std::size_t i = 0u; i < items.size(); ++i) {
            if (!detail::CollectionItemValuesEqual(items[i], items_[i])) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            return;
        }
    }

    const std::size_t oldCount = items_.size();
    const std::int32_t prevSel = GetSelectedIndex();
    const bool hadValidPrevSel =
        (oldCount > 0u && prevSel >= 0 && static_cast<std::size_t>(prevSel) < oldCount);
    const UnifiedValue prevSelectedItem = hadValidPrevSel
        ? items_[static_cast<std::size_t>(prevSel)]
        : UnifiedValue{};

    items_.resize(items.size());
    for (std::size_t i = 0u; i < items.size(); ++i) {
        items_[i] = NormalizeItemValue(items[i]);
    }
    estimatedMaxItemWidth_ = 80.0f;

    if (oldCount > 0u) {
        if (items_.empty()) {
            if (prevSel >= 0) {
                SyncSelectionMetadataAfterMutation(-1);
            }
        } else if (hadValidPrevSel) {
            std::int32_t resolvedSel = -1;
            if (static_cast<std::size_t>(prevSel) < items_.size() &&
                detail::CollectionItemValuesEqual(items_[static_cast<std::size_t>(prevSel)], prevSelectedItem)) {
                resolvedSel = prevSel;
            } else {
                for (std::size_t i = 0u; i < items_.size(); ++i) {
                    if (detail::CollectionItemValuesEqual(items_[i], prevSelectedItem)) {
                        resolvedSel = static_cast<std::int32_t>(i);
                        break;
                    }
                }
                if (resolvedSel < 0 && items_.size() == oldCount) {
                    resolvedSel = prevSel;
                }
            }
            SyncSelectionMetadataAfterMutation(resolvedSel);
        }
    } else if (prevSel >= 0 && static_cast<std::size_t>(prevSel) < items_.size()) {
        SyncSelectionMetadataAfterMutation(prevSel);
    } else if (prevSel < 0 && !items_.empty()) {
        const std::int32_t matchedFromItem = FindMatchingItemIndex(GetValue(PropId_SelectedItem));
        if (matchedFromItem >= 0) {
            SyncSelectionMetadataAfterMutation(matchedFromItem);
        }
    }

    if (auto* cb = DynamicCast<ComboBox>()) {
        (void)cb->ResolvePendingSelectedValue();
    }

    if (IsVirtualizing() && !IsA<ComboBox>() &&
        detail::ResolveItemsPanelConfig(this).mode == detail::ItemsPanelMode::VerticalStack) {
        MoveVirtualSlotsToRecyclePool(registry, uiHeap);
        InvalidateLayout();
        return;
    }

    RebuildAllMaterializedItems(registry, uiHeap);
}

bool ItemsControl::ApplyIncrementalChanges(
    const CollectionChangeRecord* changes,
    std::size_t changeCount) {
    if (!changes || changeCount == 0u) {
        return false;
    }
    for (std::size_t c = 0u; c < changeCount; ++c) {
        if (changes[c].kind == CollectionChangeKind::Reset) {
            return false;
        }
    }
    hasBoundItemsSource_ = true;
    ExecutionDomainContext* effDomains = EffectiveDomains();
    NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;

    std::int32_t selIdx = GetSelectedIndex();
    auto applySimpleDeltas = [&]() -> bool {
        for (std::size_t c = 0u; c < changeCount; ++c) {
            const auto& rec = changes[c];
            switch (rec.kind) {
                case CollectionChangeKind::Add:
                    items_.push_back(NormalizeItemValue(rec.newValue));
                    break;
                case CollectionChangeKind::Insert: {
                    const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                    items_.insert(
                        items_.begin() + static_cast<std::ptrdiff_t>(idx),
                        NormalizeItemValue(rec.newValue));
                    if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                        ++selIdx;
                    }
                    break;
                }
                case CollectionChangeKind::RemoveAt: {
                    if (rec.index >= items_.size()) {
                        break;
                    }
                    const std::size_t idx = rec.index;
                    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                    if (selIdx >= 0) {
                        if (idx < static_cast<std::size_t>(selIdx)) {
                            --selIdx;
                        } else if (idx == static_cast<std::size_t>(selIdx)) {
                            selIdx = -1;
                        }
                    }
                    break;
                }
                case CollectionChangeKind::Replace:
                    if (rec.index < items_.size()) {
                        items_[rec.index] = NormalizeItemValue(rec.newValue);
                    }
                    break;
                case CollectionChangeKind::Reset:
                    return false;
                case CollectionChangeKind::Clear:
                    items_.clear();
                    selIdx = -1;
                    break;
            }
        }
        return true;
    };

    // 1. ComboBox incremental fast path (no child row visual containers)
    if (IsA<ComboBox>()) {
        if (!applySimpleDeltas()) {
            return false;
        }
        SyncSelectionMetadataAfterMutation(selIdx);
        if (auto* cb = DynamicCast<ComboBox>()) {
            (void)cb->ResolvePendingSelectedValue();
        }
        InvalidateLayout();
        InvalidateRender();
        return true;
    }

    // 2. Virtualized ItemsControl / ListBox O(1) incremental path
    if (IsVirtualizing() &&
        detail::ResolveItemsPanelConfig(this).mode == detail::ItemsPanelMode::VerticalStack) {
        if (virtualSlots_.empty()) {
            if (!applySimpleDeltas()) {
                return false;
            }
            SyncSelectionMetadataAfterMutation(selIdx);
            InvalidateLayout();
            return true;
        }

        bool totalCountChanged = false;
        bool windowStructureDirty = false;
        bool visibleSlotReboundInPlace = false;

        for (std::size_t c = 0u; c < changeCount; ++c) {
            const auto& rec = changes[c];
            switch (rec.kind) {
                case CollectionChangeKind::Add: {
                    const std::size_t appendIdx = items_.size();
                    items_.push_back(NormalizeItemValue(rec.newValue));
                    totalCountChanged = true;

                    std::size_t targetFirst = 0u;
                    std::size_t targetEnd = 0u;
                    Size dummyConstraint{};
                    ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, dummyConstraint);
                    if (appendIdx < targetEnd ||
                        targetFirst != firstRealizedIndex_ ||
                        targetEnd != realizedEndExclusive_) {
                        windowStructureDirty = true;
                    }
                    break;
                }
                case CollectionChangeKind::Insert: {
                    const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                    items_.insert(
                        items_.begin() + static_cast<std::ptrdiff_t>(idx),
                        NormalizeItemValue(rec.newValue));
                    if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                        ++selIdx;
                    }
                    totalCountChanged = true;

                    if (idx < realizedEndExclusive_) {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex != detail::kInvalidVirtualSlotIndex &&
                                slot.itemIndex >= idx) {
                                ++slot.itemIndex;
                            }
                        }
                        windowStructureDirty = true;
                    } else {
                        std::size_t targetFirst = 0u;
                        std::size_t targetEnd = 0u;
                        Size dummyConstraint{};
                        ComputeCurrentVirtualWindow(
                            lastChildAvailableSize_,
                            targetFirst,
                            targetEnd,
                            dummyConstraint);
                        if (idx < targetEnd ||
                            targetFirst != firstRealizedIndex_ ||
                            targetEnd != realizedEndExclusive_) {
                            windowStructureDirty = true;
                        }
                    }
                    break;
                }
                case CollectionChangeKind::RemoveAt: {
                    if (rec.index >= items_.size()) {
                        break;
                    }
                    const std::size_t idx = rec.index;
                    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                    if (selIdx >= 0) {
                        if (idx < static_cast<std::size_t>(selIdx)) {
                            --selIdx;
                        } else if (idx == static_cast<std::size_t>(selIdx)) {
                            selIdx = -1;
                        }
                    }
                    totalCountChanged = true;

                    if (items_.empty()) {
                        windowStructureDirty = true;
                    } else if (idx < realizedEndExclusive_) {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex == idx) {
                                slot.itemIndex = detail::kInvalidVirtualSlotIndex;
                            } else if (slot.itemIndex != detail::kInvalidVirtualSlotIndex &&
                                       slot.itemIndex > idx) {
                                --slot.itemIndex;
                            }
                        }
                        windowStructureDirty = true;
                    } else {
                        std::size_t targetFirst = 0u;
                        std::size_t targetEnd = 0u;
                        Size dummyConstraint{};
                        ComputeCurrentVirtualWindow(
                            lastChildAvailableSize_,
                            targetFirst,
                            targetEnd,
                            dummyConstraint);
                        if (targetFirst != firstRealizedIndex_ || targetEnd != realizedEndExclusive_) {
                            windowStructureDirty = true;
                        }
                    }
                    break;
                }
                case CollectionChangeKind::Replace: {
                    if (rec.index >= items_.size()) {
                        break;
                    }
                    const std::size_t idx = rec.index;
                    items_[idx] = NormalizeItemValue(rec.newValue);
                    if (!windowStructureDirty) {
                        if (idx >= firstRealizedIndex_ && idx < realizedEndExclusive_) {
                            for (auto& slot : virtualSlots_) {
                                if (slot.itemIndex == idx && slot.element) {
                                    if (RebindMaterializedItemInPlace(
                                            slot.element.Get(), slot.appliedTemplate, idx, items_[idx])) {
                                        const float itemH = GetVirtualItemHeight();
                                        const Thickness p = GetPadding();
                                        const float innerW = hasValidMeasureConstraint_
                                            ? lastChildAvailableSize_.width
                                            : std::max(0.0f, FinalRect().width - (p.left + p.right));
                                        const float prevEstW = estimatedMaxItemWidth_;
                                        slot.element->Measure(Size{innerW, itemH});
                                        estimatedMaxItemWidth_ = std::max(
                                            estimatedMaxItemWidth_,
                                            slot.element->DesiredSize().width);
                                        if (FinalRect().width > 0.0f) {
                                            const float startX = FinalRect().x + p.left;
                                            const float startY = FinalRect().y + p.top;
                                            const float rowY =
                                                startY + static_cast<float>(slot.itemIndex) * itemH;
                                            slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
                                        }
                                        if (estimatedMaxItemWidth_ <= prevEstW + 1e-3f) {
                                            ClearLayoutDirtyFlags(
                                                static_cast<std::uint8_t>(MeasureDirty | SubtreeMeasureDirty));
                                        } else {
                                            InvalidateLayout();
                                        }
                                        visibleSlotReboundInPlace = true;
                                    } else {
                                        slot.itemIndex = detail::kInvalidVirtualSlotIndex;
                                        windowStructureDirty = true;
                                    }
                                    break;
                                }
                            }
                        }
                    } else {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex == idx) {
                                slot.itemIndex = detail::kInvalidVirtualSlotIndex;
                                break;
                            }
                        }
                    }
                    break;
                }
                case CollectionChangeKind::Reset:
                    return false;
                case CollectionChangeKind::Clear:
                    items_.clear();
                    selIdx = -1;
                    totalCountChanged = true;
                    windowStructureDirty = true;
                    break;
            }
        }

        SyncSelectionMetadataAfterMutation(selIdx);

        if (items_.empty()) {
            estimatedMaxItemWidth_ = 80.0f;
            MoveVirtualSlotsToRecyclePool(registry, uiHeap);
            InvalidateLayout();
            return true;
        }

        if (windowStructureDirty) {
            std::size_t targetFirst = 0u;
            std::size_t targetEnd = 0u;
            Size itemConstraint{};
            ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, itemConstraint);
            SyncVirtualizedWindow(targetFirst, targetEnd, itemConstraint);
            if (FinalRect().width > 0.0f) {
                const Thickness p = GetPadding();
                const float startX = FinalRect().x + p.left;
                const float startY = FinalRect().y + p.top;
                const float innerW = std::max(0.0f, FinalRect().width - (p.left + p.right));
                const float itemH = GetVirtualItemHeight();
                for (const auto& slot : virtualSlots_) {
                    if (slot.element) {
                        const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                        slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
                    }
                }
            }
            InvalidateLayout();
        } else if (totalCountChanged) {
            InvalidateLayout();
        } else if (visibleSlotReboundInPlace) {
            InvalidateRender();
        }
        return true;
    }

    // 3. Non-virtualized materialized ItemsControl incremental path
    const std::size_t childOffset = FirstItemChildOffset();
    bool needsGeneratorFullRefresh = false;
    for (std::size_t c = 0u; c < changeCount; ++c) {
        const auto& rec = changes[c];
        switch (rec.kind) {
            case CollectionChangeKind::Add: {
                const std::size_t idx = items_.size();
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_.push_back(norm);
                const AxbPackage* appliedTpl = nullptr;
                if (auto visual = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                    AddChild(visual.Get(), registry, uiHeap);
                }
                break;
            }
            case CollectionChangeKind::Insert: {
                const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), norm);
                if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                    ++selIdx;
                }
                if (itemGenerator_ && !itemBinder_ && idx + 1u < items_.size()) {
                    needsGeneratorFullRefresh = true;
                } else {
                    const AxbPackage* appliedTpl = nullptr;
                    if (auto visual = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                        InsertChildAt(visual.Get(), childOffset + idx, registry, uiHeap);
                    }
                    if (itemBinder_) {
                        UIElement* cur = FirstChild();
                        for (std::size_t skip = 0u; skip < childOffset && cur != nullptr; ++skip) {
                            cur = cur->NextSibling();
                        }
                        for (std::size_t k = 0u; cur != nullptr && k < items_.size();
                             ++k, cur = cur->NextSibling()) {
                            if (k > idx) {
                                itemBinder_(k, items_[k], *cur);
                            }
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::RemoveAt: {
                if (rec.index >= items_.size()) {
                    break;
                }
                const std::size_t idx = rec.index;
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                if (selIdx >= 0) {
                    if (idx < static_cast<std::size_t>(selIdx)) {
                        --selIdx;
                    } else if (idx == static_cast<std::size_t>(selIdx)) {
                        selIdx = -1;
                    }
                }
                if (itemGenerator_ && !itemBinder_ && idx < items_.size()) {
                    needsGeneratorFullRefresh = true;
                } else {
                    UIElement* targetCh = FirstChild();
                    for (std::size_t k = 0u; k < childOffset + idx && targetCh != nullptr; ++k) {
                        targetCh = targetCh->NextSibling();
                    }
                    if (targetCh) {
                        ReleaseChildSubtreeResources(targetCh);
                        RemoveChild(targetCh, registry, uiHeap);
                    }
                    if (itemBinder_) {
                        UIElement* cur = FirstChild();
                        for (std::size_t skip = 0u; skip < childOffset && cur != nullptr; ++skip) {
                            cur = cur->NextSibling();
                        }
                        for (std::size_t k = 0u; cur != nullptr && k < items_.size();
                             ++k, cur = cur->NextSibling()) {
                            if (k >= idx) {
                                itemBinder_(k, items_[k], *cur);
                            }
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Replace: {
                if (rec.index >= items_.size()) {
                    break;
                }
                const std::size_t idx = rec.index;
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_[idx] = norm;
                if (!needsGeneratorFullRefresh) {
                    UIElement* targetCh = FirstChild();
                    for (std::size_t k = 0u; k < childOffset + idx && targetCh != nullptr; ++k) {
                        targetCh = targetCh->NextSibling();
                    }
                    const AxbPackage* expectedTpl = !itemGenerator_ ? ResolveActiveItemTemplate(norm) : nullptr;
                    if (targetCh && RebindMaterializedItemInPlace(targetCh, expectedTpl, idx, norm)) {
                        // Rebound existing visual in-place
                    } else if (targetCh) {
                        ReleaseChildSubtreeResources(targetCh);
                        RemoveChild(targetCh, registry, uiHeap);
                        const AxbPackage* appliedTpl = nullptr;
                        if (auto replacement = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                            InsertChildAt(replacement.Get(), childOffset + idx, registry, uiHeap);
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Reset:
                return false;
            case CollectionChangeKind::Clear:
                items_.clear();
                selIdx = -1;
                needsGeneratorFullRefresh = false;
                while (ChildCount() > childOffset && LastChild() != nullptr) {
                    UIElement* ch = LastChild();
                    ReleaseChildSubtreeResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
                break;
        }
    }

    if (needsGeneratorFullRefresh) {
        RebuildAllMaterializedItems(registry, uiHeap);
    }
    if (auto* tvi = DynamicCast<TreeViewItem>()) {
        tvi->SyncHasItemsFlag();
    }
    realizedEndExclusive_ = items_.size();
    SyncSelectionMetadataAfterMutation(selIdx);
    InvalidateLayout();
    return true;
}

void ItemsControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (propId == PropId_SelectedIndex && !IsA<ComboBox>() && !syncingItemsSelection_) {
        SyncSelectionMetadataAfterMutation(GetSelectedIndex());
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
        InvalidateRender();
        return;
    }

    if (propId == PropId_SelectedItem && !IsA<ComboBox>() && !syncingItemsSelection_) {
        if (std::holds_alternative<std::monostate>(newValue) ||
            std::holds_alternative<NullObject>(newValue)) {
            if (GetSelectedIndex() != -1) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(-1);
                syncingItemsSelection_ = false;
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        } else {
            const std::int32_t matchedIdx = FindMatchingItemIndex(newValue);
            if (matchedIdx >= 0 && matchedIdx != GetSelectedIndex()) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(matchedIdx);
                syncingItemsSelection_ = false;
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        }
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
        InvalidateRender();
        return;
    }

    if (propId == PropId_SelectedValue && !IsA<ComboBox>() && !syncingItemsSelection_) {
        if (std::holds_alternative<std::monostate>(newValue) ||
            std::holds_alternative<NullObject>(newValue)) {
            if (GetSelectedIndex() != -1) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(-1);
                syncingItemsSelection_ = false;
                SyncSelectionMetadataAfterMutation(-1);
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        } else {
            ExecutionDomainContext* effDomains = EffectiveDomains();
            DualTrackBindingEngine* effBind = EffectiveBindingEngine();
            const std::string wantStr = FormatBindingString({}, &newValue, 1u, effDomains);
            std::int32_t matchedIdx = -1;
            for (std::size_t i = 0u; i < items_.size(); ++i) {
                const UnifiedValue candSv = detail::ResolveSelectedValueForItem(
                    this,
                    effDomains,
                    effBind,
                    items_[i]);
                if (detail::CollectionItemValuesEqual(candSv, newValue) ||
                    (!wantStr.empty() && FormatBindingString({}, &candSv, 1u, effDomains) == wantStr)) {
                    matchedIdx = static_cast<std::int32_t>(i);
                    break;
                }
            }
            if (matchedIdx < 0 && items_.empty()) {
                std::size_t ci = 0u;
                for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++ci) {
                    const UnifiedValue nhVal{const_cast<UIElement*>(c)->ToNativeHandle(
                        effDomains ? effDomains->nativeHandles() : FallbackVisualHandleRegistry())};
                    const UnifiedValue candSv = detail::ResolveSelectedValueForItem(
                        this,
                        effDomains,
                        effBind,
                        nhVal);
                    if (detail::CollectionItemValuesEqual(candSv, newValue) ||
                        (!wantStr.empty() && FormatBindingString({}, &candSv, 1u, effDomains) == wantStr)) {
                        matchedIdx = static_cast<std::int32_t>(ci);
                        break;
                    }
                }
            }
            if (matchedIdx >= 0 && matchedIdx != GetSelectedIndex()) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(matchedIdx);
                syncingItemsSelection_ = false;
                SyncSelectionMetadataAfterMutation(matchedIdx);
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        }
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedValue);
        InvalidateRender();
        return;
    }

    if ((propId == PropId_IsVirtualizing || propId == PropId_ItemTemplate ||
         propId == PropId_ItemsPanel || propId == PropId_ItemContainerStyle ||
         propId == PropId_DisplayMemberPath || propId == PropId_GridViewColumnsSpec) &&
        !IsA<ComboBox>()) {
        ExecutionDomainContext* effDomains = EffectiveDomains();
        NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;
        if (propId == PropId_ItemTemplate) {
            ClearRecyclePool(registry, uiHeap);
        }
        if (items_.empty()) {
            if (!IsVirtualizing()) {
                ClearRecyclePool(registry, uiHeap);
            }
            return;
        }
        if (IsVirtualizing()) {
            virtualSlots_.clear();
            firstRealizedIndex_ = 0u;
            realizedEndExclusive_ = 0u;
            while (UIElement* ch = FirstChild()) {
                ReleaseChildSubtreeResources(ch);
                RemoveChild(ch, registry, uiHeap);
            }
            InvalidateLayout();
        } else {
            RebuildAllMaterializedItems(registry, uiHeap);
        }
        return;
    }

    if (propId == PropId_ItemsSource) {
        ExecutionDomainContext* effDomains = EffectiveDomains();
        DualTrackBindingEngine* effBind = EffectiveBindingEngine();
        AnimationEngine* effAnim = EffectiveAnimationEngine();
        NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;
        if (const auto* objRef = std::get_if<ObjectRef>(&newValue)) {
            if (objRef->valid() && effDomains) {
                if (auto* heap = effDomains->heapForObject(*objRef)) {
                    const ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(*heap, *objRef);
                    if (arrRef.valid()) {
                        const std::size_t len = heap->arrayLength(arrRef).value_or(0u);
                        std::vector<UnifiedValue> extracted;
                        extracted.reserve(len);
                        for (std::size_t i = 0u; i < len; ++i) {
                            UnifiedValue elem = heap->arrayGet(arrRef, i).value_or(UnifiedValue{});
                            if (const auto* strRef = std::get_if<ObjectRef>(&elem)) {
                                if (strRef->kind == ObjectKind::String) {
                                    if (const auto sv = effDomains->stringViewAcrossDomains(*strRef)) {
                                        elem = value::FromString(std::string(*sv));
                                    }
                                }
                            }
                            extracted.push_back(std::move(elem));
                        }
                        SetItemsSource(extracted, registry, uiHeap);
                        retainedVisualResourceHolder_.Reset();
                        return;
                    }
                }
            }
        } else if (const auto* svRes = std::get_if<StringValue>(&newValue);
                   svRes && svRes->view().rfind("__aero_visual_res__:", 0) == 0) {
            if (const AxbPackage* vPkg = AxbTemplateRegistry::Instance().FindDataTemplate(svRes->view())) {
                auto holder = CreateElementByTypeName(
                    !vPkg->rootTypeName.empty() ? vPkg->rootTypeName : "StackPanel");
                if (holder) {
                    (void)MountAxbTemplate(
                        holder.Get(),
                        *vPkg,
                        effDomains,
                        effBind,
                        ObjectRef{},
                        effBind ? effBind->UiInterpreter() : nullptr,
                        nullptr,
                        effAnim);
                    NativeHandleRegistry& regRef = registry ? *registry : FallbackVisualHandleRegistry();
                    std::vector<UnifiedValue> extracted;
                    for (UIElement* c = holder->FirstChild(); c != nullptr; c = c->NextSibling()) {
                        extracted.push_back(UnifiedValue{c->ToNativeHandle(regRef)});
                    }
                    SetItemsSource(extracted, registry, uiHeap);
                    retainedVisualResourceHolder_ = std::move(holder);
                    return;
                }
            }
        } else if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const std::string_view raw = sv->str();
            std::vector<UnifiedValue> extracted;
            std::size_t start = 0u;
            while (start < raw.size()) {
                std::size_t sep = raw.find('|', start);
                if (sep == std::string_view::npos) {
                    sep = raw.size();
                }
                const std::string_view item = raw.substr(start, sep - start);
                if (!item.empty()) {
                    extracted.push_back(value::FromString(std::string(item)));
                }
                start = sep + 1u;
            }
            SetItemsSource(extracted, registry, uiHeap);
            retainedVisualResourceHolder_.Reset();
            return;
        } else if (std::holds_alternative<std::monostate>(newValue) ||
                   std::holds_alternative<NullObject>(newValue) ||
                   std::holds_alternative<NullArray>(newValue)) {
            SetItemsSource({}, registry, uiHeap);
            retainedVisualResourceHolder_.Reset();
            return;
        }
    }
}

Size ItemsControl::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};
    lastChildAvailableSize_ = childAvail;
    hasValidMeasureConstraint_ = true;

    const detail::ParsedItemsPanelConfig panelCfg = detail::ResolveItemsPanelConfig(this);
    if (IsVirtualizing() && !IsA<ComboBox>() &&
        panelCfg.mode == detail::ItemsPanelMode::VerticalStack &&
        (!items_.empty() || hasBoundItemsSource_ || FirstChild() == nullptr)) {
        if (items_.empty()) {
            return Size{padW, padH};
        }
        const float itemH = GetVirtualItemHeight();
        const float totalLogicalH = static_cast<float>(items_.size()) * itemH;

        float viewTop = 0.0f;
        float viewH = 240.0f;
        if (!ComputeLiveScrollViewport(viewTop, viewH)) {
            if (childAvail.height < detail::kUnconstrainedSpace * 0.5f && childAvail.height > 0.0f) {
                viewH = childAvail.height;
            } else if (FinalRect().height > 0.0f) {
                viewH = std::max(itemH, FinalRect().height - (p.top + p.bottom));
            }
        }

        const float maxScrollTop = std::max(0.0f, totalLogicalH - viewH);
        viewTop = std::min(viewTop, maxScrollTop);

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(viewTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((viewTop + viewH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));
        const Size slotConstraint{childAvail.width, itemH};

        if (!virtualSlots_.empty() &&
            firstIdx == firstRealizedIndex_ &&
            endIdx == realizedEndExclusive_ &&
            virtualSlots_.size() == (endIdx - firstIdx)) {
            bool contiguous = true;
            for (std::size_t i = 0u; i < virtualSlots_.size(); ++i) {
                if (!virtualSlots_[i].element || virtualSlots_[i].itemIndex != firstIdx + i) {
                    contiguous = false;
                    break;
                }
            }
            if (contiguous) {
                for (auto& slot : virtualSlots_) {
                    slot.element->Measure(slotConstraint);
                    estimatedMaxItemWidth_ =
                        std::max(estimatedMaxItemWidth_, slot.element->DesiredSize().width);
                }
                return Size{estimatedMaxItemWidth_ + padW, totalLogicalH + padH};
            }
        }

        SyncVirtualizedWindow(firstIdx, endIdx, slotConstraint);
        return Size{estimatedMaxItemWidth_ + padW, totalLogicalH + padH};
    }

    if (panelCfg.mode == detail::ItemsPanelMode::Canvas) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Measure(Size{detail::kUnconstrainedSpace, detail::kUnconstrainedSpace});
        }
        return Size{padW, padH};
    }
    if (panelCfg.mode == detail::ItemsPanelMode::HorizontalStack) {
        float totalW = 0.0f;
        float maxH = 0.0f;
        bool first = true;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Measure(Size{detail::kUnconstrainedSpace, childAvail.height});
            if (c->GetVisibility() == Visibility::Collapsed) {
                continue;
            }
            if (!first) {
                totalW += panelCfg.spacing;
            }
            first = false;
            totalW += c->DesiredSize().width;
            maxH = std::max(maxH, c->DesiredSize().height);
        }
        return Size{totalW + padW, maxH + padH};
    }
    if (panelCfg.mode == detail::ItemsPanelMode::UniformGrid) {
        std::size_t visCount = 0u;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() != Visibility::Collapsed) {
                ++visCount;
            }
        }
        std::int32_t cols = panelCfg.cols;
        std::int32_t rows = panelCfg.rows;
        std::int32_t firstCol = panelCfg.firstCol;
        if (cols <= 0) {
            cols = (rows > 0 && visCount > 0u)
                ? static_cast<std::int32_t>((visCount + firstCol + rows - 1) / rows)
                : std::max<std::int32_t>(
                      1,
                      static_cast<std::int32_t>(
                          std::ceil(std::sqrt(static_cast<float>(std::max<std::size_t>(1u, visCount))))));
        }
        if (firstCol >= cols) {
            firstCol = 0;
        }
        if (rows <= 0) {
            rows = std::max<std::int32_t>(1, static_cast<std::int32_t>((visCount + firstCol + cols - 1) / cols));
        }
        const Size cellAvail{
            (childAvail.width < detail::kUnconstrainedSpace * 0.5f)
                ? (childAvail.width / static_cast<float>(cols))
                : detail::kUnconstrainedSpace,
            (childAvail.height < detail::kUnconstrainedSpace * 0.5f)
                ? (childAvail.height / static_cast<float>(rows))
                : detail::kUnconstrainedSpace};
        float maxCW = 0.0f;
        float maxCH = 0.0f;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Measure(cellAvail);
            if (c->GetVisibility() != Visibility::Collapsed) {
                maxCW = std::max(maxCW, c->DesiredSize().width);
                maxCH = std::max(maxCH, c->DesiredSize().height);
            }
        }
        return Size{
            maxCW * static_cast<float>(cols) + padW,
            maxCH * static_cast<float>(rows) + padH};
    }
    if (panelCfg.mode == detail::ItemsPanelMode::WrapHorizontal ||
        panelCfg.mode == detail::ItemsPanelMode::WrapVertical) {
        const bool horiz = (panelCfg.mode == detail::ItemsPanelMode::WrapHorizontal);
        const float lineLimit = horiz ? childAvail.width : childAvail.height;
        float lineMain = 0.0f;
        float lineCross = 0.0f;
        float maxMain = 0.0f;
        float totalCross = 0.0f;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            const Size cAvail{
                (panelCfg.itemWidth > 0.0f) ? panelCfg.itemWidth : detail::kUnconstrainedSpace,
                (panelCfg.itemHeight > 0.0f) ? panelCfg.itemHeight : detail::kUnconstrainedSpace};
            c->Measure(cAvail);
            if (c->GetVisibility() == Visibility::Collapsed) {
                continue;
            }
            const float cw = (panelCfg.itemWidth > 0.0f) ? panelCfg.itemWidth : c->DesiredSize().width;
            const float ch = (panelCfg.itemHeight > 0.0f) ? panelCfg.itemHeight : c->DesiredSize().height;
            const float m = horiz ? cw : ch;
            const float cr = horiz ? ch : cw;
            if (lineMain > 0.0f && lineMain + m > lineLimit) {
                maxMain = std::max(maxMain, lineMain);
                totalCross += lineCross;
                lineMain = m;
                lineCross = cr;
            } else {
                lineMain += m;
                lineCross = std::max(lineCross, cr);
            }
        }
        maxMain = std::max(maxMain, lineMain);
        totalCross += lineCross;
        return horiz
            ? Size{maxMain + padW, totalCross + padH}
            : Size{totalCross + padW, maxMain + padH};
    }

    float totalH = 0.0f;
    float maxW = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        totalH += d.height;
        maxW = std::max(maxW, d.width);
    }
    return Size{maxW + padW, totalH + padH};
}

void ItemsControl::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));

    const detail::ParsedItemsPanelConfig panelCfg = detail::ResolveItemsPanelConfig(this);
    if (IsVirtualizing() && !IsA<ComboBox>() &&
        panelCfg.mode == detail::ItemsPanelMode::VerticalStack &&
        (!items_.empty() || hasBoundItemsSource_ || FirstChild() == nullptr)) {
        if (items_.empty()) {
            return;
        }
        const float itemH = GetVirtualItemHeight();
        float visibleTop = 0.0f;
        float visibleH = std::max(itemH, finalRect.height - (p.top + p.bottom));
        if (ComputeLiveScrollViewport(visibleTop, visibleH)) {
            visibleH = std::max(itemH, visibleH);
        }

        const float totalLogicalH = static_cast<float>(items_.size()) * itemH;
        const float maxScrollTop = std::max(0.0f, totalLogicalH - visibleH);
        visibleTop = std::min(visibleTop, maxScrollTop);

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(visibleTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((visibleTop + visibleH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));

        const float prevEstimatedMaxW = estimatedMaxItemWidth_;
        if (firstIdx != firstRealizedIndex_ || endIdx != realizedEndExclusive_ || virtualSlots_.empty()) {
            SyncVirtualizedWindow(firstIdx, endIdx, Size{innerW, itemH});
        }

        for (const auto& slot : virtualSlots_) {
            if (slot.element) {
                const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
            }
        }

        if (estimatedMaxItemWidth_ > prevEstimatedMaxW + 1e-3f) {
            InvalidateLayout();
        } else {
            ClearLayoutDirtyFlags(static_cast<std::uint8_t>(MeasureDirty | SubtreeMeasureDirty));
            for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
                bool anyChildMeasureDirty = false;
                for (const UIElement* sib = anc->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                    if ((sib->GetLayoutDirtyFlags() & (MeasureDirty | SubtreeMeasureDirty)) != 0u) {
                        anyChildMeasureDirty = true;
                        break;
                    }
                }
                if (!anyChildMeasureDirty) {
                    anc->ClearLayoutDirtyFlags(SubtreeMeasureDirty);
                } else {
                    break;
                }
            }
        }
        return;
    }

    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));
    if (panelCfg.mode == detail::ItemsPanelMode::Canvas) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            const Size d = c->DesiredSize();
            float cx = startX;
            if (c->GetPropertySourceRank(PropId_CanvasLeft) != ValueSourceRank::Default) {
                cx = startX + Canvas::GetLeft(c);
            } else if (c->GetPropertySourceRank(PropId_CanvasRight) != ValueSourceRank::Default) {
                cx = startX + innerW - d.width - Canvas::GetRight(c);
            }
            float cy = startY;
            if (c->GetPropertySourceRank(PropId_CanvasTop) != ValueSourceRank::Default) {
                cy = startY + Canvas::GetTop(c);
            } else if (c->GetPropertySourceRank(PropId_CanvasBottom) != ValueSourceRank::Default) {
                cy = startY + innerH - d.height - Canvas::GetBottom(c);
            }
            c->Arrange(Rect{cx, cy, d.width, d.height});
        }
        return;
    }
    if (panelCfg.mode == detail::ItemsPanelMode::HorizontalStack) {
        float cursorX = startX;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() == Visibility::Collapsed) {
                c->Arrange(Rect{cursorX, startY, 0.0f, 0.0f});
                continue;
            }
            const Size d = c->DesiredSize();
            c->Arrange(Rect{cursorX, startY, d.width, innerH});
            cursorX += d.width + panelCfg.spacing;
        }
        return;
    }
    if (panelCfg.mode == detail::ItemsPanelMode::UniformGrid) {
        std::size_t visCount = 0u;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() != Visibility::Collapsed) {
                ++visCount;
            }
        }
        std::int32_t cols = panelCfg.cols;
        std::int32_t rows = panelCfg.rows;
        std::int32_t firstCol = panelCfg.firstCol;
        if (cols <= 0) {
            cols = (rows > 0 && visCount > 0u)
                ? static_cast<std::int32_t>((visCount + firstCol + rows - 1) / rows)
                : std::max<std::int32_t>(
                      1,
                      static_cast<std::int32_t>(
                          std::ceil(std::sqrt(static_cast<float>(std::max<std::size_t>(1u, visCount))))));
        }
        if (firstCol >= cols) {
            firstCol = 0;
        }
        if (rows <= 0) {
            rows = std::max<std::int32_t>(1, static_cast<std::int32_t>((visCount + firstCol + cols - 1) / cols));
        }
        const float cellW = innerW / static_cast<float>(cols);
        const float cellH = innerH / static_cast<float>(rows);
        std::int32_t slot = firstCol;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() == Visibility::Collapsed) {
                c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
                continue;
            }
            const std::int32_t rIdx = slot / cols;
            const std::int32_t cIdx = slot % cols;
            c->Arrange(Rect{startX + cIdx * cellW, startY + rIdx * cellH, cellW, cellH});
            ++slot;
        }
        return;
    }
    if (panelCfg.mode == detail::ItemsPanelMode::WrapHorizontal ||
        panelCfg.mode == detail::ItemsPanelMode::WrapVertical) {
        const bool horiz = (panelCfg.mode == detail::ItemsPanelMode::WrapHorizontal);
        const float limit = horiz ? innerW : innerH;
        float curMain = 0.0f;
        float curCross = 0.0f;
        float lineCross = 0.0f;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c->GetVisibility() == Visibility::Collapsed) {
                c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
                continue;
            }
            const float cw = (panelCfg.itemWidth > 0.0f) ? panelCfg.itemWidth : c->DesiredSize().width;
            const float ch = (panelCfg.itemHeight > 0.0f) ? panelCfg.itemHeight : c->DesiredSize().height;
            const float m = horiz ? cw : ch;
            const float cr = horiz ? ch : cw;
            if (curMain > 0.0f && curMain + m > limit) {
                curCross += lineCross;
                curMain = 0.0f;
                lineCross = 0.0f;
            }
            if (horiz) {
                c->Arrange(Rect{startX + curMain, startY + curCross, cw, ch});
            } else {
                c->Arrange(Rect{startX + curCross, startY + curMain, cw, ch});
            }
            curMain += m;
            lineCross = std::max(lineCross, cr);
        }
        return;
    }

    float cursorY = startY;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const Size d = c->DesiredSize();
        c->Arrange(Rect{startX, cursorY, innerW, d.height});
        cursorY += d.height;
    }
}

// ============================================================================
// ListBox
// ============================================================================

ListBox::ListBox() {
    EnsureControlTypesRegistered();
}

void ListBox::MarkExtraValuesInPools() const noexcept {
    Selector::MarkExtraValuesInPools();
    if (lastNotifiedSelectedItem_.isTraceable()) {
        rs_rt::markValueInPools(lastNotifiedSelectedItem_);
    }
}

void ListBox::ScrollItemIntoView(std::int32_t index) noexcept {
    if (index < 0) {
        return;
    }
    ScrollViewer* sv = nullptr;
    for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (auto* s = anc->DynamicCast<ScrollViewer>()) {
            sv = s;
            break;
        }
    }
    if (!sv) {
        return;
    }

    const Thickness p = GetPadding();
    const UIElement* svChild = this;
    while (svChild->Parent() != nullptr && svChild->Parent() != sv) {
        svChild = svChild->Parent();
    }
    const float topInset = p.top + ItemsAreaTopInset();
    float staticRowStartInContent = GetMargin().top + topInset;
    if (this != svChild) {
        if (FinalRect().height > 0.0f || svChild->FinalRect().height > 0.0f) {
            staticRowStartInContent =
                (FinalRect().y - (svChild->FinalRect().y - svChild->GetMargin().top)) + topInset;
        } else {
            for (const UIElement* cur = Parent(); cur != nullptr && cur != sv; cur = cur->Parent()) {
                staticRowStartInContent += cur->GetMargin().top + cur->GetPadding().top;
            }
        }
    }

    float relTop = 0.0f;
    float rowH = GetVirtualItemHeight();
    if (IsVirtualizing()) {
        relTop = static_cast<float>(index) * rowH;
    } else {
        std::int32_t curIdx = 0;
        float accumY = 0.0f;
        bool foundChild = false;
        for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++curIdx) {
            const float chH = (c->FinalRect().height > 0.0f)
                ? c->FinalRect().height
                : ((c->DesiredSize().height > 0.0f) ? c->DesiredSize().height : rowH);
            if (curIdx == index) {
                if (c->FinalRect().height > 0.0f && FinalRect().height > 0.0f) {
                    relTop = c->FinalRect().y - (FinalRect().y + topInset);
                } else {
                    relTop = accumY;
                }
                rowH = chH;
                foundChild = true;
                break;
            }
            accumY += chH;
        }
        if (!foundChild) {
            relTop = static_cast<float>(index) * rowH;
        }
    }

    float dummyViewTop = 0.0f;
    float viewH = 0.0f;
    (void)ComputeLiveScrollViewport(dummyViewTop, viewH);
    if (viewH <= 0.0f) {
        return;
    }

    const float itemTop = staticRowStartInContent + relTop;
    const float itemBottom = itemTop + rowH;
    const float curScrollY = std::max(0.0f, sv->GetVerticalOffset());
    if (itemTop < curScrollY) {
        sv->SetVerticalOffset(std::max(0.0f, itemTop));
    } else if (itemBottom > curScrollY + viewH) {
        sv->SetVerticalOffset(std::max(0.0f, itemBottom - viewH));
    }
}

void ListBox::SetSelectionMode(std::int32_t mode) {
    SetValue(
        PropId_SelectionMode,
        value::FromInt(std::clamp<std::int32_t>(mode, 0, 2)),
        ValueSourceRank::Local);
}

std::int32_t ListBox::GetSelectionMode() const noexcept {
    const UnifiedValue v = GetValue(PropId_SelectionMode);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return std::clamp<std::int32_t>(static_cast<std::int32_t>(*i), 0, 2);
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_SelectionMode, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            return std::clamp<std::int32_t>(static_cast<std::int32_t>(*pi), 0, 2);
        }
    }
    return 0;
}

bool ListBox::IsIndexSelected(std::int32_t index) const noexcept {
    if (index < 0) {
        return false;
    }
    if (GetSelectedIndex() == index) {
        return true;
    }
    return std::find(selectedIndices_.begin(), selectedIndices_.end(), index) != selectedIndices_.end();
}

void ListBox::ClearSelection() {
    selectedIndices_.clear();
    selectionAnchorIndex_ = -1;
    SetSelectedIndex(-1);
    InvalidateRender();
}

bool ListBox::SelectIndexWithModifiers(std::int32_t index, std::uint8_t modifiers) {
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::vector<std::int32_t> prevSelectedIndices = selectedIndices_;
    const std::int32_t prevPrimary = GetSelectedIndex();
    auto emitMultiSelectChangedIfNeeded = [&]() {
        if (selectedIndices_ != prevSelectedIndices && GetSelectedIndex() == prevPrimary) {
            const std::int32_t curIdx = GetSelectedIndex();
            lastNotifiedSelectedIndex_ = curIdx;
            lastNotifiedSelectedItem_ = GetSelectedItem();
            if (lastNotifiedSelectedItem_.isTraceable()) {
                rs_rt::pinValueInPools(lastNotifiedSelectedItem_);
                EnsureRegisteredInValuePoolSet();
            }
            if (selectionChangedCallback_) {
                selectionChangedCallback_(*this, curIdx);
            }
            (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(curIdx), true);
        }
    };
    const std::int32_t mode = GetSelectionMode();
    if (mode == 1) {
        auto it = std::find(selectedIndices_.begin(), selectedIndices_.end(), clamped);
        if (it != selectedIndices_.end()) {
            selectedIndices_.erase(it);
            const std::int32_t nextPrimary = selectedIndices_.empty() ? -1 : selectedIndices_.front();
            SetSelectedIndex(nextPrimary);
        } else {
            selectedIndices_.push_back(clamped);
            std::sort(selectedIndices_.begin(), selectedIndices_.end());
            SetSelectedIndex(clamped);
        }
        selectionAnchorIndex_ = clamped;
        ScrollItemIntoView(clamped);
        InvalidateRender();
        emitMultiSelectChangedIfNeeded();
        return selectedIndices_ != prevSelectedIndices;
    }
    if (mode == 2) {
        if ((modifiers & KeyMod_Shift) != 0u && selectionAnchorIndex_ >= 0) {
            const std::int32_t lo = std::min(selectionAnchorIndex_, clamped);
            const std::int32_t hi = std::max(selectionAnchorIndex_, clamped);
            if ((modifiers & KeyMod_Ctrl) == 0u) {
                selectedIndices_.clear();
            }
            for (std::int32_t k = lo; k <= hi; ++k) {
                if (std::find(selectedIndices_.begin(), selectedIndices_.end(), k) ==
                    selectedIndices_.end()) {
                    selectedIndices_.push_back(k);
                }
            }
            std::sort(selectedIndices_.begin(), selectedIndices_.end());
            SetSelectedIndex(clamped);
            ScrollItemIntoView(clamped);
            InvalidateRender();
            emitMultiSelectChangedIfNeeded();
            return selectedIndices_ != prevSelectedIndices;
        }
        if ((modifiers & KeyMod_Ctrl) != 0u) {
            auto it = std::find(selectedIndices_.begin(), selectedIndices_.end(), clamped);
            if (it != selectedIndices_.end()) {
                selectedIndices_.erase(it);
                SetSelectedIndex(selectedIndices_.empty() ? -1 : selectedIndices_.front());
            } else {
                selectedIndices_.push_back(clamped);
                std::sort(selectedIndices_.begin(), selectedIndices_.end());
                SetSelectedIndex(clamped);
            }
            selectionAnchorIndex_ = clamped;
            ScrollItemIntoView(clamped);
            InvalidateRender();
            emitMultiSelectChangedIfNeeded();
            return selectedIndices_ != prevSelectedIndices;
        }
    }
    selectedIndices_.assign(1u, clamped);
    selectionAnchorIndex_ = clamped;
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    ScrollItemIntoView(clamped);
    return clamped != prev;
}

bool ListBox::SelectIndexInteractive(std::int32_t index) {
    return SelectIndexWithModifiers(index, 0u);
}

bool ListBox::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }

    std::size_t childOffset = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++childOffset) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Rect cr = c->FinalRect();
        if (cr.width > 0.0f && cr.height > 0.0f &&
            localPoint.x >= cr.x && localPoint.x < cr.x + cr.width &&
            localPoint.y >= cr.y && localPoint.y < cr.y + cr.height) {
            if (!c->IsEnabled()) {
                return true;
            }
            const std::size_t logicalIdx = IsVirtualizing()
                ? (FirstRealizedItemIndex() + childOffset)
                : childOffset;
            if (logicalIdx < count) {
                (void)SelectIndexWithModifiers(static_cast<std::int32_t>(logicalIdx), modifiers);
                return true;
            }
        }
    }

    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float relY = localPoint.y - (r.y + p.top + ItemsAreaTopInset());
    if (relY < 0.0f) {
        return false;
    }
    if (IsVirtualizing() || FirstChild() == nullptr) {
        const float itemH = std::max(1.0f, GetVirtualItemHeight());
        const auto hitIdx = static_cast<std::int32_t>(std::floor(relY / itemH));
        if (hitIdx >= 0 && static_cast<std::size_t>(hitIdx) < count) {
            (void)SelectIndexWithModifiers(hitIdx, modifiers);
            return true;
        }
        return false;
    }

    float accumY = 0.0f;
    std::size_t idx = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr && idx < count; c = c->NextSibling(), ++idx) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const float chH = (c->DesiredSize().height > 0.0f) ? c->DesiredSize().height : GetVirtualItemHeight();
        if (relY >= accumY && relY < accumY + chH) {
            if (!c->IsEnabled()) {
                return true;
            }
            (void)SelectIndexWithModifiers(static_cast<std::int32_t>(idx), modifiers);
            return true;
        }
        accumY += chH;
    }
    return false;
}

bool ListBox::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t cur = GetSelectedIndex();
    switch (key) {
        case KeyCode::Down: {
            const std::int32_t next = (cur < 0)
                ? 0
                : std::min<std::int32_t>(static_cast<std::int32_t>(count - 1u), cur + 1);
            (void)SelectIndexInteractive(next);
            return true;
        }
        case KeyCode::Up: {
            const std::int32_t prev = (cur <= 0) ? 0 : (cur - 1);
            (void)SelectIndexInteractive(prev);
            return true;
        }
        case KeyCode::Home:
            (void)SelectIndexInteractive(0);
            return true;
        case KeyCode::End:
            (void)SelectIndexInteractive(static_cast<std::int32_t>(count - 1u));
            return true;
        default:
            return false;
    }
}

void ListBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Selector::OnPropertyChanged(propId, newValue);
    if (propId == PropId_SelectedIndex || propId == PropId_SelectedItem) {
        if (IsDeferringSelectionNotify()) {
            return;
        }
        const std::int32_t curIdx = GetSelectedIndex();
        const UnifiedValue curItem = GetSelectedItem();
        const bool indexChanged = (curIdx != lastNotifiedSelectedIndex_);
        const bool itemChanged = hasNotifiedSelection_ &&
                                 !detail::CollectionItemValuesEqual(curItem, lastNotifiedSelectedItem_);
        if (indexChanged || itemChanged) {
            hasNotifiedSelection_ = true;
            lastNotifiedSelectedIndex_ = curIdx;
            lastNotifiedSelectedItem_ = curItem;
            if (lastNotifiedSelectedItem_.isTraceable()) {
                rs_rt::pinValueInPools(lastNotifiedSelectedItem_);
                EnsureRegisteredInValuePoolSet();
            }
            if (selectionChangedCallback_) {
                selectionChangedCallback_(*this, curIdx);
            }
            (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(curIdx), true);
        }
    }
}

// ============================================================================
// ListView
// ============================================================================

ListView::ListView() {
    EnsureControlTypesRegistered();
}

bool ListView::HasGridViewHeader() const noexcept {
    const UnifiedValue gv = GetValue(PropId_GridViewColumnsSpec);
    const auto* sv = std::get_if<StringValue>(&gv);
    return sv && !sv->view().empty();
}

Size ListView::MeasureCore(Size availableSize) noexcept {
    const float hdrH = HeaderBarHeight();
    const Size innerAvail{
        availableSize.width,
        std::isfinite(availableSize.height) ? std::max(0.0f, availableSize.height - hdrH) : availableSize.height};
    const Size baseSize = ListBox::MeasureCore(innerAvail);
    float minHeaderW = 0.0f;
    if (hdrH > 0.0f) {
        const UnifiedValue gv = GetValue(PropId_GridViewColumnsSpec);
        if (const auto* sv = std::get_if<StringValue>(&gv)) {
            const std::string_view allCols = sv->view();
            std::size_t pos = 0u;
            while (pos <= allCols.size()) {
                const std::size_t recEnd = allCols.find('\x1e', pos);
                const std::string_view rec = (recEnd == std::string_view::npos)
                    ? allCols.substr(pos)
                    : allCols.substr(pos, recEnd - pos);
                const std::size_t f0 = rec.find('\x1f');
                if (f0 != std::string_view::npos) {
                    const std::size_t f1 = rec.find('\x1f', f0 + 1u);
                    const std::string_view wStr = (f1 == std::string_view::npos)
                        ? rec.substr(f0 + 1u)
                        : rec.substr(f0 + 1u, f1 - (f0 + 1u));
                    float cw = 100.0f;
                    if (!wStr.empty() &&
                        !str::EqualsIgnoreCase(wStr, "Auto") &&
                        !str::EqualsIgnoreCase(wStr, "NaN")) {
                        float parsedW = 0.0f;
                        if (TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Width, wStr), parsedW) &&
                            std::isfinite(parsedW) && parsedW > 0.0f) {
                            cw = parsedW;
                        }
                    }
                    minHeaderW += std::max(24.0f, cw);
                }
                if (recEnd == std::string_view::npos) {
                    break;
                }
                pos = recEnd + 1u;
            }
        }
    }
    return Size{std::max(baseSize.width, minHeaderW + 8.0f), baseSize.height + hdrH};
}

void ListView::ArrangeCore(Rect finalRect) noexcept {
    const float hdrH = HeaderBarHeight();
    const Rect rowsRect{
        finalRect.x,
        finalRect.y + hdrH,
        finalRect.width,
        std::max(0.0f, finalRect.height - hdrH)};
    ListBox::ArrangeCore(rowsRect);
}

bool ListView::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    const float hdrH = HeaderBarHeight();
    if (hdrH > 0.0f && localPoint.y < FinalRect().y + GetPadding().top + hdrH) {
        outRequestCapture = false;
        return true;
    }
    return ListBox::OnPointerDown(localPoint, button, modifiers, outRequestCapture);
}

// ============================================================================
// ComboBox
// ============================================================================

ComboBox::ComboBox() {
    EnsureControlTypesRegistered();
}

bool ComboBox::ShouldSkipControlTemplatePackage(const AxbPackage& package) const noexcept {
    for (const std::string& s : package.stringTable) {
        if (s == "Popup" || s == "PART_Popup" || s == "ItemsPresenter") {
            return true;
        }
    }
    return false;
}

std::size_t ComboBox::ComboItemCount() const noexcept {
    if (!Items().empty()) {
        return Items().size();
    }
    return ChildCount();
}

UnifiedValue ComboBox::GetComboItemValue(std::size_t index) const {
    if (!Items().empty()) {
        return (index < Items().size()) ? Items()[index] : UnifiedValue{};
    }
    std::size_t cur = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
        if (cur == index) {
            return value::FromString(detail::ExtractElementDisplayText(c));
        }
    }
    return UnifiedValue{};
}

std::string ComboBox::FormatItemDisplayText(const UnifiedValue& val) const {
    return detail::ResolveSmartObjectDisplayText(this, Domains(), BoundTwoWayEngine(), val);
}

std::string ComboBox::GetComboItemText(std::size_t index) const {
    if (!Items().empty()) {
        if (index < Items().size()) {
            return FormatItemDisplayText(Items()[index]);
        }
        return {};
    }
    std::size_t cur = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
        if (cur == index) {
            return detail::ExtractElementDisplayText(c);
        }
    }
    return {};
}

void ComboBox::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t ComboBox::GetSelectedIndex() const noexcept {
    const UnifiedValue v = GetValue(PropId_SelectedIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    return -1;
}

UnifiedValue ComboBox::GetSelectedItem() const {
    const UnifiedValue explicitItem = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(explicitItem) &&
        !std::holds_alternative<NullObject>(explicitItem)) {
        return explicitItem;
    }
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < ComboItemCount()) {
        return GetComboItemValue(static_cast<std::size_t>(idx));
    }
    return UnifiedValue{};
}

std::string ComboBox::GetSelectedItemText() const {
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < ComboItemCount()) {
        return GetComboItemText(static_cast<std::size_t>(idx));
    }
    const UnifiedValue itemVal = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(itemVal) &&
        !std::holds_alternative<NullObject>(itemVal)) {
        return FormatItemDisplayText(itemVal);
    }
    return {};
}

void ComboBox::SetIsDropDownOpen(bool open) {
    SetValue(PropId_IsDropDownOpen, UnifiedValue{open}, ValueSourceRank::Local);
}

bool ComboBox::IsDropDownOpen() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsDropDownOpen);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void ComboBox::SetIsEditable(bool editable) {
    SetValue(PropId_IsEditable, UnifiedValue{editable}, ValueSourceRank::Local);
}

bool ComboBox::IsEditable() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsEditable);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

float ComboBox::GetMaxDropDownHeight() const noexcept {
    float h = 0.0f;
    if (TryGetFloat(GetValue(PropId_MaxDropDownHeight), h) && std::isfinite(h) && h > 0.0f) {
        return h;
    }
    return 0.0f;
}

std::string ComboBox::GetDisplayText() const {
    if (IsEditable()) {
        const UnifiedValue tv = GetValue(PropId_Text);
        if (const auto* sv = std::get_if<StringValue>(&tv); sv && !sv->view().empty()) {
            return sv->str();
        }
    }
    const std::string sel = GetSelectedItemText();
    if (!sel.empty()) {
        return sel;
    }
    const UnifiedValue tv = GetValue(PropId_Text);
    if (const auto* sv = std::get_if<StringValue>(&tv); sv && !sv->view().empty()) {
        return sv->str();
    }
    return {};
}

bool ComboBox::OnTextInput(std::string_view utf8Text) {
    if (!IsEditable() || !IsEffectivelyEnabled() || utf8Text.empty()) {
        return false;
    }
    std::string cur = GetDisplayText();
    for (char ch : utf8Text) {
        if (static_cast<unsigned char>(ch) >= 0x20u && ch != 0x7F) {
            cur.push_back(ch);
        }
    }
    syncingSelection_ = true;
    SetValue(PropId_Text, value::FromString(cur), ValueSourceRank::Local);
    const std::size_t count = ComboItemCount();
    for (std::size_t i = 0u; i < count; ++i) {
        if (GetComboItemText(i) == cur) {
            SetSelectedIndex(static_cast<std::int32_t>(i));
            SetValue(PropId_SelectedItem, GetComboItemValue(i), ValueSourceRank::Local);
            break;
        }
    }
    syncingSelection_ = false;
    (void)NotifyTwoWayPropertyMutated(PropId_Text);
    InvalidateRender();
    return true;
}

void ComboBox::SyncSelectedItemFromIndex() {
    if (syncingSelection_) {
        return;
    }
    const std::int32_t idx = GetSelectedIndex();
    const std::size_t count = ComboItemCount();
    syncingSelection_ = true;
    if (idx >= 0 && static_cast<std::size_t>(idx) < count) {
        const UnifiedValue rawItem = (!Items().empty() && static_cast<std::size_t>(idx) < Items().size())
            ? Items()[static_cast<std::size_t>(idx)]
            : [&]() -> UnifiedValue {
                std::size_t cur = 0u;
                for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
                    if (cur == static_cast<std::size_t>(idx)) {
                        return UnifiedValue{c->ToNativeHandle(
                            Domains() ? Domains()->nativeHandles() : FallbackVisualHandleRegistry())};
                    }
                }
                return GetComboItemValue(static_cast<std::size_t>(idx));
            }();
        SetValue(PropId_SelectedItem, GetComboItemValue(static_cast<std::size_t>(idx)), ValueSourceRank::Local);
        SetValue(
            PropId_Text,
            value::FromString(GetComboItemText(static_cast<std::size_t>(idx))),
            ValueSourceRank::Local);
        const UnifiedValue sv = detail::ResolveSelectedValueForItem(
            this,
            Domains(),
            BoundTwoWayEngine(),
            rawItem);
        if (!std::holds_alternative<std::monostate>(sv) && !std::holds_alternative<NullObject>(sv)) {
            SetValue(PropId_SelectedValue, sv, ValueSourceRank::Local);
        } else {
            SetValue(
                PropId_SelectedValue,
                GetComboItemValue(static_cast<std::size_t>(idx)),
                ValueSourceRank::Local);
        }
    } else {
        if (!hasPendingSelectedValue_) {
            (void)ClearValue(PropId_SelectedValue, ValueSourceRank::Local);
        }
    }
    syncingSelection_ = false;
}

bool ComboBox::SelectIndexInteractive(std::int32_t index) {
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        SetIsDropDownOpen(false);
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    SyncSelectedItemFromIndex();
    SetIsDropDownOpen(false);
    highlightedPopupIndex_ = -1;

    if (clamped != prev) {
        if (selectionChangedCallback_) {
            selectionChangedCallback_(*this, clamped);
        }
        (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(clamped), true);
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedValue);
        (void)NotifyTwoWayPropertyMutated(PropId_Text);
        return true;
    }
    return false;
}

Rect ComboBox::DropDownPopupLocalRect() const noexcept {
    if (!IsDropDownOpen()) {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const Rect r = FinalRect();
    const float w = (r.width > 0.0f) ? r.width : std::max(100.0f, GetWidth());
    const float h = (r.height > 0.0f) ? r.height : std::max(28.0f, GetHeight());
    float popupH = static_cast<float>(count) * kPopupRowHeight;
    const float maxDropH = GetMaxDropDownHeight();
    if (maxDropH > 0.0f) {
        popupH = std::min(popupH, maxDropH);
    }
    return Rect{
        r.x,
        r.y + h + 2.0f,
        w,
        popupH};
}

Rect ComboBox::DropDownPopupScreenRect() const noexcept {
    return LocalToScreenBounds(DropDownPopupLocalRect());
}

std::int32_t ComboBox::HitTestPopupItemIndex(Point screenPoint) const noexcept {
    if (!IsDropDownOpen()) {
        return -1;
    }
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        return -1;
    }
    const Point localPt = ScreenToLocal(screenPoint);
    const Rect popup = DropDownPopupLocalRect();
    if (popup.width <= 0.0f || popup.height <= 0.0f ||
        localPt.x < popup.x || localPt.x > popup.x + popup.width ||
        localPt.y < popup.y || localPt.y > popup.y + popup.height) {
        return -1;
    }
    const float relY = localPt.y - popup.y;
    const auto idx = static_cast<std::int32_t>(std::floor(relY / kPopupRowHeight));
    return std::clamp<std::int32_t>(idx, 0, static_cast<std::int32_t>(count - 1u));
}

bool ComboBox::TryResolveSelectedValue(const UnifiedValue& targetVal) {
    const std::string wantStr = FormatBindingString({}, &targetVal, 1u, Domains());
    const std::size_t count = ComboItemCount();
    for (std::size_t i = 0u; i < count; ++i) {
        const UnifiedValue rawItem = (!Items().empty() && i < Items().size())
            ? Items()[i]
            : [&]() -> UnifiedValue {
                std::size_t cur = 0u;
                for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
                    if (cur == i) {
                        return UnifiedValue{c->ToNativeHandle(
                            Domains() ? Domains()->nativeHandles() : FallbackVisualHandleRegistry())};
                    }
                }
                return GetComboItemValue(i);
            }();
        const UnifiedValue candSv = detail::ResolveSelectedValueForItem(
            this,
            Domains(),
            BoundTwoWayEngine(),
            rawItem);
        if (detail::CollectionItemValuesEqual(candSv, targetVal) ||
            (!wantStr.empty() && FormatBindingString({}, &candSv, 1u, Domains()) == wantStr)) {
            hasPendingSelectedValue_ = false;
            pendingSelectedValue_ = {};
            syncingSelection_ = true;
            SetSelectedIndex(static_cast<std::int32_t>(i));
            syncingSelection_ = false;
            SyncSelectedItemFromIndex();
            return true;
        }
    }
    return false;
}

void ComboBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Selector::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Visibility && GetVisibility() != Visibility::Visible && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    } else if (propId == PropId_ItemsSource || propId == PropId_SelectedValuePath) {
        if (hasPendingSelectedValue_ && TryResolveSelectedValue(pendingSelectedValue_)) {
            return;
        }
        SyncSelectedItemFromIndex();
    } else if (propId == PropId_SelectedIndex) {
        if (!syncingSelection_) {
            hasPendingSelectedValue_ = false;
            pendingSelectedValue_ = {};
        }
        SyncSelectedItemFromIndex();
    } else if (propId == PropId_SelectedValue && !syncingSelection_) {
        if (std::holds_alternative<std::monostate>(newValue) ||
            std::holds_alternative<NullObject>(newValue)) {
            hasPendingSelectedValue_ = false;
            pendingSelectedValue_ = {};
            if (GetSelectedIndex() >= 0) {
                syncingSelection_ = true;
                SetSelectedIndex(-1);
                syncingSelection_ = false;
                SyncSelectedItemFromIndex();
            }
        } else if (!TryResolveSelectedValue(newValue)) {
            pendingSelectedValue_ = newValue;
            if (pendingSelectedValue_.isTraceable()) {
                rs_rt::pinValueInPools(pendingSelectedValue_);
            }
            hasPendingSelectedValue_ = true;
        }
    } else if (propId == PropId_SelectedItem && !syncingSelection_) {
        hasPendingSelectedValue_ = false;
        pendingSelectedValue_ = {};
        const std::string targetStr = rs_rt::valueToString(newValue);
        const std::size_t count = ComboItemCount();
        for (std::size_t i = 0u; i < count; ++i) {
            if (GetComboItemText(i) == targetStr) {
                syncingSelection_ = true;
                SetSelectedIndex(static_cast<std::int32_t>(i));
                syncingSelection_ = false;
                break;
            }
        }
    }
}

void ComboBox::OnFocusChanged(bool focused) {
    if (!focused && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    }
}

void ComboBox::OnEnabledStateChanged(bool effectivelyEnabled) {
    if (!effectivelyEnabled && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    }
}

bool ComboBox::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)localPoint;
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const bool nextOpen = !IsDropDownOpen();
    SetIsDropDownOpen(nextOpen);
    highlightedPopupIndex_ = nextOpen ? GetSelectedIndex() : -1;
    return true;
}

bool ComboBox::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = ComboItemCount();
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        if (IsDropDownOpen()) {
            const std::int32_t pick = (highlightedPopupIndex_ >= 0)
                ? highlightedPopupIndex_
                : std::max<std::int32_t>(0, GetSelectedIndex());
            (void)SelectIndexInteractive(pick);
        } else {
            SetIsDropDownOpen(true);
            highlightedPopupIndex_ = GetSelectedIndex();
        }
        return true;
    }
    if (IsEditable() && key == KeyCode::Backspace) {
        std::string cur = GetDisplayText();
        const std::size_t cpCount = detail::CountUtf8CodePointsSafe(cur);
        if (cpCount > 0u) {
            cur.resize(detail::Utf8ByteOffsetAtCodePoint(cur, cpCount - 1u));
            const std::int32_t prevIdx = GetSelectedIndex();
            std::int32_t matchedIdx = -1;
            const std::size_t comboCount = ComboItemCount();
            for (std::size_t i = 0u; i < comboCount; ++i) {
                if (GetComboItemText(i) == cur) {
                    matchedIdx = static_cast<std::int32_t>(i);
                    break;
                }
            }
            syncingSelection_ = true;
            SetValue(PropId_Text, value::FromString(cur), ValueSourceRank::Local);
            SetSelectedIndex(matchedIdx);
            if (matchedIdx >= 0) {
                SetValue(
                    PropId_SelectedItem,
                    GetComboItemValue(static_cast<std::size_t>(matchedIdx)),
                    ValueSourceRank::Local);
            } else {
                (void)ClearValue(PropId_SelectedItem, ValueSourceRank::Local);
            }
            syncingSelection_ = false;
            if (matchedIdx != prevIdx) {
                if (selectionChangedCallback_) {
                    selectionChangedCallback_(*this, matchedIdx);
                }
                (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(matchedIdx), true);
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
            }
            (void)NotifyTwoWayPropertyMutated(PropId_Text);
            InvalidateRender();
            return true;
        }
    }
    if (key == KeyCode::Escape && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
        return true;
    }
    if (count > 0u && (key == KeyCode::Up || key == KeyCode::Down)) {
        if (IsDropDownOpen()) {
            std::int32_t cur = (highlightedPopupIndex_ >= 0)
                ? highlightedPopupIndex_
                : std::max<std::int32_t>(0, GetSelectedIndex());
            cur += (key == KeyCode::Down) ? 1 : -1;
            cur = std::clamp<std::int32_t>(cur, 0, static_cast<std::int32_t>(count - 1u));
            SetHighlightedPopupIndex(cur);
        } else {
            const std::int32_t cur = std::max<std::int32_t>(0, GetSelectedIndex());
            const std::int32_t next = std::clamp<std::int32_t>(
                cur + ((key == KeyCode::Down) ? 1 : -1),
                0,
                static_cast<std::int32_t>(count - 1u));
            (void)SelectIndexInteractive(next);
        }
        return true;
    }
    return false;
}

Size ComboBox::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float padW = (p.left + p.right > 0.0f) ? (p.left + p.right) : 12.0f;
    const float padH = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 6.0f;
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    float maxTextW = 64.0f;
    const std::size_t count = ComboItemCount();
    for (std::size_t i = 0u; i < count; ++i) {
        maxTextW = std::max(maxTextW, MeasureUtf8TextRun(GetComboItemText(i), fontSize).width);
    }
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(Size{0.0f, 0.0f});
    }
    return Size{
        std::max(100.0f, maxTextW + 28.0f + padW),
        std::max(28.0f, fontSize * 1.25f + padH)};
}

void ComboBox::ArrangeCore(Rect finalRect) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{finalRect.x, finalRect.y, 0.0f, 0.0f});
    }
}

// ============================================================================
// ListBoxItem, ComboBoxItem & ListViewItem
// ============================================================================

ListBoxItem::ListBoxItem() {
    EnsureControlTypesRegistered();
}

void ListBoxItem::SetIsSelected(bool selected) {
    SetValue(PropId_IsChecked, value::FromBool(selected), ValueSourceRank::Local);
}

bool ListBoxItem::IsSelected() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsChecked), false);
}

void ListBoxItem::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ContentControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsChecked) {
        InvalidateRender();
    }
}

void ListBoxItem::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    ContentControl::PopulateAriaInfo(ioInfo);
    ioInfo.role = "listitem";
    if (ioInfo.label.empty()) {
        ioInfo.label = GetContentText();
    }
}

ComboBoxItem::ComboBoxItem() {
    EnsureControlTypesRegistered();
}

void ComboBoxItem::SetIsSelected(bool selected) {
    SetValue(PropId_IsChecked, value::FromBool(selected), ValueSourceRank::Local);
}

bool ComboBoxItem::IsSelected() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsChecked), false);
}

void ComboBoxItem::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ContentControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsChecked) {
        InvalidateRender();
    }
}

void ComboBoxItem::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    ContentControl::PopulateAriaInfo(ioInfo);
    ioInfo.role = "option";
    if (ioInfo.label.empty()) {
        ioInfo.label = GetContentText();
    }
}

ListViewItem::ListViewItem() {
    EnsureControlTypesRegistered();
}

// ============================================================================
// TabItem & TabControl
// ============================================================================

TabItem::TabItem() {
    EnsureControlTypesRegistered();
}

void TabItem::SetIsSelected(bool selected) {
    SetValue(PropId_IsChecked, value::FromBool(selected), ValueSourceRank::Local);
}

bool TabItem::IsSelected() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsChecked), false);
}

Size TabItem::MeasureCore(Size availableSize) noexcept {
    if (Parent() && Parent()->IsA<TabControl>()) {
        const Thickness p = GetPadding();
        const float padW = p.left + p.right;
        const float padH = p.top + p.bottom;
        const Size childAvail{
            std::max(0.0f, availableSize.width - padW),
            std::max(0.0f, availableSize.height - padH)};
        UIElement* hdrElem = GetHeaderElement();
        float maxW = 0.0f;
        float maxH = 0.0f;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c == hdrElem) {
                continue;
            }
            c->Measure(childAvail);
            if (c->GetVisibility() != Visibility::Collapsed) {
                maxW = std::max(maxW, c->DesiredSize().width);
                maxH = std::max(maxH, c->DesiredSize().height);
            }
        }
        return Size{maxW + padW, maxH + padH};
    }
    return HeaderedContentControl::MeasureCore(availableSize);
}

void TabItem::ArrangeCore(Rect finalRect) noexcept {
    if (Parent() && Parent()->IsA<TabControl>()) {
        const Thickness p = GetPadding();
        contentLocalRect_ = Rect{
            finalRect.x + p.left,
            finalRect.y + p.top,
            std::max(0.0f, finalRect.width - (p.left + p.right)),
            std::max(0.0f, finalRect.height - (p.top + p.bottom))};
        UIElement* hdrElem = GetHeaderElement();
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c == hdrElem) {
                continue;
            }
            c->Arrange(contentLocalRect_);
        }
        return;
    }
    HeaderedContentControl::ArrangeCore(finalRect);
}

TabControl::TabControl() {
    EnsureControlTypesRegistered();
}

std::size_t TabControl::TabCount() const noexcept {
    std::size_t count = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            ++count;
        }
    }
    return count;
}

UIElement* TabControl::GetTabElement(std::size_t index) const noexcept {
    std::size_t i = 0u;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (i == index) {
            return c;
        }
        ++i;
    }
    return nullptr;
}

std::string TabControl::GetTabHeaderText(std::size_t index) const {
    const UIElement* elem = GetTabElement(index);
    if (!elem) {
        return {};
    }
    if (const auto* ti = elem->DynamicCast<HeaderedContentControl>()) {
        const std::string h = ti->GetHeader();
        if (!h.empty()) {
            return h;
        }
    }
    const UnifiedValue hv = elem->GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&hv); sv && !sv->view().empty()) {
        return sv->str();
    }
    const UnifiedValue tv = elem->GetValue(PropId_Title);
    if (const auto* sv = std::get_if<StringValue>(&tv); sv && !sv->view().empty()) {
        return sv->str();
    }
    return "Tab " + std::to_string(index + 1u);
}

void TabControl::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t TabControl::GetSelectedIndex() const noexcept {
    const std::size_t count = TabCount();
    if (count == 0u) {
        return detail::CoerceInt32Property(GetValue(PropId_SelectedIndex), -1);
    }
    const std::int32_t raw = detail::CoerceInt32Property(GetValue(PropId_SelectedIndex), 0);
    return std::clamp<std::int32_t>(raw, 0, static_cast<std::int32_t>(count - 1u));
}

bool TabControl::SelectTabInteractive(std::int32_t index) {
    const std::size_t count = TabCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    SyncTabSelectionStates();
    (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
    if (clamped != prev) {
        if (selectionChangedCallback_) {
            selectionChangedCallback_(*this, clamped);
        }
        (void)DispatchWrapperRoutedEvent("SelectionChanged", value::FromInt(clamped), true);
    }
    return true;
}

void TabControl::SetTabStripPlacement(TabStripPlacement placement) {
    SetValue(
        PropId_TabStripPlacement,
        value::FromInt(static_cast<std::int64_t>(placement)),
        ValueSourceRank::Local);
}

TabStripPlacement TabControl::GetTabStripPlacement() const noexcept {
    const UnifiedValue v = GetValue(PropId_TabStripPlacement);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<TabStripPlacement>(*i);
        }
    }
    return TabStripPlacement::Top;
}

Rect TabControl::GetTabHeaderLocalRect(std::size_t index) const noexcept {
    if (index < tabHeaderRects_.size()) {
        return tabHeaderRects_[index];
    }
    return Rect{};
}

std::int32_t TabControl::HitTestTabHeaderIndex(Point localPoint) const noexcept {
    for (std::size_t i = 0u; i < tabHeaderRects_.size(); ++i) {
        if (detail::PointInRect(tabHeaderRects_[i], localPoint)) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

bool TabControl::IsChildInactiveTabPage(const UIElement* child) const noexcept {
    if (!child || child->Parent() != this) {
        return false;
    }
    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t idx = 0;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (c == child) {
            return idx != activeIdx;
        }
        ++idx;
    }
    return true;
}

void TabControl::SyncTabSelectionStates() noexcept {
    if (syncingTabs_) {
        return;
    }
    syncingTabs_ = true;
    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t idx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (auto* ti = c->DynamicCast<TabItem>()) {
            ti->SetIsSelected(idx == activeIdx);
        }
        ++idx;
    }
    syncingTabs_ = false;
}

void TabControl::MarkExtraValuesInPools() const noexcept {
    Selector::MarkExtraValuesInPools();
}

UnifiedValue TabControl::NormalizeTabItemValue(const UnifiedValue& raw) {
    UnifiedValue out = raw;
    if (const auto* strRef = std::get_if<ObjectRef>(&out)) {
        if (strRef->kind == ObjectKind::String) {
            if (ExecutionDomainContext* effDomains = EffectiveDomains()) {
                if (const auto sv = effDomains->stringViewAcrossDomains(*strRef)) {
                    out = value::FromString(std::string(*sv));
                }
            }
        }
    }
    if (out.isTraceable()) {
        rs_rt::pinValueInPools(out);
        EnsureRegisteredInValuePoolSet();
    }
    return out;
}

void TabControl::ReleaseTabChildResources(UIElement* child) {
    ReleaseItemsChildSubtreeResources(
        child,
        EffectiveBindingEngine(),
        EffectiveStyleEngine(),
        EffectiveAnimationEngine());
}

IntrusivePtr<UIElement> TabControl::InstantiateTabItemForValue(const UnifiedValue& itemVal) {
    auto tab = MakeIntrusive<TabItem>();
    tab->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
    const UnifiedValue itemTplVal = GetValue(PropId_ItemTemplate);
    const UnifiedValue contentTplVal = GetValue(PropId_ContentTemplate);
    const AxbPackage* itemTplPkg = nullptr;
    const AxbPackage* contentTplPkg = nullptr;
    if (const auto* sv = std::get_if<StringValue>(&itemTplVal); sv && !sv->view().empty()) {
        itemTplPkg = AxbTemplateRegistry::Instance().FindDataTemplate(sv->view());
    }
    if (const auto* sv = std::get_if<StringValue>(&contentTplVal); sv && !sv->view().empty()) {
        contentTplPkg = AxbTemplateRegistry::Instance().FindDataTemplate(sv->view());
    }
    ObjectRef itemObj{};
    if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
        itemObj = *ref;
    }
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    StyleTriggerEngine* effStyle = EffectiveStyleEngine();
    AnimationEngine* effAnim = EffectiveAnimationEngine();
    NativeHandleRegistry* reg = effDomains ? &effDomains->nativeHandles() : nullptr;
    ManagedHeap* heap = effDomains ? &effDomains->uiHeap() : nullptr;

    auto resolveTabDisplayStr = [&]() -> std::string {
        std::string hdrStr = rs_rt::valueToString(itemVal);
        if (itemObj.valid() && effDomains && effBind && effBind->Program()) {
            if (const auto* oh = effDomains->heapForObject(itemObj)) {
                const SymbolId tid = oh->objectTypeId(itemObj).value_or(0);
                for (const char* cand : {"Header", "Name", "Title", "Label", "Text"}) {
                    if (auto slot = effDomains->resolvePropertySlot(*effBind->Program(), tid, cand)) {
                        if (auto fv = oh->fieldGet(itemObj, slot->backingFieldIndex)) {
                            hdrStr = FormatBindingString({}, &(*fv), 1u, effDomains);
                            break;
                        }
                    }
                }
            }
        }
        return hdrStr;
    };
    auto isTabItemRootTemplate = [](const AxbPackage* pkg) noexcept -> bool {
        if (!pkg) {
            return false;
        }
        if (pkg->rootTypeName == "TabItem") {
            return true;
        }
        int depth = 0;
        for (const auto& inst : pkg->instructions) {
            if (inst.opcode == AxbOpcode::BeginElement) {
                ++depth;
            } else if (inst.opcode == AxbOpcode::EndElement) {
                --depth;
            } else if (depth == 1 &&
                       (inst.opcode == AxbOpcode::SetStaticProperty ||
                        inst.opcode == AxbOpcode::BindProperty) &&
                       (inst.propId == PropId_Header || inst.propId == PropId_Title)) {
                return true;
            }
        }
        return false;
    };

    if (itemTplPkg && (contentTplPkg || !isTabItemRootTemplate(itemTplPkg))) {
        auto hdrRoot = !itemTplPkg->rootTypeName.empty()
            ? CreateElementByTypeName(itemTplPkg->rootTypeName)
            : MakeIntrusive<Border>();
        if (hdrRoot) {
            hdrRoot->SetNamescopeBoundary(true);
            hdrRoot->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
            tab->AddChild(hdrRoot.Get(), reg, heap);
            tab->SetHeaderElement(hdrRoot.Get());
            (void)MountAxbTemplate(
                hdrRoot.Get(),
                *itemTplPkg,
                effDomains,
                effBind,
                itemObj,
                nullptr,
                nullptr,
                effAnim);
        }
        if (contentTplPkg) {
            auto bodyRoot = !contentTplPkg->rootTypeName.empty()
                ? CreateElementByTypeName(contentTplPkg->rootTypeName)
                : MakeIntrusive<Border>();
            if (bodyRoot) {
                bodyRoot->SetNamescopeBoundary(true);
                bodyRoot->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
                tab->AddChild(bodyRoot.Get(), reg, heap);
                (void)MountAxbTemplate(
                    bodyRoot.Get(),
                    *contentTplPkg,
                    effDomains,
                    effBind,
                    itemObj,
                    nullptr,
                    nullptr,
                    effAnim);
            }
        } else {
            auto bodyTb = MakeIntrusive<TextBlock>();
            bodyTb->SetText(resolveTabDisplayStr());
            tab->AddChild(bodyTb.Get(), reg, heap);
        }
        if (effStyle) {
            effStyle->RegisterPackageStyles(*itemTplPkg);
            if (contentTplPkg) {
                effStyle->RegisterPackageStyles(*contentTplPkg);
            }
            (void)effStyle->BindSubtreeStyles(tab.Get());
            (void)effStyle->EvaluateSubtree(tab.Get());
        }
    } else if (const AxbPackage* tplPkg = itemTplPkg ? itemTplPkg : contentTplPkg) {
        tab->SetNamescopeBoundary(true);
        (void)MountAxbTemplate(
            tab.Get(),
            *tplPkg,
            effDomains,
            effBind,
            itemObj,
            nullptr,
            nullptr,
            effAnim);
        if (contentTplPkg && !itemTplPkg && tab->GetHeader().empty()) {
            tab->SetHeader(resolveTabDisplayStr());
        }
        if (effStyle) {
            effStyle->RegisterPackageStyles(*tplPkg);
            (void)effStyle->BindSubtreeStyles(tab.Get());
            (void)effStyle->EvaluateSubtree(tab.Get());
        }
    } else {
        tab->SetHeader(resolveTabDisplayStr());
    }
    return tab;
}

void TabControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    items_.clear();
    items_.reserve(items.size());
    for (const auto& item : items) {
        items_.push_back(NormalizeTabItemValue(item));
    }
    while (UIElement* ch = FirstChild()) {
        ReleaseTabChildResources(ch);
        RemoveChild(ch, registry, uiHeap);
    }
    for (const auto& item : items_) {
        IntrusivePtr<UIElement> tab = InstantiateTabItemForValue(item);
        if (tab) {
            AddChild(tab.Get(), registry, uiHeap);
        }
    }
    if (items_.empty()) {
        SetSelectedIndex(-1);
    } else {
        const std::int32_t rawSel = detail::CoerceInt32Property(GetValue(PropId_SelectedIndex), 0);
        if (rawSel >= static_cast<std::int32_t>(items_.size())) {
            SetSelectedIndex(static_cast<std::int32_t>(items_.size() - 1u));
        } else if (rawSel < 0) {
            SetSelectedIndex(0);
        }
    }
    SyncTabSelectionStates();
    InvalidateLayout();
}

bool TabControl::ApplyIncrementalChanges(
    const CollectionChangeRecord* changes,
    std::size_t changeCount) {
    if (!changes || changeCount == 0u) {
        return false;
    }
    for (std::size_t c = 0u; c < changeCount; ++c) {
        if (changes[c].kind == CollectionChangeKind::Reset) {
            return false;
        }
    }
    ExecutionDomainContext* effDomains = EffectiveDomains();
    DualTrackBindingEngine* effBind = EffectiveBindingEngine();
    NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;
    std::int32_t selIdx = detail::CoerceInt32Property(GetValue(PropId_SelectedIndex), -1);

    for (std::size_t c = 0u; c < changeCount; ++c) {
        const auto& rec = changes[c];
        switch (rec.kind) {
            case CollectionChangeKind::Add: {
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_.push_back(norm);
                if (auto tab = InstantiateTabItemForValue(norm)) {
                    AddChild(tab.Get(), registry, uiHeap);
                }
                break;
            }
            case CollectionChangeKind::Insert: {
                const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), norm);
                if (auto tab = InstantiateTabItemForValue(norm)) {
                    InsertChildAt(tab.Get(), idx, registry, uiHeap);
                }
                if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                    ++selIdx;
                }
                break;
            }
            case CollectionChangeKind::RemoveAt: {
                if (rec.index >= items_.size()) {
                    break;
                }
                const std::size_t idx = rec.index;
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                UIElement* targetChild = FirstChild();
                for (std::size_t k = 0u; k < idx && targetChild != nullptr; ++k) {
                    targetChild = targetChild->NextSibling();
                }
                if (targetChild) {
                    ReleaseTabChildResources(targetChild);
                    RemoveChild(targetChild, registry, uiHeap);
                }
                if (selIdx >= 0) {
                    if (idx < static_cast<std::size_t>(selIdx)) {
                        --selIdx;
                    } else if (idx == static_cast<std::size_t>(selIdx)) {
                        selIdx = items_.empty()
                            ? -1
                            : std::min<std::int32_t>(selIdx, static_cast<std::int32_t>(items_.size() - 1u));
                    }
                }
                break;
            }
            case CollectionChangeKind::Replace: {
                if (rec.index >= items_.size()) {
                    break;
                }
                const std::size_t idx = rec.index;
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_[idx] = norm;
                UIElement* targetChild = FirstChild();
                for (std::size_t k = 0u; k < idx && targetChild != nullptr; ++k) {
                    targetChild = targetChild->NextSibling();
                }
                if (targetChild) {
                    targetChild->SetValue(PropId_DataContext, norm, ValueSourceRank::Local);
                    if (effBind) {
                        ObjectRef itemObj{};
                        if (const auto* ref = std::get_if<ObjectRef>(&norm)) {
                            itemObj = *ref;
                        }
                        (void)effBind->RebindSubtreeRootObject(targetChild, itemObj);
                    }
                    if (auto* ti = targetChild->DynamicCast<TabItem>()) {
                        if (ti->ChildCount() == 0u) {
                            ti->SetHeader(rs_rt::valueToString(norm));
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Reset:
                return false;
            case CollectionChangeKind::Clear:
                items_.clear();
                while (UIElement* ch = FirstChild()) {
                    ReleaseTabChildResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
                selIdx = -1;
                break;
        }
    }

    SetSelectedIndex(selIdx);
    SyncTabSelectionStates();
    InvalidateLayout();
    detail::MarkSubtreeRenderDirtyRecursive(this);
    return true;
}

void TabControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    if (propId == PropId_ItemsSource) {
        Control::OnPropertyChanged(propId, newValue);
        ExecutionDomainContext* effDomains = EffectiveDomains();
        DualTrackBindingEngine* effBind = EffectiveBindingEngine();
        AnimationEngine* effAnim = EffectiveAnimationEngine();
        NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;
        if (const auto* objRef = std::get_if<ObjectRef>(&newValue)) {
            if (objRef->valid() && effDomains) {
                if (auto* heap = effDomains->heapForObject(*objRef)) {
                    const ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(*heap, *objRef);
                    if (arrRef.valid()) {
                        const std::size_t len = heap->arrayLength(arrRef).value_or(0u);
                        std::vector<UnifiedValue> extracted;
                        extracted.reserve(len);
                        for (std::size_t i = 0u; i < len; ++i) {
                            extracted.push_back(heap->arrayGet(arrRef, i).value_or(UnifiedValue{}));
                        }
                        SetItemsSource(extracted, registry, uiHeap);
                        retainedVisualResourceHolder_.Reset();
                        return;
                    }
                }
            }
        } else if (const auto* svRes = std::get_if<StringValue>(&newValue);
                   svRes && svRes->view().rfind("__aero_visual_res__:", 0) == 0) {
            if (const AxbPackage* vPkg = AxbTemplateRegistry::Instance().FindDataTemplate(svRes->view())) {
                auto holder = CreateElementByTypeName(
                    !vPkg->rootTypeName.empty() ? vPkg->rootTypeName : "StackPanel");
                if (holder) {
                    (void)MountAxbTemplate(
                        holder.Get(),
                        *vPkg,
                        effDomains,
                        effBind,
                        ObjectRef{},
                        nullptr,
                        nullptr,
                        effAnim);
                    NativeHandleRegistry& regRef = registry ? *registry : FallbackVisualHandleRegistry();
                    std::vector<UnifiedValue> extracted;
                    for (UIElement* c = holder->FirstChild(); c != nullptr; c = c->NextSibling()) {
                        extracted.push_back(UnifiedValue{c->ToNativeHandle(regRef)});
                    }
                    SetItemsSource(extracted, registry, uiHeap);
                    retainedVisualResourceHolder_ = std::move(holder);
                    return;
                }
            }
        } else if (std::holds_alternative<std::monostate>(newValue) ||
                   std::holds_alternative<NullObject>(newValue) ||
                   std::holds_alternative<NullArray>(newValue)) {
            SetItemsSource({}, registry, uiHeap);
            retainedVisualResourceHolder_.Reset();
            return;
        }
        return;
    }
    if (propId == PropId_ItemTemplate || propId == PropId_ContentTemplate) {
        Control::OnPropertyChanged(propId, newValue);
        if (!items_.empty()) {
            ExecutionDomainContext* effDomains = EffectiveDomains();
            NativeHandleRegistry* registry = effDomains ? &effDomains->nativeHandles() : nullptr;
            ManagedHeap* uiHeap = effDomains ? &effDomains->uiHeap() : nullptr;
            std::vector<UnifiedValue> copyItems = items_;
            SetItemsSource(copyItems, registry, uiHeap);
        }
        return;
    }
    if (propId == PropId_IsVirtualizing || propId == PropId_ItemsPanel ||
        propId == PropId_ItemContainerStyle || propId == PropId_DisplayMemberPath ||
        propId == PropId_GridViewColumnsSpec) {
        Control::OnPropertyChanged(propId, newValue);
        return;
    }

    Selector::OnPropertyChanged(propId, newValue);
    if (propId == PropId_SelectedIndex || propId == PropId_TabStripPlacement) {
        SyncTabSelectionStates();
        InvalidateLayout();
        detail::MarkSubtreeRenderDirtyRecursive(this);
    }
}

bool TabControl::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::int32_t hitTab = HitTestTabHeaderIndex(localPoint);
    if (hitTab >= 0) {
        return SelectTabInteractive(hitTab);
    }
    return false;
}

bool TabControl::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TabCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t cur = std::max<std::int32_t>(0, GetSelectedIndex());
    if (key == KeyCode::Tab && (modifiers & KeyMod_Ctrl) != 0u) {
        const std::int32_t delta = ((modifiers & KeyMod_Shift) != 0u) ? -1 : 1;
        const std::int32_t next = static_cast<std::int32_t>(
            (cur + delta + static_cast<std::int32_t>(count)) % static_cast<std::int32_t>(count));
        (void)SelectTabInteractive(next);
        return true;
    }
    if (key == KeyCode::Home) {
        (void)SelectTabInteractive(0);
        return true;
    }
    if (key == KeyCode::End) {
        (void)SelectTabInteractive(static_cast<std::int32_t>(count - 1u));
        return true;
    }
    if (key == KeyCode::Right || key == KeyCode::Down) {
        const std::int32_t next = std::min<std::int32_t>(
            static_cast<std::int32_t>(count - 1u),
            cur + 1);
        return SelectTabInteractive(next);
    }
    if (key == KeyCode::Left || key == KeyCode::Up) {
        const std::int32_t prev = std::max<std::int32_t>(0, cur - 1);
        return SelectTabInteractive(prev);
    }
    return false;
}

Size TabControl::MeasureCore(Size availableSize) noexcept {
    SyncTabSelectionStates();
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    const TabStripPlacement placement = GetTabStripPlacement();
    const bool isVerticalStrip =
        (placement == TabStripPlacement::Left || placement == TabStripPlacement::Right);

    float stripW = 0.0f;
    float stripH = isVerticalStrip ? 0.0f : kDefaultTabStripHeight;
    const std::size_t count = TabCount();
    for (std::size_t i = 0u; i < count; ++i) {
        UIElement* hdrElem = GetTabItemHeaderElement(GetTabElement(i));
        if (hdrElem) {
            hdrElem->Measure(Size{innerAvailW, kDefaultTabStripHeight});
            if (isVerticalStrip) {
                stripW = std::max(stripW, std::max(88.0f, hdrElem->DesiredSize().width + 24.0f));
                stripH += std::max(30.0f, hdrElem->DesiredSize().height + 10.0f);
            } else {
                stripW += std::max(64.0f, hdrElem->DesiredSize().width + 20.0f);
                stripH = std::max(stripH, hdrElem->DesiredSize().height + 8.0f);
            }
        } else {
            const Size ts = MeasureUtf8TextRun(GetTabHeaderText(i), fontSize);
            if (isVerticalStrip) {
                stripW = std::max(stripW, std::max(88.0f, ts.width + 24.0f));
                stripH += std::max(30.0f, ts.height + 12.0f);
            } else {
                stripW += std::max(64.0f, ts.width + 24.0f);
                stripH = std::max(stripH, ts.height + 12.0f);
            }
        }
    }
    if (count == 0u) {
        stripW = 0.0f;
        stripH = 0.0f;
    }

    const std::int32_t activeIdx = GetSelectedIndex();
    const Size pageAvail = isVerticalStrip
        ? Size{std::max(0.0f, innerAvailW - stripW - 8.0f), std::max(0.0f, innerAvailH - 8.0f)}
        : Size{std::max(0.0f, innerAvailW - 8.0f), std::max(0.0f, innerAvailH - stripH - 8.0f)};

    float activePageW = 0.0f;
    float activePageH = 0.0f;
    std::int32_t visIdx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Measure(Size{0.0f, 0.0f});
            continue;
        }
        if (visIdx == activeIdx) {
            c->Measure(pageAvail);
            activePageW = c->DesiredSize().width + 8.0f;
            activePageH = c->DesiredSize().height + 8.0f;
        } else {
            c->Measure(Size{0.0f, 0.0f});
        }
        ++visIdx;
    }

    if (isVerticalStrip) {
        return Size{
            stripW + activePageW + padW,
            std::max(stripH, activePageH) + padH};
    }
    return Size{
        std::max(stripW, activePageW) + padW,
        stripH + activePageH + padH};
}

void TabControl::ArrangeCore(Rect finalRect) noexcept {
    SyncTabSelectionStates();
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const std::size_t count = TabCount();
    tabHeaderRects_.clear();
    tabHeaderRects_.reserve(count);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    const TabStripPlacement placement = GetTabStripPlacement();
    const bool isVerticalStrip =
        (placement == TabStripPlacement::Left || placement == TabStripPlacement::Right);

    if (isVerticalStrip) {
        float stripW = (count > 0u) ? std::min(innerW, 88.0f) : 0.0f;
        std::vector<float> desiredTabHeights(count, 30.0f);
        float totalDesiredTabH = 0.0f;
        for (std::size_t i = 0u; i < count; ++i) {
            UIElement* hdrElem = GetTabItemHeaderElement(GetTabElement(i));
            if (hdrElem) {
                stripW = std::min(innerW, std::max(stripW, hdrElem->DesiredSize().width + 24.0f));
                desiredTabHeights[i] = std::max(30.0f, hdrElem->DesiredSize().height + 10.0f);
            } else {
                const Size ts = MeasureUtf8TextRun(GetTabHeaderText(i), fontSize);
                stripW = std::min(innerW, std::max(stripW, ts.width + 24.0f));
                desiredTabHeights[i] = std::max(30.0f, ts.height + 12.0f);
            }
            totalDesiredTabH += desiredTabHeights[i];
        }
        const float stripX = (placement == TabStripPlacement::Right)
            ? (startX + std::max(0.0f, innerW - stripW))
            : startX;
        tabStripLocalRect_ = Rect{stripX, startY, stripW, innerH};

        const float heightScale = (totalDesiredTabH > innerH && totalDesiredTabH > 1e-4f)
            ? (innerH / totalDesiredTabH)
            : 1.0f;
        float cursorY = startY;
        for (std::size_t i = 0u; i < count; ++i) {
            const float th = desiredTabHeights[i] * heightScale;
            const Rect tabRect{stripX, cursorY, stripW, th};
            tabHeaderRects_.push_back(tabRect);
            cursorY += th;
            ArrangeTabItemHeaderElement(GetTabElement(i), tabRect);
        }
        const float pageX = (placement == TabStripPlacement::Right) ? startX : (startX + stripW);
        const float pageW = std::max(0.0f, innerW - stripW);
        contentPanelLocalRect_ = Rect{pageX, startY, pageW, innerH};
    } else {
        float stripH = (count > 0u) ? std::min(innerH, kDefaultTabStripHeight) : 0.0f;
        std::vector<float> desiredTabWidths(count, 64.0f);
        float totalDesiredTabW = 0.0f;
        for (std::size_t i = 0u; i < count; ++i) {
            UIElement* hdrElem = GetTabItemHeaderElement(GetTabElement(i));
            if (hdrElem) {
                desiredTabWidths[i] = std::max(64.0f, hdrElem->DesiredSize().width + 20.0f);
                stripH = std::min(innerH, std::max(stripH, hdrElem->DesiredSize().height + 8.0f));
            } else {
                const Size ts = MeasureUtf8TextRun(GetTabHeaderText(i), fontSize);
                desiredTabWidths[i] = std::max(64.0f, ts.width + 24.0f);
            }
            totalDesiredTabW += desiredTabWidths[i];
        }

        const float stripY = (placement == TabStripPlacement::Bottom)
            ? (startY + std::max(0.0f, innerH - stripH))
            : startY;
        tabStripLocalRect_ = Rect{startX, stripY, innerW, stripH};

        const float widthScale = (totalDesiredTabW > innerW && totalDesiredTabW > 1e-4f)
            ? (innerW / totalDesiredTabW)
            : 1.0f;
        float cursorX = startX;
        for (std::size_t i = 0u; i < count; ++i) {
            const float tw = desiredTabWidths[i] * widthScale;
            const Rect tabRect{cursorX, stripY, tw, stripH};
            tabHeaderRects_.push_back(tabRect);
            cursorX += tw;
            ArrangeTabItemHeaderElement(GetTabElement(i), tabRect);
        }

        const float pageY = (placement == TabStripPlacement::Bottom) ? startY : (startY + stripH);
        const float pageH = std::max(0.0f, innerH - stripH);
        contentPanelLocalRect_ = Rect{startX, pageY, innerW, pageH};
    }

    const Rect innerPageSlot{
        contentPanelLocalRect_.x + 4.0f,
        contentPanelLocalRect_.y + 4.0f,
        std::max(0.0f, contentPanelLocalRect_.width - 8.0f),
        std::max(0.0f, contentPanelLocalRect_.height - 8.0f)};

    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t visIdx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{contentPanelLocalRect_.x, contentPanelLocalRect_.y, 0.0f, 0.0f});
            continue;
        }
        if (visIdx == activeIdx) {
            c->Arrange(innerPageSlot);
        } else {
            c->Arrange(Rect{contentPanelLocalRect_.x, contentPanelLocalRect_.y, 0.0f, 0.0f});
        }
        ++visIdx;
    }
}

bool ItemsControl::MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept {
    if (targetType == "TreeView" ||
        targetType == "ItemsPresenter") {
        return true;
    }
    return Control::MatchesBindingAncestorLegacyFallback(targetType);
}

bool ListBox::MatchesXamlAncestorTypeAlias(std::string_view wantedType) const noexcept {
    if (wantedType == "ListView") {
        return true;
    }
    return Selector::MatchesXamlAncestorTypeAlias(wantedType);
}

bool ListBox::MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept {
    if (targetType == "ListView") {
        return true;
    }
    return Selector::MatchesBindingAncestorLegacyFallback(targetType);
}

bool ListBox::TryExecuteSelectContainerAction(UIElement* originTarget) {
    std::size_t childOffset = 0u;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == TemplateRoot()) {
            continue;
        }
        bool isMatch = false;
        for (const UIElement* w = originTarget; w != nullptr;
             w = w->Parent() ? w->Parent() : w->TemplatedParent()) {
            if (w == c) {
                isMatch = true;
                break;
            }
        }
        if (isMatch) {
            const std::size_t logicalIdx = IsVirtualizing()
                ? (FirstRealizedItemIndex() + childOffset)
                : childOffset;
            (void)SelectIndexInteractive(static_cast<std::int32_t>(logicalIdx));
            return true;
        }
        ++childOffset;
    }
    return true;
}

void ComboBox::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "combobox";
    ioInfo.value = GetSelectedItemText();
}

bool TabItem::TryExecuteSelectContainerAction(UIElement* originTarget) {
    (void)originTarget;
    for (UIElement* p = Parent(); p != nullptr; p = p->Parent()) {
        if (auto* tc = p->DynamicCast<TabControl>()) {
            const std::size_t count = tc->TabCount();
            for (std::size_t idx = 0u; idx < count; ++idx) {
                if (tc->GetTabElement(idx) == this) {
                    (void)tc->SelectTabInteractive(static_cast<std::int32_t>(idx));
                    return true;
                }
            }
            break;
        }
    }
    SetIsSelected(true);
    return true;
}

void MenuItem::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "menuitem";
    ioInfo.value = GetContentText();
    if (ioInfo.value.empty()) {
        ioInfo.value = GetHeader();
    }
}

bool TreeViewItem::TryExecuteSelectContainerAction(UIElement* originTarget) {
    (void)originTarget;
    if (TreeView* tv = FindAncestorTreeView()) {
        tv->SelectContainer(this);
    } else {
        SetIsSelected(true);
    }
    return true;
}

} // namespace aero
