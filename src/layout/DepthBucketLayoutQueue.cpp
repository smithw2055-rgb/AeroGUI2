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

std::size_t DepthBucketLayoutQueue::HighestSetBitIndex(std::uint64_t mask) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long index = 0;
    _BitScanReverse64(&index, mask);
    return static_cast<std::size_t>(index);
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(63 - __builtin_clzll(mask));
#else
    std::size_t idx = 63u;
    while (idx > 0u && ((mask >> idx) & 1ULL) == 0ULL) {
        --idx;
    }
    return idx;
#endif
}

void DepthBucketLayoutQueue::EnqueueMeasure(UIElement* element) noexcept {
    if (!element) {
        return;
    }
    element->InvalidateMeasure();
    if (element->inMeasureQueue_) {
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

void DepthBucketLayoutQueue::EnqueueUpwardMeasure(
    UIElement* element,
    std::uint64_t& upwardMask) noexcept {
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
    upwardMask |= (1ULL << bucket);
}

void DepthBucketLayoutQueue::EnqueueArrange(UIElement* element) noexcept {
    if (!element) {
        return;
    }
    element->InvalidateArrange();
    if (element->inArrangeQueue_) {
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

void DepthBucketLayoutQueue::EnqueueDirtySubtree(UIElement* root) noexcept {
    if (!root) {
        return;
    }
    const std::uint8_t flags = root->fastProps_.layoutDirtyFlags;
    if ((flags & (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) == 0u) {
        return;
    }
    root->ClearLayoutDirtyFlags(SubtreeLayoutDirty);
    if ((flags & MeasureDirty) != 0u) {
        EnqueueMeasure(root);
        root->ClearLayoutDirtyFlags(SubtreeLayoutDirty);
    }
    if ((flags & ArrangeDirty) != 0u) {
        EnqueueArrange(root);
        root->ClearLayoutDirtyFlags(SubtreeLayoutDirty);
    }
    if ((flags & SubtreeLayoutDirty) != 0u) {
        for (UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
            EnqueueDirtySubtree(c);
        }
    }
}

std::size_t DepthBucketLayoutQueue::FlushLayout(Size rootViewportSize) noexcept {
    std::size_t processedCount = 0;
    std::uint64_t upwardMeasureMask = 0ULL;

    auto resolveAvailableSize = [&](const UIElement* elem) noexcept -> Size {
        if (const UIElement* p = elem->Parent()) {
            if (elem->lastMeasureConstraint_.width >= 0.0f &&
                elem->lastMeasureConstraint_.height >= 0.0f) {
                return elem->lastMeasureConstraint_;
            }
            const Rect pr = p->FinalRect();
            if (pr.width > 0.0f || pr.height > 0.0f) {
                return Size{pr.width, pr.height};
            }
        }
        return rootViewportSize;
    };

    // Phase 1a: Drain dirty Measure buckets top-down from shallowest depth (0) to deepest (63)
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
                const Size avail = resolveAvailableSize(head);
                const Size oldDesired = head->DesiredSize();
                head->Measure(avail);
                if (head->DesiredSize() != oldDesired) {
                    if (UIElement* parent = head->Parent()) {
                        if (ClampDepth(parent->treeDepth_) < bucket) {
                            EnqueueUpwardMeasure(parent, upwardMeasureMask);
                        } else {
                            // Deep overflow (>= 63) fallback: bubble synchronously
                            UIElement* anc = parent;
                            while (anc) {
                                const Size prevAnc = anc->DesiredSize();
                                anc->fastProps_.layoutDirtyFlags |= MeasureDirty;
                                anc->Measure(resolveAvailableSize(anc));
                                EnqueueArrange(anc);
                                ++processedCount;
                                if (anc->DesiredSize() == prevAnc) {
                                    break;
                                }
                                anc = anc->Parent();
                                if (anc && ClampDepth(anc->treeDepth_) < bucket) {
                                    EnqueueUpwardMeasure(anc, upwardMeasureMask);
                                    break;
                                }
                            }
                        }
                    }
                }
                EnqueueArrange(head);
                ++processedCount;
            }
            head = next;
        }
    }

    // Phase 1b: Coalesced bottom-up ancestor re-measure pass (deepest -> 0),
    // ensuring each dirty parent is re-measured at most once per frame with O(1) clean-sibling hits.
    while (upwardMeasureMask != 0ULL) {
        const std::size_t bucket = HighestSetBitIndex(upwardMeasureMask);
        UIElement* head = measureHeads_[bucket];
        measureHeads_[bucket] = nullptr;
        measureTails_[bucket] = nullptr;
        upwardMeasureMask &= ~(1ULL << bucket);

        while (head) {
            UIElement* next = head->nextInMeasureBucket_;
            head->nextInMeasureBucket_ = nullptr;
            head->inMeasureQueue_ = false;

            const Size prevAncDesired = head->DesiredSize();
            head->fastProps_.layoutDirtyFlags |= MeasureDirty;
            head->Measure(resolveAvailableSize(head));
            EnqueueArrange(head);
            ++processedCount;
            if (head->DesiredSize() != prevAncDesired) {
                if (UIElement* parent = head->Parent()) {
                    EnqueueUpwardMeasure(parent, upwardMeasureMask);
                }
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
                Rect r = head->LastArrangeSlot();
                if (r.width < 0.0f) {
                    r = head->FinalRect();
                    if (r.width <= 0.0f && r.height <= 0.0f) {
                        const Size d = head->DesiredSize();
                        r.width = d.width;
                        r.height = d.height;
                    }
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
