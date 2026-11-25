#include "aero/visual/DrawingCommandBuffer.hpp"

#include <utility>

namespace aero {

DrawingCommandList* DrawingCommandBuffer::EnsureActiveList(VisualNodeId node) {
    if (!node.IsValid()) {
        return nullptr;
    }
    const std::uint64_t key = PackNodeKey(node);
    if (lastActiveList_ != nullptr && lastActiveKey_ == key) {
        return lastActiveList_;
    }
    DrawingCommandList* list = &streams_[key];
    lastActiveKey_ = key;
    lastActiveList_ = list;
    return list;
}

void DrawingCommandBuffer::PushCommand(VisualNodeId node, DrawCommand&& cmd) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        list->commands.push_back(std::move(cmd));
        ++globalVersion_;
    }
}

void DrawingCommandBuffer::Begin(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        list->commands.clear();
        list->recording = true;
        ++globalVersion_;
    }
}

void DrawingCommandBuffer::End(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        list->recording = false;
        ++list->version;
        ++globalVersion_;
    }
}

void DrawingCommandBuffer::ClearNode(VisualNodeId node) {
    if (!node.IsValid()) return;
    const std::uint64_t key = PackNodeKey(node);
    if (lastActiveKey_ == key) {
        lastActiveKey_ = 0u;
        lastActiveList_ = nullptr;
    }
    if (streams_.erase(key) > 0u) {
        ++globalVersion_;
    }
}

void DrawingCommandBuffer::ClearAll() noexcept {
    lastActiveKey_ = 0u;
    lastActiveList_ = nullptr;
    if (!streams_.empty()) {
        streams_.clear();
        ++globalVersion_;
    }
}

void DrawingCommandBuffer::Clear(VisualNodeId node, std::uint32_t rgba) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::Clear;
    cmd.colorRgba = rgba;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::FillRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t rgba,
    std::int32_t gradId) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::FillRect;
    cmd.rect = Rect{x, y, w, h};
    cmd.colorRgba = rgba;
    cmd.gradientId = gradId;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t rgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawRect;
    cmd.rect = Rect{x, y, w, h};
    cmd.colorRgba = rgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::FillRoundedRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    float rx,
    float ry,
    std::uint32_t rgba,
    std::int32_t gradId) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::FillRoundedRect;
    cmd.rect = Rect{x, y, w, h};
    cmd.p0 = rx;
    cmd.p1 = ry;
    cmd.colorRgba = rgba;
    cmd.gradientId = gradId;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawRoundedRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    float rx,
    float ry,
    std::uint32_t rgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawRoundedRect;
    cmd.rect = Rect{x, y, w, h};
    cmd.p0 = rx;
    cmd.p1 = ry;
    cmd.colorRgba = rgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::FillEllipse(
    VisualNodeId node,
    float cx,
    float cy,
    float rx,
    float ry,
    std::uint32_t rgba,
    std::int32_t gradId) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::FillEllipse;
    cmd.rect = Rect{cx, cy, rx, ry};
    cmd.colorRgba = rgba;
    cmd.gradientId = gradId;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawEllipse(
    VisualNodeId node,
    float cx,
    float cy,
    float rx,
    float ry,
    std::uint32_t rgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawEllipse;
    cmd.rect = Rect{cx, cy, rx, ry};
    cmd.colorRgba = rgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawLine(
    VisualNodeId node,
    float x1,
    float y1,
    float x2,
    float y2,
    std::uint32_t rgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawLine;
    cmd.p0 = x1;
    cmd.p1 = y1;
    cmd.p2 = x2;
    cmd.p3 = y2;
    cmd.colorRgba = rgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawPolyline(
    VisualNodeId node,
    std::vector<Point> points,
    std::uint32_t rgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawPolyline;
    cmd.points = std::move(points);
    cmd.colorRgba = rgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawPath(
    VisualNodeId node,
    std::string_view svgPath,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t fillRgba,
    std::uint32_t strokeRgba,
    float strokeW) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawPath;
    cmd.text.assign(svgPath);
    cmd.rect = Rect{x, y, w, h};
    cmd.colorRgba = fillRgba;
    cmd.secondaryRgba = strokeRgba;
    cmd.strokeWidth = strokeW;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawImage(
    VisualNodeId node,
    std::string_view uri,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t tintRgba) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawImage;
    cmd.text.assign(uri);
    cmd.rect = Rect{x, y, w, h};
    cmd.colorRgba = tintRgba;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawText(
    VisualNodeId node,
    std::string_view text,
    float x,
    float y,
    float w,
    float h,
    std::string_view fontFamily,
    float fontSize,
    std::uint32_t rgba,
    HorizontalAlignment hAlign,
    VerticalAlignment vAlign) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawText;
    cmd.rect = Rect{x, y, w, h};
    cmd.p0 = fontSize;
    cmd.colorRgba = rgba;
    cmd.intArg0 = static_cast<std::int32_t>(hAlign);
    cmd.intArg1 = static_cast<std::int32_t>(vAlign);
    cmd.text = std::string(fontFamily);
    cmd.text.push_back('\0');
    cmd.text.append(text);
    PushCommand(node, std::move(cmd));
}

namespace {

[[nodiscard]] bool IsIdentStartAscii(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u >= 0x80u;
}

[[nodiscard]] bool IsIdentContAscii(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return IsIdentStartAscii(c) || (u >= '0' && u <= '9');
}

[[nodiscard]] bool IsRsKeyword(std::string_view word) noexcept {
    static constexpr std::string_view kKeywords[] = {
        "module", "import", "public", "private", "protected", "internal", "static",
        "class", "struct", "interface", "enum", "partial", "virtual", "override",
        "abstract", "sealed", "readonly", "const", "event", "delegate", "async",
        "await", "sequence", "yield", "if", "else", "for", "foreach", "in", "while",
        "do", "switch", "case", "default", "break", "continue", "return", "new",
        "null", "true", "false", "this", "base", "is", "as", "var", "get", "set",
        "ref", "out", "try", "catch", "finally", "throw"
    };
    for (const auto kw : kKeywords) {
        if (word == kw) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsRsBuiltinType(std::string_view word) noexcept {
    static constexpr std::string_view kTypes[] = {
        "void", "bool", "int", "uint", "long", "ulong", "float", "double", "string",
        "object", "handle", "byte", "short", "ushort", "char", "Fixed", "Vec2",
        "Vec3", "Vec4", "Quat", "Color", "Rect", "Size", "Point", "Thickness"
    };
    for (const auto ty : kTypes) {
        if (word == ty) {
            return true;
        }
    }
    return !word.empty() && word[0] >= 'A' && word[0] <= 'Z';
}

void TokenizeSyntaxSpans(
    std::string_view line,
    SyntaxHighlightLang lang,
    std::uint32_t defaultRgba,
    std::vector<StyledTextSpan>& outSpans) {
    outSpans.clear();
    if (line.empty()) {
        return;
    }

    constexpr std::uint32_t kColKeyword   = 0xFFC084FCu; // Violet
    constexpr std::uint32_t kColType      = 0xFF38BDF8u; // Sky cyan
    constexpr std::uint32_t kColString    = 0xFF86EFACu; // Mint green
    constexpr std::uint32_t kColNumber    = 0xFFFDBA74u; // Warm amber
    constexpr std::uint32_t kColComment   = 0xFF64748Bu; // Muted slate
    constexpr std::uint32_t kColAttribute = 0xFFFACC15u; // Gold attribute/markup
    constexpr std::uint32_t kColTag       = 0xFF60A5FAu; // XML tag blue
    constexpr std::uint32_t kColAttrName  = 0xFF93C5FDu; // XML attr light blue

    auto pushSpan = [&](std::size_t start, std::size_t end, std::uint32_t col) {
        if (end <= start) return;
        if (!outSpans.empty() &&
            outSpans.back().colorRgba == col &&
            static_cast<std::size_t>(outSpans.back().byteOffset) + outSpans.back().byteLength == start) {
            outSpans.back().byteLength += static_cast<std::uint32_t>(end - start);
            return;
        }
        StyledTextSpan sp{};
        sp.byteOffset = static_cast<std::uint32_t>(start);
        sp.byteLength = static_cast<std::uint32_t>(end - start);
        sp.colorRgba = col;
        outSpans.push_back(sp);
    };

    const std::size_t n = line.size();
    if (lang == SyntaxHighlightLang::Plain) {
        pushSpan(0u, n, defaultRgba);
        return;
    }

    if (lang == SyntaxHighlightLang::Xaml) {
        std::size_t i = 0u;
        while (i < n) {
            if (i + 4u <= n && line.substr(i, 4u) == "<!--") {
                const std::size_t endC = line.find("-->", i + 4u);
                const std::size_t stop = (endC == std::string_view::npos) ? n : (endC + 3u);
                pushSpan(i, stop, kColComment);
                i = stop;
                continue;
            }
            if (line[i] == '<') {
                std::size_t j = i + 1u;
                if (j < n && (line[j] == '/' || line[j] == '?' || line[j] == '!')) ++j;
                while (j < n && (IsIdentContAscii(line[j]) || line[j] == ':' || line[j] == '.' || line[j] == '-')) ++j;
                pushSpan(i, j, kColTag);
                i = j;
                while (i < n && line[i] != '>') {
                    if (line[i] == '"' || line[i] == '\'') {
                        const char q = line[i];
                        std::size_t k = i + 1u;
                        while (k < n && line[k] != q) ++k;
                        if (k < n) ++k;
                        const std::string_view inner = (k >= i + 2u) ? line.substr(i + 1u, k - i - 2u) : std::string_view{};
                        const bool isMarkupExt = !inner.empty() && inner.front() == '{';
                        pushSpan(i, k, isMarkupExt ? kColAttribute : kColString);
                        i = k;
                    } else if (IsIdentStartAscii(line[i])) {
                        std::size_t k = i + 1u;
                        while (k < n && (IsIdentContAscii(line[k]) || line[k] == ':' || line[k] == '.' || line[k] == '-')) ++k;
                        pushSpan(i, k, kColAttrName);
                        i = k;
                    } else if (line[i] == '/') {
                        pushSpan(i, i + 1u, kColTag);
                        ++i;
                    } else {
                        pushSpan(i, i + 1u, defaultRgba);
                        ++i;
                    }
                }
                if (i < n && line[i] == '>') {
                    pushSpan(i, i + 1u, kColTag);
                    ++i;
                }
                continue;
            }
            const std::size_t nextLt = line.find('<', i);
            const std::size_t stop = (nextLt == std::string_view::npos) ? n : nextLt;
            pushSpan(i, stop, defaultRgba);
            i = stop;
        }
        return;
    }

    // RealScript or AeroScene (.ascene / .adata / .agraph)
    std::size_t i = 0u;
    bool seenNonSpace = false;
    while (i < n) {
        const char ch = line[i];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            std::size_t j = i + 1u;
            while (j < n && (line[j] == ' ' || line[j] == '\t' || line[j] == '\r')) ++j;
            pushSpan(i, j, defaultRgba);
            i = j;
            continue;
        }
        if ((ch == '/' && i + 1u < n && line[i + 1u] == '/') ||
            (lang == SyntaxHighlightLang::AeroScene && ch == '#')) {
            pushSpan(i, n, kColComment);
            break;
        }
        if (!seenNonSpace && ch == '[') {
            const std::size_t closeBr = line.find(']', i + 1u);
            if (closeBr != std::string_view::npos) {
                pushSpan(i, closeBr + 1u, kColAttribute);
                i = closeBr + 1u;
                seenNonSpace = true;
                continue;
            }
        }
        if (!seenNonSpace && ch == '@') {
            std::size_t j = i + 1u;
            while (j < n && line[j] != ' ' && line[j] != '\t') ++j;
            pushSpan(i, j, kColAttribute);
            i = j;
            seenNonSpace = true;
            continue;
        }
        seenNonSpace = true;
        if (ch == '"' || ch == '\'') {
            const char q = ch;
            std::size_t j = i + 1u;
            while (j < n) {
                if (line[j] == '\\' && j + 1u < n) {
                    j += 2u;
                    continue;
                }
                if (line[j] == q) {
                    ++j;
                    break;
                }
                ++j;
            }
            pushSpan(i, j, kColString);
            i = j;
            continue;
        }
        if ((ch >= '0' && ch <= '9') ||
            (ch == '-' && i + 1u < n && line[i + 1u] >= '0' && line[i + 1u] <= '9')) {
            std::size_t j = i + 1u;
            while (j < n && (IsIdentContAscii(line[j]) || line[j] == '.')) ++j;
            pushSpan(i, j, kColNumber);
            i = j;
            continue;
        }
        if (IsIdentStartAscii(ch)) {
            std::size_t j = i + 1u;
            while (j < n && IsIdentContAscii(line[j])) ++j;
            const std::string_view word = line.substr(i, j - i);
            std::uint32_t col = defaultRgba;
            if (lang == SyntaxHighlightLang::AeroScene) {
                if (j < n && (line[j] == ':' || line[j] == '=')) {
                    col = kColAttrName;
                } else if (word == "true" || word == "false" || word == "null" ||
                           word == "entity" || word == "component" || word == "prefab" || word == "override") {
                    col = kColKeyword;
                } else if (IsRsBuiltinType(word)) {
                    col = kColType;
                }
            } else {
                if (IsRsKeyword(word)) {
                    col = kColKeyword;
                } else if (IsRsBuiltinType(word)) {
                    col = kColType;
                }
            }
            pushSpan(i, j, col);
            i = j;
            continue;
        }
        pushSpan(i, i + 1u, defaultRgba);
        ++i;
    }
}

} // namespace

void DrawingCommandBuffer::BeginTextRuns(
    VisualNodeId node,
    std::string_view text,
    float x,
    float y,
    float w,
    float h,
    std::string_view fontFamily,
    float fontSize,
    std::uint32_t defaultRgba) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawTextRuns;
    cmd.rect = Rect{x, y, w, h};
    cmd.p0 = fontSize;
    cmd.colorRgba = defaultRgba;
    cmd.text = std::string(fontFamily);
    cmd.text.push_back('\0');
    cmd.text.append(text);
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::AddTextRunSpan(
    VisualNodeId node,
    std::uint32_t byteOffset,
    std::uint32_t byteLength,
    std::uint32_t colorRgba) {
    if (byteLength == 0u) {
        return;
    }
    DrawingCommandList* list = EnsureActiveList(node);
    if (!list || list->commands.empty() || list->commands.back().kind != DrawCommandKind::DrawTextRuns) {
        return;
    }
    StyledTextSpan sp{};
    sp.byteOffset = byteOffset;
    sp.byteLength = byteLength;
    sp.colorRgba = colorRgba;
    list->commands.back().textSpans.push_back(sp);
    ++globalVersion_;
}

void DrawingCommandBuffer::DrawTextRuns(
    VisualNodeId node,
    std::string_view text,
    float x,
    float y,
    float w,
    float h,
    std::string_view fontFamily,
    float fontSize,
    std::uint32_t defaultRgba,
    std::vector<StyledTextSpan> spans) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::DrawTextRuns;
    cmd.rect = Rect{x, y, w, h};
    cmd.p0 = fontSize;
    cmd.colorRgba = defaultRgba;
    cmd.text = std::string(fontFamily);
    cmd.text.push_back('\0');
    cmd.text.append(text);
    cmd.textSpans = std::move(spans);
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::DrawSyntaxLine(
    VisualNodeId node,
    std::string_view lineText,
    float x,
    float y,
    float w,
    float h,
    std::string_view fontFamily,
    float fontSize,
    SyntaxHighlightLang lang,
    std::uint32_t defaultRgba) {
    std::vector<StyledTextSpan> spans;
    TokenizeSyntaxSpans(lineText, lang, defaultRgba, spans);
    DrawTextRuns(node, lineText, x, y, w, h, fontFamily, fontSize, defaultRgba, std::move(spans));
}

void DrawingCommandBuffer::PushClip(VisualNodeId node, float x, float y, float w, float h) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PushClip;
    cmd.rect = Rect{x, y, w, h};
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::PopClip(VisualNodeId node) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PopClip;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::PushTransform(
    VisualNodeId node,
    float tx,
    float ty,
    float sx,
    float sy,
    float rotDeg) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PushTransform;
    cmd.p0 = tx;
    cmd.p1 = ty;
    cmd.p2 = sx;
    cmd.p3 = sy;
    cmd.strokeWidth = rotDeg;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::PopTransform(VisualNodeId node) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PopTransform;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::PushOpacity(VisualNodeId node, float opacity) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PushOpacity;
    cmd.p0 = opacity;
    PushCommand(node, std::move(cmd));
}

void DrawingCommandBuffer::PopOpacity(VisualNodeId node) {
    DrawCommand cmd{};
    cmd.kind = DrawCommandKind::PopOpacity;
    PushCommand(node, std::move(cmd));
}

const DrawingCommandList* DrawingCommandBuffer::FindCommands(VisualNodeId node) const noexcept {
    if (!node.IsValid()) return nullptr;
    const std::uint64_t key = PackNodeKey(node);
    if (lastActiveList_ != nullptr && lastActiveKey_ == key) {
        return lastActiveList_;
    }
    const auto it = streams_.find(key);
    return (it != streams_.end()) ? &it->second : nullptr;
}

bool DrawingCommandBuffer::HasCommands(VisualNodeId node) const noexcept {
    const DrawingCommandList* list = FindCommands(node);
    return list != nullptr && !list->commands.empty();
}

} // namespace aero
