module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. TextBoxBase (Shared UTF-8 Code-Point Editing, Selection, Clipboard, IME)
// ============================================================================

public class TextBoxBase : Control {
    private string _rawText = "";
    private string _placeholderText = "";
    private bool _isReadOnly = false;
    private bool _acceptsReturn = false;
    private int _maxLength = 0;
    private Brush _caretBrush;
    private Brush _selectionBrush;

    private int _caretCp = 0;
    private int _anchorCp = 0;
    private bool _dragSelecting = false;

    private static bool _caretHookInstalled = false;
    private static TextBoxBase _activeBlinkBox;
    private static float _caretBlinkTimer = 0.0f;
    private static bool _caretBlinkVisible = true;

    protected TextBlock _textVisual;
    protected TextBlock _placeholderVisual;

    public event TextChangedEventHandler TextChanged;

    private static void EnsureCaretBlinkHook() {
        if (_caretHookInstalled) { return; }
        _caretHookInstalled = true;
        BindingEngine.AddFrameTickHook((float dt) => { TextBoxBase.StepCaretBlink(dt); });
    }

    public static void StepCaretBlink(float dt) {
        if (_activeBlinkBox == null || _activeBlinkBox._isDisposedTree ||
            !_activeBlinkBox.IsFocused || _activeBlinkBox._isReadOnly) {
            _activeBlinkBox = null;
            return;
        }
        _caretBlinkTimer += dt;
        if (_caretBlinkTimer >= 0.53f) {
            while (_caretBlinkTimer >= 0.53f) {
                _caretBlinkTimer -= 0.53f;
            }
            _caretBlinkVisible = !_caretBlinkVisible;
            _activeBlinkBox.RefreshTextSelectionVisual();
        }
        if (ParseUtil.IsNativeReady()) {
            int remainMs = (int)((0.53f - _caretBlinkTimer) * 1000.0f) + 1;
            if (remainMs < 16) { remainMs = 16; }
            Host_RequestWakeAfterMs(remainMs);
        }
    }

    protected void ResetCaretBlinkPhase() {
        EnsureCaretBlinkHook();
        _caretBlinkTimer = 0.0f;
        _caretBlinkVisible = true;
        if (IsFocused && !_isReadOnly) {
            _activeBlinkBox = this;
            if (ParseUtil.IsNativeReady()) {
                Host_RequestWakeAfterMs(530);
            }
        }
    }

    protected TextBoxBase() : base() {
        EnsureCaretBlinkHook();
        Focusable = true;
        ClipToBounds = true;
        Background = new SolidColorBrush("#FF141821");
        Foreground = new SolidColorBrush("#FFF1F5F9");
        BorderBrush = new SolidColorBrush("#FF364054");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(5.0f);
        Padding = new Thickness(8.0f, 6.0f, 8.0f, 6.0f);
        MinWidth = 120.0f;
        MinHeight = 32.0f;
        _caretBrush = new SolidColorBrush("#FFF1F5F9");
        _selectionBrush = new SolidColorBrush("#663B82F6");
    }

    public override string GetTypeName() { return "TextBoxBase"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "TextChanged") {
            TextChanged += (object s, TextChangedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    public bool IsReadOnly {
        get { return _isReadOnly; }
        set { _isReadOnly = value; RefreshTextVisual(); SyncImeState(); }
    }
    public bool AcceptsReturn {
        get { return _acceptsReturn; }
        set { _acceptsReturn = value; }
    }
    public int MaxLength {
        get { return _maxLength; }
        set { _maxLength = value >= 0 ? value : 0; }
    }
    public string PlaceholderText {
        get { return _placeholderText; }
        set {
            _placeholderText = value == null ? "" : value;
            BindingEngine.MarkFlushDirty();
            NotifyBoundPropertyChanged("Placeholder", DependencyObject.BoxString(_placeholderText));
            NotifyBoundPropertyChanged("Text.Placeholder", DependencyObject.BoxString(_placeholderText));
            RefreshTextVisual();
        }
    }
    public string Placeholder {
        get { return PlaceholderText; }
        set { PlaceholderText = value; }
    }
    public Brush CaretBrush {
        get { return _caretBrush; }
        set { _caretBrush = value; RefreshTextSelectionVisual(); }
    }
    public Brush SelectionBrush {
        get { return _selectionBrush; }
        set { _selectionBrush = value; RefreshTextSelectionVisual(); }
    }

    public int CaretIndex {
        get { return _caretCp; }
        set { MoveCaret(value, false); }
    }
    public int SelectionStart {
        get { return _caretCp < _anchorCp ? _caretCp : _anchorCp; }
    }
    public int SelectionLength {
        get {
            int d = _caretCp - _anchorCp;
            return d < 0 ? -d : d;
        }
    }

    protected string InternalRawText() { return _rawText; }

    protected void SetInternalRawText(string value, bool resetCaret = true) {
        string next = value == null ? "" : value;
        if (_maxLength > 0 && next.Length > _maxLength) {
            next = next.Substring(0, _maxLength);
        }
        if (_rawText == next) { return; }
        _rawText = next;
        int count = ParseUtil.Utf8CodePointCount(_rawText);
        if (resetCaret) {
            _caretCp = count;
            _anchorCp = count;
        } else {
            _caretCp = ClampCp(_caretCp);
            _anchorCp = ClampCp(_anchorCp);
        }
        ResetCaretBlinkPhase();
        BindingEngine.MarkFlushDirty();
        OnRawTextChanged(_rawText);
        RefreshTextVisual();
        UpdateTriggersAndStates();
        TextChanged(this, new TextChangedEventArgs(CanExposePlaintext() ? _rawText : "", this));
    }

    protected virtual string GetDisplayText() { return _rawText; }
    protected virtual bool CanCopyPlaintext() { return true; }
    protected virtual bool CanExposePlaintext() { return true; }
    protected virtual void OnRawTextChanged(string newRaw) {}

    public string GetSelectedText() {
        if (!CanCopyPlaintext()) { return ""; }
        int len = SelectionLength;
        if (len <= 0) { return ""; }
        return ParseUtil.SliceCodePoints(_rawText, SelectionStart, len);
    }

    private int ClampCp(int cp) {
        int maxCp = ParseUtil.Utf8CodePointCount(_rawText);
        if (cp < 0) { return 0; }
        if (cp > maxCp) { return maxCp; }
        return cp;
    }

    public void Select(int start, int length) {
        int s = ClampCp(start);
        int e = ClampCp(s + (length < 0 ? 0 : length));
        _anchorCp = s;
        _caretCp = e;
        RefreshTextSelectionVisual();
        SyncImeState();
    }

    public void SelectAll() {
        _anchorCp = 0;
        _caretCp = ParseUtil.Utf8CodePointCount(_rawText);
        RefreshTextSelectionVisual();
        SyncImeState();
    }

    public void MoveCaret(int targetCp, bool extendSelection) {
        _caretCp = ClampCp(targetCp);
        if (!extendSelection) { _anchorCp = _caretCp; }
        ResetCaretBlinkPhase();
        RefreshTextSelectionVisual();
        SyncImeState();
    }

    protected void ReplaceSelection(string insertText) {
        if (_isReadOnly) { return; }
        string ins = insertText == null ? "" : insertText;
        int start = SelectionStart;
        int selLen = SelectionLength;
        int totalCp = ParseUtil.Utf8CodePointCount(_rawText);

        string before = ParseUtil.SliceCodePoints(_rawText, 0, start);
        string after = ParseUtil.SliceCodePoints(_rawText, start + selLen, totalCp - (start + selLen));
        if (_maxLength > 0) {
            int remaining = _maxLength - (before.Length + after.Length);
            if (remaining <= 0) { ins = ""; }
            else if (ins.Length > remaining) { ins = ins.Substring(0, remaining); }
        }
        string next = before + ins + after;
        int nextCaret = start + ParseUtil.Utf8CodePointCount(ins);
        _rawText = next;
        _caretCp = ClampCp(nextCaret);
        _anchorCp = _caretCp;
        ResetCaretBlinkPhase();
        BindingEngine.MarkFlushDirty();
        OnRawTextChanged(_rawText);
        RefreshTextVisual();
        UpdateTriggersAndStates();
        SyncImeState();
        TextChanged(this, new TextChangedEventArgs(CanExposePlaintext() ? _rawText : "", this));
    }

    protected void DeleteBackward(bool wholeWord) {
        if (_isReadOnly) { return; }
        if (SelectionLength > 0) {
            ReplaceSelection("");
            return;
        }
        if (_caretCp <= 0) { return; }
        int from = wholeWord ? ParseUtil.FindPrevWordBoundary(_rawText, _caretCp) : (_caretCp - 1);
        _anchorCp = from;
        ReplaceSelection("");
    }

    protected void DeleteForward(bool wholeWord) {
        if (_isReadOnly) { return; }
        if (SelectionLength > 0) {
            ReplaceSelection("");
            return;
        }
        int totalCp = ParseUtil.Utf8CodePointCount(_rawText);
        if (_caretCp >= totalCp) { return; }
        int to = wholeWord ? ParseUtil.FindNextWordBoundary(_rawText, _caretCp) : (_caretCp + 1);
        _anchorCp = _caretCp;
        _caretCp = to;
        ReplaceSelection("");
    }

    public void Copy() {
        if (!CanCopyPlaintext()) { return; }
        string sel = GetSelectedText();
        if (sel.Length > 0) { Host_SetClipboardText(sel); }
    }

    public void Cut() {
        if (_isReadOnly || !CanCopyPlaintext()) { return; }
        string sel = GetSelectedText();
        if (sel.Length > 0) {
            Host_SetClipboardText(sel);
            ReplaceSelection("");
        }
    }

    public void Paste() {
        if (_isReadOnly) { return; }
        string clip = Host_GetClipboardText();
        if (clip != null && clip.Length > 0) {
            if (!_acceptsReturn) {
                clip = clip.Replace("\r", "").Replace("\n", " ");
            }
            ReplaceSelection(clip);
        }
    }

    protected void RefreshTextVisual() {
        if (_textVisual != null) {
            _textVisual.Text = GetDisplayText();
            _textVisual.Foreground = Foreground;
            _textVisual.FontFamily = FontFamily;
            _textVisual.FontSize = FontSize;
            _textVisual.FontWeight = FontWeight;
            _textVisual.FontStyle = FontStyle;
        }
        if (_placeholderVisual != null) {
            _placeholderVisual.Text = _placeholderText;
            _placeholderVisual.FontSize = FontSize;
            _placeholderVisual.Visibility = (_rawText.Length == 0 && _placeholderText.Length > 0)
                ? Visibility.Visible
                : Visibility.Collapsed;
        }
        RefreshTextSelectionVisual();
    }

    protected void RefreshTextSelectionVisual() {
        if (_textVisual == null || !ParseUtil.IsValidHandle(_textVisual.__nativeHandle)) { return; }
        uint selCol = _selectionBrush != null ? _selectionBrush.ResolveRgba() : ParseUtil.PackArgb(102u, 59u, 130u, 246u);
        uint carCol = _caretBrush != null ? _caretBrush.ResolveRgba() : ParseUtil.PackArgb(255u, 241u, 245u, 249u);
        bool blinkOn = (_activeBlinkBox != this) ? true : _caretBlinkVisible;
        bool showCaret = IsFocused && !_isReadOnly && blinkOn;
        Visual_SetTextSelection(
            _textVisual.__nativeHandle,
            SelectionStart,
            SelectionLength,
            _caretCp,
            showCaret,
            selCol,
            carCol);
    }

    protected void SyncImeState() {
        bool active = IsFocused && !_isReadOnly;
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        if (!active) {
            Host_SyncImeRect(0.0f, 0.0f, 0.0f, 0.0f, false);
            return;
        }
        float x = LayoutX + Padding.Left + (float)_caretCp * (FontSize * 0.55f);
        float y = LayoutY + Padding.Top;
        Host_SyncImeRect(x, y, 2.0f, FontSize * 1.3f, true);
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        if (Style == null && Template == null) {
            BorderBrush = new SolidColorBrush(IsFocused ? "#FF3B82F6" : (IsMouseOver ? "#FF64748B" : "#FF364054"));
        }
        RefreshTextSelectionVisual();
    }

    protected override void OnTypographyChanged() {
        base.OnTypographyChanged();
        RefreshTextVisual();
    }

    public override void OnGotFocus() {
        base.OnGotFocus();
        ResetCaretBlinkPhase();
        RefreshTextSelectionVisual();
        SyncImeState();
    }

    public override void OnLostFocus() {
        base.OnLostFocus();
        if (_activeBlinkBox == this) { _activeBlinkBox = null; }
        RefreshTextSelectionVisual();
        SyncImeState();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        Focus();
        if (_textVisual != null && ParseUtil.IsValidHandle(_textVisual.__nativeHandle)) {
            float lx = Visual_ScreenToLocal(_textVisual.__nativeHandle, e.X, e.Y, 0);
            float ly = Visual_ScreenToLocal(_textVisual.__nativeHandle, e.X, e.Y, 1);
            int hitCp = Visual_HitTestTextCp(_textVisual.__nativeHandle, lx, ly);
            bool shift = KeyModifiers.Has(e.Modifiers, KeyModifiers.Shift());
            MoveCaret(hitCp, shift);
        }
        _dragSelecting = true;
        CaptureMouse();
        e.Handled = true;
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_dragSelecting || _textVisual == null || !ParseUtil.IsValidHandle(_textVisual.__nativeHandle)) { return; }
        float lx = Visual_ScreenToLocal(_textVisual.__nativeHandle, e.X, e.Y, 0);
        float ly = Visual_ScreenToLocal(_textVisual.__nativeHandle, e.X, e.Y, 1);
        int hitCp = Visual_HitTestTextCp(_textVisual.__nativeHandle, lx, ly);
        MoveCaret(hitCp, true);
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (_dragSelecting) {
            _dragSelecting = false;
            ReleaseMouseCapture();
        }
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        bool shift = e.IsShiftDown;
        bool ctrl = e.IsControlDown;

        if (ctrl && e.Key == KeyCode.A) { SelectAll(); e.Handled = true; return; }
        if (ctrl && e.Key == KeyCode.C) { Copy(); e.Handled = true; return; }
        if (ctrl && e.Key == KeyCode.X) { Cut(); e.Handled = true; return; }
        if (ctrl && e.Key == KeyCode.V) { Paste(); e.Handled = true; return; }

        if (e.Key == KeyCode.Left) {
            int next = ctrl ? ParseUtil.FindPrevWordBoundary(_rawText, _caretCp) : (_caretCp - 1);
            MoveCaret(next, shift);
            e.Handled = true;
            return;
        }
        if (e.Key == KeyCode.Right) {
            int next = ctrl ? ParseUtil.FindNextWordBoundary(_rawText, _caretCp) : (_caretCp + 1);
            MoveCaret(next, shift);
            e.Handled = true;
            return;
        }
        if (e.Key == KeyCode.Home) { MoveCaret(0, shift); e.Handled = true; return; }
        if (e.Key == KeyCode.End) { MoveCaret(ParseUtil.Utf8CodePointCount(_rawText), shift); e.Handled = true; return; }
        if (e.Key == KeyCode.Backspace) { DeleteBackward(ctrl); e.Handled = true; return; }
        if (e.Key == KeyCode.Delete) { DeleteForward(ctrl); e.Handled = true; return; }
        if (e.Key == KeyCode.Enter && _acceptsReturn) { ReplaceSelection("\n"); e.Handled = true; return; }
    }

    public override void OnTextInput(TextCompositionEventArgs e) {
        base.OnTextInput(e);
        if (!IsEnabled || _isReadOnly) { return; }
        if (e.Text != null && e.Text.Length > 0) {
            ReplaceSelection(e.Text);
            e.Handled = true;
        }
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        if (Template == null) { return; }
        _placeholderVisual = null;
        UIElement host = GetTemplateChild("PART_ContentHost");
        if (host == null) { return; }
        if (host is TextBlock) {
            _textVisual = host as TextBlock;
        } else {
            _textVisual = new TextBlock(GetDisplayText());
            _textVisual.Name = "PART_Text";
            _textVisual.Foreground = Foreground;
            _textVisual.FontFamily = FontFamily;
            _textVisual.FontSize = FontSize;
            _textVisual.FontWeight = FontWeight;
            _textVisual.FontStyle = FontStyle;
            _textVisual.VerticalAlignment = VerticalAlignment.Center;
            _textVisual.Margin = new Thickness(2.0f, 0.0f, 2.0f, 0.0f);
            if (host is ContentControl) {
                ContentControl cc = host as ContentControl;
                if (cc.Template == null) {
                    cc.SetPropertyValue("HorizontalScrollBarVisibility", DependencyObject.BoxString("Hidden"));
                    cc.SetPropertyValue("VerticalScrollBarVisibility", DependencyObject.BoxString(_acceptsReturn ? "Auto" : "Disabled"));
                    cc.BorderThickness = new Thickness(0.0f);
                    if (cc.Background == null) {
                        cc.Background = Brushes.Transparent();
                    }
                }
                cc.Content = _textVisual;
            } else if (host is Decorator) {
                (host as Decorator).Child = _textVisual;
            } else if (host is Panel) {
                (host as Panel).ClearChildren();
                (host as Panel).AddChild(_textVisual);
            }
        }
        RefreshTextVisual();
    }

    protected override UIElement CreateDefaultTemplate() {
        var host = new Grid();
        host.HorizontalAlignment = HorizontalAlignment.Stretch;
        host.VerticalAlignment = VerticalAlignment.Center;

        _placeholderVisual = new TextBlock(_placeholderText);
        _placeholderVisual.Foreground = new SolidColorBrush("#FF64748B");
        _placeholderVisual.FontFamily = FontFamily;
        _placeholderVisual.FontSize = FontSize;
        _placeholderVisual.VerticalAlignment = VerticalAlignment.Center;

        _textVisual = new TextBlock(GetDisplayText());
        _textVisual.Name = "PART_Text";
        _textVisual.Foreground = Foreground;
        _textVisual.FontFamily = FontFamily;
        _textVisual.FontSize = FontSize;
        _textVisual.FontWeight = FontWeight;
        _textVisual.FontStyle = FontStyle;
        _textVisual.VerticalAlignment = VerticalAlignment.Center;

        host.AddChild(_placeholderVisual);
        host.AddChild(_textVisual);
        RefreshTextVisual();
        return host;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsReadOnly") { IsReadOnly = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "AcceptsReturn") { AcceptsReturn = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "MaxLength") { MaxLength = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "PlaceholderText" || propName == "Placeholder" ||
            propName == "Text.Placeholder" || propName == "noesis:Text.Placeholder") {
            PlaceholderText = DependencyObject.UnboxString(value, "");
            return true;
        }
        if (propName == "CaretBrush") { CaretBrush = UIElement.CoerceBrush(value); return true; }
        if (propName == "SelectionBrush") { SelectionBrush = UIElement.CoerceBrush(value); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsReadOnly") { return DependencyObject.BoxBool(_isReadOnly); }
        if (propName == "AcceptsReturn") { return DependencyObject.BoxBool(_acceptsReturn); }
        if (propName == "MaxLength") { return DependencyObject.BoxInt(_maxLength); }
        if (propName == "PlaceholderText" || propName == "Placeholder" ||
            propName == "Text.Placeholder" || propName == "noesis:Text.Placeholder") {
            return DependencyObject.BoxString(_placeholderText);
        }
        if (propName == "CaretBrush") { return _caretBrush; }
        if (propName == "SelectionBrush") { return _selectionBrush; }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 2. TextBox
// ============================================================================

public class TextBox : TextBoxBase {
    public TextBox() : base() {}
    public TextBox(string initialText, string placeholder = "") : base() {
        if (placeholder != null && placeholder.Length > 0) { PlaceholderText = placeholder; }
        if (initialText != null && initialText.Length > 0) { SetInternalRawText(initialText, true); }
    }

    public override string GetTypeName() { return "TextBox"; }

    public string Text {
        get { return InternalRawText(); }
        set { SetInternalRawText(value, true); }
    }

    public string SelectedText {
        get { return GetSelectedText(); }
        set { ReplaceSelection(value); }
    }

    public TextWrapping TextWrapping {
        get { return _textVisual != null ? _textVisual.TextWrapping : TextWrapping.NoWrap; }
        set { if (_textVisual != null) { _textVisual.TextWrapping = value; } }
    }

    public TextAlignment TextAlignment {
        get { return _textVisual != null ? _textVisual.TextAlignment : TextAlignment.Left; }
        set { if (_textVisual != null) { _textVisual.TextAlignment = value; } }
    }

    public int MaxLines { get; set; } = 0;
    public int MinLines { get; set; } = 1;

    protected override void OnRawTextChanged(string newRaw) {
        BindingEngine.NotifyTargetPropertyChanged(this, "Text", DependencyObject.BoxString(newRaw));
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Text") { Text = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "MaxLines") { MaxLines = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "MinLines") { MinLines = DependencyObject.UnboxInt(value, 1); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Text") { return DependencyObject.BoxString(Text); }
        if (propName == "MaxLines") { return DependencyObject.BoxInt(MaxLines); }
        if (propName == "MinLines") { return DependencyObject.BoxInt(MinLines); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 3. PasswordBox (Masked Visual + Clipboard Plaintext Guard)
// ============================================================================

public class PasswordBox : TextBoxBase {
    private string _passwordChar = "•";

    public event RoutedEventHandler PasswordChanged;

    public PasswordBox() : base() {}
    public PasswordBox(string placeholder) : base() {
        if (placeholder != null && placeholder.Length > 0) { PlaceholderText = placeholder; }
    }

    public override string GetTypeName() { return "PasswordBox"; }

    public string Password {
        get { return InternalRawText(); }
        set { SetInternalRawText(value, true); }
    }

    public string PasswordChar {
        get { return _passwordChar; }
        set {
            if (value == null || value.Length == 0) {
                _passwordChar = "•";
            } else {
                _passwordChar = ParseUtil.SliceCodePoints(value, 0, 1);
                if (_passwordChar.Length == 0) { _passwordChar = "•"; }
            }
            RefreshTextVisual();
        }
    }

    protected override bool CanCopyPlaintext() { return false; }
    protected override bool CanExposePlaintext() { return false; }

    protected override string GetDisplayText() {
        int cpCount = ParseUtil.Utf8CodePointCount(InternalRawText());
        string masked = "";
        for (int i = 0; i < cpCount; i += 1) {
            masked = masked + _passwordChar;
        }
        return masked;
    }

    protected override void OnRawTextChanged(string newRaw) {
        PasswordChanged(this, new RoutedEventArgs("PasswordChanged", this));
        int cpLen = ParseUtil.Utf8CodePointCount(newRaw);
        BindingEngine.NotifyTargetPropertyChanged(this, "Password", DependencyObject.BoxString(newRaw));
        BindingEngine.NotifyTargetPropertyChanged(this, "Text.PasswordLength", DependencyObject.BoxInt(cpLen));
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Password") { Password = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "PasswordChar") { PasswordChar = DependencyObject.UnboxString(value, "•"); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Password") { return DependencyObject.BoxString(Password); }
        if (propName == "PasswordChar") { return DependencyObject.BoxString(_passwordChar); }
        if (propName == "PasswordLength" || propName == "Text.PasswordLength" || propName == "noesis:Text.PasswordLength") {
            return DependencyObject.BoxInt(ParseUtil.Utf8CodePointCount(InternalRawText()));
        }
        if (propName == "Text") { return DependencyObject.BoxString(""); }
        return base.GetPropertyValue(propName);
    }
}
