#include "aero/render/UIResourceStore.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

GradientBrushRegistry& GradientBrushRegistry::Instance() noexcept {
    return UIResourceStore::Default().GradientBrushes();
}

UIResourceStore::UIResourceStore(std::uint16_t fontAtlasSize)
    : fontAtlasSize_(std::max<std::uint16_t>(fontAtlasSize, 512u)) {}

UIResourceStore::~UIResourceStore() {
    ClearAll();
}

UIResourceStore& UIResourceStore::Default() noexcept {
    static UIResourceStore sDefaultStore(kDefaultFontAtlasSize);
    return sDefaultStore;
}

void UIResourceStore::EnsureFontsInitialized() const {
    if (fonts_ != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (fonts_ == nullptr) {
        fonts_ = std::make_shared<FontAtlas>(fontAtlasSize_);
    }
}

void UIResourceStore::EnsureImagesInitialized() const {
    if (images_ != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (images_ == nullptr) {
        images_ = std::make_shared<ImageAtlas>();
    }
}

FontAtlas& UIResourceStore::Fonts() {
    EnsureFontsInitialized();
    return *fonts_;
}

const FontAtlas& UIResourceStore::Fonts() const {
    EnsureFontsInitialized();
    return *fonts_;
}

void UIResourceStore::SetFontAtlas(std::shared_ptr<FontAtlas> fonts) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    fonts_ = std::move(fonts);
}

ImageAtlas& UIResourceStore::Images() {
    EnsureImagesInitialized();
    return *images_;
}

const ImageAtlas& UIResourceStore::Images() const {
    EnsureImagesInitialized();
    return *images_;
}

void UIResourceStore::SetImageAtlas(std::shared_ptr<ImageAtlas> images) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    images_ = std::move(images);
}

void UIResourceStore::AddAssetSearchDirectory(std::string_view directory) {
    if (directory.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& existing : assetSearchDirs_) {
        if (existing == directory) {
            return;
        }
    }
    assetSearchDirs_.emplace_back(directory);
}

void UIResourceStore::ClearAssetSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    assetSearchDirs_.clear();
}

std::vector<std::string> UIResourceStore::AssetSearchDirectoriesSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return assetSearchDirs_;
}

void UIResourceStore::RegisterMtsdfPath(const AxbMtsdfPathEntry& entry) {
    if (entry.pathHash == 0ull) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    mtsdfEntries_[entry.pathHash] = entry;
}

const AxbMtsdfPathEntry* UIResourceStore::FindMtsdfPath(std::uint64_t pathHash) const noexcept {
    if (pathHash == 0ull) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = mtsdfEntries_.find(pathHash);
    return (it != mtsdfEntries_.end()) ? &it->second : nullptr;
}

void UIResourceStore::ClearMtsdfPaths() noexcept {
    std::vector<std::uint32_t> toRelease;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mtsdfEntries_.clear();
        for (const auto& kv : pathMaskEntries_) {
            if (kv.second.textureId > 0u) {
                toRelease.push_back(kv.second.textureId);
            }
        }
        pathMaskEntries_.clear();
        pathMaskTotalBytes_ = 0u;
    }
    if (!toRelease.empty() && images_ != nullptr) {
        for (std::uint32_t id : toRelease) {
            images_->ReleaseRgbaTextureSlot(id);
        }
    }
}

void UIResourceStore::BeginPathMaskFrame() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    pathMaskFrameStartSeq_ = ++pathMaskAccessSeq_;
}

bool UIResourceStore::StoreOrLookupPathMaskLocked(
    std::uint64_t key,
    std::uint64_t groupHash,
    std::uint32_t texW,
    std::uint32_t texH,
    float u0,
    float v0,
    float u1,
    float v1,
    std::vector<std::uint8_t>&& rgba8,
    RasterizedPathMaskEntry& outEntry,
    bool allowSharedAtlasPack,
    bool isPureWhiteAlphaMask) {
    if (texW == 0u || texH == 0u || rgba8.size() < static_cast<std::size_t>(texW) * texH * 4u) {
        return false;
    }
    ImageAtlas& atlas = Images();
    const std::size_t incomingBytes = rgba8.size();
    constexpr std::size_t kMaxPathMaskEntries = 96u;
    constexpr std::size_t kMaxPathMaskTotalBytes = 16u * 1024u * 1024u;
    constexpr std::size_t kLargeMaskThresholdBytes = 64u * 1024u;

    bool attemptSharedPack = false;
    if (allowSharedAtlasPack &&
        incomingBytes < kLargeMaskThresholdBytes && texW <= 256u && texH <= 256u) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
        std::size_t sameGroupCount = 0u;
        if (groupHash != 0u) {
            for (const auto& kv : pathMaskEntries_) {
                if (kv.second.groupHash == groupHash) {
                    ++sameGroupCount;
                }
            }
        }
        attemptSharedPack = (sameGroupCount < 2u);
    }

    if (attemptSharedPack) {
        float aU0 = 0.0f, aV0 = 0.0f, aU1 = 1.0f, aV1 = 1.0f;
        if (atlas.TryPackMaskIntoSharedAtlas(key, texW, texH, rgba8.data(), rgba8.size(), aU0, aV0, aU1, aV1) &&
            ImageAtlas::kSharedUiAtlasTextureId == 0u) {
            const float spanU = aU1 - aU0;
            const float spanV = aV1 - aV0;
            RasterizedPathMaskEntry entry{};
            entry.textureId = 0u;
            entry.texWidth = texW;
            entry.texHeight = texH;
            entry.u0 = aU0 + u0 * spanU;
            entry.v0 = aV0 + v0 * spanV;
            entry.u1 = aU0 + u1 * spanU;
            entry.v1 = aV0 + v1 * spanV;
            entry.groupHash = groupHash;
            entry.byteSize = 0u;
            std::vector<std::uint32_t> sharedEvictIds;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (const auto prevIt = pathMaskEntries_.find(key); prevIt != pathMaskEntries_.end()) {
                    pathMaskTotalBytes_ = (pathMaskTotalBytes_ >= prevIt->second.byteSize)
                        ? (pathMaskTotalBytes_ - prevIt->second.byteSize)
                        : 0u;
                    if (prevIt->second.textureId > 0u) {
                        sharedEvictIds.push_back(prevIt->second.textureId);
                    }
                    prevIt->second = entry;
                    prevIt->second.lastUsedTick = ++pathMaskAccessSeq_;
                    entry.lastUsedTick = prevIt->second.lastUsedTick;
                } else {
                    while (!pathMaskEntries_.empty() && pathMaskEntries_.size() >= kMaxPathMaskEntries) {
                        auto lru = pathMaskEntries_.end();
                        for (auto it = pathMaskEntries_.begin(); it != pathMaskEntries_.end(); ++it) {
                            if (pathMaskFrameStartSeq_ > 0u && it->second.lastUsedTick > pathMaskFrameStartSeq_) {
                                continue;
                            }
                            if (lru == pathMaskEntries_.end() ||
                                it->second.lastUsedTick < lru->second.lastUsedTick) {
                                lru = it;
                            }
                        }
                        if (lru == pathMaskEntries_.end()) {
                            break;
                        }
                        pathMaskTotalBytes_ = (pathMaskTotalBytes_ >= lru->second.byteSize)
                            ? (pathMaskTotalBytes_ - lru->second.byteSize)
                            : 0u;
                        if (lru->second.textureId > 0u) {
                            sharedEvictIds.push_back(lru->second.textureId);
                        }
                        pathMaskEntries_.erase(lru);
                    }
                    entry.lastUsedTick = ++pathMaskAccessSeq_;
                    pathMaskEntries_.emplace(key, entry);
                }
            }
            for (std::uint32_t id : sharedEvictIds) {
                atlas.ReleaseRgbaTextureSlot(id);
            }
            outEntry = entry;
            return true;
        }
    }

    bool uploadAsAlpha8 = false;
    std::vector<std::uint8_t> uploadPixels;
    if (isPureWhiteAlphaMask) {
        const std::size_t pixelCount = static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH);
        uploadPixels.resize(pixelCount);
        for (std::size_t i = 0u; i < pixelCount; ++i) {
            uploadPixels[i] = rgba8[i * 4u + 3u];
        }
        std::vector<std::uint8_t>().swap(rgba8);
        uploadAsAlpha8 = true;
    } else {
        uploadPixels = std::move(rgba8);
    }
    const std::size_t slotIncomingBytes = uploadPixels.size();

    std::uint32_t reuseSlotId = 0u;
    std::vector<std::uint32_t> surplusReleaseIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }

        auto evictIter = [&](std::unordered_map<std::uint64_t, RasterizedPathMaskEntry>::iterator victim) {
            if (victim == pathMaskEntries_.end()) {
                return;
            }
            pathMaskTotalBytes_ = (pathMaskTotalBytes_ >= victim->second.byteSize)
                ? (pathMaskTotalBytes_ - victim->second.byteSize)
                : 0u;
            if (victim->second.textureId > 0u) {
                if (reuseSlotId == 0u) {
                    reuseSlotId = victim->second.textureId;
                } else {
                    surplusReleaseIds.push_back(victim->second.textureId);
                }
            }
            pathMaskEntries_.erase(victim);
        };

        if (groupHash != 0u && incomingBytes >= kLargeMaskThresholdBytes) {
            std::size_t sameGroupCount = 0u;
            auto oldestSameGroup = pathMaskEntries_.end();
            for (auto it = pathMaskEntries_.begin(); it != pathMaskEntries_.end(); ++it) {
                if (it->second.groupHash == groupHash) {
                    ++sameGroupCount;
                    if ((pathMaskFrameStartSeq_ == 0u || it->second.lastUsedTick <= pathMaskFrameStartSeq_) &&
                        (oldestSameGroup == pathMaskEntries_.end() ||
                         it->second.lastUsedTick < oldestSameGroup->second.lastUsedTick)) {
                        oldestSameGroup = it;
                    }
                }
            }
            if (sameGroupCount >= 2u && oldestSameGroup != pathMaskEntries_.end()) {
                evictIter(oldestSameGroup);
            }
        }

        while (!pathMaskEntries_.empty() &&
               (pathMaskEntries_.size() >= kMaxPathMaskEntries ||
                (pathMaskTotalBytes_ + slotIncomingBytes > kMaxPathMaskTotalBytes))) {
            auto lruStandalone = pathMaskEntries_.end();
            auto lruAny = pathMaskEntries_.end();
            for (auto it = pathMaskEntries_.begin(); it != pathMaskEntries_.end(); ++it) {
                if (pathMaskFrameStartSeq_ > 0u && it->second.lastUsedTick > pathMaskFrameStartSeq_) {
                    continue;
                }
                if (lruAny == pathMaskEntries_.end() ||
                    it->second.lastUsedTick < lruAny->second.lastUsedTick) {
                    lruAny = it;
                }
                if (it->second.byteSize > 0u &&
                    (lruStandalone == pathMaskEntries_.end() ||
                     it->second.lastUsedTick < lruStandalone->second.lastUsedTick)) {
                    lruStandalone = it;
                }
            }
            if (lruStandalone != pathMaskEntries_.end() &&
                (pathMaskTotalBytes_ + slotIncomingBytes > kMaxPathMaskTotalBytes)) {
                evictIter(lruStandalone);
            } else if (lruAny != pathMaskEntries_.end()) {
                evictIter(lruAny);
            } else {
                break;
            }
        }
    }

    for (std::uint32_t id : surplusReleaseIds) {
        atlas.ReleaseRgbaTextureSlot(id);
    }

    const std::uint32_t texId = uploadAsAlpha8
        ? atlas.RegisterOrUpdateAlpha8TextureSlot(reuseSlotId, texW, texH, std::move(uploadPixels))
        : atlas.RegisterOrUpdateRgbaTextureSlot(reuseSlotId, texW, texH, std::move(uploadPixels));
    if (texId == 0u) {
        if (reuseSlotId != 0u) {
            atlas.ReleaseRgbaTextureSlot(reuseSlotId);
        }
        return false;
    }

    RasterizedPathMaskEntry entry{};
    entry.textureId = texId;
    entry.texWidth = texW;
    entry.texHeight = texH;
    entry.u0 = u0;
    entry.v0 = v0;
    entry.u1 = u1;
    entry.v1 = v1;
    entry.groupHash = groupHash;
    entry.byteSize = slotIncomingBytes;

    std::uint32_t replacedSlotId = 0u;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto prevIt = pathMaskEntries_.find(key); prevIt != pathMaskEntries_.end()) {
            pathMaskTotalBytes_ = (pathMaskTotalBytes_ >= prevIt->second.byteSize)
                ? (pathMaskTotalBytes_ - prevIt->second.byteSize)
                : 0u;
            if (prevIt->second.textureId > 0u && prevIt->second.textureId != texId) {
                replacedSlotId = prevIt->second.textureId;
            }
            prevIt->second = entry;
            prevIt->second.lastUsedTick = ++pathMaskAccessSeq_;
            entry.lastUsedTick = prevIt->second.lastUsedTick;
        } else {
            entry.lastUsedTick = ++pathMaskAccessSeq_;
            pathMaskEntries_.emplace(key, entry);
        }
        pathMaskTotalBytes_ += slotIncomingBytes;
    }
    if (replacedSlotId != 0u) {
        atlas.ReleaseRgbaTextureSlot(replacedSlotId);
    }
    outEntry = entry;
    return true;
}

bool UIResourceStore::LookupCachedPathMask(
    std::uint64_t key,
    RasterizedPathMaskEntry& outEntry) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
        it->second.lastUsedTick = ++pathMaskAccessSeq_;
        outEntry = it->second;
        return true;
    }
    return false;
}

bool UIResourceStore::StoreCachedPathMask(
    std::uint64_t key,
    std::uint32_t texW,
    std::uint32_t texH,
    float u0,
    float v0,
    float u1,
    float v1,
    std::vector<std::uint8_t>&& rgba8,
    RasterizedPathMaskEntry& outEntry,
    std::uint64_t groupHash) {
    return StoreOrLookupPathMaskLocked(
        key,
        groupHash != 0u ? groupHash : key,
        texW,
        texH,
        u0,
        v0,
        u1,
        v1,
        std::move(rgba8),
        outEntry);
}

bool UIResourceStore::GetOrRasterizePathMask(
    std::uint64_t pathHash,
    std::string_view pathData,
    const Rect& rawBounds,
    std::uint32_t innerW,
    std::uint32_t innerH,
    std::uint32_t padPx,
    RasterizedPathMaskEntry& outEntry) {
    if (pathData.empty()) {
        return false;
    }
    const std::uint64_t safeHash = (pathHash != 0ull) ? pathHash : ComputeSvgPathHash(pathData);
    constexpr std::uint32_t kMaxPathMaskDim = 1024u;
    std::uint32_t clampedW = std::clamp<std::uint32_t>(innerW, 1u, 2048u);
    std::uint32_t clampedH = std::clamp<std::uint32_t>(innerH, 1u, 2048u);
    const std::uint32_t maxDimRaw = std::max(clampedW, clampedH);
    if (maxDimRaw > kMaxPathMaskDim) {
        const float down = static_cast<float>(kMaxPathMaskDim) / static_cast<float>(maxDimRaw);
        clampedW = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedW) * down)));
        clampedH = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedH) * down)));
    }
    const std::uint32_t clampedPad = std::min<std::uint32_t>(padPx, 64u);

    auto makeMaskKey = [&](std::uint32_t w, std::uint32_t h) noexcept -> std::uint64_t {
        std::uint64_t k = safeHash ^ 0x9E3779B97F4A7C15ULL;
        k ^= (static_cast<std::uint64_t>(w) << 32u) | static_cast<std::uint64_t>(h);
        k *= 1099511628211ULL;
        k ^= static_cast<std::uint64_t>(clampedPad);
        k *= 1099511628211ULL;
        return k;
    };

    const std::uint64_t fillGroup = safeHash ^ 0xA5A5A5A500000001ULL;
    std::uint32_t effW = clampedW;
    std::uint32_t effH = clampedH;
    std::uint64_t key = makeMaskKey(effW, effH);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
        bool hasSameGroup = false;
        for (const auto& kv : pathMaskEntries_) {
            if (kv.second.groupHash == fillGroup) {
                hasSameGroup = true;
                break;
            }
        }
        if (hasSameGroup) {
            const std::uint32_t needTexW = clampedW + 2u * clampedPad;
            const std::uint32_t needTexH = clampedH + 2u * clampedPad;
            const RasterizedPathMaskEntry* bestExisting = nullptr;
            std::uint64_t bestKey = 0u;
            std::uint64_t bestArea = 0u;
            for (const auto& kv : pathMaskEntries_) {
                if (kv.second.groupHash == fillGroup &&
                    kv.second.texWidth >= needTexW &&
                    kv.second.texHeight >= needTexH &&
                    kv.second.texWidth <= needTexW * 2u &&
                    kv.second.texHeight <= needTexH * 2u) {
                    const std::uint64_t area =
                        static_cast<std::uint64_t>(kv.second.texWidth) * kv.second.texHeight;
                    if (bestExisting == nullptr || area < bestArea) {
                        bestExisting = &kv.second;
                        bestKey = kv.first;
                        bestArea = area;
                    }
                }
            }
            if (bestExisting != nullptr) {
                auto hitIt = pathMaskEntries_.find(bestKey);
                hitIt->second.lastUsedTick = ++pathMaskAccessSeq_;
                outEntry = hitIt->second;
                return true;
            }
            static constexpr std::uint32_t kBuckets[] = {
                32u, 64u, 128u, 256u, 512u, 1024u
            };
            const std::uint32_t maxDim = std::max(clampedW, clampedH);
            std::uint32_t bucketMax = kMaxPathMaskDim;
            for (const std::uint32_t b : kBuckets) {
                if (b >= maxDim) {
                    bucketMax = b;
                    break;
                }
            }
            if (maxDim > 0u && bucketMax != maxDim) {
                const float scale = static_cast<float>(bucketMax) / static_cast<float>(maxDim);
                effW = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedW) * scale)), 1u, 2048u);
                effH = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedH) * scale)), 1u, 2048u);
                key = makeMaskKey(effW, effH);
                if (const auto bit = pathMaskEntries_.find(key); bit != pathMaskEntries_.end()) {
                    bit->second.lastUsedTick = ++pathMaskAccessSeq_;
                    outEntry = bit->second;
                    return true;
                }
            }
        }
    }

    std::vector<std::uint8_t> rgba8;
    if (!RasterizeSvgPathFillAlphaMask(pathData, rawBounds, effW, effH, clampedPad, rgba8)) {
        return false;
    }

    const std::uint32_t texW = effW + 2u * clampedPad;
    const std::uint32_t texH = effH + 2u * clampedPad;
    const float u0 = static_cast<float>(clampedPad) / static_cast<float>(texW);
    const float v0 = static_cast<float>(clampedPad) / static_cast<float>(texH);
    const float u1 = static_cast<float>(clampedPad + effW) / static_cast<float>(texW);
    const float v1 = static_cast<float>(clampedPad + effH) / static_cast<float>(texH);
    return StoreOrLookupPathMaskLocked(key, fillGroup, texW, texH, u0, v0, u1, v1, std::move(rgba8), outEntry, true, true);
}

bool UIResourceStore::GetOrRasterizePathStrokeMask(
    std::uint64_t pathHash,
    std::string_view pathData,
    const Rect& rawBounds,
    std::uint32_t innerW,
    std::uint32_t innerH,
    float strokeWidthPx,
    std::uint32_t padPx,
    RasterizedPathMaskEntry& outEntry,
    std::uint8_t startLineCap,
    std::uint8_t endLineCap) {
    if (pathData.empty() || strokeWidthPx <= 0.0f) {
        return false;
    }
    const std::uint64_t safeHash = (pathHash != 0ull) ? pathHash : ComputeSvgPathHash(pathData);
    constexpr std::uint32_t kMaxStrokeMaskDim = 1024u;
    std::uint32_t clampedW = std::clamp<std::uint32_t>(innerW, 1u, 2048u);
    std::uint32_t clampedH = std::clamp<std::uint32_t>(innerH, 1u, 2048u);
    float scaledStrokeW = strokeWidthPx;
    std::uint32_t clampedPad = std::clamp<std::uint32_t>(padPx, 1u, 256u);
    const std::uint32_t maxStrokeDimRaw = std::max(clampedW, clampedH);
    if (maxStrokeDimRaw > kMaxStrokeMaskDim) {
        const float down = static_cast<float>(kMaxStrokeMaskDim) / static_cast<float>(maxStrokeDimRaw);
        clampedW = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedW) * down)));
        clampedH = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedH) * down)));
        scaledStrokeW = std::max(0.25f, strokeWidthPx * down);
        clampedPad = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::ceil(static_cast<float>(clampedPad) * down)));
    }
    const auto capBits = (static_cast<std::uint64_t>(startLineCap & 0xFu) << 4u) |
                         static_cast<std::uint64_t>(endLineCap & 0xFu);

    auto makeStrokeKey = [&](std::uint32_t w, std::uint32_t h, float sW) noexcept -> std::uint64_t {
        const auto qStroke = static_cast<std::uint64_t>(std::max<long>(1L, std::lround(sW * 16.0f)));
        std::uint64_t k = safeHash ^ 0xD1B54A32D192ED03ULL;
        k ^= (static_cast<std::uint64_t>(w) << 32u) | static_cast<std::uint64_t>(h);
        k *= 1099511628211ULL;
        k ^= (qStroke << 16u) | (capBits << 8u) | static_cast<std::uint64_t>(clampedPad);
        k *= 1099511628211ULL;
        return k;
    };

    const std::uint64_t strokeGroup = safeHash ^ 0x5A5A5A5A00000002ULL;
    std::uint32_t effW = clampedW;
    std::uint32_t effH = clampedH;
    float effStrokeW = scaledStrokeW;
    std::uint64_t key = makeStrokeKey(effW, effH, effStrokeW);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
        bool hasSameGroup = false;
        for (const auto& kv : pathMaskEntries_) {
            if (kv.second.groupHash == strokeGroup) {
                hasSameGroup = true;
                break;
            }
        }
        if (hasSameGroup) {
            static constexpr std::uint32_t kBuckets[] = {
                32u, 64u, 128u, 256u, 512u, 1024u
            };
            const std::uint32_t maxDim = std::max(clampedW, clampedH);
            std::uint32_t bucketMax = kMaxStrokeMaskDim;
            for (const std::uint32_t b : kBuckets) {
                if (b >= maxDim) {
                    bucketMax = b;
                    break;
                }
            }
            if (maxDim > 0u && bucketMax != maxDim) {
                const float scale = static_cast<float>(bucketMax) / static_cast<float>(maxDim);
                effW = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedW) * scale)), 1u, kMaxStrokeMaskDim);
                effH = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(static_cast<float>(clampedH) * scale)), 1u, kMaxStrokeMaskDim);
                effStrokeW = std::max(0.25f, scaledStrokeW * scale);
                key = makeStrokeKey(effW, effH, effStrokeW);
                if (const auto bit = pathMaskEntries_.find(key); bit != pathMaskEntries_.end()) {
                    bit->second.lastUsedTick = ++pathMaskAccessSeq_;
                    outEntry = bit->second;
                    return true;
                }
            }
        }
    }

    std::vector<std::uint8_t> rgba8;
    if (!RasterizeSvgPathStrokeAlphaMask(
            pathData, rawBounds, effW, effH, effStrokeW, clampedPad, rgba8, startLineCap, endLineCap)) {
        return false;
    }

    const std::uint32_t texW = effW + 2u * clampedPad;
    const std::uint32_t texH = effH + 2u * clampedPad;
    return StoreOrLookupPathMaskLocked(key, strokeGroup, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, std::move(rgba8), outEntry, true, true);
}

void UIResourceStore::ClearAll() noexcept {
    if (fonts_ != nullptr) {
        fonts_->ClearCustomFonts();
    }
    if (images_ != nullptr) {
        images_->Clear();
    }
    gradientBrushes_.Clear();
    std::lock_guard<std::mutex> lock(mutex_);
    assetSearchDirs_.clear();
    mtsdfEntries_.clear();
    pathMaskEntries_.clear();
    pathMaskTotalBytes_ = 0u;
    pathMaskAccessSeq_ = 0u;
    pathMaskFrameStartSeq_ = 0u;
}

std::size_t UIResourceStore::EstimatedMemoryBytes() const noexcept {
    std::size_t total = 0u;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        total += pathMaskTotalBytes_;
        for (const auto& [k, e] : mtsdfEntries_) {
            (void)k;
            total += e.mtsdfRgbaPixels.capacity();
        }
    }
    return total;
}

} // namespace aero
