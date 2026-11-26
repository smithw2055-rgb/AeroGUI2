#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "aero/compiler/AeroCompilerV3.hpp"
#include "realscript/mir/Mir.h"

namespace aero {

/// Resolves the AeroGUI RealScript stdlib root directory (`Aero.Native` + `Aero.UI.*`).
[[nodiscard]] std::filesystem::path ResolveStdlibRootDirectory(
    std::string_view explicitDir = {});

/// Expands requested AeroStdlibPackageFlags to the normalized transitive package bitmask.
[[nodiscard]] std::uint32_t ResolveEffectiveStdlibPackageMask(
    std::uint32_t requestedMask) noexcept;

/// Infers minimal AeroStdlibPackageFlags required by a compile input.
[[nodiscard]] std::uint32_t InferRequiredStdlibPackages(
    const AeroV3CompileInput& input) noexcept;

/// Computes a deterministic 64-bit FNV-1a hash over stdlib source paths,
/// contents, and reactive macro mode for process and disk cache keying.
[[nodiscard]] std::uint64_t ComputeStdlibPackHash(
    const std::vector<SourceFile>& sources,
    bool enableReactiveMacros) noexcept;

/// Collects `module <Name>;` identifiers declared across a set of RealScript sources.
[[nodiscard]] std::unordered_set<std::string> CollectDeclaredModuleNames(
    const std::vector<SourceFile>& sources);

/// Discovers Data/Samples/*.xaml, Theme/NoesisTheme.Brushes.*.xaml, and Language-*.xaml
/// under a sample directory for CompiledXamlCatalog emission.
void CollectCatalogXamlEntries(
    const std::filesystem::path& sampleDir,
    std::vector<CatalogXamlEntry>& outCatalogEntries);

/// Removes all dummy MIR function bodies from `module Aero.Native` so optimizers,
/// bytecode lowerers, and C++ AOT emitters keep external runtime binding calls intact.
void StripAeroNativeMirBodies(std::vector<rs_mir::Module>& modules);

} // namespace aero
