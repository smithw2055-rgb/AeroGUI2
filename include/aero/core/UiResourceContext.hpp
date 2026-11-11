#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

// ============================================================================
// UiResourceContext (P2-1):
// Instance-scoped resource container isolating AxbTemplateRegistry (styles,
// ControlTemplates, DataTemplates, Storyboards, DynamicResource subscriptions,
// localization dictionaries, MTSDF vector icon cache), FontAtlas, ImageAtlas,
// GradientBrushRegistry, and Rive asset search directories across concurrent
// Editor viewports, Play-In-Editor (PIE) worlds, and independent host windows.
// ============================================================================
class UiResourceContext {
public:
    UiResourceContext();
    explicit UiResourceContext(std::uint16_t fontAtlasSize);
    ~UiResourceContext();

    UiResourceContext(const UiResourceContext&) = delete;
    UiResourceContext& operator=(const UiResourceContext&) = delete;
    UiResourceContext(UiResourceContext&&) = delete;
    UiResourceContext& operator=(UiResourceContext&&) = delete;

    // Process-wide fallback context used when no ScopedUiResourceContext is active on the calling thread
    [[nodiscard]] static UiResourceContext& Default() noexcept;

    // Active context on the calling thread (falls back to Default() when un-scoped)
    [[nodiscard]] static UiResourceContext& Current() noexcept;

    // Non-null pointer if a ScopedUiResourceContext is currently pushed on this thread, else nullptr
    [[nodiscard]] static UiResourceContext* ActiveOverride() noexcept;
    [[nodiscard]] static bool HasActiveThreadOverride() noexcept;

    // Number of live UiResourceContext instances (excluding or including Default once initialized)
    [[nodiscard]] static std::size_t LiveInstanceCount() noexcept;

    // Optional non-intrusive binding between an ExecutionDomainContext and a UiResourceContext
    static void BindToDomains(
        const ExecutionDomainContext* domains,
        std::shared_ptr<UiResourceContext> context);
    static void BindToDomains(
        const ExecutionDomainContext& domains,
        std::shared_ptr<UiResourceContext> context) {
        BindToDomains(&domains, std::move(context));
    }
    static void UnbindFromDomains(const ExecutionDomainContext* domains) noexcept;
    static void UnbindFromDomains(const ExecutionDomainContext& domains) noexcept {
        UnbindFromDomains(&domains);
    }
    [[nodiscard]] static std::shared_ptr<UiResourceContext> FindForDomains(
        const ExecutionDomainContext* domains) noexcept;

    [[nodiscard]] AxbTemplateRegistry& Templates() noexcept { return templates_; }
    [[nodiscard]] const AxbTemplateRegistry& Templates() const noexcept { return templates_; }

    [[nodiscard]] FontAtlas& Fonts() noexcept { return fontAtlas_; }
    [[nodiscard]] const FontAtlas& Fonts() const noexcept { return fontAtlas_; }
    [[nodiscard]] FontAtlas& GetFontAtlas() noexcept { return fontAtlas_; }
    [[nodiscard]] const FontAtlas& GetFontAtlas() const noexcept { return fontAtlas_; }

    [[nodiscard]] ImageAtlas& Images() noexcept { return imageAtlas_; }
    [[nodiscard]] const ImageAtlas& Images() const noexcept { return imageAtlas_; }
    [[nodiscard]] ImageAtlas& GetImageAtlas() noexcept { return imageAtlas_; }
    [[nodiscard]] const ImageAtlas& GetImageAtlas() const noexcept { return imageAtlas_; }

    [[nodiscard]] GradientBrushRegistry& GradientBrushes() noexcept { return gradientBrushes_; }
    [[nodiscard]] const GradientBrushRegistry& GradientBrushes() const noexcept { return gradientBrushes_; }

    void AddRiveSearchDirectory(std::string_view directory);
    void ClearRiveSearchDirectories() noexcept;
    [[nodiscard]] std::vector<std::string> RiveSearchDirectoriesSnapshot() const;

    // Clear all mutable resources, styles, custom fonts, textures, and gradient brushes in this context
    void ClearAll() noexcept;

private:
    friend class ScopedUiResourceContext;

    AxbTemplateRegistry templates_;
    FontAtlas fontAtlas_;
    ImageAtlas imageAtlas_;
    GradientBrushRegistry gradientBrushes_;
    mutable std::mutex riveDirMutex_;
    std::vector<std::string> riveSearchDirs_;
};

// ============================================================================
// ScopedUiResourceContext (P2-1):
// Zero-allocation RAII thread_local stack guard routing all *::Instance() calls
// on the calling thread to the bound UiResourceContext for the scope's lifetime.
// ============================================================================
class ScopedUiResourceContext {
public:
    explicit ScopedUiResourceContext(UiResourceContext& context) noexcept;
    explicit ScopedUiResourceContext(UiResourceContext* context) noexcept;
    explicit ScopedUiResourceContext(std::shared_ptr<UiResourceContext> context) noexcept;
    ~ScopedUiResourceContext() noexcept;

    ScopedUiResourceContext(const ScopedUiResourceContext&) = delete;
    ScopedUiResourceContext& operator=(const ScopedUiResourceContext&) = delete;
    ScopedUiResourceContext(ScopedUiResourceContext&&) = delete;
    ScopedUiResourceContext& operator=(ScopedUiResourceContext&&) = delete;

    [[nodiscard]] bool IsActive() const noexcept { return active_; }
    [[nodiscard]] UiResourceContext* Context() const noexcept { return pushed_; }

private:
    friend class UiResourceContext;

    std::shared_ptr<UiResourceContext> pinned_{};
    ScopedUiResourceContext* prevFrame_ = nullptr;
    UiResourceContext* previous_ = nullptr;
    UiResourceContext* pushed_ = nullptr;
    bool active_ = false;
};

} // namespace aero
