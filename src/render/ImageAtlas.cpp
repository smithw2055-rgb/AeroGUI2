#include "aero/render/ImageAtlas.hpp"
#include "aero/render/RiveRenderer.hpp"
#include "aero/core/UIResourceStore.hpp"

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

    const std::uint8_t* singleIdatPtr = nullptr;
    std::size_t singleIdatLen = 0u;
    std::size_t idatCount = 0u;
    std::vector<std::uint8_t> idatCompressed;

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
            if (idatCount == 0u) {
                singleIdatPtr = chunkData;
                singleIdatLen = chunkLen;
            } else {
                if (idatCount == 1u) {
                    idatCompressed.reserve(size);
                    idatCompressed.insert(idatCompressed.end(), singleIdatPtr, singleIdatPtr + singleIdatLen);
                }
                idatCompressed.insert(idatCompressed.end(), chunkData, chunkData + chunkLen);
            }
            ++idatCount;
        } else if (std::memcmp(typePtr, "IEND", 4u) == 0) {
            seenIend = true;
            break;
        }

        pos += static_cast<std::size_t>(chunkLen) + 4u; // skip CRC
    }

    const bool hasIdat = (idatCount == 1u) ? (singleIdatLen > 0u) : !idatCompressed.empty();
    if (!seenIhdr || !hasIdat || (colorType == 3u && paletteEntries == 0u)) {
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

    const std::uint8_t* idatPtr = (idatCount == 1u) ? singleIdatPtr : idatCompressed.data();
    const std::size_t idatLen = (idatCount == 1u) ? singleIdatLen : idatCompressed.size();
    std::vector<std::uint8_t> rawDecompressed;
    if (!DecompressZlib(
            idatPtr,
            idatLen,
            rawDecompressed,
            static_cast<std::size_t>(expectedDecompressedBytes))) {
        SetError(outError, "Failed to decompress PNG zlib IDAT stream.");
        return false;
    }
    if (!idatCompressed.empty()) {
        std::vector<std::uint8_t>().swap(idatCompressed);
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
        if (dstStartX == 0u && dstStepX == 1u && bitDepth == 8u) {
            std::uint8_t* dstRow = outImage.rgbaPixels.data() + static_cast<std::size_t>(dstY) * width * 4u;
            if (colorType == 6u) {
                std::memcpy(dstRow, scanRow, static_cast<std::size_t>(subWidth) * 4u);
                return;
            }
            if (colorType == 2u && !hasKeyTransparency) {
                for (std::uint32_t sx = 0u; sx < subWidth; ++sx) {
                    const std::uint8_t* p = scanRow + sx * 3u;
                    std::uint8_t* d = dstRow + sx * 4u;
                    d[0] = p[0];
                    d[1] = p[1];
                    d[2] = p[2];
                    d[3] = 255u;
                }
                return;
            }
        }
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
            std::vector<std::int16_t>().swap(comp.progCoeffs);
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

    struct UpsampleAxisTable {
        bool fullRes = true;
        std::vector<int> i0;
        std::vector<int> i1;
        std::vector<float> frac;
    };
    auto buildAxisTable = [&](std::uint32_t dim, std::uint32_t samp, std::uint32_t maxSamp) -> UpsampleAxisTable {
        UpsampleAxisTable t{};
        t.fullRes = (samp == maxSamp);
        if (t.fullRes) return t;
        t.i0.resize(dim);
        t.i1.resize(dim);
        t.frac.resize(dim);
        const float scale = static_cast<float>(samp) / static_cast<float>(maxSamp);
        const int maxCoord = std::max(0, static_cast<int>((dim * samp + maxSamp - 1u) / maxSamp) - 1);
        for (std::uint32_t i = 0u; i < dim; ++i) {
            const float s = (static_cast<float>(i) + 0.5f) * scale - 0.5f;
            const float fl = std::floor(s);
            const int c0 = std::clamp(static_cast<int>(fl), 0, maxCoord);
            t.i0[i] = c0;
            t.i1[i] = std::clamp(c0 + 1, 0, maxCoord);
            t.frac[i] = std::clamp(s - fl, 0.0f, 1.0f);
        }
        return t;
    };

    const UpsampleAxisTable yAxisX = buildAxisTable(width, cY.hSamp, maxHSamp);
    const UpsampleAxisTable yAxisY = buildAxisTable(height, cY.vSamp, maxVSamp);
    const UpsampleAxisTable cbAxisX = buildAxisTable(width, cCb.hSamp, maxHSamp);
    const UpsampleAxisTable cbAxisY = buildAxisTable(height, cCb.vSamp, maxVSamp);
    const UpsampleAxisTable crAxisX = buildAxisTable(width, cCr.hSamp, maxHSamp);
    const UpsampleAxisTable crAxisY = buildAxisTable(height, cCr.vSamp, maxVSamp);

    if (yAxisX.fullRes && yAxisY.fullRes &&
        !cbAxisX.fullRes && !cbAxisY.fullRes &&
        cCb.hSamp == cCr.hSamp && cCb.vSamp == cCr.vSamp) {
        // Fast path for standard 4:2:0 / 4:2:2 JPEGs (Cb & Cr share identical subsampling factors)
        const int* x0Lut = cbAxisX.i0.data();
        const int* x1Lut = cbAxisX.i1.data();
        const float* fxLut = cbAxisX.frac.data();
        for (std::uint32_t y = 0u; y < height; ++y) {
            const std::uint8_t* yRow = cY.planePixels.data() + static_cast<std::size_t>(y) * cY.planeStride;
            const int cy0 = cbAxisY.i0[y];
            const int cy1 = cbAxisY.i1[y];
            const float fy = cbAxisY.frac[y];
            const std::uint8_t* cbR0 = cCb.planePixels.data() + static_cast<std::size_t>(cy0) * cCb.planeStride;
            const std::uint8_t* cbR1 = cCb.planePixels.data() + static_cast<std::size_t>(cy1) * cCb.planeStride;
            const std::uint8_t* crR0 = cCr.planePixels.data() + static_cast<std::size_t>(cy0) * cCr.planeStride;
            const std::uint8_t* crR1 = cCr.planePixels.data() + static_cast<std::size_t>(cy1) * cCr.planeStride;
            std::uint8_t* dst = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
            for (std::uint32_t x = 0u; x < width; ++x) {
                const int cx0 = x0Lut[x];
                const int cx1 = x1Lut[x];
                const float fx = fxLut[x];
                const float cbTop = static_cast<float>(cbR0[cx0]) + (static_cast<float>(cbR0[cx1]) - static_cast<float>(cbR0[cx0])) * fx;
                const float cbBot = static_cast<float>(cbR1[cx0]) + (static_cast<float>(cbR1[cx1]) - static_cast<float>(cbR1[cx0])) * fx;
                const int cb = static_cast<int>(cbTop + (cbBot - cbTop) * fy + 0.5f) - 128;

                const float crTop = static_cast<float>(crR0[cx0]) + (static_cast<float>(crR0[cx1]) - static_cast<float>(crR0[cx0])) * fx;
                const float crBot = static_cast<float>(crR1[cx0]) + (static_cast<float>(crR1[cx1]) - static_cast<float>(crR1[cx0])) * fx;
                const int cr = static_cast<int>(crTop + (crBot - crTop) * fy + 0.5f) - 128;

                const int yv = static_cast<int>(yRow[x]);
                dst[x * 4u + 0u] = ClampIntToByte(yv + ((91881 * cr + 32768) >> 16));
                dst[x * 4u + 1u] = ClampIntToByte(yv - ((22554 * cb + 46802 * cr + 32768) >> 16));
                dst[x * 4u + 2u] = ClampIntToByte(yv + ((116130 * cb + 32768) >> 16));
                dst[x * 4u + 3u] = 255u;
            }
        }
        return true;
    }

    auto samplePlaneLut = [](const JpegComponent& comp, const UpsampleAxisTable& ax, const UpsampleAxisTable& ay, std::uint32_t px, std::uint32_t py) noexcept -> int {
        if (ax.fullRes && ay.fullRes) {
            return static_cast<int>(comp.planePixels[static_cast<std::size_t>(py) * comp.planeStride + px]);
        }
        const int x0 = ax.fullRes ? static_cast<int>(px) : ax.i0[px];
        const int x1 = ax.fullRes ? static_cast<int>(px) : ax.i1[px];
        const float fx = ax.fullRes ? 0.0f : ax.frac[px];
        const int y0 = ay.fullRes ? static_cast<int>(py) : ay.i0[py];
        const int y1 = ay.fullRes ? static_cast<int>(py) : ay.i1[py];
        const float fy = ay.fullRes ? 0.0f : ay.frac[py];
        const std::uint8_t* r0 = comp.planePixels.data() + static_cast<std::size_t>(y0) * comp.planeStride;
        const std::uint8_t* r1 = comp.planePixels.data() + static_cast<std::size_t>(y1) * comp.planeStride;
        const float top = static_cast<float>(r0[x0]) + (static_cast<float>(r0[x1]) - static_cast<float>(r0[x0])) * fx;
        const float bot = static_cast<float>(r1[x0]) + (static_cast<float>(r1[x1]) - static_cast<float>(r1[x0])) * fx;
        return static_cast<int>(top + (bot - top) * fy + 0.5f);
    };

    for (std::uint32_t y = 0u; y < height; ++y) {
        std::uint8_t* dst = outImage.rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0u; x < width; ++x) {
            const int yv = samplePlaneLut(cY, yAxisX, yAxisY, x, y);
            const int cb = samplePlaneLut(cCb, cbAxisX, cbAxisY, x, y) - 128;
            const int cr = samplePlaneLut(cCr, crAxisX, crAxisY, x, y) - 128;
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
    struct CachedDecodedEntry {
        std::uintmax_t fileSize = 0u;
        std::int64_t mtimeTick = 0;
        DecodedImage image{};
    };
    static std::mutex sDecodeCacheMutex;
    static std::unordered_map<std::string, CachedDecodedEntry> sDecodeCache;

    std::error_code ec;
    const std::filesystem::path fsPath(filePath);
    std::filesystem::path normPath = std::filesystem::weakly_canonical(fsPath, ec);
    if (ec) {
        ec.clear();
        normPath = fsPath.lexically_normal();
    }
    std::string keyStr = normPath.generic_string();
#ifdef _WIN32
    for (char& ch : keyStr) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
#endif
    const std::uintmax_t fSize = std::filesystem::file_size(fsPath, ec);
    std::int64_t mtimeTick = 0;
    if (!ec) {
        const auto ftime = std::filesystem::last_write_time(fsPath, ec);
        if (!ec) {
            mtimeTick = static_cast<std::int64_t>(ftime.time_since_epoch().count());
        }
    }
    if (!ec && fSize > 0u) {
        std::lock_guard<std::mutex> lock(sDecodeCacheMutex);
        const auto it = sDecodeCache.find(keyStr);
        if (it != sDecodeCache.end() &&
            it->second.fileSize == fSize &&
            it->second.mtimeTick == mtimeTick &&
            it->second.image.width > 0u) {
            outImage = it->second.image;
            return true;
        }
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadAllFileBytes(std::string(filePath), bytes)) {
        outImage = DecodedImage{};
        SetError(outError, "Failed to open or read image file.");
        return false;
    }
    if (!DecodeImageFromMemory(bytes.data(), bytes.size(), outImage, outError)) {
        return false;
    }
    if (!ec && fSize > 0u && outImage.rgbaPixels.size() <= 32u * 1024u * 1024u) {
        std::lock_guard<std::mutex> lock(sDecodeCacheMutex);
        if (sDecodeCache.size() >= 64u) {
            sDecodeCache.clear();
        }
        sDecodeCache[keyStr] = CachedDecodedEntry{fSize, mtimeTick, outImage};
    }
    return true;
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

namespace {

void EnsureImageAtlasRegisteredWithStore() noexcept {
    UIResourceStore::RegisterImageAtlasHooks(
        []() { return std::make_shared<ImageAtlas>(); },
        [](ImageAtlas& atlas) noexcept { atlas.Clear(); });
    UIResourceStore::RegisterImageAtlasSlotHook(
        [](ImageAtlas& atlas,
           std::uint32_t reuseSlotId,
           std::uint32_t w,
           std::uint32_t h,
           std::vector<std::uint8_t>&& rgbaPixels) {
            return atlas.RegisterOrUpdateRgbaTextureSlot(
                reuseSlotId, w, h, std::move(rgbaPixels));
        },
        [](ImageAtlas& atlas, std::uint32_t texId) noexcept {
            atlas.ReleaseRgbaTextureSlot(texId);
        },
        [](ImageAtlas& atlas,
           std::uint64_t maskKey,
           std::uint32_t w,
           std::uint32_t h,
           const std::uint8_t* rgbaPixels,
           std::size_t byteSize,
           std::uint32_t& outTexId,
           float& outU0,
           float& outV0,
           float& outU1,
           float& outV1) -> bool {
            outTexId = ImageAtlas::kSharedUiAtlasTextureId;
            return atlas.TryPackMaskIntoSharedAtlas(
                maskKey, w, h, rgbaPixels, byteSize, outU0, outV0, outU1, outV1);
        });
}

const bool gImageAtlasStoreHooksInit = []() noexcept {
    EnsureImageAtlasRegisteredWithStore();
    return true;
}();

} // namespace

ImageAtlas& ImageAtlas::Instance() noexcept {
    EnsureImageAtlasRegisteredWithStore();
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
    if (textureSlots_.find(kSharedUiAtlasTextureId) != textureSlots_.end()) {
        return;
    }
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

    skyline_.Reset(sharedAtlasSize_, sharedAtlasSize_);
    revision_.fetch_add(1u, std::memory_order_release);
}

bool ImageAtlas::TryAllocateSkylineRectLocked(
    std::uint32_t paddedW,
    std::uint32_t paddedH,
    std::uint32_t& outX,
    std::uint32_t& outY) noexcept {
    return skyline_.TryAllocate(paddedW, paddedH, outX, outY);
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
    bool forceStandalone) {
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
        (width + 2u) <= sharedAtlasSize_ &&
        (height + 2u) <= sharedAtlasSize_;

    ImageAtlasEntry entry{};
    entry.uriKey = canonicalKey;
    entry.width = width;
    entry.height = height;

    bool packedInSharedAtlas = false;
    if (fitsPackThreshold) {
        EnsureSharedAtlasAllocatedLocked();
        std::uint32_t paddedX = 0u;
        std::uint32_t paddedY = 0u;
        if (TryAllocateSkylineRectLocked(width + 2u, height + 2u, paddedX, paddedY)) {
            const std::uint32_t innerX = paddedX + 1u;
            const std::uint32_t innerY = paddedY + 1u;
            TextureSlotBuffer& sharedBuf = textureSlots_[kSharedUiAtlasTextureId];
            BlitWithExtrudedBorderLocked(sharedBuf, innerX, innerY, width, height, pixels->data());

            const float invAtlasW = 1.0f / static_cast<float>(sharedAtlasSize_);
            const float invAtlasH = 1.0f / static_cast<float>(sharedAtlasSize_);
            entry.textureId = kSharedUiAtlasTextureId;
            entry.atlasX = innerX;
            entry.atlasY = innerY;
            entry.atlasWidth = sharedAtlasSize_;
            entry.atlasHeight = sharedAtlasSize_;
            entry.u0 = static_cast<float>(innerX) * invAtlasW;
            entry.v0 = static_cast<float>(innerY) * invAtlasH;
            entry.u1 = static_cast<float>(innerX + width) * invAtlasW;
            entry.v1 = static_cast<float>(innerY + height) * invAtlasH;
            entry.isStandalone = false;
            packedInSharedAtlas = true;
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

struct PlasmaShaderParams {
    float sx = 64.0f;
    float sy = 64.0f;
    float seed = 100.0f;
    float timeSec = 0.0f;
    float vw = 300.0f;
    float vh = 90.0f;
    std::string colorHex = "#FF01BCFF";
};

bool ParsePlasmaShaderUri(std::string_view uri, PlasmaShaderParams& outParams, std::string& outSlotKey) {
    if (uri.size() < 16u || uri.substr(0u, 16u) != "shader://plasma?") return false;
    outParams = PlasmaShaderParams{};
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
            else if (k == "color") outParams.colorHex = v;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1u);
    }

    const std::uint32_t w = static_cast<std::uint32_t>(std::lround(outParams.vw));
    const std::uint32_t h = static_cast<std::uint32_t>(std::lround(outParams.vh));
    const int seedInt = static_cast<int>(std::lround(outParams.seed));
    const long sxQ = std::lround(outParams.sx * 100.0f);
    const long syQ = std::lround(outParams.sy * 100.0f);
    outSlotKey = "shader://plasma_slot?seed=" + std::to_string(seedInt) +
                 "&sx=" + std::to_string(sxQ) + "&sy=" + std::to_string(syQ) +
                 "&color=" + outParams.colorHex +
                 "&w=" + std::to_string(w) + "&h=" + std::to_string(h);
    return true;
}

bool SynthesizePlasmaShaderImageFromParams(const PlasmaShaderParams& params, DecodedImage& outImg) {
    float baseR = 0.004f, baseG = 0.737f, baseB = 1.0f, baseA = 1.0f;
    std::string cleanCol = params.colorHex;
    if (cleanCol.size() >= 3u && cleanCol.substr(0u, 3u) == "%23") {
        cleanCol = "#" + cleanCol.substr(3u);
    }
    if (!cleanCol.empty()) {
        std::uint32_t rgba = ParseColorRgba32(cleanCol);
        if (rgba == 0u) { rgba = 0xFFFFBC01u; }
        baseR = static_cast<float>(rgba & 0xFFu) / 255.0f;
        baseG = static_cast<float>((rgba >> 8u) & 0xFFu) / 255.0f;
        baseB = static_cast<float>((rgba >> 16u) & 0xFFu) / 255.0f;
        baseA = static_cast<float>((rgba >> 24u) & 0xFFu) / 255.0f;
    }

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
        PlasmaShaderParams params{};
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
        constexpr std::size_t kMaxRiveSlotCache = 48u;
        std::string_view pathPart;
        std::string instKey;
        SplitRiveSlotKeyParts(uri, pathPart, instKey);
        const std::string riveKey = "rive_slot://" + std::string(localBaseDir) + "|" +
            std::string(pathPart) + "|" + instKey;
        std::uint32_t reuseSlotId = 0u;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = riveSlots_.find(riveKey); it != riveSlots_.end()) {
                it->second.lastUsedTick = ++dynamicSlotTick_;
                if (it->second.currentExactUri == uri &&
                    textureSlots_.find(it->second.entry.textureId) != textureSlots_.end()) {
                    return &it->second.entry;
                }
                reuseSlotId = it->second.entry.textureId;
            }
        }
        DecodedImage riveImg{};
        if (!RenderRiveUriToImage(uri, *this, localBaseDir, riveImg) || !riveImg.Valid()) {
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
            constexpr std::size_t kMaxCacheEntries = 64u;
            constexpr std::size_t kMaxCacheBytes = 32u * 1024u * 1024u;
            const std::size_t imgBytes = img.rgbaPixels ? img.rgbaPixels->size() : 0u;
            if (imgBytes == 0u || imgBytes > 24u * 1024u * 1024u || imgBytes > kMaxCacheBytes) {
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

    std::lock_guard<std::mutex> lock(mutex_);
    return InsertDecodedSharedLocked(
        std::move(canonicalKey),
        sharedDecoded.width,
        sharedDecoded.height,
        std::move(sharedDecoded.rgbaPixels),
        false);
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
    std::shared_ptr<const std::vector<std::uint8_t>>& outPixels) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        outWidth = 0u;
        outHeight = 0u;
        outPixels.reset();
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
    const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    if (slot.width == 0u || slot.height == 0u || !snap || snap->size() < expectedBytes) {
        outWidth = 0u;
        outHeight = 0u;
        outPixels.reset();
        return false;
    }
    outWidth = slot.width;
    outHeight = slot.height;
    outPixels = std::move(snap);
    return true;
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
    const std::vector<std::uint8_t>& px =
        slot.cowAtlasPixels ? *slot.cowAtlasPixels
                            : (slot.pixelSnapshot ? *slot.pixelSnapshot : slot.rgbaPixels);
    const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    if (slot.width == 0u || slot.height == 0u || px.size() < expectedBytes) {
        outWidth = 0u;
        outHeight = 0u;
        return false;
    }
    outWidth = slot.width;
    outHeight = slot.height;
    if (ioKnownSlotRevision == 0u || ioKnownSlotRevision != slot.dirtyRevision) {
        outRgba8.assign(px.data(), px.data() + expectedBytes);
        ioKnownSlotRevision = slot.dirtyRevision;
        outWasUpdated = true;
    }
    return true;
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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textureSlots_.find(textureId);
    if (it == textureSlots_.end()) {
        outSlotWidth = 0u;
        outSlotHeight = 0u;
        return false;
    }
    const TextureSlotBuffer& slot = it->second;
    const std::vector<std::uint8_t>& px =
        slot.cowAtlasPixels ? *slot.cowAtlasPixels
                            : (slot.pixelSnapshot ? *slot.pixelSnapshot : slot.rgbaPixels);
    const std::size_t expectedBytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    if (slot.width == 0u || slot.height == 0u || px.size() < expectedBytes) {
        outSlotWidth = 0u;
        outSlotHeight = 0u;
        return false;
    }
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
        SlotCacheStatus status = SlotCacheStatus::Unset;
        std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    };
    thread_local std::array<TlsTexSampleSlot, 16> tlsTex{};
    const std::uint64_t curRev = revision_.load(std::memory_order_acquire);
    auto& ts = tlsTex[textureId & 15u];
    if (ts.instanceId != instanceId_ || ts.rev != curRev || ts.texId != textureId ||
        ts.status == SlotCacheStatus::Unset) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t lockedRev = revision_.load(std::memory_order_acquire);
        const auto it = textureSlots_.find(textureId);
        ts.instanceId = instanceId_;
        ts.rev = lockedRev;
        ts.texId = textureId;
        if (it == textureSlots_.end()) {
            ts.width = 0u;
            ts.height = 0u;
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
        if (sBuf.width == 0u || sBuf.height == 0u || !snap ||
            snap->size() < static_cast<std::size_t>(sBuf.width) * sBuf.height * 4u) {
            ts.width = 0u;
            ts.height = 0u;
            ts.status = SlotCacheStatus::Invalid;
            ts.pixels.reset();
            return Color{0.0f, 0.0f, 0.0f, 0.0f};
        }
        ts.width = sBuf.width;
        ts.height = sBuf.height;
        ts.status = SlotCacheStatus::Valid;
        ts.pixels = std::move(snap);
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

    const std::uint8_t* p00 = pxData + (static_cast<std::size_t>(y0) * slotW + x0) * 4u;
    const std::uint8_t* p10 = pxData + (static_cast<std::size_t>(y0) * slotW + x1) * 4u;
    const std::uint8_t* p01 = pxData + (static_cast<std::size_t>(y1) * slotW + x0) * 4u;
    const std::uint8_t* p11 = pxData + (static_cast<std::size_t>(y1) * slotW + x1) * 4u;

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
    skyline_.ClearToEmpty();
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
