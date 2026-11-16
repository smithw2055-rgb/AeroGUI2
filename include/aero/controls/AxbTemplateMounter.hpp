#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

class AnimationEngine;
class DualTrackBindingEngine;
class ExecutionDomainContext;

// Generic Custom Control Factory Registry (zero sample-specific knowledge in core)
using IntrusiveCustomControlFactory = IntrusivePtr<UIElement>(*)();

void RegisterCustomControlType(
    std::string_view typeName,
    IntrusiveCustomControlFactory factory,
    bool isTemplatedControl = false);

[[nodiscard]] bool IsCustomControlRegistered(
    std::string_view typeName,
    bool* outIsTemplatedControl = nullptr) noexcept;

// Factory and self-contained .axb4 instruction VM mounting
[[nodiscard]] IntrusivePtr<UIElement> CreateElementByTypeName(std::string_view typeName);

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains = nullptr,
    DualTrackBindingEngine* bindingEngine = nullptr,
    ObjectRef dataContext = {},
    const Interpreter* eventInterpreter = nullptr,
    rs_aot::ExecutionContext* eventAotContext = nullptr,
    AnimationEngine* animationEngine = nullptr);

bool PreMountAxbTemplateStructure(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef wrapperContext,
    std::vector<IntrusivePtr<UIElement>>& outMountedElements,
    const Interpreter* eventInterpreter = nullptr,
    rs_aot::ExecutionContext* eventAotContext = nullptr,
    AnimationEngine* animationEngine = nullptr);

bool RebindMountedAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    std::vector<IntrusivePtr<UIElement>>& mountedElements,
    ExecutionDomainContext* domains = nullptr,
    DualTrackBindingEngine* bindingEngine = nullptr,
    ObjectRef dataContext = {},
    const Interpreter* eventInterpreter = nullptr,
    rs_aot::ExecutionContext* eventAotContext = nullptr,
    AnimationEngine* animationEngine = nullptr);

bool MountControlTemplatePackage(
    UIElement* hostControl,
    UIElement* templateRootControl,
    const AxbPackage& templatePackage,
    ExecutionDomainContext* domains = nullptr,
    DualTrackBindingEngine* bindingEngine = nullptr,
    ObjectRef dataContext = {},
    const Interpreter* eventInterpreter = nullptr,
    rs_aot::ExecutionContext* eventAotContext = nullptr,
    AnimationEngine* animationEngine = nullptr);

void EnsureControlTypesRegistered() noexcept;

} // namespace aero
