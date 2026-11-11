#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

// Compact binary WebFont glyph shard magic ("AGFS" = AeroGUI Font Shard v1)
inline constexpr std::uint32_t kAeroFontShardMagic = 0x53464741u;
inline constexpr std::uint16_t kAeroFontShardVersion = 1u;

struct TtfPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool onCurve = true;
};

struct TtfContourSegment {
    // Quadratic Bezier: B(t) = (1-t)^2 * p0 + 2(1-t)t * p1 + t^2 * p2
    // Straight line when isQuadratic == false (uses p0 -> p2)
    float p0x = 0.0f;
    float p0y = 0.0f;
    float p1x = 0.0f;
    float p1y = 0.0f;
    float p2x = 0.0f;
    float p2y = 0.0f;
    bool isQuadratic = false;
};

struct TtfGlyphOutline {
    std::uint32_t glyphIndex = 0;
    float xMin = 0.0f;
    float yMin = 0.0f;
    float xMax = 0.0f;
    float yMax = 0.0f;
    float advanceWidth = 0.0f;
    float leftSideBearing = 0.0f;
    bool isEmptyOutline = true;
    std::vector<TtfContourSegment> segments;
};

// Zero-dependency pure C++17 TrueType (.ttf) binary parser (32-bit float pipeline, 0 exceptions, 0 RTTI)
class TrueTypeFontFace {
public:
    TrueTypeFontFace() = default;

    bool LoadFromMemory(const std::uint8_t* data, std::size_t size, std::string* outError = nullptr);
    bool LoadFromFile(std::string_view filePath, std::string* outError = nullptr);

    [[nodiscard]] bool IsValid() const noexcept { return valid_; }
    [[nodiscard]] const std::string& FamilyName() const noexcept { return familyName_; }
    [[nodiscard]] const std::string& FullName() const noexcept { return fullName_; }

    [[nodiscard]] std::uint16_t UnitsPerEm() const noexcept { return unitsPerEm_; }
    [[nodiscard]] std::int16_t AscentFUnits() const noexcept { return ascent_; }
    [[nodiscard]] std::int16_t DescentFUnits() const noexcept { return descent_; }
    [[nodiscard]] std::int16_t LineGapFUnits() const noexcept { return lineGap_; }
    [[nodiscard]] std::uint16_t GlyphCount() const noexcept { return numGlyphs_; }

    [[nodiscard]] float AscentEm() const noexcept {
        return (unitsPerEm_ > 0u) ? (static_cast<float>(ascent_) / static_cast<float>(unitsPerEm_)) : 0.8f;
    }
    [[nodiscard]] float DescentEm() const noexcept {
        return (unitsPerEm_ > 0u)
            ? (static_cast<float>(descent_ < 0 ? -descent_ : descent_) / static_cast<float>(unitsPerEm_))
            : 0.2f;
    }
    [[nodiscard]] float LineGapEm() const noexcept {
        return (unitsPerEm_ > 0u && lineGap_ > 0)
            ? (static_cast<float>(lineGap_) / static_cast<float>(unitsPerEm_))
            : 0.0f;
    }

    // Maps Unicode codePoint to glyphIndex via cmap Format 12 or Format 4 (0 = .notdef if unmapped)
    [[nodiscard]] std::uint32_t FindGlyphIndex(std::uint32_t codePoint) const noexcept;

    // Reads exact hmtx horizontal metrics (in FUnits) for glyphIndex
    void GetGlyphHorizontalMetrics(
        std::uint32_t glyphIndex,
        float& outAdvanceWidthFUnits,
        float& outLeftSideBearingFUnits) const noexcept;

    // Reads optional kern pair adjustment (in FUnits) between two glyph indices
    [[nodiscard]] float GetKerningFUnits(std::uint32_t leftGlyphIndex, std::uint32_t rightGlyphIndex) const noexcept;

    // Extracts quadratic Bezier + line segments for simple or recursive compound glyphs
    bool ExtractGlyphOutline(std::uint32_t glyphIndex, TtfGlyphOutline& outOutline) const;

private:
    struct CmapFormat4Segment {
        std::uint16_t startCode = 0;
        std::uint16_t endCode = 0;
        std::int16_t idDelta = 0;
        std::uint16_t idRangeOffset = 0;
        std::uint32_t idRangeOffsetBytePos = 0;
    };

    struct CmapFormat12Group {
        std::uint32_t startCharCode = 0;
        std::uint32_t endCharCode = 0;
        std::uint32_t startGlyphId = 0;
    };

    struct KernPairEntry {
        std::uint32_t packedPair = 0; // (leftGlyph << 16) | rightGlyph
        std::int16_t valueFUnits = 0;
    };

    bool ParseTables(std::string* outError);
    bool ParseNameTable(std::uint32_t offset, std::uint32_t length);
    bool ParseCmapTable(std::uint32_t offset, std::uint32_t length);
    void ParseKernTable(std::uint32_t offset, std::uint32_t length);
    [[nodiscard]] bool GetGlyphDataSpan(
        std::uint32_t glyphIndex,
        std::uint32_t& outOffset,
        std::uint32_t& outLength) const noexcept;
    bool ExtractGlyphRecursive(
        std::uint32_t glyphIndex,
        std::uint32_t depth,
        std::uint32_t& ioTotalVisitedComponents,
        std::vector<TtfContourSegment>& outSegments,
        std::vector<TtfPoint>* outRawPoints) const;

    std::vector<std::uint8_t> data_;
    bool valid_ = false;
    std::string familyName_;
    std::string fullName_;

    std::uint16_t unitsPerEm_ = 1000u;
    std::int16_t indexToLocFormat_ = 0;
    std::int16_t ascent_ = 800;
    std::int16_t descent_ = -200;
    std::int16_t lineGap_ = 0;
    std::uint16_t numberOfHMetrics_ = 0u;
    std::uint16_t numGlyphs_ = 0u;

    std::uint32_t hmtxOffset_ = 0u;
    std::uint32_t hmtxLength_ = 0u;
    std::uint32_t locaOffset_ = 0u;
    std::uint32_t locaLength_ = 0u;
    std::uint32_t glyfOffset_ = 0u;
    std::uint32_t glyfLength_ = 0u;

    bool hasFormat12_ = false;
    bool hasFormat4_ = false;
    bool cmap12Sorted_ = true;
    bool cmap4Sorted_ = true;
    bool isSymbolCmap_ = false;
    std::vector<CmapFormat12Group> cmap12Groups_;
    std::vector<CmapFormat4Segment> cmap4Segments_;
    std::uint16_t cmap6FirstCode_ = 0u;
    std::vector<std::uint16_t> cmap6GlyphIds_;

    std::vector<KernPairEntry> kernPairs_;
};

struct FontAtlasGlyph {
    std::uint32_t codePoint = 0u;
    std::uint32_t glyphIndex = 0u;
    std::uint16_t atlasX = 0u;
    std::uint16_t atlasY = 0u;
    std::uint16_t cellWidth = 0u;
    std::uint16_t cellHeight = 0u;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    // Normalized to 1.0 em (multiply by fontSize to get screen pixels)
    float advanceEm = 0.6f;
    float bearingXEm = 0.0f;   // Horizontal offset from penX to left of padded SDF quad
    float bearingYEm = 0.8f;   // Vertical offset above baseline to top of padded SDF quad
    float quadWidthEm = 0.6f;  // Width of padded SDF quad in em units
    float quadHeightEm = 1.0f; // Height of padded SDF quad in em units
    float pxRange = 4.0f;
    bool isWhitespace = false;
    bool isFallback = false;
    bool valid = false;
};

struct PositionedGlyph {
    std::uint32_t codePoint = 0u;
    std::uint32_t glyphIndex = 0u;
    std::uint32_t lineIndex = 0u;
    float penX = 0.0f;
    float baselineY = 0.0f;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float advanceX = 0.0f;
    float pxRange = 4.0f;
    std::uint16_t cellWidth = 0u;
    std::uint16_t cellHeight = 0u;
    bool isWhitespace = false;
};

struct TextLineMetrics {
    std::uint32_t lineIndex = 0u;
    float width = 0.0f;
    float topY = 0.0f;
    float baselineY = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
    std::size_t firstGlyphIndex = 0u;
    std::size_t glyphCount = 0u;
};

struct TextLayoutResult {
    float totalWidth = 0.0f;
    float totalHeight = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
    bool usedTrueTypeMetrics = false;
    std::vector<TextLineMetrics> lines;
    std::vector<PositionedGlyph> glyphs;
};

// Dynamic Skyline SDF Font Atlas (top half v in [0.0..0.5) of 1024x1024 unified atlas)
// Includes built-in crisp geometric ASCII SDF fallback when no external .ttf is loaded.
class FontAtlas {
public:
    static constexpr std::uint16_t kDefaultAtlasSize = 1024u;
    static constexpr float kDefaultSdfPxRange = 4.0f;
    static constexpr float kDefaultBakeEmPixels = 36.0f;

    static FontAtlas& Instance() noexcept;

    explicit FontAtlas(std::uint16_t atlasSize = kDefaultAtlasSize);
    ~FontAtlas() = default;

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Load a TrueType (.ttf) font from memory or file path and register under familyAlias (plus internal TTF name)
    bool LoadTrueTypeFontFromMemory(
        const std::uint8_t* data,
        std::size_t size,
        std::string_view familyAlias = {},
        bool setAsDefault = false,
        std::string* outError = nullptr);

    bool LoadTrueTypeFontFromFile(
        std::string_view filePath,
        std::string_view familyAlias = {},
        bool setAsDefault = false,
        std::string* outError = nullptr);

    void SetDefaultFontFamily(std::string_view familyName);
    [[nodiscard]] std::string DefaultFontFamily() const;

    // Reset custom loaded TrueType fonts & shard glyphs while preserving built-in fallback ASCII SDF atlas
    void ClearCustomFonts();

    [[nodiscard]] bool HasFontFamily(std::string_view familyName) const noexcept;
    [[nodiscard]] const TrueTypeFontFace* FindFontFace(std::string_view familyName) const noexcept;

    // Resolve or dynamically bake SDF glyph into the Skyline atlas (thread-safe)
    [[nodiscard]] FontAtlasGlyph EnsureGlyph(
        std::string_view fontFamily,
        std::uint32_t codePoint);

    // Fast lookup of pre-baked built-in fallback ASCII glyph (0 allocations, 0 locks)
    [[nodiscard]] const FontAtlasGlyph& GetBuiltInAsciiGlyph(std::uint32_t codePoint) const noexcept {
        return builtInAscii_[codePoint < 128u ? codePoint : 0u];
    }

    // Export a Unicode codePoint range [rangeStart .. rangeEnd] from a loaded TrueType font into compact "AGFS" binary shard
    [[nodiscard]] bool ExportGlyphShard(
        std::string_view fontFamily,
        std::uint32_t rangeStart,
        std::uint32_t rangeEnd,
        std::vector<std::uint8_t>& outShardBytes,
        float bakeEmPixels = kDefaultBakeEmPixels) const;

    // Unpack a compact "AGFS" binary glyph shard directly into the dynamic Skyline SDF atlas
    bool UnpackGlyphShard(
        const std::uint8_t* shardBytes,
        std::size_t byteSize,
        std::uint32_t* outRangeStart = nullptr,
        std::uint32_t* outRangeEnd = nullptr,
        std::size_t* outUnpackedGlyphCount = nullptr);

    // Full UTF-8 layout engine with kerning, Ascent/Descent/LineGap, and NoWrap/Wrap/WrapWithOverflow
    [[nodiscard]] TextLayoutResult MeasureLayout(
        std::string_view utf8Text,
        std::string_view fontFamily,
        float fontSize,
        float maxWidth = 1000000.0f,
        TextWrapping wrapMode = TextWrapping::NoWrap);

    // Bilinear normalized [0..1] SDF sample at UV coordinates (u, v) in [0..1]^2
    [[nodiscard]] float SampleSdfBilinear(float u, float v) const noexcept;

    // Direct CPU reference SDF coverage matching GPU UberShader PrimType 2 (SdfTextGlyph)
    [[nodiscard]] float EvaluateGlyphSdfCoverage(
        float u,
        float v,
        float uSpan,
        float vSpan,
        float screenQuadW,
        float screenQuadH,
        float pxRange = kDefaultSdfPxRange) const noexcept;

    [[nodiscard]] std::uint16_t AtlasWidth() const noexcept { return atlasWidth_; }
    [[nodiscard]] std::uint16_t AtlasHeight() const noexcept { return atlasHeight_; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_.load(std::memory_order_acquire); }
    [[nodiscard]] const std::vector<std::uint8_t>& SdfR8Pixels() const noexcept { return sdfPixels_; }

    // Thread-safe snapshot expansion of the top SDF glyph band [0 .. min(maxRows, glyphRegionHeight_)] into RGBA8
    void CopyGlyphRegionRgba8(
        std::uint32_t maxRows,
        std::vector<std::uint8_t>& outRgba8,
        std::uint32_t& outWidth,
        std::uint32_t& outRows,
        std::uint64_t* outRevision = nullptr) const;

    [[nodiscard]] static std::string NormalizeFamilyKey(std::string_view rawFamily);

    // Analytic SDF baking of a TrueType outline into a single-channel R8 cell (128 = contour boundary)
    static void BakeOutlineToSdfCell(
        const TtfGlyphOutline& outline,
        std::uint16_t unitsPerEm,
        float bakeEmPixels,
        float pxRange,
        FontAtlasGlyph& outMeta,
        std::vector<std::uint8_t>& outCellPixels);

private:
    struct RegisteredFontEntry {
        std::uint32_t fontId = 0u;
        std::string canonicalKey;
        std::shared_ptr<TrueTypeFontFace> face;
        float shardAscentEm = 0.8f;
        float shardDescentEm = 0.2f;
        float shardLineGapEm = 0.0f;
        bool isShardOnly = false;
    };

    void InitBuiltInFallbackAscii();
    bool AllocateSkylineRectLocked(
        std::uint16_t cellW,
        std::uint16_t cellH,
        std::uint16_t& outX,
        std::uint16_t& outY) noexcept;
    void BlitCellR8Locked(
        std::uint16_t dstX,
        std::uint16_t dstY,
        std::uint16_t cellW,
        std::uint16_t cellH,
        const std::uint8_t* srcPixels) noexcept;
    [[nodiscard]] const RegisteredFontEntry* ResolveFontEntryLocked(std::string_view fontFamily) const noexcept;
    [[nodiscard]] FontAtlasGlyph EnsureGlyphLocked(
        const RegisteredFontEntry* fontEntry,
        std::uint32_t codePoint);

    mutable std::mutex mutex_;
    std::uint16_t atlasWidth_ = kDefaultAtlasSize;
    std::uint16_t atlasHeight_ = kDefaultAtlasSize;
    std::uint16_t glyphRegionHeight_ = kDefaultAtlasSize / 2u; // Top half v in [0.0..0.5)
    std::vector<std::uint16_t> skyline_;
    std::vector<std::uint8_t> sdfPixels_;
    std::atomic<std::uint64_t> revision_{1u};

    std::uint32_t nextFontId_ = 1u;
    std::string defaultFamilyKey_;
    std::vector<RegisteredFontEntry> fonts_;
    std::unordered_map<std::string, std::size_t> familyAliasToIndex_;
    // Key = (uint64_t(fontId) << 32) | uint64_t(codePoint); fontId == 0 denotes built-in/global shard fallback
    std::unordered_map<std::uint64_t, FontAtlasGlyph> glyphCache_;

    FontAtlasGlyph builtInAscii_[128]{};
    FontAtlasGlyph builtInCjkBoxGlyph_{};
};

// Convenience top-level layout measurement API required by AeroGUI 2.0 text pipeline
[[nodiscard]] TextLayoutResult MeasureTextLayout(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth = 1000000.0f,
    TextWrapping wrapMode = TextWrapping::NoWrap);

} // namespace aero
