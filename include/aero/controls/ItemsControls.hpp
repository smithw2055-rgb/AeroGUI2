#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/controls/Control.hpp"

namespace aero {

class AnimationEngine;
class ExecutionDomainContext;
class ScrollViewer;
class StyleTriggerEngine;

enum class TabStripPlacement : std::uint8_t {
    Top = 0,
    Bottom = 1,
    Left = 2,
    Right = 3,
};

// ============================================================================
// ContentPresenter
// ============================================================================

class ContentPresenter : public Control {
    AERO_DECLARE_CLASS(ContentPresenter, Control)
public:
    ContentPresenter();
    ~ContentPresenter() override = default;

    [[nodiscard]] std::string GetContentSource() const;
    [[nodiscard]] std::string GetTemplateContentSource() const override {
        return GetContentSource();
    }
    [[nodiscard]] std::string GetEffectiveText() const;
    [[nodiscard]] Color GetEffectiveForeground() const noexcept;
    [[nodiscard]] float GetEffectiveFontSize() const noexcept;
    [[nodiscard]] std::string GetEffectiveFontFamily() const;
    [[nodiscard]] TextWrapping GetEffectiveTextWrapping() const noexcept;
    [[nodiscard]] HorizontalAlignment GetEffectiveHorizontalContentAlignment() const noexcept;
    [[nodiscard]] VerticalAlignment GetEffectiveVerticalContentAlignment() const noexcept;
    [[nodiscard]] Point ComputeTextOrigin(const Rect& boxRect, Size textSize) const noexcept;

    void SyncContentTemplateChild();
    void SyncTemplateContentHost() override { SyncContentTemplateChild(); }

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;

private:
    IntrusivePtr<UIElement> contentTemplateChild_;
    std::string activeContentTemplateKey_;
    ObjectRef activeContentTemplateObj_{};
    bool hasActiveContentTemplateObj_ = false;
    bool syncingContentTemplate_ = false;
};

// ============================================================================
// ItemsControl, HeaderedItemsControl, MenuBase, Selector
// ============================================================================

class ItemsControl : public Control {
    AERO_DECLARE_CLASS(ItemsControl, Control)
public:
    using ItemGeneratorFn = std::function<IntrusivePtr<UIElement>(std::size_t index, const UnifiedValue& item)>;
    using ItemBinderFn = std::function<void(std::size_t index, const UnifiedValue& item, UIElement& element)>;

    ItemsControl();
    ~ItemsControl() override;

    void SetItemGenerator(ItemGeneratorFn generator) {
        itemGenerator_ = std::move(generator);
    }

    void SetItemBinder(ItemBinderFn binder) {
        itemBinder_ = std::move(binder);
    }

    void SetDomains(ExecutionDomainContext* domains) noexcept {
        domains_ = domains;
    }
    void SetBindingEngine(DualTrackBindingEngine* engine) noexcept {
        bindingEngine_ = engine;
    }
    void SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept;
    void AssignStyleEnginePointer(StyleTriggerEngine* styleEngine) noexcept {
        styleEngine_ = styleEngine;
    }
    void SetAnimationEngine(AnimationEngine* animationEngine) noexcept {
        animationEngine_ = animationEngine;
    }

    void SetItemTemplateKey(std::string_view key);
    [[nodiscard]] std::string GetItemTemplateKey() const;

    void SetIsVirtualizing(bool enabled);
    [[nodiscard]] bool IsVirtualizing() const noexcept;

    void SetVirtualItemHeight(float itemHeight);
    [[nodiscard]] float GetVirtualItemHeight() const noexcept;

    virtual void SetSelectedIndex(std::int32_t index);
    [[nodiscard]] virtual std::int32_t GetSelectedIndex() const noexcept;
    [[nodiscard]] virtual UnifiedValue GetSelectedItem() const;

    [[nodiscard]] std::size_t TotalItemCount() const noexcept {
        return !items_.empty() ? items_.size() : (hasBoundItemsSource_ ? 0u : ChildCount());
    }
    [[nodiscard]] std::size_t RealizedItemCount() const noexcept {
        return ChildCount();
    }
    [[nodiscard]] std::size_t FirstRealizedItemIndex() const noexcept {
        return firstRealizedIndex_;
    }
    [[nodiscard]] std::size_t RealizedWindowEndExclusive() const noexcept {
        return realizedEndExclusive_;
    }
    [[nodiscard]] const std::vector<UnifiedValue>& Items() const noexcept {
        return items_;
    }

    virtual void SetItemsSource(
        const std::vector<UnifiedValue>& items,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);

    virtual bool ApplyIncrementalChanges(
        const CollectionChangeRecord* changes,
        std::size_t changeCount);

    [[nodiscard]] bool ShouldSkipControlTemplatePackage(
        const AxbPackage& package) const noexcept override;

    [[nodiscard]] bool MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept override;
    void AssignContainerStyleEngine(StyleTriggerEngine* engine) noexcept override {
        AssignStyleEnginePointer(engine);
    }
    [[nodiscard]] bool TryApplyIncrementalCollectionChanges(
        const CollectionChangeRecord* deltas,
        std::size_t count) override {
        return ApplyIncrementalChanges(deltas, count);
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

protected:
    [[nodiscard]] ExecutionDomainContext* EffectiveDomains() const noexcept;
    [[nodiscard]] DualTrackBindingEngine* EffectiveBindingEngine() const noexcept;
    [[nodiscard]] StyleTriggerEngine* EffectiveStyleEngine() const noexcept;
    [[nodiscard]] AnimationEngine* EffectiveAnimationEngine() const noexcept;
    [[nodiscard]] ExecutionDomainContext* Domains() const noexcept { return EffectiveDomains(); }
    [[nodiscard]] DualTrackBindingEngine* BindingEngine() const noexcept { return EffectiveBindingEngine(); }
    [[nodiscard]] bool IsDeferringSelectionNotify() const noexcept { return deferSelectionNotify_; }
    [[nodiscard]] bool ComputeLiveScrollViewport(float& outViewTop, float& outViewH) const noexcept;
    void MarkExtraValuesInPools() const noexcept override;

    ExecutionDomainContext* domains_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    StyleTriggerEngine* styleEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    std::vector<UnifiedValue> items_;
    IntrusivePtr<UIElement> retainedVisualResourceHolder_;

private:
    struct RealizedVirtualSlot {
        std::size_t itemIndex = 0;
        IntrusivePtr<UIElement> element{};
        const AxbPackage* appliedTemplate = nullptr;
    };

    [[nodiscard]] UnifiedValue NormalizeItemValue(const UnifiedValue& raw);
    [[nodiscard]] std::int32_t FindMatchingItemIndex(const UnifiedValue& target) const;
    void ReleaseChildSubtreeResources(UIElement* ch);
    void ClearRecyclePool(NativeHandleRegistry* registry = nullptr, ManagedHeap* uiHeap = nullptr);
    void MoveVirtualSlotsToRecyclePool(NativeHandleRegistry* registry = nullptr, ManagedHeap* uiHeap = nullptr);
    [[nodiscard]] IntrusivePtr<UIElement> CreateMaterializedItemVisual(
        std::size_t index,
        const UnifiedValue& itemVal,
        const AxbPackage*& outAppliedTemplate);
    bool RebindMaterializedItemInPlace(
        UIElement* elem,
        const AxbPackage* appliedTpl,
        std::size_t index,
        const UnifiedValue& itemVal);
    void SyncSelectionMetadataAfterMutation(std::int32_t nextSelectedIndex);
    void ComputeCurrentVirtualWindow(
        Size fallbackChildAvail,
        std::size_t& outFirstIdx,
        std::size_t& outEndExclusive,
        Size& outMeasureConstraint) const noexcept;

    void RebuildAllMaterializedItems(NativeHandleRegistry* registry, ManagedHeap* uiHeap);
    void SyncVirtualizedWindow(
        std::size_t targetFirstIdx,
        std::size_t targetEndExclusive,
        Size measureConstraint);
    [[nodiscard]] const ScrollViewer* FindAncestorScrollViewer() const noexcept;
    [[nodiscard]] std::size_t FirstItemChildOffset() const noexcept;
    [[nodiscard]] const AxbDataTemplate* ResolveActiveDataTemplateEntry(const UnifiedValue& sampleItem) const noexcept;
    [[nodiscard]] const AxbPackage* ResolveActiveItemTemplate(const UnifiedValue& sampleItem) const noexcept;
    [[nodiscard]] IntrusivePtr<UIElement> InstantiateTemplatedItem(
        const AxbPackage& tplPkg,
        const UnifiedValue& itemVal);

    ItemGeneratorFn itemGenerator_;
    ItemBinderFn itemBinder_;
    std::vector<RealizedVirtualSlot> virtualSlots_;
    std::vector<RealizedVirtualSlot> recyclePool_;
    std::size_t firstRealizedIndex_ = 0;
    std::size_t realizedEndExclusive_ = 0;
    float estimatedMaxItemWidth_ = 80.0f;
    Size lastChildAvailableSize_{240.0f, 240.0f};
    bool hasValidMeasureConstraint_ = false;
    bool hasBoundItemsSource_ = false;
    bool syncingItemsSelection_ = false;
    bool deferSelectionNotify_ = false;
};

class HeaderedItemsControl : public ItemsControl {
    AERO_DECLARE_CLASS(HeaderedItemsControl, ItemsControl)
public:
    HeaderedItemsControl();
    ~HeaderedItemsControl() override = default;

    virtual void SetHeader(std::string_view text);
    [[nodiscard]] virtual std::string GetHeader() const;

    void SetHeaderTemplateKey(std::string_view key);
    [[nodiscard]] std::string GetHeaderTemplateKey() const;

    void SetHeaderElement(UIElement* element);
    [[nodiscard]] UIElement* GetHeaderElement() const noexcept;
    [[nodiscard]] UIElement* GetRawHeaderElement() const noexcept { return headerElement_.Get(); }
    [[nodiscard]] bool HasHeader() const noexcept;

    [[nodiscard]] const UIElement* GetCompositeHeaderElement() const noexcept override {
        return GetHeaderElement();
    }
    [[nodiscard]] UIElement* GetLogicalHeaderElementForTemplate() const noexcept override {
        return GetRawHeaderElement();
    }

protected:
    IntrusivePtr<UIElement> headerElement_;
};

class MenuBase : public ItemsControl {
    AERO_DECLARE_CLASS(MenuBase, ItemsControl)
public:
    MenuBase();
    ~MenuBase() override = default;

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetSpacing(float spacing);
    [[nodiscard]] float GetSpacing() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class Selector : public ItemsControl {
    AERO_DECLARE_CLASS(Selector, ItemsControl)
public:
    Selector();
    ~Selector() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    [[nodiscard]] std::int32_t SelectedIndex() const noexcept { return GetSelectedIndex(); }
    [[nodiscard]] UnifiedValue SelectedItem() const { return GetSelectedItem(); }

    void SetSelectedValue(const UnifiedValue& value);
    [[nodiscard]] UnifiedValue GetSelectedValue() const;

    void SetSelectedValuePath(std::string_view path);
    [[nodiscard]] std::string GetSelectedValuePath() const;

    virtual bool SelectIndexInteractive(std::int32_t index);
};

// ============================================================================
// ListBox, ListView, ComboBox, TabItem, TabControl
// ============================================================================

class ListBox : public Selector {
    AERO_DECLARE_CLASS(ListBox, Selector)
public:
    using SelectionChangedCallback = std::function<void(ListBox&, std::int32_t)>;

    ListBox();
    ~ListBox() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetSelectionChangedCallback(SelectionChangedCallback callback) {
        selectionChangedCallback_ = std::move(callback);
    }

    bool SelectIndexInteractive(std::int32_t index) override;
    bool SelectIndexWithModifiers(std::int32_t index, std::uint8_t modifiers);

    void SetSelectionMode(std::int32_t mode);
    [[nodiscard]] std::int32_t GetSelectionMode() const noexcept;
    [[nodiscard]] const std::vector<std::int32_t>& SelectedIndices() const noexcept { return selectedIndices_; }
    [[nodiscard]] bool IsIndexSelected(std::int32_t index) const noexcept;
    void ClearSelection();

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;

    [[nodiscard]] bool MatchesXamlAncestorTypeAlias(std::string_view wantedType) const noexcept override;
    [[nodiscard]] bool MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept override;
    [[nodiscard]] bool TryExecuteSelectContainerAction(UIElement* originTarget) override;

protected:
    [[nodiscard]] virtual float ItemsAreaTopInset() const noexcept { return 0.0f; }
    void MarkExtraValuesInPools() const noexcept override;

private:
    void ScrollItemIntoView(std::int32_t index) noexcept;

    SelectionChangedCallback selectionChangedCallback_;
    std::vector<std::int32_t> selectedIndices_;
    std::int32_t selectionAnchorIndex_ = -1;
    std::int32_t lastNotifiedSelectedIndex_ = -1;
    UnifiedValue lastNotifiedSelectedItem_{};
    bool hasNotifiedSelection_ = false;
};

class ListView : public ListBox {
    AERO_DECLARE_CLASS(ListView, ListBox)
public:
    static constexpr float kHeaderBarHeight = 26.0f;

    ListView();
    ~ListView() override = default;

    [[nodiscard]] bool HasGridViewHeader() const noexcept;
    [[nodiscard]] float HeaderBarHeight() const noexcept {
        return HasGridViewHeader() ? kHeaderBarHeight : 0.0f;
    }
    [[nodiscard]] float ItemsAreaTopInset() const noexcept override {
        return HeaderBarHeight();
    }

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
};

class ComboBox : public Selector {
    AERO_DECLARE_CLASS(ComboBox, Selector)
public:
    using SelectionChangedCallback = std::function<void(ComboBox&, std::int32_t)>;
    static constexpr float kPopupRowHeight = 26.0f;

    ComboBox();
    ~ComboBox() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetSelectedIndex(std::int32_t index) override;
    [[nodiscard]] std::int32_t GetSelectedIndex() const noexcept override;
    [[nodiscard]] UnifiedValue GetSelectedItem() const override;
    [[nodiscard]] std::string GetSelectedItemText() const;

    [[nodiscard]] std::size_t ComboItemCount() const noexcept;
    [[nodiscard]] std::string FormatItemDisplayText(const UnifiedValue& val) const;
    [[nodiscard]] std::string GetComboItemText(std::size_t index) const;
    [[nodiscard]] UnifiedValue GetComboItemValue(std::size_t index) const;

    void SetIsDropDownOpen(bool open);
    [[nodiscard]] bool IsDropDownOpen() const noexcept;

    void SetIsEditable(bool editable);
    [[nodiscard]] bool IsEditable() const noexcept;
    [[nodiscard]] float GetMaxDropDownHeight() const noexcept;
    [[nodiscard]] std::string GetDisplayText() const;
    bool OnTextInput(std::string_view utf8Text) override;

    bool SelectIndexInteractive(std::int32_t index) override;
    bool ResolvePendingSelectedValue() {
        return hasPendingSelectedValue_ && TryResolveSelectedValue(pendingSelectedValue_);
    }

    [[nodiscard]] std::int32_t HighlightedPopupIndex() const noexcept {
        return highlightedPopupIndex_;
    }
    void SetHighlightedPopupIndex(std::int32_t idx) noexcept {
        if (highlightedPopupIndex_ != idx) {
            highlightedPopupIndex_ = idx;
            InvalidateRender();
        }
    }

    [[nodiscard]] Rect DropDownPopupLocalRect() const noexcept;
    [[nodiscard]] Rect DropDownPopupScreenRect() const noexcept;
    [[nodiscard]] std::int32_t HitTestPopupItemIndex(Point screenPoint) const noexcept;

    void SetSelectionChangedCallback(SelectionChangedCallback callback) {
        selectionChangedCallback_ = std::move(callback);
    }

    [[nodiscard]] bool ShouldSkipControlTemplatePackage(
        const AxbPackage& package) const noexcept override;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool HasDropdownOverlayBehavior() const noexcept override { return true; }
    [[nodiscard]] bool IsDropdownOverlayOpen() const noexcept override { return IsDropDownOpen(); }
    void SetDropdownOverlayOpen(bool open) override { SetIsDropDownOpen(open); }
    [[nodiscard]] std::int32_t HitTestDropdownOverlayItem(Point screenPt) const noexcept override {
        return HitTestPopupItemIndex(screenPt);
    }
    void SetHighlightedDropdownOverlayItem(std::int32_t idx) noexcept override {
        SetHighlightedPopupIndex(idx);
    }
    bool SelectDropdownOverlayItemInteractive(std::int32_t idx) override {
        return SelectIndexInteractive(idx);
    }
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool SuppressesUntemplatedRenderChildren() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;
    void EmitDeferredOverlayPopupPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& st) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void OnFocusChanged(bool focused) override;
    void OnEnabledStateChanged(bool effectivelyEnabled) override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    void SyncSelectedItemFromIndex();
    bool TryResolveSelectedValue(const UnifiedValue& targetVal);

    SelectionChangedCallback selectionChangedCallback_;
    UnifiedValue pendingSelectedValue_{};
    std::int32_t highlightedPopupIndex_ = -1;
    bool syncingSelection_ = false;
    bool hasPendingSelectedValue_ = false;
};

class ListBoxItem : public ContentControl {
    AERO_DECLARE_CLASS(ListBoxItem, ContentControl)
public:
    ListBoxItem();
    ~ListBoxItem() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetIsSelected(bool selected);
    [[nodiscard]] bool IsSelected() const noexcept;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
};

class ComboBoxItem : public ContentControl {
    AERO_DECLARE_CLASS(ComboBoxItem, ContentControl)
public:
    ComboBoxItem();
    ~ComboBoxItem() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetIsSelected(bool selected);
    [[nodiscard]] bool IsSelected() const noexcept;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
};

class ListViewItem : public ListBoxItem {
    AERO_DECLARE_CLASS(ListViewItem, ListBoxItem)
public:
    ListViewItem();
    ~ListViewItem() override = default;
};

class TabItem : public HeaderedContentControl {
    AERO_DECLARE_CLASS(TabItem, HeaderedContentControl)
public:
    TabItem();
    ~TabItem() override = default;

    void SetIsSelected(bool selected);
    [[nodiscard]] bool IsSelected() const noexcept;

    [[nodiscard]] bool TryExecuteSelectContainerAction(UIElement* originTarget) override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override {
        (void)tree;
        (void)curState;
        (void)r;
        (void)w;
        (void)h;
        (void)effOpacity;
        (void)ioEmittedCount;
        return false;
    }

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class TabControl : public Selector {
    AERO_DECLARE_CLASS(TabControl, Selector)
public:
    using SelectionChangedCallback = std::function<void(TabControl&, std::int32_t)>;
    static constexpr float kDefaultTabStripHeight = 32.0f;

    TabControl();
    ~TabControl() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetSelectedIndex(std::int32_t index) override;
    [[nodiscard]] std::int32_t GetSelectedIndex() const noexcept override;
    bool SelectTabInteractive(std::int32_t index);
    bool SelectIndexInteractive(std::int32_t index) override {
        return SelectTabInteractive(index);
    }

    void SetTabStripPlacement(TabStripPlacement placement);
    [[nodiscard]] TabStripPlacement GetTabStripPlacement() const noexcept;

    [[nodiscard]] std::size_t TabCount() const noexcept;
    [[nodiscard]] UIElement* GetTabElement(std::size_t index) const noexcept;
    [[nodiscard]] std::string GetTabHeaderText(std::size_t index) const;
    [[nodiscard]] const std::vector<Rect>& TabHeaderLocalRects() const noexcept {
        return tabHeaderRects_;
    }
    [[nodiscard]] Rect GetTabHeaderLocalRect(std::size_t index) const noexcept;
    [[nodiscard]] Rect TabStripLocalRect() const noexcept { return tabStripLocalRect_; }
    [[nodiscard]] Rect ContentPanelLocalRect() const noexcept { return contentPanelLocalRect_; }
    [[nodiscard]] std::int32_t HitTestTabHeaderIndex(Point localPoint) const noexcept;
    [[nodiscard]] bool IsChildInactiveTabPage(const UIElement* child) const noexcept;
    [[nodiscard]] bool IsChildHiddenByLayout(const UIElement* child) const noexcept override {
        return IsChildInactiveTabPage(child);
    }

    void SetItemsSource(
        const std::vector<UnifiedValue>& items,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr) override;
    bool ApplyIncrementalChanges(
        const CollectionChangeRecord* changes,
        std::size_t changeCount) override;

    [[nodiscard]] bool HandlesCtrlTabNavigation() const noexcept override { return true; }
    void AssignContainerStyleEngine(StyleTriggerEngine* engine) noexcept override {
        AssignStyleEnginePointer(engine);
    }
    [[nodiscard]] bool TryApplyIncrementalCollectionChanges(
        const CollectionChangeRecord* deltas,
        std::size_t count) override {
        return ApplyIncrementalChanges(deltas, count);
    }
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool RequiresFullRenderRebuildOnDirty() const noexcept override { return true; }
    [[nodiscard]] bool ShouldEmitRenderChild(
        const UIElement* child,
        bool& outCollapseSubtree,
        const UIElement*& outFallbackHeaderOnly) const noexcept override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void SetSelectionChangedCallback(SelectionChangedCallback callback) {
        selectionChangedCallback_ = std::move(callback);
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

protected:
    void MarkExtraValuesInPools() const noexcept override;

private:
    void SyncTabSelectionStates() noexcept;
    [[nodiscard]] UnifiedValue NormalizeTabItemValue(const UnifiedValue& raw);
    [[nodiscard]] IntrusivePtr<UIElement> InstantiateTabItemForValue(const UnifiedValue& itemVal);
    void ReleaseTabChildResources(UIElement* child);

    SelectionChangedCallback selectionChangedCallback_;
    std::vector<Rect> tabHeaderRects_;
    Rect tabStripLocalRect_{};
    Rect contentPanelLocalRect_{};
    bool syncingTabs_ = false;
};

// ============================================================================
// StatusBar, ToolBar, Menu, ContextMenu, MenuItem, TreeView
// ============================================================================

class StatusBar : public ItemsControl {
    AERO_DECLARE_CLASS(StatusBar, ItemsControl)
public:
    StatusBar();
    ~StatusBar() override = default;

    void SetLastChildFill(bool lastChildFill);
    [[nodiscard]] bool GetLastChildFill() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class StatusBarItem : public ContentControl {
    AERO_DECLARE_CLASS(StatusBarItem, ContentControl)
public:
    StatusBarItem();
    ~StatusBarItem() override = default;

    [[nodiscard]] std::string GetEffectiveText() const;
};

class ToolBar : public HeaderedItemsControl {
    AERO_DECLARE_CLASS(ToolBar, HeaderedItemsControl)
public:
    ToolBar();
    ~ToolBar() override = default;

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetSpacing(float spacing);
    [[nodiscard]] float GetSpacing() const noexcept;

    [[nodiscard]] std::string GetHeaderText() const;
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class Menu : public MenuBase {
    AERO_DECLARE_CLASS(Menu, MenuBase)
public:
    Menu();
    ~Menu() override = default;

    [[nodiscard]] bool IsMenuRootContainer() const noexcept override { return true; }
};

class ContextMenu : public MenuBase {
    AERO_DECLARE_CLASS(ContextMenu, MenuBase)
public:
    ContextMenu();
    ~ContextMenu() override = default;

    void SetIsOpen(bool open);
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsOpenForHitTest() const noexcept override { return IsOpen(); }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class MenuItem : public HeaderedItemsControl {
    AERO_DECLARE_CLASS(MenuItem, HeaderedItemsControl)
public:
    using ClickCallback = std::function<void(MenuItem&)>;

    MenuItem();
    ~MenuItem() override;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetContentText(std::string_view text);
    [[nodiscard]] std::string GetContentText() const;

    void SetClickCallback(ClickCallback callback) { clickCallback_ = std::move(callback); }
    void BindScriptClickHandler(
        const Interpreter* interp,
        ObjectRef targetObject,
        SymbolId methodId) noexcept {
        clickBinding_.BindScript(interp, targetObject, methodId);
    }
    void BindAotClickHandler(
        rs_aot::ExecutionContext* aotCtx,
        ObjectRef targetObject,
        SymbolId methodId) noexcept {
        clickBinding_.BindAot(aotCtx, targetObject, methodId);
    }

    bool PerformClick(RuntimeError* error = nullptr);
    virtual bool ActivateFromInput(RuntimeError* error = nullptr);
    [[nodiscard]] std::uint32_t ClickGeneration() const noexcept { return clickGeneration_; }

    void SetIsCheckable(bool checkable) {
        SetValue(PropId_IsCheckable, UnifiedValue{checkable}, ValueSourceRank::Local);
    }
    [[nodiscard]] bool IsCheckable() const noexcept {
        bool v = false;
        (void)GetValue(PropId_IsCheckable).tryGetBool(v);
        return v;
    }

    void SetIsChecked(bool checked) {
        SetValue(PropId_IsChecked, UnifiedValue{checked}, ValueSourceRank::Local);
    }
    [[nodiscard]] bool IsChecked() const noexcept {
        bool v = false;
        (void)GetValue(PropId_IsChecked).tryGetBool(v);
        return v;
    }

    void SetIsSubmenuOpen(bool open) {
        SetValue(PropId_IsSubmenuOpen, UnifiedValue{open}, ValueSourceRank::Local);
    }
    [[nodiscard]] bool IsSubmenuOpen() const noexcept {
        bool v = false;
        (void)GetValue(PropId_IsSubmenuOpen).tryGetBool(v);
        return v;
    }

    void SetInputGestureText(std::string_view gestureText) {
        SetValue(PropId_InputGestureText, value::FromString(std::string(gestureText)), ValueSourceRank::Local);
    }
    [[nodiscard]] std::string GetInputGestureText() const {
        const UnifiedValue v = GetValue(PropId_InputGestureText);
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            return sv->str();
        }
        return {};
    }

    [[nodiscard]] static bool HasAnyOpenSubmenu() noexcept;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool TracksPressedWithinBoundsDuringCapture() const noexcept override {
        return true;
    }
    [[nodiscard]] bool IsMenuSubmenuItem() const noexcept override { return true; }
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool HasOwnOpaqueShadowSurfaceForRender() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
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
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    bool submenuCounted_ = false;
    ClickCallback clickCallback_;
    ScriptClickBinding clickBinding_;
    std::uint32_t clickGeneration_ = 0u;
};

class TreeView;

class TreeViewItem : public HeaderedItemsControl {
    AERO_DECLARE_CLASS(TreeViewItem, HeaderedItemsControl)
public:
    TreeViewItem();
    ~TreeViewItem() override = default;

    [[nodiscard]] bool IsInteractiveControl() const noexcept override { return true; }

    void SetHeader(std::string_view header) override;
    [[nodiscard]] std::string GetHeader() const override;

    void SetIsExpanded(bool expanded);
    [[nodiscard]] bool IsExpanded() const noexcept;

    void SetIsSelected(bool selected);
    [[nodiscard]] bool IsSelected() const noexcept;

    [[nodiscard]] TreeView* FindAncestorTreeView() noexcept;
    void SyncHasItemsFlag() noexcept;

    [[nodiscard]] bool TryExecuteSelectContainerAction(UIElement* originTarget) override;
    [[nodiscard]] bool TryGetResolvedHeaderText(std::string& outHeader) const override {
        outHeader = GetHeader();
        return true;
    }
    [[nodiscard]] bool ShouldEmitRenderChild(
        const UIElement* child,
        bool& outCollapseSubtree,
        const UIElement*& outFallbackHeaderOnly) const noexcept override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class TreeView : public ItemsControl {
    AERO_DECLARE_CLASS(TreeView, ItemsControl)
public:
    using SelectedItemChangedCallback = std::function<void(TreeView&, TreeViewItem*)>;

    TreeView();
    ~TreeView() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SelectContainer(TreeViewItem* item);
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    [[nodiscard]] TreeViewItem* SelectedContainer() const noexcept { return selectedContainer_; }
    [[nodiscard]] std::string GetSelectedHeader() const;
    void SetSelectedItemChangedCallback(SelectedItemChangedCallback cb) {
        selectedItemChangedCallback_ = std::move(cb);
    }

private:
    TreeViewItem* selectedContainer_ = nullptr;
    SelectedItemChangedCallback selectedItemChangedCallback_;
    bool selecting_ = false;
};

} // namespace aero
