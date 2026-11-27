#include "aero/asset/AssetUriResolver.hpp"

#include "aero/core/StringUtils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace aero {

namespace {

[[nodiscard]] std::string ToLowerAsciiCopy(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

[[nodiscard]] std::string_view StripUriSchemeAndComponent(
    std::string_view rawUri,
    bool& outWasPackOrComponent) noexcept {
    outWasPackOrComponent = false;
    const std::string lower = ToLowerAsciiCopy(rawUri);
    const std::size_t compPos = lower.find(";component/");
    if (compPos != std::string::npos) {
        outWasPackOrComponent = true;
        return rawUri.substr(compPos + 11u);
    }
    if (str::StartsWith(lower, "pack://")) {
        outWasPackOrComponent = true;
        const std::size_t tripleComma = lower.find(",,,", 7u);
        if (tripleComma != std::string::npos) {
            std::string_view rest = rawUri.substr(tripleComma + 3u);
            if (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) {
                rest.remove_prefix(1u);
            }
            return rest;
        }
        const std::size_t slashPos = lower.find('/', 7u);
        return (slashPos != std::string::npos) ? rawUri.substr(slashPos + 1u) : rawUri.substr(7u);
    }
    if (str::StartsWith(lower, "file://")) {
        std::string_view rest = rawUri.substr(7u);
        if (rest.size() >= 3u && (rest[0] == '/' || rest[0] == '\\') && rest[2] == ':') {
            rest.remove_prefix(1u);
        }
        return rest;
    }
    return rawUri;
}

} // namespace

std::string NormalizeAssetUriPath(std::string_view rawUri, std::string_view baseDir) {
    rawUri = str::Trim(rawUri);
    if (rawUri.empty()) {
        return {};
    }

    bool wasPackOrComponent = false;
    rawUri = StripUriSchemeAndComponent(rawUri, wasPackOrComponent);
    if (wasPackOrComponent) {
        while (!rawUri.empty() && (rawUri.front() == '/' || rawUri.front() == '\\')) {
            rawUri.remove_prefix(1u);
        }
    }

    std::string unified;
    unified.reserve(baseDir.size() + 1u + rawUri.size());

    const bool isAbsUnix = !rawUri.empty() && rawUri.front() == '/';
    const bool isAbsWin = rawUri.size() >= 2u && rawUri[1] == ':';
    if (!isAbsUnix && !isAbsWin && !baseDir.empty()) {
        unified.append(baseDir);
        if (!unified.empty() && unified.back() != '/' && unified.back() != '\\') {
            unified.push_back('/');
        }
    }
    unified.append(rawUri);

    for (char& ch : unified) {
        if (ch == '\\') {
            ch = '/';
        }
    }

    const bool leadingSlash = !unified.empty() && unified.front() == '/';
    std::vector<std::string_view> segments;
    std::size_t cur = 0u;
    const std::string_view view(unified);
    while (cur < view.size()) {
        const std::size_t slash = view.find('/', cur);
        const std::string_view seg = (slash == std::string_view::npos)
            ? view.substr(cur)
            : view.substr(cur, slash - cur);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                if (!segments.empty() && segments.back() != "..") {
                    segments.pop_back();
                } else if (!leadingSlash) {
                    segments.push_back(seg);
                }
            } else {
                segments.push_back(seg);
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        cur = slash + 1u;
    }

    std::string result;
    result.reserve(unified.size());
    if (leadingSlash) {
        result.push_back('/');
    }
    for (std::size_t i = 0u; i < segments.size(); ++i) {
        if (i > 0u) {
            result.push_back('/');
        }
        result.append(segments[i]);
    }
    return result;
}

std::string ReadTextAssetWithProbes(
    std::string_view rawUri,
    const std::vector<std::string>& searchDirs,
    const std::vector<std::string>& fallbackBaseDirs,
    std::string* outLoadedParentDir) {
    namespace fs = std::filesystem;

    rawUri = str::Trim(rawUri);
    if (rawUri.empty()) {
        return {};
    }

    std::string normalizedRaw(rawUri);
    std::replace(normalizedRaw.begin(), normalizedRaw.end(), '\\', '/');

    bool wasPackOrComponent = false;
    const std::string_view stripped = StripUriSchemeAndComponent(normalizedRaw, wasPackOrComponent);
    std::string directPath = wasPackOrComponent ? std::string(normalizedRaw) : std::string(stripped);
    std::string relPath(stripped);

    while (!relPath.empty()) {
        if (relPath[0] == '/') {
            relPath.erase(0u, 1u);
        } else if (relPath.size() >= 2u && relPath[0] == '.' && relPath[1] == '/') {
            relPath.erase(0u, 2u);
        } else {
            break;
        }
    }

    std::string text;
    auto tryReadFile = [&](const fs::path& candidate) -> bool {
        if (candidate.empty()) {
            return false;
        }
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec) || ec) {
            return false;
        }
        std::ifstream ifs(candidate, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }
        std::string loaded((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (ifs.bad()) {
            return false;
        }
        if (loaded.size() >= 3u &&
            static_cast<unsigned char>(loaded[0]) == 0xEFu &&
            static_cast<unsigned char>(loaded[1]) == 0xBBu &&
            static_cast<unsigned char>(loaded[2]) == 0xBFu) {
            loaded.erase(0u, 3u);
        }
        text = std::move(loaded);
        if (outLoadedParentDir) {
            std::error_code dirEc;
            const fs::path absFile = candidate.is_absolute() ? candidate : fs::absolute(candidate, dirEc);
            const fs::path parentDir = (!dirEc && !absFile.empty()) ? absFile.parent_path() : candidate.parent_path();
            *outLoadedParentDir = parentDir.empty() ? std::string{} : parentDir.generic_string();
        }
        return true;
    };

    if (tryReadFile(fs::path(directPath)) || (!relPath.empty() && tryReadFile(fs::path(relPath)))) {
        return text;
    }
    if (relPath.empty()) {
        return {};
    }

    const fs::path relFs(relPath);
    for (const std::string& searchDir : searchDirs) {
        if (searchDir.empty()) {
            continue;
        }
        const fs::path dirPath(searchDir);
        if (tryReadFile(dirPath / relFs)) {
            return text;
        }
        const fs::path parentDir = dirPath.parent_path();
        if (!parentDir.empty() && tryReadFile(parentDir / relFs)) {
            return text;
        }
    }

    auto probeWithSampleLayouts = [&](const fs::path& base) -> bool {
        return tryReadFile(base / relFs) ||
               tryReadFile(base / "Samples" / "Gallery" / "Data" / relFs) ||
               tryReadFile(base / "Samples" / "Gallery" / "Data" / "Samples" / relFs) ||
               tryReadFile(base / "Samples" / "Theme" / "Data" / relFs) ||
               tryReadFile(base / "Samples" / "Theme" / "Data" / "Theme" / relFs);
    };

    auto probeAncestors = [&](const fs::path& startDir) -> bool {
        if (startDir.empty()) {
            return false;
        }
        std::error_code ec;
        fs::path probe = startDir.is_absolute() ? startDir : fs::absolute(startDir, ec);
        if (ec || probe.empty()) {
            probe = startDir;
        }
        for (int up = 0; up < 6 && !probe.empty(); ++up) {
            if (probeWithSampleLayouts(probe)) {
                return true;
            }
            const fs::path parent = probe.parent_path();
            if (parent == probe) {
                break;
            }
            probe = parent;
        }
        return false;
    };

    for (const std::string& baseDir : fallbackBaseDirs) {
        if (!baseDir.empty() && probeAncestors(fs::path(baseDir))) {
            return text;
        }
    }

    std::error_code cwdEc;
    if (probeAncestors(fs::current_path(cwdEc))) {
        return text;
    }
    return {};
}

} // namespace aero
