#include "aero/core/AeroObject.hpp"

namespace aero {

const TypeClass* AeroObject::StaticTypeClass() noexcept {
    static TypeClass sTypeClass{};
    static const bool sRegistered = []() {
        TypeRegistry::Instance().RegisterType(
            sTypeClass,
            "Aero.Core.AeroObject",
            nullptr,
            +[]() -> IntrusivePtr<AeroObject> {
                return MakeIntrusive<AeroObject>();
            });
        return true;
    }();
    (void)sRegistered;
    return &sTypeClass;
}

} // namespace aero
