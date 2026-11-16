#pragma once

#include <cstdint>
#include <memory>
#include <string>
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
#include "aero/visual/VisualHostBindings.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualTree.hpp"

namespace aero {

struct AeroAppHostConfig {
    std::uint32_t viewportWidth = 1280u;
    std::uint32_t viewportHeight = 800u;
    bool headless = true;
    std::string windowTitle = "AeroGUI 3.0";
    std::string entryClassName;
    std::vector<std::string> assetSearchDirs;
};

class AeroAppHost final {
public:
    explicit AeroAppHost(const AeroAppHostConfig& config = AeroAppHostConfig{});
    ~AeroAppHost();

    AeroAppHost(const AeroAppHost&) = delete;
    AeroAppHost& operator=(const AeroAppHost&) = delete;

    [[nodiscard]] bool InitializeRuntime();
    [[nodiscard]] bool LoadProgramImage(std::shared_ptr<const ProgramImage> image);
    void Tick(float dtSeconds);
    void Shutdown() noexcept;

    [[nodiscard]] UIResourceStore& Resources() noexcept { return *resourceStore_; }
    [[nodiscard]] VisualWorld& World() noexcept { return world_; }
    [[nodiscard]] DrawingCommandBuffer& DrawBuffer() noexcept { return drawBuffer_; }
    [[nodiscard]] VisualAnimationEngine& Animations() noexcept { return animEngine_; }
    [[nodiscard]] VisualInputRouter& Input() noexcept { return inputRouter_; }
    [[nodiscard]] PagedRenderTree& RenderTree() noexcept { return renderTree_; }
    [[nodiscard]] ExecutionDomainContext& Domains() noexcept { return domains_; }
    [[nodiscard]] DesktopEventHost& DesktopHost() noexcept { return desktopHost_; }
    [[nodiscard]] NativeWindowHost& WindowHost() noexcept { return windowHost_; }

private:
    AeroAppHostConfig config_{};
    std::shared_ptr<UIResourceStore> resourceStore_;
    ExecutionDomainContext domains_;
    VisualWorld world_;
    DrawingCommandBuffer drawBuffer_;
    VisualAnimationEngine animEngine_;
    VisualInputRouter inputRouter_;
    PagedRenderTree renderTree_;
    ZeroCopyGpuStream gpuStream_;
    DesktopEventHost desktopHost_;
    NativeWindowHost windowHost_;
    UIHostPlatformHooks platformHooks_{};

    std::shared_ptr<BindingRegistry> bindings_;
    std::shared_ptr<const ProgramImage> programImage_;
    std::unique_ptr<Interpreter> interpreter_;
    bool initialized_ = false;
};

} // namespace aero
