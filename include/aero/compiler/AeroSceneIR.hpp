#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

enum class AeroFrontendFormat : std::uint8_t {
    Xaml = 0,
    RiveRiv = 1,
    RiveBinary = 1,
    LottieJson = 2,
    SvgToken = 3,
    SvgVectorToken = 3,
    FigmaDesignTokens = 4,
};

using FrontendSourceFormat = AeroFrontendFormat;

enum class IrContractPortDirection : std::uint8_t {
    Input = 0,
    OutputTrigger = 1,
    TwoWay = 2,
};

enum class IrContractPortValueType : std::uint8_t {
    Bool = 0,
    Float = 1,
    Int = 2,
    Color = 3,
    String = 4,
    Trigger = 5,
};

struct IrContractPort {
    std::string name;
    IrContractPortDirection direction = IrContractPortDirection::Input;
    IrContractPortValueType valueType = IrContractPortValueType::Float;
    std::uint16_t assignedPropId = 0xFFFFu; // PropId_FirstContractInput + slotIdx
    std::uint16_t smInputIndex = 0u;
    float defaultFloat = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    UnifiedValue defaultValue{};
};

struct IrSceneNode {
    std::uint32_t localElementIdx = 0;
    std::uint32_t parentElementIdx = 0xFFFFFFFFu;
    std::uint32_t externalTransformParentIdx = 0xFFFFFFFFu; // Non-tree Rive bone / Lottie Null layer parent
    float externalConstraintWeight = 1.0f;
    std::uint16_t externalConstraintFlags = 0x07u;
    std::string typeName;
    std::string name;
    float width = 0.0f;
    float height = 0.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotateDeg = 0.0f;
    float skewXDeg = 0.0f;
    float skewYDeg = 0.0f;
    float opacity = 1.0f;
    std::string svgPathData;
    std::vector<std::uint32_t> childIndices;
    std::vector<std::pair<std::uint16_t, UnifiedValue>> staticProperties;
};

struct IrVectorShapeAsset {
    std::uint64_t pathHash = 0;
    std::string svgPathData;
    float strokeThickness = 0.0f;
    float pxRange = 4.0f;
    std::uint16_t adaptiveCellSize = 0u; // 0 = auto-select 32/64/128 via SelectAdaptiveMtsdfCellSize
};

// Two-Tier AeroSceneGraphIR:
// Tier 1: Structural & Layout IR (Visual Tree, Contract Ports, Adaptive MTSDF Vector Assets)
// Tier 2: Motion & State Machine IR (Multi-Layer State Machines, CubicBezier Clips, Transform Constraints -> MOT6)
struct AeroSceneGraphIR {
    AeroFrontendFormat sourceFormat = AeroFrontendFormat::Xaml;
    std::string sourcePath;
    std::string className;
    std::string rootTypeName = "Canvas";
    std::string rootDataTypeName;
    float designWidth = 256.0f;
    float designHeight = 256.0f;
    std::vector<IrSceneNode> nodes;
    std::vector<IrContractPort> contractPorts;
    std::vector<IrVectorShapeAsset> vectorShapes;
    AxbPackage loweredPackage{};
};

class IFrontendImporter {
public:
    virtual ~IFrontendImporter() = default;

    [[nodiscard]] virtual AeroFrontendFormat Format() const noexcept = 0;
    [[nodiscard]] virtual bool ImportToSceneIR(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AeroSceneGraphIR& outIr,
        std::vector<std::string>& outDiagnostics) const = 0;
};

class XamlFrontendImporter final : public IFrontendImporter {
public:
    [[nodiscard]] AeroFrontendFormat Format() const noexcept override { return AeroFrontendFormat::Xaml; }
    [[nodiscard]] bool ImportToSceneIR(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AeroSceneGraphIR& outIr,
        std::vector<std::string>& outDiagnostics) const override;
};

class RiveFrontendImporter final : public IFrontendImporter {
public:
    [[nodiscard]] AeroFrontendFormat Format() const noexcept override { return AeroFrontendFormat::RiveRiv; }
    [[nodiscard]] bool ImportToSceneIR(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AeroSceneGraphIR& outIr,
        std::vector<std::string>& outDiagnostics) const override;
};

class LottieFrontendImporter final : public IFrontendImporter {
public:
    [[nodiscard]] AeroFrontendFormat Format() const noexcept override { return AeroFrontendFormat::LottieJson; }
    [[nodiscard]] bool ImportToSceneIR(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AeroSceneGraphIR& outIr,
        std::vector<std::string>& outDiagnostics) const override;
};

class SvgTokenFrontendImporter final : public IFrontendImporter {
public:
    [[nodiscard]] AeroFrontendFormat Format() const noexcept override { return AeroFrontendFormat::SvgToken; }
    [[nodiscard]] bool ImportToSceneIR(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AeroSceneGraphIR& outIr,
        std::vector<std::string>& outDiagnostics) const override;
};

// Strongly-typed runtime ContractInputBank bridging host C++, RealScript VM, Track A/B bindings,
// and in-place AnimationEngine state machines via PropId_FirstContractInput (0x1000 + slotIdx).
class ContractInputBank {
public:
    ContractInputBank() noexcept = default;
    ContractInputBank(
        UIElement* rootElement,
        AnimationEngine* animationEngine,
        const AxbPackage* package) noexcept
        : rootElement_(rootElement)
        , animationEngine_(animationEngine)
        , package_(package) {}

    void Bind(
        UIElement* rootElement,
        AnimationEngine* animationEngine,
        const AxbPackage* package) noexcept {
        rootElement_ = rootElement;
        animationEngine_ = animationEngine;
        package_ = package;
    }

    static bool SetFloat(
        UIElement* rootElement,
        const AxbPackage& package,
        std::string_view portName,
        float value,
        AnimationEngine* animationEngine = nullptr) noexcept {
        ContractInputBank bank(rootElement, animationEngine, &package);
        return bank.SetFloat(portName, value);
    }

    static float GetFloat(
        const UIElement* rootElement,
        const AxbPackage& package,
        std::string_view portName,
        AnimationEngine* animationEngine = nullptr,
        float fallback = 0.0f) noexcept {
        if (animationEngine && rootElement) {
            return animationEngine->GetStateMachineInputFloatByName(rootElement, portName);
        }
        if (!rootElement) {
            return fallback;
        }
        for (std::size_t i = 0; i < package.contractInputs.size(); ++i) {
            if (package.GetString(package.contractInputs[i].nameStrIdx) == portName) {
                const std::uint16_t propId = (package.contractInputs[i].bridgePropId != 0xFFFFu)
                    ? package.contractInputs[i].bridgePropId
                    : static_cast<std::uint16_t>(PropId_FirstContractInput + i);
                if (const auto* entry = rootElement->FindCompactEntry(propId)) {
                    float f = fallback;
                    if (TryGetFloat(entry->effectiveValue, f)) {
                        return f;
                    }
                    if (const auto* b = std::get_if<bool>(&entry->effectiveValue)) {
                        return *b ? 1.0f : 0.0f;
                    }
                }
                float defF = fallback;
                (void)TryGetFloat(package.contractInputs[i].defaultValue, defF);
                return defF;
            }
        }
        return fallback;
    }

    bool SetFloat(std::string_view portName, float value) noexcept {
        if (!package_) {
            return animationEngine_ && rootElement_ &&
                   animationEngine_->SetStateMachineInputFloatByName(rootElement_, portName, value);
        }
        for (std::size_t i = 0; i < package_->contractInputs.size(); ++i) {
            if (package_->GetString(package_->contractInputs[i].nameStrIdx) == portName) {
                return SetFloatBySlot(static_cast<std::uint16_t>(i), value);
            }
        }
        return animationEngine_ && rootElement_ &&
               animationEngine_->SetStateMachineInputFloatByName(rootElement_, portName, value);
    }

    bool SetFloatBySlot(std::uint16_t slotIdx, float value) noexcept {
        bool ok = false;
        if (rootElement_) {
            std::uint16_t propId = static_cast<std::uint16_t>(PropId_FirstContractInput + slotIdx);
            if (package_ && slotIdx < package_->contractInputs.size() &&
                package_->contractInputs[slotIdx].bridgePropId != 0xFFFFu) {
                propId = package_->contractInputs[slotIdx].bridgePropId;
            }
            ok = rootElement_->SetValue(propId, MakeFloat(value), ValueSourceRank::Local);
        }
        if (animationEngine_ && rootElement_) {
            ok = animationEngine_->SetStateMachineInputFloat(rootElement_, slotIdx, value) || ok;
        }
        return ok;
    }

    bool SetBool(std::string_view portName, bool value) noexcept {
        if (!package_) {
            return animationEngine_ && rootElement_ &&
                   animationEngine_->SetStateMachineInputFloatByName(
                       rootElement_, portName, value ? 1.0f : 0.0f);
        }
        for (std::size_t i = 0; i < package_->contractInputs.size(); ++i) {
            if (package_->GetString(package_->contractInputs[i].nameStrIdx) == portName) {
                const auto slotIdx = static_cast<std::uint16_t>(i);
                bool ok = false;
                if (rootElement_) {
                    const std::uint16_t propId = (package_->contractInputs[i].bridgePropId != 0xFFFFu)
                        ? package_->contractInputs[i].bridgePropId
                        : static_cast<std::uint16_t>(PropId_FirstContractInput + slotIdx);
                    ok = rootElement_->SetValue(propId, UnifiedValue{value}, ValueSourceRank::Local);
                }
                if (animationEngine_ && rootElement_) {
                    ok = animationEngine_->SetStateMachineInputFloat(
                             rootElement_, slotIdx, value ? 1.0f : 0.0f) || ok;
                }
                return ok;
            }
        }
        return animationEngine_ && rootElement_ &&
               animationEngine_->SetStateMachineInputFloatByName(
                   rootElement_, portName, value ? 1.0f : 0.0f);
    }

    bool FireTrigger(std::string_view portName) noexcept {
        if (!animationEngine_ || !rootElement_) {
            return false;
        }
        return animationEngine_->FireStateMachineTriggerByName(rootElement_, portName);
    }

    [[nodiscard]] float GetFloat(std::string_view portName, float fallback = 0.0f) const noexcept {
        if (package_) {
            return GetFloat(rootElement_, *package_, portName, animationEngine_, fallback);
        }
        if (animationEngine_ && rootElement_) {
            return animationEngine_->GetStateMachineInputFloatByName(rootElement_, portName);
        }
        return fallback;
    }

    [[nodiscard]] bool GetBool(std::string_view portName) const noexcept {
        return GetFloat(portName, 0.0f) >= 0.5f;
    }

private:
    UIElement* rootElement_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    const AxbPackage* package_ = nullptr;
};

} // namespace aero
