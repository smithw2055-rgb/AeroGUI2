#include "aero/core/UIViewContext.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

void UIViewContext::SetDpiScale(float scale) noexcept {
    dpiScale_ = (std::isfinite(scale) && scale > 0.0f)
        ? std::clamp(scale, 0.25f, 8.0f)
        : 1.0f;
}

void UIViewContext::Reset() noexcept {
    domains_ = nullptr;
    bindingEngine_ = nullptr;
    styleEngine_ = nullptr;
    animationEngine_ = nullptr;
    layoutQueue_ = nullptr;
    inputRouter_ = nullptr;
    renderTree_ = nullptr;
    resourceContext_ = nullptr;
    dpiScale_ = 1.0f;
}

} // namespace aero
