#include "aero/render/ImageAtlas.hpp"
#include "aero/asset/AssetUriResolver.hpp"
#include "aero/render/RiveRenderer.hpp"
#include "aero/render/UIResourceStore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace aero {

namespace {

void SetError(std::string* outErr, const char* msg) {
    if (outErr) {
        *outErr = msg;
    }
}

[[nodiscard]] bool FileExistsOnDisk(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    std::fclose(fp);
    return true;
}

[[nodiscard]] bool ReadAllFileBytes(const std::string& path, std::vector<std::uint8_t>& outBytes) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    const long sz = std::ftell(fp);
    if (sz <= 0) {
        std::fclose(fp);
        return false;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0) {
        std::fclose(fp);
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(sz));
    const std::size_t nRead = std::fread(outBytes.data(), 1u, outBytes.size(), fp);
    std::fclose(fp);
    return nRead == outBytes.size();
}

} // namespace

// ============================================================================
// URI Normalization & Stretch Geometry / UV Cropping
// ============================================================================

std::string NormalizeImageUriPath(std::string_view rawUri, std::string_view baseDir) {
    return NormalizeAssetUriPath(rawUri, baseDir);
}

ImageStretchPlacement ComputeImageStretchPlacement(
    const Rect& contentBox,
    float naturalWidth,
    float naturalHeight,
    Stretch stretch,
    StretchDirection stretchDirection,
    float baseU0,
    float baseV0,
    float baseU1,
    float baseV1) noexcept {
    ImageStretchPlacement out{};
    if (contentBox.width <= 0.0f || contentBox.height <= 0.0f ||
        naturalWidth <= 0.0f || naturalHeight <= 0.0f) {
        return out;
    }

    float sx = 1.0f;
    float sy = 1.0f;
    switch (stretch) {
        case Stretch::None:
            sx = 1.0f;
            sy = 1.0f;
            break;
        case Stretch::Fill:
            sx = contentBox.width / naturalWidth;
            sy = contentBox.height / naturalHeight;
            break;
        case Stretch::Uniform: {
            const float s = std::min(contentBox.width / naturalWidth, contentBox.height / naturalHeight);
            sx = s;
            sy = s;
            break;
        }
        case Stretch::UniformToFill: {
            const float s = std::max(contentBox.width / naturalWidth, contentBox.height / naturalHeight);
            sx = s;
            sy = s;
            break;
        }
    }

    if (stretch != Stretch::None) {
        if (stretchDirection == StretchDirection::UpOnly) {
            sx = std::max(1.0f, sx);
            sy = std::max(1.0f, sy);
        } else if (stretchDirection == StretchDirection::DownOnly) {
            sx = std::min(1.0f, sx);
            sy = std::min(1.0f, sy);
        }
    }

    const float virtualW = std::max(1e-4f, naturalWidth * sx);
    const float virtualH = std::max(1e-4f, naturalHeight * sy);
    const float vx = contentBox.x + (contentBox.width - virtualW) * 0.5f;
    const float vy = contentBox.y + (contentBox.height - virtualH) * 0.5f;

    // Intersect centered virtual image rect with contentBox and compute exact cropped UV sub-rect
    // (eliminates GPU Scissor CommandPage breaks for UniformToFill and oversized None images!)
    const float ix0 = std::max(contentBox.x, vx);
    const float iy0 = std::max(contentBox.y, vy);
    const float ix1 = std::min(contentBox.x + contentBox.width, vx + virtualW);
    const float iy1 = std::min(contentBox.y + contentBox.height, vy + virtualH);
    if (ix1 <= ix0 || iy1 <= iy0) {
        return out;
    }

    const float cropU0 = std::clamp((ix0 - vx) / virtualW, 0.0f, 1.0f);
    const float cropU1 = std::clamp((ix1 - vx) / virtualW, 0.0f, 1.0f);
    const float cropV0 = std::clamp((iy0 - vy) / virtualH, 0.0f, 1.0f);
    const float cropV1 = std::clamp((iy1 - vy) / virtualH, 0.0f, 1.0f);

    const float spanU = baseU1 - baseU0;
    const float spanV = baseV1 - baseV0;

    out.destRect = Rect{ix0, iy0, ix1 - ix0, iy1 - iy0};
    out.u0 = baseU0 + cropU0 * spanU;
    out.u1 = baseU0 + cropU1 * spanU;
    out.v0 = baseV0 + cropV0 * spanV;
    out.v1 = baseV0 + cropV1 * spanV;
    out.valid = true;
    return out;
}

// ============================================================================
// ImageAtlas Implementation (Skyline Bin Packer + Multi-Texture Slot Manager)
// ============================================================================

ImageAtlas& ImageAtlas::Instance() noexcept {
    return UIResourceStore::Default().Images();
}

namespace {

std::atomic<std::uint64_t> gNextImageAtlasInstanceId{1u};

} // namespace

ImageAtlas::ImageAtlas() noexcept
    : instanceId_(gNextImageAtlasInstanceId.fetch_add(1u, std::memory_order_relaxed)) {}

void ImageAtlas::ConfigureAtlas(
    std::uint32_t sharedAtlasSize,
    std::uint32_t maxPackableWidth,
    std::uint32_t maxPackableHeight) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t clampedAtlas = std::clamp(sharedAtlasSize, 64u, 8192u);
    if (clampedAtlas != sharedAtlasSize_ && textureSlots_.find(kSharedUiAtlasTextureId) == textureSlots_.end()) {
        sharedAtlasSize_ = clampedAtlas;
    } else if (textureSlots_.find(kSharedUiAtlasTextureId) == textureSlots_.end()) {
        sharedAtlasSize_ = clampedAtlas;
    }
    const std::uint32_t maxPackLimit = std::max(sharedAtlasSize_, kDefaultMaxSharedAtlasSize) - 2u;
    maxPackableWidth_ = std::min(maxPackableWidth, maxPackLimit);
    maxPackableHeight_ = std::min(maxPackableHeight, maxPackLimit);
    revision_.fetch_add(1u, std::memory_order_release);
}

std::uint32_t ImageAtlas::SharedAtlasSize() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sharedAtlasSize_;
}

std::uint32_t ImageAtlas::MaxPackableWidth() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxPackableWidth_;
}

std::uint32_t ImageAtlas::MaxPackableHeight() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxPackableHeight_;
}

std::size_t ImageAtlas::SharedAtlasPageCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sharedAtlasPages_.size();
}

void ImageAtlas::SetBaseDirectory(std::string_view baseDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    baseDirectory_ = NormalizeImageUriPath(baseDir);
}

std::string ImageAtlas::GetBaseDirectory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return baseDirectory_;
}

void ImageAtlas::AddSearchDirectory(std::string_view dirPath) {
    std::string norm = NormalizeImageUriPath(dirPath);
    if (norm.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(searchDirectories_.begin(), searchDirectories_.end(), norm) == searchDirectories_.end()) {
        searchDirectories_.push_back(std::move(norm));
    }
}

void ImageAtlas::ClearSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    searchDirectories_.clear();
}

bool ImageAtlas::RegisterEncodedMemoryAsset(
    std::string_view uriKey,
    const std::uint8_t* encodedBytes,
    std::size_t byteSize) {
    const std::string normKey = NormalizeImageUriPath(uriKey);
    if (normKey.empty() || !encodedBytes || byteSize == 0u) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    memoryEncodedAssets_[normKey] = std::vector<std::uint8_t>(encodedBytes, encodedBytes + byteSize);
    return true;
}

void ImageAtlas::EnsureWritableAtlasPixelsLocked(
    TextureSlotBuffer& atlasBuf,
    std::uint32_t atlasSize) {
    const std::size_t byteSize = static_cast<std::size_t>(atlasSize) * atlasSize * 4u;
    if (!atlasBuf.cowAtlasPixels) {
        atlasBuf.cowAtlasPixels = std::make_shared<std::vector<std::uint8_t>>(byteSize, 0u);
    } else if (atlasBuf.cowAtlasPixels.use_count() > 1) {
        atlasBuf.cowAtlasPixels = std::make_shared<std::vector<std::uint8_t>>(*atlasBuf.cowAtlasPixels);
    }
}

void ImageAtlas::EnsureSharedAtlasAllocatedLocked() {
    if (!sharedAtlasPages_.empty() &&
        textureSlots_.find(kSharedUiAtlasTextureId) != textureSlots_.end()) {
        return;
    }
    if (textureSlots_.find(kSharedUiAtlasTextureId) == textureSlots_.end()) {
        TextureSlotBuffer slot{};
        slot.textureId = kSharedUiAtlasTextureId;
        slot.width = sharedAtlasSize_;
        slot.height = sharedAtlasSize_;
        slot.dirtyRevision = 1u;
        slot.isAtlas = true;
        EnsureWritableAtlasPixelsLocked(slot, sharedAtlasSize_);
        slot.pixelSnapshot.reset();
        slot.hasDirtyRect = false;
        slot.dirtyBaseRevision = 1u;
        textureSlots_.emplace(kSharedUiAtlasTextureId, std::move(slot));
        revision_.fetch_add(1u, std::memory_order_release);
    }
    if (sharedAtlasPages_.empty()) {
        SharedAtlasPage page0{};
        page0.textureId = kSharedUiAtlasTextureId;
        page0.size = sharedAtlasSize_;
        page0.skyline.Reset(sharedAtlasSize_, sharedAtlasSize_);
        sharedAtlasPages_.push_back(std::move(page0));
    }
}

bool ImageAtlas::TryAllocateSkylineRectLocked(
    std::uint32_t paddedW,
    std::uint32_t paddedH,
    std::uint32_t& outX,
    std::uint32_t& outY) noexcept {
    if (sharedAtlasPages_.empty()) {
        return false;
    }
    return sharedAtlasPages_[0].skyline.TryAllocate(paddedW, paddedH, outX, outY);
}

void ImageAtlas::BlitWithExtrudedBorderLocked(
    TextureSlotBuffer& atlasBuf,
    std::uint32_t innerX,
    std::uint32_t innerY,
    const DecodedImage& img) noexcept {
    BlitWithExtrudedBorderLocked(
        atlasBuf, innerX, innerY, img.width, img.height, img.rgbaPixels.data());
}

void ImageAtlas::BlitWithExtrudedBorderLocked(
    TextureSlotBuffer& atlasBuf,
    std::uint32_t innerX,
    std::uint32_t innerY,
    std::uint32_t iw,
    std::uint32_t ih,
    const std::uint8_t* srcPixels) noexcept {
    EnsureWritableAtlasPixelsLocked(atlasBuf, atlasBuf.width);
    std::uint8_t* const atlasData = atlasBuf.cowAtlasPixels ? atlasBuf.cowAtlasPixels->data() : nullptr;
    if (!atlasData) {
        return;
    }
    const std::uint32_t aw = atlasBuf.width;

    auto copyTexel = [&](std::uint32_t dstX, std::uint32_t dstY, std::uint32_t srcX, std::uint32_t srcY) noexcept {
        std::uint8_t* dst = atlasData + (static_cast<std::size_t>(dstY) * aw + dstX) * 4u;
        const std::uint8_t* src = srcPixels + (static_cast<std::size_t>(srcY) * iw + srcX) * 4u;
        std::memcpy(dst, src, 4u);
    };

    // Copy interior rows
    for (std::uint32_t y = 0u; y < ih; ++y) {
        std::uint8_t* dstRow = atlasData +
            (static_cast<std::size_t>(innerY + y) * aw + innerX) * 4u;
        const std::uint8_t* srcRow = srcPixels + static_cast<std::size_t>(y) * iw * 4u;
        std::memcpy(dstRow, srcRow, static_cast<std::size_t>(iw) * 4u);

        // 1px left & right extruded border
        if (innerX > 0u) {
            copyTexel(innerX - 1u, innerY + y, 0u, y);
        }
        if (innerX + iw < aw) {
            copyTexel(innerX + iw, innerY + y, iw - 1u, y);
        }
    }

    // 1px top & bottom extruded border + 4 corners
    if (innerY > 0u) {
        std::uint8_t* topDst = atlasData +
            (static_cast<std::size_t>(innerY - 1u) * aw + innerX) * 4u;
        const std::uint8_t* topSrc = srcPixels;
        std::memcpy(topDst, topSrc, static_cast<std::size_t>(iw) * 4u);
        if (innerX > 0u) copyTexel(innerX - 1u, innerY - 1u, 0u, 0u);
        if (innerX + iw < aw) copyTexel(innerX + iw, innerY - 1u, iw - 1u, 0u);
    }
    if (innerY + ih < atlasBuf.height) {
        std::uint8_t* botDst = atlasData +
            (static_cast<std::size_t>(innerY + ih) * aw + innerX) * 4u;
        const std::uint8_t* botSrc = srcPixels + static_cast<std::size_t>(ih - 1u) * iw * 4u;
        std::memcpy(botDst, botSrc, static_cast<std::size_t>(iw) * 4u);
        if (innerX > 0u) copyTexel(innerX - 1u, innerY + ih, 0u, ih - 1u);
        if (innerX + iw < aw) copyTexel(innerX + iw, innerY + ih, iw - 1u, ih - 1u);
    }

    atlasBuf.pixelSnapshot.reset();
    const std::uint32_t bx0 = (innerX > 0u) ? (innerX - 1u) : 0u;
    const std::uint32_t by0 = (innerY > 0u) ? (innerY - 1u) : 0u;
    const std::uint32_t bx1 = std::min<std::uint32_t>(atlasBuf.width, innerX + iw + 1u);
    const std::uint32_t by1 = std::min<std::uint32_t>(atlasBuf.height, innerY + ih + 1u);
    if (!atlasBuf.hasDirtyRect) {
        atlasBuf.dirtyBaseRevision = atlasBuf.dirtyRevision;
        atlasBuf.dirtyX0 = bx0;
        atlasBuf.dirtyY0 = by0;
        atlasBuf.dirtyX1 = bx1;
        atlasBuf.dirtyY1 = by1;
        atlasBuf.hasDirtyRect = true;
    } else {
        atlasBuf.dirtyX0 = std::min(atlasBuf.dirtyX0, bx0);
        atlasBuf.dirtyY0 = std::min(atlasBuf.dirtyY0, by0);
        atlasBuf.dirtyX1 = std::max(atlasBuf.dirtyX1, bx1);
        atlasBuf.dirtyY1 = std::max(atlasBuf.dirtyY1, by1);
    }
    const std::uint64_t nextRev = revision_.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    atlasBuf.dirtyRevision = static_cast<std::uint32_t>(nextRev != 0u ? nextRev : 1u);
}

bool ImageAtlas::TryPackMaskIntoSharedAtlas(
    std::uint64_t maskKey,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgbaPixels,
    std::size_t byteSize,
    float& outU0,
    float& outV0,
    float& outU1,
    float& outV1) {
    if (width == 0u || height == 0u || rgbaPixels == nullptr ||
        byteSize < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = packedMasks_.find(maskKey); it != packedMasks_.end()) {
        if (it->second.width == width && it->second.height == height) {
            outU0 = it->second.u0;
            outV0 = it->second.v0;
            outU1 = it->second.u1;
            outV1 = it->second.v1;
            return true;
        }
    }

    if (width > maxPackableWidth_ ||
        height > maxPackableHeight_ ||
        (width + 2u) > sharedAtlasSize_ ||
        (height + 2u) > sharedAtlasSize_ ||
        packedMasks_.size() >= 1024u) {
        return false;
    }

    const std::size_t reqArea = static_cast<std::size_t>(width + 2u) * static_cast<std::size_t>(height + 2u);
    const std::size_t maxMaskBudget =
        (static_cast<std::size_t>(sharedAtlasSize_) * static_cast<std::size_t>(sharedAtlasSize_)) / 2u;
    if (packedMaskAreaTexels_ + reqArea > maxMaskBudget) {
        return false;
    }

    EnsureSharedAtlasAllocatedLocked();
    std::uint32_t paddedX = 0u;
    std::uint32_t paddedY = 0u;
    if (!TryAllocateSkylineRectLocked(width + 2u, height + 2u, paddedX, paddedY)) {
        return false;
    }

    const std::uint32_t innerX = paddedX + 1u;
    const std::uint32_t innerY = paddedY + 1u;
    TextureSlotBuffer& sharedBuf = textureSlots_[kSharedUiAtlasTextureId];
    BlitWithExtrudedBorderLocked(sharedBuf, innerX, innerY, width, height, rgbaPixels);

    const float invAtlasW = 1.0f / static_cast<float>(sharedAtlasSize_);
    const float invAtlasH = 1.0f / static_cast<float>(sharedAtlasSize_);
    PackedMaskUvEntry entry{};
    entry.width = width;
    entry.height = height;
    entry.u0 = static_cast<float>(innerX) * invAtlasW;
    entry.v0 = static_cast<float>(innerY) * invAtlasH;
    entry.u1 = static_cast<float>(innerX + width) * invAtlasW;
    entry.v1 = static_cast<float>(innerY + height) * invAtlasH;
    packedMasks_[maskKey] = entry;
    packedMaskAreaTexels_ += reqArea;

    outU0 = entry.u0;
    outV0 = entry.v0;
    outU1 = entry.u1;
    outV1 = entry.v1;
    return true;
}

const ImageAtlasEntry* ImageAtlas::InsertDecodedLocked(
    std::string canonicalKey,
    DecodedImage&& image,
    bool forceStandalone) {
    if (!image.Valid()) {
        return nullptr;
    }
    auto pixels = std::make_shared<const std::vector<std::uint8_t>>(std::move(image.rgbaPixels));
    return InsertDecodedSharedLocked(
        std::move(canonicalKey),
        image.width,
        image.height,
        std::move(pixels),
        forceStandalone);
}

const ImageAtlasEntry* ImageAtlas::InsertDecodedSharedLocked(
    std::string canonicalKey,
    std::uint32_t width,
    std::uint32_t height,
    std::shared_ptr<const std::vector<std::uint8_t>> pixels,
    bool forceStandalone,
    std::string reloadableFilePath,
    std::string reloadableMemAssetKey) {
    const std::size_t expectedBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (width == 0u || height == 0u || !pixels || pixels->size() != expectedBytes) {
        return nullptr;
    }

    if (const auto existing = entries_.find(canonicalKey); existing != entries_.end()) {
        return &existing->second;
    }

    const bool fitsPackThreshold = !forceStandalone &&
        width <= maxPackableWidth_ &&
        height <= maxPackableHeight_ &&
        (width + 2u) <= kDefaultMaxSharedAtlasSize &&
        (height + 2u) <= kDefaultMaxSharedAtlasSize;

    ImageAtlasEntry entry{};
    entry.uriKey = canonicalKey;
    entry.width = width;
    entry.height = height;

    bool packedInSharedAtlas = false;
    if (fitsPackThreshold) {
        EnsureSharedAtlasAllocatedLocked();
        const std::uint32_t reqW = width + 2u;
        const std::uint32_t reqH = height + 2u;
        auto packIntoPage = [&](SharedAtlasPage& pg) -> bool {
            if (reqW > pg.size || reqH > pg.size) {
                return false;
            }
            std::uint32_t paddedX = 0u;
            std::uint32_t paddedY = 0u;
            if (!pg.skyline.TryAllocate(reqW, reqH, paddedX, paddedY)) {
                return false;
            }
            const std::uint32_t innerX = paddedX + 1u;
            const std::uint32_t innerY = paddedY + 1u;
            TextureSlotBuffer& sharedBuf = textureSlots_[pg.textureId];
            BlitWithExtrudedBorderLocked(sharedBuf, innerX, innerY, width, height, pixels->data());

            const float invAtlas = 1.0f / static_cast<float>(pg.size);
            entry.textureId = pg.textureId;
            entry.atlasX = innerX;
            entry.atlasY = innerY;
            entry.atlasWidth = pg.size;
            entry.atlasHeight = pg.size;
            entry.u0 = static_cast<float>(innerX) * invAtlas;
            entry.v0 = static_cast<float>(innerY) * invAtlas;
            entry.u1 = static_cast<float>(innerX + width) * invAtlas;
            entry.v1 = static_cast<float>(innerY + height) * invAtlas;
            entry.isStandalone = false;
            return true;
        };

        for (SharedAtlasPage& pg : sharedAtlasPages_) {
            if (packIntoPage(pg)) {
                packedInSharedAtlas = true;
                break;
            }
        }

        if (!packedInSharedAtlas && sharedAtlasPages_.size() < kMaxSharedAtlasPages) {
            std::uint32_t nextSize = std::max<std::uint32_t>(1024u, sharedAtlasSize_);
            const std::uint32_t needExtent = std::max(reqW, reqH);
            while (nextSize < needExtent && nextSize < kDefaultMaxSharedAtlasSize) {
                nextSize *= 2u;
            }
            nextSize = std::min(nextSize, kDefaultMaxSharedAtlasSize);
            if (needExtent <= nextSize) {
                const std::uint32_t pageTexId = AllocateStandaloneTextureIdLocked();
                TextureSlotBuffer pageSlot{};
                pageSlot.textureId = pageTexId;
                pageSlot.width = nextSize;
                pageSlot.height = nextSize;
                pageSlot.dirtyRevision = 1u;
                pageSlot.isAtlas = true;
                EnsureWritableAtlasPixelsLocked(pageSlot, nextSize);
                pageSlot.pixelSnapshot.reset();
                pageSlot.hasDirtyRect = false;
                pageSlot.dirtyBaseRevision = 1u;
                textureSlots_.emplace(pageTexId, std::move(pageSlot));

                SharedAtlasPage newPage{};
                newPage.textureId = pageTexId;
                newPage.size = nextSize;
                newPage.skyline.Reset(nextSize, nextSize);
                sharedAtlasPages_.push_back(std::move(newPage));
                packedInSharedAtlas = packIntoPage(sharedAtlasPages_.back());
            }
        }
    }

    if (!packedInSharedAtlas) {
        const std::uint32_t texId = AllocateStandaloneTextureIdLocked();
        const std::uint64_t pendingRev = revision_.load(std::memory_order_relaxed) + 1u;
        TextureSlotBuffer slot{};
        slot.textureId = texId;
        slot.width = width;
        slot.height = height;
        slot.dirtyRevision = static_cast<std::uint32_t>(pendingRev != 0u ? pendingRev : 1u);
        slot.isAtlas = false;
        slot.pixelSnapshot = std::move(pixels);
        slot.rgbaPixels.clear();
        slot.reloadableFilePath = std::move(reloadableFilePath);
        slot.reloadableMemAssetKey = std::move(reloadableMemAssetKey);
        slot.cpuPixelsEvicted = false;
        textureSlots_.emplace(texId, std::move(slot));
        revision_.fetch_add(1u, std::memory_order_release);

        entry.textureId = texId;
        entry.atlasX = 0u;
        entry.atlasY = 0u;
        entry.atlasWidth = entry.width;
        entry.atlasHeight = entry.height;
        entry.u0 = 0.0f;
        entry.v0 = 0.0f;
        entry.u1 = 1.0f;
        entry.v1 = 1.0f;
        entry.isStandalone = true;
    }

    auto [it, _] = entries_.emplace(std::move(canonicalKey), std::move(entry));
    return &it->second;
}

const ImageAtlasEntry* ImageAtlas::RegisterDecodedImage(
    std::string_view uriKey,
    DecodedImage image,
    bool forceStandalone) {
    const std::string normKey = NormalizeImageUriPath(uriKey);
    if (normKey.empty() || !image.Valid()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = entries_.find(normKey); it != entries_.end()) {
        return &it->second;
    }
    return InsertDecodedLocked(normKey, std::move(image), forceStandalone);
}

std::uint32_t ImageAtlas::AllocateStandaloneTextureIdLocked() noexcept {
    while (!freeStandaloneTextureIds_.empty()) {
        const std::uint32_t recycled = freeStandaloneTextureIds_.back();
        freeStandaloneTextureIds_.pop_back();
        if (recycled > 0u &&
            recycled != kSharedUiAtlasTextureId &&
            textureSlots_.find(recycled) == textureSlots_.end()) {
            if (recycled >= nextStandaloneTextureId_) {
                nextStandaloneTextureId_ = recycled + 1u;
            }
            return recycled;
        }
    }
    return nextStandaloneTextureId_++;
}

std::uint32_t ImageAtlas::RegisterOrUpdateRgbaTextureSlot(
    std::uint32_t reuseSlotId,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>&& rgbaPixels) {
    if (width == 0u || height == 0u ||
        rgbaPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t texId = 0u;
    if (reuseSlotId > 0u && reuseSlotId != kSharedUiAtlasTextureId) {
        const auto freeIt = std::find(freeStandaloneTextureIds_.begin(), freeStandaloneTextureIds_.end(), reuseSlotId);
        if (freeIt != freeStandaloneTextureIds_.end()) {
            freeStandaloneTextureIds_.erase(freeIt);
        }
        if (reuseSlotId >= nextStandaloneTextureId_) {
            nextStandaloneTextureId_ = reuseSlotId + 1u;
        }
        texId = reuseSlotId;
    } else {
        texId = AllocateStandaloneTextureIdLocked();
    }
    auto nextSnap = std::make_shared<const std::vector<std::uint8_t>>(std::move(rgbaPixels));
    const std::uint64_t pendingRev = revision_.load(std::memory_order_relaxed) + 1u;
    TextureSlotBuffer& slot = textureSlots_[texId];
    slot.textureId = texId;
    slot.width = width;
    slot.height = height;
    slot.dirtyRevision = static_cast<std::uint32_t>(pendingRev != 0u ? pendingRev : 1u);
    slot.isAtlas = false;
    slot.isExternalGpuHandle = false;
    slot.isAlpha8 = false;
    slot.externalGpuHandle = 0u;
    slot.hasDirtyRect = false;
    slot.dirtyBaseRevision = slot.dirtyRevision;
    slot.rgbaPixels.clear();
    slot.rgbaPixels.shrink_to_fit();
    slot.pixelSnapshot = std::move(nextSnap);
    revision_.fetch_add(1u, std::memory_order_release);
    return texId;
}

std::uint32_t ImageAtlas::RegisterOrUpdateAlpha8TextureSlot(
    std::uint32_t reuseSlotId,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>&& alphaPixels) {
    if (width == 0u || height == 0u ||
        alphaPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t texId = 0u;
    if (reuseSlotId > 0u && reuseSlotId != kSharedUiAtlasTextureId) {
        const auto freeIt = std::find(freeStandaloneTextureIds_.begin(), freeStandaloneTextureIds_.end(), reuseSlotId);
        if (freeIt != freeStandaloneTextureIds_.end()) {
            freeStandaloneTextureIds_.erase(freeIt);
        }
        if (reuseSlotId >= nextStandaloneTextureId_) {
            nextStandaloneTextureId_ = reuseSlotId + 1u;
        }
        texId = reuseSlotId;
    } else {
        texId = AllocateStandaloneTextureIdLocked();
    }
    auto nextSnap = std::make_shared<const std::vector<std::uint8_t>>(std::move(alphaPixels));
    const std::uint64_t pendingRev = revision_.load(std::memory_order_relaxed) + 1u;
    TextureSlotBuffer& slot = textureSlots_[texId];
    slot.textureId = texId;
    slot.width = width;
    slot.height = height;
    slot.dirtyRevision = static_cast<std::uint32_t>(pendingRev != 0u ? pendingRev : 1u);
    slot.isAtlas = false;
    slot.isExternalGpuHandle = false;
    slot.isAlpha8 = true;
    slot.externalGpuHandle = 0u;
    slot.hasDirtyRect = false;
    slot.dirtyBaseRevision = slot.dirtyRevision;
    slot.rgbaPixels.clear();
    slot.rgbaPixels.shrink_to_fit();
    slot.pixelSnapshot = std::move(nextSnap);
    revision_.fetch_add(1u, std::memory_order_release);
    return texId;
}

void ImageAtlas::ReleaseRgbaTextureSlot(std::uint32_t textureId) noexcept {
    if (textureId == 0u || textureId == kSharedUiAtlasTextureId) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it != textureSlots_.end() && !it->second.isAtlas) {
        textureSlots_.erase(it);
        if (std::find(freeStandaloneTextureIds_.begin(), freeStandaloneTextureIds_.end(), textureId) ==
            freeStandaloneTextureIds_.end()) {
            freeStandaloneTextureIds_.push_back(textureId);
        }
        revision_.fetch_add(1u, std::memory_order_release);
    }
}

const ImageAtlasEntry* ImageAtlas::RegisterExternalGpuTexture(
    std::string_view uriKey,
    std::uint32_t externalGpuHandle,
    std::uint32_t width,
    std::uint32_t height,
    bool flipY,
    std::vector<std::uint8_t> fallbackRgbaPixels) {
    const std::string normKey = NormalizeImageUriPath(uriKey);
    if (normKey.empty() || width == 0u || height == 0u) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t texId = 0u;
    if (const auto existingIt = entries_.find(normKey);
        existingIt != entries_.end() &&
        existingIt->second.isStandalone &&
        existingIt->second.textureId != kSharedUiAtlasTextureId) {
        texId = existingIt->second.textureId;
    } else {
        texId = AllocateStandaloneTextureIdLocked();
    }

    const std::uint64_t pendingRev = revision_.load(std::memory_order_relaxed) + 1u;
    TextureSlotBuffer& slot = textureSlots_[texId];
    slot.textureId = texId;
    slot.width = width;
    slot.height = height;
    slot.dirtyRevision = static_cast<std::uint32_t>(pendingRev != 0u ? pendingRev : 1u);
    slot.dirtyBaseRevision = slot.dirtyRevision;
    slot.isAtlas = false;
    slot.hasDirtyRect = false;
    slot.isExternalGpuHandle = (externalGpuHandle != 0u);
    slot.externalGpuHandle = externalGpuHandle;
    slot.rgbaPixels.clear();
    const std::size_t expectedBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (fallbackRgbaPixels.size() == expectedBytes) {
        slot.pixelSnapshot = std::make_shared<const std::vector<std::uint8_t>>(std::move(fallbackRgbaPixels));
    } else {
        slot.pixelSnapshot.reset();
    }

    ImageAtlasEntry& entry = entries_[normKey];
    entry.uriKey = normKey;
    entry.textureId = texId;
    entry.width = width;
    entry.height = height;
    entry.atlasX = 0u;
    entry.atlasY = 0u;
    entry.atlasWidth = width;
    entry.atlasHeight = height;
    entry.u0 = 0.0f;
    entry.v0 = flipY ? 1.0f : 0.0f;
    entry.u1 = 1.0f;
    entry.v1 = flipY ? 0.0f : 1.0f;
    entry.isStandalone = true;

    revision_.fetch_add(1u, std::memory_order_release);
    return &entry;
}

bool ImageAtlas::UnregisterExternalGpuTexture(std::string_view uriKey) noexcept {
    const std::string normKey = NormalizeImageUriPath(uriKey);
    if (normKey.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(normKey);
    if (it == entries_.end() || !it->second.isStandalone || it->second.textureId == kSharedUiAtlasTextureId) {
        return false;
    }
    const std::uint32_t texId = it->second.textureId;
    entries_.erase(it);
    if (const auto slotIt = textureSlots_.find(texId); slotIt != textureSlots_.end() && !slotIt->second.isAtlas) {
        textureSlots_.erase(slotIt);
        if (std::find(freeStandaloneTextureIds_.begin(), freeStandaloneTextureIds_.end(), texId) ==
            freeStandaloneTextureIds_.end()) {
            freeStandaloneTextureIds_.push_back(texId);
        }
    }
    revision_.fetch_add(1u, std::memory_order_release);
    return true;
}

bool ImageAtlas::TryGetExternalGpuTextureHandle(
    std::uint32_t textureId,
    std::uint32_t& outExternalGpuHandle,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end() || !it->second.isExternalGpuHandle || it->second.externalGpuHandle == 0u) {
        outExternalGpuHandle = 0u;
        outWidth = 0u;
        outHeight = 0u;
        return false;
    }
    outExternalGpuHandle = it->second.externalGpuHandle;
    outWidth = it->second.width;
    outHeight = it->second.height;
    return true;
}

bool ImageAtlas::RegisterOrUpdateVisualBrushSlot(
    std::uint64_t cacheKey,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>&& rgbaPixels,
    ImageAtlasEntry& outEntry) {
    constexpr std::size_t kMaxVisualBrushSlotCache = 64u;
    if (width == 0u || height == 0u ||
        rgbaPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }

    std::uint32_t reuseSlotId = 0u;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = visualBrushSlots_.find(cacheKey); it != visualBrushSlots_.end()) {
            if (textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
                reuseSlotId = it->second.entry.textureId;
            }
        } else if (visualBrushSlots_.size() >= kMaxVisualBrushSlotCache) {
            auto lruIt = visualBrushSlots_.begin();
            for (auto it = visualBrushSlots_.begin(); it != visualBrushSlots_.end(); ++it) {
                if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                    lruIt = it;
                }
            }
            reuseSlotId = lruIt->second.entry.textureId;
            visualBrushSlots_.erase(lruIt);
        }
    }

    const std::uint32_t finalTexId = RegisterOrUpdateRgbaTextureSlot(
        reuseSlotId, width, height, std::move(rgbaPixels));
    if (finalTexId == 0u) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DynamicSlotCacheEntry& cached = visualBrushSlots_[cacheKey];
    if (cached.slotKey.empty()) {
        cached.slotKey = "__visualbrush__/" + std::to_string(cacheKey);
    }
    cached.currentExactUri = cached.slotKey;
    cached.lastUsedTick = ++dynamicSlotTick_;
    cached.entry.uriKey = cached.slotKey;
    cached.entry.textureId = finalTexId;
    cached.entry.width = width;
    cached.entry.height = height;
    cached.entry.atlasX = 0u;
    cached.entry.atlasY = 0u;
    cached.entry.atlasWidth = width;
    cached.entry.atlasHeight = height;
    cached.entry.u0 = 0.0f;
    cached.entry.v0 = 0.0f;
    cached.entry.u1 = 1.0f;
    cached.entry.v1 = 1.0f;
    cached.entry.isStandalone = true;
    outEntry = cached.entry;
    return true;
}

std::string ImageAtlas::ResolveExistingFileLocked(
    std::string_view cleanRelOrAbs,
    std::string_view localBaseDir) const {
    if (cleanRelOrAbs.empty()) {
        return {};
    }
    const std::string direct(cleanRelOrAbs);
    if (FileExistsOnDisk(direct)) {
        return direct;
    }
    if (!localBaseDir.empty()) {
        const std::string cand = NormalizeImageUriPath(cleanRelOrAbs, localBaseDir);
        if (FileExistsOnDisk(cand)) {
            return cand;
        }
    }
    if (!baseDirectory_.empty()) {
        const std::string cand = NormalizeImageUriPath(cleanRelOrAbs, baseDirectory_);
        if (FileExistsOnDisk(cand)) {
            return cand;
        }
    }
    for (const std::string& dir : searchDirectories_) {
        const std::string cand = NormalizeImageUriPath(cleanRelOrAbs, dir);
        if (FileExistsOnDisk(cand)) {
            return cand;
        }
    }
    return {};
}

std::string ImageAtlas::ResolveUriToPath(
    std::string_view uri,
    std::string_view localBaseDir) const {
    const std::string clean = NormalizeImageUriPath(uri);
    if (clean.empty()) {
        return {};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return ResolveExistingFileLocked(clean, localBaseDir);
}

namespace {

void SplitRiveSlotKeyParts(
    std::string_view uri,
    std::string_view& outPathPart,
    std::string& outInstKey) {
    outPathPart = uri;
    outInstKey.clear();
    if (const std::size_t qPos = uri.find('?'); qPos != std::string_view::npos) {
        outPathPart = uri.substr(0u, qPos);
        std::string_view q = uri.substr(qPos + 1u);
        while (!q.empty()) {
            const std::size_t amp = q.find('&');
            const std::string_view kv = (amp != std::string_view::npos) ? q.substr(0u, amp) : q;
            if (kv.size() > 7u && kv.substr(0u, 7u) == "__inst=") {
                outInstKey = std::string(kv.substr(7u));
            }
            if (amp == std::string_view::npos) break;
            q.remove_prefix(amp + 1u);
        }
    }
}

} // namespace

const ImageAtlasEntry* ImageAtlas::FindLoaded(
    std::string_view uri,
    std::string_view localBaseDir) const noexcept {
    if (IsRiveAssetUri(uri)) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [k, v] : riveSlots_) {
            if (v.currentExactUri == uri && textureSlots_.find(v.entry.textureId) != textureSlots_.end()) {
                return &v.entry;
            }
        }
        std::string_view pathPart;
        std::string instKey;
        SplitRiveSlotKeyParts(uri, pathPart, instKey);
        const std::string riveKey = "rive_slot://" + std::string(localBaseDir) + "|" +
            std::string(pathPart) + "|" + instKey;
        if (const auto it = riveSlots_.find(riveKey);
            it != riveSlots_.end() && textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
            return &it->second.entry;
        }
        if (localBaseDir.empty()) {
            const std::string suffix = "|" + std::string(pathPart) + "|" + instKey;
            for (const auto& [k, v] : riveSlots_) {
                if (k.size() >= suffix.size() &&
                    k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                    textureSlots_.find(v.entry.textureId) != textureSlots_.end()) {
                    return &v.entry;
                }
            }
        }
        return nullptr;
    }
    const std::string clean = NormalizeImageUriPath(uri);
    if (clean.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = entries_.find(clean); it != entries_.end()) {
        return &it->second;
    }
    if (!localBaseDir.empty()) {
        const std::string relCand = NormalizeImageUriPath(clean, localBaseDir);
        if (const auto it = entries_.find(relCand); it != entries_.end()) {
            return &it->second;
        }
    }
    if (!baseDirectory_.empty()) {
        const std::string baseCand = NormalizeImageUriPath(clean, baseDirectory_);
        if (const auto it = entries_.find(baseCand); it != entries_.end()) {
            return &it->second;
        }
    }
    for (const std::string& dir : searchDirectories_) {
        const std::string dirCand = NormalizeImageUriPath(clean, dir);
        if (const auto it = entries_.find(dirCand); it != entries_.end()) {
            return &it->second;
        }
    }
    return nullptr;
}


namespace {

std::uint32_t HashLattice3D(int ix, int iy, int iz, std::uint32_t seed) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(ix) * 374761393u +
                      static_cast<std::uint32_t>(iy) * 668265263u +
                      static_cast<std::uint32_t>(iz) * 2147483647u +
                      seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

float Grad3D(std::uint32_t h, float x, float y, float z) noexcept {
    switch (h & 15u) {
        case 0u:  return  x + y;
        case 1u:  return -x + y;
        case 2u:  return  x - y;
        case 3u:  return -x - y;
        case 4u:  return  x + z;
        case 5u:  return -x + z;
        case 6u:  return  x - z;
        case 7u:  return -x - z;
        case 8u:  return  y + z;
        case 9u:  return -y + z;
        case 10u: return  y - z;
        case 11u: return -y - z;
        case 12u: return  y + x;
        case 13u: return -y + z;
        case 14u: return  y - x;
        default:  return -y - z;
    }
}

float Perlin3D(float x, float y, float z, std::uint32_t seed) noexcept {
    const float flX = std::floor(x);
    const float flY = std::floor(y);
    const float flZ = std::floor(z);
    const int x0 = static_cast<int>(flX);
    const int y0 = static_cast<int>(flY);
    const int z0 = static_cast<int>(flZ);
    const float fx = x - flX;
    const float fy = y - flY;
    const float fz = z - flZ;
    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    const float uz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);

    auto lerp = [](float a, float b, float t) noexcept -> float {
        return a + (b - a) * t;
    };

    const float n000 = Grad3D(HashLattice3D(x0,     y0,     z0,     seed), fx,        fy,        fz);
    const float n100 = Grad3D(HashLattice3D(x0 + 1, y0,     z0,     seed), fx - 1.0f, fy,        fz);
    const float n010 = Grad3D(HashLattice3D(x0,     y0 + 1, z0,     seed), fx,        fy - 1.0f, fz);
    const float n110 = Grad3D(HashLattice3D(x0 + 1, y0 + 1, z0,     seed), fx - 1.0f, fy - 1.0f, fz);
    const float n001 = Grad3D(HashLattice3D(x0,     y0,     z0 + 1, seed), fx,        fy,        fz - 1.0f);
    const float n101 = Grad3D(HashLattice3D(x0 + 1, y0,     z0 + 1, seed), fx - 1.0f, fy,        fz - 1.0f);
    const float n011 = Grad3D(HashLattice3D(x0,     y0 + 1, z0 + 1, seed), fx,        fy - 1.0f, fz - 1.0f);
    const float n111 = Grad3D(HashLattice3D(x0 + 1, y0 + 1, z0 + 1, seed), fx - 1.0f, fy - 1.0f, fz - 1.0f);

    const float nx00 = lerp(n000, n100, ux);
    const float nx10 = lerp(n010, n110, ux);
    const float nx01 = lerp(n001, n101, ux);
    const float nx11 = lerp(n011, n111, ux);
    const float nxy0 = lerp(nx00, nx10, uy);
    const float nxy1 = lerp(nx01, nx11, uy);
    return lerp(nxy0, nxy1, uz);
}

bool ParsePlasmaShaderUri(std::string_view uri, PlasmaRenderParams& outParams, std::string& outSlotKey) {
    if (uri.size() < 16u || uri.substr(0u, 16u) != "shader://plasma?") return false;
    outParams = PlasmaRenderParams{};
    std::string colorHex = "#FF01BCFF";
    std::string_view q = uri.substr(16u);
    while (!q.empty()) {
        const std::size_t amp = q.find('&');
        const std::string_view kv = (amp != std::string_view::npos) ? q.substr(0u, amp) : q;
        if (const std::size_t eq = kv.find('='); eq != std::string_view::npos) {
            const std::string_view k = kv.substr(0u, eq);
            const std::string v(kv.substr(eq + 1u));
            if (k == "sx") outParams.sx = std::max(4.0f, std::strtof(v.c_str(), nullptr));
            else if (k == "sy") outParams.sy = std::max(4.0f, std::strtof(v.c_str(), nullptr));
            else if (k == "seed") outParams.seed = std::strtof(v.c_str(), nullptr);
            else if (k == "time") outParams.timeSec = std::strtof(v.c_str(), nullptr);
            else if (k == "vw") outParams.vw = std::clamp(std::strtof(v.c_str(), nullptr), 32.0f, 512.0f);
            else if (k == "vh") outParams.vh = std::clamp(std::strtof(v.c_str(), nullptr), 16.0f, 512.0f);
            else if (k == "color") colorHex = v;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1u);
    }

    if (colorHex.size() >= 3u && colorHex.substr(0u, 3u) == "%23") {
        colorHex = "#" + colorHex.substr(3u);
    }
    std::uint32_t rgba = colorHex.empty() ? 0xFFFFBC01u : ParseColorRgba32(colorHex);
    if (rgba == 0u && !colorHex.empty()) { rgba = 0xFFFFBC01u; }
    outParams.colorRgba = rgba;

    const std::uint32_t w = static_cast<std::uint32_t>(std::lround(outParams.vw));
    const std::uint32_t h = static_cast<std::uint32_t>(std::lround(outParams.vh));
    const int seedInt = static_cast<int>(std::lround(outParams.seed));
    const long sxQ = std::lround(outParams.sx * 100.0f);
    const long syQ = std::lround(outParams.sy * 100.0f);
    outSlotKey = "shader://plasma_slot?seed=" + std::to_string(seedInt) +
                 "&sx=" + std::to_string(sxQ) + "&sy=" + std::to_string(syQ) +
                 "&rgba=" + std::to_string(rgba) +
                 "&w=" + std::to_string(w) + "&h=" + std::to_string(h);
    return true;
}

bool SynthesizePlasmaShaderImageFromParams(const PlasmaRenderParams& params, DecodedImage& outImg) {
    const std::uint32_t rgba = (params.colorRgba == 0u) ? 0xFFFFBC01u : params.colorRgba;
    const float baseR = static_cast<float>(rgba & 0xFFu) / 255.0f;
    const float baseG = static_cast<float>((rgba >> 8u) & 0xFFu) / 255.0f;
    const float baseB = static_cast<float>((rgba >> 16u) & 0xFFu) / 255.0f;
    const float baseA = static_cast<float>((rgba >> 24u) & 0xFFu) / 255.0f;

    const std::uint32_t w = static_cast<std::uint32_t>(std::lround(params.vw));
    const std::uint32_t h = static_cast<std::uint32_t>(std::lround(params.vh));
    outImg.width = w;
    outImg.height = h;
    outImg.format = ImageFormat::RawRgba8;
    outImg.rgbaPixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);

    const int seedInt = static_cast<int>(std::lround(params.seed));
    const std::uint32_t seedU = static_cast<std::uint32_t>(seedInt);
    const std::uint32_t seedW1 = seedU * 3u + 11u;
    const std::uint32_t seedW2 = seedU * 5u + 29u;
    const std::uint32_t seedN0 = seedU * 7u + 53u;
    const std::uint32_t seedN1 = seedU * 11u + 97u;
    const float seedOx = static_cast<float>((seedU * 17u) % 997u) * 0.37f;
    const float seedOy = static_cast<float>((seedU * 31u) % 997u) * 0.43f;

    const float invSx = 1.30f / std::max(4.0f, params.sx);
    const float invSy = 1.30f / std::max(4.0f, params.sy);
    const float tz = params.timeSec * 0.55f;
    const float driftX = params.timeSec * 0.35f;
    const float driftY = -params.timeSec * 0.18f;

    auto shadePlasmaField = [&](float field, std::uint8_t* dst) noexcept {
        const float raw = std::clamp((field + 0.08f) / 0.56f, 0.0f, 1.0f);
        const float glow = raw * raw * (3.0f - 2.0f * raw);
        const float invGlow = 1.0f - glow;
        const float alpha = std::clamp(1.0f - invGlow * invGlow, 0.0f, 1.0f);
        const float boost = 1.0f + 0.80f * glow;
        const float coreT = std::clamp((glow - 0.46f) / 0.54f, 0.0f, 1.0f);
        const float hotCore = coreT * coreT * (3.0f - 2.0f * coreT) * 0.92f;
        const float cR = std::clamp(baseR * boost, 0.0f, 1.0f);
        const float cG = std::clamp(baseG * boost, 0.0f, 1.0f);
        const float cB = std::clamp(baseB * boost, 0.0f, 1.0f);
        const float outR = cR + (1.0f - cR) * hotCore;
        const float outG = cG + (1.0f - cG) * hotCore;
        const float outB = cB + (1.0f - cB) * hotCore;
        dst[0] = static_cast<std::uint8_t>(std::clamp(outR, 0.0f, 1.0f) * 255.0f + 0.5f);
        dst[1] = static_cast<std::uint8_t>(std::clamp(outG, 0.0f, 1.0f) * 255.0f + 0.5f);
        dst[2] = static_cast<std::uint8_t>(std::clamp(outB, 0.0f, 1.0f) * 255.0f + 0.5f);
        dst[3] = static_cast<std::uint8_t>(std::clamp(baseA * alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    auto evalFieldAt = [&](float pxF, float pyF) noexcept -> float {
        const float x = (pxF + 0.5f) * invSx + seedOx + driftX;
        const float y = (pyF + 0.5f) * invSy + seedOy + driftY;
        const float wx = Perlin3D(x * 0.75f, y * 0.75f, tz * 0.85f, seedW1);
        const float wy = Perlin3D(x * 0.75f + 13.7f, y * 0.75f + 7.3f, tz * 0.85f + 5.1f, seedW2);
        const float n0 = Perlin3D(x + wx * 0.90f, y + wy * 0.90f, tz, seedN0);
        const float n1 = 0.20f * Perlin3D(x * 1.75f - wx * 0.45f, y * 1.75f + wy * 0.45f, tz * 1.30f + 9.4f, seedN1);
        return n0 + n1;
    };

    // When surface exceeds 4096 pixels and noise wavelength is >= 16px, sample the smooth continuous
    // Perlin scalar field on a 4x4 lattice and bilinearly reconstruct before applying the sharp glow curve.
    if (static_cast<std::size_t>(w) * h > 4096u && params.sx >= 16.0f && params.sy >= 16.0f) {
        constexpr std::uint32_t kStep = 4u;
        const std::uint32_t gw = (w + kStep - 1u) / kStep + 1u;
        const std::uint32_t gh = (h + kStep - 1u) / kStep + 1u;
        std::vector<float> grid(static_cast<std::size_t>(gw) * gh);
        for (std::uint32_t gy = 0u; gy < gh; ++gy) {
            const float pyF = static_cast<float>(std::min<std::uint32_t>(gy * kStep, h - 1u));
            for (std::uint32_t gx = 0u; gx < gw; ++gx) {
                const float pxF = static_cast<float>(std::min<std::uint32_t>(gx * kStep, w - 1u));
                grid[static_cast<std::size_t>(gy) * gw + gx] = evalFieldAt(pxF, pyF);
            }
        }
        constexpr float kInvStep = 1.0f / static_cast<float>(kStep);
        for (std::uint32_t py = 0u; py < h; ++py) {
            const std::uint32_t gy0 = py / kStep;
            const std::uint32_t gy1 = std::min<std::uint32_t>(gy0 + 1u, gh - 1u);
            const float ty = static_cast<float>(py - gy0 * kStep) * kInvStep;
            const float* row0 = grid.data() + static_cast<std::size_t>(gy0) * gw;
            const float* row1 = grid.data() + static_cast<std::size_t>(gy1) * gw;
            std::uint8_t* dstRow = outImg.rgbaPixels.data() + static_cast<std::size_t>(py) * w * 4u;
            for (std::uint32_t px = 0u; px < w; ++px) {
                const std::uint32_t gx0 = px / kStep;
                const std::uint32_t gx1 = std::min<std::uint32_t>(gx0 + 1u, gw - 1u);
                const float tx = static_cast<float>(px - gx0 * kStep) * kInvStep;
                const float top = row0[gx0] + (row0[gx1] - row0[gx0]) * tx;
                const float bot = row1[gx0] + (row1[gx1] - row1[gx0]) * tx;
                shadePlasmaField(top + (bot - top) * ty, dstRow + px * 4u);
            }
        }
    } else {
        for (std::uint32_t py = 0u; py < h; ++py) {
            for (std::uint32_t px = 0u; px < w; ++px) {
                shadePlasmaField(
                    evalFieldAt(static_cast<float>(px), static_cast<float>(py)),
                    outImg.rgbaPixels.data() + (static_cast<std::size_t>(py) * w + px) * 4u);
            }
        }
    }
    return true;
}

} // namespace

const ImageAtlasEntry* ImageAtlas::FindLoadedRive(
    std::uint32_t instanceId,
    std::string_view assetPath,
    std::string_view localBaseDir) const noexcept {
    if (assetPath.empty()) {
        return nullptr;
    }
    std::string_view pathPart = assetPath;
    if (pathPart.size() >= 7u && pathPart.substr(0u, 7u) == "rive://") {
        pathPart.remove_prefix(7u);
    }
    if (const std::size_t q = pathPart.find('?'); q != std::string_view::npos) {
        pathPart = pathPart.substr(0u, q);
    }
    const std::string instKey = std::to_string(instanceId);
    const std::string riveKey = "rive_slot://" + std::string(localBaseDir) + "|" +
        std::string(pathPart) + "|" + instKey;
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = riveSlots_.find(riveKey);
        it != riveSlots_.end() && textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
        return &it->second.entry;
    }
    if (localBaseDir.empty()) {
        const std::string suffix = "|" + std::string(pathPart) + "|" + instKey;
        for (const auto& [k, v] : riveSlots_) {
            if (k.size() >= suffix.size() &&
                k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                textureSlots_.find(v.entry.textureId) != textureSlots_.end()) {
                return &v.entry;
            }
        }
    }
    return nullptr;
}

const ImageAtlasEntry* ImageAtlas::LoadOrGetPlasma(const PlasmaRenderParams& rawParams) {
    constexpr std::size_t kMaxPlasmaSlotCache = 64u;
    PlasmaRenderParams params = rawParams;
    params.sx = std::max(4.0f, params.sx);
    params.sy = std::max(4.0f, params.sy);
    params.vw = std::clamp(params.vw, 32.0f, 512.0f);
    params.vh = std::clamp(params.vh, 16.0f, 512.0f);
    if (params.colorRgba == 0u) {
        params.colorRgba = 0xFFFFBC01u;
    }

    const std::uint32_t w = static_cast<std::uint32_t>(std::lround(params.vw));
    const std::uint32_t h = static_cast<std::uint32_t>(std::lround(params.vh));
    const int seedInt = static_cast<int>(std::lround(params.seed));
    const long sxQ = std::lround(params.sx * 100.0f);
    const long syQ = std::lround(params.sy * 100.0f);
    const std::string slotKey =
        "shader://plasma_slot?seed=" + std::to_string(seedInt) +
        "&sx=" + std::to_string(sxQ) + "&sy=" + std::to_string(syQ) +
        "&rgba=" + std::to_string(params.colorRgba) +
        "&w=" + std::to_string(w) + "&h=" + std::to_string(h);

    std::uint32_t reuseSlotId = 0u;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = plasmaSlots_.find(slotKey); it != plasmaSlots_.end()) {
            it->second.lastUsedTick = ++dynamicSlotTick_;
            if (it->second.hasNumericPlasma &&
                it->second.lastPlasma.timeSec == params.timeSec &&
                it->second.lastPlasma.sx == params.sx &&
                it->second.lastPlasma.sy == params.sy &&
                it->second.lastPlasma.seed == params.seed &&
                it->second.lastPlasma.vw == params.vw &&
                it->second.lastPlasma.vh == params.vh &&
                it->second.lastPlasma.colorRgba == params.colorRgba &&
                textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
                return &it->second.entry;
            }
            reuseSlotId = it->second.entry.textureId;
        }
    }

    DecodedImage plasmaImg{};
    if (!SynthesizePlasmaShaderImageFromParams(params, plasmaImg)) {
        return nullptr;
    }
    if (reuseSlotId == 0u) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (plasmaSlots_.size() >= kMaxPlasmaSlotCache) {
            auto lruIt = plasmaSlots_.begin();
            for (auto it = plasmaSlots_.begin(); it != plasmaSlots_.end(); ++it) {
                if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                    lruIt = it;
                }
            }
            reuseSlotId = lruIt->second.entry.textureId;
            plasmaSlots_.erase(lruIt);
        }
    }
    const std::uint32_t finalTexId = RegisterOrUpdateRgbaTextureSlot(
        reuseSlotId, plasmaImg.width, plasmaImg.height, std::move(plasmaImg.rgbaPixels));
    if (finalTexId == 0u) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    DynamicSlotCacheEntry& cached = plasmaSlots_[slotKey];
    cached.slotKey = slotKey;
    cached.currentExactUri = slotKey;
    cached.lastUsedTick = ++dynamicSlotTick_;
    cached.hasNumericPlasma = true;
    cached.lastPlasma = params;
    cached.entry.uriKey = slotKey;
    cached.entry.textureId = finalTexId;
    cached.entry.width = plasmaImg.width;
    cached.entry.height = plasmaImg.height;
    cached.entry.atlasX = 0u;
    cached.entry.atlasY = 0u;
    cached.entry.atlasWidth = plasmaImg.width;
    cached.entry.atlasHeight = plasmaImg.height;
    cached.entry.u0 = 0.0f;
    cached.entry.v0 = 0.0f;
    cached.entry.u1 = 1.0f;
    cached.entry.v1 = 1.0f;
    cached.entry.isStandalone = true;
    return &cached.entry;
}

const ImageAtlasEntry* ImageAtlas::LoadOrGetRive(
    const RiveRenderRequest& request,
    std::uint64_t inputsRevision,
    std::string_view localBaseDir) {
    constexpr std::size_t kMaxRiveSlotCache = 16u;
    if (request.assetPath.empty()) {
        return nullptr;
    }
    std::string_view pathPart = request.assetPath;
    if (pathPart.size() >= 7u && pathPart.substr(0u, 7u) == "rive://") {
        pathPart.remove_prefix(7u);
    }
    if (const std::size_t q = pathPart.find('?'); q != std::string_view::npos) {
        pathPart = pathPart.substr(0u, q);
    }
    if (pathPart.empty()) {
        return nullptr;
    }

    const std::string riveKey = "rive_slot://" + std::string(localBaseDir) + "|" +
        std::string(pathPart) + "|" + std::to_string(request.instanceId);
    std::uint32_t reuseSlotId = 0u;
    std::uint64_t prevPoseHash = 0u;
    bool hasPrevPose = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = riveSlots_.find(riveKey); it != riveSlots_.end()) {
            it->second.lastUsedTick = ++dynamicSlotTick_;
            const bool hasSlot =
                textureSlots_.find(it->second.entry.textureId) != textureSlots_.end();
            if (hasSlot &&
                it->second.hasNumericRive &&
                it->second.lastRiveT == request.tLoopSec &&
                it->second.lastRiveTs == request.tStateSec &&
                it->second.lastRiveW == request.reqW &&
                it->second.lastRiveH == request.reqH &&
                it->second.lastRiveInputsRev == inputsRevision) {
                return &it->second.entry;
            }
            reuseSlotId = it->second.entry.textureId;
            if (hasSlot && it->second.hasLastPoseHash) {
                prevPoseHash = it->second.lastPoseHash;
                hasPrevPose = true;
            }
        }
    }

    DecodedImage riveImg{};
    std::uint64_t newPoseHash = 0u;
    bool poseChanged = true;
    if (!RenderRiveParamsToImageIfChanged(
            request,
            *this,
            localBaseDir,
            hasPrevPose ? &prevPoseHash : nullptr,
            newPoseHash,
            poseChanged,
            riveImg)) {
        return nullptr;
    }
    if (!poseChanged && hasPrevPose) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = riveSlots_.find(riveKey);
            it != riveSlots_.end() &&
            textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
            it->second.lastUsedTick = ++dynamicSlotTick_;
            it->second.hasNumericRive = true;
            it->second.lastRiveT = request.tLoopSec;
            it->second.lastRiveTs = request.tStateSec;
            it->second.lastRiveW = request.reqW;
            it->second.lastRiveH = request.reqH;
            it->second.lastRiveInputsRev = inputsRevision;
            return &it->second.entry;
        }
        if (!RenderRiveParamsToImageIfChanged(
                request, *this, localBaseDir, nullptr, newPoseHash, poseChanged, riveImg)) {
            return nullptr;
        }
    }
    if (!riveImg.Valid()) {
        return nullptr;
    }
    if (reuseSlotId == 0u) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = riveSlots_.find(riveKey); it != riveSlots_.end()) {
            reuseSlotId = it->second.entry.textureId;
        } else if (riveSlots_.size() >= kMaxRiveSlotCache) {
            auto lruIt = riveSlots_.begin();
            for (auto it = riveSlots_.begin(); it != riveSlots_.end(); ++it) {
                if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                    lruIt = it;
                }
            }
            reuseSlotId = lruIt->second.entry.textureId;
            riveSlots_.erase(lruIt);
        }
    }
    const std::uint32_t finalTexId = RegisterOrUpdateRgbaTextureSlot(
        reuseSlotId, riveImg.width, riveImg.height, std::move(riveImg.rgbaPixels));
    if (finalTexId == 0u) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    DynamicSlotCacheEntry& cached = riveSlots_[riveKey];
    cached.slotKey = riveKey;
    cached.currentExactUri = riveKey;
    cached.lastUsedTick = ++dynamicSlotTick_;
    cached.lastPoseHash = newPoseHash;
    cached.hasLastPoseHash = true;
    cached.hasNumericRive = true;
    cached.lastRiveT = request.tLoopSec;
    cached.lastRiveTs = request.tStateSec;
    cached.lastRiveW = request.reqW;
    cached.lastRiveH = request.reqH;
    cached.lastRiveInputsRev = inputsRevision;
    cached.entry.uriKey = riveKey;
    cached.entry.textureId = finalTexId;
    cached.entry.width = riveImg.width;
    cached.entry.height = riveImg.height;
    cached.entry.atlasX = 0u;
    cached.entry.atlasY = 0u;
    cached.entry.atlasWidth = riveImg.width;
    cached.entry.atlasHeight = riveImg.height;
    cached.entry.u0 = 0.0f;
    cached.entry.v0 = 0.0f;
    cached.entry.u1 = 1.0f;
    cached.entry.v1 = 1.0f;
    cached.entry.isStandalone = true;
    return &cached.entry;
}

const ImageAtlasEntry* ImageAtlas::LoadOrGet(
    std::string_view uri,
    std::string_view localBaseDir) {
    if (uri.size() >= 20u && uri.substr(0u, 20u) == "shader://saturation?") {
        struct SatDepthGuard {
            int& depth;
            explicit SatDepthGuard(int& d) noexcept : depth(d) { ++depth; }
            ~SatDepthGuard() noexcept { --depth; }
        };
        thread_local int sSatDepth = 0;
        if (sSatDepth >= 2) {
            return nullptr;
        }
        SatDepthGuard depthGuard(sSatDepth);

        std::string srcParam;
        float sat = 1.0f;
        std::string_view q = uri.substr(20u);
        while (!q.empty()) {
            if (q.size() >= 4u && q.substr(0u, 4u) == "src=") {
                const std::string_view rest = q.substr(4u);
                // Preserve nested query params inside `src=` (e.g. `shader://plasma?w=...&h=...`),
                // stopping only if a trailing `&sat=` parameter follows `src=`.
                if (const std::size_t satTail = rest.find("&sat="); satTail != std::string_view::npos) {
                    srcParam = std::string(rest.substr(0u, satTail));
                    q = rest.substr(satTail + 1u);
                    continue;
                }
                srcParam = std::string(rest);
                break;
            }
            const std::size_t amp = q.find('&');
            const std::string_view kv = (amp != std::string_view::npos) ? q.substr(0u, amp) : q;
            if (const std::size_t eq = kv.find('='); eq != std::string_view::npos) {
                const std::string_view k = kv.substr(0u, eq);
                const std::string v(kv.substr(eq + 1u));
                if (k == "sat") sat = std::clamp(std::strtof(v.c_str(), nullptr), 0.0f, 4.0f);
            }
            if (amp == std::string_view::npos) break;
            q.remove_prefix(amp + 1u);
        }
        if (srcParam.empty() ||
            (srcParam.size() >= 20u && srcParam.substr(0u, 20u) == "shader://saturation?")) {
            return nullptr;
        }
        const ImageAtlasEntry* baseEntry = LoadOrGet(srcParam, localBaseDir);
        if (!baseEntry || baseEntry->width == 0u || baseEntry->height == 0u) {
            return nullptr;
        }
        // Snapshot entry fields immediately so subsequent dynamic-slot evictions cannot dangle.
        const ImageAtlasEntry baseCopy = *baseEntry;
        const std::string satKey = "shader://sat_cached?sat=" +
            std::to_string(std::lround(sat * 1000.0f)) + "&src=" + baseCopy.uriKey;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = entries_.find(satKey); it != entries_.end()) {
                return &it->second;
            }
        }
        std::uint32_t slotW = 0u, slotH = 0u;
        std::shared_ptr<const std::vector<std::uint8_t>> slotPixels;
        if (!TryGetTextureSlotSnapshot(baseCopy.textureId, slotW, slotH, slotPixels) ||
            !slotPixels || slotW == 0u || slotH == 0u ||
            slotPixels->size() < static_cast<std::size_t>(slotW) * static_cast<std::size_t>(slotH) * 4u) {
            return FindLoaded(srcParam, localBaseDir);
        }
        DecodedImage satImg{};
        satImg.width = baseCopy.width;
        satImg.height = baseCopy.height;
        satImg.format = ImageFormat::RawRgba8;
        satImg.rgbaPixels.resize(static_cast<std::size_t>(satImg.width) * satImg.height * 4u);
        const std::uint32_t offX = baseCopy.isStandalone ? 0u : baseCopy.atlasX;
        const std::uint32_t offY = baseCopy.isStandalone ? 0u : baseCopy.atlasY;
        const std::uint8_t* srcData = slotPixels->data();
        for (std::uint32_t py = 0u; py < satImg.height; ++py) {
            const std::uint32_t sy = std::min(offY + py, slotH - 1u);
            for (std::uint32_t px = 0u; px < satImg.width; ++px) {
                const std::uint32_t sx = std::min(offX + px, slotW - 1u);
                const std::uint8_t* sp = srcData + (static_cast<std::size_t>(sy) * slotW + sx) * 4u;
                const float r = static_cast<float>(sp[0]);
                const float g = static_cast<float>(sp[1]);
                const float b = static_cast<float>(sp[2]);
                const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                std::uint8_t* dp = satImg.rgbaPixels.data() + (static_cast<std::size_t>(py) * satImg.width + px) * 4u;
                dp[0] = static_cast<std::uint8_t>(std::clamp(lum + sat * (r - lum), 0.0f, 255.0f) + 0.5f);
                dp[1] = static_cast<std::uint8_t>(std::clamp(lum + sat * (g - lum), 0.0f, 255.0f) + 0.5f);
                dp[2] = static_cast<std::uint8_t>(std::clamp(lum + sat * (b - lum), 0.0f, 255.0f) + 0.5f);
                dp[3] = sp[3];
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return InsertDecodedLocked(satKey, std::move(satImg), false);
    }
    if (uri.size() >= 16u && uri.substr(0u, 16u) == "shader://plasma?") {
        constexpr std::size_t kMaxPlasmaSlotCache = 64u;
        PlasmaRenderParams params{};
        std::string slotKey;
        if (!ParsePlasmaShaderUri(uri, params, slotKey)) {
            return nullptr;
        }
        std::uint32_t reuseSlotId = 0u;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = plasmaSlots_.find(slotKey); it != plasmaSlots_.end()) {
                it->second.lastUsedTick = ++dynamicSlotTick_;
                if (it->second.currentExactUri == uri &&
                    textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
                    return &it->second.entry;
                }
                reuseSlotId = it->second.entry.textureId;
            }
        }
        DecodedImage plasmaImg{};
        if (!SynthesizePlasmaShaderImageFromParams(params, plasmaImg)) {
            return nullptr;
        }
        if (reuseSlotId == 0u) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (plasmaSlots_.size() >= kMaxPlasmaSlotCache) {
                auto lruIt = plasmaSlots_.begin();
                for (auto it = plasmaSlots_.begin(); it != plasmaSlots_.end(); ++it) {
                    if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                        lruIt = it;
                    }
                }
                reuseSlotId = lruIt->second.entry.textureId;
                plasmaSlots_.erase(lruIt);
            }
        }
        const std::uint32_t finalTexId = RegisterOrUpdateRgbaTextureSlot(
            reuseSlotId, plasmaImg.width, plasmaImg.height, std::move(plasmaImg.rgbaPixels));
        if (finalTexId == 0u) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        DynamicSlotCacheEntry& cached = plasmaSlots_[slotKey];
        cached.slotKey = std::move(slotKey);
        cached.currentExactUri = std::string(uri);
        cached.lastUsedTick = ++dynamicSlotTick_;
        cached.entry.uriKey = cached.currentExactUri;
        cached.entry.textureId = finalTexId;
        cached.entry.width = plasmaImg.width;
        cached.entry.height = plasmaImg.height;
        cached.entry.atlasX = 0u;
        cached.entry.atlasY = 0u;
        cached.entry.atlasWidth = plasmaImg.width;
        cached.entry.atlasHeight = plasmaImg.height;
        cached.entry.u0 = 0.0f;
        cached.entry.v0 = 0.0f;
        cached.entry.u1 = 1.0f;
        cached.entry.v1 = 1.0f;
        cached.entry.isStandalone = true;
        return &cached.entry;
    }
    if (IsRiveAssetUri(uri)) {
        constexpr std::size_t kMaxRiveSlotCache = 16u;
        std::string_view pathPart;
        std::string instKey;
        SplitRiveSlotKeyParts(uri, pathPart, instKey);
        const std::string riveKey = "rive_slot://" + std::string(localBaseDir) + "|" +
            std::string(pathPart) + "|" + instKey;
        std::uint32_t reuseSlotId = 0u;
        std::uint64_t prevPoseHash = 0u;
        bool hasPrevPose = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = riveSlots_.find(riveKey); it != riveSlots_.end()) {
                it->second.lastUsedTick = ++dynamicSlotTick_;
                const bool hasSlot =
                    textureSlots_.find(it->second.entry.textureId) != textureSlots_.end();
                if (it->second.currentExactUri == uri && hasSlot) {
                    return &it->second.entry;
                }
                reuseSlotId = it->second.entry.textureId;
                if (hasSlot && it->second.hasLastPoseHash) {
                    prevPoseHash = it->second.lastPoseHash;
                    hasPrevPose = true;
                }
            }
        }
        DecodedImage riveImg{};
        std::uint64_t newPoseHash = 0u;
        bool poseChanged = true;
        if (!RenderRiveUriToImageIfChanged(
                uri,
                *this,
                localBaseDir,
                hasPrevPose ? &prevPoseHash : nullptr,
                newPoseHash,
                poseChanged,
                riveImg)) {
            return nullptr;
        }
        if (!poseChanged && hasPrevPose) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = riveSlots_.find(riveKey);
                it != riveSlots_.end() &&
                textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
                it->second.currentExactUri = std::string(uri);
                it->second.lastUsedTick = ++dynamicSlotTick_;
                it->second.entry.uriKey = it->second.currentExactUri;
                return &it->second.entry;
            }
            // Slot was evicted concurrently; re-render without prevPoseHash
            if (!RenderRiveUriToImageIfChanged(
                    uri, *this, localBaseDir, nullptr, newPoseHash, poseChanged, riveImg)) {
                return nullptr;
            }
        }
        if (!riveImg.Valid()) {
            return nullptr;
        }
        if (reuseSlotId == 0u) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = riveSlots_.find(riveKey); it != riveSlots_.end()) {
                reuseSlotId = it->second.entry.textureId;
            } else if (riveSlots_.size() >= kMaxRiveSlotCache) {
                auto lruIt = riveSlots_.begin();
                for (auto it = riveSlots_.begin(); it != riveSlots_.end(); ++it) {
                    if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                        lruIt = it;
                    }
                }
                reuseSlotId = lruIt->second.entry.textureId;
                riveSlots_.erase(lruIt);
            }
        }
        const std::uint32_t finalTexId = RegisterOrUpdateRgbaTextureSlot(
            reuseSlotId, riveImg.width, riveImg.height, std::move(riveImg.rgbaPixels));
        if (finalTexId == 0u) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        DynamicSlotCacheEntry& cached = riveSlots_[riveKey];
        cached.slotKey = riveKey;
        cached.currentExactUri = std::string(uri);
        cached.lastUsedTick = ++dynamicSlotTick_;
        cached.lastPoseHash = newPoseHash;
        cached.hasLastPoseHash = true;
        cached.entry.uriKey = cached.currentExactUri;
        cached.entry.textureId = finalTexId;
        cached.entry.width = riveImg.width;
        cached.entry.height = riveImg.height;
        cached.entry.atlasX = 0u;
        cached.entry.atlasY = 0u;
        cached.entry.atlasWidth = riveImg.width;
        cached.entry.atlasHeight = riveImg.height;
        cached.entry.u0 = 0.0f;
        cached.entry.v0 = 0.0f;
        cached.entry.u1 = 1.0f;
        cached.entry.v1 = 1.0f;
        cached.entry.isStandalone = true;
        return &cached.entry;
    }
    const std::string clean = NormalizeImageUriPath(uri);
    if (clean.empty()) {
        return nullptr;
    }

    std::vector<std::uint8_t> memCopy;
    std::string resolvedDiskPath;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = entries_.find(clean); it != entries_.end()) {
            return &it->second;
        }
        if (const auto memIt = memoryEncodedAssets_.find(clean); memIt != memoryEncodedAssets_.end()) {
            memCopy = memIt->second;
        } else {
            resolvedDiskPath = ResolveExistingFileLocked(clean, localBaseDir);
            if (!resolvedDiskPath.empty()) {
                if (const auto diskIt = entries_.find(resolvedDiskPath); diskIt != entries_.end()) {
                    return &diskIt->second;
                }
            }
        }
    }

    struct CachedDecodedImage {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::shared_ptr<const std::vector<std::uint8_t>> rgbaPixels;
    };

    struct GlobalDecodedFileCache {
        std::mutex mutex;
        std::unordered_map<std::string, CachedDecodedImage> byKey;
        std::size_t totalBytes = 0u;

        void InsertBounded(const std::string& key, const CachedDecodedImage& img) {
            constexpr std::size_t kMaxCacheEntries = 32u;
            constexpr std::size_t kMaxCacheBytes = 4u * 1024u * 1024u;
            const std::size_t imgBytes = img.rgbaPixels ? img.rgbaPixels->size() : 0u;
            if (imgBytes == 0u || imgBytes > 1u * 1024u * 1024u || imgBytes > kMaxCacheBytes) {
                return;
            }
            std::lock_guard<std::mutex> dLock(mutex);
            const auto existing = byKey.find(key);
            if (existing != byKey.end()) {
                const std::size_t oldBytes = existing->second.rgbaPixels ? existing->second.rgbaPixels->size() : 0u;
                totalBytes = (totalBytes >= oldBytes) ? (totalBytes - oldBytes) : 0u;
                byKey.erase(existing);
            }
            if (byKey.size() >= kMaxCacheEntries || totalBytes + imgBytes > kMaxCacheBytes) {
                byKey.clear();
                totalBytes = 0u;
            }
            byKey.emplace(key, img);
            totalBytes += imgBytes;
        }
    };
    static GlobalDecodedFileCache sDecodedFileCache;

    CachedDecodedImage sharedDecoded{};
    std::string canonicalKey;
    if (!memCopy.empty()) {
        std::uint64_t h64 = 1469598103934665603ULL;
        for (std::uint8_t b : memCopy) {
            h64 ^= static_cast<std::uint64_t>(b);
            h64 *= 1099511628211ULL;
        }
        const std::string memCacheKey = "mem:" + std::to_string(memCopy.size()) + ":" + std::to_string(h64);
        bool hitMemCache = false;
        {
            std::lock_guard<std::mutex> dLock(sDecodedFileCache.mutex);
            const auto cit = sDecodedFileCache.byKey.find(memCacheKey);
            if (cit != sDecodedFileCache.byKey.end()) {
                sharedDecoded = cit->second;
                hitMemCache = true;
            }
        }
        if (!hitMemCache) {
            DecodedImage decoded{};
            if (!DecodeImageFromMemory(memCopy.data(), memCopy.size(), decoded)) {
                return nullptr;
            }
            sharedDecoded.width = decoded.width;
            sharedDecoded.height = decoded.height;
            sharedDecoded.rgbaPixels =
                std::make_shared<const std::vector<std::uint8_t>>(std::move(decoded.rgbaPixels));
            sDecodedFileCache.InsertBounded(memCacheKey, sharedDecoded);
        }
        canonicalKey = clean;
    } else if (!resolvedDiskPath.empty()) {
        const std::string diskCacheKey = "file:" + resolvedDiskPath;
        bool hitDiskCache = false;
        {
            std::lock_guard<std::mutex> dLock(sDecodedFileCache.mutex);
            const auto cit = sDecodedFileCache.byKey.find(diskCacheKey);
            if (cit != sDecodedFileCache.byKey.end()) {
                sharedDecoded = cit->second;
                hitDiskCache = true;
            }
        }
        if (!hitDiskCache) {
            DecodedImage decoded{};
            if (!DecodeImageFromFile(resolvedDiskPath, decoded)) {
                return nullptr;
            }
            sharedDecoded.width = decoded.width;
            sharedDecoded.height = decoded.height;
            sharedDecoded.rgbaPixels =
                std::make_shared<const std::vector<std::uint8_t>>(std::move(decoded.rgbaPixels));
            sDecodedFileCache.InsertBounded(diskCacheKey, sharedDecoded);
        }
        canonicalKey = resolvedDiskPath;
    } else {
        return nullptr;
    }

    std::string reloadFilePath;
    std::string reloadMemKey;
    if (!memCopy.empty()) {
        reloadMemKey = clean;
    } else if (!resolvedDiskPath.empty()) {
        reloadFilePath = resolvedDiskPath;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return InsertDecodedSharedLocked(
        std::move(canonicalKey),
        sharedDecoded.width,
        sharedDecoded.height,
        std::move(sharedDecoded.rgbaPixels),
        false,
        std::move(reloadFilePath),
        std::move(reloadMemKey));
}

bool ImageAtlas::EvictStandaloneCpuPixelsAfterGpuUpload(
    std::uint32_t textureId,
    std::uint32_t uploadedRevision) const noexcept {
    if (textureId == kSharedUiAtlasTextureId || uploadedRevision == 0u) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        return false;
    }
    const TextureSlotBuffer& slot = it->second;
    if (slot.isAtlas || slot.isExternalGpuHandle || slot.isAlpha8 ||
        slot.dirtyRevision != uploadedRevision ||
        (slot.reloadableFilePath.empty() && slot.reloadableMemAssetKey.empty())) {
        return false;
    }
    slot.pixelSnapshot.reset();
    std::vector<std::uint8_t>().swap(slot.rgbaPixels);
    slot.cpuPixelsEvicted = true;
    return true;
}

bool ImageAtlas::TryReloadEvictedSlotPixels(std::uint32_t textureId) const noexcept {
    std::uint32_t expectedRev = 0u;
    std::uint32_t expectedW = 0u;
    std::uint32_t expectedH = 0u;
    std::string filePath;
    std::string memKey;
    std::vector<std::uint8_t> memBytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = textureSlots_.find(textureId);
        if (it == textureSlots_.end()) {
            return false;
        }
        const TextureSlotBuffer& slot = it->second;
        if (!slot.cpuPixelsEvicted) {
            const std::size_t bpp = slot.isAlpha8 ? 1u : 4u;
            const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * bpp;
            const bool hasPx = (slot.cowAtlasPixels && slot.cowAtlasPixels->size() >= expectedBytes) ||
                               (slot.pixelSnapshot && slot.pixelSnapshot->size() >= expectedBytes) ||
                               (slot.rgbaPixels.size() >= expectedBytes);
            return hasPx;
        }
        if (slot.pixelSnapshot &&
            slot.pixelSnapshot->size() >= static_cast<std::size_t>(slot.width) * slot.height * 4u) {
            slot.cpuPixelsEvicted = false;
            return true;
        }
        expectedRev = slot.dirtyRevision;
        expectedW = slot.width;
        expectedH = slot.height;
        filePath = slot.reloadableFilePath;
        memKey = slot.reloadableMemAssetKey;
        if (!memKey.empty()) {
            if (const auto mit = memoryEncodedAssets_.find(memKey); mit != memoryEncodedAssets_.end()) {
                memBytes = mit->second;
            }
        }
    }

    DecodedImage decoded{};
    bool ok = false;
    try {
        if (!memBytes.empty()) {
            ok = DecodeImageFromMemory(memBytes.data(), memBytes.size(), decoded);
        } else if (!filePath.empty()) {
            ok = DecodeImageFromFile(filePath, decoded);
        }
    } catch (...) {
        return false;
    }
    if (!ok || !decoded.Valid() || decoded.width != expectedW || decoded.height != expectedH) {
        return false;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> snap;
    try {
        snap = std::make_shared<const std::vector<std::uint8_t>>(std::move(decoded.rgbaPixels));
    } catch (...) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        return false;
    }
    const TextureSlotBuffer& slot = it->second;
    if (slot.isAtlas || slot.dirtyRevision != expectedRev ||
        slot.width != expectedW || slot.height != expectedH) {
        return false;
    }
    slot.pixelSnapshot = std::move(snap);
    slot.cpuPixelsEvicted = false;
    return true;
}

bool ImageAtlas::HasTextureSlot(std::uint32_t textureId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return textureSlots_.find(textureId) != textureSlots_.end();
}

const TextureSlotBuffer* ImageAtlas::GetTextureSlot(std::uint32_t textureId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    return (it != textureSlots_.end()) ? &it->second : nullptr;
}

std::uint64_t ImageAtlas::GetTextureSlotRevision(std::uint32_t textureId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    return (it != textureSlots_.end()) ? static_cast<std::uint64_t>(it->second.dirtyRevision) : 0u;
}

bool ImageAtlas::TryGetTextureSlotSnapshot(
    std::uint32_t textureId,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight,
    std::shared_ptr<const std::vector<std::uint8_t>>& outPixels,
    bool* optOutIsAlpha8) const noexcept {
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool needReload = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = textureSlots_.find(textureId);
            if (it == textureSlots_.end()) {
                outWidth = 0u;
                outHeight = 0u;
                outPixels.reset();
                if (optOutIsAlpha8) *optOutIsAlpha8 = false;
                return false;
            }
            const TextureSlotBuffer& slot = it->second;
            std::shared_ptr<const std::vector<std::uint8_t>> snap =
                slot.cowAtlasPixels
                    ? std::static_pointer_cast<const std::vector<std::uint8_t>>(slot.cowAtlasPixels)
                    : slot.pixelSnapshot;
            if (!snap && !slot.rgbaPixels.empty()) {
                try {
                    snap = std::make_shared<const std::vector<std::uint8_t>>(slot.rgbaPixels);
                    slot.pixelSnapshot = snap;
                } catch (...) {
                    snap.reset();
                }
            }
            const std::size_t bpp = slot.isAlpha8 ? 1u : 4u;
            const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * bpp;
            if (slot.width > 0u && slot.height > 0u && snap && snap->size() >= expectedBytes) {
                outWidth = slot.width;
                outHeight = slot.height;
                outPixels = std::move(snap);
                if (optOutIsAlpha8) *optOutIsAlpha8 = slot.isAlpha8;
                return true;
            }
            needReload = (attempt == 0 && slot.cpuPixelsEvicted);
        }
        if (!needReload || !TryReloadEvictedSlotPixels(textureId)) {
            break;
        }
    }
    outWidth = 0u;
    outHeight = 0u;
    outPixels.reset();
    if (optOutIsAlpha8) *optOutIsAlpha8 = false;
    return false;
}

bool ImageAtlas::CopyTextureSlotIfModified(
    std::uint32_t textureId,
    std::uint32_t& ioKnownSlotRevision,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight,
    std::vector<std::uint8_t>& outRgba8,
    bool& outWasUpdated) const {
    outWasUpdated = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool needReload = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = textureSlots_.find(textureId);
            if (it == textureSlots_.end()) {
                outWidth = 0u;
                outHeight = 0u;
                return false;
            }
            const TextureSlotBuffer& slot = it->second;
            if (slot.width > 0u && slot.height > 0u &&
                ioKnownSlotRevision != 0u &&
                ioKnownSlotRevision == slot.dirtyRevision &&
                slot.cpuPixelsEvicted) {
                outWidth = slot.width;
                outHeight = slot.height;
                return true;
            }
            const std::vector<std::uint8_t>& px =
                slot.cowAtlasPixels ? *slot.cowAtlasPixels
                                    : (slot.pixelSnapshot ? *slot.pixelSnapshot : slot.rgbaPixels);
            const std::size_t bpp = slot.isAlpha8 ? 1u : 4u;
            const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * bpp;
            if (slot.width > 0u && slot.height > 0u && px.size() >= expectedBytes) {
                outWidth = slot.width;
                outHeight = slot.height;
                if (ioKnownSlotRevision == 0u || ioKnownSlotRevision != slot.dirtyRevision) {
                    if (slot.isAlpha8) {
                        const std::size_t nPx = static_cast<std::size_t>(slot.width) * slot.height;
                        outRgba8.resize(nPx * 4u);
                        for (std::size_t i = 0u; i < nPx; ++i) {
                            const std::size_t o = i * 4u;
                            outRgba8[o + 0u] = 255u;
                            outRgba8[o + 1u] = 255u;
                            outRgba8[o + 2u] = 255u;
                            outRgba8[o + 3u] = px[i];
                        }
                    } else {
                        outRgba8.assign(px.data(), px.data() + expectedBytes);
                    }
                    ioKnownSlotRevision = slot.dirtyRevision;
                    outWasUpdated = true;
                }
                return true;
            }
            needReload = (attempt == 0 && slot.cpuPixelsEvicted);
        }
        if (!needReload || !TryReloadEvictedSlotPixels(textureId)) {
            break;
        }
    }
    outWidth = 0u;
    outHeight = 0u;
    return false;
}

bool ImageAtlas::AcquireTextureSlotUploadIfModified(
    std::uint32_t textureId,
    std::uint32_t& ioKnownSlotRevision,
    std::uint32_t& outSlotWidth,
    std::uint32_t& outSlotHeight,
    std::uint32_t& outDirtyX,
    std::uint32_t& outDirtyY,
    std::uint32_t& outDirtyW,
    std::uint32_t& outDirtyH,
    std::shared_ptr<const std::vector<std::uint8_t>>& outFullSnapshot,
    std::vector<std::uint8_t>& outSubRectRgba8,
    bool& outWasUpdated,
    bool* optOutIsAlpha8) const {
    outWasUpdated = false;
    outFullSnapshot.reset();
    if (optOutIsAlpha8) *optOutIsAlpha8 = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool needReload = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = textureSlots_.find(textureId);
            if (it == textureSlots_.end()) {
                outSlotWidth = 0u;
                outSlotHeight = 0u;
                return false;
            }
            const TextureSlotBuffer& slot = it->second;
            if (slot.width > 0u && slot.height > 0u &&
                ioKnownSlotRevision != 0u &&
                ioKnownSlotRevision == slot.dirtyRevision &&
                slot.cpuPixelsEvicted) {
                outSlotWidth = slot.width;
                outSlotHeight = slot.height;
                if (optOutIsAlpha8) *optOutIsAlpha8 = slot.isAlpha8;
                return true;
            }
            const std::shared_ptr<const std::vector<std::uint8_t>> snapPtr =
                slot.cowAtlasPixels ? std::static_pointer_cast<const std::vector<std::uint8_t>>(slot.cowAtlasPixels)
                                    : slot.pixelSnapshot;
            const std::vector<std::uint8_t>& px = snapPtr ? *snapPtr : slot.rgbaPixels;
            const std::size_t bpp = slot.isAlpha8 ? 1u : 4u;
            const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * bpp;
            if (slot.width > 0u && slot.height > 0u && px.size() >= expectedBytes) {
                outSlotWidth = slot.width;
                outSlotHeight = slot.height;
                if (optOutIsAlpha8) *optOutIsAlpha8 = slot.isAlpha8;
                if (ioKnownSlotRevision != 0u && ioKnownSlotRevision == slot.dirtyRevision) {
                    return true;
                }
                if (ioKnownSlotRevision != 0u &&
                    slot.isAtlas &&
                    slot.hasDirtyRect &&
                    ioKnownSlotRevision == slot.dirtyBaseRevision &&
                    slot.dirtyX1 > slot.dirtyX0 &&
                    slot.dirtyY1 > slot.dirtyY0 &&
                    slot.dirtyX1 <= slot.width &&
                    slot.dirtyY1 <= slot.height) {
                    const std::uint32_t subW = slot.dirtyX1 - slot.dirtyX0;
                    const std::uint32_t subH = slot.dirtyY1 - slot.dirtyY0;
                    outDirtyX = slot.dirtyX0;
                    outDirtyY = slot.dirtyY0;
                    outDirtyW = subW;
                    outDirtyH = subH;
                    outSubRectRgba8.resize(static_cast<std::size_t>(subW) * subH * 4u);
                    for (std::uint32_t row = 0u; row < subH; ++row) {
                        const std::uint8_t* srcRow = px.data() +
                            (static_cast<std::size_t>(slot.dirtyY0 + row) * slot.width + slot.dirtyX0) * 4u;
                        std::memcpy(
                            outSubRectRgba8.data() + static_cast<std::size_t>(row) * subW * 4u,
                            srcRow,
                            static_cast<std::size_t>(subW) * 4u);
                    }
                    slot.dirtyBaseRevision = slot.dirtyRevision;
                    slot.hasDirtyRect = false;
                    ioKnownSlotRevision = slot.dirtyRevision;
                    outWasUpdated = true;
                    return true;
                }
                outDirtyX = 0u;
                outDirtyY = 0u;
                outDirtyW = slot.width;
                outDirtyH = slot.height;
                if (snapPtr) {
                    outFullSnapshot = snapPtr;
                } else {
                    outSubRectRgba8.assign(px.data(), px.data() + expectedBytes);
                }
                slot.dirtyBaseRevision = slot.dirtyRevision;
                slot.hasDirtyRect = false;
                ioKnownSlotRevision = slot.dirtyRevision;
                outWasUpdated = true;
                return true;
            }
            needReload = (attempt == 0 && slot.cpuPixelsEvicted);
        }
        if (!needReload || !TryReloadEvictedSlotPixels(textureId)) {
            break;
        }
    }
    outSlotWidth = 0u;
    outSlotHeight = 0u;
    return false;
}

bool ImageAtlas::CopyTextureSlotDirtySubRectIfModified(
    std::uint32_t textureId,
    std::uint32_t& ioKnownSlotRevision,
    std::uint32_t& outSlotWidth,
    std::uint32_t& outSlotHeight,
    std::uint32_t& outDirtyX,
    std::uint32_t& outDirtyY,
    std::uint32_t& outDirtyW,
    std::uint32_t& outDirtyH,
    std::vector<std::uint8_t>& outSubRectRgba8,
    bool& outWasUpdated) const {
    outWasUpdated = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool needReload = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = textureSlots_.find(textureId);
            if (it == textureSlots_.end()) {
                outSlotWidth = 0u;
                outSlotHeight = 0u;
                return false;
            }
            const TextureSlotBuffer& slot = it->second;
            if (slot.width > 0u && slot.height > 0u &&
                ioKnownSlotRevision != 0u &&
                ioKnownSlotRevision == slot.dirtyRevision &&
                slot.cpuPixelsEvicted) {
                outSlotWidth = slot.width;
                outSlotHeight = slot.height;
                return true;
            }
            const std::vector<std::uint8_t>& px =
                slot.cowAtlasPixels ? *slot.cowAtlasPixels
                                    : (slot.pixelSnapshot ? *slot.pixelSnapshot : slot.rgbaPixels);
            const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
            if (slot.width > 0u && slot.height > 0u && px.size() >= expectedBytes) {
                outSlotWidth = slot.width;
                outSlotHeight = slot.height;
                if (ioKnownSlotRevision != 0u && ioKnownSlotRevision == slot.dirtyRevision) {
                    return true;
                }
                if (ioKnownSlotRevision != 0u &&
                    slot.isAtlas &&
                    slot.hasDirtyRect &&
                    ioKnownSlotRevision == slot.dirtyBaseRevision &&
                    slot.dirtyX1 > slot.dirtyX0 &&
                    slot.dirtyY1 > slot.dirtyY0 &&
                    slot.dirtyX1 <= slot.width &&
                    slot.dirtyY1 <= slot.height) {
                    const std::uint32_t subW = slot.dirtyX1 - slot.dirtyX0;
                    const std::uint32_t subH = slot.dirtyY1 - slot.dirtyY0;
                    outDirtyX = slot.dirtyX0;
                    outDirtyY = slot.dirtyY0;
                    outDirtyW = subW;
                    outDirtyH = subH;
                    outSubRectRgba8.resize(static_cast<std::size_t>(subW) * subH * 4u);
                    for (std::uint32_t row = 0u; row < subH; ++row) {
                        const std::uint8_t* srcRow = px.data() +
                            (static_cast<std::size_t>(slot.dirtyY0 + row) * slot.width + slot.dirtyX0) * 4u;
                        std::memcpy(
                            outSubRectRgba8.data() + static_cast<std::size_t>(row) * subW * 4u,
                            srcRow,
                            static_cast<std::size_t>(subW) * 4u);
                    }
                    slot.dirtyBaseRevision = slot.dirtyRevision;
                    slot.hasDirtyRect = false;
                    ioKnownSlotRevision = slot.dirtyRevision;
                    outWasUpdated = true;
                    return true;
                }
                outDirtyX = 0u;
                outDirtyY = 0u;
                outDirtyW = slot.width;
                outDirtyH = slot.height;
                outSubRectRgba8.assign(px.data(), px.data() + expectedBytes);
                slot.dirtyBaseRevision = slot.dirtyRevision;
                slot.hasDirtyRect = false;
                ioKnownSlotRevision = slot.dirtyRevision;
                outWasUpdated = true;
                return true;
            }
            needReload = (attempt == 0 && slot.cpuPixelsEvicted);
        }
        if (!needReload || !TryReloadEvictedSlotPixels(textureId)) {
            break;
        }
    }
    outSlotWidth = 0u;
    outSlotHeight = 0u;
    return false;
}

Color ImageAtlas::SampleBilinearRgba(std::uint32_t textureId, float u, float v) const noexcept {
    enum class SlotCacheStatus : std::uint8_t {
        Unset = 0,
        Valid = 1,
        Missing = 2,
        Invalid = 3,
    };
    struct TlsTexSampleSlot {
        std::uint64_t instanceId = 0u;
        std::uint64_t rev = 0u;
        std::uint32_t texId = 0xFFFFFFFFu;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        bool isAlpha8 = false;
        SlotCacheStatus status = SlotCacheStatus::Unset;
        std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    };
    thread_local std::array<TlsTexSampleSlot, 16> tlsTex{};
    const std::uint64_t curRev = revision_.load(std::memory_order_acquire);
    auto& ts = tlsTex[textureId & 15u];
    if (ts.instanceId != instanceId_ || ts.rev != curRev || ts.texId != textureId ||
        ts.status == SlotCacheStatus::Unset) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool needReload = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const std::uint64_t lockedRev = revision_.load(std::memory_order_acquire);
                const auto it = textureSlots_.find(textureId);
                ts.instanceId = instanceId_;
                ts.rev = lockedRev;
                ts.texId = textureId;
                if (it == textureSlots_.end()) {
                    ts.width = 0u;
                    ts.height = 0u;
                    ts.isAlpha8 = false;
                    ts.status = SlotCacheStatus::Missing;
                    ts.pixels.reset();
                    return Color{1.0f, 1.0f, 1.0f, 1.0f};
                }
                const TextureSlotBuffer& sBuf = it->second;
                std::shared_ptr<const std::vector<std::uint8_t>> snap =
                    sBuf.cowAtlasPixels
                        ? std::static_pointer_cast<const std::vector<std::uint8_t>>(sBuf.cowAtlasPixels)
                        : sBuf.pixelSnapshot;
                if (!snap && !sBuf.rgbaPixels.empty()) {
                    try {
                        snap = std::make_shared<const std::vector<std::uint8_t>>(sBuf.rgbaPixels);
                        sBuf.pixelSnapshot = snap;
                    } catch (...) {
                        snap.reset();
                    }
                }
                const std::size_t bpp = sBuf.isAlpha8 ? 1u : 4u;
                if (sBuf.width > 0u && sBuf.height > 0u && snap &&
                    snap->size() >= static_cast<std::size_t>(sBuf.width) * sBuf.height * bpp) {
                    ts.width = sBuf.width;
                    ts.height = sBuf.height;
                    ts.isAlpha8 = sBuf.isAlpha8;
                    ts.status = SlotCacheStatus::Valid;
                    ts.pixels = std::move(snap);
                    break;
                }
                needReload = (attempt == 0 && sBuf.cpuPixelsEvicted);
            }
            if (!needReload || !TryReloadEvictedSlotPixels(textureId)) {
                ts.width = 0u;
                ts.height = 0u;
                ts.isAlpha8 = false;
                ts.status = SlotCacheStatus::Invalid;
                ts.pixels.reset();
                return Color{0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
    }
    if (ts.status == SlotCacheStatus::Missing) {
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    if (ts.status != SlotCacheStatus::Valid || !ts.pixels || ts.width == 0u || ts.height == 0u) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const std::shared_ptr<const std::vector<std::uint8_t>> pixelHolder = ts.pixels;
    const std::uint32_t slotW = ts.width;
    const std::uint32_t slotH = ts.height;
    const bool isAlpha8 = ts.isAlpha8;
    const std::uint8_t* const pxData = pixelHolder->data();

    const float clampedU = std::clamp(u, 0.0f, 1.0f);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const float texX = clampedU * static_cast<float>(slotW) - 0.5f;
    const float texY = clampedV * static_cast<float>(slotH) - 0.5f;

    const int maxX = static_cast<int>(slotW) - 1;
    const int maxY = static_cast<int>(slotH) - 1;
    const int x0 = std::clamp(static_cast<int>(std::floor(texX)), 0, maxX);
    const int y0 = std::clamp(static_cast<int>(std::floor(texY)), 0, maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);
    const float fx = std::clamp(texX - std::floor(texX), 0.0f, 1.0f);
    const float fy = std::clamp(texY - std::floor(texY), 0.0f, 1.0f);

    constexpr float kInv255 = 1.0f / 255.0f;
    if (isAlpha8) {
        const float c00 = static_cast<float>(pxData[static_cast<std::size_t>(y0) * slotW + x0]);
        const float c10 = static_cast<float>(pxData[static_cast<std::size_t>(y0) * slotW + x1]);
        const float c01 = static_cast<float>(pxData[static_cast<std::size_t>(y1) * slotW + x0]);
        const float c11 = static_cast<float>(pxData[static_cast<std::size_t>(y1) * slotW + x1]);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return Color{1.0f, 1.0f, 1.0f, (top + (bot - top) * fy) * kInv255};
    }

    const std::uint8_t* p00 = pxData + (static_cast<std::size_t>(y0) * slotW + x0) * 4u;
    const std::uint8_t* p10 = pxData + (static_cast<std::size_t>(y0) * slotW + x1) * 4u;
    const std::uint8_t* p01 = pxData + (static_cast<std::size_t>(y1) * slotW + x0) * 4u;
    const std::uint8_t* p11 = pxData + (static_cast<std::size_t>(y1) * slotW + x1) * 4u;

    auto bilerpCh = [&](std::size_t ch) noexcept -> float {
        const float c00 = static_cast<float>(p00[ch]);
        const float c10 = static_cast<float>(p10[ch]);
        const float c01 = static_cast<float>(p01[ch]);
        const float c11 = static_cast<float>(p11[ch]);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return (top + (bot - top) * fy) * kInv255;
    };

    return Color{bilerpCh(0u), bilerpCh(1u), bilerpCh(2u), bilerpCh(3u)};
}

std::size_t ImageAtlas::LoadedEntryCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::size_t ImageAtlas::StandaloneTextureCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0u;
    for (const auto& [tid, slot] : textureSlots_) {
        if (tid != kSharedUiAtlasTextureId && !slot.isAtlas) {
            ++count;
        }
    }
    return count;
}

std::size_t ImageAtlas::EstimatedMemoryBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0u;
    for (const auto& [k, bytes] : memoryEncodedAssets_) {
        total += k.capacity() + bytes.capacity();
    }
    for (const auto& [tid, slot] : textureSlots_) {
        (void)tid;
        total += slot.rgbaPixels.capacity();
        if (slot.pixelSnapshot) {
            total += slot.pixelSnapshot->capacity();
        }
        if (slot.cowAtlasPixels) {
            total += slot.cowAtlasPixels->capacity();
        }
    }
    total += entries_.size() * (sizeof(ImageAtlasEntry) + 48u);
    return total;
}

void ImageAtlas::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    sharedAtlasPages_.clear();
    memoryEncodedAssets_.clear();
    entries_.clear();
    packedMasks_.clear();
    packedMaskAreaTexels_ = 0u;
    plasmaSlots_.clear();
    riveSlots_.clear();
    visualBrushSlots_.clear();
    dynamicSlotTick_ = 0u;
    textureSlots_.clear();
    freeStandaloneTextureIds_.clear();
    nextStandaloneTextureId_ = 1u;
    revision_.fetch_add(1u, std::memory_order_release);
}

bool CompositeMaskedPathTexture(
    UIResourceStore& store,
    ImageAtlas& atlas,
    std::uint64_t pathHash,
    std::string_view pathData,
    const Rect& rawBounds,
    const Rect& elemRect,
    const Rect& maskFittedRect,
    std::uint32_t innerW,
    std::uint32_t innerH,
    std::uint32_t padPx,
    const ImageAtlasEntry& imgEntry,
    Stretch fillStretch,
    std::uint32_t fillTintRgba,
    bool tileWrap,
    RasterizedPathMaskEntry& outEntry) {
    if (pathData.empty() || imgEntry.width == 0u || imgEntry.height == 0u) {
        return false;
    }

    std::uint32_t slotW = 0u;
    std::uint32_t slotH = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> slotPixels;
    if (!atlas.TryGetTextureSlotSnapshot(imgEntry.textureId, slotW, slotH, slotPixels) ||
        slotW == 0u || slotH == 0u || !slotPixels ||
        slotPixels->size() < static_cast<std::size_t>(slotW) * static_cast<std::size_t>(slotH) * 4u) {
        return false;
    }

    const std::uint64_t safeHash = (pathHash != 0ull) ? pathHash : ComputeSvgPathHash(pathData);
    const std::uint32_t clampedW = std::clamp<std::uint32_t>(innerW, 1u, 2048u);
    const std::uint32_t clampedH = std::clamp<std::uint32_t>(innerH, 1u, 2048u);
    const std::uint32_t clampedPad = std::min<std::uint32_t>(padPx, 256u);
    const std::uint64_t texRev = atlas.GetTextureSlotRevision(imgEntry.textureId);

    std::uint64_t key = safeHash ^ 0xA341316C98765431ULL;
    auto mixU64 = [&](std::uint64_t v) noexcept {
        key ^= v;
        key *= 1099511628211ULL;
    };
    auto mixFloat = [&](float v) noexcept {
        const float clean = (v == 0.0f) ? 0.0f : v;
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &clean, sizeof(bits));
        mixU64(static_cast<std::uint64_t>(bits));
    };

    mixU64((static_cast<std::uint64_t>(clampedW) << 32u) | static_cast<std::uint64_t>(clampedH));
    mixU64(static_cast<std::uint64_t>(clampedPad));
    for (unsigned char ch : imgEntry.uriKey) {
        mixU64(static_cast<std::uint64_t>(ch));
    }
    mixU64((static_cast<std::uint64_t>(imgEntry.textureId) << 32u) ^ texRev);
    mixU64((static_cast<std::uint64_t>(imgEntry.width) << 32u) | static_cast<std::uint64_t>(imgEntry.height));
    mixFloat(imgEntry.u0);
    mixFloat(imgEntry.v0);
    mixFloat(imgEntry.u1);
    mixFloat(imgEntry.v1);
    mixU64((static_cast<std::uint64_t>(fillStretch) << 33u) ^
           (static_cast<std::uint64_t>(fillTintRgba) << 1u) ^
           (tileWrap ? 1ull : 0ull));
    mixFloat(rawBounds.x);
    mixFloat(rawBounds.y);
    mixFloat(rawBounds.width);
    mixFloat(rawBounds.height);
    mixFloat(elemRect.x);
    mixFloat(elemRect.y);
    mixFloat(elemRect.width);
    mixFloat(elemRect.height);
    mixFloat(maskFittedRect.x);
    mixFloat(maskFittedRect.y);
    mixFloat(maskFittedRect.width);
    mixFloat(maskFittedRect.height);

    if (store.LookupCachedPathMask(key, outEntry)) {
        return true;
    }

    // Cache static 1-channel SVG path alpha masks across frames so animated brushes (PlasmaBrush)
    // never re-parse or supersample-rasterize invariant vector outlines when texRev changes!
    const std::uint32_t maskTexW = clampedW + 2u * clampedPad;
    const std::uint32_t maskTexH = clampedH + 2u * clampedPad;
    const std::size_t maskPixels = static_cast<std::size_t>(maskTexW) * maskTexH;
    std::uint64_t alphaShapeKey = safeHash ^ 0x71E2D3C4B5A69788ULL;
    alphaShapeKey ^= (static_cast<std::uint64_t>(clampedW) << 32u) | static_cast<std::uint64_t>(clampedH);
    alphaShapeKey *= 1099511628211ULL;
    alphaShapeKey ^= static_cast<std::uint64_t>(clampedPad);
    alphaShapeKey *= 1099511628211ULL;

    struct StaticPathAlphaCache {
        std::mutex mutex;
        std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> masks;
        std::size_t totalBytes = 0u;
    };
    static StaticPathAlphaCache sAlphaCache;

    std::vector<std::uint8_t> maskRgba;
    bool hitAlphaCache = false;
    if (maskPixels <= 512u * 512u) {
        std::lock_guard<std::mutex> aLock(sAlphaCache.mutex);
        if (const auto ait = sAlphaCache.masks.find(alphaShapeKey);
            ait != sAlphaCache.masks.end() && ait->second.size() == maskPixels) {
            maskRgba.resize(maskPixels * 4u);
            const std::uint8_t* srcA = ait->second.data();
            for (std::size_t i = 0u; i < maskPixels; ++i) {
                maskRgba[i * 4u + 3u] = srcA[i];
            }
            hitAlphaCache = true;
        }
    }
    if (!hitAlphaCache) {
        if (!RasterizeSvgPathFillAlphaMask(pathData, rawBounds, clampedW, clampedH, clampedPad, maskRgba)) {
            return false;
        }
        if (maskPixels <= 512u * 512u && maskRgba.size() >= maskPixels * 4u) {
            std::vector<std::uint8_t> compactA(maskPixels);
            for (std::size_t i = 0u; i < maskPixels; ++i) {
                compactA[i] = maskRgba[i * 4u + 3u];
            }
            std::lock_guard<std::mutex> aLock(sAlphaCache.mutex);
            if (sAlphaCache.masks.size() >= 48u || sAlphaCache.totalBytes + maskPixels > 8u * 1024u * 1024u) {
                sAlphaCache.masks.clear();
                sAlphaCache.totalBytes = 0u;
            }
            sAlphaCache.totalBytes += compactA.size();
            sAlphaCache.masks[alphaShapeKey] = std::move(compactA);
        }
    }

    const std::uint32_t texW = clampedW + 2u * clampedPad;
    const std::uint32_t texH = clampedH + 2u * clampedPad;
    const std::size_t expectedBytes = static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH) * 4u;
    if (maskRgba.size() < expectedBytes) {
        return false;
    }

    const ImageStretchPlacement place = ComputeImageStretchPlacement(
        elemRect,
        static_cast<float>(imgEntry.width),
        static_cast<float>(imgEntry.height),
        fillStretch,
        StretchDirection::Both,
        imgEntry.u0,
        imgEntry.v0,
        imgEntry.u1,
        imgEntry.v1);

    constexpr float kInv255 = 1.0f / 255.0f;
    // Colors are packed in 0xAABBGGRR format (R = low byte, B = bits 16..23)
    const std::uint32_t effTint = (fillTintRgba != 0u) ? fillTintRgba : 0xFFFFFFFFu;
    const float tr = static_cast<float>(effTint & 0xFFu) * kInv255;
    const float tg = static_cast<float>((effTint >> 8u) & 0xFFu) * kInv255;
    const float tb = static_cast<float>((effTint >> 16u) & 0xFFu) * kInv255;
    const float ta = static_cast<float>((effTint >> 24u) & 0xFFu) * kInv255;

    const std::uint8_t* const srcPx = slotPixels->data();
    const int maxX = static_cast<int>(slotW) - 1;
    const int maxY = static_cast<int>(slotH) - 1;
    const float maxXf = static_cast<float>(maxX);
    const float maxYf = static_cast<float>(maxY);
    const bool validDrawBox = place.valid && (place.destRect.width > 1e-6f && place.destRect.height > 1e-6f);

    for (std::uint32_t py = 0u; py < texH; ++py) {
        const float ny = ((static_cast<float>(py) - static_cast<float>(clampedPad)) + 0.5f) /
                         static_cast<float>(clampedH);
        const float wy = maskFittedRect.y + ny * maskFittedRect.height;
        const float rawRy = validDrawBox ? ((wy - place.destRect.y) / place.destRect.height) : 0.0f;
        for (std::uint32_t px = 0u; px < texW; ++px) {
            std::uint8_t* dst = maskRgba.data() + (static_cast<std::size_t>(py) * texW + px) * 4u;
            const std::uint8_t maskAlpha = dst[3];
            if (maskAlpha == 0u || !validDrawBox) {
                dst[0] = 0u;
                dst[1] = 0u;
                dst[2] = 0u;
                dst[3] = 0u;
                continue;
            }
            const float nx = ((static_cast<float>(px) - static_cast<float>(clampedPad)) + 0.5f) /
                             static_cast<float>(clampedW);
            const float wx = maskFittedRect.x + nx * maskFittedRect.width;
            float rx = (wx - place.destRect.x) / place.destRect.width;
            float ry = rawRy;
            if (tileWrap) {
                rx = rx - std::floor(rx);
                ry = ry - std::floor(ry);
            } else if (rx < 0.0f || rx > 1.0f || ry < 0.0f || ry > 1.0f) {
                dst[0] = 0u;
                dst[1] = 0u;
                dst[2] = 0u;
                dst[3] = 0u;
                continue;
            }

            const float u = std::clamp(place.u0 + rx * (place.u1 - place.u0), 0.0f, 1.0f);
            const float v = std::clamp(place.v0 + ry * (place.v1 - place.v0), 0.0f, 1.0f);
            const float texX = std::clamp(u * static_cast<float>(slotW) - 0.5f, 0.0f, maxXf);
            const float texY = std::clamp(v * static_cast<float>(slotH) - 0.5f, 0.0f, maxYf);
            const int x0 = std::clamp(static_cast<int>(std::floor(texX)), 0, maxX);
            const int y0 = std::clamp(static_cast<int>(std::floor(texY)), 0, maxY);
            const int x1 = std::min(x0 + 1, maxX);
            const int y1 = std::min(y0 + 1, maxY);
            const float fx = std::clamp(texX - std::floor(texX), 0.0f, 1.0f);
            const float fy = std::clamp(texY - std::floor(texY), 0.0f, 1.0f);

            const std::uint8_t* p00 = srcPx + (static_cast<std::size_t>(y0) * slotW + x0) * 4u;
            const std::uint8_t* p10 = srcPx + (static_cast<std::size_t>(y0) * slotW + x1) * 4u;
            const std::uint8_t* p01 = srcPx + (static_cast<std::size_t>(y1) * slotW + x0) * 4u;
            const std::uint8_t* p11 = srcPx + (static_cast<std::size_t>(y1) * slotW + x1) * 4u;

            auto sampleCh = [&](std::size_t ch) noexcept -> float {
                const float top = static_cast<float>(p00[ch]) +
                                  (static_cast<float>(p10[ch]) - static_cast<float>(p00[ch])) * fx;
                const float bot = static_cast<float>(p01[ch]) +
                                  (static_cast<float>(p11[ch]) - static_cast<float>(p01[ch])) * fx;
                return top + (bot - top) * fy;
            };

            const float sr = sampleCh(0u) * tr;
            const float sg = sampleCh(1u) * tg;
            const float sb = sampleCh(2u) * tb;
            const float sa = sampleCh(3u) * ta * (static_cast<float>(maskAlpha) * kInv255);
            dst[0] = static_cast<std::uint8_t>(std::clamp(sr, 0.0f, 255.0f) + 0.5f);
            dst[1] = static_cast<std::uint8_t>(std::clamp(sg, 0.0f, 255.0f) + 0.5f);
            dst[2] = static_cast<std::uint8_t>(std::clamp(sb, 0.0f, 255.0f) + 0.5f);
            dst[3] = static_cast<std::uint8_t>(std::clamp(sa, 0.0f, 255.0f) + 0.5f);
        }
    }

    const float u0 = static_cast<float>(clampedPad) / static_cast<float>(texW);
    const float v0 = static_cast<float>(clampedPad) / static_cast<float>(texH);
    const float u1 = static_cast<float>(clampedPad + clampedW) / static_cast<float>(texW);
    const float v1 = static_cast<float>(clampedPad + clampedH) / static_cast<float>(texH);
    const std::uint64_t imgGroup =
        safeHash ^
        (static_cast<std::uint64_t>(imgEntry.textureId) << 32u) ^
        static_cast<std::uint64_t>(fillTintRgba) ^
        0xC3C3C3C300000003ULL;
    return store.StoreCachedPathMask(key, texW, texH, u0, v0, u1, v1, std::move(maskRgba), outEntry, imgGroup);
}

bool CompositeMaskedPathImage(
    UIResourceStore& store,
    ImageAtlas& atlas,
    std::uint64_t pathHash,
    std::string_view pathData,
    const Rect& rawBounds,
    const Rect& elemRect,
    const Rect& maskFittedRect,
    std::uint32_t innerW,
    std::uint32_t innerH,
    std::uint32_t padPx,
    std::string_view fillImageUri,
    Stretch fillStretch,
    std::uint32_t fillTintRgba,
    bool tileWrap,
    RasterizedPathMaskEntry& outEntry) {
    if (pathData.empty() || fillImageUri.empty()) {
        return false;
    }
    const ImageAtlasEntry* imgEntry = atlas.LoadOrGet(fillImageUri);
    if (!imgEntry || imgEntry->width == 0u || imgEntry->height == 0u) {
        return false;
    }
    const ImageAtlasEntry entryCopy = *imgEntry;
    return CompositeMaskedPathTexture(
        store,
        atlas,
        pathHash,
        pathData,
        rawBounds,
        elemRect,
        maskFittedRect,
        innerW,
        innerH,
        padPx,
        entryCopy,
        fillStretch,
        fillTintRgba,
        tileWrap,
        outEntry);
}

} // namespace aero
