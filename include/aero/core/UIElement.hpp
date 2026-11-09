#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

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
    UIElement();
    virtual ~UIElement();

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
    virtual Size MeasureCore(Size availableSize) noexcept;
    virtual void ArrangeCore(Rect finalRect) noexcept;
    virtual void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue);

    [[nodiscard]] Size DesiredSize() const noexcept { return desiredSize_; }
    [[nodiscard]] Rect FinalRect() const noexcept { return finalRect_; }
    [[nodiscard]] std::uint16_t TreeDepth() const noexcept { return treeDepth_; }

    // 8 Fast Inline Properties (0 heap allocation fast path)
    [[nodiscard]] float GetWidth() const noexcept { return fastProps_.width; }
    void SetWidth(float v) noexcept {
        if (fastProps_.width != v) {
            fastProps_.width = v;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
        }
    }

    [[nodiscard]] float GetHeight() const noexcept { return fastProps_.height; }
    void SetHeight(float v) noexcept {
        if (fastProps_.height != v) {
            fastProps_.height = v;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
        }
    }

    [[nodiscard]] float GetMinWidth() const noexcept { return fastProps_.minWidth; }
    void SetMinWidth(float v) noexcept {
        if (fastProps_.minWidth != v) {
            fastProps_.minWidth = v;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
        }
    }

    [[nodiscard]] float GetMinHeight() const noexcept { return fastProps_.minHeight; }
    void SetMinHeight(float v) noexcept {
        if (fastProps_.minHeight != v) {
            fastProps_.minHeight = v;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
        }
    }

    [[nodiscard]] Thickness GetMargin() const noexcept {
        return Thickness{
            fastProps_.marginL,
            fastProps_.marginT,
            fastProps_.marginR,
            fastProps_.marginB};
    }
    void SetMargin(const Thickness& m) noexcept {
        if (GetMargin() != m) {
            fastProps_.marginL = m.left;
            fastProps_.marginT = m.top;
            fastProps_.marginR = m.right;
            fastProps_.marginB = m.bottom;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
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
        if (GetPadding() != p) {
            fastProps_.paddingL = p.left;
            fastProps_.paddingT = p.top;
            fastProps_.paddingR = p.right;
            fastProps_.paddingB = p.bottom;
            fastProps_.layoutDirtyFlags |= (MeasureDirty | ArrangeDirty);
        }
    }

    [[nodiscard]] float GetOpacity() const noexcept { return fastProps_.opacity; }
    void SetOpacity(float v) noexcept {
        if (fastProps_.opacity != v) {
            fastProps_.opacity = v;
            fastProps_.layoutDirtyFlags |= RenderDirty;
        }
    }

    [[nodiscard]] Visibility GetVisibility() const noexcept {
        return static_cast<Visibility>(fastProps_.visibility);
    }
    void SetVisibility(Visibility v) noexcept {
        const auto raw = static_cast<std::uint8_t>(v);
        if (fastProps_.visibility != raw) {
            fastProps_.visibility = raw;
            fastProps_.layoutDirtyFlags |= LayoutAllDirty;
        }
    }

    [[nodiscard]] std::uint8_t GetLayoutDirtyFlags() const noexcept {
        return fastProps_.layoutDirtyFlags;
    }
    void ClearLayoutDirtyFlags(std::uint8_t mask = LayoutAllDirty) noexcept {
        fastProps_.layoutDirtyFlags &= static_cast<std::uint8_t>(~mask);
    }

    // Unified DependencyProperty read/write (fast inline for propId < 8, 32B sparse entry for >= 8)
    [[nodiscard]] UnifiedValue GetValue(std::uint16_t propId) const;
    bool SetValue(
        std::uint16_t propId,
        const UnifiedValue& value,
        ValueSourceRank rank = ValueSourceRank::Local);
    bool ClearValue(std::uint16_t propId, ValueSourceRank rank = ValueSourceRank::Local);

    // Sparse entry metadata inspection/mutation (bindingSlotId & animationTrackId in 32B entry)
    [[nodiscard]] const CompactPropertyEntry* FindCompactEntry(
        std::uint16_t propId) const noexcept;
    bool SetPropertyMetadata(
        std::uint16_t propId,
        std::uint16_t bindingSlotId,
        std::uint16_t animationTrackId);
    [[nodiscard]] std::size_t SparsePropertyCount() const noexcept {
        return sparseProps_.size();
    }

private:
    friend class DepthBucketLayoutQueue;

    void PropagateRootConnection(
        bool connected,
        NativeHandleRegistry* registry,
        ManagedHeap* uiHeap) noexcept;
    void UpdateWrapperPinState(
        NativeHandleRegistry* registry,
        ManagedHeap* uiHeap) noexcept;
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

    // Layout computed geometry
    Size desiredSize_{0.0f, 0.0f};
    Rect finalRect_{0.0f, 0.0f, 0.0f, 0.0f};
    std::string name_;

    // Zero-hash NativeHandle slot & generation cache
    NativeHandle cachedNativeHandle_{};
    NativeHandleRegistry* boundRegistry_ = nullptr;
    ManagedHeap* boundUiHeap_ = nullptr;
    ObjectRef boundWrapperRef_{};
    PersistentRoot pinnedWrapperRoot_{};
    bool isConnectedToRoot_ = false;

    // 8 high-frequency properties inline + 32B compact sparse vector
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

struct AxbPackage;
class DualTrackBindingEngine;

class Window : public Panel {
public:
    Window();
    explicit Window(
        std::string_view className,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr);
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
        const Interpreter* eventInterpreter = nullptr);

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
        scriptTargetObject_ = targetObject;
        scriptClickMethodId_ = methodSymbolId;
    }
    bool PerformClick(RuntimeError* error = nullptr);

    Size MeasureCore(Size availableSize) noexcept override;

private:
    ClickCallback clickCallback_;
    const Interpreter* scriptInterpreter_ = nullptr;
    ObjectRef scriptTargetObject_{};
    SymbolId scriptClickMethodId_ = 0;
};

// Initialize built-in AeroGUI TypeClass hierarchy and Euler-tour intervals
void EnsureBuiltinTypesRegistered() noexcept;

} // namespace aero
