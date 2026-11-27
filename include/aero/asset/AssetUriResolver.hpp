#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aero {

// Normalizes WPF/Noesis pack://, file://, and ;component/ URIs, joins relative paths
// against baseDir when provided, converts '\\' to '/', and collapses '.' / '..' segments.
[[nodiscard]] std::string NormalizeAssetUriPath(
    std::string_view rawUri,
    std::string_view baseDir = {});

// Resolves and reads a UTF-8 text asset (stripping any UTF-8 BOM) across direct paths,
// registered search directories, fallback base directories, and ancestor sample layouts.
// If outLoadedParentDir is non-null and a file is found, receives the resolved parent directory.
[[nodiscard]] std::string ReadTextAssetWithProbes(
    std::string_view rawUri,
    const std::vector<std::string>& searchDirs,
    const std::vector<std::string>& fallbackBaseDirs,
    std::string* outLoadedParentDir = nullptr);

} // namespace aero
