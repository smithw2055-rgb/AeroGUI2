module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. ItemsPresenter & ItemsControl (Real Panel Composition + Recycling)
// ============================================================================

public class ItemsPresenter : FrameworkElement {
    private Panel _itemsPanel;
    private Func<Panel> _panelFactory;

    public ItemsPresenter() : base(VisualKind.Box) {}
    public override string GetTypeName() { return "ItemsPresenter"; }

    public Panel ItemsPanel {
        get {
            if (_itemsPanel == null) { EnsurePanel(); }
            return _itemsPanel;
        }
    }

    public Func<Panel> PanelFactory {
        get { return _panelFactory; }
        set {
            _panelFactory = value;
            RebuildPanel();
        }
    }

    public Panel EnsurePanel() {
        if (_itemsPanel != null) { return _itemsPanel; }
        if (_panelFactory != null) {
            var fn = _panelFactory;
            _itemsPanel = fn();
        }
        if (_itemsPanel == null) {
            var sp = new StackPanel(Orientation.Vertical, 0.0f);
            sp.HorizontalAlignment = HorizontalAlignment.Stretch;
            _itemsPanel = sp;
        }
        _itemsPanel.IsItemsHost = true;
        ClearChildren();
        AddChild(_itemsPanel);
        return _itemsPanel;
    }

    public void RebuildPanel() {
        UIElement[] existing = new UIElement[0];
        if (_itemsPanel != null) {
            for (int i = 0; i < _itemsPanel.ChildCount(); i += 1) {
                existing.Push(_itemsPanel.ChildAt(i));
            }
            _itemsPanel.ClearChildren();
        }
        _itemsPanel = null;
        Panel next = EnsurePanel();
        for (int i = 0; i < existing.Length; i += 1) {
            next.AddChild(existing[i]);
        }
    }
}

public class ItemsPanelTemplate : DependencyObject {
    private Func<Panel> _factory;
    public ResourceDictionary AmbientResources { get; set; }
    public ItemsPanelTemplate() {}
    public ItemsPanelTemplate(Func<Panel> factory) { _factory = factory; }
    public Panel CreatePanelScoped() {
        if (_factory == null) { return null; }
        ResourceDictionary.PushAmbient(AmbientResources);
        var fn = _factory;
        Panel p = fn();
        if (p != null && AmbientResources != null && p.AmbientResources == null) { p.AmbientResources = AmbientResources; }
        ResourceDictionary.PopAmbient(AmbientResources);
        return p;
    }
    public Func<Panel> GetFactory() {
        if (_factory == null || AmbientResources == null) { return _factory; }
        ItemsPanelTemplate self = this;
        return () => { return self.CreatePanelScoped(); };
    }
    public Func<Panel> Factory {
        get { return _factory; }
        set { _factory = value; }
    }
}

public class ItemsControl : Control {
    private DependencyObject[] _manualItems;
    private DependencyObject _itemsSourceBox;
    private DataTemplate _itemTemplate;
    private Style _itemContainerStyle;
    private Func<Panel> _itemsPanelFactory;
    private string _displayMemberPath = "";
    private int _alternationCount = 0;

    private bool _virtualizing = false;
    private float _estimatedItemHeight = 32.0f;
    private int _virtFirstIndex = 0;
    private int _virtLastIndex = -1;

    protected ItemsPresenter _itemsPresenter;
    protected Panel _directItemsHost;
    protected UIElement[] _activeContainers;
    protected DependencyObject[] _activeBoxes;
    protected UIElement[] _recycledPool;
    private Action _collectionChangedHook;

    public ItemsControl() : base() {
        _manualItems = new DependencyObject[0];
        _activeContainers = new UIElement[0];
        _activeBoxes = new DependencyObject[0];
        _recycledPool = new UIElement[0];
    }

    public override string GetTypeName() { return "ItemsControl"; }

    public object ItemsSource {
        get { return DependencyObject.Unbox(_itemsSourceBox); }
        set { SetItemsSourceBox(DependencyObject.BoxObject(value)); }
    }
    public DependencyObject ItemsSourceBox {
        get { return _itemsSourceBox; }
        set { SetItemsSourceBox(value); }
    }

    public DataTemplate ItemTemplate {
        get { return _itemTemplate; }
        set { _itemTemplate = value; RecycleActiveContainers(); RefreshItems(); }
    }
    public Style ItemContainerStyle {
        get { return _itemContainerStyle; }
        set { _itemContainerStyle = value; RecycleActiveContainers(); RefreshItems(); }
    }
    public Func<Panel> ItemsPanel {
        get { return _itemsPanelFactory; }
        set {
            _itemsPanelFactory = value;
            if (_itemsPresenter != null) { _itemsPresenter.PanelFactory = value; }
            RecycleActiveContainers();
            RefreshItems();
        }
    }
    public Func<Panel> ItemsPanelFactory() { return _itemsPanelFactory; }
    public string DisplayMemberPath {
        get { return _displayMemberPath; }
        set { _displayMemberPath = value == null ? "" : value; RecycleActiveContainers(); RefreshItems(); }
    }
    public int AlternationCount {
        get { return _alternationCount; }
        set { _alternationCount = value >= 0 ? value : 0; RefreshItems(); }
    }
    public bool IsVirtualizing {
        get { return _virtualizing; }
        set { _virtualizing = value; RefreshItems(); }
    }
    public float EstimatedItemHeight {
        get { return _estimatedItemHeight; }
        set { _estimatedItemHeight = value > 8.0f ? value : 32.0f; }
    }

    public int ItemCount() {
        if (_itemsSourceBox != null) {
            if (_itemsSourceBox is ICollectionNotifier) {
                return (_itemsSourceBox as ICollectionNotifier).ItemCount();
            }
            object raw = DependencyObject.Unbox(_itemsSourceBox);
            if (raw is ICollectionNotifier) {
                return (raw as ICollectionNotifier).ItemCount();
            }
            return 0;
        }
        return _manualItems.Length;
    }

    public DependencyObject GetItemBoxAt(int index) {
        if (index < 0 || index >= ItemCount()) { return null; }
        if (_itemsSourceBox != null) {
            if (_itemsSourceBox is ICollectionNotifier) {
                return (_itemsSourceBox as ICollectionNotifier).BoxItemAt(index);
            }
            object raw = DependencyObject.Unbox(_itemsSourceBox);
            if (raw is ICollectionNotifier) {
                return (raw as ICollectionNotifier).BoxItemAt(index);
            }
            return null;
        }
        return _manualItems[index];
    }

    public object GetItemAt(int index) {
        return DependencyObject.Unbox(GetItemBoxAt(index));
    }

    public int IndexOfItemBox(DependencyObject box) {
        int n = ItemCount();
        for (int i = 0; i < n; i += 1) {
            if (ItemsControl.SameItemBox(GetItemBoxAt(i), box)) { return i; }
        }
        return -1;
    }

    internal int IndexOfItemContainer(UIElement container) {
        if (container == null) { return -1; }
        if (_activeContainers != null) {
            for (int i = 0; i < _activeContainers.Length; i += 1) {
                if (_activeContainers[i] == container) {
                    return (_virtualizing ? _virtFirstIndex : 0) + i;
                }
            }
        }
        DependencyObject box = (container is ContentControl)
            ? (container as ContentControl).ContentBox
            : container.EffectiveDataContextBox();
        return box != null ? IndexOfItemBox(box) : -1;
    }

    internal bool TryReplaceItemBoxAt(int index, DependencyObject box) {
        if (_itemsSourceBox != null) {
            object src = ItemsSource;
            if (src != null && ParseUtil.IsNativeReady() &&
                Reflect_ListSet(src, index, DependencyObject.Unbox(box))) {
                CollectionChangeHub.NotifyChanged(src);
                return true;
            }
            return false;
        }
        if (index < 0 || index >= _manualItems.Length) { return false; }
        _manualItems[index] = box;
        RefreshItems();
        return true;
    }

    internal bool TryInsertItemBoxAt(int index, DependencyObject box) {
        if (_itemsSourceBox != null) {
            object src = ItemsSource;
            if (src != null && ParseUtil.IsNativeReady() &&
                Reflect_ListInsert(src, index, DependencyObject.Unbox(box))) {
                CollectionChangeHub.NotifyChanged(src);
                return true;
            }
            return false;
        }
        int ins = index < 0 ? 0 : (index > _manualItems.Length ? _manualItems.Length : index);
        _manualItems.Push(box);
        for (int i = _manualItems.Length - 1; i > ins; i -= 1) {
            _manualItems[i] = _manualItems[i - 1];
        }
        _manualItems[ins] = box;
        RefreshItems();
        return true;
    }

    internal bool TryAddItemBox(DependencyObject box) {
        if (_itemsSourceBox != null) {
            object src = ItemsSource;
            if (src != null && ParseUtil.IsNativeReady() &&
                Reflect_ListAdd(src, DependencyObject.Unbox(box))) {
                CollectionChangeHub.NotifyChanged(src);
                return true;
            }
            return false;
        }
        AddItemBox(box);
        return true;
    }

    internal bool TryRemoveItemAt(int index) {
        if (_itemsSourceBox != null) {
            object src = ItemsSource;
            if (src != null && ParseUtil.IsNativeReady() && Reflect_ListRemoveAt(src, index)) {
                CollectionChangeHub.NotifyChanged(src);
                return true;
            }
            return false;
        }
        if (index < 0 || index >= _manualItems.Length) { return false; }
        RemoveItemAt(index);
        return true;
    }

    public void AddItemBox(DependencyObject box) {
        _manualItems.Push(box);
        if (_itemsSourceBox == null) { RefreshItems(); }
    }
    public void AddItem(object item) { AddItemBox(DependencyObject.BoxObject(item)); }
    public void AddItem(string item) { AddItemBox(DependencyObject.BoxString(item)); }
    public void AddItem(int item) { AddItemBox(DependencyObject.BoxInt(item)); }
    public void AddItem(float item) { AddItemBox(DependencyObject.BoxFloat(item)); }

    public void RemoveItemAt(int index) {
        if (index < 0 || index >= _manualItems.Length) { return; }
        _manualItems.RemoveAt(index);
        if (_itemsSourceBox == null) { RefreshItems(); }
    }

    public void ClearItems() {
        _manualItems.Clear();
        if (_itemsSourceBox == null) { RefreshItems(); }
    }

    public void SetItemsSourceBox(DependencyObject newSource) {
        if (_itemsSourceBox != null && _collectionChangedHook != null) {
            object oldObj = DependencyObject.Unbox(_itemsSourceBox);
            CollectionChangeHub.Unsubscribe(oldObj, _collectionChangedHook);
        }
        _itemsSourceBox = newSource;
        if (_itemsSourceBox != null) {
            if (_collectionChangedHook == null) {
                _collectionChangedHook = () => { OnItemsCollectionChanged(); };
            }
            object srcObj = DependencyObject.Unbox(_itemsSourceBox);
            CollectionChangeHub.Subscribe(srcObj, _collectionChangedHook);
        }
        OnItemsSourceChanged();
        RefreshItems();
    }

    protected virtual void OnItemsSourceChanged() {}
    protected virtual void OnItemsCollectionChanged() { RefreshItems(); }

    public int ContainerCount() { return _activeContainers.Length; }
    public UIElement ContainerAt(int index) { return _activeContainers[index]; }

    protected virtual bool IsItemItsOwnContainer(DependencyObject itemBox) {
        return itemBox is UIElement;
    }

    protected virtual UIElement CreateContainerForItem() {
        var cp = new ContentPresenter();
        cp.HorizontalAlignment = HorizontalAlignment.Stretch;
        return cp;
    }

    protected UIElement AcquireContainer() {
        if (_recycledPool.Length > 0) {
            int last = _recycledPool.Length - 1;
            UIElement reused = _recycledPool[last];
            _recycledPool.RemoveAt(last);
            reused.Visibility = Visibility.Visible;
            return reused;
        }
        return CreateContainerForItem();
    }

    protected virtual void PrepareContainerForItem(UIElement container, DependencyObject itemBox, int index) {
        if (container == null) { return; }
        object rawData = DependencyObject.Unbox(itemBox);
        if (rawData != null) {
            container.DataContext = rawData;
        }
        DependencyObject displayBox = itemBox;
        if (_displayMemberPath.Length > 0 && rawData != null) {
            DependencyObject memberBox = BindingEngine.ReadPathBox(rawData, _displayMemberPath);
            if (memberBox != null) { displayBox = memberBox; }
        }
        if (container is ContentControl) {
            ContentControl cc = container as ContentControl;
            cc.SetContentAndTemplate(displayBox, _itemTemplate);
        } else if (container is ContentPresenter) {
            ContentPresenter cp = container as ContentPresenter;
            cp.SetContentAndTemplate(displayBox, _itemTemplate);
        }
        if (_alternationCount > 0) {
            container.Tag = DependencyObject.BoxInt(index % _alternationCount);
        }
    }

    protected virtual void ClearContainerForItem(UIElement container) {
        if (container == null) { return; }
        container.ClearLocalDataContext();
        if (container is ContentControl) { (container as ContentControl).SetContentAndTemplate(null, null); }
        else if (container is ContentPresenter) { (container as ContentPresenter).SetContentAndTemplate(null, null); }
    }

    internal static bool SameItemBox(DependencyObject a, DependencyObject b) {
        if (a == b) { return true; }
        if (a == null || b == null) { return false; }
        if (a is BoxedString && b is BoxedString) {
            return (a as BoxedString).Value == (b as BoxedString).Value;
        }
        if (a is BoxedInt && b is BoxedInt) {
            return (a as BoxedInt).Value == (b as BoxedInt).Value;
        }
        if (a is BoxedFloat && b is BoxedFloat) {
            return (a as BoxedFloat).Value == (b as BoxedFloat).Value;
        }
        if (a is BoxedBool && b is BoxedBool) {
            return (a as BoxedBool).Value == (b as BoxedBool).Value;
        }
        if (a.BoxValueEquals(b)) { return true; }
        object ua = DependencyObject.Unbox(a);
        object ub = DependencyObject.Unbox(b);
        if (ua != null && ub != null && ua == ub) {
            return true;
        }
        return false;
    }

    private void RecycleActiveContainers() {
        Panel host = EnsureItemsHostPanel();
        if (host != null) { host.ClearChildren(); }
        for (int i = 0; i < _activeContainers.Length; i += 1) {
            UIElement c = _activeContainers[i];
            if (!IsContainerRecyclable(c)) { continue; }
            ClearContainerForItem(c);
            _recycledPool.Push(c);
        }
        _activeContainers.Clear();
        _activeBoxes.Clear();
    }

    protected virtual bool IsContainerRecyclable(UIElement c) {
        return true;
    }

    public void UpdateVirtualizedViewport(float scrollY, float viewportHeight) {
        if (!_virtualizing || EnsureItemsHostPanel() == null) {
            RefreshItems();
            return;
        }
        int total = ItemCount();
        if (total == 0) {
            RecycleActiveContainers();
            _virtFirstIndex = 0;
            _virtLastIndex = -1;
            return;
        }
        float vh = viewportHeight > 0.0f ? viewportHeight : 400.0f;
        int first = (int)(scrollY / _estimatedItemHeight) - 2;
        if (first < 0) { first = 0; }
        int visCount = (int)(vh / _estimatedItemHeight) + 5;
        int last = first + visCount;
        if (last >= total) { last = total - 1; }
        if (first == _virtFirstIndex && last == _virtLastIndex && _activeContainers.Length > 0) {
            return;
        }
        _virtFirstIndex = first;
        _virtLastIndex = last;
        BuildContainerRange(first, last);
    }

    public virtual void RefreshItems() {
        int total = ItemCount();
        BindingEngine.NotifyTargetPropertyChanged(this, "Items.Count", DependencyObject.BoxInt(total));
        if (EnsureItemsHostPanel() == null) { return; }
        if (_virtualizing && total > 32) {
            UpdateVirtualizedViewport(0.0f, ActualHeight > 0.0f ? ActualHeight : 400.0f);
            return;
        }
        BuildContainerRange(0, total - 1);
    }

    protected void BuildContainerRange(int firstIdx, int lastIdx) {
        Panel host = EnsureItemsHostPanel();
        if (host == null) { return; }
        if (lastIdx < firstIdx) {
            RecycleActiveContainers();
            return;
        }

        BindingEngine.BeginBatch();
        int desiredCount = lastIdx - firstIdx + 1;
        UIElement[] prevContainers = _activeContainers;
        DependencyObject[] prevBoxes = _activeBoxes;
        int prevCount = prevContainers.Length;
        var prevUsed = new bool[prevCount];
        var nextContainers = new UIElement[desiredCount];
        var nextBoxes = new DependencyObject[desiredCount];

        for (int j = 0; j < desiredCount; j += 1) {
            int itemIdx = firstIdx + j;
            DependencyObject box = GetItemBoxAt(itemIdx);
            nextBoxes[j] = box;
            if (IsItemItsOwnContainer(box)) {
                UIElement own = box as UIElement;
                PrepareOwnContainer(own, itemIdx);
                nextContainers[j] = own;
                for (int k = 0; k < prevCount; k += 1) {
                    if (!prevUsed[k] && prevContainers[k] == own) {
                        prevUsed[k] = true;
                        break;
                    }
                }
                continue;
            }
            int hit = -1;
            for (int k = 0; k < prevCount; k += 1) {
                if (prevUsed[k]) { continue; }
                if (k < prevBoxes.Length && ItemsControl.SameItemBox(prevBoxes[k], box) && IsContainerRecyclable(prevContainers[k])) {
                    hit = k;
                    break;
                }
            }
            if (hit >= 0) {
                prevUsed[hit] = true;
                UIElement reused = prevContainers[hit];
                if (_alternationCount > 0 && reused != null) {
                    reused.Tag = DependencyObject.BoxInt(itemIdx % _alternationCount);
                }
                nextContainers[j] = reused;
            } else {
                UIElement c = AcquireContainer();
                if (_itemContainerStyle != null && c != null) {
                    c.Style = _itemContainerStyle;
                }
                PrepareContainerForItem(c, box, itemIdx);
                nextContainers[j] = c;
            }
        }

        UIElement hostEl = host;
        for (int k = 0; k < prevCount; k += 1) {
            if (prevUsed[k]) { continue; }
            UIElement dead = prevContainers[k];
            if (dead == null) { continue; }
            if (hostEl != null && dead.Parent == hostEl) {
                host.RemoveChild(dead);
            }
            DependencyObject oldBox = k < prevBoxes.Length ? prevBoxes[k] : null;
            if (!IsItemItsOwnContainer(oldBox) && IsContainerRecyclable(dead)) {
                ClearContainerForItem(dead);
                _recycledPool.Push(dead);
            }
        }

        if (host != null) {
            for (int j = 0; j < desiredCount; j += 1) {
                UIElement c = nextContainers[j];
                if (c == null) { continue; }
                if (j < host.ChildCount() && host.ChildAt(j) == c) {
                    continue;
                }
                if (j >= host.ChildCount() && c.Parent != hostEl) {
                    host.AddChild(c);
                } else {
                    host.InsertChild(j, c);
                }
            }
        }

        _activeContainers = nextContainers;
        _activeBoxes = nextBoxes;
        BindingEngine.EndBatch();
        OnContainersRebuilt();
    }

    protected virtual void OnContainersRebuilt() {}
    protected virtual void PrepareOwnContainer(UIElement container, int index) {}

    private static ItemsPresenter FindFirstItemsPresenter(UIElement rootNode, UIElement owner) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node != rootNode && node is ItemsControl) { continue; }
            if (node is ItemsPresenter) { return node as ItemsPresenter; }
            if (node is Popup) {
                UIElement popChild = (node as Popup).Child;
                if (popChild != null) { q.Push(popChild); }
                continue;
            }
            if (node != rootNode && node is Control) {
                Control subCtrl = node as Control;
                subCtrl.ApplyStyleInternal();
                if (subCtrl.TemplateRoot() == null) {
                    subCtrl.ApplyTemplate();
                }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    protected static Panel FindItemsHostPanel(UIElement rootNode, UIElement owner) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node != rootNode && node is ItemsControl) { continue; }
            if (node is Panel) {
                Panel p = node as Panel;
                if (p.IsItemsHost) { return p; }
            }
            if (node is Popup) {
                UIElement popChild = (node as Popup).Child;
                if (popChild != null) { q.Push(popChild); }
                continue;
            }
            if (node != rootNode && node is Control) {
                Control subCtrl = node as Control;
                subCtrl.ApplyStyleInternal();
                if (subCtrl.TemplateRoot() == null) {
                    subCtrl.ApplyTemplate();
                }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    protected Panel EnsureItemsHostPanel() {
        if (_itemsPresenter != null) { return _itemsPresenter.EnsurePanel(); }
        return _directItemsHost;
    }

    public override void OnApplyTemplate() {
        _itemsPresenter = null;
        _directItemsHost = null;
        UIElement found = GetTemplateChild("PART_ItemsPresenter");
        if (found is ItemsPresenter) {
            _itemsPresenter = found as ItemsPresenter;
        } else {
            _itemsPresenter = FindFirstItemsPresenter(TemplateRoot(), this);
        }
        if (_itemsPresenter != null) {
            if (_itemsPanelFactory != null) { _itemsPresenter.PanelFactory = _itemsPanelFactory; }
            _itemsPresenter.EnsurePanel();
            RefreshItems();
        } else {
            _directItemsHost = FindItemsHostPanel(TemplateRoot(), this);
            if (_directItemsHost != null) {
                RefreshItems();
            }
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.Name = "PART_ItemsPresenter";
        if (_itemsPanelFactory != null) { _itemsPresenter.PanelFactory = _itemsPanelFactory; }
        _itemsPresenter.EnsurePanel();
        RefreshItems();
        return _itemsPresenter;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "ItemsSource") { SetItemsSourceBox(value); return true; }
        if (propName == "ItemTemplate" && value is DataTemplate) { ItemTemplate = value as DataTemplate; return true; }
        if (propName == "ItemContainerStyle" && value is Style) { ItemContainerStyle = value as Style; return true; }
        if (propName == "ItemsPanel" && value is ItemsPanelTemplate) {
            ItemsPanelTemplate ipt = value as ItemsPanelTemplate;
            ItemsPanel = ipt.GetFactory();
            return true;
        }
        if (propName == "DisplayMemberPath") { DisplayMemberPath = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "AlternationCount") { AlternationCount = DependencyObject.UnboxInt(value, 0); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "HasItems") { return DependencyObject.BoxBool(ItemCount() > 0); }
        if (propName == "Items.Count") { return DependencyObject.BoxInt(ItemCount()); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 2. SelectableItem, ListBoxItem, ComboBoxItem, ListViewItem, Selector, ListBox
// ============================================================================

public class SelectableItem : ContentControl {
    private bool _isSelected = false;
    private int _itemIndex = -1;
    private Selector _ownerSelector;

    public event RoutedEventHandler Selected;
    public event RoutedEventHandler Unselected;

    public SelectableItem() : base() {
        Focusable = true;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        HorizontalContentAlignment = HorizontalAlignment.Left;
        VerticalContentAlignment = VerticalAlignment.Center;
        Padding = new Thickness(10.0f, 6.0f, 10.0f, 6.0f);
        CornerRadius = new CornerRadius(4.0f);
    }

    public override string GetTypeName() { return "SelectableItem"; }

    public int ItemIndex {
        get { return _itemIndex; }
        set { _itemIndex = value; }
    }
    public Selector OwnerSelector {
        get { return _ownerSelector; }
        set { _ownerSelector = value; }
    }

    public bool IsSelected {
        get { return _isSelected; }
        set {
            if (_isSelected == value) { return; }
            if (_ownerSelector != null) {
                _ownerSelector.NotifyContainerSelected(this, value);
                return;
            }
            _isSelected = value;
            UpdateTriggersAndStates();
            UpdateSelectionVisual();
            if (_isSelected) { Selected(this, new RoutedEventArgs("Selected", this)); }
            else { Unselected(this, new RoutedEventArgs("Unselected", this)); }
        }
    }

    internal void SetSelectedSilent(bool sel) {
        if (_isSelected == sel) { return; }
        _isSelected = sel;
        UpdateTriggersAndStates();
        UpdateSelectionVisual();
        if (_isSelected) { Selected(this, new RoutedEventArgs("Selected", this)); }
        else { Unselected(this, new RoutedEventArgs("Unselected", this)); }
    }

    protected override void UpdateVisualStates(bool useTransitions) {
        base.UpdateVisualStates(useTransitions);
        if (_isSelected) {
            bool focused = IsFocused || (_ownerSelector != null && _ownerSelector.IsFocused);
            if (!focused && VisualStateManager.HasState(this, "SelectedUnfocused")) {
                VisualStateManager.GoToState(this, "SelectedUnfocused", useTransitions);
            } else {
                VisualStateManager.GoToState(this, "Selected", useTransitions);
            }
        } else {
            VisualStateManager.GoToState(this, "Unselected", useTransitions);
        }
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        UpdateSelectionVisual();
    }

    protected virtual void UpdateSelectionVisual() {
        if (Style != null || Template != null) { return; }
        if (_isSelected) {
            Background = new SolidColorBrush(ParseUtil.PackArgb(255u, 37u, 99u, 235u));
            Foreground = Brushes.White();
        } else if (IsMouseOver) {
            Background = new SolidColorBrush(ParseUtil.PackArgb(255u, 45u, 53u, 72u));
            Foreground = Brushes.DefaultForeground();
        } else {
            Background = Brushes.Transparent();
            Foreground = Brushes.DefaultForeground();
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        if (_ownerSelector != null) {
            _ownerSelector.NotifyItemClicked(this, _itemIndex, e.Modifiers);
            e.Handled = true;
        } else {
            IsSelected = !IsSelected;
            e.Handled = true;
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsSelected") { IsSelected = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsSelected") { return DependencyObject.BoxBool(_isSelected); }
        return base.GetPropertyValue(propName);
    }
}

public class ListBoxItem : SelectableItem {
    public ListBoxItem() : base() {}
    public override string GetTypeName() { return "ListBoxItem"; }
}

public class ComboBoxItem : SelectableItem {
    public ComboBoxItem() : base() {
        CornerRadius = new CornerRadius(0.0f);
        Padding = new Thickness(10.0f, 8.0f, 10.0f, 8.0f);
    }
    protected override void UpdateSelectionVisual() {
        if (Style != null || Template != null) { return; }
        if (IsSelected) {
            Background = new SolidColorBrush("#FF0C74B3");
            Foreground = new SolidColorBrush("#FFFFFFFF");
        } else if (IsMouseOver) {
            Background = new SolidColorBrush("#FF3A4046");
            Foreground = new SolidColorBrush("#FFE8ECF0");
        } else {
            Background = Brushes.Transparent();
            Foreground = new SolidColorBrush("#FFD0D4D8");
        }
    }
    public override string GetTypeName() { return "ComboBoxItem"; }
}

public class ListViewItem : SelectableItem {
    public ListViewItem() : base() {}
    public override string GetTypeName() { return "ListViewItem"; }
}

public class Selector : ItemsControl {
    private int _selectedIndex = -1;
    private int _pendingSelectedIndex = -1;
    protected DependencyObject _selectedItemBox;
    private SelectionMode _selectionMode = SelectionMode.Single;
    private int[] _selectedIndices;
    private int _anchorIndex = -1;
    public string SelectedValuePath { get; set; } = "";

    public event SelectionChangedEventHandler SelectionChanged;

    public Selector() : base() {
        _selectedIndices = new int[0];
        Focusable = true;
    }

    public override string GetTypeName() { return "Selector"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "SelectionChanged") {
            SelectionChanged += (object s, SelectionChangedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    public SelectionMode SelectionMode {
        get { return _selectionMode; }
        set { _selectionMode = value; }
    }

    public int SelectedIndex {
        get { return _selectedIndex; }
        set { SetSelectedIndexInternal(value, true); }
    }

    protected void SetSelectedItemBoxInternal(DependencyObject box) {
        int idx = IndexOfItemBox(box);
        if (idx >= 0) {
            SetSelectedIndexInternal(idx, true);
            return;
        }
        int oldIdx = _selectedIndex;
        DependencyObject oldBox = _selectedItemBox;
        _selectedIndices.Clear();
        _selectedIndex = -1;
        _selectedItemBox = box;
        if (oldIdx != -1 || !ItemsControl.SameItemBox(oldBox, box)) {
            OnSelectionChanged(oldIdx, -1, oldBox, box);
        } else {
            SyncContainerSelectionStates();
        }
    }

    public object SelectedItem {
        get { return DependencyObject.Unbox(_selectedItemBox); }
        set { SetSelectedItemBoxInternal(DependencyObject.BoxObject(value)); }
    }

    public object SelectedValue {
        get {
            object item = SelectedItem;
            if (item == null || SelectedValuePath.Length == 0) { return item; }
            return DependencyObject.Unbox(BindingEngine.ReadPathBox(item, SelectedValuePath));
        }
    }

    public int[] SelectedIndices() { return _selectedIndices; }

    public void UnselectAll() {
        _selectedIndices.Clear();
        SetSelectedIndexInternal(-1, true);
    }

    protected virtual void OnSelectionChanged(int oldIdx, int newIdx, DependencyObject oldBox, DependencyObject newBox) {
        SyncContainerSelectionStates();
        BindingEngine.MarkFlushDirty();
        NotifyStateInvalidatedHooks();
        UpdateTriggersAndStates();
        SelectionChanged(this, new SelectionChangedEventArgs(this, newIdx, oldBox, newBox));
        BindingEngine.NotifyTargetPropertyChanged(this, "SelectedIndex", DependencyObject.BoxInt(newIdx));
        BindingEngine.NotifyTargetPropertyChanged(this, "SelectedItem", newBox);
        BindingEngine.NotifyTargetPropertyChanged(this, "SelectedItem.Content", GetPropertyValue("SelectedItem.Content"));
    }

    internal void NotifyContainerSelected(SelectableItem item, bool selected) {
        if (item == null) { return; }
        int idx = item.ItemIndex;
        if (idx < 0 || idx >= ItemCount()) {
            idx = IndexOfContainer(item);
        }
        if (idx < 0 || idx >= ItemCount()) {
            item.SetSelectedSilent(selected);
            return;
        }
        if (selected) {
            SetSelectedIndexInternal(idx, _selectionMode == SelectionMode.Single);
            if (_selectionMode != SelectionMode.Single && _selectedIndices.IndexOf(idx) < 0) {
                _selectedIndices.Push(idx);
                SyncContainerSelectionStates();
            }
        } else {
            if (_selectionMode == SelectionMode.Single) {
                if (_selectedIndex == idx) {
                    SetSelectedIndexInternal(-1, true);
                } else {
                    item.SetSelectedSilent(false);
                }
            } else {
                int pos = _selectedIndices.IndexOf(idx);
                if (pos >= 0) {
                    _selectedIndices.RemoveAt(pos);
                }
                if (_selectedIndex == idx) {
                    int nextIdx = _selectedIndices.Length > 0 ? _selectedIndices[_selectedIndices.Length - 1] : -1;
                    SetSelectedIndexInternal(nextIdx, false);
                } else {
                    SyncContainerSelectionStates();
                    item.SetSelectedSilent(false);
                }
            }
        }
    }

    protected void SetSelectedIndexInternal(int newIdx, bool replaceMultiSet) {
        int count = ItemCount();
        if (newIdx < 0) {
            _pendingSelectedIndex = -1;
        } else if (count == 0) {
            _pendingSelectedIndex = newIdx;
            return;
        }
        int clamped = -1;
        if (newIdx >= 0 && count > 0) {
            clamped = newIdx < count ? newIdx : (count - 1);
        }
        if (clamped >= 0) { _pendingSelectedIndex = -1; }
        int oldIdx = _selectedIndex;
        DependencyObject oldBox = _selectedItemBox;
        DependencyObject newBox = clamped >= 0 ? GetItemBoxAt(clamped) : null;

        if (replaceMultiSet) {
            _selectedIndices.Clear();
            if (clamped >= 0) { _selectedIndices.Push(clamped); }
        }
        if (oldIdx == clamped && ItemsControl.SameItemBox(oldBox, newBox)) { return; }
        _selectedIndex = clamped;
        _selectedItemBox = newBox;
        if (clamped >= 0) { _anchorIndex = clamped; }
        OnSelectionChanged(oldIdx, clamped, oldBox, newBox);
    }

    protected void TryApplyPendingSelection() {
        int total = ItemCount();
        if (total <= 0) { return; }
        if (_pendingSelectedIndex >= 0) {
            int pend = _pendingSelectedIndex;
            _pendingSelectedIndex = -1;
            int targetIdx = pend < total ? pend : (total - 1);
            SetSelectedIndexInternal(targetIdx, true);
            if (pend >= total) {
                _pendingSelectedIndex = pend;
            }
            return;
        }
        if (_selectedIndex < 0 && _selectedItemBox != null) {
            int idx = IndexOfItemBox(_selectedItemBox);
            if (idx >= 0) {
                SetSelectedIndexInternal(idx, true);
                return;
            }
        }
    }

    public virtual void NotifyItemClicked(SelectableItem item, int index, int modifiers) {
        int idx = index >= 0 ? index : IndexOfContainer(item);
        if (idx < 0) { return; }
        bool ctrl = KeyModifiers.Has(modifiers, KeyModifiers.Ctrl());
        bool shift = KeyModifiers.Has(modifiers, KeyModifiers.Shift());

        if (_selectionMode == SelectionMode.Single) {
            SetSelectedIndexInternal(idx, true);
            return;
        }
        if (_selectionMode == SelectionMode.Multiple || (_selectionMode == SelectionMode.Extended && ctrl)) {
            int pos = _selectedIndices.IndexOf(idx);
            if (pos >= 0) { _selectedIndices.RemoveAt(pos); }
            else { _selectedIndices.Push(idx); }
            int primary = _selectedIndices.Length > 0 ? _selectedIndices[_selectedIndices.Length - 1] : -1;
            SetSelectedIndexInternal(primary, false);
            return;
        }
        if (_selectionMode == SelectionMode.Extended && shift && _anchorIndex >= 0) {
            int lo = _anchorIndex < idx ? _anchorIndex : idx;
            int hi = _anchorIndex > idx ? _anchorIndex : idx;
            _selectedIndices.Clear();
            for (int k = lo; k <= hi; k += 1) { _selectedIndices.Push(k); }
            SetSelectedIndexInternal(idx, false);
            return;
        }
        SetSelectedIndexInternal(idx, true);
    }

    protected int IndexOfContainer(UIElement container) {
        for (int i = 0; i < _activeContainers.Length; i += 1) {
            if (_activeContainers[i] == container) { return i; }
        }
        return -1;
    }

    protected void SyncContainerSelectionStates() {
        for (int i = 0; i < _activeContainers.Length; i += 1) {
            UIElement c = _activeContainers[i];
            if (c is SelectableItem) {
                SelectableItem si = c as SelectableItem;
                int itemIdx = si.ItemIndex >= 0 ? si.ItemIndex : i;
                bool sel = (_selectionMode == SelectionMode.Single)
                    ? (itemIdx == _selectedIndex)
                    : _selectedIndices.Contains(itemIdx);
                si.SetSelectedSilent(sel);
            }
        }
    }

    protected override UIElement CreateContainerForItem() {
        return new ListBoxItem();
    }

    protected override void PrepareContainerForItem(UIElement container, DependencyObject itemBox, int index) {
        base.PrepareContainerForItem(container, itemBox, index);
        if (container is SelectableItem) {
            SelectableItem si = container as SelectableItem;
            si.ItemIndex = index;
            si.OwnerSelector = this;
            si.SetSelectedSilent(index == _selectedIndex || _selectedIndices.Contains(index));
        }
    }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        if (container is SelectableItem) {
            SelectableItem si = container as SelectableItem;
            si.ItemIndex = index;
            si.OwnerSelector = this;
            si.SetSelectedSilent(index == _selectedIndex || _selectedIndices.Contains(index));
        }
    }

    protected override void OnContainersRebuilt() {
        TryApplyPendingSelection();
        SyncContainerSelectionStates();
    }

    protected override void OnItemsCollectionChanged() {
        base.OnItemsCollectionChanged();
        TryApplyPendingSelection();
        if (_selectedIndex >= ItemCount()) {
            SetSelectedIndexInternal(-1, true);
        } else {
            SyncContainerSelectionStates();
        }
    }

    public override void RefreshItems() {
        base.RefreshItems();
        TryApplyPendingSelection();
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "SelectedIndex") { SelectedIndex = DependencyObject.UnboxInt(value, -1); return true; }
        if (propName == "SelectedItem") {
            SetSelectedItemBoxInternal(value);
            return true;
        }
        if (propName == "SelectedValuePath") { SelectedValuePath = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "SelectedIndex") { return DependencyObject.BoxInt(_selectedIndex); }
        if (propName == "SelectedItem") { return _selectedItemBox; }
        if (propName == "SelectedItem.Content") {
            object item = DependencyObject.Unbox(_selectedItemBox);
            if (item is ContentControl) {
                return (item as ContentControl).ContentBox;
            }
            return _selectedItemBox;
        }
        return base.GetPropertyValue(propName);
    }
}

public class ListBox : Selector {
    protected ScrollViewer _scrollViewer;

    public ListBox() : base() {
        Background = new SolidColorBrush("#FF171B24");
        BorderBrush = new SolidColorBrush("#FF364054");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(4.0f);
    }

    public override string GetTypeName() { return "ListBox"; }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (Template != null && EnsureItemsHostPanel() == null) { return; }
        int n = ItemCount();
        if (n == 0) { return; }
        if (e.Key == KeyCode.Down) {
            SelectedIndex = SelectedIndex < n - 1 ? (SelectedIndex + 1) : 0;
            e.Handled = true;
        } else if (e.Key == KeyCode.Up) {
            SelectedIndex = SelectedIndex > 0 ? (SelectedIndex - 1) : (n - 1);
            e.Handled = true;
        } else if (e.Key == KeyCode.Home) {
            SelectedIndex = 0;
            e.Handled = true;
        } else if (e.Key == KeyCode.End) {
            SelectedIndex = n - 1;
            e.Handled = true;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _scrollViewer = new ScrollViewer();
        _scrollViewer.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
        _scrollViewer.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        _scrollViewer.ScrollChanged += (object s, ScrollEventArgs e) => {
            if (IsVirtualizing) {
                UpdateVirtualizedViewport(_scrollViewer.VerticalOffset, _scrollViewer.ViewportHeight);
            }
        };

        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.Name = "PART_ItemsPresenter";
        if (ItemsPanelFactory() != null) { _itemsPresenter.PanelFactory = ItemsPanelFactory(); }
        _itemsPresenter.EnsurePanel();
        _scrollViewer.Content = _itemsPresenter;
        RefreshItems();
        return _scrollViewer;
    }
}

// ============================================================================
// 3. GridViewColumn, GridView, ListView
// ============================================================================

public class GridViewColumn : DependencyObject {
    public string Header { get; set; } = "";
    public float Width { get; set; } = 120.0f;
    public Binding DisplayMemberBinding { get; set; }
    public DataTemplate CellTemplate { get; set; }
    public string DisplayMemberPath {
        get { return DisplayMemberBinding != null ? DisplayMemberBinding.Path : ""; }
        set { DisplayMemberBinding = new Binding(value); }
    }

    public GridViewColumn() {}
    public GridViewColumn(string header, string memberPath, float width = 120.0f) {
        Header = header == null ? "" : header;
        Width = width > 10.0f ? width : 120.0f;
        if (memberPath != null && memberPath.Length > 0) {
            DisplayMemberBinding = new Binding(memberPath);
        }
    }
}

public class GridView : DependencyObject {
    private GridViewColumn[] _columns;

    public GridView() { _columns = new GridViewColumn[0]; }
    public GridViewColumn[] Columns() { return _columns; }
    public void AddColumn(GridViewColumn col) { if (col != null) { _columns.Push(col); } }
    public void ClearColumns() { _columns.Clear(); }
}

public class GridViewHeaderRowPresenter : FrameworkElement {
    private DependencyObject _contextMenuBox;
    private DependencyObject _templateSelectorBox;
    private DependencyObject _toolTipBox;
    private DependencyObject _columnsBox;
    public bool AllowsColumnReorder { get; set; } = true;
    public Style ColumnHeaderContainerStyle { get; set; }
    public DataTemplate ColumnHeaderTemplate { get; set; }

    public object ColumnHeaderContextMenu {
        get { return DependencyObject.Unbox(_contextMenuBox); }
        set { _contextMenuBox = DependencyObject.BoxObject(value); }
    }
    public object ColumnHeaderTemplateSelector {
        get { return DependencyObject.Unbox(_templateSelectorBox); }
        set { _templateSelectorBox = DependencyObject.BoxObject(value); }
    }
    public object ColumnHeaderToolTip {
        get { return DependencyObject.Unbox(_toolTipBox); }
        set { _toolTipBox = DependencyObject.BoxObject(value); }
    }
    public object Columns {
        get { return DependencyObject.Unbox(_columnsBox); }
        set { _columnsBox = DependencyObject.BoxObject(value); }
    }

    public GridViewHeaderRowPresenter() : base() {}
    public override string GetTypeName() { return "GridViewHeaderRowPresenter"; }
}

public class GridViewRowPresenter : FrameworkElement {
    private DependencyObject _contentBox;
    private DependencyObject _columnsBox;

    public object Content {
        get { return DependencyObject.Unbox(_contentBox); }
        set { _contentBox = DependencyObject.BoxObject(value); }
    }
    public object Columns {
        get { return DependencyObject.Unbox(_columnsBox); }
        set { _columnsBox = DependencyObject.BoxObject(value); }
    }

    public GridViewRowPresenter() : base() {}
    public override string GetTypeName() { return "GridViewRowPresenter"; }
}

public class ListView : ListBox {
    private GridView _view;
    private Grid _headerRow;

    public ListView() : base() {}
    public override string GetTypeName() { return "ListView"; }

    public GridView View {
        get { return _view; }
        set { _view = value; RebuildHeaderColumns(); RefreshItems(); }
    }

    private void RebuildHeaderColumns() {
        if (_headerRow == null) { return; }
        _headerRow.ClearChildren();
        _headerRow.ClearColumnDefinitions();
        if (_view == null) {
            _headerRow.Visibility = Visibility.Collapsed;
            return;
        }
        GridViewColumn[] cols = _view.Columns();
        if (cols.Length == 0) {
            _headerRow.Visibility = Visibility.Collapsed;
            return;
        }
        _headerRow.Visibility = Visibility.Visible;
        for (int i = 0; i < cols.Length; i += 1) {
            _headerRow.AddColumnDefinition(GridUnitType.Pixel, cols[i].Width);
            var hCell = new Border();
            hCell.Background = new SolidColorBrush("#FF232A38");
            hCell.Padding = new Thickness(8.0f, 6.0f, 8.0f, 6.0f);
            hCell.BorderBrush = new SolidColorBrush("#FF364054");
            hCell.BorderThickness = new Thickness(0.0f, 0.0f, 1.0f, 1.0f);
            var hText = new TextBlock(cols[i].Header);
            hText.FontWeight = "Bold";
            hText.FontSize = 13.0f;
            hText.Foreground = new SolidColorBrush("#FFCBD5E1");
            hCell.Child = hText;
            UIElement.SetGridColumn(hCell, i);
            _headerRow.AddChild(hCell);
        }
    }

    protected override UIElement CreateContainerForItem() {
        return new ListViewItem();
    }

    protected override void PrepareContainerForItem(UIElement container, DependencyObject itemBox, int index) {
        if (_view == null || _view.Columns().Length == 0) {
            base.PrepareContainerForItem(container, itemBox, index);
            return;
        }
        if (!(container is ListViewItem)) {
            base.PrepareContainerForItem(container, itemBox, index);
            return;
        }
        ListViewItem lvi = container as ListViewItem;
        lvi.ItemIndex = index;
        lvi.OwnerSelector = this;
        lvi.SetSelectedSilent(index == SelectedIndex || SelectedIndices().Contains(index));
        object rawData = DependencyObject.Unbox(itemBox);
        if (rawData != null) { lvi.DataContext = rawData; }

        GridViewColumn[] cols = _view.Columns();
        var rowGrid = new Grid();
        for (int i = 0; i < cols.Length; i += 1) {
            GridViewColumn col = cols[i];
            rowGrid.AddColumnDefinition(GridUnitType.Pixel, col.Width);
            UIElement cellEl = null;
            if (col.CellTemplate != null) {
                cellEl = col.CellTemplate.Instantiate(rawData);
            } else {
                string path = col.DisplayMemberBinding != null ? col.DisplayMemberBinding.Path : "";
                string text = "";
                if (rawData != null && path.Length > 0) {
                    text = DependencyObject.UnboxString(BindingEngine.ReadPathBox(rawData, path), "");
                } else {
                    text = DependencyObject.UnboxString(itemBox, "");
                }
                var tb = new TextBlock(text);
                tb.Padding = new Thickness(6.0f, 2.0f, 6.0f, 2.0f);
                tb.VerticalAlignment = VerticalAlignment.Center;
                tb.TextTrimming = TextTrimming.CharacterEllipsis;
                tb.ClipToBounds = true;
                cellEl = tb;
            }
            if (cellEl != null) {
                UIElement.SetGridColumn(cellEl, i);
                rowGrid.AddChild(cellEl);
            }
        }
        lvi.Content = rowGrid;
    }

    protected override UIElement CreateDefaultTemplate() {
        var root = new Grid();
        root.AddRowDefinition(GridUnitType.Auto, 1.0f);
        root.AddRowDefinition(GridUnitType.Star, 1.0f);

        _headerRow = new Grid();
        UIElement.SetGridRow(_headerRow, 0);

        UIElement listBody = base.CreateDefaultTemplate();
        UIElement.SetGridRow(listBody, 1);

        root.AddChild(_headerRow);
        root.AddChild(listBody);
        RebuildHeaderColumns();
        return root;
    }
}

// ============================================================================
// 4. ComboBox (Real Popup Dropdown)
// ============================================================================

public class ComboBox : Selector {
    private bool _isDropDownOpen = false;
    private bool _isEditable = false;
    private float _maxDropDownHeight = 240.0f;

    private Border _headerBox;
    private ContentPresenter _selectionDisplay;
    private Path _arrowIcon;
    private Popup _dropdownPopup;
    private ToggleButton _dropDownToggle;
    private Border _dropBorder;
    private ScrollViewer _dropScroll;
    private bool _pendingScrollToSelected = false;
    private static ComboBox[] _scrollPendingCombos;
    private static bool _scrollHookReg = false;

    public event EventHandler DropDownOpened;
    public event EventHandler DropDownClosed;

    public ComboBox() : base() {
    }

    public override string GetTypeName() { return "ComboBox"; }

    public bool IsEditable { get { return _isEditable; } set { _isEditable = value; } }
    public string Placeholder { get; set; } = "";
    public float MaxDropDownHeight {
        get { return _maxDropDownHeight; }
        set { _maxDropDownHeight = value > 40.0f ? value : 240.0f; }
    }

    public bool IsDropDownOpen {
        get { return _isDropDownOpen; }
        set {
            if (_isDropDownOpen == value) { return; }
            _isDropDownOpen = value;
            if (_isDropDownOpen) {
                SyncDropDownDimensions();
            }
            if (_dropDownToggle != null && _dropDownToggle.IsChecked != _isDropDownOpen) {
                _dropDownToggle.IsChecked = _isDropDownOpen;
            }
            if (_dropdownPopup != null) {
                _dropdownPopup.IsOpen = _isDropDownOpen;
            }
            if (_isDropDownOpen) {
                QueueScrollSelectedIntoView();
            } else {
                _pendingScrollToSelected = false;
            }
            UpdateHeaderVisual();
            BindingEngine.NotifyTargetPropertyChanged(this, "IsDropDownOpen", DependencyObject.BoxBool(_isDropDownOpen));
            UpdateTriggersAndStates();
            if (_isDropDownOpen) { DropDownOpened(this, EventArgs.Empty()); }
            else { DropDownClosed(this, EventArgs.Empty()); }
        }
    }

    private void SyncSelectedPreview() {
        if (_selectionDisplay == null) { return; }
        DependencyObject box = SelectedIndex >= 0 ? GetItemBoxAt(SelectedIndex) : _selectedItemBox;
        if (box == null) {
            _selectionDisplay.SetContent("");
            return;
        }
        if (box is UIElement) {
            if (box is ContentControl) {
                _selectionDisplay.SetContentBox((box as ContentControl).ContentBox);
            } else {
                _selectionDisplay.SetContent(box.ToDisplayString());
            }
            return;
        }
        object raw = DependencyObject.Unbox(box);
        if (DisplayMemberPath.Length > 0 && raw != null) {
            _selectionDisplay.SetContentBox(BindingEngine.ReadPathBox(raw, DisplayMemberPath));
        } else if (ItemTemplate != null) {
            _selectionDisplay.ContentTemplate = ItemTemplate;
            _selectionDisplay.SetContentBox(box);
        } else {
            _selectionDisplay.SetContentBox(box);
        }
    }

    protected override UIElement CreateContainerForItem() {
        return new ComboBoxItem();
    }

    private static ContentPresenter FindSelectionBoxPresenter(UIElement rootNode) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node is ContentPresenter) {
                ContentPresenter cp = node as ContentPresenter;
                if (cp.ContentSource == "SelectionBoxItem") { return cp; }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    private static ToggleButton FindDescendantToggleButton(UIElement rootNode) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node is ToggleButton) { return node as ToggleButton; }
            if (node is Popup) { continue; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    private void NotifyPopupClosedInternal() {
        _isDropDownOpen = false;
        if (_dropDownToggle != null && _dropDownToggle.IsChecked) {
            _dropDownToggle.IsChecked = false;
        }
        BindingEngine.NotifyTargetPropertyChanged(this, "IsDropDownOpen", DependencyObject.BoxBool(false));
        UpdateTriggersAndStates();
        DropDownClosed(this, EventArgs.Empty());
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        if (Template != null) {
            _selectionDisplay = null;
            _dropdownPopup = null;
            _dropDownToggle = null;
            UIElement site = GetTemplateChild("ContentSite");
            if (!(site is ContentPresenter)) { site = GetTemplateChild("PART_SelectionBox"); }
            if (site is ContentPresenter) { _selectionDisplay = site as ContentPresenter; }
            else { _selectionDisplay = FindSelectionBoxPresenter(TemplateRoot()); }
            UIElement tog = GetTemplateChild("DropDownButton");
            if (!(tog is ToggleButton)) { tog = GetTemplateChild("PART_ToggleButton"); }
            if (tog is ToggleButton) { _dropDownToggle = tog as ToggleButton; }
            else { _dropDownToggle = FindDescendantToggleButton(TemplateRoot()); }
            if (_dropDownToggle != null && _dropDownToggle.IsChecked != _isDropDownOpen) {
                _dropDownToggle.IsChecked = _isDropDownOpen;
            }
            UIElement pop = GetTemplateChild("PART_Popup");
            if (pop is Popup) {
                _dropdownPopup = pop as Popup;
                _dropdownPopup.PlacementTarget = this;
                _dropdownPopup.IsOpen = _isDropDownOpen;
                _dropdownPopup.Closed += (object s, EventArgs e) => { NotifyPopupClosedInternal(); };
            }
        }
        SyncSelectedPreview();
    }

    protected override void OnSelectionChanged(int oldIdx, int newIdx, DependencyObject oldBox, DependencyObject newBox) {
        base.OnSelectionChanged(oldIdx, newIdx, oldBox, newBox);
        SyncSelectedPreview();
    }

    public override void NotifyItemClicked(SelectableItem item, int index, int modifiers) {
        base.NotifyItemClicked(item, index, modifiers);
        IsDropDownOpen = false;
    }

    public override void RefreshItems() {
        base.RefreshItems();
        SyncSelectedPreview();
        UpdateTriggersAndStates();
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsDropDownOpen") { IsDropDownOpen = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "MaxDropDownHeight") { MaxDropDownHeight = DependencyObject.UnboxFloat(value, 240.0f); return true; }
        bool handled = base.SetPropertyValue(propName, value);
        if (handled && propName == "ItemTemplate") { SyncSelectedPreview(); }
        return handled;
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsDropDownOpen") { return DependencyObject.BoxBool(_isDropDownOpen); }
        if (propName == "MaxDropDownHeight") { return DependencyObject.BoxFloat(_maxDropDownHeight); }
        if (propName == "HasItems") { return DependencyObject.BoxBool(ItemCount() > 0); }
        if (propName == "SelectionBoxItem") {
            return SelectedIndex >= 0 ? GetItemBoxAt(SelectedIndex) : _selectedItemBox;
        }
        return base.GetPropertyValue(propName);
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (e.Key == KeyCode.Escape && _isDropDownOpen) {
            IsDropDownOpen = false;
            e.Handled = true;
        } else if (e.Key == KeyCode.Down) {
            if (!_isDropDownOpen) { IsDropDownOpen = true; }
            else if (SelectedIndex < ItemCount() - 1) { SelectedIndex = SelectedIndex + 1; }
            e.Handled = true;
        } else if (e.Key == KeyCode.Up && SelectedIndex > 0) {
            SelectedIndex = SelectedIndex - 1;
            e.Handled = true;
        } else if (e.Key == KeyCode.Enter || e.Key == KeyCode.Space) {
            IsDropDownOpen = !IsDropDownOpen;
            e.Handled = true;
        }
    }

    private void SyncDropDownDimensions() {
        if (_dropBorder == null) { return; }
        float w = ActualWidth > 0.0f ? ActualWidth : (_headerBox != null && _headerBox.ActualWidth > 0.0f ? _headerBox.ActualWidth : 120.0f);
        _dropBorder.Width = Math.Maxf(w, 120.0f);
        _dropBorder.MaxHeight = _maxDropDownHeight;
    }

    private void UpdateHeaderVisual() {
        if (_headerBox == null) { return; }
        if (!IsEnabled) {
            _headerBox.Background = new SolidColorBrush("#FF22262A");
            _headerBox.BorderBrush = new SolidColorBrush("#FF464B50");
        } else if (_isDropDownOpen || IsMouseOver) {
            _headerBox.Background = new SolidColorBrush("#FF33383D");
            _headerBox.BorderBrush = new SolidColorBrush("#FFA4AAB0");
        } else {
            _headerBox.Background = new SolidColorBrush("#FF2B3034");
            _headerBox.BorderBrush = new SolidColorBrush("#FF6B7278");
        }
    }

    private void QueueScrollSelectedIntoView() {
        if (_dropScroll == null || SelectedIndex < 0) { return; }
        _pendingScrollToSelected = true;
        if (!TryScrollSelectedIntoView()) {
            float estRow = 34.0f;
            float estOff = Math.Maxf(0.0f, SelectedIndex * estRow - (_maxDropDownHeight - estRow) * 0.5f);
            _dropScroll.ScrollToVerticalOffset(estOff);
            EnsureScrollPendingHook();
            _scrollPendingCombos.Push(this);
        }
    }

    private bool TryScrollSelectedIntoView() {
        if (_dropScroll == null || SelectedIndex < 0 || SelectedIndex >= ContainerCount()) {
            _pendingScrollToSelected = false;
            return true;
        }
        float vh = _dropScroll.ViewportHeight;
        UIElement c = ContainerAt(SelectedIndex);
        if (vh <= 0.0f || c == null || c.ActualHeight <= 0.0f) { return false; }
        float top = c.LayoutY;
        float h = c.ActualHeight;
        float cur = _dropScroll.VerticalOffset;
        if (top < cur || top + h > cur + vh) {
            _dropScroll.ScrollToVerticalOffset(Math.Maxf(0.0f, top - (vh - h) * 0.5f));
        }
        _pendingScrollToSelected = false;
        return true;
    }

    private static void EnsureScrollPendingHook() {
        if (_scrollPendingCombos == null) { _scrollPendingCombos = new ComboBox[0]; }
        if (_scrollHookReg) { return; }
        _scrollHookReg = true;
        BindingEngine.AddPreFlushHook(() => { ComboBox.FlushPendingScrolls(); });
    }

    public static void FlushPendingScrolls() {
        if (_scrollPendingCombos == null || _scrollPendingCombos.Length == 0) { return; }
        var remaining = new ComboBox[0];
        for (int i = 0; i < _scrollPendingCombos.Length; i += 1) {
            ComboBox cb = _scrollPendingCombos[i];
            if (cb == null || !cb._pendingScrollToSelected || !cb._isDropDownOpen) { continue; }
            if (!cb.TryScrollSelectedIntoView()) {
                remaining.Push(cb);
            }
        }
        _scrollPendingCombos = remaining;
    }

    protected override void SyncPadding() {
        if (Template != null) {
            base.SyncPadding();
            return;
        }
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetPadding(__nativeHandle, 0.0f, 0.0f, 0.0f, 0.0f);
        }
        if (_selectionDisplay != null) {
            Thickness pad = Padding;
            float l = pad.Left > 0.0f ? pad.Left : 8.0f;
            float t = pad.Top > 0.0f ? pad.Top : 5.0f;
            float r = pad.Right > 0.0f ? pad.Right : 6.0f;
            float b = pad.Bottom > 0.0f ? pad.Bottom : 5.0f;
            _selectionDisplay.Margin = new Thickness(l, t, r, b);
        }
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        UpdateHeaderVisual();
    }

    protected override UIElement CreateDefaultTemplate() {
        var root = new Grid();
        root.AddColumnDefinition(GridUnitType.Star, 1.0f);
        root.AddColumnDefinition(GridUnitType.Auto, 1.0f);

        _headerBox = new Border();
        _headerBox.Background = new SolidColorBrush("#FF2B3034");
        _headerBox.BorderBrush = new SolidColorBrush("#FF6B7278");
        _headerBox.BorderThickness = new Thickness(1.0f);
        _headerBox.CornerRadius = new CornerRadius(1.5f);
        _headerBox.MinWidth = 100.0f;
        _headerBox.MinHeight = 28.0f;
        _headerBox.HorizontalAlignment = HorizontalAlignment.Stretch;
        UIElement.SetGridColumnSpan(_headerBox, 2);

        _selectionDisplay = new ContentPresenter();
        _selectionDisplay.HorizontalAlignment = HorizontalAlignment.Left;
        _selectionDisplay.VerticalAlignment = VerticalAlignment.Center;
        _selectionDisplay.Margin = new Thickness(8.0f, 5.0f, 6.0f, 5.0f);
        UIElement.SetGridColumn(_selectionDisplay, 0);

        _arrowIcon = new Path("M 1,3 L 6,8 L 11,3");
        _arrowIcon.Width = 12.0f;
        _arrowIcon.Height = 10.0f;
        _arrowIcon.Margin = new Thickness(0.0f, 5.0f, 8.0f, 5.0f);
        _arrowIcon.Stroke = new SolidColorBrush("#FFD0D4D8");
        _arrowIcon.StrokeThickness = 1.25f;
        _arrowIcon.Stretch = Stretch.None;
        _arrowIcon.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_arrowIcon, 1);

        _dropdownPopup = new Popup();
        _dropdownPopup.Name = "PART_Popup";
        _dropdownPopup.PlacementTarget = this;
        _dropdownPopup.StaysOpen = false;
        _dropdownPopup.VerticalOffset = 0.0f;
        _dropdownPopup.Closed += (object s, EventArgs e) => { NotifyPopupClosedInternal(); };

        _dropBorder = new Border();
        _dropBorder.Background = new SolidColorBrush("#FF252A2E");
        _dropBorder.BorderBrush = new SolidColorBrush("#FF4B5258");
        _dropBorder.BorderThickness = new Thickness(1.0f);
        _dropBorder.CornerRadius = new CornerRadius(0.0f);
        _dropBorder.Padding = new Thickness(0.0f);
        _dropBorder.MaxHeight = _maxDropDownHeight;
        _dropBorder.MinWidth = 100.0f;

        _dropScroll = new ScrollViewer();
        _dropScroll.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
        _dropScroll.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.Name = "PART_ItemsPresenter";
        if (ItemsPanelFactory() != null) { _itemsPresenter.PanelFactory = ItemsPanelFactory(); }
        _itemsPresenter.EnsurePanel();
        _dropScroll.Content = _itemsPresenter;
        _dropBorder.Child = _dropScroll;
        _dropdownPopup.Child = _dropBorder;

        root.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
            if (IsEnabled) { IsDropDownOpen = !IsDropDownOpen; e.Handled = true; }
        };

        root.AddChild(_headerBox);
        root.AddChild(_selectionDisplay);
        root.AddChild(_arrowIcon);
        root.AddChild(_dropdownPopup);
        UpdateHeaderVisual();
        RefreshItems();
        return root;
    }
}

// ============================================================================
// 5. TabItem & TabControl
// ============================================================================

public class TabItem : HeaderedContentControl {
    private bool _isSelected = false;
    private int _tabIndex = -1;
    private TabControl _ownerTabControl;
    private Border _accentUnderline;

    public event RoutedEventHandler Selected;
    public event RoutedEventHandler Unselected;

    public TabItem() : base() {
        Padding = new Thickness(12.0f, 5.0f, 12.0f, 5.0f);
        Focusable = true;
    }
    public TabItem(string headerText, UIElement content = null) : this() {
        SetHeader(headerText);
        if (content != null) { Content = content; }
    }

    public override string GetTypeName() { return "TabItem"; }

    public int TabIndex { get { return _tabIndex; } set { _tabIndex = value; } }
    public TabControl OwnerTabControl { get { return _ownerTabControl; } set { _ownerTabControl = value; } }

    public bool IsSelected {
        get { return _isSelected; }
        set {
            if (_isSelected == value) { return; }
            _isSelected = value;
            UpdateTabVisual();
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("IsSelected", DependencyObject.BoxBool(_isSelected));
            if (_isSelected) { Selected(this, new RoutedEventArgs("Selected", this)); }
            else { Unselected(this, new RoutedEventArgs("Unselected", this)); }
        }
    }

    internal void SetSelectedSilent(bool sel) {
        if (_isSelected == sel) { return; }
        _isSelected = sel;
        UpdateTabVisual();
        UpdateTriggersAndStates();
        NotifyBoundPropertyChanged("IsSelected", DependencyObject.BoxBool(_isSelected));
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsSelected") { IsSelected = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsSelected") { return DependencyObject.BoxBool(_isSelected); }
        return base.GetPropertyValue(propName);
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        UpdateTabVisual();
    }

    protected override void SyncPadding() {
        if (Template == null && _headerPresenter != null) {
            _headerPresenter.Margin = Padding;
            if (ParseUtil.IsValidHandle(__nativeHandle)) {
                Visual_SetPadding(__nativeHandle, 0.0f, 0.0f, 0.0f, 0.0f);
            }
            return;
        }
        base.SyncPadding();
    }

    private void UpdateTabVisual() {
        if (Style == null) {
            Background = new SolidColorBrush(_isSelected ? "#FF282D32" : (IsMouseOver ? "#FF23272B" : "#00000000"));
            Foreground = new SolidColorBrush(_isSelected ? "#FFF1F5F9" : "#FFD0D6DC");
        }
        if (_accentUnderline != null) {
            _accentUnderline.Visibility = _isSelected ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        if (_ownerTabControl != null) {
            _ownerTabControl.SelectTabIndex(_tabIndex);
            e.Handled = true;
        }
    }

    public override void OnApplyTemplate() {
        if (Template == null) { return; }
        _presenter = null;
        _headerPresenter = null;
        UIElement hFound = GetTemplateChild("PART_HeaderPresenter");
        if (!(hFound is ContentPresenter)) { hFound = GetTemplateChild("HeaderHost"); }
        if (!(hFound is ContentPresenter)) {
            UIElement ch = GetTemplateChild("ContentHost");
            if (ch is ContentPresenter && (ch as ContentPresenter).ContentSource == "Header") {
                hFound = ch;
            }
        }
        if (hFound is ContentPresenter) {
            _headerPresenter = hFound as ContentPresenter;
        } else {
            _headerPresenter = ContentControl.FindFirstTemplatedPresenter(TemplateRoot(), this, true);
        }
        if (_headerPresenter != null) {
            _headerPresenter.ContentTemplate = HeaderTemplate;
            _headerPresenter.SetContentBox(HeaderBox);
        }
        UpdateTabVisual();
    }

    protected override UIElement CreateDefaultTemplate() {
        var box = new StackPanel(Orientation.Vertical, 0.0f);

        _accentUnderline = new Border();
        _accentUnderline.Height = 2.0f;
        _accentUnderline.Background = new SolidColorBrush("#FF2D7EC4");
        _accentUnderline.HorizontalAlignment = HorizontalAlignment.Stretch;

        _headerPresenter = new ContentPresenter();
        _headerPresenter.Name = "PART_HeaderPresenter";
        _headerPresenter.ContentSource = "Header";
        _headerPresenter.HorizontalAlignment = HorizontalAlignment.Center;
        _headerPresenter.VerticalAlignment = VerticalAlignment.Center;
        _headerPresenter.Margin = Padding;
        _headerPresenter.ContentTemplate = HeaderTemplate;
        _headerPresenter.SetContentBox(HeaderBox);

        box.AddChild(_accentUnderline);
        box.AddChild(_headerPresenter);
        SyncPadding();
        UpdateTabVisual();
        return box;
    }
}

public class TabControl : Selector {
    private TabStripPlacement _tabStripPlacement = TabStripPlacement.Top;
    private Panel _tabStrip;
    private Border _contentFrame;
    private ContentPresenter _selectedContentHost;

    public TabControl() : base() {
        Background = Brushes.Transparent();
        Padding = new Thickness(10.0f);
    }

    public override string GetTypeName() { return "TabControl"; }

    public TabStripPlacement TabStripPlacement {
        get { return _tabStripPlacement; }
        set { _tabStripPlacement = value; ApplyTemplate(); }
    }

    public object SelectedContent {
        get { return _selectedContentHost != null ? DependencyObject.Unbox(_selectedContentHost.ContentBox) : null; }
    }

    protected override void SyncPadding() {
        if (_contentFrame != null) {
            _contentFrame.Padding = Padding;
        }
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetPadding(__nativeHandle, 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    public void SelectTabIndex(int idx) {
        SetSelectedIndexInternal(idx, true);
    }

    protected override bool IsItemItsOwnContainer(DependencyObject itemBox) {
        return itemBox is TabItem;
    }
    protected override bool IsContainerRecyclable(UIElement c) {
        return false;
    }
    protected override UIElement CreateContainerForItem() {
        var ti = new TabItem();
        return ti;
    }

    protected override void OnSelectionChanged(int oldIdx, int newIdx, DependencyObject oldBox, DependencyObject newBox) {
        base.OnSelectionChanged(oldIdx, newIdx, oldBox, newBox);
        SyncTabSelectionVisuals();
        BindingEngine.NotifyTargetPropertyChanged(this, "SelectedContent", _selectedContentHost != null ? _selectedContentHost.ContentBox : null);
    }

    private void SyncTabSelectionVisuals() {
        for (int i = 0; i < _activeContainers.Length; i += 1) {
            if (_activeContainers[i] is TabItem) {
                (_activeContainers[i] as TabItem).SetSelectedSilent(i == SelectedIndex);
            }
        }
        if (_selectedContentHost == null) { return; }
        if (SelectedIndex < 0 || SelectedIndex >= ItemCount()) {
            _selectedContentHost.SetContentBox(null);
            return;
        }
        DependencyObject itemBox = GetItemBoxAt(SelectedIndex);
        if (itemBox is TabItem) {
            TabItem ti = itemBox as TabItem;
            _selectedContentHost.ContentTemplate = ti.ContentTemplate;
            _selectedContentHost.SetContentBox(ti.ContentBox);
        } else {
            _selectedContentHost.ContentTemplate = ItemTemplate;
            _selectedContentHost.SetContentBox(itemBox);
        }
    }

    public override void RefreshItems() {
        if (_tabStrip == null) { return; }
        _tabStrip.ClearChildren();
        _activeContainers.Clear();
        int n = ItemCount();
        for (int i = 0; i < n; i += 1) {
            DependencyObject box = GetItemBoxAt(i);
            TabItem ti = null;
            if (box is TabItem) {
                ti = box as TabItem;
            } else {
                ti = new TabItem();
                ti.SetHeader(DependencyObject.UnboxString(box, "Tab"));
                ti.SetContentBox(box);
            }
            ti.TabIndex = i;
            ti.OwnerTabControl = this;
            _activeContainers.Push(ti);
            _tabStrip.AddChild(ti);
        }
        if (n > 0 && SelectedIndex < 0) {
            SetSelectedIndexInternal(0, true);
        } else {
            SyncTabSelectionVisuals();
        }
    }

    public override void OnApplyTemplate() {
        if (Template == null) {
            base.OnApplyTemplate();
            return;
        }
        _tabStrip = null;
        _contentFrame = null;
        _selectedContentHost = null;
        UIElement hp = GetTemplateChild("HeaderPanel");
        if (hp is Panel) { _tabStrip = hp as Panel; }
        else { _tabStrip = ItemsControl.FindItemsHostPanel(TemplateRoot(), this); }
        UIElement ch = GetTemplateChild("PART_SelectedContentHost");
        if (ch is ContentPresenter) { _selectedContentHost = ch as ContentPresenter; }
        RefreshItems();
    }

    protected override UIElement CreateDefaultTemplate() {
        var dock = new DockPanel();
        _tabStrip = new StackPanel(
            (_tabStripPlacement == TabStripPlacement.Left || _tabStripPlacement == TabStripPlacement.Right)
                ? Orientation.Vertical
                : Orientation.Horizontal,
            0.0f);
        _tabStrip.Background = Brushes.Transparent();
        _tabStrip.Padding = new Thickness(0.0f);

        Dock stripDock = Dock.Top;
        if (_tabStripPlacement == TabStripPlacement.Bottom) { stripDock = Dock.Bottom; }
        else if (_tabStripPlacement == TabStripPlacement.Left) { stripDock = Dock.Left; }
        else if (_tabStripPlacement == TabStripPlacement.Right) { stripDock = Dock.Right; }
        UIElement.SetDock(_tabStrip, stripDock);

        _contentFrame = new Border();
        _contentFrame.Background = new SolidColorBrush("#FF272C31");
        _contentFrame.Padding = Padding;
        _selectedContentHost = new ContentPresenter();
        _selectedContentHost.Name = "PART_SelectedContentHost";
        _contentFrame.Child = _selectedContentHost;

        dock.AddChild(_tabStrip);
        dock.AddChild(_contentFrame);
        SyncPadding();
        RefreshItems();
        return dock;
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "SelectedContent") {
            return _selectedContentHost != null ? _selectedContentHost.ContentBox : null;
        }
        return base.GetPropertyValue(propName);
    }
}
