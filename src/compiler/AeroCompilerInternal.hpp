#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "aero/asset/AxbFormat.hpp"
#include "aero/compiler/AeroSceneIR.hpp"
#include "aero/core/StringUtils.hpp"

namespace aero::compiler_detail {

inline constexpr std::uint16_t kUnmappedPropId = 0xFFFFu;

// Zero-allocation ASCII float32 parser for compiler literals (supports trailing 'f'/'F' and prefix parsing).
[[nodiscard]] inline float ParseFloat32(std::string_view text, float fallback = 0.0f) noexcept {
    text = str::Trim(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        text.remove_suffix(1u);
    }
    if (text.empty()) {
        return fallback;
    }
    char buf[128]{};
    if (text.size() < sizeof(buf)) {
        std::memcpy(buf, text.data(), text.size());
        char* end = nullptr;
        const float val = std::strtof(buf, &end);
        return (end == buf) ? fallback : val;
    }
    std::string heapBuf(text);
    char* end = nullptr;
    const float val = std::strtof(heapBuf.c_str(), &end);
    return (end == heapBuf.c_str()) ? fallback : val;
}

[[nodiscard]] inline std::string CanonicalizeEcsComponentName(std::string_view compName) {
    std::string canonicalComp(compName);
    if (canonicalComp.find("::") == std::string::npos) {
        if (const std::size_t lastDot = canonicalComp.rfind('.'); lastDot != std::string::npos) {
            canonicalComp.replace(lastDot, 1u, "::");
        }
    }
    return canonicalComp;
}

// Implemented in src/compiler/FrontendImporters.cpp
void PopulateSceneIrFromPackage(
    const AxbPackage& pkg,
    FrontendSourceFormat format,
    std::string_view sourcePath,
    AeroSceneGraphIR& outIr);

[[nodiscard]] bool IsSvgAssetInput(std::string_view path, std::string_view content) noexcept;
[[nodiscard]] bool IsLottieAssetInput(std::string_view path, std::string_view content) noexcept;
[[nodiscard]] bool IsRiveAssetInput(std::string_view path, std::string_view content) noexcept;

} // namespace aero::compiler_detail
