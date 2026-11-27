#include "aero/compiler/AeroCompilerV3.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "aero/compiler/MirReachabilityPruner.hpp"
#include "aero/compiler/StdlibDllLoader.hpp"
#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/compiler/XamlToRsEmitter.hpp"
#include "aero/core/ArbcCodec.hpp"
#include "aero/core/StringUtils.hpp"
#include "realscript/Version.h"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/ModuleBuildCache.h"
#include "realscript/compiler/PrecompiledModule.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/diagnostics/Diagnostic.h"
#include "realscript/optimization/Optimizer.h"
#include "realscript/runtime/Runtime.h"

#include <chrono>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#endif

namespace aero {
namespace {

struct SessionSnapshotCache final {
    std::mutex mutex;
    std::unordered_map<const rs_comp::CompilerSession*, rs_comp::BuildSnapshot> snapshots;
};

[[nodiscard]] SessionSnapshotCache& GetSessionSnapshotCache() {
    static SessionSnapshotCache cache;
    return cache;
}

struct StdlibCacheKey final {
    std::string resolvedRoot;
    std::uint64_t stdlibHash = 0u;
    std::uint32_t packageMask = AeroStdlibPkg_All;
    bool enableReactiveMacros = true;

    [[nodiscard]] bool operator==(const StdlibCacheKey& other) const noexcept {
        return stdlibHash == other.stdlibHash &&
               packageMask == other.packageMask &&
               enableReactiveMacros == other.enableReactiveMacros &&
               resolvedRoot == other.resolvedRoot;
    }
};

struct StdlibCacheKeyHash final {
    [[nodiscard]] std::size_t operator()(const StdlibCacheKey& key) const noexcept {
        std::uint64_t h = key.stdlibHash;
        h ^= std::hash<std::string>{}(key.resolvedRoot) + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
        h ^= static_cast<std::uint64_t>(key.packageMask) * 0x9e3779b97f4a7c15ull;
        h ^= key.enableReactiveMacros ? 0x517cc1b727220a95ull : 0x243f6a8885a308d3ull;
        return static_cast<std::size_t>(h);
    }
};

struct ProcessStdlibCacheEntry final {
    std::shared_ptr<rs_comp::CompilerSession> session;
    rs_comp::BuildSnapshot snapshot;
    mutable std::mutex preloweredBcMutex;
    mutable std::unordered_map<std::string, rs_bc::Module> preloweredNonPrunableBc;
};

struct ProcessStdlibCache final {
    std::mutex lookupMutex;
    std::mutex buildMutex;
    std::unordered_map<StdlibCacheKey, std::shared_ptr<const ProcessStdlibCacheEntry>, StdlibCacheKeyHash> entries;
};

[[nodiscard]] ProcessStdlibCache& GetProcessStdlibCache() {
    static ProcessStdlibCache cache;
    return cache;
}

[[nodiscard]] std::string ReadEnvVar(const char* name) {
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

[[nodiscard]] std::filesystem::path ResolveEffectiveCacheDir(const AeroV3CompileInput& input) {
    if (!input.allowDiskCache) {
        return {};
    }
    if (!input.cacheDir.empty()) {
        return std::filesystem::path(input.cacheDir);
    }
    const std::string envDir = ReadEnvVar("AERO_CACHE_DIR");
    if (!envDir.empty()) {
        return std::filesystem::path(envDir);
    }
    return {};
}

[[nodiscard]] std::string Hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] std::uint64_t Fnv1a64(const void* data, std::size_t size) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0u; i < size; ++i) {
        h ^= static_cast<std::uint64_t>(bytes[i]);
        h *= 1099511628211ull;
    }
    return h;
}

[[nodiscard]] std::uint64_t FastChecksum64(const void* data, std::size_t size) noexcept {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(size);
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t i = 0u;
    while (i + 8u <= size) {
        std::uint64_t word = 0u;
        std::memcpy(&word, p + i, 8u);
        h ^= word;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= (h >> 30u);
        i += 8u;
    }
    if (i < size) {
        std::uint64_t tail = 0u;
        std::memcpy(&tail, p + i, size - i);
        h ^= tail;
        h *= 0x94D049BB133111EBULL;
        h ^= (h >> 31u);
    }
    return h;
}

struct SnapMetaEntry final {
    std::string moduleName;
    std::uint64_t sourceFingerprint = 0u;
    std::uint64_t publicFingerprint = 0u;
    std::uint64_t dependencyFingerprint = 0u;
};

constexpr std::uint32_t kSnapMetaVersion = 1u;

[[nodiscard]] std::vector<std::uint8_t> EncodeSnapMeta(
    std::uint64_t stdlibHash,
    bool enableReactiveMacros,
    const rs_comp::BuildSnapshot& snapshot) {
    std::vector<std::uint8_t> buf;
    auto pushBytes = [&](const void* p, std::size_t n) {
        const auto* c = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), c, c + n);
    };
    auto pushU32 = [&](std::uint32_t v) {
        for (int s = 0; s < 32; s += 8) {
            buf.push_back(static_cast<std::uint8_t>((v >> s) & 0xFFu));
        }
    };
    auto pushU64 = [&](std::uint64_t v) {
        for (int s = 0; s < 64; s += 8) {
            buf.push_back(static_cast<std::uint8_t>((v >> s) & 0xFFu));
        }
    };
    auto pushStr = [&](std::string_view s) {
        pushU32(static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) {
            pushBytes(s.data(), s.size());
        }
    };

    pushBytes("ASNP", 4u);
    pushU32(kSnapMetaVersion);
    pushU64(stdlibHash);
    buf.push_back(enableReactiveMacros ? 1u : 0u);
    pushU32(realscript::kVersionMajor);
    pushU32(realscript::kVersionMinor);
    pushU32(realscript::kVersionPatch);
    pushU32(realscript::kSdkCompatibilityVersion);

    std::vector<std::pair<std::string, SnapMetaEntry>> entries;
    entries.reserve(snapshot.modules.size());
    for (const auto& [modName, cachedMod] : snapshot.modules) {
        entries.emplace_back(
            modName,
            SnapMetaEntry{
                modName,
                cachedMod.sourceFingerprint,
                cachedMod.publicFingerprint,
                cachedMod.dependencyFingerprint,
            });
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    pushU32(static_cast<std::uint32_t>(entries.size()));
    for (const auto& [name, entry] : entries) {
        pushStr(name);
        pushU64(entry.sourceFingerprint);
        pushU64(entry.publicFingerprint);
        pushU64(entry.dependencyFingerprint);
    }

    const std::uint64_t checksum = Fnv1a64(buf.data(), buf.size());
    pushU64(checksum);
    return buf;
}

[[nodiscard]] bool DecodeSnapMeta(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t expectedStdlibHash,
    bool expectedReactiveMacros,
    std::unordered_map<std::string, SnapMetaEntry>& outEntries) {
    outEntries.clear();
    if (bytes.size() < 41u) {
        return false;
    }
    const std::size_t payloadSize = bytes.size() - 8u;
    std::size_t pos = 0u;
    auto readU32 = [&](std::uint32_t& out) -> bool {
        if (pos + 4u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 32; s += 8) {
            out |= static_cast<std::uint32_t>(bytes[pos++]) << s;
        }
        return true;
    };
    auto readU64 = [&](std::uint64_t& out) -> bool {
        if (pos + 8u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 64; s += 8) {
            out |= static_cast<std::uint64_t>(bytes[pos++]) << s;
        }
        return true;
    };

    if (std::memcmp(bytes.data(), "ASNP", 4u) != 0) {
        return false;
    }
    pos = 4u;

    std::uint32_t ver = 0u;
    std::uint64_t hash = 0u;
    if (!readU32(ver) || ver != kSnapMetaVersion ||
        !readU64(hash) || hash != expectedStdlibHash ||
        pos >= payloadSize) {
        return false;
    }
    const bool macros = (bytes[pos++] != 0u);
    if (macros != expectedReactiveMacros) {
        return false;
    }

    std::uint32_t vMaj = 0u, vMin = 0u, vPat = 0u, vSdk = 0u;
    if (!readU32(vMaj) || !readU32(vMin) || !readU32(vPat) || !readU32(vSdk) ||
        vMaj != realscript::kVersionMajor ||
        vMin != realscript::kVersionMinor ||
        vPat != realscript::kVersionPatch ||
        vSdk != realscript::kSdkCompatibilityVersion) {
        return false;
    }

    std::uint32_t count = 0u;
    if (!readU32(count) || count == 0u || count > 1024u) {
        return false;
    }
    for (std::uint32_t i = 0u; i < count; ++i) {
        std::uint32_t len = 0u;
        if (!readU32(len) || pos + len > payloadSize) {
            return false;
        }
        std::string modName(reinterpret_cast<const char*>(bytes.data() + pos), len);
        pos += len;
        SnapMetaEntry entry;
        entry.moduleName = modName;
        if (!readU64(entry.sourceFingerprint) ||
            !readU64(entry.publicFingerprint) ||
            !readU64(entry.dependencyFingerprint)) {
            return false;
        }
        outEntries.emplace(std::move(modName), std::move(entry));
    }
    if (pos != payloadSize) {
        return false;
    }

    std::uint64_t storedChecksum = 0u;
    for (int s = 0; s < 64; s += 8) {
        storedChecksum |= static_cast<std::uint64_t>(bytes[pos++]) << s;
    }
    return storedChecksum == Fnv1a64(bytes.data(), payloadSize);
}

[[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::uint8_t>& outBytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const auto sz = in.tellg();
    if (sz < 0) {
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(outBytes.data()), sz);
    }
    return in.good();
}

void WriteBinaryFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    static std::atomic<std::uint64_t> sTmpSeq{0u};
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return;
        }
    }
    const std::uint64_t seq = sTmpSeq.fetch_add(1u, std::memory_order_relaxed);
    const std::filesystem::path tmpPath =
        path.string() + ".tmp." + std::to_string(seq);
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return;
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmpPath, ec);
            return;
        }
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
    }
}

[[nodiscard]] rs_comp::CompilationOptions MakeStdlibCompilationOptions(
    std::shared_ptr<rs_comp::CompilerSession> session,
    bool enableReactiveMacros) {
    rs_comp::CompilationOptions opts;
        opts.collectSymbolOccurrences = false;
        opts.emitDebugInfo = false;
    opts.allowDefaultBuiltinPacks = true;
    opts.session = std::move(session);
    if (enableReactiveMacros) {
        opts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
    }
    return opts;
}

[[nodiscard]] rs_comp::ModuleBuildCacheOptions MakeStdlibDiskCacheOptions(
    const std::filesystem::path& cacheDir,
    bool enableReactiveMacros) {
    rs_comp::ModuleBuildCacheOptions opts;
    opts.cacheDirectory = cacheDir;
    opts.retainMirModules = true;
    opts.emitDeclarationStubs = true;
    if (enableReactiveMacros) {
        opts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
    }
    return opts;
}

[[nodiscard]] bool TryLoadStdlibSnapshotFromDisk(
    const std::filesystem::path& cacheDir,
    const std::vector<SourceFile>& stdlibSources,
    std::uint64_t stdlibHash,
    bool enableReactiveMacros,
    rs_comp::BuildSnapshot& outSnapshot) {
    if (cacheDir.empty()) {
        return false;
    }
    const std::filesystem::path snapMetaPath =
        cacheDir / ("aero_stdlib_" + Hex64(stdlibHash) + ".snapmeta");
    std::vector<std::uint8_t> metaBytes;
    std::unordered_map<std::string, SnapMetaEntry> metaEntries;
    if (!ReadBinaryFile(snapMetaPath, metaBytes) ||
        !DecodeSnapMeta(metaBytes, stdlibHash, enableReactiveMacros, metaEntries)) {
        return false;
    }

    const rs_comp::ModuleBuildCache diskCache(cacheDir);
    const rs_comp::CachedBuildResult diskRes = diskCache.build(
        stdlibSources, MakeStdlibDiskCacheOptions(cacheDir, enableReactiveMacros));
    if (!diskRes.succeeded() || diskRes.statistics.cacheMisses != 0u ||
        diskRes.mirModules.empty()) {
        return false;
    }

    std::unordered_map<std::string, const rs_mir::Module*> mirByName;
    for (const auto& m : diskRes.mirModules) {
        mirByName.emplace(m.name, &m);
    }

    rs_comp::BuildSnapshot snapshot;
    for (const auto& [modName, entry] : metaEntries) {
        const auto mIt = mirByName.find(modName);
        if (mIt == mirByName.end()) {
            return false;
        }
        rs_comp::CachedModule cm;
        cm.sourceFingerprint = entry.sourceFingerprint;
        cm.publicFingerprint = entry.publicFingerprint;
        cm.dependencyFingerprint = entry.dependencyFingerprint;
        cm.module = *mIt->second;
        snapshot.modules.emplace(modName, std::move(cm));
    }
    outSnapshot = std::move(snapshot);
    return true;
}

void PersistStdlibDiskCache(
    const std::filesystem::path& cacheDir,
    const std::vector<SourceFile>& stdlibSources,
    std::uint64_t stdlibHash,
    bool enableReactiveMacros,
    const rs_comp::BuildSnapshot& snapshot) {
    if (cacheDir.empty() || snapshot.modules.empty()) {
        return;
    }
    const rs_comp::ModuleBuildCache diskCache(cacheDir);
    auto diskOpts = MakeStdlibDiskCacheOptions(cacheDir, enableReactiveMacros);
    diskOpts.retainMirModules = false;
    const rs_comp::CachedBuildResult diskRes = diskCache.build(stdlibSources, diskOpts);
    if (!diskRes.succeeded()) {
        return;
    }
    const std::filesystem::path snapMetaPath =
        cacheDir / ("aero_stdlib_" + Hex64(stdlibHash) + ".snapmeta");
    WriteBinaryFileAtomic(
        snapMetaPath, EncodeSnapMeta(stdlibHash, enableReactiveMacros, snapshot));
}

void PopulateStdlibDerivedCache(ProcessStdlibCacheEntry& entry) {
    rs_bc::Lowerer lowerer;
    for (const auto& [modName, cm] : entry.snapshot.modules) {
        if (modName == "Aero.UI") {
            continue;
        }
        std::vector<rs_mir::Module> tmpMods{cm.module};
        StripAeroNativeMirBodies(tmpMods);
        rs_mir::Module& mirMod = tmpMods.front();
        mirMod.sourceFiles.clear();
        for (auto& fn : mirMod.functions) {
            fn.debugInfo.sourceName.clear();
            fn.debugInfo.sourceFileId = 0u;
            fn.debugInfo.declaration = {};
            fn.debugInfo.body = {};
            fn.debugInfo.sequencePoints.clear();
            fn.debugInfo.locals.clear();
        }
        rs_bc::Module bcMod = lowerer.lower(mirMod);
        rs_diag::DiagnosticBag bcDiag;
        if (rs_bc::verifyModule(bcMod, bcDiag) && !bcDiag.hasErrors()) {
            entry.preloweredNonPrunableBc.emplace(modName, std::move(bcMod));
        }
    }
}


[[nodiscard]] bool ResolveStdlibCacheForCompile(
    const std::string& resolvedRootKey,
    const std::vector<SourceFile>& stdlibSources,
    std::uint64_t stdlibHash,
    std::uint32_t packageMask,
    bool enableReactiveMacros,
    const std::filesystem::path& effectiveCacheDir,
    rs_comp::CompilerSession* callerSession,
    std::shared_ptr<rs_comp::CompilerSession>& outSession,
    rs_comp::BuildSnapshot& outSnapshot,
    std::shared_ptr<const ProcessStdlibCacheEntry>& outEntry,
    bool& outCacheHit,
    bool allowDiskSnapshotLoad = true) {
    outCacheHit = false;
    outEntry.reset();
    const StdlibCacheKey key{resolvedRootKey, stdlibHash, packageMask, enableReactiveMacros};
    auto& procCache = GetProcessStdlibCache();

    const std::filesystem::path snapMetaPath = effectiveCacheDir.empty()
        ? std::filesystem::path{}
        : (effectiveCacheDir / ("aero_stdlib_" + Hex64(stdlibHash) + ".snapmeta"));

    {
        std::lock_guard<std::mutex> lock(procCache.lookupMutex);
        if (const auto it = procCache.entries.find(key); it != procCache.entries.end()) {
            std::error_code ec;
            if (snapMetaPath.empty() || std::filesystem::exists(snapMetaPath, ec)) {
                if (callerSession == nullptr) {
                    outSession = it->second->session;
                }
                outEntry = it->second;
                outSnapshot = it->second->snapshot;
                outCacheHit = true;
                return true;
            }
        }
    }

    std::lock_guard<std::mutex> buildLock(procCache.buildMutex);
    {
        std::shared_ptr<const ProcessStdlibCacheEntry> cachedEntry;
        {
            std::lock_guard<std::mutex> lock(procCache.lookupMutex);
            if (const auto it = procCache.entries.find(key); it != procCache.entries.end()) {
                cachedEntry = it->second;
            }
        }
        if (cachedEntry) {
            std::error_code ec;
            if (!snapMetaPath.empty() && !std::filesystem::exists(snapMetaPath, ec)) {
                PersistStdlibDiskCache(
                    effectiveCacheDir,
                    stdlibSources,
                    stdlibHash,
                    enableReactiveMacros,
                    cachedEntry->snapshot);
            }
            if (callerSession == nullptr) {
                outSession = cachedEntry->session;
            }
            outEntry = cachedEntry;
            outSnapshot = cachedEntry->snapshot;
            outCacheHit = true;
            return true;
        }
    }

    // Try cross-process disk cache before running a cold stdlib compilation.
    rs_comp::BuildSnapshot loadedSnap;
    if (allowDiskSnapshotLoad &&
        TryLoadStdlibSnapshotFromDisk(
            effectiveCacheDir, stdlibSources, stdlibHash, enableReactiveMacros, loadedSnap)) {
        auto freshEntry = std::make_shared<ProcessStdlibCacheEntry>();
        freshEntry->session = std::make_shared<rs_comp::CompilerSession>();
        freshEntry->snapshot = loadedSnap;
        PopulateStdlibDerivedCache(*freshEntry);
        {
            std::lock_guard<std::mutex> lock(procCache.lookupMutex);
            procCache.entries[key] = freshEntry;
        }
        if (callerSession == nullptr) {
            outSession = freshEntry->session;
        }
        outEntry = freshEntry;
        outSnapshot = std::move(loadedSnap);
        outCacheHit = true;
        return true;
    }

    // Cold build: compile ONLY stdlibSources into a pure snapshot & warmed session.
    std::shared_ptr<rs_comp::CompilerSession> warmSession;
    if (callerSession != nullptr) {
        warmSession = std::shared_ptr<rs_comp::CompilerSession>(
            callerSession, [](rs_comp::CompilerSession*) {});
    } else {
        warmSession = std::make_shared<rs_comp::CompilerSession>();
    }

    const rs_comp::Compilation stdlibComp(
        stdlibSources, MakeStdlibCompilationOptions(warmSession, enableReactiveMacros));
    rs_comp::BuildResult warmupRes = stdlibComp.build(nullptr);
    if (warmupRes.diagnostics.hasErrors()) {
        return false;
    }

    if (!effectiveCacheDir.empty()) {
        PersistStdlibDiskCache(
            effectiveCacheDir, stdlibSources, stdlibHash, enableReactiveMacros, warmupRes.snapshot);
    }

    // Only store in process-wide cache when compiled with a dedicated owned session.
    if (callerSession == nullptr) {
        auto procEntry = std::make_shared<ProcessStdlibCacheEntry>();
        procEntry->session = std::move(warmSession);
        procEntry->snapshot = warmupRes.snapshot;
        PopulateStdlibDerivedCache(*procEntry);
        {
            std::lock_guard<std::mutex> lock(procCache.lookupMutex);
            procCache.entries[key] = procEntry;
        }
        outSession = procEntry->session;
        outEntry = procEntry;
    }
    outSnapshot = std::move(warmupRes.snapshot);
    outCacheHit = false;
    return true;
}

void AppendDiagnostics(
    const rs_diag::DiagnosticBag& bag,
    std::vector<std::string>& out) {
    for (const auto& entry : bag.items()) {
        const char* sev = (entry.severity == rs_diag::DiagnosticSeverity::Error)
            ? "error"
            : "warning";
        std::string line;
        if (!entry.sourceName.empty()) {
            line += entry.sourceName + ": ";
        }
        line += std::string(sev) + " " + entry.code + ": " + entry.message;
        out.push_back(std::move(line));
    }
}

[[nodiscard]] std::string SanitizeCppIdent(std::string_view raw, std::string_view fallback) {
    std::string out;
    for (unsigned char ch : raw) {
        if (std::isalnum(ch) != 0 || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0) {
        out = std::string(fallback);
    }
    return out;
}

void AppendUint32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

[[nodiscard]] std::vector<std::uint8_t> EncodeProgramBundle(
    const std::vector<rs_bc::Module>& modules) {
    return EncodeArbcBundle(modules);
}

[[nodiscard]] bool DecodeProgramBundle(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<rs_bc::Module>& outModules) {
    return DecodeArbcBundle(data, size, outModules, /*allowSingleModuleFallback=*/false, nullptr);
}

struct DualHash128 final {
    std::uint64_t h1 = 14695981039346656037ull;
    std::uint64_t h2 = 0x9e3779b97f4a7c15ull;

    void mixBytes(const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const unsigned char*>(data);
        std::size_t i = 0u;
        while (i + 8u <= size) {
            std::uint64_t word = 0u;
            std::memcpy(&word, bytes + i, 8u);
            h1 ^= word;
            h1 *= 1099511628211ull;
            h1 ^= (h1 >> 32u);
            h2 ^= word + 0x9e3779b97f4a7c15ull + (h2 << 6u) + (h2 >> 2u);
            h2 *= 11400714819323198485ull;
            h2 ^= (h2 >> 30u);
            i += 8u;
        }
        for (; i < size; ++i) {
            const std::uint64_t b = bytes[i];
            h1 ^= b;
            h1 *= 1099511628211ull;
            h2 ^= b + 0x9e3779b97f4a7c15ull + (h2 << 6u) + (h2 >> 2u);
            h2 *= 11400714819323198485ull;
        }
    }

    void mixU32(std::uint32_t v) noexcept { mixBytes(&v, sizeof(v)); }
    void mixU64(std::uint64_t v) noexcept { mixBytes(&v, sizeof(v)); }
    void mixString(std::string_view s) noexcept {
        mixU64(static_cast<std::uint64_t>(s.size()));
        if (!s.empty()) {
            mixBytes(s.data(), s.size());
        }
    }
    void mixNormalizedPath(std::string_view s) noexcept {
        mixU64(static_cast<std::uint64_t>(s.size()));
        if (s.empty()) {
            return;
        }
        unsigned char buf[256];
        std::size_t pos = 0u;
        while (pos < s.size()) {
            const std::size_t chunk = std::min<std::size_t>(sizeof(buf), s.size() - pos);
            for (std::size_t i = 0u; i < chunk; ++i) {
                char ch = s[pos + i];
                if (ch == '\\') {
                    ch = '/';
                } else if (pos + i == 0u && s.size() >= 2u && s[1] == ':' && ch >= 'a' && ch <= 'z') {
                    ch = static_cast<char>(ch - 'a' + 'A');
                }
                buf[i] = static_cast<unsigned char>(ch);
            }
            mixBytes(buf, chunk);
            pos += chunk;
        }
    }
};

struct FastStdlibStatCache final {
    struct Entry final {
        std::string resolvedRootKey;
        std::uint64_t statFingerprint = 0u;
        std::uint32_t packageMask = AeroStdlibPkg_All;
        bool enableReactiveMacros = true;
        std::uint64_t stdlibHash = 0u;
    };
    std::mutex mutex;
    std::vector<Entry> entries;
};

[[nodiscard]] FastStdlibStatCache& GetFastStdlibStatCache() {
    static FastStdlibStatCache cache;
    return cache;
}

[[nodiscard]] std::uint64_t ComputeStdlibStatFingerprint(
    const std::filesystem::path& resolvedRoot,
    std::string_view resolvedRootKey,
    std::uint32_t packageMask,
    bool enableReactiveMacros) {
    DualHash128 hasher;
    hasher.mixU32(2u);
    hasher.mixU32(packageMask);
    hasher.mixU32(enableReactiveMacros ? 1u : 0u);
    hasher.mixString(resolvedRootKey);

    std::error_code splitEc;
    std::vector<const char*> subdirs;
    subdirs.push_back("Aero.Native");
    if (std::filesystem::is_directory(resolvedRoot / "Aero.UI.Core", splitEc)) {
        subdirs.push_back("Aero.UI.Core");
        if ((packageMask & AeroStdlibPkg_Controls) != 0u) {
            subdirs.push_back("Aero.UI.Controls");
        }
        if ((packageMask & AeroStdlibPkg_RuntimeXaml) != 0u) {
            subdirs.push_back("Aero.UI.RuntimeXaml");
        }
        if ((packageMask & AeroStdlibPkg_Workbench) != 0u) {
            subdirs.push_back("Aero.UI.Workbench");
        }
    } else {
        subdirs.push_back("Aero.UI");
    }

    for (const char* sub : subdirs) {
        const std::filesystem::path dir = resolvedRoot / sub;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        struct StatEntry final {
            std::string name;
            std::uint64_t size = 0u;
            std::int64_t mtime = 0;
        };
        std::vector<StatEntry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file(ec)) {
                continue;
            }
            if (entry.path().extension() != ".rs") {
                continue;
            }
            std::error_code ecSz;
            std::error_code ecMt;
            const auto sz = entry.file_size(ecSz);
            const auto mt = entry.last_write_time(ecMt);
            entries.push_back(StatEntry{
                entry.path().filename().generic_string(),
                ecSz ? 0u : static_cast<std::uint64_t>(sz),
                ecMt ? 0 : static_cast<std::int64_t>(mt.time_since_epoch().count()),
            });
        }
        std::sort(entries.begin(), entries.end(),
            [](const StatEntry& a, const StatEntry& b) { return a.name < b.name; });
        hasher.mixString(sub);
        hasher.mixU64(static_cast<std::uint64_t>(entries.size()));
        for (const auto& e : entries) {
            hasher.mixString(e.name);
            hasher.mixU64(e.size);
            hasher.mixU64(static_cast<std::uint64_t>(e.mtime));
        }
    }
    return hasher.h1 ^ (hasher.h2 * 0x9e3779b97f4a7c15ull);
}

[[nodiscard]] bool TryGetFastStdlibHash(
    std::string_view resolvedRootKey,
    std::uint64_t statFingerprint,
    std::uint32_t packageMask,
    bool enableReactiveMacros,
    const std::filesystem::path& cacheDir,
    std::uint64_t& outStdlibHash) {
    auto& statCache = GetFastStdlibStatCache();
    {
        std::lock_guard<std::mutex> lock(statCache.mutex);
        for (const auto& e : statCache.entries) {
            if (e.statFingerprint == statFingerprint &&
                e.packageMask == packageMask &&
                e.enableReactiveMacros == enableReactiveMacros &&
                e.resolvedRootKey == resolvedRootKey) {
                outStdlibHash = e.stdlibHash;
                return true;
            }
        }
    }

    if (cacheDir.empty()) {
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadBinaryFile(cacheDir / "stdlib_stat_v1.bin", bytes) || bytes.size() < 24u) {
        return false;
    }
    const std::size_t payloadSize = bytes.size() - 8u;
    std::uint64_t storedChecksum = 0u;
    for (int s = 0; s < 64; s += 8) {
        storedChecksum |= static_cast<std::uint64_t>(bytes[payloadSize + static_cast<std::size_t>(s / 8)]) << s;
    }
    if (storedChecksum != FastChecksum64(bytes.data(), payloadSize)) {
        return false;
    }
    if (std::memcmp(bytes.data(), "ASTS", 4u) != 0) {
        return false;
    }
    std::size_t pos = 4u;
    auto readU32 = [&](std::uint32_t& out) -> bool {
        if (pos + 4u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 32; s += 8) {
            out |= static_cast<std::uint32_t>(bytes[pos++]) << s;
        }
        return true;
    };
    auto readU64 = [&](std::uint64_t& out) -> bool {
        if (pos + 8u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 64; s += 8) {
            out |= static_cast<std::uint64_t>(bytes[pos++]) << s;
        }
        return true;
    };

    std::uint32_t ver = 0u;
    std::uint32_t count = 0u;
    if (!readU32(ver) || ver != 2u || !readU32(count) || count > 64u) {
        return false;
    }

    std::vector<FastStdlibStatCache::Entry> loaded;
    loaded.reserve(count);
    for (std::uint32_t i = 0u; i < count; ++i) {
        std::uint32_t len = 0u;
        if (!readU32(len) || pos + len > payloadSize) {
            return false;
        }
        FastStdlibStatCache::Entry e;
        e.resolvedRootKey.assign(reinterpret_cast<const char*>(bytes.data() + pos), len);
        pos += len;
        if (!readU64(e.statFingerprint) || !readU32(e.packageMask) || pos >= payloadSize) {
            return false;
        }
        e.enableReactiveMacros = (bytes[pos++] != 0u);
        if (!readU64(e.stdlibHash)) {
            return false;
        }
        loaded.push_back(std::move(e));
    }
    if (pos != payloadSize) {
        return false;
    }

    bool found = false;
    {
        std::lock_guard<std::mutex> lock(statCache.mutex);
        for (const auto& le : loaded) {
            if (le.statFingerprint == statFingerprint &&
                le.packageMask == packageMask &&
                le.enableReactiveMacros == enableReactiveMacros &&
                le.resolvedRootKey == resolvedRootKey) {
                outStdlibHash = le.stdlibHash;
                found = true;
            }
            bool exists = false;
            for (const auto& cur : statCache.entries) {
                if (cur.statFingerprint == le.statFingerprint &&
                    cur.packageMask == le.packageMask &&
                    cur.enableReactiveMacros == le.enableReactiveMacros &&
                    cur.resolvedRootKey == le.resolvedRootKey) {
                    exists = true;
                    break;
                }
            }
            if (!exists && statCache.entries.size() < 16u) {
                statCache.entries.push_back(le);
            }
        }
    }
    return found;
}

void RecordFastStdlibHash(
    std::string_view resolvedRootKey,
    std::uint64_t statFingerprint,
    std::uint32_t packageMask,
    bool enableReactiveMacros,
    const std::filesystem::path& cacheDir,
    std::uint64_t stdlibHash) {
    auto& statCache = GetFastStdlibStatCache();
    std::vector<FastStdlibStatCache::Entry> snapshot;
    {
        std::lock_guard<std::mutex> lock(statCache.mutex);
        bool updated = false;
        for (auto& e : statCache.entries) {
            if (e.packageMask == packageMask &&
                e.enableReactiveMacros == enableReactiveMacros &&
                e.resolvedRootKey == resolvedRootKey) {
                e.statFingerprint = statFingerprint;
                e.stdlibHash = stdlibHash;
                updated = true;
                break;
            }
        }
        if (!updated) {
            if (statCache.entries.size() >= 16u) {
                statCache.entries.erase(statCache.entries.begin());
            }
            statCache.entries.push_back(FastStdlibStatCache::Entry{
                std::string(resolvedRootKey),
                statFingerprint,
                packageMask,
                enableReactiveMacros,
                stdlibHash,
            });
        }
        snapshot = statCache.entries;
    }

    if (cacheDir.empty()) {
        return;
    }

    std::vector<std::uint8_t> buf;
    buf.reserve(512u);
    auto pushU32 = [&](std::uint32_t v) { AppendUint32Le(buf, v); };
    auto pushU64 = [&](std::uint64_t v) {
        for (int s = 0; s < 64; s += 8) {
            buf.push_back(static_cast<std::uint8_t>((v >> s) & 0xFFu));
        }
    };
    const auto pushBytes = [&](const void* p, std::size_t n) {
        const auto* c = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), c, c + n);
    };

    pushBytes("ASTS", 4u);
    pushU32(2u);
    pushU32(static_cast<std::uint32_t>(snapshot.size()));
    for (const auto& e : snapshot) {
        pushU32(static_cast<std::uint32_t>(e.resolvedRootKey.size()));
        if (!e.resolvedRootKey.empty()) {
            pushBytes(e.resolvedRootKey.data(), e.resolvedRootKey.size());
        }
        pushU64(e.statFingerprint);
        pushU32(e.packageMask);
        buf.push_back(e.enableReactiveMacros ? 1u : 0u);
        pushU64(e.stdlibHash);
    }
    const std::uint64_t checksum = FastChecksum64(buf.data(), buf.size());
    pushU64(checksum);
    WriteBinaryFileAtomic(cacheDir / "stdlib_stat_v1.bin", buf);
}

constexpr std::uint32_t kProgramCacheVersion = 12u;

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> ComputeRawInputBundleKey(
    const AeroV3CompileInput& input,
    std::uint32_t resolvedPackages,
    std::uint64_t stdlibHash) noexcept {
    DualHash128 hasher;
    hasher.mixU32(kProgramCacheVersion);
    hasher.mixU32(realscript::kVersionMajor);
    hasher.mixU32(realscript::kVersionMinor);
    hasher.mixU32(realscript::kVersionPatch);
    hasher.mixU32(realscript::kSdkCompatibilityVersion);
    const std::uint32_t flags =
        (input.includeStdlib ? 1u : 0u) |
        (input.treeShakeStdlib ? 2u : 0u) |
        (input.optimizeBytecode ? 4u : 0u) |
        (input.enableReactiveMacros ? 8u : 0u) |
        (input.useStdlibDll ? 16u : 0u);
    hasher.mixU32(flags);
    hasher.mixU32(resolvedPackages);
    hasher.mixU64(stdlibHash);

    hasher.mixNormalizedPath(input.xamlPath);
    hasher.mixString(input.xamlContent);
    hasher.mixNormalizedPath(input.resourceSearchRoot);
    hasher.mixU64(static_cast<std::uint64_t>(input.resourceSearchPaths.size()));
    for (const auto& p : input.resourceSearchPaths) {
        hasher.mixNormalizedPath(p);
    }

    hasher.mixU64(static_cast<std::uint64_t>(input.additionalXamlSources.size()));
    for (const auto& sf : input.additionalXamlSources) {
        hasher.mixNormalizedPath(sf.path);
        hasher.mixString(sf.content);
    }

    hasher.mixU64(static_cast<std::uint64_t>(input.catalogXamlSources.size()));
    for (const auto& cat : input.catalogXamlSources) {
        hasher.mixString(cat.uri);
        hasher.mixU32(cat.isResourceDictionary ? 1u : 0u);
        hasher.mixU64(static_cast<std::uint64_t>(cat.aliasUris.size()));
        for (const auto& alias : cat.aliasUris) {
            hasher.mixString(alias);
        }
        hasher.mixNormalizedPath(cat.xamlPath);
        hasher.mixString(cat.xamlContent);
    }

    hasher.mixU64(static_cast<std::uint64_t>(input.scriptSources.size()));
    for (const auto& sf : input.scriptSources) {
        hasher.mixNormalizedPath(sf.path);
        hasher.mixString(sf.content);
    }
    return {hasher.h1, hasher.h2};
}

[[nodiscard]] std::filesystem::path MakeProgramCacheFilePath(
    const std::filesystem::path& cacheDir,
    std::uint64_t h1,
    std::uint64_t h2) {
    return cacheDir / "programs" / ("prog_" + Hex64(h1) + "_" + Hex64(h2) + ".aprg");
}

[[nodiscard]] bool TryLoadProgramBundleFromDisk(
    const std::filesystem::path& cacheFilePath,
    std::uint64_t expectedH1,
    std::uint64_t expectedH2,
    bool skipRawBytecodeOutput,
    bool retainBytecodeModules,
    AeroV3CompileResult& ioResult,
    std::shared_ptr<rs_bc::LazyBytecodeBundle> lazyBaseBundle = nullptr,
    bool consumeLazyHeader = false) {
    std::vector<std::uint8_t> bytes;
    if (!ReadBinaryFile(cacheFilePath, bytes) || bytes.size() < 88u) {
        return false;
    }
    const std::size_t payloadSize = bytes.size() - 8u;
    std::size_t pos = 0u;
    auto readU32 = [&](std::uint32_t& out) -> bool {
        if (pos + 4u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 32; s += 8) {
            out |= static_cast<std::uint32_t>(bytes[pos++]) << s;
        }
        return true;
    };
    auto readU64 = [&](std::uint64_t& out) -> bool {
        if (pos + 8u > payloadSize) return false;
        out = 0u;
        for (int s = 0; s < 64; s += 8) {
            out |= static_cast<std::uint64_t>(bytes[pos++]) << s;
        }
        return true;
    };
    auto readString = [&](std::string& out, bool assignValue = true) -> bool {
        std::uint32_t len = 0u;
        if (!readU32(len) || pos + len > payloadSize) {
            return false;
        }
        if (assignValue) {
            out.assign(reinterpret_cast<const char*>(bytes.data() + pos), len);
        }
        pos += len;
        return true;
    };

    std::uint64_t storedChecksum = 0u;
    for (int s = 0; s < 64; s += 8) {
        storedChecksum |= static_cast<std::uint64_t>(bytes[payloadSize + static_cast<std::size_t>(s / 8)]) << s;
    }
    if (storedChecksum != FastChecksum64(bytes.data(), payloadSize)) {
        return false;
    }

    if (std::memcmp(bytes.data(), "APRG", 4u) != 0) {
        return false;
    }
    pos = 4u;

    std::uint32_t ver = 0u;
    std::uint64_t h1 = 0u;
    std::uint64_t h2 = 0u;
    if (!readU32(ver) || ver != kProgramCacheVersion ||
        !readU64(h1) || h1 != expectedH1 ||
        !readU64(h2) || h2 != expectedH2) {
        return false;
    }

    std::uint64_t retainedTypes = 0u, prunedTypes = 0u, retainedFns = 0u, prunedFns = 0u;
    if (!readU64(retainedTypes) || !readU64(prunedTypes) ||
        !readU64(retainedFns) || !readU64(prunedFns)) {
        return false;
    }

    std::string rootClassName;
    std::string genRsPath;
    std::string genRsCode;
    if (!readString(rootClassName) ||
        !readString(genRsPath) ||
        !readString(genRsCode, !skipRawBytecodeOutput)) {
        return false;
    }

    std::uint32_t extDepCount = 0u;
    if (!readU32(extDepCount) || extDepCount > 4096u) {
        return false;
    }
    std::vector<std::uint8_t> depBuf;
    for (std::uint32_t i = 0u; i < extDepCount; ++i) {
        std::string depPath;
        std::uint64_t depHash = 0u;
        if (!readString(depPath) || !readU64(depHash) || depPath.empty()) {
            return false;
        }
        if (!ReadBinaryFile(std::filesystem::path(depPath), depBuf) ||
            FastChecksum64(depBuf.data(), depBuf.size()) != depHash) {
            return false;
        }
    }

    std::uint32_t diagCount = 0u;
    if (!readU32(diagCount) || diagCount > 4096u) {
        return false;
    }
    std::vector<std::string> storedDiags;
    storedDiags.reserve(diagCount);
    for (std::uint32_t i = 0u; i < diagCount; ++i) {
        std::string d;
        if (!readString(d)) {
            return false;
        }
        storedDiags.push_back(std::move(d));
    }

    std::uint32_t arbcLen = 0u;
    if (!readU32(arbcLen) || pos + arbcLen != payloadSize) {
        return false;
    }

    std::vector<rs_bc::Module> modules;
    if (!DecodeProgramBundle(bytes.data() + pos, arbcLen, modules)) {
        return false;
    }
    const std::size_t arbcOffset = pos;
    if (skipRawBytecodeOutput) {
        std::vector<std::uint8_t>().swap(bytes);
    }

    const bool hasLazyBase = (lazyBaseBundle != nullptr);
    rs_rt::RuntimeError linkErr;
    std::optional<ProgramImage> linked;
    if (hasLazyBase) {
        if (retainBytecodeModules) {
            auto userCopy = modules;
            linked = consumeLazyHeader
                ? ProgramImage::linkLayeredConsumeHeader(
                      std::move(lazyBaseBundle),
                      std::move(userCopy),
                      linkErr,
                      /*verifyUserModules=*/false)
                : ProgramImage::linkLayered(
                      lazyBaseBundle,
                      std::move(userCopy),
                      linkErr,
                      /*verifyUserModules=*/false);
        } else {
            linked = consumeLazyHeader
                ? ProgramImage::linkLayeredConsumeHeader(
                      std::move(lazyBaseBundle),
                      std::move(modules),
                      linkErr,
                      /*verifyUserModules=*/false)
                : ProgramImage::linkLayered(
                      lazyBaseBundle,
                      std::move(modules),
                      linkErr,
                      /*verifyUserModules=*/false);
            std::vector<rs_bc::Module>().swap(modules);
        }
    } else {
        linked = retainBytecodeModules
            ? ProgramImage::link(modules, linkErr, /*verifyModules=*/false)
            : ProgramImage::link(std::move(modules), linkErr, /*verifyModules=*/false);
    }
    if (!linked.has_value()) {
        return false;
    }
    if (!retainBytecodeModules) {
        linked->compactForRuntime();
    }

    ioResult.retainedMirTypes = static_cast<std::size_t>(retainedTypes);
    ioResult.prunedMirTypes = static_cast<std::size_t>(prunedTypes);
    ioResult.retainedMirFunctions = static_cast<std::size_t>(retainedFns);
    ioResult.prunedMirFunctions = static_cast<std::size_t>(prunedFns);
    ioResult.rootClassName = std::move(rootClassName);
    ioResult.generatedPartialRsPath = std::move(genRsPath);
    if (!hasLazyBase && !skipRawBytecodeOutput) {
        ioResult.generatedPartialRsCode = std::move(genRsCode);
        ioResult.programImageBytes.assign(bytes.data() + arbcOffset, bytes.data() + arbcOffset + arbcLen);
    }
    ioResult.diagnostics.insert(
        ioResult.diagnostics.end(), storedDiags.begin(), storedDiags.end());
    if (retainBytecodeModules) {
        ioResult.bytecodeModules = std::move(modules);
    }
    if (hasLazyBase) {
        ioResult.usedStdlibDll = true;
        ioResult.usedStdlibCache = false;
    }
    ioResult.programImage = std::make_shared<const ProgramImage>(std::move(*linked));
    ioResult.succeeded = true;
    return true;
}

void PersistProgramBundleToDisk(
    const std::filesystem::path& cacheFilePath,
    std::uint64_t h1,
    std::uint64_t h2,
    const AeroV3CompileResult& result,
    const std::vector<std::uint8_t>& encodedArbcBytes,
    const std::vector<ExternalXamlDep>& externalDeps) {
    if (cacheFilePath.empty() || !result.succeeded || encodedArbcBytes.empty()) {
        return;
    }
    std::vector<std::uint8_t> buf;
    buf.reserve(encodedArbcBytes.size() + result.generatedPartialRsCode.size() + 512u);
    auto pushBytes = [&](const void* p, std::size_t n) {
        const auto* c = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), c, c + n);
    };
    auto pushU32 = [&](std::uint32_t v) { AppendUint32Le(buf, v); };
    auto pushU64 = [&](std::uint64_t v) {
        for (int s = 0; s < 64; s += 8) {
            buf.push_back(static_cast<std::uint8_t>((v >> s) & 0xFFu));
        }
    };
    auto pushString = [&](std::string_view s) {
        pushU32(static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) {
            pushBytes(s.data(), s.size());
        }
    };

    pushBytes("APRG", 4u);
    pushU32(kProgramCacheVersion);
    pushU64(h1);
    pushU64(h2);
    pushU64(static_cast<std::uint64_t>(result.retainedMirTypes));
    pushU64(static_cast<std::uint64_t>(result.prunedMirTypes));
    pushU64(static_cast<std::uint64_t>(result.retainedMirFunctions));
    pushU64(static_cast<std::uint64_t>(result.prunedMirFunctions));

    pushString(result.rootClassName);
    pushString(result.generatedPartialRsPath);
    pushString(result.generatedPartialRsCode);

    pushU32(static_cast<std::uint32_t>(externalDeps.size()));
    for (const auto& dep : externalDeps) {
        pushString(dep.resolvedPath);
        pushU64(dep.contentHash);
    }

    pushU32(static_cast<std::uint32_t>(result.diagnostics.size()));
    for (const auto& d : result.diagnostics) {
        pushString(d);
    }

    pushU32(static_cast<std::uint32_t>(encodedArbcBytes.size()));
    pushBytes(encodedArbcBytes.data(), encodedArbcBytes.size());

    const std::uint64_t checksum = FastChecksum64(buf.data(), buf.size());
    pushU64(checksum);
    WriteBinaryFileAtomic(cacheFilePath, buf);
}

void CompactProcessHeapAfterCompile() noexcept {
#if defined(_WIN32)
    (void)_heapmin();
    (void)HeapCompact(GetProcessHeap(), 0u);
#endif

#if defined(__GLIBC__)
    (void)::malloc_trim(0);
#endif
}

} // namespace

AeroV3CompileResult AeroCompilerV3::Compile(const AeroV3CompileInput& input) const {
    AeroV3CompileResult result;

    const std::filesystem::path effectiveCacheDir = ResolveEffectiveCacheDir(input);
    const bool sessionAllowsEarlyHit =
        (input.compilerSession == nullptr) ||
        (input.compilerSession->parsedUnitCacheSize() > 0u);
    const bool canUseRawProgramCache =
        !effectiveCacheDir.empty() &&
        !input.emitAotCpp &&
        sessionAllowsEarlyHit &&
        !input.externalXamlResolver;

    const bool useDllMode = input.includeStdlib && input.useStdlibDll && !input.emitAotCpp;
    std::shared_ptr<const LoadedStdlibDll> loadedDll;
    std::shared_ptr<LoadedStdlibDll> oneShotLoadedDll;
    if (useDllMode) {
        std::string dllErr;
        if (input.retainStdlibDllCache) {
            loadedDll = LoadStdlibDll(input.stdlibDllPath, &dllErr);
        } else if (canUseRawProgramCache) {
            oneShotLoadedDll = LoadStdlibDllLazyOnly(input.stdlibDllPath, &dllErr);
            loadedDll = oneShotLoadedDll;
        } else {
            oneShotLoadedDll = LoadStdlibDllFrontendOnly(input.stdlibDllPath, &dllErr);
            loadedDll = oneShotLoadedDll;
        }
        const bool needFrontendNow = !canUseRawProgramCache || input.retainStdlibDllCache;
        const bool needLazyNow = canUseRawProgramCache || input.retainStdlibDllCache;
        if (!loadedDll || !loadedDll->header ||
            (needLazyNow && !loadedDll->lazyBundle) ||
            (needFrontendNow && !loadedDll->frontendPack)) {
            result.diagnostics.emplace_back(
                dllErr.empty()
                    ? std::string("StdlibDllLoader: failed to load aerogui_stdlib dynamic library")
                    : ("StdlibDllLoader: " + dllErr));
            return result;
        }
        result.stdlibDllLoadMs = loadedDll->loadAndDecodeMs;
    }

    const std::uint32_t effectiveStdlibPackages =
        useDllMode
            ? AeroStdlibPkg_All
            : (input.stdlibPackages != AeroStdlibPkg_Auto
                   ? ResolveEffectiveStdlibPackageMask(input.stdlibPackages)
                   : (input.compilerSession != nullptr
                          ? AeroStdlibPkg_All
                          : InferRequiredStdlibPackages(input)));
    result.resolvedStdlibPackages = input.includeStdlib ? effectiveStdlibPackages : 0u;

    std::vector<SourceFile> stdlibSources;
    std::string resolvedStdlibRootKey;
    std::filesystem::path resolvedStdlibRoot;
    std::uint64_t stdlibStatFp = 0u;
    std::uint64_t stdlibHash = 0u;
    if (useDllMode) {
        stdlibHash = loadedDll->header->frontendPackChecksum ^
            (loadedDll->header->lazyBytecodeChecksum * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(loadedDll->header->abiVersion) << 32u) ^
            loadedDll->header->frontendPackSize ^
            (loadedDll->header->lazyBytecodeSize << 17u);
    } else if (input.includeStdlib) {
        resolvedStdlibRoot = ResolveStdlibRootDirectory(input.stdlibRootDir);
        if (resolvedStdlibRoot.empty()) {
            result.diagnostics.emplace_back(
                "StdlibModulePack: failed to locate stdlib Aero.Native / Aero.UI sources");
            return result;
        }
        std::error_code ec;
        const auto canon = std::filesystem::weakly_canonical(resolvedStdlibRoot, ec);
        resolvedStdlibRootKey = (!ec && !canon.empty())
            ? canon.lexically_normal().generic_string()
            : resolvedStdlibRoot.lexically_normal().generic_string();
        stdlibStatFp = ComputeStdlibStatFingerprint(
            resolvedStdlibRoot, resolvedStdlibRootKey, effectiveStdlibPackages, input.enableReactiveMacros);
        if (!TryGetFastStdlibHash(
                resolvedStdlibRootKey,
                stdlibStatFp,
                effectiveStdlibPackages,
                input.enableReactiveMacros,
                effectiveCacheDir,
                stdlibHash)) {
            stdlibSources = LoadStdlibModulePack(input.stdlibRootDir, effectiveStdlibPackages);
            if (stdlibSources.empty()) {
                result.diagnostics.emplace_back(
                    "StdlibModulePack: failed to locate stdlib Aero.Native / Aero.UI sources");
                return result;
            }
            stdlibHash = ComputeStdlibPackHash(stdlibSources, input.enableReactiveMacros);
            RecordFastStdlibHash(
                resolvedStdlibRootKey,
                stdlibStatFp,
                effectiveStdlibPackages,
                input.enableReactiveMacros,
                effectiveCacheDir,
                stdlibHash);
        }
    }

    std::uint64_t progH1 = 0u;
    std::uint64_t progH2 = 0u;
    std::filesystem::path progCacheFilePath;
    if (canUseRawProgramCache) {
        const auto [h1, h2] = ComputeRawInputBundleKey(input, effectiveStdlibPackages, stdlibHash);
        progH1 = h1;
        progH2 = h2;
        progCacheFilePath = MakeProgramCacheFilePath(effectiveCacheDir, progH1, progH2);
        if (TryLoadProgramBundleFromDisk(
                progCacheFilePath,
                progH1,
                progH2,
                input.skipRawBytecodeOutput,
                input.retainBytecodeModules,
                result,
                useDllMode ? loadedDll->lazyBundle : nullptr,
                useDllMode && oneShotLoadedDll != nullptr)) {
            if (useDllMode) {
                result.stdlibDllLoadMs = loadedDll->loadAndDecodeMs;
                oneShotLoadedDll.reset();
                loadedDll.reset();
                if (!input.retainBytecodeModules) {
                    CompactProcessHeapAfterCompile();
                }
            } else {
                result.usedStdlibCache = input.includeStdlib;
            }
            return result;
        }
    }

    if (useDllMode && oneShotLoadedDll && !oneShotLoadedDll->frontendPack) {
        std::string packErr;
        if (!DecodeStdlibDllFrontendPack(*oneShotLoadedDll, &packErr)) {
            result.diagnostics.emplace_back(
                packErr.empty()
                    ? std::string("StdlibDllLoader: failed to decode PrecompiledFrontendPack")
                    : ("StdlibDllLoader: " + packErr));
            return result;
        }
        result.stdlibDllLoadMs = oneShotLoadedDll->loadAndDecodeMs;
    }

    if (input.includeStdlib && !useDllMode && stdlibSources.empty()) {
        stdlibSources = LoadStdlibModulePack(input.stdlibRootDir, effectiveStdlibPackages);
        if (stdlibSources.empty()) {
            result.diagnostics.emplace_back(
                "StdlibModulePack: failed to locate stdlib Aero.Native / Aero.UI sources");
            return result;
        }
        const std::uint64_t exactHash = ComputeStdlibPackHash(stdlibSources, input.enableReactiveMacros);
        if (exactHash != stdlibHash) {
            stdlibHash = exactHash;
            RecordFastStdlibHash(
                resolvedStdlibRootKey,
                stdlibStatFp,
                effectiveStdlibPackages,
                input.enableReactiveMacros,
                effectiveCacheDir,
                stdlibHash);
            if (canUseRawProgramCache) {
                const auto [h1, h2] = ComputeRawInputBundleKey(input, effectiveStdlibPackages, stdlibHash);
                progH1 = h1;
                progH2 = h2;
                progCacheFilePath = MakeProgramCacheFilePath(effectiveCacheDir, progH1, progH2);
            }
        }
    }

    // Emit primary XAML -> .g.rs
    XamlEmitResult xamlRes;
    if (!input.xamlContent.empty()) {
        XamlEmitOptions emitOpts;
        emitOpts.xamlPath = input.xamlPath.empty() ? "MainView.xaml" : input.xamlPath;
        emitOpts.externalXamlResolver = input.externalXamlResolver;
        emitOpts.resourceSearchRoot = input.resourceSearchRoot;
        emitOpts.resourceSearchPaths = input.resourceSearchPaths;
        emitOpts.pruneUnreferencedExternalStyles = true;
        emitOpts.usageHintTexts.reserve(
            input.additionalXamlSources.size() + input.scriptSources.size());
        for (const auto& addSf : input.additionalXamlSources) {
            if (!addSf.content.empty()) {
                emitOpts.usageHintTexts.push_back(addSf.content);
            }
        }
        for (const auto& sf : input.scriptSources) {
            if (!sf.content.empty()) {
                emitOpts.usageHintTexts.push_back(sf.content);
            }
        }
        xamlRes = EmitXamlToRealScript(input.xamlContent, emitOpts);
        result.generatedPartialRsPath = xamlRes.generatedFilePath;
        if (!input.skipRawBytecodeOutput) {
            result.generatedPartialRsCode = xamlRes.generatedRsCode;
        }
        result.rootClassName = xamlRes.rootClassName;
        result.diagnostics.insert(
            result.diagnostics.end(),
            xamlRes.diagnostics.begin(),
            xamlRes.diagnostics.end());
        if (!xamlRes.succeeded) {
            return result;
        }
    }

    // Emit additional XAMLs (child UserControls, Resources.xaml, App.xaml) -> .g.rs
    std::vector<XamlEmitResult> additionalXamlResults;
    additionalXamlResults.reserve(input.additionalXamlSources.size());
    for (std::size_t idx = 0u; idx < input.additionalXamlSources.size(); ++idx) {
        const auto& addSf = input.additionalXamlSources[idx];
        if (addSf.content.empty()) {
            continue;
        }
        XamlEmitOptions addOpts;
        addOpts.xamlPath = addSf.path.empty()
            ? ("AdditionalView_" + std::to_string(idx) + ".xaml")
            : addSf.path;
        addOpts.externalXamlResolver = input.externalXamlResolver;
        addOpts.resourceSearchRoot = input.resourceSearchRoot;
        addOpts.resourceSearchPaths = input.resourceSearchPaths;
        addOpts.pruneUnreferencedExternalStyles = true;
        addOpts.usageHintTexts.reserve(
            (input.xamlContent.empty() ? 0u : 1u) +
            input.additionalXamlSources.size() +
            input.scriptSources.size());
        if (!input.xamlContent.empty()) {
            addOpts.usageHintTexts.push_back(input.xamlContent);
        }
        for (std::size_t other = 0u; other < input.additionalXamlSources.size(); ++other) {
            if (other != idx && !input.additionalXamlSources[other].content.empty()) {
                addOpts.usageHintTexts.push_back(input.additionalXamlSources[other].content);
            }
        }
        for (const auto& sf : input.scriptSources) {
            if (!sf.content.empty()) {
                addOpts.usageHintTexts.push_back(sf.content);
            }
        }
        XamlEmitResult addRes = EmitXamlToRealScript(addSf.content, addOpts);
        result.diagnostics.insert(
            result.diagnostics.end(),
            addRes.diagnostics.begin(),
            addRes.diagnostics.end());
        if (!addRes.succeeded) {
            return result;
        }
        additionalXamlResults.push_back(std::move(addRes));
    }

    std::vector<ExternalXamlDep> mergedExternalDeps;
    {
        std::unordered_set<std::string> seenDeps;
        auto appendDeps = [&](const std::vector<ExternalXamlDep>& deps) {
            for (const auto& dep : deps) {
                if (!dep.resolvedPath.empty() && seenDeps.insert(dep.resolvedPath).second) {
                    mergedExternalDeps.push_back(dep);
                }
            }
        };
        appendDeps(xamlRes.externalDependencies);
        for (const auto& addRes : additionalXamlResults) {
            appendDeps(addRes.externalDependencies);
        }
    }

    // Emit build-time CompiledXamlCatalog entries into module __AeroCompiledCatalog
    std::vector<SourceFile> catalogGeneratedSources;
    if (!input.catalogXamlSources.empty()) {
        auto escapeRsLiteral = [](std::string_view text) {
            std::string out;
            out.reserve(text.size() + 8u);
            out.push_back('"');
            for (const char ch : text) {
                if (ch == '\\' || ch == '"') {
                    out.push_back('\\');
                }
                out.push_back(ch);
            }
            out.push_back('"');
            return out;
        };

        std::ostringstream regBody;
        std::size_t okCount = 0u;
        for (std::size_t i = 0u; i < input.catalogXamlSources.size(); ++i) {
            const CatalogXamlEntry& entry = input.catalogXamlSources[i];
            if (entry.xamlContent.empty() || entry.uri.empty()) {
                continue;
            }
            XamlEmitOptions catOpts;
            catOpts.xamlPath = entry.xamlPath.empty() ? entry.uri : entry.xamlPath;
            catOpts.defaultModuleName = "__AeroCompiledCatalog";
            catOpts.classNameOverride = "__CatalogItem_" + std::to_string(i);
            catOpts.forceStandaloneClass = true;
            catOpts.externalXamlResolver = input.externalXamlResolver;
            catOpts.resourceSearchRoot = input.resourceSearchRoot;
            catOpts.resourceSearchPaths = input.resourceSearchPaths;
            catOpts.pruneUnreferencedExternalStyles = !entry.isResourceDictionary;
            if (!entry.isResourceDictionary) {
                catOpts.usageHintTexts.push_back(entry.xamlContent);
            }
            XamlEmitResult catRes = EmitXamlToRealScript(entry.xamlContent, catOpts);
            if (!catRes.succeeded || catRes.generatedRsCode.empty()) {
                result.diagnostics.emplace_back(
                    "CompiledXamlCatalog skipped '" + entry.uri + "'" +
                    (catRes.diagnostics.empty() ? std::string{} : (": " + catRes.diagnostics.front())));
                continue;
            }
            catalogGeneratedSources.push_back(SourceFile{
                "__AeroCompiledCatalog_Item_" + std::to_string(i) + ".g.rs",
                std::move(catRes.generatedRsCode)});

            auto emitUriReg = [&](std::string_view uriKey) {
                if (uriKey.empty()) {
                    return;
                }
                if (entry.isResourceDictionary) {
                    regBody << "        CompiledXamlCatalog.RegisterResourceDictionary("
                            << escapeRsLiteral(uriKey)
                            << ", () => { return __CatalogItem_" << i << ".Build(); });\n";
                } else {
                    regBody << "        CompiledXamlCatalog.RegisterElement("
                            << escapeRsLiteral(uriKey)
                            << ", (ResourceDictionary fallback) => { return __CatalogItem_" << i << ".Build(); });\n";
                }
            };
            emitUriReg(entry.uri);
            for (const auto& alias : entry.aliasUris) {
                emitUriReg(alias);
            }
            ++okCount;
        }

        if (okCount > 0u) {
            std::ostringstream initUnit;
            initUnit << "module __AeroCompiledCatalog;\n"
                     << "import Aero.UI;\n\n"
                     << "public static class __AeroCompiledXamlCatalogInit\n"
                     << "{\n"
                     << "    private static bool _registered = false;\n"
                     << "    public static void RegisterAll()\n"
                     << "    {\n"
                     << "        if (_registered) { return; }\n"
                     << "        _registered = true;\n"
                     << regBody.str()
                     << "    }\n"
                     << "}\n";
            catalogGeneratedSources.push_back(
                SourceFile{"__AeroCompiledCatalogInit.rs", initUnit.str()});
            result.compiledCatalogEntryCount = okCount;
        } else {
            catalogGeneratedSources.clear();
        }
    }

    // Assemble declared modules before C# compatibility lowering
    std::unordered_set<std::string> availableModules =
        useDllMode
            ? [&]() {
                  const auto names = loadedDll->frontendPack->moduleNames();
                  return std::unordered_set<std::string>(names.begin(), names.end());
              }()
            : CollectDeclaredModuleNames(stdlibSources);
    const auto scriptDeclared = CollectDeclaredModuleNames(input.scriptSources);
    availableModules.insert(scriptDeclared.begin(), scriptDeclared.end());
    if (!xamlRes.moduleName.empty()) {
        availableModules.insert(xamlRes.moduleName);
    }
    for (const auto& addRes : additionalXamlResults) {
        if (!addRes.moduleName.empty()) {
            availableModules.insert(addRes.moduleName);
        }
    }
    if (!catalogGeneratedSources.empty()) {
        availableModules.insert("__AeroCompiledCatalog");
    }

    std::vector<SourceFile> loweredScripts;
    loweredScripts.reserve(input.scriptSources.size());
    for (const auto& sf : input.scriptSources) {
        SourceFile lowered;
        lowered.path = sf.path;
        lowered.content = LowerCSharpCompatSyntax(sf.content, &availableModules);
        loweredScripts.push_back(std::move(lowered));
    }

    std::vector<SourceFile> allSources;
    allSources.reserve(
        stdlibSources.size() + additionalXamlResults.size() +
        catalogGeneratedSources.size() + loweredScripts.size() + 1u);
    std::unordered_set<std::string> seenPaths;

    auto pushUniqueSource = [&](SourceFile sf) {
        if (sf.path.empty()) {
            sf.path = "unit_" + std::to_string(allSources.size()) + ".rs";
        }
        if (seenPaths.insert(sf.path).second) {
            allSources.push_back(std::move(sf));
        }
    };

    for (const auto& sf : stdlibSources) {
        pushUniqueSource(sf);
    }
    if (!xamlRes.generatedRsCode.empty()) {
        pushUniqueSource(SourceFile{xamlRes.generatedFilePath, std::move(xamlRes.generatedRsCode)});
    }
    for (auto& addRes : additionalXamlResults) {
        if (!addRes.generatedRsCode.empty()) {
            pushUniqueSource(SourceFile{addRes.generatedFilePath, std::move(addRes.generatedRsCode)});
        }
    }
    for (auto& catSf : catalogGeneratedSources) {
        pushUniqueSource(std::move(catSf));
    }
    for (auto& lowered : loweredScripts) {
        pushUniqueSource(std::move(lowered));
    }

    if (allSources.empty()) {
        result.diagnostics.emplace_back("No XAML or script sources supplied to AeroCompilerV3::Compile");
        return result;
    }

    // Configure RealScript compilation + stdlib snapshot/disk cache (or precompiled DLL pack)
    const auto tCompStart = std::chrono::steady_clock::now();
    rs_comp::CompilationOptions compOpts;
    compOpts.populateSnapshot = !useDllMode && (input.compilerSession != nullptr);
    compOpts.collectSymbolOccurrences = false;
    compOpts.emitDebugInfo = input.emitAotCpp;
    compOpts.allowDefaultBuiltinPacks = true;
    if (input.enableReactiveMacros) {
        compOpts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
    }

    rs_comp::BuildSnapshot previousSnapshot;
    bool usePreviousSnapshot = false;
    bool stdlibCacheCandidate = false;
    std::shared_ptr<const ProcessStdlibCacheEntry> procEntry;

    if (useDllMode) {
        compOpts.precompiledModules = loadedDll->frontendPack;
        compOpts.session.reset();
        compOpts.populateSnapshot = false;
    } else if (input.compilerSession != nullptr) {
        compOpts.session = std::shared_ptr<rs_comp::CompilerSession>(
            input.compilerSession, [](rs_comp::CompilerSession*) {});
        if (input.compilerSession->parsedUnitCacheSize() > 0) {
            auto& cache = GetSessionSnapshotCache();
            std::lock_guard<std::mutex> lock(cache.mutex);
            const auto it = cache.snapshots.find(input.compilerSession);
            if (it != cache.snapshots.end()) {
                previousSnapshot = it->second;
                usePreviousSnapshot = true;
                stdlibCacheCandidate = input.includeStdlib;
            }
        }
        if (!usePreviousSnapshot && input.includeStdlib && input.allowStdlibCache) {
            std::shared_ptr<rs_comp::CompilerSession> unusedSession;
            if (ResolveStdlibCacheForCompile(
                    resolvedStdlibRootKey,
                    stdlibSources,
                    stdlibHash,
                    effectiveStdlibPackages,
                    input.enableReactiveMacros,
                    effectiveCacheDir,
                    input.compilerSession,
                    unusedSession,
                    previousSnapshot,
                    procEntry,
                    stdlibCacheCandidate)) {
                usePreviousSnapshot = true;
            }
        }
    } else if (input.includeStdlib && input.allowStdlibCache) {
        std::shared_ptr<rs_comp::CompilerSession> procSession;
        if (ResolveStdlibCacheForCompile(
                resolvedStdlibRootKey,
                stdlibSources,
                stdlibHash,
                effectiveStdlibPackages,
                input.enableReactiveMacros,
                effectiveCacheDir,
                nullptr,
                procSession,
                previousSnapshot,
                procEntry,
                stdlibCacheCandidate,
                true)) {
            compOpts.session = std::move(procSession);
            usePreviousSnapshot = true;
        }
    }

    if (!useDllMode &&
        input.compilerSession == nullptr &&
        !usePreviousSnapshot &&
        !input.allowStdlibCache) {
        compOpts.populateSnapshot = false;
        compOpts.collectSymbolOccurrences = false;
        compOpts.emitDebugInfo = input.emitAotCpp;
    }
    if (!useDllMode &&
        input.includeStdlib &&
        input.treeShakeStdlib &&
        !usePreviousSnapshot &&
        input.compilerSession == nullptr) {
        compOpts.pruneUnreachableModules = {"Aero.UI", "RealScript.Math"};
        compOpts.reachabilityRootSymbols = {
            "InputDispatcher",
            "BindingEngine",
            "DependencyObject",
            "UIElement",
            "FrameworkElement",
            "BoxedString",
            "BoxedFloat",
            "BoxedInt",
            "BoxedBool",
            "CollectionBox",
            "NativeObjectBox",
            "INotifyPropertyChanged",
        };
        if (result.compiledCatalogEntryCount > 0u) {
            compOpts.reachabilityRootSymbols.insert("__AeroCompiledXamlCatalogInit");
        }
    }

    xamlRes = XamlEmitResult{};
    std::vector<XamlEmitResult>().swap(additionalXamlResults);
    std::vector<SourceFile>().swap(loweredScripts);
    if (!input.allowStdlibCache && input.compilerSession == nullptr) {
        std::vector<SourceFile>().swap(stdlibSources);
    }

    std::vector<rs_mir::Module> mirModules;
    bool stdlibReused = false;
    std::size_t prunedFrontendTypes = 0u;
    std::size_t prunedFrontendFunctions = 0u;
    std::unordered_set<std::string> reusedModuleNames;
    {
        const rs_comp::Compilation compilation(std::move(allSources), std::move(compOpts));
        std::vector<SourceFile>().swap(allSources);
        rs_comp::BuildResult buildRes = compilation.build(
            usePreviousSnapshot ? &previousSnapshot : nullptr);
        AppendDiagnostics(buildRes.diagnostics, result.diagnostics);

        if (buildRes.diagnostics.hasErrors()) {
            return result;
        }

        for (const auto& bi : buildRes.buildInfo) {
            if ((bi.name == "Aero.Native" || bi.name == "Aero.UI") && bi.reused) {
                stdlibReused = true;
            }
            if (bi.reused) {
                reusedModuleNames.insert(bi.name);
            }
        }
        prunedFrontendTypes = buildRes.prunedFrontendTypes;
        prunedFrontendFunctions = buildRes.prunedFrontendFunctions;

        // Only update per-caller SessionSnapshotCache; NEVER overwrite the shared
        // process-wide stdlib snapshot with user compilation units.
        if (!useDllMode && input.compilerSession != nullptr) {
            auto& cache = GetSessionSnapshotCache();
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (cache.snapshots.size() >= 4u &&
                cache.snapshots.find(input.compilerSession) == cache.snapshots.end()) {
                cache.snapshots.erase(cache.snapshots.begin());
            }
            cache.snapshots[input.compilerSession] = std::move(buildRes.snapshot);
        }

        mirModules = std::move(buildRes.modules);
    }
    if (oneShotLoadedDll) {
        oneShotLoadedDll->frontendPack.reset();
    }
    std::vector<SourceFile>().swap(stdlibSources);
    previousSnapshot = {};
    result.usedStdlibCache = stdlibCacheCandidate && stdlibReused;

    // CRITICAL: Strip Aero.Native dummy bodies BEFORE tree-shaker, optimizer, lowerer, and AOT emitter!
    StripAeroNativeMirBodies(mirModules);

    if (input.includeStdlib && input.treeShakeStdlib && !useDllMode) {
        MirPruneStats pruneStats;
        (void)PruneUnreachableStdlibMir(mirModules, &pruneStats);
        result.retainedMirTypes = pruneStats.retainedTypes;
        result.prunedMirTypes = prunedFrontendTypes + pruneStats.prunedTypes;
        result.retainedMirFunctions = pruneStats.retainedFunctions;
        result.prunedMirFunctions = prunedFrontendFunctions + pruneStats.prunedFunctions;
    } else {
        std::unordered_set<SymbolId> uniqueTypeIds;
        std::size_t totalFns = 0u;
        for (const auto& mod : mirModules) {
            totalFns += mod.functions.size();
            for (const auto& t : mod.types) {
                if (t.id != 0u) {
                    uniqueTypeIds.insert(t.id);
                }
            }
        }
        result.retainedMirTypes = uniqueTypeIds.size();
        result.prunedMirTypes = 0u;
        result.retainedMirFunctions = totalFns;
        result.prunedMirFunctions = 0u;
    }

    if (input.optimizeBytecode) {
        rs_opt::Options optOpts;
        optOpts.level = rs_opt::Level::Aggressive;
        optOpts.maximumIterations = 4;
        optOpts.preserveDebugInfo = input.emitAotCpp;
        optOpts.enableInlining = true;
        optOpts.enableLicm = true;
        optOpts.enableStructFieldForwarding = true;
        optOpts.enableDevirtualization = true;

        rs_diag::DiagnosticBag optDiag;
        const rs_opt::Optimizer optimizer;
        auto optRes = optimizer.optimize(std::move(mirModules), optDiag, optOpts);
        mirModules = std::move(optRes.modules);
        AppendDiagnostics(optDiag, result.diagnostics);
        if (optDiag.hasErrors()) {
            return result;
        }
    }

    // Strip debug metadata before lowering to shrink runtime bytecode when AOT C++ isn't requested.
    if (!input.emitAotCpp) {
        for (auto& mirMod : mirModules) {
            mirMod.sourceFiles.clear();
            for (auto& fn : mirMod.functions) {
                fn.debugInfo.sourceName.clear();
                fn.debugInfo.sourceFileId = 0u;
                fn.debugInfo.declaration = {};
                fn.debugInfo.body = {};
                fn.debugInfo.sequencePoints.clear();
                fn.debugInfo.locals.clear();
            }
        }
    }

    // Lower MIR -> Bytecode modules and verify
    rs_bc::Lowerer lowerer;
    result.bytecodeModules.reserve(mirModules.size());
    for (auto& mirMod : mirModules) {
        const bool canReuseStdlibBc =
            !input.optimizeBytecode &&
            !input.emitAotCpp &&
            procEntry &&
            reusedModuleNames.count(mirMod.name) > 0u &&
            (!input.treeShakeStdlib || mirMod.name != "Aero.UI");
        if (canReuseStdlibBc) {
            bool reused = false;
            {
                std::lock_guard<std::mutex> bcLock(procEntry->preloweredBcMutex);
                if (const auto preIt = procEntry->preloweredNonPrunableBc.find(mirMod.name);
                    preIt != procEntry->preloweredNonPrunableBc.end() &&
                    preIt->second.functions.size() == mirMod.functions.size() &&
                    preIt->second.types.size() == mirMod.types.size()) {
                    result.bytecodeModules.push_back(preIt->second);
                    reused = true;
                }
            }
            if (reused) {
                if (!input.emitAotCpp) {
                    mirMod = rs_mir::Module{};
                }
                continue;
            }
        }
        rs_bc::Module bcMod = lowerer.lower(mirMod);
        rs_diag::DiagnosticBag bcDiag;
        if (!rs_bc::verifyModule(bcMod, bcDiag)) {
            AppendDiagnostics(bcDiag, result.diagnostics);
            return result;
        }
        if (canReuseStdlibBc && mirMod.name == "Aero.UI" && !input.treeShakeStdlib) {
            std::lock_guard<std::mutex> bcLock(procEntry->preloweredBcMutex);
            if (procEntry->preloweredNonPrunableBc.find("Aero.UI") == procEntry->preloweredNonPrunableBc.end()) {
                procEntry->preloweredNonPrunableBc.emplace("Aero.UI", bcMod);
            }
        }
        result.bytecodeModules.push_back(std::move(bcMod));
        if (!input.emitAotCpp) {
            mirMod = rs_mir::Module{};
        }
    }
    procEntry.reset();
    std::unordered_set<std::string>().swap(reusedModuleNames);
    if (!input.emitAotCpp) {
        std::vector<rs_mir::Module>().swap(mirModules);
    }

    const auto tCompEnd = std::chrono::steady_clock::now();
    result.compileFrontendAndMirMs =
        std::chrono::duration<double, std::milli>(tCompEnd - tCompStart).count();

    const bool useDirectMoveLink = !input.retainBytecodeModules && !input.emitAotCpp;
    const bool needEncodedBundle = (!useDllMode && !input.skipRawBytecodeOutput) || !progCacheFilePath.empty();
    std::vector<std::uint8_t> encodedProgramBytes;
    if (needEncodedBundle) {
        encodedProgramBytes = EncodeProgramBundle(result.bytecodeModules);
    }

    if (useDllMode && oneShotLoadedDll && !oneShotLoadedDll->lazyBundle) {
        CompactProcessHeapAfterCompile();
        std::string lazyErr;
        if (!DecodeStdlibDllLazyBundle(*oneShotLoadedDll, &lazyErr)) {
            result.diagnostics.emplace_back(
                lazyErr.empty()
                    ? std::string("StdlibDllLoader: failed to decode LazyBytecodeBundle")
                    : ("StdlibDllLoader: " + lazyErr));
            return result;
        }
        result.stdlibDllLoadMs = oneShotLoadedDll->loadAndDecodeMs;
    }

    // Link ProgramImage (modules above already passed rs_bc::verifyModule or cached verification)
    const auto tLinkStart = std::chrono::steady_clock::now();
    rs_rt::RuntimeError linkErr;
    std::optional<ProgramImage> linked;
    if (useDllMode) {
        if (useDirectMoveLink || !input.retainBytecodeModules) {
            if (oneShotLoadedDll && oneShotLoadedDll->lazyBundle) {
                linked = ProgramImage::linkLayeredConsumeHeader(
                    std::move(oneShotLoadedDll->lazyBundle),
                    std::move(result.bytecodeModules),
                    linkErr,
                    /*verifyUserModules=*/false);
                oneShotLoadedDll.reset();
                loadedDll.reset();
            } else {
                linked = ProgramImage::linkLayered(
                    loadedDll->lazyBundle,
                    std::move(result.bytecodeModules),
                    linkErr,
                    /*verifyUserModules=*/false);
            }
            std::vector<rs_bc::Module>().swap(result.bytecodeModules);
            if (linked.has_value()) {
                linked->compactForRuntime();
            }
            CompactProcessHeapAfterCompile();
        } else if (oneShotLoadedDll && oneShotLoadedDll->lazyBundle) {
            auto userCopy = result.bytecodeModules;
            linked = ProgramImage::linkLayeredConsumeHeader(
                std::move(oneShotLoadedDll->lazyBundle),
                std::move(userCopy),
                linkErr,
                /*verifyUserModules=*/false);
            oneShotLoadedDll.reset();
            loadedDll.reset();
            if (linked.has_value()) {
                linked->compactForRuntime();
            }
            CompactProcessHeapAfterCompile();
        } else {
            auto userCopy = result.bytecodeModules;
            linked = ProgramImage::linkLayered(
                loadedDll->lazyBundle,
                std::move(userCopy),
                linkErr,
                /*verifyUserModules=*/false);
        }
    } else if (useDirectMoveLink) {
        linked = ProgramImage::link(std::move(result.bytecodeModules), linkErr, /*verifyModules=*/false);
        std::vector<rs_bc::Module>().swap(result.bytecodeModules);
        if (linked.has_value()) {
            linked->compactForRuntime();
        }
        CompactProcessHeapAfterCompile();
    } else {
        linked = input.retainBytecodeModules
            ? ProgramImage::link(result.bytecodeModules, linkErr, /*verifyModules=*/false)
            : ProgramImage::link(std::move(result.bytecodeModules), linkErr, /*verifyModules=*/false);
        if (!input.retainBytecodeModules) {
            std::vector<rs_bc::Module>().swap(result.bytecodeModules);
        }
    }
    const auto tLinkEnd = std::chrono::steady_clock::now();
    result.linkMs = std::chrono::duration<double, std::milli>(tLinkEnd - tLinkStart).count();
    result.usedStdlibDll = useDllMode;
    if (!linked.has_value()) {
        result.diagnostics.push_back(
            std::string("ProgramImage link error ") + rs_rt::errorCodeName(linkErr.code) +
            ": " + linkErr.message);
        return result;
    }
    result.programImage = std::make_shared<const ProgramImage>(std::move(*linked));

    // Optional C++17 AOT emission
    if (input.emitAotCpp) {
        const std::string builderName = !input.aotViewBuilderName.empty()
            ? input.aotViewBuilderName
            : (!result.rootClassName.empty()
                   ? ("Build" + SanitizeCppIdent(result.rootClassName, "AeroView"))
                   : std::string("BuildAeroView"));
        const std::string progName = rs_aot::sanitizeCppIdentifier(builderName);
        const std::string cppNs = input.aotCppNamespace.empty()
            ? std::string("aero::aot_generated")
            : input.aotCppNamespace;

        rs_aot::GenerationOptions aotOpts;
        aotOpts.programName = progName;
        aotOpts.cppNamespace = cppNs;
        aotOpts.emitLineDirectives = false;
        aotOpts.trustedRelease = input.aotTrustedReleaseOnly;
        aotOpts.emitMode = input.aotTrustedReleaseOnly
            ? rs_aot::AotEmitMode::TrustedReleaseOnly
            : rs_aot::AotEmitMode::FullMultiTier;
        aotOpts.enableSharedReferenceGenerics = true;

        rs_diag::DiagnosticBag aotDiag;
        const rs_aot::CppGenerator cppGenerator;
        auto generated = cppGenerator.generate(mirModules, aotDiag, aotOpts);
        AppendDiagnostics(aotDiag, result.diagnostics);
        if (aotDiag.hasErrors()) {
            return result;
        }

        result.aotCpp.cppNamespace = cppNs;
        result.aotCpp.builderFunctionName = builderName;
        result.aotCpp.scriptAotHeader = std::move(generated.header);
        result.aotCpp.scriptAotSource = std::move(generated.source);
        result.aotCpp.scriptAotContentHash = generated.contentHash;
    }

    result.succeeded = true;
    if (!progCacheFilePath.empty()) {
        PersistProgramBundleToDisk(
            progCacheFilePath, progH1, progH2, result, encodedProgramBytes, mergedExternalDeps);
    }
    if (!useDllMode && !input.skipRawBytecodeOutput && !encodedProgramBytes.empty()) {
        result.programImageBytes = std::move(encodedProgramBytes);
    }
    return result;
}

void AeroCompilerV3::ClearProcessCaches() noexcept {
    {
        auto& procCache = GetProcessStdlibCache();
        std::lock_guard<std::mutex> buildLock(procCache.buildMutex);
        std::lock_guard<std::mutex> lookupLock(procCache.lookupMutex);
        procCache.entries.clear();
    }
    {
        auto& sessionCache = GetSessionSnapshotCache();
        std::lock_guard<std::mutex> lock(sessionCache.mutex);
        sessionCache.snapshots.clear();
    }
    {
        auto& statCache = GetFastStdlibStatCache();
        std::lock_guard<std::mutex> lock(statCache.mutex);
        statCache.entries.clear();
    }
    ClearStdlibDllCache();
    CompactProcessHeapAfterCompile();
}

} // namespace aero
