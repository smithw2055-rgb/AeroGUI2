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

// P1-4: 32-byte 8-byte-aligned sparse property entry.
// Because UnifiedValue (rs_rt::Value) is 8-byte aligned (24 bytes),
// the 4 bytes following (propId, sourceRank, flags) are explicitly utilized for
// bindingSlotId and animationTrackId instead of being wasted as compiler padding.
struct alignas(8) CompactPropertyEntry {
    std::uint16_t propId = 0;
    std::uint8_t sourceRank = static_cast<std::uint8_t>(ValueSourceRank::Local);
    std::uint8_t flags = 0;
    std::uint16_t bindingSlotId = 0xFFFFu;
    std::uint16_t animationTrackId = 0xFFFFu;
    UnifiedValue effectiveValue{};
};

static_assert(
    sizeof(CompactPropertyEntry) == 32,
    "CompactPropertyEntry must be exactly 32 bytes (2 entries per 64B cache line)");

} // namespace aero
