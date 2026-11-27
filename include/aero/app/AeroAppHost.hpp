#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualRenderAdapter.hpp"
#include "aero/visual/VisualTree.hpp"

#ifdef AERO_HAS_COMPILER_V3
#include "aero/compiler/AeroCompilerV3.hpp"
#endif

namespace realscript::aot {
struct TypeDescriptor;
struct FunctionDescriptor;
} // namespace realscript::aot

namespace aero {

struct AeroAppHostConfig {
    std::uint32_t viewportWidth = 1280u;
    std::uint32_t viewportHeight = 800u;
    bool headless = true;
    bool preferSoftwareRasterizer = false;
    bool enableHighDpi = false;
    float dpiScale = 0.0f;
    bool optimizeBytecode = false;
    bool enableStencilJit = false;
    std::uint32_t stencilJitHotnessThreshold = 32u;
    std::string windowTitle = "AeroGUI 3.0";
    std::string entryClassName;
    std::string entryFunctionName;
    std::vector<std::string> assetSearchDirs;
    Color clearColor{0.059f, 0.090f, 0.165f, 1.0f};
};

struct PlayedSoundRecord {
    std::string uri;
    std::string resolvedPath;
    float volume = 1.0f;
};

struct AeroStencilJitStats {
    bool active = false;
    bool nativeBackendActive = false;
    std::size_t trackedFunctions = 0;
    std::size_t nativeFunctions = 0;
    std::size_t threadedFunctions = 0;
    std::size_t fallbackFunctions = 0;
    std::size_t machineCodeBytes = 0;
    std::uint64_t invokeHits = 0;
    std::uint64_t fallbackHits = 0;
    bool simActive = false;
    std::size_t simTrackedFunctions = 0;
    std::size_t simNativeFunctions = 0;
    std::size_t simMachineCodeBytes = 0;
    std::uint64_t uiInvokeHits = 0;
    std::uint64_t simInvokeHits = 0;
};

#ifdef AERO_HAS_COMPILER_V3
/// Discovers a sample directory's entry XAML, bundled auxiliary XAMLs, script sources,
/// and resource/asset search paths into compile and host configurations.
[[nodiscard]] bool DiscoverSampleProject(
    const std::filesystem::path& sampleDir,
    AeroV3CompileInput& outCompileInput,
    AeroAppHostConfig& outHostConfig);

/// Discovers catalog XAML files (Data/Samples/*.xaml, Theme/NoesisTheme.Brushes.*.xaml,
/// Localization/Data/Language-*.xaml) for build-time CompiledXamlCatalog emission.
void DiscoverCatalogXamlSources(
    const std::filesystem::path& sampleDir,
    std::vector<CatalogXamlEntry>& outCatalogEntries);
#endif

class AeroAppHost final {
public:
    using SimTickCallback = std::function<void(float dtSeconds, AeroAppHost& host)>;

    explicit AeroAppHost(const AeroAppHostConfig& config = AeroAppHostConfig{});
    ~AeroAppHost();

    AeroAppHost(const AeroAppHost&) = delete;
    AeroAppHost& operator=(const AeroAppHost&) = delete;
    AeroAppHost(AeroAppHost&&) = delete;
    AeroAppHost& operator=(AeroAppHost&&) = delete;

    [[nodiscard]] bool InitializeRuntime();
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

    // Execution backend 1: Bytecode ProgramImage / precompiled .arbc bundle
    [[nodiscard]] bool LoadProgramImage(
        std::shared_ptr<const ProgramImage> image,
        std::string_view entryClassOrFn = {});
    [[nodiscard]] bool LoadProgramImageWithPreferredEntry(
        std::shared_ptr<const ProgramImage> image,
        std::string_view entryClassOrFn,
        std::string_view preferredEntryClass);
    [[nodiscard]] bool LoadProgramImageBytes(
        const std::uint8_t* data,
        std::size_t size,
        std::string_view entryClassOrFn = {},
        std::string_view preferredEntryClass = {});
    [[nodiscard]] bool LoadProgramImageFile(
        const std::filesystem::path& filePath,
        std::string_view entryClassOrFn = {},
        std::string_view preferredEntryClass = {});

    void RegisterAssetDirectory(std::string_view dir);
    void DiscoverAssetDirectories(const std::filesystem::path& sampleRoot);
    void SetDevCompileResultOpaque(std::shared_ptr<void> result) noexcept;
    [[nodiscard]] const void* DevCompileResultOpaque() const noexcept;
    void SetLastErrorMessage(std::string message);

#ifdef AERO_HAS_COMPILER_V3
    [[nodiscard]] bool CompileAndLoad(
        const AeroV3CompileInput& input,
        std::string* outError = nullptr);
    [[nodiscard]] const AeroV3CompileResult* LastCompileResult() const noexcept;
#endif

    // Execution backend 2: C++17 AOT ProgramDescriptor (zero Interpreter)
    [[nodiscard]] bool LoadAotProgram(
        const rs_aot::ProgramDescriptor* descriptor,
        std::string_view entryFunctionOrClass = {});

    // Sim / Track-B ECS integration
    void SetSimTickCallback(SimTickCallback callback);
    bool BindSimFloatColumn(
        VisualNodeId nodeId,
        VisualAnimProp targetProp,
        const float* columnData,
        std::size_t rowIndex,
        std::size_t columnCount = 1u);

    // Frame pipeline & headless capture
    void StepFrame(float dtSeconds = 0.016f, bool forceRenderSync = false);
    void Tick(float dtSeconds);

    [[nodiscard]] bool RenderHeadlessRgba(
        std::uint32_t width,
        std::uint32_t height,
        std::vector<std::uint8_t>& outRgba);

    [[nodiscard]] bool SaveScreenshotPpm(
        std::string_view filePath,
        std::uint32_t width = 0u,
        std::uint32_t height = 0u);

    // Zero-copy external GPU viewport texture registration (e.g. "viewport://scene")
    bool RegisterExternalViewportTexture(
        std::string_view uriKey,
        std::uint32_t externalGpuHandle,
        std::uint32_t width,
        std::uint32_t height,
        bool flipY = true,
        std::vector<std::uint8_t> fallbackRgbaPixels = {});
    bool UnregisterExternalViewportTexture(std::string_view uriKey) noexcept;

    // Unified input injection helpers
    void InjectPointerMove(float x, float y, std::uint8_t modifiers = KeyMod_None);
    void InjectPointerButton(
        float x,
        float y,
        PointerButton button,
        bool isDown,
        std::uint8_t modifiers = KeyMod_None);
    void InjectClick(
        float x,
        float y,
        PointerButton button = PointerButton::Left,
        std::uint8_t modifiers = KeyMod_None);
    void InjectMouseWheel(
        float x,
        float y,
        float deltaX,
        float deltaY,
        std::uint8_t modifiers = KeyMod_None);
    void InjectKey(KeyCode key, bool isDown, std::uint8_t modifiers = KeyMod_None);
    void InjectTextInput(std::string_view utf8Text);
    void InjectResize(std::uint32_t width, std::uint32_t height);

    std::uint64_t RunWindowedLoop(std::uint64_t maxFrames = 0u);
    void Shutdown() noexcept;

    // Diagnostics & Subsystem Accessors
    [[nodiscard]] const AeroAppHostConfig& Config() const noexcept { return config_; }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }
    [[nodiscard]] std::size_t PlayedSoundCount() const noexcept { return playedSounds_.size(); }
    [[nodiscard]] const std::vector<PlayedSoundRecord>& PlayedSounds() const noexcept { return playedSounds_; }
    void ClearPlayedSounds() noexcept { playedSounds_.clear(); }
    [[nodiscard]] bool IsAotMode() const noexcept { return aotDescriptor_ != nullptr && aotContext_ != nullptr; }
    [[nodiscard]] bool IsBytecodeMode() const noexcept { return programImage_ != nullptr && interpreter_ != nullptr; }
    [[nodiscard]] bool IsCloseRequested() const noexcept { return closeRequested_ || windowHost_.ShouldClose(); }

    [[nodiscard]] VisualWorld& World() noexcept { return world_; }
    [[nodiscard]] const VisualWorld& World() const noexcept { return world_; }
    [[nodiscard]] DrawingCommandBuffer& DrawBuffer() noexcept { return drawBuffer_; }
    [[nodiscard]] const DrawingCommandBuffer& DrawBuffer() const noexcept { return drawBuffer_; }
    [[nodiscard]] VisualAnimationEngine& AnimEngine() noexcept { return animEngine_; }
    [[nodiscard]] VisualInputRouter& InputRouter() noexcept { return inputRouter_; }
    [[nodiscard]] const VisualInputRouter& InputRouter() const noexcept { return inputRouter_; }
    [[nodiscard]] PagedRenderTree& RenderTree() noexcept { return renderTree_; }
    [[nodiscard]] const PagedRenderTree& RenderTree() const noexcept { return renderTree_; }
    [[nodiscard]] const ZeroCopyGpuStream& GpuStream() const noexcept { return gpuStream_; }
    [[nodiscard]] DesktopEventHost& DesktopHost() noexcept { return desktopHost_; }
    [[nodiscard]] const DesktopEventHost& DesktopHost() const noexcept { return desktopHost_; }
    [[nodiscard]] NativeWindowHost& WindowHost() noexcept { return windowHost_; }
    [[nodiscard]] ExecutionDomainContext& Domains() noexcept { return domains_; }
    [[nodiscard]] const ExecutionDomainContext& Domains() const noexcept { return domains_; }
    [[nodiscard]] std::shared_ptr<UIResourceStore> ResourceStore() const noexcept { return resourceStore_; }
    [[nodiscard]] std::shared_ptr<const ProgramImage> Program() const noexcept { return programImage_; }
    [[nodiscard]] Interpreter* ScriptInterpreter() noexcept { return interpreter_.get(); }
    [[nodiscard]] Interpreter* SimInterpreter();
    [[nodiscard]] AeroStencilJitStats StencilJitStats() const noexcept;
    [[nodiscard]] rs_aot::ExecutionContext* AotContext() noexcept { return aotContext_.get(); }
    [[nodiscard]] rs_aot::ExecutionContext* SimAotContext() noexcept { return simAotContext_.get(); }

    // Mounted object & script reflection helpers
    [[nodiscard]] ObjectRef RootScriptObject() const noexcept { return rootObject_; }
    [[nodiscard]] ObjectRef SimScriptObject() const noexcept { return simObject_; }
    [[nodiscard]] VisualNodeId RootVisualNode() const noexcept { return world_.Root(); }
    [[nodiscard]] ObjectRef RootDataContext() const noexcept;
    [[nodiscard]] bool ReadScriptMember(ObjectRef obj, std::string_view dotPath, UnifiedValue& outValue) const;
    [[nodiscard]] bool InvokeScriptFunction(
        std::string_view qualifiedOrShortName,
        const std::vector<UnifiedValue>& args = {},
        UnifiedValue* outResult = nullptr,
        DomainKind domain = DomainKind::UiPresentation);
    [[nodiscard]] bool InvokeScriptMethod(
        ObjectRef targetObj,
        std::string_view methodName,
        const std::vector<UnifiedValue>& args = {},
        UnifiedValue* outResult = nullptr,
        DomainKind domain = DomainKind::UiPresentation);
    [[nodiscard]] VisualNodeId FindVisualByOwnerName(std::string_view elementName) const noexcept;

private:
    struct CachedDispatchSymbols {
        SymbolId dispatchPointer = 0u;
        SymbolId dispatchKey = 0u;
        SymbolId dispatchText = 0u;
        SymbolId dispatchFocus = 0u;
        SymbolId dispatchCustomMeasure = 0u;
        SymbolId dispatchDraw = 0u;
        SymbolId bindingFlush = 0u;
        SymbolId bindingFlushPostLayout = 0u;
        SymbolId bindingStepFrameTicks = 0u;
        SymbolId stepGame = 0u;
        SymbolId simAdvanceTick = 0u;
        SymbolId rootSyncHudFromSim = 0u;
        SymbolId windowShow = 0u;
        SymbolId applyTemplate = 0u;
        SymbolId controlApplyTemplate = 0u;
        SymbolId applyTemplateSubtree = 0u;
        SymbolId ensureLoadedSubtree = 0u;
        SymbolId popupSetOverlayHost = 0u;
        SymbolId registerWorkbenchFactories = 0u;
        SymbolId registerCompiledXamlCatalog = 0u;
    };

    void RegisterAssetDirectoryInternal(std::string_view dir);
    [[nodiscard]] std::string ResolveSoundAssetPath(std::string_view uri) const;
    [[nodiscard]] bool LoadProgramImageInternal(
        std::shared_ptr<const ProgramImage> image,
        std::string_view entryClassOrFn,
        std::string_view preferredEntryClass);
    void ResetLoadedProgramState() noexcept;
    void WireKernelBridges();
    void ResolveDispatchSymbols();
    [[nodiscard]] bool InstantiateAndMountEntry(std::string_view entryHint);
    [[nodiscard]] bool MountScriptRootObject(ObjectRef candidate);
    void FlushBindings();
    void FlushBindingsPostLayout();
    void StepFrameInternal(float dtSeconds, bool forceRenderSync, bool skipScriptFrameCallbacks);
    void SetupStencilJitIfEnabled();
    void EnsureSimStencilJit() const;
    [[nodiscard]] Interpreter* EnsureSimInterpreter() const;
    bool DispatchPendingCanvasDraws();
    [[nodiscard]] bool InvokeSymbol(
        SymbolId fnId,
        rs_rt::ValueSpan args,
        UnifiedValue* outResult,
        DomainKind domain) const;

    [[nodiscard]] bool HandleScriptInputEvent(
        VisualNodeId nodeId,
        VisualCallbackFlags flag,
        const NativeInputEvent& ev);
    [[nodiscard]] Size HandleCustomMeasure(
        VisualWorld& world,
        VisualNodeId nodeId,
        Size availSize);
    void HandleCustomArrange(
        VisualWorld& world,
        VisualNodeId nodeId,
        Rect finalSlot);

    AeroAppHostConfig config_;
    std::shared_ptr<UIResourceStore> resourceStore_;
    mutable ExecutionDomainContext domains_;
    VisualWorld world_;
    DrawingCommandBuffer drawBuffer_;
    VisualAnimationEngine animEngine_;
    UIHostPlatformHooks platformHooks_;
    VisualInputRouter inputRouter_;
    PagedRenderTree renderTree_;
    ZeroCopyGpuStream gpuStream_;
    DesktopEventHost desktopHost_;
    NativeWindowHost windowHost_;
    std::shared_ptr<BindingRegistry> bindings_;

    std::shared_ptr<const ProgramImage> programImage_;
    std::shared_ptr<void> uiStencilModule_;
    mutable std::shared_ptr<void> simStencilModule_;
    std::unique_ptr<Interpreter> interpreter_;
    mutable std::unique_ptr<Interpreter> simInterpreter_;

    const rs_aot::ProgramDescriptor* aotDescriptor_ = nullptr;
    std::unordered_map<SymbolId, const rs_aot::TypeDescriptor*> aotPrebuiltTypes_;
    std::unordered_map<SymbolId, const rs_aot::FunctionDescriptor*> aotPrebuiltFunctions_;
    std::unique_ptr<rs_aot::ExecutionContext> aotContext_;
    std::unique_ptr<rs_aot::ExecutionContext> simAotContext_;

    // Fixed ODR layout shared identically by AeroGUI::AppRuntime (/GR-) and AeroGUI::AppDevHost.
    std::shared_ptr<void> devCompileResultOpaque_;

    CachedDispatchSymbols symbols_{};
    std::string preferredEntryClass_;
    ObjectRef rootObject_{};
    PersistentRoot rootObjectPin_{};
    ObjectRef simObject_{};
    PersistentRoot simObjectPin_{};
    SimTickCallback simTickCallback_{};
    std::unordered_map<std::uint64_t, std::pair<float, float>> lastCanvasDrawSizes_;
    std::uint64_t lastGcDestroyedNodes_ = 0u;
    std::size_t lastLazyCompiledCount_ = 0u;
    std::uint32_t idleLazyFrames_ = 0u;
    std::vector<PlayedSoundRecord> playedSounds_;
    std::string lastError_;
    Size lastNotifiedViewport_{-1.0f, -1.0f};
    std::optional<std::chrono::steady_clock::time_point> scheduledWakeAt_;
    bool scriptWakeRequested_ = false;
    bool renderWakeRequested_ = false;
    mutable bool pendingScriptSync_ = true;
    bool hasFlushedBindings_ = false;
    std::uint64_t lastFlushedObservableEpoch_ = 0u;
    bool closeRequested_ = false;
    bool inStepFrame_ = false;
    bool initialized_ = false;
};

} // namespace aero
