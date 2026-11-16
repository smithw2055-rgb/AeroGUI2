#include "aero/app/AeroAppHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <realscript/aot_cpp/AotRuntime.h>
#include <realscript/semantic/Semantic.h>

#include "aero/visual/VisualHostBindings.hpp"

namespace aero {

namespace {

[[nodiscard]] bool WritePpmP6(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8) {
    if (!rgba8 || width == 0u || height == 0u || filePath.empty()) {
        return false;
    }
    const std::filesystem::path p(filePath);
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<char> rgbBuf(pixelCount * 3u);
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        rgbBuf[i * 3u + 0u] = static_cast<char>(rgba8[i * 4u + 0u]);
        rgbBuf[i * 3u + 1u] = static_cast<char>(rgba8[i * 4u + 1u]);
        rgbBuf[i * 3u + 2u] = static_cast<char>(rgba8[i * 4u + 2u]);
    }
    out.write(rgbBuf.data(), static_cast<std::streamsize>(rgbBuf.size()));
    return out.good();
}

[[nodiscard]] bool IsStdlibModuleName(std::string_view moduleName) noexcept {
    return moduleName.rfind("Aero.", 0u) == 0u ||
           moduleName.rfind("RealScript.", 0u) == 0u;
}

[[nodiscard]] std::string NormalizeDoubleColonToDot(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0u; i < s.size(); ++i) {
        if (i + 1u < s.size() && s[i] == ':' && s[i + 1u] == ':') {
            out.push_back('.');
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

[[nodiscard]] bool EndsWithSymbol(std::string_view full, std::string_view suffix) noexcept {
    if (full.size() < suffix.size() || suffix.empty()) {
        return false;
    }
    if (full == suffix) {
        return true;
    }
    if (full.compare(full.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    if (suffix.front() == ':' || suffix.front() == '.') {
        return true;
    }
    const char prev = full[full.size() - suffix.size() - 1u];
    return prev == '.' || prev == ':';
}

[[nodiscard]] bool NameMatchesTypeSpec(
    std::string_view moduleName,
    std::string_view typeName,
    std::string_view spec) noexcept {
    if (spec.empty() || typeName.empty()) {
        return false;
    }
    if (typeName == spec) {
        return true;
    }
    std::string dotQualified;
    std::string colonQualified;
    if (!moduleName.empty()) {
        dotQualified.reserve(moduleName.size() + 1u + typeName.size());
        dotQualified.append(moduleName);
        dotQualified.push_back('.');
        dotQualified.append(typeName);
        colonQualified.reserve(moduleName.size() + 2u + typeName.size());
        colonQualified.append(moduleName);
        colonQualified.append("::");
        colonQualified.append(typeName);
    }
    if (!dotQualified.empty() && (dotQualified == spec || colonQualified == spec)) {
        return true;
    }
    // Also tolerate Foo.Bar.Class when moduleName is Foo.Bar
    std::string normalized(spec);
    for (std::size_t pos = 0u; (pos = normalized.find("::", pos)) != std::string::npos;) {
        normalized.replace(pos, 2u, ".");
    }
    return normalized == dotQualified || normalized == typeName;
}

[[nodiscard]] std::uint64_t VisualNodeKey(VisualNodeId id) noexcept {
    return (static_cast<std::uint64_t>(id.generation) << 32u) | static_cast<std::uint64_t>(id.index);
}

[[nodiscard]] UnifiedValue DefaultFieldValue(rs_sem::PrimitiveType prim, rs_sem::SymbolId typeId) noexcept {
    switch (prim) {
        case rs_sem::PrimitiveType::Bool:
            return UnifiedValue{false};
        case rs_sem::PrimitiveType::Byte:
            return UnifiedValue{rs_rt::ByteValue{0u}};
        case rs_sem::PrimitiveType::SByte:
            return UnifiedValue{rs_rt::SByteValue{0}};
        case rs_sem::PrimitiveType::Short:
            return UnifiedValue{rs_rt::ShortValue{0}};
        case rs_sem::PrimitiveType::UShort:
            return UnifiedValue{rs_rt::UShortValue{0u}};
        case rs_sem::PrimitiveType::Int:
            return UnifiedValue{std::int64_t{0}};
        case rs_sem::PrimitiveType::UInt:
            return UnifiedValue{rs_rt::UIntValue{0u}};
        case rs_sem::PrimitiveType::Long:
            return UnifiedValue{rs_rt::LongValue{0}};
        case rs_sem::PrimitiveType::ULong:
            return UnifiedValue{rs_rt::ULongValue{0u}};
        case rs_sem::PrimitiveType::Float:
            return UnifiedValue{rs_rt::FloatValue{0.0f}};
        case rs_sem::PrimitiveType::Double:
            return UnifiedValue{0.0};
        case rs_sem::PrimitiveType::Char:
            return UnifiedValue{rs_rt::CharValue{0u}};
        case rs_sem::PrimitiveType::String:
            return UnifiedValue{rs_rt::NullString{}};
        case rs_sem::PrimitiveType::Object:
            return UnifiedValue{rs_rt::NullObject{}};
        case rs_sem::PrimitiveType::Array:
            return UnifiedValue{rs_rt::NullArray{}};
        case rs_sem::PrimitiveType::Handle: {
            rs_rt::NativeHandle nh{};
            nh.typeId = typeId;
            return UnifiedValue{nh};
        }
        case rs_sem::PrimitiveType::Enum: {
            rs_rt::EnumValue ev{};
            ev.typeId = typeId;
            ev.value = 0;
            return UnifiedValue{ev};
        }
        case rs_sem::PrimitiveType::Struct:
            if (const auto inlineStruct = rs_rt::makeDefaultInlineStructValue(typeId)) {
                return *inlineStruct;
            }
            return UnifiedValue{};
        default:
            return UnifiedValue{};
    }
}

[[nodiscard]] bool IsBytecodeTypeDerivedFrom(
    const ProgramImage& image,
    const rs_sem::TypeSymbol* type,
    std::string_view baseShortName) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_sem::TypeSymbol* cur = type;
    while (cur && visited.insert(cur->id).second) {
        if (cur->name == baseShortName) {
            return true;
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = image.resolveType(cur->baseTypeId);
    }
    return false;
}

[[nodiscard]] const rs_aot::TypeDescriptor* FindAotTypeById(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId) noexcept {
    if (!prog.types || typeId == 0u) {
        return nullptr;
    }
    for (std::uint32_t i = 0u; i < prog.typeCount; ++i) {
        if (prog.types[i].id == typeId) {
            return &prog.types[i];
        }
    }
    return nullptr;
}

[[nodiscard]] bool IsAotTypeDerivedFrom(
    const rs_aot::ProgramDescriptor& prog,
    const rs_aot::TypeDescriptor* type,
    std::string_view baseShortName) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = type;
    while (cur && visited.insert(cur->id).second) {
        if (cur->name && std::string_view(cur->name) == baseShortName) {
            return true;
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = FindAotTypeById(prog, cur->baseTypeId);
    }
    return false;
}

[[nodiscard]] SymbolId FindBytecodeFunctionSymbol(
    const ProgramImage& image,
    std::string_view exactOrSuffix,
    bool excludeStdlibModules = false) noexcept {
    if (exactOrSuffix.empty()) {
        return 0u;
    }
    if (!excludeStdlibModules) {
        if (const auto direct = image.findFunction(std::string(exactOrSuffix))) {
            return *direct;
        }
    }
    const std::string dotQuery = NormalizeDoubleColonToDot(exactOrSuffix);
    std::string_view bareQuery = exactOrSuffix;
    if (bareQuery.rfind("::", 0u) == 0u) {
        bareQuery.remove_prefix(2u);
    }
    if (!excludeStdlibModules && dotQuery != exactOrSuffix) {
        if (const auto directDot = image.findFunction(dotQuery)) {
            return *directDot;
        }
    }
    for (const auto& mod : image.modules()) {
        if (excludeStdlibModules && IsStdlibModuleName(mod.name)) {
            continue;
        }
        for (const auto& fn : mod.functions) {
            if (fn.name == exactOrSuffix || fn.name == bareQuery || fn.name == dotQuery) {
                return fn.symbolId;
            }
            const std::string qualColon = mod.name.empty() ? fn.name : (mod.name + "::" + fn.name);
            const std::string qualDot = mod.name.empty() ? fn.name : (mod.name + "." + fn.name);
            if (EndsWithSymbol(fn.name, exactOrSuffix) ||
                EndsWithSymbol(fn.name, dotQuery) ||
                EndsWithSymbol(qualColon, exactOrSuffix) ||
                EndsWithSymbol(qualDot, dotQuery)) {
                return fn.symbolId;
            }
        }
    }
    return 0u;
}

[[nodiscard]] SymbolId FindAotFunctionSymbol(
    const rs_aot::ProgramDescriptor& prog,
    std::string_view exactOrSuffix,
    bool excludeStdlibPrefixes = false) noexcept {
    if (!prog.functions || exactOrSuffix.empty()) {
        return 0u;
    }
    const std::string dotQuery = NormalizeDoubleColonToDot(exactOrSuffix);
    std::string_view bareQuery = exactOrSuffix;
    if (bareQuery.rfind("::", 0u) == 0u) {
        bareQuery.remove_prefix(2u);
    }
    auto isStdlibFn = [](std::string_view fname) noexcept {
        return fname.rfind("Aero.", 0u) == 0u ||
               fname.rfind("RealScript.", 0u) == 0u ||
               fname.rfind("Aero::", 0u) == 0u ||
               fname.rfind("RealScript::", 0u) == 0u;
    };
    for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
        const auto& fn = prog.functions[i];
        if (!fn.name) {
            continue;
        }
        const std::string_view fname(fn.name);
        if (excludeStdlibPrefixes && isStdlibFn(fname)) {
            continue;
        }
        if (fname == exactOrSuffix || fname == dotQuery || fname == bareQuery) {
            return fn.symbolId;
        }
    }
    for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
        const auto& fn = prog.functions[i];
        if (!fn.name) {
            continue;
        }
        const std::string_view fname(fn.name);
        if (excludeStdlibPrefixes && isStdlibFn(fname)) {
            continue;
        }
        if (EndsWithSymbol(fname, exactOrSuffix) ||
            EndsWithSymbol(fname, dotQuery) ||
            EndsWithSymbol(fname, bareQuery)) {
            return fn.symbolId;
        }
    }
    return 0u;
}

[[nodiscard]] SymbolId FindBytecodeClassMethod(
    const ProgramImage& image,
    SymbolId typeId,
    std::string_view methodName,
    std::size_t nonSelfArgCount) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_sem::TypeSymbol* cur = image.resolveType(typeId);
    while (cur && visited.insert(cur->id).second) {
        for (const auto& m : cur->methods) {
            if (m.name == methodName && m.parameters.size() == nonSelfArgCount) {
                return m.id;
            }
        }
        const std::string qualColon =
            (cur->moduleName.empty() ? std::string{} : (cur->moduleName + ".")) +
            cur->name + "::" + std::string(methodName);
        if (const auto direct = image.findFunction(qualColon)) {
            return *direct;
        }
        const std::string shortDot = cur->name + "." + std::string(methodName);
        for (const auto& mod : image.modules()) {
            if (!cur->moduleName.empty() && mod.name != cur->moduleName) {
                continue;
            }
            for (const auto& fn : mod.functions) {
                if (fn.name == shortDot || EndsWithSymbol(fn.name, shortDot)) {
                    return fn.symbolId;
                }
            }
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = image.resolveType(cur->baseTypeId);
    }
    return 0u;
}

[[nodiscard]] SymbolId FindAotClassMethod(
    const rs_aot::ProgramDescriptor& prog,
    SymbolId typeId,
    std::string_view methodName,
    std::uint32_t totalArgCountIncludingSelf) noexcept {
    std::unordered_set<SymbolId> visited;
    const rs_aot::TypeDescriptor* cur = FindAotTypeById(prog, typeId);
    while (cur && visited.insert(cur->id).second) {
        std::string fullPrefix;
        if (cur->moduleName && cur->moduleName[0] != '\0') {
            fullPrefix.append(cur->moduleName);
            fullPrefix.push_back('.');
        }
        if (cur->name) {
            fullPrefix.append(cur->name);
        }
        const std::string shortSuffix = std::string(cur->name ? cur->name : "") + "::" + std::string(methodName);
        const std::string dotSuffix = std::string(cur->name ? cur->name : "") + "." + std::string(methodName);
        const std::string ctorAltSuffix = std::string(cur->name ? cur->name : "") + ".." + std::string(methodName);
        const std::string fullColon = fullPrefix + "::" + std::string(methodName);
        const std::string fullDot = fullPrefix + "." + std::string(methodName);
        const std::string fullCtorAlt = fullPrefix + ".." + std::string(methodName);

        if (prog.functions) {
            for (std::uint32_t i = 0u; i < prog.functionCount; ++i) {
                const auto& fn = prog.functions[i];
                if (!fn.name || fn.parameterCount != totalArgCountIncludingSelf) {
                    continue;
                }
                const std::string_view fname(fn.name);
                if (fname == fullColon || fname == fullDot || fname == fullCtorAlt ||
                    EndsWithSymbol(fname, shortSuffix) ||
                    EndsWithSymbol(fname, dotSuffix) ||
                    EndsWithSymbol(fname, ctorAltSuffix)) {
                    return fn.symbolId;
                }
            }
        }
        if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
            break;
        }
        cur = FindAotTypeById(prog, cur->baseTypeId);
    }
    return 0u;
}

[[nodiscard]] bool ReadMemberFromHeap(
    const ExecutionDomainContext& domains,
    const ProgramImage* image,
    const Interpreter* interp,
    const rs_aot::ProgramDescriptor* aotProg,
    rs_aot::ExecutionContext* aotCtx,
    ObjectRef obj,
    std::string_view memberName,
    UnifiedValue& outValue) {
    if (!obj.valid() || memberName.empty()) {
        return false;
    }
    ManagedHeap* heap = domains.heapForObject(obj);
    if (!heap || !heap->isAlive(obj)) {
        return false;
    }
    const SymbolId typeId = heap->objectTypeIdFast(obj);
    if (typeId == 0u) {
        return false;
    }

    if (image) {
        if (const auto slot = domains.resolvePropertySlot(*image, typeId, memberName)) {
            rs_rt::RuntimeError err{};
            if (domains.readPropertySlotFast(domains.activeDomain(), obj, *slot, outValue, &err, interp)) {
                return true;
            }
            if (slot->isAutoProperty && slot->backingFieldIndex != PropertySlotHandle::InvalidIndex) {
                if (const UnifiedValue* fp = heap->fieldGetFast(obj, slot->backingFieldIndex)) {
                    outValue = *fp;
                    return true;
                }
            }
            if (slot->getterSymbolId != 0u && interp) {
                const auto exec = interp->invoke(slot->getterSymbolId, {UnifiedValue{obj}});
                if (exec.succeeded) {
                    outValue = exec.value;
                    return true;
                }
            }
        }
        if (const rs_sem::TypeSymbol* ts = image->resolveType(typeId)) {
            const std::string backing = "$" + std::string(memberName);
            for (const auto& f : ts->fields) {
                if (!f.staticField && (f.name == memberName || f.name == backing)) {
                    if (const UnifiedValue* fp = heap->fieldGetFast(obj, f.index)) {
                        outValue = *fp;
                        return true;
                    }
                }
            }
        }
        const SymbolId getterSym = FindBytecodeClassMethod(
            *image, typeId, "get_" + std::string(memberName), 0u);
        if (getterSym != 0u && interp) {
            const auto exec = interp->invoke(getterSym, {UnifiedValue{obj}});
            if (exec.succeeded) {
                outValue = exec.value;
                return true;
            }
        }
    }

    if (aotProg && aotCtx) {
        const std::string backing = "$" + std::string(memberName);
        std::unordered_set<SymbolId> visited;
        const rs_aot::TypeDescriptor* cur = FindAotTypeById(*aotProg, typeId);
        while (cur && visited.insert(cur->id).second) {
            if (cur->fields) {
                for (std::uint32_t fi = 0u; fi < cur->fieldCount; ++fi) {
                    const auto& fd = cur->fields[fi];
                    if (fd.staticField || !fd.name) {
                        continue;
                    }
                    const std::string_view fname(fd.name);
                    if (fname == memberName || fname == backing) {
                        if (const UnifiedValue* fp = heap->fieldGetFast(obj, fd.index)) {
                            outValue = *fp;
                            return true;
                        }
                    }
                }
            }
            if (cur->baseTypeId == 0u || cur->baseTypeId == cur->id) {
                break;
            }
            cur = FindAotTypeById(*aotProg, cur->baseTypeId);
        }
        const SymbolId getterSym = FindAotClassMethod(
            *aotProg, typeId, "get_" + std::string(memberName), 1u);
        if (getterSym != 0u) {
            const UnifiedValue selfArg{obj};
            if (aotCtx->invoke(getterSym, &selfArg, 1u, outValue)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool ReadScriptStringValue(
    const ExecutionDomainContext& domains,
    const UnifiedValue& val,
    std::string& outStr) {
    if (const auto* sv = std::get_if<StringValue>(&val)) {
        outStr = sv->str();
        return true;
    }
    if (const auto* ov = std::get_if<ObjectRef>(&val); ov && ov->valid()) {
        if (const auto sv = domains.stringViewAcrossDomains(*ov)) {
            outStr.assign(*sv);
            return true;
        }
    }
    return false;
}

} // namespace

AeroAppHost::AeroAppHost(const AeroAppHostConfig& config)
    : config_(config)
    , resourceStore_(std::make_shared<UIResourceStore>())
    , domains_()
    , world_(&domains_.nativeHandles(), resourceStore_.get(), &domains_.uiHeap())
    , drawBuffer_()
    , animEngine_()
    , inputRouter_(&world_, nullptr, &platformHooks_)
    , renderTree_()
    , gpuStream_(ShaderBackend::DesktopGL33, 256u)
    , desktopHost_(&world_, &drawBuffer_, &animEngine_, &inputRouter_, &renderTree_, resourceStore_)
    , windowHost_()
    , bindings_(std::make_shared<BindingRegistry>()) {
    for (const auto& dir : config_.assetSearchDirs) {
        if (!dir.empty()) {
            resourceStore_->AddAssetSearchDirectory(dir);
        }
    }
}

AeroAppHost::~AeroAppHost() {
    Shutdown();
}

void AeroAppHost::WireKernelBridges() {
    animEngine_.SetWakeHostCallback([this]() {
        desktopHost_.SetActiveAnimations(true);
        desktopHost_.TriggerWake();
        windowHost_.WakeEventLoop();
    });

    inputRouter_.SetScriptEventDispatcher(
        [this](VisualNodeId nodeId, VisualCallbackFlags flag, const NativeInputEvent& ev) -> bool {
            return HandleScriptInputEvent(nodeId, flag, ev);
        });

    world_.SetCustomLayoutCallbacks(
        [this](VisualWorld& w, VisualNodeId nid, Size avail) -> Size {
            return HandleCustomMeasure(w, nid, avail);
        },
        [this](VisualWorld& w, VisualNodeId nid, Rect slot) {
            HandleCustomArrange(w, nid, slot);
        });
}

bool AeroAppHost::InitializeRuntime() {
    if (initialized_) {
        return true;
    }

    lastError_.clear();
    const float vpW = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth));
    const float vpH = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight));

    desktopHost_.SetViewportSize(Size{vpW, vpH});
    windowHost_.BindDesktopHost(&desktopHost_, &renderTree_, &gpuStream_, &inputRouter_);

    WireKernelBridges();

    NativeWindowConfig winCfg{};
    winCfg.title = config_.windowTitle;
    winCfg.width = std::max<std::uint32_t>(1u, config_.viewportWidth);
    winCfg.height = std::max<std::uint32_t>(1u, config_.viewportHeight);
    winCfg.clearColor = config_.clearColor;
    winCfg.preferHardwareGpu = !config_.preferSoftwareRasterizer && !config_.headless;
    winCfg.allowHeadlessFallback = true;
    winCfg.forceHeadless = config_.headless;

    if (!windowHost_.Initialize(winCfg)) {
        lastError_ = "NativeWindowHost initialization failed";
        return false;
    }

    initialized_ = true;
    return true;
}

void AeroAppHost::ResetLoadedProgramState() noexcept {
    simObjectPin_.reset();
    simObject_ = ObjectRef{};
    rootObjectPin_.reset();
    rootObject_ = ObjectRef{};
    symbols_ = CachedDispatchSymbols{};
    lastCanvasDrawSizes_.clear();

    animEngine_.Clear();
    drawBuffer_.ClearAll();
    renderTree_.Clear();
    world_.Clear();
    desktopHost_.SetStateFlags(false, false, false, false);

    if (simInterpreter_) {
        domains_.detachInterpreter(*simInterpreter_);
        simInterpreter_.reset();
    }
    if (interpreter_) {
        domains_.detachInterpreter(*interpreter_);
        interpreter_.reset();
    }
    if (simAotContext_) {
        domains_.detachAotContext(*simAotContext_);
        simAotContext_.reset();
    }
    if (aotContext_) {
        domains_.detachAotContext(*aotContext_);
        aotContext_.reset();
    }
    programImage_.reset();
    aotDescriptor_ = nullptr;
    bindings_ = std::make_shared<BindingRegistry>();
}

void AeroAppHost::ResolveDispatchSymbols() {
    symbols_ = CachedDispatchSymbols{};
    if (programImage_) {
        const auto find = [&](std::string_view suffix) -> SymbolId {
            return FindBytecodeFunctionSymbol(*programImage_, suffix);
        };
        symbols_.dispatchPointer = find("InputDispatcher::__AeroDispatchPointer");
        symbols_.dispatchKey = find("InputDispatcher::__AeroDispatchKey");
        symbols_.dispatchText = find("InputDispatcher::__AeroDispatchText");
        symbols_.dispatchFocus = find("InputDispatcher::__AeroDispatchFocus");
        symbols_.dispatchCustomMeasure = find("InputDispatcher::__AeroDispatchCustomMeasure");
        symbols_.dispatchDraw = find("InputDispatcher::__AeroDispatchDraw");
        symbols_.bindingFlush = find("BindingEngine::Flush");
        symbols_.windowShow = find("Window::Show");
        symbols_.applyTemplate = find("FrameworkElement::ApplyTemplate");
        symbols_.ensureLoadedSubtree = find("UIElement::EnsureLoadedSubtree");
    } else if (aotDescriptor_) {
        const auto find = [&](std::string_view suffix) -> SymbolId {
            return FindAotFunctionSymbol(*aotDescriptor_, suffix);
        };
        symbols_.dispatchPointer = find("InputDispatcher::__AeroDispatchPointer");
        symbols_.dispatchKey = find("InputDispatcher::__AeroDispatchKey");
        symbols_.dispatchText = find("InputDispatcher::__AeroDispatchText");
        symbols_.dispatchFocus = find("InputDispatcher::__AeroDispatchFocus");
        symbols_.dispatchCustomMeasure = find("InputDispatcher::__AeroDispatchCustomMeasure");
        symbols_.dispatchDraw = find("InputDispatcher::__AeroDispatchDraw");
        symbols_.bindingFlush = find("BindingEngine::Flush");
        symbols_.windowShow = find("Window::Show");
        symbols_.applyTemplate = find("FrameworkElement::ApplyTemplate");
        symbols_.ensureLoadedSubtree = find("UIElement::EnsureLoadedSubtree");
    }
}

bool AeroAppHost::LoadProgramImage(
    std::shared_ptr<const ProgramImage> image,
    std::string_view entryClassOrFn) {
    const bool trace = std::getenv("AERO_TRACE") != nullptr;
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] %s\n", msg);
            std::fflush(stderr);
        }
    };
    logTrace("LoadProgramImage: start");
    if (!image) {
        lastError_ = "LoadProgramImage: null ProgramImage";
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }

    ResetLoadedProgramState();

    programImage_ = std::move(image);
    domains_.registerProgramObservableMetadata(*programImage_);

    interpreter_ = std::make_unique<Interpreter>(programImage_, domains_.uiHeapPtr());
    simInterpreter_ = std::make_unique<Interpreter>(programImage_, domains_.simHeapPtr());
    domains_.attachInterpreter(*interpreter_, DomainKind::UiPresentation);
    domains_.attachInterpreter(*simInterpreter_, DomainKind::SimDeterministic);

    if (!RegisterVisualHostBindings(
            *bindings_,
            world_,
            drawBuffer_,
            animEngine_,
            inputRouter_,
            domains_,
            programImage_.get(),
            interpreter_.get(),
            nullptr,
            &platformHooks_)) {
        lastError_ = "RegisterVisualHostBindings failed (Bytecode mode)";
        return false;
    }

    interpreter_->setBindingRegistry(bindings_);
    simInterpreter_->setBindingRegistry(bindings_);

    ResolveDispatchSymbols();
    logTrace("LoadProgramImage: bindings & symbols ready");

    std::string entryHint(entryClassOrFn);
    if (entryHint.empty()) {
        entryHint = !config_.entryFunctionName.empty() ? config_.entryFunctionName : config_.entryClassName;
    }
    if (!InstantiateAndMountEntry(entryHint)) {
        return false;
    }
    logTrace("LoadProgramImage: mounted entry, stepping initial frame");

    StepFrame(0.0f, true);
    logTrace("LoadProgramImage: done");
    return true;
}

#ifdef AERO_HAS_COMPILER_V3
bool AeroAppHost::CompileAndLoad(
    const AeroV3CompileInput& input,
    std::string* outError) {
    AeroV3CompileInput effective = input;
    if (config_.optimizeBytecode && !effective.optimizeBytecode) {
        effective.optimizeBytecode = true;
    }
    const AeroCompilerV3 compiler;
    auto compiled = std::make_unique<AeroV3CompileResult>(compiler.Compile(effective));
    lastCompileResult_ = std::move(compiled);

    if (!lastCompileResult_->succeeded || !lastCompileResult_->programImage) {
        std::string msg = "AeroCompilerV3 compilation failed";
        for (const auto& d : lastCompileResult_->diagnostics) {
            msg.append("\n  ");
            msg.append(d);
        }
        lastError_ = msg;
        if (outError) {
            *outError = msg;
        }
        return false;
    }

    std::string entryHint = config_.entryFunctionName;
    if (entryHint.empty() && !config_.entryClassName.empty()) {
        entryHint = config_.entryClassName;
    }
    if (!LoadProgramImage(lastCompileResult_->programImage, entryHint)) {
        if (entryHint.empty() && !lastCompileResult_->rootClassName.empty()) {
            if (LoadProgramImage(lastCompileResult_->programImage, lastCompileResult_->rootClassName)) {
                return true;
            }
        }
        if (outError) {
            *outError = lastError_;
        }
        return false;
    }
    return true;
}

const AeroV3CompileResult* AeroAppHost::LastCompileResult() const noexcept {
    return lastCompileResult_.get();
}
#endif

bool AeroAppHost::LoadAotProgram(
    const rs_aot::ProgramDescriptor* descriptor,
    std::string_view entryFunctionOrClass) {
    if (!descriptor) {
        lastError_ = "LoadAotProgram: null ProgramDescriptor";
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }

    ResetLoadedProgramState();

    aotDescriptor_ = descriptor;
    domains_.registerAotProgramAttributes(*aotDescriptor_);

    aotContext_ = std::make_unique<rs_aot::ExecutionContext>(
        *aotDescriptor_, domains_.uiHeapPtr(), bindings_);
    simAotContext_ = std::make_unique<rs_aot::ExecutionContext>(
        *aotDescriptor_, domains_.simHeapPtr(), bindings_);
    domains_.attachAotContext(*aotContext_, DomainKind::UiPresentation);
    domains_.attachAotContext(*simAotContext_, DomainKind::SimDeterministic);

    if (!RegisterVisualHostBindings(
            *bindings_,
            world_,
            drawBuffer_,
            animEngine_,
            inputRouter_,
            domains_,
            nullptr,
            nullptr,
            aotContext_.get(),
            &platformHooks_)) {
        lastError_ = "RegisterVisualHostBindings failed (AOT mode)";
        return false;
    }

    ResolveDispatchSymbols();

    std::string entryHint(entryFunctionOrClass);
    if (entryHint.empty()) {
        entryHint = !config_.entryFunctionName.empty() ? config_.entryFunctionName : config_.entryClassName;
    }
    if (!InstantiateAndMountEntry(entryHint)) {
        return false;
    }

    StepFrame(0.0f, true);
    return true;
}

bool AeroAppHost::InvokeSymbol(
    SymbolId fnId,
    const std::vector<UnifiedValue>& args,
    UnifiedValue* outResult,
    DomainKind domain) const {
    if (fnId == 0u) {
        return false;
    }
    if (programImage_) {
        Interpreter* targetInterp =
            (domain == DomainKind::SimDeterministic && simInterpreter_)
                ? simInterpreter_.get()
                : interpreter_.get();
        if (!targetInterp) {
            return false;
        }
        if (std::getenv("AERO_TRACE") != nullptr) {
            std::fprintf(stderr, "[AERO_TRACE] InvokeSymbol: bytecode sym=%llu args=%zu\n",
                static_cast<unsigned long long>(fnId), args.size());
            std::fflush(stderr);
        }
        rs_rt::Limits limits{};
        limits.instructionBudget = 32'000'000u;
        const auto exec = targetInterp->invoke(fnId, args, limits);
        if (std::getenv("AERO_TRACE") != nullptr) {
            std::fprintf(stderr, "[AERO_TRACE] InvokeSymbol: returned ok=%d err=%s\n",
                exec.succeeded ? 1 : 0, exec.error.message.c_str());
            for (const auto& st : exec.error.stackTrace) {
                std::fprintf(stderr, "[AERO_TRACE]   at %s\n", st.c_str());
            }
            std::fflush(stderr);
        }
        if (!exec.succeeded) {
            return false;
        }
        if (outResult) {
            *outResult = exec.value;
        }
        return true;
    }
    if (aotDescriptor_) {
        rs_aot::ExecutionContext* targetCtx =
            (domain == DomainKind::SimDeterministic && simAotContext_)
                ? simAotContext_.get()
                : aotContext_.get();
        if (!targetCtx) {
            return false;
        }
        UnifiedValue res{};
        if (!targetCtx->invoke(fnId, args.data(), args.size(), res)) {
            return false;
        }
        if (outResult) {
            *outResult = res;
        }
        return true;
    }
    return false;
}

bool AeroAppHost::InstantiateAndMountEntry(std::string_view entryHint) {
    const bool trace = std::getenv("AERO_TRACE") != nullptr;
    if (trace) {
        std::fprintf(stderr, "[AERO_TRACE] InstantiateAndMountEntry(entryHint='%.*s')\n",
            static_cast<int>(entryHint.size()), entryHint.data());
        if (programImage_) {
            for (const auto& m : programImage_->modules()) {
                if (m.name.rfind("Aero.", 0u) == 0u || m.name.rfind("RealScript.", 0u) == 0u) {
                    continue;
                }
                std::fprintf(stderr, "[AERO_TRACE]   user mod='%s' fns=%zu types=%zu\n",
                    m.name.c_str(), m.functions.size(), m.types.size());
                for (const auto& fn : m.functions) {
                    std::fprintf(stderr, "[AERO_TRACE]     fn id=%llu name='%s'\n",
                        static_cast<unsigned long long>(fn.symbolId), fn.name.c_str());
                }
                for (const auto& ts : m.types) {
                    std::fprintf(stderr, "[AERO_TRACE]     type id=%llu mod='%s' name='%s' base=%llu ctors=%zu\n",
                        static_cast<unsigned long long>(ts.id),
                        ts.moduleName.c_str(),
                        ts.name.c_str(),
                        static_cast<unsigned long long>(ts.baseTypeId),
                        ts.constructors.size());
                }
            }
        }
        std::fflush(stderr);
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);

    auto tryInvokeCandidateFn = [&](SymbolId fnId, ObjectRef& outObj) -> bool {
        if (fnId == 0u) {
            return false;
        }
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] invoking fnId=%llu\n",
                static_cast<unsigned long long>(fnId));
            std::fflush(stderr);
        }
        UnifiedValue res{};
        if (!InvokeSymbol(fnId, {}, &res, DomainKind::UiPresentation)) {
            if (trace) {
                std::fprintf(stderr, "[AERO_TRACE] fnId=%llu invoke failed\n",
                    static_cast<unsigned long long>(fnId));
                std::fflush(stderr);
            }
            return false;
        }
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] fnId=%llu succeeded tag=%d\n",
                static_cast<unsigned long long>(fnId), static_cast<int>(res.rawTag()));
            std::fflush(stderr);
        }
        if (const auto* objRef = std::get_if<ObjectRef>(&res); objRef && objRef->valid()) {
            outObj = *objRef;
            return true;
        }
        return false;
    };

    auto instantiateBytecodeType = [&](const rs_sem::TypeSymbol& rawTs) -> bool {
        const rs_sem::TypeSymbol* resolved = programImage_->resolveType(rawTs.id);
        const rs_sem::TypeSymbol& ts = resolved ? *resolved : rawTs;
        auto findFieldTypeId = [&](std::string_view typeSpec) noexcept -> SymbolId {
            if (typeSpec.empty()) {
                return 0u;
            }
            for (const auto& m : programImage_->modules()) {
                for (const auto& t : m.types) {
                    if (NameMatchesTypeSpec(t.moduleName, t.name, typeSpec)) {
                        return t.id;
                    }
                }
            }
            return 0u;
        };

        std::vector<UnifiedValue> fields;
        std::vector<std::size_t> refFields;
        fields.reserve(ts.fields.size());
        for (std::size_t fi = 0u; fi < ts.fields.size(); ++fi) {
            const auto& f = ts.fields[fi];
            const SymbolId fTypeId = findFieldTypeId(f.typeName);
            fields.push_back(DefaultFieldValue(f.type, fTypeId));
            if (f.type == rs_sem::PrimitiveType::String ||
                f.type == rs_sem::PrimitiveType::Object ||
                f.type == rs_sem::PrimitiveType::Array ||
                (f.type == rs_sem::PrimitiveType::Struct &&
                 !rs_rt::makeDefaultInlineStructValue(fTypeId).has_value())) {
                refFields.push_back(fi);
            }
        }
        RuntimeError allocErr{};
        const auto alloc = domains_.uiHeap().allocateObject(
            ts.id, std::move(fields), std::move(refFields), &allocErr);
        if (!alloc) {
            lastError_ = "allocateObject failed: " + allocErr.message;
            return false;
        }
        PersistentRoot pin = domains_.uiHeap().retain(*alloc);
        SymbolId ctorId = 0u;
        for (const auto& c : ts.constructors) {
            if (!c.staticMethod && c.parameters.empty()) {
                ctorId = c.id;
                break;
            }
        }
        if (ctorId == 0u) {
            const std::string qName = ts.moduleName.empty()
                ? ts.name
                : (ts.moduleName + "." + ts.name);
            ctorId = FindBytecodeFunctionSymbol(*programImage_, qName + "::.ctor");
            if (ctorId == 0u) {
                ctorId = FindBytecodeFunctionSymbol(*programImage_, ts.name + "..ctor");
            }
        }
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] instantiateBytecodeType '%s.%s' id=%llu fields=%zu ctorId=%llu\n",
                ts.moduleName.c_str(), ts.name.c_str(),
                static_cast<unsigned long long>(ts.id),
                ts.fields.size(),
                static_cast<unsigned long long>(ctorId));
            std::fflush(stderr);
        }
        if (ctorId != 0u) {
            rs_rt::Limits ctorLimits{};
            ctorLimits.instructionBudget = 32'000'000u;
            const auto exec = interpreter_->invoke(ctorId, {UnifiedValue{*alloc}}, ctorLimits);
            if (!exec.succeeded) {
                std::string stackSummary;
                for (std::size_t si = 0u; si < exec.error.stackTrace.size() && si < 6u; ++si) {
                    stackSummary += (si == 0u ? " [" : " -> ") + exec.error.stackTrace[si];
                }
                if (!stackSummary.empty()) {
                    stackSummary.push_back(']');
                }
                lastError_ = "Entry constructor failed: " + exec.error.message + stackSummary;
                return false;
            }
        } else {
            ObjectRef builtObj{};
            const SymbolId clsBuild = FindBytecodeFunctionSymbol(
                *programImage_,
                (ts.moduleName.empty() ? ts.name : (ts.moduleName + "." + ts.name)) + "::Build");
            if (tryInvokeCandidateFn(clsBuild, builtObj)) {
                return MountScriptRootObject(builtObj);
            }
        }
        return MountScriptRootObject(*alloc);
    };

    auto instantiateAotType = [&](const rs_aot::TypeDescriptor& td) -> bool {
        UnifiedValue allocVal{};
        if (!aotContext_->newObject(td.id, allocVal)) {
            lastError_ = "AOT newObject failed: " + aotContext_->error().message;
            return false;
        }
        if (const auto* objRef = std::get_if<ObjectRef>(&allocVal); objRef && objRef->valid()) {
            PersistentRoot pin = domains_.uiHeap().retain(*objRef);
            const SymbolId ctorId = FindAotClassMethod(*aotDescriptor_, td.id, ".ctor", 1u);
            if (ctorId != 0u) {
                UnifiedValue unused{};
                if (!aotContext_->invoke(ctorId, &allocVal, 1u, unused)) {
                    lastError_ = "AOT entry constructor failed: " + aotContext_->error().message;
                    return false;
                }
            }
            return MountScriptRootObject(*objRef);
        }
        return false;
    };

    ObjectRef entryObj{};

    // 1. If entryHint directly identifies a callable function, invoke it first.
    if (!entryHint.empty()) {
        const SymbolId fnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, entryHint)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, entryHint) : 0u);
        if (tryInvokeCandidateFn(fnId, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
    }

    // 2. If entryHint identifies a class (or module.class), try <Class>::Build or construct <Class>.
    if (!entryHint.empty() && programImage_) {
        const SymbolId buildFn = FindBytecodeFunctionSymbol(
            *programImage_, std::string(entryHint) + "::Build");
        if (tryInvokeCandidateFn(buildFn, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& mod : programImage_->modules()) {
                for (const auto& ts : mod.types) {
                    if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType) {
                        continue;
                    }
                    if (pass == 0 && (IsStdlibModuleName(mod.name) || IsStdlibModuleName(ts.moduleName))) {
                        continue;
                    }
                    if (NameMatchesTypeSpec(ts.moduleName, ts.name, entryHint)) {
                        return instantiateBytecodeType(ts);
                    }
                }
            }
        }
    }

    if (!entryHint.empty() && aotDescriptor_) {
        const SymbolId buildFn = FindAotFunctionSymbol(
            *aotDescriptor_, std::string(entryHint) + "::Build");
        if (tryInvokeCandidateFn(buildFn, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
        if (aotDescriptor_->types) {
            for (std::uint32_t ti = 0u; ti < aotDescriptor_->typeCount; ++ti) {
                const auto& td = aotDescriptor_->types[ti];
                if (td.kind != rs_sem::TypeKind::Class || td.interfaceType || td.delegateType || !td.name) {
                    continue;
                }
                const std::string_view modName = td.moduleName ? std::string_view(td.moduleName) : std::string_view{};
                if (NameMatchesTypeSpec(modName, td.name, entryHint)) {
                    return instantiateAotType(td);
                }
            }
        }
    }

    // 3. Auto-discover standard entry factories: CreateMainWindow, Main, CreateRoot, Build
    const char* kStandardFactories[] = {"::CreateMainWindow", "::Main", "::CreateRoot", "::Build"};
    for (const char* suffix : kStandardFactories) {
        const SymbolId fnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, suffix, true)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, suffix, true) : 0u);
        if (tryInvokeCandidateFn(fnId, entryObj)) {
            return MountScriptRootObject(entryObj);
        }
    }

    // 4. Auto-discover first concrete Window/UserControl/Page subclass outside Aero.*/RealScript.* stdlib
    if (programImage_) {
        for (const auto& mod : programImage_->modules()) {
            if (IsStdlibModuleName(mod.name)) {
                continue;
            }
            for (const auto& ts : mod.types) {
                if (ts.kind != rs_sem::TypeKind::Class || ts.abstractType || ts.staticType ||
                    IsStdlibModuleName(ts.moduleName)) {
                    continue;
                }
                if (!ts.moduleName.empty() && ts.moduleName != mod.name) {
                    continue;
                }
                if (IsBytecodeTypeDerivedFrom(*programImage_, &ts, "Window") ||
                    IsBytecodeTypeDerivedFrom(*programImage_, &ts, "UserControl") ||
                    IsBytecodeTypeDerivedFrom(*programImage_, &ts, "Page")) {
                    return instantiateBytecodeType(ts);
                }
            }
        }
    } else if (aotDescriptor_ && aotDescriptor_->types) {
        for (std::uint32_t ti = 0u; ti < aotDescriptor_->typeCount; ++ti) {
            const auto& td = aotDescriptor_->types[ti];
            if (td.kind != rs_sem::TypeKind::Class || td.interfaceType || td.delegateType || !td.name) {
                continue;
            }
            const std::string_view modName = td.moduleName ? std::string_view(td.moduleName) : std::string_view{};
            if (IsStdlibModuleName(modName)) {
                continue;
            }
            if (IsAotTypeDerivedFrom(*aotDescriptor_, &td, "Window") ||
                IsAotTypeDerivedFrom(*aotDescriptor_, &td, "UserControl") ||
                IsAotTypeDerivedFrom(*aotDescriptor_, &td, "Page")) {
                return instantiateAotType(td);
            }
        }
    }

    if (world_.Root().IsValid()) {
        return true;
    }

    lastError_ = entryHint.empty()
        ? "Could not auto-discover UI entry function or Window subclass"
        : ("Could not instantiate UI entry '" + std::string(entryHint) + "'");
    return false;
}

bool AeroAppHost::MountScriptRootObject(ObjectRef candidate) {
    if (!candidate.valid()) {
        lastError_ = "MountScriptRootObject: invalid ObjectRef";
        return false;
    }

    ObjectRef uiRoot = candidate;
    // Unwrap Application -> MainWindow if caller returned an Application instance
    {
        UnifiedValue mainWinVal{};
        if (ReadMemberFromHeap(
                domains_,
                programImage_.get(),
                interpreter_.get(),
                aotDescriptor_,
                aotContext_.get(),
                candidate,
                "MainWindow",
                mainWinVal)) {
            if (const auto* winRef = std::get_if<ObjectRef>(&mainWinVal); winRef && winRef->valid()) {
                uiRoot = *winRef;
            }
        }
    }

    const bool trace = std::getenv("AERO_TRACE") != nullptr;
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] %s\n", msg);
            std::fflush(stderr);
        }
    };
    logTrace("MountScriptRootObject: start");

    rootObject_ = uiRoot;
    rootObjectPin_ = domains_.uiHeap().retain(rootObject_);

    // Show Window (or ApplyTemplate + EnsureLoadedSubtree for UserControl/Page/UIElement)
    const SymbolId typeId = domains_.uiHeap().objectTypeIdFast(rootObject_);
    bool invokedShow = false;
    if (typeId != 0u) {
        SymbolId showSym = 0u;
        if (programImage_) {
            showSym = FindBytecodeClassMethod(*programImage_, typeId, "Show", 0u);
            if (showSym == 0u && symbols_.windowShow != 0u) {
                const rs_sem::TypeSymbol* rt = programImage_->resolveType(typeId);
                if (rt && IsBytecodeTypeDerivedFrom(*programImage_, rt, "Window")) {
                    showSym = symbols_.windowShow;
                }
            }
        } else if (aotDescriptor_) {
            showSym = FindAotClassMethod(*aotDescriptor_, typeId, "Show", 1u);
            if (showSym == 0u && symbols_.windowShow != 0u) {
                const rs_aot::TypeDescriptor* rt = FindAotTypeById(*aotDescriptor_, typeId);
                if (rt && IsAotTypeDerivedFrom(*aotDescriptor_, rt, "Window")) {
                    showSym = symbols_.windowShow;
                }
            }
        }
        logTrace("MountScriptRootObject: invoking Show");
        if (showSym != 0u) {
            invokedShow = InvokeSymbol(showSym, {UnifiedValue{rootObject_}}, nullptr, DomainKind::UiPresentation);
        }
        if (!invokedShow) {
            logTrace("MountScriptRootObject: invoking ApplyTemplate/EnsureLoadedSubtree");
            SymbolId tplSym = 0u;
            SymbolId loadSym = 0u;
            if (programImage_) {
                tplSym = FindBytecodeClassMethod(*programImage_, typeId, "ApplyTemplate", 0u);
                if (tplSym == 0u) tplSym = symbols_.applyTemplate;
                loadSym = FindBytecodeClassMethod(*programImage_, typeId, "EnsureLoadedSubtree", 0u);
                if (loadSym == 0u) loadSym = symbols_.ensureLoadedSubtree;
            } else if (aotDescriptor_) {
                tplSym = FindAotClassMethod(*aotDescriptor_, typeId, "ApplyTemplate", 1u);
                if (tplSym == 0u) tplSym = symbols_.applyTemplate;
                loadSym = FindAotClassMethod(*aotDescriptor_, typeId, "EnsureLoadedSubtree", 1u);
                if (loadSym == 0u) loadSym = symbols_.ensureLoadedSubtree;
            }
            if (tplSym != 0u) {
                (void)InvokeSymbol(tplSym, {UnifiedValue{rootObject_}}, nullptr, DomainKind::UiPresentation);
            }
            if (loadSym != 0u) {
                (void)InvokeSymbol(loadSym, {UnifiedValue{rootObject_}}, nullptr, DomainKind::UiPresentation);
            }
        }
    }

    logTrace("MountScriptRootObject: reading __nativeHandle");
    // Bind native root handle to VisualWorld::SetRoot
    UnifiedValue handleVal{};
    if (!ReadMemberFromHeap(
            domains_,
            programImage_.get(),
            interpreter_.get(),
            aotDescriptor_,
            aotContext_.get(),
            rootObject_,
            "__nativeHandle",
            handleVal)) {
        lastError_ = "Mounted root script object does not expose __nativeHandle";
        return false;
    }

    NativeHandle nh{};
    if (!handleVal.tryGetNativeHandle(nh)) {
        lastError_ = "Mounted root __nativeHandle is not a valid NativeHandle";
        return false;
    }
    const VisualNodeId rootNode = world_.ResolveHandle(nh);
    if (!rootNode.IsValid()) {
        lastError_ = "Mounted root __nativeHandle does not resolve to a live VisualNode (slot=" +
            std::to_string(nh.slot) + ", gen=" + std::to_string(nh.generation) +
            ", reg=" + std::to_string(nh.registryId) + ", tid=" + std::to_string(nh.typeId) +
            ", objTypeId=" + std::to_string(typeId) +
            ", liveNodes=" + std::to_string(world_.LiveNodeCount()) + ")";
        return false;
    }

    world_.SetRoot(rootNode);

    // Optional Dual-Domain SimDeterministic factory discovery (CreateSimState / CreateSim)
    if (!simObject_.valid()) {
        const SymbolId simFnId = programImage_
            ? FindBytecodeFunctionSymbol(*programImage_, "::CreateSimState", true)
            : (aotDescriptor_ ? FindAotFunctionSymbol(*aotDescriptor_, "::CreateSimState", true) : 0u);
        if (simFnId != 0u) {
            UnifiedValue simRes{};
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            const bool okSim = InvokeSymbol(simFnId, {}, &simRes, DomainKind::SimDeterministic);
            domains_.setActiveDomain(DomainKind::UiPresentation);
            if (okSim) {
                if (const auto* simRef = std::get_if<ObjectRef>(&simRes); simRef && simRef->valid()) {
                    simObject_ = *simRef;
                    simObjectPin_ = domains_.simHeap().retain(simObject_);
                    (void)InvokeScriptMethod(
                        rootObject_,
                        "AttachSimState",
                        {UnifiedValue{simObject_}},
                        nullptr,
                        DomainKind::UiPresentation);
                }
            }
        }
    }

    logTrace("MountScriptRootObject: flushing bindings");
    FlushBindings();
    logTrace("MountScriptRootObject: done");
    desktopHost_.NotifyDirtyLayout();
    return true;
}

bool AeroAppHost::HandleScriptInputEvent(
    VisualNodeId nodeId,
    VisualCallbackFlags flag,
    const NativeInputEvent& ev) {
    const VisualNode* node = world_.GetNode(nodeId);
    if (!node || !node->nativeHandle.valid()) {
        return false;
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);

    UnifiedValue res{};
    switch (flag) {
        case VisualCallback_PointerMove:
        case VisualCallback_PointerDown:
        case VisualCallback_PointerUp:
        case VisualCallback_PointerEnter:
        case VisualCallback_PointerLeave:
        case VisualCallback_MouseWheel: {
            if (symbols_.dispatchPointer == 0u) {
                return false;
            }
            const std::vector<UnifiedValue> args{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint32_t>(flag))},
                UnifiedValue{FloatValue{ev.x}},
                UnifiedValue{FloatValue{ev.y}},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint8_t>(ev.button))},
                UnifiedValue{static_cast<std::int64_t>(ev.modifiers)},
                UnifiedValue{FloatValue{ev.deltaX}},
                UnifiedValue{FloatValue{ev.deltaY}},
            };
            if (!InvokeSymbol(symbols_.dispatchPointer, args, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (std::getenv("AERO_TRACE") != nullptr) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    node->hasScriptOwner ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_KeyDown:
        case VisualCallback_KeyUp: {
            if (symbols_.dispatchKey == 0u) {
                return false;
            }
            const std::vector<UnifiedValue> args{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint32_t>(flag))},
                UnifiedValue{static_cast<std::int64_t>(static_cast<std::uint16_t>(ev.key))},
                UnifiedValue{static_cast<std::int64_t>(ev.modifiers)},
            };
            if (!InvokeSymbol(symbols_.dispatchKey, args, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (std::getenv("AERO_TRACE") != nullptr) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    node->hasScriptOwner ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_TextInput: {
            if (symbols_.dispatchText == 0u) {
                return false;
            }
            const std::string text(ev.utf8Bytes.data(), ev.utf8Length);
            const std::vector<UnifiedValue> args{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{StringValue{text}},
            };
            if (!InvokeSymbol(symbols_.dispatchText, args, &res, DomainKind::UiPresentation)) {
                return false;
            }
            bool handled = false;
            const bool gotBool = res.tryGetBool(handled);
            if (std::getenv("AERO_TRACE") != nullptr) {
                std::fprintf(stderr, "[AERO_TRACE] DispatchPointer node=%u flag=%u pos=(%.1f,%.1f) owner=%d handled=%d (gotBool=%d)\n",
                    nodeId.index, static_cast<unsigned>(flag), ev.x, ev.y,
                    node->hasScriptOwner ? 1 : 0, handled ? 1 : 0, gotBool ? 1 : 0);
                std::fflush(stderr);
            }
            return gotBool && handled;
        }
        case VisualCallback_FocusChanged: {
            if (symbols_.dispatchFocus == 0u) {
                return false;
            }
            const std::vector<UnifiedValue> args{
                UnifiedValue{node->nativeHandle},
                UnifiedValue{ev.isDown},
            };
            return InvokeSymbol(symbols_.dispatchFocus, args, nullptr, DomainKind::UiPresentation);
        }
        default:
            return false;
    }
}

Size AeroAppHost::HandleCustomMeasure(
    VisualWorld& world,
    VisualNodeId nodeId,
    Size availSize) {
    const VisualNode* node = world.GetNode(nodeId);
    if (!node) {
        return Size{0.0f, 0.0f};
    }
    if (symbols_.dispatchCustomMeasure != 0u && node->nativeHandle.valid()) {
        const float aw = std::isfinite(availSize.width) ? availSize.width : -1.0f;
        const float ah = std::isfinite(availSize.height) ? availSize.height : -1.0f;
        const std::vector<UnifiedValue> args{
            UnifiedValue{node->nativeHandle},
            UnifiedValue{FloatValue{aw}},
            UnifiedValue{FloatValue{ah}},
        };
        domains_.setActiveDomain(DomainKind::UiPresentation);
        (void)InvokeSymbol(symbols_.dispatchCustomMeasure, args, nullptr, DomainKind::UiPresentation);
    }

    const VisualNode* updated = world.GetNode(nodeId);
    if (!updated) {
        return Size{0.0f, 0.0f};
    }
    const float marginH = std::max(0.0f, updated->layout.margin.left) +
                          std::max(0.0f, updated->layout.margin.right);
    const float marginV = std::max(0.0f, updated->layout.margin.top) +
                          std::max(0.0f, updated->layout.margin.bottom);
    const float padH = std::max(0.0f, updated->layout.padding.left) +
                       std::max(0.0f, updated->layout.padding.right);
    const float padV = std::max(0.0f, updated->layout.padding.top) +
                       std::max(0.0f, updated->layout.padding.bottom);

    auto axisDesired = [](float explicitDim, float minDim, float maxDim, float pad, float avail) noexcept -> float {
        const float lo = std::max(0.0f, std::isfinite(minDim) ? minDim : 0.0f);
        const float hi = (std::isfinite(maxDim) && maxDim >= lo) ? maxDim : 1e9f;
        if (std::isfinite(explicitDim)) {
            return std::clamp(explicitDim, lo, hi);
        }
        const float fallback = std::max(pad, lo);
        if (std::isfinite(avail)) {
            return std::clamp(std::max(fallback, 0.0f), lo, std::min(hi, std::max(0.0f, avail)));
        }
        return std::clamp(fallback, lo, hi);
    };

    const float boxW = axisDesired(
        updated->layout.width,
        updated->layout.minWidth,
        updated->layout.maxWidth,
        padH,
        std::isfinite(availSize.width) ? std::max(0.0f, availSize.width - marginH) : availSize.width);
    const float boxH = axisDesired(
        updated->layout.height,
        updated->layout.minHeight,
        updated->layout.maxHeight,
        padV,
        std::isfinite(availSize.height) ? std::max(0.0f, availSize.height - marginV) : availSize.height);
    return Size{boxW + marginH, boxH + marginV};
}

void AeroAppHost::HandleCustomArrange(
    VisualWorld&,
    VisualNodeId,
    Rect) {}

void AeroAppHost::FlushBindings() {
    if (symbols_.bindingFlush == 0u) {
        return;
    }
    domains_.setActiveDomain(DomainKind::UiPresentation);
    (void)InvokeSymbol(symbols_.bindingFlush, {}, nullptr, DomainKind::UiPresentation);
}

bool AeroAppHost::DispatchPendingCanvasDraws() {
    if (symbols_.dispatchDraw == 0u) {
        return false;
    }
    bool anyDispatched = false;
    const std::size_t slots = world_.ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        const VisualNode* node = world_.NodeAtSlot(s);
        if (!node || !node->nativeHandle.valid()) {
            continue;
        }
        if (node->kind != VisualKind::CanvasDraw && (node->callbackMask & VisualCallback_Draw) == 0u) {
            continue;
        }
        const VisualNodeId nid = node->id;
        const float curW = node->layout.layoutRect.width;
        const float curH = node->layout.layoutRect.height;
        const std::uint64_t key = VisualNodeKey(nid);
        bool sizeChanged = false;
        const auto it = lastCanvasDrawSizes_.find(key);
        if (it == lastCanvasDrawSizes_.end()) {
            sizeChanged = true;
        } else if (std::fabs(it->second.first - curW) > 0.25f || std::fabs(it->second.second - curH) > 0.25f) {
            sizeChanged = true;
        }
        if (node->drawDirty || !drawBuffer_.FindCommands(nid) || sizeChanged) {
            lastCanvasDrawSizes_[key] = {curW, curH};
            domains_.setActiveDomain(DomainKind::UiPresentation);
            (void)InvokeSymbol(symbols_.dispatchDraw, {UnifiedValue{node->nativeHandle}}, nullptr, DomainKind::UiPresentation);
            anyDispatched = true;
        }
    }
    return anyDispatched;
}

void AeroAppHost::SetSimTickCallback(SimTickCallback callback) {
    simTickCallback_ = std::move(callback);
}

bool AeroAppHost::BindSimFloatColumn(
    VisualNodeId nodeId,
    VisualAnimProp targetProp,
    const float* columnData,
    std::size_t rowIndex,
    std::size_t columnCount) {
    return world_.BindEcsColumn(
        nodeId,
        static_cast<std::uint32_t>(targetProp),
        columnData,
        rowIndex,
        columnCount);
}

void AeroAppHost::StepFrame(float dtSeconds, bool forceRenderSync) {
    if (!initialized_) {
        return;
    }
    const bool trace = std::getenv("AERO_TRACE") != nullptr;
    auto logTrace = [&](const char* msg) {
        if (trace) {
            std::fprintf(stderr, "[AERO_TRACE] StepFrame: %s\n", msg);
            std::fflush(stderr);
        }
    };
    const float dt = (dtSeconds > 0.0f && std::isfinite(dtSeconds)) ? dtSeconds : 0.0f;

    // 1. Optional Sim domain tick callback & script SimState advance
    bool simUpdated = false;
    if (simTickCallback_) {
        domains_.setActiveDomain(DomainKind::SimDeterministic);
        simTickCallback_(dt, *this);
        domains_.setActiveDomain(DomainKind::UiPresentation);
        simUpdated = true;
    }
    if (simObject_.valid() && dt > 0.0f) {
        if (InvokeScriptMethod(
                simObject_,
                "AdvanceTick",
                {UnifiedValue{FloatValue{dt}}},
                nullptr,
                DomainKind::SimDeterministic)) {
            simUpdated = true;
        }
    }

    // 2. Cross-domain [SimCommand] queue execution in SimDeterministic domain
    auto flushPendingSim = [&]() -> bool {
        if (domains_.pendingSimCommandCount() == 0u) {
            return false;
        }
        domains_.setActiveDomain(DomainKind::SimDeterministic);
        std::size_t ran = 0u;
        if (simInterpreter_) {
            ran = domains_.flushSimCommands(*simInterpreter_);
        } else if (simAotContext_) {
            ran = domains_.flushSimCommands(*simAotContext_);
        }
        domains_.setActiveDomain(DomainKind::UiPresentation);
        if (ran > 0u) {
            desktopHost_.NotifyDirtyObjects();
            return true;
        }
        return false;
    };
    simUpdated |= flushPendingSim();

    // 3. Queued pointer / keyboard / text input dispatch
    if (inputRouter_.HasPendingEvents()) {
        if (inputRouter_.FlushQueuedEvents() > 0u) {
            desktopHost_.NotifyDirtyObjects();
            desktopHost_.NotifyDirtyLayout();
        }
    }

    // Flush any [SimCommand]s newly queued by UI input handlers in the same frame
    simUpdated |= flushPendingSim();

    if (simUpdated && rootObject_.valid()) {
        (void)InvokeScriptMethod(
            rootObject_, "SyncHudFromSim", {}, nullptr, DomainKind::UiPresentation);
    }

    // 4. Track-B zero-alloc C++ ECS column synchronization (authoritative after script sync)
    if (world_.FlushTrackB() > 0u) {
        desktopHost_.NotifyDirtyObjects();
    }

    // 5. Declarative BindingEngine synchronization (OneWay / TwoWay / Multi)
    logTrace("FlushBindings");
    FlushBindings();

    // 6. VisualAnimationEngine tick + async UI tasks
    (void)domains_.stepAllUiTasks(interpreter_.get());
    if (animEngine_.HasActiveAnimations()) {
        if (animEngine_.Tick(dt, world_)) {
            desktopHost_.NotifyDirtyLayout();
        }
        desktopHost_.SetActiveAnimations(animEngine_.HasActiveAnimations());
    } else {
        desktopHost_.SetActiveAnimations(false);
    }

    // 7. Depth-bucket layout pass (measure/arrange/3D world transforms)
    const float vpW = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportWidth));
    const float vpH = static_cast<float>(std::max<std::uint32_t>(1u, config_.viewportHeight));
    const Size targetViewport{vpW, vpH};
    const bool viewportChanged = (world_.LastLayoutViewport() != targetViewport);
    if (forceRenderSync || viewportChanged || world_.HasAnyDirty()) {
        logTrace("UpdateLayout");
        world_.UpdateLayout(vpW, vpH);
    }

    // 8. Retained CanvasDraw (CustomDrawElement) invalidation pass
    logTrace("DispatchPendingCanvasDraws");
    const bool drawUpdated = DispatchPendingCanvasDraws();

    // 9. PagedRenderTree sync + GPU/Software frame presentation
    const bool needSync = forceRenderSync || drawUpdated || world_.HasAnyDirty() ||
                          (renderTree_.NodeCount() == 0u && world_.Root().IsValid());
    if (needSync) {
        logTrace("TickUi RenderRecord");
        desktopHost_.SetViewportSize(targetViewport);
        desktopHost_.NotifyDirtyLayout();
        desktopHost_.TickUi(0.0f, UiPhase_RenderRecord);
    } else {
        desktopHost_.SetStateFlags(animEngine_.HasActiveAnimations(), false, false, false);
    }

    if (windowHost_.IsInitialized() &&
        (needSync || forceRenderSync || !desktopHost_.CanSleepIdle())) {
        logTrace("TickAndPresentFrame");
        (void)windowHost_.TickAndPresentFrame(0.0f, forceRenderSync || needSync);
    }
    logTrace("done");
}

void AeroAppHost::Tick(float dtSeconds) {
    StepFrame(dtSeconds, false);
}

bool AeroAppHost::RenderHeadlessRgba(
    std::uint32_t width,
    std::uint32_t height,
    std::vector<std::uint8_t>& outRgba) {
    if (width == 0u || height == 0u) {
        return false;
    }
    if (!initialized_ && !InitializeRuntime()) {
        return false;
    }
    if (config_.viewportWidth != width || config_.viewportHeight != height) {
        InjectResize(width, height);
    }
    StepFrame(0.0f, true);

    std::uint32_t capW = 0u;
    std::uint32_t capH = 0u;
    if (windowHost_.IsInitialized() &&
        windowHost_.Width() == width &&
        windowHost_.Height() == height &&
        windowHost_.CaptureFramebufferRgba8(outRgba, capW, capH) &&
        capW == width && capH == height &&
        outRgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return true;
    }

    ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree_, width, height, outRgba, config_.clearColor);
    return outRgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
}

bool AeroAppHost::SaveScreenshotPpm(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height) {
    const std::uint32_t targetW = width > 0u ? width : std::max<std::uint32_t>(1u, config_.viewportWidth);
    const std::uint32_t targetH = height > 0u ? height : std::max<std::uint32_t>(1u, config_.viewportHeight);
    std::vector<std::uint8_t> rgba;
    if (!RenderHeadlessRgba(targetW, targetH, rgba)) {
        return false;
    }
    return WritePpmP6(filePath, targetW, targetH, rgba.data());
}

void AeroAppHost::InjectPointerMove(float x, float y, std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseMove(x, y, modifiers);
    } else {
        (void)inputRouter_.QueuePointerMove(Point{x, y}, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectPointerButton(
    float x,
    float y,
    PointerButton button,
    bool isDown,
    std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseButton(x, y, button, isDown, modifiers);
    } else {
        (void)inputRouter_.QueuePointerButton(Point{x, y}, button, isDown, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectClick(float x, float y, PointerButton button, std::uint8_t modifiers) {
    InjectPointerMove(x, y, modifiers);
    InjectPointerButton(x, y, button, true, modifiers);
    InjectPointerButton(x, y, button, false, modifiers);
}

void AeroAppHost::InjectMouseWheel(
    float x,
    float y,
    float deltaX,
    float deltaY,
    std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformMouseWheel(x, y, deltaX, deltaY, modifiers);
    } else {
        (void)inputRouter_.QueueMouseWheel(Point{x, y}, deltaX, deltaY, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectKey(KeyCode key, bool isDown, std::uint8_t modifiers) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformKey(key, isDown, modifiers);
    } else {
        (void)inputRouter_.QueueKey(key, isDown, modifiers);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectTextInput(std::string_view utf8Text) {
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformTextInput(utf8Text);
    } else {
        (void)inputRouter_.QueueTextInput(utf8Text);
        desktopHost_.NotifyInputEvent();
    }
}

void AeroAppHost::InjectResize(std::uint32_t width, std::uint32_t height) {
    if (width == 0u || height == 0u) {
        return;
    }
    config_.viewportWidth = width;
    config_.viewportHeight = height;
    if (windowHost_.IsInitialized()) {
        windowHost_.OnPlatformResize(width, height);
    } else {
        desktopHost_.SetViewportSize(Size{static_cast<float>(width), static_cast<float>(height)});
        desktopHost_.NotifyDirtyLayout();
    }
}

std::uint64_t AeroAppHost::RunWindowedLoop(std::uint64_t maxFrames) {
    if (!initialized_ && !InitializeRuntime()) {
        return 0u;
    }
    StepFrame(0.0f, true);

    std::uint64_t frames = 0u;
    while (!windowHost_.ShouldClose()) {
        if (maxFrames > 0u && frames >= maxFrames) {
            break;
        }
        const bool idleAllowed = desktopHost_.CanSleepIdle() && !static_cast<bool>(simTickCallback_);
        const std::int32_t waitMs = idleAllowed
            ? (maxFrames > 0u ? 0 : -1)
            : 16;
        if (!windowHost_.PumpEventsAndRender(0.016f, waitMs)) {
            break;
        }

        const std::uint32_t w = std::max<std::uint32_t>(1u, windowHost_.LogicalWidth());
        const std::uint32_t h = std::max<std::uint32_t>(1u, windowHost_.LogicalHeight());
        config_.viewportWidth = w;
        config_.viewportHeight = h;

        bool followUpDirty = false;
        if (simTickCallback_) {
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            simTickCallback_(0.016f, *this);
            domains_.setActiveDomain(DomainKind::UiPresentation);
            followUpDirty = true;
        }
        if (domains_.pendingSimCommandCount() > 0u) {
            domains_.setActiveDomain(DomainKind::SimDeterministic);
            if (simInterpreter_) {
                (void)domains_.flushSimCommands(*simInterpreter_);
            } else if (simAotContext_) {
                (void)domains_.flushSimCommands(*simAotContext_);
            }
            domains_.setActiveDomain(DomainKind::UiPresentation);
            followUpDirty = true;
        }
        if (world_.FlushTrackB() > 0u) {
            followUpDirty = true;
        }
        FlushBindings();
        if (world_.HasAnyDirty()) {
            followUpDirty = true;
        }
        if (followUpDirty) {
            StepFrame(0.0f, false);
        } else if (DispatchPendingCanvasDraws()) {
            StepFrame(0.0f, true);
        }
        ++frames;
    }
    return frames;
}

void AeroAppHost::Shutdown() noexcept {
    if (windowHost_.IsInitialized()) {
        windowHost_.Shutdown();
    }
    ResetLoadedProgramState();
    initialized_ = false;
}

ObjectRef AeroAppHost::RootDataContext() const noexcept {
    if (!rootObject_.valid()) {
        return ObjectRef{};
    }
    UnifiedValue dcVal{};
    if (ReadMemberFromHeap(
            domains_,
            programImage_.get(),
            interpreter_.get(),
            aotDescriptor_,
            aotContext_.get(),
            rootObject_,
            "DataContext",
            dcVal)) {
        if (const auto* dcRef = std::get_if<ObjectRef>(&dcVal); dcRef && dcRef->valid()) {
            return *dcRef;
        }
    }
    return ObjectRef{};
}

bool AeroAppHost::ReadScriptMember(ObjectRef obj, std::string_view dotPath, UnifiedValue& outValue) const {
    if (!obj.valid() || dotPath.empty()) {
        return false;
    }
    ObjectRef cur = obj;
    std::string_view rem = dotPath;
    for (;;) {
        const std::size_t dot = rem.find('.');
        if (dot == std::string_view::npos) {
            return ReadMemberFromHeap(
                domains_,
                programImage_.get(),
                interpreter_.get(),
                aotDescriptor_,
                aotContext_.get(),
                cur,
                rem,
                outValue);
        }
        const std::string_view seg = rem.substr(0u, dot);
        UnifiedValue hopVal{};
        if (!ReadMemberFromHeap(
                domains_,
                programImage_.get(),
                interpreter_.get(),
                aotDescriptor_,
                aotContext_.get(),
                cur,
                seg,
                hopVal)) {
            return false;
        }
        const auto* nextObj = std::get_if<ObjectRef>(&hopVal);
        if (!nextObj || !nextObj->valid()) {
            return false;
        }
        cur = *nextObj;
        rem.remove_prefix(dot + 1u);
    }
}

bool AeroAppHost::InvokeScriptFunction(
    std::string_view qualifiedOrShortName,
    const std::vector<UnifiedValue>& args,
    UnifiedValue* outResult,
    DomainKind domain) {
    SymbolId fnId = 0u;
    if (programImage_) {
        fnId = FindBytecodeFunctionSymbol(*programImage_, qualifiedOrShortName);
    } else if (aotDescriptor_) {
        fnId = FindAotFunctionSymbol(*aotDescriptor_, qualifiedOrShortName);
    }
    if (fnId == 0u) {
        return false;
    }
    const DomainKind prevDomain = domains_.activeDomain();
    domains_.setActiveDomain(domain);
    const bool ok = InvokeSymbol(fnId, args, outResult, domain);
    domains_.setActiveDomain(prevDomain);
    return ok;
}

bool AeroAppHost::InvokeScriptMethod(
    ObjectRef targetObj,
    std::string_view methodName,
    const std::vector<UnifiedValue>& args,
    UnifiedValue* outResult,
    DomainKind domain) {
    if (!targetObj.valid() || methodName.empty()) {
        return false;
    }
    ManagedHeap* heap = domains_.heapForObject(targetObj);
    if (!heap || !heap->isAlive(targetObj)) {
        return false;
    }
    const SymbolId typeId = heap->objectTypeIdFast(targetObj);
    if (typeId == 0u) {
        return false;
    }
    SymbolId fnId = 0u;
    if (programImage_) {
        fnId = FindBytecodeClassMethod(*programImage_, typeId, methodName, args.size());
    } else if (aotDescriptor_) {
        fnId = FindAotClassMethod(
            *aotDescriptor_, typeId, methodName, static_cast<std::uint32_t>(args.size() + 1u));
    }
    if (fnId == 0u) {
        return false;
    }
    std::vector<UnifiedValue> fullArgs;
    fullArgs.reserve(args.size() + 1u);
    fullArgs.push_back(UnifiedValue{targetObj});
    fullArgs.insert(fullArgs.end(), args.begin(), args.end());

    const DomainKind prevDomain = domains_.activeDomain();
    domains_.setActiveDomain(domain);
    const bool ok = InvokeSymbol(fnId, fullArgs, outResult, domain);
    domains_.setActiveDomain(prevDomain);
    return ok;
}

VisualNodeId AeroAppHost::FindVisualByOwnerName(std::string_view elementName) const noexcept {
    if (elementName.empty()) {
        return VisualNodeId{};
    }
    const std::size_t slots = world_.ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        const VisualNode* node = world_.NodeAtSlot(s);
        if (!node || !node->hasScriptOwner || !node->scriptOwner.valid()) {
            continue;
        }
        UnifiedValue nameVal{};
        if (ReadMemberFromHeap(
                domains_,
                programImage_.get(),
                interpreter_.get(),
                aotDescriptor_,
                aotContext_.get(),
                node->scriptOwner,
                "Name",
                nameVal)) {
            std::string nameStr;
            if (ReadScriptStringValue(domains_, nameVal, nameStr) && nameStr == elementName) {
                return node->id;
            }
        }
    }
    return VisualNodeId{};
}

} // namespace aero
