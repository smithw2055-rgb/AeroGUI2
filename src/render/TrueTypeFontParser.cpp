#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace {

struct TtfReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;

    [[nodiscard]] bool InBounds(std::size_t offset, std::size_t byteCount) const noexcept {
        return offset <= size && byteCount <= (size - offset);
    }

    [[nodiscard]] bool ReadU8(std::size_t offset, std::uint8_t& out) const noexcept {
        if (!InBounds(offset, 1u)) {
            return false;
        }
        out = data[offset];
        return true;
    }

    [[nodiscard]] bool ReadI8(std::size_t offset, std::int8_t& out) const noexcept {
        std::uint8_t u = 0u;
        if (!ReadU8(offset, u)) {
            return false;
        }
        out = static_cast<std::int8_t>(u);
        return true;
    }

    [[nodiscard]] bool ReadU16BE(std::size_t offset, std::uint16_t& out) const noexcept {
        if (!InBounds(offset, 2u)) {
            return false;
        }
        out = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[offset]) << 8u) |
            static_cast<std::uint16_t>(data[offset + 1u]));
        return true;
    }

    [[nodiscard]] bool ReadI16BE(std::size_t offset, std::int16_t& out) const noexcept {
        std::uint16_t u = 0u;
        if (!ReadU16BE(offset, u)) {
            return false;
        }
        out = static_cast<std::int16_t>(u);
        return true;
    }

    [[nodiscard]] bool ReadU32BE(std::size_t offset, std::uint32_t& out) const noexcept {
        if (!InBounds(offset, 4u)) {
            return false;
        }
        out = (static_cast<std::uint32_t>(data[offset + 0u]) << 24u) |
              (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
              (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
              static_cast<std::uint32_t>(data[offset + 3u]);
        return true;
    }

    [[nodiscard]] bool ReadF2Dot14BE(std::size_t offset, float& out) const noexcept {
        std::int16_t raw = 0;
        if (!ReadI16BE(offset, raw)) {
            return false;
        }
        out = static_cast<float>(raw) * (1.0f / 16384.0f);
        return true;
    }
};

constexpr std::uint32_t MakeTag(char a, char b, char c, char d) noexcept {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) << 24u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 8u) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(d));
}

std::string DecodeUtf16BeToUtf8(const std::uint8_t* bytes, std::size_t byteLen) {
    std::string out;
    out.reserve(byteLen);
    std::size_t i = 0u;
    while (i + 1u < byteLen) {
        const std::uint16_t w0 = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[i]) << 8u) | bytes[i + 1u]);
        i += 2u;
        if (w0 >= 0xD800u && w0 <= 0xDBFFu && i + 1u < byteLen) {
            const std::uint16_t w1 = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[i]) << 8u) | bytes[i + 1u]);
            if (w1 >= 0xDC00u && w1 <= 0xDFFFu) {
                i += 2u;
                const std::uint32_t cp =
                    0x10000u + ((static_cast<std::uint32_t>(w0 - 0xD800u) << 10u) |
                                static_cast<std::uint32_t>(w1 - 0xDC00u));
                str::AppendUtf8CodePoint(out, cp);
                continue;
            }
        }
        if (w0 != 0u) {
            str::AppendUtf8CodePoint(out, w0);
        }
    }
    return out;
}

// Little-endian helpers for "AGFS" binary shard serialization
} // namespace

// ============================================================================
// TrueTypeFontFace Implementation
// ============================================================================

bool TrueTypeFontFace::ParseLoadedBuffer(std::string* outError) {
    valid_ = false;
    isCff_ = false;
    weightClass_ = 400u;
    isItalic_ = false;
    contentHash_ = 0u;
    familyName_.clear();
    fullName_.clear();
    cmap12Groups_.clear();
    cmap4Segments_.clear();
    cmap6GlyphIds_.clear();
    kernPairs_.clear();
    hasFormat12_ = false;
    hasFormat4_ = false;
    isSymbolCmap_ = false;

    const std::size_t size = data_.size();
    if (size < 12u) {
        data_.clear();
        if (outError) {
            *outError = "TTF buffer is null or smaller than 12-byte offset table";
        }
        return false;
    }

    std::uint64_t h = 14695981039346656037ULL;
    const std::size_t stride = (size > 65536u) ? (size / 16384u) : 1u;
    for (std::size_t i = 0u; i < size; i += stride) {
        h ^= static_cast<std::uint64_t>(data_[i]);
        h *= 1099511628211ULL;
    }
    h ^= static_cast<std::uint64_t>(size);
    h *= 1099511628211ULL;
    contentHash_ = (h == 0u) ? 1u : h;
    if (!ParseTables(outError)) {
        data_.clear();
        return false;
    }
    valid_ = true;
    return true;
}

bool TrueTypeFontFace::LoadFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    std::string* outError) {
    if (!data || size < 12u) {
        data_.clear();
        return ParseLoadedBuffer(outError);
    }
    data_.assign(data, data + size);
    return ParseLoadedBuffer(outError);
}

bool TrueTypeFontFace::LoadFromMemory(
    std::vector<std::uint8_t>&& ownedData,
    std::string* outError) {
    data_ = std::move(ownedData);
    return ParseLoadedBuffer(outError);
}

bool TrueTypeFontFace::LoadFromFile(std::string_view filePath, std::string* outError) {
    std::ifstream ifs(std::string(filePath), std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        if (outError) {
            *outError = "Failed to open font file: " + std::string(filePath);
        }
        return false;
    }
    const std::streampos endPos = ifs.tellg();
    if (endPos <= 0) {
        if (outError) {
            *outError = "Font file is empty: " + std::string(filePath);
        }
        return false;
    }
    const auto fileSize = static_cast<std::size_t>(endPos);
    std::vector<std::uint8_t> bytes(fileSize);
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fileSize))) {
        if (outError) {
            *outError = "Failed to read font file bytes: " + std::string(filePath);
        }
        return false;
    }

    if (!LoadFromMemory(std::move(bytes), outError)) {
        return false;
    }

    if (familyName_.empty()) {
        // Fallback family name from file stem
        std::string_view stem = filePath;
        if (const auto slash = stem.find_last_of("/\\"); slash != std::string_view::npos) {
            stem = stem.substr(slash + 1u);
        }
        if (const auto dot = stem.rfind('.'); dot != std::string_view::npos) {
            stem = stem.substr(0, dot);
        }
        familyName_.assign(stem);
    }
    return true;
}

bool TrueTypeFontFace::ParseTables(std::string* outError) {
    TtfReader r{data_.data(), data_.size()};
    std::uint32_t sfntVersion = 0u;
    std::uint16_t numTables = 0u;
    if (!r.ReadU32BE(0u, sfntVersion) || !r.ReadU16BE(4u, numTables)) {
        if (outError) *outError = "Corrupted sfnt header";
        return false;
    }

    // Standard TrueType / OpenType sfnt versions: 0x00010000, 'true', or 'OTTO' (CFF)
    if (sfntVersion != 0x00010000u &&
        sfntVersion != MakeTag('t', 'r', 'u', 'e') &&
        sfntVersion != MakeTag('O', 'T', 'T', 'O')) {
        if (outError) *outError = "Unsupported sfnt version (requires TrueType glyf or OpenType CFF outlines)";
        return false;
    }
    if (numTables == 0u || !r.InBounds(12u, static_cast<std::size_t>(numTables) * 16u)) {
        if (outError) *outError = "Invalid TrueType table directory count";
        return false;
    }

    struct TableSpan {
        std::uint32_t offset = 0u;
        std::uint32_t length = 0u;
        bool present = false;
    };
    TableSpan head{}, hhea{}, maxp{}, cmap{}, hmtx{}, loca{}, glyf{}, cff{}, name{}, kern{}, os2{};

    for (std::uint16_t i = 0u; i < numTables; ++i) {
        const std::size_t recOff = 12u + static_cast<std::size_t>(i) * 16u;
        std::uint32_t tag = 0u, off = 0u, len = 0u;
        if (!r.ReadU32BE(recOff, tag) ||
            !r.ReadU32BE(recOff + 8u, off) ||
            !r.ReadU32BE(recOff + 12u, len)) {
            return false;
        }
        if (!r.InBounds(off, len)) {
            if (outError) *outError = "TrueType table exceeds file bounds";
            return false;
        }
        const TableSpan span{off, len, true};
        if (tag == MakeTag('h', 'e', 'a', 'd')) head = span;
        else if (tag == MakeTag('h', 'h', 'e', 'a')) hhea = span;
        else if (tag == MakeTag('m', 'a', 'x', 'p')) maxp = span;
        else if (tag == MakeTag('c', 'm', 'a', 'p')) cmap = span;
        else if (tag == MakeTag('h', 'm', 't', 'x')) hmtx = span;
        else if (tag == MakeTag('l', 'o', 'c', 'a')) loca = span;
        else if (tag == MakeTag('g', 'l', 'y', 'f')) glyf = span;
        else if (tag == MakeTag('C', 'F', 'F', ' ')) cff = span;
        else if (tag == MakeTag('n', 'a', 'm', 'e')) name = span;
        else if (tag == MakeTag('k', 'e', 'r', 'n')) kern = span;
        else if (tag == MakeTag('O', 'S', '/', '2')) os2 = span;
        else if (tag == MakeTag('G', 'S', 'U', 'B')) ParseGsubTable(off, len);
    }

    const bool hasGlyf = loca.present && glyf.present;
    const bool hasCff = cff.present;
    if (!head.present || !hhea.present || !maxp.present ||
        !cmap.present || !hmtx.present || (!hasGlyf && !hasCff)) {
        if (outError) *outError = "Missing mandatory font table (head/hhea/maxp/cmap/hmtx/glyf|CFF )";
        return false;
    }

    isCff_ = hasCff && !hasGlyf;

    if (head.length < 54u ||
        !r.ReadU16BE(head.offset + 18u, unitsPerEm_) ||
        !r.ReadI16BE(head.offset + 50u, indexToLocFormat_) ||
        unitsPerEm_ < 16u ||
        (!isCff_ && indexToLocFormat_ != 0 && indexToLocFormat_ != 1)) {
        if (outError) *outError = "Invalid TrueType head table";
        return false;
    }

    std::uint16_t macStyle = 0u;
    (void)r.ReadU16BE(head.offset + 44u, macStyle);
    if (os2.present && os2.length >= 6u) {
        std::uint16_t usWeight = 400u;
        if (r.ReadU16BE(os2.offset + 4u, usWeight) && usWeight >= 100u && usWeight <= 1000u) {
            weightClass_ = usWeight;
        }
        if (os2.length >= 64u) {
            std::uint16_t fsSelection = 0u;
            if (r.ReadU16BE(os2.offset + 62u, fsSelection)) {
                isItalic_ = ((fsSelection & 0x0001u) != 0u) || ((fsSelection & 0x0200u) != 0u);
                if ((fsSelection & 0x0020u) != 0u && weightClass_ <= 400u) {
                    weightClass_ = 700u;
                }
            }
        } else {
            isItalic_ = (macStyle & 0x0002u) != 0u;
            if ((macStyle & 0x0001u) != 0u && weightClass_ <= 400u) {
                weightClass_ = 700u;
            }
        }
    } else {
        isItalic_ = (macStyle & 0x0002u) != 0u;
        if ((macStyle & 0x0001u) != 0u) {
            weightClass_ = 700u;
        }
    }

    if (hhea.length < 36u ||
        !r.ReadI16BE(hhea.offset + 4u, ascent_) ||
        !r.ReadI16BE(hhea.offset + 6u, descent_) ||
        !r.ReadI16BE(hhea.offset + 8u, lineGap_) ||
        !r.ReadU16BE(hhea.offset + 34u, numberOfHMetrics_) ||
        numberOfHMetrics_ == 0u) {
        if (outError) *outError = "Invalid TrueType hhea table";
        return false;
    }

    if (maxp.length < 6u || !r.ReadU16BE(maxp.offset + 4u, numGlyphs_) || numGlyphs_ == 0u) {
        if (outError) *outError = "Invalid TrueType maxp table";
        return false;
    }

    hmtxOffset_ = hmtx.offset;
    hmtxLength_ = hmtx.length;
    locaOffset_ = loca.offset;
    locaLength_ = loca.length;
    glyfOffset_ = glyf.offset;
    glyfLength_ = glyf.length;

    if (static_cast<std::uint64_t>(hmtxOffset_) + static_cast<std::uint64_t>(hmtxLength_) > data_.size()) {
        if (outError) *outError = "TrueType hmtx table exceeds file bounds";
        return false;
    }
    if (numberOfHMetrics_ > numGlyphs_) {
        numberOfHMetrics_ = numGlyphs_;
    }
    if (hmtxLength_ < static_cast<std::size_t>(numberOfHMetrics_) * 4u) {
        if (outError) *outError = "Truncated TrueType hmtx table";
        return false;
    }

    if (isCff_) {
        if (!ParseCffTable(cff.offset, cff.length, outError)) {
            return false;
        }
    } else {
        const std::size_t requiredLocaBytes = (indexToLocFormat_ == 0)
            ? (static_cast<std::size_t>(numGlyphs_) + 1u) * 2u
            : (static_cast<std::size_t>(numGlyphs_) + 1u) * 4u;
        if (locaLength_ < requiredLocaBytes) {
            if (outError) *outError = "Truncated TrueType loca table";
            return false;
        }
    }

    if (!ParseCmapTable(cmap.offset, cmap.length)) {
        if (outError) *outError = "No supported Unicode cmap subtable (Format 4/12/6) found";
        return false;
    }

    if (name.present) {
        (void)ParseNameTable(name.offset, name.length);
    }
    if (kern.present) {
        ParseKernTable(kern.offset, kern.length);
    }
    return true;
}

bool TrueTypeFontFace::ParseNameTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 6u) {
        return false;
    }
    std::uint16_t count = 0u, stringOffset = 0u;
    if (!r.ReadU16BE(offset + 2u, count) || !r.ReadU16BE(offset + 4u, stringOffset)) {
        return false;
    }
    const std::size_t strStorageBase = static_cast<std::size_t>(offset) + stringOffset;
    if (!r.InBounds(offset + 6u, static_cast<std::size_t>(count) * 12u)) {
        return false;
    }

    std::string prefFamily;
    std::string winFamily;
    std::string macFamily;
    std::string winFull;
    std::string macFull;

    for (std::uint16_t i = 0u; i < count; ++i) {
        const std::size_t rec = static_cast<std::size_t>(offset) + 6u + static_cast<std::size_t>(i) * 12u;
        std::uint16_t platformId = 0u, encodingId = 0u, languageId = 0u, nameId = 0u, strLen = 0u, strOff = 0u;
        if (!r.ReadU16BE(rec + 0u, platformId) ||
            !r.ReadU16BE(rec + 2u, encodingId) ||
            !r.ReadU16BE(rec + 4u, languageId) ||
            !r.ReadU16BE(rec + 6u, nameId) ||
            !r.ReadU16BE(rec + 8u, strLen) ||
            !r.ReadU16BE(rec + 10u, strOff)) {
            continue;
        }
        if (strLen == 0u || !r.InBounds(strStorageBase + strOff, strLen)) {
            continue;
        }
        const std::uint8_t* rawStr = data_.data() + strStorageBase + strOff;

        if ((platformId == 3u && (encodingId == 1u || encodingId == 10u || encodingId == 0u)) ||
            platformId == 0u) {
            const std::string decoded = DecodeUtf16BeToUtf8(rawStr, strLen);
            if (decoded.empty()) continue;
            if (nameId == 16u && (prefFamily.empty() || languageId == 0x0409u)) {
                prefFamily = decoded;
            } else if (nameId == 1u && (winFamily.empty() || languageId == 0x0409u)) {
                winFamily = decoded;
            } else if (nameId == 4u && (winFull.empty() || languageId == 0x0409u)) {
                winFull = decoded;
            }
        } else if (platformId == 1u && encodingId == 0u) {
            std::string ascii;
            ascii.reserve(strLen);
            for (std::uint16_t k = 0u; k < strLen; ++k) {
                if (rawStr[k] >= 0x20u && rawStr[k] <= 0x7Eu) {
                    ascii.push_back(static_cast<char>(rawStr[k]));
                }
            }
            if (nameId == 1u && macFamily.empty()) macFamily = std::move(ascii);
            else if (nameId == 4u && macFull.empty()) macFull = std::move(ascii);
        }
    }

    if (!prefFamily.empty()) familyName_ = std::move(prefFamily);
    else if (!winFamily.empty()) familyName_ = std::move(winFamily);
    else if (!macFamily.empty()) familyName_ = std::move(macFamily);

    if (!winFull.empty()) fullName_ = std::move(winFull);
    else if (!macFull.empty()) fullName_ = std::move(macFull);
    else fullName_ = familyName_;
    return !familyName_.empty();
}

bool TrueTypeFontFace::ParseCmapTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 4u) {
        return false;
    }
    std::uint16_t numSubtables = 0u;
    if (!r.ReadU16BE(offset + 2u, numSubtables) ||
        !r.InBounds(offset + 4u, static_cast<std::size_t>(numSubtables) * 8u)) {
        return false;
    }

    std::size_t bestFmt12Off = 0u;
    int bestFmt12Score = -1;
    std::size_t bestFmt4Off = 0u;
    int bestFmt4Score = -1;
    bool bestFmt4Symbol = false;
    std::size_t bestFmt6Off = 0u;
    std::size_t bestFmt0Off = 0u;

    for (std::uint16_t i = 0u; i < numSubtables; ++i) {
        const std::size_t rec = static_cast<std::size_t>(offset) + 4u + static_cast<std::size_t>(i) * 8u;
        std::uint16_t plat = 0u, enc = 0u;
        std::uint32_t relOff = 0u;
        if (!r.ReadU16BE(rec, plat) || !r.ReadU16BE(rec + 2u, enc) || !r.ReadU32BE(rec + 4u, relOff)) {
            continue;
        }
        const std::size_t subOff = static_cast<std::size_t>(offset) + relOff;
        std::uint16_t fmt = 0u;
        if (!r.ReadU16BE(subOff, fmt)) {
            continue;
        }
        if (fmt == 12u) {
            const int score = (plat == 3u && enc == 10u) ? 100 : (plat == 0u ? 90 : 50);
            if (score > bestFmt12Score) {
                bestFmt12Score = score;
                bestFmt12Off = subOff;
            }
        } else if (fmt == 4u) {
            int score = 10;
            if (plat == 3u && enc == 1u) score = 100;
            else if (plat == 0u) score = 90;
            else if (plat == 3u && enc == 0u) score = 60;
            if (score > bestFmt4Score) {
                bestFmt4Score = score;
                bestFmt4Off = subOff;
                bestFmt4Symbol = (plat == 3u && enc == 0u);
            }
        } else if (fmt == 6u && bestFmt6Off == 0u) {
            bestFmt6Off = subOff;
        } else if (fmt == 0u && bestFmt0Off == 0u) {
            bestFmt0Off = subOff;
        }
    }

    if (bestFmt12Off != 0u) {
        std::uint32_t subLen = 0u, numGroups = 0u;
        if (r.ReadU32BE(bestFmt12Off + 4u, subLen) &&
            r.ReadU32BE(bestFmt12Off + 12u, numGroups) &&
            r.InBounds(bestFmt12Off + 16u, static_cast<std::size_t>(numGroups) * 12u)) {
            cmap12Groups_.resize(numGroups);
            cmap12Sorted_ = true;
            for (std::uint32_t g = 0u; g < numGroups; ++g) {
                const std::size_t gOff = bestFmt12Off + 16u + static_cast<std::size_t>(g) * 12u;
                (void)r.ReadU32BE(gOff + 0u, cmap12Groups_[g].startCharCode);
                (void)r.ReadU32BE(gOff + 4u, cmap12Groups_[g].endCharCode);
                (void)r.ReadU32BE(gOff + 8u, cmap12Groups_[g].startGlyphId);
                if (cmap12Groups_[g].startCharCode > cmap12Groups_[g].endCharCode ||
                    (g > 0u && cmap12Groups_[g].endCharCode <= cmap12Groups_[g - 1u].endCharCode)) {
                    cmap12Sorted_ = false;
                }
            }
            hasFormat12_ = !cmap12Groups_.empty();
        }
    }

    if (bestFmt4Off != 0u) {
        std::uint16_t subLen = 0u, segCountX2 = 0u;
        if (r.ReadU16BE(bestFmt4Off + 2u, subLen) &&
            r.ReadU16BE(bestFmt4Off + 6u, segCountX2) &&
            segCountX2 >= 2u && (segCountX2 % 2u) == 0u) {
            const std::uint16_t segCount = static_cast<std::uint16_t>(segCountX2 / 2u);
            const std::size_t endCodeBase = bestFmt4Off + 14u;
            const std::size_t startCodeBase = endCodeBase + static_cast<std::size_t>(segCount) * 2u + 2u;
            const std::size_t idDeltaBase = startCodeBase + static_cast<std::size_t>(segCount) * 2u;
            const std::size_t idRangeBase = idDeltaBase + static_cast<std::size_t>(segCount) * 2u;
            if (r.InBounds(idRangeBase, static_cast<std::size_t>(segCount) * 2u)) {
                cmap4Segments_.resize(segCount);
                cmap4Sorted_ = true;
                for (std::uint16_t s = 0u; s < segCount; ++s) {
                    const std::size_t wOff = static_cast<std::size_t>(s) * 2u;
                    (void)r.ReadU16BE(endCodeBase + wOff, cmap4Segments_[s].endCode);
                    (void)r.ReadU16BE(startCodeBase + wOff, cmap4Segments_[s].startCode);
                    (void)r.ReadI16BE(idDeltaBase + wOff, cmap4Segments_[s].idDelta);
                    (void)r.ReadU16BE(idRangeBase + wOff, cmap4Segments_[s].idRangeOffset);
                    cmap4Segments_[s].idRangeOffsetBytePos = static_cast<std::uint32_t>(idRangeBase + wOff);
                    if (cmap4Segments_[s].startCode > cmap4Segments_[s].endCode ||
                        (s > 0u && cmap4Segments_[s].endCode <= cmap4Segments_[s - 1u].endCode)) {
                        cmap4Sorted_ = false;
                    }
                }
                hasFormat4_ = !cmap4Segments_.empty();
                isSymbolCmap_ = bestFmt4Symbol;
            }
        }
    }

    if (!hasFormat12_ && !hasFormat4_) {
        if (bestFmt6Off != 0u) {
            std::uint16_t firstCode = 0u, entryCount = 0u;
            if (r.ReadU16BE(bestFmt6Off + 6u, firstCode) &&
                r.ReadU16BE(bestFmt6Off + 8u, entryCount) &&
                entryCount > 0u &&
                r.InBounds(bestFmt6Off + 10u, static_cast<std::size_t>(entryCount) * 2u)) {
                cmap6FirstCode_ = firstCode;
                cmap6GlyphIds_.resize(entryCount);
                for (std::uint16_t i = 0u; i < entryCount; ++i) {
                    (void)r.ReadU16BE(bestFmt6Off + 10u + static_cast<std::size_t>(i) * 2u, cmap6GlyphIds_[i]);
                }
            }
        } else if (bestFmt0Off != 0u && r.InBounds(bestFmt0Off + 6u, 256u)) {
            cmap6FirstCode_ = 0u;
            cmap6GlyphIds_.resize(256u);
            for (std::size_t i = 0u; i < 256u; ++i) {
                cmap6GlyphIds_[i] = data_[bestFmt0Off + 6u + i];
            }
        }
    }

    return hasFormat12_ || hasFormat4_ || !cmap6GlyphIds_.empty();
}

void TrueTypeFontFace::ParseKernTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 4u) {
        return;
    }
    std::uint16_t version16 = 0u;
    if (!r.ReadU16BE(offset, version16)) {
        return;
    }

    if (version16 == 0u) {
        // Microsoft/TrueType kern header: uint16 version(0), uint16 nTables
        std::uint16_t nTables = 0u;
        if (!r.ReadU16BE(offset + 2u, nTables)) {
            return;
        }
        std::size_t cur = static_cast<std::size_t>(offset) + 4u;
        const std::size_t tableEnd = static_cast<std::size_t>(offset) + length;
        for (std::uint16_t t = 0u; t < nTables && cur + 6u <= tableEnd; ++t) {
            std::uint16_t subLen = 0u, coverage = 0u;
            if (!r.ReadU16BE(cur + 2u, subLen) || !r.ReadU16BE(cur + 4u, coverage)) {
                break;
            }
            const std::uint8_t format = static_cast<std::uint8_t>((coverage >> 8u) & 0xFFu);
            const bool horizontal = (coverage & 0x0001u) != 0u;
            const bool minimum = (coverage & 0x0002u) != 0u;
            const bool crossStream = (coverage & 0x0004u) != 0u;
            if (format == 0u && horizontal && !minimum && !crossStream && cur + 14u <= tableEnd) {
                std::uint16_t nPairs = 0u;
                if (r.ReadU16BE(cur + 6u, nPairs) &&
                    r.InBounds(cur + 14u, static_cast<std::size_t>(nPairs) * 6u)) {
                    kernPairs_.reserve(kernPairs_.size() + nPairs);
                    for (std::uint16_t p = 0u; p < nPairs; ++p) {
                        const std::size_t pOff = cur + 14u + static_cast<std::size_t>(p) * 6u;
                        std::uint16_t left = 0u, right = 0u;
                        std::int16_t val = 0;
                        (void)r.ReadU16BE(pOff + 0u, left);
                        (void)r.ReadU16BE(pOff + 2u, right);
                        (void)r.ReadI16BE(pOff + 4u, val);
                        if (val != 0) {
                            kernPairs_.push_back(KernPairEntry{
                                (static_cast<std::uint32_t>(left) << 16u) | right,
                                val});
                        }
                    }
                }
            }
            if (subLen < 6u) {
                break;
            }
            cur += subLen;
        }
    }

    if (!kernPairs_.empty()) {
        std::sort(kernPairs_.begin(), kernPairs_.end(), [](const KernPairEntry& a, const KernPairEntry& b) {
            return a.packedPair < b.packedPair;
        });
    }
}


void TrueTypeFontFace::ParseGsubTable(std::uint32_t offset, std::uint32_t length) {
    gsubFeatures_.clear();
    hasArabicGsub_ = false;
    if (length < 10u || offset > data_.size() || length > data_.size() - offset) return;
    const std::uint8_t* g = data_.data() + offset;
    auto ru16 = [&](std::uint32_t p) noexcept -> std::uint16_t {
        if (p + 2u > length) return 0u;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(g[p]) << 8u) | g[p + 1u]);
    };
    auto ri16 = [&](std::uint32_t p) noexcept -> std::int16_t {
        return static_cast<std::int16_t>(ru16(p));
    };
    auto ru32 = [&](std::uint32_t p) noexcept -> std::uint32_t {
        if (p + 4u > length) return 0u;
        return (static_cast<std::uint32_t>(g[p]) << 24u) |
               (static_cast<std::uint32_t>(g[p + 1u]) << 16u) |
               (static_cast<std::uint32_t>(g[p + 2u]) << 8u) |
               static_cast<std::uint32_t>(g[p + 3u]);
    };
    auto readCoverage = [&](std::uint32_t covOff, std::vector<std::uint16_t>& outGlyphs) {
        outGlyphs.clear();
        if (covOff + 4u > length) return;
        const std::uint16_t fmt = ru16(covOff);
        if (fmt == 1u) {
            const std::uint16_t cnt = ru16(covOff + 2u);
            if (covOff + 4u + 2u * cnt > length) return;
            outGlyphs.reserve(cnt);
            for (std::uint16_t i = 0u; i < cnt; ++i) outGlyphs.push_back(ru16(covOff + 4u + 2u * i));
        } else if (fmt == 2u) {
            const std::uint16_t rcnt = ru16(covOff + 2u);
            if (covOff + 4u + 6u * rcnt > length) return;
            for (std::uint16_t i = 0u; i < rcnt; ++i) {
                const std::uint16_t s = ru16(covOff + 4u + 6u * i);
                const std::uint16_t e = ru16(covOff + 6u + 6u * i);
                for (std::uint32_t gid = s; gid <= e && outGlyphs.size() < 8192u; ++gid) {
                    outGlyphs.push_back(static_cast<std::uint16_t>(gid));
                }
            }
        }
    };

    const std::uint16_t featListOff = ru16(6u);
    const std::uint16_t lookupListOff = ru16(8u);
    if (featListOff < 10u || lookupListOff < 10u || featListOff + 2u > length || lookupListOff + 2u > length) return;

    const std::uint16_t lookupCount = ru16(lookupListOff);
    const std::uint16_t featCount = ru16(featListOff);
    if (featListOff + 2u + 6u * featCount > length) return;

    auto findOrAddFeat = [&](std::uint32_t tag) -> GsubFeatureRules& {
        for (auto& fr : gsubFeatures_) {
            if (fr.tag == tag) return fr;
        }
        gsubFeatures_.push_back(GsubFeatureRules{tag, {}, {}});
        return gsubFeatures_.back();
    };

    std::vector<std::uint16_t> cov;
    for (std::uint16_t fi = 0u; fi < featCount; ++fi) {
        const std::uint32_t ftag = ru32(featListOff + 2u + 6u * fi);
        const bool wanted =
            ftag == 0x69736F6Cu || // 'isol'
            ftag == 0x696E6974u || // 'init'
            ftag == 0x6D656469u || // 'medi'
            ftag == 0x66696E61u || // 'fina'
            ftag == 0x6C696761u || // 'liga'
            ftag == 0x726C6967u;   // 'rlig'
        if (!wanted) continue;
        const std::uint32_t fOff = featListOff + ru16(featListOff + 6u + 6u * fi);
        if (fOff + 4u > length) continue;
        const std::uint16_t lIdxCount = ru16(fOff + 2u);
        if (fOff + 4u + 2u * lIdxCount > length) continue;
        GsubFeatureRules& rules = findOrAddFeat(ftag);

        for (std::uint16_t li = 0u; li < lIdxCount; ++li) {
            const std::uint16_t lookupIdx = ru16(fOff + 4u + 2u * li);
            if (lookupIdx >= lookupCount || lookupListOff + 4u + 2u * lookupIdx > length) continue;
            const std::uint32_t lOff = lookupListOff + ru16(lookupListOff + 2u + 2u * lookupIdx);
            if (lOff + 6u > length) continue;
            const std::uint16_t lType = ru16(lOff);
            const std::uint16_t subCount = ru16(lOff + 4u);
            if (lOff + 6u + 2u * subCount > length) continue;
            for (std::uint16_t si = 0u; si < subCount; ++si) {
                std::uint16_t effType = lType;
                std::uint32_t sOff = lOff + ru16(lOff + 6u + 2u * si);
                if (effType == 7u) { // Extension Substitution
                    if (sOff + 8u > length || ru16(sOff) != 1u) continue;
                    effType = ru16(sOff + 2u);
                    const std::uint32_t extDelta = ru32(sOff + 4u);
                    if (extDelta > length - sOff) continue;
                    sOff += extDelta;
                }
                if (sOff + 6u > length) continue;
                const std::uint16_t substFmt = ru16(sOff);
                if (effType == 1u) { // SingleSubst
                    readCoverage(sOff + ru16(sOff + 2u), cov);
                    if (substFmt == 1u) {
                        const std::int16_t delta = ri16(sOff + 4u);
                        for (std::uint16_t fg : cov) {
                            rules.singles.push_back({fg, static_cast<std::uint16_t>((fg + delta) & 0xFFFF)});
                        }
                    } else if (substFmt == 2u) {
                        const std::uint16_t gCnt = ru16(sOff + 4u);
                        if (sOff + 6u + 2u * gCnt <= length) {
                            const std::size_t n = std::min<std::size_t>(cov.size(), gCnt);
                            for (std::size_t k = 0u; k < n; ++k) {
                                rules.singles.push_back({cov[k], ru16(sOff + 6u + 2u * static_cast<std::uint32_t>(k))});
                            }
                        }
                    }
                } else if (effType == 4u && substFmt == 1u) { // LigatureSubst
                    readCoverage(sOff + ru16(sOff + 2u), cov);
                    const std::uint16_t setCnt = ru16(sOff + 4u);
                    if (sOff + 6u + 2u * setCnt <= length) {
                        const std::size_t n = std::min<std::size_t>(cov.size(), setCnt);
                        for (std::size_t k = 0u; k < n; ++k) {
                            const std::uint32_t lsOff = sOff + ru16(sOff + 6u + 2u * static_cast<std::uint32_t>(k));
                            if (lsOff + 2u > length) continue;
                            const std::uint16_t ligCnt = ru16(lsOff);
                            if (lsOff + 2u + 2u * ligCnt > length) continue;
                            for (std::uint16_t m = 0u; m < ligCnt; ++m) {
                                const std::uint32_t lgOff = lsOff + ru16(lsOff + 2u + 2u * m);
                                if (lgOff + 4u > length) continue;
                                const std::uint16_t ligGlyph = ru16(lgOff);
                                const std::uint16_t compCnt = ru16(lgOff + 2u);
                                if (compCnt == 1u) {
                                    rules.singles.push_back({cov[k], ligGlyph});
                                } else if (compCnt == 2u && lgOff + 6u <= length) {
                                    rules.ligs.push_back({cov[k], ru16(lgOff + 4u), ligGlyph});
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!rules.singles.empty() || !rules.ligs.empty()) {
            hasArabicGsub_ = true;
        }
    }
}

std::uint32_t TrueTypeFontFace::ApplyArabicGsubFeature(std::uint32_t tag4, std::uint32_t glyphId) const noexcept {
    for (const auto& fr : gsubFeatures_) {
        if (fr.tag != tag4) continue;
        for (const auto& r : fr.singles) {
            if (r.fromGlyph == glyphId) return r.toGlyph;
        }
    }
    return glyphId;
}

std::uint32_t TrueTypeFontFace::ApplyArabicGsubLigature(std::uint32_t firstGlyphId, std::uint32_t secondGlyphId) const noexcept {
    for (const auto& fr : gsubFeatures_) {
        if (fr.tag != 0x6C696761u && fr.tag != 0x726C6967u) continue;
        for (const auto& r : fr.ligs) {
            if (r.firstGlyph == firstGlyphId && r.secondGlyph == secondGlyphId) return r.ligGlyph;
        }
    }
    return 0u;
}

std::uint32_t TrueTypeFontFace::FindGlyphIndex(std::uint32_t codePoint) const noexcept {
    if (!valid_) {
        return 0u;
    }

    if (hasFormat12_) {
        if (cmap12Sorted_) {
            auto it = std::lower_bound(
                cmap12Groups_.begin(),
                cmap12Groups_.end(),
                codePoint,
                [](const CmapFormat12Group& g, std::uint32_t cp) {
                    return g.endCharCode < cp;
                });
            if (it != cmap12Groups_.end() && codePoint >= it->startCharCode && codePoint <= it->endCharCode) {
                const std::uint32_t gid = it->startGlyphId + (codePoint - it->startCharCode);
                return (gid < numGlyphs_) ? gid : 0u;
            }
        } else {
            for (const auto& grp : cmap12Groups_) {
                if (codePoint >= grp.startCharCode && codePoint <= grp.endCharCode) {
                    const std::uint32_t gid = grp.startGlyphId + (codePoint - grp.startCharCode);
                    return (gid < numGlyphs_) ? gid : 0u;
                }
            }
        }
    }

    auto lookupFmt4 = [&](std::uint32_t cp) noexcept -> std::uint32_t {
        if (!hasFormat4_ || cp > 0xFFFFu) {
            return 0u;
        }
        const auto cp16 = static_cast<std::uint16_t>(cp);
        const CmapFormat4Segment* seg = nullptr;
        if (cmap4Sorted_) {
            auto it = std::lower_bound(
                cmap4Segments_.begin(),
                cmap4Segments_.end(),
                cp16,
                [](const CmapFormat4Segment& s, std::uint16_t target) {
                    return s.endCode < target;
                });
            if (it != cmap4Segments_.end() && cp16 >= it->startCode) {
                seg = &(*it);
            }
        } else {
            for (const auto& s : cmap4Segments_) {
                if (cp16 >= s.startCode && cp16 <= s.endCode) {
                    seg = &s;
                    break;
                }
            }
        }
        if (!seg) {
            return 0u;
        }
        if (seg->idRangeOffset == 0u) {
            const std::uint32_t gid = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(cp16) + static_cast<std::int32_t>(seg->idDelta));
            return (gid < numGlyphs_) ? gid : 0u;
        }
        const std::size_t wordAddr = static_cast<std::size_t>(seg->idRangeOffsetBytePos) +
            static_cast<std::size_t>(seg->idRangeOffset) +
            static_cast<std::size_t>(cp16 - seg->startCode) * 2u;
        TtfReader r{data_.data(), data_.size()};
        std::uint16_t rawGid = 0u;
        if (!r.ReadU16BE(wordAddr, rawGid) || rawGid == 0u) {
            return 0u;
        }
        const std::uint32_t gid = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(rawGid) + static_cast<std::int32_t>(seg->idDelta));
        return (gid < numGlyphs_) ? gid : 0u;
    };

    if (hasFormat4_) {
        const std::uint32_t direct = lookupFmt4(codePoint);
        if (direct != 0u) {
            return direct;
        }
        if (isSymbolCmap_ && codePoint >= 0x20u && codePoint <= 0xFFu) {
            return lookupFmt4(0xF000u + codePoint);
        }
    }

    if (!cmap6GlyphIds_.empty() && codePoint >= cmap6FirstCode_) {
        const std::uint32_t rel = codePoint - cmap6FirstCode_;
        if (rel < cmap6GlyphIds_.size()) {
            const std::uint32_t gid = cmap6GlyphIds_[rel];
            return (gid < numGlyphs_) ? gid : 0u;
        }
    }
    return 0u;
}

void TrueTypeFontFace::GetGlyphHorizontalMetrics(
    std::uint32_t glyphIndex,
    float& outAdvanceWidthFUnits,
    float& outLeftSideBearingFUnits) const noexcept {
    outAdvanceWidthFUnits = static_cast<float>(unitsPerEm_) * 0.6f;
    outLeftSideBearingFUnits = 0.0f;
    if (!valid_ || numberOfHMetrics_ == 0u) {
        return;
    }

    const std::size_t hmtxEnd = std::min<std::size_t>(
        data_.size(),
        static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(hmtxLength_));
    TtfReader r{data_.data(), hmtxEnd};
    const std::uint32_t clampedGlyph = (glyphIndex < numGlyphs_) ? glyphIndex : 0u;
    if (clampedGlyph < numberOfHMetrics_) {
        const std::size_t off = static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(clampedGlyph) * 4u;
        std::uint16_t adv = 0u;
        std::int16_t lsb = 0;
        if (r.ReadU16BE(off, adv) && r.ReadI16BE(off + 2u, lsb)) {
            outAdvanceWidthFUnits = static_cast<float>(adv);
            outLeftSideBearingFUnits = static_cast<float>(lsb);
        }
    } else {
        const std::size_t lastAdvOff =
            static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(numberOfHMetrics_ - 1u) * 4u;
        const std::size_t lsbOff =
            static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(numberOfHMetrics_) * 4u +
            static_cast<std::size_t>(clampedGlyph - numberOfHMetrics_) * 2u;
        std::uint16_t adv = 0u;
        std::int16_t lsb = 0;
        if (r.ReadU16BE(lastAdvOff, adv)) {
            outAdvanceWidthFUnits = static_cast<float>(adv);
        }
        if (r.ReadI16BE(lsbOff, lsb)) {
            outLeftSideBearingFUnits = static_cast<float>(lsb);
        }
    }
}

float TrueTypeFontFace::GetKerningFUnits(
    std::uint32_t leftGlyphIndex,
    std::uint32_t rightGlyphIndex) const noexcept {
    if (kernPairs_.empty() || leftGlyphIndex > 0xFFFFu || rightGlyphIndex > 0xFFFFu) {
        return 0.0f;
    }
    const std::uint32_t key = (leftGlyphIndex << 16u) | rightGlyphIndex;
    auto it = std::lower_bound(
        kernPairs_.begin(),
        kernPairs_.end(),
        key,
        [](const KernPairEntry& e, std::uint32_t k) {
            return e.packedPair < k;
        });
    if (it != kernPairs_.end() && it->packedPair == key) {
        return static_cast<float>(it->valueFUnits);
    }
    return 0.0f;
}

bool TrueTypeFontFace::GetGlyphDataSpan(
    std::uint32_t glyphIndex,
    std::uint32_t& outOffset,
    std::uint32_t& outLength) const noexcept {
    outOffset = 0u;
    outLength = 0u;
    if (!valid_ || glyphIndex >= numGlyphs_) {
        return false;
    }
    TtfReader r{data_.data(), data_.size()};
    std::uint32_t g0 = 0u, g1 = 0u;
    if (indexToLocFormat_ == 0) {
        std::uint16_t s0 = 0u, s1 = 0u;
        const std::size_t base = static_cast<std::size_t>(locaOffset_) + static_cast<std::size_t>(glyphIndex) * 2u;
        if (!r.ReadU16BE(base, s0) || !r.ReadU16BE(base + 2u, s1)) {
            return false;
        }
        g0 = static_cast<std::uint32_t>(s0) * 2u;
        g1 = static_cast<std::uint32_t>(s1) * 2u;
    } else {
        const std::size_t base = static_cast<std::size_t>(locaOffset_) + static_cast<std::size_t>(glyphIndex) * 4u;
        if (!r.ReadU32BE(base, g0) || !r.ReadU32BE(base + 4u, g1)) {
            return false;
        }
    }
    if (g1 < g0 || g1 > glyfLength_) {
        return false;
    }
    outOffset = glyfOffset_ + g0;
    outLength = g1 - g0;
    return true;
}

bool TrueTypeFontFace::ExtractGlyphRecursive(
    std::uint32_t glyphIndex,
    std::uint32_t depth,
    std::uint32_t& ioTotalVisitedComponents,
    std::vector<TtfContourSegment>& outSegments,
    std::vector<TtfPoint>* outRawPoints) const {
    if (depth > 8u || ++ioTotalVisitedComponents > 128u || glyphIndex >= numGlyphs_) {
        return false;
    }

    std::uint32_t gOff = 0u, gLen = 0u;
    if (!GetGlyphDataSpan(glyphIndex, gOff, gLen)) {
        return false;
    }
    if (gLen == 0u) {
        // Valid whitespace glyph (e.g. U+0020 space)
        return true;
    }
    if (gLen < 10u) {
        return false;
    }

    const std::size_t glyphEnd = std::min<std::size_t>(
        data_.size(),
        static_cast<std::size_t>(gOff) + static_cast<std::size_t>(gLen));
    TtfReader r{data_.data(), glyphEnd};
    std::int16_t numContours = 0;
    if (!r.ReadI16BE(gOff, numContours)) {
        return false;
    }

    if (numContours == 0) {
        return true;
    }

    if (numContours > 0) {
        // Simple TrueType Glyph
        const auto contourCount = static_cast<std::size_t>(numContours);
        std::size_t pos = static_cast<std::size_t>(gOff) + 10u;
        if (!r.InBounds(pos, contourCount * 2u + 2u)) {
            return false;
        }

        std::vector<std::uint16_t> endPts(contourCount);
        for (std::size_t c = 0u; c < contourCount; ++c) {
            (void)r.ReadU16BE(pos, endPts[c]);
            pos += 2u;
            if (c > 0u && endPts[c] < endPts[c - 1u]) {
                return false;
            }
        }

        const std::size_t numPoints = static_cast<std::size_t>(endPts.back()) + 1u;
        std::uint16_t instructionLength = 0u;
        if (!r.ReadU16BE(pos, instructionLength)) {
            return false;
        }
        pos += 2u + instructionLength;
        if (pos > static_cast<std::size_t>(gOff) + gLen) {
            return false;
        }

        std::vector<std::uint8_t> flags(numPoints, 0u);
        for (std::size_t i = 0u; i < numPoints;) {
            std::uint8_t fl = 0u;
            if (!r.ReadU8(pos++, fl)) {
                return false;
            }
            flags[i++] = fl;
            if ((fl & 0x08u) != 0u) {
                std::uint8_t repeatCount = 0u;
                if (!r.ReadU8(pos++, repeatCount)) {
                    return false;
                }
                for (std::uint8_t rep = 0u; rep < repeatCount && i < numPoints; ++rep) {
                    flags[i++] = fl;
                }
            }
        }

        std::vector<TtfPoint> pts(numPoints);
        std::int32_t curX = 0;
        for (std::size_t i = 0u; i < numPoints; ++i) {
            const std::uint8_t fl = flags[i];
            pts[i].onCurve = (fl & 0x01u) != 0u;
            if ((fl & 0x02u) != 0u) {
                std::uint8_t dx = 0u;
                if (!r.ReadU8(pos++, dx)) return false;
                curX += ((fl & 0x10u) != 0u) ? static_cast<std::int32_t>(dx) : -static_cast<std::int32_t>(dx);
            } else if ((fl & 0x10u) == 0u) {
                std::int16_t dx = 0;
                if (!r.ReadI16BE(pos, dx)) return false;
                pos += 2u;
                curX += dx;
            }
            pts[i].x = static_cast<float>(curX);
        }

        std::int32_t curY = 0;
        for (std::size_t i = 0u; i < numPoints; ++i) {
            const std::uint8_t fl = flags[i];
            if ((fl & 0x04u) != 0u) {
                std::uint8_t dy = 0u;
                if (!r.ReadU8(pos++, dy)) return false;
                curY += ((fl & 0x20u) != 0u) ? static_cast<std::int32_t>(dy) : -static_cast<std::int32_t>(dy);
            } else if ((fl & 0x20u) == 0u) {
                std::int16_t dy = 0;
                if (!r.ReadI16BE(pos, dy)) return false;
                pos += 2u;
                curY += dy;
            }
            pts[i].y = static_cast<float>(curY);
        }

        if (outRawPoints) {
            outRawPoints->insert(outRawPoints->end(), pts.begin(), pts.end());
        }

        // Reconstruct closed TrueType quadratic Bezier + line segments per contour
        std::size_t startIdx = 0u;
        for (std::size_t c = 0u; c < contourCount; ++c) {
            const std::size_t endIdx = endPts[c];
            const std::size_t n = (endIdx >= startIdx) ? (endIdx - startIdx + 1u) : 0u;
            if (n >= 2u) {
                const TtfPoint* ring = pts.data() + startIdx;
                TtfPoint startOn{};
                std::size_t firstStep = 0u;
                std::size_t stepCount = n;

                if (ring[0].onCurve) {
                    startOn = ring[0];
                    firstStep = 1u;
                    stepCount = n; // Visits indices 1 .. n (where n % n == 0 closes back to startOn)
                } else if (ring[n - 1u].onCurve) {
                    startOn = ring[n - 1u];
                    firstStep = 0u;
                    stepCount = n - 1u; // Visits indices 0 .. n-1
                } else {
                    startOn = TtfPoint{
                        0.5f * (ring[n - 1u].x + ring[0].x),
                        0.5f * (ring[n - 1u].y + ring[0].y),
                        true};
                    firstStep = 0u;
                    stepCount = n; // Visits indices 0 .. n-1 and closes to implied startOn
                }

                TtfPoint curOn = startOn;
                std::size_t step = 0u;
                while (step < stepCount) {
                    const std::size_t idx = (firstStep + step) % n;
                    const TtfPoint pt = ring[idx];
                    if (pt.onCurve) {
                        if (std::fabs(pt.x - curOn.x) > 1e-4f || std::fabs(pt.y - curOn.y) > 1e-4f) {
                            outSegments.push_back(TtfContourSegment{
                                curOn.x, curOn.y,
                                0.5f * (curOn.x + pt.x), 0.5f * (curOn.y + pt.y),
                                pt.x, pt.y,
                                false});
                        }
                        curOn = pt;
                        ++step;
                    } else {
                        if (step + 1u < stepCount) {
                            const TtfPoint nextPt = ring[(firstStep + step + 1u) % n];
                            if (nextPt.onCurve) {
                                outSegments.push_back(TtfContourSegment{
                                    curOn.x, curOn.y, pt.x, pt.y, nextPt.x, nextPt.y, true});
                                curOn = nextPt;
                                step += 2u;
                            } else {
                                const TtfPoint mid{
                                    0.5f * (pt.x + nextPt.x),
                                    0.5f * (pt.y + nextPt.y),
                                    true};
                                outSegments.push_back(TtfContourSegment{
                                    curOn.x, curOn.y, pt.x, pt.y, mid.x, mid.y, true});
                                curOn = mid;
                                ++step;
                            }
                        } else {
                            outSegments.push_back(TtfContourSegment{
                                curOn.x, curOn.y, pt.x, pt.y, startOn.x, startOn.y, true});
                            curOn = startOn;
                            ++step;
                        }
                    }
                }
                if (std::fabs(curOn.x - startOn.x) > 1e-4f || std::fabs(curOn.y - startOn.y) > 1e-4f) {
                    outSegments.push_back(TtfContourSegment{
                        curOn.x, curOn.y,
                        0.5f * (curOn.x + startOn.x), 0.5f * (curOn.y + startOn.y),
                        startOn.x, startOn.y,
                        false});
                }
            }
            startIdx = endIdx + 1u;
        }
        return true;
    }

    // Compound TrueType Glyph (numContours < 0)
    constexpr std::uint16_t kArg1And2AreWords = 0x0001u;
    constexpr std::uint16_t kArgsAreXyValues = 0x0002u;
    constexpr std::uint16_t kWeHaveAScale = 0x0008u;
    constexpr std::uint16_t kMoreComponents = 0x0020u;
    constexpr std::uint16_t kWeHaveAnXyScale = 0x0040u;
    constexpr std::uint16_t kWeHaveATwoByTwo = 0x0080u;
    constexpr std::uint16_t kScaledComponentOffset = 0x0800u;

    std::size_t pos = static_cast<std::size_t>(gOff) + 10u;
    std::vector<TtfPoint> compositePoints;
    std::uint16_t compFlags = kMoreComponents;

    while ((compFlags & kMoreComponents) != 0u && pos + 4u <= glyphEnd) {
        std::uint16_t subGlyphIdx = 0u;
        if (!r.ReadU16BE(pos, compFlags) || !r.ReadU16BE(pos + 2u, subGlyphIdx)) {
            return false;
        }
        pos += 4u;

        std::int32_t arg1 = 0;
        std::int32_t arg2 = 0;
        if ((compFlags & kArg1And2AreWords) != 0u) {
            if ((compFlags & kArgsAreXyValues) != 0u) {
                std::int16_t a1 = 0, a2 = 0;
                if (!r.ReadI16BE(pos, a1) || !r.ReadI16BE(pos + 2u, a2)) return false;
                arg1 = a1;
                arg2 = a2;
            } else {
                std::uint16_t u1 = 0u, u2 = 0u;
                if (!r.ReadU16BE(pos, u1) || !r.ReadU16BE(pos + 2u, u2)) return false;
                arg1 = static_cast<std::int32_t>(u1);
                arg2 = static_cast<std::int32_t>(u2);
            }
            pos += 4u;
        } else {
            if ((compFlags & kArgsAreXyValues) != 0u) {
                std::int8_t a1 = 0, a2 = 0;
                if (!r.ReadI8(pos, a1) || !r.ReadI8(pos + 1u, a2)) return false;
                arg1 = a1;
                arg2 = a2;
            } else {
                std::uint8_t u1 = 0u, u2 = 0u;
                if (!r.ReadU8(pos, u1) || !r.ReadU8(pos + 1u, u2)) return false;
                arg1 = static_cast<std::int32_t>(u1);
                arg2 = static_cast<std::int32_t>(u2);
            }
            pos += 2u;
        }

        float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
        if ((compFlags & kWeHaveAScale) != 0u) {
            float s = 1.0f;
            if (!r.ReadF2Dot14BE(pos, s)) return false;
            pos += 2u;
            m00 = s;
            m11 = s;
        } else if ((compFlags & kWeHaveAnXyScale) != 0u) {
            if (!r.ReadF2Dot14BE(pos, m00) || !r.ReadF2Dot14BE(pos + 2u, m11)) return false;
            pos += 4u;
        } else if ((compFlags & kWeHaveATwoByTwo) != 0u) {
            // TrueType 2x2 order: xscale, scale01, scale10, yscale -> x' = m00*x + m01*y, y' = m10*x + m11*y
            if (!r.ReadF2Dot14BE(pos + 0u, m00) ||
                !r.ReadF2Dot14BE(pos + 2u, m10) ||
                !r.ReadF2Dot14BE(pos + 4u, m01) ||
                !r.ReadF2Dot14BE(pos + 6u, m11)) {
                return false;
            }
            pos += 8u;
        }

        std::vector<TtfContourSegment> childSegs;
        std::vector<TtfPoint> childPts;
        if (!ExtractGlyphRecursive(subGlyphIdx, depth + 1u, ioTotalVisitedComponents, childSegs, &childPts)) {
            return false;
        }

        float tx = 0.0f;
        float ty = 0.0f;
        if ((compFlags & kArgsAreXyValues) != 0u) {
            tx = static_cast<float>(arg1);
            ty = static_cast<float>(arg2);
            if ((compFlags & kScaledComponentOffset) != 0u) {
                const float sx = std::sqrt(m00 * m00 + m10 * m10);
                const float sy = std::sqrt(m01 * m01 + m11 * m11);
                tx *= sx;
                ty *= sy;
            }
        } else {
            const auto pIdxParent = static_cast<std::size_t>(arg1);
            const auto pIdxChild = static_cast<std::size_t>(arg2);
            if (pIdxParent < compositePoints.size() && pIdxChild < childPts.size()) {
                const float cx = m00 * childPts[pIdxChild].x + m01 * childPts[pIdxChild].y;
                const float cy = m10 * childPts[pIdxChild].x + m11 * childPts[pIdxChild].y;
                tx = compositePoints[pIdxParent].x - cx;
                ty = compositePoints[pIdxParent].y - cy;
            }
        }

        for (auto& pt : childPts) {
            const float nx = m00 * pt.x + m01 * pt.y + tx;
            const float ny = m10 * pt.x + m11 * pt.y + ty;
            pt.x = nx;
            pt.y = ny;
        }
        compositePoints.insert(compositePoints.end(), childPts.begin(), childPts.end());

        for (auto& seg : childSegs) {
            const float x0 = m00 * seg.p0x + m01 * seg.p0y + tx;
            const float y0 = m10 * seg.p0x + m11 * seg.p0y + ty;
            const float x1 = m00 * seg.p1x + m01 * seg.p1y + tx;
            const float y1 = m10 * seg.p1x + m11 * seg.p1y + ty;
            const float x2 = m00 * seg.p2x + m01 * seg.p2y + tx;
            const float y2 = m10 * seg.p2x + m11 * seg.p2y + ty;
            seg.p0x = x0; seg.p0y = y0;
            seg.p1x = x1; seg.p1y = y1;
            seg.p2x = x2; seg.p2y = y2;
            outSegments.push_back(seg);
        }
    }

    if (outRawPoints) {
        outRawPoints->insert(outRawPoints->end(), compositePoints.begin(), compositePoints.end());
    }
    return true;
}

namespace {

[[nodiscard]] bool ReadCffIndex(
    const TtfReader& r,
    std::size_t indexOffset,
    std::size_t tableEnd,
    std::uint32_t& outEndOffset,
    std::vector<std::pair<std::uint32_t, std::uint32_t>>& outItems) {
    outItems.clear();
    if (indexOffset + 2u > tableEnd) {
        return false;
    }
    std::uint16_t count = 0u;
    if (!r.ReadU16BE(indexOffset, count)) {
        return false;
    }
    if (count == 0u) {
        outEndOffset = static_cast<std::uint32_t>(indexOffset + 2u);
        return true;
    }
    if (indexOffset + 3u > tableEnd) {
        return false;
    }
    std::uint8_t offSize = 0u;
    if (!r.ReadU8(indexOffset + 2u, offSize) || offSize < 1u || offSize > 4u) {
        return false;
    }
    const std::size_t offTableStart = indexOffset + 3u;
    const std::size_t offTableBytes = static_cast<std::size_t>(count + 1u) * static_cast<std::size_t>(offSize);
    if (offTableStart + offTableBytes > tableEnd) {
        return false;
    }
    const std::size_t dataBase = offTableStart + offTableBytes - 1u;
    auto readOff = [&](std::size_t idx, std::uint32_t& outOff) noexcept -> bool {
        const std::size_t p = offTableStart + idx * static_cast<std::size_t>(offSize);
        std::uint32_t val = 0u;
        for (std::uint8_t k = 0u; k < offSize; ++k) {
            std::uint8_t b = 0u;
            if (!r.ReadU8(p + k, b)) return false;
            val = (val << 8u) | static_cast<std::uint32_t>(b);
        }
        outOff = val;
        return true;
    };
    std::uint32_t prevOff = 0u;
    if (!readOff(0u, prevOff) || prevOff == 0u) {
        return false;
    }
    outItems.reserve(count);
    for (std::uint16_t i = 0u; i < count; ++i) {
        std::uint32_t nextOff = 0u;
        if (!readOff(static_cast<std::size_t>(i) + 1u, nextOff) || nextOff < prevOff) {
            return false;
        }
        const std::size_t absStart = dataBase + static_cast<std::size_t>(prevOff);
        const std::size_t len = static_cast<std::size_t>(nextOff - prevOff);
        if (absStart + len > tableEnd) {
            return false;
        }
        outItems.push_back({static_cast<std::uint32_t>(absStart), static_cast<std::uint32_t>(len)});
        prevOff = nextOff;
    }
    outEndOffset = static_cast<std::uint32_t>(dataBase + static_cast<std::size_t>(prevOff));
    return true;
}

} // namespace

bool TrueTypeFontFace::ParseCffTable(std::uint32_t offset, std::uint32_t length, std::string* outError) {
    cffCharStrings_.items.clear();
    cffLocalSubrs_.items.clear();
    cffGlobalSubrs_.items.clear();

    TtfReader r{data_.data(), data_.size()};
    const std::size_t tableStart = offset;
    const std::size_t tableEnd = tableStart + length;
    if (length < 4u || !r.InBounds(tableStart, length)) {
        if (outError) *outError = "Invalid CFF table bounds";
        return false;
    }
    std::uint8_t major = 0u, minor = 0u, hdrSize = 0u;
    if (!r.ReadU8(tableStart, major) || !r.ReadU8(tableStart + 1u, minor) ||
        !r.ReadU8(tableStart + 2u, hdrSize) || major != 1u || hdrSize < 4u ||
        static_cast<std::size_t>(hdrSize) >= length) {
        if (outError) *outError = "Unsupported CFF version/header";
        return false;
    }

    auto toIndexTable = [&](std::size_t idxPos, CffIndexTable& outIdx) -> bool {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> raw;
        if (!ReadCffIndex(r, idxPos, tableEnd, outIdx.endOffset, raw)) {
            return false;
        }
        outIdx.items.reserve(raw.size());
        for (const auto& kv : raw) {
            outIdx.items.push_back(CffIndexTable::Slice{kv.first, kv.second});
        }
        return true;
    };

    CffIndexTable nameIdx{}, topDictIdx{}, strIdx{};
    if (!toIndexTable(tableStart + hdrSize, nameIdx) ||
        !toIndexTable(nameIdx.endOffset, topDictIdx) ||
        topDictIdx.items.empty() ||
        !toIndexTable(topDictIdx.endOffset, strIdx) ||
        !toIndexTable(strIdx.endOffset, cffGlobalSubrs_)) {
        if (outError) *outError = "Truncated CFF top headers";
        return false;
    }

    auto parseDict = [&](std::uint32_t dOff, std::uint32_t dLen,
                         std::int32_t& outCharStringsRel,
                         std::int32_t& outPrivSize,
                         std::int32_t& outPrivRel,
                         std::int32_t& outSubrsRel) {
        std::size_t p = dOff;
        const std::size_t endP = static_cast<std::size_t>(dOff) + dLen;
        std::vector<std::int32_t> stk;
        while (p < endP) {
            std::uint8_t b0 = 0u;
            if (!r.ReadU8(p++, b0)) break;
            if (b0 <= 21u) {
                std::uint16_t op = b0;
                if (b0 == 12u) {
                    std::uint8_t b1 = 0u;
                    if (!r.ReadU8(p++, b1)) break;
                    op = static_cast<std::uint16_t>(0x0C00u | b1);
                }
                if (op == 17u && !stk.empty()) {
                    outCharStringsRel = stk.back();
                } else if (op == 18u && stk.size() >= 2u) {
                    outPrivSize = stk[stk.size() - 2u];
                    outPrivRel = stk.back();
                } else if (op == 19u && !stk.empty()) {
                    outSubrsRel = stk.back();
                }
                stk.clear();
            } else if (b0 == 28u) {
                std::int16_t v = 0;
                if (!r.ReadI16BE(p, v)) break;
                p += 2u;
                stk.push_back(static_cast<std::int32_t>(v));
            } else if (b0 == 29u) {
                std::uint32_t u = 0u;
                if (!r.ReadU32BE(p, u)) break;
                p += 4u;
                stk.push_back(static_cast<std::int32_t>(u));
            } else if (b0 == 30u) {
                while (p < endP) {
                    std::uint8_t nb = 0u;
                    if (!r.ReadU8(p++, nb)) break;
                    if ((nb & 0x0Fu) == 0x0Fu || ((nb >> 4u) & 0x0Fu) == 0x0Fu) break;
                }
                stk.push_back(0);
            } else if (b0 >= 32u && b0 <= 246u) {
                stk.push_back(static_cast<std::int32_t>(b0) - 139);
            } else if (b0 >= 247u && b0 <= 250u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) break;
                stk.push_back((static_cast<std::int32_t>(b0) - 247) * 256 + static_cast<std::int32_t>(b1) + 108);
            } else if (b0 >= 251u && b0 <= 254u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) break;
                stk.push_back(-(static_cast<std::int32_t>(b0) - 251) * 256 - static_cast<std::int32_t>(b1) - 108);
            }
        }
    };

    std::int32_t charStringsRel = -1, privSize = 0, privRel = -1, unusedSubr = -1;
    parseDict(topDictIdx.items[0].offset, topDictIdx.items[0].length,
              charStringsRel, privSize, privRel, unusedSubr);
    if (charStringsRel <= 0 || tableStart + static_cast<std::size_t>(charStringsRel) >= tableEnd) {
        if (outError) *outError = "CFF missing CharStrings INDEX";
        return false;
    }
    if (!toIndexTable(tableStart + static_cast<std::size_t>(charStringsRel), cffCharStrings_) ||
        cffCharStrings_.items.empty()) {
        if (outError) *outError = "Invalid CFF CharStrings INDEX";
        return false;
    }

    if (privRel > 0 && privSize > 0 &&
        tableStart + static_cast<std::size_t>(privRel) + static_cast<std::size_t>(privSize) <= tableEnd) {
        std::int32_t u1 = -1, u2 = 0, u3 = -1, subrsRel = -1;
        parseDict(
            static_cast<std::uint32_t>(tableStart + static_cast<std::size_t>(privRel)),
            static_cast<std::uint32_t>(privSize),
            u1, u2, u3, subrsRel);
        if (subrsRel > 0) {
            const std::size_t subrsPos = tableStart + static_cast<std::size_t>(privRel) + static_cast<std::size_t>(subrsRel);
            if (subrsPos < tableEnd) {
                (void)toIndexTable(subrsPos, cffLocalSubrs_);
            }
        }
    }
    return true;
}

bool TrueTypeFontFace::ExtractCffGlyphOutline(
    std::uint32_t glyphIndex,
    std::vector<TtfContourSegment>& outSegments) const {
    if (glyphIndex >= cffCharStrings_.items.size()) {
        return false;
    }

    TtfReader r{data_.data(), data_.size()};
    std::vector<float> stack;
    stack.reserve(64u);

    float curX = 0.0f, curY = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    bool hasOpenContour = false;
    bool widthChecked = false;
    std::uint32_t numHints = 0u;

    auto closeContour = [&]() {
        if (hasOpenContour) {
            if (std::fabs(curX - startX) > 1e-4f || std::fabs(curY - startY) > 1e-4f) {
                outSegments.push_back(TtfContourSegment{
                    curX, curY,
                    0.5f * (curX + startX), 0.5f * (curY + startY),
                    startX, startY,
                    false});
            }
            hasOpenContour = false;
        }
    };

    auto moveTo = [&](float nx, float ny) {
        closeContour();
        curX = nx;
        curY = ny;
        startX = nx;
        startY = ny;
        hasOpenContour = true;
    };

    auto lineTo = [&](float nx, float ny) {
        if (!hasOpenContour) {
            startX = curX;
            startY = curY;
            hasOpenContour = true;
        }
        outSegments.push_back(TtfContourSegment{
            curX, curY,
            0.5f * (curX + nx), 0.5f * (curY + ny),
            nx, ny,
            false});
        curX = nx;
        curY = ny;
    };

    auto emitCubicSegment = [&](float p0x, float p0y, float c1x, float c1y,
                                float c2x, float c2y, float p3x, float p3y) {
        auto emitSub = [&](float ax, float ay, float bx, float by,
                           float cx, float cy, float dx, float dy) {
            const float qx = 0.75f * (bx + cx) - 0.25f * (ax + dx);
            const float qy = 0.75f * (by + cy) - 0.25f * (ay + dy);
            outSegments.push_back(TtfContourSegment{ax, ay, qx, qy, dx, dy, true});
        };
        auto splitHalf = [](float a0, float a1, float a2, float a3,
                            float& l1, float& l2, float& mid, float& r1, float& r2) {
            const float m01 = 0.5f * (a0 + a1);
            const float m12 = 0.5f * (a1 + a2);
            const float m23 = 0.5f * (a2 + a3);
            l1 = m01;
            l2 = 0.5f * (m01 + m12);
            r2 = m23;
            r1 = 0.5f * (m12 + m23);
            mid = 0.5f * (l2 + r1);
        };
        float xL1, xL2, xM, xR1, xR2, yL1, yL2, yM, yR1, yR2;
        splitHalf(p0x, c1x, c2x, p3x, xL1, xL2, xM, xR1, xR2);
        splitHalf(p0y, c1y, c2y, p3y, yL1, yL2, yM, yR1, yR2);

        float q1L1, q1L2, q1M, q1R1, q1R2, y1L1, y1L2, y1M, y1R1, y1R2;
        splitHalf(p0x, xL1, xL2, xM, q1L1, q1L2, q1M, q1R1, q1R2);
        splitHalf(p0y, yL1, yL2, yM, y1L1, y1L2, y1M, y1R1, y1R2);
        emitSub(p0x, p0y, q1L1, y1L1, q1L2, y1L2, q1M, y1M);
        emitSub(q1M, y1M, q1R1, y1R1, q1R2, y1R2, xM, yM);

        float q2L1, q2L2, q2M, q2R1, q2R2, y2L1, y2L2, y2M, y2R1, y2R2;
        splitHalf(xM, xR1, xR2, p3x, q2L1, q2L2, q2M, q2R1, q2R2);
        splitHalf(yM, yR1, yR2, p3y, y2L1, y2L2, y2M, y2R1, y2R2);
        emitSub(xM, yM, q2L1, y2L1, q2L2, y2L2, q2M, y2M);
        emitSub(q2M, y2M, q2R1, y2R1, q2R2, y2R2, p3x, p3y);
    };

    auto curveTo = [&](float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        if (!hasOpenContour) {
            startX = curX;
            startY = curY;
            hasOpenContour = true;
        }
        const float c1x = curX + dx1;
        const float c1y = curY + dy1;
        const float c2x = c1x + dx2;
        const float c2y = c1y + dy2;
        const float p3x = c2x + dx3;
        const float p3y = c2y + dy3;
        emitCubicSegment(curX, curY, c1x, c1y, c2x, c2y, p3x, p3y);
        curX = p3x;
        curY = p3y;
    };

    auto subrBias = [](std::size_t n) noexcept -> std::int32_t {
        if (n < 1240u) return 107;
        if (n < 33900u) return 1131;
        return 32768;
    };

    auto consumeOptionalWidth = [&](bool hasExtraWidth) {
        if (!widthChecked) {
            widthChecked = true;
            if (hasExtraWidth && !stack.empty()) {
                stack.erase(stack.begin());
            }
        }
    };

    bool ended = false;
    auto runCharstring = [&](auto& self, const CffIndexTable::Slice& slice, std::uint32_t depth) -> bool {
        if (depth > 16u) {
            return false;
        }
        std::size_t p = slice.offset;
        const std::size_t endP = static_cast<std::size_t>(slice.offset) + slice.length;
        while (p < endP && !ended) {
            std::uint8_t b0 = 0u;
            if (!r.ReadU8(p++, b0)) return false;
            if (b0 >= 32u && b0 <= 246u) {
                stack.push_back(static_cast<float>(static_cast<std::int32_t>(b0) - 139));
                continue;
            }
            if (b0 >= 247u && b0 <= 250u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) return false;
                stack.push_back(static_cast<float>((static_cast<std::int32_t>(b0) - 247) * 256 + static_cast<std::int32_t>(b1) + 108));
                continue;
            }
            if (b0 >= 251u && b0 <= 254u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) return false;
                stack.push_back(static_cast<float>(-(static_cast<std::int32_t>(b0) - 251) * 256 - static_cast<std::int32_t>(b1) - 108));
                continue;
            }
            if (b0 == 28u) {
                std::int16_t v = 0;
                if (!r.ReadI16BE(p, v)) return false;
                p += 2u;
                stack.push_back(static_cast<float>(v));
                continue;
            }
            if (b0 == 255u) {
                std::uint32_t u = 0u;
                if (!r.ReadU32BE(p, u)) return false;
                p += 4u;
                const auto s = static_cast<std::int32_t>(u);
                stack.push_back(static_cast<float>(s) / 65536.0f);
                continue;
            }

            switch (b0) {
                case 1u:  // hstem
                case 3u:  // vstem
                case 18u: // hstemhm
                case 23u: // vstemhm
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    numHints += static_cast<std::uint32_t>(stack.size() / 2u);
                    stack.clear();
                    break;
                case 19u: // hintmask
                case 20u: // cntrmask
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    if (!stack.empty()) {
                        numHints += static_cast<std::uint32_t>(stack.size() / 2u);
                        stack.clear();
                    }
                    p += static_cast<std::size_t>((numHints + 7u) / 8u);
                    if (p > endP) return false;
                    break;
                case 10u: { // callsubr
                    if (stack.empty()) return false;
                    const std::int32_t idx = static_cast<std::int32_t>(std::lround(stack.back())) +
                                             subrBias(cffLocalSubrs_.items.size());
                    stack.pop_back();
                    if (idx < 0 || static_cast<std::size_t>(idx) >= cffLocalSubrs_.items.size()) return false;
                    if (!self(self, cffLocalSubrs_.items[static_cast<std::size_t>(idx)], depth + 1u)) return false;
                    break;
                }
                case 29u: { // callgsubr
                    if (stack.empty()) return false;
                    const std::int32_t idx = static_cast<std::int32_t>(std::lround(stack.back())) +
                                             subrBias(cffGlobalSubrs_.items.size());
                    stack.pop_back();
                    if (idx < 0 || static_cast<std::size_t>(idx) >= cffGlobalSubrs_.items.size()) return false;
                    if (!self(self, cffGlobalSubrs_.items[static_cast<std::size_t>(idx)], depth + 1u)) return false;
                    break;
                }
                case 11u: // return
                    return true;
                case 14u: // endchar
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    stack.clear();
                    closeContour();
                    ended = true;
                    return true;
                case 21u: // rmoveto
                    consumeOptionalWidth(stack.size() > 2u);
                    if (stack.size() >= 2u) {
                        moveTo(curX + stack[0], curY + stack[1]);
                    }
                    stack.clear();
                    break;
                case 22u: // hmoveto
                    consumeOptionalWidth(stack.size() > 1u);
                    if (!stack.empty()) {
                        moveTo(curX + stack[0], curY);
                    }
                    stack.clear();
                    break;
                case 4u: // vmoveto
                    consumeOptionalWidth(stack.size() > 1u);
                    if (!stack.empty()) {
                        moveTo(curX, curY + stack[0]);
                    }
                    stack.clear();
                    break;
                case 5u: // rlineto
                    for (std::size_t i = 0u; i + 1u < stack.size(); i += 2u) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                    }
                    stack.clear();
                    break;
                case 6u: { // hlineto
                    bool horiz = true;
                    for (std::size_t i = 0u; i < stack.size(); ++i) {
                        if (horiz) lineTo(curX + stack[i], curY);
                        else lineTo(curX, curY + stack[i]);
                        horiz = !horiz;
                    }
                    stack.clear();
                    break;
                }
                case 7u: { // vlineto
                    bool vert = true;
                    for (std::size_t i = 0u; i < stack.size(); ++i) {
                        if (vert) lineTo(curX, curY + stack[i]);
                        else lineTo(curX + stack[i], curY);
                        vert = !vert;
                    }
                    stack.clear();
                    break;
                }
                case 8u: // rrcurveto
                    for (std::size_t i = 0u; i + 5u < stack.size(); i += 6u) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                    }
                    stack.clear();
                    break;
                case 24u: { // rcurveline
                    std::size_t i = 0u;
                    while (i + 7u < stack.size()) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                        i += 6u;
                    }
                    if (i + 1u < stack.size()) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                    }
                    stack.clear();
                    break;
                }
                case 25u: { // rlinecurve
                    std::size_t i = 0u;
                    while (i + 7u < stack.size()) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                        i += 2u;
                    }
                    if (i + 5u < stack.size()) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                    }
                    stack.clear();
                    break;
                }
                case 26u: { // vvcurveto
                    std::size_t i = 0u;
                    float dx1 = 0.0f;
                    if ((stack.size() % 4u) == 1u) {
                        dx1 = stack[i++];
                    }
                    while (i + 3u < stack.size()) {
                        curveTo(dx1, stack[i], stack[i + 1u], stack[i + 2u], 0.0f, stack[i + 3u]);
                        dx1 = 0.0f;
                        i += 4u;
                    }
                    stack.clear();
                    break;
                }
                case 27u: { // hhcurveto
                    std::size_t i = 0u;
                    float dy1 = 0.0f;
                    if ((stack.size() % 4u) == 1u) {
                        dy1 = stack[i++];
                    }
                    while (i + 3u < stack.size()) {
                        curveTo(stack[i], dy1, stack[i + 1u], stack[i + 2u], stack[i + 3u], 0.0f);
                        dy1 = 0.0f;
                        i += 4u;
                    }
                    stack.clear();
                    break;
                }
                case 30u:   // vhcurveto
                case 31u: { // hvcurveto
                    bool verticalStart = (b0 == 30u);
                    std::size_t i = 0u;
                    const std::size_t n = stack.size();
                    while (i + 3u < n) {
                        const bool isLast = (i + 4u == n) || (i + 5u == n);
                        const float dLast = (isLast && i + 5u == n) ? stack[i + 4u] : 0.0f;
                        if (verticalStart) {
                            // vhcurveto: dy1 dx2 dy2 dx3 [dyf]
                            curveTo(0.0f, stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], dLast);
                        } else {
                            // hvcurveto: dx1 dx2 dy2 dy3 [dxf]
                            curveTo(stack[i], 0.0f, stack[i + 1u], stack[i + 2u], dLast, stack[i + 3u]);
                        }
                        i += (isLast && i + 5u == n) ? 5u : 4u;
                        verticalStart = !verticalStart;
                    }
                    stack.clear();
                    break;
                }
                case 12u: {
                    std::uint8_t b1 = 0u;
                    if (!r.ReadU8(p++, b1)) return false;
                    if (b1 == 34u && stack.size() >= 7u) { // hflex
                        curveTo(stack[0], 0.0f, stack[1], stack[2], stack[3], 0.0f);
                        curveTo(stack[4], 0.0f, stack[5], -stack[2], stack[6], 0.0f);
                    } else if (b1 == 35u && stack.size() >= 13u) { // flex
                        curveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                        curveTo(stack[6], stack[7], stack[8], stack[9], stack[10], stack[11]);
                    } else if (b1 == 36u && stack.size() >= 9u) { // hflex1
                        const float dy1 = stack[1], dy2 = stack[3], dy5 = stack[7];
                        curveTo(stack[0], dy1, stack[2], dy2, stack[4], 0.0f);
                        curveTo(stack[5], 0.0f, stack[6], dy5, stack[8], -(dy1 + dy2 + dy5));
                    } else if (b1 == 37u && stack.size() >= 11u) { // flex1
                        const float dxSum = stack[0] + stack[2] + stack[4] + stack[6] + stack[8];
                        const float dySum = stack[1] + stack[3] + stack[5] + stack[7] + stack[9];
                        const bool dxDom = std::fabs(dxSum) > std::fabs(dySum);
                        curveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                        curveTo(stack[6], stack[7], stack[8], stack[9],
                                dxDom ? stack[10] : -dxSum, dxDom ? -dySum : stack[10]);
                    }
                    stack.clear();
                    break;
                }
                default:
                    stack.clear();
                    break;
            }
        }
        return true;
    };

    const bool ok = runCharstring(runCharstring, cffCharStrings_.items[glyphIndex], 0u);
    closeContour();
    return ok;
}

bool TrueTypeFontFace::ExtractGlyphOutline(
    std::uint32_t glyphIndex,
    TtfGlyphOutline& outOutline) const {
    outOutline = TtfGlyphOutline{};
    outOutline.glyphIndex = glyphIndex;
    if (!valid_ || glyphIndex >= numGlyphs_) {
        return false;
    }

    GetGlyphHorizontalMetrics(glyphIndex, outOutline.advanceWidth, outOutline.leftSideBearing);

    std::uint32_t totalVisitedComponents = 0u;
    const bool extracted = isCff_
        ? ExtractCffGlyphOutline(glyphIndex, outOutline.segments)
        : ExtractGlyphRecursive(glyphIndex, 0u, totalVisitedComponents, outOutline.segments, nullptr);
    if (!extracted) {
        return false;
    }

    if (outOutline.segments.empty()) {
        outOutline.isEmptyOutline = true;
        return true;
    }

    outOutline.isEmptyOutline = false;
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    auto expandBounds = [&](float x, float y) noexcept {
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };
    for (const auto& s : outOutline.segments) {
        expandBounds(s.p0x, s.p0y);
        expandBounds(s.p2x, s.p2y);
        if (s.isQuadratic) {
            // Check exact quadratic Bezier X/Y extrema in (0, 1) rather than over-expanding to control hull
            const float dxDenom = s.p0x - 2.0f * s.p1x + s.p2x;
            if (std::fabs(dxDenom) > 1e-5f) {
                const float tx = (s.p0x - s.p1x) / dxDenom;
                if (tx > 0.0f && tx < 1.0f) {
                    const float u = 1.0f - tx;
                    expandBounds(u * u * s.p0x + 2.0f * u * tx * s.p1x + tx * tx * s.p2x, s.p0y);
                }
            }
            const float dyDenom = s.p0y - 2.0f * s.p1y + s.p2y;
            if (std::fabs(dyDenom) > 1e-5f) {
                const float ty = (s.p0y - s.p1y) / dyDenom;
                if (ty > 0.0f && ty < 1.0f) {
                    const float u = 1.0f - ty;
                    expandBounds(s.p0x, u * u * s.p0y + 2.0f * u * ty * s.p1y + ty * ty * s.p2y);
                }
            }
        }
    }
    outOutline.xMin = minX;
    outOutline.yMin = minY;
    outOutline.xMax = std::max(minX + 1.0f, maxX);
    outOutline.yMax = std::max(minY + 1.0f, maxY);
    return true;
}

} // namespace aero
