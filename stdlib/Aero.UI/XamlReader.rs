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
            if (rd != null && rd.Contains(key)) { return rd.GetBox(key); }
        }
        if (targetEl != null) {
            DependencyObject local = targetEl.FindResourceBox(key);
            if (local != null) { return local; }
        }
        DependencyObject amb = ResourceDictionary.FindAmbientBox(key);
        if (amb != null) { return amb; }
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        if (appRes != null && appRes.Contains(key)) { return appRes.GetBox(key); }
        return null;
    }
}

internal static class XamlStr {
    public static bool StartsWith(string s, string prefix) {
        if (s == null || prefix == null) { return false; }
        if (prefix.Length > s.Length) { return false; }
        return s.Substring(0, prefix.Length) == prefix;
    }

    public static bool EndsWith(string s, string suffix) {
        if (s == null || suffix == null) { return false; }
        if (suffix.Length > s.Length) { return false; }
        return s.EndsWith(suffix);
    }

    public static int FindFrom(string s, string needle, int startIdx) {
        if (s == null || needle == null || needle.Length == 0) { return -1; }
        int max = s.Length - needle.Length;
        int i = startIdx < 0 ? 0 : startIdx;
        while (i <= max) {
            if (s.Substring(i, needle.Length) == needle) { return i; }
            i += 1;
        }
        return -1;
    }

    public static int LastIndexOfChar(string s, string ch) {
        if (s == null || ch == null || ch.Length != 1) { return -1; }
        for (int i = s.Length - 1; i >= 0; i -= 1) {
            if (s.Substring(i, 1) == ch) { return i; }
        }
        return -1;
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

        if (s.Contains("GradientStops") && (s.Contains("Shape.Fill") || s.Contains("Panel.Background"))) {
            string prefix = s.Contains("GradientStops") && s.Contains("Shape.Stroke") ? "Stroke" : "Fill";
            string suffix = s.Contains("Offset") ? "Offset" : "Color";
            if (s.Contains("[0]")) { return prefix + ".Stop0" + suffix; }
            if (s.Contains("[1]")) { return prefix + ".Stop1" + suffix; }
            if (s.Contains("[2]")) { return prefix + ".Stop2" + suffix; }
            if (s.Contains("[3]")) { return prefix + ".Stop3" + suffix; }
        }
        if (s.Contains("LayoutTransform")) {
            if (s.Contains(".ScaleX")) { return "LayoutTransform.ScaleX"; }
            if (s.Contains(".ScaleY")) { return "LayoutTransform.ScaleY"; }
            if (s.Contains(".AngleX") || s.Contains(".SkewX")) { return "LayoutTransform.SkewX"; }
            if (s.Contains(".AngleY") || s.Contains(".SkewY")) { return "LayoutTransform.SkewY"; }
            if (s.Contains(".Angle") || s.Contains(".Rotation")) { return "LayoutTransform.Angle"; }
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

    public static string DecodeXmlText(string raw) {
        if (raw == null || !raw.Contains("&")) { return raw == null ? "" : raw; }
        string s = raw;
        s = s.Replace("&lt;", "<");
        s = s.Replace("&gt;", ">");
        s = s.Replace("&quot;", "\"");
        s = s.Replace("&apos;", "'");
        s = s.Replace("&#x0A;", "\n");
        s = s.Replace("&#x0D;", "\r");
        s = s.Replace("&#10;", "\n");
        s = s.Replace("&#13;", "\r");
        s = s.Replace("&#9;", "\t");
        s = s.Replace("&amp;", "&");
        return s;
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
        var res = new string[0];
        if (text == null) { return res; }
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
                sec = ParseUtil.ParseFloat(parts[0], 0.0f) * 3600.0f +
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
        int sp = 0;
        while (sp < inner.Length && !IsSpace(inner.Substring(sp, 1))) { sp += 1; }
        return inner.Substring(0, sp);
    }

    public static string ExtractExtBody(string extText) {
        if (extText == null) { return ""; }
        string t = extText.Trim();
        if (t.Length < 2) { return ""; }
        string inner = t.Substring(1, t.Length - 2).Trim();
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

    public static XamlXmlNode ParseDocument(string xmlText) {
        if (xmlText == null) { return null; }
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
                    string trailing = XamlStr.TrimPreserveNewline(cur.Substring(endCom + 3));
                    if (trailing.Length > 0) {
                        XamlXmlNode parent = stack[stack.Length - 1];
                        parent.Text = parent.Text + trailing;
                    }
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
                        string tr = XamlStr.TrimPreserveNewline(cur.Substring(endCd + 3));
                        if (tr.Length > 0) { parent.Text = parent.Text + tr; }
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
                if (stack.Length > 0) {
                    string tr = XamlStr.TrimPreserveNewline(after);
                    if (tr.Length > 0) {
                        XamlXmlNode curParent = stack[stack.Length - 1];
                        curParent.Text = curParent.Text + tr;
                    }
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
                string innerText = XamlStr.TrimPreserveNewline(after);
                if (innerText.Length > 0) {
                    node.Text = node.Text + innerText;
                }
                stack.Push(node);
            } else if (stack.Length > 0) {
                string tr = XamlStr.TrimPreserveNewline(after);
                if (tr.Length > 0) {
                    XamlXmlNode curParent = stack[stack.Length - 1];
                    curParent.Text = curParent.Text + tr;
                }
            }
        }
        return root;
    }
}

public static class XamlReader {
    public static UIElement LoadUri(string uri, ResourceDictionary fallbackResources = null) {
        if (uri == null || uri.Trim().Length == 0) { return null; }
        string normUri = uri.Trim();
        string text = Host_ReadTextAsset(normUri);
        if (text == null || text.Trim().Length == 0) { return null; }
        return ParseWithUri(text, fallbackResources, normUri);
    }

    public static UIElement Parse(string xamlText, ResourceDictionary fallbackResources = null) {
        return ParseWithUri(xamlText, fallbackResources, "");
    }

    public static ResourceDictionary LoadResourceDictionary(string uri) {
        if (uri == null || uri.Trim().Length == 0) { return new ResourceDictionary(); }
        string normUri = uri.Trim();
        string text = Host_ReadTextAsset(normUri);
        if (text == null || text.Trim().Length == 0) { return new ResourceDictionary(); }
        XamlXmlNode root = XamlDomParser.ParseDocument(text);
        if (root == null) { return new ResourceDictionary(); }
        var ctx = new XamlBuildContext(null, normUri);
        return BuildResourceDictionaryNode(root, ctx);
    }

    private static UIElement ParseWithUri(string xamlText, ResourceDictionary fallbackResources, string baseUri) {
        if (xamlText == null || xamlText.Trim().Length == 0) { return null; }
        XamlXmlNode root = XamlDomParser.ParseDocument(xamlText);
        if (root == null) { return null; }
        var ctx = new XamlBuildContext(fallbackResources, baseUri);
        if (root.LocalName == "ResourceDictionary") {
            ResourceDictionary rd = BuildResourceDictionaryNode(root, ctx);
            ResourceDictionary.GlobalApplicationResources().Merge(rd);
            return null;
        }
        DependencyObject obj = BuildObjectNode(root, ctx, null);
        UIElement el = obj as UIElement;
        if (el == null) { return null; }
        if (fallbackResources != null && el.AmbientResources == null) {
            el.AmbientResources = fallbackResources;
        }
        el.ApplyStyleSubtree();
        el.RefreshDynamicResourcesRecursive();
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
            string key = UnwrapTypeSpec(body);
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
            string low = val.Trim().ToLower();
            if (low == "auto" || low == "nan") { return DependencyObject.BoxFloat(ParseUtil.NaN()); }
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(val, ParseUtil.NaN()));
        }
        return DependencyObject.BoxString(val);
    }

    private static void ApplyPropertyValueOrExt(
        UIElement el, string rawPropName, string rawVal, XamlBuildContext ctx) {
        if (el == null || rawPropName == null || rawPropName.Length == 0) { return; }
        string propName = XamlStr.StripOuterParens(rawPropName);

        if (XamlStr.LooksLikeMarkupExtension(rawVal)) {
            string kind = XamlStr.ExtractExtType(rawVal);
            string body = XamlStr.ExtractExtBody(rawVal);
            if (kind == "DynamicResource") {
                el.SetResourceReference(propName, UnwrapTypeSpec(body));
                return;
            }
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

        ApplyLiteralProperty(el, propName, rawVal, ctx);
    }

    private static void ApplyLiteralProperty(
        UIElement el, string propName, string rawVal, XamlBuildContext ctx) {
        string val = rawVal == null ? "" : rawVal;
        if (XamlStr.StartsWith(val, "{}")) { val = val.Substring(2); }

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
                tb.TextWrapping = val.Trim() == "Wrap" ? TextWrapping.Wrap : TextWrapping.NoWrap;
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
        if (propName == "ImageSource" && el is Image) {
            (el as Image).Source = val;
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

    private static bool IsIgnoredAttributeName(string attrName, string localName) {
        if (attrName == "xmlns" || XamlStr.StartsWith(attrName, "xmlns:") ||
            XamlStr.StartsWith(attrName, "mc:") || XamlStr.StartsWith(attrName, "d:") ||
            attrName == "x:Class" || attrName == "x:Key" || attrName == "x:Shared" ||
            attrName == "x:DataType" || attrName == "TargetType" || attrName == "DataType" ||
            attrName == "BasedOn" || attrName == "Source") {
            return true;
        }
        if (attrName == "x:Name" || attrName == "Name") { return false; }
        return attrName == "SnapsToDevicePixels" || attrName == "UseLayoutRounding";
    }

    private static UIElement CreateUIElementByTag(string localName) {
        if (localName == "Grid") { return new Grid(); }
        if (localName == "StackPanel") { return new StackPanel(); }
        if (localName == "VirtualizingStackPanel") { return new VirtualizingStackPanel(); }
        if (localName == "TabPanel") { return new TabPanel(); }
        if (localName == "ToolBarPanel") { return new ToolBarPanel(); }
        if (localName == "DockPanel") { return new DockPanel(); }
        if (localName == "WrapPanel") { return new WrapPanel(); }
        if (localName == "ToolBarOverflowPanel") { return new ToolBarOverflowPanel(); }
        if (localName == "UniformGrid") { return new UniformGrid(); }
        if (localName == "Canvas") { return new Canvas(); }
        if (localName == "Panel") { return new Panel(); }

        if (localName == "Border") { return new Border(); }
        if (localName == "Decorator") { return new Decorator(); }
        if (localName == "BoxShadow") { return new BoxShadow(); }
        if (localName == "Viewbox") { return new Viewbox(); }
        if (localName == "ScrollViewer") { return new ScrollViewer(); }
        if (localName == "ScrollContentPresenter") { return new ScrollContentPresenter(); }
        if (localName == "ContentControl") { return new ContentControl(); }
        if (localName == "ContentPresenter" || localName == "TransitionContentPresenter") { return new ContentPresenter(); }
        if (localName == "Label") { return new Label(); }
        if (localName == "HeaderedContentControl") { return new HeaderedContentControl(); }
        if (localName == "GroupBox") { return new GroupBox(); }
        if (localName == "Expander") { return new Expander(); }
        if (localName == "Separator") { return new Separator(); }
        if (localName == "UserControl") { return new UserControl(); }
        if (localName == "Page") { return new Page(); }
        if (localName == "Window") { return new Window(); }

        if (localName == "TextBlock" || localName == "RichText") { return new TextBlock(); }
        if (localName == "TextBox") { return new TextBox(); }
        if (localName == "PasswordBox") { return new PasswordBox(); }

        if (localName == "Button") { return new Button(); }
        if (localName == "RepeatButton") { return new RepeatButton(); }
        if (localName == "ToggleButton") { return new ToggleButton(); }
        if (localName == "CheckBox") { return new CheckBox(); }
        if (localName == "RadioButton") { return new RadioButton(); }
        if (localName == "Hyperlink") { return new Hyperlink(); }
        if (localName == "Slider") { return new Slider(); }
        if (localName == "ProgressBar") { return new ProgressBar(); }
        if (localName == "ScrollBar") { return new ScrollBar(); }
        if (localName == "Track") { return new Track(); }
        if (localName == "Thumb") { return new Thumb(); }
        if (localName == "TickBar") { return new TickBar(); }
        if (localName == "GridSplitter") { return new GridSplitter(); }

        if (localName == "ItemsControl") { return new ItemsControl(); }
        if (localName == "ItemsPresenter") { return new ItemsPresenter(); }
        if (localName == "ListBox") { return new ListBox(); }
        if (localName == "ListBoxItem") { return new ListBoxItem(); }
        if (localName == "ListView") { return new ListView(); }
        if (localName == "ListViewItem") { return new ListViewItem(); }
        if (localName == "ComboBox") { return new ComboBox(); }
        if (localName == "ComboBoxItem") { return new ComboBoxItem(); }
        if (localName == "TabControl") { return new TabControl(); }
        if (localName == "TabItem") { return new TabItem(); }
        if (localName == "TreeView") { return new TreeView(); }
        if (localName == "TreeViewItem") { return new TreeViewItem(); }
        if (localName == "Menu") { return new Menu(); }
        if (localName == "MenuItem") { return new MenuItem(); }
        if (localName == "ContextMenu") { return new ContextMenu(); }
        if (localName == "ToolBar") { return new ToolBar(); }
        if (localName == "StatusBar") { return new StatusBar(); }
        if (localName == "StatusBarItem") { return new StatusBarItem(); }
        if (localName == "ToolTip") { return new ToolTip(); }
        if (localName == "Popup") { return new Popup(); }
        if (localName == "GridViewHeaderRowPresenter") { return new GridViewHeaderRowPresenter(); }
        if (localName == "GridViewRowPresenter") { return new GridViewRowPresenter(); }

        if (localName == "Image") { return new Image(); }
        if (localName == "Rectangle") { return new Rectangle(); }
        if (localName == "Ellipse") { return new Ellipse(); }
        if (localName == "Line") { return new Line(); }
        if (localName == "Path") { return new Path(); }
        if (localName == "Geometry") { return new Geometry(); }
        if (localName == "Polygon") { return new Polygon(); }
        if (localName == "Polyline") { return new Polyline(); }
        if (localName == "CustomDrawElement") { return new CustomDrawElement(); }
        return null;
    }

    private static DependencyObject BuildNonVisualObjectNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        string tag = node.LocalName;

        if (tag == "ResourceDictionary") { return BuildResourceDictionaryNode(node, ctx); }
        if (tag == "Style") { return BuildStyleNode(node, ctx); }
        if (tag == "ControlTemplate") { return BuildControlTemplateNode(node, ctx); }
        if (tag == "HierarchicalDataTemplate") { return BuildHierarchicalDataTemplateNode(node, ctx); }
        if (tag == "DataTemplate") { return BuildDataTemplateNode(node, ctx); }
        if (tag == "ItemsPanelTemplate") { return BuildItemsPanelTemplateNode(node, ctx); }
        if (tag == "Storyboard") { return BuildStoryboardNode(node, ctx); }
        if (tag == "SolidColorBrush") { return BuildSolidColorBrushNode(node, ctx); }
        if (tag == "LinearGradientBrush") { return BuildLinearGradientBrushNode(node, ctx); }
        if (tag == "RadialGradientBrush") { return BuildRadialGradientBrushNode(node, ctx); }
        if (tag == "ImageBrush") { return BuildImageBrushNode(node, ctx); }
        if (tag == "VisualBrush") { return Brushes.Transparent(); }
        if (tag == "Color") {
            string c = node.Text.Trim();
            if (c.Length == 0) { c = "#00000000"; }
            return DependencyObject.BoxString(ParseUtil.FormatArgbHex(ParseUtil.ParseColor(c)));
        }
        if (tag == "Double" || tag == "sys:Double") {
            return DependencyObject.BoxFloat(ParseUtil.ParseFloat(node.Text.Trim(), 0.0f));
        }
        if (tag == "String" || tag == "sys:String" || tag == "FontFamily") {
            return DependencyObject.BoxString(node.Text.Trim());
        }
        if (tag == "Boolean" || tag == "sys:Boolean") {
            return DependencyObject.BoxBool(ParseUtil.ParseBool(node.Text.Trim(), false));
        }
        if (tag == "Int32" || tag == "sys:Int32") {
            return DependencyObject.BoxInt(ParseUtil.ParseInt(node.Text.Trim(), 0));
        }
        if (tag == "Thickness") {
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
        if (tag == "CornerRadius") {
            return CornerRadius.Parse(node.Text.Trim());
        }
        if (tag == "TranslateTransform" || tag == "ScaleTransform" || tag == "RotateTransform" ||
            tag == "SkewTransform" || tag == "TransformGroup") {
            return BuildTransformNode(node, ctx);
        }
        if (tag == "BlurEffect" || tag == "DropShadowEffect" || tag == "BackdropBlurEffect" ||
            tag == "TintEffect" || tag == "PixelateEffect" || tag == "ChromaticAberrationEffect" ||
            tag == "DirectionalBlurEffect" || tag == "SaturationEffect") {
            return BuildEffectNode(node, ctx, contextHost);
        }
        if (tag == "BoolToVisibilityConverter" || tag == "BooleanToVisibilityConverter") {
            return new BoolToVisibilityConverter();
        }
        if (tag == "InverseBoolConverter") { return new InverseBoolConverter(); }
        if (tag == "NullToVisibilityConverter") { return new NullToVisibilityConverter(); }
        if (tag == "MathConverter") { return new MathConverter(); }
        if (tag == "AlternationConverter") {
            var ac = new AlternationConverter();
            for (int i = 0; i < node.Children.Length; i += 1) {
                DependencyObject item = BuildObjectNode(node.Children[i], ctx, contextHost);
                if (item != null) { ac.AddValue(item); }
            }
            return ac;
        }
        if (tag == "GridView") {
            var gv = new GridView();
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (ch.LocalName == "GridViewColumn") {
                    var col = new GridViewColumn();
                    string hdr = ch.GetAttr("Header");
                    if (hdr != null) { col.Header = hdr; }
                    string w = ch.GetAttr("Width");
                    if (w != null) { col.Width = ParseUtil.ParseFloat(w, 120.0f); }
                    string dmb = ch.GetAttr("DisplayMemberBinding");
                    if (dmb != null && XamlStr.LooksLikeMarkupExtension(dmb)) {
                        col.DisplayMemberBinding = ToBindingObject(ParseBindingSpec(dmb, ctx));
                    }
                    gv.AddColumn(col);
                }
            }
            return gv;
        }
        if (tag == "StaticResource") {
            string rk = node.GetAttr("ResourceKey");
            if (rk == null) { rk = node.Text.Trim(); }
            return ctx != null ? ctx.FindResourceBox(UnwrapTypeSpec(rk), contextHost) : null;
        }
        return null;
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
                PopulateResourceDictionaryFromChildren(ch, el.Resources, ctx);
                ctx.PushScope(el.Resources);
            }
        }

        for (int i = 0; i < node.Attrs.Length; i += 1) {
            XamlXmlAttr a = node.Attrs[i];
            if (IsIgnoredAttributeName(a.Name, a.LocalName)) { continue; }
            if (a.Name == "x:Name" || a.Name == "Name") {
                el.Name = a.Value;
                continue;
            }
            ApplyPropertyValueOrExt(el, a.Name, a.Value, ctx);
        }

        var textBlockInlines = new Inline[0];
        bool isTb = el is TextBlock;
        if (isTb && node.Text.Length > 0 && node.Children.Length > 0) {
            textBlockInlines.Push(new Run(node.Text));
        }

        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (XamlStr.EndsWith(ch.LocalName, ".Resources")) { continue; }
            if (ch.RawName.Contains(".")) {
                ApplyPropertyElement(el, ch, ctx);
                continue;
            }
            if (isTb && (ch.LocalName == "Run" || ch.LocalName == "LineBreak" ||
                ch.LocalName == "Bold" || ch.LocalName == "Italic" ||
                ch.LocalName == "Underline" || ch.LocalName == "Span")) {
                AppendInlineNode(ch, textBlockInlines, el as TextBlock, ctx);
                continue;
            }
            DependencyObject childObj = BuildObjectNode(ch, ctx, el);
            AttachChildObject(el, childObj);
        }

        if (isTb) {
            TextBlock tb = el as TextBlock;
            if (textBlockInlines.Length > 0) {
                for (int i = 0; i < textBlockInlines.Length; i += 1) {
                    tb.AddInline(textBlockInlines[i]);
                }
            } else if (node.Text.Length > 0 && tb.Text.Length == 0 && node.GetAttr("Text") == null && node.GetAttr("noesis:RichText.Text") == null) {
                tb.Text = node.Text.Trim();
            }
        } else if (node.Text.Length > 0) {
            if (el is ContentControl && (el as ContentControl).ContentBox == null && node.GetAttr("Content") == null) {
                (el as ContentControl).SetContentBox(DependencyObject.BoxString(node.Text.Trim()));
            } else if (el is TextBox && (el as TextBox).Text.Length == 0 && node.GetAttr("Text") == null) {
                (el as TextBox).Text = node.Text.Trim();
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

    private static void AppendInlineNode(
        XamlXmlNode node, Inline[] list, TextBlock ownerTb, XamlBuildContext ctx) {
        string tag = node.LocalName;
        if (tag == "LineBreak") {
            list.Push(new LineBreak());
            return;
        }
        var run = new Run(node.Text);
        if (tag == "Bold") { run.FontWeight = "Bold"; }
        else if (tag == "Italic") { run.FontStyle = "Italic"; }
        else if (tag == "Underline") { run.Decorations = 1; }

        for (int i = 0; i < node.Attrs.Length; i += 1) {
            XamlXmlAttr a = node.Attrs[i];
            if (IsIgnoredAttributeName(a.Name, a.LocalName)) { continue; }
            if (a.Name == "Text" || a.Name == "Foreground" || a.Name == "FontSize" ||
                a.Name == "FontFamily" || a.Name == "FontWeight" || a.Name == "FontStyle" ||
                a.Name == "TextDecorations") {
                if (XamlStr.LooksLikeMarkupExtension(a.Value)) {
                    string kind = XamlStr.ExtractExtType(a.Value);
                    if (kind == "Binding") {
                        XamlParsedBinding pb = ParseBindingSpec(a.Value, ctx);
                        BindingEngine.BindInlineEx(
                            run, a.Name, pb.Path, pb.ElementName, pb.Mode,
                            pb.Converter, pb.ConverterKey, pb.ConverterParameter, pb.StringFormat,
                            pb.HasFallback, pb.FallbackBox, pb.HasTargetNull, pb.TargetNullBox,
                            pb.RelativeSourceMode, pb.AncestorType);
                    } else {
                        DependencyObject box = ResolveMarkupValue(a.Value, ctx, ownerTb);
                        run.SetPropertyValue(a.Name, box);
                    }
                } else {
                    if (a.Name == "Foreground") { run.Foreground = new SolidColorBrush(a.Value); }
                    else { run.SetPropertyValue(a.Name, DependencyObject.BoxString(a.Value)); }
                }
            }
        }
        if (run.Text.Length > 0 || node.Children.Length == 0) {
            list.Push(run);
        }
        for (int i = 0; i < node.Children.Length; i += 1) {
            AppendInlineNode(node.Children[i], list, ownerTb, ctx);
        }
    }

    private static void ApplyPropertyElement(UIElement el, XamlXmlNode propNode, XamlBuildContext ctx) {
        string raw = propNode.RawName;
        string local = propNode.LocalName;
        int dot = XamlStr.LastIndexOfChar(local, ".");
        string propName = dot >= 0 ? local.Substring(dot + 1) : local;

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
                    g.RowDefinitions.Add(new RowDefinition(gl));
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
                    g.ColumnDefinitions.Add(new ColumnDefinition(gl));
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
        if (propNode.Children.Length == 1) {
            XamlXmlNode childXml = propNode.Children[0];
            DependencyObject valBox = BuildObjectNode(childXml, ctx, el);
            ApplyResolvedPropertyBox(el, propName, valBox, ctx);
            if (childXml.LocalName == "SolidColorBrush" && IsBrushPropertyName(propName)) {
                string colAttr = childXml.GetAttr("Color");
                if (colAttr != null && XamlStr.LooksLikeMarkupExtension(colAttr)) {
                    string cKind = XamlStr.ExtractExtType(colAttr);
                    if (cKind == "Binding" || cKind == "TemplateBinding") {
                        BindElementFromSpec(el, propName + ".Color", ParseBindingSpec(colAttr, ctx), ctx);
                    }
                }
            }
            return;
        }
        if (propNode.Text.Length > 0) {
            ApplyPropertyValueOrExt(el, propName, propNode.Text.Trim(), ctx);
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
                                    rd.Merge(subDict);
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
            string key = ch.GetKeyAttr();
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

    private static void ParseGradientStopsInto(GradientBrush gb, XamlXmlNode node, XamlBuildContext ctx) {
        for (int i = 0; i < node.Children.Length; i += 1) {
            XamlXmlNode ch = node.Children[i];
            if (ch.LocalName == "GradientStop") {
                string cAttr = ch.GetAttr("Color");
                string oAttr = ch.GetAttr("Offset");
                string col = "#FF000000";
                if (cAttr != null) {
                    if (XamlStr.LooksLikeMarkupExtension(cAttr)) {
                        col = DependencyObject.UnboxString(ResolveMarkupValue(cAttr, ctx, null), "#FF000000");
                    } else {
                        col = cAttr.Trim();
                    }
                }
                float off = oAttr != null ? ParseUtil.ParseFloat(oAttr, 0.0f) : 0.0f;
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

    private static Brush BuildImageBrushNode(XamlXmlNode node, XamlBuildContext ctx) {
        string src = node.GetAttr("ImageSource");
        if (src == null) { src = ""; }
        if (src.Length == 0) {
            for (int i = 0; i < node.Children.Length; i += 1) {
                XamlXmlNode ch = node.Children[i];
                if (XamlStr.EndsWith(ch.LocalName, ".Shader") && ch.Children.Length > 0) {
                    string shaderKind = ch.Children[0].LocalName;
                    if (shaderKind == "PlasmaBrush") {
                        var lg = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);
                        lg.AddStop("#FFFF2E93", 0.0f);
                        lg.AddStop("#FF7C3AED", 0.5f);
                        lg.AddStop("#FF06B6D4", 1.0f);
                        return lg;
                    }
                    if (shaderKind == "ConicGradientBrush") {
                        var lg = new LinearGradientBrush(0.0f, 0.5f, 1.0f, 0.5f);
                        lg.AddStop("#FFEF4444", 0.0f);
                        lg.AddStop("#FFF59E0B", 0.33f);
                        lg.AddStop("#FF10B981", 0.66f);
                        lg.AddStop("#FF3B82F6", 1.0f);
                        return lg;
                    }
                    var sat = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 0.0f);
                    sat.AddStop("#FF334155", 0.0f);
                    sat.AddStop("#FF38BDF8", 1.0f);
                    return sat;
                }
            }
        }
        var ib = new ImageBrush(src);
        string st = node.GetAttr("Stretch");
        if (st != null) {
            string sl = st.Trim();
            if (sl == "None") { ib.Stretch = Stretch.None; }
            else if (sl == "Fill") { ib.Stretch = Stretch.Fill; }
            else if (sl == "UniformToFill") { ib.Stretch = Stretch.UniformToFill; }
            else { ib.Stretch = Stretch.Uniform; }
        }
        string vb = node.GetAttr("Viewbox");
        if (vb != null) { ib.Viewbox = vb; }
        string op = node.GetAttr("Opacity");
        if (op != null) { ib.Opacity = ParseUtil.ParseFloat(op, 1.0f); }
        return ib;
    }

    private static Transform BuildTransformNode(XamlXmlNode node, XamlBuildContext ctx) {
        string tag = node.LocalName;
        if (tag == "TranslateTransform") {
            return new TranslateTransform(
                ParseUtil.ParseFloat(node.GetAttr("X"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("Y"), 0.0f));
        }
        if (tag == "ScaleTransform") {
            return new ScaleTransform(
                ParseUtil.ParseFloat(node.GetAttr("ScaleX"), 1.0f),
                ParseUtil.ParseFloat(node.GetAttr("ScaleY"), 1.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        }
        if (tag == "RotateTransform") {
            return new RotateTransform(
                ParseUtil.ParseFloat(node.GetAttr("Angle"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        }
        if (tag == "SkewTransform") {
            return new SkewTransform(
                ParseUtil.ParseFloat(node.GetAttr("AngleX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("AngleY"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterX"), 0.0f),
                ParseUtil.ParseFloat(node.GetAttr("CenterY"), 0.0f));
        }
        var tg = new TransformGroup();
        for (int i = 0; i < node.Children.Length; i += 1) {
            Transform sub = BuildTransformNode(node.Children[i], ctx);
            if (sub != null) { tg.Add(sub); }
        }
        return tg;
    }

    private static Effect BuildEffectNode(XamlXmlNode node, XamlBuildContext ctx, UIElement contextHost) {
        string tag = node.LocalName;
        if (tag == "BlurEffect") {
            var b = new BlurEffect();
            string rAttr = node.GetAttr("Radius");
            if (rAttr != null) {
                if (XamlStr.LooksLikeMarkupExtension(rAttr)) {
                    string kind = XamlStr.ExtractExtType(rAttr);
                    if (kind == "Binding") {
                        BindingEngine.BindEffect(b, contextHost, "Radius", ToBindingObject(ParseBindingSpec(rAttr, ctx)));
                    } else {
                        b.Radius = DependencyObject.UnboxFloat(ResolveMarkupValue(rAttr, ctx, contextHost), 8.0f);
                    }
                } else {
                    b.Radius = ParseUtil.ParseFloat(rAttr, 8.0f);
                }
            }
            return b;
        }
        if (tag == "BackdropBlurEffect") {
            return new BackdropBlurEffect(ParseUtil.ParseFloat(node.GetAttr("Radius"), 16.0f));
        }
        if (tag == "DropShadowEffect") {
            float blur = ParseUtil.ParseFloat(node.GetAttr("BlurRadius"), 10.0f);
            float depth = ParseUtil.ParseFloat(node.GetAttr("ShadowDepth"), 4.0f);
            float dir = ParseUtil.ParseFloat(node.GetAttr("Direction"), 315.0f);
            float op = ParseUtil.ParseFloat(node.GetAttr("Opacity"), 0.45f);
            string col = node.GetAttr("Color");
            if (col != null && XamlStr.LooksLikeMarkupExtension(col)) {
                col = DependencyObject.UnboxString(ResolveMarkupValue(col, ctx, contextHost), "#FF000000");
            }
            return new DropShadowEffect(blur, depth, dir, col == null ? "#FF000000" : col, op);
        }
        if (tag == "TintEffect") {
            string col = node.GetAttr("Color");
            float op = ParseUtil.ParseFloat(node.GetAttr("Opacity"), 0.5f);
            return new TintEffect(col == null ? "#FF2266FF" : col, op);
        }
        if (tag == "PixelateEffect") {
            string sz = node.GetAttr("Size");
            if (sz == null) { sz = node.GetAttr("PixelSize"); }
            return new PixelateEffect(ParseUtil.ParseFloat(sz, 6.0f));
        }
        if (tag == "ChromaticAberrationEffect") {
            return new ChromaticAberrationEffect(ParseUtil.ParseFloat(node.GetAttr("Intensity"), 3.0f));
        }
        if (tag == "DirectionalBlurEffect") {
            string ang = node.GetAttr("Angle");
            if (ang == null) { ang = node.GetAttr("Direction"); }
            return new DirectionalBlurEffect(
                ParseUtil.ParseFloat(node.GetAttr("Radius"), 8.0f),
                ParseUtil.ParseFloat(ang, 0.0f));
        }
        if (tag == "SaturationEffect") {
            return new SaturationEffect(ParseUtil.ParseFloat(node.GetAttr("Factor"), 1.0f));
        }
        return null;
    }

    // ---- Styles, Setters, Triggers, Templates ----
    private static Setter BuildSetterNode(XamlXmlNode node, XamlBuildContext ctx) {
        string prop = XamlStr.StripOuterParens(node.GetAttr("Property"));
        if (prop.Length == 0) { return null; }
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
                    s.DynamicResourceKey = UnwrapTypeSpec(body);
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
            string prop = XamlStr.StripOuterParens(node.GetAttr("Property"));
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
                                string cp = XamlStr.StripOuterParens(cond.GetAttr("Property"));
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
                if (isEnter) { trig.AddEnterActionKey(body.Trim()); }
                else { trig.AddExitActionKey(body.Trim()); }
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
                PopulateResourceDictionaryFromChildren(ch, st.Resources, ctx);
                ctx.PushScope(st.Resources);
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
            if (cur.TemplatedParent == null) { cur.TemplatedParent = tp; }
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
                PopulateResourceDictionaryFromChildren(ch, ct.Resources, ctx);
                ctx.PushScope(ct.Resources);
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
            var capturedRes = ct.Resources;
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
                PopulateResourceDictionaryFromChildren(ch, dt.Resources, ctx);
                ctx.PushScope(dt.Resources);
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
            var capturedRes = dt.Resources;
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
                if (tAttr != null) { da.ToFloat = ParseUtil.ParseFloat(tAttr, 1.0f); }
                da.DurationSec = XamlStr.ParseDurationSec(ch.GetAttr("Duration"), 0.25f);
                da.DelaySec = XamlStr.ParseDurationSec(ch.GetAttr("BeginTime"), 0.0f);
                da.AutoReverse = ParseUtil.ParseBool(ch.GetAttr("AutoReverse"), false);
                da.RepeatCount = XamlStr.ParseRepeatCount(ch.GetAttr("RepeatBehavior"));
                for (int e = 0; e < ch.Children.Length; e += 1) {
                    if (XamlStr.EndsWith(ch.Children[e].LocalName, ".EasingFunction") && ch.Children[e].Children.Length > 0) {
                        da.EasingKind = ParseEasingKind(ch.Children[e].Children[0], da.EasingKind);
                        da.EasingMode = ParseEasingMode(ch.Children[e].Children[0], da.EasingMode);
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
                    float kv = ParseUtil.ParseFloat(kf.GetAttr("Value"), 0.0f);
                    EasingFunctionKind ek = EasingFunctionKind.Linear;
                    EasingMode em = EasingMode.EaseOut;
                    for (int e = 0; e < kf.Children.Length; e += 1) {
                        if (XamlStr.EndsWith(kf.Children[e].LocalName, ".EasingFunction") && kf.Children[e].Children.Length > 0) {
                            ek = ParseEasingKind(kf.Children[e].Children[0], ek);
                            em = ParseEasingMode(kf.Children[e].Children[0], em);
                        }
                    }
                    if (kf.LocalName == "SplineDoubleKeyFrame") { ek = EasingFunctionKind.Spline; }
                    dakf.AddKeyFrame(new EasingDoubleKeyFrame(kv, kt, ek, em));
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
                DependencyObject vbox = XamlStr.LooksLikeMarkupExtension(v)
                    ? ResolveMarkupValue(v, ctx, host)
                    : DependencyObject.BoxString(v);
                bdt.SetValueBox(vbox);
            }
            trig = bdt;
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

    private static TriggerAction BuildTriggerActionNode(
        XamlXmlNode node, XamlBuildContext ctx, UIElement host) {
        string tag = node.LocalName;
        if (tag == "ChangePropertyAction") {
            var act = new ChangePropertyAction();
            string pn = node.GetAttr("PropertyName");
            if (pn != null) { act.PropertyName = XamlStr.StripOuterParens(pn); }
            string tn = node.GetAttr("TargetName");
            if (tn != null) { act.TargetName = tn; }
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
        if (tag == "RemoveElementAction") {
            var rea = new RemoveElementAction();
            string tn = node.GetAttr("TargetName");
            if (tn != null) { rea.TargetName = tn; }
            return rea;
        }
        if (tag == "SetFocusAction") {
            var sfa = new SetFocusAction();
            string tn = node.GetAttr("TargetName");
            if (tn != null) { sfa.TargetName = tn; }
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
