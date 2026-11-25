module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// XamlReader: Dynamic Runtime XAML/XML Parser & Object Instantiator
// ============================================================================

internal class XamlXmlAttr {
    public string Name = "";
    public string LocalName = "";
    public string Value = "";
    internal bool CachedInit = false;
    internal string CachedPropName = "";
    internal string CachedLocalKey = "";
    internal bool IsToolTipProp = false;
    internal int CachedExtMode = 0;
    internal string CachedKey = "";
    internal XamlParsedBinding CachedBinding;

    public XamlXmlAttr(string name, string localName, string value) {
        Name = name == null ? "" : name;
        LocalName = localName == null ? "" : localName;
        Value = value == null ? "" : value;
    }
}

internal class XamlXmlNode {
    public string RawName = "";
    public string LocalName = "";
    public XamlXmlAttr[] Attrs;
    public XamlXmlNode[] Children;
    public string Text = "";

    public XamlXmlNode(string rawName, string localName) {
        RawName = rawName == null ? "" : rawName;
        LocalName = localName == null ? "" : localName;
        Attrs = new XamlXmlAttr[0];
        Children = new XamlXmlNode[0];
    }

    public string GetAttr(string name) {
        if (name == null || name.Length == 0) { return null; }
        for (int i = 0; i < Attrs.Length; i += 1) {
            if (Attrs[i].Name == name) { return Attrs[i].Value; }
        }
        return null;
    }

    public string GetKeyAttr() {
        string v = GetAttr("x:Key");
        if (v != null && v.Length > 0) { return v; }
        v = GetAttr("Key");
        if (v != null && v.Length > 0) { return v; }
        return "";
    }

    public string GetNameAttr() {
        string v = GetAttr("x:Name");
        if (v != null && v.Length > 0) { return v; }
        v = GetAttr("Name");
        if (v != null && v.Length > 0) { return v; }
        return "";
    }
}

internal class XamlParsedBinding {
    public string Path = "";
    public string ElementName = "";
    public int Mode = 0;
    public string RelativeSourceMode = "";
    public string AncestorType = "";
    public IValueConverter Converter;
    public string ConverterKey = "";
    public string ConverterParameter = "";
    public string StringFormat = "";
    public DependencyObject ExplicitSourceBox;
    public bool HasFallback = false;
    public DependencyObject FallbackBox;
    public bool HasTargetNull = false;
    public DependencyObject TargetNullBox;
}

internal class XamlBuildContext {
    public ResourceDictionary[] Scopes;
    public ResourceDictionary FallbackResources;
    public UIElement TemplatedParent;
    public string BaseUri = "";

    public XamlBuildContext(ResourceDictionary fallback, string baseUri) {
        Scopes = new ResourceDictionary[0];
        FallbackResources = fallback;
        BaseUri = baseUri == null ? "" : baseUri;
        if (fallback != null) { Scopes.Push(fallback); }
    }

    public XamlBuildContext ForkForTemplate(UIElement templatedParent, ResourceDictionary tplRes) {
        var child = new XamlBuildContext(FallbackResources, BaseUri);
        for (int i = 0; i < Scopes.Length; i += 1) {
            if (Scopes[i] != FallbackResources) {
                child.Scopes.Push(Scopes[i]);
            }
        }
        if (tplRes != null) { child.Scopes.Push(tplRes); }
        child.TemplatedParent = templatedParent;
        return child;
    }

    public void PushScope(ResourceDictionary rd) {
        if (rd != null) { Scopes.Push(rd); }
    }

    public void PopScope(ResourceDictionary rd) {
        if (rd == null) { return; }
        for (int i = Scopes.Length - 1; i >= 0; i -= 1) {
            if (Scopes[i] == rd) {
                Scopes.RemoveAt(i);
                return;
            }
        }
    }

    public DependencyObject FindResourceBox(string key, UIElement targetEl = null) {
        if (key == null || key.Length == 0) { return null; }
        for (int i = Scopes.Length - 1; i >= 0; i -= 1) {
            ResourceDictionary rd = Scopes[i];
            if (rd != null) {
                DependencyObject hit = rd.TryGetLocalBox(key);
                if (hit != null) { return hit; }
            }
        }
        if (targetEl != null) {
            DependencyObject local = targetEl.FindResourceBox(key);
            if (local != null) { return local; }
        }
        DependencyObject amb = ResourceDictionary.FindAmbientBox(key);
        if (amb != null) { return amb; }
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        return appRes != null ? appRes.TryGetLocalBox(key) : null;
    }
}

internal static class XamlStr {
    public static bool StartsWith(string s, string prefix) {
        if (s == null || prefix == null) { return false; }
        if (prefix.Length > s.Length) { return false; }
        return s.StartsWith(prefix);
    }

    public static bool EndsWith(string s, string suffix) {
        if (s == null || suffix == null) { return false; }
        if (suffix.Length > s.Length) { return false; }
        return s.EndsWith(suffix);
    }

    public static int FindFrom(string s, string needle, int startIdx) {
        if (s == null || needle == null || needle.Length == 0) { return -1; }
        if (startIdx <= 0) { return s.IndexOf(needle); }
        if (startIdx > s.Length - needle.Length) { return -1; }
        if (!s.Contains(needle)) { return -1; }
        int rel = s.Substring(startIdx).IndexOf(needle);
        return rel >= 0 ? (startIdx + rel) : -1;
    }

    public static int LastIndexOfChar(string s, string ch) {
        if (s == null || ch == null || ch.Length != 1) { return -1; }
        return s.LastIndexOf(ch);
    }

    public static bool IsSpace(string ch) {
        return ch == " " || ch == "\t" || ch == "\r" || ch == "\n";
    }

    public static string StripPrefix(string name) {
        if (name == null || name.Length == 0) { return ""; }
        int colon = LastIndexOfChar(name, ":");
        if (colon >= 0 && colon + 1 < name.Length) {
            return name.Substring(colon + 1);
        }
        return name;
    }

    public static string StripOuterParens(string prop) {
        if (prop == null) { return ""; }
        string s = prop.Trim();
        if (s.Length >= 2 && StartsWith(s, "(") && EndsWith(s, ")")) {
            s = s.Substring(1, s.Length - 2).Trim();
        }
        if (StartsWith(s, "noesis:")) { s = s.Substring(7); }
        else if (StartsWith(s, "b:")) { s = s.Substring(2); }
        else if (StartsWith(s, "local:")) { s = s.Substring(6); }
        if (StartsWith(s, "UIElement.")) { return s.Substring(10); }
        if (StartsWith(s, "FrameworkElement.")) { return s.Substring(17); }
        if (StartsWith(s, "Control.")) { return s.Substring(8); }
        if (StartsWith(s, "Panel.")) { return s.Substring(6); }
        return s;
    }

    public static string NormalizeAnimTargetProp(string rawProp) {
        if (rawProp == null) { return ""; }
        string s = rawProp.Trim();
        if (s.Length == 0) { return ""; }

        if (s == "(Border.Background).(SolidColorBrush.Opacity)" ||
            s == "(Panel.Background).(Brush.Opacity)" ||
            s == "(Panel.Background).(SolidColorBrush.Opacity)" ||
            s == "(Control.Background).(Brush.Opacity)" ||
            s == "(Control.Background).(SolidColorBrush.Opacity)") {
            return "BrushOpacity";
        }
        if (s == "(Shape.Fill).(Brush.Opacity)" || s == "(Shape.Fill).(SolidColorBrush.Opacity)") {
            return "Fill.Opacity";
        }
        if (s == "(Shape.Stroke).(Brush.Opacity)" || s == "(Shape.Stroke).(SolidColorBrush.Opacity)") {
            return "Stroke.Opacity";
        }
        if (s == "(Control.Foreground).(SolidColorBrush.Color)" || s == "(TextBlock.Foreground).(SolidColorBrush.Color)") {
            return "Foreground.Color";
        }
        if (s == "(Border.Background).(SolidColorBrush.Color)" || s == "(Panel.Background).(SolidColorBrush.Color)" ||
            s == "(Control.Background).(SolidColorBrush.Color)") {
            return "Background.Color";
        }
        if (s == "(Border.BorderBrush).(SolidColorBrush.Color)" || s == "(Control.BorderBrush).(SolidColorBrush.Color)") {
            return "BorderBrush.Color";
        }
        if (s == "(Shape.Fill).(SolidColorBrush.Color)") { return "Fill.Color"; }
        if (s == "(Shape.Stroke).(SolidColorBrush.Color)") { return "Stroke.Color"; }
        if (s == "(noesis:Path.TrimStart)") { return "PathTrimStart"; }
        if (s == "(noesis:Path.TrimEnd)") { return "PathTrimEnd"; }

        if (s.Contains("GradientStops") && (s.Contains("Shape.Fill") || s.Contains("Shape.Stroke") || s.Contains("Background"))) {
            string prefix = s.Contains("Shape.Stroke") ? "Stroke" : (s.Contains("Background") ? "Background" : "Fill");
            string suffix = s.Contains("Offset") ? "Offset" : "Color";
            if (s.Contains("[0]")) { return prefix + ".Stop0" + suffix; }
            if (s.Contains("[1]")) { return prefix + ".Stop1" + suffix; }
            if (s.Contains("[2]")) { return prefix + ".Stop2" + suffix; }
            if (s.Contains("[3]")) { return prefix + ".Stop3" + suffix; }
        }
        if (s.Contains("PlasmaBrush.")) {
            string pfx = s.Contains("Background") ? "Background" : "Fill";
            if (s.Contains(".Time")) { return pfx + ".PlasmaTime"; }
            if (s.Contains(".Seed")) { return pfx + ".PlasmaSeed"; }
            if (s.Contains(".ScaleX")) { return pfx + ".PlasmaScaleX"; }
            if (s.Contains(".ScaleY")) { return pfx + ".PlasmaScaleY"; }
            if (s.Contains(".Color")) { return pfx + ".PlasmaColor"; }
        }
        if (s.Contains("Transform3D") || s.Contains("CompositeTransform3D")) {
            if (s.Contains(".RotationX")) { return "Transform3D.RotationX"; }
            if (s.Contains(".RotationY")) { return "Transform3D.RotationY"; }
            if (s.Contains(".RotationZ")) { return "Transform3D.RotationZ"; }
            if (s.Contains(".TranslateX")) { return "Transform3D.TranslateX"; }
            if (s.Contains(".TranslateY")) { return "Transform3D.TranslateY"; }
            if (s.Contains(".TranslateZ")) { return "Transform3D.TranslateZ"; }
            if (s.Contains(".ScaleX")) { return "Transform3D.ScaleX"; }
            if (s.Contains(".ScaleY")) { return "Transform3D.ScaleY"; }
            if (s.Contains(".ScaleZ")) { return "Transform3D.ScaleZ"; }
            if (s.Contains(".CenterX")) { return "Transform3D.CenterX"; }
            if (s.Contains(".CenterY")) { return "Transform3D.CenterY"; }
            if (s.Contains(".CenterZ")) { return "Transform3D.CenterZ"; }
        }
        if (s.Contains("LayoutTransform")) {
            if (s.Contains(".ScaleX")) { return "LayoutTransform.ScaleX"; }
            if (s.Contains(".ScaleY")) { return "LayoutTransform.ScaleY"; }
            if (s.Contains(".AngleX") || s.Contains(".SkewX")) { return "LayoutTransform.SkewX"; }
            if (s.Contains(".AngleY") || s.Contains(".SkewY")) { return "LayoutTransform.SkewY"; }
            if (s.Contains(".Angle") || s.Contains(".Rotation")) { return "LayoutTransform.Angle"; }
        }
        if (s.Contains("RelativeTransform")) {
            string prefix = s.Contains("Fill") ? "Fill." : (s.Contains("Stroke") ? "Stroke." : (s.Contains("Foreground") ? "Foreground." : "Background."));
            if (s.Contains(".X") || s.Contains("TranslateX")) { return prefix + "RelativeTransform.X"; }
            if (s.Contains(".Y") || s.Contains("TranslateY")) { return prefix + "RelativeTransform.Y"; }
        }
        if (s.Contains("RenderTransform")) {
            if (s.Contains(".ScaleX")) { return "RenderTransform.ScaleX"; }
            if (s.Contains(".ScaleY")) { return "RenderTransform.ScaleY"; }
            if (s.Contains(".AngleX")) { return "RenderTransform.AngleX"; }
            if (s.Contains(".AngleY")) { return "RenderTransform.AngleY"; }
            if (s.Contains(".Angle")) { return "RenderTransform.Angle"; }
            if (s.Contains(".X")) { return "RenderTransform.X"; }
            if (s.Contains(".Y")) { return "RenderTransform.Y"; }
        }
        return StripOuterParens(s);
    }

    private static int ParseHexEntityCode(string hex) {
        if (hex == null || hex.Length == 0) { return -1; }
        int acc = 0;
        for (int i = 0; i < hex.Length; i += 1) {
            string ch = hex.Substring(i, 1);
            int d = -1;
            if (ch == "0") { d = 0; } else if (ch == "1") { d = 1; }
            else if (ch == "2") { d = 2; } else if (ch == "3") { d = 3; }
            else if (ch == "4") { d = 4; } else if (ch == "5") { d = 5; }
            else if (ch == "6") { d = 6; } else if (ch == "7") { d = 7; }
            else if (ch == "8") { d = 8; } else if (ch == "9") { d = 9; }
            else if (ch == "a" || ch == "A") { d = 10; }
            else if (ch == "b" || ch == "B") { d = 11; }
            else if (ch == "c" || ch == "C") { d = 12; }
            else if (ch == "d" || ch == "D") { d = 13; }
            else if (ch == "e" || ch == "E") { d = 14; }
            else if (ch == "f" || ch == "F") { d = 15; }
            if (d < 0) { return -1; }
            acc = acc * 16 + d;
            if (acc > 1114111) { return -1; }
        }
        return acc;
    }

    private static int ParseDecEntityCode(string dec) {
        if (dec == null || dec.Length == 0) { return -1; }
        int acc = 0;
        for (int i = 0; i < dec.Length; i += 1) {
            string ch = dec.Substring(i, 1);
            int d = -1;
            if (ch == "0") { d = 0; } else if (ch == "1") { d = 1; }
            else if (ch == "2") { d = 2; } else if (ch == "3") { d = 3; }
            else if (ch == "4") { d = 4; } else if (ch == "5") { d = 5; }
            else if (ch == "6") { d = 6; } else if (ch == "7") { d = 7; }
            else if (ch == "8") { d = 8; } else if (ch == "9") { d = 9; }
            if (d < 0) { return -1; }
            acc = acc * 10 + d;
            if (acc > 1114111) { return -1; }
        }
        return acc;
    }

    public static string DecodeXmlText(string raw) {
        if (raw == null || !raw.Contains("&")) { return raw == null ? "" : raw; }
        string rebuilt = "";
        int pos = 0;
        while (pos < raw.Length) {
            int amp = FindFrom(raw, "&", pos);
            if (amp < 0) {
                rebuilt = rebuilt + raw.Substring(pos);
                break;
            }
            rebuilt = rebuilt + raw.Substring(pos, amp - pos);
            int semi = FindFrom(raw, ";", amp + 1);
            if (semi > amp + 1 && semi - amp <= 12) {
                string ent = raw.Substring(amp + 1, semi - (amp + 1));
                if (ent == "lt") { rebuilt = rebuilt + "<"; pos = semi + 1; continue; }
                if (ent == "gt") { rebuilt = rebuilt + ">"; pos = semi + 1; continue; }
                if (ent == "quot") { rebuilt = rebuilt + "\""; pos = semi + 1; continue; }
                if (ent == "apos") { rebuilt = rebuilt + "'"; pos = semi + 1; continue; }
                if (ent == "amp") { rebuilt = rebuilt + "&"; pos = semi + 1; continue; }
                if (StartsWith(ent, "#") && ent.Length > 1) {
                    string body = ent.Substring(1);
                    int cp = -1;
                    if (StartsWith(body, "x") || StartsWith(body, "X")) {
                        cp = ParseHexEntityCode(body.Substring(1));
                    } else {
                        cp = ParseDecEntityCode(body);
                    }
                    if (cp > 0 && cp <= 1114111 && !(cp >= 55296 && cp <= 57343)) {
                        string enc = ParseUtil.FromCodePoint(cp);
                        if (enc.Length > 0) {
                            rebuilt = rebuilt + enc;
                            pos = semi + 1;
                            continue;
                        }
                    }
                }
            }
            rebuilt = rebuilt + "&";
            pos = amp + 1;
        }
        return rebuilt;
    }

    public static string TrimPreserveNewline(string s) {
        if (s == null || s.Length == 0) { return ""; }
        if (s.Trim().Length == 0) { return ""; }
        return DecodeXmlText(s);
    }

    public static string ExtractDirectoryUri(string uri) {
        if (uri == null || uri.Length == 0) { return ""; }
        string norm = uri.Trim().Replace("\\", "/");
        int slash = LastIndexOfChar(norm, "/");
        if (slash >= 0) {
            return norm.Substring(0, slash + 1);
        }
        return "";
    }

    public static string ResolveUri(string baseUri, string relUri) {
        if (relUri == null) { return ""; }
        string r = relUri.Trim().Replace("\\", "/");
        if (r.Length == 0) { return ""; }
        string low = r.ToLower();
        if (StartsWith(low, "pack://") || StartsWith(low, "file://") || StartsWith(r, "/") || r.Contains(";component/")) {
            return r;
        }
        while (StartsWith(r, "./")) {
            r = r.Substring(2);
        }
        string baseDir = ExtractDirectoryUri(baseUri);
        if (baseDir.Length > 0) {
            return baseDir + r;
        }
        return r;
    }

    public static string[] SplitTopLevelCommas(string text) {
        if (text == null) { return new string[0]; }
        if (!text.Contains(",")) {
            var single = new string[1];
            single[0] = text.Trim();
            return single;
        }
        if (!text.Contains("{") && !text.Contains("'") && !text.Contains("\"")) {
            string[] rawParts = text.Split(",");
            for (int i = 0; i < rawParts.Length; i += 1) {
                rawParts[i] = rawParts[i].Trim();
            }
            return rawParts;
        }
        var res = new string[0];
        int depth = 0;
        string inQuote = "";
        int start = 0;
        for (int i = 0; i < text.Length; i += 1) {
            string ch = text.Substring(i, 1);
            if (inQuote.Length > 0) {
                if (ch == inQuote) { inQuote = ""; }
                continue;
            }
            if (ch == "'" || ch == "\"") {
                inQuote = ch;
            } else if (ch == "{") {
                depth += 1;
            } else if (ch == "}" && depth > 0) {
                depth -= 1;
            } else if (ch == "," && depth == 0) {
                res.Push(text.Substring(start, i - start).Trim());
                start = i + 1;
            }
        }
        if (start <= text.Length) {
            res.Push(text.Substring(start).Trim());
        }
        return res;
    }

    public static int FindTopLevelEquals(string seg) {
        if (seg == null) { return -1; }
        if (!seg.Contains("{") && !seg.Contains("'") && !seg.Contains("\"")) {
            return seg.IndexOf("=");
        }
        int depth = 0;
        string inQuote = "";
        for (int i = 0; i < seg.Length; i += 1) {
            string ch = seg.Substring(i, 1);
            if (inQuote.Length > 0) {
                if (ch == inQuote) { inQuote = ""; }
                continue;
            }
            if (ch == "'" || ch == "\"") {
                inQuote = ch;
            } else if (ch == "{") {
                depth += 1;
            } else if (ch == "}" && depth > 0) {
                depth -= 1;
            } else if (ch == "=" && depth == 0) {
                return i;
            }
        }
        return -1;
    }

    public static string Unquote(string v) {
        if (v == null) { return ""; }
        string t = v.Trim();
        if (t.Length >= 2) {
            string first = t.Substring(0, 1);
            string last = t.Substring(t.Length - 1, 1);
            if ((first == "'" && last == "'") || (first == "\"" && last == "\"")) {
                return t.Substring(1, t.Length - 2);
            }
        }
        return t;
    }

    public static float ParseKeyTimeSec(string raw) {
        if (raw == null) { return 0.0f; }
        string t = raw.Trim();
        if (t.Length == 0 || t == "0") { return 0.0f; }
        if (t.Contains(":")) {
            string[] parts = t.Split(":");
            float sec = 0.0f;
            if (parts.Length == 2) {
                sec = ParseUtil.ParseFloat(parts[0], 0.0f) * 60.0f + ParseUtil.ParseFloat(parts[1], 0.0f);
            } else if (parts.Length >= 3) {
                float days = 0.0f;
                float hours = 0.0f;
                if (parts[0].Contains(".")) {
                    string[] dh = parts[0].Split(".");
                    if (dh.Length >= 2) {
                        days = ParseUtil.ParseFloat(dh[0], 0.0f);
                        hours = ParseUtil.ParseFloat(dh[1], 0.0f);
                    }
                } else {
                    hours = ParseUtil.ParseFloat(parts[0], 0.0f);
                }
                sec = days * 86400.0f +
                      hours * 3600.0f +
                      ParseUtil.ParseFloat(parts[1], 0.0f) * 60.0f +
                      ParseUtil.ParseFloat(parts[2], 0.0f);
            }
            return sec;
        }
        if (EndsWith(t, "s") || EndsWith(t, "S")) {
            return ParseUtil.ParseFloat(t.Substring(0, t.Length - 1), 0.0f);
        }
        return ParseUtil.ParseFloat(t, 0.0f);
    }

    public static float ParseDurationSec(string raw, float fallback = 0.25f) {
        if (raw == null || raw.Trim().Length == 0) { return fallback; }
        string t = raw.Trim();
        if (t == "Automatic" || t == "Forever") { return fallback; }
        return ParseKeyTimeSec(t);
    }

    public static int ParseRepeatCount(string raw) {
        if (raw == null || raw.Trim().Length == 0) { return 1; }
        string t = raw.Trim().ToLower();
        if (t == "forever") { return -1; }
        if (EndsWith(t, "x")) {
            return ParseUtil.ParseInt(t.Substring(0, t.Length - 1), 1);
        }
        return ParseUtil.ParseInt(t, 1);
    }

    public static bool LooksLikeMarkupExtension(string v) {
        if (v == null) { return false; }
        string t = v.Trim();
        if (t.Length < 2) { return false; }
        if (StartsWith(t, "{}")) { return false; }
        return StartsWith(t, "{") && EndsWith(t, "}");
    }

    public static string ExtractExtType(string extText) {
        if (extText == null) { return ""; }
        string t = extText.Trim();
        if (t.Length < 2) { return ""; }
        string inner = t.Substring(1, t.Length - 2).Trim();
        if (!inner.Contains("\t") && !inner.Contains("\r") && !inner.Contains("\n")) {
            int spFast = inner.IndexOf(" ");
            return spFast < 0 ? inner : inner.Substring(0, spFast);
        }
        int sp = 0;
        while (sp < inner.Length && !IsSpace(inner.Substring(sp, 1))) { sp += 1; }
        return inner.Substring(0, sp);
    }

    public static string ExtractExtBody(string extText) {
        if (extText == null) { return ""; }
        string t = extText.Trim();
        if (t.Length < 2) { return ""; }
        string inner = t.Substring(1, t.Length - 2).Trim();
        if (!inner.Contains("\t") && !inner.Contains("\r") && !inner.Contains("\n")) {
            int spFast = inner.IndexOf(" ");
            return spFast >= 0 ? inner.Substring(spFast + 1).Trim() : "";
        }
        int sp = 0;
        while (sp < inner.Length && !IsSpace(inner.Substring(sp, 1))) { sp += 1; }
        return sp < inner.Length ? inner.Substring(sp).Trim() : "";
    }
}

internal static class XamlDomParser {
    private static int FindTagCloseOutsideQuotes(string chunk) {
        string quote = "";
        for (int i = 0; i < chunk.Length; i += 1) {
            string ch = chunk.Substring(i, 1);
            if (quote.Length > 0) {
                if (ch == quote) { quote = ""; }
            } else {
                if (ch == "\"" || ch == "'") {
                    quote = ch;
                } else if (ch == ">") {
                    return i;
                }
            }
        }
        return -1;
    }

    private static void ParseAttributes(string attrText, XamlXmlNode node) {
        int i = 0;
        int len = attrText.Length;
        while (i < len) {
            while (i < len && XamlStr.IsSpace(attrText.Substring(i, 1))) { i += 1; }
            if (i >= len) { break; }
            int nameStart = i;
            while (i < len) {
                string ch = attrText.Substring(i, 1);
                if (ch == "=" || XamlStr.IsSpace(ch)) { break; }
                i += 1;
            }
            string rawAttr = attrText.Substring(nameStart, i - nameStart).Trim();
            while (i < len && XamlStr.IsSpace(attrText.Substring(i, 1))) { i += 1; }
            if (i >= len || attrText.Substring(i, 1) != "=") {
                if (rawAttr.Length > 0) {
                    node.Attrs.Push(new XamlXmlAttr(rawAttr, XamlStr.StripPrefix(rawAttr), ""));
                }
                continue;
            }
            i += 1;
            while (i < len && XamlStr.IsSpace(attrText.Substring(i, 1))) { i += 1; }
            if (i >= len) { break; }
            string q = attrText.Substring(i, 1);
            string val = "";
            if (q == "\"" || q == "'") {
                i += 1;
                int valStart = i;
                while (i < len && attrText.Substring(i, 1) != q) { i += 1; }
                val = XamlStr.DecodeXmlText(attrText.Substring(valStart, i - valStart));
                if (i < len) { i += 1; }
            } else {
                int valStart = i;
                while (i < len && !XamlStr.IsSpace(attrText.Substring(i, 1))) { i += 1; }
                val = XamlStr.DecodeXmlText(attrText.Substring(valStart, i - valStart));
            }
            if (rawAttr.Length > 0) {
                node.Attrs.Push(new XamlXmlAttr(rawAttr, XamlStr.StripPrefix(rawAttr), val));
            }
        }
    }

    private static void AppendParentTextSegment(XamlXmlNode parent, string rawSeg) {
        if (parent == null || rawSeg == null || rawSeg.Length == 0) { return; }
        string decoded = XamlStr.DecodeXmlText(rawSeg);
        string tr = decoded.Trim();
        bool isInlineContainer = (parent.LocalName == "TextBlock" || parent.LocalName == "Span" ||
            parent.LocalName == "Bold" || parent.LocalName == "Italic" ||
            parent.LocalName == "Underline" || parent.LocalName == "Hyperlink");
        if (isInlineContainer) {
            if (tr.Length == 0) {
                parent.Children.Push(new XamlXmlNode("#ws", "#ws"));
            } else {
                bool hasLead = decoded.Substring(0, 1).Trim().Length == 0;
                bool hasTrail = decoded.Substring(decoded.Length - 1, 1).Trim().Length == 0;
                bool prevIsLineBreak = parent.Children.Length > 0 && parent.Children[parent.Children.Length - 1].LocalName == "LineBreak";
                if (hasLead && parent.Children.Length > 0 && !prevIsLineBreak) {
                    parent.Children.Push(new XamlXmlNode("#ws", "#ws"));
                }
                var txtNode = new XamlXmlNode("#text", "#text");
                txtNode.Text = tr;
                parent.Children.Push(txtNode);
                parent.Text = parent.Text + decoded;
                if (hasTrail) {
                    parent.Children.Push(new XamlXmlNode("#ws", "#ws"));
                }
            }
            return;
        }
        if (tr.Length > 0) {
            if (parent.LocalName == "ItemsControl" || parent.LocalName == "ListBox" ||
                parent.LocalName == "ListView" || parent.LocalName == "ComboBox") {
                var itemTxt = new XamlXmlNode("#text", "#text");
                itemTxt.Text = tr;
                parent.Children.Push(itemTxt);
            }
            parent.Text = parent.Text + decoded;
        }
    }

    public static XamlXmlNode ParseDocument(string xmlText) {
        if (xmlText == null) { return null; }
        int nativeCount = Xml_ParseDocument(xmlText);
        if (nativeCount >= 0) {
            if (nativeCount == 0) {
                Xml_ClearDocument();
                return null;
            }
            var nodes = new XamlXmlNode[nativeCount];
            for (int id = 0; id < nativeCount; id += 1) {
                int meta = Xml_NodeMeta(id);
                int parentId = (meta % 262144) - 1;
                int attrCount = (meta / 262144) % 1024;
                bool hasText = ((meta / 268435456) % 2) != 0;
                bool hasPrefix = ((meta / 536870912) % 2) != 0;
                string raw = Xml_NodeRawName(id);
                string local = hasPrefix ? Xml_NodeLocalName(id) : raw;
                var n = new XamlXmlNode(raw, local);
                if (hasText) {
                    n.Text = Xml_NodeText(id);
                }
                for (int a = 0; a < attrCount; a += 1) {
                    string aName = Xml_AttrName(id, a);
                    string aLocal = aName.Contains(":") ? XamlStr.StripPrefix(aName) : aName;
                    n.Attrs.Push(new XamlXmlAttr(aName, aLocal, Xml_AttrValue(id, a)));
                }
                nodes[id] = n;
                if (parentId >= 0 && parentId < id) {
                    nodes[parentId].Children.Push(n);
                }
            }
            Xml_ClearDocument();
            return nodes[0];
        }
        string src = xmlText.Trim();
        if (src.Length == 0) { return null; }
        int firstLt = XamlStr.FindFrom(src, "<", 0);
        if (firstLt < 0) { return null; }
        if (firstLt > 0) { src = src.Substring(firstLt); }

        string[] chunks = src.Split("<");
        var stack = new XamlXmlNode[0];
        XamlXmlNode root = null;

        int cIdx = 0;
        while (cIdx < chunks.Length) {
            string chunk = chunks[cIdx];
            cIdx += 1;
            if (chunk == null || chunk.Length == 0) { continue; }

            if (XamlStr.StartsWith(chunk, "!--")) {
                string cur = chunk;
                while (!cur.Contains("-->") && cIdx < chunks.Length) {
                    cur = cur + "<" + chunks[cIdx];
                    cIdx += 1;
                }
                int endCom = XamlStr.FindFrom(cur, "-->", 3);
                if (endCom >= 0 && endCom + 3 < cur.Length && stack.Length > 0) {
                    AppendParentTextSegment(stack[stack.Length - 1], cur.Substring(endCom + 3));
                }
                continue;
            }

            if (XamlStr.StartsWith(chunk, "![CDATA[")) {
                string cur = chunk;
                while (!cur.Contains("]]>") && cIdx < chunks.Length) {
                    cur = cur + "<" + chunks[cIdx];
                    cIdx += 1;
                }
                int endCd = XamlStr.FindFrom(cur, "]]>", 8);
                if (endCd >= 0 && stack.Length > 0) {
                    XamlXmlNode parent = stack[stack.Length - 1];
                    parent.Text = parent.Text + cur.Substring(8, endCd - 8);
                    if (endCd + 3 < cur.Length) {
                        AppendParentTextSegment(parent, cur.Substring(endCd + 3));
                    }
                }
                continue;
            }

            if (XamlStr.StartsWith(chunk, "?") || XamlStr.StartsWith(chunk, "!")) {
                continue;
            }

            int gt = FindTagCloseOutsideQuotes(chunk);
            if (gt < 0) { continue; }
            string tagHeader = chunk.Substring(0, gt).Trim();
            string after = gt + 1 < chunk.Length ? chunk.Substring(gt + 1) : "";

            if (XamlStr.StartsWith(tagHeader, "/")) {
                string closeName = tagHeader.Substring(1).Trim();
                while (stack.Length > 0) {
                    XamlXmlNode popped = stack[stack.Length - 1];
                    stack.RemoveAt(stack.Length - 1);
                    if (popped.RawName == closeName || popped.LocalName == XamlStr.StripPrefix(closeName)) {
                        break;
                    }
                }
                if (stack.Length > 0 && after.Length > 0) {
                    AppendParentTextSegment(stack[stack.Length - 1], after);
                }
                continue;
            }

            bool selfClosing = XamlStr.EndsWith(tagHeader, "/");
            if (selfClosing) {
                tagHeader = tagHeader.Substring(0, tagHeader.Length - 1).Trim();
            }

            int nameEnd = 0;
            while (nameEnd < tagHeader.Length && !XamlStr.IsSpace(tagHeader.Substring(nameEnd, 1))) {
                nameEnd += 1;
            }
            string rawName = tagHeader.Substring(0, nameEnd);
            string attrRest = nameEnd < tagHeader.Length ? tagHeader.Substring(nameEnd) : "";

            var node = new XamlXmlNode(rawName, XamlStr.StripPrefix(rawName));
            if (attrRest.Length > 0) {
                ParseAttributes(attrRest, node);
            }

            if (root == null) { root = node; }
            if (stack.Length > 0) {
                stack[stack.Length - 1].Children.Push(node);
            }

            if (!selfClosing) {
                if (after.Length > 0) {
                    AppendParentTextSegment(node, after);
                }
                stack.Push(node);
            } else if (stack.Length > 0 && after.Length > 0) {
                AppendParentTextSegment(stack[stack.Length - 1], after);
            }
        }
        return root;
    }
}

public static class XamlReader {
    private static string[] _cachedExtDictUris;
    private static ResourceDictionary[] _cachedExtDicts;

    private static ResourceDictionary TryGetCachedExtDict(string uri) {
        if (uri == null || uri.Length == 0 || _cachedExtDictUris == null) { return null; }
        for (int i = _cachedExtDictUris.Length - 1; i >= 0; i -= 1) {
            if (_cachedExtDictUris[i] == uri) {
                return _cachedExtDicts[i];
            }
        }
        return null;
    }

    private static void StoreCachedExtDict(string uri, ResourceDictionary rd) {
        if (uri == null || uri.Length == 0 || rd == null) { return; }
        if (_cachedExtDictUris == null) {
            _cachedExtDictUris = new string[0];
            _cachedExtDicts = new ResourceDictionary[0];
        }
        for (int i = 0; i < _cachedExtDictUris.Length; i += 1) {
            if (_cachedExtDictUris[i] == uri) {
                _cachedExtDicts[i] = rd;
                return;
            }
        }
        if (_cachedExtDictUris.Length >= 24) {
            _cachedExtDictUris.RemoveAt(0);
            _cachedExtDicts.RemoveAt(0);
        }
        _cachedExtDictUris.Push(uri);
        _cachedExtDicts.Push(rd);
    }

    private static string[] _cachedDocUris;
    private static XamlXmlNode[] _cachedDocNodes;

    private static XamlXmlNode TryGetCachedDoc(string uri) {
        if (uri == null || uri.Length == 0 || _cachedDocUris == null) { return null; }
        for (int i = _cachedDocUris.Length - 1; i >= 0; i -= 1) {
            if (_cachedDocUris[i] == uri) {
                XamlXmlNode hit = _cachedDocNodes[i];
                int last = _cachedDocUris.Length - 1;
                if (i != last) {
                    _cachedDocUris.RemoveAt(i);
                    _cachedDocNodes.RemoveAt(i);
                    _cachedDocUris.Push(uri);
                    _cachedDocNodes.Push(hit);
                }
                return hit;
            }
        }
        return null;
    }

    private static void StoreCachedDoc(string uri, XamlXmlNode root) {
        if (uri == null || uri.Length == 0 || root == null) { return; }
        if (_cachedDocUris == null) {
            _cachedDocUris = new string[0];
            _cachedDocNodes = new XamlXmlNode[0];
        }
        for (int i = 0; i < _cachedDocUris.Length; i += 1) {
            if (_cachedDocUris[i] == uri) {
                _cachedDocUris.RemoveAt(i);
                _cachedDocNodes.RemoveAt(i);
                _cachedDocUris.Push(uri);
                _cachedDocNodes.Push(root);
                return;
            }
        }
        if (_cachedDocUris.Length >= 16) {
            _cachedDocUris.RemoveAt(0);
            _cachedDocNodes.RemoveAt(0);
        }
        _cachedDocUris.Push(uri);
        _cachedDocNodes.Push(root);
    }

    public static void ClearDocumentCache() {
        _cachedDocUris = null;
        _cachedDocNodes = null;
    }

    public static UIElement LoadUri(string uri, ResourceDictionary fallbackResources = null) {
        if (uri == null || uri.Trim().Length == 0) { return null; }
        string normUri = uri.Trim();
        XamlXmlNode cachedRoot = TryGetCachedDoc(normUri);
        if (cachedRoot != null) {
            return ParseRootWithUri(cachedRoot, fallbackResources, normUri);
        }
        string text = Host_ReadTextAsset(normUri);
        if (text == null || text.Trim().Length == 0) { return null; }
        XamlXmlNode root = XamlDomParser.ParseDocument(text);
        if (root == null) { return null; }
        StoreCachedDoc(normUri, root);
        return ParseRootWithUri(root, fallbackResources, normUri);
    }

    public static UIElement Parse(string xamlText, ResourceDictionary fallbackResources = null) {
        return ParseWithUri(xamlText, fallbackResources, "");
    }

    public static ResourceDictionary LoadResourceDictionary(string uri) {
        if (uri == null || uri.Trim().Length == 0) { return null; }
        string normUri = uri.Trim();
        ResourceDictionary cached = TryGetCachedExtDict(normUri);
        if (cached != null) { return cached; }
        string text = Host_ReadTextAsset(normUri);
        if (text == null || text.Trim().Length == 0) { return null; }
        XamlXmlNode root = XamlDomParser.ParseDocument(text);
        if (root == null) { return null; }
        var ctx = new XamlBuildContext(null, normUri);
        ResourceDictionary built = BuildResourceDictionaryNode(root, ctx);
        StoreCachedExtDict(normUri, built);
        return built;
    }

    private static UIElement ParseWithUri(string xamlText, ResourceDictionary fallbackResources, string baseUri) {
        if (xamlText == null || xamlText.Trim().Length == 0) { return null; }
        XamlXmlNode root = XamlDomParser.ParseDocument(xamlText);
        if (root == null) { return null; }
        return ParseRootWithUri(root, fallbackResources, baseUri);
    }

    private static UIElement ParseRootWithUri(XamlXmlNode root, ResourceDictionary fallbackResources, string baseUri) {
        if (root == null) { return null; }
        var ctx = new XamlBuildContext(fallbackResources, baseUri);
        if (root.LocalName == "ResourceDictionary") {
            ResourceDictionary rd = BuildResourceDictionaryNode(root, ctx);
            ResourceDictionary.GlobalApplicationResources().Merge(rd);
            return null;
        }
        ResourceDictionary.PushAmbient(fallbackResources);
        UIElement._xamlBuildDepth += 1;
        BindingEngine.BeginBatch();
        DependencyObject obj = BuildObjectNode(root, ctx, null);
        UIElement._xamlBuildDepth -= 1;
        ResourceDictionary.PopAmbient(fallbackResources);
        UIElement el = obj as UIElement;
        if (el == null) {
            BindingEngine.EndBatch();
            return null;
        }
        el.SetUiFlag(1024, true);
        if (fallbackResources != null && el.AmbientResources == null) {
            el.AmbientResources = fallbackResources;
        }
        el.ApplyStyleSubtree();
        el.RefreshDynamicResourcesRecursive();
        BindingEngine.EndBatch();
        BindingEngine.FlushSubtree(el);
        return el;
    }

    private static string UnwrapTypeSpec(string raw) {
        if (raw == null) { return ""; }
        string t = raw.Trim();
        if (XamlStr.StartsWith(t, "{x:Type") || XamlStr.StartsWith(t, "{Type")) {
            return XamlStr.StripPrefix(XamlStr.ExtractExtBody(t));
        }
        return XamlStr.StripPrefix(t);
    }

    private static string NormalizeResourceKey(string raw) {
        if (raw == null) { return ""; }
        string t = raw.Trim();
        if (XamlStr.StartsWith(t, "{x:Type") || XamlStr.StartsWith(t, "{Type") ||
            XamlStr.StartsWith(t, "{x:Static") || XamlStr.StartsWith(t, "{Static")) {
            string body = XamlStr.ExtractExtBody(t).Trim();
            int colon = body.IndexOf(":");
            int dot = body.IndexOf(".");
            if (colon > 0 && colon + 1 < body.Length && (dot < 0 || colon < dot)) {
                body = body.Substring(colon + 1);
            }
            return body;
        }
        return t;
    }

    private static DependencyObject ResolveStaticConstantBox(string spec) {
        string s = spec == null ? "" : spec.Trim();
        if (s == "Visibility.Visible" || s == "Visible") { return DependencyObject.BoxInt(0); }
        if (s == "Visibility.Hidden" || s == "Hidden") { return DependencyObject.BoxInt(1); }
        if (s == "Visibility.Collapsed" || s == "Collapsed") { return DependencyObject.BoxInt(2); }
        if (s == "Double.NaN" || s == "double.NaN" || s == "sys:Double.NaN") { return DependencyObject.BoxFloat(ParseUtil.NaN()); }
        if (s == "Orientation.Horizontal") { return DependencyObject.BoxInt(0); }
        if (s == "Orientation.Vertical") { return DependencyObject.BoxInt(1); }
        if (s == "Dock.Left") { return DependencyObject.BoxInt(0); }
        if (s == "Dock.Top") { return DependencyObject.BoxInt(1); }
        if (s == "Dock.Right") { return DependencyObject.BoxInt(2); }
        if (s == "Dock.Bottom") { return DependencyObject.BoxInt(3); }
        if (s == "Stretch.None") { return DependencyObject.BoxInt(0); }
        if (s == "Stretch.Fill") { return DependencyObject.BoxInt(1); }
        if (s == "Stretch.Uniform") { return DependencyObject.BoxInt(2); }
        if (s == "Stretch.UniformToFill") { return DependencyObject.BoxInt(3); }
        if (s == "HorizontalAlignment.Stretch") { return DependencyObject.BoxInt(0); }
        if (s == "HorizontalAlignment.Left") { return DependencyObject.BoxInt(1); }
        if (s == "HorizontalAlignment.Center") { return DependencyObject.BoxInt(2); }
        if (s == "HorizontalAlignment.Right") { return DependencyObject.BoxInt(3); }
        if (s == "VerticalAlignment.Stretch") { return DependencyObject.BoxInt(0); }
        if (s == "VerticalAlignment.Top") { return DependencyObject.BoxInt(1); }
        if (s == "VerticalAlignment.Center") { return DependencyObject.BoxInt(2); }
        if (s == "VerticalAlignment.Bottom") { return DependencyObject.BoxInt(3); }
        if (s == "Brushes.Transparent") { return Brushes.Transparent(); }
        if (s == "Brushes.White") { return Brushes.White(); }
        if (s == "Brushes.Black") { return Brushes.Black(); }
        if (s == "Brushes.Red") { return Brushes.Red(); }
        if (s == "Brushes.Green") { return Brushes.Green(); }
        if (s == "Brushes.Blue") { return Brushes.Blue(); }
        if (XamlStr.StartsWith(s, "Brushes.")) {
            uint rgba = ParseUtil.ParseColor(s.Substring(8));
            return new SolidColorBrush(rgba);
        }
        if (XamlStr.StartsWith(s, "Colors.")) {
            uint rgba = ParseUtil.ParseColor(s.Substring(7));
            return DependencyObject.BoxString(ParseUtil.FormatArgbHex(rgba));
        }
        if (s == "ApplicationCommands.Close") { return DependencyObject.BoxObject(ApplicationCommands.Close()); }
        if (s == "ApplicationCommands.Cut") { return DependencyObject.BoxObject(ApplicationCommands.Cut()); }
        if (s == "ApplicationCommands.Copy") { return DependencyObject.BoxObject(ApplicationCommands.Copy()); }
        if (s == "ApplicationCommands.Paste") { return DependencyObject.BoxObject(ApplicationCommands.Paste()); }
        if (s == "ApplicationCommands.Save") { return DependencyObject.BoxObject(ApplicationCommands.Save()); }
        if (s == "ApplicationCommands.Open") { return DependencyObject.BoxObject(ApplicationCommands.Open()); }
        if (s == "ApplicationCommands.New") { return DependencyObject.BoxObject(ApplicationCommands.New()); }
        if (s == "ApplicationCommands.Delete") { return DependencyObject.BoxObject(ApplicationCommands.Delete()); }
        if (s == "ApplicationCommands.SelectAll") { return DependencyObject.BoxObject(ApplicationCommands.SelectAll()); }
        return DependencyObject.BoxString(s);
    }

    private static XamlParsedBinding ParseBindingSpec(string rawExt, XamlBuildContext ctx) {
        var b = new XamlParsedBinding();
        string kind = XamlStr.ExtractExtType(rawExt);
        string body = XamlStr.ExtractExtBody(rawExt);
        if (kind == "TemplateBinding") {
            b.RelativeSourceMode = "TemplatedParent";
            b.Path = body.Trim();
            if (b.Path.Contains(",")) {
                string[] parts = XamlStr.SplitTopLevelCommas(body);
                if (parts.Length > 0) { b.Path = parts[0].Trim(); }
            }
            return b;
        }
        if (body.Length == 0) {
            b.Path = ".";
            return b;
        }
        string[] segs = XamlStr.SplitTopLevelCommas(body);
        for (int i = 0; i < segs.Length; i += 1) {
            string seg = segs[i].Trim();
            if (seg.Length == 0) { continue; }
            int eq = XamlStr.FindTopLevelEquals(seg);
            if (eq < 0) {
                if (i == 0) { b.Path = XamlStr.Unquote(seg); }
                continue;
            }
            string k = seg.Substring(0, eq).Trim();
            string v = XamlStr.Unquote(seg.Substring(eq + 1).Trim());
            if (k == "Path") {
                b.Path = XamlStr.StripOuterParens(v);
            } else if (k == "ElementName") {
                b.ElementName = v;
            } else if (k == "Mode") {
                if (v == "TwoWay") { b.Mode = 1; }
                else if (v == "OneTime") { b.Mode = 2; }
                else if (v == "OneWayToSource") { b.Mode = 3; }
                else { b.Mode = 0; }
            } else if (k == "Converter") {
                if (XamlStr.LooksLikeMarkupExtension(v)) {
                    DependencyObject cBox = ResolveMarkupValue(v, ctx, null);
                    if (cBox is IValueConverter) {
                        b.Converter = cBox as IValueConverter;
                    } else if (cBox != null && DependencyObject.Unbox(cBox) is IValueConverter) {
                        b.Converter = DependencyObject.Unbox(cBox) as IValueConverter;
                    }
                    if (b.Converter == null && XamlStr.StartsWith(v.Trim(), "{StaticResource")) {
                        b.ConverterKey = XamlStr.ExtractExtBody(v).Trim();
                    }
                } else {
                    b.ConverterKey = v;
                }
            } else if (k == "ConverterParameter") {
                if (XamlStr.LooksLikeMarkupExtension(v)) {
                    b.ConverterParameter = DependencyObject.UnboxString(ResolveMarkupValue(v, ctx, null), v);
                } else {
                    b.ConverterParameter = v;
                }
            } else if (k == "StringFormat") {
                if (XamlStr.StartsWith(v, "{}")) { v = v.Substring(2); }
                b.StringFormat = v;
            } else if (k == "FallbackValue") {
                b.HasFallback = true;
                b.FallbackBox = XamlStr.LooksLikeMarkupExtension(v)
                    ? ResolveMarkupValue(v, ctx, null)
                    : DependencyObject.BoxString(v);
            } else if (k == "TargetNullValue") {
                b.HasTargetNull = true;
                b.TargetNullBox = XamlStr.LooksLikeMarkupExtension(v)
                    ? ResolveMarkupValue(v, ctx, null)
                    : DependencyObject.BoxString(v);
            } else if (k == "Source") {
                if (XamlStr.LooksLikeMarkupExtension(v)) {
                    b.ExplicitSourceBox = ResolveMarkupValue(v, ctx, null);
                } else {
                    b.ExplicitSourceBox = DependencyObject.BoxString(v);
                }
            } else if (k == "RelativeSource") {
                ParseRelativeSourceClause(v, b);
            }
        }
        return b;
    }

    private static void ParseRelativeSourceClause(string raw, XamlParsedBinding b) {
        if (raw == null || raw.Length == 0) { return; }
        string t = raw.Trim();
        if (XamlStr.StartsWith(t, "{") && XamlStr.EndsWith(t, "}")) {
            t = XamlStr.ExtractExtBody(t);
        }
        string[] parts = XamlStr.SplitTopLevelCommas(t);
        for (int i = 0; i < parts.Length; i += 1) {
            string p = parts[i].Trim();
            if (p.Length == 0) { continue; }
            int eq = XamlStr.FindTopLevelEquals(p);
            if (eq < 0) {
                if (p == "TemplatedParent" || p == "Self" || p == "FindAncestor") {
                    b.RelativeSourceMode = p;
                }
                continue;
            }
            string pk = p.Substring(0, eq).Trim();
            string pv = XamlStr.Unquote(p.Substring(eq + 1).Trim());
            if (pk == "Mode") {
                b.RelativeSourceMode = pv;
            } else if (pk == "AncestorType") {
                b.AncestorType = UnwrapTypeSpec(pv);
                if (b.RelativeSourceMode.Length == 0) { b.RelativeSourceMode = "FindAncestor"; }
            }
        }
        if (b.RelativeSourceMode.Length == 0 && b.AncestorType.Length > 0) {
            b.RelativeSourceMode = "FindAncestor";
        }
    }

    private static DependencyObject ResolveMarkupValue(string rawExt, XamlBuildContext ctx, UIElement targetEl) {
        string kind = XamlStr.ExtractExtType(rawExt);
        string body = XamlStr.ExtractExtBody(rawExt);
        if (kind == "x:Null" || kind == "Null") { return null; }
        if (kind == "x:Static" || kind == "Static") { return ResolveStaticConstantBox(body); }
        if (kind == "x:Type" || kind == "Type") { return DependencyObject.BoxString(UnwrapTypeSpec(rawExt)); }
        if (kind == "StaticResource" || kind == "DynamicResource") {
            string key = NormalizeResourceKey(body);
            return ctx != null ? ctx.FindResourceBox(key, targetEl) : null;
        }
        return DependencyObject.BoxString(rawExt);
    }

    private static Binding ToBindingObject(XamlParsedBinding pb) {
        var b = new Binding();
        b.Path = pb.Path;
        b.ElementName = pb.ElementName;
        b.Mode = pb.Mode == 1 ? BindingMode.TwoWay : (pb.Mode == 2 ? BindingMode.OneTime : BindingMode.OneWay);
        b.Converter = pb.Converter;
        b.ConverterKey = pb.ConverterKey;
        b.ConverterParameter = pb.ConverterParameter;
        b.StringFormat = pb.StringFormat;
        b.RelativeSourceMode = pb.RelativeSourceMode;
        b.RelativeSourceAncestorType = pb.AncestorType;
        if (pb.ExplicitSourceBox != null) { b.SourceBox = pb.ExplicitSourceBox; }
        if (pb.HasFallback) { b.FallbackBox = pb.FallbackBox; }
        if (pb.HasTargetNull) { b.TargetNullBox = pb.TargetNullBox; }
        return b;
    }

    private static void BindElementFromSpec(UIElement target, string targetProp, XamlParsedBinding pb, XamlBuildContext ctx) {
        if (target == null || targetProp.Length == 0 || pb == null) { return; }
        if (pb.RelativeSourceMode == "TemplatedParent") {
            UIElement tp = ctx != null ? ctx.TemplatedParent : target.TemplatedParent;
            BindingEngine.BindTemplateParentEx(
                target, targetProp, tp as Control, pb.Path,
                pb.Converter, pb.ConverterKey, pb.ConverterParameter, pb.StringFormat,
                pb.HasFallback, pb.FallbackBox, pb.HasTargetNull, pb.TargetNullBox, pb.Mode);
            return;
        }
        if (pb.ElementName.Length > 0) {
            BindingEngine.BindElementEx(
                target, targetProp, pb.ElementName, pb.Path, pb.Mode,
                pb.Converter, pb.ConverterKey, pb.ConverterParameter, pb.StringFormat,
                pb.HasFallback, pb.FallbackBox, pb.HasTargetNull, pb.TargetNullBox);
            return;
        }
        object srcObj = pb.ExplicitSourceBox != null ? DependencyObject.Unbox(pb.ExplicitSourceBox) : null;
        BindingEngine.BindPathEx(
            target, targetProp, srcObj, pb.Path, pb.Mode,
            pb.Converter, pb.ConverterKey, pb.ConverterParameter, pb.StringFormat,
            pb.HasFallback, pb.FallbackBox, pb.HasTargetNull, pb.TargetNullBox,
            pb.RelativeSourceMode, pb.AncestorType);
    }

    private static bool IsBrushPropertyName(string prop) {
        return prop == "Background" || prop == "Foreground" || prop == "BorderBrush" ||
               prop == "Fill" || prop == "Stroke" || prop == "OpacityMask" ||
               prop == "TextBlock.Foreground" || prop == "TextElement.Foreground";
    }

    private static DependencyObject CoerceLiteralForProperty(string propName, string rawVal) {
        if (rawVal == null) { return null; }
        string val = rawVal;
        if (XamlStr.StartsWith(val, "{}")) { val = val.Substring(2); }
        string p = propName == null ? "" : propName;
        if (IsBrushPropertyName(p)) {
            return new SolidColorBrush(val.Trim());
        }
        if (p == "Margin" || p == "Padding" || p == "BorderThickness") {
            return Thickness.Parse(val);
        }
        if (p == "CornerRadius") {
            return CornerRadius.Parse(val);
        }
        if (p == "RenderTransformOrigin" || p == "StartPoint" || p == "EndPoint" ||
            p == "Center" || p == "GradientOrigin") {
            return Point.Parse(val);
        }
        if (p == "Width" || p == "Height" || p == "MinWidth" || p == "MinHeight" ||
            p == "MaxWidth" || p == "MaxHeight") {
            string trimmed = val.Trim();
            string low = trimmed.ToLower();
            if ((p == "Width" || p == "Height") && trimmed.Contains("*")) {
                return GridLength.Parse(trimmed);
            }
            if (low == "auto" || low == "nan") { return DependencyObject.BoxFloat(ParseUtil.NaN()); }
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(val, ParseUtil.NaN()));
        }
        if (p == "DragDrop.MatchKind" || p == "DragDrop.IsOverlayHost" ||
            p == "DragDrop.AutoTransfer" || p == "DragDrop.IsContentEmpty" ||
            p == "DragDrop.IsDropCompatible") {
            return DependencyObject.BoxBool(ParseUtil.ParseBool(val, false));
        }
        if (p == "ToolTipService.InitialShowDelay" || p == "ToolTipService.BetweenShowDelay" ||
            p == "ToolTipService.ShowDuration") {
            return DependencyObject.BoxInt(ParseUtil.ParseInt(val, 0));
        }
        if (p == "Opacity" || p == "SelectionOpacity") {
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(val, 1.0f));
        }
        if (XamlStr.StartsWith(p, "Custom.") && XamlStr.EndsWith(p, "_Single")) {
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(val, 0.0f));
        }
        return DependencyObject.BoxString(val);
    }

    private static void ApplyXmlAttr(UIElement el, XamlXmlAttr a, XamlBuildContext ctx) {
        if (el == null || a == null || a.Name == null || a.Name.Length == 0) { return; }
        if (!a.CachedInit) {
            a.CachedInit = true;
            a.CachedPropName = XamlStr.StripOuterParens(a.Name);
            string normKey = NormalizeSetterProp(a.CachedPropName);
            if (normKey != "Style" && normKey != "Name" &&
                !XamlStr.StartsWith(normKey, "Grid.") &&
                !XamlStr.StartsWith(normKey, "Canvas.") &&
                !XamlStr.StartsWith(normKey, "DockPanel.")) {
                a.CachedLocalKey = normKey;
            }
            a.IsToolTipProp = (normKey == "ToolTip" || normKey == "ToolTipService.ToolTip");
            if (!XamlStr.LooksLikeMarkupExtension(a.Value)) {
                a.CachedExtMode = 0;
            } else {
                string kind = XamlStr.ExtractExtType(a.Value);
                string body = XamlStr.ExtractExtBody(a.Value);
                if (kind == "DynamicResource") {
                    a.CachedExtMode = 1;
                    a.CachedKey = NormalizeResourceKey(body);
                } else if (kind == "TemplateBinding") {
                    a.CachedExtMode = 2;
                    a.CachedBinding = ParseBindingSpec(a.Value, null);
                } else if (kind == "Binding" &&
                           !body.Contains("StaticResource") &&
                           !body.Contains("DynamicResource") &&
                           !body.Contains("x:Static") &&
                           !body.Contains("Static ")) {
                    a.CachedExtMode = 2;
                    a.CachedBinding = ParseBindingSpec(a.Value, null);
                } else if (kind == "StaticResource") {
                    a.CachedExtMode = 3;
                    a.CachedKey = NormalizeResourceKey(body);
                } else {
                    a.CachedExtMode = 4;
                }
            }
        }
        if (a.CachedPropName.Length == 0) { return; }
        if (XamlStr.StartsWith(a.CachedPropName, "DragDrop.") || a.CachedPropName.Contains(":DragDrop.")) {
            DragDrop.EnsureInstalled();
        }
        if (a.CachedLocalKey.Length > 0) {
            el.MarkLocalProperty(a.CachedLocalKey);
        }
        if (a.IsToolTipProp) {
            ToolTipService.EnsureInstalled();
        }
        if (a.CachedExtMode == 0) {
            el.ClearResourceReference(a.CachedPropName);
            ApplyLiteralProperty(el, a.CachedPropName, a.Value, ctx);
            return;
        }
        if (a.CachedExtMode == 1) {
            el.SetResourceReference(a.CachedPropName, a.CachedKey);
            return;
        }
        if (a.CachedExtMode == 2) {
            el.ClearResourceReference(a.CachedPropName);
            BindElementFromSpec(el, a.CachedPropName, a.CachedBinding, ctx);
            return;
        }
        if (a.CachedExtMode == 3) {
            el.ClearResourceReference(a.CachedPropName);
            DependencyObject resBox = ctx != null
                ? ctx.FindResourceBox(a.CachedKey, el)
                : el.FindResourceBox(a.CachedKey);
            if (resBox != null) {
                ApplyResolvedPropertyBox(el, a.CachedPropName, resBox, ctx);
            }
            return;
        }
        ApplyPropertyValueOrExt(el, a.Name, a.Value, ctx);
    }

    private static void ApplyPropertyValueOrExt(
        UIElement el, string rawPropName, string rawVal, XamlBuildContext ctx) {
        if (el == null || rawPropName == null || rawPropName.Length == 0) { return; }
        string propName = XamlStr.StripOuterParens(rawPropName);
        el.MarkLocalProperty(NormalizeSetterProp(propName));
        if (XamlStr.StartsWith(propName, "DragDrop.") || propName.Contains(":DragDrop.")) {
            DragDrop.EnsureInstalled();
        }

        if (XamlStr.LooksLikeMarkupExtension(rawVal)) {
            string kind = XamlStr.ExtractExtType(rawVal);
            string body = XamlStr.ExtractExtBody(rawVal);
            if (kind == "DynamicResource") {
                el.SetResourceReference(propName, NormalizeResourceKey(body));
                return;
            }
            el.ClearResourceReference(propName);
            if (kind == "Binding" || kind == "TemplateBinding") {
                XamlParsedBinding pb = ParseBindingSpec(rawVal, ctx);
                BindElementFromSpec(el, propName, pb, ctx);
                return;
            }
            if (kind == "StaticResource") {
                DependencyObject resBox = ResolveMarkupValue(rawVal, ctx, el);
                if (resBox != null) {
                    ApplyResolvedPropertyBox(el, propName, resBox, ctx);
                }
                return;
            }
            DependencyObject mBox = ResolveMarkupValue(rawVal, ctx, el);
            ApplyResolvedPropertyBox(el, propName, mBox, ctx);
            return;
        }

        el.ClearResourceReference(propName);
        ApplyLiteralProperty(el, propName, rawVal, ctx);
    }

    private static void ApplyLiteralProperty(
        UIElement el, string propName, string rawVal, XamlBuildContext ctx) {
        string val = rawVal == null ? "" : rawVal;
        if (XamlStr.StartsWith(val, "{}")) { val = val.Substring(2); }

        if (propName == "Text" && el is TextBlock) {
            (el as TextBlock).Text = val;
            return;
        }
        if (propName == "Margin") {
            el.Margin = Thickness.Parse(val);
            return;
        }
        if (propName == "Padding") {
            el.Padding = Thickness.Parse(val);
            return;
        }
        if (propName == "Width" && !val.Contains("*")) {
            string lowW = val.Trim().ToLower();
            el.Width = (lowW == "auto" || lowW == "nan") ? ParseUtil.NaN() : ParseUtil.ParseFloat(val, ParseUtil.NaN());
            return;
        }
        if (propName == "Height" && !val.Contains("*")) {
            string lowH = val.Trim().ToLower();
            el.Height = (lowH == "auto" || lowH == "nan") ? ParseUtil.NaN() : ParseUtil.ParseFloat(val, ParseUtil.NaN());
            return;
        }
        if (propName == "Grid.Row") {
            UIElement.SetGridRow(el, ParseUtil.ParseInt(val, 0));
            return;
        }
        if (propName == "Grid.Column") {
            UIElement.SetGridColumn(el, ParseUtil.ParseInt(val, 0));
            return;
        }
        if (propName == "HorizontalAlignment") {
            string ha = val.Trim();
            if (ha == "Left") { el.HorizontalAlignment = HorizontalAlignment.Left; }
            else if (ha == "Center") { el.HorizontalAlignment = HorizontalAlignment.Center; }
            else if (ha == "Right") { el.HorizontalAlignment = HorizontalAlignment.Right; }
            else { el.HorizontalAlignment = HorizontalAlignment.Stretch; }
            return;
        }
        if (propName == "VerticalAlignment") {
            string va = val.Trim();
            if (va == "Top") { el.VerticalAlignment = VerticalAlignment.Top; }
            else if (va == "Center") { el.VerticalAlignment = VerticalAlignment.Center; }
            else if (va == "Bottom") { el.VerticalAlignment = VerticalAlignment.Bottom; }
            else { el.VerticalAlignment = VerticalAlignment.Stretch; }
            return;
        }
        if (propName == "Visibility") {
            string vs = val.Trim();
            if (vs == "Collapsed") { el.Visibility = Visibility.Collapsed; }
            else if (vs == "Hidden") { el.Visibility = Visibility.Hidden; }
            else { el.Visibility = Visibility.Visible; }
            return;
        }
        if (propName == "Orientation" && el is StackPanel) {
            (el as StackPanel).Orientation = val.Trim() == "Horizontal" ? Orientation.Horizontal : Orientation.Vertical;
            return;
        }
        if (propName == "Content" && el is ContentControl) {
            (el as ContentControl).SetContent(val);
            return;
        }
        if (propName == "Header" && el is HeaderedContentControl) {
            (el as HeaderedContentControl).SetHeader(val);
            return;
        }
        if (propName == "Background" && !(el is Shape)) {
            el.Background = new SolidColorBrush(val.Trim());
            return;
        }

        if (propName == "TransitionContentPresenter.Enter") {
            TransitionContentPresenter.SetEnterKey(el, val.Trim());
            return;
        }
        if (propName == "TransitionContentPresenter.Exit") {
            TransitionContentPresenter.SetExitKey(el, val.Trim());
            return;
        }
        if (propName == "RichText.Text") {
            RichText.Register();
            if (el is TextBlock) { RichText.Apply(el as TextBlock, val); }
            return;
        }
        if (propName == "DockPanel.Dock" || propName == "Dock") {
            string d = val.Trim();
            if (d == "Top") { UIElement.SetDock(el, Dock.Top); }
            else if (d == "Right") { UIElement.SetDock(el, Dock.Right); }
            else if (d == "Bottom") { UIElement.SetDock(el, Dock.Bottom); }
            else { UIElement.SetDock(el, Dock.Left); }
            return;
        }
        if (el is Grid) {
            Grid g = el as Grid;
            if (propName == "RowSpacing") { g.RowSpacing = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "ColumnSpacing") { g.ColumnSpacing = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "RowDefinitions") { g.SetRowDefinitions(val); return; }
            if (propName == "ColumnDefinitions") { g.SetColumnDefinitions(val); return; }
        }
        if (el is DockPanel && propName == "LastChildFill") {
            (el as DockPanel).LastChildFill = ParseUtil.ParseBool(val, true);
            return;
        }
        if (el is WrapPanel) {
            WrapPanel wp = el as WrapPanel;
            if (propName == "ItemWidth") { wp.ItemWidth = ParseUtil.ParseFloat(val, ParseUtil.NaN()); return; }
            if (propName == "ItemHeight") { wp.ItemHeight = ParseUtil.ParseFloat(val, ParseUtil.NaN()); return; }
            if (propName == "Orientation") {
                wp.Orientation = val.Trim() == "Vertical" ? Orientation.Vertical : Orientation.Horizontal;
                return;
            }
        }
        if (el is UniformGrid) {
            UniformGrid ug = el as UniformGrid;
            if (propName == "Rows") { ug.Rows = ParseUtil.ParseInt(val, 0); return; }
            if (propName == "Columns") { ug.Columns = ParseUtil.ParseInt(val, 0); return; }
            if (propName == "FirstColumn") { ug.FirstColumn = ParseUtil.ParseInt(val, 0); return; }
        }
        if (el is Slider) {
            Slider sl = el as Slider;
            if (propName == "IsSnapToTickEnabled") { sl.IsSnapToTickEnabled = ParseUtil.ParseBool(val, false); return; }
            if (propName == "IsMoveToPointEnabled") { sl.IsMoveToPointEnabled = ParseUtil.ParseBool(val, false); return; }
            if (propName == "Ticks") { sl.Ticks = val; return; }
            if (propName == "SmallChange") { sl.SmallChange = ParseUtil.ParseFloat(val, 0.1f); return; }
            if (propName == "LargeChange") { sl.LargeChange = ParseUtil.ParseFloat(val, 1.0f); return; }
        }
        if (el is ProgressBar && propName == "IsIndeterminate") {
            (el as ProgressBar).IsIndeterminate = ParseUtil.ParseBool(val, false);
            return;
        }
        if (el is TabControl && propName == "TabStripPlacement") {
            TabControl tc = el as TabControl;
            string t = val.Trim();
            if (t == "Bottom") { tc.TabStripPlacement = TabStripPlacement.Bottom; }
            else if (t == "Left") { tc.TabStripPlacement = TabStripPlacement.Left; }
            else if (t == "Right") { tc.TabStripPlacement = TabStripPlacement.Right; }
            else { tc.TabStripPlacement = TabStripPlacement.Top; }
            return;
        }
        if (el is ListBox && propName == "SelectionMode") {
            ListBox lb = el as ListBox;
            string sm = val.Trim();
            if (sm == "Multiple") { lb.SelectionMode = SelectionMode.Multiple; }
            else if (sm == "Extended") { lb.SelectionMode = SelectionMode.Extended; }
            else { lb.SelectionMode = SelectionMode.Single; }
            return;
        }
        if (el is TextBox) {
            TextBox tb = el as TextBox;
            if (propName == "IsReadOnly") { tb.IsReadOnly = ParseUtil.ParseBool(val, false); return; }
            if (propName == "AcceptsReturn") { tb.AcceptsReturn = ParseUtil.ParseBool(val, false); return; }
            if (propName == "Placeholder" || propName == "Watermark") { tb.Placeholder = val; return; }
            if (propName == "TextWrapping") {
                string tw = val.Trim();
                tb.TextWrapping = tw == "Wrap" ? TextWrapping.Wrap : (tw == "WrapWithOverflow" ? TextWrapping.WrapWithOverflow : TextWrapping.NoWrap);
                return;
            }
        }
        if (el is PasswordBox && propName == "PasswordChar") {
            (el as PasswordBox).PasswordChar = val;
            return;
        }
        if (el is TextBlock && (propName == "TextDecorations" || propName == "Decorations")) {
            (el as TextBlock).TextDecorations = val.Trim() == "Underline" ? 1 : ParseUtil.ParseInt(val, 0);
            return;
        }
        if (el is Popup) {
            Popup pop = el as Popup;
            if (propName == "Placement") { pop.Placement = val.Trim(); return; }
            if (propName == "StaysOpen") { pop.StaysOpen = ParseUtil.ParseBool(val, false); return; }
            if (propName == "IsOpen") { pop.IsOpen = ParseUtil.ParseBool(val, false); return; }
            if (propName == "HorizontalOffset") { pop.HorizontalOffset = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "VerticalOffset") { pop.VerticalOffset = ParseUtil.ParseFloat(val, 0.0f); return; }
        }
        if (el is PolyShapeBase && propName == "Points") {
            (el as PolyShapeBase).Points = val;
            return;
        }
        if (el is Line) {
            Line ln = el as Line;
            if (propName == "X1") { ln.X1 = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "Y1") { ln.Y1 = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "X2") { ln.X2 = ParseUtil.ParseFloat(val, 0.0f); return; }
            if (propName == "Y2") { ln.Y2 = ParseUtil.ParseFloat(val, 0.0f); return; }
        }
        if ((propName == "ImageSource" || propName == "Source") && el is Image) {
            string imgUri = (ctx != null && ctx.BaseUri.Length > 0) ? XamlStr.ResolveUri(ctx.BaseUri, val) : val.Trim();
            (el as Image).Source = imgUri;
            return;
        }
        if (propName == "RenderTransform") {
            if (val.Contains(",")) {
                string[] nums = val.Split(",");
                if (nums.Length >= 6) {
                    el.RenderTransform = new TranslateTransform(
                        ParseUtil.ParseFloat(nums[4], 0.0f),
                        ParseUtil.ParseFloat(nums[5], 0.0f));
                }
            }
            return;
        }
        if (propName == "LayoutTransform") {
            if (val.Contains(",")) {
                string[] nums = val.Split(",");
                if (nums.Length >= 4) {
                    el.LayoutTransform = new ScaleTransform(
                        ParseUtil.ParseFloat(nums[0], 1.0f),
                        ParseUtil.ParseFloat(nums[3], 1.0f));
                }
            }
            return;
        }
        if (propName == "DataContext") {
            el.DataContextBox = DependencyObject.BoxString(val);
            return;
        }

        DependencyObject boxed = CoerceLiteralForProperty(propName, val);
        el.SetPropertyValue(propName, boxed);
    }

    private static void ApplyResolvedPropertyBox(
        UIElement el, string propName, DependencyObject valBox, XamlBuildContext ctx) {
        if (el == null) { return; }
        if (propName == "DockPanel.Dock" || propName == "Dock") {
            int di = DependencyObject.UnboxInt(valBox, 0);
            UIElement.SetDock(el, EnumConvert.ToDock(di));
            return;
        }
        if (propName == "TransitionContentPresenter.Enter") {
            TransitionContentPresenter.SetEnterKey(el, DependencyObject.UnboxString(valBox, ""));
            return;
        }
        if (propName == "TransitionContentPresenter.Exit") {
            TransitionContentPresenter.SetExitKey(el, DependencyObject.UnboxString(valBox, ""));
            return;
        }
        if (propName == "DataContext") {
            el.DataContextBox = valBox;
            return;
        }
        if (el is ItemsControl && propName == "ItemTemplate" && valBox is DataTemplate) {
            (el as ItemsControl).ItemTemplate = valBox as DataTemplate;
            return;
        }
        if (el is ItemsControl && propName == "ItemContainerStyle" && valBox is Style) {
            (el as ItemsControl).ItemContainerStyle = valBox as Style;
            return;
        }
        if (el is ItemsControl && propName == "ItemsPanel" && valBox is ItemsPanelTemplate) {
            (el as ItemsControl).ItemsPanel = (valBox as ItemsPanelTemplate).GetFactory();
            return;
        }
        if (el is ContentControl && propName == "ContentTemplate" && valBox is DataTemplate) {
            (el as ContentControl).ContentTemplate = valBox as DataTemplate;
            return;
        }
        if (el is HeaderedContentControl && propName == "HeaderTemplate" && valBox is DataTemplate) {
            (el as HeaderedContentControl).HeaderTemplate = valBox as DataTemplate;
            return;
        }
        if (el is HeaderedItemsControl && propName == "HeaderTemplate" && valBox is DataTemplate) {
            (el as HeaderedItemsControl).HeaderTemplate = valBox as DataTemplate;
            return;
        }
        if (el is ListView && propName == "View" && valBox is GridView) {
            (el as ListView).View = valBox as GridView;
            return;
        }
        el.SetPropertyValue(propName, valBox);
    }

    private static bool IsIgnoredAttributeName(string attrName, string tagName) {
        if (attrName == "xmlns" || XamlStr.StartsWith(attrName, "xmlns:") ||
            XamlStr.StartsWith(attrName, "mc:") || XamlStr.StartsWith(attrName, "d:") ||
            attrName == "x:Class" || attrName == "x:Key" || attrName == "x:Shared" ||
            attrName == "x:DataType" || attrName == "TargetType" || attrName == "DataType" ||
            attrName == "BasedOn" || (attrName == "Source" && tagName == "ResourceDictionary")) {
            return true;
        }
        if (attrName == "x:Name" || attrName == "Name") { return false; }
        return attrName == "SnapsToDevicePixels" || attrName == "UseLayoutRounding";
    }

    private static Dictionary<string, int> _uiTagKindMap;

    private static void EnsureUiTagKindMap() {
        if (_uiTagKindMap != null) { return; }
        var m = new Dictionary<string, int>();
        m["Grid"] = 1;
        m["StackPanel"] = 2;
        m["VirtualizingStackPanel"] = 3;
        m["TabPanel"] = 4;
        m["ToolBarPanel"] = 5;
        m["DockPanel"] = 6;
        m["WrapPanel"] = 7;
        m["ToolBarOverflowPanel"] = 8;
        m["UniformGrid"] = 9;
        m["Canvas"] = 10;
        m["Panel"] = 11;
        m["Border"] = 12;
        m["Decorator"] = 13;
        m["BoxShadow"] = 14;
        m["Viewbox"] = 15;
        m["ScrollViewer"] = 16;
        m["ScrollContentPresenter"] = 17;
        m["ContentControl"] = 18;
        m["ContentPresenter"] = 19;
        m["TransitionContentPresenter"] = 19;
        m["Label"] = 20;
        m["HeaderedContentControl"] = 21;
        m["GroupBox"] = 22;
        m["Expander"] = 23;
        m["Separator"] = 24;
        m["UserControl"] = 25;
        m["Page"] = 26;
        m["Window"] = 27;
        m["TextBlock"] = 28;
        m["RichText"] = 28;
        m["TextBox"] = 29;
        m["PasswordBox"] = 30;
        m["Button"] = 31;
        m["RepeatButton"] = 32;
        m["ToggleButton"] = 33;
        m["CheckBox"] = 34;
        m["RadioButton"] = 35;
        m["Hyperlink"] = 36;
        m["Slider"] = 37;
        m["ProgressBar"] = 38;
        m["ScrollBar"] = 39;
        m["Track"] = 40;
        m["Thumb"] = 41;
        m["TickBar"] = 42;
        m["GridSplitter"] = 43;
        m["ItemsControl"] = 44;
        m["ItemsPresenter"] = 45;
        m["ListBox"] = 46;
        m["ListBoxItem"] = 47;
        m["ListView"] = 48;
        m["ListViewItem"] = 49;
        m["ComboBox"] = 50;
        m["ComboBoxItem"] = 51;
        m["TabControl"] = 52;
        m["TabItem"] = 53;
        m["TreeView"] = 54;
        m["TreeViewItem"] = 55;
        m["Menu"] = 56;
        m["MenuItem"] = 57;
        m["ContextMenu"] = 58;
        m["ToolBar"] = 59;
        m["StatusBar"] = 60;
        m["StatusBarItem"] = 61;
        m["ToolTip"] = 62;
        m["Popup"] = 63;
        m["GridViewColumnHeader"] = 64;
        m["GridViewHeaderRowPresenter"] = 65;
        m["GridViewRowPresenter"] = 66;
        m["Image"] = 67;
        m["Rectangle"] = 68;
        m["Ellipse"] = 69;
        m["Line"] = 70;
        m["Path"] = 71;
        m["Geometry"] = 72;
        m["Polygon"] = 73;
        m["Polyline"] = 74;
        m["CustomDrawElement"] = 75;
        m["RiveControl"] = 76;
        m["RiveInput"] = 77;
        m["NumericScrubber"] = 78;
        m["Vector2Field"] = 79;
        m["Vector3Field"] = 80;
        m["ViewportSurface"] = 81;
        m["DockPane"] = 82;
        m["DockTabGroup"] = 83;
        m["DockSplitNode"] = 84;
        m["DockingWorkspace"] = 85;
        m["CodeEditorView"] = 86;
        m["PropertyGrid"] = 87;
        m["NodeGraphCanvas"] = 88;
        _uiTagKindMap = m;
    }

    private static UIElement CreateUIElementByTag(string localName) {
        if (localName == null || localName.Length == 0) { return null; }
        EnsureUiTagKindMap();
        if (!_uiTagKindMap.ContainsKey(localName)) { return null; }
        int kind = _uiTagKindMap[localName];
        switch (kind) {
            case 1: return new Grid();
            case 2: return new StackPanel();
            case 3: return new VirtualizingStackPanel();
            case 4: return new TabPanel();
            case 5: return new ToolBarPanel();
            case 6: return new DockPanel();
            case 7: return new WrapPanel();
            case 8: return new ToolBarOverflowPanel();
            case 9: return new UniformGrid();
            case 10: return new Canvas();
            case 11: return new Panel();
            case 12: return new Border();
            case 13: return new Decorator();
            case 14: return new BoxShadow();
            case 15: return new Viewbox();
            case 16: return new ScrollViewer();
            case 17: return new ScrollContentPresenter();
            case 18: return new ContentControl();
            case 19: return new ContentPresenter();
            case 20: return new Label();
            case 21: return new HeaderedContentControl();
            case 22: return new GroupBox();
            case 23: return new Expander();
            case 24: return new Separator();
            case 25: return new UserControl();
            case 26: return new Page();
            case 27: return new Window();
            case 28: return new TextBlock();
            case 29: return new TextBox();
            case 30: return new PasswordBox();
            case 31: return new Button();
            case 32: return new RepeatButton();
            case 33: return new ToggleButton();
            case 34: return new CheckBox();
            case 35: return new RadioButton();
            case 36: return new Hyperlink();
            case 37: return new Slider();
            case 38: return new ProgressBar();
            case 39: return new ScrollBar();
            case 40: return new Track();
            case 41: return new Thumb();
            case 42: return new TickBar();
            case 43: return new GridSplitter();
            case 44: return new ItemsControl();
            case 45: return new ItemsPresenter();
            case 46: return new ListBox();
            case 47: return new ListBoxItem();
            case 48: return new ListView();
            case 49: return new ListViewItem();
            case 50: return new ComboBox();
            case 51: return new ComboBoxItem();
            case 52: return new TabControl();
            case 53: return new TabItem();
            case 54: return new TreeView();
            case 55: return new TreeViewItem();
            case 56: return new Menu();
            case 57: return new MenuItem();
            case 58: return new ContextMenu();
            case 59: return new ToolBar();
            case 60: return new StatusBar();
            case 61: return new StatusBarItem();
            case 62: return new ToolTip();
            case 63: return new Popup();
            case 64: return new GridViewColumnHeader();
            case 65: return new GridViewHeaderRowPresenter();
            case 66: return new GridViewRowPresenter();
            case 67: return new Image();
            case 68: return new Rectangle();
            case 69: return new Ellipse();
            case 70: return new Line();
            case 71: return new Path();
            case 72: return new Geometry();
            case 73: return new Polygon();
            case 74: return new Polyline();
            case 75: return new CustomDrawElement();
            case 76: return new RiveControl();
            case 77: return new RiveInput();
            case 78: return new NumericScrubber();
            case 79: return new Vector2Field();
            case 80: return new Vector3Field();
            case 81: return new ViewportSurface();
            case 82: return new DockPane();
            case 83: return new DockTabGroup();
            case 84: return new DockSplitNode();
            case 85: return new DockingWorkspace();
            case 86: return new CodeEditorView();
            case 87: return new PropertyGrid();
            case 88: return new NodeGraphCanvas();
            default: return null;
        }
    }

    private static Dictionary<string, int> _nonVisTagKindMap;

    private static void EnsureNonVisTagKindMap() {
        if (_nonVisTagKindMap != null) { return; }
        var m = new Dictionary<string, int>();
        m["ResourceDictionary"] = 1;
        m["Style"] = 2;
        m["ControlTemplate"] = 3;
        m["HierarchicalDataTemplate"] = 4;
        m["DataTemplate"] = 5;
        m["ItemsPanelTemplate"] = 6;
        m["Storyboard"] = 7;
        m["SolidColorBrush"] = 8;
        m["LinearGradientBrush"] = 9;
        m["RadialGradientBrush"] = 10;
        m["ImageBrush"] = 11;
        m["VisualBrush"] = 12;
        m["BitmapImage"] = 13;
        m["GradientStop"] = 14;
        m["GradientStopCollection"] = 15;
        m["Color"] = 16;
        m["Double"] = 17;
        m["sys:Double"] = 17;
        m["String"] = 18;
        m["sys:String"] = 18;
        m["FontFamily"] = 18;
        m["Boolean"] = 19;
        m["sys:Boolean"] = 19;
        m["Int32"] = 20;
        m["sys:Int32"] = 20;
        m["Thickness"] = 21;
        m["CornerRadius"] = 22;
        m["TranslateTransform"] = 23;
        m["ScaleTransform"] = 23;
        m["RotateTransform"] = 23;
        m["SkewTransform"] = 23;
        m["TransformGroup"] = 23;
        m["CompositeTransform3D"] = 24;
        m["PlaneProjection"] = 25;
        m["BlurEffect"] = 26;
        m["DropShadowEffect"] = 26;
        m["BackdropBlurEffect"] = 26;
        m["TintEffect"] = 26;
        m["PixelateEffect"] = 26;
        m["ChromaticAberrationEffect"] = 26;
        m["DirectionalBlurEffect"] = 26;
        m["SaturationEffect"] = 26;
        m["BoolToVisibilityConverter"] = 27;
        m["BooleanToVisibilityConverter"] = 27;
        m["InverseBoolConverter"] = 28;
        m["NullToVisibilityConverter"] = 29;
        m["MathConverter"] = 30;
        m["AlternationConverter"] = 31;
        m["GridView"] = 32;
        m["StaticResource"] = 33;
        _nonVisTagKindMap = m;
    }

    private static DependencyObject BuildNonVisualObjectNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        string tag = node.LocalName;
        if (tag == null || tag.Length == 0) { return null; }
        EnsureNonVisTagKindMap();
        if (!_nonVisTagKindMap.ContainsKey(tag)) { return null; }
        int kind = _nonVisTagKindMap[tag];
        if (kind == 1) { return BuildResourceDictionaryNode(node, ctx); }
        if (kind == 2) { return BuildStyleNode(node, ctx); }
        if (kind == 3) { return BuildControlTemplateNode(node, ctx); }
        if (kind == 4) { return BuildHierarchicalDataTemplateNode(node, ctx); }
        if (kind == 5) { return BuildDataTemplateNode(node, ctx); }
        if (kind == 6) { return BuildItemsPanelTemplateNode(node, ctx); }
        if (kind == 7) { return BuildStoryboardNode(node, ctx); }
        if (kind == 8) { return BuildSolidColorBrushNode(node, ctx); }
        if (kind == 9) { return BuildLinearGradientBrushNode(node, ctx); }
        if (kind == 10) { return BuildRadialGradientBrushNode(node, ctx); }
        if (kind == 11) { return BuildImageBrushNode(node, ctx); }
        if (kind == 12) { return BuildVisualBrushNode(node, ctx, contextHost); }
        if (kind == 13) {
            string uriSrc = node.GetAttr("UriSource");
            if (uriSrc == null) { uriSrc = node.Text.Trim(); }
            string resolved = (ctx != null && ctx.BaseUri.Length > 0) ? XamlStr.ResolveUri(ctx.BaseUri, uriSrc) : uriSrc;
            return DependencyObject.BoxString(resolved);
        }
        if (kind == 14) {
            string cAttr = node.GetAttr("Color");
            string col = "#FF000000";
            if (cAttr != null) {
                if (XamlStr.LooksLikeMarkupExtension(cAttr)) {
                    string ck = XamlStr.ExtractExtType(cAttr);
                    if (ck != "Binding" && ck != "TemplateBinding") {
                        col = DependencyObject.UnboxString(ResolveMarkupValue(cAttr, ctx, null), "#FF000000");
                    }
                } else if (cAttr.Trim().Length > 0) {
                    col = cAttr.Trim();
                }
            }
            string oAttr = node.GetAttr("Offset");
            float off = 0.0f;
            if (oAttr != null) {
                if (XamlStr.LooksLikeMarkupExtension(oAttr)) {
                    string ok = XamlStr.ExtractExtType(oAttr);
                    if (ok != "Binding" && ok != "TemplateBinding") {
                        off = DependencyObject.UnboxFloat(ResolveMarkupValue(oAttr, ctx, null), 0.0f);
                    }
                } else {
                    off = ParseUtil.ParseFloat(oAttr, 0.0f);
                }
            }
            return new GradientStop(col, off);
        }
        if (kind == 15) {
            var gsc = new ArrayCollectionBox();
            for (int i = 0; i < node.Children.Length; i += 1) {
                DependencyObject sub = BuildNonVisualObjectNode(node.Children[i], ctx, contextHost);
                if (sub != null) { gsc.AddBox(sub); }
            }
            return gsc;
        }
        if (kind == 16) {
            string c = node.Text.Trim();
            if (c.Length == 0) { c = "#00000000"; }
            return DependencyObject.BoxString(ParseUtil.FormatArgbHex(ParseUtil.ParseColor(c)));
        }
        if (kind == 17) {
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(node.Text.Trim(), 0.0f));
        }
        if (kind == 18) {
            return DependencyObject.BoxString(node.Text.Trim());
        }
        if (kind == 19) {
            return DependencyObject.BoxBool(ParseUtil.ParseBool(node.Text.Trim(), false));
        }
        if (kind == 20) {
            return DependencyObject.BoxInt(ParseUtil.ParseInt(node.Text.Trim(), 0));
        }
        if (kind == 21) {
            string ts = node.Text.Trim();
            if (ts.Length == 0) {
                string l = node.GetAttr("Left");
                if (l != null) {
                    return new Thickness(
                        ParseUtil.ParseFloat(l, 0.0f),
                        ParseUtil.ParseFloat(node.GetAttr("Top"), 0.0f),
                        ParseUtil.ParseFloat(node.GetAttr("Right"), 0.0f),
                        ParseUtil.ParseFloat(node.GetAttr("Bottom"), 0.0f));
                }
            }
            return Thickness.Parse(ts);
        }
        if (kind == 22) {
            return CornerRadius.Parse(node.Text.Trim());
        }
        if (kind == 23) {
            return BuildTransformNode(node, ctx);
        }
        if (kind == 24) {
            return BuildCompositeTransform3DNode(node);
        }
        if (kind == 25) {
            return BuildPlaneProjectionNode(node);
        }
        if (kind == 26) {
            return BuildEffectNode(node, ctx, contextHost);
        }
        if (kind == 27) {
            return new BoolToVisibilityConverter();
        }
        if (kind == 28) { return new InverseBoolConverter(); }
        if (kind == 29) { return new NullToVisibilityConverter(); }
        if (kind == 30) {
            var mc = new MathConverter();
            string expr = node.GetAttr("Expression");
            if (expr != null) { mc.Expression = expr; }
            return mc;
        }
        if (kind == 31) {
            var ac = new AlternationConverter();
            for (int i = 0; i < node.Children.Length; i += 1) {
                DependencyObject item = BuildObjectNode(node.Children[i], ctx, contextHost);
                if (item != null) { ac.AddValue(item); }
            }
            return ac;
        }
        if (kind == 32) {
            var gv = new GridView();
            string acr = node.GetAttr("AllowsColumnReorder");
            if (acr != null) { gv.AllowsColumnReorder = ParseUtil.ParseBool(acr, true); }
            string chsAttr = node.GetAttr("ColumnHeaderContainerStyle");
            if (chsAttr != null && XamlStr.LooksLikeMarkupExtension(chsAttr)) {
                DependencyObject stBox = ResolveMarkupValue(chsAttr, ctx, contextHost);
                if (stBox is Style) { gv.ColumnHeaderContainerStyle = stBox as Style; }
            }
            string chtAttr = node.GetAttr("ColumnHeaderTemplate");
            if (chtAttr != null && XamlStr.LooksLikeMarkupExtension(chtAttr)) {
                DependencyObject dtBox = ResolveMarkupValue(chtAttr, ctx, contextHost);
                if (dtBox is DataTemplate) { gv.ColumnHeaderTemplate = dtBox as DataTemplate; }
            }
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (ch.LocalName == "GridViewColumn") {
                    GridViewColumn col = BuildGridViewColumnNode(ch, ctx, contextHost);
                    if (col != null) { gv.AddColumn(col); }
                } else if (ch.LocalName == "GridView.Columns") {
                    for (int k = 0; k < ch.Children.Length; k += 1) {
                        if (ch.Children[k].LocalName == "GridViewColumn") {
                            GridViewColumn col = BuildGridViewColumnNode(ch.Children[k], ctx, contextHost);
                            if (col != null) { gv.AddColumn(col); }
                        }
                    }
                }
            }
            return gv;
        }
        if (kind == 33) {
            string rk = node.GetAttr("ResourceKey");
            if (rk == null) { rk = node.Text.Trim(); }
            return ctx != null ? ctx.FindResourceBox(NormalizeResourceKey(rk), contextHost) : null;
        }
        return null;
    }

    private static GridViewColumn BuildGridViewColumnNode(XamlXmlNode ch, XamlBuildContext ctx, UIElement contextHost) {
        if (ch == null) { return null; }
        var col = new GridViewColumn();
        string hdr = ch.GetAttr("Header");
        if (hdr != null) {
            if (XamlStr.LooksLikeMarkupExtension(hdr)) {
                col.Header = DependencyObject.UnboxString(ResolveMarkupValue(hdr, ctx, contextHost), hdr);
            } else {
                col.Header = hdr;
            }
        }
        string w = ch.GetAttr("Width");
        if (w != null) { col.Width = ParseUtil.ParseFloat(w, 120.0f); }
        string hcs = ch.GetAttr("HeaderContainerStyle");
        if (hcs != null && XamlStr.LooksLikeMarkupExtension(hcs)) {
            DependencyObject hcsBox = ResolveMarkupValue(hcs, ctx, contextHost);
            if (hcsBox is Style) { col.HeaderContainerStyle = hcsBox as Style; }
        }
        string htpl = ch.GetAttr("HeaderTemplate");
        if (htpl != null && XamlStr.LooksLikeMarkupExtension(htpl)) {
            DependencyObject htBox = ResolveMarkupValue(htpl, ctx, contextHost);
            if (htBox is DataTemplate) { col.HeaderTemplate = htBox as DataTemplate; }
        }
        string ctpl = ch.GetAttr("CellTemplate");
        if (ctpl != null && XamlStr.LooksLikeMarkupExtension(ctpl)) {
            DependencyObject ctBox = ResolveMarkupValue(ctpl, ctx, contextHost);
            if (ctBox is DataTemplate) { col.CellTemplate = ctBox as DataTemplate; }
        }
        string dmb = ch.GetAttr("DisplayMemberBinding");
        if (dmb != null && XamlStr.LooksLikeMarkupExtension(dmb)) {
            col.DisplayMemberBinding = ToBindingObject(ParseBindingSpec(dmb, ctx));
        }
        for (int ci = 0; ci < ch.Children.Length; ci += 1) {
            XamlXmlNode subCh = ch.Children[ci];
            if (subCh.LocalName == "GridViewColumn.CellTemplate" && subCh.Children.Length > 0) {
                DependencyObject ctObj = BuildObjectNode(subCh.Children[0], ctx, contextHost);
                if (ctObj is DataTemplate) { col.CellTemplate = ctObj as DataTemplate; }
            } else if (subCh.LocalName == "GridViewColumn.HeaderContainerStyle" && subCh.Children.Length > 0) {
                DependencyObject stObj = BuildObjectNode(subCh.Children[0], ctx, contextHost);
                if (stObj is Style) { col.HeaderContainerStyle = stObj as Style; }
            } else if (subCh.LocalName == "GridViewColumn.HeaderTemplate" && subCh.Children.Length > 0) {
                DependencyObject htObj = BuildObjectNode(subCh.Children[0], ctx, contextHost);
                if (htObj is DataTemplate) { col.HeaderTemplate = htObj as DataTemplate; }
            } else if (subCh.LocalName == "GridViewColumn.Header") {
                if (subCh.Text.Trim().Length > 0) { col.Header = subCh.Text.Trim(); }
            }
        }
        return col;
    }

    private static DependencyObject BuildObjectNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        if (node == null) { return null; }
        DependencyObject nonVis = BuildNonVisualObjectNode(node, ctx, contextHost);
        if (nonVis != null) { return nonVis; }

        UIElement el = CreateUIElementByTag(node.LocalName);
        if (el == null) { return null; }

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Resources")) {
                ctx.PushScope(el.Resources);
                PopulateResourceDictionaryFromChildren(ch, el.Resources, ctx);
            }
        }

        for (int i = 0; i < node.Attrs.Length; i += 1) {
            XamlXmlAttr a = node.Attrs[i];
            if (IsIgnoredAttributeName(a.Name, node.LocalName)) { continue; }
            if (a.Name == "x:Name" || a.Name == "Name") {
                el.Name = a.Value;
                if (el is UserControl || el is Page) {
                    el._internalRootName = a.Value;
                }
                continue;
            }
            ApplyXmlAttr(el, a, ctx);
        }

        var textBlockInlines = new Inline[0];
        var textBlockSpaced = new bool[0];
        bool isTb = el is TextBlock;
        bool hasRealInlineTag = false;
        bool pendingInlineWs = false;
        var deferredTriggerNodes = new XamlXmlNode[0];

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Resources")) { continue; }
            if (XamlStr.EndsWith(ch.LocalName, ".Triggers") && !ch.RawName.Contains("Interaction.")) {
                deferredTriggerNodes.Push(ch);
                continue;
            }
            if (isTb && ch.LocalName == "#ws") {
                if (textBlockInlines.Length > 0) { pendingInlineWs = true; }
                continue;
            }
            if (isTb && ch.LocalName == "#text") {
                if (ch.Text.Length > 0) {
                    textBlockInlines.Push(new Run(ch.Text));
                    textBlockSpaced.Push(textBlockInlines.Length > 1 && pendingInlineWs);
                }
                pendingInlineWs = false;
                continue;
            }
            if (ch.RawName.Contains(".")) {
                ApplyPropertyElement(el, ch, ctx);
                continue;
            }
            if (isTb && (ch.LocalName == "Run" || ch.LocalName == "LineBreak" ||
                ch.LocalName == "Bold" || ch.LocalName == "Italic" ||
                ch.LocalName == "Underline" || ch.LocalName == "Span" ||
                ch.LocalName == "Hyperlink")) {
                hasRealInlineTag = true;
                pendingInlineWs = AppendInlineNode(
                    ch, textBlockInlines, textBlockSpaced, pendingInlineWs,
                    null, null, null, el as TextBlock, ctx);
                continue;
            }
            if (ch.LocalName == "#text" && el is ItemsControl) {
                if (ch.Text.Trim().Length > 0) {
                    (el as ItemsControl).AddItem(ch.Text.Trim());
                }
                continue;
            }
            if (ch.LocalName == "#text" || ch.LocalName == "#ws") { continue; }
            DependencyObject childObj = BuildObjectNode(ch, ctx, el);
            AttachChildObject(el, childObj);
        }

        if (isTb) {
            TextBlock tb = el as TextBlock;
            if (hasRealInlineTag && textBlockInlines.Length > 0) {
                for (int i = 0; i < textBlockInlines.Length; i += 1) {
                    Inline inl = textBlockInlines[i];
                    if (inl is Run) {
                        if (i < textBlockSpaced.Length && textBlockSpaced[i]) {
                            tb.AddRunSpaced(inl as Run);
                        } else {
                            tb.AddRun(inl as Run);
                        }
                    } else {
                        tb.AddInline(inl);
                    }
                }
            } else if (node.Text.Length > 0 && tb.Text.Length == 0 && node.GetAttr("Text") == null && node.GetAttr("noesis:RichText.Text") == null) {
                tb.Text = node.Text.Trim();
            }
        } else if (node.Text.Length > 0) {
            if (el is Path && (el as Path).Data.Length == 0 && node.GetAttr("Data") == null) {
                (el as Path).Data = node.Text.Trim();
            } else if (el is ContentControl && (el as ContentControl).ContentBox == null && node.GetAttr("Content") == null) {
                (el as ContentControl).SetContentBox(DependencyObject.BoxString(node.Text.Trim()));
            } else if (el is TextBox && (el as TextBox).Text.Length == 0 && node.GetAttr("Text") == null) {
                (el as TextBox).Text = node.Text.Trim();
            }
        }

        for (int ti = 0; ti < deferredTriggerNodes.Length; ti += 1) {
            XamlXmlNode trigProp = deferredTriggerNodes[ti];
            for (int k = 0; k < trigProp.Children.Length; k += 1) {
                TriggerBase etrig = BuildTriggerNode(trigProp.Children[k], ctx);
                if (etrig != null) {
                    etrig.Attach(el);
                }
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            if (XamlStr.EndsWith(node.Children[i].LocalName, ".Resources")) {
                ctx.PopScope(el.Resources);
            }
        }
        return el;
    }

    private static void AttachChildObject(UIElement parent, DependencyObject childObj) {
        if (parent == null || childObj == null) { return; }
        if (parent is RiveControl && childObj is RiveInput) {
            (parent as RiveControl).AddInput(childObj as RiveInput);
            return;
        }
        UIElement childEl = childObj as UIElement;
        if (parent is ItemsControl) {
            (parent as ItemsControl).AddItemBox(childObj);
            return;
        }
        if (parent is Decorator && childEl != null) {
            (parent as Decorator).Child = childEl;
            return;
        }
        if (parent is Popup && childEl != null) {
            (parent as Popup).Child = childEl;
            return;
        }
        if (parent is ContentControl) {
            (parent as ContentControl).SetContentBox(childObj);
            return;
        }
        if (parent is ContentPresenter) {
            (parent as ContentPresenter).SetContentBox(childObj);
            return;
        }
        if (childEl != null) {
            parent.AddChild(childEl);
        }
    }

    private static void CopyInlineStyle(Inline src, Inline dst) {
        if (src == null || dst == null) { return; }
        if (src.Foreground != null && dst.Foreground == null) { dst.Foreground = src.Foreground; }
        if (src.FontFamily.Length > 0 && dst.FontFamily.Length == 0) { dst.FontFamily = src.FontFamily; }
        if (src.FontWeight.Length > 0 && dst.FontWeight.Length == 0) { dst.FontWeight = src.FontWeight; }
        if (src.FontStyle.Length > 0 && dst.FontStyle.Length == 0) { dst.FontStyle = src.FontStyle; }
        if (src.FontSize > 0.0f && dst.FontSize <= 0.0f) { dst.FontSize = src.FontSize; }
        if (src.Decorations != 0 && dst.Decorations == 0) { dst.Decorations = src.Decorations; }
    }

    private static void CopyInlineBindings(string[] props, XamlParsedBinding[] specs, Inline dst) {
        if (props == null || specs == null || dst == null) { return; }
        for (int i = 0; i < props.Length; i += 1) {
            XamlParsedBinding pb = specs[i];
            if (pb == null) { continue; }
            BindingEngine.BindInlineEx(
                dst, props[i], pb.Path, pb.ElementName, pb.Mode,
                pb.Converter, pb.ConverterKey, pb.ConverterParameter, pb.StringFormat,
                pb.HasFallback, pb.FallbackBox, pb.HasTargetNull, pb.TargetNullBox,
                pb.RelativeSourceMode, pb.AncestorType);
        }
    }

    private static bool AppendInlineNode(
        XamlXmlNode node, Inline[] list, bool[] spacedList, bool leadingSpace,
        Inline inheritedStyle, string[] inhBindProps, XamlParsedBinding[] inhBindSpecs,
        TextBlock ownerTb, XamlBuildContext ctx) {
        string tag = node.LocalName;
        if (tag == "LineBreak") {
            list.Push(new LineBreak());
            spacedList.Push(false);
            return false;
        }
        var styleHolder = new Run("");
        if (inheritedStyle != null) { CopyInlineStyle(inheritedStyle, styleHolder); }
        if (tag == "Bold") { styleHolder.FontWeight = "Bold"; }
        else if (tag == "Italic") { styleHolder.FontStyle = "Italic"; }
        else if (tag == "Underline") { styleHolder.Decorations = 1; }
        else if (tag == "Hyperlink") {
            DependencyObject hlBox = ResolveMarkupValue("{DynamicResource Brush.Hyperlink.Normal}", ctx, ownerTb);
            Brush hlBrush = UIElement.CoerceBrush(hlBox);
            styleHolder.Foreground = hlBrush != null ? hlBrush : new SolidColorBrush("#FF1098DF");
        }

        var localBindProps = new string[0];
        var localBindSpecs = new XamlParsedBinding[0];
        if (inhBindProps != null && inhBindSpecs != null) {
            for (int b = 0; b < inhBindProps.Length; b += 1) {
                localBindProps.Push(inhBindProps[b]);
                localBindSpecs.Push(inhBindSpecs[b]);
            }
        }

        bool hasExplicitTextAttr = false;
        for (int i = 0; i < node.Attrs.Length; i += 1) {
            XamlXmlAttr a = node.Attrs[i];
            if (IsIgnoredAttributeName(a.Name, node.LocalName)) { continue; }
            if (a.Name == "Text" || a.Name == "Foreground" || a.Name == "FontSize" ||
                a.Name == "FontFamily" || a.Name == "FontWeight" || a.Name == "FontStyle" ||
                a.Name == "TextDecorations") {
                if (a.Name == "Text") { hasExplicitTextAttr = true; }
                if (XamlStr.LooksLikeMarkupExtension(a.Value)) {
                    string kind = XamlStr.ExtractExtType(a.Value);
                    if (kind == "Binding") {
                        XamlParsedBinding pb = ParseBindingSpec(a.Value, ctx);
                        localBindProps.Push(a.Name);
                        localBindSpecs.Push(pb);
                    } else {
                        DependencyObject box = ResolveMarkupValue(a.Value, ctx, ownerTb);
                        styleHolder.SetPropertyValue(a.Name, box);
                    }
                } else {
                    if (a.Name == "Foreground") { styleHolder.Foreground = new SolidColorBrush(a.Value); }
                    else { styleHolder.SetPropertyValue(a.Name, DependencyObject.BoxString(a.Value)); }
                }
            }
        }

        if (node.Children.Length > 0 && tag != "Run") {
            bool pendingWs = leadingSpace;
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (ch.LocalName == "#ws") {
                    if (list.Length > 0) { pendingWs = true; }
                    continue;
                }
                if (ch.LocalName == "#text") {
                    if (ch.Text.Length > 0) {
                        var subRun = new Run(ch.Text);
                        CopyInlineStyle(styleHolder, subRun);
                        CopyInlineBindings(localBindProps, localBindSpecs, subRun);
                        list.Push(subRun);
                        spacedList.Push(list.Length > 1 && pendingWs);
                    }
                    pendingWs = false;
                    continue;
                }
                pendingWs = AppendInlineNode(
                    ch, list, spacedList, pendingWs,
                    styleHolder, localBindProps, localBindSpecs, ownerTb, ctx);
            }
            return pendingWs;
        }

        if (!hasExplicitTextAttr) {
            styleHolder.Text = node.Text;
        }
        CopyInlineBindings(localBindProps, localBindSpecs, styleHolder);
        list.Push(styleHolder);
        spacedList.Push(list.Length > 1 && leadingSpace);
        return false;
    }

    private static void BindPlasmaAttrIfExt(UIElement el, string targetProp, string attrVal, XamlBuildContext ctx) {
        if (el == null || attrVal == null || !XamlStr.LooksLikeMarkupExtension(attrVal)) { return; }
        string kind = XamlStr.ExtractExtType(attrVal);
        if (kind == "Binding" || kind == "TemplateBinding") {
            BindElementFromSpec(el, targetProp, ParseBindingSpec(attrVal, ctx), ctx);
        }
    }

    private static XamlParsedBinding ParseBindingSpecFromElementNode(XamlXmlNode bNode, XamlBuildContext ctx) {
        var pb = new XamlParsedBinding();
        string pathAttr = bNode.GetAttr("Path");
        if (pathAttr != null) { pb.Path = XamlStr.StripOuterParens(pathAttr); }
        string elName = bNode.GetAttr("ElementName");
        if (elName != null) { pb.ElementName = elName; }
        string modeAttr = bNode.GetAttr("Mode");
        if (modeAttr == "TwoWay") { pb.Mode = 1; }
        else if (modeAttr == "OneTime") { pb.Mode = 2; }
        else if (modeAttr == "OneWayToSource") { pb.Mode = 3; }
        string convAttr = bNode.GetAttr("Converter");
        if (convAttr != null) {
            if (XamlStr.LooksLikeMarkupExtension(convAttr)) {
                DependencyObject cBox = ResolveMarkupValue(convAttr, ctx, null);
                if (cBox is IValueConverter) { pb.Converter = cBox as IValueConverter; }
                else if (cBox != null && DependencyObject.Unbox(cBox) is IValueConverter) {
                    pb.Converter = DependencyObject.Unbox(cBox) as IValueConverter;
                }
                if (pb.Converter == null && XamlStr.StartsWith(convAttr.Trim(), "{StaticResource")) {
                    pb.ConverterKey = XamlStr.ExtractExtBody(convAttr).Trim();
                }
            } else {
                pb.ConverterKey = convAttr;
            }
        }
        string cpAttr = bNode.GetAttr("ConverterParameter");
        if (cpAttr != null) { pb.ConverterParameter = cpAttr; }
        string sfAttr = bNode.GetAttr("StringFormat");
        if (sfAttr != null) {
            if (XamlStr.StartsWith(sfAttr, "{}")) { sfAttr = sfAttr.Substring(2); }
            pb.StringFormat = sfAttr;
        }
        string rsAttr = bNode.GetAttr("RelativeSource");
        if (rsAttr != null) { ParseRelativeSourceClause(rsAttr, pb); }
        return pb;
    }

    private static Binding ParseBindingElementNode(XamlXmlNode bNode, XamlBuildContext ctx) {
        return ToBindingObject(ParseBindingSpecFromElementNode(bNode, ctx));
    }

    private static MultiBinding BuildMultiBindingNode(XamlXmlNode mbNode, XamlBuildContext ctx) {
        var mb = new MultiBinding();
        string sf = mbNode.GetAttr("StringFormat");
        if (sf != null) {
            if (XamlStr.StartsWith(sf, "{}")) { sf = sf.Substring(2); }
            mb.StringFormat = sf;
        }
        string cp = mbNode.GetAttr("ConverterParameter");
        if (cp != null) { mb.ConverterParameter = cp; }
        string convAttr = mbNode.GetAttr("Converter");
        if (convAttr != null && XamlStr.LooksLikeMarkupExtension(convAttr)) {
            DependencyObject cBox = ResolveMarkupValue(convAttr, ctx, null);
            if (cBox is IMultiValueConverter) { mb.Converter = cBox as IMultiValueConverter; }
            else if (cBox != null && DependencyObject.Unbox(cBox) is IMultiValueConverter) {
                mb.Converter = DependencyObject.Unbox(cBox) as IMultiValueConverter;
            }
        }
        for (int i = 0; i < mbNode.Children.Length; i += 1) {
            XamlXmlNode ch = mbNode.Children[i];
            if (ch.LocalName == "MultiBinding.Converter" && ch.Children.Length > 0) {
                DependencyObject cObj = BuildObjectNode(ch.Children[0], ctx, null);
                if (cObj is IMultiValueConverter) { mb.Converter = cObj as IMultiValueConverter; }
                else if (cObj != null && DependencyObject.Unbox(cObj) is IMultiValueConverter) {
                    mb.Converter = DependencyObject.Unbox(cObj) as IMultiValueConverter;
                }
            } else if (ch.LocalName == "Binding") {
                mb.AddBinding(ParseBindingElementNode(ch, ctx));
            }
        }
        return mb;
    }

    private static void ApplyPropertyElement(UIElement el, XamlXmlNode propNode, XamlBuildContext ctx) {
        string raw = propNode.RawName;
        string local = propNode.LocalName;
        int dot = XamlStr.LastIndexOfChar(local, ".");
        string propName = dot >= 0 ? local.Substring(dot + 1) : local;
        string cleanRawProp = XamlStr.StripOuterParens(raw);
        string effectiveProp = (XamlStr.StartsWith(cleanRawProp, "Custom.") ||
                                XamlStr.StartsWith(cleanRawProp, "DragDrop.") ||
                                XamlStr.StartsWith(cleanRawProp, "ToolTipService."))
            ? cleanRawProp
            : propName;

        if (raw == "b:Interaction.Triggers" || raw == "Interaction.Triggers" || (propName == "Triggers" && raw.Contains("Interaction."))) {
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                BehaviorTriggerBase trig = BuildBehaviorTriggerNode(propNode.Children[i], ctx, el);
                if (trig != null) { Interaction.AddTrigger(el, trig); }
            }
            return;
        }
        if (raw == "b:Interaction.Behaviors" || raw == "Interaction.Behaviors" || (propName == "Behaviors" && raw.Contains("Interaction."))) {
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                BehaviorBase beh = BuildBehaviorNode(propNode.Children[i], ctx, el);
                if (beh != null) { Interaction.AddBehavior(el, beh); }
            }
            return;
        }
        if (propName == "VisualStateGroups" || local == "VisualStateManager.VisualStateGroups") {
            var groups = BuildVisualStateGroupsList(propNode, ctx);
            if (groups.Length > 0) {
                VisualStateManager.SetVisualStateGroups(el, groups);
            }
            return;
        }
        if (propName == "Triggers") {
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                TriggerBase tr = BuildTriggerNode(propNode.Children[i], ctx);
                if (tr != null) { tr.Attach(el); }
            }
            return;
        }
        if (propName == "RowDefinitions" && el is Grid) {
            Grid g = el as Grid;
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                XamlXmlNode rd = propNode.Children[i];
                if (rd.LocalName == "RowDefinition") {
                    GridLength gl = ResolveGridLength(rd.GetAttr("Height"), ctx, el);
                    var rDef = new RowDefinition(gl);
                    string rName = rd.GetAttr("x:Name");
                    if (rName == null) { rName = rd.GetAttr("Name"); }
                    if (rName != null) { rDef.Name = rName; }
                    g.RowDefinitions.Add(rDef);
                }
            }
            return;
        }
        if (propName == "ColumnDefinitions" && el is Grid) {
            Grid g = el as Grid;
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                XamlXmlNode cd = propNode.Children[i];
                if (cd.LocalName == "ColumnDefinition") {
                    GridLength gl = ResolveGridLength(cd.GetAttr("Width"), ctx, el);
                    var cDef = new ColumnDefinition(gl);
                    string cName = cd.GetAttr("x:Name");
                    if (cName == null) { cName = cd.GetAttr("Name"); }
                    if (cName != null) { cDef.Name = cName; }
                    g.ColumnDefinitions.Add(cDef);
                }
            }
            return;
        }
        if (propName == "InputBindings") {
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                XamlXmlNode kbNode = propNode.Children[i];
                if (kbNode.LocalName == "KeyBinding") {
                    KeyCode kc = EnumConvert.ParseKeyCode(kbNode.GetAttr("Key"));
                    int mods = KeyModifiers.Parse(kbNode.GetAttr("Modifiers"));
                    var kb = new KeyBinding(null, kc, mods);
                    string cmdAttr = kbNode.GetAttr("Command");
                    if (cmdAttr != null) {
                        if (XamlStr.LooksLikeMarkupExtension(cmdAttr)) {
                            string kind = XamlStr.ExtractExtType(cmdAttr);
                            if (kind == "Binding") {
                                kb.CommandPath = ParseBindingSpec(cmdAttr, ctx).Path;
                            } else {
                                DependencyObject cb = ResolveMarkupValue(cmdAttr, ctx, el);
                                kb.Command = DependencyObject.Unbox(cb) as ICommand;
                            }
                        } else {
                            kb.CommandPath = cmdAttr;
                        }
                    }
                    el.AddInputBinding(kb);
                }
            }
            return;
        }
        if (propName == "CommandBindings") {
            return;
        }
        if (propName == "DecreaseRepeatButton" && el is Track && propNode.Children.Length > 0) {
            UIElement ch = BuildObjectNode(propNode.Children[0], ctx, el) as UIElement;
            if (ch != null) { (el as Track).DecreaseRepeatButton = ch; }
            return;
        }
        if (propName == "IncreaseRepeatButton" && el is Track && propNode.Children.Length > 0) {
            UIElement ch = BuildObjectNode(propNode.Children[0], ctx, el) as UIElement;
            if (ch != null) { (el as Track).IncreaseRepeatButton = ch; }
            return;
        }
        if (propName == "Thumb" && el is Track && propNode.Children.Length > 0) {
            UIElement ch = BuildObjectNode(propNode.Children[0], ctx, el) as UIElement;
            if (ch is Thumb) { (el as Track).Thumb = ch as Thumb; }
            return;
        }
        if (propName == "Header") {
            DependencyObject hdrBox = null;
            if (propNode.Children.Length == 1) {
                hdrBox = BuildObjectNode(propNode.Children[0], ctx, el);
            } else if (propNode.Children.Length > 1) {
                var sp = new StackPanel(Orientation.Vertical, 4.0f);
                for (int i = 0; i < propNode.Children.Length; i += 1) {
                    UIElement c = BuildObjectNode(propNode.Children[i], ctx, el) as UIElement;
                    if (c != null) { sp.AddChild(c); }
                }
                hdrBox = sp;
            } else if (propNode.Text.Length > 0) {
                hdrBox = DependencyObject.BoxString(propNode.Text.Trim());
            }
            if (el is HeaderedContentControl) { (el as HeaderedContentControl).SetHeaderBox(hdrBox); }
            else if (el is HeaderedItemsControl) { (el as HeaderedItemsControl).SetHeaderBox(hdrBox); }
            return;
        }
        if (propName == "Icon" && el is MenuItem && propNode.Children.Length > 0) {
            DependencyObject ico = BuildObjectNode(propNode.Children[0], ctx, el);
            (el as MenuItem).Icon = DependencyObject.Unbox(ico);
            return;
        }
        if (propName == "ToolTip" && propNode.Children.Length > 0) {
            ToolTipService.EnsureInstalled();
            DependencyObject tt = BuildObjectNode(propNode.Children[0], ctx, el);
            el.ToolTipBox = tt;
            return;
        }
        if (propName == "ContextMenu" && propNode.Children.Length > 0) {
            DependencyObject cm = BuildObjectNode(propNode.Children[0], ctx, el);
            if (cm is ContextMenu) {
                el.ContextMenu = cm as ContextMenu;
            }
            return;
        }
        if (propName == "Data" && el is Path) {
            if (propNode.Text.Length > 0) {
                (el as Path).Data = propNode.Text.Trim();
            } else if (propNode.Children.Length > 0) {
                string dAttr = propNode.Children[0].GetAttr("Figures");
                if (dAttr != null && dAttr.Length > 0) { (el as Path).Data = dAttr; }
            }
            return;
        }
        if (propName == "Inputs" && el is RiveControl) {
            for (int i = 0; i < propNode.Children.Length; i += 1) {
                DependencyObject ch = BuildObjectNode(propNode.Children[i], ctx, el);
                if (ch is RiveInput) { (el as RiveControl).AddInput(ch as RiveInput); }
            }
            return;
        }
        el.MarkLocalProperty(NormalizeSetterProp(effectiveProp));
        el.ClearResourceReference(effectiveProp);
        if (propNode.Children.Length == 1) {
            XamlXmlNode childXml = propNode.Children[0];
            if (childXml.LocalName == "MultiBinding") {
                BindingEngine.BindMultiBinding(el, effectiveProp, BuildMultiBindingNode(childXml, ctx));
                return;
            }
            if (childXml.LocalName == "Binding") {
                BindElementFromSpec(el, effectiveProp, ParseBindingSpecFromElementNode(childXml, ctx), ctx);
                return;
            }
            DependencyObject valBox = BuildObjectNode(childXml, ctx, el);
            ApplyResolvedPropertyBox(el, effectiveProp, valBox, ctx);
            if (childXml.LocalName == "CompositeTransform3D") {
                BindPlasmaAttrIfExt(el, "Transform3D.RotationX", childXml.GetAttr("RotationX"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.RotationY", childXml.GetAttr("RotationY"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.RotationZ", childXml.GetAttr("RotationZ"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.TranslateX", childXml.GetAttr("TranslateX"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.TranslateY", childXml.GetAttr("TranslateY"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.TranslateZ", childXml.GetAttr("TranslateZ"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.ScaleX", childXml.GetAttr("ScaleX"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.ScaleY", childXml.GetAttr("ScaleY"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.ScaleZ", childXml.GetAttr("ScaleZ"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.CenterX", childXml.GetAttr("CenterX"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.CenterY", childXml.GetAttr("CenterY"), ctx);
                BindPlasmaAttrIfExt(el, "Transform3D.CenterZ", childXml.GetAttr("CenterZ"), ctx);
            } else if ((childXml.LocalName == "LinearGradientBrush" || childXml.LocalName == "RadialGradientBrush") &&
                       (propName == "Fill" || propName == "Stroke" || propName == "Background")) {
                BindGradientStopsIfExt(el, propName, childXml, ctx);
            } else if (childXml.LocalName == "SolidColorBrush" && IsBrushPropertyName(effectiveProp)) {
                string colAttr = childXml.GetAttr("Color");
                if (colAttr != null && XamlStr.LooksLikeMarkupExtension(colAttr)) {
                    string cKind = XamlStr.ExtractExtType(colAttr);
                    if (cKind == "Binding" || cKind == "TemplateBinding") {
                        BindElementFromSpec(el, propName + ".Color", ParseBindingSpec(colAttr, ctx), ctx);
                    }
                }
            } else if (childXml.LocalName == "ImageBrush" && (propName == "Fill" || propName == "Background")) {
                string srcAttr = childXml.GetAttr("ImageSource");
                if (srcAttr != null && XamlStr.LooksLikeMarkupExtension(srcAttr)) {
                    string sKind = XamlStr.ExtractExtType(srcAttr);
                    if (sKind == "Binding" || sKind == "TemplateBinding") {
                        BindElementFromSpec(el, propName + ".ImageSource", ParseBindingSpec(srcAttr, ctx), ctx);
                    }
                }
                for (int si = 0; si < childXml.Children.Length; si += 1) {
                    XamlXmlNode shProp = childXml.Children[si];
                    if (XamlStr.EndsWith(shProp.LocalName, ".Shader") && shProp.Children.Length > 0 &&
                        shProp.Children[0].LocalName == "PlasmaBrush") {
                        XamlXmlNode sh = shProp.Children[0];
                        string shName = sh.GetAttr("x:Name");
                        if (shName == null || shName.Length == 0) { shName = sh.GetAttr("Name"); }
                        if (shName != null && shName.Length > 0 && el._internalRootName.Length == 0) {
                            el._internalRootName = shName.Trim();
                            if (el.Name.Length == 0) { el.Name = shName.Trim(); }
                        }
                        BindPlasmaAttrIfExt(el, propName + ".PlasmaSeed", sh.GetAttr("Seed"), ctx);
                        BindPlasmaAttrIfExt(el, propName + ".PlasmaColor", sh.GetAttr("Color"), ctx);
                        BindPlasmaAttrIfExt(el, propName + ".PlasmaTime", sh.GetAttr("Time"), ctx);
                        BindPlasmaAttrIfExt(el, propName + ".PlasmaScaleX", sh.GetAttr("ScaleX"), ctx);
                        BindPlasmaAttrIfExt(el, propName + ".PlasmaScaleY", sh.GetAttr("ScaleY"), ctx);
                    }
                }
            }
            return;
        }
        if (propNode.Text.Length > 0) {
            ApplyPropertyValueOrExt(el, effectiveProp, propNode.Text.Trim(), ctx);
        }
    }

    private static GridLength ResolveGridLength(string raw, XamlBuildContext ctx, UIElement scope) {
        if (raw == null || raw.Trim().Length == 0) { return GridLength.FromStar(1.0f); }
        string t = raw.Trim();
        if (XamlStr.LooksLikeMarkupExtension(t)) {
            DependencyObject box = ResolveMarkupValue(t, ctx, scope);
            if (box is GridLength) { return box as GridLength; }
            if (box is BoxedFloat) { return GridLength.FromPixel((box as BoxedFloat).Value); }
            t = DependencyObject.UnboxString(box, "1*");
        }
        return GridLength.Parse(t);
    }

    // ---- ResourceDictionaries & MergedDictionaries ----
    private static ResourceDictionary BuildResourceDictionaryNode(XamlXmlNode root, XamlBuildContext ctx) {
        var rd = new ResourceDictionary();
        ctx.PushScope(rd);
        PopulateResourceDictionaryFromChildren(root, rd, ctx);
        ctx.PopScope(rd);
        return rd;
    }

    private static void PopulateResourceDictionaryFromChildren(
        XamlXmlNode containerNode, ResourceDictionary rd, XamlBuildContext ctx) {
        if (containerNode == null || rd == null) { return; }
        for (int i = 0; i < containerNode.Children.Length; i += 1) {
            XamlXmlNode ch = containerNode.Children[i];
            if (ch.LocalName == "ResourceDictionary.MergedDictionaries") {
                for (int m = 0; m < ch.Children.Length; m += 1) {
                    XamlXmlNode mNode = ch.Children[m];
                    if (mNode.LocalName == "ResourceDictionary") {
                        string srcAttr = mNode.GetAttr("Source");
                        if (srcAttr != null && srcAttr.Trim().Length > 0) {
                            string resolvedUri = XamlStr.ResolveUri(ctx.BaseUri, srcAttr);
                            ResourceDictionary cachedSub = ctx.Scopes.Length <= 2 ? TryGetCachedExtDict(resolvedUri) : null;
                            if (cachedSub == null && ctx.Scopes.Length <= 2) {
                                cachedSub = TryGetCachedExtDict(srcAttr.Trim());
                            }
                            if (cachedSub != null) {
                                rd.Merge(cachedSub);
                            } else {
                                string text = Host_ReadTextAsset(resolvedUri);
                                if (text == null || text.Trim().Length == 0) {
                                    resolvedUri = srcAttr.Trim();
                                    text = Host_ReadTextAsset(resolvedUri);
                                }
                                if (text != null && text.Trim().Length > 0) {
                                    XamlXmlNode subRoot = XamlDomParser.ParseDocument(text);
                                    if (subRoot != null) {
                                        var subCtx = new XamlBuildContext(ctx.FallbackResources, resolvedUri);
                                        for (int s = 0; s < ctx.Scopes.Length; s += 1) {
                                            subCtx.PushScope(ctx.Scopes[s]);
                                        }
                                        ResourceDictionary subDict = BuildResourceDictionaryNode(subRoot, subCtx);
                                        if (ctx.Scopes.Length <= 2 && subDict != null) {
                                            StoreCachedExtDict(resolvedUri, subDict);
                                        }
                                        rd.Merge(subDict);
                                    }
                                }
                            }
                        } else {
                            ResourceDictionary inlineDict = BuildResourceDictionaryNode(mNode, ctx);
                            rd.Merge(inlineDict);
                        }
                    }
                }
                continue;
            }
            if (ch.LocalName == "ResourceDictionary") {
                ResourceDictionary subInline = BuildResourceDictionaryNode(ch, ctx);
                rd.Merge(subInline);
                continue;
            }
            string key = NormalizeResourceKey(ch.GetKeyAttr());
            DependencyObject val = BuildObjectNode(ch, ctx, null);
            if (key.Length == 0) {
                if (val is Style) {
                    key = (val as Style).TargetType;
                } else if (val is DataTemplate) {
                    key = (val as DataTemplate).DataType;
                }
            }
            if (key.Length > 0 && val != null) {
                rd.SetBox(key, val);
            }
        }
    }

    // ---- Brushes & Transforms & Effects ----
    private static SolidColorBrush BuildSolidColorBrushNode(XamlXmlNode node, XamlBuildContext ctx) {
        string colAttr = node.GetAttr("Color");
        string col = "#00000000";
        if (colAttr != null) {
            if (XamlStr.LooksLikeMarkupExtension(colAttr)) {
                string cKind = XamlStr.ExtractExtType(colAttr);
                if (cKind != "Binding" && cKind != "TemplateBinding") {
                    DependencyObject cBox = ResolveMarkupValue(colAttr, ctx, null);
                    col = DependencyObject.UnboxString(cBox, "#00000000");
                }
            } else {
                col = colAttr.Trim();
            }
        } else if (node.Text != null && node.Text.Trim().Length > 0) {
            col = node.Text.Trim();
        } else {
            col = "#FF000000";
        }
        var br = new SolidColorBrush(col);
        string opAttr = node.GetAttr("Opacity");
        if (opAttr != null) {
            if (XamlStr.LooksLikeMarkupExtension(opAttr)) {
                br.Opacity = DependencyObject.UnboxFloat(ResolveMarkupValue(opAttr, ctx, null), 1.0f);
            } else {
                br.Opacity = ParseUtil.ParseFloat(opAttr, 1.0f);
            }
        }
        return br;
    }

    private static void BindGradientStopsRecursive(UIElement el, string propPrefix, XamlXmlNode containerNode, int[] slotCounter, XamlBuildContext ctx) {
        for (int i = 0; i < containerNode.Children.Length; i += 1) {
            XamlXmlNode ch = containerNode.Children[i];
            if (ch.LocalName == "GradientStop") {
                int idx = slotCounter[0];
                slotCounter[0] = idx + 1;
                string cAttr = ch.GetAttr("Color");
                if (cAttr != null && XamlStr.LooksLikeMarkupExtension(cAttr)) {
                    string ck = XamlStr.ExtractExtType(cAttr);
                    if (ck == "Binding" || ck == "TemplateBinding") {
                        BindElementFromSpec(el, propPrefix + ".Stop" + idx + "Color", ParseBindingSpec(cAttr, ctx), ctx);
                    }
                }
                string oAttr = ch.GetAttr("Offset");
                if (oAttr != null && XamlStr.LooksLikeMarkupExtension(oAttr)) {
                    string ok = XamlStr.ExtractExtType(oAttr);
                    if (ok == "Binding" || ok == "TemplateBinding") {
                        BindElementFromSpec(el, propPrefix + ".Stop" + idx + "Offset", ParseBindingSpec(oAttr, ctx), ctx);
                    }
                }
            } else if (ch.LocalName == "GradientStopCollection" || XamlStr.EndsWith(ch.LocalName, ".GradientStops")) {
                BindGradientStopsRecursive(el, propPrefix, ch, slotCounter, ctx);
            }
        }
    }

    private static void BindGradientStopsIfExt(UIElement el, string propPrefix, XamlXmlNode brushNode, XamlBuildContext ctx) {
        if (el == null || brushNode == null) { return; }
        var counter = new int[1];
        counter[0] = 0;
        BindGradientStopsRecursive(el, propPrefix, brushNode, counter, ctx);
        for (int i = 0; i < brushNode.Children.Length; i += 1) {
            XamlXmlNode ch = brushNode.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".RelativeTransform") && ch.Children.Length > 0) {
                XamlXmlNode tr = ch.Children[0];
                if (tr.LocalName == "TranslateTransform") {
                    string xa = tr.GetAttr("X");
                    if (xa != null && XamlStr.LooksLikeMarkupExtension(xa)) {
                        string xk = XamlStr.ExtractExtType(xa);
                        if (xk == "Binding" || xk == "TemplateBinding") {
                            BindElementFromSpec(el, propPrefix + ".RelativeTransform.X", ParseBindingSpec(xa, ctx), ctx);
                        }
                    }
                    string ya = tr.GetAttr("Y");
                    if (ya != null && XamlStr.LooksLikeMarkupExtension(ya)) {
                        string yk = XamlStr.ExtractExtType(ya);
                        if (yk == "Binding" || yk == "TemplateBinding") {
                            BindElementFromSpec(el, propPrefix + ".RelativeTransform.Y", ParseBindingSpec(ya, ctx), ctx);
                        }
                    }
                }
            }
        }
    }

    private static void ParseGradientStopsInto(GradientBrush gb, XamlXmlNode node, XamlBuildContext ctx) {
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "GradientStop") {
                string cAttr = ch.GetAttr("Color");
                string oAttr = ch.GetAttr("Offset");
                string col = "#FF000000";
                if (cAttr != null) {
                    if (XamlStr.LooksLikeMarkupExtension(cAttr)) {
                        string ck = XamlStr.ExtractExtType(cAttr);
                        if (ck != "Binding" && ck != "TemplateBinding") {
                            col = DependencyObject.UnboxString(ResolveMarkupValue(cAttr, ctx, null), "#FF000000");
                        }
                    } else {
                        col = cAttr.Trim();
                    }
                }
                float off = 0.0f;
                if (oAttr != null) {
                    if (XamlStr.LooksLikeMarkupExtension(oAttr)) {
                        string ok = XamlStr.ExtractExtType(oAttr);
                        if (ok != "Binding" && ok != "TemplateBinding") {
                            off = DependencyObject.UnboxFloat(ResolveMarkupValue(oAttr, ctx, null), 0.0f);
                        }
                    } else {
                        off = ParseUtil.ParseFloat(oAttr, 0.0f);
                    }
                }
                gb.AddStop(col, off);
            } else if (ch.LocalName == "GradientStopCollection" || XamlStr.EndsWith(ch.LocalName, ".GradientStops")) {
                ParseGradientStopsInto(gb, ch, ctx);
            }
        }
    }

    private static void ApplyCommonGradientAttrs(GradientBrush gb, XamlXmlNode node) {
        string op = node.GetAttr("Opacity");
        if (op != null) { gb.Opacity = ParseUtil.ParseFloat(op, 1.0f); }
        string sm = node.GetAttr("SpreadMethod");
        if (sm != null) {
            string sml = sm.Trim().ToLower();
            gb.SpreadMethod = sml == "reflect" ? 1 : (sml == "repeat" ? 2 : 0);
        }
        string mm = node.GetAttr("MappingMode");
        if (mm != null) {
            gb.MappingMode = mm.Trim().ToLower() == "absolute" ? 1 : 0;
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".RelativeTransform") && ch.Children.Length > 0) {
                XamlXmlNode tr = ch.Children[0];
                if (tr.LocalName == "TranslateTransform") {
                    string xa = tr.GetAttr("X");
                    if (xa != null && !XamlStr.LooksLikeMarkupExtension(xa)) {
                        gb.RelativeTranslateX = ParseUtil.ParseFloat(xa, 0.0f);
                    }
                    string ya = tr.GetAttr("Y");
                    if (ya != null && !XamlStr.LooksLikeMarkupExtension(ya)) {
                        gb.RelativeTranslateY = ParseUtil.ParseFloat(ya, 0.0f);
                    }
                }
            }
        }
    }

    private static LinearGradientBrush BuildLinearGradientBrushNode(XamlXmlNode node, XamlBuildContext ctx) {
        Point sp = new Point(0.0f, 0.0f);
        Point ep = new Point(1.0f, 1.0f);
        string spAttr = node.GetAttr("StartPoint");
        if (spAttr != null) { sp = Point.Parse(spAttr); }
        string epAttr = node.GetAttr("EndPoint");
        if (epAttr != null) { ep = Point.Parse(epAttr); }
        var lgb = new LinearGradientBrush(sp, ep);
        ApplyCommonGradientAttrs(lgb, node);
        ParseGradientStopsInto(lgb, node, ctx);
        return lgb;
    }

    private static RadialGradientBrush BuildRadialGradientBrushNode(XamlXmlNode node, XamlBuildContext ctx) {
        Point ctr = new Point(0.5f, 0.5f);
        Point org = new Point(0.5f, 0.5f);
        string ctrAttr = node.GetAttr("Center");
        if (ctrAttr != null) { ctr = Point.Parse(ctrAttr); }
        string orgAttr = node.GetAttr("GradientOrigin");
        if (orgAttr != null) { org = Point.Parse(orgAttr); } else { org = ctr; }
        float rx = ParseUtil.ParseFloat(node.GetAttr("RadiusX"), 0.5f);
        float ry = ParseUtil.ParseFloat(node.GetAttr("RadiusY"), 0.5f);
        var rgb = new RadialGradientBrush(ctr.X, ctr.Y, org.X, org.Y, rx, ry);
        ApplyCommonGradientAttrs(rgb, node);
        ParseGradientStopsInto(rgb, node, ctx);
        return rgb;
    }

    private static VisualBrush BuildVisualBrushNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        var vb = new VisualBrush();
        string st = node.GetAttr("Stretch");
        if (st != null) {
            string sl = st.Trim();
            if (sl == "None") { vb.Stretch = Stretch.None; }
            else if (sl == "Fill") { vb.Stretch = Stretch.Fill; }
            else if (sl == "UniformToFill") { vb.Stretch = Stretch.UniformToFill; }
            else { vb.Stretch = Stretch.Uniform; }
        }
        string tm = node.GetAttr("TileMode");
        if (tm != null && tm.Trim().Length > 0) { vb.TileMode = tm.Trim(); }
        string vbx = node.GetAttr("Viewbox");
        if (vbx != null) { vb.Viewbox = vbx; }
        string vp = node.GetAttr("Viewport");
        if (vp != null) { vb.Viewport = vp; }
        string vpu = node.GetAttr("ViewportUnits");
        if (vpu != null) { vb.ViewportUnits = vpu; }
        string ax = node.GetAttr("AlignmentX");
        if (ax != null) { vb.AlignmentX = ax; }
        string ay = node.GetAttr("AlignmentY");
        if (ay != null) { vb.AlignmentY = ay; }
        string op = node.GetAttr("Opacity");
        if (op != null) { vb.Opacity = ParseUtil.ParseFloat(op, 1.0f); }
        string visAttr = node.GetAttr("Visual");
        if (visAttr != null && XamlStr.LooksLikeMarkupExtension(visAttr)) {
            string vKind = XamlStr.ExtractExtType(visAttr);
            if (vKind == "Binding") {
                XamlParsedBinding pb = ParseBindingSpec(visAttr, ctx);
                if (pb.ElementName.Length > 0) {
                    vb.VisualElementName = pb.ElementName;
                    if (contextHost != null) { vb.ResolveVisual(contextHost); }
                }
            } else {
                DependencyObject resolved = ResolveMarkupValue(visAttr, ctx, contextHost);
                if (resolved is UIElement) { vb.Visual = resolved as UIElement; }
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Visual") && ch.Children.Length > 0) {
                UIElement childVis = BuildObjectNode(ch.Children[0], ctx, contextHost) as UIElement;
                if (childVis != null) { vb.Visual = childVis; }
            }
        }
        return vb;
    }

    private static Brush BuildImageBrushNode(XamlXmlNode node, XamlBuildContext ctx) {
        string src = node.GetAttr("ImageSource");
        if (src == null) { src = ""; }
        string resolvedSrc = (src.Length > 0 && !XamlStr.LooksLikeMarkupExtension(src) && ctx != null && ctx.BaseUri.Length > 0)
            ? XamlStr.ResolveUri(ctx.BaseUri, src)
            : src;
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Shader") && ch.Children.Length > 0) {
                XamlXmlNode sh = ch.Children[0];
                string shaderKind = sh.LocalName;
                if (shaderKind == "PlasmaBrush") {
                    float sx = ParseUtil.ParseFloat(sh.GetAttr("ScaleX"), 64.0f);
                    float sy = ParseUtil.ParseFloat(sh.GetAttr("ScaleY"), 64.0f);
                    float tm = ParseUtil.ParseFloat(sh.GetAttr("Time"), 0.0f);
                    string seedAttr = sh.GetAttr("Seed");
                    float seed = (seedAttr != null && !XamlStr.LooksLikeMarkupExtension(seedAttr))
                        ? ParseUtil.ParseFloat(seedAttr, 100.0f) : 100.0f;
                    string colAttr = sh.GetAttr("Color");
                    string col = "#FF01BCFF";
                    if (colAttr != null && colAttr.Length > 0 && !XamlStr.LooksLikeMarkupExtension(colAttr)) {
                        col = ParseUtil.FormatArgbHex(ParseUtil.ParseColor(colAttr));
                    }
                    float vw = 240.0f;
                    float vh = 160.0f;
                    string vpAttr = node.GetAttr("Viewport");
                    if (vpAttr != null && vpAttr.Trim().Length > 0) {
                        string[] parts = vpAttr.Trim().Replace(",", " ").Split(" ");
                        var nums = new float[0];
                        for (int p = 0; p < parts.Length; p += 1) {
                            string pt = parts[p].Trim();
                            if (pt.Length > 0) { nums.Push(ParseUtil.ParseFloat(pt, 0.0f)); }
                        }
                        if (nums.Length >= 4) {
                            vw = nums[2];
                            vh = nums[3];
                        }
                    }
                    var pib = new ImageBrush("shader://plasma");
                    string pst = node.GetAttr("Stretch");
                    if (pst != null) {
                        string sl = pst.Trim();
                        if (sl == "None") { pib.Stretch = Stretch.None; }
                        else if (sl == "Fill") { pib.Stretch = Stretch.Fill; }
                        else if (sl == "UniformToFill") { pib.Stretch = Stretch.UniformToFill; }
                        else { pib.Stretch = Stretch.Uniform; }
                    }
                    string ptm = node.GetAttr("TileMode");
                    if (ptm != null && ptm.Trim().Length > 0) { pib.TileMode = ptm.Trim(); }
                    string pvb = node.GetAttr("Viewbox");
                    if (pvb != null) { pib.Viewbox = pvb; }
                    if (vpAttr != null) { pib.Viewport = vpAttr; }
                    string pvpu = node.GetAttr("ViewportUnits");
                    if (pvpu != null) { pib.ViewportUnits = pvpu; }
                    string pop = node.GetAttr("Opacity");
                    if (pop != null) { pib.Opacity = ParseUtil.ParseFloat(pop, 1.0f); }
                    pib.EnablePlasmaShader(sx, sy, tm, seed, col, vw, vh);
                    return pib;
                }
                if (shaderKind == "ConicGradientBrush") {
                    var cg = new ConicGradientBrush();
                    ApplyCommonGradientAttrs(cg, node);
                    ParseGradientStopsInto(cg, sh, ctx);
                    if (cg.StopCount() == 0) {
                        cg.AddStop("#FFFF0000", 0.0f);
                        cg.AddStop("#FFFFFF00", 0.25f);
                        cg.AddStop("#FFADFF2F", 0.5f);
                        cg.AddStop("#FF1E90FF", 0.75f);
                        cg.AddStop("#FF800080", 1.0f);
                    }
                    return cg;
                }
                if (shaderKind == "SaturationBrush") {
                    if (resolvedSrc.Length == 0) {
                        var fb = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
                        fb.AddStop("#FF64748B", 0.0f);
                        fb.AddStop("#FF94A3B8", 0.5f);
                        fb.AddStop("#FFCBD5E1", 1.0f);
                        string sopFb = node.GetAttr("Opacity");
                        if (sopFb != null) { fb.Opacity = ParseUtil.ParseFloat(sopFb, 1.0f); }
                        return fb;
                    }
                    float satFactor = ParseUtil.ParseFloat(sh.GetAttr("Saturation"), 1.0f);
                    var sib = new ImageBrush("shader://saturation?sat=" + satFactor + "&src=" + resolvedSrc);
                    string sst = node.GetAttr("Stretch");
                    if (sst != null) {
                        string sl = sst.Trim();
                        if (sl == "None") { sib.Stretch = Stretch.None; }
                        else if (sl == "Fill") { sib.Stretch = Stretch.Fill; }
                        else if (sl == "UniformToFill") { sib.Stretch = Stretch.UniformToFill; }
                        else { sib.Stretch = Stretch.Uniform; }
                    }
                    string stm = node.GetAttr("TileMode");
                    if (stm != null && stm.Trim().Length > 0) { sib.TileMode = stm.Trim(); }
                    string svb = node.GetAttr("Viewbox");
                    if (svb != null) { sib.Viewbox = svb; }
                    string svp = node.GetAttr("Viewport");
                    if (svp != null) { sib.Viewport = svp; }
                    string svpu = node.GetAttr("ViewportUnits");
                    if (svpu != null) { sib.ViewportUnits = svpu; }
                    string sop = node.GetAttr("Opacity");
                    if (sop != null) { sib.Opacity = ParseUtil.ParseFloat(sop, 1.0f); }
                    return sib;
                }
            }
        }
        var ib = new ImageBrush(resolvedSrc);
        string st = node.GetAttr("Stretch");
        if (st != null) {
            string sl = st.Trim();
            if (sl == "None") { ib.Stretch = Stretch.None; }
            else if (sl == "Fill") { ib.Stretch = Stretch.Fill; }
            else if (sl == "UniformToFill") { ib.Stretch = Stretch.UniformToFill; }
            else { ib.Stretch = Stretch.Uniform; }
        }
        string tmMode = node.GetAttr("TileMode");
        if (tmMode != null && tmMode.Trim().Length > 0) { ib.TileMode = tmMode.Trim(); }
        string vb = node.GetAttr("Viewbox");
        if (vb != null) { ib.Viewbox = vb; }
        string vp2 = node.GetAttr("Viewport");
        if (vp2 != null) { ib.Viewport = vp2; }
        string vpu2 = node.GetAttr("ViewportUnits");
        if (vpu2 != null) { ib.ViewportUnits = vpu2; }
        string op = node.GetAttr("Opacity");
        if (op != null) { ib.Opacity = ParseUtil.ParseFloat(op, 1.0f); }
        return ib;
    }

    private static void AttachTransformExtraAttrs(Transform tr, XamlXmlNode node, XamlBuildContext ctx) {
        if (tr == null || node == null) { return; }
        for (int i = 0; i < node.Attrs.Length; i += 1) {
            XamlXmlAttr a = node.Attrs[i];
            if (a.Name.Contains(".")) {
                DependencyObject valBox = XamlStr.LooksLikeMarkupExtension(a.Value)
                    ? ResolveMarkupValue(a.Value, ctx, null)
                    : CoerceLiteralForProperty(a.Name, a.Value);
                tr.SetAttachedValue(a.Name, valBox);
                string stripped = XamlStr.StripPrefix(a.Name);
                if (stripped != a.Name) { tr.SetAttachedValue(stripped, valBox); }
            }
        }
    }

    private static Transform BuildTransformNode(XamlXmlNode node, XamlBuildContext ctx) {
        string tag = node.LocalName;
        Transform res = null;
        if (tag == "TranslateTransform") {
            res = new TranslateTransform(
                ParseUtil.ParseFloat(node.GetAttr("X"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("Y"), 0.0f));
        } else if (tag == "ScaleTransform") {
            res = new ScaleTransform(
                ParseUtil.ParseFloat(node.GetAttr("ScaleX"), 1.0f),
                ParseUtil.ParseFloat(node.GetAttr("ScaleY"), 1.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        } else if (tag == "RotateTransform") {
            res = new RotateTransform(
                ParseUtil.ParseFloat(node.GetAttr("Angle"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        } else if (tag == "SkewTransform") {
            res = new SkewTransform(
                ParseUtil.ParseFloat(node.GetAttr("AngleX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("AngleY"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        } else {
            var tg = new TransformGroup();
            for (int i = 0; i < node.Children.Length; i += 1) {
                Transform sub = BuildTransformNode(node.Children[i], ctx);
                if (sub != null) { tg.Add(sub); }
            }
            res = tg;
        }
        AttachTransformExtraAttrs(res, node, ctx);
        return res;
    }

    private static CompositeTransform3D BuildCompositeTransform3DNode(XamlXmlNode node) {
        var t3 = new CompositeTransform3D();
        string rx = node.GetAttr("RotationX");
        if (rx != null && !XamlStr.LooksLikeMarkupExtension(rx)) { t3.RotationX = ParseUtil.ParseFloat(rx, 0.0f); }
        string ry = node.GetAttr("RotationY");
        if (ry != null && !XamlStr.LooksLikeMarkupExtension(ry)) { t3.RotationY = ParseUtil.ParseFloat(ry, 0.0f); }
        string rz = node.GetAttr("RotationZ");
        if (rz != null && !XamlStr.LooksLikeMarkupExtension(rz)) { t3.RotationZ = ParseUtil.ParseFloat(rz, 0.0f); }
        string tx = node.GetAttr("TranslateX");
        if (tx != null && !XamlStr.LooksLikeMarkupExtension(tx)) { t3.TranslateX = ParseUtil.ParseFloat(tx, 0.0f); }
        string ty = node.GetAttr("TranslateY");
        if (ty != null && !XamlStr.LooksLikeMarkupExtension(ty)) { t3.TranslateY = ParseUtil.ParseFloat(ty, 0.0f); }
        string tz = node.GetAttr("TranslateZ");
        if (tz != null && !XamlStr.LooksLikeMarkupExtension(tz)) { t3.TranslateZ = ParseUtil.ParseFloat(tz, 0.0f); }
        string sx = node.GetAttr("ScaleX");
        if (sx != null && !XamlStr.LooksLikeMarkupExtension(sx)) { t3.ScaleX = ParseUtil.ParseFloat(sx, 1.0f); }
        string sy = node.GetAttr("ScaleY");
        if (sy != null && !XamlStr.LooksLikeMarkupExtension(sy)) { t3.ScaleY = ParseUtil.ParseFloat(sy, 1.0f); }
        string sz = node.GetAttr("ScaleZ");
        if (sz != null && !XamlStr.LooksLikeMarkupExtension(sz)) { t3.ScaleZ = ParseUtil.ParseFloat(sz, 1.0f); }
        string cx = node.GetAttr("CenterX");
        if (cx != null && !XamlStr.LooksLikeMarkupExtension(cx)) { t3.CenterX = ParseUtil.ParseFloat(cx, 0.0f); }
        string cy = node.GetAttr("CenterY");
        if (cy != null && !XamlStr.LooksLikeMarkupExtension(cy)) { t3.CenterY = ParseUtil.ParseFloat(cy, 0.0f); }
        string cz = node.GetAttr("CenterZ");
        if (cz != null && !XamlStr.LooksLikeMarkupExtension(cz)) { t3.CenterZ = ParseUtil.ParseFloat(cz, 0.0f); }
        return t3;
    }

    private static PlaneProjection BuildPlaneProjectionNode(XamlXmlNode node) {
        var pp = new PlaneProjection();
        string rx = node.GetAttr("RotationX");
        if (rx != null && !XamlStr.LooksLikeMarkupExtension(rx)) { pp.RotationX = ParseUtil.ParseFloat(rx, 0.0f); }
        string ry = node.GetAttr("RotationY");
        if (ry != null && !XamlStr.LooksLikeMarkupExtension(ry)) { pp.RotationY = ParseUtil.ParseFloat(ry, 0.0f); }
        string rz = node.GetAttr("RotationZ");
        if (rz != null && !XamlStr.LooksLikeMarkupExtension(rz)) { pp.RotationZ = ParseUtil.ParseFloat(rz, 0.0f); }
        string lx = node.GetAttr("LocalOffsetX");
        if (lx != null && !XamlStr.LooksLikeMarkupExtension(lx)) { pp.LocalOffsetX = ParseUtil.ParseFloat(lx, 0.0f); }
        string ly = node.GetAttr("LocalOffsetY");
        if (ly != null && !XamlStr.LooksLikeMarkupExtension(ly)) { pp.LocalOffsetY = ParseUtil.ParseFloat(ly, 0.0f); }
        string lz = node.GetAttr("LocalOffsetZ");
        if (lz != null && !XamlStr.LooksLikeMarkupExtension(lz)) { pp.LocalOffsetZ = ParseUtil.ParseFloat(lz, 0.0f); }
        string gx = node.GetAttr("GlobalOffsetX");
        if (gx != null && !XamlStr.LooksLikeMarkupExtension(gx)) { pp.GlobalOffsetX = ParseUtil.ParseFloat(gx, 0.0f); }
        string gy = node.GetAttr("GlobalOffsetY");
        if (gy != null && !XamlStr.LooksLikeMarkupExtension(gy)) { pp.GlobalOffsetY = ParseUtil.ParseFloat(gy, 0.0f); }
        string gz = node.GetAttr("GlobalOffsetZ");
        if (gz != null && !XamlStr.LooksLikeMarkupExtension(gz)) { pp.GlobalOffsetZ = ParseUtil.ParseFloat(gz, 0.0f); }
        string cx = node.GetAttr("CenterOfRotationX");
        if (cx != null && !XamlStr.LooksLikeMarkupExtension(cx)) { pp.CenterOfRotationX = ParseUtil.ParseFloat(cx, 0.5f); }
        string cy = node.GetAttr("CenterOfRotationY");
        if (cy != null && !XamlStr.LooksLikeMarkupExtension(cy)) { pp.CenterOfRotationY = ParseUtil.ParseFloat(cy, 0.5f); }
        string cz = node.GetAttr("CenterOfRotationZ");
        if (cz != null && !XamlStr.LooksLikeMarkupExtension(cz)) { pp.CenterOfRotationZ = ParseUtil.ParseFloat(cz, 0.0f); }
        return pp;
    }

    private static void ApplyOrBindEffectFloat(
        Effect eff, string targetProp, string rawAttr, float defaultVal, XamlBuildContext ctx, UIElement contextHost) {
        if (eff == null) { return; }
        if (rawAttr == null) {
            eff.SetPropertyValue(targetProp, DependencyObject.BoxFloat(defaultVal));
            return;
        }
        if (XamlStr.LooksLikeMarkupExtension(rawAttr)) {
            string kind = XamlStr.ExtractExtType(rawAttr);
            if (kind == "Binding" || kind == "TemplateBinding") {
                eff.SetPropertyValue(targetProp, DependencyObject.BoxFloat(defaultVal));
                BindingEngine.BindEffect(eff, contextHost, targetProp, ToBindingObject(ParseBindingSpec(rawAttr, ctx)));
            } else {
                float resolved = DependencyObject.UnboxFloat(ResolveMarkupValue(rawAttr, ctx, contextHost), defaultVal);
                eff.SetPropertyValue(targetProp, DependencyObject.BoxFloat(resolved));
            }
        } else {
            eff.SetPropertyValue(targetProp, DependencyObject.BoxFloat(ParseUtil.ParseFloat(rawAttr, defaultVal)));
        }
    }

    private static void ApplyOrBindEffectColor(
        Effect eff, string rawAttr, string defaultHex, XamlBuildContext ctx, UIElement contextHost) {
        if (eff == null) { return; }
        if (rawAttr == null) {
            eff.Color = defaultHex;
            return;
        }
        if (XamlStr.LooksLikeMarkupExtension(rawAttr)) {
            string kind = XamlStr.ExtractExtType(rawAttr);
            if (kind == "Binding" || kind == "TemplateBinding") {
                eff.Color = defaultHex;
                BindingEngine.BindEffect(eff, contextHost, "Color", ToBindingObject(ParseBindingSpec(rawAttr, ctx)));
            } else {
                eff.Color = DependencyObject.UnboxString(ResolveMarkupValue(rawAttr, ctx, contextHost), defaultHex);
            }
        } else {
            eff.Color = rawAttr.Trim();
        }
    }

    private static Effect BuildEffectNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        string tag = node.LocalName;
        if (tag == "BlurEffect") {
            var b = new BlurEffect();
            ApplyOrBindEffectFloat(b, "Radius", node.GetAttr("Radius"), 8.0f, ctx, contextHost);
            return b;
        }
        if (tag == "BackdropBlurEffect") {
            var bb = new BackdropBlurEffect(16.0f);
            ApplyOrBindEffectFloat(bb, "Radius", node.GetAttr("Radius"), 16.0f, ctx, contextHost);
            return bb;
        }
        if (tag == "DropShadowEffect") {
            var ds = new DropShadowEffect(5.0f, 5.0f, 315.0f, "#FF000000", 1.0f);
            ApplyOrBindEffectFloat(ds, "BlurRadius", node.GetAttr("BlurRadius"), 5.0f, ctx, contextHost);
            ApplyOrBindEffectFloat(ds, "ShadowDepth", node.GetAttr("ShadowDepth"), 5.0f, ctx, contextHost);
            ApplyOrBindEffectFloat(ds, "Direction", node.GetAttr("Direction"), 315.0f, ctx, contextHost);
            ApplyOrBindEffectFloat(ds, "Opacity", node.GetAttr("Opacity"), 1.0f, ctx, contextHost);
            ApplyOrBindEffectColor(ds, node.GetAttr("Color"), "#FF000000", ctx, contextHost);
            return ds;
        }
        if (tag == "TintEffect") {
            var te = new TintEffect("#FF2266FF", 0.5f);
            ApplyOrBindEffectColor(te, node.GetAttr("Color"), "#FF2266FF", ctx, contextHost);
            ApplyOrBindEffectFloat(te, "Opacity", node.GetAttr("Opacity"), 0.5f, ctx, contextHost);
            return te;
        }
        if (tag == "PixelateEffect") {
            var pe = new PixelateEffect(6.0f);
            string sz = node.GetAttr("Size");
            if (sz == null) { sz = node.GetAttr("PixelSize"); }
            ApplyOrBindEffectFloat(pe, "Size", sz, 6.0f, ctx, contextHost);
            return pe;
        }
        if (tag == "ChromaticAberrationEffect") {
            var cae = new ChromaticAberrationEffect(3.0f);
            ApplyOrBindEffectFloat(cae, "Intensity", node.GetAttr("Intensity"), 3.0f, ctx, contextHost);
            return cae;
        }
        if (tag == "DirectionalBlurEffect") {
            var dbe = new DirectionalBlurEffect(8.0f, 0.0f);
            string ang = node.GetAttr("Angle");
            if (ang == null) { ang = node.GetAttr("Direction"); }
            ApplyOrBindEffectFloat(dbe, "Radius", node.GetAttr("Radius"), 8.0f, ctx, contextHost);
            ApplyOrBindEffectFloat(dbe, "Angle", ang, 0.0f, ctx, contextHost);
            return dbe;
        }
        if (tag == "SaturationEffect") {
            var se = new SaturationEffect(1.0f);
            ApplyOrBindEffectFloat(se, "Factor", node.GetAttr("Factor"), 1.0f, ctx, contextHost);
            return se;
        }
        return null;
    }

    // ---- Styles, Setters, Triggers, Templates ----
    private static string NormalizeSetterProp(string rawProp) {
        string p = XamlStr.StripOuterParens(rawProp);
        int dot = XamlStr.FindFrom(p, ".", 0);
        if (dot > 0 && dot + 1 < p.Length) {
            string owner = p.Substring(0, dot);
            if (owner != "Grid" && owner != "Canvas" && owner != "DockPanel" &&
                owner != "ScrollViewer" && owner != "ToolTipService" &&
                owner != "DragDrop" && owner != "Custom" &&
                owner != "Element" && owner != "Transform3D" && owner != "Projection3D" &&
                owner != "RenderOptions" && owner != "TextOptions" &&
                owner != "VirtualizingStackPanel" && owner != "RiveControl" &&
                owner != "Storyboard" && owner != "Loc" &&
                owner != "Background" && owner != "Foreground" && owner != "BorderBrush" &&
                owner != "Fill" && owner != "Stroke" &&
                owner != "RenderTransform" && owner != "LayoutTransform" &&
                owner != "Effect" && owner != "Data" && owner != "OpacityMask" &&
                !owner.Contains(":")) {
                return p.Substring(dot + 1);
            }
        }
        return p;
    }

    private static Setter BuildSetterNode(XamlXmlNode node, XamlBuildContext ctx) {
        string prop = NormalizeSetterProp(node.GetAttr("Property"));
        if (prop.Length == 0) { return null; }
        if (XamlStr.StartsWith(prop, "DragDrop.")) { DragDrop.EnsureInstalled(); }
        if (prop == "ToolTip" || prop == "ToolTipService.ToolTip") { ToolTipService.EnsureInstalled(); }
        string targetName = node.GetAttr("TargetName");
        if (targetName == null) { targetName = ""; }

        string valAttr = node.GetAttr("Value");
        if (valAttr != null) {
            if (XamlStr.LooksLikeMarkupExtension(valAttr)) {
                string kind = XamlStr.ExtractExtType(valAttr);
                string body = XamlStr.ExtractExtBody(valAttr);
                if (kind == "DynamicResource") {
                    var s = new Setter();
                    s.Property = prop;
                    s.TargetName = targetName;
                    s.DynamicResourceKey = NormalizeResourceKey(body);
                    return s;
                }
                if (kind == "Binding" || kind == "TemplateBinding") {
                    XamlParsedBinding pb = ParseBindingSpec(valAttr, ctx);
                    Setter sb = Setter.FromBinding(
                        prop, pb.Path, pb.Converter, pb.ConverterKey, pb.ConverterParameter,
                        pb.StringFormat, pb.Mode, pb.ElementName, pb.RelativeSourceMode,
                        pb.AncestorType, targetName);
                    if (pb.HasFallback) { sb.SetBindingFallback(pb.FallbackBox); }
                    return sb;
                }
                DependencyObject box = ResolveMarkupValue(valAttr, ctx, null);
                if (box is BoxedString && IsBrushPropertyName(prop)) {
                    box = new SolidColorBrush((box as BoxedString).Value);
                }
                return Setter.FromBox(prop, box, targetName);
            }
            return Setter.FromBox(prop, CoerceLiteralForProperty(prop, valAttr), targetName);
        }

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "Setter.Value" && ch.Children.Length > 0) {
                DependencyObject valBox = BuildObjectNode(ch.Children[0], ctx, null);
                return Setter.FromBox(prop, valBox, targetName);
            }
        }
        return null;
    }

    private static TriggerBase BuildTriggerNode(XamlXmlNode node, XamlBuildContext ctx) {
        string tag = node.LocalName;
        TriggerBase trig = null;

        if (tag == "Trigger") {
            string prop = NormalizeSetterProp(node.GetAttr("Property"));
            string val = node.GetAttr("Value");
            string srcName = node.GetAttr("SourceName");
            DependencyObject valBox = val != null
                ? (XamlStr.LooksLikeMarkupExtension(val) ? ResolveMarkupValue(val, ctx, null) : DependencyObject.BoxString(val))
                : DependencyObject.BoxBool(true);
            var tr = new Trigger(prop, valBox);
            if (srcName != null) { tr.SourceName = srcName; }
            trig = tr;
        } else if (tag == "DataTrigger") {
            string bindAttr = node.GetAttr("Binding");
            string val = node.GetAttr("Value");
            DependencyObject valBox = val != null
                ? (XamlStr.LooksLikeMarkupExtension(val) ? ResolveMarkupValue(val, ctx, null) : DependencyObject.BoxString(val))
                : DependencyObject.BoxBool(true);
            var dt = new DataTrigger("", valBox);
            if (bindAttr != null && XamlStr.LooksLikeMarkupExtension(bindAttr)) {
                XamlParsedBinding pb = ParseBindingSpec(bindAttr, ctx);
                dt.BindingPath = pb.Path;
                dt.ElementName = pb.ElementName;
                dt.RelativeSourceMode = pb.RelativeSourceMode;
                dt.RelativeSourceAncestorType = pb.AncestorType;
            }
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (ch.LocalName == "DataTrigger.Binding" && ch.Children.Length > 0) {
                    XamlXmlNode bChild = ch.Children[0];
                    if (bChild.LocalName == "MultiBinding") {
                        dt.SetMultiBinding(BuildMultiBindingNode(bChild, ctx));
                    } else if (bChild.LocalName == "Binding") {
                        dt.SetDynamicBinding(ParseBindingElementNode(bChild, ctx));
                    }
                }
            }
            trig = dt;
        } else if (tag == "MultiTrigger" || tag == "MultiDataTrigger") {
            var mdt = new MultiDataTrigger();
            bool isPropertyMulti = tag == "MultiTrigger";
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (XamlStr.EndsWith(ch.LocalName, ".Conditions")) {
                    for (int c = 0; c < ch.Children.Length; c += 1) {
                        XamlXmlNode cond = ch.Children[c];
                        if (cond.LocalName == "Condition") {
                            string cv = cond.GetAttr("Value");
                            DependencyObject cvBox = cv != null
                                ? (XamlStr.LooksLikeMarkupExtension(cv) ? ResolveMarkupValue(cv, ctx, null) : DependencyObject.BoxString(cv))
                                : DependencyObject.BoxBool(true);
                            if (isPropertyMulti) {
                                string cp = NormalizeSetterProp(cond.GetAttr("Property"));
                                string sn = cond.GetAttr("SourceName");
                                mdt.AddCondition(cp, cvBox, sn == null ? "" : sn, "Self", "");
                            } else {
                                string cb = cond.GetAttr("Binding");
                                if (cb != null && XamlStr.LooksLikeMarkupExtension(cb)) {
                                    XamlParsedBinding pb = ParseBindingSpec(cb, ctx);
                                    mdt.AddCondition(pb.Path, cvBox, pb.ElementName, pb.RelativeSourceMode, pb.AncestorType);
                                }
                            }
                        }
                    }
                }
            }
            trig = mdt;
        } else if (tag == "EventTrigger") {
            string evName = node.GetAttr("RoutedEvent");
            var et = new EventTrigger(evName == null ? "Loaded" : XamlStr.StripPrefix(evName));
            string sn = node.GetAttr("SourceName");
            if (sn != null) { et.SourceName = sn; }
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (ch.LocalName == "BeginStoryboard") {
                    AttachBeginStoryboardAction(ch, et, true, ctx);
                } else if (XamlStr.EndsWith(ch.LocalName, ".Actions")) {
                    for (int a = 0; a < ch.Children.Length; a += 1) {
                        if (ch.Children[a].LocalName == "BeginStoryboard") {
                            AttachBeginStoryboardAction(ch.Children[a], et, true, ctx);
                        }
                    }
                }
            }
            return et;
        }

        if (trig == null) { return null; }

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "Setter") {
                Setter s = BuildSetterNode(ch, ctx);
                if (s != null) { trig.AddSetter(s); }
            } else if (XamlStr.EndsWith(ch.LocalName, ".Setters")) {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    Setter s = BuildSetterNode(ch.Children[k], ctx);
                    if (s != null) { trig.AddSetter(s); }
                }
            } else if (XamlStr.EndsWith(ch.LocalName, ".EnterActions")) {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    if (ch.Children[k].LocalName == "BeginStoryboard") {
                        AttachBeginStoryboardAction(ch.Children[k], trig, true, ctx);
                    }
                }
            } else if (XamlStr.EndsWith(ch.LocalName, ".ExitActions")) {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    if (ch.Children[k].LocalName == "BeginStoryboard") {
                        AttachBeginStoryboardAction(ch.Children[k], trig, false, ctx);
                    }
                }
            }
        }
        return trig;
    }

    private static void AttachBeginStoryboardAction(
        XamlXmlNode beginNode, TriggerBase trig, bool isEnter, XamlBuildContext ctx) {
        string sbAttr = beginNode.GetAttr("Storyboard");
        if (sbAttr != null && XamlStr.LooksLikeMarkupExtension(sbAttr)) {
            string kind = XamlStr.ExtractExtType(sbAttr);
            string body = XamlStr.ExtractExtBody(sbAttr);
            if (kind == "StaticResource") {
                string sbKey = body.Trim();
                DependencyObject sbBox = ctx != null ? ctx.FindResourceBox(sbKey, null) : null;
                if (sbBox is Storyboard) {
                    if (isEnter) { trig.AddEnterAction(sbBox as Storyboard); }
                    else { trig.AddExitAction(sbBox as Storyboard); }
                    return;
                }
                if (isEnter) { trig.AddEnterActionKey(sbKey); }
                else { trig.AddExitActionKey(sbKey); }
                return;
            }
        }
        for (int i = 0; i < beginNode.Children.Length; i += 1) {
            if (beginNode.Children[i].LocalName == "Storyboard") {
                Storyboard sb = BuildStoryboardNode(beginNode.Children[i], ctx);
                if (sb != null) {
                    if (isEnter) { trig.AddEnterAction(sb); }
                    else { trig.AddExitAction(sb); }
                }
            }
        }
    }

    private static Style BuildStyleNode(XamlXmlNode node, XamlBuildContext ctx) {
        string tt = UnwrapTypeSpec(node.GetAttr("TargetType"));
        Style basedOn = null;
        string boAttr = node.GetAttr("BasedOn");
        if (boAttr != null && XamlStr.LooksLikeMarkupExtension(boAttr)) {
            DependencyObject boBox = ResolveMarkupValue(boAttr, ctx, null);
            if (boBox is Style) { basedOn = boBox as Style; }
        }
        var st = new Style(tt, basedOn);
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "Style.Resources") {
                ctx.PushScope(st.Resources);
                PopulateResourceDictionaryFromChildren(ch, st.Resources, ctx);
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "Setter") {
                Setter s = BuildSetterNode(ch, ctx);
                if (s != null) { st.AddSetter(s); }
            } else if (ch.LocalName == "Style.Setters") {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    Setter s = BuildSetterNode(ch.Children[k], ctx);
                    if (s != null) { st.AddSetter(s); }
                }
            } else if (ch.LocalName == "Style.Triggers") {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    TriggerBase tr = BuildTriggerNode(ch.Children[k], ctx);
                    if (tr != null) { st.AddTrigger(tr); }
                }
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            if (node.Children[i].LocalName == "Style.Resources") {
                ctx.PopScope(st.Resources);
            }
        }
        return st;
    }

    private static ResourceDictionary[] SnapshotScopes(XamlBuildContext ctx) {
        var snap = new ResourceDictionary[ctx.Scopes.Length];
        for (int i = 0; i < ctx.Scopes.Length; i += 1) {
            snap[i] = ctx.Scopes[i];
        }
        return snap;
    }

    internal static UIElement InstantiateTemplateRoot(
        XamlXmlNode visualNode, XamlBuildContext baseCtx, UIElement templatedParent,
        object dataContext, ResourceDictionary templateRes,
        VisualStateGroup[] vsgList) {
        XamlBuildContext tplCtx = baseCtx.ForkForTemplate(templatedParent, templateRes);
        DependencyObject obj = BuildObjectNode(visualNode, tplCtx, templatedParent);
        UIElement root = obj as UIElement;
        if (root != null) {
            if (templatedParent != null) {
                PropagateTemplatedParent(root, templatedParent);
            }
            if (dataContext != null) {
                root.DataContextBox = DependencyObject.BoxObject(dataContext);
            }
            if (vsgList != null && vsgList.Length > 0) {
                VisualStateManager.SetVisualStateGroups(root, vsgList);
            }
        }
        return root;
    }

    private static void PropagateTemplatedParent(UIElement root, UIElement tp) {
        if (root == null || tp == null) { return; }
        var q = new UIElement[0];
        q.Push(root);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur.TemplatedParent == null) { cur.SetTemplatedParentDirect(tp); }
            for (int i = 0; i < cur.ChildCount(); i += 1) {
                q.Push(cur.ChildAt(i));
            }
            if (cur is Popup && (cur as Popup).Child != null) {
                q.Push((cur as Popup).Child);
            }
            if (cur is ContentControl) {
                object c = (cur as ContentControl).Content;
                if (c is UIElement) { q.Push(c as UIElement); }
            }
        }
    }

    private static ControlTemplate BuildControlTemplateNode(XamlXmlNode node, XamlBuildContext ctx) {
        string tt = UnwrapTypeSpec(node.GetAttr("TargetType"));
        var ct = new ControlTemplate();
        ct.TargetType = tt;

        XamlXmlNode visualChild = null;
        VisualStateGroup[] vsg = new VisualStateGroup[0];

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "ControlTemplate.Resources") {
                ctx.PushScope(ct.Resources);
                PopulateResourceDictionaryFromChildren(ch, ct.Resources, ctx);
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "ControlTemplate.Resources") { continue; }
            if (ch.LocalName == "ControlTemplate.Triggers") {
                for (int t = 0; t < ch.Children.Length; t += 1) {
                    TriggerBase tr = BuildTriggerNode(ch.Children[t], ctx);
                    if (tr != null) { ct.AddTrigger(tr); }
                }
                continue;
            }
            if (!ch.RawName.Contains(".") && visualChild == null) {
                visualChild = ch;
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            if (node.Children[i].LocalName == "ControlTemplate.Resources") {
                ctx.PopScope(ct.Resources);
            }
        }

        if (visualChild != null) {
            for (int i = 0; i < visualChild.Children.Length; i += 1) {
                if (visualChild.Children[i].LocalName == "VisualStateManager.VisualStateGroups") {
                    vsg = BuildVisualStateGroupsList(visualChild.Children[i], ctx);
                    break;
                }
            }
            var capturedVisual = visualChild;
            var capturedScopes = SnapshotScopes(ctx);
            var capturedBaseUri = ctx.BaseUri;
            var capturedFallback = ctx.FallbackResources;
            var capturedRes = ct.RawResourcesOrNull();
            var capturedVsg = vsg;
            ct.Factory = (Control owner) => {
                var c = new XamlBuildContext(capturedFallback, capturedBaseUri);
                c.Scopes = capturedScopes;
                UIElement tpEl = owner;
                object noDc = null;
                return XamlReader.InstantiateTemplateRoot(capturedVisual, c, tpEl, noDc, capturedRes, capturedVsg);
            };
        }
        return ct;
    }

    private static DataTemplate BuildDataTemplateNode(XamlXmlNode node, XamlBuildContext ctx) {
        var dt = new DataTemplate();
        dt.DataType = UnwrapTypeSpec(node.GetAttr("DataType"));
        PopulateDataTemplateCommon(dt, node, ctx);
        return dt;
    }

    private static HierarchicalDataTemplate BuildHierarchicalDataTemplateNode(XamlXmlNode node, XamlBuildContext ctx) {
        var hdt = new HierarchicalDataTemplate();
        hdt.DataType = UnwrapTypeSpec(node.GetAttr("DataType"));
        string itemsSrc = node.GetAttr("ItemsSource");
        if (itemsSrc != null) {
            if (XamlStr.LooksLikeMarkupExtension(itemsSrc)) {
                hdt.ItemsSourcePath = ParseBindingSpec(itemsSrc, ctx).Path;
            } else {
                hdt.ItemsSourcePath = itemsSrc.Trim();
            }
        }
        string itemTplAttr = node.GetAttr("ItemTemplate");
        if (itemTplAttr != null && XamlStr.LooksLikeMarkupExtension(itemTplAttr)) {
            string kind = XamlStr.ExtractExtType(itemTplAttr);
            string body = XamlStr.ExtractExtBody(itemTplAttr);
            if (kind == "StaticResource") {
                hdt.ItemTemplateKey = body.Trim();
                DependencyObject resolved = ctx.FindResourceBox(hdt.ItemTemplateKey, null);
                if (resolved is DataTemplate) { hdt.ItemTemplate = resolved as DataTemplate; }
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "HierarchicalDataTemplate.ItemTemplate" && ch.Children.Length > 0) {
                DependencyObject subDt = BuildObjectNode(ch.Children[0], ctx, null);
                if (subDt is DataTemplate) { hdt.ItemTemplate = subDt as DataTemplate; }
            }
        }
        PopulateDataTemplateCommon(hdt, node, ctx);
        return hdt;
    }

    private static void PopulateDataTemplateCommon(DataTemplate dt, XamlXmlNode node, XamlBuildContext ctx) {
        XamlXmlNode visualChild = null;
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Resources")) {
                ctx.PushScope(dt.Resources);
                PopulateResourceDictionaryFromChildren(ch, dt.Resources, ctx);
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Resources") ||
                XamlStr.EndsWith(ch.LocalName, ".ItemTemplate")) { continue; }
            if (XamlStr.EndsWith(ch.LocalName, ".Triggers")) {
                for (int t = 0; t < ch.Children.Length; t += 1) {
                    TriggerBase tr = BuildTriggerNode(ch.Children[t], ctx);
                    if (tr != null) { dt.AddTrigger(tr); }
                }
                continue;
            }
            if (!ch.RawName.Contains(".") && visualChild == null) {
                visualChild = ch;
            }
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            if (XamlStr.EndsWith(node.Children[i].LocalName, ".Resources")) {
                ctx.PopScope(dt.Resources);
            }
        }
        if (visualChild != null) {
            var capturedVisual = visualChild;
            var capturedScopes = SnapshotScopes(ctx);
            var capturedBaseUri = ctx.BaseUri;
            var capturedFallback = ctx.FallbackResources;
            var capturedRes = dt.RawResourcesOrNull();
            dt.Factory = (object dc) => {
                var c = new XamlBuildContext(capturedFallback, capturedBaseUri);
                c.Scopes = capturedScopes;
                UIElement noTp = null;
                VisualStateGroup[] noVsg = null;
                return XamlReader.InstantiateTemplateRoot(capturedVisual, c, noTp, dc, capturedRes, noVsg);
            };
        }
    }

    private static ItemsPanelTemplate BuildItemsPanelTemplateNode(XamlXmlNode node, XamlBuildContext ctx) {
        XamlXmlNode panelNode = null;
        for (int i = 0; i < node.Children.Length; i += 1) {
            if (!node.Children[i].RawName.Contains(".")) {
                panelNode = node.Children[i];
                break;
            }
        }
        if (panelNode == null) { return new ItemsPanelTemplate(); }
        var capturedVisual = panelNode;
        var capturedScopes = SnapshotScopes(ctx);
        var capturedBaseUri = ctx.BaseUri;
        var capturedFallback = ctx.FallbackResources;
        return new ItemsPanelTemplate(() => {
            var c = new XamlBuildContext(capturedFallback, capturedBaseUri);
            c.Scopes = capturedScopes;
            UIElement noTp = null;
            object noDc = null;
            ResourceDictionary noRes = null;
            VisualStateGroup[] noVsg = null;
            return XamlReader.InstantiateTemplateRoot(capturedVisual, c, noTp, noDc, noRes, noVsg) as Panel;
        });
    }

    // ---- VisualStates & Storyboards ----
    private static EasingFunctionKind ParseEasingKind(XamlXmlNode easeNode, EasingFunctionKind fallback) {
        if (easeNode == null) { return fallback; }
        string t = easeNode.LocalName;
        if (t == "QuadraticEase") { return EasingFunctionKind.Quadratic; }
        if (t == "CubicEase" || t == "PowerEase") { return EasingFunctionKind.Cubic; }
        if (t == "QuarticEase") { return EasingFunctionKind.Quartic; }
        if (t == "QuinticEase") { return EasingFunctionKind.Quintic; }
        if (t == "SineEase") { return EasingFunctionKind.Sine; }
        if (t == "CircleEase") { return EasingFunctionKind.Circle; }
        if (t == "ExponentialEase") { return EasingFunctionKind.Exponential; }
        if (t == "BackEase") { return EasingFunctionKind.Back; }
        if (t == "BounceEase") { return EasingFunctionKind.Bounce; }
        if (t == "ElasticEase") { return EasingFunctionKind.Elastic; }
        return fallback;
    }

    private static EasingMode ParseEasingMode(XamlXmlNode easeNode, EasingMode fallback) {
        if (easeNode == null) { return fallback; }
        string m = easeNode.GetAttr("EasingMode");
        if (m == "EaseIn") { return EasingMode.EaseIn; }
        if (m == "EaseInOut") { return EasingMode.EaseInOut; }
        if (m == "EaseOut") { return EasingMode.EaseOut; }
        return fallback;
    }

    private static float ParseEasingParam0(XamlXmlNode easeNode) {
        if (easeNode == null) { return 0.0f; }
        string t = easeNode.LocalName;
        string attrName = "";
        if (t == "BounceEase") { attrName = "Bounces"; }
        else if (t == "ElasticEase") { attrName = "Oscillations"; }
        else if (t == "BackEase") { attrName = "Amplitude"; }
        else if (t == "ExponentialEase") { attrName = "Exponent"; }
        if (attrName.Length == 0) { return 0.0f; }
        string v = easeNode.GetAttr(attrName);
        return v != null ? ParseUtil.ParseFloat(v, 0.0f) : 0.0f;
    }

    private static float ParseEasingParam1(XamlXmlNode easeNode) {
        if (easeNode == null) { return 0.0f; }
        string t = easeNode.LocalName;
        string attrName = "";
        if (t == "BounceEase") { attrName = "Bounciness"; }
        else if (t == "ElasticEase") { attrName = "Springiness"; }
        if (attrName.Length == 0) { return 0.0f; }
        string v = easeNode.GetAttr(attrName);
        return v != null ? ParseUtil.ParseFloat(v, 0.0f) : 0.0f;
    }

    private static int ParseEasingParamMask(XamlXmlNode easeNode) {
        if (easeNode == null) { return 0; }
        string t = easeNode.LocalName;
        int mask = 0;
        if (t == "BounceEase") {
            if (easeNode.GetAttr("Bounces") != null) { mask = mask + 1; }
            if (easeNode.GetAttr("Bounciness") != null) { mask = mask + 2; }
        } else if (t == "ElasticEase") {
            if (easeNode.GetAttr("Oscillations") != null) { mask = mask + 1; }
            if (easeNode.GetAttr("Springiness") != null) { mask = mask + 2; }
        } else if (t == "BackEase") {
            if (easeNode.GetAttr("Amplitude") != null) { mask = mask + 1; }
        } else if (t == "ExponentialEase") {
            if (easeNode.GetAttr("Exponent") != null) { mask = mask + 1; }
        }
        return mask;
    }

    private static VisualStateGroup[] BuildVisualStateGroupsList(XamlXmlNode vsgContainer, XamlBuildContext ctx) {
        var groups = new VisualStateGroup[0];
        for (int i = 0; i < vsgContainer.Children.Length; i += 1) {
            XamlXmlNode gNode = vsgContainer.Children[i];
            if (gNode.LocalName != "VisualStateGroup") { continue; }
            var vsg = new VisualStateGroup();
            string gName = gNode.GetNameAttr();
            if (gName.Length > 0) { vsg.Name = gName; }
            for (int c = 0; c < gNode.Children.Length; c += 1) {
                XamlXmlNode ch = gNode.Children[c];
                if (ch.LocalName == "VisualState") {
                    var vs = new VisualState(ch.GetNameAttr());
                    for (int sc = 0; sc < ch.Children.Length; sc += 1) {
                        if (ch.Children[sc].LocalName == "Storyboard") {
                            vs.Storyboard = BuildStoryboardNode(ch.Children[sc], ctx);
                        }
                    }
                    vsg.AddState(vs);
                } else if (ch.LocalName == "VisualStateGroup.Transitions") {
                    for (int tr = 0; tr < ch.Children.Length; tr += 1) {
                        XamlXmlNode tNode = ch.Children[tr];
                        if (tNode.LocalName == "VisualTransition") {
                            var vt = new VisualTransition();
                            string fr = tNode.GetAttr("From");
                            string to = tNode.GetAttr("To");
                            string dur = tNode.GetAttr("GeneratedDuration");
                            if (fr != null) { vt.From = fr; }
                            if (to != null) { vt.To = to; }
                            if (dur != null) { vt.GeneratedDurationSec = XamlStr.ParseDurationSec(dur, 0.0f); }
                            for (int tc = 0; tc < tNode.Children.Length; tc += 1) {
                                XamlXmlNode tch = tNode.Children[tc];
                                if (tch.LocalName == "Storyboard") {
                                    vt.Storyboard = BuildStoryboardNode(tch, ctx);
                                } else if (tch.LocalName == "VisualTransition.GeneratedEasingFunction" && tch.Children.Length > 0) {
                                    vt.EasingKind = ParseEasingKind(tch.Children[0], EasingFunctionKind.Linear);
                                    vt.EasingMode = ParseEasingMode(tch.Children[0], EasingMode.EaseOut);
                                    int vtMask = ParseEasingParamMask(tch.Children[0]);
                                    if (vtMask != 0) {
                                        vt.SetEasingParams(ParseEasingParam0(tch.Children[0]), ParseEasingParam1(tch.Children[0]), vtMask);
                                    }
                                }
                            }
                            vsg.AddTransition(vt);
                        }
                    }
                }
            }
            groups.Push(vsg);
        }
        return groups;
    }

    private static Storyboard BuildStoryboardNode(XamlXmlNode node, XamlBuildContext ctx) {
        var sb = new Storyboard();
        string tn = node.GetAttr("Storyboard.TargetName");
        if (tn != null) { sb.TargetName = tn; }
        string sr = node.GetAttr("SpeedRatio");
        if (sr != null) { sb.SpeedRatio = ParseUtil.ParseFloat(sr, 1.0f); }
        string sbBt = node.GetAttr("BeginTime");
        if (sbBt != null) { sb.BeginTimeSec = XamlStr.ParseDurationSec(sbBt, 0.0f); }
        string sbDur = node.GetAttr("Duration");
        if (sbDur != null) { sb.ExplicitDurationSec = XamlStr.ParseDurationSec(sbDur, 0.0f); }
        string rb = node.GetAttr("RepeatBehavior");
        if (rb != null && rb.Trim().ToLower() == "forever") { sb.RepeatForever = true; }

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            string tag = ch.LocalName;
            string animTn = ch.GetAttr("Storyboard.TargetName");
            if (animTn == null) { animTn = sb.TargetName; }
            string animTp = XamlStr.NormalizeAnimTargetProp(ch.GetAttr("Storyboard.TargetProperty"));

            if (tag == "DoubleAnimation") {
                var da = new DoubleAnimation();
                da.TargetName = animTn;
                da.TargetProperty = animTp.Length > 0 ? animTp : "Opacity";
                string fAttr = ch.GetAttr("From");
                if (fAttr != null) { da.FromFloat = ParseUtil.ParseFloat(fAttr, 0.0f); da.HasFrom = true; }
                string tAttr = ch.GetAttr("To");
                if (tAttr != null) { da.ToFloat = ParseUtil.ParseFloat(tAttr, 1.0f); da.HasTo = true; }
                else { da.HasTo = false; }
                da.DurationSec = XamlStr.ParseDurationSec(ch.GetAttr("Duration"), 0.25f);
                da.DelaySec = XamlStr.ParseDurationSec(ch.GetAttr("BeginTime"), 0.0f);
                da.AutoReverse = ParseUtil.ParseBool(ch.GetAttr("AutoReverse"), false);
                da.RepeatCount = XamlStr.ParseRepeatCount(ch.GetAttr("RepeatBehavior"));
                for (int e = 0; e < ch.Children.Length; e += 1) {
                    if (XamlStr.EndsWith(ch.Children[e].LocalName, ".EasingFunction") && ch.Children[e].Children.Length > 0) {
                        da.EasingKind = ParseEasingKind(ch.Children[e].Children[0], da.EasingKind);
                        da.EasingMode = ParseEasingMode(ch.Children[e].Children[0], da.EasingMode);
                        da.EaseParam0 = ParseEasingParam0(ch.Children[e].Children[0]);
                        da.EaseParam1 = ParseEasingParam1(ch.Children[e].Children[0]);
                        da.EaseParamMask = ParseEasingParamMask(ch.Children[e].Children[0]);
                    }
                }
                sb.Add(da);
            } else if (tag == "ColorAnimation") {
                var ca = new ColorAnimation();
                ca.TargetName = animTn;
                ca.TargetProperty = animTp;
                string fAttr = ch.GetAttr("From");
                if (fAttr != null) {
                    ca.HasFrom = true;
                    ca.FromColor = XamlStr.LooksLikeMarkupExtension(fAttr)
                        ? DependencyObject.UnboxString(ResolveMarkupValue(fAttr, ctx, null), "#00000000")
                        : fAttr.Trim();
                }
                string tAttr = ch.GetAttr("To");
                if (tAttr != null) {
                    ca.ToColor = XamlStr.LooksLikeMarkupExtension(tAttr)
                        ? DependencyObject.UnboxString(ResolveMarkupValue(tAttr, ctx, null), "#FFFFFFFF")
                        : tAttr.Trim();
                }
                ca.DurationSec = XamlStr.ParseDurationSec(ch.GetAttr("Duration"), 0.25f);
                ca.DelaySec = XamlStr.ParseDurationSec(ch.GetAttr("BeginTime"), 0.0f);
                ca.AutoReverse = ParseUtil.ParseBool(ch.GetAttr("AutoReverse"), false);
                ca.RepeatCount = XamlStr.ParseRepeatCount(ch.GetAttr("RepeatBehavior"));
                sb.Add(ca);
            } else if (tag == "DoubleAnimationUsingKeyFrames") {
                var dakf = new DoubleAnimationUsingKeyFrames();
                dakf.TargetName = animTn;
                dakf.TargetProperty = animTp.Length > 0 ? animTp : "Opacity";
                dakf.DelaySec = XamlStr.ParseDurationSec(ch.GetAttr("BeginTime"), 0.0f);
                dakf.AutoReverse = ParseUtil.ParseBool(ch.GetAttr("AutoReverse"), false);
                dakf.RepeatCount = XamlStr.ParseRepeatCount(ch.GetAttr("RepeatBehavior"));
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    XamlXmlNode kf = ch.Children[k];
                    float kt = XamlStr.ParseKeyTimeSec(kf.GetAttr("KeyTime"));
                    string valAttr = kf.GetAttr("Value");
                    float kv = 0.0f;
                    Binding valBind = null;
                    if (valAttr != null) {
                        if (XamlStr.LooksLikeMarkupExtension(valAttr)) {
                            string vKind = XamlStr.ExtractExtType(valAttr);
                            if (vKind == "Binding" || vKind == "TemplateBinding") {
                                valBind = ToBindingObject(ParseBindingSpec(valAttr, ctx));
                            } else {
                                kv = DependencyObject.UnboxFloat(ResolveMarkupValue(valAttr, ctx, null), 0.0f);
                            }
                        } else {
                            kv = ParseUtil.ParseFloat(valAttr, 0.0f);
                        }
                    }
                    EasingFunctionKind ek = EasingFunctionKind.Linear;
                    EasingMode em = EasingMode.EaseOut;
                    float kfP0 = 0.0f;
                    float kfP1 = 0.0f;
                    int kfMask = 0;
                    for (int e = 0; e < kf.Children.Length; e += 1) {
                        if (XamlStr.EndsWith(kf.Children[e].LocalName, ".EasingFunction") && kf.Children[e].Children.Length > 0) {
                            ek = ParseEasingKind(kf.Children[e].Children[0], ek);
                            em = ParseEasingMode(kf.Children[e].Children[0], em);
                            kfP0 = ParseEasingParam0(kf.Children[e].Children[0]);
                            kfP1 = ParseEasingParam1(kf.Children[e].Children[0]);
                            kfMask = ParseEasingParamMask(kf.Children[e].Children[0]);
                        }
                    }
                    if (kf.LocalName == "SplineDoubleKeyFrame") {
                        ek = EasingFunctionKind.Spline;
                        string ksAttr = kf.GetAttr("KeySpline");
                        if (ksAttr != null && ksAttr.Trim().Length > 0) {
                            string[] ksParts = ksAttr.Trim().Replace(",", " ").Split(" ");
                            var ksNums = new float[0];
                            for (int sp = 0; sp < ksParts.Length; sp += 1) {
                                string spt = ksParts[sp].Trim();
                                if (spt.Length > 0) { ksNums.Push(ParseUtil.ParseFloat(spt, 0.0f)); }
                            }
                            if (ksNums.Length >= 4) {
                                float qx1 = (float)((int)(Math.Clampf(ksNums[0], 0.0f, 1.0f) * 4000.0f + 0.5f));
                                float qy1 = (float)((int)(Math.Clampf(ksNums[1], 0.0f, 1.0f) * 4000.0f + 0.5f));
                                float qx2 = (float)((int)(Math.Clampf(ksNums[2], 0.0f, 1.0f) * 4000.0f + 0.5f));
                                float qy2 = (float)((int)(Math.Clampf(ksNums[3], 0.0f, 1.0f) * 4000.0f + 0.5f));
                                kfP0 = qx1 * 4096.0f + qy1;
                                kfP1 = qx2 * 4096.0f + qy2;
                                kfMask = 16;
                            }
                        }
                    }
                    var edkf = new EasingDoubleKeyFrame(kv, kt, ek, em);
                    edkf.IsDiscrete = (kf.LocalName == "DiscreteDoubleKeyFrame");
                    edkf.EaseParam0 = kfP0;
                    edkf.EaseParam1 = kfP1;
                    edkf.EaseParamMask = kfMask;
                    if (valBind != null) { edkf.ValueBinding = valBind; }
                    dakf.AddKeyFrame(edkf);
                }
                sb.Add(dakf);
            } else if (tag == "ColorAnimationUsingKeyFrames") {
                string prevCol = "";
                float prevTime = 0.0f;
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    XamlXmlNode kf = ch.Children[k];
                    float kt = XamlStr.ParseKeyTimeSec(kf.GetAttr("KeyTime"));
                    string valAttr = kf.GetAttr("Value");
                    string col = valAttr == null ? "#FF000000" : (XamlStr.LooksLikeMarkupExtension(valAttr)
                        ? DependencyObject.UnboxString(ResolveMarkupValue(valAttr, ctx, null), "#FF000000")
                        : valAttr.Trim());
                    if (k == 0 && kt <= 0.0005f) {
                        if (ch.Children.Length == 1) {
                            var caZero = new ColorAnimation();
                            caZero.TargetName = animTn;
                            caZero.TargetProperty = animTp;
                            caZero.DelaySec = 0.0f;
                            caZero.DurationSec = 0.0f;
                            caZero.ToColor = col;
                            sb.Add(caZero);
                            break;
                        }
                        prevCol = col;
                        prevTime = kt;
                        continue;
                    }
                    var caSeg = new ColorAnimation();
                    caSeg.TargetName = animTn;
                    caSeg.TargetProperty = animTp;
                    caSeg.DelaySec = prevTime;
                    caSeg.DurationSec = Math.Maxf(0.001f, kt - prevTime);
                    caSeg.ToColor = col;
                    if (prevCol.Length > 0) { caSeg.FromColor = prevCol; caSeg.HasFrom = true; }
                    sb.Add(caSeg);
                    prevCol = col;
                    prevTime = kt;
                }
            } else if (tag == "ObjectAnimationUsingKeyFrames" || tag == "BooleanAnimationUsingKeyFrames") {
                for (int k = 0; k < ch.Children.Length; k += 1) {
                    XamlXmlNode kf = ch.Children[k];
                    if (kf.LocalName == "DiscreteObjectKeyFrame" || kf.LocalName == "DiscreteBooleanKeyFrame") {
                        float kt = XamlStr.ParseKeyTimeSec(kf.GetAttr("KeyTime"));
                        string vAttr = kf.GetAttr("Value");
                        DependencyObject vBox = null;
                        if (vAttr != null) {
                            vBox = XamlStr.LooksLikeMarkupExtension(vAttr)
                                ? ResolveMarkupValue(vAttr, ctx, null)
                                : CoerceLiteralForProperty(animTp, vAttr);
                        } else if (kf.Children.Length > 0) {
                            vBox = BuildObjectNode(kf.Children[0], ctx, null);
                        }
                        sb.AddDiscreteKeyFrame(animTn, animTp, kt, vBox);
                    }
                }
            } else if (tag == "StaticResource") {
                string rk = ch.GetAttr("ResourceKey");
                if (rk != null) {
                    DependencyObject resBox = ctx != null ? ctx.FindResourceBox(rk.Trim()) : null;
                    if (resBox is Storyboard) {
                        sb.MergeFrom(resBox as Storyboard);
                    }
                }
            }
        }
        return sb;
    }

    // ---- Behaviors & Interaction Triggers ----
    private static BehaviorTriggerBase BuildBehaviorTriggerNode(
        XamlXmlNode node, XamlBuildContext ctx, UIElement host) {
        string tag = node.LocalName;
        BehaviorTriggerBase trig = null;

        if (tag == "EventTrigger") {
            var bet = new BehaviorEventTrigger(node.GetAttr("EventName"));
            string sn = node.GetAttr("SourceName");
            if (sn != null) { bet.SourceName = sn; }
            string so = node.GetAttr("SourceObject");
            if (so != null && XamlStr.LooksLikeMarkupExtension(so)) {
                string kind = XamlStr.ExtractExtType(so);
                if (kind == "Binding") {
                    XamlParsedBinding pb = ParseBindingSpec(so, ctx);
                    if (pb.ElementName.Length > 0) { bet.SourceName = pb.ElementName; }
                }
            }
            trig = bet;
        } else if (tag == "DataTrigger") {
            var bdt = new BehaviorDataTrigger();
            string bindAttr = node.GetAttr("Binding");
            if (bindAttr != null && XamlStr.LooksLikeMarkupExtension(bindAttr)) {
                XamlParsedBinding pb = ParseBindingSpec(bindAttr, ctx);
                bdt.BindingPath = pb.Path;
                bdt.ElementName = pb.ElementName;
                bdt.RelativeSourceMode = pb.RelativeSourceMode;
                bdt.RelativeSourceAncestorType = pb.AncestorType;
            }
            string cmp = node.GetAttr("Comparison");
            if (cmp != null) { bdt.Comparison = cmp; }
            string v = node.GetAttr("Value");
            if (v != null) {
                if (XamlStr.LooksLikeMarkupExtension(v)) {
                    string vKind = XamlStr.ExtractExtType(v);
                    if (vKind == "Binding" || vKind == "TemplateBinding") {
                        bdt.ValueBinding = ToBindingObject(ParseBindingSpec(v, ctx));
                    } else {
                        bdt.SetValueBox(ResolveMarkupValue(v, ctx, host));
                    }
                } else {
                    bdt.SetValueBox(DependencyObject.BoxString(v));
                }
            }
            trig = bdt;
        } else if (tag == "PropertyChangedTrigger") {
            var pct = new PropertyChangedTrigger();
            string bindAttr = node.GetAttr("Binding");
            if (bindAttr != null && XamlStr.LooksLikeMarkupExtension(bindAttr)) {
                pct.Binding = ToBindingObject(ParseBindingSpec(bindAttr, ctx));
            }
            trig = pct;
        } else if (tag == "KeyTrigger") {
            var kt = new KeyTrigger();
            string keyAttr = node.GetAttr("Key");
            if (keyAttr != null) { kt.Key = EnumConvert.ParseKeyCode(keyAttr); }
            string modAttr = node.GetAttr("Modifiers");
            if (modAttr != null) { kt.Modifiers = KeyModifiers.Parse(modAttr); }
            trig = kt;
        } else if (tag == "StoryboardCompletedTrigger") {
            var sct = new StoryboardCompletedTrigger();
            string sbAttr = node.GetAttr("Storyboard");
            if (sbAttr != null && XamlStr.LooksLikeMarkupExtension(sbAttr)) {
                DependencyObject sbBox = ResolveMarkupValue(sbAttr, ctx, host);
                if (sbBox is Storyboard) { sct.Storyboard = sbBox as Storyboard; }
            }
            trig = sct;
        }

        if (trig == null) { return null; }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode actNode = node.Children[i];
            if (actNode.LocalName == "DataTrigger.Value" && trig is BehaviorDataTrigger) {
                if (actNode.Children.Length > 0 && actNode.Children[0].LocalName == "Binding") {
                    (trig as BehaviorDataTrigger).ValueBinding = ParseBindingElementNode(actNode.Children[0], ctx);
                } else if (actNode.Children.Length > 0) {
                    (trig as BehaviorDataTrigger).SetValueBox(BuildObjectNode(actNode.Children[0], ctx, host));
                } else if (actNode.Text.Trim().Length > 0) {
                    (trig as BehaviorDataTrigger).SetValueBox(DependencyObject.BoxString(actNode.Text.Trim()));
                }
                continue;
            }
            if (actNode.LocalName == "Interaction.Behaviors" || XamlStr.EndsWith(actNode.LocalName, ".Behaviors")) {
                for (int b = 0; b < actNode.Children.Length; b += 1) {
                    if (actNode.Children[b].LocalName == "ConditionBehavior") {
                        AddConditionsFromBehaviorNode(actNode.Children[b], trig, ctx, host);
                    }
                }
                continue;
            }
            TriggerAction act = BuildTriggerActionNode(actNode, ctx, host);
            if (act != null) { trig.AddAction(act); }
        }
        return trig;
    }

    private static void AddConditionsFromBehaviorNode(
        XamlXmlNode node, BehaviorTriggerBase trig, XamlBuildContext ctx, UIElement host) {
        if (node == null || trig == null) { return; }
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "ConditionalExpression" || XamlStr.EndsWith(ch.LocalName, ".Condition")) {
                XamlXmlNode exprNode = ch.LocalName == "ConditionalExpression" ? ch :
                    (ch.Children.Length > 0 ? ch.Children[0] : null);
                if (exprNode == null) { continue; }
                for (int c = 0; c < exprNode.Children.Length; c += 1) {
                    XamlXmlNode ccNode = exprNode.Children[c];
                    if (ccNode.LocalName == "ComparisonCondition") {
                        string leftAttr = ccNode.GetAttr("LeftOperand");
                        string opAttr = ccNode.GetAttr("Operator");
                        string rightAttr = ccNode.GetAttr("RightOperand");
                        string path = leftAttr == null ? "" : leftAttr;
                        string elemName = "";
                        if (leftAttr != null && XamlStr.LooksLikeMarkupExtension(leftAttr)) {
                            XamlParsedBinding spec = ParseBindingSpec(leftAttr, ctx);
                            path = spec.Path;
                            elemName = spec.ElementName;
                        }
                        DependencyObject rBox = rightAttr != null
                            ? (XamlStr.LooksLikeMarkupExtension(rightAttr) ? ResolveMarkupValue(rightAttr, ctx, host) : DependencyObject.BoxString(rightAttr))
                            : null;
                        trig.AddCondition(new ComparisonCondition(path, opAttr == null ? "Equal" : opAttr, rBox, elemName));
                    }
                }
            }
        }
    }

    private static void ApplyActionTargetAttrs(TriggerAction act, XamlXmlNode node, XamlBuildContext ctx) {
        if (act == null || node == null) { return; }
        string tn = node.GetAttr("TargetName");
        if (tn != null) { act.TargetName = tn; }
        string toAttr = node.GetAttr("TargetObject");
        if (toAttr != null && XamlStr.LooksLikeMarkupExtension(toAttr)) {
            string kind = XamlStr.ExtractExtType(toAttr);
            if (kind == "Binding" || kind == "TemplateBinding") {
                XamlParsedBinding pb = ParseBindingSpec(toAttr, ctx);
                string effPath = pb.Path.Length > 0 ? pb.Path : ".";
                act.SetTargetObjectBinding(effPath, pb.RelativeSourceMode, pb.AncestorType, pb.ElementName);
            }
        }
    }

    private static TriggerAction BuildTriggerActionNode(
        XamlXmlNode node, XamlBuildContext ctx, UIElement host) {
        string tag = node.LocalName;
        if (tag == "ChangePropertyAction") {
            var act = new ChangePropertyAction();
            string pn = node.GetAttr("PropertyName");
            if (pn != null) { act.PropertyName = XamlStr.StripOuterParens(pn); }
            ApplyActionTargetAttrs(act, node, ctx);
            string an = node.GetNameAttr();
            if (an.Length > 0) { act.ActionName = an; }
            string valAttr = node.GetAttr("Value");
            if (valAttr != null) {
                if (XamlStr.LooksLikeMarkupExtension(valAttr)) {
                    string kind = XamlStr.ExtractExtType(valAttr);
                    if (kind == "Binding" || kind == "TemplateBinding") {
                        act.ValueBinding = ToBindingObject(ParseBindingSpec(valAttr, ctx));
                    } else {
                        act.SetValueBox(ResolveMarkupValue(valAttr, ctx, host));
                    }
                } else {
                    act.SetValueBox(DependencyObject.BoxString(valAttr));
                }
            }
            return act;
        }
        if (tag == "ControlStoryboardAction") {
            var csa = new ControlStoryboardAction();
            string sbAttr = node.GetAttr("Storyboard");
            if (sbAttr != null && XamlStr.LooksLikeMarkupExtension(sbAttr)) {
                DependencyObject sbBox = ResolveMarkupValue(sbAttr, ctx, host);
                if (sbBox is Storyboard) { csa.Storyboard = sbBox as Storyboard; }
            }
            string opt = node.GetAttr("ControlStoryboardOption");
            if (opt != null) { csa.ControlStoryboardOption = opt; }
            return csa;
        }
        if (tag == "InvokeCommandAction") {
            var ica = new InvokeCommandAction();
            ApplyActionTargetAttrs(ica, node, ctx);
            string cmdAttr = node.GetAttr("Command");
            if (cmdAttr != null && XamlStr.LooksLikeMarkupExtension(cmdAttr)) {
                XamlParsedBinding cpb = ParseBindingSpec(cmdAttr, ctx);
                ica.CommandPath = cpb.Path;
            }
            string cpAttr = node.GetAttr("CommandParameter");
            if (cpAttr != null) {
                if (XamlStr.LooksLikeMarkupExtension(cpAttr)) {
                    string cpKind = XamlStr.ExtractExtType(cpAttr);
                    if (cpKind == "Binding" || cpKind == "TemplateBinding") {
                        XamlParsedBinding ppb = ParseBindingSpec(cpAttr, ctx);
                        ica.CommandParameterPath = ppb.Path.Length > 0 ? ppb.Path : ".";
                        ica.CommandParameterElementName = ppb.ElementName;
                        ica.CommandParameterRelativeMode = ppb.RelativeSourceMode;
                        ica.CommandParameterAncestorType = ppb.AncestorType;
                    } else {
                        ica.SetCommandParameterBox(ResolveMarkupValue(cpAttr, ctx, host));
                    }
                } else {
                    ica.SetCommandParameter(cpAttr);
                }
            }
            string pea = node.GetAttr("PassEventArgsToCommand");
            if (pea != null) { ica.PassEventArgsToCommand = ParseUtil.ParseBool(pea, false); }
            return ica;
        }
        if (tag == "PlaySoundAction") {
            var psa = new PlaySoundAction();
            string srcAttr = node.GetAttr("Source");
            if (srcAttr != null) { psa.Source = srcAttr; }
            string volAttr = node.GetAttr("Volume");
            if (volAttr != null) { psa.Volume = ParseUtil.ParseFloat(volAttr, 1.0f); }
            return psa;
        }
        if (tag == "RemoveElementAction") {
            var rea = new RemoveElementAction();
            ApplyActionTargetAttrs(rea, node, ctx);
            return rea;
        }
        if (tag == "GoToStateAction") {
            var gts = new GoToStateAction();
            string sn = node.GetAttr("StateName");
            if (sn != null) { gts.StateName = sn; }
            ApplyActionTargetAttrs(gts, node, ctx);
            string ut = node.GetAttr("UseTransitions");
            if (ut != null) { gts.UseTransitions = ParseUtil.ParseBool(ut, true); }
            return gts;
        }
        if (tag == "SetFocusAction") {
            var sfa = new SetFocusAction();
            ApplyActionTargetAttrs(sfa, node, ctx);
            return sfa;
        }
        return null;
    }

    private static BehaviorBase BuildBehaviorNode(
        XamlXmlNode node, XamlBuildContext ctx, UIElement host) {
        string tag = node.LocalName;
        if (tag == "MouseDragElementBehavior") {
            var mdb = new MouseDragElementBehavior();
            string c = node.GetAttr("ConstrainToParentBounds");
            if (c != null) { mdb.ConstrainToParentBounds = ParseUtil.ParseBool(c, false); }
            return mdb;
        }
        if (tag == "TranslateZoomRotateBehavior") {
            var tzb = new TranslateZoomRotateBehavior();
            string sg = node.GetAttr("SupportedGestures");
            if (sg != null) { tzb.SupportedGestures = sg; }
            return tzb;
        }
        if (tag == "BackgroundEffectBehavior") {
            var beb = new BackgroundEffectBehavior();
            if (node.Children.Length > 0) {
                DependencyObject effBox = BuildObjectNode(node.Children[0], ctx, host);
                if (effBox is Effect) { beb.Effect = effBox as Effect; }
            }
            return beb;
        }
        return null;
    }
}
