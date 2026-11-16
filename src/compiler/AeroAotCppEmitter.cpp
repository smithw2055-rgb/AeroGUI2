#include "aero/compiler/AeroCompiler.hpp"
#include "AeroCompilerInternal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/controls/AxbTemplateMounter.hpp"
#include "aero/controls/ItemsControls.hpp"
#include "aero/controls/MediaControls.hpp"
#include "aero/core/StringUtils.hpp"
#include "realscript/aot_cpp/AotCpp.h"

namespace aero {

namespace {

std::string FormatFloatLiteral(float v) {
    if (std::isinf(v)) {
        return (v < 0.0f) ? "-std::numeric_limits<float>::infinity()" : "std::numeric_limits<float>::infinity()";
    }
    if (std::isnan(v)) {
        return "-1.0f";
    }
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6ff", v);
    return std::string(buf);
}

std::string EscapeAotCppString(std::string_view sv) {
    std::string out;
    out.reserve(sv.size() + 8u);
    for (std::size_t i = 0u; i < sv.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(sv[i]);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '?':
                if (i + 1u < sv.size() && sv[i + 1u] == '?') {
                    out += "\\?";
                } else {
                    out.push_back('?');
                }
                break;
            default:
                if (c < 0x20u || c >= 0x7Fu) {
                    // Use fixed 3-digit octal escape so following [0-9a-fA-F] chars are never eaten as hex!
                    char oct[5];
                    oct[0] = '\\';
                    oct[1] = static_cast<char>('0' + ((c >> 6u) & 0x07u));
                    oct[2] = static_cast<char>('0' + ((c >> 3u) & 0x07u));
                    oct[3] = static_cast<char>('0' + (c & 0x07u));
                    oct[4] = '\0';
                    out.append(oct, 4u);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string EmitUnifiedValueLiteralCpp(const UnifiedValue& val) {
    if (const auto* b = std::get_if<bool>(&val)) {
        return *b ? "aero::UnifiedValue{true}" : "aero::UnifiedValue{false}";
    }
    if (const auto* i = std::get_if<std::int64_t>(&val)) {
        return "aero::value::FromInt(" + std::to_string(*i) + "LL)";
    }
    if (const auto* f = std::get_if<FloatValue>(&val)) {
        return "aero::MakeFloat(" + FormatFloatLiteral(f->value) + ")";
    }
    Float4Value f4{};
    if (val.tryGetQuatf(f4)) {
        return "aero::MakeFloat4InlineFast(" +
               FormatFloatLiteral(f4.x) + ", " +
               FormatFloatLiteral(f4.y) + ", " +
               FormatFloatLiteral(f4.z) + ", " +
               FormatFloatLiteral(f4.w) + ")";
    }
    if (const auto* s = std::get_if<StringValue>(&val)) {
        return "aero::value::FromString(\"" + EscapeAotCppString(s->str()) + "\")";
    }
    return "aero::UnifiedValue{}";
}

void AppendStoryboardPopulateCpp(
    std::string& src,
    const AxbStoryboard& sb,
    const std::string& sbVar,
    const std::string& indent) {
    if (!sb.key.empty()) {
        src += indent + sbVar + ".key = \"" + EscapeAotCppString(sb.key) + "\";\n";
    }
    for (const auto& tl : sb.timelines) {
        src += indent + "{\n";
        src += indent + "    aero::AxbAnimationTimeline tl{};\n";
        if (!tl.targetName.empty()) {
            src += indent + "    tl.targetName = \"" + EscapeAotCppString(tl.targetName) + "\";\n";
        }
        src += indent + "    tl.targetPropertyId = " + std::to_string(tl.targetPropertyId) + "u;\n";
        src += indent + "    tl.beginTimeSeconds = " + FormatFloatLiteral(tl.beginTimeSeconds) + ";\n";
        src += indent + "    tl.durationSeconds = " + FormatFloatLiteral(tl.durationSeconds) + ";\n";
        src += indent + "    tl.hasFrom = " + std::string(tl.hasFrom ? "true" : "false") + ";\n";
        if (tl.hasFrom) {
            src += indent + "    tl.fromValue = " + EmitUnifiedValueLiteralCpp(tl.fromValue) + ";\n";
        }
        src += indent + "    tl.hasTo = " + std::string(tl.hasTo ? "true" : "false") + ";\n";
        src += indent + "    tl.isBy = " + std::string(tl.isBy ? "true" : "false") + ";\n";
        if (tl.hasTo) {
            src += indent + "    tl.toValue = " + EmitUnifiedValueLiteralCpp(tl.toValue) + ";\n";
        }
        src += indent + "    tl.easingKind = static_cast<aero::EasingFunctionKind>(" +
               std::to_string(static_cast<unsigned>(tl.easingKind)) + "u);\n";
        src += indent + "    tl.easingMode = static_cast<aero::EasingMode>(" +
               std::to_string(static_cast<unsigned>(tl.easingMode)) + "u);\n";
        src += indent + "    tl.fillBehavior = static_cast<aero::FillBehavior>(" +
               std::to_string(static_cast<unsigned>(tl.fillBehavior)) + "u);\n";
        src += indent + "    tl.autoReverse = " + std::string(tl.autoReverse ? "true" : "false") + ";\n";
        src += indent + "    tl.repeatCount = " + std::to_string(tl.repeatCount) + ";\n";
        for (const auto& kf : tl.keyFrames) {
            src += indent + "    {\n";
            src += indent + "        aero::AxbKeyFrame kf{};\n";
            src += indent + "        kf.timeSeconds = " + FormatFloatLiteral(kf.timeSeconds) + ";\n";
            src += indent + "        kf.value = " + EmitUnifiedValueLiteralCpp(kf.value) + ";\n";
            src += indent + "        kf.easingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(kf.easingKind)) + "u);\n";
            src += indent + "        kf.easingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(kf.easingMode)) + "u);\n";
            src += indent + "        kf.isDiscrete = " + std::string(kf.isDiscrete ? "true" : "false") + ";\n";
            src += indent + "        tl.keyFrames.push_back(std::move(kf));\n";
            src += indent + "    }\n";
        }
        src += indent + "    " + sbVar + ".timelines.push_back(std::move(tl));\n";
        src += indent + "}\n";
    }
}

void AppendSetterListPopulateCpp(
    std::string& src,
    const std::vector<AxbPropertySetter>& setters,
    const std::string& targetVecExpr,
    const std::string& indent) {
    for (const auto& s : setters) {
        src += indent + "{\n";
        src += indent + "    aero::AxbPropertySetter ps{};\n";
        if (!s.targetName.empty()) {
            src += indent + "    ps.targetName = \"" + EscapeAotCppString(s.targetName) + "\";\n";
        }
        src += indent + "    ps.propId = " + std::to_string(s.propId) + "u;\n";
        src += indent + "    ps.value = " + EmitUnifiedValueLiteralCpp(s.value) + ";\n";
        src += indent + "    " + targetVecExpr + ".push_back(std::move(ps));\n";
        src += indent + "}\n";
    }
}

void AppendGridDefinitionCpp(
    std::string& src,
    const std::string& curVar,
    std::string_view methodName,
    const AxbInstruction& inst) {
    const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
    src += "    if (" + curVar + ") {\n";
    src += "        if (auto* grid = " + curVar + "->DynamicCast<aero::Grid>()) {\n";
    src += "            grid->" + std::string(methodName) + "(static_cast<aero::GridUnitType>(" +
           std::to_string(static_cast<unsigned>(inst.flags)) + "u), " +
           FormatFloatLiteral(val) + ");\n";
    src += "        }\n";
    src += "    }\n";
}

} // namespace

AeroViewAotCppOutput AeroCompiler::GenerateAotCppFromPackage(
    const AxbPackage& package,
    std::string_view cppNamespace,
    std::string_view builderFunctionName) {
    AeroViewAotCppOutput out{};
    out.cppNamespace = cppNamespace.empty()
        ? "aero::aot_generated"
        : std::string(cppNamespace);

    if (!builderFunctionName.empty()) {
        out.builderFunctionName = std::string(builderFunctionName);
    } else if (!package.className.empty()) {
        out.builderFunctionName = "BuildView_" + rs_aot::sanitizeCppIdentifier(package.className);
    } else {
        out.builderFunctionName = "BuildAeroView";
    }

    // 1. Emit C++17 Header (.hpp)
    std::string hdr;
    hdr += "#pragma once\n\n";
    hdr += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    hdr += "#include <cstdint>\n";
    hdr += "#include <limits>\n";
    hdr += "#include \"aero/controls/Controls.hpp\"\n";
    hdr += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n\n";
    hdr += "namespace " + out.cppNamespace + " {\n\n";
    hdr += "bool " + out.builderFunctionName + "(\n";
    hdr += "    aero::UIElement* rootControl,\n";
    hdr += "    aero::ExecutionDomainContext* domains = nullptr,\n";
    hdr += "    aero::DualTrackBindingEngine* bindingEngine = nullptr,\n";
    hdr += "    aero::ObjectRef dataContext = {},\n";
    hdr += "    const aero::Interpreter* eventInterpreter = nullptr,\n";
    hdr += "    aero::rs_aot::ExecutionContext* eventAotContext = nullptr);\n\n";
    hdr += "} // namespace " + out.cppNamespace + "\n";
    out.header = std::move(hdr);

    // 2. Emit C++17 Source (.cpp) - unrolled batch creation, static properties, MTSDF, Styles/Storyboards & bindings
    bool hasNestedMultiInBindingChains = false;
    for (const auto& ch : package.bindingChains) {
        if (ch.nestedMultiBinding != nullptr) {
            hasNestedMultiInBindingChains = true;
            break;
        }
    }
    bool hasP1StylePayload = false;
    for (const auto& st : package.styles) {
        if (!st.controlTemplateKey.empty() || st.inlineControlTemplate != nullptr) {
            hasP1StylePayload = true;
            break;
        }
        for (const auto& tr : st.triggers) {
            if (!tr.enterActions.empty() || !tr.exitActions.empty()) {
                hasP1StylePayload = true;
                break;
            }
            for (const auto& c : tr.conditions) {
                if (!c.converterKey.empty() || c.converterSymbolId != 0) {
                    hasP1StylePayload = true;
                    break;
                }
            }
            if (hasP1StylePayload) break;
        }
        if (hasP1StylePayload) break;
        for (const auto& et : st.eventTriggers) {
            if (!et.actions.empty() || !et.conditions.empty()) {
                hasP1StylePayload = true;
                break;
            }
        }
        if (hasP1StylePayload) break;
    }
    const bool needSharedPkg =
        !package.multiBindings.empty() ||
        package.HasMotionSection() ||
        !package.dataTemplates.empty() ||
        !package.controlTemplates.empty() ||
        hasNestedMultiInBindingChains ||
        hasP1StylePayload;

    std::vector<std::uint8_t> sharedPkgBytes;
    if (needSharedPkg) {
        sharedPkgBytes = package.Serialize();
    }
    const bool emitSharedPkg = needSharedPkg && !sharedPkgBytes.empty();

    std::string src;
    src += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    src += "#include <limits>\n";
    src += "#include <mutex>\n";
    src += "#include \"aero/animation/AnimationEngine.hpp\"\n";
    src += "#include \"aero/controls/Controls.hpp\"\n";
    src += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n";
    src += "#include \"realscript/runtime/Runtime.h\"\n\n";
    src += "namespace " + out.cppNamespace + " {\n\n";
    src += "bool " + out.builderFunctionName + "(\n";
    src += "    aero::UIElement* rootControl,\n";
    src += "    aero::ExecutionDomainContext* domains,\n";
    src += "    aero::DualTrackBindingEngine* bindingEngine,\n";
    src += "    aero::ObjectRef dataContext,\n";
    src += "    const aero::Interpreter* eventInterpreter,\n";
    src += "    aero::rs_aot::ExecutionContext* eventAotContext) {\n";
    src += "    if (!rootControl) {\n";
    src += "        return false;\n";
    src += "    }\n";
    src += "    aero::EnsureControlTypesRegistered();\n";

    if (emitSharedPkg) {
        src += "    static std::mutex sAotSharedPkgMutex;\n";
        src += "    static const aero::ProgramImage* sAotBoundProgram = nullptr;\n";
        src += "    static aero::AxbPackage* const sAotSharedPkgPtr = []() {\n";
        src += "        auto* p = new aero::AxbPackage();\n";
        src += "        static constexpr std::uint8_t kAotSharedPkgBytes[] = {";
        for (std::size_t b = 0; b < sharedPkgBytes.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(sharedPkgBytes[b])) + "u";
        }
        src += "};\n";
        src += "        (void)aero::AxbPackage::Deserialize(kAotSharedPkgBytes, sizeof(kAotSharedPkgBytes), *p);\n";
        src += "        p->PinValuesInPools();\n";
        src += "        aero::rs_rt::registerExternalValuePoolRootMarker(\n";
        src += "            p,\n";
        src += "            [p]() noexcept {\n";
        src += "                std::lock_guard<std::mutex> lock(sAotSharedPkgMutex);\n";
        src += "                p->MarkValuesInPools();\n";
        src += "            });\n";
        src += "        return p;\n";
        src += "    }();\n";
        src += "    aero::AxbPackage& sAotSharedPkg = *sAotSharedPkgPtr;\n";
        src += "    bool sAotReboundSharedPkg = false;\n";
        src += "    {\n";
        src += "        std::lock_guard<std::mutex> lock(sAotSharedPkgMutex);\n";
        src += "        const aero::ProgramImage* liveProg = bindingEngine ? bindingEngine->Program() : nullptr;\n";
        src += "        if (liveProg && liveProg != sAotBoundProgram) {\n";
        src += "            (void)sAotSharedPkg.BindAgainstProgram(*liveProg, {}, domains);\n";
        src += "            sAotSharedPkg.PinValuesInPools();\n";
        src += "            sAotBoundProgram = liveProg;\n";
        src += "            sAotReboundSharedPkg = true;\n";
        src += "        }\n";
        src += "    }\n";
        src += "    (void)sAotReboundSharedPkg;\n";
    }

    // Emit pre-baked MTSDF vector path entries so pure C++17 AOT views have 0 runtime SVG parsing
    for (std::size_t mpIdx = 0; mpIdx < package.mtsdfPaths.size(); ++mpIdx) {
        const auto& mp = package.mtsdfPaths[mpIdx];
        if (mp.pathHash == 0 || mp.mtsdfRgbaPixels.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindMtsdfPath(" +
               std::to_string(mp.pathHash) + "ULL)) {\n";
        const std::string arrName = "kMtsdfPixels_" + std::to_string(mpIdx);
        src += "        static constexpr std::uint8_t " + arrName + "[] = {";
        for (std::size_t b = 0; b < mp.mtsdfRgbaPixels.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(mp.mtsdfRgbaPixels[b])) + "u";
        }
        src += "};\n";
        src += "        aero::AxbMtsdfPathEntry entry{};\n";
        src += "        entry.pathHash = " + std::to_string(mp.pathHash) + "ULL;\n";
        src += "        entry.boundsX = " + FormatFloatLiteral(mp.boundsX) + ";\n";
        src += "        entry.boundsY = " + FormatFloatLiteral(mp.boundsY) + ";\n";
        src += "        entry.boundsW = " + FormatFloatLiteral(mp.boundsW) + ";\n";
        src += "        entry.boundsH = " + FormatFloatLiteral(mp.boundsH) + ";\n";
        src += "        entry.u0 = " + FormatFloatLiteral(mp.u0) + ";\n";
        src += "        entry.v0 = " + FormatFloatLiteral(mp.v0) + ";\n";
        src += "        entry.u1 = " + FormatFloatLiteral(mp.u1) + ";\n";
        src += "        entry.v1 = " + FormatFloatLiteral(mp.v1) + ";\n";
        src += "        entry.atlasWidth = " + std::to_string(mp.atlasWidth) + "u;\n";
        src += "        entry.atlasHeight = " + std::to_string(mp.atlasHeight) + "u;\n";
        src += "        entry.cellWidth = " + std::to_string(mp.cellWidth) + "u;\n";
        src += "        entry.cellHeight = " + std::to_string(mp.cellHeight) + "u;\n";
        src += "        entry.pxRange = " + FormatFloatLiteral(mp.pxRange) + ";\n";
        src += "        entry.mtsdfRgbaPixels.assign(" + arrName + ", " + arrName + " + sizeof(" + arrName + "));\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterMtsdfPath(std::move(entry));\n";
        src += "    }\n";
    }

    // Emit keyed scalar/color/string resources into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& resPair : package.resources) {
        if (resPair.first.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindResource(\"" +
               EscapeAotCppString(resPair.first) + "\")) {\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterResource(\"" +
               EscapeAotCppString(resPair.first) + "\", " +
               EmitUnifiedValueLiteralCpp(resPair.second) + ");\n";
        src += "    }\n";
    }

    // Emit pre-compiled Storyboards & Styles into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& sb : package.storyboards) {
        if (sb.key.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindStoryboard(\"" +
               EscapeAotCppString(sb.key) + "\")) {\n";
        src += "        aero::AxbStoryboard sbDef{};\n";
        AppendStoryboardPopulateCpp(src, sb, "sbDef", "        ");
        src += "        aero::AxbTemplateRegistry::Instance().RegisterStoryboard(std::move(sbDef));\n";
        src += "    }\n";
    }

    for (std::size_t stIdx = 0; stIdx < package.styles.size(); ++stIdx) {
        const auto& st = package.styles[stIdx];
        if (!st.key.empty()) {
            src += "    if (" + std::string(emitSharedPkg ? "sAotReboundSharedPkg || " : "") +
                   "!aero::AxbTemplateRegistry::Instance().FindStyle(\"" +
                   EscapeAotCppString(st.key) + "\")) {\n";
        } else {
            src += "    {\n";
        }
        src += "        aero::AxbStyleDefinition stDef{};\n";
        if (!st.key.empty()) {
            src += "        stDef.key = \"" + EscapeAotCppString(st.key) + "\";\n";
        }
        if (!st.targetTypeName.empty()) {
            src += "        stDef.targetTypeName = \"" + EscapeAotCppString(st.targetTypeName) + "\";\n";
        }
        if (!st.basedOnKey.empty()) {
            src += "        stDef.basedOnKey = \"" + EscapeAotCppString(st.basedOnKey) + "\";\n";
        }
        if (!st.controlTemplateKey.empty()) {
            src += "        stDef.controlTemplateKey = \"" + EscapeAotCppString(st.controlTemplateKey) + "\";\n";
        }
        AppendSetterListPopulateCpp(src, st.setters, "stDef.setters", "        ");
        for (const auto& trig : st.triggers) {
            src += "        {\n";
            src += "            aero::AxbStyleTrigger trigDef{};\n";
            src += "            trigDef.overrideLocalValues = " +
                   std::string(trig.overrideLocalValues ? "true" : "false") + ";\n";
            for (const auto& cond : trig.conditions) {
                src += "            {\n";
                src += "                aero::AxbTriggerCondition tc{};\n";
                src += "                tc.propId = " + std::to_string(cond.propId) + "u;\n";
                src += "                tc.expectedValue = " + EmitUnifiedValueLiteralCpp(cond.expectedValue) + ";\n";
                src += "                tc.sourceKind = static_cast<aero::TriggerConditionSource>(" +
                       std::to_string(static_cast<unsigned>(cond.sourceKind)) + "u);\n";
                src += "                tc.converter = static_cast<aero::ValueConverterKind>(" +
                       std::to_string(static_cast<unsigned>(cond.converter)) + "u);\n";
                if (!cond.converterKey.empty()) {
                    src += "                tc.converterKey = \"" + EscapeAotCppString(cond.converterKey) + "\";\n";
                }
                if (!cond.converterParameter.empty()) {
                    src += "                tc.converterParameter = \"" + EscapeAotCppString(cond.converterParameter) + "\";\n";
                }
                src += "                tc.converterSymbolId = " + std::to_string(cond.converterSymbolId) + "ULL;\n";
                src += "                tc.expectNull = " + std::string(cond.expectNull ? "true" : "false") + ";\n";
                if (!cond.sourceName.empty()) {
                    src += "                tc.sourceName = \"" + EscapeAotCppString(cond.sourceName) + "\";\n";
                }
                if (!cond.bindingPath.empty()) {
                    src += "                tc.bindingPath = \"" + EscapeAotCppString(cond.bindingPath) + "\";\n";
                }
                for (const auto& chop : cond.compiledHops) {
                    src += "                {\n";
                    src += "                    aero::AxbCompiledHop ch{};\n";
                    src += "                    ch.memberName = \"" + EscapeAotCppString(chop.memberName) + "\";\n";
                    src += "                    ch.declaringTypeId = " + std::to_string(chop.declaringTypeId) + "ULL;\n";
                    src += "                    ch.fieldIndex = " + std::to_string(chop.fieldIndex) + "u;\n";
                    src += "                    ch.observableBitIndex = " + std::to_string(static_cast<unsigned>(chop.observableBitIndex)) + "u;\n";
                    src += "                    ch.getterSymbolId = " + std::to_string(chop.getterSymbolId) + "ULL;\n";
                    src += "                    ch.setterSymbolId = " + std::to_string(chop.setterSymbolId) + "ULL;\n";
                    src += "                    ch.isAutoProperty = " + std::string(chop.isAutoProperty ? "true" : "false") + ";\n";
                    src += "                    ch.isObservable = " + std::string(chop.isObservable ? "true" : "false") + ";\n";
                    src += "                    ch.isReadOnly = " + std::string(chop.isReadOnly ? "true" : "false") + ";\n";
                    src += "                    ch.isSimState = " + std::string(chop.isSimState ? "true" : "false") + ";\n";
                    src += "                    ch.isSimCommand = " + std::string(chop.isSimCommand ? "true" : "false") + ";\n";
                    src += "                    tc.compiledHops.push_back(std::move(ch));\n";
                    src += "                }\n";
                }
                src += "                trigDef.conditions.push_back(std::move(tc));\n";
                src += "            }\n";
            }
            AppendSetterListPopulateCpp(src, trig.setters, "trigDef.setters", "            ");
            for (const auto& esb : trig.enterStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard esbDef{};\n";
                AppendStoryboardPopulateCpp(src, esb, "esbDef", "                ");
                src += "                trigDef.enterStoryboards.push_back(std::move(esbDef));\n";
                src += "            }\n";
            }
            for (const auto& xsb : trig.exitStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard xsbDef{};\n";
                AppendStoryboardPopulateCpp(src, xsb, "xsbDef", "                ");
                src += "                trigDef.exitStoryboards.push_back(std::move(xsbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.triggers.push_back(std::move(trigDef));\n";
            src += "        }\n";
        }
        for (const auto& vsg : st.visualStateGroups) {
            src += "        {\n";
            src += "            aero::AxbVisualStateGroup vsgDef{};\n";
            src += "            vsgDef.name = \"" + EscapeAotCppString(vsg.name) + "\";\n";
            src += "            vsgDef.defaultTransitionDuration = " +
                   FormatFloatLiteral(vsg.defaultTransitionDuration) + ";\n";
            src += "            vsgDef.defaultEasingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingKind)) + "u);\n";
            src += "            vsgDef.defaultEasingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingMode)) + "u);\n";
            for (const auto& vs : vsg.states) {
                src += "            {\n";
                src += "                aero::AxbVisualState vsDef{};\n";
                src += "                vsDef.name = \"" + EscapeAotCppString(vs.name) + "\";\n";
                AppendSetterListPopulateCpp(src, vs.setters, "vsDef.setters", "                ");
                AppendStoryboardPopulateCpp(src, vs.storyboard, "vsDef.storyboard", "                ");
                src += "                vsgDef.states.push_back(std::move(vsDef));\n";
                src += "            }\n";
            }
            src += "            stDef.visualStateGroups.push_back(std::move(vsgDef));\n";
            src += "        }\n";
        }
        for (const auto& et : st.eventTriggers) {
            src += "        {\n";
            src += "            aero::AxbEventTrigger etDef{};\n";
            src += "            etDef.eventKind = static_cast<aero::RoutedTriggerEvent>(" +
                   std::to_string(static_cast<unsigned>(et.eventKind)) + "u);\n";
            if (!et.sourceName.empty()) {
                src += "            etDef.sourceName = \"" + EscapeAotCppString(et.sourceName) + "\";\n";
            }
            for (const auto& etSb : et.storyboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard etSbDef{};\n";
                AppendStoryboardPopulateCpp(src, etSb, "etSbDef", "                ");
                src += "                etDef.storyboards.push_back(std::move(etSbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.eventTriggers.push_back(std::move(etDef));\n";
            src += "        }\n";
        }
        if (emitSharedPkg && hasP1StylePayload) {
            src += "        if (" + std::to_string(stIdx) + "u < sAotSharedPkg.styles.size()) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(sAotSharedPkg.styles[" +
                   std::to_string(stIdx) + "u]);\n";
            src += "        } else {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
            src += "        }\n";
        } else if (emitSharedPkg) {
            src += "        if (sAotReboundSharedPkg && " + std::to_string(stIdx) + "u < sAotSharedPkg.styles.size()) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(sAotSharedPkg.styles[" +
                   std::to_string(stIdx) + "u]);\n";
            src += "        } else {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
            src += "        }\n";
        } else {
            src += "        aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
        }
        src += "    }\n";
    }

    if (emitSharedPkg) {
        for (std::size_t dtIdx = 0; dtIdx < package.dataTemplates.size(); ++dtIdx) {
            const auto& dt = package.dataTemplates[dtIdx];
            if (!dt.templatePackage) {
                continue;
            }
            if (!dt.key.empty()) {
                src += "    if (sAotReboundSharedPkg || !aero::AxbTemplateRegistry::Instance().FindDataTemplate(\"" +
                       EscapeAotCppString(dt.key) + "\")) {\n";
            } else {
                src += "    {\n";
            }
            src += "        if (" + std::to_string(dtIdx) + "u < sAotSharedPkg.dataTemplates.size() && "
                   "sAotSharedPkg.dataTemplates[" + std::to_string(dtIdx) + "u].templatePackage) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterDataTemplate(sAotSharedPkg.dataTemplates[" +
                   std::to_string(dtIdx) + "u]);\n";
            src += "        }\n";
            src += "    }\n";
        }

        for (std::size_t ctIdx = 0; ctIdx < package.controlTemplates.size(); ++ctIdx) {
            const auto& ct = package.controlTemplates[ctIdx];
            if (!ct.templatePackage) {
                continue;
            }
            if (!ct.key.empty()) {
                src += "    if (sAotReboundSharedPkg || !aero::AxbTemplateRegistry::Instance().FindControlTemplate(\"" +
                       EscapeAotCppString(ct.key) + "\")) {\n";
            } else {
                src += "    {\n";
            }
            src += "        if (" + std::to_string(ctIdx) + "u < sAotSharedPkg.controlTemplates.size() && "
                   "sAotSharedPkg.controlTemplates[" + std::to_string(ctIdx) + "u].templatePackage) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterControlTemplate(sAotSharedPkg.controlTemplates[" +
                   std::to_string(ctIdx) + "u]);\n";
            src += "        }\n";
            src += "    }\n";
        }
    }

    src += "    aero::NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;\n";
    src += "    aero::ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;\n";
    src += "    const aero::ObjectRef effectiveContext = dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();\n";
    src += "    const aero::ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid() ? rootControl->BoundScriptWrapper() : effectiveContext;\n\n";
    src += "    (void)registry; (void)uiHeap; (void)effectiveContext; (void)wrapperObj;\n";
    src += "    (void)bindingEngine; (void)eventInterpreter; (void)eventAotContext;\n\n";

    std::vector<std::string> elemVarStack;
    std::size_t nextElemId = 0;
    bool rootVisited = false;
    src += "    std::vector<aero::UIElement*> localElements;\n";
    src += "    localElements.reserve(" + std::to_string(package.instructions.size() / 2u + 1u) + "u);\n";

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                const std::string varName = "elem_" + std::to_string(nextElemId++);

                if (!rootVisited) {
                    rootVisited = true;
                    src += "    aero::UIElement* " + varName + " = rootControl; (void)" + varName + ";\n";
                    if (package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        const std::string ownerTypeCond = (package.fieldsOwnerTypeId != 0)
                            ? (" && uiHeap->objectTypeIdFast(wrapperObj) == " + std::to_string(package.fieldsOwnerTypeId) + "ULL")
                            : std::string{};
                        src += "    if (domains && registry && uiHeap && wrapperObj.valid() && uiHeap->isAlive(wrapperObj)" + ownerTypeCond + ") {\n";
                        src += "        const auto rootH = rootControl->ToNativeHandle(*registry);\n";
                        src += "        (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                               std::to_string(package.rootHandleFieldIndex) + "u, rootH, nullptr);\n";
                        src += "    }\n";
                    }
                } else {
                    const std::string parentVar = elemVarStack.empty() ? "rootControl" : elemVarStack.back();
                    const std::string escapedType = EscapeAotCppString(std::string(typeName));
                    const std::string_view shortChild = str::StripNamespacePrefix(typeName);
                    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr &&
                        shortChild != "UserControl" && shortChild != "Page") {
                        src += "    aero::IntrusivePtr<aero::UIElement> " + varName +
                               "_ptr = aero::MakeIntrusive<aero::UserControl>(\"" + escapedType +
                               "\", domains, bindingEngine, effectiveContext, eventInterpreter, eventAotContext);\n";
                    } else {
                        src += "    auto " + varName + "_ptr = aero::CreateElementByTypeName(\"" +
                               escapedType + "\");\n";
                    }
                    src += "    aero::UIElement* " + varName + " = " + varName + "_ptr.Get();\n";
                    src += "    if (" + parentVar + " && " + varName + ") {\n";
                    src += "        " + parentVar + "->AddChild(" + varName + ", registry, uiHeap);\n";
                    if ((inst.flags & 0x01u) != 0u) {
                        src += "        if (auto* hcc = " + parentVar + "->DynamicCast<aero::HeaderedContentControl>()) {\n";
                        src += "            hcc->SetHeaderElement(" + varName + ");\n";
                        src += "        }\n";
                    }
                    src += "    }\n";
                }
                src += "    localElements.push_back(" + varName + ");\n";
                src += "    if (" + varName + " && bindingEngine) {\n";
                src += "        " + varName + "->BindTwoWayEngine(bindingEngine);\n";
                src += "    }\n";

                if (const auto probe = CreateElementByTypeName(typeName)) {
                    if (probe->IsA<ItemsControl>()) {
                        src += "    if (auto* ic = " + varName + " ? " + varName + "->DynamicCast<aero::ItemsControl>() : nullptr) {\n";
                        src += "        ic->SetDomains(domains);\n";
                        src += "        ic->SetBindingEngine(bindingEngine);\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            ic->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    } else if (probe->IsA<TabControl>()) {
                        src += "    if (auto* tc = " + varName + " ? " + varName + "->DynamicCast<aero::TabControl>() : nullptr) {\n";
                        src += "        tc->SetDomains(domains);\n";
                        src += "        tc->SetBindingEngine(bindingEngine);\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            tc->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    }
                    if (probe->IsA<RiveControl>()) {
                        src += "    if (auto* rc = " + varName + " ? " + varName + "->DynamicCast<aero::RiveControl>() : nullptr) {\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            rc->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    }
                }

                if (!elemName.empty()) {
                    src += "    if (" + varName + ") {\n";
                    src += "        " + varName + "->SetName(\"" + EscapeAotCppString(std::string(elemName)) + "\");\n";
                    for (const auto& namedSlot : package.namedControls) {
                        if (namedSlot.name == elemName &&
                            namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                            const std::string ownerTypeCond = (package.fieldsOwnerTypeId != 0)
                                ? (" && uiHeap->objectTypeIdFast(wrapperObj) == " + std::to_string(package.fieldsOwnerTypeId) + "ULL")
                                : std::string{};
                            src += "        if (domains && registry && uiHeap && wrapperObj.valid() && uiHeap->isAlive(wrapperObj)" + ownerTypeCond + ") {\n";
                            src += "            const auto h = " + varName + "->ToNativeHandle(*registry);\n";
                            src += "            (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                                   std::to_string(namedSlot.fieldIndexInClass) + "u, h, nullptr);\n";
                            src += "        }\n";
                            break;
                        }
                    }
                    src += "    }\n";
                }

                elemVarStack.push_back(varName);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elemVarStack.empty()) {
                    elemVarStack.pop_back();
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + ") {\n";
                    src += "        " + curVar + "->SetValue(" + std::to_string(inst.propId) + "u, " +
                           EmitUnifiedValueLiteralCpp(inst.literalValue) + ", aero::ValueSourceRank::Local);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!elemVarStack.empty() && inst.strIdx0 < package.bindingChains.size()) {
                    const std::string& curVar = elemVarStack.back();
                    const auto& chain = package.bindingChains[inst.strIdx0];
                    const bool isElemBind = !chain.elementName.empty() ||
                                            !chain.relativeAncestorType.empty() ||
                                            chain.relativeSelf;
                    src += "    if (" + curVar + " && bindingEngine" +
                           std::string(isElemBind ? "" : " && effectiveContext.valid()") + ") {\n";
                    if (chain.mode == BindingMode::TwoWay) {
                        src += "        " + curVar + "->BindTwoWayEngine(bindingEngine);\n";
                    }
                    if (emitSharedPkg && chain.nestedMultiBinding != nullptr) {
                        src += "        if (" + std::to_string(inst.strIdx0) + "u < sAotSharedPkg.bindingChains.size()) {\n";
                        if (isElemBind) {
                            src += "            (void)bindingEngine->BindElementProperty(sAotSharedPkg.bindingChains[" +
                                   std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        } else {
                            src += "            (void)bindingEngine->BindChain(effectiveContext, sAotSharedPkg.bindingChains[" +
                                   std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        }
                        src += "        }\n";
                    } else {
                        src += "        aero::AxbCompiledBindingChain chain{};\n";
                        src += "        chain.rawPath = \"" + EscapeAotCppString(chain.rawPath) + "\";\n";
                        src += "        chain.targetPropertyId = " + std::to_string(chain.targetPropertyId) + "u;\n";
                        src += "        chain.mode = static_cast<aero::BindingMode>(" +
                               std::to_string(static_cast<unsigned>(chain.mode)) + "u);\n";
                        src += "        chain.converter = static_cast<aero::ValueConverterKind>(" +
                               std::to_string(static_cast<unsigned>(chain.converter)) + "u);\n";
                        if (!chain.converterKey.empty()) {
                            src += "        chain.converterKey = \"" + EscapeAotCppString(chain.converterKey) + "\";\n";
                        }
                        if (!chain.converterParameter.empty()) {
                            src += "        chain.converterParameter = \"" + EscapeAotCppString(chain.converterParameter) + "\";\n";
                        }
                        if (!chain.stringFormat.empty()) {
                            src += "        chain.stringFormat = \"" + EscapeAotCppString(chain.stringFormat) + "\";\n";
                        }
                        src += "        chain.converterSymbolId = " + std::to_string(chain.converterSymbolId) + "ULL;\n";
                        src += "        chain.convertBackSymbolId = " + std::to_string(chain.convertBackSymbolId) + "ULL;\n";
                        if (!chain.elementName.empty()) {
                            src += "        chain.elementName = \"" + EscapeAotCppString(chain.elementName) + "\";\n";
                        }
                        if (!chain.relativeAncestorType.empty()) {
                            src += "        chain.relativeAncestorType = \"" + EscapeAotCppString(chain.relativeAncestorType) + "\";\n";
                        }
                        src += "        chain.relativeSelf = " + std::string(chain.relativeSelf ? "true" : "false") + ";\n";
                        if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                            src += "        chain.fallbackValue = " + EmitUnifiedValueLiteralCpp(chain.fallbackValue) + ";\n";
                        }
                        if (!std::holds_alternative<std::monostate>(chain.targetNullValue)) {
                            src += "        chain.targetNullValue = " + EmitUnifiedValueLiteralCpp(chain.targetNullValue) + ";\n";
                        }
                        for (const auto& hop : chain.hops) {
                            src += "        {\n";
                            src += "            aero::AxbCompiledHop hop{};\n";
                            src += "            hop.memberName = \"" + EscapeAotCppString(hop.memberName) + "\";\n";
                            src += "            hop.declaringTypeId = " + std::to_string(hop.declaringTypeId) + "ULL;\n";
                            src += "            hop.fieldIndex = " + std::to_string(hop.fieldIndex) + "u;\n";
                            src += "            hop.observableBitIndex = " + std::to_string(static_cast<unsigned>(hop.observableBitIndex)) + "u;\n";
                            src += "            hop.getterSymbolId = " + std::to_string(hop.getterSymbolId) + "ULL;\n";
                            src += "            hop.setterSymbolId = " + std::to_string(hop.setterSymbolId) + "ULL;\n";
                            src += "            hop.isAutoProperty = " + std::string(hop.isAutoProperty ? "true" : "false") + ";\n";
                            src += "            hop.isObservable = " + std::string(hop.isObservable ? "true" : "false") + ";\n";
                            src += "            hop.isReadOnly = " + std::string(hop.isReadOnly ? "true" : "false") + ";\n";
                            src += "            hop.isSimState = " + std::string(hop.isSimState ? "true" : "false") + ";\n";
                            src += "            hop.isSimCommand = " + std::string(hop.isSimCommand ? "true" : "false") + ";\n";
                            src += "            chain.hops.push_back(std::move(hop));\n";
                            src += "        }\n";
                        }
                        if (isElemBind) {
                            src += "        (void)bindingEngine->BindElementProperty(chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        } else {
                            src += "        (void)bindingEngine->BindChain(effectiveContext, chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        }
                    }
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindMultiProperty: {
                if (!elemVarStack.empty() && emitSharedPkg && inst.strIdx0 < package.multiBindings.size()) {
                    const std::string& curVar = elemVarStack.back();
                    const auto& mb = package.multiBindings[inst.strIdx0];
                    src += "    if (" + curVar + " && bindingEngine) {\n";
                    if (mb.mode == BindingMode::TwoWay) {
                        src += "        " + curVar + "->BindTwoWayEngine(bindingEngine);\n";
                    }
                    src += "        if (" + std::to_string(inst.strIdx0) + "u < sAotSharedPkg.multiBindings.size()) {\n";
                    src += "            (void)bindingEngine->BindMultiChain(effectiveContext, sAotSharedPkg.multiBindings[" +
                           std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    const std::string canonicalComp = compiler_detail::CanonicalizeEcsComponentName(compName);
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : rs_sem::stableTypeId(canonicalComp);
                    src += "    if (" + curVar + " && bindingEngine) {\n";
                    src += "        (void)bindingEngine->BindEcsColumn(" + std::to_string(compId) + "ULL, \"" +
                           EscapeAotCppString(std::string(fieldName)) + "\", 0u, aero::IntrusivePtr<aero::UIElement>(" +
                           curVar + ", true), " + std::to_string(inst.propId) + "u);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    SymbolId handlerSym = inst.symbolId;
                    if (handlerSym == 0 && inst.strIdx0 < package.eventBindings.size()) {
                        handlerSym = package.eventBindings[inst.strIdx0].handlerSymbolId;
                    }
                    src += "    if (" + curVar + " && wrapperObj.valid() && " +
                           std::to_string(handlerSym) + "ULL != 0ULL) {\n";
                    src += "        auto bindClickHandler = [&](auto* target) {\n";
                    src += "            if (eventInterpreter) {\n";
                    src += "                target->BindScriptClickHandler(eventInterpreter, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            } else if (eventAotContext) {\n";
                    src += "                target->BindAotClickHandler(eventAotContext, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            }\n";
                    src += "        };\n";
                    src += "        if (auto* btn = " + curVar + "->DynamicCast<aero::ButtonBase>()) {\n";
                    src += "            bindClickHandler(btn);\n";
                    src += "        } else if (auto* mi = " + curVar + "->DynamicCast<aero::MenuItem>()) {\n";
                    src += "            bindClickHandler(mi);\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::AddRowDefinition: {
                if (!elemVarStack.empty()) {
                    AppendGridDefinitionCpp(src, elemVarStack.back(), "AddRowDefinition", inst);
                }
                break;
            }

            case AxbOpcode::AddColumnDefinition: {
                if (!elemVarStack.empty()) {
                    AppendGridDefinitionCpp(src, elemVarStack.back(), "AddColumnDefinition", inst);
                }
                break;
            }

            case AxbOpcode::SetResourceProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string escapedKey = EscapeAotCppString(std::string(package.GetString(inst.strIdx0)));
                    const bool isDynamicOrLoc =
                        (inst.flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) != 0u;
                    src += "    if (" + curVar + ") {\n";
                    if (isDynamicOrLoc) {
                        src += "        if (const auto* dv = aero::AxbTemplateRegistry::Instance().FindDynamicResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *dv);\n";
                        src += "        } else if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *rv);\n";
                    } else {
                        src += "        if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *rv);\n";
                    }
                    if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        src += "        } else {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, " +
                               EmitUnifiedValueLiteralCpp(inst.literalValue) + ");\n";
                    }
                    src += "        }\n";
                    if (isDynamicOrLoc) {
                        src += "        aero::AxbTemplateRegistry::Instance().SubscribeDynamicResource(" +
                               curVar + ", rootControl, " + std::to_string(inst.propId) + "u, \"" +
                               escapedKey + "\", " + std::to_string(static_cast<unsigned>(inst.flags)) + "u);\n";
                    }
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindTemplateParent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + " && rootControl->TemplatedParent()) {\n";
                    src += "        rootControl->TemplatedParent()->AddTemplateBinding(" +
                           curVar + ", static_cast<std::uint16_t>(" + std::to_string(inst.strIdx0) +
                           "u), " + std::to_string(inst.propId) +
                           "u, static_cast<aero::ValueConverterKind>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u));\n";
                    src += "    }\n";
                }
                break;
            }
        }
    }

    if (emitSharedPkg && package.HasMotionSection()) {
        src += "    if (bindingEngine && bindingEngine->GetAnimationEngine()) {\n";
        src += "        (void)bindingEngine->GetAnimationEngine()->InstantiatePackageMotion(\n";
        src += "            rootControl,\n";
        src += "            localElements.data(),\n";
        src += "            localElements.size(),\n";
        src += "            sAotSharedPkg);\n";
        src += "    }\n";
    }

    src += "    return " + std::string(rootVisited ? "true" : "false") + ";\n";
    src += "}\n\n";
    src += "} // namespace " + out.cppNamespace + "\n";
    out.source = std::move(src);
    return out;
}

} // namespace aero
