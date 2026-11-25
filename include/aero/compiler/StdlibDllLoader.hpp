#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "aero/compiler/StdlibDllAbi.h"
#include "aero/core/RsAliases.hpp"

namespace realscript::compiler {
struct PrecompiledFrontendPack;
} // namespace realscript::compiler

namespace realscript::bytecode {
struct LazyBytecodeBundle;
} // namespace realscript::bytecode

namespace aero {

struct LoadedStdlibDll final {
    std::string resolvedPath;
    const AeroStdlibDllHeader* header = nullptr;
    std::shared_ptr<const rs_comp::PrecompiledFrontendPack> frontendPack;
    std::shared_ptr<rs_bc::LazyBytecodeBundle> lazyBundle;
    double loadAndDecodeMs = 0.0;
};

[[nodiscard]] std::uint64_t ComputeStdlibDllChecksum64(
    const void* data,
    std::size_t size) noexcept;

[[nodiscard]] std::filesystem::path ResolveStdlibDllPath(
    std::string_view explicitPath = {});

[[nodiscard]] std::shared_ptr<const LoadedStdlibDll> LoadStdlibDll(
    std::string_view explicitPath = {},
    std::string* outError = nullptr);

[[nodiscard]] std::shared_ptr<LoadedStdlibDll> LoadStdlibDllOneShot(
    std::string_view explicitPath = {},
    std::string* outError = nullptr);

[[nodiscard]] std::shared_ptr<LoadedStdlibDll> LoadStdlibDllLazyOnly(
    std::string_view explicitPath = {},
    std::string* outError = nullptr);

[[nodiscard]] bool DecodeStdlibDllFrontendPack(
    LoadedStdlibDll& loaded,
    std::string* outError = nullptr);

void ClearStdlibDllCache() noexcept;
void ClearStdlibDllCacheForTesting() noexcept;

} // namespace aero
