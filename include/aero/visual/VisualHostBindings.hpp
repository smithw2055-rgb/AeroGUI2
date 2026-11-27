#pragma once

#include "aero/core/RsAliases.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualTree.hpp"

namespace aero {

bool RegisterVisualHostBindings(
    BindingRegistry& bindings,
    VisualWorld& world,
    DrawingCommandBuffer& drawBuffer,
    VisualAnimationEngine& animEngine,
    VisualInputRouter& inputRouter,
    ExecutionDomainContext& domains,
    const ProgramImage* programImage = nullptr,
    const Interpreter* interpreter = nullptr,
    rs_aot::ExecutionContext* aotContext = nullptr,
    UIHostPlatformHooks* platformHooks = nullptr);

} // namespace aero
