#include "aero/compiler/AeroCompilerV3.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "aero/compiler/MirReachabilityPruner.hpp"
#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/compiler/XamlToRsEmitter.hpp"
#include "aero/core/StringUtils.hpp"
#include "realscript/Version.h"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/ModuleBuildCache.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/diagnostics/Diagnostic.h"
#include "realscript/optimization/Optimizer.h"
#include "realscript/runtime/Runtime.h"

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
    bool enableReactiveMacros = true;

    [[nodiscard]] bool operator==(const StdlibCacheKey& other) const noexcept {
        return stdlibHash == other.stdlibHash &&
               enableReactiveMacros == other.enableReactiveMacros &&
               resolvedRoot == other.resolvedRoot;
    }
};

struct StdlibCacheKeyHash final {
    [[nodiscard]] std::size_t operator()(const StdlibCacheKey& key) const noexcept {
        std::uint64_t h = key.stdlibHash;
        h ^= std::hash<std::string>{}(key.resolvedRoot) + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
        h ^= key.enableReactiveMacros ? 0x517cc1b727220a95ull : 0x243f6a8885a308d3ull;
        return static_cast<std::size_t>(h);
    }
};

struct ProcessStdlibCacheEntry final {
    std::shared_ptr<rs_comp::CompilerSession> session;
    rs_comp::BuildSnapshot snapshot;
    std::unordered_map<std::string, rs_bc::Module> preloweredNonPrunableBc;
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
        if (modName == "Aero.Native" || modName == "Aero.UI") {
            continue;
        }
        rs_bc::Module bcMod = lowerer.lower(cm.module);
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
    bool enableReactiveMacros,
    const std::filesystem::path& effectiveCacheDir,
    rs_comp::CompilerSession* callerSession,
    std::shared_ptr<rs_comp::CompilerSession>& outSession,
    rs_comp::BuildSnapshot& outSnapshot,
    std::shared_ptr<const ProcessStdlibCacheEntry>& outEntry,
    bool& outCacheHit) {
    outCacheHit = false;
    outEntry.reset();
    const StdlibCacheKey key{resolvedRootKey, stdlibHash, enableReactiveMacros};
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
                    outEntry = it->second;
                }
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
                outEntry = cachedEntry;
            }
            outSnapshot = cachedEntry->snapshot;
            outCacheHit = true;
            return true;
        }
    }

    // Try cross-process disk cache before running a cold stdlib compilation.
    rs_comp::BuildSnapshot loadedSnap;
    if (TryLoadStdlibSnapshotFromDisk(
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
            outEntry = freshEntry;
        }
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

    // Never share a non-owning caller session pointer inside the process-wide cache.
    auto procEntry = std::make_shared<ProcessStdlibCacheEntry>();
    procEntry->session = (callerSession == nullptr)
        ? warmSession
        : std::make_shared<rs_comp::CompilerSession>();
    procEntry->snapshot = warmupRes.snapshot;
    PopulateStdlibDerivedCache(*procEntry);
    {
        std::lock_guard<std::mutex> lock(procCache.lookupMutex);
        procCache.entries[key] = procEntry;
    }

    if (callerSession == nullptr) {
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
    std::vector<std::uint8_t> bytes;
    bytes.reserve(4096);
    bytes.push_back('A');
    bytes.push_back('R');
    bytes.push_back('B');
    bytes.push_back('C');
    AppendUint32Le(bytes, static_cast<std::uint32_t>(modules.size()));
    for (const auto& mod : modules) {
        const auto modBytes = rs_bc::encodeModule(mod);
        AppendUint32Le(bytes, static_cast<std::uint32_t>(modBytes.size()));
        bytes.insert(bytes.end(), modBytes.begin(), modBytes.end());
    }
    return bytes;
}

} // namespace

AeroV3CompileResult AeroCompilerV3::Compile(const AeroV3CompileInput& input) const {
    AeroV3CompileResult result;

    std::vector<SourceFile> stdlibSources;
    std::string resolvedStdlibRootKey;
    std::uint64_t stdlibHash = 0u;
    if (input.includeStdlib) {
        const std::filesystem::path resolvedRoot = ResolveStdlibRootDirectory(input.stdlibRootDir);
        stdlibSources = LoadStdlibModulePack(input.stdlibRootDir);
        if (stdlibSources.empty()) {
            result.diagnostics.emplace_back(
                "StdlibModulePack: failed to locate stdlib Aero.Native / Aero.UI sources");
            return result;
        }
        std::error_code ec;
        const auto canon = std::filesystem::weakly_canonical(resolvedRoot, ec);
        resolvedStdlibRootKey = (!ec && !canon.empty())
            ? canon.lexically_normal().generic_string()
            : resolvedRoot.lexically_normal().generic_string();
        stdlibHash = ComputeStdlibPackHash(stdlibSources, input.enableReactiveMacros);
    }

    // Emit primary XAML -> .g.rs
    XamlEmitResult xamlRes;
    if (!input.xamlContent.empty()) {
        XamlEmitOptions emitOpts;
        emitOpts.xamlPath = input.xamlPath.empty() ? "MainView.xaml" : input.xamlPath;
        emitOpts.externalXamlResolver = input.externalXamlResolver;
        emitOpts.resourceSearchRoot = input.resourceSearchRoot;
        emitOpts.resourceSearchPaths = input.resourceSearchPaths;
        xamlRes = EmitXamlToRealScript(input.xamlContent, emitOpts);
        result.generatedPartialRsPath = xamlRes.generatedFilePath;
        result.generatedPartialRsCode = xamlRes.generatedRsCode;
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

    // Assemble declared modules before C# compatibility lowering
    std::unordered_set<std::string> availableModules = CollectDeclaredModuleNames(stdlibSources);
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

    std::vector<SourceFile> allSources;
    allSources.reserve(
        stdlibSources.size() + additionalXamlResults.size() + input.scriptSources.size() + 1u);
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
        pushUniqueSource(SourceFile{xamlRes.generatedFilePath, xamlRes.generatedRsCode});
    }
    for (const auto& addRes : additionalXamlResults) {
        if (!addRes.generatedRsCode.empty()) {
            pushUniqueSource(SourceFile{addRes.generatedFilePath, addRes.generatedRsCode});
        }
    }
    for (const auto& sf : input.scriptSources) {
        SourceFile lowered;
        lowered.path = sf.path;
        lowered.content = LowerCSharpCompatSyntax(sf.content, &availableModules);
        pushUniqueSource(std::move(lowered));
    }

    if (allSources.empty()) {
        result.diagnostics.emplace_back("No XAML or script sources supplied to AeroCompilerV3::Compile");
        return result;
    }

    // Configure RealScript compilation + stdlib snapshot/disk cache
    rs_comp::CompilationOptions compOpts;
    compOpts.allowDefaultBuiltinPacks = true;
    if (input.enableReactiveMacros) {
        compOpts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
    }

    rs_comp::BuildSnapshot previousSnapshot;
    bool usePreviousSnapshot = false;
    bool stdlibCacheCandidate = false;
    std::shared_ptr<const ProcessStdlibCacheEntry> procEntry;
    const std::filesystem::path effectiveCacheDir = ResolveEffectiveCacheDir(input);

    if (input.compilerSession != nullptr) {
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
        if (!usePreviousSnapshot && input.includeStdlib) {
            std::shared_ptr<rs_comp::CompilerSession> unusedSession;
            std::shared_ptr<const ProcessStdlibCacheEntry> unusedEntry;
            if (ResolveStdlibCacheForCompile(
                    resolvedStdlibRootKey,
                    stdlibSources,
                    stdlibHash,
                    input.enableReactiveMacros,
                    effectiveCacheDir,
                    input.compilerSession,
                    unusedSession,
                    previousSnapshot,
                    unusedEntry,
                    stdlibCacheCandidate)) {
                usePreviousSnapshot = true;
            }
        }
    } else if (input.includeStdlib) {
        std::shared_ptr<rs_comp::CompilerSession> procSession;
        if (ResolveStdlibCacheForCompile(
                resolvedStdlibRootKey,
                stdlibSources,
                stdlibHash,
                input.enableReactiveMacros,
                effectiveCacheDir,
                nullptr,
                procSession,
                previousSnapshot,
                procEntry,
                stdlibCacheCandidate)) {
            compOpts.session = std::move(procSession);
            usePreviousSnapshot = true;
        }
    }

    const rs_comp::Compilation compilation(std::move(allSources), std::move(compOpts));
    rs_comp::BuildResult buildRes = compilation.build(
        usePreviousSnapshot ? &previousSnapshot : nullptr);
    AppendDiagnostics(buildRes.diagnostics, result.diagnostics);

    if (buildRes.diagnostics.hasErrors()) {
        return result;
    }

    bool stdlibReused = false;
    for (const auto& bi : buildRes.buildInfo) {
        if (bi.name == "Aero.Native" || bi.name == "Aero.UI") {
            if (bi.reused) {
                stdlibReused = true;
            }
        }
    }
    result.usedStdlibCache = stdlibCacheCandidate && stdlibReused;

    // Only update per-caller SessionSnapshotCache; NEVER overwrite the shared
    // process-wide stdlib snapshot with user compilation units.
    if (input.compilerSession != nullptr) {
        auto& cache = GetSessionSnapshotCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.snapshots[input.compilerSession] = buildRes.snapshot;
    }

    // CRITICAL: Strip Aero.Native dummy bodies BEFORE tree-shaker, optimizer, lowerer, and AOT emitter!
    std::vector<rs_mir::Module> mirModules = std::move(buildRes.modules);
    StripAeroNativeMirBodies(mirModules);

    if (input.includeStdlib && input.treeShakeStdlib) {
        MirPruneStats pruneStats;
        (void)PruneUnreachableStdlibMir(mirModules, &pruneStats);
        result.retainedMirTypes = pruneStats.retainedTypes;
        result.prunedMirTypes = pruneStats.prunedTypes;
        result.retainedMirFunctions = pruneStats.retainedFunctions;
        result.prunedMirFunctions = pruneStats.prunedFunctions;
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
        optOpts.preserveDebugInfo = true;
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

    // Lower MIR -> Bytecode modules and verify
    rs_bc::Lowerer lowerer;
    result.bytecodeModules.reserve(mirModules.size());
    for (const auto& mirMod : mirModules) {
        if (!input.optimizeBytecode && procEntry) {
            if (const auto preIt = procEntry->preloweredNonPrunableBc.find(mirMod.name);
                preIt != procEntry->preloweredNonPrunableBc.end() &&
                preIt->second.functions.size() == mirMod.functions.size() &&
                preIt->second.types.size() == mirMod.types.size()) {
                result.bytecodeModules.push_back(preIt->second);
                continue;
            }
        }
        rs_bc::Module bcMod = lowerer.lower(mirMod);
        rs_diag::DiagnosticBag bcDiag;
        if (!rs_bc::verifyModule(bcMod, bcDiag)) {
            AppendDiagnostics(bcDiag, result.diagnostics);
            return result;
        }
        result.bytecodeModules.push_back(std::move(bcMod));
    }

    result.programImageBytes = EncodeProgramBundle(result.bytecodeModules);

    // Link ProgramImage
    rs_rt::RuntimeError linkErr;
    auto linked = ProgramImage::link(result.bytecodeModules, linkErr);
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
    return result;
}

} // namespace aero
