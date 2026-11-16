#pragma once

namespace aero {

class ExecutionDomainContext;
class DualTrackBindingEngine;
class StyleTriggerEngine;
class AnimationEngine;
class DepthBucketLayoutQueue;
class InputEventRouter;
class PagedRenderTree;
class UIResourceContext;

// Non-owning per-view / per-viewport subsystem service container shared across a UIElement tree.
class UIViewContext {
public:
    UIViewContext() noexcept = default;
    ~UIViewContext() noexcept = default;

    void Reset() noexcept;

    void SetDomains(ExecutionDomainContext* domains) noexcept { domains_ = domains; }
    [[nodiscard]] ExecutionDomainContext* GetDomains() const noexcept { return domains_; }

    void SetBindingEngine(DualTrackBindingEngine* engine) noexcept { bindingEngine_ = engine; }
    [[nodiscard]] DualTrackBindingEngine* GetBindingEngine() const noexcept { return bindingEngine_; }

    void SetStyleEngine(StyleTriggerEngine* engine) noexcept { styleEngine_ = engine; }
    [[nodiscard]] StyleTriggerEngine* GetStyleEngine() const noexcept { return styleEngine_; }

    void SetAnimationEngine(AnimationEngine* engine) noexcept { animationEngine_ = engine; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }

    void SetLayoutQueue(DepthBucketLayoutQueue* queue) noexcept { layoutQueue_ = queue; }
    [[nodiscard]] DepthBucketLayoutQueue* GetLayoutQueue() const noexcept { return layoutQueue_; }

    void SetInputRouter(InputEventRouter* router) noexcept { inputRouter_ = router; }
    [[nodiscard]] InputEventRouter* GetInputRouter() const noexcept { return inputRouter_; }

    void SetRenderTree(PagedRenderTree* tree) noexcept { renderTree_ = tree; }
    [[nodiscard]] PagedRenderTree* GetRenderTree() const noexcept { return renderTree_; }

    void SetResourceContext(UIResourceContext* resourceContext) noexcept {
        resourceContext_ = resourceContext;
    }
    [[nodiscard]] UIResourceContext* GetResourceContext() const noexcept {
        return resourceContext_;
    }

    void SetDpiScale(float scale) noexcept;
    [[nodiscard]] float GetDpiScale() const noexcept { return dpiScale_; }

private:
    ExecutionDomainContext* domains_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    StyleTriggerEngine* styleEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    DepthBucketLayoutQueue* layoutQueue_ = nullptr;
    InputEventRouter* inputRouter_ = nullptr;
    PagedRenderTree* renderTree_ = nullptr;
    UIResourceContext* resourceContext_ = nullptr;
    float dpiScale_ = 1.0f;
};

} // namespace aero
