#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aero {

struct SkylineNode {
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    std::uint32_t width = 0u;
};

/// 2D Bottom-Left Skyline rectangle bin-packer with configurable origin margins.
/// Allocates non-overlapping `(rectWidth x rectHeight)` slots inside
/// `[originX, originX + packWidth) x [originY, originY + packHeight)`.
class SkylinePacker {
public:
    SkylinePacker() noexcept = default;

    SkylinePacker(
        std::uint32_t packWidth,
        std::uint32_t packHeight,
        std::uint32_t originX = 0u,
        std::uint32_t originY = 0u) {
        Reset(packWidth, packHeight, originX, originY);
    }

    void Reset(
        std::uint32_t packWidth,
        std::uint32_t packHeight,
        std::uint32_t originX = 0u,
        std::uint32_t originY = 0u) {
        originX_ = originX;
        originY_ = originY;
        width_ = packWidth;
        height_ = packHeight;
        nodes_.clear();
        if (width_ > 0u && height_ > 0u) {
            nodes_.push_back(SkylineNode{0u, 0u, width_});
        }
    }

    void Clear() noexcept {
        nodes_.clear();
        if (width_ > 0u && height_ > 0u) {
            nodes_.push_back(SkylineNode{0u, 0u, width_});
        }
    }

    void ClearToEmpty() noexcept {
        nodes_.clear();
    }

    [[nodiscard]] bool IsEmpty() const noexcept {
        return nodes_.empty();
    }

    [[nodiscard]] bool TryAllocate(
        std::uint32_t rectWidth,
        std::uint32_t rectHeight,
        std::uint32_t& outX,
        std::uint32_t& outY) noexcept {
        if (rectWidth == 0u || rectHeight == 0u ||
            rectWidth > width_ || rectHeight > height_ || nodes_.empty()) {
            return false;
        }

        std::size_t bestIndex = static_cast<std::size_t>(-1);
        std::uint32_t bestY = 0xFFFFFFFFu;
        std::uint32_t bestWidth = 0xFFFFFFFFu;

        for (std::size_t i = 0u; i < nodes_.size(); ++i) {
            const std::uint32_t x = nodes_[i].x;
            if (x + rectWidth > width_) {
                continue;
            }
            std::uint32_t y = nodes_[i].y;
            std::int64_t widthLeft = static_cast<std::int64_t>(rectWidth);
            std::size_t j = i;
            bool fits = true;
            while (widthLeft > 0) {
                if (j >= nodes_.size()) {
                    fits = false;
                    break;
                }
                y = std::max(y, nodes_[j].y);
                if (y + rectHeight > height_) {
                    fits = false;
                    break;
                }
                widthLeft -= static_cast<std::int64_t>(nodes_[j].width);
                ++j;
            }
            if (fits && (y < bestY || (y == bestY && nodes_[i].width < bestWidth))) {
                bestIndex = i;
                bestY = y;
                bestWidth = nodes_[i].width;
            }
        }

        if (bestIndex == static_cast<std::size_t>(-1)) {
            return false;
        }

        const std::uint32_t placedX = nodes_[bestIndex].x;
        const std::uint32_t placedY = bestY;
        const SkylineNode newNode{placedX, placedY + rectHeight, rectWidth};
        nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(bestIndex), newNode);

        for (std::size_t i = bestIndex + 1u; i < nodes_.size(); ++i) {
            const std::uint32_t prevRight = nodes_[i - 1u].x + nodes_[i - 1u].width;
            if (nodes_[i].x < prevRight) {
                const std::uint32_t shrink = prevRight - nodes_[i].x;
                if (nodes_[i].width <= shrink) {
                    nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(i));
                    --i;
                } else {
                    nodes_[i].x += shrink;
                    nodes_[i].width -= shrink;
                    break;
                }
            } else {
                break;
            }
        }

        // Merge adjacent co-level skyline spans
        for (std::size_t i = 0u; i + 1u < nodes_.size(); ++i) {
            if (nodes_[i].y == nodes_[i + 1u].y) {
                nodes_[i].width += nodes_[i + 1u].width;
                nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(i + 1u));
                --i;
            }
        }

        outX = originX_ + placedX;
        outY = originY_ + placedY;
        return true;
    }

    [[nodiscard]] std::uint32_t PackWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t PackHeight() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t OriginX() const noexcept { return originX_; }
    [[nodiscard]] std::uint32_t OriginY() const noexcept { return originY_; }

private:
    std::uint32_t originX_ = 0u;
    std::uint32_t originY_ = 0u;
    std::uint32_t width_ = 0u;
    std::uint32_t height_ = 0u;
    std::vector<SkylineNode> nodes_;
};

} // namespace aero
