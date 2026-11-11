#include "aero/core/UIElement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_set>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

namespace aero {

namespace {

struct LiveTraceableUIElementRegistry {
    std::mutex mutex;
    std::unordered_set<const UIElement*> elements;
    bool markerRegistered = false;

    static LiveTraceableUIElementRegistry& Instance() noexcept {
        static LiveTraceableUIElementRegistry reg;
        return reg;
    }

    void Register(const UIElement* elem) {
        if (!elem) return;
        bool needHook = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            elements.insert(elem);
            if (!markerRegistered) {
                markerRegistered = true;
                needHook = true;
            }
        }
        if (needHook) {
            rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
                std::lock_guard<std::mutex> lock(mutex);
                for (const UIElement* e : elements) {
                    if (e) {
                        e->MarkSparseValuesInPools();
                    }
                }
            });
        }
    }

    void Unregister(const UIElement* elem) noexcept {
        if (!elem) return;
        std::lock_guard<std::mutex> lock(mutex);
        elements.erase(elem);
    }
};

TypeClass gUIElementTypeClass{};
TypeClass gPanelTypeClass{};
TypeClass gWindowTypeClass{};
TypeClass gButtonTypeClass{};
bool gBuiltinTypesInitialized = false;
std::atomic<UIElementDestroyHook> gElementDestroyHook{nullptr};
std::atomic<UIElementLocSourceHook> gElementLocSourceHook{nullptr};
std::atomic<std::uint64_t> gTotalConstructedUIElements{0};
std::atomic<std::uint64_t> gTotalDestroyedUIElements{0};

void DetachSubtreeRenderNodes(UIElement* elem) noexcept {
    if (!elem) {
        return;
    }
    elem->DetachRenderNodeSpan();
    for (UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        DetachSubtreeRenderNodes(c);
    }
}

[[nodiscard]] bool TryCoerceBoolValue(const UnifiedValue& val, bool& outBool) noexcept {
    if (const auto* b = std::get_if<bool>(&val)) {
        outBool = *b;
        return true;
    }
    if (const auto* i = std::get_if<std::int64_t>(&val)) {
        outBool = (*i != 0);
        return true;
    }
    if (const auto* sv = std::get_if<StringValue>(&val)) {
        const std::string_view s = sv->view();
        if (s == "true" || s == "True" || s == "TRUE" || s == "1") {
            outBool = true;
            return true;
        }
        if (s == "false" || s == "False" || s == "FALSE" || s == "0") {
            outBool = false;
            return true;
        }
    }
    return false;
}

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
        case PropId_RiveSource:
        case PropId_Stretch:
        case PropId_MaxWidth:
        case PropId_MaxHeight:
        case PropId_Dock:
        case PropId_LastChildFill:
        case PropId_ItemWidth:
        case PropId_ItemHeight:
        case PropId_StretchDirection:
        case PropId_HorizontalScrollBarVisibility:
        case PropId_VerticalScrollBarVisibility:
        case PropId_LayoutTransform:
        case PropId_IsVirtualizing:
        case PropId_IsPassword:
        case PropId_FontFamily:
        case PropId_TextWrapping:
        case PropId_Template:
        case PropId_HorizontalContentAlignment:
        case PropId_VerticalContentAlignment:
        case PropId_FlowDirection:
        case PropId_RadiusX:
        case PropId_RadiusY:
        case PropId_ShapePoints:
        case PropId_UniformRows:
        case PropId_UniformColumns:
        case PropId_FirstColumn:
        case PropId_IsExpanded:
        case PropId_Header:
        case PropId_StrokeThickness:
        case PropId_LineX1:
        case PropId_LineY1:
        case PropId_LineX2:
        case PropId_LineY2:
        case PropId_ExpandDirection:
        case PropId_TabStripPlacement:
        case PropId_ContentTemplate:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr std::uint16_t CompanionGradientPropId(std::uint16_t propId) noexcept {
    switch (propId) {
        case PropId_Background:
            return PropId_BackgroundGradientId;
        case PropId_Foreground:
            return PropId_ForegroundGradientId;
        case PropId_BorderBrush:
            return PropId_BorderGradientId;
        default:
            return 0u;
    }
}

[[nodiscard]] Color ComputeBrushRepresentativeColor(std::uint32_t brushId) noexcept {
    if (const GradientBrushDesc* brush = GradientBrushRegistry::Instance().Find(brushId)) {
        if (!brush->stops.empty()) {
            Color c = brush->stops.front().color;
            c.a = std::clamp(c.a * brush->opacity, 0.0f, 1.0f);
            return c;
        }
        return SampleGradientRampAtT(*brush, 0.5f);
    }
    return Color{1.0f, 1.0f, 1.0f, 1.0f};
}

[[nodiscard]] constexpr bool IsArrangeOnlySparseProperty(std::uint16_t propId) noexcept {
    switch (propId) {
        case PropId_HorizontalOffset:
        case PropId_VerticalOffset:
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
        case PropId_Dock:
        case PropId_MaxWidth:
        case PropId_MaxHeight:
        case PropId_LayoutTransform:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] Size MapAvailableSizeThroughLayoutTransform(Size slotAvail, const Matrix2D& lt) noexcept {
    const float a = std::fabs(lt.m00);
    const float b = std::fabs(lt.m01);
    const float c = std::fabs(lt.m10);
    const float d = std::fabs(lt.m11);
    const float det = std::fabs(lt.m00 * lt.m11 - lt.m01 * lt.m10);
    if (det <= 1e-6f) {
        return Size{0.0f, 0.0f};
    }
    if (b <= 1e-6f && c <= 1e-6f) {
        return Size{slotAvail.width / a, slotAvail.height / d};
    }
    if (a <= 1e-6f && d <= 1e-6f) {
        return Size{slotAvail.height / c, slotAvail.width / b};
    }
    const float maxW = std::min(
        (a > 1e-6f) ? (slotAvail.width / a) : 1e6f,
        (c > 1e-6f) ? (slotAvail.height / c) : 1e6f);
    const float maxH = std::min(
        (b > 1e-6f) ? (slotAvail.width / b) : 1e6f,
        (d > 1e-6f) ? (slotAvail.height / d) : 1e6f);
    const float candidateW = 0.5f * maxW;
    const float candidateH = std::min(
        maxH,
        std::min(
            (b > 1e-6f) ? std::max(0.0f, (slotAvail.width - a * candidateW) / b) : maxH,
            (d > 1e-6f) ? std::max(0.0f, (slotAvail.height - c * candidateW) / d) : maxH));
    return Size{candidateW, candidateH};
}

} // namespace

namespace {
constexpr std::uint16_t kEntryMeta_PinnedTemplateSlot = 0x0001u;
} // namespace

struct TemplateBindingLink {
    UIElement* target = nullptr;
    std::uint16_t sourcePropId = 0;
    std::uint16_t targetPropId = 0;
    ValueConverterKind converter = ValueConverterKind::None;
};

struct TemplatePartEntry {
    std::string name;
    UIElement* element = nullptr;
};

struct UIElement::MountedControlTemplateData {
    AxbControlTemplate definition{};
    IntrusivePtr<UIElement> templateRoot{};
    std::vector<IntrusivePtr<UIElement>> logicalChildren;
    std::vector<TemplateBindingLink> bindings;
    std::vector<TemplatePartEntry> namedParts;
    std::vector<ContentPresenter*> contentPresenters;
    std::vector<UIElement*> inheritedTextNodes;
};

void RegisterUIElementResourceHooks(
    UIElementDestroyHook onDestroyed,
    UIElementLocSourceHook onLocSourceChanged) noexcept {
    gElementDestroyHook.store(onDestroyed, std::memory_order_release);
    gElementLocSourceHook.store(onLocSourceChanged, std::memory_order_release);
}

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

std::uint64_t UIElement::TotalConstructedCount() noexcept {
    return gTotalConstructedUIElements.load(std::memory_order_relaxed);
}

std::uint64_t UIElement::TotalDestroyedCount() noexcept {
    return gTotalDestroyedUIElements.load(std::memory_order_relaxed);
}

std::uint64_t UIElement::LiveInstanceCount() noexcept {
    const std::uint64_t c = gTotalConstructedUIElements.load(std::memory_order_relaxed);
    const std::uint64_t d = gTotalDestroyedUIElements.load(std::memory_order_relaxed);
    return (c >= d) ? (c - d) : 0ULL;
}

UIElement::UIElement() {
    gTotalConstructedUIElements.fetch_add(1u, std::memory_order_relaxed);
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
    gTotalDestroyedUIElements.fetch_add(1u, std::memory_order_relaxed);
    if (const auto destroyFn = gElementDestroyHook.load(std::memory_order_acquire)) {
        destroyFn(this);
    }
    if (registeredInValuePoolSet_) {
        LiveTraceableUIElementRegistry::Instance().Unregister(this);
        registeredInValuePoolSet_ = false;
    }
    DetachRenderNodeSpan();
    boundBindingEngine_ = nullptr;
    mountedTemplate_.reset();

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

bool UIElement::InsertChildAt(
    UIElement* child,
    std::size_t index,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    if (!child || child == this) {
        return false;
    }
    for (const UIElement* p = this; p != nullptr; p = p->parent_) {
        if (p == child) {
            return false;
        }
    }
    if (child->parent_) {
        child->parent_->RemoveChild(child, registry, uiHeap);
    }

    UIElement* nextNode = firstChild_;
    std::size_t curIdx = 0u;
    while (nextNode != nullptr && curIdx < index) {
        nextNode = nextNode->nextSibling_;
        ++curIdx;
    }
    if (nextNode == nullptr) {
        return AddChild(child, registry, uiHeap);
    }

    child->AddRef();
    child->parent_ = this;
    UIElement* prevNode = nextNode->prevSibling_;
    child->prevSibling_ = prevNode;
    child->nextSibling_ = nextNode;
    nextNode->prevSibling_ = child;
    if (prevNode) {
        prevNode->nextSibling_ = child;
    } else {
        firstChild_ = child;
    }

    child->UpdateSubtreeDepth(static_cast<std::uint16_t>(treeDepth_ + 1u));
    DetachSubtreeRenderNodes(child);
    DetachRenderNodeSpan();
    child->InvalidateRender();
    InvalidateLayout();
    child->PropagateRootConnection(
        isConnectedToRoot_,
        registry ? registry : boundRegistry_,
        uiHeap ? uiHeap : boundUiHeap_);
    return true;
}

void UIElement::ReorderExistingChildren(
    UIElement* const* orderedChildren,
    std::size_t count) noexcept {
    if (!orderedChildren || count <= 1u) {
        return;
    }
    if (count != ChildCount()) {
        return;
    }
    bool alreadyOrdered = true;
    UIElement* scan = firstChild_;
    for (std::size_t i = 0u; i < count; ++i) {
        UIElement* elem = orderedChildren[i];
        if (!elem || elem->parent_ != this) {
            return;
        }
        if (scan != elem) {
            alreadyOrdered = false;
        }
        if (scan) {
            scan = scan->nextSibling_;
        }
    }
    if (alreadyOrdered) {
        return;
    }
    firstChild_ = orderedChildren[0];
    lastChild_ = orderedChildren[count - 1u];
    for (std::size_t i = 0u; i < count; ++i) {
        orderedChildren[i]->prevSibling_ = (i > 0u) ? orderedChildren[i - 1u] : nullptr;
        orderedChildren[i]->nextSibling_ = (i + 1u < count) ? orderedChildren[i + 1u] : nullptr;
    }
    DetachRenderNodeSpan();
    InvalidateRender();
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
    if (boundRegistry_ && cachedNativeHandle_.valid()) {
        (void)boundRegistry_->release(cachedNativeHandle_);
        cachedNativeHandle_ = {};
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
    pinnedWrapperRoot_.reset();
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

bool UIElement::CanReceiveFocus() const noexcept {
    if (!IsFocusable() || !IsEffectivelyEnabled()) {
        return false;
    }
    for (const UIElement* cur = this; cur != nullptr; cur = cur->parent_) {
        if (cur->GetVisibility() != Visibility::Visible || cur->GetOpacity() <= 0.0f) {
            return false;
        }
    }
    return true;
}

bool UIElement::NotifyTwoWayPropertyMutated(std::uint16_t propId, RuntimeError* error) {
    if (!boundBindingEngine_) {
        return false;
    }
    return boundBindingEngine_->PushTwoWayToSource(this, propId, error);
}

Matrix2D UIElement::ComputeLocalToScreenMatrix2D() const noexcept {
    const UIElement* chain[64]{};
    std::size_t depth = 0;
    for (const UIElement* anc = parent_; anc != nullptr && depth < 64u; anc = anc->parent_) {
        chain[depth++] = anc;
    }

    Matrix2D worldM = Matrix2D::Identity();
    for (std::size_t idx = depth; idx > 0u; --idx) {
        const UIElement* anc = chain[idx - 1u];
        const Matrix2D ancLocal = anc->ComputeLocalEffectiveTransform2D();
        if (!ancLocal.IsIdentity()) {
            worldM = Matrix2D::Multiply(worldM, ancLocal);
        }
        const Matrix2D ancChild = anc->GetChildContainerTransform2D();
        if (!ancChild.IsIdentity()) {
            worldM = Matrix2D::Multiply(worldM, ancChild);
        }
    }

    const Matrix2D selfLocal = ComputeLocalEffectiveTransform2D();
    if (!selfLocal.IsIdentity()) {
        worldM = Matrix2D::Multiply(worldM, selfLocal);
    }
    return worldM;
}

Matrix4x4 UIElement::ComputeLocalToScreenMatrix3D() const noexcept {
    const UIElement* chain[64]{};
    std::size_t depth = 0;
    for (const UIElement* anc = parent_; anc != nullptr && depth < 64u; anc = anc->parent_) {
        chain[depth++] = anc;
    }

    Matrix4x4 world3D = Matrix4x4::Identity();
    for (std::size_t idx = depth; idx > 0u; --idx) {
        const UIElement* anc = chain[idx - 1u];
        const Matrix4x4 ancLocal3D = anc->ComputeLocalEffectiveTransform3D();
        if (!ancLocal3D.IsIdentity()) {
            world3D = Matrix4x4::ComposePlanarHomography(world3D, ancLocal3D);
        }
        const Matrix2D ancChild = anc->GetChildContainerTransform2D();
        if (!ancChild.IsIdentity()) {
            world3D = Matrix4x4::ComposePlanarHomography(world3D, Matrix4x4::FromMatrix2D(ancChild));
        }
    }

    const Matrix4x4 selfLocal3D = ComputeLocalEffectiveTransform3D();
    if (!selfLocal3D.IsIdentity()) {
        world3D = Matrix4x4::ComposePlanarHomography(world3D, selfLocal3D);
    }
    return world3D;
}

Point UIElement::ScreenToLocal(Point screenPoint) const noexcept {
    if (HasAncestorOrSelfTransform3D()) {
        Point localPt = screenPoint;
        if (ComputeLocalToScreenMatrix3D().InverseProjectScreenToLocalPlane(screenPoint, localPt)) {
            return localPt;
        }
        return screenPoint;
    }
    const Matrix2D worldM = ComputeLocalToScreenMatrix2D();
    if (worldM.IsIdentity()) {
        return screenPoint;
    }
    Matrix2D inv{};
    if (worldM.TryInvert(inv)) {
        return inv.TransformPoint(screenPoint);
    }
    return screenPoint;
}

Rect UIElement::LocalToScreenBounds(Rect localRect) const noexcept {
    if (HasAncestorOrSelfTransform3D()) {
        return ComputeLocalToScreenMatrix3D().TransformRectBounds(localRect);
    }
    const Matrix2D worldM = ComputeLocalToScreenMatrix2D();
    if (worldM.IsIdentity()) {
        return localRect;
    }
    return worldM.TransformRectBounds(localRect);
}

bool UIElement::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)localPoint;
    (void)button;
    (void)modifiers;
    outRequestCapture = false;
    return false;
}

bool UIElement::OnPointerMove(Point localPoint, std::uint8_t modifiers) {
    (void)localPoint;
    (void)modifiers;
    return false;
}

bool UIElement::OnPointerUp(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool insideBounds) {
    (void)localPoint;
    (void)button;
    (void)modifiers;
    (void)insideBounds;
    return false;
}

bool UIElement::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)key;
    (void)modifiers;
    return false;
}

bool UIElement::OnTextInput(std::string_view utf8Text) {
    (void)utf8Text;
    return false;
}

void UIElement::OnFocusChanged(bool focused) {
    (void)focused;
}

void UIElement::OnPointerCaptureLost() noexcept {}

void UIElement::OnEnabledStateChanged(bool effectivelyEnabled) {
    (void)effectivelyEnabled;
}

void UIElement::PropagateEnabledStateToSubtree() noexcept {
    const bool effEnabled = IsEffectivelyEnabled();
    if (!effEnabled) {
        if ((fastProps_.reserved & InteractiveState_Pressed) != 0u) {
            fastProps_.reserved = static_cast<std::uint8_t>(
                fastProps_.reserved & ~(InteractiveState_Hovered | InteractiveState_Pressed));
            OnPointerCaptureLost();
        } else {
            fastProps_.reserved = static_cast<std::uint8_t>(
                fastProps_.reserved & ~InteractiveState_Hovered);
        }
    }
    OnEnabledStateChanged(effEnabled);
    InvalidateRender();
    for (UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        c->PropagateEnabledStateToSubtree();
    }
}

float UIElement::GetMaxWidth() const noexcept {
    if (const auto* entry = FindCompactEntry(PropId_MaxWidth)) {
        float out = std::numeric_limits<float>::infinity();
        if (TryGetFloat(entry->effectiveValue, out) && out >= 0.0f && !std::isnan(out)) {
            return out;
        }
    }
    return std::numeric_limits<float>::infinity();
}

void UIElement::SetMaxWidth(float v) {
    if (std::isnan(v) || v < 0.0f || std::isinf(v)) {
        (void)ClearValue(PropId_MaxWidth, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_MaxWidth, MakeFloat(v), ValueSourceRank::Local);
    }
}

float UIElement::GetMaxHeight() const noexcept {
    if (const auto* entry = FindCompactEntry(PropId_MaxHeight)) {
        float out = std::numeric_limits<float>::infinity();
        if (TryGetFloat(entry->effectiveValue, out) && out >= 0.0f && !std::isnan(out)) {
            return out;
        }
    }
    return std::numeric_limits<float>::infinity();
}

void UIElement::SetMaxHeight(float v) {
    if (std::isnan(v) || v < 0.0f || std::isinf(v)) {
        (void)ClearValue(PropId_MaxHeight, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_MaxHeight, MakeFloat(v), ValueSourceRank::Local);
    }
}

void UIElement::SetRenderTransform(const Matrix2D& matrix) {
    (void)SetValue(
        PropId_RenderTransform,
        UnifiedValue{Float4Value{matrix.m00, matrix.m01, matrix.m10, matrix.m11}},
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_RenderTransformTranslation,
        UnifiedValue{Float4Value{matrix.tx, matrix.ty, 0.0f, 0.0f}},
        ValueSourceRank::Local);
}

Matrix2D UIElement::GetRenderTransform() const noexcept {
    Matrix2D m = Matrix2D::Identity();
    if (const auto* lin = FindCompactEntry(PropId_RenderTransform)) {
        Float4Value f4{};
        if (lin->effectiveValue.tryGetFloat4(f4)) {
            m.m00 = f4.x;
            m.m01 = f4.y;
            m.m10 = f4.z;
            m.m11 = f4.w;
        }
    }
    if (const auto* tr = FindCompactEntry(PropId_RenderTransformTranslation)) {
        Float4Value f4{};
        if (tr->effectiveValue.tryGetFloat4(f4)) {
            m.tx = f4.x;
            m.ty = f4.y;
        }
    }
    return m;
}

void UIElement::ClearRenderTransform() {
    (void)ClearValue(PropId_RenderTransform, ValueSourceRank::Local);
    (void)ClearValue(PropId_RenderTransformTranslation, ValueSourceRank::Local);
}

void UIElement::SetLayoutTransform(const Matrix2D& matrix) {
    (void)SetValue(
        PropId_LayoutTransform,
        UnifiedValue{Float4Value{matrix.m00, matrix.m01, matrix.m10, matrix.m11}},
        ValueSourceRank::Local);
}

Matrix2D UIElement::GetLayoutTransform() const noexcept {
    Matrix2D m = Matrix2D::Identity();
    if (const auto* lt = FindCompactEntry(PropId_LayoutTransform)) {
        Float4Value f4{};
        if (lt->effectiveValue.tryGetFloat4(f4)) {
            m.m00 = f4.x;
            m.m01 = f4.y;
            m.m10 = f4.z;
            m.m11 = f4.w;
        }
    }
    return m;
}

void UIElement::ClearLayoutTransform() {
    (void)ClearValue(PropId_LayoutTransform, ValueSourceRank::Local);
}

void UIElement::SetRenderTransformOrigin(Point origin) {
    (void)SetValue(
        PropId_RenderTransformOrigin,
        value::FromPoint(origin),
        ValueSourceRank::Local);
}

Point UIElement::GetRenderTransformOrigin() const noexcept {
    if (const auto* orig = FindCompactEntry(PropId_RenderTransformOrigin)) {
        Float4Value f4{};
        if (orig->effectiveValue.tryGetFloat4(f4)) {
            return Point{f4.x, f4.y};
        }
    }
    return Point{0.0f, 0.0f};
}

Matrix2D UIElement::ComputeLocalEffectiveTransform2D() const noexcept {
    return ComputeLocalEffectiveTransform2DForRect(finalRect_);
}

Matrix2D UIElement::ComputeLocalEffectiveTransform2DForRect(const Rect& r) const noexcept {
    const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, fastProps_.width);
    const float h = (r.height > 0.0f) ? r.height : std::max(0.0f, fastProps_.height);

    Matrix2D eff = Matrix2D::Identity();
    float effBoxW = w;
    float effBoxH = h;

    // 0. LayoutTransform (maps unrotated [r.x..r.x+w]x[r.y..r.y+h] into post-layout AABB [r.x..r.x+effBoxW]x[r.y..r.y+effBoxH])
    if (const auto* ltEntry = FindCompactEntry(PropId_LayoutTransform)) {
        Float4Value f4{};
        if (ltEntry->effectiveValue.tryGetFloat4(f4)) {
            const Matrix2D lt{f4.x, f4.y, f4.z, f4.w, 0.0f, 0.0f};
            if (!lt.IsIdentity()) {
                const float minRelX = std::min(0.0f, lt.m00 * w) + std::min(0.0f, lt.m01 * h);
                const float minRelY = std::min(0.0f, lt.m10 * w) + std::min(0.0f, lt.m11 * h);
                eff = Matrix2D{
                    lt.m00,
                    lt.m01,
                    lt.m10,
                    lt.m11,
                    r.x - minRelX - (lt.m00 * r.x + lt.m01 * r.y),
                    r.y - minRelY - (lt.m10 * r.x + lt.m11 * r.y)};
                effBoxW = std::fabs(lt.m00) * w + std::fabs(lt.m01) * h;
                effBoxH = std::fabs(lt.m10) * w + std::fabs(lt.m11) * h;
            }
        }
    }

    // 1. 3D foreshortening projection around element layout-box center (if active)
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    if (const auto* yEntry = FindCompactEntry(PropId_Projection3DYaw)) {
        (void)TryGetFloat(yEntry->effectiveValue, yawDeg);
    }
    if (const auto* pEntry = FindCompactEntry(PropId_Projection3DPitch)) {
        (void)TryGetFloat(pEntry->effectiveValue, pitchDeg);
    }
    if (yawDeg != 0.0f || pitchDeg != 0.0f) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float cosYaw = std::cos(yawDeg * kDegToRad);
        const float cosPitch = std::cos(pitchDeg * kDegToRad);
        const float cx = r.x + effBoxW * 0.5f;
        const float cy = r.y + effBoxH * 0.5f;
        eff = Matrix2D::Multiply(Matrix2D::Scaling(cosYaw, cosPitch, cx, cy), eff);
    }

    // 2. Standard 2D Affine RenderTransform + Independent Phase 5C/6 Animated Transform Channels (TRS + Skew)
    //    composed around normalized RenderTransformOrigin without double-scaling static translation!
    const bool hasRtLin = (FindCompactEntry(PropId_RenderTransform) != nullptr);
    const bool hasRtTrans = (FindCompactEntry(PropId_RenderTransformTranslation) != nullptr);
    const auto* sxEntry = FindCompactEntry(PropId_AnimScaleX);
    const auto* syEntry = FindCompactEntry(PropId_AnimScaleY);
    const auto* skxEntry = FindCompactEntry(PropId_AnimSkewX);
    const auto* skyEntry = FindCompactEntry(PropId_AnimSkewY);
    const auto* rotEntry = FindCompactEntry(PropId_AnimRotateAngle);
    const auto* txEntry = FindCompactEntry(PropId_AnimTranslateX);
    const auto* tyEntry = FindCompactEntry(PropId_AnimTranslateY);
    const bool hasAnimTf = (sxEntry || syEntry || skxEntry || skyEntry || rotEntry || txEntry || tyEntry);

    if (hasRtLin || hasRtTrans || hasAnimTf) {
        Matrix2D combinedRt = Matrix2D::Identity();
        if (hasAnimTf) {
            float animSx = 1.0f;
            float animSy = 1.0f;
            float animSkx = 0.0f;
            float animSky = 0.0f;
            float animDeg = 0.0f;
            float animTx = 0.0f;
            float animTy = 0.0f;
            if (sxEntry) (void)TryGetFloat(sxEntry->effectiveValue, animSx);
            if (syEntry) (void)TryGetFloat(syEntry->effectiveValue, animSy);
            if (skxEntry) (void)TryGetFloat(skxEntry->effectiveValue, animSkx);
            if (skyEntry) (void)TryGetFloat(skyEntry->effectiveValue, animSky);
            if (rotEntry) (void)TryGetFloat(rotEntry->effectiveValue, animDeg);
            if (txEntry) (void)TryGetFloat(txEntry->effectiveValue, animTx);
            if (tyEntry) (void)TryGetFloat(tyEntry->effectiveValue, animTy);

            if (std::fabs(animSx - 1.0f) > 1e-6f || std::fabs(animSy - 1.0f) > 1e-6f) {
                combinedRt = Matrix2D::Multiply(Matrix2D::Scaling(animSx, animSy), combinedRt);
            }
            if (std::fabs(animSkx) > 1e-6f || std::fabs(animSky) > 1e-6f) {
                combinedRt = Matrix2D::Multiply(Matrix2D::Skew(animSkx, animSky), combinedRt);
            }
            if (std::fabs(animDeg) > 1e-6f) {
                combinedRt = Matrix2D::Multiply(Matrix2D::Rotation(animDeg), combinedRt);
            }
            if (std::fabs(animTx) > 1e-6f || std::fabs(animTy) > 1e-6f) {
                combinedRt = Matrix2D::Multiply(Matrix2D::Translation(animTx, animTy), combinedRt);
            }
        }
        if (hasRtLin || hasRtTrans) {
            combinedRt = Matrix2D::Multiply(GetRenderTransform(), combinedRt);
        }
        if (!combinedRt.IsIdentity()) {
            const Point origin = GetRenderTransformOrigin();
            const float pivotX = r.x + origin.x * effBoxW;
            const float pivotY = r.y + origin.y * effBoxH;
            eff = Matrix2D::Multiply(combinedRt.AroundPivot(pivotX, pivotY), eff);
        }
    }

    // 3. Legacy PropId_RenderTransformMatrix (Float4Value{sx, sy, tx, ty} around global origin)
    if (const auto* rtmEntry = FindCompactEntry(PropId_RenderTransformMatrix)) {
        Float4Value f4{};
        if (rtmEntry->effectiveValue.tryGetFloat4(f4)) {
            const Matrix2D legacyM{f4.x, 0.0f, 0.0f, f4.y, f4.z, f4.w};
            eff = Matrix2D::Multiply(legacyM, eff);
        }
    }

    return eff;
}

void UIElement::SetTransform3D(const CompositeTransform3DParams& params) {
    const float camDist = (params.cameraDistance > 10.0f) ? params.cameraDistance : 1000.0f;
    (void)SetValue(
        PropId_Transform3DRotateXYZ,
        MakeFloat4InlineFast(params.rotationX, params.rotationY, params.rotationZ, camDist),
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_Transform3DTranslateXYZ,
        MakeFloat4InlineFast(params.translateX, params.translateY, params.translateZ, 0.0f),
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_Transform3DScaleXYZ,
        MakeFloat4InlineFast(params.scaleX, params.scaleY, params.scaleZ, 1.0f),
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_Transform3DCenterXYZ,
        MakeFloat4InlineFast(
            params.centerX,
            params.centerY,
            params.centerZ,
            params.hasExplicitCenter ? 1.0f : 0.0f),
        ValueSourceRank::Local);
}

CompositeTransform3DParams UIElement::GetTransform3D() const noexcept {
    CompositeTransform3DParams p{};
    if (const auto* rot = FindCompactEntry(PropId_Transform3DRotateXYZ)) {
        Float4Value f4{};
        if (rot->effectiveValue.tryGetFloat4(f4)) {
            p.rotationX = f4.x;
            p.rotationY = f4.y;
            p.rotationZ = f4.z;
            if (f4.w > 10.0f) {
                p.cameraDistance = f4.w;
            }
        }
    }
    if (const auto* tr = FindCompactEntry(PropId_Transform3DTranslateXYZ)) {
        Float4Value f4{};
        if (tr->effectiveValue.tryGetFloat4(f4)) {
            p.translateX = f4.x;
            p.translateY = f4.y;
            p.translateZ = f4.z;
        }
    }
    if (const auto* sc = FindCompactEntry(PropId_Transform3DScaleXYZ)) {
        Float4Value f4{};
        if (sc->effectiveValue.tryGetFloat4(f4)) {
            p.scaleX = f4.x;
            p.scaleY = f4.y;
            p.scaleZ = f4.z;
        }
    }
    if (const auto* ctr = FindCompactEntry(PropId_Transform3DCenterXYZ)) {
        Float4Value f4{};
        if (ctr->effectiveValue.tryGetFloat4(f4)) {
            p.centerX = f4.x;
            p.centerY = f4.y;
            p.centerZ = f4.z;
            p.hasExplicitCenter = (f4.w > 0.5f);
        }
    }
    // Independent 3D animation / binding scalar channels override base components when active
    if (const auto* rx = FindCompactEntry(PropId_AnimRotation3DX)) {
        (void)TryGetFloat(rx->effectiveValue, p.rotationX);
    }
    if (const auto* ry = FindCompactEntry(PropId_AnimRotation3DY)) {
        (void)TryGetFloat(ry->effectiveValue, p.rotationY);
    }
    if (const auto* rz = FindCompactEntry(PropId_AnimRotation3DZ)) {
        (void)TryGetFloat(rz->effectiveValue, p.rotationZ);
    }
    if (const auto* tx = FindCompactEntry(PropId_AnimTranslate3DX)) {
        (void)TryGetFloat(tx->effectiveValue, p.translateX);
    }
    if (const auto* ty = FindCompactEntry(PropId_AnimTranslate3DY)) {
        (void)TryGetFloat(ty->effectiveValue, p.translateY);
    }
    if (const auto* tz = FindCompactEntry(PropId_AnimTranslate3DZ)) {
        (void)TryGetFloat(tz->effectiveValue, p.translateZ);
    }
    return p;
}

void UIElement::ClearTransform3D() {
    (void)ClearValue(PropId_Transform3DRotateXYZ, ValueSourceRank::Local);
    (void)ClearValue(PropId_Transform3DTranslateXYZ, ValueSourceRank::Local);
    (void)ClearValue(PropId_Transform3DScaleXYZ, ValueSourceRank::Local);
    (void)ClearValue(PropId_Transform3DCenterXYZ, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimRotation3DX, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimRotation3DY, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimRotation3DZ, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimTranslate3DX, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimTranslate3DY, ValueSourceRank::Local);
    (void)ClearValue(PropId_AnimTranslate3DZ, ValueSourceRank::Local);
}

bool UIElement::HasActiveTransform3D() const noexcept {
    if (FindCompactEntry(PropId_Transform3DRotateXYZ) == nullptr &&
        FindCompactEntry(PropId_Transform3DTranslateXYZ) == nullptr &&
        FindCompactEntry(PropId_Transform3DScaleXYZ) == nullptr &&
        FindCompactEntry(PropId_AnimRotation3DX) == nullptr &&
        FindCompactEntry(PropId_AnimRotation3DY) == nullptr &&
        FindCompactEntry(PropId_AnimRotation3DZ) == nullptr &&
        FindCompactEntry(PropId_AnimTranslate3DX) == nullptr &&
        FindCompactEntry(PropId_AnimTranslate3DY) == nullptr &&
        FindCompactEntry(PropId_AnimTranslate3DZ) == nullptr) {
        return false;
    }
    return !GetTransform3D().IsIdentity();
}

bool UIElement::HasAncestorOrSelfTransform3D() const noexcept {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->parent_) {
        if (cur->HasActiveTransform3D()) {
            return true;
        }
    }
    return false;
}

Matrix4x4 UIElement::ComputeLocalEffectiveTransform3D() const noexcept {
    return ComputeLocalEffectiveTransform3DForRect(finalRect_);
}

Matrix4x4 UIElement::ComputeLocalEffectiveTransform3DForRect(const Rect& r) const noexcept {
    const Matrix2D eff2D = ComputeLocalEffectiveTransform2DForRect(r);
    Matrix4x4 eff3D = Matrix4x4::FromMatrix2D(eff2D);
    if (!HasActiveTransform3D()) {
        return eff3D;
    }

    const CompositeTransform3DParams p = GetTransform3D();
    const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, fastProps_.width);
    const float h = (r.height > 0.0f) ? r.height : std::max(0.0f, fastProps_.height);
    const float boxCenterX = r.x + w * 0.5f;
    const float boxCenterY = r.y + h * 0.5f;

    float pivotX = boxCenterX;
    float pivotY = boxCenterY;
    float pivotZ = 0.0f;
    if (p.hasExplicitCenter) {
        pivotX = r.x + p.centerX;
        pivotY = r.y + p.centerY;
        pivotZ = p.centerZ;
    } else if (FindCompactEntry(PropId_RenderTransformOrigin) != nullptr) {
        const Point orig = GetRenderTransformOrigin();
        pivotX = r.x + orig.x * w;
        pivotY = r.y + orig.y * h;
    }

    const Matrix4x4 proj3D = Matrix4x4::FromCompositeTransform3D(
        p,
        pivotX,
        pivotY,
        pivotZ,
        boxCenterX,
        boxCenterY);
    return Matrix4x4::ComposePlanarHomography(proj3D, eff3D);
}

void UIElement::SetBlurEffect(float radiusPx) {
    const float clamped = std::max(0.0f, radiusPx);
    (void)SetValue(
        PropId_EffectKind,
        value::FromInt(static_cast<std::int64_t>(UIEffectKind::Blur)),
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_EffectParams,
        MakeFloat4InlineFast(clamped, -1.0f, 0.0f, 1.0f),
        ValueSourceRank::Local);
}

void UIElement::SetDropShadowEffect(
    float blurRadiusPx,
    float shadowDepthPx,
    float directionDeg,
    const Color& color,
    float opacity) {
    (void)SetValue(
        PropId_EffectKind,
        value::FromInt(static_cast<std::int64_t>(UIEffectKind::DropShadow)),
        ValueSourceRank::Local);
    (void)SetValue(
        PropId_EffectParams,
        MakeFloat4InlineFast(
            std::max(0.0f, blurRadiusPx),
            std::max(0.0f, shadowDepthPx),
            directionDeg,
            std::clamp(opacity, 0.0f, 1.0f)),
        ValueSourceRank::Local);
    (void)SetValue(PropId_EffectColor, value::FromColor(color), ValueSourceRank::Local);
}

void UIElement::ClearEffect() {
    (void)ClearValue(PropId_EffectKind, ValueSourceRank::Local);
    (void)ClearValue(PropId_EffectParams, ValueSourceRank::Local);
    (void)ClearValue(PropId_EffectColor, ValueSourceRank::Local);
    (void)ClearValue(PropId_EffectBlurRadius, ValueSourceRank::Local);
    (void)ClearValue(PropId_EffectShadowDepth, ValueSourceRank::Local);
}

UIEffectKind UIElement::GetEffectKind() const noexcept {
    return GetEffectDescriptor().kind;
}

UIEffectDescriptor UIElement::GetEffectDescriptor() const noexcept {
    UIEffectDescriptor desc{};
    bool hasExplicitKind = false;
    if (const auto* kEntry = FindCompactEntry(PropId_EffectKind)) {
        if (const auto* iv = std::get_if<std::int64_t>(&kEntry->effectiveValue)) {
            hasExplicitKind = true;
            if (*iv == 1) desc.kind = UIEffectKind::Blur;
            else if (*iv == 2) desc.kind = UIEffectKind::DropShadow;
            else desc.kind = UIEffectKind::None;
        }
    }

    if (const auto* pEntry = FindCompactEntry(PropId_EffectParams)) {
        Float4Value f4{};
        if (pEntry->effectiveValue.tryGetFloat4(f4)) {
            if (!hasExplicitKind) {
                desc.kind = (f4.y < -0.5f) ? UIEffectKind::Blur : UIEffectKind::DropShadow;
            }
            desc.blurRadius = std::max(0.0f, f4.x);
            if (f4.y >= 0.0f) {
                desc.shadowDepth = f4.y;
            }
            desc.directionDeg = f4.z;
            desc.opacity = std::clamp(f4.w, 0.0f, 1.0f);
        }
    }

    if (const auto* brEntry = FindCompactEntry(PropId_EffectBlurRadius)) {
        float br = desc.blurRadius;
        if (TryGetFloat(brEntry->effectiveValue, br)) {
            desc.blurRadius = std::max(0.0f, br);
            if (!hasExplicitKind && desc.kind == UIEffectKind::None && desc.blurRadius > 0.0f) {
                desc.kind = UIEffectKind::Blur;
            }
        }
    }
    if (const auto* sdEntry = FindCompactEntry(PropId_EffectShadowDepth)) {
        float sd = desc.shadowDepth;
        if (TryGetFloat(sdEntry->effectiveValue, sd)) {
            desc.shadowDepth = std::max(0.0f, sd);
        }
    }
    if (const auto* cEntry = FindCompactEntry(PropId_EffectColor)) {
        if (const auto cOpt = value::TryGetColor(cEntry->effectiveValue)) {
            desc.color = *cOpt;
        }
    }
    return desc;
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

UIElement* UIElement::HitTest(Point point) noexcept {
    if (GetVisibility() != Visibility::Visible || GetOpacity() <= 0.0f) {
        return nullptr;
    }

    // Invert this element's 3D perspective planar homography (when Transform3D is active)
    // or full 2D affine RenderTransform (+ RenderTransformOrigin + Projection3D)
    Point localPt = point;
    if (HasActiveTransform3D()) {
        if (!ComputeLocalEffectiveTransform3D().InverseProjectScreenToLocalPlane(point, localPt)) {
            return nullptr;
        }
    } else {
        const Matrix2D localTransform = ComputeLocalEffectiveTransform2D();
        if (!localTransform.IsIdentity()) {
            Matrix2D invLocal{};
            if (!localTransform.TryInvert(invLocal)) {
                return nullptr;
            }
            localPt = invLocal.TransformPoint(localPt);
        }
    }

    const Rect r = finalRect_;

    // Optional ClipToBounds check before testing children
    bool childrenReachable = true;
    const UnifiedValue clipVal = GetValue(PropId_ClipToBounds);
    if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
        if (r.width <= 0.0f || r.height <= 0.0f ||
            localPt.x < r.x || localPt.x > r.x + r.width ||
            localPt.y < r.y || localPt.y > r.y + r.height) {
            return nullptr;
        }
        const Rect childClip = GetLocalClipBounds();
        if (childClip.width <= 0.0f || childClip.height <= 0.0f ||
            localPt.x < childClip.x || localPt.x > childClip.x + childClip.width ||
            localPt.y < childClip.y || localPt.y > childClip.y + childClip.height) {
            childrenReachable = false;
        }
    }

    // Invert optional container child transform (e.g. Viewbox uniform/fill child scaling)
    Point childPt = localPt;
    if (childrenReachable) {
        const Matrix2D childContainerTransform = GetChildContainerTransform2D();
        if (!childContainerTransform.IsIdentity()) {
            Matrix2D invChild{};
            if (childContainerTransform.TryInvert(invChild)) {
                childPt = invChild.TransformPoint(localPt);
            } else {
                childrenReachable = false;
            }
        }
    }

    // Traverse children in reverse Z-order (lastChild_ -> firstChild_), skipping hidden Expander/TabControl slots
    if (childrenReachable) {
        const auto* selfExpander = DynamicCast<Expander>();
        const auto* selfTabControl = DynamicCast<TabControl>();
        for (UIElement* child = lastChild_; child != nullptr; child = child->prevSibling_) {
            if (selfExpander && selfExpander->IsBodyChildHidden(child)) {
                continue;
            }
            if (selfTabControl && selfTabControl->IsChildInactiveTabPage(child)) {
                continue;
            }
            if (UIElement* hit = child->HitTest(childPt)) {
                return hit;
            }
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

    const float minW = std::max(0.0f, fastProps_.minWidth);
    const float minH = std::max(0.0f, fastProps_.minHeight);
    const float effMaxW = std::max(minW, GetMaxWidth());
    const float effMaxH = std::max(minH, GetMaxHeight());

    const Size outerSlotAvail{
        std::max(0.0f, availableSize.width - marginW),
        std::max(0.0f, availableSize.height - marginH)};

    Matrix2D lt = Matrix2D::Identity();
    bool hasLt = false;
    if (const auto* ltEntry = FindCompactEntry(PropId_LayoutTransform)) {
        Float4Value f4{};
        if (ltEntry->effectiveValue.tryGetFloat4(f4)) {
            lt = Matrix2D{f4.x, f4.y, f4.z, f4.w, 0.0f, 0.0f};
            hasLt = !lt.IsIdentity();
        }
    }

    const Size mappedAvail = hasLt
        ? MapAvailableSizeThroughLayoutTransform(outerSlotAvail, lt)
        : outerSlotAvail;

    Size innerAvail{
        std::min(mappedAvail.width, effMaxW),
        std::min(mappedAvail.height, effMaxH)};

    if (fastProps_.width >= 0.0f) {
        innerAvail.width = std::clamp(fastProps_.width, minW, effMaxW);
    }
    if (fastProps_.height >= 0.0f) {
        innerAvail.height = std::clamp(fastProps_.height, minH, effMaxH);
    }

    Size coreSize = MeasureCore(innerAvail);
    if (fastProps_.width >= 0.0f) {
        coreSize.width = fastProps_.width;
    }
    if (fastProps_.height >= 0.0f) {
        coreSize.height = fastProps_.height;
    }

    coreSize.width = std::clamp(coreSize.width, minW, effMaxW);
    coreSize.height = std::clamp(coreSize.height, minH, effMaxH);
    measuredCoreSize_ = coreSize;

    const float desiredCoreW = hasLt
        ? (std::fabs(lt.m00) * coreSize.width + std::fabs(lt.m01) * coreSize.height)
        : coreSize.width;
    const float desiredCoreH = hasLt
        ? (std::fabs(lt.m10) * coreSize.width + std::fabs(lt.m11) * coreSize.height)
        : coreSize.height;

    desiredSize_ = {
        desiredCoreW + marginW,
        desiredCoreH + marginH};
    lastMeasureConstraint_ = availableSize;
    ClearLayoutDirtyFlags(MeasureDirty | SubtreeMeasureDirty);
}

void UIElement::Arrange(Rect finalRect) noexcept {
    lastArrangeSlot_ = finalRect;

    bool hiddenByCompositeParent = false;
    if (parent_ != nullptr) {
        if (const auto* pExp = parent_->DynamicCast<Expander>(); pExp && pExp->IsBodyChildHidden(this)) {
            hiddenByCompositeParent = true;
        } else if (const auto* pTab = parent_->DynamicCast<TabControl>(); pTab && pTab->IsChildInactiveTabPage(this)) {
            hiddenByCompositeParent = true;
        }
    }

    if (GetVisibility() == Visibility::Collapsed || hiddenByCompositeParent) {
        const Rect collapsedRect{finalRect.x, finalRect.y, 0.0f, 0.0f};
        if (finalRect_ != collapsedRect) {
            finalRect_ = collapsedRect;
            InvalidateRender();
        }
        if (hiddenByCompositeParent) {
            const UIElement* keepHdr = nullptr;
            if (const auto* selfHcc = DynamicCast<HeaderedContentControl>()) {
                keepHdr = selfHcc->GetHeaderElement();
            }
            for (UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
                if (c == keepHdr) {
                    continue;
                }
                c->Arrange(collapsedRect);
            }
        }
        ClearLayoutDirtyFlags(ArrangeDirty | SubtreeArrangeDirty);
        return;
    }

    const Thickness m = GetMargin();
    const float marginW = m.left + m.right;
    const float marginH = m.top + m.bottom;

    const float slotX = finalRect.x + m.left;
    const float slotY = finalRect.y + m.top;
    const float slotW = std::max(0.0f, finalRect.width - marginW);
    const float slotH = std::max(0.0f, finalRect.height - marginH);

    const float minW = std::max(0.0f, fastProps_.minWidth);
    const float minH = std::max(0.0f, fastProps_.minHeight);
    const float effMaxW = std::max(minW, GetMaxWidth());
    const float effMaxH = std::max(minH, GetMaxHeight());

    Matrix2D lt = Matrix2D::Identity();
    bool hasLt = false;
    if (const auto* ltEntry = FindCompactEntry(PropId_LayoutTransform)) {
        Float4Value f4{};
        if (ltEntry->effectiveValue.tryGetFloat4(f4)) {
            lt = Matrix2D{f4.x, f4.y, f4.z, f4.w, 0.0f, 0.0f};
            hasLt = !lt.IsIdentity();
        }
    }

    const HorizontalAlignment hAlign = GetHorizontalAlignment();
    const VerticalAlignment vAlign = GetVerticalAlignment();

    const Size mappedSlot = hasLt
        ? MapAvailableSizeThroughLayoutTransform(Size{slotW, slotH}, lt)
        : Size{slotW, slotH};

    const float naturalCoreW = hasLt
        ? measuredCoreSize_.width
        : std::max(0.0f, desiredSize_.width - marginW);
    const float naturalCoreH = hasLt
        ? measuredCoreSize_.height
        : std::max(0.0f, desiredSize_.height - marginH);

    const bool stretchLocalW = !hasLt
        ? (hAlign == HorizontalAlignment::Stretch)
        : ((std::fabs(lt.m01) <= 1e-6f && std::fabs(lt.m10) <= 1e-6f)
               ? (hAlign == HorizontalAlignment::Stretch)
               : ((std::fabs(lt.m00) <= 1e-6f && std::fabs(lt.m11) <= 1e-6f)
                      ? (vAlign == VerticalAlignment::Stretch)
                      : (hAlign == HorizontalAlignment::Stretch && vAlign == VerticalAlignment::Stretch)));

    const bool stretchLocalH = !hasLt
        ? (vAlign == VerticalAlignment::Stretch)
        : ((std::fabs(lt.m01) <= 1e-6f && std::fabs(lt.m10) <= 1e-6f)
               ? (vAlign == VerticalAlignment::Stretch)
               : ((std::fabs(lt.m00) <= 1e-6f && std::fabs(lt.m11) <= 1e-6f)
                      ? (hAlign == HorizontalAlignment::Stretch)
                      : (hAlign == HorizontalAlignment::Stretch && vAlign == VerticalAlignment::Stretch)));

    float arrangedW = mappedSlot.width;
    if (fastProps_.width >= 0.0f) {
        arrangedW = std::clamp(fastProps_.width, minW, effMaxW);
    } else if (stretchLocalW) {
        arrangedW = std::clamp(mappedSlot.width, minW, effMaxW);
    } else {
        const float naturalW = (lastMeasureConstraint_.width >= 0.0f) ? naturalCoreW : mappedSlot.width;
        arrangedW = std::clamp(naturalW, minW, effMaxW);
    }

    float arrangedH = mappedSlot.height;
    if (fastProps_.height >= 0.0f) {
        arrangedH = std::clamp(fastProps_.height, minH, effMaxH);
    } else if (stretchLocalH) {
        arrangedH = std::clamp(mappedSlot.height, minH, effMaxH);
    } else {
        const float naturalH = (lastMeasureConstraint_.height >= 0.0f) ? naturalCoreH : mappedSlot.height;
        arrangedH = std::clamp(naturalH, minH, effMaxH);
    }

    const float layoutBoxW = hasLt
        ? (std::fabs(lt.m00) * arrangedW + std::fabs(lt.m01) * arrangedH)
        : arrangedW;
    const float layoutBoxH = hasLt
        ? (std::fabs(lt.m10) * arrangedW + std::fabs(lt.m11) * arrangedH)
        : arrangedH;

    float offsetX = 0.0f;
    switch (hAlign) {
        case HorizontalAlignment::Left:
            offsetX = 0.0f;
            break;
        case HorizontalAlignment::Center:
            offsetX = (slotW - layoutBoxW) * 0.5f;
            break;
        case HorizontalAlignment::Right:
            offsetX = slotW - layoutBoxW;
            break;
        case HorizontalAlignment::Stretch:
            if (parent_ != nullptr && layoutBoxW < slotW) {
                offsetX = (slotW - layoutBoxW) * 0.5f;
            }
            break;
    }

    float offsetY = 0.0f;
    switch (vAlign) {
        case VerticalAlignment::Top:
            offsetY = 0.0f;
            break;
        case VerticalAlignment::Center:
            offsetY = (slotH - layoutBoxH) * 0.5f;
            break;
        case VerticalAlignment::Bottom:
            offsetY = slotH - layoutBoxH;
            break;
        case VerticalAlignment::Stretch:
            if (parent_ != nullptr && layoutBoxH < slotH) {
                offsetY = (slotH - layoutBoxH) * 0.5f;
            }
            break;
    }

    const Rect innerRect{
        slotX + offsetX,
        slotY + offsetY,
        arrangedW,
        arrangedH};

    if (finalRect_ != innerRect) {
        finalRect_ = innerRect;
        InvalidateRender();
    }
    ArrangeCore(innerRect);
    ClearLayoutDirtyFlags(ArrangeDirty | SubtreeArrangeDirty);
}

Size UIElement::MeasureCore(Size availableSize) noexcept {
    const Thickness p = mountedTemplate_ ? Thickness{} : GetPadding();
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
    const Thickness p = mountedTemplate_ ? Thickness{} : GetPadding();
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
    if (propId == PropId_Template) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            if (!sv->view().empty()) {
                (void)ApplyControlTemplateByKey(sv->view());
            } else {
                ClearControlTemplate();
            }
        } else if (std::holds_alternative<std::monostate>(newValue)) {
            ClearControlTemplate();
        }
        return;
    }
    if (mountedTemplate_) {
        if (!IsFastInlinePropertyId(propId) || std::holds_alternative<std::monostate>(newValue)) {
            SyncTemplateBindingsForProperty(propId);
        }
    }
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
        case PropId_HorizontalAlignment:
            return static_cast<std::int64_t>(GetHorizontalAlignment());
        case PropId_VerticalAlignment:
            return static_cast<std::int64_t>(GetVerticalAlignment());
        case PropId_IsEnabled:
            return IsEnabled();
        case PropId_Focusable:
            return IsFocusable();
        default:
            break;
    }
    if (const auto* entry = FindCompactEntry(propId)) {
        return entry->effectiveValue;
    }
    if (propId == PropId_Title) {
        if (const auto* hdrEntry = FindCompactEntry(PropId_Header)) {
            if (std::holds_alternative<StringValue>(hdrEntry->effectiveValue)) {
                return hdrEntry->effectiveValue;
            }
        }
    } else if (propId == PropId_Header) {
        if (const auto* titleEntry = FindCompactEntry(PropId_Title)) {
            if (std::holds_alternative<StringValue>(titleEntry->effectiveValue)) {
                return titleEntry->effectiveValue;
            }
        }
    }
    return std::monostate{};
}

ValueSourceRank UIElement::GetPropertySourceRank(std::uint16_t propId) const noexcept {
    const int fastSlot = FastInlineSlotIndex(propId);
    if (fastSlot >= 0) {
        return static_cast<ValueSourceRank>(fastInlineRanks_[static_cast<std::size_t>(fastSlot)]);
    }
    if (const auto* entry = FindCompactEntry(propId)) {
        if (!std::holds_alternative<std::monostate>(entry->effectiveValue)) {
            return static_cast<ValueSourceRank>(entry->sourceRank);
        }
    }
    return ValueSourceRank::Default;
}

bool UIElement::SetValue(
    std::uint16_t propId,
    const UnifiedValue& value,
    ValueSourceRank rank) {
    const auto rawRank = static_cast<std::uint8_t>(rank);
    const int fastSlot = FastInlineSlotIndex(propId);
    if (fastSlot >= 0) {
        if (rawRank < fastInlineRanks_[static_cast<std::size_t>(fastSlot)]) {
            return false;
        }
    }
    float numericVal = 0.0f;
    bool boolVal = false;
    switch (propId) {
        case PropId_Width:
            if (TryGetFloat(value, numericVal)) {
                SetWidth(numericVal);
                fastInlineRanks_[0] = (numericVal < 0.0f)
                    ? static_cast<std::uint8_t>(ValueSourceRank::Default)
                    : rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Height:
            if (TryGetFloat(value, numericVal)) {
                SetHeight(numericVal);
                fastInlineRanks_[1] = (numericVal < 0.0f)
                    ? static_cast<std::uint8_t>(ValueSourceRank::Default)
                    : rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_MinWidth:
            if (TryGetFloat(value, numericVal)) {
                SetMinWidth(numericVal);
                fastInlineRanks_[2] = rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_MinHeight:
            if (TryGetFloat(value, numericVal)) {
                SetMinHeight(numericVal);
                fastInlineRanks_[3] = rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Margin:
            if (auto m = value::TryGetThickness(value)) {
                SetMargin(*m);
                fastInlineRanks_[4] = rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Padding:
            if (auto p = value::TryGetThickness(value)) {
                SetPadding(*p);
                fastInlineRanks_[5] = rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Opacity:
            if (TryGetFloat(value, numericVal)) {
                SetOpacity(numericVal);
                fastInlineRanks_[6] = rawRank;
                OnPropertyChanged(propId, value);
                return true;
            }
            return false;
        case PropId_Visibility:
            if (const auto* i = std::get_if<std::int64_t>(&value)) {
                if (*i >= 0 && *i <= 2) {
                    SetVisibility(static_cast<Visibility>(*i));
                    fastInlineRanks_[7] = rawRank;
                    OnPropertyChanged(propId, value);
                    return true;
                }
            } else if (const auto* sv = std::get_if<StringValue>(&value)) {
                const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(propId, sv->str());
                if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
                    SetVisibility(static_cast<Visibility>(*pi));
                    fastInlineRanks_[7] = rawRank;
                    OnPropertyChanged(propId, parsed);
                    return true;
                }
            }
            return false;
        case PropId_HorizontalAlignment:
            if (const auto* i = std::get_if<std::int64_t>(&value)) {
                if (*i >= 0 && *i <= 3) {
                    SetHorizontalAlignment(static_cast<HorizontalAlignment>(*i));
                    fastInlineRanks_[8] = rawRank;
                    OnPropertyChanged(propId, value);
                    return true;
                }
            } else if (const auto* sv = std::get_if<StringValue>(&value)) {
                const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(propId, sv->str());
                if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
                    SetHorizontalAlignment(static_cast<HorizontalAlignment>(*pi));
                    fastInlineRanks_[8] = rawRank;
                    OnPropertyChanged(propId, parsed);
                    return true;
                }
            }
            return false;
        case PropId_VerticalAlignment:
            if (const auto* i = std::get_if<std::int64_t>(&value)) {
                if (*i >= 0 && *i <= 3) {
                    SetVerticalAlignment(static_cast<VerticalAlignment>(*i));
                    fastInlineRanks_[9] = rawRank;
                    OnPropertyChanged(propId, value);
                    return true;
                }
            } else if (const auto* sv = std::get_if<StringValue>(&value)) {
                const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(propId, sv->str());
                if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
                    SetVerticalAlignment(static_cast<VerticalAlignment>(*pi));
                    fastInlineRanks_[9] = rawRank;
                    OnPropertyChanged(propId, parsed);
                    return true;
                }
            }
            return false;
        case PropId_IsEnabled:
            if (TryCoerceBoolValue(value, boolVal)) {
                SetIsEnabled(boolVal);
                fastInlineRanks_[10] = rawRank;
                OnPropertyChanged(propId, UnifiedValue{boolVal});
                return true;
            }
            return false;
        case PropId_Focusable:
            if (TryCoerceBoolValue(value, boolVal)) {
                SetFocusable(boolVal);
                fastInlineRanks_[11] = rawRank;
                OnPropertyChanged(propId, UnifiedValue{boolVal});
                return true;
            }
            return false;
        case PropId_RenderTransform:
            if (const auto* sv = std::get_if<StringValue>(&value)) {
                const Matrix2D m = ParseXamlMatrix2DLiteral(sv->str());
                const bool okLin = SetValue(
                    PropId_RenderTransform,
                    UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}},
                    rank);
                if (okLin) {
                    (void)SetValue(
                        PropId_RenderTransformTranslation,
                        UnifiedValue{Float4Value{m.tx, m.ty, 0.0f, 0.0f}},
                        rank);
                }
                return okLin;
            }
            break;
        case PropId_LayoutTransform:
            if (const auto* sv = std::get_if<StringValue>(&value)) {
                const Matrix2D m = ParseXamlMatrix2DLiteral(sv->str());
                return SetValue(
                    PropId_LayoutTransform,
                    UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}},
                    rank);
            }
            break;
        case PropId_TextWrapping:
            if (const auto* sv = std::get_if<StringValue>(&value)) {
                return SetValue(
                    PropId_TextWrapping,
                    ParseBuiltinLiteralPropertyValue(PropId_TextWrapping, sv->view()),
                    rank);
            }
            break;
        case PropId_Background:
        case PropId_Foreground:
        case PropId_BorderBrush: {
            const std::uint16_t gradPropId = CompanionGradientPropId(propId);
            if (const auto* sv = std::get_if<StringValue>(&value)) {
                if (IsGradientBrushSpec(sv->view())) {
                    if (const auto* existing = FindCompactEntry(propId);
                        existing && rawRank < existing->sourceRank) {
                        return false;
                    }
                    const std::uint32_t brushId =
                        GradientBrushRegistry::Instance().RegisterOrResolveSpec(sv->view());
                    if (brushId == 0u) {
                        return false;
                    }
                    const Color repColor = ComputeBrushRepresentativeColor(brushId);
                    const bool okBase = SetValue(propId, value::FromColor(repColor), rank);
                    if (okBase && gradPropId != 0u) {
                        (void)SetValue(
                            gradPropId,
                            value::FromInt(static_cast<std::int64_t>(brushId)),
                            rank);
                    }
                    return okBase;
                }
                const std::string_view rawStr = sv->view();
                const bool looksLikeUriOrAsset =
                    rawStr.empty() ||
                    (!rawStr.empty() && rawStr.front() != '#' &&
                     (rawStr.find('.') != std::string_view::npos ||
                      rawStr.find('/') != std::string_view::npos ||
                      rawStr.find('\\') != std::string_view::npos ||
                      rawStr.find(':') != std::string_view::npos));
                if (!looksLikeUriOrAsset) {
                    const UnifiedValue parsedColor = ParseBuiltinLiteralPropertyValue(propId, rawStr);
                    if (std::holds_alternative<Float4Value>(parsedColor)) {
                        return SetValue(propId, parsedColor, rank);
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    if (value.isTraceable()) {
        rs_rt::pinValueInPools(value);
        if (!registeredInValuePoolSet_) {
            registeredInValuePoolSet_ = true;
            LiveTraceableUIElementRegistry::Instance().Register(this);
        }
    }

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
        if (const std::uint16_t gradPropId = CompanionGradientPropId(propId);
            gradPropId != 0u && std::holds_alternative<Float4Value>(value)) {
            (void)ClearValue(gradPropId, rank);
        }
        InvalidateRender();
        if (IsLayoutAffectingSparseProperty(propId)) {
            InvalidateLayout();
            if (IsParentLayoutAttachedProperty(propId) && parent_) {
                parent_->InvalidateLayout();
            }
        } else if (IsArrangeOnlySparseProperty(propId)) {
            InvalidateArrange();
        }
        OnPropertyChanged(propId, value);
        if (propId == PropId_LocSource) {
            if (const auto* sv = std::get_if<StringValue>(&value)) {
                if (const auto locFn = gElementLocSourceHook.load(std::memory_order_acquire)) {
                    locFn(this, sv->view());
                }
            }
        }
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
    if (const std::uint16_t gradPropId = CompanionGradientPropId(propId);
        gradPropId != 0u && std::holds_alternative<Float4Value>(value)) {
        (void)ClearValue(gradPropId, rank);
    }
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    } else if (IsArrangeOnlySparseProperty(propId)) {
        InvalidateArrange();
    }
    OnPropertyChanged(propId, value);
    if (propId == PropId_LocSource) {
        if (const auto* sv = std::get_if<StringValue>(&value)) {
            if (const auto locFn = gElementLocSourceHook.load(std::memory_order_acquire)) {
                locFn(this, sv->view());
            }
        }
    }
    return true;
}

void UIElement::SetStyleKey(std::string_view styleKey) {
    if (styleKey.empty()) {
        (void)ClearValue(PropId_Style, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_Style, value::FromString(std::string(styleKey)), ValueSourceRank::Local);
    }
}

std::string UIElement::GetStyleKey() const {
    const UnifiedValue v = GetValue(PropId_Style);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

bool UIElement::ForceEffectiveValue(
    std::uint16_t propId,
    const UnifiedValue& value,
    ValueSourceRank rank) {
    const int fastSlot = FastInlineSlotIndex(propId);
    if (fastSlot >= 0) {
        fastInlineRanks_[static_cast<std::size_t>(fastSlot)] =
            static_cast<std::uint8_t>(ValueSourceRank::Default);
        return SetValue(propId, value, rank);
    }
    if (propId == PropId_RenderTransform) {
        if (const auto* sv = std::get_if<StringValue>(&value)) {
            const Matrix2D m = ParseXamlMatrix2DLiteral(sv->str());
            const bool okLin = ForceEffectiveValue(
                PropId_RenderTransform,
                UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}},
                rank);
            if (okLin) {
                (void)ForceEffectiveValue(
                    PropId_RenderTransformTranslation,
                    UnifiedValue{Float4Value{m.tx, m.ty, 0.0f, 0.0f}},
                    rank);
            }
            return okLin;
        }
    } else if (propId == PropId_LayoutTransform) {
        if (const auto* sv = std::get_if<StringValue>(&value)) {
            const Matrix2D m = ParseXamlMatrix2DLiteral(sv->str());
            return ForceEffectiveValue(
                PropId_LayoutTransform,
                UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}},
                rank);
        }
    } else if (const std::uint16_t gradPropId = CompanionGradientPropId(propId); gradPropId != 0u) {
        if (const auto* sv = std::get_if<StringValue>(&value)) {
            if (IsGradientBrushSpec(sv->view())) {
                const std::uint32_t brushId =
                    GradientBrushRegistry::Instance().RegisterOrResolveSpec(sv->view());
                if (brushId == 0u) {
                    return false;
                }
                const Color repColor = ComputeBrushRepresentativeColor(brushId);
                const bool okBase = ForceEffectiveValue(propId, value::FromColor(repColor), rank);
                if (okBase) {
                    (void)ForceEffectiveValue(
                        gradPropId,
                        value::FromInt(static_cast<std::int64_t>(brushId)),
                        rank);
                }
                return okBase;
            }
            const UnifiedValue parsedColor = ParseBuiltinLiteralPropertyValue(propId, sv->view());
            if (std::holds_alternative<Float4Value>(parsedColor)) {
                return ForceEffectiveValue(propId, parsedColor, rank);
            }
        }
    }
    if (value.isTraceable()) {
        rs_rt::pinValueInPools(value);
        if (!registeredInValuePoolSet_) {
            registeredInValuePoolSet_ = true;
            LiveTraceableUIElementRegistry::Instance().Register(this);
        }
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
        it->sourceRank = rawRank;
        it->effectiveValue = value;
    } else {
        CompactPropertyEntry entry{};
        entry.propId = propId;
        entry.sourceRank = rawRank;
        entry.effectiveValue = value;
        sparseProps_.insert(it, std::move(entry));
    }
    if (const std::uint16_t gradPropId = CompanionGradientPropId(propId);
        gradPropId != 0u && std::holds_alternative<Float4Value>(value)) {
        (void)ForceClearSparseEntry(gradPropId);
    }
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    } else if (IsArrangeOnlySparseProperty(propId)) {
        InvalidateArrange();
    }
    OnPropertyChanged(propId, value);
    return true;
}

bool UIElement::ForceClearSparseEntry(std::uint16_t propId) {
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
    if (it->bindingSlotId != 0xFFFFu || it->animationTrackId != 0xFFFFu ||
        (it->entryMetaFlags & kEntryMeta_PinnedTemplateSlot) != 0u) {
        it->sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Default);
        it->effectiveValue = std::monostate{};
    } else {
        sparseProps_.erase(it);
    }
    if (propId == PropId_RenderTransform) {
        (void)ForceClearSparseEntry(PropId_RenderTransformTranslation);
    } else if (const std::uint16_t gradPropId = CompanionGradientPropId(propId); gradPropId != 0u) {
        (void)ForceClearSparseEntry(gradPropId);
    }
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    } else if (IsArrangeOnlySparseProperty(propId)) {
        InvalidateArrange();
    }
    OnPropertyChanged(propId, std::monostate{});
    return true;
}

bool UIElement::ClearValue(std::uint16_t propId, ValueSourceRank rank) {
    const auto rawRank = static_cast<std::uint8_t>(rank);
    const int fastSlot = FastInlineSlotIndex(propId);
    if (fastSlot >= 0) {
        if (rawRank < fastInlineRanks_[static_cast<std::size_t>(fastSlot)]) {
            return false;
        }
    }
    switch (propId) {
    case PropId_Width:
        SetWidth(-1.0f);
        fastInlineRanks_[0] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Height:
        SetHeight(-1.0f);
        fastInlineRanks_[1] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_MinWidth:
        SetMinWidth(0.0f);
        fastInlineRanks_[2] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_MinHeight:
        SetMinHeight(0.0f);
        fastInlineRanks_[3] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Margin:
        SetMargin({});
        fastInlineRanks_[4] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Padding:
        SetPadding({});
        fastInlineRanks_[5] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Opacity:
        SetOpacity(1.0f);
        fastInlineRanks_[6] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Visibility:
        SetVisibility(Visibility::Visible);
        fastInlineRanks_[7] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_HorizontalAlignment:
        SetHorizontalAlignment(HorizontalAlignment::Stretch);
        fastInlineRanks_[8] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_VerticalAlignment:
        SetVerticalAlignment(VerticalAlignment::Stretch);
        fastInlineRanks_[9] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_IsEnabled:
        SetIsEnabled(true);
        fastInlineRanks_[10] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        OnPropertyChanged(propId, std::monostate{});
        return true;
    case PropId_Focusable:
        ClearExplicitFocusable();
        fastInlineRanks_[11] = static_cast<std::uint8_t>(ValueSourceRank::Default);
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
    if (rawRank < it->sourceRank) {
        return false;
    }
    if (it->bindingSlotId != 0xFFFFu || it->animationTrackId != 0xFFFFu ||
        (it->entryMetaFlags & kEntryMeta_PinnedTemplateSlot) != 0u) {
        it->sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Default);
        it->effectiveValue = std::monostate{};
    } else {
        sparseProps_.erase(it);
    }
    if (propId == PropId_RenderTransform) {
        (void)ClearValue(PropId_RenderTransformTranslation, rank);
    } else if (const std::uint16_t gradPropId = CompanionGradientPropId(propId); gradPropId != 0u) {
        (void)ClearValue(gradPropId, rank);
    }
    InvalidateRender();
    if (IsLayoutAffectingSparseProperty(propId)) {
        InvalidateLayout();
        if (IsParentLayoutAttachedProperty(propId) && parent_) {
            parent_->InvalidateLayout();
        }
    } else if (IsArrangeOnlySparseProperty(propId)) {
        InvalidateArrange();
    }
    OnPropertyChanged(propId, std::monostate{});
    return true;
}

void UIElement::CoerceSparseFloatInPlace(std::uint16_t propId, float clampedValue) noexcept {
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        it->effectiveValue = MakeFloat(clampedValue);
    }
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
    if (IsFastInlinePropertyId(propId)) {
        return false;
    }
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

bool UIElement::SetPropertyExtendedMetadata(
    std::uint16_t propId,
    std::uint16_t secondaryTrackId,
    std::uint16_t contractInputSlot,
    std::uint16_t blendWeightUnorm16,
    std::uint16_t entryMetaFlags) {
    if (IsFastInlinePropertyId(propId)) {
        return false;
    }
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        it->secondaryTrackId = secondaryTrackId;
        it->contractInputSlot = contractInputSlot;
        it->blendWeightUnorm16 = blendWeightUnorm16;
        it->entryMetaFlags = entryMetaFlags;
        return true;
    }
    CompactPropertyEntry entry{};
    entry.propId = propId;
    entry.sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Default);
    entry.secondaryTrackId = secondaryTrackId;
    entry.contractInputSlot = contractInputSlot;
    entry.blendWeightUnorm16 = blendWeightUnorm16;
    entry.entryMetaFlags = entryMetaFlags;
    sparseProps_.insert(it, std::move(entry));
    return true;
}

void UIElement::EnsureRegisteredInValuePoolSet() noexcept {
    if (!registeredInValuePoolSet_) {
        registeredInValuePoolSet_ = true;
        LiveTraceableUIElementRegistry::Instance().Register(this);
    }
}

void UIElement::MarkSparseValuesInPools() const noexcept {
    for (const auto& entry : sparseProps_) {
        if (entry.effectiveValue.isTraceable()) {
            rs_rt::markValueInPools(entry.effectiveValue);
        }
    }
    if (mountedTemplate_ && mountedTemplate_->definition.templatePackage) {
        mountedTemplate_->definition.templatePackage->MarkValuesInPools();
    }
    MarkExtraValuesInPools();
}

void UIElement::MarkSubtreeValuesInPools() const noexcept {
    MarkSparseValuesInPools();
    for (const UIElement* c = firstChild_; c != nullptr; c = c->nextSibling_) {
        c->MarkSubtreeValuesInPools();
    }
}

void Button::SetContentText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string Button::GetContentText() const {
    const auto v = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    const auto contentVal = GetValue(PropId_Content);
    if (const auto* s = std::get_if<StringValue>(&contentVal)) {
        return s->str();
    }
    const auto titleVal = GetValue(PropId_Title);
    if (const auto* s = std::get_if<StringValue>(&titleVal)) {
        return s->str();
    }
    return {};
}

bool Button::PerformClick(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    IntrusivePtr<Button> selfGuard(this, true);
    ++clickGeneration_;
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

bool Button::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    return PerformClick(error);
}

bool Button::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)localPoint;
    (void)modifiers;
    if (button == PointerButton::Left && IsEffectivelyEnabled()) {
        outRequestCapture = true;
        return true;
    }
    outRequestCapture = false;
    return false;
}

bool Button::OnPointerUp(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool insideBounds) {
    (void)localPoint;
    (void)modifiers;
    if (button == PointerButton::Left && insideBounds && IsEffectivelyEnabled()) {
        return ActivateFromInput();
    }
    return false;
}

bool Button::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        return ActivateFromInput();
    }
    return false;
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

// ============================================================================
// P1-1: ControlTemplate Instantiation, Zero-Alloc TemplateBinding & PART_* Lookup
// ============================================================================

namespace {

void CollectTemplateTextBlocksRecursive(
    UIElement* node,
    UIElement* expectedHost,
    std::vector<UIElement*>& outNodes) {
    if (!node) {
        return;
    }
    if (node->TemplatedParent() == expectedHost && node->IsA<TextBlock>()) {
        outNodes.push_back(node);
    }
    for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
        CollectTemplateTextBlocksRecursive(c, expectedHost, outNodes);
    }
}

UIElement* FindTemplateChildRecursiveFallback(
    UIElement* node,
    const UIElement* expectedHost,
    std::string_view partName) noexcept {
    if (!node) {
        return nullptr;
    }
    if (node->TemplatedParent() == expectedHost && node->Name() == partName) {
        return node;
    }
    for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (UIElement* found = FindTemplateChildRecursiveFallback(c, expectedHost, partName)) {
            return found;
        }
    }
    return nullptr;
}

constexpr bool MightCarryTraceableString(std::uint16_t propId) noexcept {
    return propId == PropId_Content ||
           propId == PropId_Text ||
           propId == PropId_Title ||
           propId == PropId_Placeholder ||
           propId == PropId_FontFamily ||
           propId == PropId_Tag ||
           propId == PropId_PathData ||
           propId == PropId_ImageSource;
}

} // namespace

UIElement* UIElement::TemplateRoot() const noexcept {
    return mountedTemplate_ ? mountedTemplate_->templateRoot.Get() : nullptr;
}

const AxbControlTemplate* UIElement::MountedControlTemplate() const noexcept {
    return mountedTemplate_ ? &mountedTemplate_->definition : nullptr;
}

std::string UIElement::GetControlTemplateKey() const {
    if (mountedTemplate_ && !mountedTemplate_->definition.key.empty()) {
        return mountedTemplate_->definition.key;
    }
    const UnifiedValue v = GetValue(PropId_Template);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void UIElement::SetControlTemplateKey(std::string_view templateKey) {
    if (templateKey.empty()) {
        (void)ClearValue(PropId_Template, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_Template, value::FromString(std::string(templateKey)), ValueSourceRank::Local);
    }
}

const std::vector<IntrusivePtr<UIElement>>& UIElement::LogicalTemplateContentChildren() const noexcept {
    static const std::vector<IntrusivePtr<UIElement>> kEmpty;
    return mountedTemplate_ ? mountedTemplate_->logicalChildren : kEmpty;
}

void UIElement::EnsureSparseSlotReserved(std::uint16_t propId) {
    if (IsFastInlinePropertyId(propId)) {
        return;
    }
    if (sparseProps_.capacity() < 8u) {
        sparseProps_.reserve(8u);
    }
    auto it = std::lower_bound(
        sparseProps_.begin(),
        sparseProps_.end(),
        propId,
        [](const CompactPropertyEntry& e, std::uint16_t id) {
            return e.propId < id;
        });
    if (it != sparseProps_.end() && it->propId == propId) {
        it->entryMetaFlags |= kEntryMeta_PinnedTemplateSlot;
        return;
    }
    CompactPropertyEntry entry{};
    entry.propId = propId;
    entry.sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Default);
    entry.entryMetaFlags = kEntryMeta_PinnedTemplateSlot;
    sparseProps_.insert(it, std::move(entry));
}

void UIElement::RegisterTemplateNamedPart(std::string_view partName, UIElement* partElement) {
    if (!mountedTemplate_ || partName.empty() || !partElement) {
        return;
    }
    for (auto& existing : mountedTemplate_->namedParts) {
        if (existing.name == partName) {
            existing.element = partElement;
            return;
        }
    }
    mountedTemplate_->namedParts.push_back(TemplatePartEntry{std::string(partName), partElement});
}

void UIElement::RegisterTemplateContentPresenter(ContentPresenter* presenter) {
    if (!mountedTemplate_ || !presenter) {
        return;
    }
    if (std::find(
            mountedTemplate_->contentPresenters.begin(),
            mountedTemplate_->contentPresenters.end(),
            presenter) == mountedTemplate_->contentPresenters.end()) {
        mountedTemplate_->contentPresenters.push_back(presenter);
    }
}

void UIElement::AddTemplateBinding(
    UIElement* target,
    std::uint16_t sourcePropId,
    std::uint16_t targetPropId,
    ValueConverterKind converter) {
    if (!mountedTemplate_ || !target) {
        return;
    }
    EnsureSparseSlotReserved(sourcePropId);
    if (sourcePropId == PropId_Content || sourcePropId == PropId_Text) {
        EnsureSparseSlotReserved(PropId_Content);
        EnsureSparseSlotReserved(PropId_Text);
    }
    target->EnsureSparseSlotReserved(targetPropId);
    if (MightCarryTraceableString(sourcePropId) || MightCarryTraceableString(targetPropId)) {
        EnsureRegisteredInValuePoolSet();
        target->EnsureRegisteredInValuePoolSet();
    }

    for (auto& existing : mountedTemplate_->bindings) {
        if (existing.target == target && existing.targetPropId == targetPropId) {
            existing.sourcePropId = sourcePropId;
            existing.converter = converter;
            return;
        }
    }
    mountedTemplate_->bindings.push_back(TemplateBindingLink{
        target,
        sourcePropId,
        targetPropId,
        converter});
}

bool UIElement::ReadTemplateSourceValueZeroAlloc(
    std::uint16_t sourcePropId,
    std::uint16_t changedPropId,
    UnifiedValue& outVal) const noexcept {
    switch (sourcePropId) {
        case PropId_Width:
            if (fastProps_.width < 0.0f) return false;
            outVal = MakeFloat(fastProps_.width);
            return true;
        case PropId_Height:
            if (fastProps_.height < 0.0f) return false;
            outVal = MakeFloat(fastProps_.height);
            return true;
        case PropId_MinWidth:
            if (GetPropertySourceRank(PropId_MinWidth) == ValueSourceRank::Default) return false;
            outVal = MakeFloat(fastProps_.minWidth);
            return true;
        case PropId_MinHeight:
            if (GetPropertySourceRank(PropId_MinHeight) == ValueSourceRank::Default) return false;
            outVal = MakeFloat(fastProps_.minHeight);
            return true;
        case PropId_Margin:
            if (GetPropertySourceRank(PropId_Margin) == ValueSourceRank::Default) return false;
            outVal = MakeFloat4InlineFast(GetMargin().ToFloat4());
            return true;
        case PropId_Padding:
            if (GetPropertySourceRank(PropId_Padding) == ValueSourceRank::Default) return false;
            outVal = MakeFloat4InlineFast(GetPadding().ToFloat4());
            return true;
        case PropId_Opacity:
            if (GetPropertySourceRank(PropId_Opacity) == ValueSourceRank::Default) return false;
            outVal = MakeFloat(fastProps_.opacity);
            return true;
        case PropId_Visibility:
            if (GetPropertySourceRank(PropId_Visibility) == ValueSourceRank::Default) return false;
            outVal = value::FromInt(static_cast<std::int64_t>(fastProps_.visibility));
            return true;
        case PropId_HorizontalAlignment:
            if (GetPropertySourceRank(PropId_HorizontalAlignment) == ValueSourceRank::Default) return false;
            outVal = value::FromInt(static_cast<std::int64_t>(GetHorizontalAlignment()));
            return true;
        case PropId_VerticalAlignment:
            if (GetPropertySourceRank(PropId_VerticalAlignment) == ValueSourceRank::Default) return false;
            outVal = value::FromInt(static_cast<std::int64_t>(GetVerticalAlignment()));
            return true;
        case PropId_IsEnabled:
            outVal = UnifiedValue{IsEnabled()};
            return true;
        case PropId_Focusable:
            outVal = UnifiedValue{IsFocusable()};
            return true;
        default:
            break;
    }

    auto tryReadSparse = [&](std::uint16_t pid) noexcept -> bool {
        if (const auto* e = FindCompactEntry(pid)) {
            if (!std::holds_alternative<std::monostate>(e->effectiveValue) &&
                e->sourceRank != static_cast<std::uint8_t>(ValueSourceRank::Default)) {
                outVal = e->effectiveValue;
                return true;
            }
        }
        return false;
    };

    if (sourcePropId == PropId_Content || sourcePropId == PropId_Text) {
        if (changedPropId == PropId_Content) {
            if (tryReadSparse(PropId_Content)) return true;
            if (tryReadSparse(PropId_Text)) return true;
            return false;
        }
        if (changedPropId == PropId_Text) {
            if (tryReadSparse(PropId_Text)) return true;
            if (tryReadSparse(PropId_Content)) return true;
            return false;
        }
        if (tryReadSparse(PropId_Content)) return true;
        if (tryReadSparse(PropId_Text)) return true;
        if (tryReadSparse(PropId_Title)) return true;
        return false;
    }

    return tryReadSparse(sourcePropId);
}

void UIElement::SyncTemplateBindingsForProperty(std::uint16_t changedPropId) noexcept {
    if (!mountedTemplate_) {
        return;
    }

    const bool isContentGroup =
        (changedPropId == PropId_Content || changedPropId == PropId_Text || changedPropId == PropId_Title);

    for (const auto& link : mountedTemplate_->bindings) {
        if (!link.target) {
            continue;
        }
        const bool linkMatches = (link.sourcePropId == changedPropId) ||
            (isContentGroup && (link.sourcePropId == PropId_Content || link.sourcePropId == PropId_Text));
        if (!linkMatches) {
            continue;
        }

        UnifiedValue srcVal{};
        if (ReadTemplateSourceValueZeroAlloc(link.sourcePropId, changedPropId, srcVal)) {
            if (link.converter != ValueConverterKind::None) {
                srcVal = ApplyBuiltinValueConverter(link.converter, srcVal, false);
            }
            (void)link.target->SetValue(link.targetPropId, srcVal, ValueSourceRank::ThemeStyle);
        } else {
            (void)link.target->ClearValue(link.targetPropId, ValueSourceRank::ThemeStyle);
        }
    }

    // Notify ContentPresenters & inherited TextBlocks in template subtree (0 heap allocs!)
    if (isContentGroup ||
        changedPropId == PropId_Foreground ||
        changedPropId == PropId_FontSize ||
        changedPropId == PropId_FontFamily ||
        changedPropId == PropId_TextWrapping ||
        changedPropId == PropId_HorizontalContentAlignment ||
        changedPropId == PropId_VerticalContentAlignment) {
        const bool affectsMetrics =
            isContentGroup ||
            changedPropId == PropId_FontSize ||
            changedPropId == PropId_FontFamily ||
            changedPropId == PropId_TextWrapping ||
            changedPropId == PropId_HorizontalContentAlignment ||
            changedPropId == PropId_VerticalContentAlignment;
        for (ContentPresenter* cp : mountedTemplate_->contentPresenters) {
            if (!cp) continue;
            if (affectsMetrics) {
                cp->InvalidateLayout();
            } else {
                cp->InvalidateRender();
            }
        }
        if (changedPropId == PropId_FontSize ||
            changedPropId == PropId_FontFamily ||
            changedPropId == PropId_Foreground) {
            for (UIElement* tbNode : mountedTemplate_->inheritedTextNodes) {
                if (!tbNode) continue;
                if (changedPropId == PropId_Foreground) {
                    tbNode->InvalidateRender();
                } else {
                    tbNode->InvalidateLayout();
                }
            }
        }
    }

    if (changedPropId == PropId_Value || changedPropId == PropId_Minimum || changedPropId == PropId_Maximum) {
        if (auto* slider = DynamicCast<Slider>()) {
            slider->SyncTemplateParts();
        }
    }
}

void UIElement::SyncTemplateBindingsForTarget(UIElement* target, std::uint16_t targetPropId) noexcept {
    if (!mountedTemplate_ || !target) {
        return;
    }
    for (const auto& link : mountedTemplate_->bindings) {
        if (link.target != target || link.targetPropId != targetPropId) {
            continue;
        }
        UnifiedValue srcVal{};
        if (ReadTemplateSourceValueZeroAlloc(link.sourcePropId, 0xFFFFu, srcVal)) {
            if (link.converter != ValueConverterKind::None) {
                srcVal = ApplyBuiltinValueConverter(link.converter, srcVal, false);
            }
            (void)link.target->SetValue(link.targetPropId, srcVal, ValueSourceRank::ThemeStyle);
        } else {
            (void)link.target->ClearValue(link.targetPropId, ValueSourceRank::ThemeStyle);
        }
    }
}

void UIElement::RefreshAllTemplateBindings() noexcept {
    if (!mountedTemplate_) {
        return;
    }
    for (const auto& link : mountedTemplate_->bindings) {
        if (!link.target) {
            continue;
        }
        UnifiedValue srcVal{};
        if (ReadTemplateSourceValueZeroAlloc(link.sourcePropId, 0xFFFFu, srcVal)) {
            if (link.converter != ValueConverterKind::None) {
                srcVal = ApplyBuiltinValueConverter(link.converter, srcVal, false);
            }
            (void)link.target->SetValue(link.targetPropId, srcVal, ValueSourceRank::ThemeStyle);
        }
    }
    for (ContentPresenter* cp : mountedTemplate_->contentPresenters) {
        if (cp) {
            cp->InvalidateLayout();
        }
    }
    if (auto* slider = DynamicCast<Slider>()) {
        slider->SyncTemplateParts();
    }
}

UIElement* UIElement::GetTemplateChild(std::string_view partName) const noexcept {
    if (!mountedTemplate_ || partName.empty()) {
        return nullptr;
    }
    for (const auto& entry : mountedTemplate_->namedParts) {
        if (entry.name == partName) {
            return entry.element;
        }
    }
    return FindTemplateChildRecursiveFallback(
        mountedTemplate_->templateRoot.Get(),
        this,
        partName);
}

void UIElement::ClearControlTemplate() {
    if (!mountedTemplate_) {
        return;
    }
    std::vector<IntrusivePtr<UIElement>> savedLogical = std::move(mountedTemplate_->logicalChildren);
    for (ContentPresenter* cp : mountedTemplate_->contentPresenters) {
        if (!cp) continue;
        for (const auto& lc : savedLogical) {
            if (lc && lc->Parent() == cp) {
                (void)cp->RemoveChild(lc.Get(), boundRegistry_, boundUiHeap_);
            }
        }
    }
    if (mountedTemplate_->templateRoot) {
        (void)RemoveChild(mountedTemplate_->templateRoot.Get(), boundRegistry_, boundUiHeap_);
    }
    mountedTemplate_.reset();
    for (const auto& lc : savedLogical) {
        if (lc) {
            (void)AddChild(lc.Get(), boundRegistry_, boundUiHeap_);
        }
    }
    InvalidateLayout();
    InvalidateRender();
}

bool UIElement::ApplyControlTemplateByKey(std::string_view templateKey) {
    if (templateKey.empty()) {
        ClearControlTemplate();
        return false;
    }
    if (const AxbControlTemplate* ct = AxbTemplateRegistry::Instance().FindControlTemplate(templateKey)) {
        ExecutionDomainContext* domains = boundBindingEngine_ ? &boundBindingEngine_->Domains() : nullptr;
        AnimationEngine* anim = boundBindingEngine_ ? boundBindingEngine_->GetAnimationEngine() : nullptr;
        return ApplyControlTemplate(*ct, domains, boundBindingEngine_, BoundScriptWrapper(), nullptr, nullptr, anim);
    }
    return false;
}

bool UIElement::ApplyControlTemplate(
    const AxbControlTemplate& controlTemplate,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    if (applyingControlTemplate_ || !controlTemplate.templatePackage ||
        controlTemplate.templatePackage->instructions.empty()) {
        return false;
    }

    // Prevent infinite self-template recursion when a template contains a child of the same control type
    for (const UIElement* anc = templatedParent_; anc != nullptr; anc = anc->TemplatedParent()) {
        if (const AxbControlTemplate* ancTpl = anc->MountedControlTemplate()) {
            if (ancTpl->templatePackage.get() == controlTemplate.templatePackage.get() ||
                (!controlTemplate.key.empty() && ancTpl->key == controlTemplate.key)) {
                return false;
            }
        }
    }

    // Idempotent fast path if this exact ControlTemplate is already mounted on this host
    if (mountedTemplate_ &&
        (mountedTemplate_->definition.templatePackage.get() == controlTemplate.templatePackage.get() ||
         (!controlTemplate.key.empty() && mountedTemplate_->definition.key == controlTemplate.key &&
          mountedTemplate_->templateRoot.Get() != nullptr))) {
        mountedTemplate_->definition.triggers = controlTemplate.triggers;
        mountedTemplate_->definition.eventTriggers = controlTemplate.eventTriggers;
        mountedTemplate_->definition.visualStateGroups = controlTemplate.visualStateGroups;
        RefreshAllTemplateBindings();
        return true;
    }

    applyingControlTemplate_ = true;

    // 1. Preserve non-template logical children (e.g. graphical <Button><Path/></Button> content)
    std::vector<IntrusivePtr<UIElement>> savedLogical;
    if (mountedTemplate_) {
        savedLogical = std::move(mountedTemplate_->logicalChildren);
        for (ContentPresenter* cp : mountedTemplate_->contentPresenters) {
            if (!cp) continue;
            for (const auto& lc : savedLogical) {
                if (lc && lc->Parent() == cp) {
                    (void)cp->RemoveChild(lc.Get(), boundRegistry_, boundUiHeap_);
                }
            }
        }
        if (mountedTemplate_->templateRoot) {
            (void)RemoveChild(mountedTemplate_->templateRoot.Get(), boundRegistry_, boundUiHeap_);
        }
        mountedTemplate_.reset();
    } else {
        while (firstChild_ != nullptr) {
            IntrusivePtr<UIElement> childRef(firstChild_, true);
            (void)RemoveChild(firstChild_, boundRegistry_, boundUiHeap_);
            savedLogical.push_back(std::move(childRef));
        }
    }

    // 2. Instantiate root element of the template visual subtree
    const AxbPackage& tplPkg = *controlTemplate.templatePackage;
    const AxbInstruction& firstInst = tplPkg.instructions[0];
    if (firstInst.opcode != AxbOpcode::BeginElement) {
        applyingControlTemplate_ = false;
        return false;
    }
    const std::string_view rootTypeName = tplPkg.GetString(firstInst.strIdx0);
    IntrusivePtr<UIElement> tplRoot = CreateElementByTypeName(rootTypeName);
    if (!tplRoot) {
        applyingControlTemplate_ = false;
        return false;
    }

    auto data = std::make_unique<MountedControlTemplateData>();
    data->definition = controlTemplate;
    data->templateRoot = tplRoot;
    data->logicalChildren = std::move(savedLogical);
    mountedTemplate_ = std::move(data);

    tplRoot->SetTemplatedParent(this);
    NativeHandleRegistry* reg = domains ? &domains->nativeHandles() : boundRegistry_;
    ManagedHeap* heap = domains ? &domains->uiHeap() : boundUiHeap_;
    (void)AddChild(tplRoot.Get(), reg, heap);

    // Pre-reserve common host properties so runtime SetValue on host never reallocates sparseProps_
    for (const std::uint16_t commonProp : {
             PropId_Background,
             PropId_Foreground,
             PropId_BorderBrush,
             PropId_BorderThickness,
             PropId_CornerRadius,
             PropId_Content,
             PropId_Text,
             PropId_FontSize,
             PropId_FontFamily}) {
        EnsureSparseSlotReserved(commonProp);
    }
    EnsureRegisteredInValuePoolSet();

    // 3. Mount template instructions, wiring TemplatedParent, BindTemplateParent, PART_* & ContentPresenter
    const bool mountedOk = MountControlTemplatePackage(
        this,
        tplRoot.Get(),
        tplPkg,
        domains,
        bindingEngine ? bindingEngine : boundBindingEngine_,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);

    if (mountedOk && mountedTemplate_) {
        // Host logical UIElement content inside the primary ContentPresenter if present
        if (!mountedTemplate_->logicalChildren.empty() && !mountedTemplate_->contentPresenters.empty()) {
            ContentPresenter* primaryCp = mountedTemplate_->contentPresenters.front();
            for (const auto& lc : mountedTemplate_->logicalChildren) {
                if (lc && primaryCp) {
                    (void)primaryCp->AddChild(lc.Get(), reg, heap);
                }
            }
        }
        CollectTemplateTextBlocksRecursive(tplRoot.Get(), this, mountedTemplate_->inheritedTextNodes);
        RefreshAllTemplateBindings();
    }

    applyingControlTemplate_ = false;
    InvalidateLayout();
    InvalidateRender();
    return mountedOk;
}

} // namespace aero
