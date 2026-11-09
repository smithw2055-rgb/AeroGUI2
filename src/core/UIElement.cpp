#include "aero/core/UIElement.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

namespace {

TypeClass gUIElementTypeClass{};
TypeClass gPanelTypeClass{};
TypeClass gWindowTypeClass{};
TypeClass gButtonTypeClass{};
bool gBuiltinTypesInitialized = false;

} // namespace

void EnsureBuiltinTypesRegistered() noexcept {
    if (gBuiltinTypesInitialized) {
        return;
    }
    gBuiltinTypesInitialized = true;
    auto& reg = TypeRegistry::Instance();
    reg.RegisterType(gUIElementTypeClass, "Aero.UI.UIElement", nullptr);
    reg.RegisterType(gPanelTypeClass, "Aero.UI.Panel", &gUIElementTypeClass);
    reg.RegisterType(gWindowTypeClass, "Aero.UI.Window", &gPanelTypeClass);
    reg.RegisterType(gButtonTypeClass, "Aero.UI.Button", &gUIElementTypeClass);
}

const TypeClass* UIElement::StaticTypeClass() noexcept {
    EnsureBuiltinTypesRegistered();
    return &gUIElementTypeClass;
}

const TypeClass* Panel::StaticTypeClass() noexcept {
    EnsureBuiltinTypesRegistered();
    return &gPanelTypeClass;
}

const TypeClass* Window::StaticTypeClass() noexcept {
    EnsureBuiltinTypesRegistered();
    return &gWindowTypeClass;
}

const TypeClass* Button::StaticTypeClass() noexcept {
    EnsureBuiltinTypesRegistered();
    return &gButtonTypeClass;
}

UIElement::UIElement() {
    EnsureBuiltinTypesRegistered();
}

Panel::Panel() {
    EnsureBuiltinTypesRegistered();
}

Window::Window() {
    EnsureBuiltinTypesRegistered();
}

Button::Button() {
    EnsureBuiltinTypesRegistered();
}

UIElement::~UIElement() {
    // Detach children safely
    UIElement* child = firstChild_;
    while (child) {
        UIElement* next = child->nextSibling_;
        child->parent_ = nullptr;
        child->prevSibling_ = nullptr;
        child->nextSibling_ = nullptr;
        child->PropagateRootConnection(false, boundRegistry_, boundUiHeap_);
        child->Release();
        child = next;
    }
    firstChild_ = nullptr;
    lastChild_ = nullptr;

    pinnedWrapperRoot_.reset();

    // Generational invalidation in NativeHandleRegistry on C++ destruction
    if (boundRegistry_ && cachedNativeHandle_.valid()) {
        (void)boundRegistry_->release(cachedNativeHandle_);
        cachedNativeHandle_ = {};
        boundRegistry_ = nullptr;
    }
}

void UIElement::SetAsRootVisual(
    bool isRoot,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) noexcept {
    if (registry) boundRegistry_ = registry;
    if (uiHeap) boundUiHeap_ = uiHeap;
    const bool effectiveConnected = isRoot || (parent_ && parent_->isConnectedToRoot_);
    PropagateRootConnection(effectiveConnected, boundRegistry_, boundUiHeap_);
}

void UIElement::PropagateRootConnection(
    bool connected,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) noexcept {
    if (registry) boundRegistry_ = registry;
    if (uiHeap) boundUiHeap_ = uiHeap;
    isConnectedToRoot_ = connected;
    UpdateWrapperPinState(boundRegistry_, boundUiHeap_);
    for (UIElement* child = firstChild_; child != nullptr; child = child->nextSibling_) {
        child->PropagateRootConnection(connected, boundRegistry_, boundUiHeap_);
    }
}

void UIElement::UpdateWrapperPinState(
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) noexcept {
    if (registry) boundRegistry_ = registry;
    if (uiHeap) boundUiHeap_ = uiHeap;

    if (!isConnectedToRoot_) {
        pinnedWrapperRoot_.reset();
        return;
    }
    if (pinnedWrapperRoot_.valid()) {
        return;
    }
    if (!boundUiHeap_) {
        return;
    }
    if (boundRegistry_ && cachedNativeHandle_.valid()) {
        if (auto wrapperRef = boundRegistry_->cachedWrapper(
                cachedNativeHandle_, boundUiHeap_->heapId())) {
            if (boundUiHeap_->isAlive(*wrapperRef)) {
                pinnedWrapperRoot_ = boundUiHeap_->retain(*wrapperRef);
                return;
            }
        }
    }
    if (boundWrapperRef_.valid() && boundUiHeap_->isAlive(boundWrapperRef_)) {
        pinnedWrapperRoot_ = boundUiHeap_->retain(boundWrapperRef_);
    }
}

void UIElement::UpdateSubtreeDepth(std::uint16_t newDepth) noexcept {
    treeDepth_ = newDepth;
    const auto childDepth = static_cast<std::uint16_t>(newDepth + 1u);
    for (UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        c->UpdateSubtreeDepth(childDepth);
    }
}

bool UIElement::AddChild(
    UIElement* child,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    if (!child || child == this || child->parent_ == this) {
        return false;
    }
    // Prevent cycles
    for (const UIElement* p = this; p != nullptr; p = p->parent_) {
        if (p == child) {
            return false;
        }
    }
    if (child->parent_) {
        child->parent_->RemoveChild(child, registry, uiHeap);
    }

    child->AddRef();
    child->parent_ = this;
    child->prevSibling_ = lastChild_;
    child->nextSibling_ = nullptr;
    if (lastChild_) {
        lastChild_->nextSibling_ = child;
    } else {
        firstChild_ = child;
    }
    lastChild_ = child;

    child->UpdateSubtreeDepth(static_cast<std::uint16_t>(treeDepth_ + 1u));
    fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
    child->PropagateRootConnection(
        isConnectedToRoot_,
        registry ? registry : boundRegistry_,
        uiHeap ? uiHeap : boundUiHeap_);
    return true;
}

bool UIElement::RemoveChild(
    UIElement* child,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    if (!child || child->parent_ != this) {
        return false;
    }
    if (child->prevSibling_) {
        child->prevSibling_->nextSibling_ = child->nextSibling_;
    } else {
        firstChild_ = child->nextSibling_;
    }
    if (child->nextSibling_) {
        child->nextSibling_->prevSibling_ = child->prevSibling_;
    } else {
        lastChild_ = child->prevSibling_;
    }

    child->parent_ = nullptr;
    child->prevSibling_ = nullptr;
    child->nextSibling_ = nullptr;
    child->UpdateSubtreeDepth(0);
    fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);

    child->PropagateRootConnection(
        false,
        registry ? registry : boundRegistry_,
        uiHeap ? uiHeap : boundUiHeap_);
    child->Release();
    return true;
}

std::size_t UIElement::ChildCount() const noexcept {
    std::size_t count = 0;
    for (const UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        ++count;
    }
    return count;
}

NativeHandle UIElement::ToNativeHandle(NativeHandleRegistry& registry) {
    if (boundRegistry_ == &registry && cachedNativeHandle_.valid()) {
        if (registry.resolveRawFast(cachedNativeHandle_) == this) {
            return cachedNativeHandle_;
        }
    }
    boundRegistry_ = &registry;
    cachedNativeHandle_ = registry.createBorrowed(
        GetTypeClass()->rsSymbolId,
        this,
        GetTypeClass()->typeName ? GetTypeClass()->typeName : "UIElement");
    return cachedNativeHandle_;
}

UIElement* UIElement::FromNativeHandle(
    const NativeHandleRegistry& registry,
    NativeHandle handle) noexcept {
    void* raw = registry.resolveRawFast(handle);
    if (!raw) {
        return nullptr;
    }
    auto* elem = static_cast<UIElement*>(raw);
    if (handle.typeId != 0) {
        const auto* expectedType = TypeRegistry::Instance().FindBySymbolId(handle.typeId);
        if (expectedType && !elem->IsA(expectedType)) {
            return nullptr;
        }
    }
    return elem;
}

bool UIElement::BindScriptWrapper(
    NativeHandleRegistry& registry,
    ManagedHeap& uiHeap,
    ObjectRef wrapperRef) {
    if (!uiHeap.isAlive(wrapperRef)) {
        return false;
    }
    boundRegistry_ = &registry;
    boundUiHeap_ = &uiHeap;
    boundWrapperRef_ = wrapperRef;
    const auto handle = ToNativeHandle(registry);
    if (!registry.setCachedWrapper(handle, wrapperRef, uiHeap.heapId())) {
        return false;
    }
    UpdateWrapperPinState(&registry, &uiHeap);
    return true;
}

void UIElement::SyncVisualTreeGcRoots(
    NativeHandleRegistry& registry,
    ManagedHeap& uiHeap) noexcept {
    UpdateWrapperPinState(&registry, &uiHeap);
    for (UIElement* child = firstChild_; child != nullptr; child = child->nextSibling_) {
        child->SyncVisualTreeGcRoots(registry, uiHeap);
    }
}

UIElement* UIElement::FindName(std::string_view targetName) noexcept {
    if (!targetName.empty() && name_ == targetName) {
        return this;
    }
    for (UIElement* child = firstChild_; child != nullptr; child = child->nextSibling_) {
        if (UIElement* found = child->FindName(targetName)) {
            return found;
        }
    }
    return nullptr;
}

void UIElement::Measure(Size availableSize) noexcept {
    if (GetVisibility() == Visibility::Collapsed) {
        desiredSize_ = {0.0f, 0.0f};
        ClearLayoutDirtyFlags(MeasureDirty);
        return;
    }

    const Thickness m = GetMargin();
    const float marginW = m.left + m.right;
    const float marginH = m.top + m.bottom;

    Size innerAvail{
        std::max(0.0f, availableSize.width - marginW),
        std::max(0.0f, availableSize.height - marginH)};

    if (fastProps_.width >= 0.0f) {
        innerAvail.width = fastProps_.width;
    }
    if (fastProps_.height >= 0.0f) {
        innerAvail.height = fastProps_.height;
    }

    Size coreSize = MeasureCore(innerAvail);
    if (fastProps_.width >= 0.0f) {
        coreSize.width = fastProps_.width;
    }
    if (fastProps_.height >= 0.0f) {
        coreSize.height = fastProps_.height;
    }

    coreSize.width = std::max(coreSize.width, fastProps_.minWidth);
    coreSize.height = std::max(coreSize.height, fastProps_.minHeight);

    desiredSize_ = {
        coreSize.width + marginW,
        coreSize.height + marginH};
    ClearLayoutDirtyFlags(MeasureDirty);
}

void UIElement::Arrange(Rect finalRect) noexcept {
    if (GetVisibility() == Visibility::Collapsed) {
        finalRect_ = {finalRect.x, finalRect.y, 0.0f, 0.0f};
        ClearLayoutDirtyFlags(ArrangeDirty);
        return;
    }

    const Thickness m = GetMargin();
    Rect innerRect{
        finalRect.x + m.left,
        finalRect.y + m.top,
        std::max(0.0f, finalRect.width - (m.left + m.right)),
        std::max(0.0f, finalRect.height - (m.top + m.bottom))};

    if (fastProps_.width >= 0.0f) {
        innerRect.width = fastProps_.width;
    }
    if (fastProps_.height >= 0.0f) {
        innerRect.height = fastProps_.height;
    }

    finalRect_ = innerRect;
    ArrangeCore(innerRect);
    ClearLayoutDirtyFlags(ArrangeDirty);
}

Size UIElement::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    float maxW = 0.0f;
    float maxH = 0.0f;
    for (UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        c->Measure(childAvail);
        maxW = std::max(maxW, c->DesiredSize().width);
        maxH = std::max(maxH, c->DesiredSize().height);
    }
    return Size{maxW + padW, maxH + padH};
}

void UIElement::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const Rect childRect{
        finalRect.x + p.left,
        finalRect.y + p.top,
        std::max(0.0f, finalRect.width - (p.left + p.right)),
        std::max(0.0f, finalRect.height - (p.top + p.bottom))};
    for (UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        c->Arrange(childRect);
    }
}

void UIElement::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    (void)propId;
    (void)newValue;
}

UnifiedValue UIElement::GetValue(std::uint16_t propId) const {
    switch (propId) {
        case PropId_Width:
            return MakeFloat(fastProps_.width);
        case PropId_Height:
            return MakeFloat(fastProps_.height);
        case PropId_MinWidth:
            return MakeFloat(fastProps_.minWidth);
        case PropId_MinHeight:
            return MakeFloat(fastProps_.minHeight);
        case PropId_Margin:
            return GetMargin().ToFloat4();
        case PropId_Padding:
            return GetPadding().ToFloat4();
        case PropId_Opacity:
            return MakeFloat(fastProps_.opacity);
        case PropId_Visibility:
            return static_cast<std::int64_t>(fastProps_.visibility);
        default:
            break;
    }
    if (const auto* entry = FindCompactEntry(propId)) {
        return entry->effectiveValue;
    }
    return std::monostate{};
}

bool UIElement::SetValue(
    std::uint16_t propId,
    const UnifiedValue& value,
    ValueSourceRank rank) {
    float numericVal = 0.0f;
    switch (propId) {
        case PropId_Width:
            if (TryGetFloat(value, numericVal)) {
                SetWidth(numericVal);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Height:
            if (TryGetFloat(value, numericVal)) {
                SetHeight(numericVal);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_MinWidth:
            if (TryGetFloat(value, numericVal)) {
                SetMinWidth(numericVal);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_MinHeight:
            if (TryGetFloat(value, numericVal)) {
                SetMinHeight(numericVal);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Margin:
            if (auto m = value::TryGetThickness(value)) {
                SetMargin(*m);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Padding:
            if (auto p = value::TryGetThickness(value)) {
                SetPadding(*p);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Opacity:
            if (TryGetFloat(value, numericVal)) {
                SetOpacity(numericVal);
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Visibility:
            if (const auto* i = std::get_if<std::int64_t>(&value)) {
                if (*i >= 0 && *i <= 2) {
                    SetVisibility(static_cast<Visibility>(*i));
                    OnPropertyChanged(propId, value);
                    return true;
                }
            }
            return false;
        default:
            break;
    }

    const auto rawRank = static_cast<std::uint8_t>(rank);
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        // Respect ValueSourceRank precedence: lower rank cannot overwrite active higher rank
        if (rawRank < it->sourceRank) {
            return false;
        }
        it->sourceRank = rawRank;
        it->effectiveValue = value;
        fastProps_.layoutDirtyFlags |= RenderDirty;
        OnPropertyChanged(propId, value);
        return true;
    }

    CompactPropertyEntry entry{};
    entry.propId = propId;
    entry.sourceRank = rawRank;
    entry.flags = 0;
    entry.bindingSlotId = 0xFFFFu;
    entry.animationTrackId = 0xFFFFu;
    entry.effectiveValue = value;
    sparseProps_.insert(it, std::move(entry));
    fastProps_.layoutDirtyFlags |= RenderDirty;
    OnPropertyChanged(propId, value);
    return true;
}

bool UIElement::ClearValue(std::uint16_t propId, ValueSourceRank rank) {
    switch (propId) {
    case PropId_Width:
        SetWidth(-1.0f);
        return true;
    case PropId_Height:
        SetHeight(-1.0f);
        return true;
    case PropId_MinWidth:
        SetMinWidth(0.0f);
        return true;
    case PropId_MinHeight:
        SetMinHeight(0.0f);
        return true;
    case PropId_Margin:
        SetMargin({});
        return true;
    case PropId_Padding:
        SetPadding({});
        return true;
    case PropId_Opacity:
        SetOpacity(1.0f);
        return true;
    case PropId_Visibility:
        SetVisibility(Visibility::Visible);
        return true;
    default:
        break;
    }

    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it == sparseProps_.end() || it->propId != propId) {
        return false;
    }
    if (static_cast<std::uint8_t>(rank) < it->sourceRank) {
        return false;
    }
    sparseProps_.erase(it);
    fastProps_.layoutDirtyFlags |= RenderDirty;
    OnPropertyChanged(propId, std::monostate{});
    return true;
}

const CompactPropertyEntry* UIElement::FindCompactEntry(
    std::uint16_t propId) const noexcept {
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        return &(*it);
    }
    return nullptr;
}

bool UIElement::SetPropertyMetadata(
    std::uint16_t propId,
    std::uint16_t bindingSlotId,
    std::uint16_t animationTrackId) {
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        it->bindingSlotId = bindingSlotId;
        it->animationTrackId = animationTrackId;
        return true;
    }
    CompactPropertyEntry entry{};
    entry.propId = propId;
    entry.sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Default);
    entry.bindingSlotId = bindingSlotId;
    entry.animationTrackId = animationTrackId;
    sparseProps_.insert(it, std::move(entry));
    return true;
}

void Button::SetContentText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string Button::GetContentText() const {
    const auto v = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    const auto titleVal = GetValue(PropId_Title);
    if (const auto* s = std::get_if<StringValue>(&titleVal)) {
        return s->str();
    }
    return {};
}

bool Button::PerformClick(RuntimeError* error) {
    if (clickCallback_) {
        clickCallback_(*this);
    }
    if (scriptInterpreter_ && scriptTargetObject_.valid() && scriptClickMethodId_ != 0) {
        const auto res = scriptInterpreter_->invoke(
            scriptClickMethodId_,
            {scriptTargetObject_});
        if (!res.succeeded) {
            if (error) {
                *error = res.error;
            }
            return false;
        }
    }
    return true;
}

Size Button::MeasureCore(Size availableSize) noexcept {
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

} // namespace aero
