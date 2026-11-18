#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "aero/drawing/Geometry.hpp"

namespace aero {

class UIResourceStore;

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

struct VisualTransform3D {
    float rx = 0.0f;
    float ry = 0.0f;
    float rz = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    float camDist = 0.0f;
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

struct VisualTextData {
    std::string text;
    std::string fontFamily;
    float fontSize = 14.0f;
    std::uint32_t fgRgba = 0xFF000000u;
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
};

struct VisualImageData {
    std::string uri;
    Stretch stretch = Stretch::Uniform;
    StretchDirection stretchDir = StretchDirection::Both;
    std::uint32_t tintRgba = 0xFFFFFFFFu;
    float viewboxX = 0.0f;
    float viewboxY = 0.0f;
    float viewboxW = 1.0f;
    float viewboxH = 1.0f;
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
};

struct VisualLayoutData {
    float width = std::numeric_limits<float>::quiet_NaN();
    float height = std::numeric_limits<float>::quiet_NaN();
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = std::numeric_limits<float>::infinity();
    float maxHeight = std::numeric_limits<float>::infinity();
    Thickness margin{};
    Thickness padding{};
    HorizontalAlignment hAlign = HorizontalAlignment::Stretch;
    VerticalAlignment vAlign = VerticalAlignment::Stretch;
    Visibility visibility = Visibility::Visible;
    float opacity = 1.0f;
    std::int32_t zIndex = 0;
    bool clipToBounds = false;
    bool isEnabled = true;
    bool isHitTestVisible = true;
    bool isFocusable = false;

    LayoutMode layoutMode = LayoutMode::Manual;
    float layoutParam0 = 0.0f;
    float layoutParam1 = 0.0f;
    std::int32_t layoutInt0 = 0;
    std::int32_t layoutInt1 = 0;

    std::vector<VisualGridLength> rowDefs;
    std::vector<VisualGridLength> colDefs;
    std::int32_t gridRow = 0;
    std::int32_t gridCol = 0;
    std::int32_t gridRowSpan = 1;
    std::int32_t gridColSpan = 1;
    Dock dock = Dock::Left;

    float canvasLeft = std::numeric_limits<float>::quiet_NaN();
    float canvasTop = std::numeric_limits<float>::quiet_NaN();
    float canvasRight = std::numeric_limits<float>::quiet_NaN();
    float canvasBottom = std::numeric_limits<float>::quiet_NaN();
    float scrollOffsetX = 0.0f;
    float scrollOffsetY = 0.0f;

    Size desiredSize{};
    Rect layoutRect{};
    Size scrollExtent{};
    Matrix2D worldTransform{};
    Matrix4x4 worldMatrix3D{};
    bool hasPerspective3D = false;
    bool hasValidMeasure = false;
    bool hasValidArrange = false;
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
    std::uint8_t treeDepth = 0u;
    std::uint32_t renderNodeId = 0u;
    std::uint32_t renderNodeCount = 0u;
};

struct VisualNode {
    VisualNodeId id{};
    VisualKind kind = VisualKind::Box;
    bool alive = false;
    std::uint8_t dirtyFlags = VisualDirty_All;

    VisualNodeId parent{};
    std::vector<VisualNodeId> children;

    NativeHandle nativeHandle{};
    ObjectRef scriptOwner{};
    PersistentRoot pinnedOwnerRoot{};
    bool hasScriptOwner = false;
    std::uint32_t callbackMask = VisualCallback_None;
    bool drawDirty = false;

    VisualLayoutData layout{};
    VisualBoxStyle box{};
    VisualTransform2D transform2D{};
    VisualTransform3D transform3D{};
    VisualEffectStyle effect{};
    VisualOpacityMask opacityMask{};
    VisualTextData text{};
    VisualImageData image{};
    VisualVectorData vector{};
    std::array<float, 4> customScalars{};
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
    bool SetTransform3D(
        VisualNodeId id,
        float rx,
        float ry,
        float rz,
        float tx,
        float ty,
        float tz,
        float camDist) noexcept;
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
    bool SetTextSelection(
        VisualNodeId id,
        std::int32_t selStartCp,
        std::int32_t selLenCp,
        std::int32_t caretCp,
        bool showCaret,
        std::uint32_t selRgba,
        std::uint32_t caretRgba) noexcept;
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
    bool SetImageViewbox(
        VisualNodeId id,
        float vx,
        float vy,
        float vw,
        float vh) noexcept;
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
    bool InvalidateDraw(VisualNodeId id) noexcept;

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
    [[nodiscard]] bool HasAnyDirty() const noexcept { return anyDirty_; }
    void ClearDirtyFlags() noexcept;
    void Clear() noexcept;

    void MarkScriptRootsInPools() const noexcept;
    void RefreshScriptOwnerPins() noexcept;

    [[nodiscard]] std::size_t LiveNodeCount() const noexcept { return liveCount_; }
    [[nodiscard]] std::size_t ArenaSlotCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] VisualNode* NodeAtSlot(std::size_t slot) noexcept;
    [[nodiscard]] const VisualNode* NodeAtSlot(std::size_t slot) const noexcept;

private:
    void MarkAncestorsDirty(VisualNodeId id, VisualDirtyFlags flags) noexcept;
    void DetachFromParent(VisualNode& child) noexcept;
    void RefreshOwnerPin(VisualNode& node) noexcept;
    [[nodiscard]] bool WouldCreateCycle(VisualNodeId parentId, VisualNodeId childId) const noexcept;
    void RefreshSubtreeDepths(VisualNodeId id, std::uint8_t depth) noexcept;

    SymbolId visualNodeTypeId_ = 0u;
    NativeHandleRegistry ownedFallbackRegistry_;
    NativeHandleRegistry* handleRegistry_ = nullptr;
    UIResourceStore* resourceStore_ = nullptr;
    ManagedHeap* managedHeap_ = nullptr;

    std::deque<VisualNode> nodes_;
    std::vector<std::uint32_t> freeIndices_;
    VisualNodeId rootId_{};
    std::size_t liveCount_ = 0u;
    bool anyDirty_ = false;
    std::vector<VisualEcsColumnBinding> ecsBindings_;

    Size lastViewportSize_{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    VisualNodeId lastLaidOutRoot_{};
    std::array<std::vector<VisualNodeId>, 64> measureDepthBuckets_{};
    std::array<std::vector<VisualNodeId>, 64> arrangeDepthBuckets_{};
    std::array<std::vector<VisualNodeId>, 64> transformDepthBuckets_{};
    CustomMeasureCallback customMeasureCallback_{};
    CustomArrangeCallback customArrangeCallback_{};
};

} // namespace aero
