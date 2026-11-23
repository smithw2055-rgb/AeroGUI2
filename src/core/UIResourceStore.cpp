#include "aero/core/UIResourceStore.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace {

std::atomic<UIResourceStore::FontAtlasFactoryFn> gFontFactory{nullptr};
std::atomic<UIResourceStore::FontAtlasClearFn> gFontClear{nullptr};
std::atomic<UIResourceStore::ImageAtlasFactoryFn> gImageFactory{nullptr};
std::atomic<UIResourceStore::ImageAtlasClearFn> gImageClear{nullptr};
std::atomic<UIResourceStore::ImageAtlasRegisterSlotFn> gImageRegisterSlot{nullptr};

[[nodiscard]] int HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

[[nodiscard]] int HexByte(char hi, char lo) noexcept {
    return (HexNibble(hi) << 4) | HexNibble(lo);
}

[[nodiscard]] bool IsHexDigit(char c) noexcept {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

[[nodiscard]] Color ParseHexColor(std::string_view hex) noexcept {
    for (char c : hex) {
        if (!IsHexDigit(c)) {
            return Color{0.0f, 0.0f, 0.0f, 1.0f};
        }
    }
    if (hex.size() == 3u) {
        return Color{
            static_cast<float>(HexNibble(hex[0]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[1]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[2]) * 17) / 255.0f,
            1.0f};
    }
    if (hex.size() == 4u) {
        return Color{
            static_cast<float>(HexNibble(hex[1]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[2]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[3]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[0]) * 17) / 255.0f};
    }
    if (hex.size() == 6u) {
        return Color{
            static_cast<float>(HexByte(hex[0], hex[1])) / 255.0f,
            static_cast<float>(HexByte(hex[2], hex[3])) / 255.0f,
            static_cast<float>(HexByte(hex[4], hex[5])) / 255.0f,
            1.0f};
    }
    if (hex.size() == 8u) {
        return Color{
            static_cast<float>(HexByte(hex[2], hex[3])) / 255.0f,
            static_cast<float>(HexByte(hex[4], hex[5])) / 255.0f,
            static_cast<float>(HexByte(hex[6], hex[7])) / 255.0f,
            static_cast<float>(HexByte(hex[0], hex[1])) / 255.0f};
    }
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

[[nodiscard]] std::uint32_t ClampFloatToByte(float v) noexcept {
    if (!std::isfinite(v)) return 0u;
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
}

} // namespace

Color ParseColorLiteral(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (!raw.empty() && raw.front() == '#') {
        raw.remove_prefix(1u);
        return ParseHexColor(raw);
    }
    struct NamedColorEntry {
        std::string_view name;
        std::string_view hex8;
    };
    static constexpr NamedColorEntry kNamedColors[] = {
        {"Transparent", "00000000"},
        {"White", "FFFFFFFF"},
        {"Black", "FF000000"},
        {"Red", "FFFF0000"},
        {"Green", "FF008000"},
        {"Blue", "FF0000FF"},
        {"Yellow", "FFFFFF00"},
        {"Cyan", "FF00FFFF"},
        {"Aqua", "FF00FFFF"},
        {"Magenta", "FFFF00FF"},
        {"Fuchsia", "FFFF00FF"},
        {"Gray", "FF808080"},
        {"Grey", "FF808080"},
        {"LightGray", "FFD3D3D3"},
        {"DarkGray", "FFA9A9A9"},
        {"Silver", "FFC0C0C0"},
        {"WhiteSmoke", "FFF5F5F5"},
        {"LightSeaGreen", "FF20B2AA"},
        {"MediumSeaGreen", "FF3CB371"},
        {"SeaGreen", "FF2E8B57"},
        {"Turquoise", "FF40E0D0"},
        {"MediumTurquoise", "FF48D1CC"},
        {"DarkTurquoise", "FF00CED1"},
        {"DodgerBlue", "FF1E90FF"},
        {"DeepSkyBlue", "FF00BFFF"},
        {"SkyBlue", "FF87CEEB"},
        {"LightSkyBlue", "FF87CEFA"},
        {"CornflowerBlue", "FF6495ED"},
        {"RoyalBlue", "FF4169E1"},
        {"SteelBlue", "FF4682B4"},
        {"Teal", "FF008080"},
        {"OrangeRed", "FFFF4500"},
        {"Orange", "FFFFA500"},
        {"Gold", "FFFFD700"},
        {"Crimson", "FFDC143C"},
        {"Tomato", "FFFF6347"},
        {"Coral", "FFFF7F50"},
        {"Salmon", "FFFA8072"},
        {"Lime", "FF00FF00"},
        {"LimeGreen", "FF32CD32"},
        {"LawnGreen", "FF7CFC00"},
        {"GreenYellow", "FFADFF2F"},
        {"Purple", "FF800080"},
        {"Violet", "FFEE82EE"},
        {"Plum", "FFDDA0DD"},
        {"Indigo", "FF4B0082"},
        {"SlateGray", "FF708090"},
        {"DarkSlateGray", "FF2F4F4F"},
        {"Brown", "FFA52A2A"},
        {"SaddleBrown", "FF8B4513"},
        {"Tan", "FFD2B48C"},
        {"Wheat", "FFF5DEB3"},
        {"Beige", "FFF5F5DC"},
        {"Ivory", "FFFFFFF0"},
        {"Navy", "FF000080"},
        {"MidnightBlue", "FF191970"},
        {"Maroon", "FF800000"},
        {"Olive", "FF808000"},
        {"Pink", "FFFFC0CB"},
        {"HotPink", "FFFF69B4"},
        {"DeepPink", "FFFF1493"},
        {"Moccasin", "FFFFE4B5"},
        {"Aquamarine", "FF7FFFD4"},
        {"Thistle", "FFD8BFD8"},
        {"CadetBlue", "FF5F9EA0"},
        {"PaleTurquoise", "FFAFEEEE"},
        {"YellowGreen", "FF9ACD32"},
        {"AliceBlue", "FFF0F8FF"},
        {"Khaki", "FFF0E68C"},
        {"Orchid", "FFDA70D6"},
        {"SlateGrey", "FF708090"},
        {"LightSlateGray", "FF778899"},
        {"LightSlateGrey", "FF778899"},
        {"DarkSlateGrey", "FF2F4F4F"},
        {"DimGray", "FF696969"},
        {"DimGrey", "FF696969"},
        {"Gainsboro", "FFDCDCDC"},
    };
    for (const auto& entry : kNamedColors) {
        if (str::EqualsIgnoreCase(raw, entry.name)) {
            return ParseHexColor(entry.hex8);
        }
    }
    const auto vals = detail::ParseFloatList(raw);
    if (vals.size() >= 4u) return Color{vals[0], vals[1], vals[2], vals[3]};
    if (vals.size() == 3u) return Color{vals[0], vals[1], vals[2], 1.0f};
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

std::uint32_t ParseColorRgba32(std::string_view raw) noexcept {
    const Color c = ParseColorLiteral(raw);
    return ClampFloatToByte(c.r) |
           (ClampFloatToByte(c.g) << 8u) |
           (ClampFloatToByte(c.b) << 16u) |
           (ClampFloatToByte(c.a) << 24u);
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

void UIResourceStore::RegisterFontAtlasHooks(
    FontAtlasFactoryFn factoryFn,
    FontAtlasClearFn clearFn) noexcept {
    gFontFactory.store(factoryFn, std::memory_order_release);
    gFontClear.store(clearFn, std::memory_order_release);
}

void UIResourceStore::RegisterImageAtlasHooks(
    ImageAtlasFactoryFn factoryFn,
    ImageAtlasClearFn clearFn) noexcept {
    gImageFactory.store(factoryFn, std::memory_order_release);
    gImageClear.store(clearFn, std::memory_order_release);
}

void UIResourceStore::RegisterImageAtlasSlotHook(
    ImageAtlasRegisterSlotFn registerSlotFn) noexcept {
    gImageRegisterSlot.store(registerSlotFn, std::memory_order_release);
}

void UIResourceStore::EnsureFontsInitialized() const {
    if (fonts_ != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (fonts_ != nullptr) {
        return;
    }
    if (const FontAtlasFactoryFn fn = gFontFactory.load(std::memory_order_acquire)) {
        fonts_ = fn(fontAtlasSize_);
    }
}

void UIResourceStore::EnsureImagesInitialized() const {
    if (images_ != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (images_ != nullptr) {
        return;
    }
    if (const ImageAtlasFactoryFn fn = gImageFactory.load(std::memory_order_acquire)) {
        images_ = fn();
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
    std::lock_guard<std::mutex> lock(mutex_);
    mtsdfEntries_.clear();
    pathMaskEntries_.clear();
}

bool UIResourceStore::StoreOrLookupPathMaskLocked(
    std::uint64_t key,
    std::uint32_t texW,
    std::uint32_t texH,
    float u0,
    float v0,
    float u1,
    float v1,
    std::vector<std::uint8_t>&& rgba8,
    RasterizedPathMaskEntry& outEntry) {
    const ImageAtlasRegisterSlotFn slotFn = gImageRegisterSlot.load(std::memory_order_acquire);
    if (!slotFn) {
        return false;
    }
    ImageAtlas& atlas = Images();

    std::uint32_t reuseSlotId = 0u;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
        if (pathMaskEntries_.size() >= 256u) {
            auto lru = pathMaskEntries_.begin();
            for (auto it = pathMaskEntries_.begin(); it != pathMaskEntries_.end(); ++it) {
                if (it->second.lastUsedTick < lru->second.lastUsedTick) {
                    lru = it;
                }
            }
            if (lru != pathMaskEntries_.end()) {
                reuseSlotId = lru->second.textureId;
                pathMaskEntries_.erase(lru);
            }
        }
    }

    const std::uint32_t texId = slotFn(atlas, reuseSlotId, texW, texH, std::move(rgba8));
    if (texId == 0u) {
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        entry.lastUsedTick = ++pathMaskAccessSeq_;
        pathMaskEntries_.emplace(key, entry);
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
    RasterizedPathMaskEntry& outEntry) {
    return StoreOrLookupPathMaskLocked(key, texW, texH, u0, v0, u1, v1, std::move(rgba8), outEntry);
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
    const std::uint32_t clampedW = std::clamp<std::uint32_t>(innerW, 1u, 2048u);
    const std::uint32_t clampedH = std::clamp<std::uint32_t>(innerH, 1u, 2048u);
    const std::uint32_t clampedPad = std::min<std::uint32_t>(padPx, 64u);

    std::uint64_t key = safeHash ^ 0x9E3779B97F4A7C15ULL;
    key ^= (static_cast<std::uint64_t>(clampedW) << 32u) | static_cast<std::uint64_t>(clampedH);
    key *= 1099511628211ULL;
    key ^= static_cast<std::uint64_t>(clampedPad);
    key *= 1099511628211ULL;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
    }

    std::vector<std::uint8_t> rgba8;
    if (!RasterizeSvgPathFillAlphaMask(pathData, rawBounds, clampedW, clampedH, clampedPad, rgba8)) {
        return false;
    }

    const std::uint32_t texW = clampedW + 2u * clampedPad;
    const std::uint32_t texH = clampedH + 2u * clampedPad;
    const float u0 = static_cast<float>(clampedPad) / static_cast<float>(texW);
    const float v0 = static_cast<float>(clampedPad) / static_cast<float>(texH);
    const float u1 = static_cast<float>(clampedPad + clampedW) / static_cast<float>(texW);
    const float v1 = static_cast<float>(clampedPad + clampedH) / static_cast<float>(texH);
    return StoreOrLookupPathMaskLocked(key, texW, texH, u0, v0, u1, v1, std::move(rgba8), outEntry);
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
    const std::uint32_t clampedW = std::clamp<std::uint32_t>(innerW, 1u, 2048u);
    const std::uint32_t clampedH = std::clamp<std::uint32_t>(innerH, 1u, 2048u);
    const std::uint32_t clampedPad = std::clamp<std::uint32_t>(padPx, 1u, 256u);
    const auto quantizedStroke = static_cast<std::uint64_t>(std::max<long>(1L, std::lround(strokeWidthPx * 16.0f)));
    const auto capBits = (static_cast<std::uint64_t>(startLineCap & 0xFu) << 4u) |
                         static_cast<std::uint64_t>(endLineCap & 0xFu);

    std::uint64_t key = safeHash ^ 0xD1B54A32D192ED03ULL;
    key ^= (static_cast<std::uint64_t>(clampedW) << 32u) | static_cast<std::uint64_t>(clampedH);
    key *= 1099511628211ULL;
    key ^= (quantizedStroke << 16u) | (capBits << 8u) | static_cast<std::uint64_t>(clampedPad);
    key *= 1099511628211ULL;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = pathMaskEntries_.find(key); it != pathMaskEntries_.end()) {
            it->second.lastUsedTick = ++pathMaskAccessSeq_;
            outEntry = it->second;
            return true;
        }
    }

    std::vector<std::uint8_t> rgba8;
    if (!RasterizeSvgPathStrokeAlphaMask(
            pathData, rawBounds, clampedW, clampedH, strokeWidthPx, clampedPad, rgba8, startLineCap, endLineCap)) {
        return false;
    }

    const std::uint32_t texW = clampedW + 2u * clampedPad;
    const std::uint32_t texH = clampedH + 2u * clampedPad;
    return StoreOrLookupPathMaskLocked(key, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, std::move(rgba8), outEntry);
}

void UIResourceStore::ClearAll() noexcept {
    if (fonts_ != nullptr) {
        if (const FontAtlasClearFn clearFn = gFontClear.load(std::memory_order_acquire)) {
            clearFn(*fonts_);
        }
    }
    if (images_ != nullptr) {
        if (const ImageAtlasClearFn clearFn = gImageClear.load(std::memory_order_acquire)) {
            clearFn(*images_);
        }
    }
    gradientBrushes_.Clear();
    std::lock_guard<std::mutex> lock(mutex_);
    assetSearchDirs_.clear();
    mtsdfEntries_.clear();
    pathMaskEntries_.clear();
    pathMaskAccessSeq_ = 0u;
}

} // namespace aero
