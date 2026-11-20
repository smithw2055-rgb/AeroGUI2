#pragma once

#include <string_view>
#include "aero/render/ImageAtlas.hpp"

namespace aero {

[[nodiscard]] bool IsRiveAssetUri(std::string_view uri) noexcept;

[[nodiscard]] bool RenderRiveUriToImage(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    DecodedImage& outImage);

} // namespace aero
