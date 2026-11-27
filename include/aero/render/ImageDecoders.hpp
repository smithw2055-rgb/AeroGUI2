#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

// Pure C++17 zero-dependency RFC1951 DEFLATE & RFC1950 zlib decompression (100% Wasm-ready)
[[nodiscard]] bool InflateRfc1951(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData,
    std::size_t* outConsumedBytes = nullptr,
    std::size_t maxOutputBytes = 128u * 1024u * 1024u) noexcept;

[[nodiscard]] bool DecompressZlib(
    const std::uint8_t* src,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outData,
    std::size_t maxOutputBytes = 128u * 1024u * 1024u) noexcept;

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

} // namespace aero
