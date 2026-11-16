#include "aero/visual/VisualHostBindings.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"

namespace aero {

namespace {

[[nodiscard]] VisualNodeId ArgNode(rs_rt::ValueSpan args, std::size_t idx, const VisualWorld& world) noexcept {
    if (idx >= args.size()) return VisualNodeId{};
    rs_rt::NativeHandle nh{};
    if (!args[idx].tryGetNativeHandle(nh)) return VisualNodeId{};
    return world.ResolveHandle(nh);
}

[[nodiscard]] float ArgFloat(rs_rt::ValueSpan args, std::size_t idx, float def = 0.0f) noexcept {
    if (idx >= args.size()) return def;
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) return fv;
    double dv = 0.0;
    if (args[idx].tryGetDouble(dv)) return static_cast<float>(dv);
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) return static_cast<float>(iv);
    return def;
}

[[nodiscard]] std::int64_t ArgInt(rs_rt::ValueSpan args, std::size_t idx, std::int64_t def = 0) noexcept {
    if (idx >= args.size()) return def;
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) return iv;
    rs_rt::EnumValue ev{};
    if (args[idx].tryGetEnum(ev)) return ev.value;
    float fv = 0.0f;
    if (args[idx].tryGetFloat(fv)) return static_cast<std::int64_t>(fv);
    return def;
}

[[nodiscard]] std::uint32_t ArgUInt(rs_rt::ValueSpan args, std::size_t idx, std::uint32_t def = 0u) noexcept {
    if (idx >= args.size()) return def;
    if (const auto* uv = std::get_if<rs_rt::UIntValue>(&args[idx])) {
        return uv->value;
    }
    return static_cast<std::uint32_t>(ArgInt(args, idx, static_cast<std::int64_t>(def)));
}

[[nodiscard]] bool ArgBool(rs_rt::ValueSpan args, std::size_t idx, bool def = false) noexcept {
    if (idx >= args.size()) return def;
    bool bv = false;
    if (args[idx].tryGetBool(bv)) return bv;
    return ArgInt(args, idx, def ? 1 : 0) != 0;
}

[[nodiscard]] std::string ArgString(rs_rt::ValueSpan args, std::size_t idx) {
    if (idx >= args.size()) return {};
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&args[idx])) {
        return sv->str();
    }
    return {};
}

[[nodiscard]] rs_rt::ObjectRef ArgObject(rs_rt::ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) return rs_rt::ObjectRef{};
    if (const auto* ov = std::get_if<rs_rt::ObjectRef>(&args[idx])) {
        return *ov;
    }
    return rs_rt::ObjectRef{};
}

[[nodiscard]] rs_rt::Value MakeFloat(float v) noexcept {
    return rs_rt::Value{rs_rt::FloatValue{v}};
}

[[nodiscard]] rs_rt::Value MakeUInt(std::uint32_t v) noexcept {
    return rs_rt::Value{rs_rt::UIntValue{v}};
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

} // namespace

bool RegisterVisualHostBindings(
    BindingRegistry& bindings,
    VisualWorld& world,
    VisualAnimationEngine& animEngine,
    VisualInputRouter& inputRouter,
    ExecutionDomainContext& domains,
    const ProgramImage* programImage,
    const Interpreter* interpreter,
    rs_aot::ExecutionContext* aotContext,
    UIHostPlatformHooks* platformHooks) {
    static DrawingCommandBuffer sDefaultDrawBuffer;
    return RegisterVisualHostBindings(
        bindings,
        world,
        sDefaultDrawBuffer,
        animEngine,
        inputRouter,
        domains,
        programImage,
        interpreter,
        aotContext,
        platformHooks);
}

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
    (void)domains;
    (void)programImage;
    (void)interpreter;
    (void)aotContext;

    auto bindFn = [&](const char* shortName, auto fn) -> bool {
        const std::string canonical = std::string("Aero.Native::") + shortName;
        return bindings.bind(canonical, std::move(fn), rs_rt::BindingDeterminism::NonDeterministic);
    };

    bool ok = true;

    // Tree & lifecycle
    ok &= bindFn("Visual_Create", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const auto kind = static_cast<VisualKind>(std::clamp<std::int64_t>(ArgInt(args, 0, 0), 0, 4));
        const VisualNodeId id = world.CreateNode(kind);
        return rs_rt::Value{world.HandleFor(id)};
    });
    ok &= bindFn("Visual_Destroy", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId id = ArgNode(args, 0, world);
        if (id.IsValid()) {
            drawBuffer.ClearNode(id);
            (void)world.DestroyNode(id);
        }
        return std::nullopt;
    });
    ok &= bindFn("Visual_AddChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.AddChild(ArgNode(args, 0, world), ArgNode(args, 1, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_InsertChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.InsertChild(ArgNode(args, 0, world), static_cast<std::int32_t>(ArgInt(args, 1, 0)), ArgNode(args, 2, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_RemoveChild", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.RemoveChild(ArgNode(args, 0, world), ArgNode(args, 1, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_ClearChildren", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.ClearChildren(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_BindOwner", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.BindOwner(ArgNode(args, 0, world), ArgObject(args, 1), static_cast<std::uint32_t>(ArgInt(args, 2, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_GetOwner", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const rs_rt::ObjectRef obj = world.GetOwner(ArgNode(args, 0, world));
        return obj.valid() ? rs_rt::Value{obj} : rs_rt::Value{rs_rt::NullObject{}};
    });

    // Geometry & Layout
    ok &= bindFn("Visual_SetSize", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetSize(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetMinMaxSize", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetMinMaxSize(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetMargin", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetMargin(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetPadding", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetPadding(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetAlignment", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetAlignment(
            ArgNode(args, 0, world),
            static_cast<HorizontalAlignment>(ArgInt(args, 1, 0)),
            static_cast<VerticalAlignment>(ArgInt(args, 2, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetVisibility", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVisibility(ArgNode(args, 0, world), static_cast<Visibility>(ArgInt(args, 1, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetOpacity", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetOpacity(ArgNode(args, 0, world), ArgFloat(args, 1, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetZIndex", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetZIndex(ArgNode(args, 0, world), static_cast<std::int32_t>(ArgInt(args, 1, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetClipToBounds", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetClipToBounds(ArgNode(args, 0, world), ArgBool(args, 1, false));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetInteractiveFlags", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetInteractiveFlags(ArgNode(args, 0, world), ArgBool(args, 1, true), ArgBool(args, 2, true), ArgBool(args, 3, false));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetLayoutMode", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetLayoutMode(
            ArgNode(args, 0, world),
            static_cast<LayoutMode>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            static_cast<std::int32_t>(ArgInt(args, 4, 0)),
            static_cast<std::int32_t>(ArgInt(args, 5, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_GridClearDefs", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridClearDefs(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_GridAddRowDef", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridAddRowDef(ArgNode(args, 0, world), static_cast<GridUnitType>(ArgInt(args, 1, 2)), ArgFloat(args, 2, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Visual_GridAddColDef", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.GridAddColDef(ArgNode(args, 0, world), static_cast<GridUnitType>(ArgInt(args, 1, 2)), ArgFloat(args, 2, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetGridCell", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetGridCell(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, 0)),
            static_cast<std::int32_t>(ArgInt(args, 2, 0)),
            static_cast<std::int32_t>(ArgInt(args, 3, 1)),
            static_cast<std::int32_t>(ArgInt(args, 4, 1)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetDock", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetDock(ArgNode(args, 0, world), static_cast<Dock>(ArgInt(args, 1, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetCanvasOffsets", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetCanvasOffsets(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetScrollOffsets", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetScrollOffsets(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return std::nullopt;
    });
    ok &= bindFn("Visual_GetLayoutRect", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const float val = world.GetLayoutRectScalar(
            ArgNode(args, 0, world),
            static_cast<VisualLayoutRectQuery>(ArgInt(args, 1, 0)));
        return MakeFloat(val);
    });
    ok &= bindFn("Visual_ScreenToLocal", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const Point local = world.ScreenToLocal(ArgNode(args, 0, world), Point{ArgFloat(args, 1), ArgFloat(args, 2)});
        return MakeFloat((ArgInt(args, 3, 0) == 0) ? local.x : local.y);
    });

    // Appearance / Transforms / Effects
    ok &= bindFn("Visual_SetBoxStyle", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxStyle(
            ArgNode(args, 0, world),
            ArgUInt(args, 1),
            ArgUInt(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7),
            ArgFloat(args, 8),
            ArgFloat(args, 9),
            ArgFloat(args, 10));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetBoxGradients", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetBoxGradients(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, -1)),
            static_cast<std::int32_t>(ArgInt(args, 2, -1)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_RegisterLinearGradient", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        GradientBrushDesc desc{};
        desc.kind = GradientBrushKind::Linear;
        desc.startPoint = Point{ArgFloat(args, 0, 0.0f), ArgFloat(args, 1, 0.0f)};
        desc.endPoint = Point{ArgFloat(args, 2, 1.0f), ArgFloat(args, 3, 1.0f)};
        desc.spreadMethod = static_cast<GradientSpreadMethod>(ArgInt(args, 4, 0));
        desc.mappingMode = static_cast<BrushMappingMode>(ArgInt(args, 5, 0));
        ParseStopsSpec(ArgString(args, 6), desc);
        const std::uint32_t id = world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc));
        return rs_rt::Value{static_cast<std::int64_t>(id)};
    });
    ok &= bindFn("Visual_RegisterRadialGradient", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        GradientBrushDesc desc{};
        desc.kind = GradientBrushKind::Radial;
        desc.center = Point{ArgFloat(args, 0, 0.5f), ArgFloat(args, 1, 0.5f)};
        desc.gradientOrigin = Point{ArgFloat(args, 2, 0.5f), ArgFloat(args, 3, 0.5f)};
        desc.radiusX = ArgFloat(args, 4, 0.5f);
        desc.radiusY = ArgFloat(args, 5, 0.5f);
        desc.spreadMethod = static_cast<GradientSpreadMethod>(ArgInt(args, 6, 0));
        desc.mappingMode = static_cast<BrushMappingMode>(ArgInt(args, 7, 0));
        ParseStopsSpec(ArgString(args, 8), desc);
        const std::uint32_t id = world.ResourceStore()->GradientBrushes().RegisterBrush(std::move(desc));
        return rs_rt::Value{static_cast<std::int64_t>(id)};
    });
    ok &= bindFn("Visual_SetTransform2D", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTransform2D(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3, 1.0f),
            ArgFloat(args, 4, 1.0f),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7),
            ArgFloat(args, 8, 0.5f),
            ArgFloat(args, 9, 0.5f));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetTransform3D", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTransform3D(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgFloat(args, 6),
            ArgFloat(args, 7));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetEffect", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetEffect(
            ArgNode(args, 0, world),
            static_cast<UIEffectKind>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgUInt(args, 5),
            ArgFloat(args, 6, 1.0f));
        return std::nullopt;
    });

    // Text / Image / Vector
    ok &= bindFn("Visual_SetText", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetText(
            ArgNode(args, 0, world),
            ArgString(args, 1),
            ArgString(args, 2),
            ArgFloat(args, 3, 14.0f),
            ArgUInt(args, 4, 0xFF000000u),
            static_cast<TextWrapping>(ArgInt(args, 5, 0)),
            static_cast<TextAlignment>(ArgInt(args, 6, 0)),
            static_cast<TextTrimming>(ArgInt(args, 7, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetTextMetrics", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextMetrics(
            ArgNode(args, 0, world),
            ArgFloat(args, 1),
            ArgFloat(args, 2),
            static_cast<std::int32_t>(ArgInt(args, 3, 0)),
            static_cast<std::int32_t>(ArgInt(args, 4, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetTextSelection", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetTextSelection(
            ArgNode(args, 0, world),
            static_cast<std::int32_t>(ArgInt(args, 1, 0)),
            static_cast<std::int32_t>(ArgInt(args, 2, 0)),
            static_cast<std::int32_t>(ArgInt(args, 3, -1)),
            ArgBool(args, 4, false),
            ArgUInt(args, 5),
            ArgUInt(args, 6));
        return std::nullopt;
    });
    ok &= bindFn("Visual_MeasureText", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const Size measured = world.MeasureText(
            ArgString(args, 0),
            ArgString(args, 1),
            ArgFloat(args, 2, 14.0f),
            ArgFloat(args, 3, 1000000.0f),
            static_cast<TextWrapping>(ArgInt(args, 4, 0)));
        return MakeFloat((ArgInt(args, 5, 0) == 0) ? measured.width : measured.height);
    });
    ok &= bindFn("Visual_HitTestTextCp", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t cp = world.HitTestTextCodePoint(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2));
        return rs_rt::Value{static_cast<std::int64_t>(cp)};
    });
    ok &= bindFn("Visual_SetImage", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetImage(
            ArgNode(args, 0, world),
            ArgString(args, 1),
            static_cast<Stretch>(ArgInt(args, 2, 2)),
            static_cast<StretchDirection>(ArgInt(args, 3, 2)),
            ArgUInt(args, 4, 0xFFFFFFFFu));
        return std::nullopt;
    });
    ok &= bindFn("Visual_SetVectorShape", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.SetVectorShape(
            ArgNode(args, 0, world),
            static_cast<VectorShapeKind>(ArgInt(args, 1, 0)),
            ArgString(args, 2),
            ArgUInt(args, 3),
            static_cast<std::int32_t>(ArgInt(args, 4, -1)),
            ArgUInt(args, 5),
            static_cast<std::int32_t>(ArgInt(args, 6, -1)),
            ArgFloat(args, 7, 1.0f),
            static_cast<Stretch>(ArgInt(args, 8, 2)),
            ArgFloat(args, 9, 0.0f),
            ArgFloat(args, 10, 1.0f));
        return std::nullopt;
    });

    // Retained DrawingContext
    ok &= bindFn("Draw_Begin", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.Begin(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Draw_End", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const VisualNodeId id = ArgNode(args, 0, world);
        drawBuffer.End(id);
        world.MarkDirty(id, VisualDirty_Visual);
        return std::nullopt;
    });
    ok &= bindFn("Draw_FillRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), static_cast<std::int32_t>(ArgInt(args, 6, -1)));
        return std::nullopt;
    });
    ok &= bindFn("Draw_StrokeRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Draw_FillRoundedRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillRoundedRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgFloat(args, 6), ArgUInt(args, 7), static_cast<std::int32_t>(ArgInt(args, 8, -1)));
        return std::nullopt;
    });
    ok &= bindFn("Draw_StrokeRoundedRect", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawRoundedRect(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgFloat(args, 6), ArgUInt(args, 7), ArgFloat(args, 8, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Draw_FillEllipse", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.FillEllipse(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), static_cast<std::int32_t>(ArgInt(args, 6, -1)));
        return std::nullopt;
    });
    ok &= bindFn("Draw_StrokeEllipse", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawEllipse(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Draw_Line", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawLine(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgUInt(args, 5), ArgFloat(args, 6, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Draw_Path", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawPath(ArgNode(args, 0, world), ArgString(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4), ArgFloat(args, 5), ArgUInt(args, 6), ArgUInt(args, 7), ArgFloat(args, 8, 1.0f));
        return std::nullopt;
    });
    ok &= bindFn("Draw_Text", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.DrawText(
            ArgNode(args, 0, world),
            ArgString(args, 1),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4),
            ArgFloat(args, 5),
            ArgString(args, 6),
            ArgFloat(args, 7, 14.0f),
            ArgUInt(args, 8, 0xFF000000u),
            static_cast<HorizontalAlignment>(ArgInt(args, 9, 0)),
            static_cast<VerticalAlignment>(ArgInt(args, 10, 0)));
        return std::nullopt;
    });
    ok &= bindFn("Draw_PushClip", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PushClip(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3), ArgFloat(args, 4));
        return std::nullopt;
    });
    ok &= bindFn("Draw_PopClip", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PopClip(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Draw_PushTransform", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PushTransform(ArgNode(args, 0, world), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3, 1.0f), ArgFloat(args, 4, 1.0f), ArgFloat(args, 5));
        return std::nullopt;
    });
    ok &= bindFn("Draw_PopTransform", [&world, &drawBuffer](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        drawBuffer.PopTransform(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Visual_InvalidateDraw", [&world](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        (void)world.InvalidateDraw(ArgNode(args, 0, world));
        return std::nullopt;
    });

    // Animation / Host / Reflection
    ok &= bindFn("Anim_StartFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t tid = animEngine.StartFloat(
            ArgNode(args, 0, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1, 0)),
            ArgFloat(args, 2),
            ArgFloat(args, 3),
            ArgFloat(args, 4, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 5, 2)),
            static_cast<EasingMode>(ArgInt(args, 6, 0)),
            ArgFloat(args, 7, 0.0f),
            ArgBool(args, 8, false),
            static_cast<std::int32_t>(ArgInt(args, 9, 1)));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StartColor", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t tid = animEngine.StartColor(
            ArgNode(args, 0, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1, 16)),
            ArgUInt(args, 2),
            ArgUInt(args, 3),
            ArgFloat(args, 4, 0.25f),
            static_cast<EasingFunctionKind>(ArgInt(args, 5, 2)),
            static_cast<EasingMode>(ArgInt(args, 6, 0)));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_SpringFloat", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        const std::int32_t tid = animEngine.SpringFloat(
            ArgNode(args, 0, world),
            static_cast<VisualAnimProp>(ArgInt(args, 1, 0)),
            0.0f,
            ArgFloat(args, 2),
            ArgFloat(args, 3, 320.0f),
            ArgFloat(args, 4, 28.0f));
        return rs_rt::Value{static_cast<std::int64_t>(tid)};
    });
    ok &= bindFn("Anim_StopAll", [&world, &animEngine](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        animEngine.StopAll(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Host_SetPointerCapture", [&world, &inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SetPointerCapture(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Host_ReleasePointerCapture", [&world, &inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.ReleasePointerCapture(ArgNode(args, 0, world));
        return std::nullopt;
    });
    ok &= bindFn("Host_SyncImeRect", [&inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SyncImeCaretRect(Rect{ArgFloat(args, 0), ArgFloat(args, 1), ArgFloat(args, 2), ArgFloat(args, 3)}, ArgBool(args, 4, true));
        return std::nullopt;
    });
    ok &= bindFn("Host_GetClipboardText", [&inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{rs_rt::StringValue{inputRouter.GetClipboardText()}};
    });
    ok &= bindFn("Host_SetClipboardText", [&inputRouter](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        inputRouter.SetClipboardText(ArgString(args, 0));
        return std::nullopt;
    });
    ok &= bindFn("Host_RequestWake", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->requestWake) {
            platformHooks->requestWake();
        }
        return std::nullopt;
    });
    ok &= bindFn("Host_CloseWindow", [platformHooks](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        if (platformHooks && platformHooks->closeWindow) {
            platformHooks->closeWindow();
        }
        return std::nullopt;
    });

    // Reflection & color utility stubs
    ok &= bindFn("Reflect_GetPropString", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{rs_rt::StringValue{std::string{}}};
    });
    ok &= bindFn("Reflect_GetPropFloat", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return MakeFloat(0.0f);
    });
    ok &= bindFn("Reflect_GetPropInt", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{static_cast<std::int64_t>(0)};
    });
    ok &= bindFn("Reflect_GetPropBool", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_GetPropObject", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{rs_rt::NullObject{}};
    });
    ok &= bindFn("Reflect_SetPropString", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_SetPropFloat", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_SetPropInt", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_SetPropBool", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_SetPropObject", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_InvokeCommand", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{false};
    });
    ok &= bindFn("Reflect_GetDirtyMask", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return rs_rt::Value{static_cast<std::int64_t>(0)};
    });
    ok &= bindFn("Color_ParseRgba", [](const rs_bc::FunctionReference&, rs_rt::ValueSpan args, rs_rt::RuntimeError&) -> std::optional<rs_rt::Value> {
        return MakeUInt(ParseColorRgba32(ArgString(args, 0)));
    });

    return ok;
}

} // namespace aero
