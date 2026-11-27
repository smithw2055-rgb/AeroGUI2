#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "aero/core/RsAliases.hpp"

namespace realscript::bytecode {
struct Module;
} // namespace realscript::bytecode

namespace aero {

[[nodiscard]] std::vector<std::uint8_t> EncodeArbcBundle(
    const std::vector<rs_bc::Module>& modules);

[[nodiscard]] bool DecodeArbcBundle(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<rs_bc::Module>& outModules,
    bool allowSingleModuleFallback = false,
    std::string* outError = nullptr);

} // namespace aero
