#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/core/RsAliases.hpp"

namespace aero {

class AxbTemplateRegistry;
class FontAtlas;
class ImageAtlas;
class GradientBrushRegistry;

// ============================================================================
// UIResourceContext (P2-1):
// Instance-scoped resource container isolating AxbTemplateRegistry (styles,
// ControlTemplates, DataTemplates, Storyboards, DynamicResource subscriptions,
// localization dictionaries, MTSDF vector icon cache), FontAtlas, ImageAtlas,
// GradientBrushRegistry, and Rive asset search directories across concurrent
// Editor viewports, Play-In-Editor (PIE) worlds, and independent host windows.
// ============================================================================
class UIResourceContext {
public:
    UIResourceContext();
    explicit UIResourceContext(std::uint16_t fontAtlasSize);
    ~UIResourceContext();

    UIResourceContext(const UIResourceContext&) = delete;
    UIResourceContext& operator=(const UIResourceContext&) = delete;
    UIResourceContext(UIResourceContext&&) = delete;
    UIResourceContext& operator=(UIResourceContext&&) = delete;

    // Process-wide fallback context used when no ScopedUIResourceContext is active on the calling thread
    [[nodiscard]] static UIResourceContext& Default() noexcept;

    // Active context on the calling thread (falls back to Default() when un-scoped)
    [[nodiscard]] static UIResourceContext& Current() noexcept;

    // Non-null pointer if a ScopedUIResourceContext is currently pushed on this thread, else nullptr
    [[nodiscard]] static UIResourceContext* ActiveOverride() noexcept;
    [[nodiscard]] static bool HasActiveThreadOverride() noexcept;

    // Number of live UIResourceContext instances (excluding or including Default once initialized)
    [[nodiscard]] static std::size_t LiveInstanceCount() noexcept;

    // Optional non-intrusive binding between an ExecutionDomainContext and a UIResourceContext
    static void BindToDomains(
        const ExecutionDomainContext* domains,
        std::shared_ptr<UIResourceContext> context);
    static void BindToDomains(
        const ExecutionDomainContext& domains,
        std::shared_ptr<UIResourceContext> context) {
        BindToDomains(&domains, std::move(context));
    }
    static void UnbindFromDomains(const ExecutionDomainContext* domains) noexcept;
    static void UnbindFromDomains(const ExecutionDomainContext& domains) noexcept {
        UnbindFromDomains(&domains);
    }
    [[nodiscard]] static std::shared_ptr<UIResourceContext> FindForDomains(
        const ExecutionDomainContext* domains) noexcept;

    [[nodiscard]] AxbTemplateRegistry& Templates() noexcept;
    [[nodiscard]] const AxbTemplateRegistry& Templates() const noexcept;

    [[nodiscard]] FontAtlas& Fonts() noexcept;
    [[nodiscard]] const FontAtlas& Fonts() const noexcept;
    [[nodiscard]] FontAtlas& GetFontAtlas() noexcept;
    [[nodiscard]] const FontAtlas& GetFontAtlas() const noexcept;

    [[nodiscard]] ImageAtlas& Images() noexcept;
    [[nodiscard]] const ImageAtlas& Images() const noexcept;
    [[nodiscard]] ImageAtlas& GetImageAtlas() noexcept;
    [[nodiscard]] const ImageAtlas& GetImageAtlas() const noexcept;

    [[nodiscard]] GradientBrushRegistry& GradientBrushes() noexcept;
    [[nodiscard]] const GradientBrushRegistry& GradientBrushes() const noexcept;

    void AddRiveSearchDirectory(std::string_view directory);
    void ClearRiveSearchDirectories() noexcept;
    [[nodiscard]] std::vector<std::string> RiveSearchDirectoriesSnapshot() const;

    void AddXamlSearchDirectory(std::string_view directory);
    void ClearXamlSearchDirectories() noexcept;
    [[nodiscard]] std::vector<std::string> XamlSearchDirectoriesSnapshot() const;

    // Clear all mutable resources, styles, custom fonts, textures, and gradient brushes in this context
    void ClearAll() noexcept;

private:
    friend class ScopedUIResourceContext;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// ScopedUIResourceContext (P2-1):
// Zero-allocation RAII thread_local stack guard routing all *::Instance() calls
// on the calling thread to the bound UIResourceContext for the scope's lifetime.
// ============================================================================
class ScopedUIResourceContext {
public:
    explicit ScopedUIResourceContext(UIResourceContext& context) noexcept;
    explicit ScopedUIResourceContext(UIResourceContext* context) noexcept;
    explicit ScopedUIResourceContext(std::shared_ptr<UIResourceContext> context) noexcept;
    ~ScopedUIResourceContext() noexcept;

    ScopedUIResourceContext(const ScopedUIResourceContext&) = delete;
    ScopedUIResourceContext& operator=(const ScopedUIResourceContext&) = delete;
    ScopedUIResourceContext(ScopedUIResourceContext&&) = delete;
    ScopedUIResourceContext& operator=(ScopedUIResourceContext&&) = delete;

    [[nodiscard]] bool IsActive() const noexcept { return active_; }
    [[nodiscard]] UIResourceContext* Context() const noexcept { return pushed_; }

private:
    friend class UIResourceContext;

    std::shared_ptr<UIResourceContext> pinned_{};
    ScopedUIResourceContext* prevFrame_ = nullptr;
    UIResourceContext* previous_ = nullptr;
    UIResourceContext* pushed_ = nullptr;
    bool active_ = false;
};

} // namespace aero
