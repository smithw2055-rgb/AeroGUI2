#pragma once

#include <cstdint>

#include "aero/core/UnifiedValue.hpp"

namespace aero {

enum class ValueSourceRank : std::uint8_t {
    Default = 0,
    Inherited = 1,
    ThemeStyle = 2,
    StyleTrigger = 3,
    TemplateTrigger = 4,
    Local = 5,
    Animation = 6,
};

enum class Visibility : std::uint8_t {
    Visible = 0,
    Hidden = 1,
    Collapsed = 2,
};

enum class HorizontalAlignment : std::uint8_t {
    Stretch = 0,
    Left = 1,
    Center = 2,
    Right = 3,
};

enum class VerticalAlignment : std::uint8_t {
    Stretch = 0,
    Top = 1,
    Center = 2,
    Bottom = 3,
};

enum class TextWrapping : std::uint8_t {
    NoWrap = 0,
    Wrap = 1,
    WrapWithOverflow = 2,
};

enum class UIEffectKind : std::uint8_t {
    None = 0,
    Blur = 1,
    DropShadow = 2,
    Pixelate = 3,
    BackdropBlur = 4,
};

enum class PointerButton : std::uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

enum class KeyCode : std::uint16_t {
    None = 0,
    Tab = 1,
    Enter = 2,
    Space = 3,
    Escape = 4,
    Backspace = 5,
    Delete = 6,
    Left = 7,
    Right = 8,
    Up = 9,
    Down = 10,
    Home = 11,
    End = 12,
    PageUp = 13,
    PageDown = 14,
};

enum KeyModifierFlags : std::uint8_t {
    KeyMod_None = 0u,
    KeyMod_Shift = 1u << 0u,
    KeyMod_Ctrl = 1u << 1u,
    KeyMod_Alt = 1u << 2u,
};

enum class TextAlignment : std::uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
    Justify = 3,
};

enum class BlendingMode : std::uint8_t {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Additive = 3,
};

enum class FocusDirection : std::uint8_t {
    Next = 0,
    Previous = 1,
    Left = 2,
    Right = 3,
    Up = 4,
    Down = 5,
};

// Zero-allocation interactive visual & focus state packed into FastInlineStorage::reserved (1 byte).
// Bit 0 is inverted (Disabled) so zero-initialized FastInlineStorage defaults to IsEnabled() == true.
enum InteractiveStateFlags : std::uint8_t {
    InteractiveState_None = 0u,
    InteractiveState_Disabled = 1u << 0u,
    InteractiveState_Hovered = 1u << 1u,
    InteractiveState_Pressed = 1u << 2u,
    InteractiveState_Focused = 1u << 3u,
    InteractiveState_FocusableExplicit = 1u << 4u,
    InteractiveState_FocusableValue = 1u << 5u,
};

enum LayoutDirtyFlags : std::uint8_t {
    LayoutClean = 0,
    MeasureDirty = 1u << 0u,
    ArrangeDirty = 1u << 1u,
    RenderDirty = 1u << 2u,
    LayoutAllDirty = MeasureDirty | ArrangeDirty | RenderDirty,
    SubtreeMeasureDirty = 1u << 3u,
    SubtreeArrangeDirty = 1u << 4u,
    SubtreeLayoutDirty = SubtreeMeasureDirty | SubtreeArrangeDirty,
    SubtreeRenderDirty = 1u << 5u,
};

// Built-in property IDs: 0..7 (plus inline-packed Horizontal/VerticalAlignment, IsEnabled, Focusable)
// map directly to FastInlineStorage with 0 heap allocations.
enum BuiltinPropertyId : std::uint16_t {
    PropId_Width = 0,
    PropId_Height = 1,
    PropId_MinWidth = 2,
    PropId_MinHeight = 3,
    PropId_Margin = 4,
    PropId_Padding = 5,
    PropId_Opacity = 6,
    PropId_Visibility = 7,
    PropId_FirstSparse = 8,
    PropId_Background = 8,
    PropId_Foreground = 9,
    PropId_CornerRadius = 10,
    PropId_Title = 11,
    PropId_Content = 12,
    PropId_Tag = 13,
    PropId_Text = 14,
    PropId_FontSize = 15,
    PropId_Orientation = 16,
    PropId_GridRow = 17,
    PropId_GridColumn = 18,
    PropId_GridRowSpan = 19,
    PropId_GridColumnSpan = 20,
    PropId_CanvasLeft = 21,
    PropId_CanvasTop = 22,
    PropId_ItemsSource = 23,
    PropId_DataContext = 24,
    PropId_CommandParameter = 25,
    PropId_Projection3DYaw = 26,
    PropId_Projection3DPitch = 27,
    PropId_BorderThickness = 28,
    PropId_Spacing = 29,
    PropId_BorderBrush = 30,
    PropId_Value = 31,
    PropId_Minimum = 32,
    PropId_Maximum = 33,
    PropId_IsChecked = 34,
    PropId_SelectedItem = 35,
    PropId_Placeholder = 36,
    PropId_SelectedIndex = 37,
    PropId_PathData = 38,
    PropId_ImageSource = 39,
    PropId_Stretch = 40,
    PropId_ClipToBounds = 41,
    PropId_RenderTransformMatrix = 42,
    // Phase 5A: Alignment, MaxWidth/MaxHeight, Dedicated Panels & Full 2D Affine Transforms
    PropId_HorizontalAlignment = 43,
    PropId_VerticalAlignment = 44,
    PropId_MaxWidth = 45,
    PropId_MaxHeight = 46,
    PropId_RenderTransform = 47,            // Float4Value{m00, m01, m10, m11}
    PropId_RenderTransformTranslation = 48, // Float4Value{tx, ty, 0.0f, 0.0f}
    PropId_RenderTransformOrigin = 49,      // Float4Value{originX, originY, 0.0f, 0.0f}
    PropId_Dock = 50,
    PropId_LastChildFill = 51,
    PropId_ItemWidth = 52,
    PropId_ItemHeight = 53,
    PropId_StretchDirection = 54,
    PropId_HorizontalScrollBarVisibility = 55,
    PropId_VerticalScrollBarVisibility = 56,
    PropId_HorizontalOffset = 57,
    PropId_VerticalOffset = 58,
    PropId_LayoutTransform = 59,            // Float4Value{m00, m01, m10, m11} (linear 2x2 layout transform)
    // Phase 5B: Interactive State, Stateful Controls & Viewport Virtualization
    PropId_IsEnabled = 60,                  // Inline in FastInlineStorage::reserved (0 heap allocs)
    PropId_Focusable = 61,                  // Inline in FastInlineStorage::reserved (0 heap allocs)
    PropId_GroupName = 62,
    PropId_IsReadOnly = 63,
    PropId_MaxLength = 64,
    PropId_CaretIndex = 65,
    PropId_IsDropDownOpen = 66,
    PropId_SmallChange = 67,
    PropId_LargeChange = 68,
    PropId_IsVirtualizing = 69,
    PropId_IsPassword = 70,
    // Phase 5C: Declarative Style Attachment & Independent Animated Transform Channels
    PropId_AnimScaleX = 71,                 // float (default 1.0f, composed around RenderTransformOrigin)
    PropId_AnimScaleY = 72,                 // float (default 1.0f, composed around RenderTransformOrigin)
    PropId_AnimRotateAngle = 73,            // float degrees (default 0.0f, composed around RenderTransformOrigin)
    PropId_AnimTranslateX = 74,             // float px (default 0.0f)
    PropId_AnimTranslateY = 75,             // float px (default 0.0f)
    PropId_Style = 76,                      // StringValue (Style resource key)
    // Phase 5D: Declarative DataTemplate Attachment
    PropId_ItemTemplate = 77,               // StringValue (DataTemplate resource key)
    // Phase 6: Independent Orthogonal Skew Animation Channels & Multi-Frontend Contract Bridges
    PropId_AnimSkewX = 78,                  // float degrees (default 0.0f, composed around RenderTransformOrigin)
    PropId_AnimSkewY = 79,                  // float degrees (default 0.0f, composed around RenderTransformOrigin)
    // P0-3: TrueType FontFamily & TextWrapping
    PropId_FontFamily = 80,                 // StringValue (Font family name or XAML "./#Family" URI)
    PropId_TextWrapping = 81,               // Int64 / TextWrapping enum (0 = NoWrap, 1 = Wrap, 2 = WrapWithOverflow)
    // P1-1: ControlTemplate, TemplatedParent & Content Alignment (reserved range 90..95)
    PropId_Template = 90,                   // StringValue (ControlTemplate resource key)
    PropId_TemplatedParent = 91,            // TemplatedParent reference metadata
    PropId_HorizontalContentAlignment = 92, // Int64 / HorizontalAlignment enum
    PropId_VerticalContentAlignment = 93,   // Int64 / VerticalAlignment enum
    PropId_ContentStringFormat = 94,        // StringValue (optional content format string)
    // P1-2: MergedDictionaries & DynamicResource / Loc Live Localization (exclusive range 96..99)
    PropId_MergedDictionarySource = 96,     // StringValue (ResourceDictionary Source URI)
    PropId_LocSource = 97,                  // StringValue (noesis:Loc.Source / Loc.Source URI)
    PropId_FlowDirection = 98,              // StringValue / Int64 (LeftToRight / RightToLeft)
    // P1-3: Gradient Brushes, Geometric Primitives & Composite Controls [100 .. 119]
    PropId_RadiusX = 100,                   // float (Rectangle.RadiusX)
    PropId_RadiusY = 101,                   // float (Rectangle.RadiusY)
    PropId_ShapePoints = 102,               // StringValue (Polygon.Points / Polyline.Points)
    PropId_UniformRows = 103,               // Int64 / float (UniformGrid.Rows)
    PropId_UniformColumns = 104,            // Int64 / float (UniformGrid.Columns)
    PropId_FirstColumn = 105,               // Int64 / float (UniformGrid.FirstColumn)
    PropId_IsExpanded = 106,                // bool (Expander.IsExpanded)
    PropId_Header = 107,                    // StringValue / bool header-slot marker (HeaderedContentControl/GroupBox/Expander/TabItem)
    PropId_BackgroundGradientId = 108,      // Int64 (GradientBrushRegistry brushId for Background / Fill)
    PropId_StrokeThickness = 109,           // float (Shape.StrokeThickness)
    PropId_ForegroundGradientId = 110,      // Int64 (GradientBrushRegistry brushId for Foreground)
    PropId_BorderGradientId = 111,          // Int64 (GradientBrushRegistry brushId for BorderBrush / Stroke)
    PropId_LineX1 = 112,                    // float (Line.X1)
    PropId_LineY1 = 113,                    // float (Line.Y1)
    PropId_LineX2 = 114,                    // float (Line.X2)
    PropId_LineY2 = 115,                    // float (Line.Y2)
    PropId_ExpandDirection = 116,           // Int64 (Expander.ExpandDirection: 0=Down, 1=Up, 2=Left, 3=Right)
    PropId_IsIndeterminate = 117,           // bool (ProgressBar.IsIndeterminate)
    PropId_TabStripPlacement = 118,         // Int64 (TabControl.TabStripPlacement: 0=Top, 1=Bottom, 2=Left, 3=Right)
    PropId_ContentTemplate = 119,           // StringValue (TabControl.ContentTemplate DataTemplate key)
    // P1-5: Exclusive BuiltinPropertyId range [130..145] for 3D Perspective, Blur/DropShadow Effects & RiveControl
    PropId_Transform3DRotateXYZ = 130,      // Float4Value{rotXDeg, rotYDeg, rotZDeg, cameraDistance}
    PropId_Transform3DTranslateXYZ = 131,   // Float4Value{tx, ty, tz, 0.0f}
    PropId_Transform3DScaleXYZ = 132,       // Float4Value{sx, sy, sz, 1.0f}
    PropId_Transform3DCenterXYZ = 133,      // Float4Value{cx, cy, cz, hasExplicitCenter}
    PropId_EffectKind = 134,                // Int64 / UIEffectKind (0 = None, 1 = Blur, 2 = DropShadow)
    PropId_EffectParams = 135,              // Float4Value{blurRadius, shadowDepth, directionDeg, opacity}
    PropId_EffectColor = 136,               // Float4Value{r, g, b, a}
    PropId_RiveSource = 137,                // StringValue (.riv binary asset source URI)
    PropId_AnimRotation3DX = 138,           // float degrees (3D X-axis rotation channel)
    PropId_AnimRotation3DY = 139,           // float degrees (3D Y-axis rotation channel)
    PropId_AnimRotation3DZ = 140,           // float degrees (3D Z-axis rotation channel)
    PropId_AnimTranslate3DX = 141,          // float px (3D X translation channel)
    PropId_AnimTranslate3DY = 142,          // float px (3D Y translation channel)
    PropId_AnimTranslate3DZ = 143,          // float px (3D Z translation channel)
    PropId_EffectBlurRadius = 144,          // float px (bindable/animatable BlurRadius / Radius override)
    PropId_EffectShadowDepth = 145,         // float px (bindable/animatable ShadowDepth override)
    PropId_BackgroundOpacity = 146,         // float [0..1] (Brush.Opacity on Background / Shape.Fill)
    PropId_BorderOpacity = 147,             // float [0..1] (Brush.Opacity on BorderBrush / Shape.Stroke)
    PropId_TextAlignment = 148,             // Int64 / TextAlignment enum (0=Left, 1=Center, 2=Right, 3=Justify)
    PropId_BlendingMode = 149,              // Int64 / BlendingMode enum (0=Normal, 1=Multiply, 2=Screen, 3=Additive)
    PropId_RichText = 150,                  // StringValue (noesis:RichText.Text BBCode string)
    PropId_RefResolution = 151,             // Float4Value{refWidth, refHeight, 0, 0} (LayoutScaler.RefResolution)
    PropId_CustomIndexInt32 = 152,          // Int64 (Custom.Index_Int32 attached integer property)
    PropId_CustomStateInt32 = 153,          // Int64 (Custom.State_Int32 attached integer property)
    PropId_CanvasRight = 154,               // float (Canvas.Right attached property)
    PropId_CanvasBottom = 155,              // float (Canvas.Bottom attached property)
    PropId_ZIndex = 156,                    // Int64 (Panel.ZIndex / Canvas.ZIndex attached property)
    PropId_ContentSource = 157,             // StringValue (ContentPresenter.ContentSource: Content/Header/SelectionBoxItem)
    PropId_DisplayMemberPath = 158,         // StringValue (ItemsControl.DisplayMemberPath)
    PropId_ItemsPanel = 159,                // StringValue (ItemsControl.ItemsPanel key or serialized panel descriptor)
    PropId_ItemContainerStyle = 160,        // StringValue (ItemsControl.ItemContainerStyle key)
    PropId_IsOpen = 161,                    // bool (Popup.IsOpen / ToolTip.IsOpen)
    PropId_EffectOpacity = 162,             // float [0..1] (DropShadowEffect.Opacity animatable override)
    PropId_HeaderTemplate = 163,            // StringValue (HeaderedContentControl.HeaderTemplate)
    PropId_TextTrimming = 164,              // Int64 (TextBlock.TextTrimming: 0=None, 1=CharacterEllipsis, 2=WordEllipsis)
    PropId_AlternationCount = 165,          // Int64 (ItemsControl.AlternationCount)
    PropId_AlternationIndex = 166,          // Int64 (ItemsControl.AlternationIndex)
    PropId_GridViewColumnsSpec = 167,       // StringValue (ListView GridView serialized columns spec)
    PropId_IsThreeState = 168,              // bool (ToggleButton/CheckBox.IsThreeState)
    PropId_RepeatDelay = 169,               // float ms (RepeatButton.Delay)
    PropId_RepeatInterval = 170,            // float ms (RepeatButton.Interval)
    PropId_ClickMode = 171,                 // Int64 (ButtonBase.ClickMode: 0=Release, 1=Press, 2=Hover)
    PropId_InputGestureText = 172,          // StringValue (MenuItem.InputGestureText)
    PropId_IsCheckable = 173,               // bool (MenuItem.IsCheckable)
    PropId_IsSubmenuOpen = 174,             // bool (MenuItem.IsSubmenuOpen)
    PropId_NavigateUri = 175,               // StringValue (Hyperlink.NavigateUri)
    PropId_ToolTipText = 176,               // StringValue (ToolTipService.ToolTip / FrameworkElement.ToolTip)
    PropId_HasItems = 177,                  // bool (ItemsControl/TreeViewItem.HasItems)
    PropId_TreeDepthIndent = 178,           // float px (TreeViewItem depth indent)
    PropId_ContextMenuKey = 179,            // StringValue (FrameworkElement.ContextMenu template key)
    PropId_ActualWidth = 180,               // float px (FrameworkElement.ActualWidth read-only)
    PropId_ActualHeight = 181,              // float px (FrameworkElement.ActualHeight read-only)
    PropId_PlacementTarget = 182,           // NativeHandle (ContextMenu/Popup.PlacementTarget)
    PropId_RoutedCommandName = 183,         // StringValue (Built-in routed command e.g. ScrollBar.LineLeftCommand)
    PropId_FirstContractInput = 0x1000u,    // Dynamic/Contract input bridge slot range [0x1000 .. 0x1FFFu]
    // Pseudo-property IDs for zero-allocation inline interactive Trigger conditions
    PropId_TriggerIsMouseOver = 0xFF01u,
    PropId_TriggerIsPressed = 0xFF02u,
    PropId_TriggerIsFocused = 0xFF03u,
    PropId_TriggerIsKeyboardFocusWithin = 0xFF04u,
};

[[nodiscard]] constexpr bool IsFastInlinePropertyId(std::uint16_t propId) noexcept {
    return propId < PropId_FirstSparse ||
           propId == PropId_HorizontalAlignment ||
           propId == PropId_VerticalAlignment ||
           propId == PropId_IsEnabled ||
           propId == PropId_Focusable;
}

inline constexpr std::size_t kFastInlinePropertySlotCount = 12u;

[[nodiscard]] constexpr int FastInlineSlotIndex(std::uint16_t propId) noexcept {
    if (propId < PropId_FirstSparse) {
        return static_cast<int>(propId);
    }
    switch (propId) {
        case PropId_HorizontalAlignment: return 8;
        case PropId_VerticalAlignment:   return 9;
        case PropId_IsEnabled:           return 10;
        case PropId_Focusable:           return 11;
        default:                         return -1;
    }
}

// High-frequency layout, rendering & interactive state properties stored inline inside UIElement (56 bytes)
struct FastInlineStorage {
    float width = -1.0f;       // < 0 denotes Auto
    float height = -1.0f;      // < 0 denotes Auto
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float marginL = 0.0f;
    float marginT = 0.0f;
    float marginR = 0.0f;
    float marginB = 0.0f;
    float paddingL = 0.0f;
    float paddingT = 0.0f;
    float paddingR = 0.0f;
    float paddingB = 0.0f;
    float opacity = 1.0f;
    std::uint8_t visibility = static_cast<std::uint8_t>(Visibility::Visible);
    mutable std::uint8_t layoutDirtyFlags = static_cast<std::uint8_t>(
        LayoutDirtyFlags::LayoutAllDirty |
        LayoutDirtyFlags::SubtreeLayoutDirty |
        LayoutDirtyFlags::SubtreeRenderDirty);
    // Bits [0..1] = HorizontalAlignment (default 0 = Stretch), Bits [2..3] = VerticalAlignment (default 0 = Stretch)
    std::uint8_t alignmentFlags = 0;
    // Packed InteractiveStateFlags (Disabled, Hovered, Pressed, Focused, FocusableExplicit, FocusableValue)
    std::uint8_t reserved = 0;
};

static_assert(
    sizeof(FastInlineStorage) == 56,
    "FastInlineStorage layout size check");

// C-03 & ABI v2.0: 32-byte 16-byte-aligned zero-hole sparse property entry.
// Bytes 0..7 store core dependency property & primary track indices;
// Bytes 8..15 explicitly utilize the 16B alignment space for dual-track Crossfade/Additive
// blending and ContractInputBank slot mapping;
// Bytes 16..31 hold the 16B UnifiedValue payload (byte 31 = ValueTag).
struct alignas(16) CompactPropertyEntry {
    // --- Bytes 0..7: Core dependency property & primary track indices ---
    std::uint16_t propId = 0;
    std::uint8_t  sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Local);
    std::uint8_t  flags = 0;
    std::uint16_t bindingSlotId = 0xFFFFu;
    std::uint16_t animationTrackId = 0xFFFFu;

    // --- Bytes 8..15: Explicit 16B alignment space (0 memory bloat for dual-track blend & contract bridge) ---
    std::uint16_t secondaryTrackId = 0xFFFFu;  // Crossfade or Additive secondary track index
    std::uint16_t contractInputSlot = 0xFFFFu; // Mapped slot in component's ContractInputBank
    std::uint16_t blendWeightUnorm16 = 0u;     // 0..65535 normalized blend weight
    std::uint16_t entryMetaFlags = 0u;         // Snapshot & inline quantization metadata flags

    // --- Bytes 16..31: 16B aligned UnifiedValue payload (byte 31 is ValueTag) ---
    UnifiedValue effectiveValue{};
};

static_assert(
    sizeof(CompactPropertyEntry) == 32,
    "CompactPropertyEntry must be exactly 32 bytes (2 entries per 64B cache line)");
static_assert(
    alignof(CompactPropertyEntry) == 16,
    "CompactPropertyEntry must be 16-byte aligned");

} // namespace aero
