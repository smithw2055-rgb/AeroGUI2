#include "aero/app/AppBootstrap.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach-o/dyld.h>
#elif !defined(__EMSCRIPTEN__)
#  include <unistd.h>
#endif

namespace aero::app {

namespace {

[[nodiscard]] std::string NormalizePathUpperDrive(const std::filesystem::path& p) {
    std::string out = p.lexically_normal().generic_string();
    if (out.size() >= 2u && out[1] == ':' && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

} // namespace

std::filesystem::path ResolveExecutableDir(const char* argv0) {
#if defined(_WIN32)
    std::wstring buf(1024u, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0u) {
            break;
        }
        if (len < buf.size() - 1u) {
            buf.resize(len);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#elif defined(__APPLE__)
    uint32_t size = 0u;
    (void)_NSGetExecutablePath(nullptr, &size);
    if (size > 0u) {
        std::string p(size, '\0');
        if (_NSGetExecutablePath(p.data(), &size) == 0) {
            return std::filesystem::path(p.c_str()).parent_path();
        }
    }
#elif !defined(__EMSCRIPTEN__)
    std::string buf(1024u, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1u);
        if (n <= 0) {
            break;
        }
        if (static_cast<std::size_t>(n) < buf.size() - 1u) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#endif
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        const auto abs = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec && abs.has_parent_path()) {
            return abs.parent_path();
        }
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::string ReadEnvString(const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }
#ifdef _MSC_VER
    char* buf = nullptr;
    std::size_t len = 0u;
    std::string val;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        if (buf[0] != '\0') {
            val = buf;
        }
        std::free(buf);
    }
    return val;
#else
    if (const char* v = std::getenv(name); v != nullptr && v[0] != '\0') {
        return std::string(v);
    }
    return {};
#endif
}

std::string ResolveDefaultCacheDir() {
    if (const std::string envCache = ReadEnvString("AERO_CACHE_DIR"); !envCache.empty()) {
        return std::filesystem::path(envCache).lexically_normal().generic_string();
    }
#ifdef _WIN32
    std::string baseDir = ReadEnvString("LOCALAPPDATA");
    if (baseDir.empty()) {
        baseDir = ReadEnvString("TEMP");
    }
    if (baseDir.empty()) {
        baseDir = ReadEnvString("TMP");
    }
    if (!baseDir.empty()) {
        return (std::filesystem::path(baseDir) / "AeroGUI2" / "cache").lexically_normal().generic_string();
    }
#else
    if (const std::string xdg = ReadEnvString("XDG_CACHE_HOME"); !xdg.empty()) {
        return (std::filesystem::path(xdg) / "aerogui2").lexically_normal().generic_string();
    }
    if (const std::string home = ReadEnvString("HOME"); !home.empty()) {
        return (std::filesystem::path(home) / ".cache" / "aerogui2").lexically_normal().generic_string();
    }
#endif
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
    if (!ec && !tmp.empty()) {
        return (tmp / "aerogui2_cache").lexically_normal().generic_string();
    }
    return {};
}

double QueryWorkingSetMb() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
    return 0.0;
#elif !defined(__EMSCRIPTEN__)
    std::ifstream in("/proc/self/statm");
    long pagesTotal = 0;
    long pagesRss = 0;
    if (in >> pagesTotal >> pagesRss) {
        const long pageSz = ::sysconf(_SC_PAGESIZE);
        if (pageSz > 0) {
            return static_cast<double>(pagesRss * pageSz) / (1024.0 * 1024.0);
        }
    }
    return 0.0;
#else
    return 0.0;
#endif
}

double QueryPrivateUsageMb() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmce{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmce),
                             sizeof(pmce))) {
        return static_cast<double>(pmce.PrivateUsage) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    return QueryWorkingSetMb();
#endif
}

bool ReadUtf8TextFile(const std::filesystem::path& path, std::string& outText) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    if (outText.size() >= 3u &&
        static_cast<unsigned char>(outText[0]) == 0xEFu &&
        static_cast<unsigned char>(outText[1]) == 0xBBu &&
        static_cast<unsigned char>(outText[2]) == 0xBFu) {
        outText.erase(0u, 3u);
    }
    return true;
}

bool WriteRgba8PpmBuffer(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8) {
    if (!rgba8 || width == 0u || height == 0u || path.empty()) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<char> rgb(pixelCount * 3u);
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        rgb[i * 3u + 0u] = static_cast<char>(rgba8[i * 4u + 0u]);
        rgb[i * 3u + 1u] = static_cast<char>(rgba8[i * 4u + 1u]);
        rgb[i * 3u + 2u] = static_cast<char>(rgba8[i * 4u + 2u]);
    }
    out.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    return out.good();
}

bool HasNonUniformRenderedPixels(const std::vector<std::uint8_t>& rgba) noexcept {
    if (rgba.size() < 4u) {
        return false;
    }
    const std::uint8_t r0 = rgba[0];
    const std::uint8_t g0 = rgba[1];
    const std::uint8_t b0 = rgba[2];
    const std::uint8_t a0 = rgba[3];
    bool anyNonZero = (r0 | g0 | b0 | a0) != 0u;
    bool anyDiff = false;
    for (std::size_t i = 4u; i + 3u < rgba.size(); i += 4u) {
        if ((rgba[i] | rgba[i + 1u] | rgba[i + 2u] | rgba[i + 3u]) != 0u) {
            anyNonZero = true;
        }
        if (rgba[i] != r0 || rgba[i + 1u] != g0 || rgba[i + 2u] != b0 || rgba[i + 3u] != a0) {
            anyDiff = true;
            break;
        }
    }
    return anyNonZero && anyDiff;
}

void DiscoverSampleAssetDirectories(
    const std::filesystem::path& sampleRoot,
    std::vector<std::string>& outDirectories) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (sampleRoot.empty() || !fs::exists(sampleRoot, ec)) {
        return;
    }
    const fs::path root = sampleRoot.lexically_normal();
    std::unordered_set<std::string> seen(outDirectories.begin(), outDirectories.end());
    auto registerDirIfExists = [&](const fs::path& dir) {
        std::error_code dirEc;
        if (!fs::is_directory(dir, dirEc)) {
            return;
        }
        const std::string norm = NormalizePathUpperDrive(dir);
        if (seen.insert(norm).second) {
            outDirectories.push_back(norm);
        }
    };

    const fs::path dataDir = root / "Data";
    const fs::path baseDirs[] = {root, dataDir};
    const char* kSubDirs[] = {
        "",
        "Images",
        "Images/Items",
        "Fonts",
        "Sounds",
        "Samples",
        "Samples/Images",
        "Samples/Fonts",
        "Theme",
        "Theme/Fonts",
    };
    for (const fs::path& base : baseDirs) {
        for (const char* sub : kSubDirs) {
            registerDirIfExists(sub[0] == '\0' ? base : (base / sub));
        }
    }

    std::error_code probeEc;
    fs::path probe = root.is_absolute() ? root : fs::absolute(root, probeEc);
    if (probeEc || probe.empty()) {
        probe = root;
    }
    for (int up = 0; up < 5 && !probe.empty(); ++up) {
        registerDirIfExists(probe / "Theme" / "Data");
        registerDirIfExists(probe / "Theme" / "Data" / "Theme");
        registerDirIfExists(probe / "Theme" / "Data" / "Theme" / "Fonts");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data" / "Theme");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data" / "Theme" / "Fonts");
        const fs::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
}

} // namespace aero::app
