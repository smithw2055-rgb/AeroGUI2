#include "aero/core/UIElement.hpp"

#include <algorithm>
#include <cmath>

#include "realscript/aot_cpp/AotRuntime.h"

namespace aero {

namespace {

TypeClass gUIElementTypeClass{};
TypeClass gPanelTypeClass{};
TypeClass gWindowTypeClass{};
TypeClass gButtonTypeClass{};
bool gBuiltinTypesInitialized = false;

void DetachSubtreeRenderNodes(UIElement* elem) noexcept {
    if (!elem) {
        return;
    }
    elem->DetachRenderNodeSpan();
    for (UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        DetachSubtreeRenderNodes(c);
    }
}

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
    DetachRenderNodeSpan();

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
    DetachSubtreeRenderNodes(child);
    child->InvalidateRender();
    InvalidateLayout();
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
    DetachSubtreeRenderNodes(child);
    // Detach parent's render tree binding marker to signal structural removal to PagedRenderTree
    DetachRenderNodeSpan();
    InvalidateLayout();

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

namespace {

[[nodiscard]] constexpr bool IsLayoutAffectingSparseProperty(std::uint16_t propId) noexcept {
    switch (propId) {
        case PropId_BorderThickness:
        case PropId_FontSize:
        case PropId_Title:
        case PropId_Text:
        case PropId_Content:
        case PropId_Placeholder:
        case PropId_ItemsSource:
        case PropId_GridRow:
        case PropId_GridColumn:
        case PropId_GridRowSpan:
        case PropId_GridColumnSpan:
        case PropId_CanvasLeft:
        case PropId_CanvasTop:
        case PropId_Orientation:
        case PropId_Spacing:
        case PropId_PathData:
        case PropId_ImageSource:
        case PropId_Stretch:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool IsParentLayoutAttachedProperty(std::uint16_t propId) noexcept {
    switch (propId) {
        case PropId_GridRow:
        case PropId_GridColumn:
        case PropId_GridRowSpan:
        case PropId_GridColumnSpan:
        case PropId_CanvasLeft:
        case PropId_CanvasTop:
            return true;
        default:
            return false;
    }
}

} // namespace

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

UIElement* UIElement::HitTest(Point point) noexcept {
    if (GetVisibility() != Visibility::Visible || GetOpacity() <= 0.0f) {
        return nullptr;
    }

    // Apply inverse 2D RenderTransformMatrix and 3D projection transform around element center
    // so child subtree hit-testing operates in this container's local coordinate space.
    Point localPt = point;
    const UnifiedValue rtmVal = GetValue(PropId_RenderTransformMatrix);
    if (const auto* f4 = std::get_if<Float4Value>(&rtmVal)) {
        const float safeSx = (std::abs(f4->x) > 1e-4f) ? f4->x : (f4->x >= 0.0f ? 1e-4f : -1e-4f);
        const float safeSy = (std::abs(f4->y) > 1e-4f) ? f4->y : (f4->y >= 0.0f ? 1e-4f : -1e-4f);
        localPt.x = (localPt.x - f4->z) / safeSx;
        localPt.y = (localPt.y - f4->w) / safeSy;
    }

    const Rect r = finalRect_;
    const float cx = r.x + r.width * 0.5f;
    const float cy = r.y + r.height * 0.5f;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    if (auto yv = value::TryGetFloat(GetValue(PropId_Projection3DYaw))) {
        yawDeg = *yv;
    }
    if (auto pv = value::TryGetFloat(GetValue(PropId_Projection3DPitch))) {
        pitchDeg = *pv;
    }
    if (yawDeg != 0.0f || pitchDeg != 0.0f) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float cosYaw = std::cos(yawDeg * kDegToRad);
        const float cosPitch = std::cos(pitchDeg * kDegToRad);
        const float safeCosYaw = (std::abs(cosYaw) > 0.05f) ? cosYaw : (cosYaw >= 0.0f ? 0.05f : -0.05f);
        const float safeCosPitch = (std::abs(cosPitch) > 0.05f) ? cosPitch : (cosPitch >= 0.0f ? 0.05f : -0.05f);
        localPt.x = cx + (localPt.x - cx) / safeCosYaw;
        localPt.y = cy + (localPt.y - cy) / safeCosPitch;
    }

    // Optional ClipToBounds check before testing children
    const UnifiedValue clipVal = GetValue(PropId_ClipToBounds);
    if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
        if (r.width <= 0.0f || r.height <= 0.0f ||
            localPt.x < r.x || localPt.x > r.x + r.width ||
            localPt.y < r.y || localPt.y > r.y + r.height) {
            return nullptr;
        }
    }

    // Traverse children in reverse Z-order (lastChild_ -> firstChild_)
    for (UIElement* child = lastChild_; child != nullptr; child = child->prevSibling_) {
        if (UIElement* hit = child->HitTest(localPt)) {
            return hit;
        }
    }

    if (r.width > 0.0f && r.height > 0.0f &&
        localPt.x >= r.x && localPt.x <= r.x + r.width &&
        localPt.y >= r.y && localPt.y <= r.y + r.height) {
        return this;
    }
    return nullptr;
}

void UIElement::Measure(Size availableSize) noexcept {
    if (GetVisibility() == Visibility::Collapsed) {
        desiredSize_ = {0.0f, 0.0f};
        lastMeasureConstraint_ = availableSize;
        ClearLayoutDirtyFlags(MeasureDirty | SubtreeMeasureDirty);
        return;
    }

    if ((fastProps_.layoutDirtyFlags & (MeasureDirty | SubtreeMeasureDirty)) == 0u &&
        lastMeasureConstraint_ == availableSize) {
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
    lastMeasureConstraint_ = availableSize;
    ClearLayoutDirtyFlags(MeasureDirty | SubtreeMeasureDirty);
}

void UIElement::Arrange(Rect finalRect) noexcept {
    if (GetVisibility() == Visibility::Collapsed) {
        const Rect collapsedRect{finalRect.x, finalRect.y, 0.0f, 0.0f};
        if (finalRect_ != collapsedRect) {
            finalRect_ = collapsedRect;
            InvalidateRender();
        }
        ClearLayoutDirtyFlags(ArrangeDirty | SubtreeArrangeDirty);
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

    if (finalRect_ != innerRect) {
        finalRect_ = innerRect;
        InvalidateRender();
    }
    ArrangeCore(innerRect);
    ClearLayoutDirtyFlags(ArrangeDirty | SubtreeArrangeDirty);
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
        InvalidateRender();
        if (IsLayoutAffectingSparseProperty(propId)) {
            InvalidateLayout();
            if (IsParentLayoutAttachedProperty(propId) && parent_) {
                parent_->InvalidateLayout();
            }
        }
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
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    }
    OnPropertyChanged(propId, value);
    return true;
}

bool UIElement::ClearValue(std::uint16_t propId, ValueSourceRank rank) {
    switch (propId) {
    case PropId_Width:
        SetWidth(-1.0f);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Height:
        SetHeight(-1.0f);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_MinWidth:
        SetMinWidth(0.0f);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_MinHeight:
        SetMinHeight(0.0f);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Margin:
        SetMargin({});
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Padding:
        SetPadding({});
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Opacity:
        SetOpacity(1.0f);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Visibility:
        SetVisibility(Visibility::Visible);
        OnPropertyChanged(propId, std::monostate{});
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
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    }
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
    if (scriptTargetObject_.valid() && scriptClickMethodId_ != 0) {
        std::vector<UnifiedValue> args;
        args.push_back(scriptTargetObject_);
        const UnifiedValue cmdParam = GetValue(PropId_CommandParameter);
        if (!std::holds_alternative<std::monostate>(cmdParam)) {
            args.push_back(cmdParam);
        }
        if (scriptInterpreter_) {
            auto res = scriptInterpreter_->invoke(scriptClickMethodId_, args);
            // Only retry for arity mismatch if 0 bytecode instructions ran (entry signature check),
            // never re-entering a handler that already started executing and failed internally.
            if (!res.succeeded &&
                res.error.code == ErrorCode::InvalidArguments &&
                res.statistics.instructionsExecuted == 0u) {
                if (args.size() == 1u) {
                    args.push_back(value::FromInt(0));
                    res = scriptInterpreter_->invoke(scriptClickMethodId_, args);
                } else if (args.size() > 1u) {
                    args.resize(1u);
                    res = scriptInterpreter_->invoke(scriptClickMethodId_, args);
                }
            }
            if (!res.succeeded) {
                if (error) {
                    *error = res.error;
                }
                return false;
            }
        } else if (scriptAotContext_) {
            const auto& prog = scriptAotContext_->program();
            for (std::uint32_t i = 0; i < prog.functionCount; ++i) {
                const auto& fn = prog.functions[i];
                if (fn.symbolId == scriptClickMethodId_) {
                    if (args.size() < fn.parameterCount && fn.parameterTypes && fn.parameterTypeIds) {
                        while (args.size() < fn.parameterCount) {
                            const std::size_t pIdx = args.size();
                            args.push_back(scriptAotContext_->defaultValue(
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
            const bool ok = scriptAotContext_->invoke(
                scriptClickMethodId_,
                args.data(),
                args.size(),
                outVal);
            if (!ok) {
                if (error) {
                    *error = scriptAotContext_->error();
                }
                return false;
            }
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
