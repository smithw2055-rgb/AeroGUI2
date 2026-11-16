#include "aero/visual/DrawingCommandBuffer.hpp"

#include <utility>

namespace aero {

DrawingCommandList* DrawingCommandBuffer::EnsureActiveList(VisualNodeId node) {
    if (!node.IsValid()) {
        return nullptr;
    }
    return &streams_[PackNodeKey(node)];
}

void DrawingCommandBuffer::Begin(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        list->commands.clear();
        list->recording = true;
    }
}

void DrawingCommandBuffer::End(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        list->recording = false;
        ++list->version;
    }
}

void DrawingCommandBuffer::ClearNode(VisualNodeId node) {
    if (!node.IsValid()) return;
    streams_.erase(PackNodeKey(node));
}

void DrawingCommandBuffer::ClearAll() noexcept {
    streams_.clear();
}

void DrawingCommandBuffer::Clear(VisualNodeId node, std::uint32_t rgba) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::Clear;
        cmd.colorRgba = rgba;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::FillRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t rgba,
    std::int32_t gradId) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::FillRect;
        cmd.rect = Rect{x, y, w, h};
        cmd.colorRgba = rgba;
        cmd.gradientId = gradId;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::DrawRect(
    VisualNodeId node,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t rgba,
    float strokeW) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawRect;
        cmd.rect = Rect{x, y, w, h};
        cmd.colorRgba = rgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
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
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::FillRoundedRect;
        cmd.rect = Rect{x, y, w, h};
        cmd.p0 = rx;
        cmd.p1 = ry;
        cmd.colorRgba = rgba;
        cmd.gradientId = gradId;
        list->commands.push_back(std::move(cmd));
    }
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
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawRoundedRect;
        cmd.rect = Rect{x, y, w, h};
        cmd.p0 = rx;
        cmd.p1 = ry;
        cmd.colorRgba = rgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::FillEllipse(
    VisualNodeId node,
    float cx,
    float cy,
    float rx,
    float ry,
    std::uint32_t rgba,
    std::int32_t gradId) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::FillEllipse;
        cmd.rect = Rect{cx, cy, rx, ry};
        cmd.colorRgba = rgba;
        cmd.gradientId = gradId;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::DrawEllipse(
    VisualNodeId node,
    float cx,
    float cy,
    float rx,
    float ry,
    std::uint32_t rgba,
    float strokeW) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawEllipse;
        cmd.rect = Rect{cx, cy, rx, ry};
        cmd.colorRgba = rgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::DrawLine(
    VisualNodeId node,
    float x1,
    float y1,
    float x2,
    float y2,
    std::uint32_t rgba,
    float strokeW) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawLine;
        cmd.p0 = x1;
        cmd.p1 = y1;
        cmd.p2 = x2;
        cmd.p3 = y2;
        cmd.colorRgba = rgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::DrawPolyline(
    VisualNodeId node,
    std::vector<Point> points,
    std::uint32_t rgba,
    float strokeW) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawPolyline;
        cmd.points = std::move(points);
        cmd.colorRgba = rgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
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
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawPath;
        cmd.text.assign(svgPath);
        cmd.rect = Rect{x, y, w, h};
        cmd.colorRgba = fillRgba;
        cmd.secondaryRgba = strokeRgba;
        cmd.strokeWidth = strokeW;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::DrawImage(
    VisualNodeId node,
    std::string_view uri,
    float x,
    float y,
    float w,
    float h,
    std::uint32_t tintRgba) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::DrawImage;
        cmd.text.assign(uri);
        cmd.rect = Rect{x, y, w, h};
        cmd.colorRgba = tintRgba;
        list->commands.push_back(std::move(cmd));
    }
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
    if (DrawingCommandList* list = EnsureActiveList(node)) {
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
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PushClip(VisualNodeId node, float x, float y, float w, float h) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PushClip;
        cmd.rect = Rect{x, y, w, h};
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PopClip(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PopClip;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PushTransform(
    VisualNodeId node,
    float tx,
    float ty,
    float sx,
    float sy,
    float rotDeg) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PushTransform;
        cmd.p0 = tx;
        cmd.p1 = ty;
        cmd.p2 = sx;
        cmd.p3 = sy;
        cmd.strokeWidth = rotDeg;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PopTransform(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PopTransform;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PushOpacity(VisualNodeId node, float opacity) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PushOpacity;
        cmd.p0 = opacity;
        list->commands.push_back(std::move(cmd));
    }
}

void DrawingCommandBuffer::PopOpacity(VisualNodeId node) {
    if (DrawingCommandList* list = EnsureActiveList(node)) {
        DrawCommand cmd{};
        cmd.kind = DrawCommandKind::PopOpacity;
        list->commands.push_back(std::move(cmd));
    }
}

const DrawingCommandList* DrawingCommandBuffer::FindCommands(VisualNodeId node) const noexcept {
    if (!node.IsValid()) return nullptr;
    const auto it = streams_.find(PackNodeKey(node));
    return (it != streams_.end()) ? &it->second : nullptr;
}

bool DrawingCommandBuffer::HasCommands(VisualNodeId node) const noexcept {
    const DrawingCommandList* list = FindCommands(node);
    return list != nullptr && !list->commands.empty();
}

} // namespace aero
