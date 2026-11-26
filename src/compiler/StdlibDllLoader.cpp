#include "aero/compiler/StdlibDllLoader.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "realscript/Version.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/bytecode/Lz4Codec.h"
#include "realscript/compiler/PrecompiledModule.h"
#include "realscript/diagnostics/Diagnostic.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#  include <dlfcn.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

namespace aero {
namespace {

#if defined(_WIN32)
constexpr const char* kStdlibDllFilename = "aerogui_stdlib.dll";
#elif defined(__APPLE__)
constexpr const char* kStdlibDllFilename = "libaerogui_stdlib.dylib";
#else
constexpr const char* kStdlibDllFilename = "libaerogui_stdlib.so";
#endif

[[nodiscard]] std::string ReadEnvString(const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }
#ifdef _MSC_VER
    char* buf = nullptr;
    std::size_t len = 0u;
    std::string out;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        if (buf[0] != '\0') {
            out = buf;
        }
        std::free(buf);
    }
    return out;
#else
    if (const char* v = std::getenv(name); v != nullptr && v[0] != '\0') {
        return std::string(v);
    }
    return {};
#endif
}

[[nodiscard]] std::filesystem::path ResolveCurrentExecutableDir() {
#if defined(_WIN32)
    std::wstring buf(1024u, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0u) {
            return {};
        }
        if (written < buf.size() - 1u) {
            buf.resize(written);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#elif defined(__APPLE__)
    uint32_t size = 0u;
    (void)_NSGetExecutablePath(nullptr, &size);
    if (size > 0u) {
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) == 0) {
            return std::filesystem::path(path.c_str()).parent_path();
        }
    }
    return {};
#elif !defined(__EMSCRIPTEN__)
    std::string buf(1024u, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1u);
        if (n <= 0) {
            return {};
        }
        if (static_cast<std::size_t>(n) < buf.size() - 1u) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path ProbeFileOrDirForStdlibDll(
    const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return {};
    }
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
        return candidate;
    }
    if (std::filesystem::is_directory(candidate, ec)) {
        const auto inDir = candidate / kStdlibDllFilename;
        if (std::filesystem::is_regular_file(inDir, ec)) {
            return inDir;
        }
    }
    return {};
}

[[nodiscard]] std::string CanonicalCacheKey(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
    if (ec || canon.empty()) {
        canon = p.lexically_normal();
    }
    std::string key = canon.generic_string();
#ifdef _WIN32
    for (char& ch : key) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
#endif
    return key;
}

struct StdlibDllCacheState final {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const LoadedStdlibDll>> entries;
    // Keep OS module handles loaded for the process lifetime so zero-copy
    // LazyBytecodeBundle rawBlobData pointers remain valid across compiles/ProgramImages.
    std::unordered_map<std::string, void*> retainedHandlesByKey;
    std::vector<void*> retainedNativeHandles;
};

[[nodiscard]] StdlibDllCacheState& GetStdlibDllCacheState() {
    static StdlibDllCacheState state;
    return state;
}

#if !defined(__EMSCRIPTEN__)
[[nodiscard]] bool LoadNativeStdlibSymbol(
    const std::filesystem::path& path,
    AeroStdlibGetHeaderFn& outFn,
    void*& outHandle,
    std::string& outErr) {
#if defined(_WIN32)
    const std::wstring wpath = path.wstring();
    HMODULE mod = LoadLibraryW(wpath.c_str());
    if (!mod) {
        const DWORD errCode = GetLastError();
        outErr = "LoadLibraryW failed for '" + path.generic_string() +
                 "' (Win32 error " + std::to_string(errCode) + ")";
        return false;
    }
    const FARPROC sym = GetProcAddress(mod, "aero_stdlib_get_header");
    if (!sym) {
        const DWORD errCode = GetLastError();
        FreeLibrary(mod);
        outErr = "GetProcAddress('aero_stdlib_get_header') failed in '" +
                 path.generic_string() + "' (Win32 error " + std::to_string(errCode) + ")";
        return false;
    }
    outFn = reinterpret_cast<AeroStdlibGetHeaderFn>(sym);
    outHandle = static_cast<void*>(mod);
    return true;
#else
    const std::string spath = path.generic_string();
    void* mod = dlopen(spath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!mod) {
        const char* dlErr = dlerror();
        outErr = "dlopen failed for '" + spath + "': " + (dlErr ? dlErr : "unknown error");
        return false;
    }
    void* sym = dlsym(mod, "aero_stdlib_get_header");
    if (!sym) {
        const char* dlErr = dlerror();
        dlclose(mod);
        outErr = "dlsym('aero_stdlib_get_header') failed in '" + spath + "': " +
                 (dlErr ? dlErr : "unknown error");
        return false;
    }
    outFn = reinterpret_cast<AeroStdlibGetHeaderFn>(sym);
    outHandle = mod;
    return true;
#endif
}
#endif

} // namespace

std::uint64_t ComputeStdlibDllChecksum64(
    const void* data,
    std::size_t size) noexcept {
    return rs_bc::fastChecksum64(static_cast<const std::uint8_t*>(data), size, 0u);
}

std::filesystem::path ResolveStdlibDllPath(std::string_view explicitPath) {
    if (!explicitPath.empty()) {
        if (auto found = ProbeFileOrDirForStdlibDll(std::filesystem::path(explicitPath)); !found.empty()) {
            return found;
        }
        return std::filesystem::path(explicitPath);
    }

    if (const std::string envPath = ReadEnvString("AERO_STDLIB_DLL"); !envPath.empty()) {
        if (auto found = ProbeFileOrDirForStdlibDll(std::filesystem::path(envPath)); !found.empty()) {
            return found;
        }
    }

    const std::filesystem::path exeDir = ResolveCurrentExecutableDir();
    std::vector<std::filesystem::path> probes;
    if (!exeDir.empty()) {
        probes.push_back(exeDir / kStdlibDllFilename);
        probes.push_back(exeDir / ".." / "bin" / kStdlibDllFilename);
        probes.push_back(exeDir / ".." / "lib" / kStdlibDllFilename);
    }
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!cwd.empty()) {
        probes.push_back(cwd / kStdlibDllFilename);
        probes.push_back(cwd / "bin" / kStdlibDllFilename);
        probes.push_back(cwd / "lib" / kStdlibDllFilename);
    }

    for (const auto& candidate : probes) {
        if (auto found = ProbeFileOrDirForStdlibDll(candidate); !found.empty()) {
            return found.lexically_normal();
        }
    }
    return {};
}

namespace {
[[nodiscard]] std::shared_ptr<LoadedStdlibDll> LoadAndDecodeStdlibDllLocked(
    const std::filesystem::path& resolved,
    const std::string& key,
    StdlibDllCacheState& state,
    std::string* outError,
    bool decodeFrontendPack = true,
    bool decodeLazyBundle = true) {
#ifdef __EMSCRIPTEN__
    (void)resolved;
    (void)key;
    (void)state;
    (void)decodeFrontendPack;
    (void)decodeLazyBundle;
    if (outError) {
        *outError = "aerogui_stdlib dynamic loading is not supported on Emscripten";
    }
    return nullptr;
#else
    const auto t0 = std::chrono::steady_clock::now();
    AeroStdlibGetHeaderFn getHeaderFn = nullptr;
    void* nativeHandle = nullptr;
    if (const auto hIt = state.retainedHandlesByKey.find(key); hIt != state.retainedHandlesByKey.end()) {
        nativeHandle = hIt->second;
#if defined(_WIN32)
        const FARPROC sym = GetProcAddress(static_cast<HMODULE>(nativeHandle), "aero_stdlib_get_header");
        getHeaderFn = sym ? reinterpret_cast<AeroStdlibGetHeaderFn>(sym) : nullptr;
#else
        getHeaderFn = reinterpret_cast<AeroStdlibGetHeaderFn>(
            dlsym(nativeHandle, "aero_stdlib_get_header"));
#endif
    }
    if (!getHeaderFn) {
        std::string loadErr;
        if (!LoadNativeStdlibSymbol(resolved, getHeaderFn, nativeHandle, loadErr)) {
            if (outError) {
                *outError = std::move(loadErr);
            }
            return nullptr;
        }
        state.retainedHandlesByKey[key] = nativeHandle;
        state.retainedNativeHandles.push_back(nativeHandle);
    }

    const AeroStdlibDllHeader* hdr = getHeaderFn ? getHeaderFn() : nullptr;
    if (!hdr) {
        if (outError) {
            *outError = "aero_stdlib_get_header() returned null in '" + resolved.generic_string() + "'";
        }
        return nullptr;
    }
    if (hdr->magic != AERO_STDLIB_DLL_MAGIC) {
        if (outError) {
            *outError = "Invalid stdlib DLL header magic in '" + resolved.generic_string() + "'";
        }
        return nullptr;
    }
    if (hdr->abiVersion != AERO_STDLIB_DLL_ABI_VERSION ||
        hdr->structSize < sizeof(AeroStdlibDllHeader)) {
        if (outError) {
            *outError = "Incompatible stdlib DLL ABI version (" + std::to_string(hdr->abiVersion) +
                        ") in '" + resolved.generic_string() + "'";
        }
        return nullptr;
    }
    if (hdr->realscriptSdkCompatVersion != realscript::kSdkCompatibilityVersion) {
        if (outError) {
            *outError = "Stdlib DLL RealScript SDK compatibility mismatch: expected " +
                        std::to_string(realscript::kSdkCompatibilityVersion) + ", got " +
                        std::to_string(hdr->realscriptSdkCompatVersion);
        }
        return nullptr;
    }
    if (!hdr->frontendPackBytes || hdr->frontendPackSize == 0u ||
        !hdr->lazyBytecodeBytes || hdr->lazyBytecodeSize == 0u) {
        if (outError) {
            *outError = "Stdlib DLL contains empty payload in '" + resolved.generic_string() + "'";
        }
        return nullptr;
    }

    std::shared_ptr<rs_comp::PrecompiledFrontendPack> decodedPack;
    if (decodeFrontendPack) {
        const std::uint64_t actualFrontendChecksum = ComputeStdlibDllChecksum64(
            hdr->frontendPackBytes, static_cast<std::size_t>(hdr->frontendPackSize));
        if (actualFrontendChecksum != hdr->frontendPackChecksum) {
            if (outError) {
                *outError = "Stdlib DLL frontend pack checksum mismatch in '" + resolved.generic_string() + "'";
            }
            return nullptr;
        }

        decodedPack = std::make_shared<rs_comp::PrecompiledFrontendPack>();
        std::string packErr;
        if (!rs_comp::decodePrecompiledFrontendPack(
                hdr->frontendPackBytes,
                static_cast<std::size_t>(hdr->frontendPackSize),
                *decodedPack,
                &packErr,
                /*copyBlob=*/false,
                /*skipChecksum=*/true)) {
            if (outError) {
                *outError = "Failed to decode PrecompiledFrontendPack from stdlib DLL: " + packErr;
            }
            return nullptr;
        }
    }

    std::shared_ptr<rs_bc::LazyBytecodeBundle> lazyBundle;
    if (decodeLazyBundle) {
        lazyBundle = std::make_shared<rs_bc::LazyBytecodeBundle>();
        rs_diag::DiagnosticBag bundleDiag;
        if (!rs_bc::decodeLazyBytecodeBundleHeader(
                hdr->lazyBytecodeBytes,
                static_cast<std::size_t>(hdr->lazyBytecodeSize),
                *lazyBundle,
                /*copyBlob=*/false,
                bundleDiag,
                /*skipChecksum=*/false) ||
            bundleDiag.hasErrors()) {
            if (outError) {
                std::ostringstream oss;
                oss << "Failed to decode LazyBytecodeBundle header from stdlib DLL:";
                for (const auto& d : bundleDiag.items()) {
                    oss << "\n  " << d.message;
                }
                *outError = oss.str();
            }
            return nullptr;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    auto loaded = std::make_shared<LoadedStdlibDll>();
    loaded->resolvedPath = resolved.lexically_normal().generic_string();
    loaded->header = hdr;
    loaded->frontendPack = std::move(decodedPack);
    loaded->lazyBundle = std::move(lazyBundle);
    loaded->loadAndDecodeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return loaded;
#endif
}
} // namespace

std::shared_ptr<const LoadedStdlibDll> LoadStdlibDll(
    std::string_view explicitPath,
    std::string* outError) {
#ifdef __EMSCRIPTEN__
    (void)explicitPath;
    if (outError) {
        *outError = "aerogui_stdlib dynamic loading is not supported on Emscripten";
    }
    return nullptr;
#else
    const std::filesystem::path resolved = ResolveStdlibDllPath(explicitPath);
    if (resolved.empty()) {
        if (outError) {
            *outError = std::string("Unable to locate ") + kStdlibDllFilename +
                        (explicitPath.empty() ? std::string{} : (" (requested: " + std::string(explicitPath) + ")"));
        }
        return nullptr;
    }
    std::error_code existEc;
    if (!std::filesystem::is_regular_file(resolved, existEc)) {
        if (outError) {
            *outError = "Stdlib DLL not found: " + resolved.generic_string();
        }
        return nullptr;
    }

    const std::string key = CanonicalCacheKey(resolved);
    auto& state = GetStdlibDllCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (const auto it = state.entries.find(key); it != state.entries.end()) {
        return it->second;
    }

    auto loaded = LoadAndDecodeStdlibDllLocked(resolved, key, state, outError);
    if (loaded) {
        state.entries.emplace(key, loaded);
    }
    return loaded;
#endif
}

std::shared_ptr<LoadedStdlibDll> LoadStdlibDllOneShot(
    std::string_view explicitPath,
    std::string* outError) {
#ifdef __EMSCRIPTEN__
    (void)explicitPath;
    if (outError) {
        *outError = "aerogui_stdlib dynamic loading is not supported on Emscripten";
    }
    return nullptr;
#else
    const std::filesystem::path resolved = ResolveStdlibDllPath(explicitPath);
    if (resolved.empty()) {
        if (outError) {
            *outError = std::string("Unable to locate ") + kStdlibDllFilename +
                        (explicitPath.empty() ? std::string{} : (" (requested: " + std::string(explicitPath) + ")"));
        }
        return nullptr;
    }
    std::error_code existEc;
    if (!std::filesystem::is_regular_file(resolved, existEc)) {
        if (outError) {
            *outError = "Stdlib DLL not found: " + resolved.generic_string();
        }
        return nullptr;
    }

    const std::string key = CanonicalCacheKey(resolved);
    auto& state = GetStdlibDllCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return LoadAndDecodeStdlibDllLocked(resolved, key, state, outError, true);
#endif
}

std::shared_ptr<LoadedStdlibDll> LoadStdlibDllLazyOnly(
    std::string_view explicitPath,
    std::string* outError) {
#ifdef __EMSCRIPTEN__
    (void)explicitPath;
    if (outError) {
        *outError = "aerogui_stdlib dynamic loading is not supported on Emscripten";
    }
    return nullptr;
#else
    const std::filesystem::path resolved = ResolveStdlibDllPath(explicitPath);
    if (resolved.empty()) {
        if (outError) {
            *outError = std::string("Unable to locate ") + kStdlibDllFilename +
                        (explicitPath.empty() ? std::string{} : (" (requested: " + std::string(explicitPath) + ")"));
        }
        return nullptr;
    }
    std::error_code existEc;
    if (!std::filesystem::is_regular_file(resolved, existEc)) {
        if (outError) {
            *outError = "Stdlib DLL not found: " + resolved.generic_string();
        }
        return nullptr;
    }

    const std::string key = CanonicalCacheKey(resolved);
    auto& state = GetStdlibDllCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return LoadAndDecodeStdlibDllLocked(resolved, key, state, outError, false, true);
#endif
}

std::shared_ptr<LoadedStdlibDll> LoadStdlibDllFrontendOnly(
    std::string_view explicitPath,
    std::string* outError) {
#ifdef __EMSCRIPTEN__
    (void)explicitPath;
    if (outError) {
        *outError = "aerogui_stdlib dynamic loading is not supported on Emscripten";
    }
    return nullptr;
#else
    const std::filesystem::path resolved = ResolveStdlibDllPath(explicitPath);
    if (resolved.empty()) {
        if (outError) {
            *outError = std::string("Unable to locate ") + kStdlibDllFilename +
                        (explicitPath.empty() ? std::string{} : (" (requested: " + std::string(explicitPath) + ")"));
        }
        return nullptr;
    }
    std::error_code existEc;
    if (!std::filesystem::is_regular_file(resolved, existEc)) {
        if (outError) {
            *outError = "Stdlib DLL not found: " + resolved.generic_string();
        }
        return nullptr;
    }

    const std::string key = CanonicalCacheKey(resolved);
    auto& state = GetStdlibDllCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return LoadAndDecodeStdlibDllLocked(resolved, key, state, outError, true, false);
#endif
}

bool DecodeStdlibDllFrontendPack(
    LoadedStdlibDll& loaded,
    std::string* outError) {
    if (loaded.frontendPack != nullptr) {
        return true;
    }
    const AeroStdlibDllHeader* hdr = loaded.header;
    if (!hdr || !hdr->frontendPackBytes || hdr->frontendPackSize == 0u) {
        if (outError) {
            *outError = "Stdlib DLL header has no frontend pack payload";
        }
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t actualFrontendChecksum = ComputeStdlibDllChecksum64(
        hdr->frontendPackBytes, static_cast<std::size_t>(hdr->frontendPackSize));
    if (actualFrontendChecksum != hdr->frontendPackChecksum) {
        if (outError) {
            *outError = "Stdlib DLL frontend pack checksum mismatch in '" + loaded.resolvedPath + "'";
        }
        return false;
    }
    auto decodedPack = std::make_shared<rs_comp::PrecompiledFrontendPack>();
    std::string packErr;
    if (!rs_comp::decodePrecompiledFrontendPack(
            hdr->frontendPackBytes,
            static_cast<std::size_t>(hdr->frontendPackSize),
            *decodedPack,
            &packErr,
            /*copyBlob=*/false,
            /*skipChecksum=*/true)) {
        if (outError) {
            *outError = "Failed to decode PrecompiledFrontendPack from stdlib DLL: " + packErr;
        }
        return false;
    }
    const auto t1 = std::chrono::steady_clock::now();
    loaded.frontendPack = std::move(decodedPack);
    loaded.loadAndDecodeMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

bool DecodeStdlibDllLazyBundle(
    LoadedStdlibDll& loaded,
    std::string* outError) {
    if (loaded.lazyBundle != nullptr) {
        return true;
    }
    const AeroStdlibDllHeader* hdr = loaded.header;
    if (!hdr || !hdr->lazyBytecodeBytes || hdr->lazyBytecodeSize == 0u) {
        if (outError) {
            *outError = "Stdlib DLL header has no lazy bytecode bundle payload";
        }
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto lazyBundle = std::make_shared<rs_bc::LazyBytecodeBundle>();
    rs_diag::DiagnosticBag bundleDiag;
    if (!rs_bc::decodeLazyBytecodeBundleHeader(
            hdr->lazyBytecodeBytes,
            static_cast<std::size_t>(hdr->lazyBytecodeSize),
            *lazyBundle,
            /*copyBlob=*/false,
            bundleDiag,
            /*skipChecksum=*/false) ||
        bundleDiag.hasErrors()) {
        if (outError) {
            std::ostringstream oss;
            oss << "Failed to decode LazyBytecodeBundle header from stdlib DLL:";
            for (const auto& d : bundleDiag.items()) {
                oss << "\n  " << d.message;
            }
            *outError = oss.str();
        }
        return false;
    }
    const auto t1 = std::chrono::steady_clock::now();
    loaded.lazyBundle = std::move(lazyBundle);
    loaded.loadAndDecodeMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

void ClearStdlibDllCache() noexcept {
    auto& state = GetStdlibDllCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.entries.clear();
}

void ClearStdlibDllCacheForTesting() noexcept {
    ClearStdlibDllCache();
}

} // namespace aero
