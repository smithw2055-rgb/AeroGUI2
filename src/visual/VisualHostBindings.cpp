#include "aero/visual/VisualHostBindings.hpp"

#include "aero/asset/AssetUriResolver.hpp"
#include "aero/asset/GradientBrush.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/UIResourceStore.hpp"
#include "aero/runtime/ScriptBackendFacade.hpp"
#include "aero/visual/NativeXmlDomBridge.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace aero {

namespace {

[[nodiscard]] VisualNodeId ArgNode(ValueSpan args, std::size_t idx, const VisualWorld& world) noexcept {
    if (idx >= args.size()) {
        return VisualNodeId{};
    }
    NativeHandle nh{};
    if (!args[idx].tryGetNativeHandle(nh)) {
        return VisualNodeId{};
    }
    return world.ResolveHandle(nh);
}

[[nodiscard]] float ArgFloat(ValueSpan args, std::size_t idx, float def = 0.0f) noexcept {
    if (idx >= args.size()) {
        return def;
    }
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) {
        return fv;
    }
    double dv = 0.0;
    if (args[idx].tryGetDouble(dv)) {
        return static_cast<float>(dv);
    }
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) {
        return static_cast<float>(iv);
    }
    return def;
}

[[nodiscard]] std::int64_t ArgInt(ValueSpan args, std::size_t idx, std::int64_t def = 0) noexcept {
    if (idx >= args.size()) {
        return def;
    }
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) {
        return iv;
    }
    EnumValue ev{};
    if (args[idx].tryGetEnum(ev)) {
        return ev.value;
    }
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) {
        return static_cast<std::int64_t>(fv);
    }
    return def;
}

[[nodiscard]] std::uint32_t ArgUInt(ValueSpan args, std::size_t idx, std::uint32_t def = 0u) noexcept {
    if (idx >= args.size()) {
        return def;
    }
    if (const auto* uv = std::get_if<rs_rt::UIntValue>(&args[idx])) {
        return uv->value;
    }
    return static_cast<std::uint32_t>(ArgInt(args, idx, static_cast<std::int64_t>(def)));
}

[[nodiscard]] bool ArgBool(ValueSpan args, std::size_t idx, bool def = false) noexcept {
    if (idx >= args.size()) {
        return def;
    }
    bool bv = false;
    if (args[idx].tryGetBool(bv)) {
        return bv;
    }
    return ArgInt(args, idx, def ? 1 : 0) != 0;
}

[[nodiscard]] std::string_view ArgStringView(
    const ExecutionDomainContext& domains,
    ValueSpan args,
    std::size_t idx) {
    if (idx >= args.size()) {
        return {};
    }
    if (const auto* sv = std::get_if<StringValue>(&args[idx])) {
        return sv->str();
    }
    if (const auto* ov = std::get_if<ObjectRef>(&args[idx]); ov && ov->valid()) {
        if (const auto view = domains.stringViewAcrossDomains(*ov)) {
            return *view;
        }
    }
    return {};
}

[[nodiscard]] ObjectRef ArgObject(ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) {
        return ObjectRef{};
    }
    if (const auto* ov = std::get_if<ObjectRef>(&args[idx])) {
        if (ov->valid() && ov->kind == ObjectKind::Record) {
            return *ov;
        }
    }
    return ObjectRef{};
}

[[nodiscard]] ObjectRef ArgCollectionOrArray(ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) {
        return ObjectRef{};
    }
    if (const auto* ov = std::get_if<ObjectRef>(&args[idx])) {
        if (ov->valid() && (ov->kind == ObjectKind::Record || ov->kind == ObjectKind::Array)) {
            return *ov;
        }
    }
    return ObjectRef{};
}

[[nodiscard]] UnifiedValue MakeRtFloat(float v) noexcept {
    return UnifiedValue{FloatValue{v}};
}

[[nodiscard]] UnifiedValue MakeUInt(std::uint32_t v) noexcept {
    return UnifiedValue{rs_rt::UIntValue{v}};
}

void ParseStopsSpec(std::string_view spec, GradientBrushDesc& desc) {
    while (!spec.empty()) {
        const std::size_t semi = spec.find(';');
        const std::string_view token = (semi == std::string_view::npos) ? spec : spec.substr(0u, semi);
        if (semi == std::string_view::npos) {
            spec = {};
        } else {
            spec.remove_prefix(semi + 1u);
        }
        const std::size_t colon = token.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        GradientStop stop{};
        stop.offset = str::ParseFloat32Or(token.substr(0u, colon), 0.0f);
        stop.color = ParseColorLiteral(token.substr(colon + 1u));
        desc.stops.push_back(stop);
    }
}

// ============================================================================
// Compile-time Zero-RTTI Typed FFI Unpacker & Declarative Binder
// ============================================================================

struct FfiEnv {
    BindingRegistry& bindings;
    VisualWorld& world;
    ExecutionDomainContext& domains;
};

template <typename T>
[[nodiscard]] T UnpackFfiArg(const FfiEnv& env, ValueSpan args, std::size_t idx) {
    using Decayed = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Decayed, VisualNodeId>) {
        return ArgNode(args, idx, env.world);
    } else if constexpr (std::is_same_v<Decayed, float>) {
        return ArgFloat(args, idx, 0.0f);
    } else if constexpr (std::is_same_v<Decayed, bool>) {
        return ArgBool(args, idx, false);
    } else if constexpr (std::is_same_v<Decayed, std::uint32_t>) {
        return ArgUInt(args, idx, 0u);
    } else if constexpr (std::is_same_v<Decayed, std::int64_t>) {
        return ArgInt(args, idx, 0);
    } else if constexpr (std::is_same_v<Decayed, std::int32_t> || std::is_same_v<Decayed, int>) {
        return static_cast<Decayed>(ArgInt(args, idx, 0));
    } else if constexpr (std::is_same_v<Decayed, std::int8_t>) {
        return static_cast<std::int8_t>(ArgInt(args, idx, 0));
    } else if constexpr (std::is_enum_v<Decayed>) {
        return static_cast<Decayed>(ArgInt(args, idx, 0));
    } else if constexpr (std::is_same_v<Decayed, std::string_view>) {
        return ArgStringView(env.domains, args, idx);
    } else if constexpr (std::is_same_v<Decayed, ObjectRef>) {
        return ArgObject(args, idx);
    } else if constexpr (std::is_same_v<Decayed, UnifiedValue>) {
        return (idx < args.size()) ? args[idx] : UnifiedValue{NullObject{}};
    } else {
        static_assert(!sizeof(Decayed), "Unsupported FFI parameter type");
    }
}

template <typename Ret>
[[nodiscard]] UnifiedValue PackFfiReturn(Ret&& ret) {
    using Decayed = std::remove_cv_t<std::remove_reference_t<Ret>>;
    if constexpr (std::is_same_v<Decayed, UnifiedValue>) {
        return std::forward<Ret>(ret);
    } else if constexpr (std::is_same_v<Decayed, float>) {
        return MakeRtFloat(ret);
    } else if constexpr (std::is_same_v<Decayed, std::uint32_t>) {
        return MakeUInt(ret);
    } else if constexpr (std::is_same_v<Decayed, bool>) {
        return UnifiedValue{ret};
    } else if constexpr (std::is_same_v<Decayed, std::int64_t>) {
        return UnifiedValue{ret};
    } else if constexpr (std::is_same_v<Decayed, std::int32_t> || std::is_same_v<Decayed, int>) {
        return UnifiedValue{static_cast<std::int64_t>(ret)};
    } else if constexpr (std::is_same_v<Decayed, NativeHandle>) {
        return UnifiedValue{ret};
    } else if constexpr (std::is_same_v<Decayed, std::string>) {
        return UnifiedValue{StringValue{std::forward<Ret>(ret)}};
    } else if constexpr (std::is_same_v<Decayed, ObjectRef>) {
        return (ret.valid() && ret.kind == ObjectKind::Record)
            ? UnifiedValue{ret}
            : UnifiedValue{NullObject{}};
    } else {
        static_assert(!sizeof(Decayed), "Unsupported FFI return type");
    }
}

template <typename Callable>
struct CallableTraits : CallableTraits<decltype(&std::remove_reference_t<Callable>::operator())> {};

template <typename ClassType, typename Ret, typename... Args>
struct CallableTraits<Ret (ClassType::*)(Args...) const> {
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
};

template <typename ClassType, typename Ret, typename... Args>
struct CallableTraits<Ret (ClassType::*)(Args...) const noexcept> {
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
};

template <typename ClassType, typename Ret, typename... Args>
struct CallableTraits<Ret (ClassType::*)(Args...)> {
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
};

template <typename ClassType, typename Ret, typename... Args>
struct CallableTraits<Ret (ClassType::*)(Args...) noexcept> {
    using ReturnType = Ret;
    using ArgsTuple = std::tuple<Args...>;
};

template <typename Fn>
bool RegisterRawFn(BindingRegistry& bindings, const char* shortName, bool includeVoidTrampoline, Fn&& fn) {
    const std::string bare(shortName);
    const std::string canonical = "Aero.Native::" + bare;
    bool ok = bindings.bind(canonical, fn, rs_rt::BindingDeterminism::NonDeterministic);
    if (includeVoidTrampoline) {
        const std::string nativeBare = "__rs_native_" + bare;
        const std::string nativeCanonical = "Aero.Native::__rs_native_" + bare;
        ok &= bindings.bind(nativeCanonical, fn, rs_rt::BindingDeterminism::NonDeterministic);
        ok &= bindings.bind(nativeBare, fn, rs_rt::BindingDeterminism::NonDeterministic);
    }
    ok &= bindings.bind(bare, std::forward<Fn>(fn), rs_rt::BindingDeterminism::NonDeterministic);
    return ok;
}

template <typename Callable, typename Tuple, std::size_t... Is>
std::optional<UnifiedValue> InvokeTypedCallableImpl(
    const FfiEnv& env,
    Callable& fn,
    ValueSpan args,
    std::index_sequence<Is...>) {
    using Ret = typename CallableTraits<Callable>::ReturnType;
    if constexpr (std::is_void_v<Ret>) {
        fn(UnpackFfiArg<std::tuple_element_t<Is, Tuple>>(env, args, Is)...);
        return UnifiedValue{std::int64_t{0}};
    } else {
        return PackFfiReturn(fn(UnpackFfiArg<std::tuple_element_t<Is, Tuple>>(env, args, Is)...));
    }
}

template <typename Callable>
bool BindTyped(const FfiEnv& env, const char* shortName, Callable&& callable) {
    using Traits = CallableTraits<Callable>;
    using Ret = typename Traits::ReturnType;
    using Tuple = typename Traits::ArgsTuple;
    VisualWorld* worldPtr = &env.world;
    ExecutionDomainContext* domainsPtr = &env.domains;
    BindingRegistry* regPtr = &env.bindings;
    return RegisterRawFn(
        env.bindings,
        shortName,
        std::is_void_v<Ret>,
        [worldPtr, domainsPtr, regPtr, fn = std::forward<Callable>(callable)](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&) mutable -> std::optional<UnifiedValue> {
            const FfiEnv callEnv{*regPtr, *worldPtr, *domainsPtr};
            return InvokeTypedCallableImpl<decltype(fn), Tuple>(
                callEnv, fn, args, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        });
}

template <typename Obj, typename Ret, typename... Args>
bool BindMemberVoid(const FfiEnv& env, const char* shortName, Obj& target, Ret (Obj::*method)(Args...)) {
    Obj* targetPtr = &target;
    return BindTyped(env, shortName, [targetPtr, method](Args... unpacked) -> void {
        (void)(targetPtr->*method)(unpacked...);
    });
}

template <typename Obj, typename Ret, typename... Args>
bool BindMemberVoid(const FfiEnv& env, const char* shortName, Obj& target, Ret (Obj::*method)(Args...) noexcept) {
    Obj* targetPtr = &target;
    return BindTyped(env, shortName, [targetPtr, method](Args... unpacked) -> void {
        (void)(targetPtr->*method)(unpacked...);
    });
}

template <typename Obj, typename Ret, typename... Args>
bool BindMemberVal(const FfiEnv& env, const char* shortName, const Obj& target, Ret (Obj::*method)(Args...) const noexcept) {
    const Obj* targetPtr = &target;
    return BindTyped(env, shortName, [targetPtr, method](Args... unpacked) -> Ret {
        return (targetPtr->*method)(unpacked...);
    });
}

} // namespace

bool RegisterVisualHostBindings(
    BindingRegistry& bindings,
    VisualWorld& world,
    DrawingCommandBuffer& drawBuffer,
    VisualAnimationEngine& animEngine,
    VisualInputRouter& inputRouter,
    ExecutionDomainContext& domains,
    const ProgramImage* programImage,
    const Interpreter* interpreter,
    rs_aot::ExecutionContext* aotContext,
    UIHostPlatformHooks* platformHooks) {
    const ScriptBackendView backend{
        &domains,
        programImage,
        interpreter,
        aotContext ? &aotContext->program() : nullptr,
        aotContext};
    const FfiEnv env{bindings, world, domains};

    bool ok = true;

    // ------------------------------------------------------------------------
    // 1. Tree & Lifecycle
    // ------------------------------------------------------------------------
    ok &= BindTyped(env, "Visual_Create", [&world](std::int64_t rawKind) -> NativeHandle {
        if (rawKind == -1) {
            return world.CreateObjectBox();
        }
        const auto kind = static_cast<VisualKind>(std::clamp<std::int64_t>(rawKind, 0, 4));
        return world.HandleFor(world.CreateNode(kind));
    });
    ok &= RegisterRawFn(bindings, "Visual_Destroy", true, [&world, &drawBuffer](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        NativeHandle rawNh{};
        if (!args.empty() && args[0].tryGetNativeHandle(rawNh) && world.DestroyObjectBox(rawNh)) {
            return UnifiedValue{std::int64_t{0}};
        }
        const VisualNodeId id = ArgNode(args, 0u, world);
        if (id.IsValid()) {
            std::vector<VisualNodeId> queue{id};
            const std::size_t limit = world.LiveNodeCount() + 1u;
            for (std::size_t qi = 0u; qi < queue.size() && qi < limit; ++qi) {
                const VisualNodeId cur = queue[qi];
                drawBuffer.ClearNode(cur);
                if (const VisualNode* node = world.GetNode(cur)) {
                    for (const VisualNodeId childId : node->children) {
                        if (childId.IsValid()) {
                            queue.push_back(childId);
                        }
                    }
                }
            }
            (void)world.DestroyNode(id);
        }
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= BindMemberVoid(env, "Visual_AddChild", world, &VisualWorld::AddChild);
    ok &= BindMemberVoid(env, "Visual_InsertChild", world, &VisualWorld::InsertChild);
    ok &= BindMemberVoid(env, "Visual_RemoveChild", world, &VisualWorld::RemoveChild);
    ok &= BindMemberVoid(env, "Visual_ClearChildren", world, &VisualWorld::ClearChildren);
    ok &= RegisterRawFn(bindings, "Visual_BindOwner", true, [&world](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        NativeHandle rawNh{};
        if (!args.empty() && args[0].tryGetNativeHandle(rawNh) && world.IsObjectBoxHandle(rawNh)) {
            const UnifiedValue val = (args.size() > 1u) ? args[1] : UnifiedValue{NullObject{}};
            (void)world.BindObjectBox(rawNh, val, static_cast<std::uint32_t>(ArgInt(args, 2u, 0)));
            return UnifiedValue{std::int64_t{0}};
        }
        (void)world.BindOwner(ArgNode(args, 0u, world), ArgObject(args, 1u), static_cast<std::uint32_t>(ArgInt(args, 2u, 0)));
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= RegisterRawFn(bindings, "Visual_GetOwner", false, [&world](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        NativeHandle rawNh{};
        if (!args.empty() && args[0].tryGetNativeHandle(rawNh) && world.IsObjectBoxHandle(rawNh)) {
            return world.GetObjectBoxValue(rawNh);
        }
        return PackFfiReturn(world.GetOwner(ArgNode(args, 0u, world)));
    });

    // ------------------------------------------------------------------------
    // 2. Geometry & Layout
    // ------------------------------------------------------------------------
    ok &= BindMemberVoid(env, "Visual_SetSize", world, &VisualWorld::SetSize);
    ok &= BindMemberVoid(env, "Visual_SetMinMaxSize", world, &VisualWorld::SetMinMaxSize);
    ok &= BindMemberVoid(env, "Visual_SetMargin", world, &VisualWorld::SetMargin);
    ok &= BindMemberVoid(env, "Visual_SetPadding", world, &VisualWorld::SetPadding);
    ok &= BindTyped(env, "Visual_SetAlignment", [&world](VisualNodeId nid, std::int64_t rawH, std::int64_t rawV) {
        (void)world.SetAlignment(
            nid,
            static_cast<HorizontalAlignment>(rawH & 0xFF),
            static_cast<VerticalAlignment>(rawV & 0xFF));
        if (VisualNode* node = world.GetNode(nid)) {
            const bool hasFd = (rawH & 0x100) != 0;
            const bool isRtl = (rawH & 0x200) != 0;
            if (node->layout.hasExplicitFlowDirection != hasFd || node->layout.explicitRtl != isRtl) {
                node->layout.hasExplicitFlowDirection = hasFd;
                node->layout.explicitRtl = isRtl;
                node->layout.hasValidMeasure = false;
                node->layout.hasValidArrange = false;
                world.MarkDirty(nid, VisualDirty_All);
            }
        }
    });
    ok &= BindMemberVoid(env, "Visual_SetVisibility", world, &VisualWorld::SetVisibility);
    ok &= BindMemberVoid(env, "Visual_SetOpacity", world, &VisualWorld::SetOpacity);
    ok &= BindMemberVoid(env, "Visual_SetZIndex", world, &VisualWorld::SetZIndex);
    ok &= BindMemberVoid(env, "Visual_SetClipToBounds", world, &VisualWorld::SetClipToBounds);
    ok &= BindMemberVoid(env, "Visual_SetInteractiveFlags", world, &VisualWorld::SetInteractiveFlags);
    ok &= BindMemberVoid(env, "Visual_SetHitTestBorderOnly", world, &VisualWorld::SetHitTestBorderOnly);
    ok &= BindMemberVoid(env, "Visual_SetLayoutMode", world, &VisualWorld::SetLayoutMode);
    ok &= BindMemberVoid(env, "Visual_GridClearDefs", world, &VisualWorld::GridClearDefs);
    ok &= BindMemberVoid(env, "Visual_GridAddRowDef", world, &VisualWorld::GridAddRowDef);
    ok &= BindMemberVoid(env, "Visual_GridAddColDef", world, &VisualWorld::GridAddColDef);
    ok &= BindMemberVoid(env, "Visual_SetGridCell", world, &VisualWorld::SetGridCell);
    ok &= BindMemberVoid(env, "Visual_SetDock", world, &VisualWorld::SetDock);
    ok &= BindMemberVoid(env, "Visual_SetCanvasOffsets", world, &VisualWorld::SetCanvasOffsets);
    ok &= BindMemberVoid(env, "Visual_SetScrollOffsets", world, &VisualWorld::SetScrollOffsets);
    ok &= BindMemberVal(env, "Visual_GetLayoutRect", world, &VisualWorld::GetLayoutRectScalar);
    ok &= BindTyped(env, "Visual_GetWorldLayoutEpoch", [&world]() -> std::int64_t {
        return static_cast<std::int64_t>(world.LayoutSizeEpoch());
    });
    ok &= BindTyped(env, "Visual_ScreenToLocal", [&world](VisualNodeId n, float sx, float sy, std::int64_t axis) -> float {
        const Point local = world.ScreenToLocal(n, Point{sx, sy});
        return (axis == 0) ? local.x : local.y;
    });

    // ------------------------------------------------------------------------
    // 3. Appearance, Transforms & Effects
    // ------------------------------------------------------------------------
    ok &= BindMemberVoid(env, "Visual_SetBoxStyle", world, &VisualWorld::SetBoxStyle);
    ok &= BindMemberVoid(env, "Visual_SetBoxGradients", world, &VisualWorld::SetBoxGradients);
    ok &= BindMemberVoid(env, "Visual_SetBoxBrushOpacity", world, &VisualWorld::SetBoxBrushOpacity);
    ok &= BindMemberVoid(env, "Visual_SetBoxShadow", world, &VisualWorld::SetBoxShadow);
    ok &= BindMemberVoid(env, "Visual_SetBoxColors", world, &VisualWorld::SetBoxColors);
    ok &= BindMemberVoid(env, "Visual_SetBorderThickness", world, &VisualWorld::SetBorderThickness);
    ok &= BindMemberVoid(env, "Visual_SetCornerRadius", world, &VisualWorld::SetCornerRadius);
    ok &= BindMemberVoid(env, "Visual_SetTransformOrigin", world, &VisualWorld::SetTransformOrigin);
    ok &= BindMemberVoid(env, "Visual_SetNodeVisualFlags", world, &VisualWorld::SetNodeVisualFlags);
    ok &= BindMemberVal(env, "Visual_GetNodeFloat", world, &VisualWorld::GetNodeGeometryFloat);

    ok &= BindTyped(env, "Visual_RegisterLinearGradient",
        [&world](float x0, float y0, float x1, float y1, GradientSpreadMethod spread, BrushMappingMode mapping, std::string_view stopsSpec) -> std::int64_t {
            GradientBrushDesc desc{};
            desc.kind = GradientBrushKind::Linear;
            desc.startPoint = Point{x0, y0};
            desc.endPoint = Point{x1, y1};
            desc.spreadMethod = spread;
            desc.mappingMode = mapping;
            ParseStopsSpec(stopsSpec, desc);
            return static_cast<std::int64_t>(world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc)));
        });
    ok &= BindTyped(env, "Visual_RegisterMutableLinearGradient",
        [&world](std::int64_t rawReuseId, float x0, float y0, float x1, float y1, GradientSpreadMethod spread, BrushMappingMode mapping, std::string_view stopsSpec) -> std::int64_t {
            const std::uint32_t reuseId = (rawReuseId > 0) ? static_cast<std::uint32_t>(rawReuseId) : 0u;
            GradientBrushDesc tempDesc{};
            ParseStopsSpec(stopsSpec, tempDesc);
            return static_cast<std::int64_t>(
                world.ResourceStore()->GradientBrushes().RegisterOrUpdateMutableLinearGradient(
                    reuseId, Point{x0, y0}, Point{x1, y1}, spread, mapping, tempDesc.stops));
        });
    ok &= BindTyped(env, "Visual_ReleaseMutableGradient", [&world](std::int64_t rawId) {
        if (rawId > 0) {
            (void)world.ResourceStore()->GradientBrushes().ReleaseMutableBrush(static_cast<std::uint32_t>(rawId));
        }
    });
    ok &= BindTyped(env, "Visual_RegisterRadialGradient",
        [&world](float cx, float cy, float ox, float oy, float rx, float ry, GradientSpreadMethod spread, BrushMappingMode mapping, std::string_view stopsSpec) -> std::int64_t {
            GradientBrushDesc desc{};
            desc.kind = GradientBrushKind::Radial;
            desc.center = Point{cx, cy};
            desc.gradientOrigin = Point{ox, oy};
            desc.radiusX = rx;
            desc.radiusY = ry;
            desc.spreadMethod = spread;
            desc.mappingMode = mapping;
            ParseStopsSpec(stopsSpec, desc);
            return static_cast<std::int64_t>(world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc)));
        });
    ok &= BindTyped(env, "Visual_RegisterConicGradient",
        [&world](float cx, float cy, GradientSpreadMethod spread, BrushMappingMode mapping, std::string_view stopsSpec) -> std::int64_t {
            GradientBrushDesc desc{};
            desc.kind = GradientBrushKind::Conic;
            desc.center = Point{cx, cy};
            desc.spreadMethod = spread;
            desc.mappingMode = mapping;
            ParseStopsSpec(stopsSpec, desc);
            return static_cast<std::int64_t>(world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc)));
        });

    ok &= BindMemberVoid(env, "Visual_SetTransform2D", world, &VisualWorld::SetTransform2D);
    ok &= BindMemberVoid(env, "Visual_SetLayoutTransform", world, &VisualWorld::SetLayoutTransform);
    ok &= BindMemberVoid(env, "Visual_SetTransform3D", world, &VisualWorld::SetTransform3D);
    ok &= BindMemberVoid(env, "Visual_SetTransform3DEx", world, &VisualWorld::SetTransform3DEx);
    ok &= BindTyped(env, "Visual_SetFocus", [&inputRouter](VisualNodeId nid) {
        (void)inputRouter.SetFocus(nid);
    });
    ok &= BindMemberVoid(env, "Visual_SetEffect", world, &VisualWorld::SetEffect);
    ok &= BindMemberVoid(env, "Visual_SetOpacityMask", world, &VisualWorld::SetOpacityMask);
    ok &= BindTyped(env, "Visual_SetBlendMode", [&world](VisualNodeId n, int rawMode) {
        const BlendingMode mode = (rawMode >= 0 && rawMode <= 3)
            ? static_cast<BlendingMode>(rawMode)
            : BlendingMode::Normal;
        (void)world.SetBlendMode(n, mode);
    });

    // ------------------------------------------------------------------------
    // 4. Text, Image & Vector
    // ------------------------------------------------------------------------
    ok &= BindMemberVoid(env, "Visual_SetText", world, &VisualWorld::SetText);
    ok &= BindMemberVoid(env, "Visual_SetTextMetrics", world, &VisualWorld::SetTextMetrics);
    ok &= BindMemberVoid(env, "Visual_SetTextStyle", world, &VisualWorld::SetTextStyle);
    ok &= BindMemberVoid(env, "Visual_SetTextBrushOpacity", world, &VisualWorld::SetTextBrushOpacity);
    ok &= BindMemberVoid(env, "Visual_ClearTextSpans", world, &VisualWorld::ClearTextSpans);
    ok &= BindMemberVoid(env, "Visual_AddTextSpan", world, &VisualWorld::AddTextSpan);
    ok &= BindMemberVoid(env, "Visual_SetTextSelection", world, &VisualWorld::SetTextSelection);
    ok &= BindTyped(env, "Visual_MeasureText",
        [&world](std::string_view text, std::string_view font, float size, float maxW, TextWrapping wrap, std::int64_t outAxis) -> float {
            const Size measured = world.MeasureText(text, font, size, maxW, wrap);
            return (outAxis == 0) ? measured.width : measured.height;
        });
    ok &= BindMemberVal(env, "Visual_HitTestTextCp", world, &VisualWorld::HitTestTextCodePoint);
    ok &= BindMemberVoid(env, "Visual_SetImage", world, &VisualWorld::SetImage);
    ok &= BindMemberVoid(env, "Visual_SetPlasmaImage", world, &VisualWorld::SetPlasmaImage);
    ok &= BindTyped(env, "Visual_SetRiveParams",
        [&world](VisualNodeId n, std::string_view path, std::int64_t instId, float tLoop, float tState, float reqW, float reqH, Stretch stretch, std::uint32_t tint) {
            (void)world.SetRiveParams(
                n, path, static_cast<std::uint32_t>(std::max<std::int64_t>(0, instId)),
                tLoop, tState, reqW, reqH, stretch, tint);
        });
    ok &= BindMemberVoid(env, "Visual_ClearRiveInputs", world, &VisualWorld::ClearRiveInputs);
    ok &= BindMemberVoid(env, "Visual_SetRiveInput", world, &VisualWorld::SetRiveInput);
    ok &= BindMemberVoid(env, "Visual_SetImageViewbox", world, &VisualWorld::SetImageViewbox);
    ok &= BindMemberVoid(env, "Visual_SetImageViewport", world, &VisualWorld::SetImageViewport);
    ok &= BindMemberVoid(env, "Visual_SetVisualBrushSource", world, &VisualWorld::SetVisualBrushSource);
    ok &= BindMemberVoid(env, "Visual_SetImageTile", world, &VisualWorld::SetImageTileMode);
    ok &= BindMemberVoid(env, "Visual_SetVectorShape", world, &VisualWorld::SetVectorShape);
    ok &= BindMemberVoid(env, "Visual_SetVectorBrushOpacity", world, &VisualWorld::SetVectorBrushOpacity);
    ok &= BindMemberVoid(env, "Visual_SetVectorStrokeDash", world, &VisualWorld::SetVectorStrokeDash);

    // ------------------------------------------------------------------------
    // 5. Retained DrawingContext
    // ------------------------------------------------------------------------
    ok &= BindMemberVoid(env, "Draw_Begin", drawBuffer, &DrawingCommandBuffer::Begin);
    ok &= BindTyped(env, "Draw_End", [&world, &drawBuffer](VisualNodeId id) {
        drawBuffer.End(id);
        world.MarkDirty(id, VisualDirty_Visual);
    });
    ok &= BindMemberVoid(env, "Draw_FillRect", drawBuffer, &DrawingCommandBuffer::FillRect);
    ok &= BindMemberVoid(env, "Draw_StrokeRect", drawBuffer, &DrawingCommandBuffer::DrawRect);
    ok &= BindMemberVoid(env, "Draw_FillRoundedRect", drawBuffer, &DrawingCommandBuffer::FillRoundedRect);
    ok &= BindMemberVoid(env, "Draw_StrokeRoundedRect", drawBuffer, &DrawingCommandBuffer::DrawRoundedRect);
    ok &= BindMemberVoid(env, "Draw_FillEllipse", drawBuffer, &DrawingCommandBuffer::FillEllipse);
    ok &= BindMemberVoid(env, "Draw_StrokeEllipse", drawBuffer, &DrawingCommandBuffer::DrawEllipse);
    ok &= BindMemberVoid(env, "Draw_Line", drawBuffer, &DrawingCommandBuffer::DrawLine);
    ok &= BindMemberVoid(env, "Draw_Path", drawBuffer, &DrawingCommandBuffer::DrawPath);
    ok &= BindMemberVoid(env, "Draw_Text", drawBuffer, &DrawingCommandBuffer::DrawText);
    ok &= BindMemberVoid(env, "Draw_Image", drawBuffer, &DrawingCommandBuffer::DrawImage);
    ok &= BindMemberVoid(env, "Draw_BeginTextRuns", drawBuffer, &DrawingCommandBuffer::BeginTextRuns);
    ok &= BindTyped(env, "Draw_AddTextRunSpan",
        [&drawBuffer](VisualNodeId n, std::int64_t byteOff, std::int64_t byteLen, std::uint32_t rgba) {
            drawBuffer.AddTextRunSpan(
                n,
                static_cast<std::uint32_t>(std::max<std::int64_t>(0, byteOff)),
                static_cast<std::uint32_t>(std::max<std::int64_t>(0, byteLen)),
                rgba);
        });
    ok &= BindTyped(env, "Draw_SyntaxLine",
        [&drawBuffer](VisualNodeId n, std::string_view lineText, float x, float y, float w, float h, std::string_view font, std::int64_t langKind, float size, std::uint32_t rgba) {
            const auto lang = static_cast<SyntaxHighlightLang>(std::clamp<std::int64_t>(langKind, 0, 3));
            drawBuffer.DrawSyntaxLine(n, lineText, x, y, w, h, font, size, lang, rgba);
        });
    ok &= BindTyped(env, "Host_GetImageNaturalSize", [&world](std::string_view uri, std::int64_t axis) -> float {
        if (uri.empty()) {
            return 0.0f;
        }
        UIResourceStore* store = world.ResourceStore() ? world.ResourceStore() : &UIResourceStore::Default();
        const ImageAtlasEntry* entry = store->Images().LoadOrGet(uri);
        if (!entry) {
            return 0.0f;
        }
        return (axis == 0) ? static_cast<float>(entry->width) : static_cast<float>(entry->height);
    });
    ok &= BindMemberVoid(env, "Draw_PushClip", drawBuffer, &DrawingCommandBuffer::PushClip);
    ok &= BindMemberVoid(env, "Draw_PopClip", drawBuffer, &DrawingCommandBuffer::PopClip);
    ok &= BindMemberVoid(env, "Draw_PushTransform", drawBuffer, &DrawingCommandBuffer::PushTransform);
    ok &= BindMemberVoid(env, "Draw_PopTransform", drawBuffer, &DrawingCommandBuffer::PopTransform);
    ok &= BindTyped(env, "Visual_InvalidateDraw", [&world, platformHooks](VisualNodeId n) {
        if (world.InvalidateDraw(n) && platformHooks && platformHooks->requestWake) {
            platformHooks->requestWake();
        }
    });

    // ------------------------------------------------------------------------
    // 6. Animation & Platform Host
    // ------------------------------------------------------------------------
    ok &= RegisterRawFn(bindings, "Anim_StartFloat", false, [&world, &animEngine](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const VisualNodeId nodeId = ArgNode(args, 0u, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1u, 0));
        const float fromVal = ArgFloat(args, 2u);
        const float toVal = ArgFloat(args, 3u);
        const float durationSec = ArgFloat(args, 4u, 0.25f);
        const float delaySec = ArgFloat(args, 7u, 0.0f);
        const std::int32_t tid = animEngine.StartFloat(
            nodeId,
            prop,
            fromVal,
            toVal,
            durationSec,
            static_cast<EasingFunctionKind>(ArgInt(args, 5u, 2)),
            static_cast<EasingMode>(ArgInt(args, 6u, 0)),
            delaySec,
            ArgBool(args, 8u, false),
            static_cast<std::int32_t>(ArgInt(args, 9u, 1)));
        if (tid > 0 && delaySec <= 0.0f && durationSec > 1e-5f) {
            VisualAnimationEngine::ApplyScalarPropImmediate(world, nodeId, prop, fromVal);
        }
        return UnifiedValue{static_cast<std::int64_t>(tid)};
    });
    ok &= RegisterRawFn(bindings, "Anim_StartFloatTo", false, [&world, &animEngine](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const VisualNodeId nodeId = ArgNode(args, 0u, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1u, 0));
        return UnifiedValue{static_cast<std::int64_t>(animEngine.StartFloat(
            nodeId,
            prop,
            VisualAnimationEngine::ReadCurrentScalarProp(world, nodeId, prop),
            ArgFloat(args, 2u),
            ArgFloat(args, 3u, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 4u, 2)),
            static_cast<EasingMode>(ArgInt(args, 5u, 0)),
            ArgFloat(args, 6u, 0.0f),
            ArgBool(args, 7u, false),
            static_cast<std::int32_t>(ArgInt(args, 8u, 1))))};
    });

    auto startColorCommon = [&world, &animEngine](ValueSpan args, bool hasAutoRepeat, bool readCurrentFrom) -> std::int64_t {
        const VisualNodeId nodeId = ArgNode(args, 0u, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1u, 16));
        const std::uint32_t fromRgba = readCurrentFrom
            ? VisualAnimationEngine::ReadCurrentColorProp(world, nodeId, prop)
            : ArgUInt(args, 2u);
        const std::size_t off = readCurrentFrom ? 2u : 3u;
        const std::uint32_t toRgba = ArgUInt(args, off);
        const float durationSec = ArgFloat(args, off + 1u, 0.25f);
        const auto easeKind = static_cast<EasingFunctionKind>(ArgInt(args, off + 2u, 2));
        const auto easeMode = static_cast<EasingMode>(ArgInt(args, off + 3u, 0));
        const float delaySec = ArgFloat(args, off + 4u, 0.0f);
        const bool autoReverse = hasAutoRepeat ? ArgBool(args, off + 5u, false) : false;
        const auto repeatCount = hasAutoRepeat ? static_cast<std::int32_t>(ArgInt(args, off + 6u, 1)) : 1;
        const std::int32_t tid = animEngine.StartColor(
            nodeId, prop, fromRgba, toRgba, durationSec, easeKind, easeMode, delaySec, autoReverse, repeatCount);
        if (!readCurrentFrom && tid > 0 && delaySec <= 0.0f && durationSec > 1e-5f &&
            prop != VisualAnimProp::ManagedCustom) {
            VisualAnimationEngine::ApplyColorPropImmediate(world, nodeId, prop, fromRgba);
        }
        return static_cast<std::int64_t>(tid);
    };
    auto startColorHandler = [startColorCommon](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        return UnifiedValue{startColorCommon(args, false, false)};
    };
    ok &= RegisterRawFn(bindings, "Anim_StartColor", false, startColorHandler);
    ok &= RegisterRawFn(bindings, "Anim_StartColorDelayed", false, startColorHandler);
    ok &= RegisterRawFn(bindings, "Anim_StartColorEx", false, [startColorCommon](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        return UnifiedValue{startColorCommon(args, true, false)};
    });
    ok &= RegisterRawFn(bindings, "Anim_StartColorTo", false, [startColorCommon](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        return UnifiedValue{startColorCommon(args, false, true)};
    });
    ok &= RegisterRawFn(bindings, "Anim_StartColorToEx", false, [startColorCommon](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        return UnifiedValue{startColorCommon(args, true, true)};
    });

    ok &= BindTyped(env, "Anim_ReadCurrentFloat", [&world](VisualNodeId n, VisualAnimProp prop) -> float {
        return VisualAnimationEngine::ReadCurrentScalarProp(world, n, prop);
    });
    ok &= BindTyped(env, "Anim_ReadCurrentColor", [&world](VisualNodeId n, VisualAnimProp prop) -> std::uint32_t {
        return VisualAnimationEngine::ReadCurrentColorProp(world, n, prop);
    });
    ok &= RegisterRawFn(bindings, "Anim_SpringFloat", false, [&world, &animEngine](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const VisualNodeId n = ArgNode(args, 0u, world);
        const auto prop = static_cast<VisualAnimProp>(ArgInt(args, 1u, 0));
        return UnifiedValue{static_cast<std::int64_t>(animEngine.SpringFloat(
            n, prop, VisualAnimationEngine::ReadCurrentScalarProp(world, n, prop),
            ArgFloat(args, 2u), ArgFloat(args, 3u, 320.0f), ArgFloat(args, 4u, 28.0f)))};
    });
    ok &= BindMemberVoid(env, "Anim_StopAll", animEngine, &VisualAnimationEngine::StopAll);
    ok &= BindMemberVoid(env, "Anim_StopTrack", animEngine, &VisualAnimationEngine::StopTrack);
    ok &= RegisterRawFn(bindings, "Anim_SetTrackEasingParams", true, [&animEngine](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        animEngine.SetTrackEasingParams(
            static_cast<std::int32_t>(ArgInt(args, 0u, 0)),
            ArgFloat(args, 1u, 0.0f),
            ArgFloat(args, 2u, 0.0f),
            static_cast<std::uint8_t>(std::clamp<std::int64_t>(ArgInt(args, 3u, 3), 0, 255)));
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= RegisterRawFn(bindings, "Anim_BeginKeyFrames", false, [&world, &animEngine](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        return UnifiedValue{static_cast<std::int64_t>(animEngine.BeginKeyFramesTrack(
            ArgNode(args, 0u, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1u, 0)),
            ArgFloat(args, 2u, 0.0f),
            ArgBool(args, 3u, false),
            static_cast<std::int32_t>(ArgInt(args, 4u, 1))))};
    });
    ok &= BindTyped(env, "Anim_AddKeyFrameFloat",
        [&animEngine](std::int32_t trackId, float timeSec, float value, EasingFunctionKind easeKind, EasingMode easeMode, float p0, float p1, std::int64_t maskRaw, bool discrete) {
            animEngine.AddKeyFrameFloat(
                trackId, timeSec, value, easeKind, easeMode, p0, p1,
                static_cast<std::uint8_t>(std::clamp<std::int64_t>(maskRaw, 0, 255)), discrete);
        });
    ok &= BindTyped(env, "Anim_ApplyFloat", [&world](VisualNodeId n, VisualAnimProp prop, float val) {
        VisualAnimationEngine::ApplyScalarPropImmediate(world, n, prop, val);
    });
    ok &= BindTyped(env, "Anim_ApplyColor", [&world](VisualNodeId n, VisualAnimProp prop, std::uint32_t rgba) {
        VisualAnimationEngine::ApplyColorPropImmediate(world, n, prop, rgba);
    });
    ok &= BindMemberVal(env, "Anim_IsTrackActive", animEngine, &VisualAnimationEngine::HasTrack);
    ok &= BindMemberVal(env, "Anim_IsTrackStarted", animEngine, &VisualAnimationEngine::IsTrackStarted);
    ok &= BindMemberVal(env, "Anim_ReadTrackFloat", animEngine, &VisualAnimationEngine::ReadTrackFloat);
    ok &= BindMemberVal(env, "Anim_ReadTrackColor", animEngine, &VisualAnimationEngine::ReadTrackColor);
    ok &= BindMemberVoid(env, "Anim_MarkTrackScriptSync", animEngine, &VisualAnimationEngine::MarkTrackScriptSync);

    ok &= BindMemberVoid(env, "Host_SetPointerCapture", inputRouter, &VisualInputRouter::SetPointerCapture);
    ok &= BindMemberVoid(env, "Host_ReleasePointerCapture", inputRouter, &VisualInputRouter::ReleasePointerCapture);
    ok &= BindTyped(env, "Host_SyncImeRect", [&inputRouter](float x, float y, float w, float h, bool active) {
        inputRouter.SyncImeCaretRect(Rect{x, y, w, h}, active);
    });
    ok &= BindTyped(env, "Host_GetClipboardText", [&inputRouter]() -> std::string {
        return inputRouter.GetClipboardText();
    });
    ok &= BindMemberVoid(env, "Host_SetClipboardText", inputRouter, &VisualInputRouter::SetClipboardText);
    ok &= BindTyped(env, "Host_RequestWake", [platformHooks]() {
        if (platformHooks && platformHooks->requestWake) {
            platformHooks->requestWake();
        }
    });
    ok &= BindTyped(env, "Host_RequestRenderWake", [platformHooks]() {
        if (platformHooks && platformHooks->requestRenderWake) {
            platformHooks->requestRenderWake();
        } else if (platformHooks && platformHooks->requestWake) {
            platformHooks->requestWake();
        }
    });
    ok &= RegisterRawFn(bindings, "Host_RequestWakeAfterMs", true, [platformHooks](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto delayMs = static_cast<std::int32_t>(std::clamp<std::int64_t>(ArgInt(args, 0u, 16), 1, 60000));
        if (platformHooks) {
            if (platformHooks->requestWakeAfterMs) {
                platformHooks->requestWakeAfterMs(delayMs);
            } else if (platformHooks->requestWake) {
                platformHooks->requestWake();
            }
        }
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= BindTyped(env, "Host_CloseWindow", [platformHooks]() {
        if (platformHooks && platformHooks->closeWindow) {
            platformHooks->closeWindow();
        }
    });
    ok &= BindTyped(env, "Host_PlaySound", [platformHooks](std::string_view uri, float volume) {
        if (platformHooks && platformHooks->playSound) {
            platformHooks->playSound(uri, volume);
        }
    });
    ok &= BindTyped(env, "Host_GetLocalTimePart", [](int part) -> std::int64_t {
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &tt);
#else
        localtime_r(&tt, &localTm);
#endif
        switch (part) {
            case 0: return static_cast<std::int64_t>(localTm.tm_year + 1900);
            case 1: return static_cast<std::int64_t>(localTm.tm_mon + 1);
            case 2: return static_cast<std::int64_t>(localTm.tm_mday);
            case 3: return static_cast<std::int64_t>(localTm.tm_hour);
            case 4: return static_cast<std::int64_t>(localTm.tm_min);
            case 5: return static_cast<std::int64_t>(localTm.tm_sec);
            case 6: {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                return static_cast<std::int64_t>((ms % 1000 + 1000) % 1000);
            }
            default: return 0;
        }
    });

    // ------------------------------------------------------------------------
    // 7. Reflection, Observable Tokens, Assets & UTF-8
    // ------------------------------------------------------------------------
    ok &= RegisterRawFn(bindings, "Reflect_GetPropString", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            if (const auto* sv = std::get_if<StringValue>(&val)) {
                return UnifiedValue{*sv};
            }
            std::string outStr;
            if (TryExtractStringValue(backend, val, outStr)) {
                return UnifiedValue{StringValue{std::move(outStr)}};
            }
        }
        return UnifiedValue{StringValue{std::string{}}};
    });
    ok &= RegisterRawFn(bindings, "Reflect_GetPropFloat", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            float fv = 0.0f;
            if (val.tryGetFloat(fv)) return MakeRtFloat(fv);
            double dv = 0.0;
            if (val.tryGetDouble(dv)) return MakeRtFloat(static_cast<float>(dv));
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return MakeRtFloat(static_cast<float>(iv));
            if (const auto* uv = std::get_if<rs_rt::UIntValue>(&val)) {
                return MakeRtFloat(static_cast<float>(uv->value));
            }
        }
        return MakeRtFloat(0.0f);
    });
    ok &= RegisterRawFn(bindings, "Reflect_GetPropInt", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return UnifiedValue{iv};
            EnumValue ev{};
            if (val.tryGetEnum(ev)) return UnifiedValue{ev.value};
            if (const auto* uv = std::get_if<rs_rt::UIntValue>(&val)) {
                return UnifiedValue{static_cast<std::int64_t>(uv->value)};
            }
            bool bv = false;
            if (val.tryGetBool(bv)) return UnifiedValue{static_cast<std::int64_t>(bv ? 1 : 0)};
            float fv = 0.0f;
            if (val.tryGetFloat(fv)) return UnifiedValue{static_cast<std::int64_t>(fv)};
        }
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= RegisterRawFn(bindings, "Reflect_ResolveEnumOrdinal", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        constexpr std::int64_t kNotFound = std::int64_t{-2147483647};
        std::string pathScratch;
        std::string memberScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        const std::string_view memberName = ExtractStringArgView(backend, args, 2u, memberScratch);
        UnifiedValue val{};
        EnumValue ev{};
        std::int64_t ordinal = kNotFound;
        if (!memberName.empty() &&
            ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val) &&
            val.tryGetEnum(ev) &&
            ResolveEnumOrdinal(backend, ev.typeId, memberName, ordinal)) {
            return UnifiedValue{ordinal};
        }
        return UnifiedValue{kNotFound};
    });
    ok &= RegisterRawFn(bindings, "Reflect_GetPropBool", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            bool bv = false;
            if (val.tryGetBool(bv)) return UnifiedValue{bv};
            std::int64_t iv = 0;
            if (val.tryGetInt64(iv)) return UnifiedValue{iv != 0};
        }
        return UnifiedValue{false};
    });
    ok &= RegisterRawFn(bindings, "Reflect_GetPropObject", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            if (const auto* ov = std::get_if<ObjectRef>(&val);
                ov && ov->valid() && ov->kind == ObjectKind::Record) {
                return UnifiedValue{*ov};
            }
        }
        return UnifiedValue{NullObject{}};
    });
    ok &= RegisterRawFn(bindings, "Reflect_GetPropTypeKind", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        UnifiedValue val{};
        if (!ReadDotPathValue(backend, ArgObject(args, 0u), propPath, val)) {
            return UnifiedValue{std::int64_t{0}};
        }
        switch (val.rawTag()) {
            case ValueTag::Bool: return UnifiedValue{std::int64_t{1}};
            case ValueTag::Byte:
            case ValueTag::SByte:
            case ValueTag::Short:
            case ValueTag::UShort:
            case ValueTag::Int64:
            case ValueTag::UInt:
            case ValueTag::Long:
            case ValueTag::ULong: return UnifiedValue{std::int64_t{2}};
            case ValueTag::Float:
            case ValueTag::Double: return UnifiedValue{std::int64_t{3}};
            case ValueTag::String: return UnifiedValue{std::int64_t{4}};
            case ValueTag::EnumInline:
            case ValueTag::EnumArena: return UnifiedValue{std::int64_t{6}};
            case ValueTag::ObjectRef:
                if (const auto* ov = std::get_if<ObjectRef>(&val); ov && ov->valid()) {
                    return UnifiedValue{std::int64_t{ov->kind == ObjectKind::String ? 4 : 5}};
                }
                return UnifiedValue{std::int64_t{0}};
            default:
                return UnifiedValue{std::int64_t{0}};
        }
    });

    ok &= RegisterRawFn(bindings, "Reflect_SetPropString", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        if (args.size() > 2u) {
            if (const auto* sv = std::get_if<StringValue>(&args[2])) {
                return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, UnifiedValue{*sv})};
            }
        }
        std::string newVal;
        (void)TryExtractStringValue(backend, args.size() > 2u ? args[2] : UnifiedValue{}, newVal);
        return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, UnifiedValue{StringValue{std::move(newVal)}})};
    });
    ok &= RegisterRawFn(bindings, "Reflect_SetPropFloat", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, MakeRtFloat(ArgFloat(args, 2u)))};
    });
    ok &= RegisterRawFn(bindings, "Reflect_SetPropInt", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, UnifiedValue{ArgInt(args, 2u)})};
    });
    ok &= RegisterRawFn(bindings, "Reflect_SetPropBool", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, UnifiedValue{ArgBool(args, 2u)})};
    });
    ok &= RegisterRawFn(bindings, "Reflect_SetPropObject", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        const UnifiedValue objVal = (args.size() > 2u) ? args[2] : UnifiedValue{NullObject{}};
        return UnifiedValue{WriteDotPathValue(backend, ArgObject(args, 0u), propPath, objVal)};
    });

    ok &= RegisterRawFn(bindings, "Reflect_ListSet", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const ObjectRef colObj = ArgCollectionOrArray(args, 0u);
        const std::int64_t rawIdx = ArgInt(args, 1u, -1);
        if (!colObj.valid() || rawIdx < 0) {
            return UnifiedValue{false};
        }
        const UnifiedValue itemVal = (args.size() > 2u) ? args[2] : UnifiedValue{NullObject{}};
        return UnifiedValue{WriteIndexedCollectionElement(backend, colObj, static_cast<std::size_t>(rawIdx), itemVal)};
    });
    ok &= RegisterRawFn(bindings, "Reflect_ListAdd", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const UnifiedValue itemVal = (args.size() > 1u) ? args[1] : UnifiedValue{NullObject{}};
        return UnifiedValue{AppendCollectionElement(backend, ArgCollectionOrArray(args, 0u), itemVal)};
    });
    ok &= RegisterRawFn(bindings, "Reflect_ListInsert", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const std::int64_t rawIdx = ArgInt(args, 1u, -1);
        if (rawIdx < 0) {
            return UnifiedValue{false};
        }
        const UnifiedValue itemVal = (args.size() > 2u) ? args[2] : UnifiedValue{NullObject{}};
        return UnifiedValue{InsertCollectionElement(backend, ArgCollectionOrArray(args, 0u), static_cast<std::size_t>(rawIdx), itemVal)};
    });
    ok &= RegisterRawFn(bindings, "Reflect_ListRemoveAt", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const std::int64_t rawIdx = ArgInt(args, 1u, -1);
        if (rawIdx < 0) {
            return UnifiedValue{false};
        }
        return UnifiedValue{RemoveCollectionElementAt(backend, ArgCollectionOrArray(args, 0u), static_cast<std::size_t>(rawIdx))};
    });

    ok &= RegisterRawFn(bindings, "Reflect_InvokeCommand", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string pathScratch;
        const std::string_view propPath = ExtractStringArgView(backend, args, 1u, pathScratch);
        std::string paramScratch;
        const std::string* paramPtr = &paramScratch;
        if (args.size() > 2u) {
            if (const auto* sv = std::get_if<StringValue>(&args[2])) {
                paramPtr = &sv->str();
            } else {
                (void)TryExtractStringValue(backend, args[2], paramScratch);
            }
        }
        return UnifiedValue{InvokeCommandOrMethodByPath(backend, ArgObject(args, 0u), propPath, paramPtr)};
    });

    ok &= BindTyped(env, "Reflect_GetDirtyMask", [backend](ObjectRef target) -> std::int64_t {
        if (ManagedHeap* heap = ResolveTargetHeap(backend, target)) {
            SymbolId typeId = heap->objectTypeIdFast(target);
            if (typeId == 0u) {
                if (const auto tidOpt = heap->objectTypeId(target)) {
                    typeId = *tidOpt;
                }
            }
            if (typeId == 0u || !backend.domains || !backend.domains->isTypeObservable(typeId)) {
                return -1;
            }
            return static_cast<std::int64_t>(heap->peekMutationVersion(target));
        }
        return -1;
    });
    ok &= BindTyped(env, "Reflect_GetGlobalObservableEpoch", [backend]() -> std::int64_t {
        if (!backend.domains) {
            return 1;
        }
        const std::uint64_t raw = backend.domains->globalObservableEpoch();
        return static_cast<std::int64_t>((raw % 1000000000ULL) + 1ULL);
    });
    ok &= BindTyped(env, "Reflect_GetObjectToken", [backend](ObjectRef target) -> std::int64_t {
        return backend.domains
            ? static_cast<std::int64_t>(backend.domains->getOrAssignObservableToken(target))
            : std::int64_t{0};
    });
    ok &= BindTyped(env, "Reflect_BeginConsumeDirtyTokens", [backend]() -> std::int64_t {
        return backend.domains
            ? static_cast<std::int64_t>(backend.domains->beginConsumeDirtyObservableTokens())
            : std::int64_t{-1};
    });
    ok &= BindTyped(env, "Reflect_GetConsumedDirtyToken", [backend](std::int64_t idx) -> std::int64_t {
        if (!backend.domains || idx < 0) {
            return 0;
        }
        return static_cast<std::int64_t>(backend.domains->getConsumedDirtyObservableToken(static_cast<std::size_t>(idx)));
    });
    ok &= BindTyped(env, "Visual_BeginConsumeDirtyPropNodes", [&world]() -> std::int64_t {
        return static_cast<std::int64_t>(world.BeginConsumeDirtyBindingPropNodes());
    });
    ok &= BindTyped(env, "Visual_GetConsumedDirtyPropOwner", [&world](std::int64_t idx) -> ObjectRef {
        if (idx < 0) {
            return ObjectRef{};
        }
        return world.GetConsumedDirtyBindingPropOwner(static_cast<std::size_t>(idx));
    });

    ok &= RegisterRawFn(bindings, "Color_ParseRgba", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string scratch;
        return MakeUInt(ParseColorRgba32(ExtractStringArgView(backend, args, 0u, scratch)));
    });

    ok &= RegisterRawFn(bindings, "Host_ReadTextAsset", false, [&world, backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string uriScratch;
        const std::string_view rawUri = ExtractStringArgView(backend, args, 0u, uriScratch);

        std::vector<std::string> searchDirs;
        std::vector<std::string> fallbackBaseDirs;
        if (const UIResourceStore* store = world.ResourceStore()) {
            searchDirs = store->AssetSearchDirectoriesSnapshot();
            if (const ImageAtlas* storeAtlas = store->TryImages()) {
                fallbackBaseDirs.push_back(storeAtlas->GetBaseDirectory());
            }
        }
        fallbackBaseDirs.push_back(ImageAtlas::Instance().GetBaseDirectory());

        std::string loadedParentDir;
        std::string text = ReadTextAssetWithProbes(rawUri, searchDirs, fallbackBaseDirs, &loadedParentDir);
        if (!loadedParentDir.empty()) {
            if (UIResourceStore* mutStore = world.ResourceStore()) {
                mutStore->AddAssetSearchDirectory(loadedParentDir);
                mutStore->Images().AddSearchDirectory(loadedParentDir);
            } else {
                ImageAtlas::Instance().AddSearchDirectory(loadedParentDir);
            }
        }
        return UnifiedValue{StringValue{std::move(text)}};
    });

    ok &= RegisterRawFn(bindings, "Utf8_CodePointCount", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string scratch;
        return UnifiedValue{str::CountUtf8CodePoints(ExtractStringArgView(backend, args, 0u, scratch))};
    });
    ok &= RegisterRawFn(bindings, "Utf8_SliceCodePoints", false, [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string scratch;
        const std::string_view sv = ExtractStringArgView(backend, args, 0u, scratch);
        const auto startCp = static_cast<std::int32_t>(std::max<std::int64_t>(0, ArgInt(args, 1u)));
        const auto countCp = static_cast<std::int32_t>(ArgInt(args, 2u));
        return UnifiedValue{StringValue{std::string(str::SliceUtf8CodePoints(sv, startCp, countCp))}};
    });
    ok &= BindTyped(env, "Utf8_FromCodePoint", [](std::int64_t cpRaw) -> std::string {
        return str::Utf8FromCodePoint(cpRaw);
    });

    ok &= RegisterNativeXmlDomBindings(bindings, backend);

    return ok;
}

} // namespace aero
