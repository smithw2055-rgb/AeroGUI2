#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/controls/Controls.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

enum class ImageFormat : std::uint8_t {
    Unknown = 0,
    Png = 1,
    Jpeg = 2,
    Bmp = 3,
    Tga = 4,
    RawRgba8 = 5,
};

struct DecodedImage {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    ImageFormat format = ImageFormat::Unknown;
    std::vector<std::uint8_t> rgbaPixels;

    [[nodiscard]] bool Valid() const noexcept {
        return width > 0u && height > 0u &&
               rgbaPixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

struct ImageStretchPlacement {
    Rect destRect{0.0f, 0.0f, 0.0f, 0.0f};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    bool valid = false;
};

struct ImageAtlasEntry {
    std::string uriKey;
    std::uint32_t textureId = 0u; // 0 = shared dynamic Skyline UI atlas, >0 = standalone texture slot
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t atlasX = 0u;
    std::uint32_t atlasY = 0u;
    std::uint32_t atlasWidth = 0u;
    std::uint32_t atlasHeight = 0u;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    bool isStandalone = false;
};

struct TextureSlotBuffer {
    std::uint32_t textureId = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t dirtyRevision = 0u;
    bool isAtlas = false;
    std::vector<std::uint8_t> rgbaPixels;
};

// Pure C++17 zero-dependency RFC1951 DEFLATE & RFC1950 zlib decompression (100% Wasm-ready)
[[nodiscard]] bool InflateRfc1951(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData,
    std::size_t* outConsumedBytes = nullptr) noexcept;

[[nodiscard]] bool DecompressZlib(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData) noexcept;

// Format sniffing & pure C++17 zero-dependency RGBA8 image decoders (PNG / JPEG / BMP / TGA)
[[nodiscard]] ImageFormat DetectImageFormat(const std::uint8_t* data, std::size_t size) noexcept;

[[nodiscard]] bool DecodePngFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeJpegFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeBmpFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeTgaFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeImageFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeImageFromFile(
    std::string_view filePath,
    DecodedImage& outImage,
    std::string* outError = nullptr);

// Normalize XAML/pack URIs (strips pack://application:,,,/Assembly;component/ prefixes, normalizes slashes & ..)
[[nodiscard]] std::string NormalizeImageUriPath(
    std::string_view rawUri,
    std::string_view baseDir = {});

// Compute exact destination quad & cropped UV sub-rect for Stretch::None / Fill / Uniform / UniformToFill
// without requiring GPU Scissor CommandPage splits when UniformToFill or None crops overflow!
[[nodiscard]] ImageStretchPlacement ComputeImageStretchPlacement(
    const Rect& contentBox,
    float naturalWidth,
    float naturalHeight,
    Stretch stretch,
    StretchDirection stretchDirection = StretchDirection::Both,
    float baseU0 = 0.0f,
    float baseV0 = 0.0f,
    float baseU1 = 1.0f,
    float baseV1 = 1.0f) noexcept;

// Dynamic Skyline UI Texture Atlas (textureId = 0) & Multi-Texture Slot Manager (textureId > 0)
class ImageAtlas {
public:
    static constexpr std::uint32_t kSharedUiAtlasTextureId = 0u;
    static constexpr std::uint32_t kDefaultSharedAtlasSize = 2048u;
    static constexpr std::uint32_t kDefaultMaxPackableExtent = 512u;

    static ImageAtlas& Instance() noexcept;

    ImageAtlas() noexcept;
    ~ImageAtlas() = default;

    ImageAtlas(const ImageAtlas&) = delete;
    ImageAtlas& operator=(const ImageAtlas&) = delete;

    // Configure shared Skyline atlas page size and max sprite dimensions eligible for atlas packing.
    // Images exceeding maxPackableWidth or maxPackableHeight receive dedicated standalone textureIds (> 0).
    void ConfigureAtlas(
        std::uint32_t sharedAtlasSize,
        std::uint32_t maxPackableWidth,
        std::uint32_t maxPackableHeight) noexcept;

    [[nodiscard]] std::uint32_t SharedAtlasSize() const noexcept;
    [[nodiscard]] std::uint32_t MaxPackableWidth() const noexcept;
    [[nodiscard]] std::uint32_t MaxPackableHeight() const noexcept;

    // Primary XAML base directory & fallback search roots for relative URIs (e.g. "Images/Earth.png")
    void SetBaseDirectory(std::string_view baseDir);
    [[nodiscard]] std::string GetBaseDirectory() const;
    void AddSearchDirectory(std::string_view dirPath);
    void ClearSearchDirectories() noexcept;

    // Register in-memory encoded image bytes (PNG/JPEG/BMP/TGA) for Wasm / embedded binary providers
    bool RegisterEncodedMemoryAsset(
        std::string_view uriKey,
        const std::uint8_t* encodedBytes,
        std::size_t byteSize);

    // Register pre-decoded RGBA8 image directly into Skyline UI atlas or standalone texture slot
    const ImageAtlasEntry* RegisterDecodedImage(
        std::string_view uriKey,
        DecodedImage image,
        bool forceStandalone = false);

    // Resolve URI (memory provider -> direct path -> localBaseDir -> baseDir -> searchDirs), decode & cache
    const ImageAtlasEntry* LoadOrGet(
        std::string_view uri,
        std::string_view localBaseDir = {});

    [[nodiscard]] const ImageAtlasEntry* FindLoaded(
        std::string_view uri,
        std::string_view localBaseDir = {}) const noexcept;

    [[nodiscard]] std::string ResolveUriToPath(
        std::string_view uri,
        std::string_view localBaseDir = {}) const;

    // Texture slot inspection & headless bilinear RGBA8 sampler for ZeroCopyGpuStream::RasterizeTreeToRgba8
    [[nodiscard]] bool HasTextureSlot(std::uint32_t textureId) const noexcept;
    [[nodiscard]] const TextureSlotBuffer* GetTextureSlot(std::uint32_t textureId) const noexcept;
    [[nodiscard]] Color SampleBilinearRgba(std::uint32_t textureId, float u, float v) const noexcept;

    [[nodiscard]] std::size_t LoadedEntryCount() const noexcept;
    [[nodiscard]] std::size_t StandaloneTextureCount() const noexcept;

    void Clear() noexcept;

private:
    struct SkylineNode {
        std::uint32_t x = 0u;
        std::uint32_t y = 0u;
        std::uint32_t width = 0u;
    };

    void EnsureSharedAtlasAllocatedLocked();
    bool TryAllocateSkylineRectLocked(
        std::uint32_t paddedW,
        std::uint32_t paddedH,
        std::uint32_t& outX,
        std::uint32_t& outY) noexcept;
    void BlitWithExtrudedBorderLocked(
        TextureSlotBuffer& atlasBuf,
        std::uint32_t innerX,
        std::uint32_t innerY,
        const DecodedImage& img) noexcept;
    const ImageAtlasEntry* InsertDecodedLocked(
        std::string canonicalKey,
        DecodedImage&& image,
        bool forceStandalone);
    [[nodiscard]] std::string ResolveExistingFileLocked(
        std::string_view cleanRelOrAbs,
        std::string_view localBaseDir) const;

    mutable std::mutex mutex_;
    std::uint32_t sharedAtlasSize_ = kDefaultSharedAtlasSize;
    std::uint32_t maxPackableWidth_ = kDefaultMaxPackableExtent;
    std::uint32_t maxPackableHeight_ = kDefaultMaxPackableExtent;
    std::uint32_t nextStandaloneTextureId_ = 1u;

    std::string baseDirectory_;
    std::vector<std::string> searchDirectories_;
    std::vector<SkylineNode> skyline_;

    std::unordered_map<std::string, std::vector<std::uint8_t>> memoryEncodedAssets_;
    std::unordered_map<std::string, ImageAtlasEntry> entries_;
    std::unordered_map<std::uint32_t, TextureSlotBuffer> textureSlots_;
};

} // namespace aero
