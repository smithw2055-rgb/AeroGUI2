#include "aero/host/HostDrivers.hpp"

#include <algorithm>
#include <cstdio>

#include "aero/controls/Controls.hpp"

namespace aero {

namespace {

bool DecodeNextUtf8CodePoint(
    std::string_view text,
    std::size_t& cursor,
    std::uint32_t& outCodePoint) noexcept {
    if (cursor >= text.size()) {
        return false;
    }
    const auto b0 = static_cast<std::uint8_t>(text[cursor]);
    if ((b0 & 0x80u) == 0u) {
        outCodePoint = b0;
        cursor += 1u;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u && cursor + 1u < text.size()) {
        const auto b1 = static_cast<std::uint8_t>(text[cursor + 1u]) & 0x3Fu;
        outCodePoint = ((static_cast<std::uint32_t>(b0 & 0x1Fu) << 6u) | b1);
        cursor += 2u;
        return true;
    }
    if ((b0 & 0xF0u) == 0xE0u && cursor + 2u < text.size()) {
        const auto b1 = static_cast<std::uint8_t>(text[cursor + 1u]) & 0x3Fu;
        const auto b2 = static_cast<std::uint8_t>(text[cursor + 2u]) & 0x3Fu;
        outCodePoint = ((static_cast<std::uint32_t>(b0 & 0x0Fu) << 12u) |
                        (static_cast<std::uint32_t>(b1) << 6u) | b2);
        cursor += 3u;
        return true;
    }
    if ((b0 & 0xF8u) == 0xF0u && cursor + 3u < text.size()) {
        const auto b1 = static_cast<std::uint8_t>(text[cursor + 1u]) & 0x3Fu;
        const auto b2 = static_cast<std::uint8_t>(text[cursor + 2u]) & 0x3Fu;
        const auto b3 = static_cast<std::uint8_t>(text[cursor + 3u]) & 0x3Fu;
        outCodePoint = ((static_cast<std::uint32_t>(b0 & 0x07u) << 18u) |
                        (static_cast<std::uint32_t>(b1) << 12u) |
                        (static_cast<std::uint32_t>(b2) << 6u) | b3);
        cursor += 4u;
        return true;
    }
    outCodePoint = 0xFFFDu;
    cursor += 1u;
    return true;
}

AriaShadowNode BuildAriaNodeRecursive(const UIElement* elem) {
    AriaShadowNode node{};
    if (!elem) {
        return node;
    }
    node.bounds = elem->FinalRect();
    node.label = elem->Name();

    if (const auto* btn = elem->DynamicCast<Button>()) {
        node.role = "button";
        node.value = btn->GetContentText();
        node.focusable = true;
    } else if (const auto* tb = elem->DynamicCast<TextBlock>()) {
        node.role = "text";
        node.value = tb->GetText();
        node.focusable = false;
    } else if (elem->IsA<Window>()) {
        node.role = "dialog";
        node.focusable = true;
    } else {
        node.role = "group";
        node.focusable = false;
    }

    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            node.children.push_back(BuildAriaNodeRecursive(c));
        }
    }
    return node;
}

} // namespace

// ============================================================================
// 1. EmbeddedGameHost
// ============================================================================

EmbeddedGameHost::EmbeddedGameHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree) {}

void EmbeddedGameHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if ((activePhaseMask & UiPhase_TrackABinding) != 0u && bindingEngine_) {
        telemetry_.trackAUpdates += bindingEngine_->FlushTrackA();
    }

    if ((activePhaseMask & UiPhase_TrackBBinding) != 0u && bindingEngine_) {
        if (renderTree_ && (activePhaseMask & UiPhase_Layout) == 0u) {
            // Fast HUD Track B direct render-tree path (bypasses DP and LayoutEngine)
            telemetry_.trackBUpdates += renderTree_->BatchUpdateFromEcsTrackB(*bindingEngine_);
        } else {
            telemetry_.trackBUpdates += bindingEngine_->FlushTrackB();
        }
    }

    if ((activePhaseMask & UiPhase_Style) != 0u) {
        telemetry_.stylePassesRun += 1u;
    }

    if ((activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.layoutPassesRun += 1u;
        if (layoutQueue_) {
            (void)layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }
}

ZeroCopySubmissionStats EmbeddedGameHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 2. DesktopEventHost
// ============================================================================

DesktopEventHost::DesktopEventHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue) {}

void DesktopEventHost::SetStateFlags(
    bool hasActiveAnimations,
    bool hasDirtyObjects,
    bool hasDirtyLayout,
    bool hasPendingInputEvents) noexcept {
    hasActiveAnimations_ = hasActiveAnimations;
    hasDirtyObjects_ = hasDirtyObjects;
    hasDirtyLayout_ = hasDirtyLayout;
    hasPendingInputEvents_ = hasPendingInputEvents;
}

bool DesktopEventHost::CanSleepIdle() const noexcept {
    return !hasActiveAnimations_ &&
           !hasDirtyObjects_ &&
           !hasDirtyLayout_ &&
           !hasPendingInputEvents_;
}

void DesktopEventHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    if (CanSleepIdle()) {
        // 0.0% CPU / 0.0% GPU polling when idle
        return;
    }

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if (hasDirtyObjects_ && bindingEngine_ && (activePhaseMask & UiPhase_TrackABinding) != 0u) {
        const std::size_t updated = bindingEngine_->FlushTrackA();
        telemetry_.trackAUpdates += updated;
        if (updated > 0) {
            hasDirtyLayout_ = true;
        }
    }

    if ((activePhaseMask & UiPhase_Style) != 0u && (hasDirtyObjects_ || hasDirtyLayout_)) {
        telemetry_.stylePassesRun += 1u;
    }

    if ((activePhaseMask & UiPhase_Layout) != 0u && hasDirtyLayout_) {
        telemetry_.layoutPassesRun += 1u;
        if (layoutQueue_) {
            (void)layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }

    // Clear transient dirty flags after processing; if animations are off, next check can sleep idle
    hasDirtyObjects_ = false;
    hasDirtyLayout_ = false;
    hasPendingInputEvents_ = false;
}

ZeroCopySubmissionStats DesktopEventHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    if (renderTree.DirtyPageCount() == 0 && CanSleepIdle()) {
        return ZeroCopySubmissionStats{};
    }
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 3. WebImeAdapter & WebFontStreamProvider & WebCanvasHost
// ============================================================================

void WebImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    focusedElement_ = focusedElement;
    baseCaretRect_ = caretScreenRect;
    overlayDomRect_ = caretScreenRect;
    imeFocused_ = (focusedElement != nullptr);
}

void WebImeAdapter::BlurTextInput() {
    focusedElement_ = nullptr;
    imeFocused_ = false;
    isComposing_ = false;
    preeditText_.clear();
}

void WebImeAdapter::OnCompositionStart() {
    isComposing_ = true;
    preeditText_.clear();
    caretOffset_ = 0;
    overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(committedText_.size()) * 2.0f;
}

void WebImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    isComposing_ = true;
    preeditText_.assign(preeditText);
    caretOffset_ = caretOffset;
    // Position transparent DOM input overlay X relative to base caret + committed + active caretOffset
    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(committedText_.size() + caretOffset) * 2.0f;
    if (focusedElement_) {
        focusedElement_->SetValue(
            PropId_Text,
            value::FromString(committedText_ + preeditText_),
            ValueSourceRank::Local);
    }
}

void WebImeAdapter::OnCompositionEnd(std::string_view committedText) {
    isComposing_ = false;
    preeditText_.clear();
    committedText_ += std::string(committedText);
    caretOffset_ = 0;
    overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(committedText_.size()) * 2.0f;
    if (focusedElement_) {
        focusedElement_->SetValue(
            PropId_Text,
            value::FromString(committedText_),
            ValueSourceRank::Local);
    }
}

std::string WebImeAdapter::GenerateOverlayCssStyle() const {
    char buf[256]{};
    std::snprintf(
        buf,
        sizeof(buf),
        "position:fixed;left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;opacity:0;pointer-events:none;z-index:9999;",
        overlayDomRect_.x,
        overlayDomRect_.y,
        std::max(1.0f, overlayDomRect_.width),
        std::max(16.0f, overlayDomRect_.height));
    return std::string(buf);
}

void WebImeAdapter::SyncClipboardFromDomEvent(std::string_view pastedText) {
    cachedClipboard_.assign(pastedText);
}

void WebImeAdapter::WriteClipboardToDom(std::string_view copiedText) {
    cachedClipboard_.assign(copiedText);
}

AriaShadowNode WebImeAdapter::BuildAriaShadowTree(const UIElement* rootElement) const {
    return BuildAriaNodeRecursive(rootElement);
}

WebFontStreamProvider::WebFontStreamProvider() {
    // Pre-load Basic ASCII shard (U+0000 .. U+00FF) at startup so Latin HUD text never stalls
    UnicodeShardDescriptor asciiShard{};
    asciiShard.shardId = 0u;
    asciiShard.rangeStart = 0x0000u;
    asciiShard.rangeEnd = 0x00FFu;
    asciiShard.shardUri = "fonts/shards/unicode_0000_00ff.bin";
    asciiShard.state = ShardLoadState::Loaded;
    asciiShard.loadedByteSize = 4096u;
    shards_[0u] = std::move(asciiShard);
    loadedShardCount_ = 1u;
}

std::uint32_t WebFontStreamProvider::ComputeShardIdForCodePoint(std::uint32_t codePoint) const noexcept {
    // Partition Unicode into 256-codepoint shards (e.g. 0x4E00 -> shard 0x4E, 0x6211 '我' -> shard 0x62)
    return codePoint >> 8u;
}

UnicodeShardDescriptor& WebFontStreamProvider::EnsureShardDescriptor(std::uint32_t codePoint) {
    const std::uint32_t shardId = ComputeShardIdForCodePoint(codePoint);
    auto it = shards_.find(shardId);
    if (it != shards_.end()) {
        return it->second;
    }

    const std::uint32_t startCp = shardId << 8u;
    const std::uint32_t endCp = startCp + 0xFFu;
    char uriBuf[128]{};
    std::snprintf(
        uriBuf,
        sizeof(uriBuf),
        "fonts/shards/cjk_shard_%04x_%04x.bin",
        startCp,
        endCp);

    UnicodeShardDescriptor desc{};
    desc.shardId = shardId;
    desc.rangeStart = startCp;
    desc.rangeEnd = endCp;
    desc.shardUri = uriBuf;
    desc.state = ShardLoadState::Unloaded;
    auto [insertedIt, _] = shards_.emplace(shardId, std::move(desc));
    return insertedIt->second;
}

std::size_t WebFontStreamProvider::InspectTextAndQueueMissingShards(
    std::string_view utf8Text,
    std::uint32_t targetRenderNodeId) {
    std::size_t queuedCount = 0;
    std::size_t cursor = 0;
    std::uint32_t cp = 0;

    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        UnicodeShardDescriptor& shard = EnsureShardDescriptor(cp);
        if (shard.state == ShardLoadState::Unloaded) {
            shard.state = ShardLoadState::FetchingAsync;
            pendingRequests_.push_back(FontShardFetchRequest{
                shard.shardId,
                shard.rangeStart,
                shard.rangeEnd,
                shard.shardUri,
                targetRenderNodeId});
            ++queuedCount;
        }
    }
    return queuedCount;
}

bool WebFontStreamProvider::OnShardDataArrived(
    std::uint32_t shardId,
    const std::vector<std::uint8_t>& shardPayload,
    PagedRenderTree* renderTreeToInvalidate) {
    auto it = shards_.find(shardId);
    if (it == shards_.end()) {
        return false;
    }

    UnicodeShardDescriptor& shard = it->second;
    if (shard.state != ShardLoadState::Loaded) {
        shard.state = ShardLoadState::Loaded;
        shard.loadedByteSize = shardPayload.size();
        loadedShardCount_ += 1u;
    }

    for (auto reqIt = pendingRequests_.begin(); reqIt != pendingRequests_.end();) {
        if (reqIt->shardId == shardId) {
            if (renderTreeToInvalidate && reqIt->requestingRenderNodeId < renderTreeToInvalidate->NodeCount()) {
                (void)renderTreeToInvalidate->InvalidateNodePage(reqIt->requestingRenderNodeId);
            }
            reqIt = pendingRequests_.erase(reqIt);
        } else {
            ++reqIt;
        }
    }
    return true;
}

ShardLoadState WebFontStreamProvider::GetCodePointShardState(std::uint32_t codePoint) const noexcept {
    const std::uint32_t shardId = ComputeShardIdForCodePoint(codePoint);
    const auto it = shards_.find(shardId);
    if (it == shards_.end()) {
        return ShardLoadState::Unloaded;
    }
    return it->second.state;
}

WebCanvasHost::WebCanvasHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    ShaderBackend webBackend)
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , webBackend_(webBackend) {}

void WebCanvasHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    if (rafState_ == WebRafState::SuspendedIdle) {
        return;
    }

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    std::size_t changes = 0;
    if ((activePhaseMask & UiPhase_TrackABinding) != 0u && bindingEngine_) {
        const std::size_t a = bindingEngine_->FlushTrackA();
        telemetry_.trackAUpdates += a;
        changes += a;
    }
    if ((activePhaseMask & UiPhase_TrackBBinding) != 0u && bindingEngine_) {
        const std::size_t b = bindingEngine_->FlushTrackB();
        telemetry_.trackBUpdates += b;
        changes += b;
    }
    if ((activePhaseMask & UiPhase_Style) != 0u) {
        telemetry_.stylePassesRun += 1u;
    }
    if ((activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.layoutPassesRun += 1u;
        if (layoutQueue_) {
            changes += layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }

    // Automatically suspend rAF if no IME composition, no pending font shards, and no dirty bindings
    if (changes == 0 && !imeAdapter_.IsComposing() && fontStream_.PendingFetchRequests().empty()) {
        rafState_ = WebRafState::SuspendedIdle;
    }
}

ZeroCopySubmissionStats WebCanvasHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    gpuStream.SetBackend(webBackend_);
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

} // namespace aero
