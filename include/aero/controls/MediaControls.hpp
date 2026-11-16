#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "aero/controls/Control.hpp"

namespace aero {

struct AxbPackage;
class AnimationEngine;
class Canvas;

// ============================================================================
// Image
// ============================================================================

class Image : public UIElement {
    AERO_DECLARE_CLASS(Image, UIElement)
public:
    Image();
    ~Image() override = default;

    void SetSource(std::string_view sourceUri);
    [[nodiscard]] std::string GetSource() const;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    void SetStretchDirection(StretchDirection dir);
    [[nodiscard]] StretchDirection GetStretchDirection() const noexcept;

    [[nodiscard]] Size GetNaturalImageSize() const;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool HasOwnOpaqueShadowSurfaceForRender() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
};

// ============================================================================
// RiveControl
// ============================================================================

class RiveControl : public Control {
    AERO_DECLARE_CLASS(RiveControl, Control)
public:
    RiveControl();
    explicit RiveControl(
        std::string_view sourceUri,
        AnimationEngine* animationEngine = nullptr);
    ~RiveControl() override;

    static void AddAssetSearchDirectory(std::string_view directory);
    static void ClearAssetSearchDirectories();

    void SetSource(std::string_view sourceUri);
    [[nodiscard]] std::string GetSource() const;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    void SetStretchDirection(StretchDirection dir);
    [[nodiscard]] StretchDirection GetStretchDirection() const noexcept;

    void SetAnimationEngine(AnimationEngine* engine) noexcept;
    [[nodiscard]] AnimationEngine* ActiveAnimationEngine() const noexcept;

    bool LoadFromMemory(
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        std::string_view artboardNameHint = {});

    bool SetInputFloat(std::string_view inputName, float value) noexcept;
    bool SetInputBool(std::string_view inputName, bool value) noexcept;
    bool FireTrigger(std::string_view triggerName) noexcept;
    [[nodiscard]] float GetInputFloat(std::string_view inputName) const noexcept;

    std::size_t Advance(float dtSeconds) noexcept;

    [[nodiscard]] bool IsRiveLoaded() const noexcept {
        return artboardRoot_ != nullptr && mountedPackage_ != nullptr;
    }
    [[nodiscard]] const AxbPackage* MountedRivePackage() const noexcept {
        return mountedPackage_.get();
    }
    [[nodiscard]] Canvas* ArtboardCanvas() const noexcept { return artboardRoot_.Get(); }
    [[nodiscard]] Canvas* ArtboardRoot() const noexcept { return artboardRoot_.Get(); }
    [[nodiscard]] Size ArtboardSize() const noexcept { return artboardSize_; }

    [[nodiscard]] float ChildScaleX() const noexcept { return childScaleX_; }
    [[nodiscard]] float ChildScaleY() const noexcept { return childScaleY_; }
    [[nodiscard]] float ChildOriginX() const noexcept { return childOriginX_; }
    [[nodiscard]] float ChildOriginY() const noexcept { return childOriginY_; }
    [[nodiscard]] Matrix2D GetChildContainerTransform2D() const noexcept override {
        return Matrix2D::Scaling(childScaleX_, childScaleY_, childOriginX_, childOriginY_);
    }
    [[nodiscard]] bool IsSubtreeNamescopeBoundary() const noexcept override { return true; }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    bool ReloadFromSourceUri(std::string_view sourceUri);
    bool MountDecodedRivePackage(std::shared_ptr<AxbPackage> pkg);

    IntrusivePtr<Canvas> artboardRoot_;
    std::shared_ptr<AxbPackage> mountedPackage_;
    AnimationEngine* externalAnimationEngine_ = nullptr;
    std::unique_ptr<AnimationEngine> ownedAnimationEngine_;
    Size artboardSize_{};
    std::string loadedSourceUri_;
    bool reloadingSource_ = false;
    float childScaleX_ = 1.0f;
    float childScaleY_ = 1.0f;
    float childOriginX_ = 0.0f;
    float childOriginY_ = 0.0f;
};

// ============================================================================
// MediaElement
// ============================================================================

class MediaElement : public UIElement {
    AERO_DECLARE_CLASS(MediaElement, UIElement)
public:
    using MediaEndedCallback = std::function<void(MediaElement&)>;

    MediaElement();
    ~MediaElement() override = default;

    void SetSource(std::string_view sourceUri);
    [[nodiscard]] std::string GetSource() const;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    void SetLoadedBehavior(std::string_view behavior);
    [[nodiscard]] std::string GetLoadedBehavior() const;

    void SetUnloadedBehavior(std::string_view behavior);
    [[nodiscard]] std::string GetUnloadedBehavior() const;

    void SetVolume(float volume);
    [[nodiscard]] float GetVolume() const noexcept;

    void SetIsMuted(bool muted);
    [[nodiscard]] bool GetIsMuted() const noexcept;

    void SetPosition(float seconds);
    [[nodiscard]] float GetPosition() const noexcept;

    void Play();
    void Pause();
    void Stop();

    [[nodiscard]] bool IsPlaying() const noexcept { return isPlaying_; }
    [[nodiscard]] float PositionSeconds() const noexcept { return positionSeconds_; }
    [[nodiscard]] float DurationSeconds() const noexcept { return durationSeconds_; }
    void SetDurationSeconds(float duration);

    void SetMediaEndedCallback(MediaEndedCallback cb) {
        mediaEndedCallback_ = std::move(cb);
    }
    [[nodiscard]] std::uint32_t MediaEndedGeneration() const noexcept {
        return mediaEndedGeneration_;
    }

    [[nodiscard]] bool NeedsContinuousTick() const override { return isPlaying_; }
    void PlayMediaAction() override { Play(); }
    void StopMediaAction() override { Stop(); }
    [[nodiscard]] std::uint32_t GetMediaEndedGeneration() const noexcept override {
        return MediaEndedGeneration();
    }
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool HasOwnOpaqueShadowSurfaceForRender() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;
    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void TickControl(float dtSeconds) override;
    Size MeasureCore(Size availableSize) noexcept override;

private:
    bool isPlaying_ = false;
    float positionSeconds_ = 0.0f;
    float durationSeconds_ = 10.0f;
    std::uint32_t mediaEndedGeneration_ = 0u;
    MediaEndedCallback mediaEndedCallback_;
};

} // namespace aero
