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
