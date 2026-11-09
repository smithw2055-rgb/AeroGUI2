#include "aero/layout/DepthBucketLayoutQueue.hpp"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
#include <intrin.h>
#endif

namespace aero {

std::size_t DepthBucketLayoutQueue::LowestSetBitIndex(std::uint64_t mask) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long index = 0;
    _BitScanForward64(&index, mask);
    return static_cast<std::size_t>(index);
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_ctzll(mask));
#else
    std::size_t idx = 0;
    while (((mask >> idx) & 1ULL) == 0ULL && idx < 63u) {
        ++idx;
    }
    return idx;
#endif
}

void DepthBucketLayoutQueue::EnqueueMeasure(UIElement* element) noexcept {
    if (!element || element->inMeasureQueue_) {
        return;
    }
    const std::size_t bucket = ClampDepth(element->treeDepth_);
    element->inMeasureQueue_ = true;
    element->nextInMeasureBucket_ = nullptr;
    if (measureTails_[bucket]) {
        measureTails_[bucket]->nextInMeasureBucket_ = element;
    } else {
        measureHeads_[bucket] = element;
    }
    measureTails_[bucket] = element;
    nonEmptyMeasureMask_ |= (1ULL << bucket);
}

void DepthBucketLayoutQueue::EnqueueArrange(UIElement* element) noexcept {
    if (!element || element->inArrangeQueue_) {
        return;
    }
    const std::size_t bucket = ClampDepth(element->treeDepth_);
    element->inArrangeQueue_ = true;
    element->nextInArrangeBucket_ = nullptr;
    if (arrangeTails_[bucket]) {
        arrangeTails_[bucket]->nextInArrangeBucket_ = element;
    } else {
        arrangeHeads_[bucket] = element;
    }
    arrangeTails_[bucket] = element;
    nonEmptyArrangeMask_ |= (1ULL << bucket);
}

void DepthBucketLayoutQueue::EnqueueSubtree(UIElement* root) noexcept {
    if (!root) {
        return;
    }
    EnqueueMeasure(root);
    EnqueueArrange(root);
    for (UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        EnqueueSubtree(c);
    }
}

std::size_t DepthBucketLayoutQueue::FlushLayout(Size rootViewportSize) noexcept {
    std::size_t processedCount = 0;

    // Phase 1: Drain Measure buckets from shallowest depth (0) to deepest (63)
    while (nonEmptyMeasureMask_ != 0ULL) {
        const std::size_t bucket = LowestSetBitIndex(nonEmptyMeasureMask_);
        UIElement* head = measureHeads_[bucket];
        measureHeads_[bucket] = nullptr;
        measureTails_[bucket] = nullptr;
        nonEmptyMeasureMask_ &= ~(1ULL << bucket);

        while (head) {
            UIElement* next = head->nextInMeasureBucket_;
            head->nextInMeasureBucket_ = nullptr;
            head->inMeasureQueue_ = false;

            if ((head->GetLayoutDirtyFlags() & MeasureDirty) != 0u || head->Parent() == nullptr) {
                Size avail = rootViewportSize;
                if (const UIElement* p = head->Parent()) {
                    const Rect pr = p->FinalRect();
                    if (pr.width > 0.0f || pr.height > 0.0f) {
                        avail = Size{pr.width, pr.height};
                    }
                }
                const Size oldDesired = head->DesiredSize();
                head->Measure(avail);
                if (head->DesiredSize() != oldDesired && head->Parent()) {
                    EnqueueArrange(head->Parent());
                }
                EnqueueArrange(head);
                ++processedCount;
            }
            head = next;
        }
    }

    // Phase 2: Drain Arrange buckets from shallowest depth (0) to deepest (63)
    while (nonEmptyArrangeMask_ != 0ULL) {
        const std::size_t bucket = LowestSetBitIndex(nonEmptyArrangeMask_);
        UIElement* head = arrangeHeads_[bucket];
        arrangeHeads_[bucket] = nullptr;
        arrangeTails_[bucket] = nullptr;
        nonEmptyArrangeMask_ &= ~(1ULL << bucket);

        while (head) {
            UIElement* next = head->nextInArrangeBucket_;
            head->nextInArrangeBucket_ = nullptr;
            head->inArrangeQueue_ = false;

            if (head->Parent() == nullptr) {
                head->Arrange(Rect{0.0f, 0.0f, rootViewportSize.width, rootViewportSize.height});
                ++processedCount;
            } else if ((head->GetLayoutDirtyFlags() & ArrangeDirty) != 0u) {
                Rect r = head->FinalRect();
                if (r.width <= 0.0f && r.height <= 0.0f) {
                    const Size d = head->DesiredSize();
                    r.width = d.width;
                    r.height = d.height;
                }
                head->Arrange(r);
                ++processedCount;
            }
            head = next;
        }
    }

    return processedCount;
}

} // namespace aero
