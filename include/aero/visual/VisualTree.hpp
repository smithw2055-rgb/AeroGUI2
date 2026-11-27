#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "aero/drawing/Geometry.hpp"
#include "aero/foundation/ChunkedArena.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

class UIResourceStore;
struct TextLayoutResult;

enum class VisualKind : std::uint8_t {
    Box = 0,
    Text = 1,
    Image = 2,
    Vector = 3,
    CanvasDraw = 4,
};

enum class LayoutMode : std::uint8_t {
    Manual = 0,
    Stack = 1,
    Dock = 2,
    Wrap = 3,
    Grid = 4,
    UniformGrid = 5,
    Canvas = 6,
    ScrollViewport = 7,
    Viewbox = 8,
    CustomCallback = 9,
};

enum class VectorShapeKind : std::uint8_t {
    SvgPath = 0,
    Ellipse = 1,
    Line = 2,
    RadialArc = 3,
    Rectangle = 4,
};

enum VisualCallbackFlags : std::uint32_t {
    VisualCallback_None = 0u,
    VisualCallback_PointerMove = 1u << 0u,
    VisualCallback_PointerDown = 1u << 1u,
    VisualCallback_PointerUp = 1u << 2u,
    VisualCallback_PointerEnter = 1u << 3u,
    VisualCallback_PointerLeave = 1u << 4u,
    VisualCallback_MouseWheel = 1u << 5u,
    VisualCallback_KeyDown = 1u << 6u,
    VisualCallback_KeyUp = 1u << 7u,
    VisualCallback_TextInput = 1u << 8u,
    VisualCallback_FocusChanged = 1u << 9u,
    VisualCallback_CustomMeasure = 1u << 10u,
    VisualCallback_CustomArrange = 1u << 11u,
    VisualCallback_Draw = 1u << 12u,
};

enum VisualDirtyFlags : std::uint8_t {
    VisualDirty_Clean = 0u,
    VisualDirty_Layout = 1u << 0u,
    VisualDirty_Visual = 1u << 1u,
    VisualDirty_Transform = 1u << 2u,
    VisualDirty_Structure = 1u << 3u,
    VisualDirty_All = VisualDirty_Layout | VisualDirty_Visual | VisualDirty_Transform | VisualDirty_Structure,
};

enum class VisualLayoutRectQuery : std::int32_t {
    X = 0,
    Y = 1,
    Width = 2,
    Height = 3,
    DesiredWidth = 4,
    DesiredHeight = 5,
    ScrollExtentWidth = 6,
    ScrollExtentHeight = 7,
    ViewportX = 8,
    ViewportY = 9,
    ViewportWidth = 10,
    ViewportHeight = 11,
};

struct VisualNodeId {
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0u;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return index != kInvalidIndex && generation != 0u;
    }

    friend constexpr bool operator==(VisualNodeId a, VisualNodeId b) noexcept {
        return a.index == b.index && a.generation == b.generation;
    }

    friend constexpr bool operator!=(VisualNodeId a, VisualNodeId b) noexcept {
        return !(a == b);
    }
};

struct VisualGridLength {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
};

struct VisualBoxStyle {
    std::uint32_t bgRgba = 0u;
    std::uint32_t borderRgba = 0u;
    std::int32_t bgGradId = -1;
    std::int32_t baseBgGradId = -1;
    std::int32_t borderGradId = -1;
    std::int32_t baseBorderGradId = -1;
    float bgOpacity = 1.0f;
    bool ownsMutableBgGrad = false;
    bool ownsMutableBorderGrad = false;
    bool isBoxShadow = false;
    Thickness borderThickness{};
    CornerRadius cornerRadius{};
};

struct VisualTransform2D {
    float tx = 0.0f;
    float ty = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    float rotDeg = 0.0f;
    float skewX = 0.0f;
    float skewY = 0.0f;
    float originX = 0.5f;
    float originY = 0.5f;
    BlendingMode blendMode = BlendingMode::Normal;
};

struct VisualLayoutTransform2D {
    float sx = 1.0f;
    float sy = 1.0f;
    float rotDeg = 0.0f;
    float skewX = 0.0f;
    float skewY = 0.0f;

    [[nodiscard]] bool IsIdentity() const noexcept {
        return std::fabs(sx - 1.0f) <= 1e-6f &&
               std::fabs(sy - 1.0f) <= 1e-6f &&
               std::fabs(rotDeg) <= 1e-6f &&
               std::fabs(skewX) <= 1e-6f &&
               std::fabs(skewY) <= 1e-6f;
    }
};

struct VisualTransform3D {
    float rx = 0.0f;
    float ry = 0.0f;
    float rz = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    float sz = 1.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    float camDist = 0.0f;
    bool useExplicitCenter = false;
    bool enabled = false;
};

struct VisualEffectStyle {
    UIEffectKind kind = UIEffectKind::None;
    float radius = 0.0f;
    float depth = 0.0f;
    float dirDeg = 0.0f;
    std::uint32_t colorRgba = 0xFF000000u;
    float opacity = 1.0f;
};

struct VisualOpacityMask {
    bool enabled = false;
    std::int32_t gradId = -1;
    std::uint32_t solidRgba = 0xFFFFFFFFu;
    float opacity = 1.0f;
    std::array<float, 4> stopOffsets{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
};

struct VisualTextSpan {
    std::string text;
    std::string fontFamily;
    float fontSize = 0.0f;
    int fontWeight = 0;
    std::int8_t fontItalic = -1;
    std::uint32_t fgRgba = 0u;
    bool hasFg = false;
    std::int32_t decorations = 0;
};

struct VisualTextData {
    std::string text;
    std::string fontFamily;
    float fontSize = 14.0f;
    std::uint32_t fgRgba = 0xFF000000u;
    std::int32_t fgGradId = -1;
    float fgOpacity = 1.0f;
    int fontWeight = 400;
    bool fontItalic = false;
    TextWrapping wrap = TextWrapping::NoWrap;
    TextAlignment align = TextAlignment::Left;
    TextTrimming trim = TextTrimming::None;
    float lineHeight = 0.0f;
    float charSpacing = 0.0f;
    std::int32_t maxLines = 0;
    std::int32_t decorations = 0;
    std::int32_t selStartCp = 0;
    std::int32_t selLenCp = 0;
    std::int32_t caretCp = -1;
    bool showCaret = false;
    std::uint32_t selRgba = 0x66FF9933u;
    std::uint32_t caretRgba = 0xFF000000u;
    std::vector<VisualTextSpan> richSpans;
    mutable bool hasCachedLayout = false;
    mutable bool cachedRtl = false;
    mutable float cachedMaxW = -1.0f;
    mutable float cachedMaxH = -1.0f;
    mutable std::uint64_t cachedFontRev = 0u;
    mutable TextLayoutResult cachedLayout{};

    void InvalidateLayoutCache() noexcept { hasCachedLayout = false; }
};

struct VisualPlasmaState {
    bool active = false;
    float sx = 64.0f;
    float sy = 64.0f;
    float timeSec = 0.0f;
    float seed = 100.0f;
    float vw = 300.0f;
    float vh = 90.0f;
    std::uint32_t colorRgba = 0xFFFFBC01u;
};

struct VisualRiveState {
    bool active = false;
    std::uint32_t instanceId = 0u;
    std::string assetPath;
    float tLoopSec = 0.0f;
    float tStateSec = 0.0f;
    float reqW = 0.0f;
    float reqH = 0.0f;
    std::uint64_t inputsRevision = 1u;
    std::vector<std::pair<std::string, float>> inputs;
};

struct VisualImageData {
    std::string uri;
    VisualPlasmaState plasma{};
    VisualRiveState rive{};
    Stretch stretch = Stretch::Uniform;
    StretchDirection stretchDir = StretchDirection::Both;
    std::uint32_t tintRgba = 0xFFFFFFFFu;
    float viewboxX = 0.0f;
    float viewboxY = 0.0f;
    float viewboxW = 1.0f;
    float viewboxH = 1.0f;
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportW = 1.0f;
    float viewportH = 1.0f;
    VisualNodeId visualBrushSource{};
    std::uint8_t alignX = 1u;
    std::uint8_t alignY = 1u;
    bool hasExplicitViewport = false;
    bool viewportAbsolute = false;
    bool tileWrap = false;

    [[nodiscard]] bool HasNonVisualBrushSource() const noexcept {
        return !uri.empty() || plasma.active || rive.active;
    }
    [[nodiscard]] bool HasSource() const noexcept {
        return HasNonVisualBrushSource() || visualBrushSource.IsValid();
    }
};

struct VisualVectorData {
    VectorShapeKind shapeKind = VectorShapeKind::SvgPath;
    std::string pathOrParams;
    std::uint32_t fillRgba = 0u;
    std::int32_t fillGradId = -1;
    std::int32_t baseFillGradId = -1;
    std::uint32_t strokeRgba = 0u;
    std::int32_t strokeGradId = -1;
    std::int32_t baseStrokeGradId = -1;
    float fillOpacity = 1.0f;
    float strokeOpacity = 1.0f;
    bool ownsMutableFillGrad = false;
    bool ownsMutableStrokeGrad = false;
    float strokeWidth = 1.0f;
    Stretch stretch = Stretch::Uniform;
    float trimStart = 0.0f;
    float trimEnd = 1.0f;
    std::vector<float> dashArray;
    std::uint8_t dashCap = 0u;
    std::uint8_t startLineCap = 0u;
    std::uint8_t endLineCap = 0u;
    mutable std::uint64_t cachedPathHash = 0u;
    mutable Rect cachedSvgBounds{};
    mutable bool svgBoundsCached = false;
};

struct VisualLayoutData {
    VisualLayoutData() noexcept
        : clipToBounds(false)
        , isEnabled(true)
        , isHitTestVisible(true)
        , isFocusable(false)
        , hasExplicitFlowDirection(false)
        , explicitRtl(false)
        , effectiveRtl(false)
        , hasLayoutTransform(false)
        , hasPerspective3D(false)
        , hasValidMeasure(false)
        , hasValidArrange(false)
        , needsLayoutClip(false)
        , lastMeasureInnerInfW(false)
        , lastMeasureInnerInfH(false)
        , suppressNativePadding(false)
        , suppressNativeBoxVisual(false)
        , includeBorderInPadding(false)
        , hitTestBorderOnly(false) {}

    float width = std::numeric_limits<float>::quiet_NaN();
    float height = std::numeric_limits<float>::quiet_NaN();
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = std::numeric_limits<float>::infinity();
    float maxHeight = std::numeric_limits<float>::infinity();
    Thickness margin{};
    Thickness padding{};
    float opacity = 1.0f;
    std::int32_t zIndex = 0;

    float layoutParam0 = 0.0f;
    float layoutParam1 = 0.0f;
    std::int32_t layoutInt0 = 0;
    std::int32_t layoutInt1 = 0;

    std::int32_t gridRow = 0;
    std::int32_t gridCol = 0;
    std::int32_t gridRowSpan = 1;
    std::int32_t gridColSpan = 1;

    float canvasLeft = std::numeric_limits<float>::quiet_NaN();
    float canvasTop = std::numeric_limits<float>::quiet_NaN();
    float canvasRight = std::numeric_limits<float>::quiet_NaN();
    float canvasBottom = std::numeric_limits<float>::quiet_NaN();
    float scrollOffsetX = 0.0f;
    float scrollOffsetY = 0.0f;

    Size desiredSize{};
    Size untransformedDesiredBox{};
    Size unclampedContentBox{};
    Rect layoutRect{};
    Size scrollExtent{};
    Matrix2D worldTransform{};
    VisualLayoutTransform2D layoutTransform2D{};
    Size lastMeasureConstraint{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    Rect lastArrangeSlot{
        0.0f,
        0.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    float viewboxScaleX = 1.0f;
    float viewboxScaleY = 1.0f;
    float viewboxOriginX = 0.0f;
    float viewboxOriginY = 0.0f;
    std::uint32_t renderNodeId = 0u;
    std::uint32_t renderNodeCount = 0u;
    std::uint16_t treeDepth = 0u;

    HorizontalAlignment hAlign = HorizontalAlignment::Stretch;
    VerticalAlignment vAlign = VerticalAlignment::Stretch;
    Visibility visibility = Visibility::Visible;
    LayoutMode layoutMode = LayoutMode::Manual;
    Dock dock = Dock::Left;

    bool clipToBounds : 1;
    bool isEnabled : 1;
    bool isHitTestVisible : 1;
    bool isFocusable : 1;
    bool hasExplicitFlowDirection : 1;
    bool explicitRtl : 1;
    bool effectiveRtl : 1;
    bool hasLayoutTransform : 1;
    bool hasPerspective3D : 1;
    bool hasValidMeasure : 1;
    bool hasValidArrange : 1;
    bool needsLayoutClip : 1;
    bool lastMeasureInnerInfW : 1;
    bool lastMeasureInnerInfH : 1;
    bool suppressNativePadding : 1;
    bool suppressNativeBoxVisual : 1;
    bool includeBorderInPadding : 1;
    bool hitTestBorderOnly : 1;
};

struct VisualSpatial3DData {
    VisualTransform3D transform3D{};
    Matrix4x4 modelMatrix3D{};
    Matrix4x4 worldMatrix3D{};
    float effectiveCamDist = 0.0f;
};

struct VisualEffectAndMaskData {
    VisualEffectStyle effect{};
    VisualOpacityMask opacityMask{};
};

struct VisualGridDefsData {
    std::vector<VisualGridLength> rowDefs;
    std::vector<VisualGridLength> colDefs;
};

// Slim 48-byte lazy pointer hub (down from 696B monolithic struct: -93.1% per-extras header!)
struct VisualNodeExtras {
    std::unique_ptr<VisualSpatial3DData> spatial3D;
    std::unique_ptr<VisualEffectAndMaskData> effectMask;
    std::unique_ptr<VisualTextData> text;
    std::unique_ptr<VisualImageData> image;
    std::unique_ptr<VisualVectorData> vector;
    std::unique_ptr<VisualGridDefsData> gridDefs;
};

struct VisualNode {
    VisualNode() noexcept
        : alive(false)
        , hasScriptOwner(false)
        , drawDirty(false)
        , inCanvasDrawList(false)
        , inDirtyBindingPropList(false) {}

    VisualNodeId id{};
    VisualNodeId parent{};
    std::vector<VisualNodeId> children;

    NativeHandle nativeHandle{};
    ObjectRef scriptOwner{};
    PersistentRoot pinnedOwnerRoot{};
    std::uint32_t callbackMask = VisualCallback_None;

    VisualLayoutData layout{};
    VisualBoxStyle box{};
    VisualTransform2D transform2D{};
    std::unique_ptr<VisualNodeExtras> extras;
    std::array<float, 4> customScalars{0.0f, 1.0f, 1.0f, 0.0f};

    VisualKind kind = VisualKind::Box;
    std::uint8_t dirtyFlags = VisualDirty_All;
    bool alive : 1;
    bool hasScriptOwner : 1;
    bool drawDirty : 1;
    bool inCanvasDrawList : 1;
    bool inDirtyBindingPropList : 1;

    [[nodiscard]] bool HasExtras() const noexcept { return extras != nullptr; }
    [[nodiscard]] bool HasSpatial3D() const noexcept { return extras && extras->spatial3D != nullptr; }
    [[nodiscard]] bool HasEffectMask() const noexcept { return extras && extras->effectMask != nullptr; }
    [[nodiscard]] bool HasTextData() const noexcept { return extras && extras->text != nullptr; }
    [[nodiscard]] bool HasImageData() const noexcept { return extras && extras->image != nullptr; }
    [[nodiscard]] bool HasVectorData() const noexcept { return extras && extras->vector != nullptr; }
    [[nodiscard]] bool HasGridDefs() const noexcept { return extras && extras->gridDefs != nullptr; }

    [[nodiscard]] VisualNodeExtras& EnsureExtras() {
        if (!extras) extras = std::make_unique<VisualNodeExtras>();
        return *extras;
    }
    [[nodiscard]] VisualSpatial3DData& EnsureSpatial3D() {
        auto& e = EnsureExtras();
        if (!e.spatial3D) e.spatial3D = std::make_unique<VisualSpatial3DData>();
        return *e.spatial3D;
    }
    [[nodiscard]] VisualEffectAndMaskData& EnsureEffectMask() {
        auto& e = EnsureExtras();
        if (!e.effectMask) e.effectMask = std::make_unique<VisualEffectAndMaskData>();
        return *e.effectMask;
    }
    [[nodiscard]] VisualTextData& EnsureTextData() {
        auto& e = EnsureExtras();
        if (!e.text) e.text = std::make_unique<VisualTextData>();
        return *e.text;
    }
    [[nodiscard]] VisualImageData& EnsureImageData() {
        auto& e = EnsureExtras();
        if (!e.image) e.image = std::make_unique<VisualImageData>();
        return *e.image;
    }
    [[nodiscard]] VisualVectorData& EnsureVectorData() {
        auto& e = EnsureExtras();
        if (!e.vector) e.vector = std::make_unique<VisualVectorData>();
        return *e.vector;
    }
    [[nodiscard]] VisualGridDefsData& EnsureGridDefs() {
        auto& e = EnsureExtras();
        if (!e.gridDefs) e.gridDefs = std::make_unique<VisualGridDefsData>();
        return *e.gridDefs;
    }

    [[nodiscard]] static const VisualSpatial3DData& DefaultSpatial3D() noexcept {
        static const VisualSpatial3DData kDefault{};
        return kDefault;
    }
    [[nodiscard]] static const VisualEffectAndMaskData& DefaultEffectMask() noexcept {
        static const VisualEffectAndMaskData kDefault{};
        return kDefault;
    }
    [[nodiscard]] static const VisualTextData& DefaultTextData() noexcept {
        static const VisualTextData kDefault{};
        return kDefault;
    }
    [[nodiscard]] static const VisualImageData& DefaultImageData() noexcept {
        static const VisualImageData kDefault{};
        return kDefault;
    }
    [[nodiscard]] static const VisualVectorData& DefaultVectorData() noexcept {
        static const VisualVectorData kDefault{};
        return kDefault;
    }
    [[nodiscard]] static const VisualGridDefsData& DefaultGridDefs() noexcept {
        static const VisualGridDefsData kDefault{};
        return kDefault;
    }

    [[nodiscard]] const VisualTransform3D& Transform3D() const noexcept {
        return HasSpatial3D() ? extras->spatial3D->transform3D : DefaultSpatial3D().transform3D;
    }
    [[nodiscard]] const VisualEffectStyle& Effect() const noexcept {
        return HasEffectMask() ? extras->effectMask->effect : DefaultEffectMask().effect;
    }
    [[nodiscard]] const VisualOpacityMask& OpacityMask() const noexcept {
        return HasEffectMask() ? extras->effectMask->opacityMask : DefaultEffectMask().opacityMask;
    }
    [[nodiscard]] const VisualTextData& Text() const noexcept {
        return HasTextData() ? *extras->text : DefaultTextData();
    }
    [[nodiscard]] const VisualImageData& Image() const noexcept {
        return HasImageData() ? *extras->image : DefaultImageData();
    }
    [[nodiscard]] const VisualVectorData& Vector() const noexcept {
        return HasVectorData() ? *extras->vector : DefaultVectorData();
    }
    [[nodiscard]] const Matrix4x4& ModelMatrix3D() const noexcept {
        return HasSpatial3D() ? extras->spatial3D->modelMatrix3D : DefaultSpatial3D().modelMatrix3D;
    }
    [[nodiscard]] Thickness EffectivePadding() const noexcept {
        if (layout.suppressNativePadding) return Thickness{};
        if (layout.includeBorderInPadding) {
            return Thickness{
                std::max(0.0f, layout.padding.left) + std::max(0.0f, box.borderThickness.left),
                std::max(0.0f, layout.padding.top) + std::max(0.0f, box.borderThickness.top),
                std::max(0.0f, layout.padding.right) + std::max(0.0f, box.borderThickness.right),
                std::max(0.0f, layout.padding.bottom) + std::max(0.0f, box.borderThickness.bottom)};
        }
        return layout.padding;
    }
    [[nodiscard]] const Matrix4x4& WorldMatrix3D() const noexcept {
        return HasSpatial3D() ? extras->spatial3D->worldMatrix3D : DefaultSpatial3D().worldMatrix3D;
    }
    [[nodiscard]] float EffectiveCamDist() const noexcept {
        return HasSpatial3D() ? extras->spatial3D->effectiveCamDist : 0.0f;
    }
    [[nodiscard]] const std::vector<VisualGridLength>& RowDefs() const noexcept {
        return HasGridDefs() ? extras->gridDefs->rowDefs : DefaultGridDefs().rowDefs;
    }
    [[nodiscard]] const std::vector<VisualGridLength>& ColDefs() const noexcept {
        return HasGridDefs() ? extras->gridDefs->colDefs : DefaultGridDefs().colDefs;
    }
};

struct VisualEcsColumnBinding {
    VisualNodeId targetNode{};
    std::uint32_t targetProp = 0u;
    const float* columnData = nullptr;
    std::size_t rowIndex = 0u;
    std::size_t columnCount = 0u;
    float lastAppliedValue = 0.0f;
    bool hasApplied = false;
};

class VisualWorld final {
public:
    static constexpr const char* kVisualNodeTypeName = "Aero.Visual.VisualNode";

    explicit VisualWorld(
        NativeHandleRegistry* handleRegistry = nullptr,
        UIResourceStore* resourceStore = nullptr,
        ManagedHeap* managedHeap = nullptr);
    ~VisualWorld();

    VisualWorld(const VisualWorld&) = delete;
    VisualWorld& operator=(const VisualWorld&) = delete;
    VisualWorld(VisualWorld&&) = delete;
    VisualWorld& operator=(VisualWorld&&) = delete;

    [[nodiscard]] SymbolId VisualNodeTypeId() const noexcept { return visualNodeTypeId_; }

    void BindRuntime(
        NativeHandleRegistry* handleRegistry,
        ManagedHeap* managedHeap) noexcept;

    void SetResourceStore(UIResourceStore* store) noexcept { resourceStore_ = store; }
    [[nodiscard]] UIResourceStore* ResourceStore() noexcept;
    [[nodiscard]] const UIResourceStore* ResourceStore() const noexcept;

    [[nodiscard]] NativeHandleRegistry& HandleRegistry() noexcept;
    [[nodiscard]] const NativeHandleRegistry& HandleRegistry() const noexcept;

    // Tree & lifecycle
    [[nodiscard]] VisualNodeId CreateNode(VisualKind kind);
    bool DestroyNode(VisualNodeId id);
    [[nodiscard]] NativeHandle HandleFor(VisualNodeId id) const noexcept;
    [[nodiscard]] VisualNodeId ResolveHandle(NativeHandle handle) const noexcept;
    [[nodiscard]] bool IsAlive(VisualNodeId id) const noexcept;

    [[nodiscard]] VisualNode* GetNode(VisualNodeId id) noexcept;
    [[nodiscard]] const VisualNode* GetNode(VisualNodeId id) const noexcept;

    void SetRoot(VisualNodeId id) noexcept;
    [[nodiscard]] VisualNodeId Root() const noexcept { return rootId_; }

    bool AddChild(VisualNodeId parent, VisualNodeId child);
    bool InsertChild(VisualNodeId parent, std::int32_t index, VisualNodeId child);
    bool RemoveChild(VisualNodeId parent, VisualNodeId child);
    bool ClearChildren(VisualNodeId parent);

    bool BindOwner(VisualNodeId id, ObjectRef owner, std::uint32_t callbackMask);
    [[nodiscard]] ObjectRef GetOwner(VisualNodeId id) const noexcept;

    // Lightweight ephemeron object boxes (zero VisualNode allocation)
    [[nodiscard]] NativeHandle CreateObjectBox();
    [[nodiscard]] bool IsObjectBoxHandle(NativeHandle handle) const noexcept;
    bool BindObjectBox(NativeHandle handle, const rs_rt::Value& val, std::uint32_t flags);
    [[nodiscard]] rs_rt::Value GetObjectBoxValue(NativeHandle handle) const noexcept;
    bool DestroyObjectBox(NativeHandle handle) noexcept;
    std::size_t SweepDeadObjectBoxes() noexcept;
    [[nodiscard]] std::size_t LiveObjectBoxCount() const noexcept { return liveObjectBoxCount_; }

    // Geometry & Layout
    bool SetSize(VisualNodeId id, float width, float height) noexcept;
    bool SetMinMaxSize(VisualNodeId id, float minW, float minH, float maxW, float maxH) noexcept;
    bool SetMargin(VisualNodeId id, float l, float t, float r, float b) noexcept;
    bool SetPadding(VisualNodeId id, float l, float t, float r, float b) noexcept;
    bool SetAlignment(VisualNodeId id, HorizontalAlignment hAlign, VerticalAlignment vAlign) noexcept;
    bool SetVisibility(VisualNodeId id, Visibility vis) noexcept;
    bool SetOpacity(VisualNodeId id, float opacity) noexcept;
    bool SetZIndex(VisualNodeId id, std::int32_t zIndex) noexcept;
    bool SetClipToBounds(VisualNodeId id, bool clip) noexcept;
    bool SetInteractiveFlags(VisualNodeId id, bool enabled, bool hitTestVisible, bool focusable) noexcept;
    bool SetHitTestBorderOnly(VisualNodeId id, bool borderOnly) noexcept;
    bool SetLayoutMode(VisualNodeId id, LayoutMode mode, float p0, float p1, std::int32_t i0, std::int32_t i1) noexcept;
    bool GridClearDefs(VisualNodeId id) noexcept;
    bool GridAddRowDef(VisualNodeId id, GridUnitType unit, float value);
    bool GridAddColDef(VisualNodeId id, GridUnitType unit, float value);
    bool SetGridCell(VisualNodeId id, std::int32_t row, std::int32_t col, std::int32_t rowSpan, std::int32_t colSpan) noexcept;
    bool SetDock(VisualNodeId id, Dock dock) noexcept;
    bool SetCanvasOffsets(VisualNodeId id, float l, float t, float r, float b) noexcept;
    bool SetScrollOffsets(VisualNodeId id, float ox, float oy) noexcept;

    [[nodiscard]] float GetLayoutRectScalar(VisualNodeId id, VisualLayoutRectQuery which) const noexcept;
    [[nodiscard]] bool TryScreenToLocal(VisualNodeId id, Point viewportPoint, Point& outLocal) const noexcept;
    [[nodiscard]] Point ScreenToLocal(VisualNodeId id, Point viewportPoint) const noexcept;

    // Appearance, Transforms & Effects
    bool SetBoxStyle(
        VisualNodeId id,
        std::uint32_t bgRgba,
        std::uint32_t borderRgba,
        float btL,
        float btT,
        float btR,
        float btB,
        float crTL,
        float crTR,
        float crBR,
        float crBL) noexcept;
    bool SetBoxGradients(VisualNodeId id, std::int32_t bgGradId, std::int32_t borderGradId) noexcept;
    bool SetBoxBrushOpacity(VisualNodeId id, float bgOpacity) noexcept;
    bool SetBoxShadow(VisualNodeId id, bool isBoxShadow) noexcept;
    bool SetBoxColors(VisualNodeId id, std::uint32_t bgRgba, std::uint32_t borderRgba) noexcept;
    bool SetBorderThickness(VisualNodeId id, float l, float t, float r, float b) noexcept;
    bool SetCornerRadius(VisualNodeId id, float tl, float tr, float br, float bl) noexcept;
    bool SetTransformOrigin(VisualNodeId id, float ox, float oy) noexcept;
    bool SetNodeVisualFlags(
        VisualNodeId id,
        bool suppressPadding,
        bool suppressBoxVisual,
        bool includeBorderInPadding) noexcept;
    [[nodiscard]] float GetNodeGeometryFloat(VisualNodeId id, std::int32_t propCode) const noexcept;
    bool SetTransform2D(
        VisualNodeId id,
        float tx,
        float ty,
        float sx,
        float sy,
        float rotDeg,
        float skewX,
        float skewY,
        float originX,
        float originY) noexcept;
    bool SetLayoutTransform2D(
        VisualNodeId id,
        float sx,
        float sy,
        float rotDeg,
        float skewX,
        float skewY) noexcept;
    bool SetLayoutTransform(
        VisualNodeId id,
        float sx,
        float sy,
        float rotDeg,
        bool enabled) noexcept;
    bool SetTransform3D(
        VisualNodeId id,
        float rx,
        float ry,
        float rz,
        float tx,
        float ty,
        float tz,
        float camDist) noexcept;
    bool SetTransform3DEx(
        VisualNodeId id,
        float rx,
        float ry,
        float rz,
        float tx,
        float ty,
        float tz,
        float sx,
        float sy,
        float sz,
        float cx,
        float cy,
        float cz,
        float camDist,
        bool useExplicitCenter) noexcept;
    bool SetEffect(
        VisualNodeId id,
        UIEffectKind kind,
        float radius,
        float depth,
        float dirDeg,
        std::uint32_t colorRgba,
        float opacity) noexcept;
    bool SetOpacityMask(
        VisualNodeId id,
        bool enabled,
        std::int32_t gradId,
        std::uint32_t solidRgba,
        float opacity) noexcept;
    bool SetBlendMode(VisualNodeId id, BlendingMode mode) noexcept;

    // Text, Image, Vector & Retained Canvas Invalidation
    bool SetText(
        VisualNodeId id,
        std::string_view text,
        std::string_view fontFamily,
        float fontSize,
        std::uint32_t fgRgba,
        TextWrapping wrap,
        TextAlignment align,
        TextTrimming trim);
    bool SetTextMetrics(
        VisualNodeId id,
        float lineHeight,
        float charSpacing,
        std::int32_t maxLines,
        std::int32_t decorations) noexcept;
    bool SetTextStyle(
        VisualNodeId id,
        int fontWeight,
        bool fontItalic,
        std::int32_t fgGradId) noexcept;
    bool SetTextBrushOpacity(VisualNodeId id, float fgOpacity) noexcept;
    bool ClearTextSpans(VisualNodeId id) noexcept;
    bool AddTextSpan(
        VisualNodeId id,
        std::string_view text,
        std::string_view fontFamily,
        float fontSize,
        int fontWeight,
        std::int8_t fontItalic,
        std::uint32_t fgRgba,
        bool hasFg,
        std::int32_t decorations);
    bool SetTextSelection(
        VisualNodeId id,
        std::int32_t selStartCp,
        std::int32_t selLenCp,
        std::int32_t caretCp,
        bool showCaret,
        std::uint32_t selRgba,
        std::uint32_t caretRgba) noexcept;
    [[nodiscard]] const TextLayoutResult* GetOrComputeTextLayout(
        const VisualNode& node,
        float maxWidth,
        float maxHeight = 1000000.0f,
        TextLayoutResult* optScratchFallback = nullptr) const;
    [[nodiscard]] TextLayoutResult LayoutTextNode(const VisualNode& node, float maxWidth) const;
    [[nodiscard]] Size MeasureTextNode(const VisualNode& node, float maxWidth) const;
    [[nodiscard]] Size MeasureText(
        std::string_view text,
        std::string_view fontFamily,
        float fontSize,
        float maxWidth,
        TextWrapping wrap,
        float charSpacing = 0.0f) const;
    [[nodiscard]] std::int32_t HitTestTextCodePoint(VisualNodeId id, float localX, float localY) const noexcept;
    bool SetImage(
        VisualNodeId id,
        std::string_view uri,
        Stretch stretch,
        StretchDirection stretchDir,
        std::uint32_t tintRgba);
    bool SetPlasmaImage(
        VisualNodeId id,
        float sx,
        float sy,
        float timeSec,
        float seed,
        std::uint32_t colorRgba,
        float vw,
        float vh,
        Stretch stretch,
        StretchDirection stretchDir,
        std::uint32_t tintRgba);
    bool SetRiveParams(
        VisualNodeId id,
        std::string_view assetPath,
        std::uint32_t instanceId,
        float tLoopSec,
        float tStateSec,
        float reqW,
        float reqH,
        Stretch stretch,
        std::uint32_t tintRgba);
    bool ClearRiveInputs(VisualNodeId id);
    bool SetRiveInput(VisualNodeId id, std::string_view inputName, float value);
    bool SetImageViewbox(
        VisualNodeId id,
        float vx,
        float vy,
        float vw,
        float vh) noexcept;
    bool SetImageViewport(
        VisualNodeId id,
        float vpx,
        float vpy,
        float vpw,
        float vph,
        bool hasExplicit,
        bool isAbsolute) noexcept;
    bool SetVisualBrushSource(
        VisualNodeId id,
        VisualNodeId sourceId,
        int alignX,
        int alignY) noexcept;
    bool SetImageTileMode(VisualNodeId id, bool tileWrap) noexcept;
    bool SetVectorShape(
        VisualNodeId id,
        VectorShapeKind shapeKind,
        std::string_view pathOrParams,
        std::uint32_t fillRgba,
        std::int32_t fillGradId,
        std::uint32_t strokeRgba,
        std::int32_t strokeGradId,
        float strokeWidth,
        Stretch stretch,
        float trimStart,
        float trimEnd);
    bool SetVectorBrushOpacity(VisualNodeId id, float fillOpacity, float strokeOpacity) noexcept;
    bool SetVectorStrokeDash(
        VisualNodeId id,
        std::string_view dashSpec,
        std::int32_t dashCap,
        std::int32_t startCap,
        std::int32_t endCap);
    bool InvalidateDraw(VisualNodeId id) noexcept;
    [[nodiscard]] const std::vector<VisualNodeId>& CanvasDrawNodes() const noexcept { return canvasDrawNodes_; }

    // Layout & ECS Track B
    using CustomMeasureCallback = std::function<Size(VisualWorld&, VisualNodeId, Size)>;
    using CustomArrangeCallback = std::function<void(VisualWorld&, VisualNodeId, Rect)>;

    void SetCustomLayoutCallbacks(CustomMeasureCallback onMeasure, CustomArrangeCallback onArrange);
    [[nodiscard]] const CustomMeasureCallback& CustomMeasureHook() const noexcept { return customMeasureCallback_; }
    [[nodiscard]] const CustomArrangeCallback& CustomArrangeHook() const noexcept { return customArrangeCallback_; }
    void UpdateLayout(float viewportWidth, float viewportHeight);
    [[nodiscard]] Size LastLayoutViewport() const noexcept { return lastViewportSize_; }
    bool BindEcsColumn(
        VisualNodeId nodeId,
        std::uint32_t targetProp,
        const float* columnData,
        std::size_t rowIndex,
        std::size_t columnCount = 1u);
    void ClearEcsBindings() noexcept;
    std::size_t FlushTrackB();

    void MarkDirty(VisualNodeId id, VisualDirtyFlags flags) noexcept;
    void MarkBindingPropDirty(VisualNodeId id) noexcept;
    [[nodiscard]] std::int32_t BeginConsumeDirtyBindingPropNodes();
    [[nodiscard]] ObjectRef GetConsumedDirtyBindingPropOwner(std::size_t idx) const noexcept;
    [[nodiscard]] bool HasAnyDirty() const noexcept { return anyDirty_; }
    [[nodiscard]] bool HasLayoutOrTransformDirty() const noexcept { return layoutOrTransformDirty_; }
    [[nodiscard]] bool IsStructureDirty() const noexcept { return structureDirty_; }
    void MarkStructureDirty() noexcept { structureDirty_ = true; layoutOrTransformDirty_ = true; ++structureVersion_; anyDirty_ = true; }
    [[nodiscard]] std::int32_t LayoutSizeEpoch() const noexcept { return layoutSizeEpoch_; }
    void BumpLayoutSizeEpoch() noexcept { layoutSizeEpoch_ = (layoutSizeEpoch_ >= 1000000000) ? 1 : (layoutSizeEpoch_ + 1); }
    [[nodiscard]] std::uint64_t WorldInstanceId() const noexcept { return worldInstanceId_; }
    [[nodiscard]] std::uint64_t StructureVersion() const noexcept { return structureVersion_; }
    [[nodiscard]] const std::vector<VisualNodeId>& DirtyNodes() const noexcept { return dirtyNodes_; }
    void ClearDirtyFlags() noexcept;
    void Clear() noexcept;

    void MarkScriptRootsInPools() const noexcept;
    void RefreshScriptOwnerPins() noexcept;

    [[nodiscard]] std::size_t LiveNodeCount() const noexcept { return liveCount_; }
    [[nodiscard]] std::uint64_t DestroyedNodeCount() const noexcept { return destroyedCount_; }
    [[nodiscard]] std::size_t ArenaSlotCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t EstimatedMemoryBytes() const noexcept;
    [[nodiscard]] VisualNode* NodeAtSlot(std::size_t slot) noexcept;
    [[nodiscard]] const VisualNode* NodeAtSlot(std::size_t slot) const noexcept;

private:
    struct ObjectBoxSlot {
        bool alive = false;
        std::uint32_t index = 0u;
        std::uint32_t generation = 0u;
        NativeHandle nativeHandle{};
        rs_rt::Value value{};
        ObjectRef ownerBox{};
        PersistentRoot pinnedValueRoot{};
    };

    [[nodiscard]] ObjectBoxSlot* ResolveObjectBox(NativeHandle handle) noexcept;
    [[nodiscard]] const ObjectBoxSlot* ResolveObjectBox(NativeHandle handle) const noexcept;
    void RefreshObjectBoxPin(ObjectBoxSlot& slot) noexcept;

    void MarkAncestorsDirty(VisualNodeId id, VisualDirtyFlags flags) noexcept;
    void DetachFromParent(VisualNode& child) noexcept;
    void DestroySubtreeDetached(VisualNodeId id) noexcept;
    void SyncCanvasDrawRegistration(VisualNode& node) noexcept;
    void ReleaseNodeMutableGradients(VisualNode& node) noexcept;
    void RefreshOwnerPin(VisualNode& node) noexcept;
    [[nodiscard]] bool WouldCreateCycle(VisualNodeId parentId, VisualNodeId childId) const noexcept;
    void RefreshSubtreeDepths(VisualNodeId id, std::uint16_t depth) noexcept;

    SymbolId visualNodeTypeId_ = 0u;
    SymbolId objectBoxTypeId_ = 0u;
    NativeHandleRegistry ownedFallbackRegistry_;
    NativeHandleRegistry* handleRegistry_ = nullptr;
    UIResourceStore* resourceStore_ = nullptr;
    ManagedHeap* managedHeap_ = nullptr;

    ChunkedArena<VisualNode, 256> nodes_;
    std::vector<std::uint32_t> freeIndices_;
    ChunkedArena<ObjectBoxSlot, 256> objectBoxes_;
    std::vector<std::uint32_t> freeObjectBoxIndices_;
    std::size_t liveObjectBoxCount_ = 0u;
    std::vector<VisualNodeId> canvasDrawNodes_;
    mutable std::vector<std::pair<VisualNodeId, std::uint16_t>> depthRefreshStack_;
    VisualNodeId rootId_{};
    std::size_t liveCount_ = 0u;
    std::uint64_t destroyedCount_ = 0u;
    std::uint64_t worldInstanceId_ = 0u;
    std::uint64_t structureVersion_ = 1u;
    std::int32_t layoutSizeEpoch_ = 1;
    bool anyDirty_ = false;
    bool layoutOrTransformDirty_ = true;
    bool structureDirty_ = true;
    std::vector<VisualNodeId> dirtyNodes_;
    std::vector<VisualNodeId> dirtyBindingPropNodes_;
    std::vector<VisualNodeId> consumedDirtyBindingPropNodes_;
    bool dirtyBindingPropOverflow_ = false;
    std::vector<VisualEcsColumnBinding> ecsBindings_;

    Size lastViewportSize_{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    VisualNodeId lastLaidOutRoot_{};
    std::vector<std::vector<VisualNodeId>> measureDepthBuckets_;
    std::vector<std::vector<VisualNodeId>> arrangeDepthBuckets_;
    std::vector<std::vector<VisualNodeId>> transformDepthBuckets_;
    CustomMeasureCallback customMeasureCallback_{};
    CustomArrangeCallback customArrangeCallback_{};
};

} // namespace aero
