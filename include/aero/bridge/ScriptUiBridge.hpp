#pragma once

#include <cstdint>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

struct ReactivePropertyBinding {
    ObjectRef sourceObject{};
    PropertySlotHandle sourceSlot{};
    IntrusivePtr<UIElement> targetElement{};
    std::uint16_t targetPropertyId = 0;
};

class ScriptUiBridge {
public:
    explicit ScriptUiBridge(ExecutionDomainContext& domains);

    // Register zero-hash C++ UIElement / Button / Window native functions into BindingRegistry
    void RegisterCoreUiBindings(BindingRegistry& bindings);

    // Bind a [Observable] source property on SimHeap or UiHeap to a UIElement property
    bool BindObservableProperty(
        ObjectRef sourceObject,
        PropertySlotHandle sourceSlot,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        RuntimeError* error = nullptr);

    // Flush all dirty [Observable] objects from SimHeap and UiHeap into bound UIElement properties
    std::size_t FlushReactiveBindings(RuntimeError* error = nullptr);

    [[nodiscard]] const std::vector<ReactivePropertyBinding>& Bindings() const noexcept {
        return bindings_;
    }

private:
    ExecutionDomainContext& domains_;
    std::vector<ReactivePropertyBinding> bindings_;
};

} // namespace aero
