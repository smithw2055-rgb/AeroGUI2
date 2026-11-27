#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace aero {

class Win32AudioEngine final {
public:
    Win32AudioEngine();
    ~Win32AudioEngine();

    Win32AudioEngine(const Win32AudioEngine&) = delete;
    Win32AudioEngine& operator=(const Win32AudioEngine&) = delete;

    void Enqueue(std::string_view filePath, float volume = 1.0f) noexcept;

    [[nodiscard]] static bool DecodeSoundFileToPcm16(
        std::string_view filePath,
        std::uint32_t& outSampleRate,
        std::uint16_t& outChannels,
        std::vector<std::int16_t>& outSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aero
