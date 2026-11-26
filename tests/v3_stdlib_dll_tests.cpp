#include "aero/app/AeroAppHost.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/StdlibDllAbi.h"
#include "aero/compiler/StdlibDllLoader.hpp"
#include "realscript/Version.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

int gFailureCount = 0;

void Require(bool cond, std::string_view msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailureCount;
    }
}

[[nodiscard]] std::string ReadFileText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::uint32_t QuantizedClearColorRgba(const aero::Color& c) noexcept {
    auto clampU8 = [](float x) noexcept -> std::uint8_t {
        if (x <= 0.0f) return 0u;
        if (x >= 1.0f) return 255u;
        return static_cast<std::uint8_t>(x * 255.0f + 0.5f);
    };
    const std::uint32_t r = clampU8(c.r);
    const std::uint32_t g = clampU8(c.g);
    const std::uint32_t b = clampU8(c.b);
    const std::uint32_t a = clampU8(c.a);
    return (r << 24u) | (g << 16u) | (b << 8u) | a;
}

[[nodiscard]] std::size_t CountDistinctColors(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t clearRgba,
    std::size_t& outNonClear) {
    std::unordered_set<std::uint32_t> uniq;
    outNonClear = 0u;
    const std::size_t pxCount = rgba.size() / 4u;
    for (std::size_t i = 0u; i < pxCount; ++i) {
        const std::uint32_t c =
            (static_cast<std::uint32_t>(rgba[i * 4u + 0u]) << 24u) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 1u]) << 16u) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 2u]) << 8u) |
            static_cast<std::uint32_t>(rgba[i * 4u + 3u]);
        uniq.insert(c);
        if (c != clearRgba) {
            ++outNonClear;
        }
        if (uniq.size() >= 64u && outNonClear > 0u) {
            // keep scanning nonClear? only need sample metrics quickly
        }
    }
    return uniq.size();
}

[[nodiscard]] bool BuildSampleForDll(
    const fs::path& sampleDir,
    std::string_view dllPath,
    aero::AeroV3CompileInput& outCompileInput,
    aero::AeroAppHostConfig& outHostConfig) {
    outCompileInput = aero::AeroV3CompileInput{};
    outHostConfig = aero::AeroAppHostConfig{};

    if (!aero::DiscoverSampleProject(sampleDir, outCompileInput, outHostConfig)) {
        std::error_code ec;
        std::vector<fs::path> scripts;
        for (const auto& entry : fs::directory_iterator(sampleDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string ext = entry.path().extension().string();
            if (ext == ".rs" || ext == ".cs") {
                scripts.push_back(entry.path());
            }
        }
        std::sort(scripts.begin(), scripts.end());
        if (scripts.empty()) {
            return false;
        }
        outCompileInput.xamlPath.clear();
        outCompileInput.xamlContent.clear();
        for (const fs::path& p : scripts) {
            outCompileInput.scriptSources.push_back(
                aero::SourceFile{p.generic_string(), ReadFileText(p)});
        }
    }

    outCompileInput.includeStdlib = true;
    outCompileInput.useStdlibDll = true;
    outCompileInput.stdlibDllPath = std::string(dllPath);
    outCompileInput.stdlibRootDir.clear();
    outCompileInput.treeShakeStdlib = false;
    outCompileInput.optimizeBytecode = false;
    outCompileInput.emitAotCpp = false;
    outCompileInput.allowDiskCache = false;
    outCompileInput.allowStdlibCache = false;
    outCompileInput.cacheDir.clear();
    outCompileInput.compilerSession = nullptr;

    outHostConfig.viewportWidth = 320u;
    outHostConfig.viewportHeight = 180u;
    outHostConfig.headless = true;
    outHostConfig.preferSoftwareRasterizer = true;
    outHostConfig.optimizeBytecode = false;
    outHostConfig.enableStencilJit = false;
    return true;
}

struct ChildProcessResult final {
    int exitCode = -1;
    std::string output;
    double wallMs = 0.0;
};

[[nodiscard]] ChildProcessResult RunIsolatedProcess(
    const fs::path& exePath,
    const std::string& argsLine,
    const fs::path& workingDir) {
    ChildProcessResult res;
    const auto t0 = std::chrono::steady_clock::now();
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0u)) {
        res.output = "CreatePipe failed";
        return res;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0u);

    // Build sanitized environment block stripping AERO_STDLIB_DIR, AERO_STDLIB_DLL, AERO_CACHE_DIR
    std::vector<wchar_t> envBlock;
    if (LPWCH rawEnv = GetEnvironmentStringsW()) {
        const wchar_t* p = rawEnv;
        while (*p != L'\0') {
            const std::wstring entry(p);
            p += entry.size() + 1u;
            const std::size_t eq = entry.find(L'=');
            const std::wstring name = (eq == std::wstring::npos) ? entry : entry.substr(0u, eq);
            auto eqFold = [&](const wchar_t* target) {
                return _wcsicmp(name.c_str(), target) == 0;
            };
            if (eqFold(L"AERO_STDLIB_DIR") ||
                eqFold(L"AERO_STDLIB_DLL") ||
                eqFold(L"AERO_CACHE_DIR")) {
                continue;
            }
            envBlock.insert(envBlock.end(), entry.begin(), entry.end());
            envBlock.push_back(L'\0');
        }
        FreeEnvironmentStringsW(rawEnv);
    }
    envBlock.push_back(L'\0');

    std::wstring cmdW = L"\"" + exePath.wstring() + L"\"";
    if (!argsLine.empty()) {
        const int needed = MultiByteToWideChar(
            CP_UTF8, 0, argsLine.c_str(), static_cast<int>(argsLine.size()), nullptr, 0);
        if (needed > 0) {
            std::wstring wargs(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, argsLine.c_str(), static_cast<int>(argsLine.size()), wargs.data(), needed);
            cmdW.push_back(L' ');
            cmdW.append(wargs);
        }
    }
    std::wstring cwdW = workingDir.wstring();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr,
        cmdW.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(),
        cwdW.c_str(),
        &si,
        &pi);
    CloseHandle(writePipe);

    if (!ok) {
        CloseHandle(readPipe);
        res.output = "CreateProcessW failed (" + std::to_string(GetLastError()) + ")";
        return res;
    }

    char buf[4096]{};
    DWORD nRead = 0u;
    while (ReadFile(readPipe, buf, sizeof(buf), &nRead, nullptr) && nRead > 0u) {
        res.output.append(buf, nRead);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1u;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    res.exitCode = static_cast<int>(code);
#else
    (void)exePath;
    (void)argsLine;
    (void)workingDir;
    res.output = "POSIX isolated launch not implemented";
#endif
    const auto t1 = std::chrono::steady_clock::now();
    res.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

void TestAbiAndErrorHandling() {
    std::cout << "[Test 1] aerogui_stdlib.dll ABI validation & error handling...\n";
    const fs::path dllPath(AERO_STDLIB_DLL_PATH);
    std::error_code ec;
    Require(fs::is_regular_file(dllPath, ec),
        "aerogui_stdlib DLL missing on disk: " + dllPath.generic_string());
    const auto fileSize = fs::file_size(dllPath, ec);
    Require(!ec && fileSize > 0u, "aerogui_stdlib DLL size query failed");

    aero::ClearStdlibDllCacheForTesting();
    std::string err;
    const auto loaded = aero::LoadStdlibDll(dllPath.generic_string(), &err);
    Require(loaded != nullptr, "LoadStdlibDll failed: " + err);
    if (!loaded) {
        return;
    }

    Require(loaded->header != nullptr, "Loaded header must be non-null");
    Require(loaded->header->magic == AERO_STDLIB_DLL_MAGIC, "Header magic mismatch");
    Require(loaded->header->abiVersion == AERO_STDLIB_DLL_ABI_VERSION, "Header ABI version mismatch");
    Require(loaded->header->structSize >= sizeof(AeroStdlibDllHeader), "Header structSize too small");
    Require(loaded->header->realscriptSdkCompatVersion == realscript::kSdkCompatibilityVersion,
        "SDK compatibility version mismatch");
    Require((loaded->header->flags & AERO_STDLIB_FLAG_OPTIMIZED_O2) != 0u,
        "Default aerogui_stdlib.dll must carry MIR O2 flag");
    Require(loaded->header->frontendPackSize > 0u && loaded->header->lazyBytecodeSize > 0u,
        "DLL payloads must be non-empty");
    Require(loaded->header->moduleListCsv != nullptr, "moduleListCsv must be non-null");
    const std::string csv = loaded->header->moduleListCsv ? loaded->header->moduleListCsv : "";
    Require(csv.find("RealScript.Math") != std::string::npos &&
            csv.find("Aero.Native") != std::string::npos &&
            csv.find("Aero.UI") != std::string::npos,
        "moduleListCsv missing expected stdlib modules: " + csv);
    Require(loaded->frontendPack != nullptr && loaded->lazyBundle != nullptr,
        "Decoded frontendPack and lazyBundle must be non-null");
    Require(loaded->lazyBundle->hasZeroCopyStrings() &&
            loaded->lazyBundle->hasZeroCopyFunctionLocs(),
        "Expected stdlib DLL LazyBytecodeBundle to use RSLB v4 zero-copy .rodata views");

    std::cout << std::fixed << std::setprecision(2)
              << "[STDLIB_DLL] path=" << dllPath.generic_string()
              << " size_bytes=" << fileSize
              << " size_mb=" << (static_cast<double>(fileSize) / (1024.0 * 1024.0))
              << " frontend_kb=" << (static_cast<double>(loaded->header->frontendPackSize) / 1024.0)
              << " lazy_bundle_kb=" << (static_cast<double>(loaded->header->lazyBytecodeSize) / 1024.0)
              << " load_decode_ms=" << loaded->loadAndDecodeMs
              << " modules=" << csv << "\n";

    std::string badErr;
    const auto missing = aero::LoadStdlibDll(
        (dllPath.parent_path() / "does_not_exist_aerogui_stdlib.dll").generic_string(),
        &badErr);
    Require(missing == nullptr && !badErr.empty(),
        "Loading missing stdlib DLL must fail cleanly with diagnostic message");
}

void TestAll28SamplesUnderDllInterpretedMode() {
    std::cout << "[Test 2] All 28 samples (23 legacy + 5 V3) via aerogui_stdlib.dll interpreted mode...\n";
    const fs::path samplesRoot(AERO_SAMPLES_DIR);
    const std::uint32_t clearRgba = QuantizedClearColorRgba(aero::AeroAppHostConfig{}.clearColor);

    const std::array<const char*, 23> kLegacySamples = {
        "BackgroundBlur", "BrushShaders", "Buttons", "CustomAnimation", "CustomControl",
        "CustomRender", "DataBinding", "Gallery", "HelloWorld", "Integration",
        "IntegrationGLUT", "Inventory", "Localization", "Login", "Menu3D",
        "QuestLog", "Rive", "Scoreboard", "Theme", "TicTacToe",
        "UserControl", "VideoEffect", "WpfQuickStart",
    };
    const std::array<const char*, 5> kV3Samples = {
        "01_CodeFirstCounter", "02_CompiledXamlMvvm", "03_ControlGallery",
        "04_VectorAndCanvas", "05_DualDomainSimHud",
    };

    std::vector<fs::path> allSampleDirs;
    std::vector<std::string> allSampleNames;
    for (const char* name : kLegacySamples) {
        allSampleDirs.push_back(samplesRoot / name);
        allSampleNames.emplace_back(name);
    }
    for (const char* name : kV3Samples) {
        allSampleDirs.push_back(samplesRoot / "V3" / name);
        allSampleNames.emplace_back(std::string("V3/") + name);
    }

    double sumCompileLinkMs = 0.0;
    double sumLazyRatioPct = 0.0;
    std::size_t sampleCountOk = 0u;

    for (std::size_t i = 0u; i < allSampleDirs.size(); ++i) {
        const auto& sName = allSampleNames[i];
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildSampleForDll(allSampleDirs[i], AERO_STDLIB_DLL_PATH, ci, hc),
            sName + ": discovery failed");

        const aero::AeroCompilerV3 compiler;
        const auto t0 = std::chrono::steady_clock::now();
        const aero::AeroV3CompileResult res = compiler.Compile(ci);
        const auto t1 = std::chrono::steady_clock::now();
        const double compileAndLinkMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!res.succeeded || !res.programImage) {
            std::string msg = sName + " DLL compile failed:";
            for (const auto& d : res.diagnostics) msg += "\n  " + d;
            Require(false, msg);
            continue;
        }
        Require(res.usedStdlibDll && !res.usedStdlibCache,
            sName + " must run cleanly from aerogui_stdlib.dll with zero disk cache");

        aero::AeroAppHost host(hc);
        const std::string entryHint = ci.xamlContent.empty() ? std::string{} : res.rootClassName;
        Require(host.LoadProgramImage(res.programImage, entryHint),
            sName + " LoadProgramImage failed: " + host.LastError());
        for (int f = 0; f < 25; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 24);
        }

        std::vector<std::uint8_t> rgba;
        Require(host.RenderHeadlessRgba(320u, 180u, rgba), sName + " RenderHeadlessRgba failed");
        std::size_t nonClear = 0u;
        const std::size_t colors = CountDistinctColors(rgba, clearRgba, nonClear);
        Require(host.World().LiveNodeCount() > 0u &&
                host.RenderTree().NodeCount() > 0u &&
                colors >= 2u,
            sName + " rendered empty/uniform output under stdlib DLL");

        const std::size_t lazyTotal = res.programImage->totalLazyFunctionCount();
        const std::size_t lazyCompiled = res.programImage->compiledLazyFunctionCount();
        Require(lazyTotal > 0u && lazyCompiled > 0u && lazyCompiled < lazyTotal,
            sName + " expected partial lazy compilation (got " +
            std::to_string(lazyCompiled) + "/" + std::to_string(lazyTotal) + ")");

        const double lazyPct =
            100.0 * static_cast<double>(lazyCompiled) / static_cast<double>(lazyTotal);
        sumCompileLinkMs += compileAndLinkMs;
        sumLazyRatioPct += lazyPct;
        ++sampleCountOk;

        std::cout << std::fixed << std::setprecision(2)
                  << "  [DLL_SAMPLE] " << sName
                  << " compile_link_ms=" << compileAndLinkMs
                  << " lazy=" << lazyCompiled << "/" << lazyTotal
                  << " (" << std::setprecision(1) << lazyPct << "%)"
                  << " colors=" << colors << "\n";
    }

    Require(sampleCountOk == 28u, "All 28 samples must succeed under stdlib DLL");
    if (sampleCountOk > 0u) {
        std::cout << std::fixed << std::setprecision(2)
                  << "[DLL_SUMMARY] passed=" << sampleCountOk << "/28"
                  << " avg_compile_link_ms=" << (sumCompileLinkMs / static_cast<double>(sampleCountOk))
                  << " avg_lazy_pct=" << (sumLazyRatioPct / static_cast<double>(sampleCountOk)) << "%\n";
    }
}

void TestIsolatedTempDirLaunchesWithoutStdlibDir() {
    std::cout << "[Test 3] Isolated temp-dir cold-start launches (zero stdlib/ directory)...\n";
    std::error_code ec;
    const fs::path tempRoot = fs::temp_directory_path(ec) / "aero_isolated_stdlib_dll_suite";
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot, ec);

    // Ensure no ancestor of tempRoot contains stdlib/Aero.UI/Core.rs
    for (fs::path probe = tempRoot; !probe.empty();) {
        Require(!fs::exists(probe / "stdlib" / "Aero.UI" / "Core.rs", ec),
            "Isolated temp root must not have an ancestor stdlib/ dir: " + probe.generic_string());
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }

    const fs::path dllSrc(AERO_STDLIB_DLL_PATH);
    const fs::path viewerSrc(AERO_VIEWER_EXE);
    const fs::path buttonsSrc(AERO_SAMPLE_BUTTONS_EXE);
    const fs::path gallerySrc(AERO_SAMPLE_GALLERY_EXE);
    const fs::path inventorySrc(AERO_SAMPLE_INVENTORY_EXE);

    // 1. Isolated aero-viewer launching Buttons
    {
        const fs::path isoDir = tempRoot / "viewer_buttons";
        fs::create_directories(isoDir, ec);
        fs::copy_file(viewerSrc, isoDir / viewerSrc.filename(),
            fs::copy_options::overwrite_existing, ec);
        Require(!ec, "Failed to copy aero-viewer to isolated dir");
        fs::copy_file(dllSrc, isoDir / dllSrc.filename(),
            fs::copy_options::overwrite_existing, ec);
        Require(!ec, "Failed to copy aerogui_stdlib DLL to isolated viewer dir");
        fs::copy(fs::path(AERO_SAMPLES_DIR) / "Buttons", isoDir / "Buttons",
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        Require(!ec, "Failed to copy Buttons sample to isolated viewer dir");
        Require(!fs::exists(isoDir / "stdlib", ec),
            "Isolated viewer dir must NOT contain stdlib/");

        const ChildProcessResult r = RunIsolatedProcess(
            isoDir / viewerSrc.filename(),
            "--dir Buttons --headless --frames 5 --profile-startup",
            isoDir);
        std::cout << "[ISOLATED] aero-viewer (Buttons) exit=" << r.exitCode
                  << " cold_wall_ms=" << std::fixed << std::setprecision(2) << r.wallMs << "\n"
                  << r.output;
        Require(r.exitCode == 0, "Isolated aero-viewer failed:\n" + r.output);
        Require(r.output.find("aero-viewer headless OK") != std::string::npos &&
                r.output.find("mode=dll") != std::string::npos,
            "Isolated aero-viewer must run in DLL mode cleanly:\n" + r.output);
    }

    // 2-4. Isolated standalone sample executables
    struct SampleCase {
        const char* label;
        fs::path srcExe;
        bool expectThemeDir;
    };
    const std::array<SampleCase, 3> cases = {{
        {"aero_sample_buttons", buttonsSrc, false},
        {"aero_sample_gallery", gallerySrc, true},
        {"aero_sample_inventory", inventorySrc, false},
    }};

    for (const auto& sc : cases) {
        const fs::path isoDir = tempRoot / sc.label;
        fs::create_directories(isoDir, ec);
        fs::copy(sc.srcExe.parent_path(), isoDir,
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        Require(!ec, std::string("Failed to copy ") + sc.label + " folder to isolated dir");
        Require(!fs::exists(isoDir / "stdlib", ec),
            std::string(sc.label) + " package must NOT contain stdlib/");
        Require(fs::exists(isoDir / dllSrc.filename(), ec),
            std::string(sc.label) + " package must bundle " + dllSrc.filename().generic_string());
        if (sc.expectThemeDir) {
            Require(fs::is_directory(isoDir / "Data" / "Theme", ec),
                "aero_sample_gallery package must bundle Data/Theme");
        }

        const fs::path isoExe = isoDir / sc.srcExe.filename();
        const ChildProcessResult r = RunIsolatedProcess(
            isoExe,
            "--headless --frames 5 --profile-startup",
            isoDir);
        std::cout << "[ISOLATED] " << sc.label << " exit=" << r.exitCode
                  << " cold_wall_ms=" << std::fixed << std::setprecision(2) << r.wallMs << "\n"
                  << r.output;
        Require(r.exitCode == 0, std::string(sc.label) + " isolated launch failed:\n" + r.output);
        Require(r.output.find("headless OK") != std::string::npos &&
                r.output.find("mode=dll") != std::string::npos,
            std::string(sc.label) + " must report mode=dll & headless OK:\n" + r.output);
    }

    fs::remove_all(tempRoot, ec);
}

void TestStdlibDllUserArbcDiskCache() {
    std::cout << "[Test 4] stdlib DLL user .arbc disk cache & lazy-only DLL load...\n";
    std::error_code ec;
    const fs::path cacheDir = fs::temp_directory_path(ec) / "aero_stdlib_dll_arbc_cache_test";
    fs::remove_all(cacheDir, ec);
    fs::create_directories(cacheDir, ec);

    // 1. Verify LoadStdlibDllLazyOnly skips PrecompiledFrontendPack until DecodeStdlibDllFrontendPack is called
    {
        std::string err;
        auto lazyOnly = aero::LoadStdlibDllLazyOnly(AERO_STDLIB_DLL_PATH, &err);
        Require(lazyOnly != nullptr, "LoadStdlibDllLazyOnly failed: " + err);
        if (lazyOnly) {
            Require(lazyOnly->lazyBundle != nullptr && lazyOnly->frontendPack == nullptr,
                "LoadStdlibDllLazyOnly must leave frontendPack == nullptr");
            Require(lazyOnly->lazyBundle->hasZeroCopyStrings() &&
                    lazyOnly->lazyBundle->hasZeroCopyFunctionLocs(),
                "LoadStdlibDllLazyOnly must preserve RSLB v4 zero-copy views");
            Require(aero::DecodeStdlibDllFrontendPack(*lazyOnly, &err),
                "DecodeStdlibDllFrontendPack failed: " + err);
            Require(lazyOnly->frontendPack != nullptr,
                "DecodeStdlibDllFrontendPack must populate frontendPack");
        }

        auto frontOnly = aero::LoadStdlibDllFrontendOnly(AERO_STDLIB_DLL_PATH, &err);
        Require(frontOnly != nullptr, "LoadStdlibDllFrontendOnly failed: " + err);
        if (frontOnly) {
            Require(frontOnly->frontendPack != nullptr && frontOnly->lazyBundle == nullptr,
                "LoadStdlibDllFrontendOnly must leave lazyBundle == nullptr");
            Require(aero::DecodeStdlibDllLazyBundle(*frontOnly, &err),
                "DecodeStdlibDllLazyBundle failed: " + err);
            Require(frontOnly->lazyBundle != nullptr &&
                    frontOnly->lazyBundle->hasZeroCopyStrings() &&
                    frontOnly->lazyBundle->hasZeroCopyFunctionLocs(),
                "DecodeStdlibDllLazyBundle must populate RSLB v4 zero-copy lazyBundle");
        }
    }

    // 2. Cold compile with allowDiskCache=true writes .arbc; warm compile hits cache without decoding frontendPack
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildSampleForDll(fs::path(AERO_SAMPLES_DIR) / "Buttons", AERO_STDLIB_DLL_PATH, ci, hc),
        "Buttons discovery failed for DLL .arbc cache test");
    ci.retainStdlibDllCache = false;
    ci.allowDiskCache = true;
    ci.cacheDir = cacheDir.generic_string();

    const aero::AeroCompilerV3 compiler;
    const auto tCold0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult coldRes = compiler.Compile(ci);
    const auto tCold1 = std::chrono::steady_clock::now();
    const double coldMs = std::chrono::duration<double, std::milli>(tCold1 - tCold0).count();
    Require(coldRes.succeeded && coldRes.programImage != nullptr && coldRes.usedStdlibDll,
        "DLL cold compile with allowDiskCache=true failed");

    std::size_t arbcFiles = 0u;
    for (const auto& entry : fs::recursive_directory_iterator(cacheDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".arbc") {
            ++arbcFiles;
        }
    }
    Require(arbcFiles >= 1u, "Expected at least 1 .arbc user cache file written in DLL mode");

    const auto tWarm0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult warmRes = compiler.Compile(ci);
    const auto tWarm1 = std::chrono::steady_clock::now();
    const double warmMs = std::chrono::duration<double, std::milli>(tWarm1 - tWarm0).count();
    Require(warmRes.succeeded && warmRes.programImage != nullptr && warmRes.usedStdlibDll && !warmRes.usedStdlibCache,
        "DLL warm .arbc compile failed");

    aero::AeroAppHost host(hc);
    Require(host.LoadProgramImage(warmRes.programImage, warmRes.rootClassName),
        "Warm DLL .arbc LoadProgramImage failed: " + host.LastError());
    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }
    std::vector<std::uint8_t> rgba;
    Require(host.RenderHeadlessRgba(320u, 180u, rgba), "Warm DLL .arbc RenderHeadlessRgba failed");
    const std::uint32_t clearRgba = QuantizedClearColorRgba(hc.clearColor);
    std::size_t nonClear = 0u;
    const std::size_t colors = CountDistinctColors(rgba, clearRgba, nonClear);
    Require(colors >= 2u && nonClear > 0u, "Warm DLL .arbc rendered blank output");

    std::cout << std::fixed << std::setprecision(2)
              << "[DLL_ARBC_CACHE] cold_ms=" << coldMs
              << " warm_ms=" << warmMs
              << " colors=" << colors << "\n";

    fs::remove_all(cacheDir, ec);
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    TestAbiAndErrorHandling();
    TestAll28SamplesUnderDllInterpretedMode();
    TestIsolatedTempDirLaunchesWithoutStdlibDir();
    TestStdlibDllUserArbcDiskCache();

    if (gFailureCount != 0) {
        std::cerr << "\nv3_stdlib_dll_tests FAILED with " << gFailureCount << " failure(s).\n";
        return 1;
    }
    std::cout << "\nv3_stdlib_dll_tests PASSED.\n";
    return 0;
}
