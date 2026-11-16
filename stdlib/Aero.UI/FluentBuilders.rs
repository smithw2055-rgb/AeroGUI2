module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Code-First Factory Functions (module Aero.UI;)
// ============================================================================

public static StackPanel VStack(float spacing = 8.0f) {
    return new StackPanel(Orientation.Vertical, spacing);
}

public static StackPanel VStack(float spacing, UIElement[] children) {
    var sp = new StackPanel(Orientation.Vertical, spacing);
    AeroFluent.AppendAll(sp, children);
    return sp;
}

public static StackPanel HStack(float spacing = 8.0f) {
    return new StackPanel(Orientation.Horizontal, spacing);
}

public static StackPanel HStack(float spacing, UIElement[] children) {
    var sp = new StackPanel(Orientation.Horizontal, spacing);
    AeroFluent.AppendAll(sp, children);
    return sp;
}

public static Grid ZStack(UIElement[] children = null) {
    var g = new Grid();
    AeroFluent.AppendAll(g, children);
    return g;
}

public static Grid GridBox(string colsSpec = "*", string rowsSpec = "Auto", float spacing = 8.0f) {
    var g = new Grid();
    g.ColumnSpacing = spacing;
    g.RowSpacing = spacing;
    if (colsSpec != null && colsSpec.Length > 0) { g.SetColumnDefinitions(colsSpec); }
    if (rowsSpec != null && rowsSpec.Length > 0) { g.SetRowDefinitions(rowsSpec); }
    return g;
}

public static DockPanel DockBox(bool lastChildFill = true) {
    var dp = new DockPanel();
    dp.LastChildFill = lastChildFill;
    return dp;
}

public static WrapPanel WrapBox(float spacing = 8.0f, Orientation orientation = Orientation.Horizontal) {
    var wp = new WrapPanel();
    wp.Orientation = orientation;
    wp.Spacing = spacing;
    return wp;
}

public static Canvas CanvasBox() {
    return new Canvas();
}

public static ScrollViewer ScrollBox(UIElement content = null, ScrollBarVisibility vVis = ScrollBarVisibility.Auto) {
    var sv = new ScrollViewer();
    sv.VerticalScrollBarVisibility = vVis;
    if (content != null) { sv.Content = content; }
    return sv;
}

public static Border Card(UIElement child = null, float padding = 16.0f, float cornerRadius = 10.0f) {
    var b = new Border();
    b.Background = new SolidColorBrush("#FF1E2430");
    b.BorderBrush = new SolidColorBrush("#FF364054");
    b.BorderThickness = new Thickness(1.0f);
    b.CornerRadius = new CornerRadius(cornerRadius);
    b.Padding = new Thickness(padding);
    if (child != null) { b.Child = child; }
    return b;
}

public static Border Box(string bgHex = "#FF1B202B", float cornerRadius = 6.0f, float padding = 8.0f) {
    var b = new Border();
    if (bgHex != null && bgHex.Length > 0) { b.Background = new SolidColorBrush(bgHex); }
    b.CornerRadius = new CornerRadius(cornerRadius);
    b.Padding = new Thickness(padding);
    return b;
}

public static TextBlock Label(string text = "", float fontSize = 14.0f, string colorHex = "#FFF1F5F9") {
    var tb = new TextBlock(text);
    tb.FontSize = fontSize;
    tb.Foreground = new SolidColorBrush(colorHex);
    tb.VerticalAlignment = VerticalAlignment.Center;
    return tb;
}

public static TextBlock Heading(string text = "", float fontSize = 22.0f, string colorHex = "#FFF8FAFC") {
    var tb = new TextBlock(text);
    tb.FontSize = fontSize;
    tb.FontWeight = "Bold";
    tb.Foreground = new SolidColorBrush(colorHex);
    return tb;
}

public static TextBlock MutedLabel(string text = "", float fontSize = 12.0f) {
    return Label(text, fontSize, "#FF94A3B8");
}

public static Separator Divider() {
    return new Separator();
}

public static Image Pic(string sourceUri, Stretch stretch = Stretch.Uniform) {
    var img = new Image(sourceUri);
    img.Stretch = stretch;
    return img;
}

public static Path SvgPath(string pathData, string strokeHex = "#FFF1F5F9", float strokeWidth = 2.0f, string fillHex = "") {
    var p = new Path(pathData);
    if (strokeHex != null && strokeHex.Length > 0) { p.Stroke = new SolidColorBrush(strokeHex); }
    p.StrokeThickness = strokeWidth;
    if (fillHex != null && fillHex.Length > 0) { p.Fill = new SolidColorBrush(fillHex); }
    return p;
}

public static Button Btn(string label, Action onClick = null) {
    var b = new Button(label);
    if (onClick != null) {
        b.Click += (object s, RoutedEventArgs e) => {
            var fn = onClick;
            fn();
        };
    }
    return b;
}

public static Button PrimaryBtn(string label, Action onClick = null) {
    var b = Btn(label, onClick);
    b.Background = new SolidColorBrush("#FF2563EB");
    b.BorderBrush = new SolidColorBrush("#FF3B82F6");
    b.Foreground = Brushes.White();
    return b;
}

public static ToggleButton ToggleBtn(string label, bool initialChecked = false, Action<bool> onChanged = null) {
    var tb = new ToggleButton();
    tb.SetContent(label);
    tb.IsChecked = initialChecked;
    if (onChanged != null) {
        tb.Checked += (object s, RoutedEventArgs e) => { var fn = onChanged; fn(true); };
        tb.Unchecked += (object s, RoutedEventArgs e) => { var fn = onChanged; fn(false); };
    }
    return tb;
}

public static TextBox InputBox(string placeholder = "", string initialText = "", Action<string> onChanged = null) {
    var tb = new TextBox(initialText, placeholder);
    if (onChanged != null) {
        tb.TextChanged += (object s, TextChangedEventArgs e) => { var fn = onChanged; fn(e.Text); };
    }
    return tb;
}

public static PasswordBox SecretBox(string placeholder = "Password") {
    return new PasswordBox(placeholder);
}

public static CheckBox Check(string label, bool initialChecked = false, Action<bool> onChanged = null) {
    var cb = new CheckBox(label, initialChecked);
    if (onChanged != null) {
        cb.Checked += (object s, RoutedEventArgs e) => { var fn = onChanged; fn(true); };
        cb.Unchecked += (object s, RoutedEventArgs e) => { var fn = onChanged; fn(false); };
    }
    return cb;
}

public static RadioButton Radio(string label, string groupName = "", bool initialChecked = false) {
    var rb = new RadioButton(label, groupName);
    rb.IsChecked = initialChecked;
    return rb;
}

public static Slider Slide(float min = 0.0f, float max = 100.0f, float val = 0.0f, Action<float> onChanged = null) {
    var sl = new Slider(min, max, val);
    if (onChanged != null) {
        sl.ValueChanged += (object s, RoutedPropertyChangedEventArgs<float> e) => {
            var fn = onChanged;
            fn(e.NewValue);
        };
    }
    return sl;
}

public static ProgressBar Progress(float min = 0.0f, float max = 100.0f, float val = 0.0f) {
    var pb = new ProgressBar();
    pb.Minimum = min;
    pb.Maximum = max;
    pb.Value = val;
    return pb;
}

public static ComboBox DropDown(string[] items = null, int selectedIndex = -1) {
    var cb = new ComboBox();
    if (items != null) {
        for (int i = 0; i < items.Length; i += 1) {
            cb.AddItem(items[i]);
        }
    }
    if (selectedIndex >= 0) { cb.SelectedIndex = selectedIndex; }
    return cb;
}

public static ListBox ListPanel(string[] items = null) {
    var lb = new ListBox();
    if (items != null) {
        for (int i = 0; i < items.Length; i += 1) {
            lb.AddItem(items[i]);
        }
    }
    return lb;
}

public static TabControl Tabs(TabItem[] tabs = null, TabStripPlacement placement = TabStripPlacement.Top) {
    var tc = new TabControl();
    tc.TabStripPlacement = placement;
    if (tabs != null) {
        for (int i = 0; i < tabs.Length; i += 1) {
            tc.AddItem(tabs[i]);
        }
    }
    return tc;
}

public static TreeView Tree(TreeViewItem[] nodes = null) {
    var tv = new TreeView();
    if (nodes != null) {
        for (int i = 0; i < nodes.Length; i += 1) {
            tv.AddItem(nodes[i]);
        }
    }
    return tv;
}

public static Window Win(string title, float width = 1024.0f, float height = 720.0f, UIElement content = null) {
    var w = new Window();
    w.Title = title;
    w.Width = width;
    w.Height = height;
    if (content != null) { w.Content = content; }
    return w;
}

// ============================================================================
// 2. Fluent Chainable Modifiers (AeroFluent)
// ============================================================================

public static class AeroFluent {
    public static void AppendAll(UIElement parent, UIElement[] children) {
        if (parent == null || children == null) { return; }
        for (int i = 0; i < children.Length; i += 1) {
            if (children[i] != null) { parent.AddChild(children[i]); }
        }
    }

    public static UIElement WithChildren(UIElement parent, UIElement[] children) {
        AppendAll(parent, children);
        return parent;
    }

    public static UIElement WithPadding(UIElement el, float all) {
        if (el != null) { el.Padding = new Thickness(all); }
        return el;
    }
    public static UIElement WithPadding(UIElement el, float h, float v) {
        if (el != null) { el.Padding = new Thickness(h, v); }
        return el;
    }

    public static UIElement WithMargin(UIElement el, float all) {
        if (el != null) { el.Margin = new Thickness(all); }
        return el;
    }
    public static UIElement WithMargin(UIElement el, float h, float v) {
        if (el != null) { el.Margin = new Thickness(h, v); }
        return el;
    }

    public static UIElement WithSize(UIElement el, float w, float h) {
        if (el != null) { el.Width = w; el.Height = h; }
        return el;
    }

    public static UIElement WithBg(UIElement el, string hexColor) {
        if (el != null) { el.Background = new SolidColorBrush(hexColor); }
        return el;
    }

    public static UIElement WithBorder(UIElement el, string hexColor, float thickness = 1.0f, float radius = 6.0f) {
        if (el != null) {
            el.BorderBrush = new SolidColorBrush(hexColor);
            el.BorderThickness = new Thickness(thickness);
            el.CornerRadius = new CornerRadius(radius);
        }
        return el;
    }

    public static UIElement WithAlign(UIElement el, HorizontalAlignment hAlign, VerticalAlignment vAlign) {
        if (el != null) { el.HorizontalAlignment = hAlign; el.VerticalAlignment = vAlign; }
        return el;
    }

    public static UIElement WithGridCell(UIElement el, int row, int col, int rowSpan = 1, int colSpan = 1) {
        UIElement.SetGridRow(el, row);
        UIElement.SetGridColumn(el, col);
        UIElement.SetGridRowSpan(el, rowSpan);
        UIElement.SetGridColumnSpan(el, colSpan);
        return el;
    }

    public static UIElement WithDock(UIElement el, Dock dock) {
        UIElement.SetDock(el, dock);
        return el;
    }

    public static UIElement WithCanvasPos(UIElement el, float left, float top) {
        UIElement.SetCanvasLeft(el, left);
        UIElement.SetCanvasTop(el, top);
        return el;
    }

    public static UIElement WithShadow(UIElement el, float blur = 12.0f, float depth = 4.0f, float opacity = 0.45f) {
        if (el != null) { el.Effect = new DropShadowEffect(blur, depth, 315.0f, "#FF000000", opacity); }
        return el;
    }

    public static UIElement WithDataContext(UIElement el, object dc) {
        if (el != null) { el.DataContext = dc; }
        return el;
    }

    public static TextBlock BindText(TextBlock tb, Func<string> getter) {
        if (tb != null && getter != null) {
            BindingEngine.BindLambdaString(tb, "Text", getter);
        }
        return tb;
    }

    public static UIElement BindVisible(UIElement el, Func<bool> predicate) {
        if (el != null && predicate != null) {
            BindingEngine.BindLambdaOneWay(el, "Visibility", () => {
                var fn = predicate;
                return DependencyObject.BoxInt(fn() ? 0 : 2);
            });
        }
        return el;
    }
}
