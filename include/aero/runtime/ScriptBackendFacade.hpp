#pragma once

#include "aero/core/RsAliases.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"

#include <realscript/aot_cpp/AotRuntime.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace aero {

// Zero-RTTI unified runtime reflection & dispatch view across RealScript
// Bytecode/StencilJIT (ProgramImage + Interpreter) and Native C++17 AOT
// (ProgramDescriptor + ExecutionContext).
struct ScriptBackendView {
    ExecutionDomainContext* domains = nullptr;
    const ProgramImage* programImage = nullptr;
    const Interpreter* interpreter = nullptr;
    const rs_aot::ProgramDescriptor* aotProgram = nullptr;
    rs_aot::ExecutionContext* aotContext = nullptr;

    [[nodiscard]] const rs_aot::ProgramDescriptor* EffectiveAotProgram() const noexcept {
        if (aotProgram) {
            return aotProgram;
        }
        return aotContext ? &aotContext->program() : nullptr;
    }
};

[[nodiscard]] bool IsStdlibModuleName(std::string_view moduleName) noexcept;

[[nodiscard]] bool NameMatchesTypeSpec(
    std::string_view moduleName,
    std::string_view typeName,
    std::string_view spec) noexcept;

[[nodiscard]] UnifiedValue MakeDefaultFieldValue(
    rs_sem::PrimitiveType prim,
    SymbolId typeId) noexcept;

[[nodiscard]] const rs_aot::TypeDescriptor* FindAotTypeById(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId) noexcept;

[[nodiscard]] bool IsBytecodeTypeDerivedFrom(
    const ProgramImage& image,
    const rs_sem::TypeSymbol* type,
    std::string_view baseShortName) noexcept;

[[nodiscard]] bool IsAotTypeDerivedFrom(
    const rs_aot::ProgramDescriptor& prog,
    const rs_aot::TypeDescriptor* type,
    std::string_view baseShortName) noexcept;

[[nodiscard]] bool IsTypeDerivedFrom(
    const ScriptBackendView& env,
    SymbolId typeId,
    std::string_view baseShortName) noexcept;

[[nodiscard]] SymbolId FindBytecodeFunctionSymbol(
    const ProgramImage& image,
    std::string_view exactOrSuffix,
    bool excludeStdlibModules = false) noexcept;

[[nodiscard]] SymbolId FindAotFunctionSymbol(
    const rs_aot::ProgramDescriptor& prog,
    std::string_view exactOrSuffix,
    bool excludeStdlibPrefixes = false) noexcept;

[[nodiscard]] SymbolId FindBackendFunctionSymbol(
    const ScriptBackendView& env,
    std::string_view exactOrSuffix,
    bool excludeStdlib = false) noexcept;

[[nodiscard]] SymbolId FindBytecodeClassMethod(
    const ProgramImage& image,
    SymbolId typeId,
    std::string_view methodName,
    std::size_t nonSelfArgCount) noexcept;

[[nodiscard]] SymbolId FindAotClassMethod(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId,
    std::string_view methodName,
    std::uint32_t totalArgCountIncludingSelf) noexcept;

[[nodiscard]] SymbolId FindBackendClassMethod(
    const ScriptBackendView& env,
    SymbolId typeId,
    std::string_view methodName,
    std::size_t nonSelfArgCount) noexcept;

[[nodiscard]] bool AllocateBytecodeObjectWithDefaults(
    const ProgramImage& image,
    ManagedHeap& heap,
    const rs_sem::TypeSymbol& rawTs,
    ObjectRef& outObj,
    std::string* outError = nullptr);

[[nodiscard]] SymbolId FindBytecodeDefaultConstructor(
    const ProgramImage& image,
    const rs_sem::TypeSymbol& ts) noexcept;

[[nodiscard]] ManagedHeap* ResolveTargetHeap(
    const ScriptBackendView& env,
    ObjectRef target) noexcept;

[[nodiscard]] bool TryExtractStringValue(
    const ScriptBackendView& env,
    const UnifiedValue& val,
    std::string& outStr);

[[nodiscard]] std::string_view ExtractStringArgView(
    const ScriptBackendView& env,
    ValueSpan args,
    std::size_t idx,
    std::string& scratch);

[[nodiscard]] bool ResolveEnumOrdinal(
    const ScriptBackendView& env,
    SymbolId enumTypeId,
    std::string_view memberName,
    std::int64_t& outValue) noexcept;

[[nodiscard]] bool ReadSingleMember(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    UnifiedValue& outValue);

[[nodiscard]] bool WriteSingleMember(
    const ScriptBackendView& env,
    ObjectRef obj,
    std::string_view memberName,
    const UnifiedValue& value);

[[nodiscard]] bool ResolveDotPathTerminal(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    ObjectRef& outParentObj,
    std::string_view& outLeafMember);

[[nodiscard]] bool ReadDotPathValue(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    UnifiedValue& outValue);

[[nodiscard]] bool WriteDotPathValue(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    const UnifiedValue& inValue);

[[nodiscard]] bool WriteIndexedCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx,
    const UnifiedValue& value);

[[nodiscard]] bool AppendCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    const UnifiedValue& item);

[[nodiscard]] bool InsertCollectionElement(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx,
    const UnifiedValue& item);

[[nodiscard]] bool RemoveCollectionElementAt(
    const ScriptBackendView& env,
    ObjectRef colObj,
    std::size_t idx);

[[nodiscard]] bool InvokeCommandOrMethodByPath(
    const ScriptBackendView& env,
    ObjectRef rootTarget,
    std::string_view propPath,
    const std::string* optStringParam);

} // namespace aero
