#include "aero/render/PagedRenderTree.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"

namespace aero {

namespace {

[[nodiscard]] std::uint8_t ClampToByte(float v) noexcept {
    if (v <= 0.0f) return 0u;
    if (v >= 1.0f) return 255u;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

[[nodiscard]] std::uint32_t ScalePackedRgbaAlpha(std::uint32_t baseRgba, float opacity) noexcept {
    if (opacity >= 0.999f) {
        return baseRgba;
    }
    const std::uint32_t a = (baseRgba >> 24u) & 0xFFu;
    const float clamped = (opacity <= 0.0f) ? 0.0f : (opacity >= 1.0f ? 1.0f : opacity);
    const auto scaledA = static_cast<std::uint32_t>(static_cast<float>(a) * clamped + 0.5f);
    return (baseRgba & 0x00FFFFFFu) | ((scaledA & 0xFFu) << 24u);
}

} // namespace

std::uint32_t PackColorRgba8(const Color& color, float opacityMultiplier) noexcept {
    const std::uint32_t r = ClampToByte(color.r);
    const std::uint32_t g = ClampToByte(color.g);
    const std::uint32_t b = ClampToByte(color.b);
    const std::uint32_t a = ClampToByte(color.a * opacityMultiplier);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

std::uint16_t PackSdfRoundedRectParams(float cornerRadius, float borderThickness) noexcept {
    const float clampedR = (cornerRadius < 0.0f) ? 0.0f : (cornerRadius > 127.5f ? 127.5f : cornerRadius);
    const float clampedB = (borderThickness < 0.0f) ? 0.0f : (borderThickness > 127.5f ? 127.5f : borderThickness);
    const auto quantR = static_cast<std::uint16_t>(clampedR * 2.0f + 0.5f) & 0xFFu;
    const auto quantB = static_cast<std::uint16_t>(clampedB * 2.0f + 0.5f) & 0xFFu;
    return static_cast<std::uint16_t>(quantR | (quantB << 8u));
}

void UnpackSdfRoundedRectParams(
    std::uint16_t packed,
    float& outCornerRadius,
    float& outBorderThickness) noexcept {
    outCornerRadius = static_cast<float>(packed & 0xFFu) * 0.5f;
    outBorderThickness = static_cast<float>((packed >> 8u) & 0xFFu) * 0.5f;
}

float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float cornerRadius,
    float aaFeatherWidth) noexcept {
    const float maxR = std::min(halfWidth, halfHeight);
    const float r = std::clamp(cornerRadius, 0.0f, maxR);
    const float qx = std::fabs(sampleLocalX) - (halfWidth - r);
    const float qy = std::fabs(sampleLocalY) - (halfHeight - r);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float outsideDist = std::sqrt(ox * ox + oy * oy);
    const float insideDist = std::min(std::max(qx, qy), 0.0f);
    const float signedDist = outsideDist + insideDist - r;

    const float feather = std::max(aaFeatherWidth, 0.001f);
    const float t = std::clamp(0.5f - (signedDist / feather), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

PagedRenderTree::PagedRenderTree() {
    InitPreCachedDigitGlyphs();
}

void PagedRenderTree::InitPreCachedDigitGlyphs() noexcept {
    for (auto& g : preCachedGlyphs_) {
        g = GlyphUvRect{0.0f, 0.0f, 0.0f, 0.0f, 8.0f, false};
    }
    // Pre-cache ASCII HUD symbols ('0'-'9', '.', '%', '+', '-', '/', ':', ' ') in a 16x8 normalized UV grid
    constexpr char kHudChars[] = "0123456789.%+-/: ";
    for (std::size_t i = 0; i < sizeof(kHudChars) - 1; ++i) {
        const auto ch = static_cast<std::uint8_t>(kHudChars[i]) & 0x7Fu;
        const float col = static_cast<float>(i % 16u);
        const float row = static_cast<float>(i / 16u);
        GlyphUvRect rect{};
        rect.u0 = col * 0.0625f;
        rect.v0 = row * 0.125f;
        rect.u1 = rect.u0 + 0.055f;
        rect.v1 = rect.v0 + 0.110f;
        rect.advanceX = 9.0f;
        rect.valid = true;
        preCachedGlyphs_[ch] = rect;
    }
}

void PagedRenderTree::Clear() noexcept {
    pages_.clear();
    nodes_.clear();
    pageDirtyFlags_.clear();
    dirtyPageList_.clear();
}

void PagedRenderTree::MarkPageDirty(
    std::uint32_t pageIndex,
    std::uint16_t vertexOffsetInPage) noexcept {
    if (pageIndex >= pages_.size()) {
        return;
    }
    CommandPage& page = pages_[pageIndex];
    page.header.dirtyRevision += 1u;
    const std::uint32_t quadIdx = static_cast<std::uint32_t>(vertexOffsetInPage / 4u);
    if (quadIdx < 64u) {
        page.header.dirtyQuadMask |= (1ULL << quadIdx);
    }
    if (pageDirtyFlags_[pageIndex] == 0u) {
        pageDirtyFlags_[pageIndex] = 1u;
        dirtyPageList_.push_back(pageIndex);
    }
}

void PagedRenderTree::WriteQuadVertices(RenderNodeDescriptor& node) noexcept {
    if (node.pageIndex >= pages_.size()) {
        return;
    }
    CommandPage& page = pages_[node.pageIndex];
    const std::size_t vOffset = node.vertexOffsetInPage;
    if (vOffset + 4u > kVerticesPerCommandPage) {
        return;
    }

    AeroVertex* __restrict v = page.vertices.data() + vOffset;
    const float w = std::max(0.0f, node.width);
    const float h = std::max(0.0f, node.height);

    // Local corners (0,0), (w,0), (w,h), (0,h) transformed by 2D affine matrix + node origin
    const float ox = node.x + node.tx;
    const float oy = node.y + node.ty;

    const float x0 = ox;
    const float y0 = oy;
    const float x1 = ox + w * node.m00;
    const float y1 = oy + w * node.m10;
    const float x2 = ox + w * node.m00 + h * node.m01;
    const float y2 = oy + w * node.m10 + h * node.m11;
    const float x3 = ox + h * node.m01;
    const float y3 = oy + h * node.m11;

    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    const auto prim = static_cast<std::uint16_t>(node.primType);
    const std::uint16_t flags = node.encodedSdfParams;

    // For SdfRoundedRect, (u, v) stores local quad coordinates [-w*0.5, +w*0.5] x [-h*0.5, +h*0.5]
    // so the fragment shader computes exact SDF distance without extra uniforms.
    const float uMin = (node.primType == AeroPrimType::SdfRoundedRect) ? (-w * 0.5f) : 0.0f;
    const float vMin = (node.primType == AeroPrimType::SdfRoundedRect) ? (-h * 0.5f) : 0.0f;
    const float uMax = (node.primType == AeroPrimType::SdfRoundedRect) ? (w * 0.5f) : 1.0f;
    const float vMax = (node.primType == AeroPrimType::SdfRoundedRect) ? (h * 0.5f) : 1.0f;

    v[0] = AeroVertex{x0, y0, uMin, vMin, packedColor, prim, flags};
    v[1] = AeroVertex{x1, y1, uMax, vMin, packedColor, prim, flags};
    v[2] = AeroVertex{x2, y2, uMax, vMax, packedColor, prim, flags};
    v[3] = AeroVertex{x3, y3, uMin, vMax, packedColor, prim, flags};
}

std::uint32_t PagedRenderTree::AllocateRenderNode(
    float x,
    float y,
    float width,
    float height,
    const Color& color,
    float cornerRadius,
    float borderThickness,
    const UIElement* owner) {
    if (pages_.empty() || (pages_.back().header.vertexCount + 4u > kVerticesPerCommandPage)) {
        const auto newPageIdx = static_cast<std::uint32_t>(pages_.size());
        CommandPage newPage{};
        newPage.header.pageIndex = newPageIdx;
        newPage.header.firstNodeId = static_cast<std::uint32_t>(nodes_.size());
        pages_.push_back(newPage);
        pageDirtyFlags_.push_back(0u);
        dirtyPageList_.reserve(pages_.size());
    }

    const auto pageIdx = static_cast<std::uint32_t>(pages_.size() - 1u);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = (cornerRadius > 0.0f || borderThickness > 0.0f)
        ? AeroPrimType::SdfRoundedRect
        : AeroPrimType::SolidQuad;
    desc.encodedSdfParams = PackSdfRoundedRectParams(cornerRadius, borderThickness);
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = 1.0f;
    desc.cornerRadius = cornerRadius;
    desc.borderThickness = borderThickness;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}

std::uint32_t PagedRenderTree::AllocateHudDigitsNode(
    float x,
    float y,
    float glyphWidth,
    float glyphHeight,
    std::uint32_t maxDigits,
    const Color& color) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    const std::uint32_t clampedDigits = std::clamp(maxDigits, 1u, maxQuadsPerPage);
    const std::uint32_t neededVertices = clampedDigits * 4u;
    if (pages_.empty() || (pages_.back().header.vertexCount + neededVertices > kVerticesPerCommandPage)) {
        const auto newPageIdx = static_cast<std::uint32_t>(pages_.size());
        CommandPage newPage{};
        newPage.header.pageIndex = newPageIdx;
        newPage.header.firstNodeId = static_cast<std::uint32_t>(nodes_.size());
        pages_.push_back(newPage);
        pageDirtyFlags_.push_back(0u);
        dirtyPageList_.reserve(pages_.size());
    }

    const auto pageIdx = static_cast<std::uint32_t>(pages_.size() - 1u);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += neededVertices;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = static_cast<std::uint16_t>(neededVertices);
    desc.primType = AeroPrimType::SdfTextGlyph;
    desc.encodedSdfParams = AeroVertexFlag_GrayscaleSdfText;
    desc.x = x;
    desc.y = y;
    desc.width = glyphWidth;
    desc.height = glyphHeight;
    desc.opacity = 1.0f;
    desc.baseRgba = PackColorRgba8(color, 1.0f);

    nodes_.push_back(desc);
    (void)UpdateHudDigitsText(nodeId, "0");
    return nodeId;
}

std::uint32_t PagedRenderTree::AllocateHudBarBatch(
    std::size_t count,
    float startX,
    float startY,
    float barWidth,
    float barHeight,
    float verticalSpacing,
    const Color& color,
    float cornerRadius) {
    if (count == 0) {
        return 0;
    }
    nodes_.reserve(nodes_.size() + count);
    const std::size_t estimatedPages = (count + 41u) / 42u + 2u;
    pages_.reserve(pages_.size() + estimatedPages);
    pageDirtyFlags_.reserve(pageDirtyFlags_.size() + estimatedPages);
    dirtyPageList_.reserve(pages_.size() + estimatedPages);

    std::uint32_t firstId = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float y = startY + static_cast<float>(i) * (barHeight + verticalSpacing);
        const std::uint32_t id = AllocateRenderNode(
            startX,
            y,
            barWidth,
            barHeight,
            color,
            cornerRadius,
            0.0f,
            nullptr);
        if (i == 0) {
            firstId = id;
        }
    }
    return firstId;
}

bool PagedRenderTree::UpdateNodeWidth(std::uint32_t nodeId, float newWidth) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.width == newWidth) {
        return false;
    }
    node.width = newWidth;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateNodeOpacity(std::uint32_t nodeId, float newOpacity) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.opacity == newOpacity) {
        return false;
    }
    node.opacity = newOpacity;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateNodeTransform2D(
    std::uint32_t nodeId,
    float m00,
    float m01,
    float m10,
    float m11,
    float tx,
    float ty) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    node.m00 = m00;
    node.m01 = m01;
    node.m10 = m10;
    node.m11 = m11;
    node.tx = tx;
    node.ty = ty;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateHudDigitsText(
    std::uint32_t nodeId,
    std::string_view asciiDigits) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.pageIndex >= pages_.size()) {
        return false;
    }

    CommandPage& page = pages_[node.pageIndex];
    const std::size_t maxSlots = node.vertexCount / 4u;
    AeroVertex* __restrict vBase = page.vertices.data() + node.vertexOffsetInPage;
    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    const auto prim = static_cast<std::uint16_t>(AeroPrimType::SdfTextGlyph);
    const std::uint16_t flags = node.encodedSdfParams;

    float cursorX = node.x + node.tx;
    const float cursorY = node.y + node.ty;
    const float gw = node.width;
    const float gh = node.height;

    for (std::size_t i = 0; i < maxSlots; ++i) {
        AeroVertex* __restrict q = vBase + (i * 4u);
        if (i < asciiDigits.size()) {
            const auto chIdx = static_cast<std::uint8_t>(asciiDigits[i]) & 0x7Fu;
            const GlyphUvRect& uv = preCachedGlyphs_[chIdx];
            const float x0 = cursorX;
            const float y0 = cursorY;
            const float x1 = cursorX + gw;
            const float y1 = cursorY + gh;
            q[0] = AeroVertex{x0, y0, uv.u0, uv.v0, packedColor, prim, flags};
            q[1] = AeroVertex{x1, y0, uv.u1, uv.v0, packedColor, prim, flags};
            q[2] = AeroVertex{x1, y1, uv.u1, uv.v1, packedColor, prim, flags};
            q[3] = AeroVertex{x0, y1, uv.u0, uv.v1, packedColor, prim, flags};
            cursorX += gw;
        } else {
            // Degenerate zero-area quad for unused trailing digit slots
            q[0] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[1] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[2] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[3] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
        }
    }

    const std::uint32_t firstQuadIdx = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
    for (std::size_t i = 0; i < maxSlots; ++i) {
        const std::uint32_t qIdx = firstQuadIdx + static_cast<std::uint32_t>(i);
        if (qIdx < 64u) {
            page.header.dirtyQuadMask |= (1ULL << qIdx);
        }
    }
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::InvalidateNodePage(std::uint32_t nodeId) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateDirtyElement(const UIElement* element) noexcept {
    if (!element) {
        return false;
    }
    for (auto& node : nodes_) {
        if (node.sourceElement == element) {
            const Rect r = element->FinalRect();
            node.x = r.x;
            node.y = r.y;
            node.width = (r.width > 0.0f) ? r.width : element->GetWidth();
            node.height = (r.height > 0.0f) ? r.height : element->GetHeight();
            node.opacity = element->GetOpacity();
            WriteQuadVertices(node);
            MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
            return true;
        }
    }
    return false;
}

std::size_t PagedRenderTree::BatchUpdateWidthsFromRawColumn(
    std::uint32_t firstNodeId,
    std::size_t nodeCount,
    const void* rawColumnBase,
    std::size_t elementStrideBytes,
    std::size_t fieldByteOffset,
    float scaleFactor) noexcept {
    if (!rawColumnBase || elementStrideBytes == 0 || firstNodeId >= nodes_.size()) {
        return 0;
    }
    const std::size_t availNodes = nodes_.size() - firstNodeId;
    const std::size_t count = (nodeCount < availNodes) ? nodeCount : availNodes;
    if (count == 0) {
        return 0;
    }

    const auto* __restrict srcBytes = static_cast<const std::uint8_t*>(rawColumnBase) + fieldByteOffset;
    RenderNodeDescriptor* __restrict nodePtr = nodes_.data() + firstNodeId;
    CommandPage* __restrict pagesBase = pages_.data();
    std::uint8_t* __restrict dirtyFlagsBase = pageDirtyFlags_.data();

    // Pre-sized raw pointer write into dirtyPageList_ (guaranteed capacity >= pages_.size())
    std::size_t dirtyListCount = dirtyPageList_.size();
    dirtyPageList_.resize(pages_.size());
    std::uint32_t* __restrict dirtyListBase = dirtyPageList_.data();

    std::size_t updatedCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        float rawVal = 0.0f;
        std::memcpy(&rawVal, srcBytes + i * elementStrideBytes, sizeof(float));
        const float w = rawVal * scaleFactor;

        RenderNodeDescriptor& node = nodePtr[i];
        if (node.width != w) {
            node.width = w;
            CommandPage& page = pagesBase[node.pageIndex];
            AeroVertex* __restrict v = page.vertices.data() + node.vertexOffsetInPage;

            const float ox = node.x + node.tx;
            if (node.m00 == 1.0f && node.m10 == 0.0f && node.m01 == 0.0f) {
                const float rightX = ox + w;
                v[1].x = rightX;
                v[2].x = rightX;
                if (node.primType == AeroPrimType::SdfRoundedRect) {
                    const float halfW = w * 0.5f;
                    v[0].u = -halfW;
                    v[1].u = halfW;
                    v[2].u = halfW;
                    v[3].u = -halfW;
                }
            } else {
                WriteQuadVertices(node);
            }

            const std::uint32_t quadIdx = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
            if (quadIdx < 64u) {
                page.header.dirtyQuadMask |= (1ULL << quadIdx);
            }
            page.header.dirtyRevision += 1u;
            if (dirtyFlagsBase[node.pageIndex] == 0u) {
                dirtyFlagsBase[node.pageIndex] = 1u;
                dirtyListBase[dirtyListCount++] = node.pageIndex;
            }
            ++updatedCount;
        }
    }

    dirtyPageList_.resize(dirtyListCount);
    return updatedCount;
}

std::size_t PagedRenderTree::BatchUpdateFromEcsTrackB(
    const DualTrackBindingEngine& bindingEngine,
    std::uint32_t firstNodeId) noexcept {
    std::size_t totalUpdated = 0;
    std::uint32_t cursorNodeId = firstNodeId;
    for (const auto& channel : bindingEngine.EcsChannels()) {
        if (!channel.rawView.basePtr || channel.elementStride == 0) {
            continue;
        }
        const std::size_t count = !channel.targetElements.empty()
            ? channel.targetElements.size()
            : channel.rawView.count;
        totalUpdated += BatchUpdateWidthsFromRawColumn(
            cursorNodeId,
            count,
            channel.rawView.basePtr,
            channel.elementStride,
            channel.fieldByteOffset,
            channel.scaleFactor);
        cursorNodeId += static_cast<std::uint32_t>(count);
    }
    return totalUpdated;
}

std::size_t PagedRenderTree::BuildFromVisualTree(const UIElement* rootElement) {
    Clear();
    if (!rootElement) {
        return 0;
    }
    TraverseAndEmitElement(rootElement);
    return nodes_.size();
}

void PagedRenderTree::TraverseAndEmitElement(const UIElement* elem) {
    if (!elem || elem->GetVisibility() == Visibility::Collapsed) {
        return;
    }

    const Rect r = elem->FinalRect();
    const float w = (r.width > 0.0f) ? r.width : elem->GetWidth();
    const float h = (r.height > 0.0f) ? r.height : elem->GetHeight();

    Color color{0.18f, 0.22f, 0.28f, 1.0f};
    if (auto bg = value::TryGetColor(elem->GetValue(PropId_Background))) {
        color = *bg;
    } else if (auto fg = value::TryGetColor(elem->GetValue(PropId_Foreground))) {
        color = *fg;
    }

    float cornerRadius = 0.0f;
    if (auto cr = value::TryGetCornerRadius(elem->GetValue(PropId_CornerRadius))) {
        cornerRadius = cr->topLeft;
    }

    float borderThickness = 0.0f;
    if (auto bt = value::TryGetThickness(elem->GetValue(PropId_BorderThickness))) {
        borderThickness = bt->left;
    }

    const std::uint32_t nodeId = AllocateRenderNode(
        r.x,
        r.y,
        w,
        h,
        color,
        cornerRadius,
        borderThickness,
        elem);
    nodes_[nodeId].opacity = elem->GetOpacity();
    WriteQuadVertices(nodes_[nodeId]);

    for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
        TraverseAndEmitElement(child);
    }
}

bool PagedRenderTree::IsPageDirty(std::size_t pageIndex) const noexcept {
    return (pageIndex < pageDirtyFlags_.size()) && (pageDirtyFlags_[pageIndex] != 0u);
}

std::size_t PagedRenderTree::DirtyPageCount() const noexcept {
    return dirtyPageList_.size();
}

void PagedRenderTree::ClearDirtyPages() noexcept {
    for (const std::uint32_t idx : dirtyPageList_) {
        if (idx < pageDirtyFlags_.size()) {
            pageDirtyFlags_[idx] = 0u;
        }
        if (idx < pages_.size()) {
            pages_[idx].header.dirtyQuadMask = 0ULL;
        }
    }
    dirtyPageList_.clear();
}

} // namespace aero
