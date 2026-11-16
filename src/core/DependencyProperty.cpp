#include "aero/core/DependencyProperty.hpp"

#include <array>
#include <cctype>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace aero {
namespace {

constexpr PropMetaFlags kLayoutSelfFlags =
    PropMetaFlags::AffectsMeasure | PropMetaFlags::AffectsArrange | PropMetaFlags::AffectsRender;

constexpr PropMetaFlags kLayoutParentAttachedFlags =
    PropMetaFlags::AffectsMeasure |
    PropMetaFlags::AffectsArrange |
    PropMetaFlags::AffectsParentMeasure |
    PropMetaFlags::AffectsParentArrange |
    PropMetaFlags::AffectsRender;

constexpr PropMetaFlags kArrangeOnlyFlags =
    PropMetaFlags::AffectsArrange | PropMetaFlags::AffectsRender;

constexpr std::array<PropertyMetadata, kBuiltinPropertyMetadataCount> BuildBuiltinPropertyMetadataTable() noexcept {
    std::array<PropertyMetadata, kBuiltinPropertyMetadataCount> table{};
    for (std::uint16_t i = 0; i < kBuiltinPropertyMetadataCount; ++i) {
        table[i] = PropertyMetadata{i, PropMetaFlags::None, CustomPropertyValueKind::String};
    }

    auto set = [&table](BuiltinPropertyId id,
                        CustomPropertyValueKind kind,
                        PropMetaFlags flags) noexcept {
        const auto idx = static_cast<std::uint16_t>(id);
        table[idx] = PropertyMetadata{idx, flags, kind};
    };

    // Fast inline core layout / visual properties [0..7]
    set(PropId_Width, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_Height, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_MinWidth, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_MinHeight, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_Margin, CustomPropertyValueKind::Thickness, kLayoutParentAttachedFlags);
    set(PropId_Padding, CustomPropertyValueKind::Thickness, kLayoutSelfFlags);
    set(PropId_Opacity, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Visibility, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);

    // Core visual & typography properties [8..42]
    set(PropId_Background, CustomPropertyValueKind::Brush, PropMetaFlags::AffectsRender);
    set(PropId_Foreground, CustomPropertyValueKind::Brush, PropMetaFlags::AffectsRender | PropMetaFlags::Inherits);
    set(PropId_CornerRadius, CustomPropertyValueKind::Thickness, PropMetaFlags::AffectsRender);
    set(PropId_Title, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_Content, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_Tag, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_Text, CustomPropertyValueKind::String, kLayoutSelfFlags | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_FontSize, CustomPropertyValueKind::Float, kLayoutSelfFlags | PropMetaFlags::Inherits);
    set(PropId_Orientation, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_GridRow, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);
    set(PropId_GridColumn, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);
    set(PropId_GridRowSpan, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);
    set(PropId_GridColumnSpan, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);
    set(PropId_CanvasLeft, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_CanvasTop, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_ItemsSource, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_DataContext, CustomPropertyValueKind::String, PropMetaFlags::Inherits);
    set(PropId_CommandParameter, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_Projection3DYaw, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Projection3DPitch, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_BorderThickness, CustomPropertyValueKind::Thickness, kLayoutSelfFlags);
    set(PropId_Spacing, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_BorderBrush, CustomPropertyValueKind::Brush, PropMetaFlags::AffectsRender);
    set(PropId_Value, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_Minimum, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Maximum, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_IsChecked, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_SelectedItem, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_Placeholder, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_SelectedIndex, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_PathData, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_ImageSource, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_Stretch, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_ClipToBounds, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender);
    set(PropId_RenderTransformMatrix, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);

    // Alignment, constraints, panels & transforms [43..59]
    set(PropId_HorizontalAlignment, CustomPropertyValueKind::Int, kLayoutSelfFlags | PropMetaFlags::AffectsParentArrange);
    set(PropId_VerticalAlignment, CustomPropertyValueKind::Int, kLayoutSelfFlags | PropMetaFlags::AffectsParentArrange);
    set(PropId_MaxWidth, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_MaxHeight, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_RenderTransform, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_RenderTransformTranslation, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_RenderTransformOrigin, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Dock, CustomPropertyValueKind::Int, kLayoutParentAttachedFlags);
    set(PropId_LastChildFill, CustomPropertyValueKind::Bool, kLayoutSelfFlags);
    set(PropId_ItemWidth, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_ItemHeight, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_StretchDirection, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_HorizontalScrollBarVisibility, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_VerticalScrollBarVisibility, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_HorizontalOffset, CustomPropertyValueKind::Float, kArrangeOnlyFlags);
    set(PropId_VerticalOffset, CustomPropertyValueKind::Float, kArrangeOnlyFlags);
    set(PropId_LayoutTransform, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);

    // Interactive state & control properties [60..81]
    set(PropId_IsEnabled, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender | PropMetaFlags::Inherits);
    set(PropId_Focusable, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_GroupName, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_IsReadOnly, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender);
    set(PropId_MaxLength, CustomPropertyValueKind::Int, PropMetaFlags::None);
    set(PropId_CaretIndex, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_IsDropDownOpen, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_SmallChange, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_LargeChange, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_IsVirtualizing, CustomPropertyValueKind::Bool, kLayoutSelfFlags);
    set(PropId_IsPassword, CustomPropertyValueKind::Bool, kLayoutSelfFlags);
    set(PropId_AnimScaleX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimScaleY, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimRotateAngle, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimTranslateX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimTranslateY, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Style, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_ItemTemplate, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_AnimSkewX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimSkewY, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_FontFamily, CustomPropertyValueKind::String, kLayoutSelfFlags | PropMetaFlags::Inherits);
    set(PropId_TextWrapping, CustomPropertyValueKind::Int, kLayoutSelfFlags);

    // Templates, dictionaries & localization [90..98]
    set(PropId_Template, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_TemplatedParent, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_HorizontalContentAlignment, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_VerticalContentAlignment, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_ContentStringFormat, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_MergedDictionarySource, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_LocSource, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_FlowDirection, CustomPropertyValueKind::Int, kLayoutSelfFlags | PropMetaFlags::Inherits);

    // Shapes, gradients & composite controls [100..119]
    set(PropId_RadiusX, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_RadiusY, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_ShapePoints, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_UniformRows, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_UniformColumns, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_FirstColumn, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_IsExpanded, CustomPropertyValueKind::Bool, kLayoutSelfFlags | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_Header, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_BackgroundGradientId, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_StrokeThickness, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_ForegroundGradientId, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_BorderGradientId, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_LineX1, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_LineY1, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_LineX2, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_LineY2, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_ExpandDirection, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_IsIndeterminate, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender);
    set(PropId_TabStripPlacement, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_ContentTemplate, CustomPropertyValueKind::String, kLayoutSelfFlags);

    // 3D transforms, effects, Rive, layout scalars & Canvas/ItemsControl [130..167]
    set(PropId_Transform3DRotateXYZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Transform3DTranslateXYZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Transform3DScaleXYZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_Transform3DCenterXYZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_EffectKind, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_EffectParams, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_EffectColor, CustomPropertyValueKind::Color, PropMetaFlags::AffectsRender);
    set(PropId_RiveSource, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_AnimRotation3DX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimRotation3DY, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimRotation3DZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimTranslate3DX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimTranslate3DY, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_AnimTranslate3DZ, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_EffectBlurRadius, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_EffectShadowDepth, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_BackgroundOpacity, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_BorderOpacity, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_TextAlignment, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_BlendingMode, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_RichText, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_RefResolution, CustomPropertyValueKind::Float, kLayoutSelfFlags);
    set(PropId_CustomIndexInt32, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_CustomStateInt32, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_CanvasRight, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_CanvasBottom, CustomPropertyValueKind::Float, kLayoutParentAttachedFlags);
    set(PropId_ZIndex, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_ContentSource, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_DisplayMemberPath, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_ItemsPanel, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_ItemContainerStyle, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_IsOpen, CustomPropertyValueKind::Bool, kLayoutSelfFlags | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_EffectOpacity, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_HeaderTemplate, CustomPropertyValueKind::String, kLayoutSelfFlags);
    set(PropId_TextTrimming, CustomPropertyValueKind::Int, kLayoutSelfFlags);
    set(PropId_AlternationCount, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_AlternationIndex, CustomPropertyValueKind::Int, PropMetaFlags::ReadOnly);
    set(PropId_GridViewColumnsSpec, CustomPropertyValueKind::String, kLayoutSelfFlags);

    // Controls, commands, text layout, shaders & media [168..221]
    set(PropId_IsThreeState, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_RepeatDelay, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_RepeatInterval, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_ClickMode, CustomPropertyValueKind::Int, PropMetaFlags::None);
    set(PropId_InputGestureText, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_IsCheckable, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_IsSubmenuOpen, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_NavigateUri, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_ToolTipText, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_HasItems, CustomPropertyValueKind::Bool, PropMetaFlags::ReadOnly);
    set(PropId_TreeDepthIndent, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_ContextMenuKey, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_ActualWidth, CustomPropertyValueKind::Float, PropMetaFlags::ReadOnly);
    set(PropId_ActualHeight, CustomPropertyValueKind::Float, PropMetaFlags::ReadOnly);
    set(PropId_PlacementTarget, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_RoutedCommandName, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_TabIndex, CustomPropertyValueKind::Int, PropMetaFlags::None);
    set(PropId_KeyBindingsSpec, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_SelectedValuePath, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_IsHitTestVisible, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_IsDirectionReversed, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender);
    set(PropId_IsSnapToTickEnabled, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_TickFrequency, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_TickPlacement, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_Ticks, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_IsMoveToPointEnabled, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_PasswordChar, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_AcceptsReturn, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_MaxLines, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_LineHeight, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_CharacterSpacing, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_TextDecorations, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_IsEditable, CustomPropertyValueKind::Bool, PropMetaFlags::AffectsRender);
    set(PropId_MaxDropDownHeight, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_SelectionMode, CustomPropertyValueKind::Int, PropMetaFlags::None);
    set(PropId_StrokeDashArray, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender);
    set(PropId_SelectedValue, CustomPropertyValueKind::String, PropMetaFlags::AffectsRender | PropMetaFlags::BindsTwoWayByDefault);
    set(PropId_CommandBindingsSpec, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_SelectionStart, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_SelectionLength, CustomPropertyValueKind::Int, PropMetaFlags::AffectsRender);
    set(PropId_EffectAmountX, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_EffectAngle, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_LoadedBehavior, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_UnloadedBehavior, CustomPropertyValueKind::String, PropMetaFlags::None);
    set(PropId_Volume, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_IsMuted, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_MediaPosition, CustomPropertyValueKind::Float, PropMetaFlags::None);
    set(PropId_MouseDragConstrain, CustomPropertyValueKind::Bool, PropMetaFlags::None);
    set(PropId_Saturation, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_PathTrimStart, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_PathTrimEnd, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_ShaderTime, CustomPropertyValueKind::Float, PropMetaFlags::AffectsRender);
    set(PropId_VisualBrush, CustomPropertyValueKind::Brush, PropMetaFlags::AffectsRender);
    set(PropId_EasingExponent, CustomPropertyValueKind::Float, PropMetaFlags::None);

    return table;
}

inline constexpr std::array<PropertyMetadata, kBuiltinPropertyMetadataCount> kBuiltinPropertyMetadata =
    BuildBuiltinPropertyMetadataTable();

constexpr bool VerifyBuiltinPropertyMetadataTable() noexcept {
    for (std::uint16_t i = 0; i < kBuiltinPropertyMetadataCount; ++i) {
        if (kBuiltinPropertyMetadata[i].propId != i) {
            return false;
        }
    }
    constexpr BuiltinPropertyId kExpectedLayoutProps[] = {
        PropId_BorderThickness, PropId_FontSize, PropId_Title, PropId_Text, PropId_Content,
        PropId_Placeholder, PropId_ItemsSource, PropId_GridRow, PropId_GridColumn,
        PropId_GridRowSpan, PropId_GridColumnSpan, PropId_CanvasLeft, PropId_CanvasTop,
        PropId_Orientation, PropId_Spacing, PropId_PathData, PropId_ImageSource, PropId_RiveSource,
        PropId_Stretch, PropId_MaxWidth, PropId_MaxHeight, PropId_Dock, PropId_LastChildFill,
        PropId_ItemWidth, PropId_ItemHeight, PropId_StretchDirection,
        PropId_HorizontalScrollBarVisibility, PropId_VerticalScrollBarVisibility,
        PropId_LayoutTransform, PropId_IsVirtualizing, PropId_IsPassword, PropId_FontFamily,
        PropId_TextWrapping, PropId_Template, PropId_HorizontalContentAlignment,
        PropId_VerticalContentAlignment, PropId_FlowDirection, PropId_RadiusX, PropId_RadiusY,
        PropId_ShapePoints, PropId_UniformRows, PropId_UniformColumns, PropId_FirstColumn,
        PropId_IsExpanded, PropId_Header, PropId_StrokeThickness, PropId_LineX1, PropId_LineY1,
        PropId_LineX2, PropId_LineY2, PropId_ExpandDirection, PropId_TabStripPlacement,
        PropId_ContentTemplate, PropId_RichText, PropId_RefResolution, PropId_CanvasRight,
        PropId_CanvasBottom, PropId_ContentSource, PropId_DisplayMemberPath, PropId_ItemsPanel,
        PropId_ItemContainerStyle, PropId_IsOpen, PropId_HeaderTemplate, PropId_TextTrimming,
        PropId_GridViewColumnsSpec,
    };
    for (const BuiltinPropertyId id : kExpectedLayoutProps) {
        const auto flags = kBuiltinPropertyMetadata[static_cast<std::uint16_t>(id)].flags;
        if (!HasPropMetaFlag(flags, PropMetaFlags::AffectsMeasure) ||
            !HasPropMetaFlag(flags, PropMetaFlags::AffectsArrange)) {
            return false;
        }
    }
    constexpr BuiltinPropertyId kExpectedArrangeOnlyProps[] = {
        PropId_HorizontalOffset, PropId_VerticalOffset,
    };
    for (const BuiltinPropertyId id : kExpectedArrangeOnlyProps) {
        const auto flags = kBuiltinPropertyMetadata[static_cast<std::uint16_t>(id)].flags;
        if (HasPropMetaFlag(flags, PropMetaFlags::AffectsMeasure) ||
            !HasPropMetaFlag(flags, PropMetaFlags::AffectsArrange)) {
            return false;
        }
    }
    constexpr BuiltinPropertyId kExpectedParentLayoutProps[] = {
        PropId_GridRow, PropId_GridColumn, PropId_GridRowSpan, PropId_GridColumnSpan,
        PropId_CanvasLeft, PropId_CanvasTop, PropId_CanvasRight, PropId_CanvasBottom,
        PropId_Dock, PropId_MaxWidth, PropId_MaxHeight, PropId_LayoutTransform,
    };
    for (const BuiltinPropertyId id : kExpectedParentLayoutProps) {
        const auto flags = kBuiltinPropertyMetadata[static_cast<std::uint16_t>(id)].flags;
        if (!HasPropMetaFlag(flags, PropMetaFlags::AffectsParentMeasure) ||
            !HasPropMetaFlag(flags, PropMetaFlags::AffectsParentArrange)) {
            return false;
        }
    }
    return true;
}

static_assert(VerifyBuiltinPropertyMetadataTable(), "Builtin PropertyMetadata table mismatch");

[[nodiscard]] std::string_view TrimWs(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] std::string_view StripXmlnsPrefixLocal(std::string_view name) noexcept {
    name = TrimWs(name);
    if (const std::size_t colon = name.rfind(':'); colon != std::string_view::npos) {
        return name.substr(colon + 1u);
    }
    return name;
}

struct CustomPropertyStorageEntry {
    std::uint16_t propId = 0xFFFFu;
    std::string ownerStorage;
    std::string propertyStorage;
    CustomPropertyValueKind valueKind = CustomPropertyValueKind::String;
    UnifiedValue defaultValue{};
    bool hasStringDefault = false;
    std::string defaultStringCopy;
};

struct CustomPropertyRegistryState {
    std::mutex mutex;
    std::deque<CustomPropertyStorageEntry> entries;
    std::unordered_map<std::string, std::uint16_t> byQualifiedKey;
    std::unordered_map<std::string, std::uint16_t> byUnqualifiedName;
};

CustomPropertyRegistryState& GetCustomPropertyRegistryState() noexcept {
    static CustomPropertyRegistryState sState;
    return sState;
}

[[nodiscard]] UnifiedValue CopyStoredDefaultValue(const CustomPropertyStorageEntry& entry) {
    if (entry.hasStringDefault) {
        return value::FromString(entry.defaultStringCopy);
    }
    return entry.defaultValue;
}

} // namespace

PropertyMetadata GetPropertyMetadata(std::uint16_t propId) noexcept {
    if (propId < kBuiltinPropertyMetadataCount) {
        return kBuiltinPropertyMetadata[propId];
    }
    if (IsCustomPropertyId(propId)) {
        CustomPropertyValueKind kind = CustomPropertyValueKind::String;
        if (TryGetCustomPropertyValueKind(propId, kind)) {
            return PropertyMetadata{propId, PropMetaFlags::AffectsRender, kind};
        }
    }
    return PropertyMetadata{propId, PropMetaFlags::None, CustomPropertyValueKind::String};
}

std::uint16_t RegisterCustomProperty(
    std::string_view ownerTypeName,
    std::string_view propertyName,
    CustomPropertyValueKind valueKind,
    UnifiedValue defaultValue) {
    const std::string_view cleanOwner = StripXmlnsPrefixLocal(ownerTypeName);
    const std::string_view cleanProp = TrimWs(propertyName);
    if (cleanOwner.empty() || cleanProp.empty()) {
        return 0xFFFFu;
    }

    auto& state = GetCustomPropertyRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);

    std::string qualKey;
    qualKey.reserve(cleanOwner.size() + 1u + cleanProp.size());
    qualKey.append(cleanOwner);
    qualKey.push_back('.');
    qualKey.append(cleanProp);

    if (const auto it = state.byQualifiedKey.find(qualKey); it != state.byQualifiedKey.end()) {
        return it->second;
    }

    const std::size_t maxSlots =
        static_cast<std::size_t>(PropId_LastCustom - PropId_FirstCustom) + 1u;
    if (state.entries.size() >= maxSlots) {
        return 0xFFFFu;
    }

    const std::uint16_t assignedId =
        static_cast<std::uint16_t>(PropId_FirstCustom + state.entries.size());

    CustomPropertyStorageEntry entry{};
    entry.propId = assignedId;
    entry.ownerStorage.assign(cleanOwner.data(), cleanOwner.size());
    entry.propertyStorage.assign(cleanProp.data(), cleanProp.size());
    entry.valueKind = valueKind;
    if (const auto* sv = std::get_if<StringValue>(&defaultValue)) {
        entry.hasStringDefault = true;
        entry.defaultStringCopy = sv->str();
    } else {
        entry.defaultValue = std::move(defaultValue);
    }
    state.entries.push_back(std::move(entry));
    state.byQualifiedKey.emplace(std::move(qualKey), assignedId);
    state.byUnqualifiedName.try_emplace(std::string(cleanProp), assignedId);
    return assignedId;
}

std::uint16_t FindCustomProperty(
    std::string_view ownerTypeName,
    std::string_view propertyName) noexcept {
    try {
        const std::string_view cleanOwner = StripXmlnsPrefixLocal(ownerTypeName);
        const std::string_view cleanProp = TrimWs(propertyName);
        if (cleanOwner.empty() || cleanProp.empty()) {
            return 0xFFFFu;
        }
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        std::string qualKey;
        qualKey.reserve(cleanOwner.size() + 1u + cleanProp.size());
        qualKey.append(cleanOwner);
        qualKey.push_back('.');
        qualKey.append(cleanProp);
        const auto it = state.byQualifiedKey.find(qualKey);
        return (it != state.byQualifiedKey.end()) ? it->second : 0xFFFFu;
    } catch (...) {
        return 0xFFFFu;
    }
}

std::uint16_t FindCustomPropertyByName(std::string_view propertyName) noexcept {
    try {
        const std::string_view cleanProp = TrimWs(propertyName);
        if (cleanProp.empty()) {
            return 0xFFFFu;
        }
        if (const std::size_t dot = cleanProp.rfind('.'); dot != std::string_view::npos) {
            const std::uint16_t qualId =
                FindCustomProperty(cleanProp.substr(0u, dot), cleanProp.substr(dot + 1u));
            if (qualId != 0xFFFFu) {
                return qualId;
            }
        }
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const auto it = state.byUnqualifiedName.find(std::string(cleanProp));
        return (it != state.byUnqualifiedName.end()) ? it->second : 0xFFFFu;
    } catch (...) {
        return 0xFFFFu;
    }
}

bool TryGetCustomPropertyDesc(
    std::uint16_t propId,
    CustomPropertyDesc& outDesc) noexcept {
    if (!IsCustomPropertyId(propId)) {
        return false;
    }
    try {
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::size_t idx = static_cast<std::size_t>(propId - PropId_FirstCustom);
        if (idx >= state.entries.size()) {
            return false;
        }
        const auto& e = state.entries[idx];
        outDesc.propId = e.propId;
        outDesc.ownerTypeName = e.ownerStorage;
        outDesc.propertyName = e.propertyStorage;
        outDesc.valueKind = e.valueKind;
        outDesc.defaultValue = CopyStoredDefaultValue(e);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryGetCustomPropertyDefaultValue(
    std::uint16_t propId,
    UnifiedValue& outDefaultValue) noexcept {
    if (!IsCustomPropertyId(propId)) {
        return false;
    }
    try {
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::size_t idx = static_cast<std::size_t>(propId - PropId_FirstCustom);
        if (idx >= state.entries.size()) {
            return false;
        }
        outDefaultValue = CopyStoredDefaultValue(state.entries[idx]);
        return true;
    } catch (...) {
        return false;
    }
}

std::string_view GetCustomPropertyName(std::uint16_t propId) noexcept {
    if (!IsCustomPropertyId(propId)) {
        return {};
    }
    try {
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::size_t idx = static_cast<std::size_t>(propId - PropId_FirstCustom);
        if (idx >= state.entries.size()) {
            return {};
        }
        return state.entries[idx].propertyStorage;
    } catch (...) {
        return {};
    }
}

bool TryGetCustomPropertyValueKind(
    std::uint16_t propId,
    CustomPropertyValueKind& outKind) noexcept {
    if (!IsCustomPropertyId(propId)) {
        return false;
    }
    try {
        auto& state = GetCustomPropertyRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::size_t idx = static_cast<std::size_t>(propId - PropId_FirstCustom);
        if (idx >= state.entries.size()) {
            return false;
        }
        outKind = state.entries[idx].valueKind;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace aero
