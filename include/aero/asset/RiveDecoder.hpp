#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

struct AxbPackage;

// P1-5: Real Rive v7 LEB128 binary .riv stream detection & runtime/compiler decoding to AxbPackage
[[nodiscard]] bool IsRiveBinaryStream(
    const std::uint8_t* rawBytes,
    std::size_t byteLength) noexcept;

[[nodiscard]] bool DecodeRiveBinaryToAxbPackage(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AxbPackage& outPackage,
    std::vector<std::string>* outDiagnostics = nullptr,
    std::string_view artboardNameHint = {});

} // namespace aero
