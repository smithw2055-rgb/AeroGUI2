#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "aero/core/UIElement.hpp"

namespace aero {

class DepthBucketLayoutQueue {
public:
    static constexpr std::size_t kBucketCount = 64;

    DepthBucketLayoutQueue() noexcept = default;

    void EnqueueMeasure(UIElement* element) noexcept;
    void EnqueueArrange(UIElement* element) noexcept;
    void EnqueueSubtree(UIElement* root) noexcept;
    void EnqueueDirtySubtree(UIElement* root) noexcept;

    [[nodiscard]] bool HasPendingMeasure() const noexcept {
        return nonEmptyMeasureMask_ != 0ULL;
    }
    [[nodiscard]] bool HasPendingArrange() const noexcept {
        return nonEmptyArrangeMask_ != 0ULL;
    }
    [[nodiscard]] std::uint64_t NonEmptyMeasureMask() const noexcept {
        return nonEmptyMeasureMask_;
    }
    [[nodiscard]] std::uint64_t NonEmptyArrangeMask() const noexcept {
        return nonEmptyArrangeMask_;
    }

    // Drain all dirty measure & arrange buckets in O(1) bit-scan depth order (0..63).
    std::size_t FlushLayout(Size rootViewportSize) noexcept;

private:
    [[nodiscard]] static std::size_t ClampDepth(std::uint16_t depth) noexcept {
        return depth < 63u ? static_cast<std::size_t>(depth) : 63u;
    }
    [[nodiscard]] static std::size_t LowestSetBitIndex(std::uint64_t mask) noexcept;
    [[nodiscard]] static std::size_t HighestSetBitIndex(std::uint64_t mask) noexcept;
    void EnqueueUpwardMeasure(UIElement* element, std::uint64_t& upwardMask) noexcept;

    std::uint64_t nonEmptyMeasureMask_ = 0ULL;
    std::uint64_t nonEmptyArrangeMask_ = 0ULL;
    std::array<UIElement*, kBucketCount> measureHeads_{};
    std::array<UIElement*, kBucketCount> measureTails_{};
    std::array<UIElement*, kBucketCount> arrangeHeads_{};
    std::array<UIElement*, kBucketCount> arrangeTails_{};
};

} // namespace aero
