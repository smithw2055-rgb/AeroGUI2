#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

inline constexpr std::uint32_t kAxb4Magic = 0x34425841u; // "AXB4"
inline constexpr std::uint32_t kAxb4Version = 0x00020000u;

enum class BindingMode : std::uint8_t {
    OneWay = 0,
    TwoWay = 1,
    OneTime = 2,
};

enum class AxbOpcode : std::uint8_t {
    BeginElement = 1,
    EndElement = 2,
    SetStaticProperty = 3,
    BindProperty = 4,
    BindEcsColumn = 5,
    BindEvent = 6,
    AddRowDefinition = 7,
    AddColumnDefinition = 8,
    SetResourceProperty = 9,
};

struct AxbCompiledHop {
    std::string memberName;
    SymbolId declaringTypeId = 0;
    std::uint32_t fieldIndex = PropertySlotHandle::InvalidIndex;
    std::uint8_t observableBitIndex = 0xFFu;
    SymbolId getterSymbolId = 0;
    SymbolId setterSymbolId = 0;
    bool isAutoProperty = false;
    bool isObservable = false;
    bool isReadOnly = false;
    bool isSimState = false;
    bool isSimCommand = false;

    [[nodiscard]] PropertySlotHandle ToSlotHandle() const noexcept {
        PropertySlotHandle slot;
        slot.typeId = declaringTypeId;
        slot.backingFieldIndex = fieldIndex;
        slot.getterSymbolId = getterSymbolId;
        slot.setterSymbolId = setterSymbolId;
        slot.observableBitIndex = observableBitIndex;
        slot.isAutoProperty = isAutoProperty;
        slot.isObservable = isObservable;
        slot.isReadOnly = isReadOnly;
        slot.isSimState = isSimState;
        slot.isSimCommand = isSimCommand;
        return slot;
    }
};

struct AxbCompiledBindingChain {
    std::string rawPath;
    std::uint16_t targetPropertyId = 0;
    BindingMode mode = BindingMode::OneWay;
    UnifiedValue fallbackValue{};
    std::vector<AxbCompiledHop> hops;
};

struct AxbNamedControlSlot {
    std::string name;
    std::string typeName;
    SymbolId typeSymbolId = 0;
    std::uint32_t fieldIndexInClass = PropertySlotHandle::InvalidIndex;
};

struct AxbEventBindingRecord {
    std::string controlName;
    std::string eventName;
    std::string handlerMethodName;
    SymbolId handlerSymbolId = 0;
};

struct AxbInstruction {
    AxbOpcode opcode = AxbOpcode::BeginElement;
    std::uint16_t propId = 0;
    std::uint8_t flags = 0;
    std::uint32_t strIdx0 = 0;
    std::uint32_t strIdx1 = 0;
    SymbolId symbolId = 0;
    UnifiedValue literalValue{};
};

struct AxbPackage {
    std::string className;
    std::string rootTypeName;
    std::uint32_t rootHandleFieldIndex = PropertySlotHandle::InvalidIndex;
    std::vector<std::string> stringTable;
    std::vector<AxbNamedControlSlot> namedControls;
    std::vector<AxbCompiledBindingChain> bindingChains;
    std::vector<AxbEventBindingRecord> eventBindings;
    std::vector<AxbInstruction> instructions;
    std::vector<std::pair<std::string, UnifiedValue>> resources;

    std::uint32_t InternString(std::string_view text);
    [[nodiscard]] std::string_view GetString(std::uint32_t index) const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> Serialize() const;
    static bool Deserialize(
        const std::uint8_t* data,
        std::size_t size,
        AxbPackage& outPackage,
        std::string* errorMessage = nullptr);
    static bool Deserialize(
        const std::vector<std::uint8_t>& bytes,
        AxbPackage& outPackage,
        std::string* errorMessage = nullptr);
};

} // namespace aero
