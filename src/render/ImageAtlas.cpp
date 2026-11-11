#include "aero/render/ImageAtlas.hpp"
#include "aero/core/UiResourceContext.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aero {

namespace {

[[nodiscard]] constexpr std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

[[nodiscard]] constexpr std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

[[nodiscard]] constexpr std::uint16_t ReadBe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(p[0]) << 8u) |
        static_cast<std::uint16_t>(p[1]));
}

[[nodiscard]] constexpr std::uint32_t ReadBe32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24u) |
           (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) |
           static_cast<std::uint32_t>(p[3]);
}

[[nodiscard]] constexpr std::uint8_t ClampIntToByte(int v) noexcept {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

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

// ============================================================================
// Pure C++17 RFC 1951 DEFLATE Bitstream & Canonical Huffman Decoder
// ============================================================================

struct DeflateBitReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
    std::size_t bytePos = 0u;
    std::uint64_t bitBuf = 0u;
    std::uint32_t bitCount = 0u;

    void Refill() noexcept {
        while (bitCount <= 56u && bytePos < size) {
            bitBuf |= (static_cast<std::uint64_t>(data[bytePos++]) << bitCount);
            bitCount += 8u;
        }
    }

    [[nodiscard]] bool EnsureBits(std::uint32_t n) noexcept {
        if (bitCount < n) {
            Refill();
        }
        return bitCount >= n;
    }

    [[nodiscard]] std::uint32_t PeekBits(std::uint32_t n) const noexcept {
        return static_cast<std::uint32_t>(bitBuf & ((1ULL << n) - 1ULL));
    }

    void DropBits(std::uint32_t n) noexcept {
        bitBuf >>= n;
        bitCount -= n;
    }

    [[nodiscard]] bool ReadBits(std::uint32_t n, std::uint32_t& outVal) noexcept {
        if (n == 0u) {
            outVal = 0u;
            return true;
        }
        if (!EnsureBits(n)) {
            return false;
        }
        outVal = PeekBits(n);
        DropBits(n);
        return true;
    }

    void AlignToByte() noexcept {
        const std::uint32_t rem = bitCount & 7u;
        if (rem != 0u) {
            DropBits(rem);
        }
    }

    [[nodiscard]] bool ReadAlignedBytes(std::uint8_t* dst, std::size_t count) noexcept {
        AlignToByte();
        std::size_t written = 0u;
        while (bitCount >= 8u && written < count) {
            dst[written++] = static_cast<std::uint8_t>(bitBuf & 0xFFu);
            DropBits(8u);
        }
        if (written < count) {
            const std::size_t rem = count - written;
            if (bytePos + rem > size) {
                return false;
            }
            std::memcpy(dst + written, data + bytePos, rem);
            bytePos += rem;
        }
        return true;
    }

    [[nodiscard]] std::size_t EffectiveConsumedBytes() const noexcept {
        const std::size_t bufferedBytes = bitCount / 8u;
        return (bytePos >= bufferedBytes) ? (bytePos - bufferedBytes) : 0u;
    }
};

struct DeflateHuffmanTable {
    static constexpr std::uint32_t kMaxBits = 15u;
    static constexpr std::uint32_t kFastBits = 9u;
    static constexpr std::uint32_t kFastMask = (1u << kFastBits) - 1u;

    // Fast 9-bit table: low 12 bits = symbol, high 4 bits = bit length (0 = slow path / invalid)
    std::array<std::uint16_t, 1u << kFastBits> fastLut{};
    std::array<std::int32_t, kMaxBits + 2u> maxCode{};
    std::array<std::int32_t, kMaxBits + 1u> valOffset{};
    std::array<std::uint16_t, 320u> sortedSymbols{};
    bool empty = true;

    [[nodiscard]] static constexpr std::uint16_t ReverseBits(std::uint16_t code, std::uint32_t len) noexcept {
        std::uint16_t rev = 0u;
        for (std::uint32_t i = 0u; i < len; ++i) {
            rev = static_cast<std::uint16_t>((rev << 1u) | ((code >> i) & 1u));
        }
        return rev;
    }

    [[nodiscard]] bool Build(const std::uint8_t* lengths, std::size_t symbolCount) noexcept {
        fastLut.fill(0u);
        maxCode.fill(-1);
        valOffset.fill(0);
        empty = true;

        std::array<std::uint32_t, kMaxBits + 1u> blCount{};
        std::uint32_t totalNonZero = 0u;
        for (std::size_t i = 0u; i < symbolCount; ++i) {
            const std::uint8_t len = lengths[i];
            if (len > kMaxBits) {
                return false;
            }
            if (len > 0u) {
                ++blCount[len];
                ++totalNonZero;
            }
        }
        if (totalNonZero == 0u) {
            // Valid empty distance tree in literal-only blocks
            return true;
        }
        empty = false;

        // Check oversubscribed Huffman tree
        std::int32_t left = 1;
        for (std::uint32_t bits = 1u; bits <= kMaxBits; ++bits) {
            left <<= 1;
            left -= static_cast<std::int32_t>(blCount[bits]);
            if (left < 0) {
                return false;
            }
        }

        std::array<std::uint16_t, kMaxBits + 1u> nextCode{};
        std::uint16_t code = 0u;
        std::int32_t symOffset = 0;
        for (std::uint32_t bits = 1u; bits <= kMaxBits; ++bits) {
            code = static_cast<std::uint16_t>((code + blCount[bits - 1u]) << 1u);
            nextCode[bits] = code;
            if (blCount[bits] > 0u) {
                valOffset[bits] = symOffset - static_cast<std::int32_t>(code);
                maxCode[bits] = static_cast<std::int32_t>(code + blCount[bits] - 1u);
                symOffset += static_cast<std::int32_t>(blCount[bits]);
            }
        }

        std::array<std::uint16_t, kMaxBits + 1u> curCode = nextCode;
        for (std::size_t sym = 0u; sym < symbolCount; ++sym) {
            const std::uint8_t len = lengths[sym];
            if (len == 0u) {
                continue;
            }
            const std::uint16_t c = curCode[len]++;
            const auto sortedIdx = static_cast<std::int32_t>(c) + valOffset[len];
            if (sortedIdx < 0 || sortedIdx >= static_cast<std::int32_t>(sortedSymbols.size())) {
                return false;
            }
            sortedSymbols[static_cast<std::size_t>(sortedIdx)] = static_cast<std::uint16_t>(sym);

            if (len <= kFastBits) {
                const std::uint16_t rev = ReverseBits(c, len);
                const auto entry = static_cast<std::uint16_t>((static_cast<std::uint16_t>(len) << 12u) | (sym & 0x0FFFu));
                const std::uint32_t step = 1u << len;
                for (std::uint32_t fill = rev; fill < (1u << kFastBits); fill += step) {
                    fastLut[fill] = entry;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool Decode(DeflateBitReader& br, std::uint16_t& outSym) const noexcept {
        if (empty) {
            return false;
        }
        br.Refill();
        if (br.bitCount >= kFastBits) {
            const std::uint16_t fast = fastLut[br.bitBuf & kFastMask];
            if (fast != 0u) {
                const std::uint32_t len = fast >> 12u;
                outSym = static_cast<std::uint16_t>(fast & 0x0FFFu);
                br.DropBits(len);
                return true;
            }
        } else if (br.bitCount > 0u) {
            const std::uint16_t fast = fastLut[br.bitBuf & kFastMask];
            const std::uint32_t len = fast >> 12u;
            if (len > 0u && len <= br.bitCount) {
                outSym = static_cast<std::uint16_t>(fast & 0x0FFFu);
                br.DropBits(len);
                return true;
            }
        }

        std::int32_t code = 0;
        for (std::uint32_t len = 1u; len <= kMaxBits; ++len) {
            if (!br.EnsureBits(1u)) {
                return false;
            }
            code = (code << 1) | static_cast<std::int32_t>(br.bitBuf & 1ULL);
            br.DropBits(1u);
            if (code <= maxCode[len]) {
                const std::int32_t idx = code + valOffset[len];
                if (idx < 0 || idx >= static_cast<std::int32_t>(sortedSymbols.size())) {
                    return false;
                }
                outSym = sortedSymbols[static_cast<std::size_t>(idx)];
                return true;
            }
        }
        return false;
    }
};

constexpr std::array<std::uint16_t, 29> kDeflateLenBase = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

constexpr std::array<std::uint8_t, 29> kDeflateLenExtra = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

constexpr std::array<std::uint16_t, 30> kDeflateDistBase = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

constexpr std::array<std::uint8_t, 30> kDeflateDistExtra = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

constexpr std::array<std::uint8_t, 19> kDeflateCodeLengthOrder = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

[[nodiscard]] std::uint32_t ComputeAdler32(const std::uint8_t* data, std::size_t len) noexcept {
    constexpr std::uint32_t kModAdler = 65521u;
    std::uint32_t s1 = 1u;
    std::uint32_t s2 = 0u;
    std::size_t pos = 0u;
    while (pos < len) {
        const std::size_t chunk = std::min<std::size_t>(len - pos, 5552u);
        for (std::size_t i = 0u; i < chunk; ++i) {
            s1 += data[pos + i];
            s2 += s1;
        }
        s1 %= kModAdler;
        s2 %= kModAdler;
        pos += chunk;
    }
    return (s2 << 16u) | s1;
}

// ============================================================================
// PNG Row Unfiltering & Sample Unpacking
// ============================================================================

[[nodiscard]] constexpr std::uint8_t PaethPredictor(int a, int b, int c) noexcept {
    const int p = a + b - c;
    const int pa = (p >= a) ? (p - a) : (a - p);
    const int pb = (p >= b) ? (p - b) : (b - p);
    const int pc = (p >= c) ? (p - c) : (c - p);
    if (pa <= pb && pa <= pc) {
        return static_cast<std::uint8_t>(a);
    }
    if (pb <= pc) {
        return static_cast<std::uint8_t>(b);
    }
    return static_cast<std::uint8_t>(c);
}

[[nodiscard]] bool UnfilterPngRow(
    std::uint8_t filterType,
    const std::uint8_t* rawRow,
    const std::uint8_t* prevRow,
    std::uint8_t* outRow,
    std::size_t rowBytes,
    std::size_t bpp) noexcept {
    switch (filterType) {
        case 0u: // None
            std::memcpy(outRow, rawRow, rowBytes);
            return true;
        case 1u: // Sub
            for (std::size_t i = 0u; i < rowBytes; ++i) {
                const std::uint8_t a = (i >= bpp) ? outRow[i - bpp] : 0u;
                outRow[i] = static_cast<std::uint8_t>(rawRow[i] + a);
            }
            return true;
        case 2u: // Up
            if (prevRow) {
                for (std::size_t i = 0u; i < rowBytes; ++i) {
                    outRow[i] = static_cast<std::uint8_t>(rawRow[i] + prevRow[i]);
                }
            } else {
                std::memcpy(outRow, rawRow, rowBytes);
            }
            return true;
        case 3u: // Average
            for (std::size_t i = 0u; i < rowBytes; ++i) {
                const int a = (i >= bpp) ? static_cast<int>(outRow[i - bpp]) : 0;
                const int b = prevRow ? static_cast<int>(prevRow[i]) : 0;
                outRow[i] = static_cast<std::uint8_t>(rawRow[i] + ((a + b) >> 1));
            }
            return true;
        case 4u: // Paeth
            for (std::size_t i = 0u; i < rowBytes; ++i) {
                const int a = (i >= bpp) ? static_cast<int>(outRow[i - bpp]) : 0;
                const int b = prevRow ? static_cast<int>(prevRow[i]) : 0;
                const int c = (prevRow && i >= bpp) ? static_cast<int>(prevRow[i - bpp]) : 0;
                outRow[i] = static_cast<std::uint8_t>(rawRow[i] + PaethPredictor(a, b, c));
            }
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::uint8_t ExtractSubByteSample(
    const std::uint8_t* row,
    std::uint32_t x,
    std::uint8_t bitDepth) noexcept {
    const std::uint32_t bitOffset = x * bitDepth;
    const std::uint8_t byteVal = row[bitOffset >> 3u];
    const std::uint32_t shift = 8u - bitDepth - (bitOffset & 7u);
    const std::uint8_t mask = static_cast<std::uint8_t>((1u << bitDepth) - 1u);
    return static_cast<std::uint8_t>((byteVal >> shift) & mask);
}

// ============================================================================
// Pure C++17 Baseline & Progressive JPEG Decoder (ITU-T.81 + JFIF/Exif)
// ============================================================================

constexpr std::array<std::uint8_t, 64> kJpegZigZag = {
    0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63};

struct JpegIdctBasis {
    std::array<std::array<float, 8>, 8> basis{};

    JpegIdctBasis() noexcept {
        constexpr float kPiOver16 = 3.14159265f / 16.0f;
        constexpr float kInvSqrt8 = 0.35355339f;
        for (std::size_t u = 0u; u < 8u; ++u) {
            const float alpha = (u == 0u) ? kInvSqrt8 : 0.5f;
            for (std::size_t x = 0u; x < 8u; ++x) {
                const float angle = (2.0f * static_cast<float>(x) + 1.0f) * static_cast<float>(u) * kPiOver16;
                basis[u][x] = alpha * std::cos(angle);
            }
        }
    }
};

[[nodiscard]] const JpegIdctBasis& GetJpegIdctBasis() noexcept {
    static const JpegIdctBasis sBasis;
    return sBasis;
}

void InverseDct8x8(const std::int32_t dequantBlock[64], std::uint8_t* outPixels, std::size_t outStride) noexcept {
    // Fast path: AC all-zero block (extremely common in sky/background/UI flat blocks)
    bool hasNonZeroAc = false;
    for (std::size_t i = 1u; i < 64u; ++i) {
        if (dequantBlock[i] != 0) {
            hasNonZeroAc = true;
            break;
        }
    }
    if (!hasNonZeroAc) {
        const float dcVal = static_cast<float>(dequantBlock[0]) * 0.125f + 128.5f;
        const std::uint8_t dcByte = ClampIntToByte(static_cast<int>(std::floor(dcVal)));
        for (std::size_t y = 0u; y < 8u; ++y) {
            std::memset(outPixels + y * outStride, dcByte, 8u);
        }
        return;
    }

    const auto& b = GetJpegIdctBasis().basis;
    float tmp[8][8];

    // Row 1D IDCT
    for (std::size_t y = 0u; y < 8u; ++y) {
        const std::int32_t* row = dequantBlock + y * 8u;
        if ((row[0] | row[1] | row[2] | row[3] | row[4] | row[5] | row[6] | row[7]) == 0) {
            for (std::size_t x = 0u; x < 8u; ++x) {
                tmp[y][x] = 0.0f;
            }
            continue;
        }
        const float c0 = static_cast<float>(row[0]);
        const float c1 = static_cast<float>(row[1]);
        const float c2 = static_cast<float>(row[2]);
        const float c3 = static_cast<float>(row[3]);
        const float c4 = static_cast<float>(row[4]);
        const float c5 = static_cast<float>(row[5]);
        const float c6 = static_cast<float>(row[6]);
        const float c7 = static_cast<float>(row[7]);
        for (std::size_t x = 0u; x < 8u; ++x) {
            tmp[y][x] = c0 * b[0][x] + c1 * b[1][x] + c2 * b[2][x] + c3 * b[3][x] +
                        c4 * b[4][x] + c5 * b[5][x] + c6 * b[6][x] + c7 * b[7][x];
        }
    }

    // Column 1D IDCT + level shift (+128)
    for (std::size_t x = 0u; x < 8u; ++x) {
        const float r0 = tmp[0][x];
        const float r1 = tmp[1][x];
        const float r2 = tmp[2][x];
        const float r3 = tmp[3][x];
        const float r4 = tmp[4][x];
        const float r5 = tmp[5][x];
        const float r6 = tmp[6][x];
        const float r7 = tmp[7][x];
        for (std::size_t y = 0u; y < 8u; ++y) {
            const float v = 128.5f +
                            (r0 * b[0][y] + r1 * b[1][y] + r2 * b[2][y] + r3 * b[3][y] +
                             r4 * b[4][y] + r5 * b[5][y] + r6 * b[6][y] + r7 * b[7][y]);
            outPixels[y * outStride + x] = ClampIntToByte(static_cast<int>(std::floor(v)));
        }
    }
}

struct JpegHuffmanTable {
    static constexpr std::uint32_t kFastBits = 9u;
    std::array<std::uint16_t, 1u << kFastBits> fastLut{};
    std::array<std::int32_t, 18u> maxCode{};
    std::array<std::int32_t, 17u> valOffset{};
    std::array<std::uint8_t, 256u> symbols{};
    bool valid = false;

    bool Build(const std::uint8_t counts[16], const std::uint8_t* symData, std::size_t totalSyms) noexcept {
        if (totalSyms > 256u) {
            return false;
        }
        fastLut.fill(0u);
        maxCode.fill(-1);
        valOffset.fill(0);
        std::memcpy(symbols.data(), symData, totalSyms);

        std::uint32_t code = 0u;
        std::int32_t symIdx = 0;
        for (std::uint32_t bits = 1u; bits <= 16u; ++bits) {
            code <<= 1u;
            const std::uint32_t cnt = counts[bits - 1u];
            if (cnt > 0u) {
                if (code + cnt > (1u << bits)) {
                    return false;
                }
                valOffset[bits] = symIdx - static_cast<std::int32_t>(code);
                for (std::uint32_t i = 0u; i < cnt; ++i) {
                    const std::uint8_t sym = symbols[static_cast<std::size_t>(symIdx + static_cast<std::int32_t>(i))];
                    const std::uint32_t curCode = code + i;
                    if (bits <= kFastBits) {
                        const std::uint32_t shift = kFastBits - bits;
                        const std::uint32_t prefix = curCode << shift;
                        const std::uint32_t fillCount = 1u << shift;
                        const auto packed = static_cast<std::uint16_t>((bits << 8u) | sym);
                        for (std::uint32_t f = 0u; f < fillCount; ++f) {
                            fastLut[prefix + f] = packed;
                        }
                    }
                }
                code += cnt;
                maxCode[bits] = static_cast<std::int32_t>(code - 1u);
                symIdx += static_cast<std::int32_t>(cnt);
            } else {
                maxCode[bits] = -1;
            }
        }
        valid = true;
        return true;
    }
};

struct JpegScanBitReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
    std::size_t pos = 0u;
    std::uint64_t bitBuf = 0u;
    std::uint32_t bitCount = 0u;
    bool hitMarker = false;
    std::uint8_t pendingMarker = 0u;

    void Refill() noexcept {
        while (bitCount <= 48u) {
            if (hitMarker || pos >= size) {
                // Pad trailing 1-bits up to lookahead width when reaching a marker/EOF at end of interval
                bitBuf = (bitBuf << 8u) | 0xFFULL;
                bitCount += 8u;
                continue;
            }
            const std::uint8_t b = data[pos++];
            if (b != 0xFFu) {
                bitBuf = (bitBuf << 8u) | static_cast<std::uint64_t>(b);
                bitCount += 8u;
            } else {
                if (pos >= size) {
                    hitMarker = true;
                } else {
                    std::uint8_t next = data[pos];
                    std::uint32_t ffRun = 0u;
                    while (next == 0xFFu) {
                        if (++ffRun > 2048u || (pos + 1u) >= size) {
                            pos = size;
                            hitMarker = true;
                            break;
                        }
                        ++pos;
                        next = data[pos];
                    }
                    if (!hitMarker) {
                        if (next == 0x00u) {
                            ++pos;
                            bitBuf = (bitBuf << 8u) | 0xFFULL;
                            bitCount += 8u;
                        } else {
                            // Encountered real marker (FF next); leave `pos` pointing at `0xFF`
                            --pos;
                            hitMarker = true;
                            pendingMarker = next;
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] std::uint32_t PeekMsb(std::uint32_t n) noexcept {
        if (bitCount < n) {
            Refill();
        }
        return static_cast<std::uint32_t>((bitBuf >> (bitCount - n)) & ((1ULL << n) - 1ULL));
    }

    void DropMsb(std::uint32_t n) noexcept {
        bitCount -= n;
    }

    [[nodiscard]] std::uint32_t ReadBitsMsb(std::uint32_t n) noexcept {
        if (n == 0u) {
            return 0u;
        }
        const std::uint32_t v = PeekMsb(n);
        DropMsb(n);
        return v;
    }

    [[nodiscard]] int ReceiveExtend(std::uint32_t s) noexcept {
        if (s == 0u || s > 16u) {
            return 0;
        }
        const std::uint32_t v = ReadBitsMsb(s);
        const std::uint32_t half = 1u << (s - 1u);
        return (v < half)
            ? (static_cast<int>(v) - static_cast<int>((1u << s) - 1u))
            : static_cast<int>(v);
    }

    [[nodiscard]] bool DecodeSymbol(const JpegHuffmanTable& ht, std::uint8_t& outSym) noexcept {
        if (!ht.valid) {
            return false;
        }
        if (bitCount < JpegHuffmanTable::kFastBits) {
            Refill();
        }
        const std::uint32_t peek9 = static_cast<std::uint32_t>(
            (bitBuf >> (bitCount - JpegHuffmanTable::kFastBits)) & ((1u << JpegHuffmanTable::kFastBits) - 1u));
        const std::uint16_t fast = ht.fastLut[peek9];
        if (fast != 0u) {
            const std::uint32_t len = fast >> 8u;
            outSym = static_cast<std::uint8_t>(fast & 0xFFu);
            DropMsb(len);
            return true;
        }

        if (bitCount < 16u) {
            Refill();
        }
        std::int32_t code = 0;
        for (std::uint32_t bits = 1u; bits <= 16u; ++bits) {
            const auto bit = static_cast<std::int32_t>((bitBuf >> (bitCount - 1u)) & 1ULL);
            DropMsb(1u);
            code = (code << 1) | bit;
            if (code <= ht.maxCode[bits]) {
                const std::int32_t idx = code + ht.valOffset[bits];
                if (idx < 0 || idx >= 256) {
                    return false;
                }
                outSym = ht.symbols[static_cast<std::size_t>(idx)];
                return true;
            }
        }
        return false;
    }

    void ConsumeRestartMarker() noexcept {
        bitBuf = 0u;
        bitCount = 0u;
        hitMarker = false;
        pendingMarker = 0u;
        // Skip optional fill bytes up to FF D0..D7
        while (pos < size && data[pos] != 0xFFu) {
            ++pos;
        }
        while (pos < size && data[pos] == 0xFFu) {
            ++pos;
        }
        if (pos < size) {
            if ((data[pos] >= 0xD0u && data[pos] <= 0xD7u) || data[pos] == 0x00u) {
                ++pos;
            } else if (pos > 0u) {
                --pos;
                hitMarker = true;
                pendingMarker = data[pos + 1u];
            }
        }
    }

    void SyncEndOfScan() noexcept {
        bitBuf = 0u;
        bitCount = 0u;
        if (!hitMarker) {
            while (pos < size) {
                if (data[pos] != 0xFFu) {
                    ++pos;
                    continue;
                }
                std::size_t mPos = pos + 1u;
                while (mPos < size && data[mPos] == 0xFFu) {
                    ++mPos;
                }
                if (mPos >= size) {
                    pos = size;
                    break;
                }
                if (data[mPos] == 0x00u || (data[mPos] >= 0xD0u && data[mPos] <= 0xD7u)) {
                    pos = mPos + 1u;
                    continue;
                }
                // Found next segment marker (leave pos at the leading 0xFF)
                pos = mPos - 1u;
                break;
            }
        }
        hitMarker = false;
        pendingMarker = 0u;
    }
};

} // namespace

// ============================================================================
// Public Decompression & Image Decoding Functions
// ============================================================================

bool InflateRfc1951(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData,
    std::size_t* outConsumedBytes,
    std::size_t maxOutputBytes) noexcept {
    outData.clear();
    if (!src || srcSize == 0u) {
        return false;
    }

    try {
        outData.reserve(std::min<std::size_t>(maxOutputBytes, std::min<std::size_t>(srcSize * 4u, 65536u)));

        DeflateBitReader br{src, srcSize, 0u, 0u, 0u};
        bool isFinalBlock = false;

        while (!isFinalBlock) {
            std::uint32_t bfinal = 0u;
            std::uint32_t btype = 0u;
            if (!br.ReadBits(1u, bfinal) || !br.ReadBits(2u, btype)) {
                return false;
            }
            isFinalBlock = (bfinal != 0u);

            if (btype == 0u) {
                // Uncompressed stored block
                std::uint8_t hdr[4]{};
                if (!br.ReadAlignedBytes(hdr, 4u)) {
                    return false;
                }
                const std::uint16_t len = ReadLe16(hdr);
                const std::uint16_t nlen = ReadLe16(hdr + 2u);
                if (static_cast<std::uint16_t>(len ^ 0xFFFFu) != nlen) {
                    return false;
                }
                if (static_cast<std::size_t>(len) > maxOutputBytes ||
                    outData.size() > maxOutputBytes - static_cast<std::size_t>(len)) {
                    return false;
                }
                const std::size_t oldSz = outData.size();
                outData.resize(oldSz + len);
                if (len > 0u && !br.ReadAlignedBytes(outData.data() + oldSz, len)) {
                    return false;
                }
            } else if (btype == 1u || btype == 2u) {
                DeflateHuffmanTable litLenTable{};
                DeflateHuffmanTable distTable{};

                if (btype == 1u) {
                    std::array<std::uint8_t, 288u> fixedLit{};
                    for (std::size_t i = 0u; i <= 143u; ++i) fixedLit[i] = 8u;
                    for (std::size_t i = 144u; i <= 255u; ++i) fixedLit[i] = 9u;
                    for (std::size_t i = 256u; i <= 279u; ++i) fixedLit[i] = 7u;
                    for (std::size_t i = 280u; i <= 287u; ++i) fixedLit[i] = 8u;
                    std::array<std::uint8_t, 32u> fixedDist{};
                    fixedDist.fill(5u);
                    if (!litLenTable.Build(fixedLit.data(), fixedLit.size()) ||
                        !distTable.Build(fixedDist.data(), fixedDist.size())) {
                        return false;
                    }
                } else {
                    std::uint32_t hlitRaw = 0u;
                    std::uint32_t hdistRaw = 0u;
                    std::uint32_t hclenRaw = 0u;
                    if (!br.ReadBits(5u, hlitRaw) ||
                        !br.ReadBits(5u, hdistRaw) ||
                        !br.ReadBits(4u, hclenRaw)) {
                        return false;
                    }
                    const std::uint32_t hlit = hlitRaw + 257u;
                    const std::uint32_t hdist = hdistRaw + 1u;
                    const std::uint32_t hclen = hclenRaw + 4u;
                    if (hlit > 286u || hdist > 32u) {
                        return false;
                    }

                    std::array<std::uint8_t, 19u> clLengths{};
                    for (std::uint32_t i = 0u; i < hclen; ++i) {
                        std::uint32_t lenVal = 0u;
                        if (!br.ReadBits(3u, lenVal)) {
                            return false;
                        }
                        clLengths[kDeflateCodeLengthOrder[i]] = static_cast<std::uint8_t>(lenVal);
                    }

                    DeflateHuffmanTable clTable{};
                    if (!clTable.Build(clLengths.data(), clLengths.size())) {
                        return false;
                    }

                    const std::uint32_t totalLengths = hlit + hdist;
                    std::array<std::uint8_t, 320u> unpackedLengths{};
                    std::uint32_t idx = 0u;
                    while (idx < totalLengths) {
                        std::uint16_t sym = 0u;
                        if (!clTable.Decode(br, sym)) {
                            return false;
                        }
                        if (sym <= 15u) {
                            unpackedLengths[idx++] = static_cast<std::uint8_t>(sym);
                        } else if (sym == 16u) {
                            if (idx == 0u) {
                                return false;
                            }
                            std::uint32_t extra = 0u;
                            if (!br.ReadBits(2u, extra)) {
                                return false;
                            }
                            const std::uint32_t repeat = 3u + extra;
                            if (idx + repeat > totalLengths) {
                                return false;
                            }
                            const std::uint8_t prev = unpackedLengths[idx - 1u];
                            for (std::uint32_t r = 0u; r < repeat; ++r) {
                                unpackedLengths[idx++] = prev;
                            }
                        } else if (sym == 17u || sym == 18u) {
                            std::uint32_t extra = 0u;
                            const std::uint32_t bits = (sym == 17u) ? 3u : 7u;
                            const std::uint32_t base = (sym == 17u) ? 3u : 11u;
                            if (!br.ReadBits(bits, extra)) {
                                return false;
                            }
                            const std::uint32_t repeat = base + extra;
                            if (idx + repeat > totalLengths) {
                                return false;
                            }
                            for (std::uint32_t r = 0u; r < repeat; ++r) {
                                unpackedLengths[idx++] = 0u;
                            }
                        } else {
                            return false;
                        }
                    }

                    if (unpackedLengths[256u] == 0u) {
                        return false;
                    }
                    if (!litLenTable.Build(unpackedLengths.data(), hlit) ||
                        !distTable.Build(unpackedLengths.data() + hlit, hdist)) {
                        return false;
                    }
                }

                // Decode Huffman-compressed literals & LZ77 back-references
                for (;;) {
                    std::uint16_t sym = 0u;
                    if (!litLenTable.Decode(br, sym)) {
                        return false;
                    }
                    if (sym < 256u) {
                        if (outData.size() >= maxOutputBytes) {
                            return false;
                        }
                        outData.push_back(static_cast<std::uint8_t>(sym));
                    } else if (sym == 256u) {
                        break;
                    } else if (sym <= 285u) {
                        const std::uint32_t lenIdx = sym - 257u;
                        std::uint32_t lenExtra = 0u;
                        if (!br.ReadBits(kDeflateLenExtra[lenIdx], lenExtra)) {
                            return false;
                        }
                        const std::size_t matchLen = static_cast<std::size_t>(kDeflateLenBase[lenIdx]) + lenExtra;
                        if (matchLen > maxOutputBytes || outData.size() > maxOutputBytes - matchLen) {
                            return false;
                        }

                        std::uint16_t distSym = 0u;
                        if (!distTable.Decode(br, distSym) || distSym >= 30u) {
                            return false;
                        }
                        std::uint32_t distExtra = 0u;
                        if (!br.ReadBits(kDeflateDistExtra[distSym], distExtra)) {
                            return false;
                        }
                        const std::size_t distance = static_cast<std::size_t>(kDeflateDistBase[distSym]) + distExtra;
                        if (distance == 0u || distance > outData.size()) {
                            return false;
                        }

                        const std::size_t dstPos = outData.size();
                        outData.resize(dstPos + matchLen);
                        std::uint8_t* dstPtr = outData.data() + dstPos;
                        const std::uint8_t* srcPtr = dstPtr - distance;
                        if (distance >= matchLen) {
                            std::memcpy(dstPtr, srcPtr, matchLen);
                        } else {
                            for (std::size_t i = 0u; i < matchLen; ++i) {
                                dstPtr[i] = srcPtr[i];
                            }
                        }
                    } else {
                        return false;
                    }
                }
            } else {
                return false;
            }
        }

        br.AlignToByte();
        if (outConsumedBytes) {
            *outConsumedBytes = br.EffectiveConsumedBytes();
        }
        return true;
    } catch (...) {
        outData.clear();
        return false;
    }
}

bool DecompressZlib(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData,
    std::size_t maxOutputBytes) noexcept {
    if (!src || srcSize < 6u) {
        return false;
    }
    const std::uint8_t cmf = src[0];
    const std::uint8_t flg = src[1];
    if ((cmf & 0x0Fu) != 8u || ((cmf >> 4u) & 0x0Fu) > 7u) {
        return false;
    }
    if (((static_cast<std::uint32_t>(cmf) << 8u) + static_cast<std::uint32_t>(flg)) % 31u != 0u) {
        return false;
    }
    std::size_t headerBytes = 2u;
    if ((flg & 0x20u) != 0u) {
        headerBytes += 4u;
    }
    if (srcSize < headerBytes + 4u) {
        return false;
    }

    std::size_t consumed = 0u;
    if (!InflateRfc1951(src + headerBytes, srcSize - headerBytes, outData, &consumed, maxOutputBytes)) {
        return false;
    }
    if (consumed > (srcSize - headerBytes) || (srcSize - headerBytes - consumed) < 4u) {
        return false;
    }
    const std::uint32_t expectedAdler = ReadBe32(src + headerBytes + consumed);
    const std::uint32_t actualAdler = ComputeAdler32(outData.data(), outData.size());
    return expectedAdler == actualAdler;
}

ImageFormat DetectImageFormat(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < 4u) {
        return ImageFormat::Unknown;
    }
    if (size >= 8u &&
        data[0] == 0x89u && data[1] == 0x50u && data[2] == 0x4Eu && data[3] == 0x47u &&
        data[4] == 0x0Du && data[5] == 0x0Au && data[6] == 0x1Au && data[7] == 0x0Au) {
        return ImageFormat::Png;
    }
    if (data[0] == 0xFFu && data[1] == 0xD8u && data[2] == 0xFFu) {
        return ImageFormat::Jpeg;
    }
    if (data[0] == 'B' && data[1] == 'M' && size >= 26u) {
        return ImageFormat::Bmp;
    }
    if (size >= 18u) {
        const std::uint8_t colorMapType = data[1];
        const std::uint8_t imgType = data[2];
        const std::uint8_t bpp = data[16];
        if ((colorMapType == 0u || colorMapType == 1u) &&
            (imgType == 2u || imgType == 3u || imgType == 10u || imgType == 11u) &&
            (bpp == 8u || bpp == 24u || bpp == 32u)) {
            return ImageFormat::Tga;
        }
    }
    return ImageFormat::Unknown;
}

bool DecodePngFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError) {
    outImage = DecodedImage{};
    if (!data || size < 33u || DetectImageFormat(data, size) != ImageFormat::Png) {
        SetError(outError, "Invalid PNG signature or truncated file.");
        return false;
    }

    bool seenIhdr = false;
    bool seenIend = false;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint8_t bitDepth = 0u;
    std::uint8_t colorType = 0u;
    std::uint8_t interlace = 0u;

    std::array<std::uint8_t, 256u> palR{};
    std::array<std::uint8_t, 256u> palG{};
    std::array<std::uint8_t, 256u> palB{};
    std::array<std::uint8_t, 256u> palA{};
    palA.fill(255u);
    std::uint32_t paletteEntries = 0u;

    bool hasKeyTransparency = false;
    std::uint16_t transGray = 0u;
    std::uint16_t transR = 0u;
    std::uint16_t transG = 0u;
    std::uint16_t transB = 0u;

    std::vector<std::uint8_t> idatCompressed;
    idatCompressed.reserve(size);

    std::size_t pos = 8u;
    while (pos + 12u <= size) {
        const std::uint32_t chunkLen = ReadBe32(data + pos);
        const std::uint8_t* typePtr = data + pos + 4u;
        pos += 8u;
        if (chunkLen > size - pos || (size - pos - chunkLen) < 4u) {
            SetError(outError, "Truncated PNG chunk.");
            return false;
        }
        const std::uint8_t* chunkData = data + pos;

        if (std::memcmp(typePtr, "IHDR", 4u) == 0) {
            if (seenIhdr || chunkLen < 13u) {
                SetError(outError, "Malformed PNG IHDR chunk.");
                return false;
            }
            width = ReadBe32(chunkData + 0u);
            height = ReadBe32(chunkData + 4u);
            bitDepth = chunkData[8];
            colorType = chunkData[9];
            const std::uint8_t compMethod = chunkData[10];
            const std::uint8_t filterMethod = chunkData[11];
            interlace = chunkData[12];

            if (width == 0u || height == 0u || width > 8192u || height > 8192u ||
                static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL > 128ULL * 1024ULL * 1024ULL ||
                compMethod != 0u || filterMethod != 0u || interlace > 1u) {
                SetError(outError, "Unsupported PNG IHDR parameters.");
                return false;
            }
            const bool depthOk =
                (colorType == 0u && (bitDepth == 1u || bitDepth == 2u || bitDepth == 4u || bitDepth == 8u || bitDepth == 16u)) ||
                (colorType == 2u && (bitDepth == 8u || bitDepth == 16u)) ||
                (colorType == 3u && (bitDepth == 1u || bitDepth == 2u || bitDepth == 4u || bitDepth == 8u)) ||
                (colorType == 4u && (bitDepth == 8u || bitDepth == 16u)) ||
                (colorType == 6u && (bitDepth == 8u || bitDepth == 16u));
            if (!depthOk) {
                SetError(outError, "Invalid PNG colorType/bitDepth combination.");
                return false;
            }
            seenIhdr = true;
        } else if (std::memcmp(typePtr, "PLTE", 4u) == 0) {
            if (chunkLen % 3u != 0u || chunkLen > 768u) {
                SetError(outError, "Malformed PNG PLTE chunk.");
                return false;
            }
            paletteEntries = chunkLen / 3u;
            for (std::uint32_t i = 0u; i < paletteEntries; ++i) {
                palR[i] = chunkData[i * 3u + 0u];
                palG[i] = chunkData[i * 3u + 1u];
                palB[i] = chunkData[i * 3u + 2u];
            }
        } else if (std::memcmp(typePtr, "tRNS", 4u) == 0) {
            if (colorType == 3u) {
                const std::uint32_t n = std::min<std::uint32_t>(chunkLen, 256u);
                for (std::uint32_t i = 0u; i < n; ++i) {
                    palA[i] = chunkData[i];
                }
            } else if (colorType == 0u && chunkLen >= 2u) {
                hasKeyTransparency = true;
                transGray = ReadBe16(chunkData);
            } else if (colorType == 2u && chunkLen >= 6u) {
                hasKeyTransparency = true;
                transR = ReadBe16(chunkData + 0u);
                transG = ReadBe16(chunkData + 2u);
                transB = ReadBe16(chunkData + 4u);
            }
        } else if (std::memcmp(typePtr, "IDAT", 4u) == 0) {
            idatCompressed.insert(idatCompressed.end(), chunkData, chunkData + chunkLen);
        } else if (std::memcmp(typePtr, "IEND", 4u) == 0) {
            seenIend = true;
            break;
        }

        pos += static_cast<std::size_t>(chunkLen) + 4u; // skip CRC
    }

    if (!seenIhdr || idatCompressed.empty() || (colorType == 3u && paletteEntries == 0u)) {
        (void)seenIend;
        SetError(outError, "Incomplete PNG data (missing IHDR, PLTE, or IDAT).");
        return false;
    }

    const std::uint32_t channels =
        (colorType == 0u) ? 1u :
        (colorType == 2u) ? 3u :
        (colorType == 3u) ? 1u :
        (colorType == 4u) ? 2u : 4u;
    const std::uint64_t bitsPerPixel =
        static_cast<std::uint64_t>(channels) * static_cast<std::uint64_t>(bitDepth);
    const std::size_t filterBpp =
        static_cast<std::size_t>(std::max<std::uint64_t>(1ULL, (bitsPerPixel + 7ULL) / 8ULL));

    constexpr std::uint64_t kMaxPngBytes = 128ULL * 1024ULL * 1024ULL;
    const std::uint64_t rgbaBytes64 =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (width > 8192u || height > 8192u || rgbaBytes64 > kMaxPngBytes) {
        SetError(outError, "PNG dimensions exceed maximum safe decode limits.");
        return false;
    }

    std::uint64_t expectedDecompressedBytes = 0ULL;
    if (interlace == 0u) {
        const std::uint64_t rowBytes64 =
            (static_cast<std::uint64_t>(width) * bitsPerPixel + 7ULL) / 8ULL;
        expectedDecompressedBytes = static_cast<std::uint64_t>(height) * (1ULL + rowBytes64);
    } else {
        constexpr std::uint32_t kStartX[7] = {0, 4, 0, 2, 0, 1, 0};
        constexpr std::uint32_t kStartY[7] = {0, 0, 4, 0, 2, 0, 1};
        constexpr std::uint32_t kStepX[7]  = {8, 8, 4, 4, 2, 2, 1};
        constexpr std::uint32_t kStepY[7]  = {8, 8, 8, 4, 4, 2, 2};
        for (int pass = 0; pass < 7; ++pass) {
            if (kStartX[pass] >= width || kStartY[pass] >= height) {
                continue;
            }
            const std::uint64_t subW =
                (static_cast<std::uint64_t>(width - kStartX[pass]) + kStepX[pass] - 1ULL) / kStepX[pass];
            const std::uint64_t subH =
                (static_cast<std::uint64_t>(height - kStartY[pass]) + kStepY[pass] - 1ULL) / kStepY[pass];
            const std::uint64_t rowBytes64 = (subW * bitsPerPixel + 7ULL) / 8ULL;
            expectedDecompressedBytes += subH * (1ULL + rowBytes64);
        }
    }
    if (expectedDecompressedBytes == 0ULL || expectedDecompressedBytes > kMaxPngBytes) {
        SetError(outError, "PNG scanline buffer exceeds maximum safe decode limits.");
        return false;
    }

    std::vector<std::uint8_t> rawDecompressed;
    if (!DecompressZlib(
            idatCompressed.data(),
            idatCompressed.size(),
            rawDecompressed,
            static_cast<std::size_t>(expectedDecompressedBytes))) {
        SetError(outError, "Failed to decompress PNG zlib IDAT stream.");
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.format = ImageFormat::Png;
    outImage.rgbaPixels.assign(static_cast<std::size_t>(rgbaBytes64), 0u);

    auto unpackScanlineToRgba = [&](const std::uint8_t* scanRow,
                                    std::uint32_t subWidth,
                                    std::uint32_t dstStartX,
                                    std::uint32_t dstStepX,
                                    std::uint32_t dstY) noexcept {
        for (std::uint32_t sx = 0u; sx < subWidth; ++sx) {
            const std::uint32_t dx = dstStartX + sx * dstStepX;
            std::uint8_t* dst = outImage.rgbaPixels.data() +
                                (static_cast<std::size_t>(dstY) * width + dx) * 4u;
            if (colorType == 6u) {
                if (bitDepth == 8u) {
                    const std::uint8_t* p = scanRow + sx * 4u;
                    dst[0] = p[0];
                    dst[1] = p[1];
                    dst[2] = p[2];
                    dst[3] = p[3];
                } else {
                    const std::uint8_t* p = scanRow + sx * 8u;
                    dst[0] = p[0];
                    dst[1] = p[2];
                    dst[2] = p[4];
                    dst[3] = p[6];
                }
            } else if (colorType == 2u) {
                if (bitDepth == 8u) {
                    const std::uint8_t* p = scanRow + sx * 3u;
                    const bool keyMatch = hasKeyTransparency &&
                        p[0] == static_cast<std::uint8_t>(transR & 0xFFu) &&
                        p[1] == static_cast<std::uint8_t>(transG & 0xFFu) &&
                        p[2] == static_cast<std::uint8_t>(transB & 0xFFu);
                    dst[0] = p[0];
                    dst[1] = p[1];
                    dst[2] = p[2];
                    dst[3] = keyMatch ? 0u : 255u;
                } else {
                    const std::uint8_t* p = scanRow + sx * 6u;
                    const std::uint16_t r16 = ReadBe16(p + 0u);
                    const std::uint16_t g16 = ReadBe16(p + 2u);
                    const std::uint16_t b16 = ReadBe16(p + 4u);
                    const bool keyMatch = hasKeyTransparency &&
                        r16 == transR && g16 == transG && b16 == transB;
                    dst[0] = p[0];
                    dst[1] = p[2];
                    dst[2] = p[4];
                    dst[3] = keyMatch ? 0u : 255u;
                }
            } else if (colorType == 3u) {
                const std::uint8_t idx = (bitDepth == 8u)
                    ? scanRow[sx]
                    : ExtractSubByteSample(scanRow, sx, bitDepth);
                if (idx < paletteEntries) {
                    dst[0] = palR[idx];
                    dst[1] = palG[idx];
                    dst[2] = palB[idx];
                    dst[3] = palA[idx];
                } else {
                    dst[0] = 0u;
                    dst[1] = 0u;
                    dst[2] = 0u;
                    dst[3] = 255u;
                }
            } else if (colorType == 0u) {
                if (bitDepth == 8u) {
                    const std::uint8_t g = scanRow[sx];
                    const bool keyMatch = hasKeyTransparency && g == static_cast<std::uint8_t>(transGray & 0xFFu);
                    dst[0] = g;
                    dst[1] = g;
                    dst[2] = g;
                    dst[3] = keyMatch ? 0u : 255u;
                } else if (bitDepth == 16u) {
                    const std::uint16_t g16 = ReadBe16(scanRow + sx * 2u);
                    const std::uint8_t g = scanRow[sx * 2u];
                    const bool keyMatch = hasKeyTransparency && g16 == transGray;
                    dst[0] = g;
                    dst[1] = g;
                    dst[2] = g;
                    dst[3] = keyMatch ? 0u : 255u;
                } else {
                    const std::uint8_t rawSample = ExtractSubByteSample(scanRow, sx, bitDepth);
                    const std::uint32_t maxVal = (1u << bitDepth) - 1u;
                    const auto g = static_cast<std::uint8_t>((rawSample * 255u + (maxVal >> 1u)) / maxVal);
                    const bool keyMatch = hasKeyTransparency && rawSample == (transGray & maxVal);
                    dst[0] = g;
                    dst[1] = g;
                    dst[2] = g;
                    dst[3] = keyMatch ? 0u : 255u;
                }
            } else if (colorType == 4u) {
                if (bitDepth == 8u) {
                    const std::uint8_t g = scanRow[sx * 2u + 0u];
                    const std::uint8_t a = scanRow[sx * 2u + 1u];
                    dst[0] = g;
                    dst[1] = g;
                    dst[2] = g;
                    dst[3] = a;
                } else {
                    const std::uint8_t g = scanRow[sx * 4u + 0u];
                    const std::uint8_t a = scanRow[sx * 4u + 2u];
                    dst[0] = g;
                    dst[1] = g;
                    dst[2] = g;
                    dst[3] = a;
                }
            }
        }
    };

    std::size_t streamCursor = 0u;
    if (interlace == 0u) {
        const std::size_t rowBytes = (static_cast<std::size_t>(width) * bitsPerPixel + 7u) / 8u;
        const std::size_t needed = static_cast<std::size_t>(height) * (1u + rowBytes);
        if (rawDecompressed.size() < needed) {
            SetError(outError, "Decompressed PNG IDAT smaller than expected scanlines.");
            return false;
        }
        std::vector<std::uint8_t> prevRow(rowBytes, 0u);
        std::vector<std::uint8_t> curRow(rowBytes, 0u);
        for (std::uint32_t y = 0u; y < height; ++y) {
            const std::uint8_t filter = rawDecompressed[streamCursor++];
            const std::uint8_t* srcRow = rawDecompressed.data() + streamCursor;
            streamCursor += rowBytes;
            if (!UnfilterPngRow(filter, srcRow, (y > 0u) ? prevRow.data() : nullptr, curRow.data(), rowBytes, filterBpp)) {
                SetError(outError, "Invalid PNG scanline filter type.");
                return false;
            }
            unpackScanlineToRgba(curRow.data(), width, 0u, 1u, y);
            prevRow.swap(curRow);
        }
    } else {
        // Adam7 7-pass interlaced PNG reconstruction
        constexpr std::uint32_t kStartX[7] = {0, 4, 0, 2, 0, 1, 0};
        constexpr std::uint32_t kStartY[7] = {0, 0, 4, 0, 2, 0, 1};
        constexpr std::uint32_t kStepX[7]  = {8, 8, 4, 4, 2, 2, 1};
        constexpr std::uint32_t kStepY[7]  = {8, 8, 8, 4, 4, 2, 2};
        for (int pass = 0; pass < 7; ++pass) {
            if (kStartX[pass] >= width || kStartY[pass] >= height) {
                continue;
            }
            const std::uint32_t subW = (width - kStartX[pass] + kStepX[pass] - 1u) / kStepX[pass];
            const std::uint32_t subH = (height - kStartY[pass] + kStepY[pass] - 1u) / kStepY[pass];
            const std::size_t rowBytes = (static_cast<std::size_t>(subW) * bitsPerPixel + 7u) / 8u;
            if (streamCursor + static_cast<std::size_t>(subH) * (1u + rowBytes) > rawDecompressed.size()) {
                SetError(outError, "Truncated Adam7 interlaced PNG scanlines.");
                return false;
            }
            std::vector<std::uint8_t> prevRow(rowBytes, 0u);
            std::vector<std::uint8_t> curRow(rowBytes, 0u);
            for (std::uint32_t sy = 0u; sy < subH; ++sy) {
                const std::uint8_t filter = rawDecompressed[streamCursor++];
                const std::uint8_t* srcRow = rawDecompressed.data() + streamCursor;
                streamCursor += rowBytes;
                if (!UnfilterPngRow(filter, srcRow, (sy > 0u) ? prevRow.data() : nullptr, curRow.data(), rowBytes, filterBpp)) {
                    SetError(outError, "Invalid PNG Adam7 filter type.");
                    return false;
                }
                unpackScanlineToRgba(curRow.data(), subW, kStartX[pass], kStepX[pass], kStartY[pass] + sy * kStepY[pass]);
                prevRow.swap(curRow);
            }
        }
    }

    return true;
}

bool DecodeJpegFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError) {
    outImage = DecodedImage{};
    if (!data || size < 4u || data[0] != 0xFFu || data[1] != 0xD8u) {
        SetError(outError, "Invalid JPEG SOI marker.");
        return false;
    }

    struct JpegComponent {
        std::uint8_t id = 0u;
        std::uint8_t hSamp = 1u;
        std::uint8_t vSamp = 1u;
        std::uint8_t qTableId = 0u;
        std::uint8_t dcTableId = 0u;
        std::uint8_t acTableId = 0u;
        std::uint32_t blocksX = 0u;
        std::uint32_t blocksY = 0u;
        std::uint32_t planeStride = 0u;
        std::uint32_t planeHeight = 0u;
        std::vector<std::uint8_t> planePixels;
        std::vector<std::int16_t> progCoeffs;
    };

    std::array<std::array<std::int32_t, 64>, 4> qTables{};
    std::array<bool, 4> qTableValid{};
    std::array<JpegHuffmanTable, 4> dcTables{};
    std::array<JpegHuffmanTable, 4> acTables{};

    bool seenSof = false;
    bool isProgressive = false;
    bool decodedScan = false;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t numComp = 0u;
    std::uint32_t maxHSamp = 1u;
    std::uint32_t maxVSamp = 1u;
    std::uint32_t mcuCols = 0u;
    std::uint32_t mcuRows = 0u;
    std::uint32_t restartInterval = 0u;
    std::array<JpegComponent, 3> comps{};

    std::size_t pos = 2u;
    while (pos < size) {
        if (data[pos] != 0xFFu) {
            ++pos;
            continue;
        }
        while (pos < size && data[pos] == 0xFFu) {
            ++pos;
        }
        if (pos >= size) {
            break;
        }
        const std::uint8_t marker = data[pos++];
        if (marker == 0xD9u) { // EOI
            break;
        }
        if (marker == 0x00u || marker == 0x01u || (marker >= 0xD0u && marker <= 0xD7u)) {
            continue;
        }
        if (pos + 2u > size) {
            SetError(outError, "Truncated JPEG marker length.");
            return false;
        }
        const std::uint16_t segLen = ReadBe16(data + pos);
        if (segLen < 2u || pos + segLen > size) {
            SetError(outError, "Invalid JPEG segment length.");
            return false;
        }
        const std::uint8_t* segData = data + pos + 2u;
        const std::size_t payloadLen = static_cast<std::size_t>(segLen) - 2u;
        pos += segLen;

        if (marker == 0xDBu) { // DQT
            std::size_t qPos = 0u;
            while (qPos < payloadLen) {
                const std::uint8_t info = segData[qPos++];
                const std::uint8_t pq = static_cast<std::uint8_t>((info >> 4u) & 0x0Fu);
                const std::uint8_t tq = static_cast<std::uint8_t>(info & 0x0Fu);
                if (tq >= 4u || pq > 1u) {
                    SetError(outError, "Invalid JPEG DQT table id/precision.");
                    return false;
                }
                const std::size_t tableBytes = (pq == 0u) ? 64u : 128u;
                if (qPos + tableBytes > payloadLen) {
                    SetError(outError, "Truncated JPEG DQT table.");
                    return false;
                }
                for (std::size_t k = 0u; k < 64u; ++k) {
                    const std::int32_t qv = (pq == 0u)
                        ? static_cast<std::int32_t>(segData[qPos + k])
                        : static_cast<std::int32_t>(ReadBe16(segData + qPos + k * 2u));
                    qTables[tq][kJpegZigZag[k]] = qv;
                }
                qTableValid[tq] = true;
                qPos += tableBytes;
            }
        } else if (marker == 0xC4u) { // DHT
            std::size_t hPos = 0u;
            while (hPos < payloadLen) {
                if (hPos + 17u > payloadLen) {
                    SetError(outError, "Truncated JPEG DHT header.");
                    return false;
                }
                const std::uint8_t info = segData[hPos++];
                const std::uint8_t tc = static_cast<std::uint8_t>((info >> 4u) & 0x0Fu);
                const std::uint8_t th = static_cast<std::uint8_t>(info & 0x0Fu);
                if (tc > 1u || th >= 4u) {
                    SetError(outError, "Invalid JPEG DHT class/id.");
                    return false;
                }
                const std::uint8_t* counts = segData + hPos;
                hPos += 16u;
                std::size_t totalSyms = 0u;
                for (std::size_t i = 0u; i < 16u; ++i) {
                    totalSyms += counts[i];
                }
                if (hPos + totalSyms > payloadLen) {
                    SetError(outError, "Truncated JPEG DHT symbols.");
                    return false;
                }
                JpegHuffmanTable& target = (tc == 0u) ? dcTables[th] : acTables[th];
                if (!target.Build(counts, segData + hPos, totalSyms)) {
                    SetError(outError, "Failed to build JPEG Huffman table.");
                    return false;
                }
                hPos += totalSyms;
            }
        } else if (marker == 0xDDu) { // DRI
            if (payloadLen >= 2u) {
                restartInterval = ReadBe16(segData);
            }
        } else if (marker == 0xC0u || marker == 0xC1u || marker == 0xC2u) { // SOF0 / SOF1 / SOF2
            if (seenSof || payloadLen < 6u) {
                SetError(outError, "Malformed or duplicate JPEG SOF marker.");
                return false;
            }
            isProgressive = (marker == 0xC2u);
            const std::uint8_t precision = segData[0];
            height = ReadBe16(segData + 1u);
            width = ReadBe16(segData + 3u);
            numComp = segData[5];
            if (precision != 8u || width == 0u || height == 0u || width > 8192u || height > 8192u ||
                static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 64ULL * 1024ULL * 1024ULL ||
                (numComp != 1u && numComp != 3u) || payloadLen < 6u + numComp * 3u) {
                SetError(outError, "Unsupported JPEG frame parameters.");
                return false;
            }
            maxHSamp = 1u;
            maxVSamp = 1u;
            for (std::uint32_t c = 0u; c < numComp; ++c) {
                comps[c].id = segData[6u + c * 3u + 0u];
                const std::uint8_t samp = segData[6u + c * 3u + 1u];
                comps[c].hSamp = static_cast<std::uint8_t>((samp >> 4u) & 0x0Fu);
                comps[c].vSamp = static_cast<std::uint8_t>(samp & 0x0Fu);
                comps[c].qTableId = segData[6u + c * 3u + 2u];
                if (comps[c].hSamp == 0u || comps[c].hSamp > 4u ||
                    comps[c].vSamp == 0u || comps[c].vSamp > 4u ||
                    comps[c].qTableId >= 4u) {
                    SetError(outError, "Invalid JPEG component sampling factors.");
                    return false;
                }
                maxHSamp = std::max<std::uint32_t>(maxHSamp, comps[c].hSamp);
                maxVSamp = std::max<std::uint32_t>(maxVSamp, comps[c].vSamp);
            }
            const std::uint32_t mcuW = maxHSamp * 8u;
            const std::uint32_t mcuH = maxVSamp * 8u;
            mcuCols = (width + mcuW - 1u) / mcuW;
            mcuRows = (height + mcuH - 1u) / mcuH;

            for (std::uint32_t c = 0u; c < numComp; ++c) {
                comps[c].blocksX = mcuCols * comps[c].hSamp;
                comps[c].blocksY = mcuRows * comps[c].vSamp;
                comps[c].planeStride = comps[c].blocksX * 8u;
                comps[c].planeHeight = comps[c].blocksY * 8u;
                comps[c].planePixels.assign(
                    static_cast<std::size_t>(comps[c].planeStride) * comps[c].planeHeight, 128u);
                if (isProgressive) {
                    comps[c].progCoeffs.assign(
                        static_cast<std::size_t>(comps[c].blocksX) * comps[c].blocksY * 64u, 0);
                }
            }
            seenSof = true;
        } else if (marker == 0xDAu) { // SOS
            if (!seenSof || payloadLen < 4u) {
                SetError(outError, "JPEG SOS encountered before SOF.");
                return false;
            }
            const std::uint8_t scanCompCount = segData[0];
            if (scanCompCount == 0u || scanCompCount > numComp ||
                payloadLen < 1u + static_cast<std::size_t>(scanCompCount) * 2u + 3u) {
                SetError(outError, "Malformed JPEG SOS header.");
                return false;
            }

            std::array<std::uint32_t, 3> scanCompIndices{};
            for (std::uint8_t i = 0u; i < scanCompCount; ++i) {
                const std::uint8_t cid = segData[1u + i * 2u];
                const std::uint8_t tdta = segData[1u + i * 2u + 1u];
                bool matched = false;
                for (std::uint32_t c = 0u; c < numComp; ++c) {
                    if (comps[c].id == cid) {
                        comps[c].dcTableId = static_cast<std::uint8_t>((tdta >> 4u) & 0x0Fu);
                        comps[c].acTableId = static_cast<std::uint8_t>(tdta & 0x0Fu);
                        if (comps[c].dcTableId >= 4u || comps[c].acTableId >= 4u) {
                            SetError(outError, "Invalid JPEG Huffman selector in SOS.");
                            return false;
                        }
                        scanCompIndices[i] = c;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    SetError(outError, "Unknown component ID in JPEG SOS.");
                    return false;
                }
            }

            const std::size_t specOff = 1u + static_cast<std::size_t>(scanCompCount) * 2u;
            const std::uint8_t ss = segData[specOff + 0u];
            const std::uint8_t se = segData[specOff + 1u];
            const std::uint8_t ahal = segData[specOff + 2u];
            const std::uint8_t ah = static_cast<std::uint8_t>((ahal >> 4u) & 0x0Fu);
            const std::uint8_t al = static_cast<std::uint8_t>(ahal & 0x0Fu);
            if (ss > 63u || se > 63u || ss > se || ah > 13u || al > 13u ||
                (isProgressive && ss == 0u && se != 0u)) {
                SetError(outError, "Invalid JPEG spectral selection range.");
                return false;
            }

            JpegScanBitReader br{data, size, pos, 0u, 0u, false, 0u};
            std::array<int, 3> dcPred{0, 0, 0};
            std::uint32_t eobRun = 0u;
            std::uint32_t mcuCounter = 0u;

            if (!isProgressive) {
                // Baseline / Extended Sequential DCT scan
                for (std::uint32_t cIdx = 0u; cIdx < scanCompCount; ++cIdx) {
                    const JpegComponent& sc = comps[scanCompIndices[cIdx]];
                    if (!qTableValid[sc.qTableId] || !dcTables[sc.dcTableId].valid || !acTables[sc.acTableId].valid) {
                        SetError(outError, "Missing DQT or DHT table required by baseline JPEG scan.");
                        return false;
                    }
                }

                if (scanCompCount == 1u && numComp > 1u) {
                    // Non-interleaved baseline single-component scan
                    JpegComponent& comp = comps[scanCompIndices[0]];
                    const std::uint32_t validBx = (width * comp.hSamp + maxHSamp * 8u - 1u) / (maxHSamp * 8u);
                    const std::uint32_t validBy = (height * comp.vSamp + maxVSamp * 8u - 1u) / (maxVSamp * 8u);
                    const auto& qt = qTables[comp.qTableId];
                    for (std::uint32_t by = 0u; by < validBy; ++by) {
                        for (std::uint32_t bx = 0u; bx < validBx; ++bx) {
                            if (restartInterval > 0u && mcuCounter > 0u && (mcuCounter % restartInterval) == 0u) {
                                br.ConsumeRestartMarker();
                                dcPred = {0, 0, 0};
                            }
                            ++mcuCounter;
                            std::int32_t block[64]{};
                            std::uint8_t dcCat = 0u;
                            if (!br.DecodeSymbol(dcTables[comp.dcTableId], dcCat) || dcCat > 11u) {
                                SetError(outError, "Invalid baseline JPEG DC code.");
                                return false;
                            }
                            dcPred[0] += br.ReceiveExtend(dcCat);
                            block[0] = dcPred[0] * qt[0];
                            std::uint32_t k = 1u;
                            while (k < 64u) {
                                std::uint8_t rs = 0u;
                                if (!br.DecodeSymbol(acTables[comp.acTableId], rs)) {
                                    SetError(outError, "Invalid baseline JPEG AC code.");
                                    return false;
                                }
                                const std::uint32_t r = (rs >> 4u) & 0x0Fu;
                                const std::uint32_t s = rs & 0x0Fu;
                                if (s == 0u) {
                                    if (r == 15u) {
                                        k += 16u;
                                        continue;
                                    }
                                    break;
                                }
                                k += r;
                                if (k >= 64u) {
                                    SetError(outError, "JPEG AC run overflowed 8x8 block.");
                                    return false;
                                }
                                const std::uint8_t natIdx = kJpegZigZag[k];
                                block[natIdx] = br.ReceiveExtend(s) * qt[natIdx];
                                ++k;
                            }
                            std::uint8_t* dstBlock = comp.planePixels.data() +
                                                     (static_cast<std::size_t>(by) * 8u * comp.planeStride) +
                                                     static_cast<std::size_t>(bx) * 8u;
                            InverseDct8x8(block, dstBlock, comp.planeStride);
                        }
                    }
                } else {
                    // Standard interleaved MCU baseline scan
                    for (std::uint32_t my = 0u; my < mcuRows; ++my) {
                        for (std::uint32_t mx = 0u; mx < mcuCols; ++mx) {
                            if (restartInterval > 0u && mcuCounter > 0u && (mcuCounter % restartInterval) == 0u) {
                                br.ConsumeRestartMarker();
                                dcPred = {0, 0, 0};
                            }
                            ++mcuCounter;

                            for (std::uint8_t scIdx = 0u; scIdx < scanCompCount; ++scIdx) {
                                const std::uint32_t c = scanCompIndices[scIdx];
                                JpegComponent& comp = comps[c];
                                const auto& qt = qTables[comp.qTableId];
                                const auto& dcHuff = dcTables[comp.dcTableId];
                                const auto& acHuff = acTables[comp.acTableId];

                                for (std::uint8_t vy = 0u; vy < comp.vSamp; ++vy) {
                                    for (std::uint8_t hx = 0u; hx < comp.hSamp; ++hx) {
                                        std::int32_t block[64]{};
                                        std::uint8_t dcCat = 0u;
                                        if (!br.DecodeSymbol(dcHuff, dcCat) || dcCat > 11u) {
                                            SetError(outError, "Invalid interleaved JPEG DC symbol.");
                                            return false;
                                        }
                                        dcPred[c] += br.ReceiveExtend(dcCat);
                                        block[0] = dcPred[c] * qt[0];

                                        std::uint32_t k = 1u;
                                        while (k < 64u) {
                                            std::uint8_t rs = 0u;
                                            if (!br.DecodeSymbol(acHuff, rs)) {
                                                SetError(outError, "Invalid interleaved JPEG AC symbol.");
                                                return false;
                                            }
                                            const std::uint32_t r = (rs >> 4u) & 0x0Fu;
                                            const std::uint32_t s = rs & 0x0Fu;
                                            if (s == 0u) {
                                                if (r == 15u) {
                                                    k += 16u;
                                                    continue;
                                                }
                                                break;
                                            }
                                            k += r;
                                            if (k >= 64u) {
                                                SetError(outError, "JPEG AC run exceeded 63.");
                                                return false;
                                            }
                                            const std::uint8_t natIdx = kJpegZigZag[k];
                                            block[natIdx] = br.ReceiveExtend(s) * qt[natIdx];
                                            ++k;
                                        }

                                        const std::uint32_t blockX = mx * comp.hSamp + hx;
                                        const std::uint32_t blockY = my * comp.vSamp + vy;
                                        std::uint8_t* dstBlock = comp.planePixels.data() +
                                            (static_cast<std::size_t>(blockY) * 8u * comp.planeStride) +
                                            static_cast<std::size_t>(blockX) * 8u;
                                        InverseDct8x8(block, dstBlock, comp.planeStride);
                                    }
                                }
                            }
                        }
                    }
                }
                decodedScan = true;
            } else {
                // Progressive DCT (SOF2) scan (DC first/refine + AC first/refine)
                auto decodeProgDcBlock = [&](JpegComponent& comp, std::uint32_t c, std::int16_t* blk) -> bool {
                    if (ah == 0u) {
                        std::uint8_t dcCat = 0u;
                        if (!br.DecodeSymbol(dcTables[comp.dcTableId], dcCat) || dcCat > 11u) {
                            return false;
                        }
                        dcPred[c] += br.ReceiveExtend(dcCat);
                        blk[0] = static_cast<std::int16_t>(
                            static_cast<std::uint16_t>(static_cast<std::uint32_t>(dcPred[c])) << al);
                    } else {
                        if (br.ReadBitsMsb(1u) != 0u) {
                            blk[0] = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(blk[0]) | static_cast<std::uint16_t>(1u << al));
                        }
                    }
                    return true;
                };

                if (ss == 0u) {
                    if (scanCompCount == 1u && numComp > 1u) {
                        const std::uint32_t c = scanCompIndices[0];
                        JpegComponent& comp = comps[c];
                        const std::uint32_t validBx = (width * comp.hSamp + maxHSamp * 8u - 1u) / (maxHSamp * 8u);
                        const std::uint32_t validBy = (height * comp.vSamp + maxVSamp * 8u - 1u) / (maxVSamp * 8u);
                        for (std::uint32_t by = 0u; by < validBy; ++by) {
                            for (std::uint32_t bx = 0u; bx < validBx; ++bx) {
                                if (restartInterval > 0u && mcuCounter > 0u && (mcuCounter % restartInterval) == 0u) {
                                    br.ConsumeRestartMarker();
                                    dcPred = {0, 0, 0};
                                }
                                ++mcuCounter;
                                std::int16_t* blk = comp.progCoeffs.data() +
                                    (static_cast<std::size_t>(by) * comp.blocksX + bx) * 64u;
                                if (!decodeProgDcBlock(comp, c, blk)) {
                                    SetError(outError, "Progressive JPEG DC decode failed.");
                                    return false;
                                }
                            }
                        }
                    } else {
                        for (std::uint32_t my = 0u; my < mcuRows; ++my) {
                            for (std::uint32_t mx = 0u; mx < mcuCols; ++mx) {
                                if (restartInterval > 0u && mcuCounter > 0u && (mcuCounter % restartInterval) == 0u) {
                                    br.ConsumeRestartMarker();
                                    dcPred = {0, 0, 0};
                                }
                                ++mcuCounter;
                                for (std::uint8_t scIdx = 0u; scIdx < scanCompCount; ++scIdx) {
                                    const std::uint32_t c = scanCompIndices[scIdx];
                                    JpegComponent& comp = comps[c];
                                    for (std::uint8_t vy = 0u; vy < comp.vSamp; ++vy) {
                                        for (std::uint8_t hx = 0u; hx < comp.hSamp; ++hx) {
                                            const std::uint32_t bx = mx * comp.hSamp + hx;
                                            const std::uint32_t by = my * comp.vSamp + vy;
                                            std::int16_t* blk = comp.progCoeffs.data() +
                                                (static_cast<std::size_t>(by) * comp.blocksX + bx) * 64u;
                                            if (!decodeProgDcBlock(comp, c, blk)) {
                                                SetError(outError, "Progressive JPEG MCU DC decode failed.");
                                                return false;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Progressive AC scan (always 1 component per scan per ITU-T.81)
                    const std::uint32_t c = scanCompIndices[0];
                    JpegComponent& comp = comps[c];
                    const auto& acHuff = acTables[comp.acTableId];
                    const std::uint32_t validBx = (width * comp.hSamp + maxHSamp * 8u - 1u) / (maxHSamp * 8u);
                    const std::uint32_t validBy = (height * comp.vSamp + maxVSamp * 8u - 1u) / (maxVSamp * 8u);
                    const auto bitVal = static_cast<std::int16_t>(static_cast<std::uint16_t>(1u << al));

                    for (std::uint32_t by = 0u; by < validBy; ++by) {
                        for (std::uint32_t bx = 0u; bx < validBx; ++bx) {
                            if (restartInterval > 0u && mcuCounter > 0u && (mcuCounter % restartInterval) == 0u) {
                                br.ConsumeRestartMarker();
                                eobRun = 0u;
                            }
                            ++mcuCounter;
                            std::int16_t* blk = comp.progCoeffs.data() +
                                (static_cast<std::size_t>(by) * comp.blocksX + bx) * 64u;

                            if (ah == 0u) {
                                if (eobRun > 0u) {
                                    --eobRun;
                                    continue;
                                }
                                std::uint32_t k = ss;
                                while (k <= se) {
                                    std::uint8_t rs = 0u;
                                    if (!br.DecodeSymbol(acHuff, rs)) {
                                        SetError(outError, "Progressive JPEG AC first scan decode failed.");
                                        return false;
                                    }
                                    const std::uint32_t r = (rs >> 4u) & 0x0Fu;
                                    const std::uint32_t s = rs & 0x0Fu;
                                    if (s == 0u) {
                                        if (r == 15u) {
                                            k += 16u;
                                            continue;
                                        }
                                        eobRun = (1u << r) + br.ReadBitsMsb(r) - 1u;
                                        break;
                                    }
                                    k += r;
                                    if (k > se) {
                                        SetError(outError, "Progressive JPEG AC run overflow.");
                                        return false;
                                    }
                                    blk[kJpegZigZag[k]] = static_cast<std::int16_t>(
                                        static_cast<std::uint16_t>(static_cast<std::uint32_t>(br.ReceiveExtend(s))) << al);
                                    ++k;
                                }
                            } else {
                                std::uint32_t k = ss;
                                if (eobRun == 0u) {
                                    while (k <= se) {
                                        std::uint8_t rs = 0u;
                                        if (!br.DecodeSymbol(acHuff, rs)) {
                                            SetError(outError, "Progressive JPEG AC refine decode failed.");
                                            return false;
                                        }
                                        std::int32_t r = static_cast<std::int32_t>((rs >> 4u) & 0x0Fu);
                                        const std::uint32_t s = rs & 0x0Fu;
                                        std::int16_t newCoeff = 0;
                                        if (s == 0u) {
                                            if (r < 15) {
                                                eobRun = (1u << static_cast<std::uint32_t>(r)) +
                                                         br.ReadBitsMsb(static_cast<std::uint32_t>(r));
                                                break;
                                            }
                                        } else if (s == 1u) {
                                            newCoeff = (br.ReadBitsMsb(1u) != 0u) ? bitVal : static_cast<std::int16_t>(-bitVal);
                                        } else {
                                            SetError(outError, "Invalid progressive AC refine amplitude.");
                                            return false;
                                        }

                                        while (k <= se) {
                                            std::int16_t& coef = blk[kJpegZigZag[k]];
                                            if (coef != 0) {
                                                if (br.ReadBitsMsb(1u) != 0u) {
                                                    if ((coef & bitVal) == 0) {
                                                        coef = static_cast<std::int16_t>(coef + ((coef > 0) ? bitVal : -bitVal));
                                                    }
                                                }
                                            } else {
                                                if (r == 0) {
                                                    break;
                                                }
                                                --r;
                                            }
                                            ++k;
                                        }
                                        if (newCoeff != 0 && k <= se) {
                                            blk[kJpegZigZag[k]] = newCoeff;
                                        }
                                        ++k;
                                    }
                                }
                                if (eobRun > 0u) {
                                    while (k <= se) {
                                        std::int16_t& coef = blk[kJpegZigZag[k]];
                                        if (coef != 0 && br.ReadBitsMsb(1u) != 0u && (coef & bitVal) == 0) {
                                            coef = static_cast<std::int16_t>(coef + ((coef > 0) ? bitVal : -bitVal));
                                        }
                                        ++k;
                                    }
                                    --eobRun;
                                }
                            }
                        }
                    }
                }
                decodedScan = true;
            }

            br.SyncEndOfScan();
            pos = br.pos;
        }
    }

    if (!seenSof || !decodedScan) {
        SetError(outError, "JPEG missing SOF or SOS scan payload.");
        return false;
    }

    if (isProgressive) {
        for (std::uint32_t c = 0u; c < numComp; ++c) {
            JpegComponent& comp = comps[c];
            if (!qTableValid[comp.qTableId]) {
                SetError(outError, "Missing DQT table for progressive JPEG component.");
                return false;
            }
            const auto& qt = qTables[comp.qTableId];
            for (std::uint32_t by = 0u; by < comp.blocksY; ++by) {
                for (std::uint32_t bx = 0u; bx < comp.blocksX; ++bx) {
                    const std::int16_t* srcBlk = comp.progCoeffs.data() +
                        (static_cast<std::size_t>(by) * comp.blocksX + bx) * 64u;
                    std::int32_t dequant[64]{};
                    for (std::size_t i = 0u; i < 64u; ++i) {
                        dequant[i] = static_cast<std::int32_t>(srcBlk[i]) * qt[i];
                    }
                    std::uint8_t* dstBlock = comp.planePixels.data() +
                        (static_cast<std::size_t>(by) * 8u * comp.planeStride) +
                        static_cast<std::size_t>(bx) * 8u;
                    InverseDct8x8(dequant, dstBlock, comp.planeStride);
                }
            }
        }
    }

    outImage.width = width;
    outImage.height = height;
    outImage.format = ImageFormat::Jpeg;
    outImage.rgbaPixels.resize(static_cast<std::size_t>(width) * height * 4u);

    if (numComp == 1u) {
        const JpegComponent& yComp = comps[0];
        for (std::uint32_t y = 0u; y < height; ++y) {
            const std::uint8_t* yRow = yComp.planePixels.data() + static_cast<std::size_t>(y) * yComp.planeStride;
            std::uint8_t* dst = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
            for (std::uint32_t x = 0u; x < width; ++x) {
                const std::uint8_t lum = yRow[x];
                dst[x * 4u + 0u] = lum;
                dst[x * 4u + 1u] = lum;
                dst[x * 4u + 2u] = lum;
                dst[x * 4u + 3u] = 255u;
            }
        }
        return true;
    }

    // 3-component YCbCr -> RGBA8 with bilinear chroma upsampling for 4:2:0 / 4:2:2 / 4:4:4
    const JpegComponent& cY = comps[0];
    const JpegComponent& cCb = comps[1];
    const JpegComponent& cCr = comps[2];

    const bool fullResYCbCr =
        (cY.hSamp == maxHSamp && cY.vSamp == maxVSamp &&
         cCb.hSamp == maxHSamp && cCb.vSamp == maxVSamp &&
         cCr.hSamp == maxHSamp && cCr.vSamp == maxVSamp);

    if (fullResYCbCr) {
        for (std::uint32_t y = 0u; y < height; ++y) {
            const std::uint8_t* yRow = cY.planePixels.data() + static_cast<std::size_t>(y) * cY.planeStride;
            const std::uint8_t* cbRow = cCb.planePixels.data() + static_cast<std::size_t>(y) * cCb.planeStride;
            const std::uint8_t* crRow = cCr.planePixels.data() + static_cast<std::size_t>(y) * cCr.planeStride;
            std::uint8_t* dst = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
            for (std::uint32_t x = 0u; x < width; ++x) {
                const int yv = static_cast<int>(yRow[x]);
                const int cb = static_cast<int>(cbRow[x]) - 128;
                const int cr = static_cast<int>(crRow[x]) - 128;
                dst[x * 4u + 0u] = ClampIntToByte(yv + ((91881 * cr + 32768) >> 16));
                dst[x * 4u + 1u] = ClampIntToByte(yv - ((22554 * cb + 46802 * cr + 32768) >> 16));
                dst[x * 4u + 2u] = ClampIntToByte(yv + ((116130 * cb + 32768) >> 16));
                dst[x * 4u + 3u] = 255u;
            }
        }
        return true;
    }

    auto sampleSubsampledPlane = [&](const JpegComponent& comp, std::uint32_t px, std::uint32_t py) noexcept -> int {
        if (comp.hSamp == maxHSamp && comp.vSamp == maxVSamp) {
            return static_cast<int>(comp.planePixels[static_cast<std::size_t>(py) * comp.planeStride + px]);
        }
        const float scaleX = static_cast<float>(comp.hSamp) / static_cast<float>(maxHSamp);
        const float scaleY = static_cast<float>(comp.vSamp) / static_cast<float>(maxVSamp);
        const float sx = (static_cast<float>(px) + 0.5f) * scaleX - 0.5f;
        const float sy = (static_cast<float>(py) + 0.5f) * scaleY - 0.5f;
        const int maxCx = static_cast<int>((width * comp.hSamp + maxHSamp - 1u) / maxHSamp) - 1;
        const int maxCy = static_cast<int>((height * comp.vSamp + maxVSamp - 1u) / maxVSamp) - 1;
        const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, std::max(0, maxCx));
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, std::max(0, maxCy));
        const int x1 = std::clamp(x0 + 1, 0, std::max(0, maxCx));
        const int y1 = std::clamp(y0 + 1, 0, std::max(0, maxCy));
        const float fx = std::clamp(sx - std::floor(sx), 0.0f, 1.0f);
        const float fy = std::clamp(sy - std::floor(sy), 0.0f, 1.0f);

        const std::uint8_t* r0 = comp.planePixels.data() + static_cast<std::size_t>(y0) * comp.planeStride;
        const std::uint8_t* r1 = comp.planePixels.data() + static_cast<std::size_t>(y1) * comp.planeStride;
        const float top = static_cast<float>(r0[x0]) + (static_cast<float>(r0[x1]) - static_cast<float>(r0[x0])) * fx;
        const float bot = static_cast<float>(r1[x0]) + (static_cast<float>(r1[x1]) - static_cast<float>(r1[x0])) * fx;
        return static_cast<int>(top + (bot - top) * fy + 0.5f);
    };

    for (std::uint32_t y = 0u; y < height; ++y) {
        std::uint8_t* dst = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0u; x < width; ++x) {
            const int yv = sampleSubsampledPlane(cY, x, y);
            const int cb = sampleSubsampledPlane(cCb, x, y) - 128;
            const int cr = sampleSubsampledPlane(cCr, x, y) - 128;
            dst[x * 4u + 0u] = ClampIntToByte(yv + ((91881 * cr + 32768) >> 16));
            dst[x * 4u + 1u] = ClampIntToByte(yv - ((22554 * cb + 46802 * cr + 32768) >> 16));
            dst[x * 4u + 2u] = ClampIntToByte(yv + ((116130 * cb + 32768) >> 16));
            dst[x * 4u + 3u] = 255u;
        }
    }
    return true;
}

bool DecodeBmpFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError) {
    outImage = DecodedImage{};
    if (!data || size < 54u || data[0] != 'B' || data[1] != 'M') {
        SetError(outError, "Invalid BMP header.");
        return false;
    }

    const std::uint32_t dataOffset = ReadLe32(data + 10u);
    const std::uint32_t dibSize = ReadLe32(data + 14u);
    if (dibSize < 40u ||
        14ULL + static_cast<std::uint64_t>(dibSize) > static_cast<std::uint64_t>(size) ||
        static_cast<std::uint64_t>(dataOffset) >= static_cast<std::uint64_t>(size)) {
        SetError(outError, "Unsupported or truncated BMP DIB header.");
        return false;
    }

    const auto rawW = static_cast<std::int32_t>(ReadLe32(data + 18u));
    const std::uint32_t rawHu = ReadLe32(data + 22u);
    const auto rawH = static_cast<std::int32_t>(rawHu);
    const std::uint16_t planes = ReadLe16(data + 26u);
    const std::uint16_t bpp = ReadLe16(data + 28u);
    const std::uint32_t compression = ReadLe32(data + 30u);
    const std::uint32_t clrUsed = ReadLe32(data + 46u);

    const bool topDown = (rawH < 0);
    const std::uint32_t absH = topDown ? (0u - rawHu) : rawHu;
    if (rawW <= 0 || absH == 0u || rawW > 8192 || absH > 8192u || planes != 1u ||
        (compression != 0u && compression != 3u) ||
        (bpp != 8u && bpp != 16u && bpp != 24u && bpp != 32u)) {
        SetError(outError, "Unsupported BMP dimensions, bit depth, or compression.");
        return false;
    }

    const auto width = static_cast<std::uint32_t>(rawW);
    const auto height = absH;
    const std::uint64_t rowStride64 = ((static_cast<std::uint64_t>(width) * bpp + 31ULL) / 32ULL) * 4ULL;
    if (static_cast<std::uint64_t>(dataOffset) + rowStride64 * static_cast<std::uint64_t>(height) >
        static_cast<std::uint64_t>(size)) {
        SetError(outError, "Truncated BMP pixel array.");
        return false;
    }
    const auto rowStride = static_cast<std::size_t>(rowStride64);

    std::uint32_t rMask = 0x00FF0000u;
    std::uint32_t gMask = 0x0000FF00u;
    std::uint32_t bMask = 0x000000FFu;
    std::uint32_t aMask = (bpp == 32u) ? 0xFF000000u : 0u;
    if (bpp == 16u) {
        rMask = 0x7C00u;
        gMask = 0x03E0u;
        bMask = 0x001Fu;
        aMask = 0u;
    }
    if (compression == 3u) {
        const std::uint64_t maskOff64 = (dibSize >= 56u)
            ? 54ULL
            : (14ULL + static_cast<std::uint64_t>(dibSize));
        if (maskOff64 + 12ULL > static_cast<std::uint64_t>(size) ||
            (dibSize < 56u && maskOff64 + 12ULL > static_cast<std::uint64_t>(dataOffset))) {
            SetError(outError, "Truncated BMP BI_BITFIELDS masks.");
            return false;
        }
        const auto maskOff = static_cast<std::size_t>(maskOff64);
        rMask = ReadLe32(data + maskOff + 0u);
        gMask = ReadLe32(data + maskOff + 4u);
        bMask = ReadLe32(data + maskOff + 8u);
        if (dibSize >= 56u && maskOff64 + 16ULL <= static_cast<std::uint64_t>(size)) {
            aMask = ReadLe32(data + maskOff + 12u);
        } else {
            aMask = 0u;
        }
    }

    auto extractMaskedByte = [](std::uint32_t val, std::uint32_t mask, std::uint8_t fallback) noexcept -> std::uint8_t {
        if (mask == 0u) {
            return fallback;
        }
        std::uint32_t shift = 0u;
        std::uint32_t m = mask;
        while ((m & 1u) == 0u) {
            m >>= 1u;
            ++shift;
        }
        const std::uint32_t raw = (val & mask) >> shift;
        return static_cast<std::uint8_t>((raw * 255u + (m >> 1u)) / m);
    };

    const std::uint64_t paletteOffset64 = 14ULL + static_cast<std::uint64_t>(dibSize);
    const auto paletteOffset = static_cast<std::size_t>(paletteOffset64);
    const std::uint32_t palCount = (bpp == 8u) ? ((clrUsed > 0u && clrUsed <= 256u) ? clrUsed : 256u) : 0u;
    if (bpp == 8u &&
        (paletteOffset64 + static_cast<std::uint64_t>(palCount) * 4ULL > static_cast<std::uint64_t>(dataOffset) ||
         paletteOffset64 + static_cast<std::uint64_t>(palCount) * 4ULL > static_cast<std::uint64_t>(size))) {
        SetError(outError, "Truncated BMP color table.");
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.format = ImageFormat::Bmp;
    outImage.rgbaPixels.resize(static_cast<std::size_t>(width) * height * 4u);

    bool anyNonZeroAlpha = false;
    for (std::uint32_t y = 0u; y < height; ++y) {
        const std::uint32_t srcY = topDown ? y : (height - 1u - y);
        const std::uint8_t* srcRow = data + dataOffset + static_cast<std::size_t>(srcY) * rowStride;
        std::uint8_t* dstRow = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;

        for (std::uint32_t x = 0u; x < width; ++x) {
            std::uint8_t* dst = dstRow + x * 4u;
            if (bpp == 24u) {
                dst[0] = srcRow[x * 3u + 2u];
                dst[1] = srcRow[x * 3u + 1u];
                dst[2] = srcRow[x * 3u + 0u];
                dst[3] = 255u;
            } else if (bpp == 32u) {
                const std::uint32_t px = ReadLe32(srcRow + x * 4u);
                dst[0] = extractMaskedByte(px, rMask, 0u);
                dst[1] = extractMaskedByte(px, gMask, 0u);
                dst[2] = extractMaskedByte(px, bMask, 0u);
                dst[3] = extractMaskedByte(px, aMask, 255u);
                if (dst[3] != 0u) {
                    anyNonZeroAlpha = true;
                }
            } else if (bpp == 16u) {
                const std::uint16_t px = ReadLe16(srcRow + x * 2u);
                dst[0] = extractMaskedByte(px, rMask, 0u);
                dst[1] = extractMaskedByte(px, gMask, 0u);
                dst[2] = extractMaskedByte(px, bMask, 0u);
                dst[3] = 255u;
            } else {
                const std::uint8_t idx = srcRow[x];
                if (idx < palCount) {
                    const std::uint8_t* pe = data + paletteOffset + static_cast<std::size_t>(idx) * 4u;
                    dst[0] = pe[2];
                    dst[1] = pe[1];
                    dst[2] = pe[0];
                    dst[3] = 255u;
                } else {
                    dst[0] = 0u;
                    dst[1] = 0u;
                    dst[2] = 0u;
                    dst[3] = 255u;
                }
            }
        }
    }

    // Standard 32-bit BI_RGB BMPs written by GDI often leave alpha = 0 across the entire image
    if (bpp == 32u && compression == 0u && !anyNonZeroAlpha) {
        const std::size_t totalPx = static_cast<std::size_t>(width) * height;
        for (std::size_t i = 0u; i < totalPx; ++i) {
            outImage.rgbaPixels[i * 4u + 3u] = 255u;
        }
    }
    return true;
}

bool DecodeTgaFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError) {
    outImage = DecodedImage{};
    if (!data || size < 18u) {
        SetError(outError, "Truncated TGA header.");
        return false;
    }

    const std::uint8_t idLen = data[0];
    const std::uint8_t colorMapType = data[1];
    const std::uint8_t imgType = data[2];
    const std::uint16_t width = ReadLe16(data + 12u);
    const std::uint16_t height = ReadLe16(data + 14u);
    const std::uint8_t bpp = data[16];
    const std::uint8_t desc = data[17];

    if (colorMapType != 0u || width == 0u || height == 0u ||
        (imgType != 2u && imgType != 3u && imgType != 10u && imgType != 11u) ||
        (bpp != 8u && bpp != 24u && bpp != 32u)) {
        SetError(outError, "Unsupported TGA image type or bit depth.");
        return false;
    }

    const std::size_t bytesPerPx = bpp / 8u;
    std::size_t pos = 18u + idLen;
    if (pos > size) {
        SetError(outError, "Truncated TGA ID field.");
        return false;
    }

    const bool topOrigin = (desc & 0x20u) != 0u;
    const std::size_t totalPixels = static_cast<std::size_t>(width) * height;
    outImage.width = width;
    outImage.height = height;
    outImage.format = ImageFormat::Tga;
    outImage.rgbaPixels.resize(totalPixels * 4u);

    auto writePixelAtLinearIndex = [&](std::size_t linearIdx, const std::uint8_t* srcPx) noexcept {
        const std::uint32_t x = static_cast<std::uint32_t>(linearIdx % width);
        const std::uint32_t rawY = static_cast<std::uint32_t>(linearIdx / width);
        const std::uint32_t dstY = topOrigin ? rawY : (height - 1u - rawY);
        std::uint8_t* dst = outImage.rgbaPixels.data() + (static_cast<std::size_t>(dstY) * width + x) * 4u;
        if (bytesPerPx == 1u) {
            dst[0] = srcPx[0];
            dst[1] = srcPx[0];
            dst[2] = srcPx[0];
            dst[3] = 255u;
        } else if (bytesPerPx == 3u) {
            dst[0] = srcPx[2];
            dst[1] = srcPx[1];
            dst[2] = srcPx[0];
            dst[3] = 255u;
        } else {
            dst[0] = srcPx[2];
            dst[1] = srcPx[1];
            dst[2] = srcPx[0];
            dst[3] = srcPx[3];
        }
    };

    if (imgType == 2u || imgType == 3u) {
        if (pos + totalPixels * bytesPerPx > size) {
            SetError(outError, "Truncated uncompressed TGA pixel buffer.");
            return false;
        }
        for (std::size_t i = 0u; i < totalPixels; ++i) {
            writePixelAtLinearIndex(i, data + pos + i * bytesPerPx);
        }
    } else {
        std::size_t pixelCursor = 0u;
        while (pixelCursor < totalPixels) {
            if (pos >= size) {
                SetError(outError, "Truncated RLE TGA stream.");
                return false;
            }
            const std::uint8_t packetHdr = data[pos++];
            const std::size_t count = static_cast<std::size_t>(packetHdr & 0x7Fu) + 1u;
            if (pixelCursor + count > totalPixels) {
                SetError(outError, "TGA RLE packet exceeded image dimensions.");
                return false;
            }
            if ((packetHdr & 0x80u) != 0u) {
                if (pos + bytesPerPx > size) {
                    SetError(outError, "Truncated TGA RLE run pixel.");
                    return false;
                }
                const std::uint8_t* runPx = data + pos;
                pos += bytesPerPx;
                for (std::size_t k = 0u; k < count; ++k) {
                    writePixelAtLinearIndex(pixelCursor++, runPx);
                }
            } else {
                if (pos + count * bytesPerPx > size) {
                    SetError(outError, "Truncated TGA RLE literal span.");
                    return false;
                }
                for (std::size_t k = 0u; k < count; ++k) {
                    writePixelAtLinearIndex(pixelCursor++, data + pos);
                    pos += bytesPerPx;
                }
            }
        }
    }
    return true;
}

bool DecodeImageFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    DecodedImage& outImage,
    std::string* outError) {
    switch (DetectImageFormat(data, size)) {
        case ImageFormat::Png:
            return DecodePngFromMemory(data, size, outImage, outError);
        case ImageFormat::Jpeg:
            return DecodeJpegFromMemory(data, size, outImage, outError);
        case ImageFormat::Bmp:
            return DecodeBmpFromMemory(data, size, outImage, outError);
        case ImageFormat::Tga:
            return DecodeTgaFromMemory(data, size, outImage, outError);
        default:
            outImage = DecodedImage{};
            SetError(outError, "Unrecognized or unsupported image format.");
            return false;
    }
}

bool DecodeImageFromFile(
    std::string_view filePath,
    DecodedImage& outImage,
    std::string* outError) {
    std::vector<std::uint8_t> bytes;
    if (!ReadAllFileBytes(std::string(filePath), bytes)) {
        outImage = DecodedImage{};
        SetError(outError, "Failed to open or read image file.");
        return false;
    }
    return DecodeImageFromMemory(bytes.data(), bytes.size(), outImage, outError);
}

// ============================================================================
// URI Normalization & Stretch Geometry / UV Cropping
// ============================================================================

std::string NormalizeImageUriPath(std::string_view rawUri, std::string_view baseDir) {
    while (!rawUri.empty() && (rawUri.front() == ' ' || rawUri.front() == '\t')) {
        rawUri.remove_prefix(1u);
    }
    while (!rawUri.empty() && (rawUri.back() == ' ' || rawUri.back() == '\t')) {
        rawUri.remove_suffix(1u);
    }
    if (rawUri.empty()) {
        return {};
    }

    // Strip WPF / NoesisGUI pack://application:,,,/ and /AssemblyName;component/ prefixes
    constexpr std::string_view kPackPrefix = "pack://application:,,,";
    if (rawUri.size() >= kPackPrefix.size() && rawUri.substr(0u, kPackPrefix.size()) == kPackPrefix) {
        rawUri.remove_prefix(kPackPrefix.size());
        if (!rawUri.empty() && rawUri.front() == '/') {
            rawUri.remove_prefix(1u);
        }
    }
    constexpr std::string_view kFilePrefix = "file://";
    if (rawUri.size() >= kFilePrefix.size() && rawUri.substr(0u, kFilePrefix.size()) == kFilePrefix) {
        rawUri.remove_prefix(kFilePrefix.size());
    }
    if (const std::size_t compPos = rawUri.find(";component/"); compPos != std::string_view::npos) {
        rawUri = rawUri.substr(compPos + 11u);
    }

    std::string unified;
    unified.reserve(baseDir.size() + 1u + rawUri.size());

    const bool isAbsUnix = !rawUri.empty() && rawUri.front() == '/';
    const bool isAbsWin = rawUri.size() >= 2u && rawUri[1] == ':';
    if (!isAbsUnix && !isAbsWin && !baseDir.empty()) {
        unified.append(baseDir);
        if (!unified.empty() && unified.back() != '/' && unified.back() != '\\') {
            unified.push_back('/');
        }
    }
    unified.append(rawUri);

    for (char& ch : unified) {
        if (ch == '\\') {
            ch = '/';
        }
    }

    const bool leadingSlash = !unified.empty() && unified.front() == '/';
    std::vector<std::string_view> segments;
    std::size_t cur = 0u;
    const std::string_view view(unified);
    while (cur < view.size()) {
        const std::size_t slash = view.find('/', cur);
        const std::string_view seg = (slash == std::string_view::npos)
            ? view.substr(cur)
            : view.substr(cur, slash - cur);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                if (!segments.empty() && segments.back() != "..") {
                    segments.pop_back();
                } else if (!leadingSlash) {
                    segments.push_back(seg);
                }
            } else {
                segments.push_back(seg);
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        cur = slash + 1u;
    }

    std::string result;
    result.reserve(unified.size());
    if (leadingSlash) {
        result.push_back('/');
    }
    for (std::size_t i = 0u; i < segments.size(); ++i) {
        if (i > 0u) {
            result.push_back('/');
        }
        result.append(segments[i]);
    }
    return result;
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
    return UiResourceContext::Current().Images();
}

ImageAtlas::ImageAtlas() noexcept = default;

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
    maxPackableWidth_ = std::min(maxPackableWidth, sharedAtlasSize_ - 2u);
    maxPackableHeight_ = std::min(maxPackableHeight, sharedAtlasSize_ - 2u);
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

void ImageAtlas::EnsureSharedAtlasAllocatedLocked() {
    if (textureSlots_.find(kSharedUiAtlasTextureId) != textureSlots_.end()) {
        return;
    }
    TextureSlotBuffer slot{};
    slot.textureId = kSharedUiAtlasTextureId;
    slot.width = sharedAtlasSize_;
    slot.height = sharedAtlasSize_;
    slot.dirtyRevision = 1u;
    slot.isAtlas = true;
    slot.rgbaPixels.assign(
        static_cast<std::size_t>(sharedAtlasSize_) * sharedAtlasSize_ * 4u, 0u);
    textureSlots_.emplace(kSharedUiAtlasTextureId, std::move(slot));

    skyline_.clear();
    skyline_.push_back(SkylineNode{0u, 0u, sharedAtlasSize_});
}

bool ImageAtlas::TryAllocateSkylineRectLocked(
    std::uint32_t paddedW,
    std::uint32_t paddedH,
    std::uint32_t& outX,
    std::uint32_t& outY) noexcept {
    if (paddedW > sharedAtlasSize_ || paddedH > sharedAtlasSize_ || skyline_.empty()) {
        return false;
    }

    std::size_t bestIndex = static_cast<std::size_t>(-1);
    std::uint32_t bestY = 0xFFFFFFFFu;
    std::uint32_t bestWidth = 0xFFFFFFFFu;

    for (std::size_t i = 0u; i < skyline_.size(); ++i) {
        const std::uint32_t x = skyline_[i].x;
        if (x + paddedW > sharedAtlasSize_) {
            continue;
        }
        std::uint32_t y = skyline_[i].y;
        std::int64_t widthLeft = static_cast<std::int64_t>(paddedW);
        std::size_t j = i;
        bool fits = true;
        while (widthLeft > 0) {
            if (j >= skyline_.size()) {
                fits = false;
                break;
            }
            y = std::max(y, skyline_[j].y);
            if (y + paddedH > sharedAtlasSize_) {
                fits = false;
                break;
            }
            widthLeft -= static_cast<std::int64_t>(skyline_[j].width);
            ++j;
        }
        if (fits) {
            if (y < bestY || (y == bestY && skyline_[i].width < bestWidth)) {
                bestIndex = i;
                bestY = y;
                bestWidth = skyline_[i].width;
            }
        }
    }

    if (bestIndex == static_cast<std::size_t>(-1)) {
        return false;
    }

    const std::uint32_t placedX = skyline_[bestIndex].x;
    const std::uint32_t placedY = bestY;
    SkylineNode newNode{placedX, placedY + paddedH, paddedW};
    skyline_.insert(skyline_.begin() + static_cast<std::ptrdiff_t>(bestIndex), newNode);

    for (std::size_t i = bestIndex + 1u; i < skyline_.size(); ++i) {
        const std::uint32_t prevRight = skyline_[i - 1u].x + skyline_[i - 1u].width;
        if (skyline_[i].x < prevRight) {
            const std::uint32_t shrink = prevRight - skyline_[i].x;
            if (skyline_[i].width <= shrink) {
                skyline_.erase(skyline_.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
            } else {
                skyline_[i].x += shrink;
                skyline_[i].width -= shrink;
                break;
            }
        } else {
            break;
        }
    }

    // Merge adjacent co-level skyline spans
    for (std::size_t i = 0u; i + 1u < skyline_.size(); ++i) {
        if (skyline_[i].y == skyline_[i + 1u].y) {
            skyline_[i].width += skyline_[i + 1u].width;
            skyline_.erase(skyline_.begin() + static_cast<std::ptrdiff_t>(i + 1u));
            --i;
        }
    }

    outX = placedX;
    outY = placedY;
    return true;
}

void ImageAtlas::BlitWithExtrudedBorderLocked(
    TextureSlotBuffer& atlasBuf,
    std::uint32_t innerX,
    std::uint32_t innerY,
    const DecodedImage& img) noexcept {
    const std::uint32_t aw = atlasBuf.width;
    const std::uint32_t iw = img.width;
    const std::uint32_t ih = img.height;

    auto copyTexel = [&](std::uint32_t dstX, std::uint32_t dstY, std::uint32_t srcX, std::uint32_t srcY) noexcept {
        std::uint8_t* dst = atlasBuf.rgbaPixels.data() + (static_cast<std::size_t>(dstY) * aw + dstX) * 4u;
        const std::uint8_t* src = img.rgbaPixels.data() + (static_cast<std::size_t>(srcY) * iw + srcX) * 4u;
        std::memcpy(dst, src, 4u);
    };

    // Copy interior rows
    for (std::uint32_t y = 0u; y < ih; ++y) {
        std::uint8_t* dstRow = atlasBuf.rgbaPixels.data() +
            (static_cast<std::size_t>(innerY + y) * aw + innerX) * 4u;
        const std::uint8_t* srcRow = img.rgbaPixels.data() + static_cast<std::size_t>(y) * iw * 4u;
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
        std::uint8_t* topDst = atlasBuf.rgbaPixels.data() +
            (static_cast<std::size_t>(innerY - 1u) * aw + innerX) * 4u;
        const std::uint8_t* topSrc = img.rgbaPixels.data();
        std::memcpy(topDst, topSrc, static_cast<std::size_t>(iw) * 4u);
        if (innerX > 0u) copyTexel(innerX - 1u, innerY - 1u, 0u, 0u);
        if (innerX + iw < aw) copyTexel(innerX + iw, innerY - 1u, iw - 1u, 0u);
    }
    if (innerY + ih < atlasBuf.height) {
        std::uint8_t* botDst = atlasBuf.rgbaPixels.data() +
            (static_cast<std::size_t>(innerY + ih) * aw + innerX) * 4u;
        const std::uint8_t* botSrc = img.rgbaPixels.data() + static_cast<std::size_t>(ih - 1u) * iw * 4u;
        std::memcpy(botDst, botSrc, static_cast<std::size_t>(iw) * 4u);
        if (innerX > 0u) copyTexel(innerX - 1u, innerY + ih, 0u, ih - 1u);
        if (innerX + iw < aw) copyTexel(innerX + iw, innerY + ih, iw - 1u, ih - 1u);
    }

    const std::uint64_t nextRev = revision_.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    atlasBuf.dirtyRevision = static_cast<std::uint32_t>(nextRev != 0u ? nextRev : 1u);
}

const ImageAtlasEntry* ImageAtlas::InsertDecodedLocked(
    std::string canonicalKey,
    DecodedImage&& image,
    bool forceStandalone) {
    if (!image.Valid()) {
        return nullptr;
    }

    if (const auto existing = entries_.find(canonicalKey); existing != entries_.end()) {
        return &existing->second;
    }

    const bool fitsPackThreshold = !forceStandalone &&
        image.width <= maxPackableWidth_ &&
        image.height <= maxPackableHeight_ &&
        (image.width + 2u) <= sharedAtlasSize_ &&
        (image.height + 2u) <= sharedAtlasSize_;

    ImageAtlasEntry entry{};
    entry.uriKey = canonicalKey;
    entry.width = image.width;
    entry.height = image.height;

    bool packedInSharedAtlas = false;
    if (fitsPackThreshold) {
        EnsureSharedAtlasAllocatedLocked();
        std::uint32_t paddedX = 0u;
        std::uint32_t paddedY = 0u;
        if (TryAllocateSkylineRectLocked(image.width + 2u, image.height + 2u, paddedX, paddedY)) {
            const std::uint32_t innerX = paddedX + 1u;
            const std::uint32_t innerY = paddedY + 1u;
            TextureSlotBuffer& sharedBuf = textureSlots_[kSharedUiAtlasTextureId];
            BlitWithExtrudedBorderLocked(sharedBuf, innerX, innerY, image);

            const float invAtlasW = 1.0f / static_cast<float>(sharedAtlasSize_);
            const float invAtlasH = 1.0f / static_cast<float>(sharedAtlasSize_);
            entry.textureId = kSharedUiAtlasTextureId;
            entry.atlasX = innerX;
            entry.atlasY = innerY;
            entry.atlasWidth = sharedAtlasSize_;
            entry.atlasHeight = sharedAtlasSize_;
            entry.u0 = static_cast<float>(innerX) * invAtlasW;
            entry.v0 = static_cast<float>(innerY) * invAtlasH;
            entry.u1 = static_cast<float>(innerX + image.width) * invAtlasW;
            entry.v1 = static_cast<float>(innerY + image.height) * invAtlasH;
            entry.isStandalone = false;
            packedInSharedAtlas = true;
        }
    }

    if (!packedInSharedAtlas) {
        const std::uint32_t texId = nextStandaloneTextureId_++;
        const std::uint64_t nextRev = revision_.fetch_add(1u, std::memory_order_acq_rel) + 1u;
        TextureSlotBuffer slot{};
        slot.textureId = texId;
        slot.width = image.width;
        slot.height = image.height;
        slot.dirtyRevision = static_cast<std::uint32_t>(nextRev != 0u ? nextRev : 1u);
        slot.isAtlas = false;
        slot.rgbaPixels = std::move(image.rgbaPixels);
        textureSlots_.emplace(texId, std::move(slot));

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

const ImageAtlasEntry* ImageAtlas::FindLoaded(
    std::string_view uri,
    std::string_view localBaseDir) const noexcept {
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

const ImageAtlasEntry* ImageAtlas::LoadOrGet(
    std::string_view uri,
    std::string_view localBaseDir) {
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

    DecodedImage decoded{};
    std::string canonicalKey;
    if (!memCopy.empty()) {
        if (!DecodeImageFromMemory(memCopy.data(), memCopy.size(), decoded)) {
            return nullptr;
        }
        canonicalKey = clean;
    } else if (!resolvedDiskPath.empty()) {
        if (!DecodeImageFromFile(resolvedDiskPath, decoded)) {
            return nullptr;
        }
        canonicalKey = resolvedDiskPath;
    } else {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return InsertDecodedLocked(std::move(canonicalKey), std::move(decoded), false);
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

bool ImageAtlas::CopyTextureSlotIfModified(
    std::uint32_t textureId,
    std::uint32_t& ioKnownSlotRevision,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight,
    std::vector<std::uint8_t>& outRgba8,
    bool& outWasUpdated) const {
    outWasUpdated = false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        outWidth = 0u;
        outHeight = 0u;
        return false;
    }
    const TextureSlotBuffer& slot = it->second;
    const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    if (slot.width == 0u || slot.height == 0u || slot.rgbaPixels.size() < expectedBytes) {
        outWidth = 0u;
        outHeight = 0u;
        return false;
    }
    outWidth = slot.width;
    outHeight = slot.height;
    if (ioKnownSlotRevision == 0u || ioKnownSlotRevision != slot.dirtyRevision) {
        outRgba8.assign(slot.rgbaPixels.data(), slot.rgbaPixels.data() + expectedBytes);
        ioKnownSlotRevision = slot.dirtyRevision;
        outWasUpdated = true;
    }
    return true;
}

Color ImageAtlas::SampleBilinearRgba(std::uint32_t textureId, float u, float v) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    const TextureSlotBuffer& slot = it->second;
    if (slot.width == 0u || slot.height == 0u ||
        slot.rgbaPixels.size() < static_cast<std::size_t>(slot.width) * slot.height * 4u) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }

    const float clampedU = std::clamp(u, 0.0f, 1.0f);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const float texX = clampedU * static_cast<float>(slot.width) - 0.5f;
    const float texY = clampedV * static_cast<float>(slot.height) - 0.5f;

    const int maxX = static_cast<int>(slot.width) - 1;
    const int maxY = static_cast<int>(slot.height) - 1;
    const int x0 = std::clamp(static_cast<int>(std::floor(texX)), 0, maxX);
    const int y0 = std::clamp(static_cast<int>(std::floor(texY)), 0, maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);
    const float fx = std::clamp(texX - std::floor(texX), 0.0f, 1.0f);
    const float fy = std::clamp(texY - std::floor(texY), 0.0f, 1.0f);

    const std::uint8_t* p00 = slot.rgbaPixels.data() + (static_cast<std::size_t>(y0) * slot.width + x0) * 4u;
    const std::uint8_t* p10 = slot.rgbaPixels.data() + (static_cast<std::size_t>(y0) * slot.width + x1) * 4u;
    const std::uint8_t* p01 = slot.rgbaPixels.data() + (static_cast<std::size_t>(y1) * slot.width + x0) * 4u;
    const std::uint8_t* p11 = slot.rgbaPixels.data() + (static_cast<std::size_t>(y1) * slot.width + x1) * 4u;

    constexpr float kInv255 = 1.0f / 255.0f;
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

void ImageAtlas::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    skyline_.clear();
    memoryEncodedAssets_.clear();
    entries_.clear();
    textureSlots_.clear();
    nextStandaloneTextureId_ = 1u;
    revision_.fetch_add(1u, std::memory_order_release);
}

} // namespace aero
