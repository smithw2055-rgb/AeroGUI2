#pragma once

#include <string_view>
#include "aero/render/ImageAtlas.hpp"

namespace aero {

struct RiveRenderRequest {
    std::string_view assetPath;
    std::uint32_t instanceId = 0u;
    float tLoopSec = 0.0f;
    float tStateSec = 0.0f;
    float reqW = 0.0f;
    float reqH = 0.0f;
    const std::pair<std::string, float>* inputs = nullptr;
    std::size_t inputCount = 0u;
};

[[nodiscard]] bool IsRiveAssetUri(std::string_view uri) noexcept;

[[nodiscard]] bool RenderRiveParamsToImageIfChanged(
    const RiveRenderRequest& request,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    const std::uint64_t* prevPoseHash,
    std::uint64_t& outPoseHash,
    bool& outChanged,
    DecodedImage& outImage);

[[nodiscard]] bool RenderRiveUriToImage(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    DecodedImage& outImage);

[[nodiscard]] bool RenderRiveUriToImageIfChanged(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    const std::uint64_t* prevPoseHash,
    std::uint64_t& outPoseHash,
    bool& outChanged,
    DecodedImage& outImage);

} // namespace aero
