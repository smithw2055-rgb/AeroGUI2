#include "aero/app/AeroAppHost.hpp"
#include "aero/app/AppBootstrap.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <realscript/aot_cpp/AotRuntime.h>
#include <realscript/bytecode/Bytecode.h>
#include <realscript/diagnostics/Diagnostic.h>
#ifdef AERO_HAS_STENCIL_JIT
#include <realscript/jit/StencilJit.h>
#endif
#include <realscript/semantic/Semantic.h>

#include "aero/core/ArbcCodec.hpp"
#include "aero/core/ArbcCodec.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/visual/VisualHostBindings.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#endif

#include "aero/runtime/ScriptBackendFacade.hpp"

namespace aero {

namespace {

[[nodiscard]] bool IsAeroTraceEnabled() noexcept {
    static const bool enabled = (std::getenv("AERO_TRACE") != nullptr);
    return enabled;
}

[[nodiscard]] bool WritePpmP6(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8) {
    return app::WriteRgba8PpmBuffer(std::filesystem::path(filePath), width, height, rgba8);
}

[[nodiscard]] std::uint64_t VisualNodeKey(VisualNodeId id) noexcept {
    return (static_cast<std::uint64_t>(id.generation) << 32u) | static_cast<std::uint64_t>(id.index);
}

[[nodiscard]] ScriptBackendView MakeBackendView(
    const ExecutionDomainContext& domains,
    const ProgramImage* image,
    const Interpreter* interp,
    const rs_aot::ProgramDescriptor* aotProg,
    rs_aot::ExecutionContext* aotCtx) noexcept {
    return ScriptBackendView{
        const_cast<ExecutionDomainContext*>(&domains),
        image,
        interp,
        aotProg,
        aotCtx};
}

[[nodiscard]] bool DecodeProgramBundleBytes(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<rs_bc::Module>& outModules,
    std::string& outError) {
    return DecodeArbcBundle(data, size, outModules, /*allowSingleModuleFallback=*/true, &outError);
}

} // namespace

AeroAppHost::AeroAppHost(const AeroAppHostConfig& config)
    : config_(config)
    , resourceStore_(std::make_shared<UIResourceStore>())
    , domains_()
    , world_(&domains_.nativeHandles(), resourceStore_.get(), &domains_.uiHeap())
    , drawBuffer_()
    , animEngine_()
    , inputRouter_(&world_, nullptr, &platformHooks_)
    , renderTree_()
    , gpuStream_(ShaderBackend::DesktopGL33, 32u)
    , desktopHost_(&world_, &drawBuffer_, &animEngine_, &inputRouter_, &renderTree_, resourceStore_)
    , windowHost_()
    , bindings_(std::make_shared<BindingRegistry>()) {
    for (const auto& dir : config_.assetSearchDirs) {
        RegisterAssetDirectoryInternal(dir);
    }
}

void AeroAppHost::RegisterAssetDirectoryInternal(std::string_view dir) {
    if (dir.empty()) {
        return;
    }
    const std::string dirStr(dir);
    const auto existing = resourceStore_->AssetSearchDirectoriesSnapshot();
    const bool alreadyAdded = std::find(existing.begin(), existing.end(), dirStr) != existing.end();
    resourceStore_->AddAssetSearchDirectory(dirStr);
    resourceStore_->Images().AddSearchDirectory(dirStr);
    if (alreadyAdded) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path dirPath(dirStr);
    if (!std::filesystem::is_directory(dirPath, ec)) {
        return;
    }
    std::vector<std::filesystem::path> ttfFiles;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (str::EqualsIgnoreCase(ext, ".ttf") || str::EqualsIgnoreCase(ext, ".otf")) {
            ttfFiles.push_back(entry.path());
        }
    }
    if (ttfFiles.empty()) {
        return;
    }
    // Load Regular variants last so base family aliases resolve to Regular weight.
    auto isRegular = [](const std::filesystem::path& p) {
        const std::string name = p.stem().string();
        return name.find("Regular") != std::string::npos ||
               name.find("regular") != std::string::npos;
    };
    std::sort(ttfFiles.begin(), ttfFiles.end(),
        [&](const std::filesystem::path& a, const std::filesystem::path& b) {
            const bool aReg = isRegular(a);
            const bool bReg = isRegular(b);
            if (aReg != bReg) {
                return !aReg;
            }
            return a.generic_string() < b.generic_string();
        });
    FontAtlas& fonts = resourceStore_->Fonts();
    for (const auto& ttf : ttfFiles) {
        (void)fonts.LoadTrueTypeFontFromFile(ttf.generic_string());
    }
    if (fonts.DefaultFontFamily().empty()) {
        if (fonts.HasFontFamily("PT Root UI")) {
            fonts.SetDefaultFontFamily("PT Root UI");
        } else if (fonts.HasFontFamily("Roboto")) {
            fonts.SetDefaultFontFamily("Roboto");
        }
    }
}

void AeroAppHost::RegisterAssetDirectory(std::string_view dir) {
    RegisterAssetDirectoryInternal(dir);
}

void AeroAppHost::DiscoverAssetDirectories(const std::filesystem::path& sampleRoot) {
    std::vector<std::string> dirs;
    app::DiscoverSampleAssetDirectories(sampleRoot, dirs);
    for (const auto& dir : dirs) {
        RegisterAssetDirectoryInternal(dir);
    }
}

void AeroAppHost::SetDevCompileResultOpaque(void* result, DevOpaqueDeleterFn deleter) noexcept {
    if (devCompileResultPtr_ && devCompileResultDeleter_) {
        devCompileResultDeleter_(devCompileResultPtr_);
    }
    devCompileResultPtr_ = result;
    devCompileResultDeleter_ = deleter;
}

const void* AeroAppHost::DevCompileResultOpaque() const noexcept {
    return devCompileResultPtr_;
}

void AeroAppHost::SetLastErrorMessage(std::string message) {
    lastError_ = std::move(message);
}

AeroAppHost::~AeroAppHost() {
    Shutdown();
    SetDevCompileResultOpaque(nullptr, nullptr);
}

void AeroAppHost::WireKernelBridges() {
    desktopHost_.SetBindingFlushCallback(
        [](void* ud) noexcept {
            if (auto* self = static_cast<AeroAppHost*>(ud)) {
                self->FlushBindings();
            }
        },
        this);

    windowHost_.SetModalFrameCallback(
        [](float dtSeconds, void* ud) noexcept {
            if (auto* self = static_cast<AeroAppHost*>(ud)) {
                self->StepFrame(dtSeconds, true);
            }
        },
        this);

    animEngine_.SetWakeHostCallback([this]() {
        desktopHost_.SetActiveAnimations(true);
        desktopHost_.TriggerWake();
        windowHost_.WakeEventLoop();
    });

    platformHooks_.requestWake = [this]() {
        scriptWakeRequested_ = true;
        pendingScriptSync_ = true;
        desktopHost_.TriggerWake();
        windowHost_.WakeEventLoop();
    };

    platformHooks_.requestRenderWake = [this]() {
        renderWakeRequested_ = true;
        desktopHost_.TriggerWake();
        windowHost_.WakeEventLoop();
    };

    platformHooks_.requestWakeAfterMs = [this](std::int32_t delayMs) {
        const std::int32_t clamped = std::clamp(delayMs, 1, 60000);
        const auto target = std::chrono::steady_clock::now() + std::chrono::milliseconds(clamped);
        if (!scheduledWakeAt_.has_value() || target < *scheduledWakeAt_) {
            scheduledWakeAt_ = target;
        }
    };

    platformHooks_.closeWindow = [this]() {
        closeRequested_ = true;
        windowHost_.RequestClose();
        desktopHost_.TriggerWake();
        windowHost_.WakeEventLoop();
    };

    platformHooks_.playSound = [this](std::string_view uri, float volume) {
        const float clampedVol = std::clamp(volume, 0.0f, 1.0f);
        const std::string resolved = ResolveSoundAssetPath(uri);
        if (playedSounds_.size() >= 256u) {
            playedSounds_.erase(playedSounds_.begin(), playedSounds_.begin() + 128);
        }
        playedSounds_.push_back(PlayedSoundRecord{std::string(uri), resolved, clampedVol});
        if (!config_.headless && !resolved.empty()) {
            windowHost_.PlaySoundFile(resolved, clampedVol);
        }
    };

    inputRouter_.SetScriptEventDispatcher(
        [this](VisualNodeId nodeId, VisualCallbackFlags flag, const NativeInputEvent& ev) -> bool {
            return HandleScriptInputEvent(nodeId, flag, ev);
        });

    world_.SetCustomLayoutCallbacks(
        [this](VisualWorld& w, VisualNodeId nid, Size avail) -> Size {
            return HandleCustomMeasure(w, nid, avail);
        },
        [this](VisualWorld& w, VisualNodeId nid, Rect slot) {
            HandleCustomArrange(w, nid, slot);
        });
}

bool AeroAppHost::InitializeRuntime() {
    if (initialized_) {
        return true;
    }

    lastError_.clear();
    const float vpW = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth));
    const float vpH = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight));

    desktopHost_.SetViewportSize(Size{vpW, vpH});
    windowHost_.BindDesktopHost(&desktopHost_, &renderTree_, &gpuStream_, &inputRouter_);

    WireKernelBridges();

    NativeWindowConfig winCfg{};
    winCfg.title = config_.windowTitle;
    winCfg.width = std::max<std::uint32_t>(1u, config_.viewportWidth);
    winCfg.height = std::max<std::uint32_t>(1u, config_.viewportHeight);
    winCfg.clearColor = config_.clearColor;
    winCfg.preferHardwareGpu = !config_.preferSoftwareRasterizer && !config_.headless;
    winCfg.enableHighDpi = config_.enableHighDpi;
    winCfg.dpiScale = config_.dpiScale;
    winCfg.allowHeadlessFallback = true;
    winCfg.forceHeadless = config_.headless;

    if (!windowHost_.Initialize(winCfg)) {
        lastError_ = "NativeWindowHost initialization failed";
        return false;
    }

    if (!config_.headless && windowHost_.Mode() != NativeWindowMode::HeadlessSoftware) {
        const float dpi = std::max(0.25f, windowHost_.DpiScale());
        const float treeDpi = (windowHost_.Mode() == NativeWindowMode::Win32GdiSoftware) ? 1.0f : dpi;
        config_.viewportWidth = std::max<std::uint32_t>(1u, windowHost_.LogicalWidth());
        config_.viewportHeight = std::max<std::uint32_t>(1u, windowHost_.LogicalHeight());
        desktopHost_.SetDpiScale(dpi);
        desktopHost_.SetViewportSize(Size{
            static_cast<float>(windowHost_.Width()),
            static_cast<float>(windowHost_.Height())});
        renderTree_.SetRootDpiScale(treeDpi);
    } else {
        desktopHost_.SetDpiScale(1.0f);
        desktopHost_.SetViewportSize(Size{
            static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth)),
            static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight))});
        renderTree_.SetRootDpiScale(1.0f);
    }

    initialized_ = true;
    return true;
}

std::string AeroAppHost::ResolveSoundAssetPath(std::string_view uri) const {
    if (uri.empty()) {
        return {};
    }
    std::string clean(uri);
    const std::size_t compPos = clean.find(";component/");
    if (compPos != std::string::npos) {
        clean.erase(0u, compPos + 11u);
    } else {
        const std::size_t compBackPos = clean.find(";component\\");
        if (compBackPos != std::string::npos) {
            clean.erase(0u, compBackPos + 11u);
        }
    }
    std::replace(clean.begin(), clean.end(), '\\', '/');
    while (!clean.empty() && clean.front() == '/') {
        clean.erase(clean.begin());
    }
    if (clean.empty()) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path directRaw{std::string(uri)};
    if (directRaw.is_absolute() && std::filesystem::is_regular_file(directRaw, ec)) {
        return directRaw.lexically_normal().string();
    }
    const std::filesystem::path relPath(clean);
    if (std::filesystem::is_regular_file(relPath, ec)) {
        return relPath.lexically_normal().string();
    }

    const auto searchDirs = resourceStore_
        ? resourceStore_->AssetSearchDirectoriesSnapshot()
        : std::vector<std::string>{};
    std::vector<std::filesystem::path> candidateSuffixes;
    candidateSuffixes.push_back(relPath);
    if (clean.size() > 5u && aero::str::EqualsIgnoreCase(std::string_view(clean).substr(0u, 5u), "Data/")) {
        candidateSuffixes.emplace_back(clean.substr(5u));
    }
    const std::filesystem::path fileName = relPath.filename();
    if (!fileName.empty() && fileName != relPath) {
        candidateSuffixes.push_back(fileName);
    }

    for (const std::string& dir : searchDirs) {
        const std::filesystem::path baseDir(dir);
        for (const std::filesystem::path& suffix : candidateSuffixes) {
            const std::filesystem::path candidate = baseDir / suffix;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate.lexically_normal().string();
            }
        }
    }
    return {};
}

void AeroAppHost::ResetLoadedProgramState() noexcept {
    closeRequested_ = false;
    inStepFrame_ = false;
    renderWakeRequested_ = false;
    pendingScriptSync_ = true;
    hasFlushedBindings_ = false;
    lastFlushedObservableEpoch_ = 0u;
    scheduledWakeAt_.reset();
    simObjectPin_.reset();
    simObject_ = ObjectRef{};
    rootObjectPin_.reset();
    rootObject_ = ObjectRef{};
    symbols_ = CachedDispatchSymbols{};
    lastCanvasDrawSizes_.clear();
    lastNotifiedViewport_ = Size{-1.0f, -1.0f};
    playedSounds_.clear();

    animEngine_.Clear();
    drawBuffer_.ClearAll();
    renderTree_.Clear();
    world_.Clear();
    desktopHost_.SetStateFlags(false, false, false, false);

    domains_.clearDesktopJitInvoker();
    simStencilModule_.reset();
    uiStencilModule_.reset();

    if (simInterpreter_) {
        domains_.detachInterpreter(*simInterpreter_);
        simInterpreter_.reset();
    }
    if (interpreter_) {
        domains_.detachInterpreter(*interpreter_);
        interpreter_.reset();
    }
    if (simAotContext_) {
        domains_.detachAotContext(*simAotContext_);
        simAotContext_.reset();
    }
    if (aotContext_) {
        domains_.detachAotContext(*aotContext_);
        aotContext_.reset();
    }
    programImage_.reset();
    aotDescriptor_ = nullptr;
    aotPrebuiltTypes_.clear();
    aotPrebuiltFunctions_.clear();
    preferredEntryClass_.clear();
    bindings_ = std::make_shared<BindingRegistry>();
}

void AeroAppHost::ResolveDispatchSymbols() {
    symbols_ = CachedDispatchSymbols{};
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    const auto find = [&](std::string_view suffix) -> SymbolId {
        return FindBackendFunctionSymbol(backend, suffix);
    };
    symbols_.dispatchPointer = find("InputDispatcher::__AeroDispatchPointer");
    symbols_.dispatchKey = find("InputDispatcher::__AeroDispatchKey");
    symbols_.dispatchText = find("InputDispatcher::__AeroDispatchText");
    symbols_.dispatchFocus = find("InputDispatcher::__AeroDispatchFocus");
    symbols_.dispatchCustomMeasure = find("InputDispatcher::__AeroDispatchCustomMeasure");
    symbols_.dispatchDraw = find("InputDispatcher::__AeroDispatchDraw");
    symbols_.bindingFlush = find("BindingEngine::Flush");
    symbols_.bindingFlushPostLayout = find("BindingEngine::FlushPostLayout");
    symbols_.bindingStepFrameTicks = find("BindingEngine::StepFrameTicks");
    symbols_.windowShow = find("Window::Show");
    symbols_.applyTemplate = find("FrameworkElement::ApplyTemplate");
    symbols_.controlApplyTemplate = find("Control::ApplyTemplate");
    symbols_.applyTemplateSubtree = find("UIElement::ApplyTemplateSubtree");
    symbols_.ensureLoadedSubtree = find("UIElement::EnsureLoadedSubtree");
    symbols_.popupSetOverlayHost = find("PopupService::SetOverlayHost");
    symbols_.registerWorkbenchFactories = find("Aero.UI::__RegisterWorkbenchXamlFactories");
    if (symbols_.registerWorkbenchFactories == 0u) {
        symbols_.registerWorkbenchFactories = find("__RegisterWorkbenchXamlFactories");
    }
    symbols_.registerCompiledXamlCatalog = find("__AeroCompiledXamlCatalogInit::RegisterAll");
}

bool AeroAppHost::LoadProgramImage(
    std::shared_ptr<const ProgramImage> image,
    std::string_view entryClassOrFn) {
    return LoadProgramImageInternal(std::move(image), entryClassOrFn, {});
}

bool AeroAppHost::LoadProgramImageWithPreferredEntry(
    std::shared_ptr<const ProgramImage> image,
    std::string_view entryClassOrFn,
    std::string_view preferredEntryClass) {
    return LoadProgramImageInternal(std::move(image), entryClassOrFn, preferredEntryClass);
}

bool AeroAppHost::LoadProgramImageBytes(
    const std::uint8_t* data,
    std::size_t size,
    std::string_view entryClassOrFn,
    std::string_view preferredEntryClass) {
    std::vector<rs_bc::Module> modules;
    std::string decodeErr;
    if (!DecodeProgramBundleBytes(data, size, modules, decodeErr)) {
        lastError_ = std::move(decodeErr);
        return false;
    }
    rs_rt::RuntimeError linkErr{};
    auto linked = ProgramImage::link(std::move(modules), linkErr, /*verifyModules=*/false);
    if (!linked.has_value()) {
        lastError_ = "ProgramImage::link failed: " + linkErr.message;
        return false;
    }
    return LoadProgramImageInternal(
        std::make_shared<const ProgramImage>(std::move(*linked)),
        entryClassOrFn,
        preferredEntryClass);
}

bool AeroAppHost::LoadProgramImageFile(
    const std::filesystem::path& filePath,
    std::string_view entryClassOrFn,
    std::string_view preferredEntryClass) {
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        lastError_ = "LoadProgramImageFile: cannot open file: " + filePath.string();
        return false;
    }
    const std::streamoff endPos = in.tellg();
    if (endPos <= 0) {
        lastError_ = "LoadProgramImageFile: empty bytecode file: " + filePath.string();
        return false;
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(endPos));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), endPos)) {
        lastError_ = "LoadProgramImageFile: failed reading file: " + filePath.string();
        return false;
    }
    return LoadProgramImageBytes(bytes.data(), bytes.size(), entryClassOrFn, preferredEntryClass);
}

bool AeroAppHost::LoadProgramImageInternal(
    std::shared_ptr<const ProgramImage> image,
    std::string_view entryClassOrFn,
    std::string_view preferredEntryClass) {
    const bool trace = IsAeroTraceEnabled();
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] %s\n", msg);
            std::fflush(stderr);
        }
    };
    logTrace("LoadProgramImage: start");
    if (!image) {
        lastError_ = "LoadProgramImage: null ProgramImage";
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }

    ResetLoadedProgramState();

    programImage_ = std::move(image);
    preferredEntryClass_ = std::string(preferredEntryClass);
    domains_.registerProgramObservableMetadata(*programImage_);

    domains_.clearDesktopJitInvoker();
    interpreter_ = std::make_unique<Interpreter>(programImage_, domains_.uiHeapPtr());
    domains_.attachInterpreter(*interpreter_, DomainKind::UiPresentation);

    if (!RegisterVisualHostBindings(
            *bindings_,
            world_,
            drawBuffer_,
            animEngine_,
            inputRouter_,
            domains_,
            programImage_.get(),
            interpreter_.get(),
            nullptr,
            &platformHooks_)) {
        lastError_ = "RegisterVisualHostBindings failed (Bytecode mode)";
        return false;
    }

    interpreter_->setBindingRegistry(bindings_);
    SetupStencilJitIfEnabled();
    if (domains_.hasSimStateTypes() || domains_.hasSimCommands()) {
        (void)EnsureSimInterpreter();
    }

    ResolveDispatchSymbols();
    logTrace("LoadProgramImage: bindings & symbols ready");

    std::string entryHint(entryClassOrFn);
    if (entryHint.empty()) {
        entryHint = !config_.entryFunctionName.empty() ? config_.entryFunctionName : config_.entryClassName;
    }
    if (!InstantiateAndMountEntry(entryHint)) {
        return false;
    }
    logTrace("LoadProgramImage: mounted entry, stepping initial frame");

    StepFrame(0.0f, true);
    ZeroCopyGpuStream::ReleaseTransientPools();
    if (programImage_) {
        programImage_->releaseTransientLazyCaches();
    }
    {
        rs_rt::ShadowStack emptyStack;
        domains_.uiHeap().collectFull(emptyStack);
        (void)domains_.uiHeap().trimExcessCapacity();
    }
#ifdef _WIN32
    (void)_heapmin();
    (void)HeapCompact(GetProcessHeap(), 0u);
#endif
    if (trace) {
        const VisualNode* rn = world_.GetNode(world_.Root());
        const Rect r = rn ? rn->layout.layoutRect : Rect{};
        std::fprintf(
            stderr,
            "[AERO_TRACE] LoadProgramImage: done liveNodes=%zu renderNodes=%zu rootRect=(%.1f,%.1f %.1fx%.1f)\n",
            world_.LiveNodeCount(),
            renderTree_.NodeCount(),
            r.x,
            r.y,
            r.width,
            r.height);
        auto dumpNode = [&](auto& self, VisualNodeId id, int depth) -> void {
            const VisualNode* n = world_.GetNode(id);
            if (!n || !n->alive) {
                return;
            }
            std::string name;
            if (n->hasScriptOwner && n->scriptOwner.valid()) {
                const ScriptBackendView backend = MakeBackendView(
                    domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
                UnifiedValue nv{};
                if (ReadSingleMember(backend, n->scriptOwner, "Name", nv)) {
                    (void)TryExtractStringValue(backend, nv, name);
                }
            }
            std::fprintf(
                stderr,
                "[AERO_TRACE] %*snode idx=%u kind=%u mode=%u vis=%u op=%.2f rect=(%.1f,%.1f %.1fx%.1f) des=(%.1fx%.1f) bg=%08X grad=%d vfill=%08X vlen=%zu render=(%u+%u) name='%s'\n",
                depth * 2,
                "",
                id.index,
                static_cast<unsigned>(n->kind),
                static_cast<unsigned>(n->layout.layoutMode),
                static_cast<unsigned>(n->layout.visibility),
                n->layout.opacity,
                n->layout.layoutRect.x,
                n->layout.layoutRect.y,
                n->layout.layoutRect.width,
                n->layout.layoutRect.height,
                n->layout.desiredSize.width,
                n->layout.desiredSize.height,
                n->box.bgRgba,
                n->box.bgGradId,
                n->Vector().fillRgba,
                n->Vector().pathOrParams.size(),
                n->layout.renderNodeId,
                n->layout.renderNodeCount,
                name.c_str());
            for (const auto& ch : n->children) {
                self(self, ch, depth + 1);
            }
        };
        dumpNode(dumpNode, world_.Root(), 0);
        std::fflush(stderr);
    }
    return true;
}

bool AeroAppHost::LoadAotProgram(
    const rs_aot::ProgramDescriptor* descriptor,
    std::string_view entryFunctionOrClass) {
    if (!descriptor) {
        lastError_ = "LoadAotProgram: null ProgramDescriptor";
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }

    ResetLoadedProgramState();

    aotDescriptor_ = descriptor;
    domains_.registerAotProgramAttributes(*aotDescriptor_);

    aotPrebuiltTypes_.clear();
    if (aotDescriptor_->types && aotDescriptor_->typeCount > 0u) {
        aotPrebuiltTypes_.reserve(aotDescriptor_->typeCount);
        for (std::uint32_t i = 0u; i < aotDescriptor_->typeCount; ++i) {
            aotPrebuiltTypes_.emplace(aotDescriptor_->types[i].id, &aotDescriptor_->types[i]);
        }
    }
    aotPrebuiltFunctions_.clear();
    if (aotDescriptor_->functions && aotDescriptor_->functionCount > 0u) {
        aotPrebuiltFunctions_.reserve(aotDescriptor_->functionCount);
        for (std::uint32_t i = 0u; i < aotDescriptor_->functionCount; ++i) {
            aotPrebuiltFunctions_.emplace(aotDescriptor_->functions[i].symbolId, &aotDescriptor_->functions[i]);
        }
    }

    rs_rt::ExecutionOptions aotOpts{};
    aotOpts.limits.instructionBudget = 128'000'000u;
    aotOpts.limits.gcWorkBudget = 0u;

    aotContext_ = std::make_unique<rs_aot::ExecutionContext>(
        *aotDescriptor_, domains_.uiHeapPtr(), bindings_, aotOpts,
        &aotPrebuiltTypes_, &aotPrebuiltFunctions_);
    simAotContext_ = std::make_unique<rs_aot::ExecutionContext>(
        *aotDescriptor_, domains_.simHeapPtr(), bindings_, aotOpts,
        &aotPrebuiltTypes_, &aotPrebuiltFunctions_);
    domains_.attachAotContext(*aotContext_, DomainKind::UiPresentation);
    domains_.attachAotContext(*simAotContext_, DomainKind::SimDeterministic);

    if (!RegisterVisualHostBindings(
            *bindings_,
            world_,
            drawBuffer_,
            animEngine_,
            inputRouter_,
            domains_,
            nullptr,
            nullptr,
            aotContext_.get(),
            &platformHooks_)) {
        lastError_ = "RegisterVisualHostBindings failed (AOT mode)";
        return false;
    }

    ResolveDispatchSymbols();

    std::string entryHint(entryFunctionOrClass);
    if (entryHint.empty()) {
        entryHint = !config_.entryFunctionName.empty() ? config_.entryFunctionName : config_.entryClassName;
    }
    if (!InstantiateAndMountEntry(entryHint)) {
        return false;
    }

    StepFrame(0.0f, true);
    return true;
}

void AeroAppHost::SetupStencilJitIfEnabled() {
    uiStencilModule_.reset();
    simStencilModule_.reset();
    domains_.resetDesktopJitCounters();
#ifdef AERO_HAS_STENCIL_JIT
    if (!config_.enableStencilJit || !programImage_ || !realscript::jit::StencilJit::nativeAvailable()) {
        return;
    }
    const bool isLazyProgram = (programImage_->totalLazyFunctionCount() > 0u);
    const realscript::jit::StencilJit jitCompiler;
    realscript::jit::StencilJitOptions jitOpts{};
    jitOpts.enableNativeCodegen = true;
    jitOpts.enableThreadedFallback = !isLazyProgram;
    jitOpts.hotnessThreshold = isLazyProgram
        ? std::max<std::uint32_t>(1u, config_.stencilJitHotnessThreshold)
        : config_.stencilJitHotnessThreshold;
    auto compileRes = jitCompiler.compile(programImage_, jitOpts);
    if (!compileRes.succeeded() ||
        !compileRes.module ||
        !compileRes.module->valid() ||
        !(isLazyProgram ||
          jitOpts.hotnessThreshold > 0u ||
          compileRes.nativeFunctionCount > 0u ||
          compileRes.threadedFunctionCount > 0u)) {
        return;
    }
    auto uiMod = compileRes.module;
    uiMod->setHeap(domains_.uiHeapPtr());
    uiMod->setBindingRegistry(bindings_);
    uiStencilModule_ = uiMod;
    if (simInterpreter_) {
        EnsureSimStencilJit();
    }
    domains_.setDesktopJitInvoker(
        [this](
            DomainKind domain,
            SymbolId functionId,
            const UnifiedValue* arguments,
            std::size_t argumentCount,
            bool& outSucceeded,
            UnifiedValue& outResult,
            rs_rt::RuntimeError& outError) -> bool {
            const realscript::jit::StencilModule* targetMod = nullptr;
            if (domain == DomainKind::UiPresentation) {
                targetMod = static_cast<const realscript::jit::StencilModule*>(uiStencilModule_.get());
            } else if (domain == DomainKind::SimDeterministic) {
                if (!simStencilModule_) {
                    EnsureSimStencilJit();
                }
                targetMod = static_cast<const realscript::jit::StencilModule*>(simStencilModule_.get());
            } else {
                return false;
            }
            if (!targetMod || !targetMod->valid()) {
                return false;
            }
            rs_rt::ExecutionOptions execOpts{};
            execOpts.limits.instructionBudget = 128'000'000u;
            execOpts.limits.gcWorkBudget = 0u;
            execOpts.executionPolicy = domains_.executionPolicy(domain);
            rs_rt::ExecutionResult exec{};
            if (!targetMod->tryInvokeSpan(
                    functionId,
                    rs_rt::ValueSpan(arguments, argumentCount),
                    std::move(execOpts),
                    exec)) {
                return false;
            }
            outSucceeded = exec.succeeded;
            outResult = std::move(exec.value);
            outError = std::move(exec.error);
            return true;
        });
#endif
}

void AeroAppHost::EnsureSimStencilJit() const {
#ifdef AERO_HAS_STENCIL_JIT
    if (simStencilModule_ || !config_.enableStencilJit || !programImage_ ||
        !realscript::jit::StencilJit::nativeAvailable()) {
        return;
    }
    const bool isLazyProgram = (programImage_->totalLazyFunctionCount() > 0u);
    const realscript::jit::StencilJit jitCompiler;
    realscript::jit::StencilJitOptions jitOpts{};
    jitOpts.enableNativeCodegen = true;
    jitOpts.enableThreadedFallback = !isLazyProgram;
    jitOpts.hotnessThreshold = isLazyProgram
        ? std::max<std::uint32_t>(1u, config_.stencilJitHotnessThreshold)
        : config_.stencilJitHotnessThreshold;
    auto compileRes = jitCompiler.compile(programImage_, jitOpts);
    if (compileRes.succeeded() &&
        compileRes.module &&
        compileRes.module->valid() &&
        (isLazyProgram ||
         jitOpts.hotnessThreshold > 0u ||
         compileRes.nativeFunctionCount > 0u ||
         compileRes.threadedFunctionCount > 0u)) {
        auto simMod = compileRes.module;
        simMod->setHeap(domains_.simHeapPtr());
        simMod->setBindingRegistry(bindings_);
        simStencilModule_ = simMod;
        if (uiStencilModule_) {
            const auto uiMod = std::static_pointer_cast<const realscript::jit::StencilModule>(uiStencilModule_);
            if (uiMod && uiMod->valid()) {
                uiMod->attachToProgramImage(*programImage_);
            }
        }
    }
#endif
}

Interpreter* AeroAppHost::EnsureSimInterpreter() const {
    if (!simInterpreter_ && programImage_) {
        simInterpreter_ = std::make_unique<Interpreter>(programImage_, domains_.simHeapPtr());
        domains_.attachInterpreter(*simInterpreter_, DomainKind::SimDeterministic);
        if (bindings_) {
            simInterpreter_->setBindingRegistry(bindings_);
        }
        EnsureSimStencilJit();
    }
    return simInterpreter_.get();
}

Interpreter* AeroAppHost::SimInterpreter() {
    return EnsureSimInterpreter();
}

bool AeroAppHost::InvokeSymbol(
    SymbolId fnId,
    rs_rt::ValueSpan args,
    UnifiedValue* outResult,
    DomainKind domain) const {
    if (fnId == 0u) {
        return false;
    }
    if (programImage_) {
        Interpreter* targetInterp =
            (domain == DomainKind::SimDeterministic)
                ? EnsureSimInterpreter()
                : interpreter_.get();
        if (!targetInterp) {
            return false;
        }
        if (IsAeroTraceEnabled()) {
            std::fprintf(stderr, "[AERO_TRACE] InvokeSymbol: bytecode sym=%llu args=%zu\n",
                static_cast<unsigned long long>(fnId), args.size());
            std::fflush(stderr);
        }
        rs_rt::ExecutionOptions execOpts{};
        execOpts.limits.instructionBudget = 128'000'000u;
        execOpts.limits.gcWorkBudget = 0u;
        const auto exec = domains_.invokeMethod(domain, targetInterp, fnId, args.data(), args.size(), execOpts);
        if (IsAeroTraceEnabled()) {
            std::fprintf(stderr, "[AERO_TRACE] InvokeSymbol: returned ok=%d err=%s\n",
                exec.succeeded ? 1 : 0, exec.error.message.c_str());
            for (const auto& st : exec.error.stackTrace) {
                std::fprintf(stderr, "[AERO_TRACE]   at %s\n", st.c_str());
            }
            std::fflush(stderr);
        }
        if (!exec.succeeded) {
            return false;
        }
        if (outResult) {
            *outResult = exec.value;
        }
        return true;
    }
    if (aotDescriptor_) {
        rs_aot::ExecutionContext* targetCtx =
            (domain == DomainKind::SimDeterministic && simAotContext_)
                ? simAotContext_.get()
                : aotContext_.get();
        if (!targetCtx) {
            return false;
        }
        UnifiedValue res{};
        if (!targetCtx->invoke(fnId, args.data(), args.size(), res)) {
            return false;
        }
        if (outResult) {
            *outResult = res;
        }
        return true;
    }
    return false;
}

bool AeroAppHost::InstantiateAndMountEntry(std::string_view entryHint) {
    const bool trace = IsAeroTraceEnabled();
    if (trace) {
        std::fprintf(stderr, "[AERO_TRACE] InstantiateAndMountEntry(entryHint='%.*s')\n",
            static_cast<int>(entryHint.size()), entryHint.data());
        if (programImage_) {
            for (const auto& m : programImage_->modules()) {
                if (m.name.rfind("Aero.", 0u) == 0u || m.name.rfind("RealScript.", 0u) == 0u) {
                    continue;
                }
                std::fprintf(stderr, "[AERO_TRACE]   user mod='%s' fns=%zu types=%zu\n",
                    m.name.c_str(), m.functions.size(), m.types.size());
                for (const auto& fn : m.functions) {
                    std::fprintf(stderr, "[AERO_TRACE]     fn id=%llu name='%s'\n",
                        static_cast<unsigned long long>(fn.symbolId), fn.name.c_str());
                }
                for (const auto& ts : m.types) {
                    std::fprintf(stderr, "[AERO_TRACE]     type id=%llu mod='%s' name='%s' base=%llu ctors=%zu\n",
                        static_cast<unsigned long long>(ts.id),
                        ts.moduleName.c_str(),
                        ts.name.c_str(),
                        static_cast<unsigned long long>(ts.baseTypeId),
                        ts.constructors.size());
                }
            }
        }
        std::fflush(stderr);
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);

    if (symbols_.registerWorkbenchFactories != 0u) {
        (void)InvokeSymbol(
            symbols_.registerWorkbenchFactories,
            rs_rt::ValueSpan{},
            nullptr,
            DomainKind::UiPresentation);
    }
    if (symbols_.registerCompiledXamlCatalog != 0u) {
        (void)InvokeSymbol(
            symbols_.registerCompiledXamlCatalog,
            rs_rt::ValueSpan{},
            nullptr,
            DomainKind::UiPresentation);
    }

    auto tryInvokeCandidateFn = [&](SymbolId fnId, ObjectRef& outObj) -> bool {
        if (fnId == 0u) {
            return false;
        }
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] invoking fnId=%llu\n",
                static_cast<unsigned long long>(fnId));
            std::fflush(stderr);
        }
        UnifiedValue res{};
        if (!InvokeSymbol(fnId, rs_rt::ValueSpan{}, &res, DomainKind::UiPresentation)) {
            if (trace) {
                std::fprintf(stderr, "[AERO_TRACE] fnId=%llu invoke failed\n",
                    static_cast<unsigned long long>(fnId));
                std::fflush(stderr);
            }
            return false;
        }
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] fnId=%llu succeeded tag=%d\n",
                static_cast<unsigned long long>(fnId), static_cast<int>(res.rawTag()));
            std::fflush(stderr);
        }
        if (const auto* objRef = std::get_if<ObjectRef>(&res); objRef && objRef->valid()) {
            outObj = *objRef;
            return true;
        }
        return false;
    };

    auto instantiateBytecodeType = [&](const rs_sem::TypeSymbol& rawTs) -> bool {
        const rs_sem::TypeSymbol* resolved = programImage_->resolveType(rawTs.id);
        const rs_sem::TypeSymbol& ts = resolved ? *resolved : rawTs;
        ObjectRef allocObj{};
        if (!AllocateBytecodeObjectWithDefaults(*programImage_, domains_.uiHeap(), ts, allocObj, &lastError_)) {
            return false;
        }
        PersistentRoot pin = domains_.uiHeap().retain(allocObj);
        const SymbolId ctorId = FindBytecodeDefaultConstructor(*programImage_, ts);
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] instantiateBytecodeType '%s.%s' id=%llu fields=%zu ctorId=%llu\n",
                ts.moduleName.c_str(), ts.name.c_str(),
                static_cast<unsigned long long>(ts.id),
                ts.fields.size(),
                static_cast<unsigned long long>(ctorId));
            std::fflush(stderr);
        }
        if (ctorId != 0u) {
            rs_rt::ExecutionOptions ctorOpts{};
            ctorOpts.limits.instructionBudget = 128'000'000u;
            ctorOpts.limits.gcWorkBudget = 0u;
            const auto exec = domains_.invokeMethod(
                DomainKind::UiPresentation, interpreter_.get(), ctorId, {UnifiedValue{allocObj}}, ctorOpts);
            if (!exec.succeeded) {
                std::string stackSummary;
                for (std::size_t si = 0u; si < exec.error.stackTrace.size() && si < 6u; ++si) {
                    stackSummary += (si == 0u ? " [" : " -> ") + exec.error.stackTrace[si];
                }
                if (!stackSummary.empty()) {
                    stackSummary.push_back(']');
                }
                lastError_ = "Entry constructor failed: " + exec.error.message + stackSummary;
                return false;
            }
        } else {
            ObjectRef builtObj{};
            const SymbolId clsBuild = FindBytecodeFunctionSymbol(
                *programImage_,
                (ts.moduleName.empty() ? ts.name : (ts.moduleName + "." + ts.name)) + "::Build");
            if (tryInvokeCandidateFn(clsBuild, builtObj)) {
                return MountScriptRootObject(builtObj);
            }
        }
        return MountScriptRootObject(allocObj);
    };

    auto instantiateAotType = [&](const rs_aot::TypeDescriptor& td) -> bool {
        UnifiedValue allocVal{};
        if (!aotContext_->newObject(td.id, allocVal)) {
            lastError_ = "AOT newObject failed: " + aotContext_->error().message;
            return false;
        }
        if (const auto* objRef = std::get_if<ObjectRef>(&allocVal); objRef && objRef->valid()) {
            PersistentRoot pin = domains_.uiHeap().retain(*objRef);
            const SymbolId ctorId = FindAotClassMethod(*aotDescriptor_, td.id, ".ctor", 1u);
            if (ctorId != 0u) {
                UnifiedValue unused{};
                if (!aotContext_->invoke(ctorId, &allocVal, 1u, unused)) {
                    lastError_ = "AOT entry constructor failed: " + aotContext_->error().message;
                    return false;
                }
            }
            return MountScriptRootObject(*objRef);
        }
        return false;
    };

    auto initializeAppResourcesIfPresent = [&]() {
        if (programImage_) {
            for (const auto& mod : programImage_->modules()) {
                if (IsStdlibModuleName(mod.name)) {
                    continue;
                }
                for (const auto& ts : mod.types) {
                    if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType ||
                        IsStdlibModuleName(ts.moduleName)) {
                        continue;
                    }
                    if (!ts.moduleName.empty() && ts.moduleName != mod.name) {
                        continue;
                    }
                    if (!IsBytecodeTypeDerivedFrom(*programImage_, &ts, "Application")) {
                        continue;
                    }
                    if ((!entryHint.empty() && NameMatchesTypeSpec(ts.moduleName, ts.name, entryHint)) ||
                        (!preferredEntryClass_.empty() && NameMatchesTypeSpec(ts.moduleName, ts.name, preferredEntryClass_))) {
                        continue;
                    }
                    const rs_sem::TypeSymbol* resolved = programImage_->resolveType(ts.id);
                    const rs_sem::TypeSymbol& fullTs = resolved ? *resolved : ts;
                    ObjectRef allocObj{};
                    if (!AllocateBytecodeObjectWithDefaults(*programImage_, domains_.uiHeap(), fullTs, allocObj)) {
                        continue;
                    }
                    PersistentRoot pin = domains_.uiHeap().retain(allocObj);
                    if (const SymbolId ctorId = FindBytecodeDefaultConstructor(*programImage_, fullTs); ctorId != 0u) {
                        const UnifiedValue ctorArgs[1]{UnifiedValue{allocObj}};
                        (void)InvokeSymbol(ctorId, rs_rt::ValueSpan{ctorArgs, 1u}, nullptr, DomainKind::UiPresentation);
                    }
                    if (const SymbolId initSym = FindBytecodeClassMethod(*programImage_, fullTs.id, "InitializeComponent", 0u); initSym != 0u) {
                        const UnifiedValue initArgs[1]{UnifiedValue{allocObj}};
                        (void)InvokeSymbol(initSym, rs_rt::ValueSpan{initArgs, 1u}, nullptr, DomainKind::UiPresentation);
                    }
                }
            }
        } else if (aotDescriptor_ && aotContext_ && aotDescriptor_->types) {
            for (std::uint32_t ti = 0u; ti < aotDescriptor_->typeCount; ++ti) {
                const auto& td = aotDescriptor_->types[ti];
                if (td.kind != rs_sem::TypeKind::Class || td.interfaceType || td.delegateType || !td.name) {
                    continue;
                }
                const std::string_view modName = td.moduleName ? std::string_view(td.moduleName) : std::string_view{};
                if (IsStdlibModuleName(modName) || !IsAotTypeDerivedFrom(*aotDescriptor_, &td, "Application")) {
                    continue;
                }
                if ((!entryHint.empty() && NameMatchesTypeSpec(modName, td.name, entryHint)) ||
                    (!preferredEntryClass_.empty() && NameMatchesTypeSpec(modName, td.name, preferredEntryClass_))) {
                    continue;
                }
                UnifiedValue allocVal{};
                if (!aotContext_->newObject(td.id, allocVal)) {
                    continue;
                }
                if (const auto* objRef = std::get_if<ObjectRef>(&allocVal); objRef && objRef->valid()) {
                    PersistentRoot pin = domains_.uiHeap().retain(*objRef);
                    if (const SymbolId ctorId = FindAotClassMethod(*aotDescriptor_, td.id, ".ctor", 1u); ctorId != 0u) {
                        UnifiedValue unused{};
                        (void)aotContext_->invoke(ctorId, &allocVal, 1u, unused);
                    }
                    if (const SymbolId initId = FindAotClassMethod(*aotDescriptor_, td.id, "InitializeComponent", 1u); initId != 0u) {
                        UnifiedValue unused{};
                        (void)aotContext_->invoke(initId, &allocVal, 1u, unused);
                    }
                }
            }
        }
    };

    ObjectRef entryObj{};

    // 1. If entryHint directly identifies a callable function, invoke it first.
    if (!entryHint.empty()) {
        const SymbolId fnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, entryHint)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, entryHint) : 0u);
        if (tryInvokeCandidateFn(fnId, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
    }

    initializeAppResourcesIfPresent();

    // 2. If entryHint identifies a class (or module.class), try <Class>::Build or construct <Class>.
    if (!entryHint.empty() && programImage_) {
        const SymbolId buildFn = FindBytecodeFunctionSymbol(
            *programImage_, std::string(entryHint) + "::Build");
        if (tryInvokeCandidateFn(buildFn, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& mod : programImage_->modules()) {
                for (const auto& ts : mod.types) {
                    if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType) {
                        continue;
                    }
                    if (pass == 0 && (IsStdlibModuleName(mod.name) || IsStdlibModuleName(ts.moduleName))) {
                        continue;
                    }
                    if (NameMatchesTypeSpec(ts.moduleName, ts.name, entryHint)) {
                        return instantiateBytecodeType(ts);
                    }
                }
            }
        }
    }

    if (!entryHint.empty() && aotDescriptor_) {
        const SymbolId buildFn = FindAotFunctionSymbol(
            *aotDescriptor_, std::string(entryHint) + "::Build");
        if (tryInvokeCandidateFn(buildFn, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
        if (aotDescriptor_->types) {
            for (std::uint32_t ti = 0u; ti < aotDescriptor_->typeCount; ++ti) {
                const auto& td = aotDescriptor_->types[ti];
                if (td.kind != rs_sem::TypeKind::Class || td.interfaceType || td.delegateType || !td.name) {
                    continue;
                }
                const std::string_view modName = td.moduleName ? std::string_view(td.moduleName) : std::string_view{};
                if (NameMatchesTypeSpec(modName, td.name, entryHint)) {
                    return instantiateAotType(td);
                }
            }
        }
    }

    // 3. Auto-discover standard entry factories: CreateMainWindow, Main, CreateRoot
    const char* kStandardFactories[] = {"::CreateMainWindow", "::Main", "::CreateRoot"};
    for (const char* suffix : kStandardFactories) {
        const SymbolId fnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, suffix, true)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, suffix, true) : 0u);
        if (tryInvokeCandidateFn(fnId, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
    }

    // 4. Preferred root class discovered during compilation (e.g. main XAML entry <Stem>View or x:Class)
    if (!preferredEntryClass_.empty() && programImage_) {
        const SymbolId buildFn = FindBytecodeFunctionSymbol(
            *programImage_, preferredEntryClass_ + "::Build");
        if (tryInvokeCandidateFn(buildFn, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
        for (const auto& mod : programImage_->modules()) {
            if (IsStdlibModuleName(mod.name)) {
                continue;
            }
            for (const auto& ts : mod.types) {
                if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType ||
                    IsStdlibModuleName(ts.moduleName)) {
                    continue;
                }
                if (NameMatchesTypeSpec(ts.moduleName, ts.name, preferredEntryClass_)) {
                    return instantiateBytecodeType(ts);
                }
            }
        }
    }

    // 5. Auto-discover first concrete Window -> Page -> UserControl -> UIElement -> Application subclass
    if (programImage_) {
        const char* kBasePriority[] = {"Window", "Page", "UserControl", "UIElement", "Application"};
        for (const char* baseName : kBasePriority) {
            for (const auto& mod : programImage_->modules()) {
                if (IsStdlibModuleName(mod.name)) {
                    continue;
                }
                for (const auto& ts : mod.types) {
                    if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType ||
                        IsStdlibModuleName(ts.moduleName)) {
                        continue;
                    }
                    if (!ts.moduleName.empty() && ts.moduleName != mod.name) {
                        continue;
                    }
                    if (IsBytecodeTypeDerivedFrom(*programImage_, &ts, baseName)) {
                        return instantiateBytecodeType(ts);
                    }
                }
            }
        }
    } else if (aotDescriptor_ && aotDescriptor_->types) {
        const char* kBasePriority[] = {"Window", "Page", "UserControl", "UIElement", "Application"};
        for (const char* baseName : kBasePriority) {
            for (std::uint32_t ti = 0u; ti < aotDescriptor_->typeCount; ++ti) {
                const auto& td = aotDescriptor_->types[ti];
                if (td.kind != rs_sem::TypeKind::Class || td.interfaceType || td.delegateType || !td.name) {
                    continue;
                }
                const std::string_view modName = td.moduleName ? std::string_view(td.moduleName) : std::string_view{};
                if (IsStdlibModuleName(modName)) {
                    continue;
                }
                if (IsAotTypeDerivedFrom(*aotDescriptor_, &td, baseName)) {
                    return instantiateAotType(td);
                }
            }
        }
    }

    if (world_.Root().IsValid()) {
        return true;
    }

    lastError_ = entryHint.empty()
        ? "Could not auto-discover UI entry function or Window subclass"
        : ("Could not instantiate UI entry '" + std::string(entryHint) + "'");
    return false;
}

bool AeroAppHost::MountScriptRootObject(ObjectRef candidate) {
    if (!candidate.valid()) {
        lastError_ = "MountScriptRootObject: invalid ObjectRef";
        return false;
    }

    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    const SymbolId candTypeId = domains_.uiHeap().objectTypeIdFast(candidate);
    const bool isApplication = IsTypeDerivedFrom(backend, candTypeId, "Application");

    if (isApplication) {
        if (const SymbolId initSym = FindBackendClassMethod(backend, candTypeId, "InitializeComponent", 0u);
            initSym != 0u) {
            const UnifiedValue initArgs[1]{UnifiedValue{candidate}};
            (void)InvokeSymbol(initSym, rs_rt::ValueSpan{initArgs, 1u}, nullptr, DomainKind::UiPresentation);
        }
        UnifiedValue checkWinVal{};
        const bool hasMainWin = ReadSingleMember(backend, candidate, "MainWindow", checkWinVal) &&
            std::holds_alternative<ObjectRef>(checkWinVal) &&
            std::get<ObjectRef>(checkWinVal).valid();
        if (!hasMainWin) {
            if (const SymbolId runSym1 = FindBackendClassMethod(backend, candTypeId, "Run", 1u); runSym1 != 0u) {
                const UnifiedValue runArgs[2]{UnifiedValue{candidate}, UnifiedValue{rs_rt::NullObject{}}};
                (void)InvokeSymbol(runSym1, rs_rt::ValueSpan{runArgs, 2u}, nullptr, DomainKind::UiPresentation);
            } else if (const SymbolId runSym0 = FindBackendClassMethod(backend, candTypeId, "Run", 0u); runSym0 != 0u) {
                const UnifiedValue runArgs[1]{UnifiedValue{candidate}};
                (void)InvokeSymbol(runSym0, rs_rt::ValueSpan{runArgs, 1u}, nullptr, DomainKind::UiPresentation);
            }
        }
    }

    ObjectRef uiRoot = candidate;
    // Unwrap Application -> MainWindow if caller returned an Application instance
    {
        UnifiedValue mainWinVal{};
        if (ReadSingleMember(backend, candidate, "MainWindow", mainWinVal)) {
            if (const auto* winRef = std::get_if<ObjectRef>(&mainWinVal); winRef && winRef->valid()) {
                uiRoot = *winRef;
            }
        }
    }

    const bool trace = IsAeroTraceEnabled();
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] %s\n", msg);
            std::fflush(stderr);
        }
    };
    logTrace("MountScriptRootObject: start");

    rootObject_ = uiRoot;
    rootObjectPin_ = domains_.uiHeap().retain(rootObject_);

    // Prepare template & overlay host before mounting root; defer Window.Show() / EnsureLoadedSubtree
    // until after SetRoot() + layout convergence so Loaded animations observe real ActualWidth/ActualHeight.
    const SymbolId typeId = domains_.uiHeap().objectTypeIdFast(rootObject_);
    SymbolId deferredShowSym = 0u;
    SymbolId deferredLoadSym = 0u;
    if (typeId != 0u) {
        deferredShowSym = FindBackendClassMethod(backend, typeId, "Show", 0u);
        symbols_.stepGame = FindBackendClassMethod(backend, typeId, "StepGame", 1u);
        symbols_.rootSyncHudFromSim = FindBackendClassMethod(backend, typeId, "SyncHudFromSim", 0u);
        if (deferredShowSym == 0u && symbols_.windowShow != 0u &&
            IsTypeDerivedFrom(backend, typeId, "Window")) {
            deferredShowSym = symbols_.windowShow;
        }

        logTrace("MountScriptRootObject: invoking SetOverlayHost/ApplyTemplateSubtree");
        if (symbols_.popupSetOverlayHost != 0u) {
            const UnifiedValue overlayArgs[1]{UnifiedValue{rootObject_}};
            (void)InvokeSymbol(
                symbols_.popupSetOverlayHost, rs_rt::ValueSpan{overlayArgs, 1u}, nullptr, DomainKind::UiPresentation);
        }
        SymbolId tplSym = symbols_.applyTemplateSubtree != 0u
            ? symbols_.applyTemplateSubtree
            : FindBackendClassMethod(backend, typeId, "ApplyTemplate", 0u);
        if (tplSym == 0u) {
            tplSym = symbols_.controlApplyTemplate != 0u
                ? symbols_.controlApplyTemplate
                : symbols_.applyTemplate;
        }
        SymbolId loadSym = FindBackendClassMethod(backend, typeId, "EnsureLoadedSubtree", 0u);
        if (loadSym == 0u) {
            loadSym = symbols_.ensureLoadedSubtree;
        }
        if (tplSym != 0u) {
            const UnifiedValue tplArgs[1]{UnifiedValue{rootObject_}};
            (void)InvokeSymbol(tplSym, rs_rt::ValueSpan{tplArgs, 1u}, nullptr, DomainKind::UiPresentation);
        }
        deferredLoadSym = loadSym;
    }

    logTrace("MountScriptRootObject: reading __nativeHandle");
    // Bind native root handle to VisualWorld::SetRoot
    UnifiedValue handleVal{};
    if (!ReadSingleMember(backend, rootObject_, "__nativeHandle", handleVal)) {
        lastError_ = "Mounted root script object does not expose __nativeHandle";
        return false;
    }

    NativeHandle nh{};
    if (!handleVal.tryGetNativeHandle(nh)) {
        lastError_ = "Mounted root __nativeHandle is not a valid NativeHandle";
        return false;
    }
    const VisualNodeId rootNode = world_.ResolveHandle(nh);
    if (!rootNode.IsValid()) {
        lastError_ = "Mounted root __nativeHandle does not resolve to a live VisualNode (slot=" +
            std::to_string(nh.slot) + ", gen=" + std::to_string(nh.generation) +
            ", reg=" + std::to_string(nh.registryId) + ", tid=" + std::to_string(nh.typeId) +
            ", objTypeId=" + std::to_string(typeId) +
            ", liveNodes=" + std::to_string(world_.LiveNodeCount()) + ")";
        return false;
    }

    world_.SetRoot(rootNode);

    if (world_.Root().IsValid()) {
        const bool hasLiveWin = !config_.headless && windowHost_.IsInitialized() &&
            windowHost_.Mode() != NativeWindowMode::HeadlessSoftware &&
            windowHost_.Width() > 0u && windowHost_.Height() > 0u;
        float initW = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth));
        float initH = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight));
        if (hasLiveWin) {
            const float dpi = std::max(0.25f, windowHost_.DpiScale());
            initW = static_cast<float>(windowHost_.Width()) / dpi;
            initH = static_cast<float>(windowHost_.Height()) / dpi;
        }
        for (int pass = 0; pass < 3; ++pass) {
            world_.UpdateLayout(initW, initH);
            FlushBindings();
            if (world_.FlushTrackB() == 0u && !world_.HasLayoutOrTransformDirty()) {
                break;
            }
        }
        if (world_.HasLayoutOrTransformDirty()) {
            world_.UpdateLayout(initW, initH);
            FlushBindings();
        }

        bool invokedShow = false;
        if (deferredShowSym != 0u) {
            logTrace("MountScriptRootObject: invoking deferred Show");
            const UnifiedValue showArgs[1]{UnifiedValue{rootObject_}};
            invokedShow = InvokeSymbol(
                deferredShowSym, rs_rt::ValueSpan{showArgs, 1u}, nullptr, DomainKind::UiPresentation);
        }
        if (!invokedShow && deferredLoadSym != 0u) {
            const UnifiedValue loadArgs[1]{UnifiedValue{rootObject_}};
            (void)InvokeSymbol(deferredLoadSym, rs_rt::ValueSpan{loadArgs, 1u}, nullptr, DomainKind::UiPresentation);
        }
        world_.UpdateLayout(initW, initH);
        FlushBindings();
    } else if (deferredShowSym != 0u) {
        logTrace("MountScriptRootObject: invoking Show without visual root");
        const UnifiedValue showArgs[1]{UnifiedValue{rootObject_}};
        (void)InvokeSymbol(deferredShowSym, rs_rt::ValueSpan{showArgs, 1u}, nullptr, DomainKind::UiPresentation);
    }

    // Optional Dual-Domain SimDeterministic factory discovery (CreateSimState / CreateSim)
    if (!simObject_.valid()) {
        const SymbolId simFnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, "::CreateSimState", true)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, "::CreateSimState", true) : 0u);
        if (simFnId != 0u) {
            UnifiedValue simRes{};
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            const bool okSim = InvokeSymbol(simFnId, rs_rt::ValueSpan{}, &simRes, DomainKind::SimDeterministic);
            domains_.setActiveDomain(DomainKind::UiPresentation);
            if (okSim) {
                if (const auto* simRef = std::get_if<ObjectRef>(&simRes); simRef && simRef->valid()) {
                    simObject_ = *simRef;
                    simObjectPin_ = domains_.simHeap().retain(simObject_);
                    const SymbolId simTypeId = domains_.simHeap().objectTypeIdFast(simObject_);
                    if (simTypeId != 0u) {
                        if (programImage_) {
                            symbols_.simAdvanceTick = FindBytecodeClassMethod(
                                *programImage_, simTypeId, "AdvanceTick", 1u);
                        } else if (aotDescriptor_) {
                            symbols_.simAdvanceTick = FindAotClassMethod(
                                *aotDescriptor_, simTypeId, "AdvanceTick", 2u);
                        }
                    }
                    (void)InvokeScriptMethod(
                        rootObject_,
                        "AttachSimState",
                        {UnifiedValue{simObject_}},
                        nullptr,
                        DomainKind::UiPresentation);
                }
            }
        }
    }

    logTrace("MountScriptRootObject: done");
    desktopHost_.NotifyDirtyLayout();
    return true;
}

bool AeroAppHost::HandleScriptInputEvent(
    VisualNodeId nodeId,
    VisualCallbackFlags flag,
    const NativeInputEvent& ev) {
    const VisualNode* node = world_.GetNode(nodeId);
    const bool allowNullNode =
        (flag == VisualCallback_PointerLeave ||
         flag == VisualCallback_PointerUp ||
         flag == VisualCallback_PointerMove ||
         flag == VisualCallback_PointerDown);
    if ((!node || !node->nativeHandle.valid()) && !allowNullNode) {
        return false;
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);

    UnifiedValue res{};
    switch (flag) {
        case VisualCallback_PointerMove:
        case VisualCallback_PointerDown:
        case VisualCallback_PointerUp:
        case VisualCallback_PointerEnter:
        case VisualCallback_PointerLeave:
        case VisualCallback_MouseWheel: {
            if (symbols_.dispatchPointer == 0u) {
                return false;
            }
            const NativeHandle argHandle =
                (node && node->nativeHandle.valid()) ? node->nativeHandle : NativeHandle{};
            const UnifiedValue args[8]{
                UnifiedValue{argHandle},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint32_t>(flag))},
                UnifiedValue{FloatValue{ev.x}},
                UnifiedValue{FloatValue{ev.y}},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint8_t>(ev.button))},
                UnifiedValue{static_cast<std::int64_t>(ev.modifiers)},
                UnifiedValue{FloatValue{ev.deltaX}},
                UnifiedValue{FloatValue{ev.deltaY}},
            };
            if (!InvokeSymbol(symbols_.dispatchPointer, rs_rt::ValueSpan{args, 8u}, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (IsAeroTraceEnabled()) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    (node && node->hasScriptOwner) ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_KeyDown:
        case VisualCallback_KeyUp: {
            if (symbols_.dispatchKey == 0u) {
                return false;
            }
            const UnifiedValue args[4]{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint32_t>(flag))},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint16_t>(ev.key))},
                UnifiedValue{static_cast<std::int64_t>(ev.modifiers)},
            };
            if (!InvokeSymbol(symbols_.dispatchKey, rs_rt::ValueSpan{args, 4u}, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (IsAeroTraceEnabled()) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    node->hasScriptOwner ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_TextInput: {
            if (symbols_.dispatchText == 0u) {
                return false;
            }
            const std::string text(ev.utf8Bytes.data(), ev.utf8Length);
            const UnifiedValue args[2]{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{StringValue{text}},
            };
            if (!InvokeSymbol(symbols_.dispatchText, rs_rt::ValueSpan{args, 2u}, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (IsAeroTraceEnabled()) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    node->hasScriptOwner ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_FocusChanged: {
            if (symbols_.dispatchFocus == 0u) {
                return false;
            }
            const UnifiedValue args[2]{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{ev.isDown},
            };
            return InvokeSymbol(symbols_.dispatchFocus, rs_rt::ValueSpan{args, 2u}, nullptr, DomainKind::UiPresentation);
        }
        default:
            return false;
    }
}

Size AeroAppHost::HandleCustomMeasure(
    VisualWorld& world,
    VisualNodeId nodeId,
    Size availSize) {
    const VisualNode* node = world.GetNode(nodeId);
    if (!node) {
        return Size{0.0f, 0.0f};
    }
    if (symbols_.dispatchCustomMeasure != 0u && node->nativeHandle.valid()) {
        const float aw = std::isfinite(availSize.width) ? availSize.width : -1.0f;
        const float ah = std::isfinite(availSize.height) ? availSize.height : -1.0f;
        const UnifiedValue args[3]{
            UnifiedValue{node->nativeHandle},
            UnifiedValue{FloatValue{aw}},
            UnifiedValue{FloatValue{ah}},
        };
        domains_.setActiveDomain(DomainKind::UiPresentation);
        (void)InvokeSymbol(symbols_.dispatchCustomMeasure, rs_rt::ValueSpan{args, 3u}, nullptr, DomainKind::UiPresentation);
    }

    const VisualNode* updated = world.GetNode(nodeId);
    if (!updated) {
        return Size{0.0f, 0.0f};
    }
    const float marginH = std::max(0.0f, updated->layout.margin.left) +
                          std::max(0.0f, updated->layout.margin.right);
    const float marginV = std::max(0.0f, updated->layout.margin.top) +
                          std::max(0.0f, updated->layout.margin.bottom);
    const float padH = std::max(0.0f, updated->layout.padding.left) +
                       std::max(0.0f, updated->layout.padding.right);
    const float padV = std::max(0.0f, updated->layout.padding.top) +
                       std::max(0.0f, updated->layout.padding.bottom);

    auto axisDesired = [](float explicitDim, float minDim, float maxDim, float pad, float avail) noexcept -> float {
        const float lo = std::max(0.0f, std::isfinite(minDim) ? minDim : 0.0f);
        const float hi = (std::isfinite(maxDim) && maxDim >= lo) ? maxDim : 1e9f;
        if (std::isfinite(explicitDim)) {
            return std::clamp(explicitDim, lo, hi);
        }
        const float fallback = std::max(pad, lo);
        if (std::isfinite(avail)) {
            return std::clamp(std::max(fallback, 0.0f), lo, std::min(hi, std::max(0.0f, avail)));
        }
        return std::clamp(fallback, lo, hi);
    };

    const float boxW = axisDesired(
        updated->layout.width,
        updated->layout.minWidth,
        updated->layout.maxWidth,
        padH,
        std::isfinite(availSize.width) ? std::max(0.0f, availSize.width - marginH) : availSize.width);
    const float boxH = axisDesired(
        updated->layout.height,
        updated->layout.minHeight,
        updated->layout.maxHeight,
        padV,
        std::isfinite(availSize.height) ? std::max(0.0f, availSize.height - marginV) : availSize.height);
    return Size{boxW + marginH, boxH + marginV};
}

void AeroAppHost::HandleCustomArrange(
    VisualWorld&,
    VisualNodeId,
    Rect) {}

void AeroAppHost::FlushBindings() {
    pendingScriptSync_ = false;
    if (symbols_.bindingFlush == 0u) {
        hasFlushedBindings_ = true;
        lastFlushedObservableEpoch_ = domains_.globalObservableEpoch();
        return;
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);
    (void)InvokeSymbol(symbols_.bindingFlush, rs_rt::ValueSpan{}, nullptr, DomainKind::UiPresentation);
    hasFlushedBindings_ = true;
    lastFlushedObservableEpoch_ = domains_.globalObservableEpoch();
}

void AeroAppHost::FlushBindingsPostLayout() {
    if (symbols_.bindingFlushPostLayout == 0u) {
        FlushBindings();
        return;
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);
    (void)InvokeSymbol(symbols_.bindingFlushPostLayout, rs_rt::ValueSpan{}, nullptr, DomainKind::UiPresentation);
}

bool AeroAppHost::DispatchPendingCanvasDraws() {
    if (symbols_.dispatchDraw == 0u) {
        return false;
    }
    if (lastCanvasDrawSizes_.size() > 64u) {
        for (auto it = lastCanvasDrawSizes_.begin(); it != lastCanvasDrawSizes_.end();) {
            const auto slotIdx = static_cast<std::uint32_t>(it->first & 0xFFFFFFFFull);
            const auto gen = static_cast<std::uint32_t>(it->first >> 32u);
            if (!world_.IsAlive(VisualNodeId{slotIdx, gen})) {
                it = lastCanvasDrawSizes_.erase(it);
            } else {
                ++it;
            }
        }
    }
    const auto& canvasNodes = world_.CanvasDrawNodes();
    if (canvasNodes.empty()) {
        return false;
    }
    VisualNodeId stackBuf[16];
    std::vector<VisualNodeId> heapBuf;
    const std::size_t snapshotCount = canvasNodes.size();
    const VisualNodeId* snapshotPtr = stackBuf;
    if (snapshotCount <= 16u) {
        for (std::size_t i = 0u; i < snapshotCount; ++i) {
            stackBuf[i] = canvasNodes[i];
        }
    } else {
        heapBuf.assign(canvasNodes.begin(), canvasNodes.end());
        snapshotPtr = heapBuf.data();
    }
    bool anyDispatched = false;
    for (std::size_t idx = 0u; idx < snapshotCount; ++idx) {
        const VisualNodeId nid = snapshotPtr[idx];
        const VisualNode* node = world_.GetNode(nid);
        if (!node || !node->nativeHandle.valid()) {
            continue;
        }
        const float curW = node->layout.layoutRect.width;
        const float curH = node->layout.layoutRect.height;
        const std::uint64_t key = VisualNodeKey(nid);
        bool sizeChanged = false;
        const auto it = lastCanvasDrawSizes_.find(key);
        if (it == lastCanvasDrawSizes_.end()) {
            sizeChanged = true;
        } else if (std::fabs(it->second.first - curW) > 0.25f || std::fabs(it->second.second - curH) > 0.25f) {
            sizeChanged = true;
        }
        if (node->drawDirty || !drawBuffer_.FindCommands(nid) || sizeChanged) {
            lastCanvasDrawSizes_[key] = {curW, curH};
            const UnifiedValue drawArgs[1]{UnifiedValue{node->nativeHandle}};
            domains_.setActiveDomain(DomainKind::UiPresentation);
            (void)InvokeSymbol(symbols_.dispatchDraw, rs_rt::ValueSpan{drawArgs, 1u}, nullptr, DomainKind::UiPresentation);
            anyDispatched = true;
        }
    }
    return anyDispatched;
}

void AeroAppHost::SetSimTickCallback(SimTickCallback callback) {
    simTickCallback_ = std::move(callback);
}

bool AeroAppHost::BindSimFloatColumn(
    VisualNodeId nodeId,
    VisualAnimProp targetProp,
    const float* columnData,
    std::size_t rowIndex,
    std::size_t columnCount) {
    return world_.BindEcsColumn(
        nodeId,
        static_cast<std::uint32_t>(targetProp),
        columnData,
        rowIndex,
        columnCount);
}

void AeroAppHost::StepFrame(float dtSeconds, bool forceRenderSync) {
    StepFrameInternal(dtSeconds, forceRenderSync, false);
}

void AeroAppHost::StepFrameInternal(float dtSeconds, bool forceRenderSync, bool skipScriptFrameCallbacks) {
    if (!initialized_ || inStepFrame_) {
        return;
    }
    struct StepFrameScopeGuard {
        bool& flag;
        explicit StepFrameScopeGuard(bool& f) noexcept : flag(f) { flag = true; }
        ~StepFrameScopeGuard() noexcept { flag = false; }
    } stepGuard(inStepFrame_);
    const bool trace = IsAeroTraceEnabled();
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] StepFrame: %s\n", msg);
            std::fflush(stderr);
        }
    };
    const float dt = (dtSeconds > 0.0f && std::isfinite(dtSeconds)) ? dtSeconds : 0.0f;

    // 1. Optional Sim domain tick callback & script SimState advance
    bool simUpdated = false;
    if (simTickCallback_) {
        if (!skipScriptFrameCallbacks) {
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            simTickCallback_(dt, *this);
            domains_.setActiveDomain(DomainKind::UiPresentation);
        }
        simUpdated = true;
    }
    if (simObject_.valid() && dt > 0.0f) {
        if (symbols_.simAdvanceTick != 0u) {
            const DomainKind prevDomain = domains_.activeDomain();
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            const UnifiedValue tickArgs[2]{UnifiedValue{simObject_}, UnifiedValue{FloatValue{dt}}};
            if (InvokeSymbol(
                    symbols_.simAdvanceTick,
                    rs_rt::ValueSpan{tickArgs, 2u},
                    nullptr,
                    DomainKind::SimDeterministic)) {
                pendingScriptSync_ = true;
                simUpdated = true;
            }
            domains_.setActiveDomain(prevDomain);
        } else if (InvokeScriptMethod(
                       simObject_,
                       "AdvanceTick",
                       {UnifiedValue{FloatValue{dt}}},
                       nullptr,
                       DomainKind::SimDeterministic)) {
            simUpdated = true;
        }
    }

    // 2. Cross-domain [SimCommand] queue execution in SimDeterministic domain
    simUpdated |= FlushPendingSimCommands(true);

    // 3. Queued pointer / keyboard / text input dispatch
    std::size_t flushedInputCount = 0u;
    if (inputRouter_.HasPendingEvents()) {
        flushedInputCount = inputRouter_.FlushQueuedEvents();
        if (flushedInputCount > 0u) {
            desktopHost_.NotifyDirtyObjects();
            desktopHost_.NotifyDirtyLayout();
        }
    }

    // Flush any [SimCommand]s newly queued by UI input handlers in the same frame
    simUpdated |= FlushPendingSimCommands(true);

    if (simUpdated && rootObject_.valid()) {
        if (symbols_.rootSyncHudFromSim != 0u) {
            const DomainKind prevDomain = domains_.activeDomain();
            domains_.setActiveDomain(DomainKind::UiPresentation);
            const UnifiedValue syncArgs[1]{UnifiedValue{rootObject_}};
            if (InvokeSymbol(
                    symbols_.rootSyncHudFromSim,
                    rs_rt::ValueSpan{syncArgs, 1u},
                    nullptr,
                    DomainKind::UiPresentation)) {
                pendingScriptSync_ = true;
            }
            domains_.setActiveDomain(prevDomain);
        } else {
            (void)InvokeScriptMethod(
                rootObject_, "SyncHudFromSim", {}, nullptr, DomainKind::UiPresentation);
        }
    }

    bool ranStepGame = false;
    if (!skipScriptFrameCallbacks && dt > 0.0f) {
        if (symbols_.stepGame != 0u && rootObject_.valid()) {
            const UnifiedValue stepArgs[2]{UnifiedValue{rootObject_}, UnifiedValue{FloatValue{dt}}};
            ranStepGame = InvokeSymbol(
                symbols_.stepGame,
                rs_rt::ValueSpan{stepArgs, 2u},
                nullptr,
                DomainKind::UiPresentation);
        }
        if (symbols_.bindingStepFrameTicks != 0u) {
            const UnifiedValue tickArgs[1]{UnifiedValue{FloatValue{dt}}};
            (void)InvokeSymbol(
                symbols_.bindingStepFrameTicks,
                rs_rt::ValueSpan{tickArgs, 1u},
                nullptr,
                DomainKind::UiPresentation);
        }
    }

    // 4. Track-B zero-alloc C++ ECS column synchronization (authoritative after script sync)
    const std::size_t trackBCount = world_.FlushTrackB();
    if (trackBCount > 0u) {
        desktopHost_.NotifyDirtyObjects();
    }

    // 5. Declarative BindingEngine synchronization (OneWay / TwoWay / Multi)
    const bool hadScriptWake = scriptWakeRequested_;
    bool ranFlushThisFrame = false;
    const bool observableChanged =
        !hasFlushedBindings_ || (domains_.globalObservableEpoch() != lastFlushedObservableEpoch_);
    const bool needsPreAnimFlush =
        observableChanged ||
        pendingScriptSync_ ||
        hadScriptWake ||
        simUpdated ||
        (flushedInputCount > 0u) ||
        ranStepGame ||
        (trackBCount > 0u) ||
        forceRenderSync;
    if (needsPreAnimFlush) {
        logTrace("FlushBindings");
        FlushBindings();
        ranFlushThisFrame = true;
    }

    // 6. VisualAnimationEngine tick + async UI tasks
    const std::size_t uiTasksStepped = domains_.stepAllUiTasks(interpreter_.get(), aotContext_.get());
    if (animEngine_.HasActiveAnimations()) {
        if (animEngine_.Tick(dt, world_)) {
            desktopHost_.NotifyDirtyLayout();
        }
        if (animEngine_.LastTickNeededScriptSync() ||
            uiTasksStepped > 0u ||
            pendingScriptSync_ ||
            scriptWakeRequested_) {
            logTrace("FlushBindings");
            FlushBindings();
            ranFlushThisFrame = true;
        }
        const bool stillActive = animEngine_.HasActiveAnimations();
        desktopHost_.SetActiveAnimations(stillActive);
        if (!stillActive) {
            pendingScriptSync_ = true;
            if (!ranFlushThisFrame) {
                logTrace("FlushBindings");
                FlushBindings();
                ranFlushThisFrame = true;
            }
            desktopHost_.NotifyDirtyObjects();
            desktopHost_.NotifyDirtyLayout();
        }
    } else {
        desktopHost_.SetActiveAnimations(false);
        if (uiTasksStepped > 0u || pendingScriptSync_ || scriptWakeRequested_) {
            logTrace("FlushBindings");
            FlushBindings();
            ranFlushThisFrame = true;
        }
    }

    // 7. Depth-bucket layout pass (measure/arrange/3D world transforms)
    const bool hasLiveWindow = !config_.headless && windowHost_.IsInitialized() &&
        windowHost_.Mode() != NativeWindowMode::HeadlessSoftware &&
        windowHost_.Width() > 0u && windowHost_.Height() > 0u;
    float vpW = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth));
    float vpH = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight));
    if (hasLiveWindow) {
        const float dpi = std::max(0.25f, windowHost_.DpiScale());
        vpW = static_cast<float>(windowHost_.Width()) / dpi;
        vpH = static_cast<float>(windowHost_.Height()) / dpi;
    }
    const Size targetViewport{vpW, vpH};
    const bool scriptViewportChanged = (lastNotifiedViewport_ != targetViewport);
    const bool viewportChanged = scriptViewportChanged || (world_.LastLayoutViewport() != targetViewport);
    const bool hadLayoutDirtyBefore = world_.HasLayoutOrTransformDirty();
    const bool hadDirtyBeforeLayout = world_.HasAnyDirty();
    if (forceRenderSync || viewportChanged || hadLayoutDirtyBefore) {
        const std::int32_t prevSizeEpoch = world_.LayoutSizeEpoch();
        logTrace("UpdateLayout");
        world_.UpdateLayout(vpW, vpH);
        if (scriptViewportChanged && rootObject_.valid()) {
            lastNotifiedViewport_ = targetViewport;
            (void)InvokeScriptMethod(
                rootObject_,
                "OnHostViewportResized",
                {UnifiedValue{FloatValue{vpW}}, UnifiedValue{FloatValue{vpH}}},
                nullptr);
        } else if (rootObject_.valid()) {
            lastNotifiedViewport_ = targetViewport;
        }
        const bool layoutSizesChanged = (world_.LayoutSizeEpoch() != prevSizeEpoch);
        if ((!ranFlushThisFrame && forceRenderSync) ||
            viewportChanged ||
            layoutSizesChanged ||
            pendingScriptSync_ ||
            scriptWakeRequested_) {
            FlushBindingsPostLayout();
        }
        if (world_.FlushTrackB() > 0u || world_.HasLayoutOrTransformDirty()) {
            const std::int32_t prevSizeEpoch2 = world_.LayoutSizeEpoch();
            world_.UpdateLayout(vpW, vpH);
            if (world_.LayoutSizeEpoch() != prevSizeEpoch2 ||
                pendingScriptSync_ ||
                scriptWakeRequested_) {
                FlushBindingsPostLayout();
            }
            if (world_.HasLayoutOrTransformDirty()) {
                world_.UpdateLayout(vpW, vpH);
            }
        }
    }

    // 8. Retained CanvasDraw (CustomDrawElement) invalidation pass
    logTrace("DispatchPendingCanvasDraws");
    const bool drawUpdated = DispatchPendingCanvasDraws();

    // 9. PagedRenderTree sync + GPU/Software frame presentation
    const bool needSync = forceRenderSync || drawUpdated || viewportChanged || hadDirtyBeforeLayout ||
                          world_.HasAnyDirty() ||
                          (renderTree_.NodeCount() == 0u && world_.Root().IsValid());
    const bool willTickWindowHost = hasLiveWindow &&
        (needSync || forceRenderSync || windowHost_.NeedsInitialPresent());
    if (needSync) {
        if (hasLiveWindow) {
            const float dpi = std::max(0.25f, windowHost_.DpiScale());
            const float treeDpi = (windowHost_.Mode() == NativeWindowMode::Win32GdiSoftware) ? 1.0f : dpi;
            desktopHost_.SetDpiScale(dpi);
            desktopHost_.SetViewportSize(Size{
                static_cast<float>(windowHost_.Width()),
                static_cast<float>(windowHost_.Height())});
            renderTree_.SetRootDpiScale(treeDpi);
        } else {
            desktopHost_.SetDpiScale(1.0f);
            desktopHost_.SetViewportSize(targetViewport);
            renderTree_.SetRootDpiScale(1.0f);
        }
        desktopHost_.NotifyDirtyLayout();
        if (!willTickWindowHost) {
            logTrace("TickUi RenderRecord");
            desktopHost_.TickUi(0.0f, UiPhase_RenderRecord);
        }
    } else {
        desktopHost_.SetStateFlags(animEngine_.HasActiveAnimations(), false, false, false);
    }

    if (willTickWindowHost) {
        logTrace("TickAndPresentFrame");
        (void)windowHost_.TickAndPresentFrame(
            0.0f,
            forceRenderSync || viewportChanged,
            needSync ? UiPhase_RenderRecord : UiPhase_None);
    }
    RunUiHeapGcMaintenance(forceRenderSync ? 20 : 6, true);
    if (programImage_ && programImage_->decodedLazyChunkCacheSize() > 0u) {
        const std::size_t compiledNow = programImage_->compiledLazyFunctionCount();
        if (compiledNow == lastLazyCompiledCount_) {
            ++idleLazyFrames_;
            if (idleLazyFrames_ >= 2u) {
                programImage_->releaseTransientLazyCaches();
                idleLazyFrames_ = 0u;
            }
        } else {
            lastLazyCompiledCount_ = compiledNow;
            idleLazyFrames_ = 0u;
        }
    }
    logTrace("done");
}

void AeroAppHost::Tick(float dtSeconds) {
    StepFrame(dtSeconds, false);
}

bool AeroAppHost::RenderHeadlessRgba(
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>& outRgba) {
    if (width == 0u || height == 0u) {
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }
    if (config_.viewportWidth != width || config_.viewportHeight != height) {
        InjectResize(width, height);
    }
    const bool mustForce = (renderTree_.NodeCount() == 0u) ||
        (windowHost_.IsInitialized() && windowHost_.Telemetry().framesRendered == 0u) ||
        (windowHost_.IsInitialized() &&
         (windowHost_.Width() != width || windowHost_.Height() != height));
    StepFrame(0.0f, mustForce);
    if (windowHost_.IsInitialized() &&
        windowHost_.Mode() == NativeWindowMode::HeadlessSoftware &&
        windowHost_.Width() == width &&
        windowHost_.Height() == height) {
        (void)windowHost_.TickAndPresentFrame(0.0f, true, UiPhase_RenderRecord);
    }

    std::uint32_t capW = 0u;
    std::uint32_t capH = 0u;
    const bool usedCapture =
        windowHost_.IsInitialized() &&
        windowHost_.Width() == width &&
        windowHost_.Height() == height &&
        windowHost_.CaptureFramebufferRgba8(outRgba, capW, capH) &&
        capW == width && capH == height &&
        outRgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;

    if (!usedCapture) {
        ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree_, width, height, outRgba, config_.clearColor);
        ZeroCopyGpuStream::ReleaseTransientPools();
    }
    if (std::getenv("AERO_TRACE") != nullptr && outRgba.size() >= static_cast<std::size_t>(width) * height * 4u) {
        const auto pxAt = [&](std::uint32_t x, std::uint32_t y) {
            const std::size_t o = (static_cast<std::size_t>(y) * width + x) * 4u;
            return std::array<std::uint8_t, 4>{outRgba[o], outRgba[o + 1u], outRgba[o + 2u], outRgba[o + 3u]};
        };
        const auto p0 = pxAt(0u, 0u);
        const auto pm = pxAt(width / 2u, height / 2u);
        const auto pb = pxAt(0u, height - 1u);
        const auto* g1 = resourceStore_ ? resourceStore_->GradientBrushes().Find(1u) : nullptr;
        std::fprintf(
            stderr,
            "[AERO_TRACE] RenderHeadlessRgba(%ux%u, usedCap=%d): nodes=%zu treeStore==hostStore=%d gradCount=%zu g1=(kind=%d mm=%d s=(%.2f,%.2f)->(%.2f,%.2f) stops=%zu) px(0,0)=#%02X%02X%02X%02X px(mid)=#%02X%02X%02X%02X px(bot)=#%02X%02X%02X%02X\n",
            width,
            height,
            static_cast<int>(usedCapture),
            renderTree_.NodeCount(),
            static_cast<int>(renderTree_.ResourceStore() == resourceStore_.get()),
            resourceStore_ ? resourceStore_->GradientBrushes().BrushCount() : 0u,
            g1 ? static_cast<int>(g1->kind) : -1,
            g1 ? static_cast<int>(g1->mappingMode) : -1,
            g1 ? g1->startPoint.x : -1.0f,
            g1 ? g1->startPoint.y : -1.0f,
            g1 ? g1->endPoint.x : -1.0f,
            g1 ? g1->endPoint.y : -1.0f,
            g1 ? g1->stops.size() : 0u,
            p0[0], p0[1], p0[2], p0[3],
            pm[0], pm[1], pm[2], pm[3],
            pb[0], pb[1], pb[2], pb[3]);
        if (programImage_) {
            const SymbolId b1 = FindBytecodeFunctionSymbol(*programImage_, "Shape.BuildVectorGeometrySpec");
            const SymbolId b2 = FindBytecodeFunctionSymbol(*programImage_, "Path.BuildVectorGeometrySpec");
            std::fprintf(
                stderr,
                "[AERO_TRACE] Shape.BuildVectorGeometrySpec=%llu Path.BuildVectorGeometrySpec=%llu\n",
                static_cast<unsigned long long>(b1),
                static_cast<unsigned long long>(b2));
        }
        std::fflush(stderr);
    }
    return outRgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
}

bool AeroAppHost::SaveScreenshotPpm(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height) {
    const bool hasLiveWindow = !config_.headless && windowHost_.IsInitialized() &&
        windowHost_.Mode() != NativeWindowMode::HeadlessSoftware &&
        windowHost_.Width() > 0u && windowHost_.Height() > 0u;
    std::vector<std::uint8_t> rgba;
    if (width == 0u && height == 0u && hasLiveWindow) {
        std::uint32_t capW = 0u;
        std::uint32_t capH = 0u;
        if (windowHost_.CaptureFramebufferRgba8(rgba, capW, capH) &&
            capW > 0u && capH > 0u &&
            rgba.size() == static_cast<std::size_t>(capW) * static_cast<std::size_t>(capH) * 4u) {
            return WritePpmP6(filePath, capW, capH, rgba.data());
        }
        if (RenderHeadlessRgba(windowHost_.Width(), windowHost_.Height(), rgba)) {
            return WritePpmP6(filePath, windowHost_.Width(), windowHost_.Height(), rgba.data());
        }
        return false;
    }
    const std::uint32_t targetW = width > 0u ? width : std::max<std::uint32_t>(1u, config_.viewportWidth);
    const std::uint32_t targetH = height > 0u ? height : std::max<std::uint32_t>(1u, config_.viewportHeight);
    const Size targetSize{static_cast<float>(targetW), static_cast<float>(targetH)};
    if (hasLiveWindow &&
        world_.Root().IsValid() &&
        (windowHost_.Width() != targetW || windowHost_.Height() != targetH)) {
        const Size prevLayoutVp = world_.LastLayoutViewport();
        if (prevLayoutVp != targetSize || world_.HasAnyDirty()) {
            world_.UpdateLayout(targetSize.width, targetSize.height);
        }
        PagedRenderTree tempTree;
        VisualRenderAdapter adapter;
        (void)adapter.BuildPagedRenderTree(world_, &drawBuffer_, tempTree, resourceStore_.get());
        ZeroCopyGpuStream::RasterizeTreeToRgba8(tempTree, targetW, targetH, rgba, config_.clearColor);
        if (prevLayoutVp.width > 0.0f && prevLayoutVp.height > 0.0f && prevLayoutVp != targetSize) {
            world_.UpdateLayout(prevLayoutVp.width, prevLayoutVp.height);
        }
        return WritePpmP6(filePath, targetW, targetH, rgba.data());
    }
    if (!RenderHeadlessRgba(targetW, targetH, rgba)) {
        return false;
    }
    return WritePpmP6(filePath, targetW, targetH, rgba.data());
}

bool AeroAppHost::RegisterExternalViewportTexture(
    std::string_view uriKey,
    std::uint32_t externalGpuHandle,
    std::uint32_t width,
    std::uint32_t height,
    bool flipY,
    std::vector<std::uint8_t> fallbackRgbaPixels) {
    UIResourceStore& store = resourceStore_ ? *resourceStore_ : UIResourceStore::Default();
    const ImageAtlasEntry* entry = store.Images().RegisterExternalGpuTexture(
        uriKey,
        externalGpuHandle,
        width,
        height,
        flipY,
        std::move(fallbackRgbaPixels));
    if (entry != nullptr) {
        if (world_.Root().IsValid()) {
            world_.MarkDirty(world_.Root(), VisualDirty_Visual);
        }
        return true;
    }
    return false;
}

bool AeroAppHost::UnregisterExternalViewportTexture(std::string_view uriKey) noexcept {
    UIResourceStore& store = resourceStore_ ? *resourceStore_ : UIResourceStore::Default();
    return store.Images().UnregisterExternalGpuTexture(uriKey);
}

void AeroAppHost::InjectPointerMove(float x, float y, std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseMove(x, y, modifiers);
    } else {
        (void)inputRouter_.QueuePointerMove(Point{x, y}, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectPointerButton(
    float x,
    float y,
    PointerButton button,
    bool isDown,
    std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseButton(x, y, button, isDown, modifiers);
    } else {
        (void)inputRouter_.QueuePointerButton(Point{x, y}, button, isDown, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectClick(float x, float y, PointerButton button, std::uint8_t modifiers) {
    InjectPointerMove(x, y, modifiers);
    InjectPointerButton(x, y, button, true, modifiers);
    InjectPointerButton(x, y, button, false, modifiers);
}

void AeroAppHost::InjectMouseWheel(
    float x,
    float y,
    float deltaX,
    float deltaY,
    std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseWheel(x, y, deltaX, deltaY, modifiers);
    } else {
        (void)inputRouter_.QueueMouseWheel(Point{x, y}, deltaX, deltaY, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectKey(KeyCode key, bool isDown, std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformKey(key, isDown, modifiers);
    } else {
        (void)inputRouter_.QueueKey(key, isDown, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectTextInput(std::string_view utf8Text) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformTextInput(utf8Text);
    } else {
        (void)inputRouter_.QueueTextInput(utf8Text);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectResize(std::uint32_t width, std::uint32_t height) {
    if (width == 0u || height == 0u) {
        return;
    }
    config_.viewportWidth = width;
    config_.viewportHeight = height;
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformResize(width, height);
    } else {
        desktopHost_.SetViewportSize(Size{static_cast<float>(width), static_cast<float>(height)});
        desktopHost_.NotifyDirtyLayout();
    }
}

std::uint64_t AeroAppHost::RunWindowedLoop(std::uint64_t maxFrames) {
    if (!initialized_ && !InitializeRuntime()) {
        return 0u;
    }
    const Size expectedVp{
        static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth)),
        static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight))};
    if (!world_.Root().IsValid() || renderTree_.NodeCount() == 0u ||
        world_.HasAnyDirty() || world_.LastLayoutViewport() != expectedVp ||
        windowHost_.Telemetry().framesRendered == 0u) {
        StepFrame(0.0f, true);
    }

    std::uint64_t frames = 0u;
    bool hasLastActiveTick = false;
    auto lastActiveTick = std::chrono::steady_clock::now();
    bool hasLastFrameTick = false;
    auto lastFrameTick = std::chrono::steady_clock::now();
    while (!closeRequested_ && !windowHost_.ShouldClose()) {
        if (maxFrames > 0u && frames >= maxFrames) {
            break;
        }
        const auto iterStart = std::chrono::steady_clock::now();
        const bool wakeBeforePump = scriptWakeRequested_;
        const bool renderWakeBeforePump = renderWakeRequested_;
        scriptWakeRequested_ = false;
        renderWakeRequested_ = false;
        bool scheduledWakeDue = false;
        std::int32_t scheduledWaitMs = -1;
        if (scheduledWakeAt_.has_value()) {
            if (*scheduledWakeAt_ <= iterStart) {
                scheduledWakeAt_.reset();
                scheduledWakeDue = true;
            } else {
                const auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(*scheduledWakeAt_ - iterStart).count();
                scheduledWaitMs = static_cast<std::int32_t>(std::clamp<long long>(remMs, 1, 60000));
            }
        }
        const bool idleAllowed =
            desktopHost_.CanSleepIdle() && !static_cast<bool>(simTickCallback_) &&
            !wakeBeforePump && !renderWakeBeforePump &&
            !scheduledWakeDue && !animEngine_.HasActiveAnimations();
        if (idleAllowed) {
            hasLastActiveTick = false;
            if (!scheduledWakeAt_.has_value()) {
                hasLastFrameTick = false;
            }
        }
        const std::int32_t waitMs = idleAllowed
            ? (maxFrames > 0u ? 0 : scheduledWaitMs)
            : 0;
        const std::uint64_t prevOsEvents = windowHost_.Telemetry().osEventsTranslated;
        if (!windowHost_.PumpPlatformEvents(waitMs)) {
            break;
        }

        const auto afterPumpNow = std::chrono::steady_clock::now();
        if (scheduledWakeAt_.has_value() && afterPumpNow >= *scheduledWakeAt_) {
            scheduledWakeAt_.reset();
            scheduledWakeDue = true;
        }

        const std::uint32_t w = std::max<std::uint32_t>(1u, windowHost_.LogicalWidth());
        const std::uint32_t h = std::max<std::uint32_t>(1u, windowHost_.LogicalHeight());
        config_.viewportWidth = w;
        config_.viewportHeight = h;

        const bool hasLiveWin = !config_.headless && windowHost_.IsInitialized() &&
            windowHost_.Mode() != NativeWindowMode::HeadlessSoftware &&
            windowHost_.Width() > 0u && windowHost_.Height() > 0u;
        float liveVpW = static_cast<float>(w);
        float liveVpH = static_cast<float>(h);
        if (hasLiveWin) {
            const float dpi = std::max(0.25f, windowHost_.DpiScale());
            liveVpW = static_cast<float>(windowHost_.Width()) / dpi;
            liveVpH = static_cast<float>(windowHost_.Height()) / dpi;
        }
        const Size liveTargetVp{liveVpW, liveVpH};
        const bool viewportChanged =
            (lastNotifiedViewport_ != liveTargetVp) || (world_.LastLayoutViewport() != liveTargetVp);
        const bool osEventsArrived = (windowHost_.Telemetry().osEventsTranslated != prevOsEvents);

        bool followUpDirty = viewportChanged || osEventsArrived || windowHost_.NeedsPresent();
        if (simTickCallback_) {
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            simTickCallback_(0.016f, *this);
            domains_.setActiveDomain(DomainKind::UiPresentation);
            followUpDirty = true;
        }
        float activeStepDt = 0.016f;
        if (!idleAllowed || scheduledWakeDue) {
            const auto now = std::chrono::steady_clock::now();
            if (!idleAllowed) {
                if (hasLastActiveTick) {
                    activeStepDt = std::clamp(
                        std::chrono::duration<float>(now - lastActiveTick).count(),
                        0.001f,
                        0.05f);
                }
                lastActiveTick = now;
                hasLastActiveTick = true;
                if (symbols_.stepGame != 0u && rootObject_.valid()) {
                    const UnifiedValue stepArgs[2]{
                        UnifiedValue{rootObject_},
                        UnifiedValue{FloatValue{activeStepDt}},
                    };
                    if (InvokeSymbol(
                            symbols_.stepGame,
                            rs_rt::ValueSpan{stepArgs, 2u},
                            nullptr,
                            DomainKind::UiPresentation)) {
                        pendingScriptSync_ = true;
                    }
                }
            }
            float timerDt = 0.016f;
            if (hasLastFrameTick) {
                timerDt = std::clamp(
                    std::chrono::duration<float>(now - lastFrameTick).count(),
                    0.001f,
                    2.0f);
            }
            lastFrameTick = now;
            hasLastFrameTick = true;
            if (symbols_.bindingStepFrameTicks != 0u) {
                const UnifiedValue tickArgs[1]{UnifiedValue{FloatValue{timerDt}}};
                (void)InvokeSymbol(
                    symbols_.bindingStepFrameTicks,
                    rs_rt::ValueSpan{tickArgs, 1u},
                    nullptr,
                    DomainKind::UiPresentation);
            }
            if (scriptWakeRequested_ || world_.HasAnyDirty()) {
                followUpDirty = true;
            }
        } else if (!hasLastFrameTick && scheduledWakeAt_.has_value()) {
            lastFrameTick = afterPumpNow;
            hasLastFrameTick = true;
        }
        if (FlushPendingSimCommands(false)) {
            followUpDirty = true;
        }
        if (world_.FlushTrackB() > 0u) {
            followUpDirty = true;
        }
        const bool hadActiveBeforePump = !idleAllowed;
        if (followUpDirty ||
            hadActiveBeforePump ||
            wakeBeforePump ||
            renderWakeBeforePump ||
            scheduledWakeDue ||
            scriptWakeRequested_ ||
            renderWakeRequested_ ||
            world_.HasAnyDirty() ||
            animEngine_.HasActiveAnimations()) {
            if (wakeBeforePump || scheduledWakeDue || scriptWakeRequested_ || osEventsArrived) {
                pendingScriptSync_ = true;
            }
            const float stepDt = (!idleAllowed) ? activeStepDt : 0.0f;
            StepFrameInternal(stepDt, viewportChanged || windowHost_.NeedsInitialPresent(), true);
        } else if (DispatchPendingCanvasDraws()) {
            StepFrameInternal(0.0f, true, true);
        } else if (!idleAllowed) {
            RunUiHeapGcMaintenance(4, false);
        }
        if (!idleAllowed && maxFrames == 0u && !windowHost_.ShouldClose()) {
            const auto elapsed = std::chrono::steady_clock::now() - iterStart;
            if (elapsed < std::chrono::milliseconds(16)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16) - elapsed);
            }
        }
        ++frames;
    }
    return frames;
}

bool AeroAppHost::FlushPendingSimCommands(bool notifyDirtyObjects) {
    if (domains_.pendingSimCommandCount() == 0u) {
        return false;
    }
    domains_.setActiveDomain(DomainKind::SimDeterministic);
    std::size_t ran = 0u;
    if (programImage_) {
        if (Interpreter* simInterp = EnsureSimInterpreter()) {
            ran = domains_.flushSimCommands(*simInterp);
        }
    } else if (simAotContext_) {
        ran = domains_.flushSimCommands(*simAotContext_);
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);
    if (notifyDirtyObjects) {
        if (ran > 0u) {
            desktopHost_.NotifyDirtyObjects();
            return true;
        }
        return false;
    }
    return true;
}

void AeroAppHost::RunUiHeapGcMaintenance(std::int32_t idleBudgetMs, bool sweepDeadObjectBoxes) {
    if (domains_.uiHeap().phase() == rs_rt::GcPhase::Idle &&
        domains_.uiHeap().nurserySize() >= 4096u) {
        rs_rt::ShadowStack emptyStack;
        (void)domains_.uiHeap().collectYoung(emptyStack, 64u);
        if (sweepDeadObjectBoxes) {
            (void)world_.SweepDeadObjectBoxes();
        }
    }
    const std::uint64_t destroyedNow = world_.DestroyedNodeCount();
    const bool majorTeardown = sweepDeadObjectBoxes &&
                               (destroyedNow >= lastGcDestroyedNodes_) &&
                               ((destroyedNow - lastGcDestroyedNodes_) >= 64u);
    if (domains_.uiHeap().collectionRequested() ||
        domains_.uiHeap().phase() != rs_rt::GcPhase::Idle ||
        majorTeardown) {
        const std::int32_t effBudgetMs = (majorTeardown && idleBudgetMs < 16) ? 16 : idleBudgetMs;
        const auto gcStats = domains_.uiHeap().collectIncrementalWithBudget(
            rs_rt::RootSet{},
            std::chrono::milliseconds(effBudgetMs),
            32u,
            majorTeardown);
        if (gcStats.cycleCompleted) {
            lastGcDestroyedNodes_ = world_.DestroyedNodeCount();
            if (sweepDeadObjectBoxes) {
                const std::size_t sweptBoxes = world_.SweepDeadObjectBoxes();
                if (sweptBoxes > 0u && majorTeardown) {
                    (void)domains_.uiHeap().collectIncrementalWithBudget(
                        rs_rt::RootSet{},
                        std::chrono::milliseconds(8),
                        32u,
                        true);
                    (void)world_.SweepDeadObjectBoxes();
                }
            }
            (void)domains_.uiHeap().trimExcessCapacity();
#ifdef _WIN32
            if (majorTeardown) {
                (void)_heapmin();
            }
#endif
        }
    }
}

void AeroAppHost::Shutdown() noexcept {
    if (windowHost_.IsInitialized()) {
        windowHost_.Shutdown();
    }
    ResetLoadedProgramState();
    initialized_ = false;
}

ObjectRef AeroAppHost::RootDataContext() const noexcept {
    if (!rootObject_.valid()) {
        return ObjectRef{};
    }
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    UnifiedValue dcVal{};
    if (ReadSingleMember(backend, rootObject_, "DataContext", dcVal)) {
        if (const auto* dcRef = std::get_if<ObjectRef>(&dcVal); dcRef && dcRef->valid()) {
            return *dcRef;
        }
    }
    return ObjectRef{};
}

bool AeroAppHost::ReadScriptMember(ObjectRef obj, std::string_view dotPath, UnifiedValue& outValue) const {
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    return ReadDotPathValue(backend, obj, dotPath, outValue);
}

bool AeroAppHost::InvokeScriptFunction(
    std::string_view qualifiedOrShortName,
    const std::vector<UnifiedValue>& args,
    UnifiedValue* outResult,
    DomainKind domain) {
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    const SymbolId fnId = FindBackendFunctionSymbol(backend, qualifiedOrShortName);
    if (fnId == 0u) {
        return false;
    }
    const DomainKind prevDomain = domains_.activeDomain();
    domains_.setActiveDomain(domain);
    const bool ok = InvokeSymbol(
        fnId,
        rs_rt::ValueSpan{args.data(), args.size()},
        outResult,
        domain);
    if (ok) {
        pendingScriptSync_ = true;
    }
    domains_.setActiveDomain(prevDomain);
    return ok;
}

bool AeroAppHost::InvokeScriptMethod(
    ObjectRef targetObj,
    std::string_view methodName,
    const std::vector<UnifiedValue>& args,
    UnifiedValue* outResult,
    DomainKind domain) {
    if (!targetObj.valid() || methodName.empty()) {
        return false;
    }
    ManagedHeap* heap = domains_.heapForObject(targetObj);
    if (!heap || !heap->isAlive(targetObj)) {
        return false;
    }
    const SymbolId typeId = heap->objectTypeIdFast(targetObj);
    if (typeId == 0u) {
        return false;
    }
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());
    const SymbolId fnId = FindBackendClassMethod(backend, typeId, methodName, args.size());
    if (fnId == 0u) {
        return false;
    }
    std::vector<UnifiedValue> fullArgs;
    fullArgs.reserve(args.size() + 1u);
    fullArgs.push_back(UnifiedValue{targetObj});
    for (const UnifiedValue& arg : args) {
        if (const auto* ref = std::get_if<ObjectRef>(&arg); ref && !ref->valid()) {
            fullArgs.push_back(UnifiedValue{rs_rt::NullObject{}});
        } else {
            fullArgs.push_back(arg);
        }
    }

    const DomainKind prevDomain = domains_.activeDomain();
    domains_.setActiveDomain(domain);
    const bool ok = InvokeSymbol(
        fnId,
        rs_rt::ValueSpan{fullArgs.data(), fullArgs.size()},
        outResult,
        domain);
    if (ok) {
        pendingScriptSync_ = true;
    }
    domains_.setActiveDomain(prevDomain);
    return ok;
}

VisualNodeId AeroAppHost::FindVisualByOwnerName(std::string_view elementName) const noexcept {
    if (elementName.empty()) {
        return VisualNodeId{};
    }
    const ScriptBackendView backend = MakeBackendView(
        domains_, programImage_.get(), interpreter_.get(), aotDescriptor_, aotContext_.get());

    auto matchesName = [&](const VisualNode* node) -> bool {
        if (!node || !node->hasScriptOwner || !node->scriptOwner.valid()) {
            return false;
        }
        UnifiedValue nameVal{};
        if (!ReadSingleMember(backend, node->scriptOwner, "Name", nameVal)) {
            return false;
        }
        std::string nameStr;
        return TryExtractStringValue(backend, nameVal, nameStr) && nameStr == elementName;
    };

    const VisualNodeId rootId = world_.Root();
    if (rootId.IsValid()) {
        std::vector<VisualNodeId> queue{rootId};
        for (std::size_t qi = 0u; qi < queue.size(); ++qi) {
            const VisualNode* node = world_.GetNode(queue[qi]);
            if (!node) {
                continue;
            }
            if (matchesName(node)) {
                return node->id;
            }
            for (const VisualNodeId childId : node->children) {
                queue.push_back(childId);
            }
        }
    }

    const std::size_t slots = world_.ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        const VisualNode* node = world_.NodeAtSlot(s);
        if (!node || !node->hasScriptOwner || !node->scriptOwner.valid()) {
            continue;
        }
        UnifiedValue disposedVal{};
        bool isDisposed = false;
        if (ReadSingleMember(backend, node->scriptOwner, "_isDisposedTree", disposedVal) &&
            disposedVal.tryGetBool(isDisposed) && isDisposed) {
            continue;
        }
        if (matchesName(node)) {
            return node->id;
        }
    }
    return VisualNodeId{};
}

AeroStencilJitStats AeroAppHost::StencilJitStats() const noexcept {
    AeroStencilJitStats stats{};
    stats.invokeHits = domains_.desktopJitInvokeHits();
    stats.uiInvokeHits = domains_.desktopJitInvokeHits(DomainKind::UiPresentation);
    stats.simInvokeHits = domains_.desktopJitInvokeHits(DomainKind::SimDeterministic);
    stats.fallbackHits = domains_.desktopJitFallbackHits();
#ifdef AERO_HAS_STENCIL_JIT
    if (uiStencilModule_) {
        const auto mod = std::static_pointer_cast<const realscript::jit::StencilModule>(uiStencilModule_);
        if (mod && mod->valid()) {
            stats.active = true;
            stats.nativeBackendActive = mod->nativeBackendActive();
            stats.trackedFunctions += mod->totalFunctionCount();
            stats.nativeFunctions += mod->nativeFunctionCount();
            stats.threadedFunctions += mod->threadedFunctionCount();
            stats.fallbackFunctions += mod->fallbackFunctionCount();
            stats.machineCodeBytes += mod->machineCodeBytes();
        }
    }
    if (simStencilModule_) {
        const auto simMod = std::static_pointer_cast<const realscript::jit::StencilModule>(simStencilModule_);
        if (simMod && simMod->valid()) {
            stats.active = true;
            stats.simActive = true;
            stats.nativeBackendActive = stats.nativeBackendActive || simMod->nativeBackendActive();
            stats.simTrackedFunctions = simMod->totalFunctionCount();
            stats.simNativeFunctions = simMod->nativeFunctionCount();
            stats.simMachineCodeBytes = simMod->machineCodeBytes();
            stats.trackedFunctions += simMod->totalFunctionCount();
            stats.nativeFunctions += simMod->nativeFunctionCount();
            stats.threadedFunctions += simMod->threadedFunctionCount();
            stats.fallbackFunctions += simMod->fallbackFunctionCount();
            stats.machineCodeBytes += simMod->machineCodeBytes();
        }
    }
#endif
    return stats;
}

} // namespace aero
