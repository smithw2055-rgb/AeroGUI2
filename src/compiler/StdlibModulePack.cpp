#include "aero/compiler/AeroCompilerV3.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace aero {

namespace {

[[nodiscard]] std::string ReadFileUtf8(const std::filesystem::path& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

} // namespace

std::vector<SourceFile> LoadStdlibModulePack(std::string_view stdlibRootDir) {
    std::vector<SourceFile> files;
    std::filesystem::path root(stdlibRootDir);
    if (root.empty()) {
#if defined(AERO_STDLIB_DIR)
        root = std::filesystem::path(AERO_STDLIB_DIR);
#else
        root = std::filesystem::path("stdlib");
#endif
    }
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".rs") {
            SourceFile sf{};
            sf.path = entry.path().generic_string();
            sf.content = ReadFileUtf8(entry.path());
            files.push_back(std::move(sf));
        }
    }
    return files;
}

} // namespace aero
