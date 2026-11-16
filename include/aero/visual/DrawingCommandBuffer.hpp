#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/drawing/Geometry.hpp"
#include "aero/visual/VisualTree.hpp"

namespace aero {

enum class DrawCommandKind : std::uint8_t {
    Clear = 0,
    FillRect = 1,
    DrawRect = 2,
    FillRoundedRect = 3,
    DrawRoundedRect = 4,
    FillEllipse = 5,
    DrawEllipse = 6,
    DrawLine = 7,
    DrawPolyline = 8,
    DrawPath = 9,
    DrawImage = 10,
    DrawText = 11,
    PushClip = 12,
    PopClip = 13,
    PushTransform = 14,
    PopTransform = 15,
    PushOpacity = 16,
    PopOpacity = 17,
};

struct DrawCommand {
    DrawCommandKind kind = DrawCommandKind::Clear;
    Rect rect{};
    float p0 = 0.0f;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float p3 = 0.0f;
    float strokeWidth = 1.0f;
    std::uint32_t colorRgba = 0u;
    std::uint32_t secondaryRgba = 0u;
    std::int32_t gradientId = -1;
    std::int32_t intArg0 = 0;
    std::int32_t intArg1 = 0;
    std::string text;
    std::vector<Point> points;
};

struct DrawingCommandList {
    bool recording = false;
    std::uint64_t version = 0ull;
    std::vector<DrawCommand> commands;
};

class DrawingCommandBuffer final {
public:
    DrawingCommandBuffer() = default;
    ~DrawingCommandBuffer() = default;

    void Begin(VisualNodeId node);
    void End(VisualNodeId node);
    void ClearNode(VisualNodeId node);
    void ClearAll() noexcept;

    void Clear(VisualNodeId node, std::uint32_t rgba);
    void FillRect(VisualNodeId node, float x, float y, float w, float h, std::uint32_t rgba, std::int32_t gradId = -1);
    void DrawRect(VisualNodeId node, float x, float y, float w, float h, std::uint32_t rgba, float strokeW);
    void FillRoundedRect(
        VisualNodeId node,
        float x,
        float y,
        float w,
        float h,
        float rx,
        float ry,
        std::uint32_t rgba,
        std::int32_t gradId = -1);
    void DrawRoundedRect(
        VisualNodeId node,
        float x,
        float y,
        float w,
        float h,
        float rx,
        float ry,
        std::uint32_t rgba,
        float strokeW);
    void FillEllipse(
        VisualNodeId node,
        float cx,
        float cy,
        float rx,
        float ry,
        std::uint32_t rgba,
        std::int32_t gradId = -1);
    void DrawEllipse(
        VisualNodeId node,
        float cx,
        float cy,
        float rx,
        float ry,
        std::uint32_t rgba,
        float strokeW);
    void DrawLine(
        VisualNodeId node,
        float x1,
        float y1,
        float x2,
        float y2,
        std::uint32_t rgba,
        float strokeW);
    void DrawPolyline(
        VisualNodeId node,
        std::vector<Point> points,
        std::uint32_t rgba,
        float strokeW);
    void DrawPath(
        VisualNodeId node,
        std::string_view svgPath,
        float x,
        float y,
        float w,
        float h,
        std::uint32_t fillRgba,
        std::uint32_t strokeRgba,
        float strokeW);
    void DrawImage(
        VisualNodeId node,
        std::string_view uri,
        float x,
        float y,
        float w,
        float h,
        std::uint32_t tintRgba = 0xFFFFFFFFu);
    void DrawText(
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
        VerticalAlignment vAlign);
    void PushClip(VisualNodeId node, float x, float y, float w, float h);
    void PopClip(VisualNodeId node);
    void PushTransform(
        VisualNodeId node,
        float tx,
        float ty,
        float sx,
        float sy,
        float rotDeg);
    void PopTransform(VisualNodeId node);
    void PushOpacity(VisualNodeId node, float opacity);
    void PopOpacity(VisualNodeId node);

    [[nodiscard]] const DrawingCommandList* FindCommands(VisualNodeId node) const noexcept;
    [[nodiscard]] bool HasCommands(VisualNodeId node) const noexcept;

private:
    [[nodiscard]] static std::uint64_t PackNodeKey(VisualNodeId node) noexcept {
        return (static_cast<std::uint64_t>(node.generation) << 32u) | node.index;
    }
    DrawingCommandList* EnsureActiveList(VisualNodeId node);

    std::unordered_map<std::uint64_t, DrawingCommandList> streams_;
};

} // namespace aero
