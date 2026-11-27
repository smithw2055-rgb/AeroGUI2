#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aero::app {

[[nodiscard]] std::filesystem::path ResolveExecutableDir(const char* argv0 = nullptr);
[[nodiscard]] std::string ReadEnvString(const char* name);
[[nodiscard]] std::string ResolveDefaultCacheDir();

[[nodiscard]] double QueryWorkingSetMb() noexcept;
[[nodiscard]] double QueryPrivateUsageMb() noexcept;

[[nodiscard]] bool ReadUtf8TextFile(const std::filesystem::path& path, std::string& outText);

[[nodiscard]] bool WriteRgba8PpmBuffer(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8);

[[nodiscard]] inline bool WriteRgba8PpmFile(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }
    return WriteRgba8PpmBuffer(path, width, height, rgba.data());
}

[[nodiscard]] bool HasNonUniformRenderedPixels(const std::vector<std::uint8_t>& rgba) noexcept;

void DiscoverSampleAssetDirectories(
    const std::filesystem::path& sampleRoot,
    std::vector<std::string>& outDirectories);

} // namespace aero::app
