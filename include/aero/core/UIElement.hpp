#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

struct AxbPackage;
struct AxbControlTemplate;
enum class ValueConverterKind : std::uint8_t;
class ContentPresenter;
class DualTrackBindingEngine;
class AnimationEngine;

struct UIEffectDescriptor {
    UIEffectKind kind = UIEffectKind::None;
    float blurRadius = 0.0f;
    float shadowDepth = 5.0f;
    float directionDeg = 315.0f;
    float opacity = 1.0f;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

template <typename T>
class IntrusivePtr {
public:
    constexpr IntrusivePtr() noexcept = default;
    constexpr IntrusivePtr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    explicit IntrusivePtr(T* p, bool addRef = true) noexcept : ptr_(p) {
        if (ptr_ && addRef) {
            ptr_->AddRef();
        }
    }
    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    IntrusivePtr(const IntrusivePtr<U>& other) noexcept : ptr_(other.Get()) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }
    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    ~IntrusivePtr() {
        Reset();
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            Reset(other.ptr_);
        }
        return *this;
    }
    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->Release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void Reset(T* p = nullptr, bool addRef = true) noexcept {
        if (p && addRef) {
            p->AddRef();
        }
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = p;
    }

    [[nodiscard]] T* Get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

template <typename T, typename... Args>
[[nodiscard]] IntrusivePtr<T> MakeIntrusive(Args&&... args) {
    return IntrusivePtr<T>(new T(std::forward<Args>(args)...), true);
}

class UIElement {
public:
    static constexpr std::uint32_t InvalidRenderNodeId = 0xFFFFFFFFu;

    UIElement();
    virtual ~UIElement();

    [[nodiscard]] static std::uint64_t TotalConstructedCount() noexcept;
    [[nodiscard]] static std::uint64_t TotalDestroyedCount() noexcept;
    [[nodiscard]] static std::uint64_t LiveInstanceCount() noexcept;

    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    // Intrusive reference counting
    void AddRef() const noexcept {
        refCount_.fetch_add(1u, std::memory_order_relaxed);
    }
    void Release() const noexcept {
        if (refCount_.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            delete this;
        }
    }
    [[nodiscard]] std::uint32_t RefCount() const noexcept {
        return refCount_.load(std::memory_order_relaxed);
    }

    // Static TypeClass & O(1) Euler-tour RTTI
    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] virtual const TypeClass* GetTypeClass() const noexcept {
        return StaticTypeClass();
    }
    [[nodiscard]] bool IsA(const TypeClass* target) const noexcept {
        return GetTypeClass()->IsDerivedFrom(target);
    }
    template <typename T>
    [[nodiscard]] bool IsA() const noexcept {
        return IsA(T::StaticTypeClass());
    }
    template <typename T>
    [[nodiscard]] T* DynamicCast() noexcept {
        return IsA<T>() ? static_cast<T*>(this) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T* DynamicCast() const noexcept {
        return IsA<T>() ? static_cast<const T*>(this) : nullptr;
    }

    // Pure intrusive visual tree (0 std::vector allocation for child links)
    void SetAsRootVisual(
        bool isRoot,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr) noexcept;
    [[nodiscard]] bool IsConnectedToRoot() const noexcept {
        return isConnectedToRoot_;
    }

    bool AddChild(
        UIElement* child,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);
    bool InsertChildAt(
        UIElement* child,
        std::size_t index,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);
    void ReorderExistingChildren(
        UIElement* const* orderedChildren,
        std::size_t count) noexcept;
    bool RemoveChild(
        UIElement* child,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);
    [[nodiscard]] std::size_t ChildCount() const noexcept;

    [[nodiscard]] UIElement* Parent() const noexcept { return parent_; }
    [[nodiscard]] UIElement* FirstChild() const noexcept { return firstChild_; }
    [[nodiscard]] UIElement* LastChild() const noexcept { return lastChild_; }
    [[nodiscard]] UIElement* NextSibling() const noexcept { return nextSibling_; }
    [[nodiscard]] UIElement* PrevSibling() const noexcept { return prevSibling_; }

    // Zero-hash O(1) NativeHandle conversion & generational invalidation
    [[nodiscard]] NativeHandle ToNativeHandle(NativeHandleRegistry& registry);
    [[nodiscard]] static UIElement* FromNativeHandle(
        const NativeHandleRegistry& registry,
        NativeHandle handle) noexcept;

    // P1-4: Bind a RealScript wrapper object on UiHeap and automatically keep it
    // alive via PersistentRoot while this UIElement is connected to the root visual tree.
    bool BindScriptWrapper(
        NativeHandleRegistry& registry,
        ManagedHeap& uiHeap,
        ObjectRef wrapperRef);
    void SyncVisualTreeGcRoots(
        NativeHandleRegistry& registry,
        ManagedHeap& uiHeap) noexcept;
    [[nodiscard]] bool HasPinnedScriptWrapper() const noexcept {
        return pinnedWrapperRoot_.valid();
    }
    [[nodiscard]] ObjectRef BoundScriptWrapper() const noexcept {
        return boundWrapperRef_;
    }

    // Element x:Name and O(N) subtree lookup (only for dynamic debugging; compiled code uses O(1) slots)
    [[nodiscard]] const std::string& Name() const noexcept { return name_; }
    void SetName(std::string name) noexcept { name_ = std::move(name); }
    [[nodiscard]] UIElement* FindName(std::string_view targetName) noexcept;

    // Layout geometry & Measure/Arrange lifecycle
    void Measure(Size availableSize) noexcept;
    void Arrange(Rect finalRect) noexcept;
    [[nodiscard]] UIElement* HitTest(Point point) noexcept;
    virtual Size MeasureCore(Size availableSize) noexcept;
    virtual void ArrangeCore(Rect finalRect) noexcept;
    virtual void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue);

    // Coordinate space conversions across ancestor 2D/3D transforms & Viewbox scaling (0 heap allocs)
    [[nodiscard]] Point ScreenToLocal(Point screenPoint) const noexcept;
    [[nodiscard]] Rect LocalToScreenBounds(Rect localRect) const noexcept;

    // Container-applied child coordinate transform (used by Viewbox uniform/fill scaling)
    [[nodiscard]] virtual Matrix2D GetChildContainerTransform2D() const noexcept {
        return Matrix2D::Identity();
    }

    // Container local-space clip bounds when ClipToBounds is active (ScrollViewer insets by Padding)
    [[nodiscard]] virtual Rect GetLocalClipBounds() const noexcept {
        return finalRect_;
    }

    [[nodiscard]] Size DesiredSize() const noexcept { return desiredSize_; }
    [[nodiscard]] Rect FinalRect() const noexcept { return finalRect_; }
    [[nodiscard]] Rect LastArrangeSlot() const noexcept { return lastArrangeSlot_; }
    [[nodiscard]] std::uint16_t TreeDepth() const noexcept { return treeDepth_; }

    // Zero-allocation inline interactive visual & focus states (packed in FastInlineStorage::reserved)
    [[nodiscard]] bool IsEnabled() const noexcept {
        return (fastProps_.reserved & InteractiveState_Disabled) == 0u;
    }
    void SetIsEnabled(bool enabled) noexcept {
        fastInlineRanks_[10] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        const std::uint8_t next = enabled
            ? static_cast<std::uint8_t>(fastProps_.reserved & ~InteractiveState_Disabled)
            : static_cast<std::uint8_t>(fastProps_.reserved | InteractiveState_Disabled);
        if (fastProps_.reserved != next) {
            fastProps_.reserved = next;
            PropagateEnabledStateToSubtree();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_IsEnabled);
        }
    }
    [[nodiscard]] bool IsEffectivelyEnabled() const noexcept {
        for (const UIElement* cur = this; cur != nullptr; cur = cur->parent_) {
            if (!cur->IsEnabled()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsHovered() const noexcept {
        return (fastProps_.reserved & InteractiveState_Hovered) != 0u;
    }
    void SetHovered(bool hovered) noexcept {
        const std::uint8_t next = hovered
            ? static_cast<std::uint8_t>(fastProps_.reserved | InteractiveState_Hovered)
            : static_cast<std::uint8_t>(fastProps_.reserved & ~InteractiveState_Hovered);
        if (fastProps_.reserved != next) {
            fastProps_.reserved = next;
            InvalidateRender();
        }
    }

    [[nodiscard]] bool IsPressed() const noexcept {
        return (fastProps_.reserved & InteractiveState_Pressed) != 0u;
    }
    void SetPressed(bool pressed) noexcept {
        const std::uint8_t next = pressed
            ? static_cast<std::uint8_t>(fastProps_.reserved | InteractiveState_Pressed)
            : static_cast<std::uint8_t>(fastProps_.reserved & ~InteractiveState_Pressed);
        if (fastProps_.reserved != next) {
            fastProps_.reserved = next;
            if (!pressed) {
                OnPointerCaptureLost();
            }
            InvalidateRender();
        } else if (!pressed) {
            OnPointerCaptureLost();
        }
    }

    [[nodiscard]] bool IsFocused() const noexcept {
        return (fastProps_.reserved & InteractiveState_Focused) != 0u;
    }
    void SetFocused(bool focused) noexcept {
        const std::uint8_t next = focused
            ? static_cast<std::uint8_t>(fastProps_.reserved | InteractiveState_Focused)
            : static_cast<std::uint8_t>(fastProps_.reserved & ~InteractiveState_Focused);
        if (fastProps_.reserved != next) {
            fastProps_.reserved = next;
            InvalidateRender();
            OnFocusChanged(focused);
        }
    }

    [[nodiscard]] virtual bool DefaultFocusable() const noexcept { return false; }
    [[nodiscard]] bool IsFocusable() const noexcept {
        if ((fastProps_.reserved & InteractiveState_FocusableExplicit) != 0u) {
            return (fastProps_.reserved & InteractiveState_FocusableValue) != 0u;
        }
        return DefaultFocusable();
    }
    void SetFocusable(bool focusable) noexcept {
        fastInlineRanks_[11] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        std::uint8_t next = static_cast<std::uint8_t>(
            fastProps_.reserved | InteractiveState_FocusableExplicit);
        if (focusable) {
            next = static_cast<std::uint8_t>(next | InteractiveState_FocusableValue);
        } else {
            next = static_cast<std::uint8_t>(next & ~InteractiveState_FocusableValue);
        }
        fastProps_.reserved = next;
        if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Focusable);
    }
    void ClearExplicitFocusable() noexcept {
        fastInlineRanks_[11] = static_cast<std::uint8_t>(ValueSourceRank::Default);
        fastProps_.reserved = static_cast<std::uint8_t>(
            fastProps_.reserved & ~(InteractiveState_FocusableExplicit | InteractiveState_FocusableValue));
    }
    [[nodiscard]] bool CanReceiveFocus() const noexcept;

    // Automatic TwoWay binding write-back linkage
    void BindTwoWayEngine(DualTrackBindingEngine* engine) noexcept {
        boundBindingEngine_ = engine;
    }
    [[nodiscard]] DualTrackBindingEngine* BoundTwoWayEngine() const noexcept {
        return boundBindingEngine_;
    }
    bool NotifyTwoWayPropertyMutated(std::uint16_t propId, RuntimeError* error = nullptr);

    // Virtual zero-RTTI input dispatch hooks invoked by InputEventRouter
    [[nodiscard]] virtual bool IsInteractiveControl() const noexcept {
        return DefaultFocusable();
    }
    virtual bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture);
    virtual bool OnPointerMove(Point localPoint, std::uint8_t modifiers);
    virtual bool OnPointerUp(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool insideBounds);
    virtual bool OnKeyDown(KeyCode key, std::uint8_t modifiers);
    virtual bool OnTextInput(std::string_view utf8Text);
    virtual void OnFocusChanged(bool focused);
    virtual void OnPointerCaptureLost() noexcept;
    virtual void OnEnabledStateChanged(bool effectivelyEnabled);

    // O(1) PagedRenderTree linkage (eliminates O(N) sourceElement linear scans)
    void BindRenderNodeSpan(
        std::uint32_t renderTreeId,
        std::uint32_t firstNodeId,
        std::uint16_t nodeSpan) const noexcept {
        renderTreeId_ = renderTreeId;
        renderNodeId_ = firstNodeId;
        renderNodeSpan_ = nodeSpan;
    }
    void DetachRenderNodeSpan() const noexcept {
        renderTreeId_ = 0u;
        renderNodeId_ = InvalidRenderNodeId;
        renderNodeSpan_ = 0u;
    }
    [[nodiscard]] std::uint32_t RenderTreeId() const noexcept { return renderTreeId_; }
    [[nodiscard]] std::uint32_t RenderNodeId() const noexcept { return renderNodeId_; }
    [[nodiscard]] std::uint16_t RenderNodeSpan() const noexcept { return renderNodeSpan_; }

    // Fast Inline Properties (0 heap allocation fast path)
    [[nodiscard]] float GetWidth() const noexcept { return fastProps_.width; }
    void SetWidth(float v) noexcept {
        fastInlineRanks_[0] = static_cast<std::uint8_t>(
            v < 0.0f ? ValueSourceRank::Default : ValueSourceRank::Local);
        if (fastProps_.width != v) {
            fastProps_.width = v;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Width);
        }
    }

    [[nodiscard]] float GetHeight() const noexcept { return fastProps_.height; }
    void SetHeight(float v) noexcept {
        fastInlineRanks_[1] = static_cast<std::uint8_t>(
            v < 0.0f ? ValueSourceRank::Default : ValueSourceRank::Local);
        if (fastProps_.height != v) {
            fastProps_.height = v;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Height);
        }
    }

    [[nodiscard]] float GetMinWidth() const noexcept { return fastProps_.minWidth; }
    void SetMinWidth(float v) noexcept {
        fastInlineRanks_[2] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        if (fastProps_.minWidth != v) {
            fastProps_.minWidth = v;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_MinWidth);
        }
    }

    [[nodiscard]] float GetMinHeight() const noexcept { return fastProps_.minHeight; }
    void SetMinHeight(float v) noexcept {
        fastInlineRanks_[3] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        if (fastProps_.minHeight != v) {
            fastProps_.minHeight = v;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_MinHeight);
        }
    }

    [[nodiscard]] float GetMaxWidth() const noexcept;
    void SetMaxWidth(float v);

    [[nodiscard]] float GetMaxHeight() const noexcept;
    void SetMaxHeight(float v);

    [[nodiscard]] HorizontalAlignment GetHorizontalAlignment() const noexcept {
        return static_cast<HorizontalAlignment>(fastProps_.alignmentFlags & 0x03u);
    }
    void SetHorizontalAlignment(HorizontalAlignment align) noexcept {
        fastInlineRanks_[8] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        const auto raw = static_cast<std::uint8_t>(align) & 0x03u;
        const auto next = static_cast<std::uint8_t>((fastProps_.alignmentFlags & ~0x03u) | raw);
        if (fastProps_.alignmentFlags != next) {
            fastProps_.alignmentFlags = next;
            InvalidateLayout();
            if (parent_) {
                parent_->InvalidateArrange();
            }
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_HorizontalAlignment);
        }
    }

    [[nodiscard]] VerticalAlignment GetVerticalAlignment() const noexcept {
        return static_cast<VerticalAlignment>((fastProps_.alignmentFlags >> 2u) & 0x03u);
    }
    void SetVerticalAlignment(VerticalAlignment align) noexcept {
        fastInlineRanks_[9] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        const auto raw = static_cast<std::uint8_t>((static_cast<std::uint8_t>(align) & 0x03u) << 2u);
        const auto next = static_cast<std::uint8_t>((fastProps_.alignmentFlags & ~0x0Cu) | raw);
        if (fastProps_.alignmentFlags != next) {
            fastProps_.alignmentFlags = next;
            InvalidateLayout();
            if (parent_) {
                parent_->InvalidateArrange();
            }
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_VerticalAlignment);
        }
    }

    // Full 2D Affine RenderTransform, LayoutTransform & normalized RenderTransformOrigin
    void SetRenderTransform(const Matrix2D& matrix);
    [[nodiscard]] Matrix2D GetRenderTransform() const noexcept;
    void ClearRenderTransform();

    void SetLayoutTransform(const Matrix2D& matrix);
    [[nodiscard]] Matrix2D GetLayoutTransform() const noexcept;
    void ClearLayoutTransform();

    void SetRenderTransformOrigin(Point origin);
    [[nodiscard]] Point GetRenderTransformOrigin() const noexcept;

    // Animated & decomposed 2D transform channel getters
    [[nodiscard]] float GetTranslateX() const noexcept {
        if (const auto* rt = FindCompactEntry(PropId_RenderTransformTranslation);
            rt && rt->sourceRank == static_cast<std::uint8_t>(ValueSourceRank::Animation)) {
            Float4Value f4{};
            if (rt->effectiveValue.tryGetFloat4(f4)) return f4.x;
        }
        float v = 0.0f;
        if (value::TryGetFloat(GetValue(PropId_AnimTranslateX), v)) return v;
        return GetRenderTransform().tx;
    }
    [[nodiscard]] float GetTranslateY() const noexcept {
        if (const auto* rt = FindCompactEntry(PropId_RenderTransformTranslation);
            rt && rt->sourceRank == static_cast<std::uint8_t>(ValueSourceRank::Animation)) {
            Float4Value f4{};
            if (rt->effectiveValue.tryGetFloat4(f4)) return f4.y;
        }
        float v = 0.0f;
        if (value::TryGetFloat(GetValue(PropId_AnimTranslateY), v)) return v;
        return GetRenderTransform().ty;
    }
    [[nodiscard]] float GetScaleX() const noexcept {
        float v = 1.0f;
        (void)value::TryGetFloat(GetValue(PropId_AnimScaleX), v);
        return v;
    }
    [[nodiscard]] float GetScaleY() const noexcept {
        float v = 1.0f;
        (void)value::TryGetFloat(GetValue(PropId_AnimScaleY), v);
        return v;
    }
    [[nodiscard]] float GetRotateAngle() const noexcept {
        float v = 0.0f;
        (void)value::TryGetFloat(GetValue(PropId_AnimRotateAngle), v);
        return v;
    }
    [[nodiscard]] float GetSkewX() const noexcept {
        float v = 0.0f;
        (void)value::TryGetFloat(GetValue(PropId_AnimSkewX), v);
        return v;
    }
    [[nodiscard]] float GetSkewY() const noexcept {
        float v = 0.0f;
        (void)value::TryGetFloat(GetValue(PropId_AnimSkewY), v);
        return v;
    }

    // Effective layout-space 2D affine matrix combining LayoutTransform, RenderTransform + RenderTransformOrigin,
    // legacy PropId_RenderTransformMatrix, and Projection3D Yaw/Pitch around FinalRect center.
    [[nodiscard]] Matrix2D ComputeLocalEffectiveTransform2D() const noexcept;
    [[nodiscard]] Matrix2D ComputeLocalEffectiveTransform2DForRect(const Rect& rectOverride) const noexcept;

    // P1-5: True 3D Perspective Transform (CompositeTransform3D / PlaneProjection) & Effects
    void SetTransform3D(const CompositeTransform3DParams& params);
    [[nodiscard]] CompositeTransform3DParams GetTransform3D() const noexcept;
    void ClearTransform3D();
    [[nodiscard]] bool HasActiveTransform3D() const noexcept;
    [[nodiscard]] bool HasAncestorOrSelfTransform3D() const noexcept;
    [[nodiscard]] Matrix4x4 ComputeLocalEffectiveTransform3D() const noexcept;
    [[nodiscard]] Matrix4x4 ComputeLocalEffectiveTransform3DForRect(const Rect& rectOverride) const noexcept;
    [[nodiscard]] Matrix4x4 ComputeLocalToScreenMatrix3D() const noexcept;

    void SetBlurEffect(float radiusPx);
    void SetDropShadowEffect(
        float blurRadiusPx,
        float shadowDepthPx = 5.0f,
        float directionDeg = 315.0f,
        const Color& color = Color{0.0f, 0.0f, 0.0f, 1.0f},
        float opacity = 1.0f);
    void ClearEffect();
    [[nodiscard]] UIEffectKind GetEffectKind() const noexcept;
    [[nodiscard]] UIEffectDescriptor GetEffectDescriptor() const noexcept;

    [[nodiscard]] Thickness GetMargin() const noexcept {
        return Thickness{
            fastProps_.marginL,
            fastProps_.marginT,
            fastProps_.marginR,
            fastProps_.marginB};
    }
    void SetMargin(const Thickness& m) noexcept {
        fastInlineRanks_[4] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        if (GetMargin() != m) {
            fastProps_.marginL = m.left;
            fastProps_.marginT = m.top;
            fastProps_.marginR = m.right;
            fastProps_.marginB = m.bottom;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Margin);
        }
    }

    [[nodiscard]] Thickness GetPadding() const noexcept {
        return Thickness{
            fastProps_.paddingL,
            fastProps_.paddingT,
            fastProps_.paddingR,
            fastProps_.paddingB};
    }
    void SetPadding(const Thickness& p) noexcept {
        fastInlineRanks_[5] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        if (GetPadding() != p) {
            fastProps_.paddingL = p.left;
            fastProps_.paddingT = p.top;
            fastProps_.paddingR = p.right;
            fastProps_.paddingB = p.bottom;
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Padding);
        }
    }

    [[nodiscard]] float GetOpacity() const noexcept { return fastProps_.opacity; }
    void SetOpacity(float v) noexcept {
        fastInlineRanks_[6] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        if (fastProps_.opacity != v) {
            fastProps_.opacity = v;
            InvalidateRender();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Opacity);
        }
    }

    [[nodiscard]] Visibility GetVisibility() const noexcept {
        return static_cast<Visibility>(fastProps_.visibility);
    }
    void SetVisibility(Visibility v) noexcept {
        fastInlineRanks_[7] = static_cast<std::uint8_t>(ValueSourceRank::Local);
        const auto raw = static_cast<std::uint8_t>(v);
        if (fastProps_.visibility != raw) {
            fastProps_.visibility = raw;
            InvalidateRender();
            InvalidateLayout();
            if (mountedTemplate_) SyncTemplateBindingsForProperty(PropId_Visibility);
        }
    }

    void InvalidateMeasure() noexcept {
        fastProps_.layoutDirtyFlags |= MeasureDirty;
        PropagateSubtreeDirty(SubtreeMeasureDirty);
    }
    void InvalidateArrange() noexcept {
        fastProps_.layoutDirtyFlags |= ArrangeDirty;
        PropagateSubtreeDirty(SubtreeArrangeDirty);
    }
    void InvalidateRender() noexcept {
        fastProps_.layoutDirtyFlags |= RenderDirty;
        PropagateSubtreeDirty(SubtreeRenderDirty);
    }
    void InvalidateLayout() noexcept {
        fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty | RenderDirty);
        PropagateSubtreeDirty(static_cast<std::uint8_t>(SubtreeLayoutDirty | SubtreeRenderDirty));
    }

    [[nodiscard]] std::uint8_t GetLayoutDirtyFlags() const noexcept {
        return fastProps_.layoutDirtyFlags;
    }
    void ClearLayoutDirtyFlags(
        std::uint8_t mask = static_cast<std::uint8_t>(
            LayoutAllDirty | SubtreeLayoutDirty | SubtreeRenderDirty)) const noexcept {
        fastProps_.layoutDirtyFlags &= static_cast<std::uint8_t>(~mask);
    }

    void SetStyleKey(std::string_view styleKey);
    [[nodiscard]] std::string GetStyleKey() const;

    // P1-1: ControlTemplate mounting, TemplatedParent linkage, TemplateBinding & PART_* contract
    [[nodiscard]] bool HasControlTemplate() const noexcept {
        return mountedTemplate_ != nullptr;
    }
    [[nodiscard]] UIElement* TemplateRoot() const noexcept;
    [[nodiscard]] const AxbControlTemplate* MountedControlTemplate() const noexcept;
    [[nodiscard]] std::string GetControlTemplateKey() const;
    void SetControlTemplateKey(std::string_view templateKey);

    [[nodiscard]] UIElement* TemplatedParent() const noexcept { return templatedParent_; }
    void SetTemplatedParent(UIElement* host) noexcept { templatedParent_ = host; }

    bool ApplyControlTemplate(
        const AxbControlTemplate& controlTemplate,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);
    bool ApplyControlTemplateByKey(std::string_view templateKey);
    void ClearControlTemplate();

    [[nodiscard]] UIElement* GetTemplateChild(std::string_view partName) const noexcept;
    [[nodiscard]] const std::vector<IntrusivePtr<UIElement>>& LogicalTemplateContentChildren() const noexcept;

    void AddTemplateBinding(
        UIElement* target,
        std::uint16_t sourcePropId,
        std::uint16_t targetPropId,
        ValueConverterKind converter);
    void RegisterTemplateNamedPart(std::string_view partName, UIElement* partElement);
    void RegisterTemplateContentPresenter(ContentPresenter* presenter);
    void SyncTemplateBindingsForProperty(std::uint16_t changedPropId) noexcept;
    void SyncTemplateBindingsForTarget(UIElement* target, std::uint16_t targetPropId) noexcept;
    void RefreshAllTemplateBindings() noexcept;
    void EnsureSparseSlotReserved(std::uint16_t propId);

    // Unified DependencyProperty read/write (fast inline for propId < 8 & alignments/interactive, 32B sparse entry for others)
    [[nodiscard]] UnifiedValue GetValue(std::uint16_t propId) const;
    [[nodiscard]] ValueSourceRank GetPropertySourceRank(std::uint16_t propId) const noexcept;
    bool SetValue(
        std::uint16_t propId,
        const UnifiedValue& value,
        ValueSourceRank rank = ValueSourceRank::Local);
    bool ForceEffectiveValue(
        std::uint16_t propId,
        const UnifiedValue& value,
        ValueSourceRank rank);
    bool ForceClearSparseEntry(std::uint16_t propId);
    bool ClearValue(std::uint16_t propId, ValueSourceRank rank = ValueSourceRank::Local);

    // Sparse entry metadata inspection/mutation (bindingSlotId, animationTrackId & extended blend/contract slots in 32B entry)
    [[nodiscard]] const CompactPropertyEntry* FindCompactEntry(
        std::uint16_t propId) const noexcept;
    bool SetPropertyMetadata(
        std::uint16_t propId,
        std::uint16_t bindingSlotId,
        std::uint16_t animationTrackId);
    bool SetPropertyExtendedMetadata(
        std::uint16_t propId,
        std::uint16_t secondaryTrackId,
        std::uint16_t contractInputSlot,
        std::uint16_t blendWeightUnorm16 = 0u,
        std::uint16_t entryMetaFlags = 0u);
    [[nodiscard]] std::size_t SparsePropertyCount() const noexcept {
        std::size_t count = 0u;
        for (const auto& e : sparseProps_) {
            if (!std::holds_alternative<std::monostate>(e.effectiveValue)) {
                ++count;
            }
        }
        return count;
    }
    [[nodiscard]] const std::vector<CompactPropertyEntry>& SparseProperties() const noexcept {
        return sparseProps_;
    }

    // C-03: 16-bit fast-slot active animation bitmask for O(1) fast-inline track tracking & restoration
    [[nodiscard]] std::uint16_t GetFastActiveAnimMask() const noexcept {
        return fastActiveAnimMask_;
    }
    void SetFastActiveAnimBit(int fastSlotIdx, bool active) noexcept {
        if (fastSlotIdx >= 0 && fastSlotIdx < static_cast<int>(kFastInlinePropertySlotCount)) {
            const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(fastSlotIdx));
            if (active) {
                fastActiveAnimMask_ |= bit;
            } else {
                fastActiveAnimMask_ &= static_cast<std::uint16_t>(~bit);
            }
        }
    }
    void SetFastPropertyAnimActive(std::uint16_t propId, bool active) noexcept {
        int slotIdx = -1;
        if (propId < 8u) {
            slotIdx = static_cast<int>(propId);
        } else if (propId == PropId_HorizontalAlignment) {
            slotIdx = 8;
        } else if (propId == PropId_VerticalAlignment) {
            slotIdx = 9;
        } else if (propId == PropId_IsEnabled) {
            slotIdx = 10;
        } else if (propId == PropId_Focusable) {
            slotIdx = 11;
        }
        SetFastActiveAnimBit(slotIdx, active);
    }

    void MarkSparseValuesInPools() const noexcept;
    void MarkSubtreeValuesInPools() const noexcept;

protected:
    void CoerceSparseFloatInPlace(std::uint16_t propId, float clampedValue) noexcept;
    void EnsureRegisteredInValuePoolSet() noexcept;
    virtual void MarkExtraValuesInPools() const noexcept {}

private:
    friend class DepthBucketLayoutQueue;

    [[nodiscard]] Matrix2D ComputeLocalToScreenMatrix2D() const noexcept;
    void PropagateEnabledStateToSubtree() noexcept;
    void PropagateRootConnection(
        bool connected,
        NativeHandleRegistry* registry,
        ManagedHeap* uiHeap) noexcept;
    void UpdateWrapperPinState(
        NativeHandleRegistry* registry,
        ManagedHeap* uiHeap) noexcept;
    void PropagateSubtreeDirty(std::uint8_t subtreeMask) noexcept {
        for (UIElement* cur = parent_; cur != nullptr; cur = cur->parent_) {
            if ((cur->fastProps_.layoutDirtyFlags & subtreeMask) == subtreeMask) {
                break;
            }
            cur->fastProps_.layoutDirtyFlags |= subtreeMask;
        }
    }
    void UpdateSubtreeDepth(std::uint16_t newDepth) noexcept;

    mutable std::atomic<std::uint32_t> refCount_{0};

    // Intrusive visual tree pointers
    UIElement* parent_ = nullptr;
    UIElement* firstChild_ = nullptr;
    UIElement* lastChild_ = nullptr;
    UIElement* nextSibling_ = nullptr;
    UIElement* prevSibling_ = nullptr;

    // Intrusive 64-level DepthBucketLayoutQueue links (0 heap allocation)
    UIElement* nextInMeasureBucket_ = nullptr;
    UIElement* nextInArrangeBucket_ = nullptr;
    std::uint16_t treeDepth_ = 0;
    bool inMeasureQueue_ = false;
    bool inArrangeQueue_ = false;

    // O(1) PagedRenderTree node span cache
    mutable std::uint32_t renderTreeId_ = 0u;
    mutable std::uint32_t renderNodeId_ = InvalidRenderNodeId;
    mutable std::uint16_t renderNodeSpan_ = 0u;

    // Layout computed geometry & cached Measure/Arrange constraints
    Size lastMeasureConstraint_{-1.0f, -1.0f};
    Rect lastArrangeSlot_{0.0f, 0.0f, -1.0f, -1.0f};
    Size measuredCoreSize_{0.0f, 0.0f};
    Size desiredSize_{0.0f, 0.0f};
    Rect finalRect_{0.0f, 0.0f, 0.0f, 0.0f};
    std::string name_;

    // Zero-hash NativeHandle slot, binding engine backlink & generation cache
    NativeHandle cachedNativeHandle_{};
    NativeHandleRegistry* boundRegistry_ = nullptr;
    ManagedHeap* boundUiHeap_ = nullptr;
    DualTrackBindingEngine* boundBindingEngine_ = nullptr;
    ObjectRef boundWrapperRef_{};
    PersistentRoot pinnedWrapperRoot_{};
    bool isConnectedToRoot_ = false;
    bool registeredInValuePoolSet_ = false;
    std::uint16_t fastActiveAnimMask_ = 0u;
    std::array<std::uint8_t, kFastInlinePropertySlotCount> fastInlineRanks_{};

    struct MountedControlTemplateData;
    [[nodiscard]] bool ReadTemplateSourceValueZeroAlloc(
        std::uint16_t sourcePropId,
        std::uint16_t changedPropId,
        UnifiedValue& outVal) const noexcept;

    UIElement* templatedParent_ = nullptr;
    std::unique_ptr<MountedControlTemplateData> mountedTemplate_;
    bool applyingControlTemplate_ = false;

    // High-frequency properties inline + 32B compact sparse vector
    FastInlineStorage fastProps_{};
    std::vector<CompactPropertyEntry> sparseProps_;
};

class Panel : public UIElement {
public:
    Panel();
    ~Panel() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }
};

class Window : public Panel {
public:
    Window();
    explicit Window(
        std::string_view className,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);
    ~Window() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    [[nodiscard]] const std::string& WindowClassName() const noexcept {
        return windowClassName_;
    }
    [[nodiscard]] bool IsTemplateMounted() const noexcept {
        return templateMounted_;
    }

    bool MountTemplate(
        const AxbPackage& package,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);

private:
    std::string windowClassName_;
    bool templateMounted_ = false;
};

class Button : public UIElement {
public:
    using ClickCallback = std::function<void(Button&)>;

    Button();
    ~Button() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetContentText(std::string_view text);
    [[nodiscard]] std::string GetContentText() const;

    void SetClickCallback(ClickCallback callback) {
        clickCallback_ = std::move(callback);
    }
    void BindScriptClickHandler(
        const Interpreter* interpreter,
        ObjectRef targetObject,
        SymbolId methodSymbolId) noexcept {
        scriptInterpreter_ = interpreter;
        scriptAotContext_ = nullptr;
        scriptTargetObject_ = targetObject;
        scriptClickMethodId_ = methodSymbolId;
    }
    void BindAotClickHandler(
        rs_aot::ExecutionContext* aotContext,
        ObjectRef targetObject,
        SymbolId methodSymbolId) noexcept {
        scriptAotContext_ = aotContext;
        scriptInterpreter_ = nullptr;
        scriptTargetObject_ = targetObject;
        scriptClickMethodId_ = methodSymbolId;
    }
    bool PerformClick(RuntimeError* error = nullptr);
    virtual bool ActivateFromInput(RuntimeError* error = nullptr);
    [[nodiscard]] std::uint32_t ClickGeneration() const noexcept { return clickGeneration_; }

    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnPointerUp(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool insideBounds) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;

    Size MeasureCore(Size availableSize) noexcept override;

private:
    ClickCallback clickCallback_;
    const Interpreter* scriptInterpreter_ = nullptr;
    rs_aot::ExecutionContext* scriptAotContext_ = nullptr;
    ObjectRef scriptTargetObject_{};
    SymbolId scriptClickMethodId_ = 0;
    std::uint32_t clickGeneration_ = 0u;
};

// Initialize built-in AeroGUI TypeClass hierarchy and Euler-tour intervals
void EnsureBuiltinTypesRegistered() noexcept;

// P1-2: Zero-ABI-overhead lifecycle & Loc.Source hooks for weak DynamicResource / Loc cleanup
using UIElementDestroyHook = void (*)(const UIElement* element) noexcept;
using UIElementLocSourceHook = void (*)(UIElement* element, std::string_view locSourceUri);

void RegisterUIElementResourceHooks(
    UIElementDestroyHook onDestroyed,
    UIElementLocSourceHook onLocSourceChanged) noexcept;

} // namespace aero
