#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/asset/GradientBrush.hpp"
#include "aero/asset/MtsdfPathBaker.hpp"

namespace aero {

class FontAtlas;
class ImageAtlas;

struct RasterizedPathMaskEntry {
    std::uint32_t textureId = 0u;
    std::uint32_t texWidth = 0u;
    std::uint32_t texHeight = 0u;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    std::uint64_t lastUsedTick = 0u;
};

/// Explicit per-view resource store owning FontAtlas, ImageAtlas,
/// GradientBrushRegistry, MTSDF path cache, and asset search directories.
/// Zero thread_local global state: pass explicit UIResourceStore& / pointers
/// across views or bind via UIResourceStore::Default() for host-wide defaults.
class UIResourceStore final {
public:
    using FontAtlasFactoryFn = std::shared_ptr<FontAtlas> (*)(std::uint16_t atlasSize);
    using FontAtlasClearFn = void (*)(FontAtlas& atlas) noexcept;
    using ImageAtlasFactoryFn = std::shared_ptr<ImageAtlas> (*)();
    using ImageAtlasClearFn = void (*)(ImageAtlas& atlas) noexcept;
    using ImageAtlasRegisterSlotFn = std::uint32_t (*)(
        ImageAtlas& atlas,
        std::uint32_t reuseSlotId,
        std::uint32_t width,
        std::uint32_t height,
        std::vector<std::uint8_t>&& rgbaPixels);

    static constexpr std::uint16_t kDefaultFontAtlasSize = 1024u;

    explicit UIResourceStore(std::uint16_t fontAtlasSize = kDefaultFontAtlasSize);
    ~UIResourceStore();

    UIResourceStore(const UIResourceStore&) = delete;
    UIResourceStore& operator=(const UIResourceStore&) = delete;
    UIResourceStore(UIResourceStore&&) = delete;
    UIResourceStore& operator=(UIResourceStore&&) = delete;

    [[nodiscard]] static UIResourceStore& Default() noexcept;

    static void RegisterFontAtlasHooks(
        FontAtlasFactoryFn factoryFn,
        FontAtlasClearFn clearFn) noexcept;

    static void RegisterImageAtlasHooks(
        ImageAtlasFactoryFn factoryFn,
        ImageAtlasClearFn clearFn) noexcept;
    static void RegisterImageAtlasSlotHook(
        ImageAtlasRegisterSlotFn registerSlotFn) noexcept;

    [[nodiscard]] FontAtlas& Fonts();
    [[nodiscard]] const FontAtlas& Fonts() const;
    [[nodiscard]] FontAtlas* TryFonts() noexcept { return fonts_.get(); }
    [[nodiscard]] const FontAtlas* TryFonts() const noexcept { return fonts_.get(); }
    void SetFontAtlas(std::shared_ptr<FontAtlas> fonts) noexcept;

    [[nodiscard]] ImageAtlas& Images();
    [[nodiscard]] const ImageAtlas& Images() const;
    [[nodiscard]] ImageAtlas* TryImages() noexcept { return images_.get(); }
    [[nodiscard]] const ImageAtlas* TryImages() const noexcept { return images_.get(); }
    void SetImageAtlas(std::shared_ptr<ImageAtlas> images) noexcept;

    [[nodiscard]] GradientBrushRegistry& GradientBrushes() noexcept { return gradientBrushes_; }
    [[nodiscard]] const GradientBrushRegistry& GradientBrushes() const noexcept { return gradientBrushes_; }

    void AddAssetSearchDirectory(std::string_view directory);
    void ClearAssetSearchDirectories() noexcept;
    [[nodiscard]] std::vector<std::string> AssetSearchDirectoriesSnapshot() const;

    void RegisterMtsdfPath(const AxbMtsdfPathEntry& entry);
    [[nodiscard]] const AxbMtsdfPathEntry* FindMtsdfPath(std::uint64_t pathHash) const noexcept;
    void ClearMtsdfPaths() noexcept;

    [[nodiscard]] bool GetOrRasterizePathMask(
        std::uint64_t pathHash,
        std::string_view pathData,
        const Rect& rawBounds,
        std::uint32_t innerW,
        std::uint32_t innerH,
        std::uint32_t padPx,
        RasterizedPathMaskEntry& outEntry);

    [[nodiscard]] bool GetOrRasterizePathStrokeMask(
        std::uint64_t pathHash,
        std::string_view pathData,
        const Rect& rawBounds,
        std::uint32_t innerW,
        std::uint32_t innerH,
        float strokeWidthPx,
        std::uint32_t padPx,
        RasterizedPathMaskEntry& outEntry);

    void ClearAll() noexcept;

private:
    void EnsureFontsInitialized() const;
    void EnsureImagesInitialized() const;
    [[nodiscard]] bool StoreOrLookupPathMaskLocked(
        std::uint64_t key,
        std::uint32_t texW,
        std::uint32_t texH,
        float u0,
        float v0,
        float u1,
        float v1,
        std::vector<std::uint8_t>&& rgba8,
        RasterizedPathMaskEntry& outEntry);

    std::uint16_t fontAtlasSize_ = kDefaultFontAtlasSize;
    mutable std::shared_ptr<FontAtlas> fonts_;
    mutable std::shared_ptr<ImageAtlas> images_;
    GradientBrushRegistry gradientBrushes_;

    mutable std::mutex mutex_;
    std::vector<std::string> assetSearchDirs_;
    std::unordered_map<std::uint64_t, AxbMtsdfPathEntry> mtsdfEntries_;
    std::unordered_map<std::uint64_t, RasterizedPathMaskEntry> pathMaskEntries_;
    std::uint64_t pathMaskAccessSeq_ = 0u;
};

} // namespace aero
