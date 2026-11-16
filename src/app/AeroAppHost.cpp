#include "aero/app/AeroAppHost.hpp"

#include <utility>

namespace aero {

AeroAppHost::AeroAppHost(const AeroAppHostConfig& config)
    : config_(config)
    , resourceStore_(std::make_shared<UIResourceStore>())
    , domains_()
    , world_(&domains_.nativeHandles(), resourceStore_.get(), &domains_.uiHeap())
    , drawBuffer_()
    , animEngine_()
    , inputRouter_(&world_, nullptr, &platformHooks_)
    , renderTree_()
    , gpuStream_(ShaderBackend::DesktopGL33, 256u)
    , desktopHost_(&world_, &drawBuffer_, &animEngine_, &inputRouter_, &renderTree_, resourceStore_)
    , windowHost_()
    , bindings_(std::make_shared<BindingRegistry>()) {
    for (const auto& dir : config_.assetSearchDirs) {
        resourceStore_->AddAssetSearchDirectory(dir);
    }
    renderTree_.SetResourceStore(resourceStore_.get());
}

AeroAppHost::~AeroAppHost() {
    Shutdown();
}

bool AeroAppHost::InitializeRuntime() {
    if (initialized_) {
        return true;
    }
    platformHooks_.requestWake = [this]() {
        desktopHost_.TriggerWake();
    };
    const bool ok = RegisterVisualHostBindings(
        *bindings_,
        world_,
        drawBuffer_,
        animEngine_,
        inputRouter_,
        domains_,
        programImage_.get(),
        interpreter_.get(),
        nullptr,
        &platformHooks_);
    initialized_ = ok;
    return ok;
}

bool AeroAppHost::LoadProgramImage(std::shared_ptr<const ProgramImage> image) {
    programImage_ = std::move(image);
    if (!InitializeRuntime()) {
        return false;
    }
    return programImage_ != nullptr;
}

void AeroAppHost::Tick(float dtSeconds) {
    desktopHost_.TickUi(dtSeconds, UiPhase_All);
}

void AeroAppHost::Shutdown() noexcept {
    windowHost_.Shutdown();
    world_.Clear();
    drawBuffer_.ClearAll();
    animEngine_.Clear();
    interpreter_.reset();
    programImage_.reset();
    initialized_ = false;
}

} // namespace aero
