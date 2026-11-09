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

enum LayoutDirtyFlags : std::uint8_t {
    LayoutClean = 0,
    MeasureDirty = 1u << 0u,
    ArrangeDirty = 1u << 1u,
    RenderDirty = 1u << 2u,
    LayoutAllDirty = MeasureDirty | ArrangeDirty | RenderDirty,
};

// Built-in property IDs: 0..7 map directly to FastInlineStorage with 0 heap allocations.
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
};

// 8 high-frequency layout & rendering properties stored inline inside UIElement (36 bytes)
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
    std::uint8_t layoutDirtyFlags = LayoutDirtyFlags::LayoutAllDirty;
    std::uint16_t reserved = 0;
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
