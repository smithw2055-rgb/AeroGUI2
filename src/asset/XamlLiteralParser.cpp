#include "aero/asset/AxbFormat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace {

std::uint8_t HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + (c - 'A'));
    return 0;
}

Color ParseHexColor(std::string_view hex) noexcept {
    if (hex.size() == 6) {
        const float r = static_cast<float>((HexNibble(hex[0]) << 4) | HexNibble(hex[1])) / 255.0f;
        const float g = static_cast<float>((HexNibble(hex[2]) << 4) | HexNibble(hex[3])) / 255.0f;
        const float b = static_cast<float>((HexNibble(hex[4]) << 4) | HexNibble(hex[5])) / 255.0f;
        return Color{r, g, b, 1.0f};
    }
    if (hex.size() == 8) {
        const float a = static_cast<float>((HexNibble(hex[0]) << 4) | HexNibble(hex[1])) / 255.0f;
        const float r = static_cast<float>((HexNibble(hex[2]) << 4) | HexNibble(hex[3])) / 255.0f;
        const float g = static_cast<float>((HexNibble(hex[4]) << 4) | HexNibble(hex[5])) / 255.0f;
        const float b = static_cast<float>((HexNibble(hex[6]) << 4) | HexNibble(hex[7])) / 255.0f;
        return Color{r, g, b, a};
    }
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

[[nodiscard]] Thickness ParseThicknessLiteral(std::string_view raw) {
    const auto nums = detail::ParseFloatList(raw);
    if (nums.size() == 1) return Thickness(nums[0]);
    if (nums.size() == 2) return Thickness(nums[0], nums[1]);
    if (nums.size() >= 4) return Thickness(nums[0], nums[1], nums[2], nums[3]);
    return Thickness{};
}

[[nodiscard]] std::int64_t ParseInt64Literal(std::string_view raw) noexcept {
    std::int64_t iv = 0;
    const auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), iv);
    if (ec != std::errc{} || ptr != raw.data() + raw.size()) {
        iv = static_cast<std::int64_t>(std::lround(detail::ParseFloat32Token(raw, 0.0f)));
    }
    return iv;
}

struct PropIdMapping {
    std::string_view name;
    std::uint16_t propId;
};

constexpr std::array<PropIdMapping, 387> kBuiltinPropertyTable = {{
    {"AcceptsReturn", PropId_AcceptsReturn},
    {"ActualHeight", PropId_ActualHeight},
    {"ActualWidth", PropId_ActualWidth},
    {"AlternationCount", PropId_AlternationCount},
    {"AlternationIndex", PropId_AlternationIndex},
    {"AmountX", PropId_EffectAmountX},
    {"Angle", PropId_AnimRotateAngle},
    {"AngleX", PropId_AnimSkewX},
    {"AngleY", PropId_AnimSkewY},
    {"AnimRotateAngle", PropId_AnimRotateAngle},
    {"AnimRotation3DX", PropId_AnimRotation3DX},
    {"AnimRotation3DY", PropId_AnimRotation3DY},
    {"AnimRotation3DZ", PropId_AnimRotation3DZ},
    {"AnimScaleX", PropId_AnimScaleX},
    {"AnimScaleY", PropId_AnimScaleY},
    {"AnimSkewX", PropId_AnimSkewX},
    {"AnimSkewY", PropId_AnimSkewY},
    {"AnimTranslate3DZ", PropId_AnimTranslate3DZ},
    {"AnimTranslateX", PropId_AnimTranslateX},
    {"AnimTranslateY", PropId_AnimTranslateY},
    {"Background", PropId_Background},
    {"BackgroundGradientId", PropId_BackgroundGradientId},
    {"BackgroundOpacity", PropId_BackgroundOpacity},
    {"BlendingMode", PropId_BlendingMode},
    {"BlurEffect.Radius", PropId_EffectBlurRadius},
    {"BlurRadius", PropId_EffectBlurRadius},
    {"BorderBrush", PropId_BorderBrush},
    {"BorderGradientId", PropId_BorderGradientId},
    {"BorderOpacity", PropId_BorderOpacity},
    {"BorderThickness", PropId_BorderThickness},
    {"Bottom", PropId_CanvasBottom},
    {"BoxShadow.Color", PropId_EffectColor},
    {"Brush.Time", PropId_ShaderTime},
    {"ButtonBase.ClickMode", PropId_ClickMode},
    {"Canvas.Bottom", PropId_CanvasBottom},
    {"Canvas.Left", PropId_CanvasLeft},
    {"Canvas.Right", PropId_CanvasRight},
    {"Canvas.Top", PropId_CanvasTop},
    {"Canvas.ZIndex", PropId_ZIndex},
    {"CaretIndex", PropId_CaretIndex},
    {"CharacterSpacing", PropId_CharacterSpacing},
    {"CheckBox.IsThreeState", PropId_IsThreeState},
    {"ChromaticAberrationEffect.AmountX", PropId_EffectAmountX},
    {"ClickMode", PropId_ClickMode},
    {"ClipToBounds", PropId_ClipToBounds},
    {"Column", PropId_GridColumn},
    {"ColumnSpan", PropId_GridColumnSpan},
    {"Columns", PropId_UniformColumns},
    {"ComboBox.IsEditable", PropId_IsEditable},
    {"ComboBox.MaxDropDownHeight", PropId_MaxDropDownHeight},
    {"ComboBoxItem.IsSelected", PropId_IsChecked},
    {"CommandBindingsSpec", PropId_CommandBindingsSpec},
    {"CommandParameter", PropId_CommandParameter},
    {"ConstrainToParentBounds", PropId_MouseDragConstrain},
    {"Content", PropId_Text},
    {"ContentPresenter.ContentSource", PropId_ContentSource},
    {"ContentSource", PropId_ContentSource},
    {"ContentStringFormat", PropId_ContentStringFormat},
    {"ContentTemplate", PropId_ContentTemplate},
    {"ContextMenu", PropId_ContextMenuKey},
    {"ContextMenu.PlacementTarget", PropId_PlacementTarget},
    {"ContextMenuService.ContextMenu", PropId_ContextMenuKey},
    {"CornerRadius", PropId_CornerRadius},
    {"Custom.Index_Int32", PropId_CustomIndexInt32},
    {"Custom.Number_Single", PropId_Value},
    {"Custom.State_Int32", PropId_CustomStateInt32},
    {"Data", PropId_PathData},
    {"DataContext", PropId_DataContext},
    {"Delay", PropId_RepeatDelay},
    {"DirectionalBlurEffect.Angle", PropId_EffectAngle},
    {"DirectionalBlurEffect.Radius", PropId_EffectBlurRadius},
    {"DisplayMemberPath", PropId_DisplayMemberPath},
    {"Dock", PropId_Dock},
    {"DockPanel.Dock", PropId_Dock},
    {"DropShadowEffect.Color", PropId_EffectColor},
    {"DropShadowEffect.Opacity", PropId_EffectOpacity},
    {"EasingExponent", PropId_EasingExponent},
    {"Effect", PropId_EffectParams},
    {"Effect.AmountX", PropId_EffectAmountX},
    {"Effect.Angle", PropId_EffectAngle},
    {"Effect.Color", PropId_EffectColor},
    {"Effect.Opacity", PropId_EffectOpacity},
    {"Effect.Radius", PropId_EffectBlurRadius},
    {"Effect.Size", PropId_EffectBlurRadius},
    {"EffectAmountX", PropId_EffectAmountX},
    {"EffectAngle", PropId_EffectAngle},
    {"EffectBlurRadius", PropId_EffectBlurRadius},
    {"EffectColor", PropId_EffectColor},
    {"EffectKind", PropId_EffectKind},
    {"EffectOpacity", PropId_EffectOpacity},
    {"EffectShadowDepth", PropId_EffectShadowDepth},
    {"Element.BlendingMode", PropId_BlendingMode},
    {"Element.Transform3D", PropId_Transform3DRotateXYZ},
    {"ExpandDirection", PropId_ExpandDirection},
    {"Exponent", PropId_EasingExponent},
    {"ExponentialEase.Exponent", PropId_EasingExponent},
    {"Fill", PropId_Background},
    {"FillGradientId", PropId_BackgroundGradientId},
    {"FillOpacity", PropId_BackgroundOpacity},
    {"FirstColumn", PropId_FirstColumn},
    {"FlowDirection", PropId_FlowDirection},
    {"Focusable", PropId_Focusable},
    {"FontFamily", PropId_FontFamily},
    {"FontSize", PropId_FontSize},
    {"Foreground", PropId_Foreground},
    {"ForegroundGradientId", PropId_ForegroundGradientId},
    {"FrameworkElement.ActualHeight", PropId_ActualHeight},
    {"FrameworkElement.ActualWidth", PropId_ActualWidth},
    {"FrameworkElement.ContextMenu", PropId_ContextMenuKey},
    {"FrameworkElement.ToolTip", PropId_ToolTipText},
    {"GlobalOffsetZ", PropId_AnimTranslate3DZ},
    {"Grid.Column", PropId_GridColumn},
    {"Grid.ColumnSpan", PropId_GridColumnSpan},
    {"Grid.Row", PropId_GridRow},
    {"Grid.RowSpan", PropId_GridRowSpan},
    {"GridViewColumnsSpec", PropId_GridViewColumnsSpec},
    {"GroupName", PropId_GroupName},
    {"HasItems", PropId_HasItems},
    {"Header", PropId_Header},
    {"HeaderTemplate", PropId_HeaderTemplate},
    {"HeaderedContentControl.HeaderTemplate", PropId_HeaderTemplate},
    {"Height", PropId_Height},
    {"HorizontalAlignment", PropId_HorizontalAlignment},
    {"HorizontalContentAlignment", PropId_HorizontalContentAlignment},
    {"HorizontalOffset", PropId_HorizontalOffset},
    {"HorizontalScrollBarVisibility", PropId_HorizontalScrollBarVisibility},
    {"Hyperlink.NavigateUri", PropId_NavigateUri},
    {"ImageSource", PropId_ImageSource},
    {"Index_Int32", PropId_CustomIndexInt32},
    {"InputGestureText", PropId_InputGestureText},
    {"Interval", PropId_RepeatInterval},
    {"IsCheckable", PropId_IsCheckable},
    {"IsChecked", PropId_IsChecked},
    {"IsDirectionReversed", PropId_IsDirectionReversed},
    {"IsDropDownOpen", PropId_IsDropDownOpen},
    {"IsEditable", PropId_IsEditable},
    {"IsEnabled", PropId_IsEnabled},
    {"IsExpanded", PropId_IsExpanded},
    {"IsHitTestVisible", PropId_IsHitTestVisible},
    {"IsIndeterminate", PropId_IsIndeterminate},
    {"IsMoveToPointEnabled", PropId_IsMoveToPointEnabled},
    {"IsMuted", PropId_IsMuted},
    {"IsOpen", PropId_IsOpen},
    {"IsPassword", PropId_IsPassword},
    {"IsReadOnly", PropId_IsReadOnly},
    {"IsSelected", PropId_IsChecked},
    {"IsSnapToTickEnabled", PropId_IsSnapToTickEnabled},
    {"IsSubmenuOpen", PropId_IsSubmenuOpen},
    {"IsThreeState", PropId_IsThreeState},
    {"IsVirtualizing", PropId_IsVirtualizing},
    {"ItemContainerStyle", PropId_ItemContainerStyle},
    {"ItemHeight", PropId_ItemHeight},
    {"ItemTemplate", PropId_ItemTemplate},
    {"ItemWidth", PropId_ItemWidth},
    {"ItemsControl.AlternationCount", PropId_AlternationCount},
    {"ItemsControl.AlternationIndex", PropId_AlternationIndex},
    {"ItemsControl.DisplayMemberPath", PropId_DisplayMemberPath},
    {"ItemsControl.HasItems", PropId_HasItems},
    {"ItemsControl.ItemContainerStyle", PropId_ItemContainerStyle},
    {"ItemsControl.ItemsPanel", PropId_ItemsPanel},
    {"ItemsPanel", PropId_ItemsPanel},
    {"ItemsSource", PropId_ItemsSource},
    {"KeyBindingsSpec", PropId_KeyBindingsSpec},
    {"KeyboardNavigation.TabIndex", PropId_TabIndex},
    {"LargeChange", PropId_LargeChange},
    {"LastChildFill", PropId_LastChildFill},
    {"LayoutScaler.RefResolution", PropId_RefResolution},
    {"LayoutTransform", PropId_LayoutTransform},
    {"Left", PropId_CanvasLeft},
    {"LineHeight", PropId_LineHeight},
    {"ListBox.SelectionMode", PropId_SelectionMode},
    {"ListBoxItem.IsSelected", PropId_IsChecked},
    {"ListView.SelectionMode", PropId_SelectionMode},
    {"ListViewItem.IsSelected", PropId_IsChecked},
    {"LoadedBehavior", PropId_LoadedBehavior},
    {"Loc.Source", PropId_LocSource},
    {"LocalOffsetZ", PropId_AnimTranslate3DZ},
    {"Margin", PropId_Margin},
    {"MaxDropDownHeight", PropId_MaxDropDownHeight},
    {"MaxHeight", PropId_MaxHeight},
    {"MaxLength", PropId_MaxLength},
    {"MaxLines", PropId_MaxLines},
    {"MaxValue", PropId_Maximum},
    {"MaxWidth", PropId_MaxWidth},
    {"Maximum", PropId_Maximum},
    {"MediaElement.IsMuted", PropId_IsMuted},
    {"MediaElement.LoadedBehavior", PropId_LoadedBehavior},
    {"MediaElement.Position", PropId_MediaPosition},
    {"MediaElement.UnloadedBehavior", PropId_UnloadedBehavior},
    {"MediaElement.Volume", PropId_Volume},
    {"MediaPosition", PropId_MediaPosition},
    {"MenuItem.InputGestureText", PropId_InputGestureText},
    {"MenuItem.IsCheckable", PropId_IsCheckable},
    {"MenuItem.IsSubmenuOpen", PropId_IsSubmenuOpen},
    {"MergedDictionarySource", PropId_MergedDictionarySource},
    {"MinHeight", PropId_MinHeight},
    {"MinValue", PropId_Minimum},
    {"MinWidth", PropId_MinWidth},
    {"Minimum", PropId_Minimum},
    {"MouseDragConstrain", PropId_MouseDragConstrain},
    {"MouseDragElementBehavior.ConstrainToParentBounds", PropId_MouseDragConstrain},
    {"NavigateUri", PropId_NavigateUri},
    {"Number_Single", PropId_Value},
    {"Offset", PropId_Value},
    {"Opacity", PropId_Opacity},
    {"Orientation", PropId_Orientation},
    {"Padding", PropId_Padding},
    {"Panel.ZIndex", PropId_ZIndex},
    {"Password", PropId_Text},
    {"PasswordBox.Password", PropId_Text},
    {"PasswordBox.PasswordChar", PropId_PasswordChar},
    {"PasswordChar", PropId_PasswordChar},
    {"Path.StrokeDashArray", PropId_StrokeDashArray},
    {"Path.TrimEnd", PropId_PathTrimEnd},
    {"Path.TrimStart", PropId_PathTrimStart},
    {"PathData", PropId_PathData},
    {"PixelateEffect.Size", PropId_EffectBlurRadius},
    {"Placeholder", PropId_Placeholder},
    {"PlacementTarget", PropId_PlacementTarget},
    {"PlasmaBrush.Time", PropId_ShaderTime},
    {"Points", PropId_ShapePoints},
    {"Popup.IsOpen", PropId_IsOpen},
    {"Popup.PlacementTarget", PropId_PlacementTarget},
    {"Position", PropId_MediaPosition},
    {"Projection3D.Pitch", PropId_Projection3DPitch},
    {"Projection3D.Yaw", PropId_Projection3DYaw},
    {"Projection3DPitch", PropId_Projection3DPitch},
    {"Projection3DYaw", PropId_Projection3DYaw},
    {"RadiusX", PropId_RadiusX},
    {"RadiusY", PropId_RadiusY},
    {"RefResolution", PropId_RefResolution},
    {"RenderTransform", PropId_RenderTransform},
    {"RenderTransformMatrix", PropId_RenderTransformMatrix},
    {"RenderTransformOrigin", PropId_RenderTransformOrigin},
    {"RenderTransformTranslation", PropId_RenderTransformTranslation},
    {"RepeatButton.Delay", PropId_RepeatDelay},
    {"RepeatButton.Interval", PropId_RepeatInterval},
    {"RepeatDelay", PropId_RepeatDelay},
    {"RepeatInterval", PropId_RepeatInterval},
    {"RichText", PropId_RichText},
    {"RichText.Text", PropId_RichText},
    {"Right", PropId_CanvasRight},
    {"RiveControl.Source", PropId_RiveSource},
    {"RiveSource", PropId_RiveSource},
    {"RotateAngle", PropId_AnimRotateAngle},
    {"Rotation3DX", PropId_AnimRotation3DX},
    {"Rotation3DY", PropId_AnimRotation3DY},
    {"Rotation3DZ", PropId_AnimRotation3DZ},
    {"RotationX", PropId_AnimRotation3DX},
    {"RotationY", PropId_AnimRotation3DY},
    {"RotationZ", PropId_AnimRotation3DZ},
    {"RoutedCommandName", PropId_RoutedCommandName},
    {"Row", PropId_GridRow},
    {"RowSpan", PropId_GridRowSpan},
    {"Rows", PropId_UniformRows},
    {"Saturation", PropId_Saturation},
    {"SaturationBrush.Saturation", PropId_Saturation},
    {"SaturationEffect.Saturation", PropId_Saturation},
    {"ScaleX", PropId_AnimScaleX},
    {"ScaleY", PropId_AnimScaleY},
    {"ScrollViewer.HorizontalOffset", PropId_HorizontalOffset},
    {"ScrollViewer.HorizontalScrollBarVisibility", PropId_HorizontalScrollBarVisibility},
    {"ScrollViewer.VerticalOffset", PropId_VerticalOffset},
    {"ScrollViewer.VerticalScrollBarVisibility", PropId_VerticalScrollBarVisibility},
    {"SelectedIndex", PropId_SelectedIndex},
    {"SelectedItem", PropId_SelectedItem},
    {"SelectedValue", PropId_SelectedValue},
    {"SelectedValuePath", PropId_SelectedValuePath},
    {"SelectionLength", PropId_SelectionLength},
    {"SelectionMode", PropId_SelectionMode},
    {"SelectionStart", PropId_SelectionStart},
    {"Selector.IsSelected", PropId_IsChecked},
    {"Shader.Time", PropId_ShaderTime},
    {"ShaderTime", PropId_ShaderTime},
    {"ShadowDepth", PropId_EffectShadowDepth},
    {"Shape.StrokeDashArray", PropId_StrokeDashArray},
    {"SkewX", PropId_AnimSkewX},
    {"SkewY", PropId_AnimSkewY},
    {"Slider.IsDirectionReversed", PropId_IsDirectionReversed},
    {"Slider.IsMoveToPointEnabled", PropId_IsMoveToPointEnabled},
    {"Slider.IsSnapToTickEnabled", PropId_IsSnapToTickEnabled},
    {"Slider.TickFrequency", PropId_TickFrequency},
    {"Slider.TickPlacement", PropId_TickPlacement},
    {"Slider.Ticks", PropId_Ticks},
    {"SmallChange", PropId_SmallChange},
    {"Source", PropId_ImageSource},
    {"Spacing", PropId_Spacing},
    {"State_Int32", PropId_CustomStateInt32},
    {"StepValue", PropId_SmallChange},
    {"Storyboard.TargetName", PropId_Header},
    {"Stretch", PropId_Stretch},
    {"StretchDirection", PropId_StretchDirection},
    {"Stroke", PropId_BorderBrush},
    {"StrokeDashArray", PropId_StrokeDashArray},
    {"StrokeGradientId", PropId_BorderGradientId},
    {"StrokeOpacity", PropId_BorderOpacity},
    {"StrokeThickness", PropId_StrokeThickness},
    {"Style", PropId_Style},
    {"TabIndex", PropId_TabIndex},
    {"TabItem.IsSelected", PropId_IsChecked},
    {"TabStripPlacement", PropId_TabStripPlacement},
    {"Tag", PropId_Tag},
    {"TargetName", PropId_Header},
    {"Template", PropId_Template},
    {"TemplatedParent", PropId_TemplatedParent},
    {"Text", PropId_Text},
    {"Text.CharacterSpacing", PropId_CharacterSpacing},
    {"TextAlignment", PropId_TextAlignment},
    {"TextBlock.LineHeight", PropId_LineHeight},
    {"TextBlock.MaxLines", PropId_MaxLines},
    {"TextBlock.TextAlignment", PropId_TextAlignment},
    {"TextBlock.TextDecorations", PropId_TextDecorations},
    {"TextBlock.TextTrimming", PropId_TextTrimming},
    {"TextBox.AcceptsReturn", PropId_AcceptsReturn},
    {"TextBox.MaxLines", PropId_MaxLines},
    {"TextBox.SelectionLength", PropId_SelectionLength},
    {"TextBox.SelectionStart", PropId_SelectionStart},
    {"TextDecorations", PropId_TextDecorations},
    {"TextElement.FontFamily", PropId_FontFamily},
    {"TextElement.FontSize", PropId_FontSize},
    {"TextTrimming", PropId_TextTrimming},
    {"TextWrapping", PropId_TextWrapping},
    {"TickFrequency", PropId_TickFrequency},
    {"TickPlacement", PropId_TickPlacement},
    {"Ticks", PropId_Ticks},
    {"Time", PropId_ShaderTime},
    {"TintEffect.Color", PropId_EffectColor},
    {"Title", PropId_Title},
    {"ToggleButton.IsThreeState", PropId_IsThreeState},
    {"ToolTip", PropId_ToolTipText},
    {"ToolTip.IsOpen", PropId_IsOpen},
    {"ToolTipService.ToolTip", PropId_ToolTipText},
    {"Top", PropId_CanvasTop},
    {"Track.IsDirectionReversed", PropId_IsDirectionReversed},
    {"Transform3D", PropId_Transform3DRotateXYZ},
    {"TranslateX", PropId_AnimTranslateX},
    {"TranslateY", PropId_AnimTranslateY},
    {"TranslateZ", PropId_AnimTranslate3DZ},
    {"TreeDepthIndent", PropId_TreeDepthIndent},
    {"TreeViewItem.HasItems", PropId_HasItems},
    {"TreeViewItem.IsSelected", PropId_IsChecked},
    {"TrimEnd", PropId_PathTrimEnd},
    {"TrimStart", PropId_PathTrimStart},
    {"UIElement.Effect", PropId_EffectParams},
    {"UIElement.Effect.AmountX", PropId_EffectAmountX},
    {"UIElement.Effect.Angle", PropId_EffectAngle},
    {"UIElement.Effect.Color", PropId_EffectColor},
    {"UIElement.Effect.Radius", PropId_EffectBlurRadius},
    {"UIElement.Effect.Size", PropId_EffectBlurRadius},
    {"UIElement.IsHitTestVisible", PropId_IsHitTestVisible},
    {"UnloadedBehavior", PropId_UnloadedBehavior},
    {"Value", PropId_Value},
    {"VerticalAlignment", PropId_VerticalAlignment},
    {"VerticalContentAlignment", PropId_VerticalContentAlignment},
    {"VerticalOffset", PropId_VerticalOffset},
    {"VerticalScrollBarVisibility", PropId_VerticalScrollBarVisibility},
    {"VirtualizingStackPanel.IsVirtualizing", PropId_IsVirtualizing},
    {"Visibility", PropId_Visibility},
    {"VisualBrush.Visual", PropId_VisualBrush},
    {"Volume", PropId_Volume},
    {"Width", PropId_Width},
    {"X1", PropId_LineX1},
    {"X2", PropId_LineX2},
    {"Y1", PropId_LineY1},
    {"Y2", PropId_LineY2},
    {"ZIndex", PropId_ZIndex},
    {"local:Text.CharacterSpacing", PropId_CharacterSpacing},
    {"local:Text.Placeholder", PropId_Placeholder},
    {"noesis:BoxShadow.Color", PropId_EffectColor},
    {"noesis:ChromaticAberrationEffect.AmountX", PropId_EffectAmountX},
    {"noesis:DirectionalBlurEffect.Angle", PropId_EffectAngle},
    {"noesis:DirectionalBlurEffect.Radius", PropId_EffectBlurRadius},
    {"noesis:Element.BlendingMode", PropId_BlendingMode},
    {"noesis:Element.Transform3D", PropId_Transform3DRotateXYZ},
    {"noesis:LayoutScaler.RefResolution", PropId_RefResolution},
    {"noesis:Loc.Source", PropId_LocSource},
    {"noesis:Path.TrimEnd", PropId_PathTrimEnd},
    {"noesis:Path.TrimStart", PropId_PathTrimStart},
    {"noesis:PixelateEffect.Size", PropId_EffectBlurRadius},
    {"noesis:PlasmaBrush.Time", PropId_ShaderTime},
    {"noesis:RichText.Text", PropId_RichText},
    {"noesis:RiveControl.Source", PropId_RiveSource},
    {"noesis:SaturationBrush.Saturation", PropId_Saturation},
    {"noesis:SaturationEffect.Saturation", PropId_Saturation},
    {"noesis:Text.CharacterSpacing", PropId_CharacterSpacing},
    {"noesis:Text.Placeholder", PropId_Placeholder},
    {"noesis:TintEffect.Color", PropId_EffectColor},
}};

template <std::size_t N>
[[nodiscard]] constexpr bool IsStrictlySortedUnique(const std::array<PropIdMapping, N>& arr) noexcept {
    for (std::size_t i = 1u; i < N; ++i) {
        if (!(arr[i - 1u].name < arr[i].name)) {
            return false;
        }
    }
    return true;
}

static_assert(
    IsStrictlySortedUnique(kBuiltinPropertyTable),
    "kBuiltinPropertyTable must be strictly sorted in ASCII ordinal order with unique keys");

[[nodiscard]] bool TryLookupBuiltinPropertyId(
    std::string_view propertyName,
    std::uint16_t& outPropId) noexcept {
    std::size_t lo = 0u;
    std::size_t hi = kBuiltinPropertyTable.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        const std::string_view midName = kBuiltinPropertyTable[mid].name;
        if (midName < propertyName) {
            lo = mid + 1u;
        } else if (propertyName < midName) {
            hi = mid;
        } else {
            outPropId = kBuiltinPropertyTable[mid].propId;
            return true;
        }
    }
    return false;
}

} // namespace

Color ParseXamlColorLiteral(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (!raw.empty() && raw.front() == '#') {
        raw.remove_prefix(1);
        if (raw.size() == 3) {
            const float r = static_cast<float>(HexNibble(raw[0]) * 17) / 255.0f;
            const float g = static_cast<float>(HexNibble(raw[1]) * 17) / 255.0f;
            const float b = static_cast<float>(HexNibble(raw[2]) * 17) / 255.0f;
            return Color{r, g, b, 1.0f};
        }
        return ParseHexColor(raw);
    }
    struct NamedColorEntry {
        std::string_view name;
        std::string_view hex8;
    };
    static constexpr NamedColorEntry kNamedColors[] = {
        {"Transparent", "00000000"},
        {"White", "FFFFFFFF"},
        {"Black", "FF000000"},
        {"Red", "FFFF0000"},
        {"Green", "FF008000"},
        {"Blue", "FF0000FF"},
        {"Yellow", "FFFFFF00"},
        {"Cyan", "FF00FFFF"},
        {"Aqua", "FF00FFFF"},
        {"Magenta", "FFFF00FF"},
        {"Fuchsia", "FFFF00FF"},
        {"Gray", "FF808080"},
        {"Grey", "FF808080"},
        {"LightGray", "FFD3D3D3"},
        {"DarkGray", "FFA9A9A9"},
        {"Silver", "FFC0C0C0"},
        {"WhiteSmoke", "FFF5F5F5"},
        {"LightSeaGreen", "FF20B2AA"},
        {"MediumSeaGreen", "FF3CB371"},
        {"SeaGreen", "FF2E8B57"},
        {"Turquoise", "FF40E0D0"},
        {"MediumTurquoise", "FF48D1CC"},
        {"DarkTurquoise", "FF00CED1"},
        {"DodgerBlue", "FF1E90FF"},
        {"DeepSkyBlue", "FF00BFFF"},
        {"SkyBlue", "FF87CEEB"},
        {"LightSkyBlue", "FF87CEFA"},
        {"CornflowerBlue", "FF6495ED"},
        {"RoyalBlue", "FF4169E1"},
        {"SteelBlue", "FF4682B4"},
        {"Teal", "FF008080"},
        {"OrangeRed", "FFFF4500"},
        {"Orange", "FFFFA500"},
        {"Gold", "FFFFD700"},
        {"Crimson", "FFDC143C"},
        {"Tomato", "FFFF6347"},
        {"Coral", "FFFF7F50"},
        {"Salmon", "FFFA8072"},
        {"Lime", "FF00FF00"},
        {"LimeGreen", "FF32CD32"},
        {"LawnGreen", "FF7CFC00"},
        {"GreenYellow", "FFADFF2F"},
        {"Purple", "FF800080"},
        {"Violet", "FFEE82EE"},
        {"Plum", "FFDDA0DD"},
        {"Indigo", "FF4B0082"},
        {"SlateGray", "FF708090"},
        {"DarkSlateGray", "FF2F4F4F"},
        {"Brown", "FFA52A2A"},
        {"SaddleBrown", "FF8B4513"},
        {"Tan", "FFD2B48C"},
        {"Wheat", "FFF5DEB3"},
        {"Beige", "FFF5F5DC"},
        {"Ivory", "FFFFFFF0"},
        {"Navy", "FF000080"},
        {"MidnightBlue", "FF191970"},
        {"Maroon", "FF800000"},
        {"Olive", "FF808000"},
        {"Pink", "FFFFC0CB"},
        {"HotPink", "FFFF69B4"},
        {"DeepPink", "FFFF1493"},
        {"Moccasin", "FFFFE4B5"},
        {"Aquamarine", "FF7FFFD4"},
        {"Thistle", "FFD8BFD8"},
        {"CadetBlue", "FF5F9EA0"},
        {"PaleTurquoise", "FFAFEEEE"},
        {"YellowGreen", "FF9ACD32"},
        {"AliceBlue", "FFF0F8FF"},
        {"Khaki", "FFF0E68C"},
        {"Orchid", "FFDA70D6"},
        {"SlateGrey", "FF708090"},
        {"LightSlateGray", "FF778899"},
        {"LightSlateGrey", "FF778899"},
        {"DarkSlateGrey", "FF2F4F4F"},
        {"DimGray", "FF696969"},
        {"DimGrey", "FF696969"},
        {"Gainsboro", "FFDCDCDC"},
    };
    for (const auto& entry : kNamedColors) {
        if (str::EqualsIgnoreCase(raw, entry.name)) {
            return ParseHexColor(entry.hex8);
        }
    }
    const auto vals = detail::ParseFloatList(raw);
    if (vals.size() >= 4) return Color{vals[0], vals[1], vals[2], vals[3]};
    if (vals.size() == 3) return Color{vals[0], vals[1], vals[2], 1.0f};
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

std::string DecodeXamlXmlEntities(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) {
        return std::string(raw);
    }
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] == '&') {
            if (raw.substr(i, 5) == "&amp;") {
                out.push_back('&');
                i += 5;
                continue;
            }
            if (raw.substr(i, 4) == "&lt;") {
                out.push_back('<');
                i += 4;
                continue;
            }
            if (raw.substr(i, 4) == "&gt;") {
                out.push_back('>');
                i += 4;
                continue;
            }
            if (raw.substr(i, 6) == "&quot;") {
                out.push_back('"');
                i += 6;
                continue;
            }
            if (raw.substr(i, 6) == "&apos;") {
                out.push_back('\'');
                i += 6;
                continue;
            }
        }
        out.push_back(raw[i]);
        ++i;
    }
    return out;
}

Matrix2D ParseXamlMatrix2DLiteral(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (raw.empty() || str::EqualsIgnoreCase(raw, "Identity")) {
        return Matrix2D::Identity();
    }
    const auto nums = detail::ParseFloatList(raw);
    if (nums.size() >= 6u) {
        return Matrix2D::FromWpfMatrix(nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]);
    }
    if (nums.size() >= 4u) {
        return Matrix2D::FromWpfMatrix(nums[0], nums[1], nums[2], nums[3], 0.0f, 0.0f);
    }
    return Matrix2D::Identity();
}

std::uint16_t ResolveBuiltinPropertyId(std::string_view propertyName) noexcept {
    propertyName = str::Trim(propertyName);
    if (propertyName.size() >= 2u && propertyName.front() == '(' && propertyName.back() == ')') {
        propertyName = str::Trim(propertyName.substr(1u, propertyName.size() - 2u));
    }
    std::uint16_t foundId = 0u;
    if (TryLookupBuiltinPropertyId(propertyName, foundId)) {
        return foundId;
    }
    if (propertyName != "Tag") {
        if (const std::size_t dot = propertyName.rfind('.'); dot != std::string_view::npos) {
            const std::uint16_t customId =
                FindCustomProperty(propertyName.substr(0u, dot), propertyName.substr(dot + 1u));
            if (customId != 0xFFFFu) {
                return customId;
            }
        }
        if (const std::uint16_t customId = FindCustomPropertyByName(propertyName); customId != 0xFFFFu) {
            return customId;
        }
    }
    return PropId_Tag;
}

std::uint16_t ResolveTriggerConditionPropertyId(std::string_view propertyName) noexcept {
    propertyName = str::Trim(propertyName);
    while (!propertyName.empty() && propertyName.front() == '(') {
        propertyName.remove_prefix(1);
    }
    while (!propertyName.empty() &&
           (propertyName.back() == ')' || str::detail::IsAsciiSpace(propertyName.back()))) {
        propertyName.remove_suffix(1);
    }
    propertyName = str::Trim(propertyName);
    const std::uint16_t fullBuiltin = ResolveBuiltinPropertyId(propertyName);
    if (fullBuiltin != PropId_Tag || propertyName == "Tag") {
        return fullBuiltin;
    }
    if (const auto dot = propertyName.rfind('.'); dot != std::string_view::npos) {
        propertyName = propertyName.substr(dot + 1);
    }
    if (propertyName == "IsMouseOver" || propertyName == "IsHovered") {
        return PropId_TriggerIsMouseOver;
    }
    if (propertyName == "IsPressed" || propertyName == "IsDragging") {
        return PropId_TriggerIsPressed;
    }
    if (propertyName == "IsFocused" || propertyName == "IsKeyboardFocused") {
        return PropId_TriggerIsFocused;
    }
    if (propertyName == "IsKeyboardFocusWithin") {
        return PropId_TriggerIsKeyboardFocusWithin;
    }
    return ResolveBuiltinPropertyId(propertyName);
}

std::uint16_t ResolveAnimationTargetPropertyId(std::string_view targetPropertyPath) noexcept {
    std::string_view s = str::Trim(targetPropertyPath);
    if (s.empty()) {
        return PropId_Tag;
    }
    // Check CustomPropertyRegistry first for single-hop qualified or unqualified custom properties
    if (s.find(").(") == std::string_view::npos) {
        std::string_view cleaned = s;
        while (!cleaned.empty() && cleaned.front() == '(') cleaned.remove_prefix(1u);
        while (!cleaned.empty() && cleaned.back() == ')') cleaned.remove_suffix(1u);
        cleaned = str::Trim(cleaned);
        if (const std::size_t dot = cleaned.rfind('.'); dot != std::string_view::npos) {
            const std::uint16_t customId =
                FindCustomProperty(cleaned.substr(0u, dot), cleaned.substr(dot + 1u));
            if (customId != 0xFFFFu) {
                return customId;
            }
        } else if (const std::uint16_t customId = FindCustomPropertyByName(cleaned); customId != 0xFFFFu) {
            return customId;
        }
    }
    const bool mentionsTransform3D =
        (s.find("Transform3D") != std::string_view::npos) ||
        (s.find("Projection") != std::string_view::npos);
    if (mentionsTransform3D || s.find("RotationX") != std::string_view::npos ||
        s.find("RotationY") != std::string_view::npos || s.find("RotationZ") != std::string_view::npos ||
        s.find("TranslateZ") != std::string_view::npos) {
        if (s.find("RotationX") != std::string_view::npos) return PropId_AnimRotation3DX;
        if (s.find("RotationY") != std::string_view::npos) return PropId_AnimRotation3DY;
        if (s.find("RotationZ") != std::string_view::npos) return PropId_AnimRotation3DZ;
        if (s.find("TranslateX") != std::string_view::npos || s.find("OffsetX") != std::string_view::npos) {
            return PropId_AnimTranslate3DX;
        }
        if (s.find("TranslateY") != std::string_view::npos || s.find("OffsetY") != std::string_view::npos) {
            return PropId_AnimTranslate3DY;
        }
        if (s.find("TranslateZ") != std::string_view::npos || s.find("OffsetZ") != std::string_view::npos) {
            return PropId_AnimTranslate3DZ;
        }
    }
    if (s.find("BlurRadius") != std::string_view::npos ||
        (s.find("BlurEffect") != std::string_view::npos && s.find("Radius") != std::string_view::npos) ||
        (s.find("PixelateEffect") != std::string_view::npos && s.find("Size") != std::string_view::npos) ||
        (s.find("Effect") != std::string_view::npos &&
         (s.find("Radius") != std::string_view::npos || s.find("Size") != std::string_view::npos))) {
        return PropId_EffectBlurRadius;
    }
    if (s.find("ShadowDepth") != std::string_view::npos) {
        return PropId_EffectShadowDepth;
    }
    if (s.find("DropShadowEffect.Opacity") != std::string_view::npos ||
        s.find("Effect.Opacity") != std::string_view::npos) {
        return PropId_EffectOpacity;
    }
    if (s.find("AmountX") != std::string_view::npos) {
        return PropId_EffectAmountX;
    }
    if (s.find("Effect") != std::string_view::npos && s.find("Angle") != std::string_view::npos) {
        return PropId_EffectAngle;
    }
    if (s.find("Effect") != std::string_view::npos && s.find("Color") != std::string_view::npos) {
        return PropId_EffectColor;
    }
    if (s.find("Saturation") != std::string_view::npos) {
        return PropId_Saturation;
    }
    if (s.find("TrimStart") != std::string_view::npos) {
        return PropId_PathTrimStart;
    }
    if (s.find("TrimEnd") != std::string_view::npos) {
        return PropId_PathTrimEnd;
    }
    if (s.find("ShaderTime") != std::string_view::npos ||
        s.find("PlasmaBrush.Time") != std::string_view::npos ||
        s.find(".Time)") != std::string_view::npos ||
        s == "Time") {
        return PropId_ShaderTime;
    }

    const bool mentionsRenderTransform =
        (s.find("RenderTransform") != std::string_view::npos) ||
        (s.find("ScaleTransform") != std::string_view::npos) ||
        (s.find("RotateTransform") != std::string_view::npos) ||
        (s.find("TranslateTransform") != std::string_view::npos);

    if (s.find("ScaleX") != std::string_view::npos) return PropId_AnimScaleX;
    if (s.find("ScaleY") != std::string_view::npos) return PropId_AnimScaleY;
    if (s.find("SkewX") != std::string_view::npos || s.find("AngleX") != std::string_view::npos) {
        return PropId_AnimSkewX;
    }
    if (s.find("SkewY") != std::string_view::npos || s.find("AngleY") != std::string_view::npos) {
        return PropId_AnimSkewY;
    }
    if (s.find("Angle") != std::string_view::npos) return PropId_AnimRotateAngle;
    if (s.find("TranslateX") != std::string_view::npos) return PropId_AnimTranslateX;
    if (s.find("TranslateY") != std::string_view::npos) return PropId_AnimTranslateY;
    if (mentionsRenderTransform) {
        if (s.find(".X)") != std::string_view::npos || s.rfind(".X") == s.size() - 2u) {
            return PropId_AnimTranslateX;
        }
        if (s.find(".Y)") != std::string_view::npos || s.rfind(".Y") == s.size() - 2u) {
            return PropId_AnimTranslateY;
        }
    }
    const bool mentionsBrushOpacity =
        (s.find("Brush.Opacity") != std::string_view::npos) ||
        (s.find(".Opacity") != std::string_view::npos && s.find("UIElement.Opacity") == std::string_view::npos &&
         s.find("Effect.Opacity") == std::string_view::npos);
    if (mentionsBrushOpacity) {
        if (s.find("BorderBrush") != std::string_view::npos || s.find("Stroke") != std::string_view::npos) {
            return PropId_BorderOpacity;
        }
        if (s.find("Background") != std::string_view::npos || s.find("Fill") != std::string_view::npos) {
            return PropId_BackgroundOpacity;
        }
    }
    const bool isSubPropertyBrushPath =
        (s.find("RelativeTransform") != std::string_view::npos) ||
        (s.find(".Transform.") != std::string_view::npos) ||
        (s.find("Brush.Shader") != std::string_view::npos);
    if (!isSubPropertyBrushPath &&
        (s.find("Background") != std::string_view::npos || s.find("Fill") != std::string_view::npos)) {
        return PropId_Background;
    }
    if (s.find("Foreground") != std::string_view::npos) {
        return PropId_Foreground;
    }
    if (!isSubPropertyBrushPath &&
        (s.find("BorderBrush") != std::string_view::npos || s.find("Stroke") != std::string_view::npos)) {
        return PropId_BorderBrush;
    }
    // Strip parens and take last leaf token e.g. "(UIElement.Opacity)" -> "Opacity"
    while (!s.empty() && (s.back() == ')' || str::detail::IsAsciiSpace(s.back()))) {
        s.remove_suffix(1);
    }
    if (const auto lastDot = s.rfind('.'); lastDot != std::string_view::npos) {
        s = s.substr(lastDot + 1);
    }
    while (!s.empty() && s.front() == '(') {
        s.remove_prefix(1);
    }
    return ResolveBuiltinPropertyId(s);
}

float ParseXamlTimeSpanSeconds(std::string_view rawTimeSpan, float fallbackSeconds) noexcept {
    std::string_view s = str::Trim(rawTimeSpan);
    if (s.empty() || str::EqualsIgnoreCase(s, "Automatic") || str::EqualsIgnoreCase(s, "Forever")) {
        return fallbackSeconds;
    }
    if (s.size() > 2u && str::EqualsIgnoreCase(s.substr(s.size() - 2u), "ms")) {
        return std::max(0.0f, detail::ParseFloat32Token(s.substr(0, s.size() - 2u), fallbackSeconds * 1000.0f) * 0.001f);
    }
    if (s.size() > 1u && (s.back() == 's' || s.back() == 'S')) {
        return std::max(0.0f, detail::ParseFloat32Token(s.substr(0, s.size() - 1u), fallbackSeconds));
    }
    // Format H:M:S.fff (or D.H:M:S.fff)
    const std::size_t firstColon = s.find(':');
    if (firstColon == std::string_view::npos) {
        return std::max(0.0f, detail::ParseFloat32Token(s, fallbackSeconds));
    }
    const std::size_t secondColon = s.find(':', firstColon + 1u);
    if (secondColon == std::string_view::npos) {
        const float mins = detail::ParseFloat32Token(s.substr(0, firstColon), 0.0f);
        const float secs = detail::ParseFloat32Token(s.substr(firstColon + 1u), 0.0f);
        return std::max(0.0f, mins * 60.0f + secs);
    }
    std::string_view hPart = s.substr(0, firstColon);
    float days = 0.0f;
    if (const std::size_t dayDot = hPart.find('.'); dayDot != std::string_view::npos) {
        days = detail::ParseFloat32Token(hPart.substr(0, dayDot), 0.0f);
        hPart = hPart.substr(dayDot + 1u);
    }
    const float hours = detail::ParseFloat32Token(hPart, 0.0f);
    const float mins = detail::ParseFloat32Token(s.substr(firstColon + 1u, secondColon - firstColon - 1u), 0.0f);
    const float secs = detail::ParseFloat32Token(s.substr(secondColon + 1u), 0.0f);
    return std::max(0.0f, ((days * 24.0f + hours) * 60.0f + mins) * 60.0f + secs);
}

EasingFunctionKind ParseEasingFunctionKind(std::string_view tagName) noexcept {
    tagName = str::StripNamespacePrefix(str::Trim(tagName));
    if (tagName.find("Quadratic") != std::string_view::npos) return EasingFunctionKind::Quadratic;
    if (tagName.find("Cubic") != std::string_view::npos) return EasingFunctionKind::Cubic;
    if (tagName.find("Quartic") != std::string_view::npos) return EasingFunctionKind::Quartic;
    if (tagName.find("Quintic") != std::string_view::npos) return EasingFunctionKind::Quintic;
    if (tagName.find("Sine") != std::string_view::npos) return EasingFunctionKind::Sine;
    if (tagName.find("Circle") != std::string_view::npos) return EasingFunctionKind::Circle;
    if (tagName.find("Exponential") != std::string_view::npos) return EasingFunctionKind::Exponential;
    if (tagName.find("Back") != std::string_view::npos) return EasingFunctionKind::Back;
    if (tagName.find("Bounce") != std::string_view::npos) return EasingFunctionKind::Bounce;
    if (tagName.find("Elastic") != std::string_view::npos) return EasingFunctionKind::Elastic;
    if (tagName.find("Spline") != std::string_view::npos) return EasingFunctionKind::Spline;
    return EasingFunctionKind::Linear;
}

EasingMode ParseEasingMode(std::string_view rawMode) noexcept {
    rawMode = str::Trim(rawMode);
    if (str::EqualsIgnoreCase(rawMode, "EaseIn") || rawMode == "1") return EasingMode::EaseIn;
    if (str::EqualsIgnoreCase(rawMode, "EaseInOut") || rawMode == "2") return EasingMode::EaseInOut;
    return EasingMode::EaseOut;
}

TriggerComparisonOp ParseTriggerComparisonOp(std::string_view rawOp) noexcept {
    const std::string_view s = str::Trim(rawOp);
    if (s.empty() || str::EqualsIgnoreCase(s, "Equal") || s == "==" || str::EqualsIgnoreCase(s, "Eq")) {
        return TriggerComparisonOp::Equal;
    }
    if (str::EqualsIgnoreCase(s, "NotEqual") || s == "!=" || s == "<>" || str::EqualsIgnoreCase(s, "Ne")) {
        return TriggerComparisonOp::NotEqual;
    }
    if (str::EqualsIgnoreCase(s, "LessThan") || s == "<" || str::EqualsIgnoreCase(s, "Lt")) {
        return TriggerComparisonOp::LessThan;
    }
    if (str::EqualsIgnoreCase(s, "LessThanOrEqual") || s == "<=" || str::EqualsIgnoreCase(s, "Le")) {
        return TriggerComparisonOp::LessThanOrEqual;
    }
    if (str::EqualsIgnoreCase(s, "GreaterThan") || s == ">" || str::EqualsIgnoreCase(s, "Gt")) {
        return TriggerComparisonOp::GreaterThan;
    }
    if (str::EqualsIgnoreCase(s, "GreaterThanOrEqual") || s == ">=" || str::EqualsIgnoreCase(s, "Ge")) {
        return TriggerComparisonOp::GreaterThanOrEqual;
    }
    return TriggerComparisonOp::Equal;
}

ControlStoryboardOption ParseControlStoryboardOption(std::string_view rawOpt) noexcept {
    const std::string_view s = str::Trim(rawOpt);
    if (str::EqualsIgnoreCase(s, "Stop")) {
        return ControlStoryboardOption::Stop;
    }
    if (str::EqualsIgnoreCase(s, "TogglePlayPause") ||
        str::EqualsIgnoreCase(s, "Pause") ||
        str::EqualsIgnoreCase(s, "Resume")) {
        return ControlStoryboardOption::TogglePlayPause;
    }
    return ControlStoryboardOption::Play;
}

RoutedTriggerEvent ParseRoutedTriggerEvent(
    std::string_view rawRoutedEvent,
    bool& outRecognized) noexcept {
    outRecognized = false;
    std::string_view s = str::Trim(rawRoutedEvent);
    if (s.empty()) {
        outRecognized = true;
        return RoutedTriggerEvent::Loaded;
    }
    if (const auto dot = s.rfind('.'); dot != std::string_view::npos) {
        s = s.substr(dot + 1u);
    }
    if (str::EqualsIgnoreCase(s, "Loaded")) {
        outRecognized = true;
        return RoutedTriggerEvent::Loaded;
    }
    if (str::EqualsIgnoreCase(s, "Click")) {
        outRecognized = true;
        return RoutedTriggerEvent::Click;
    }
    if (str::EqualsIgnoreCase(s, "MouseEnter")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseEnter;
    }
    if (str::EqualsIgnoreCase(s, "MouseLeave")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseLeave;
    }
    if (str::EqualsIgnoreCase(s, "Checked")) {
        outRecognized = true;
        return RoutedTriggerEvent::Checked;
    }
    if (str::EqualsIgnoreCase(s, "Unchecked")) {
        outRecognized = true;
        return RoutedTriggerEvent::Unchecked;
    }
    if (str::EqualsIgnoreCase(s, "SelectionChanged")) {
        outRecognized = true;
        return RoutedTriggerEvent::SelectionChanged;
    }
    if (str::EqualsIgnoreCase(s, "ValueChanged") || str::EqualsIgnoreCase(s, "TextChanged") ||
        str::EqualsIgnoreCase(s, "PasswordChanged")) {
        outRecognized = true;
        return RoutedTriggerEvent::ValueChanged;
    }
    if (str::EqualsIgnoreCase(s, "SelectedItemChanged")) {
        outRecognized = true;
        return RoutedTriggerEvent::SelectionChanged;
    }
    if (str::EqualsIgnoreCase(s, "Expanded")) {
        outRecognized = true;
        return RoutedTriggerEvent::Checked;
    }
    if (str::EqualsIgnoreCase(s, "Collapsed")) {
        outRecognized = true;
        return RoutedTriggerEvent::Unchecked;
    }
    if (str::EqualsIgnoreCase(s, "Indeterminate")) {
        outRecognized = true;
        return RoutedTriggerEvent::Indeterminate;
    }
    if (str::EqualsIgnoreCase(s, "GotFocus") || str::EqualsIgnoreCase(s, "GotKeyboardFocus")) {
        outRecognized = true;
        return RoutedTriggerEvent::GotFocus;
    }
    if (str::EqualsIgnoreCase(s, "LostFocus") || str::EqualsIgnoreCase(s, "LostKeyboardFocus")) {
        outRecognized = true;
        return RoutedTriggerEvent::LostFocus;
    }
    if (str::EqualsIgnoreCase(s, "MouseDown") || str::EqualsIgnoreCase(s, "MouseLeftButtonDown") ||
        str::EqualsIgnoreCase(s, "PreviewMouseDown") || str::EqualsIgnoreCase(s, "PreviewMouseLeftButtonDown")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseDown;
    }
    if (str::EqualsIgnoreCase(s, "MouseUp") || str::EqualsIgnoreCase(s, "MouseLeftButtonUp") ||
        str::EqualsIgnoreCase(s, "PreviewMouseUp") || str::EqualsIgnoreCase(s, "PreviewMouseLeftButtonUp")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseUp;
    }
    if (str::EqualsIgnoreCase(s, "KeyDown") || str::EqualsIgnoreCase(s, "PreviewKeyDown") ||
        str::EqualsIgnoreCase(s, "KeyTrigger") || str::EqualsIgnoreCase(s, "KeyDownTrigger") ||
        str::EqualsIgnoreCase(s, "KeyUp") || str::EqualsIgnoreCase(s, "PreviewKeyUp")) {
        outRecognized = true;
        return RoutedTriggerEvent::KeyDownTrigger;
    }
    if (str::EqualsIgnoreCase(s, "TimerTrigger") || str::EqualsIgnoreCase(s, "TimerTick") ||
        str::EqualsIgnoreCase(s, "Tick")) {
        outRecognized = true;
        return RoutedTriggerEvent::TimerTrigger;
    }
    if (str::EqualsIgnoreCase(s, "MediaEnded")) {
        outRecognized = true;
        return RoutedTriggerEvent::MediaEnded;
    }
    return RoutedTriggerEvent::Loaded;
}

UnifiedValue ParseBuiltinLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue) {
    rawValue = str::Trim(rawValue);
    switch (propId) {
        case PropId_Width:
        case PropId_Height:
        case PropId_ItemWidth:
        case PropId_ItemHeight:
            if (str::EqualsIgnoreCase(rawValue, "Auto") || str::EqualsIgnoreCase(rawValue, "NaN")) {
                return MakeFloat(-1.0f);
            }
            return MakeFloat(detail::ParseFloat32Token(rawValue, -1.0f));

        case PropId_MaxWidth:
        case PropId_MaxHeight:
            if (str::EqualsIgnoreCase(rawValue, "Infinity") ||
                str::EqualsIgnoreCase(rawValue, "+Infinity") ||
                str::EqualsIgnoreCase(rawValue, "Auto") ||
                str::EqualsIgnoreCase(rawValue, "NaN")) {
                return MakeFloat(std::numeric_limits<float>::infinity());
            }
            return MakeFloat(detail::ParseFloat32Token(rawValue, std::numeric_limits<float>::infinity()));

        case PropId_MinWidth:
        case PropId_MinHeight:
        case PropId_Opacity:
        case PropId_FontSize:
        case PropId_CanvasLeft:
        case PropId_CanvasTop:
        case PropId_CanvasRight:
        case PropId_CanvasBottom:
        case PropId_EffectOpacity:
        case PropId_Projection3DYaw:
        case PropId_Projection3DPitch:
        case PropId_Spacing:
        case PropId_Value:
        case PropId_Minimum:
        case PropId_Maximum:
        case PropId_HorizontalOffset:
        case PropId_VerticalOffset:
        case PropId_SmallChange:
        case PropId_LargeChange:
        case PropId_AnimScaleX:
        case PropId_AnimScaleY:
        case PropId_AnimRotateAngle:
        case PropId_AnimTranslateX:
        case PropId_AnimTranslateY:
        case PropId_AnimSkewX:
        case PropId_AnimSkewY:
        case PropId_RadiusX:
        case PropId_RadiusY:
        case PropId_StrokeThickness:
        case PropId_LineX1:
        case PropId_LineY1:
        case PropId_LineX2:
        case PropId_LineY2:
        case PropId_AnimRotation3DX:
        case PropId_AnimRotation3DY:
        case PropId_AnimRotation3DZ:
        case PropId_AnimTranslate3DX:
        case PropId_AnimTranslate3DY:
        case PropId_AnimTranslate3DZ:
        case PropId_EffectBlurRadius:
        case PropId_EffectShadowDepth:
        case PropId_RepeatDelay:
        case PropId_RepeatInterval:
        case PropId_TreeDepthIndent:
        case PropId_ActualWidth:
        case PropId_ActualHeight:
            return MakeFloat(detail::ParseFloat32Token(rawValue));

        case PropId_EffectKind: {
            if (str::EqualsIgnoreCase(rawValue, "Blur") || str::EqualsIgnoreCase(rawValue, "BlurEffect") || rawValue == "1") {
                return value::FromInt(1);
            }
            if (str::EqualsIgnoreCase(rawValue, "DropShadow") || str::EqualsIgnoreCase(rawValue, "DropShadowEffect") || rawValue == "2") {
                return value::FromInt(2);
            }
            if (str::EqualsIgnoreCase(rawValue, "Pixelate") || str::EqualsIgnoreCase(rawValue, "PixelateEffect") || rawValue == "3") {
                return value::FromInt(3);
            }
            if (str::EqualsIgnoreCase(rawValue, "BackdropBlur") || rawValue == "4") {
                return value::FromInt(4);
            }
            if (str::EqualsIgnoreCase(rawValue, "Tint") || str::EqualsIgnoreCase(rawValue, "TintEffect") || rawValue == "5") {
                return value::FromInt(5);
            }
            if (str::EqualsIgnoreCase(rawValue, "ChromaticAberration") ||
                str::EqualsIgnoreCase(rawValue, "ChromaticAberrationEffect") || rawValue == "6") {
                return value::FromInt(6);
            }
            if (str::EqualsIgnoreCase(rawValue, "DirectionalBlur") ||
                str::EqualsIgnoreCase(rawValue, "DirectionalBlurEffect") || rawValue == "7") {
                return value::FromInt(7);
            }
            if (str::EqualsIgnoreCase(rawValue, "Saturation") || str::EqualsIgnoreCase(rawValue, "SaturationEffect") || rawValue == "8") {
                return value::FromInt(8);
            }
            return value::FromInt(0);
        }

        case PropId_Transform3DRotateXYZ:
        case PropId_Transform3DTranslateXYZ:
        case PropId_Transform3DScaleXYZ:
        case PropId_Transform3DCenterXYZ:
        case PropId_EffectParams: {
            const auto nums = detail::ParseFloatList(rawValue);
            const float x = nums.size() > 0 ? nums[0] : 0.0f;
            const float y = nums.size() > 1 ? nums[1] : 0.0f;
            const float z = nums.size() > 2 ? nums[2] : 0.0f;
            const float w = nums.size() > 3 ? nums[3] : 1.0f;
            return MakeFloat4InlineFast(x, y, z, w);
        }

        case PropId_IsChecked:
            if (str::EqualsIgnoreCase(rawValue, "{x:Null}") || str::EqualsIgnoreCase(rawValue, "x:Null") ||
                str::EqualsIgnoreCase(rawValue, "null")) {
                return UnifiedValue{NullObject{}};
            }
            return str::EqualsIgnoreCase(rawValue, "true") || rawValue == "1";

        case PropId_IsThreeState:
        case PropId_IsCheckable:
        case PropId_IsSubmenuOpen:
        case PropId_HasItems:
        case PropId_ClipToBounds:
        case PropId_IsReadOnly:
        case PropId_IsDropDownOpen:
        case PropId_IsVirtualizing:
        case PropId_IsPassword:
        case PropId_IsExpanded:
        case PropId_IsIndeterminate:
        case PropId_IsOpen:
        case PropId_TriggerIsMouseOver:
        case PropId_TriggerIsPressed:
        case PropId_TriggerIsFocused:
        case PropId_TriggerIsKeyboardFocusWithin:
            return str::EqualsIgnoreCase(rawValue, "true") || rawValue == "1";

        case PropId_LastChildFill:
        case PropId_IsEnabled:
        case PropId_Focusable:
            return !str::EqualsIgnoreCase(rawValue, "false") && rawValue != "0";

        case PropId_Visibility:
            if (rawValue == "Collapsed" || rawValue == "2") return value::FromInt(2);
            if (rawValue == "Hidden" || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_HorizontalAlignment:
        case PropId_HorizontalContentAlignment:
            if (str::EqualsIgnoreCase(rawValue, "Left") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Center") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Stretch

        case PropId_VerticalAlignment:
        case PropId_VerticalContentAlignment:
            if (str::EqualsIgnoreCase(rawValue, "Top") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Center") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Stretch

        case PropId_Dock:
            if (str::EqualsIgnoreCase(rawValue, "Top") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Right") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Left

        case PropId_Orientation:
            if (str::EqualsIgnoreCase(rawValue, "Horizontal") || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_TextWrapping:
            if (str::EqualsIgnoreCase(rawValue, "Wrap") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "WrapWithOverflow") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0); // NoWrap

        case PropId_TextAlignment:
            if (str::EqualsIgnoreCase(rawValue, "Center")) return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Right")) return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Justify")) return value::FromInt(3);
            return value::FromInt(0);

        case PropId_BlendingMode:
            if (str::EqualsIgnoreCase(rawValue, "Multiply")) return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Screen")) return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Additive") || str::EqualsIgnoreCase(rawValue, "Add") || str::EqualsIgnoreCase(rawValue, "Plus")) {
                return value::FromInt(3);
            }
            return value::FromInt(0);

        case PropId_BackgroundOpacity:
        case PropId_BorderOpacity:
            return MakeFloat(std::clamp(detail::ParseFloat32Token(rawValue, 1.0f), 0.0f, 1.0f));

        case PropId_RichText:
            return value::FromString(std::string(rawValue));

        case PropId_RefResolution: {
            const auto nums = detail::ParseFloatList(rawValue);
            if (nums.size() >= 2u) {
                return UnifiedValue{Float4Value{std::max(1.0f, nums[0]), std::max(1.0f, nums[1]), 0.0f, 0.0f}};
            }
            return UnifiedValue{Float4Value{960.0f, 540.0f, 0.0f, 0.0f}};
        }

        case PropId_Stretch:
            if (str::EqualsIgnoreCase(rawValue, "None") || rawValue == "0") return value::FromInt(0);
            if (str::EqualsIgnoreCase(rawValue, "Fill") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "UniformToFill") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(2); // Default Stretch::Uniform = 2

        case PropId_StretchDirection:
            if (str::EqualsIgnoreCase(rawValue, "UpOnly") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "DownOnly") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0); // Both

        case PropId_HorizontalScrollBarVisibility:
        case PropId_VerticalScrollBarVisibility:
            if (str::EqualsIgnoreCase(rawValue, "Disabled") || rawValue == "0") return value::FromInt(0);
            if (str::EqualsIgnoreCase(rawValue, "Auto") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Hidden") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Visible") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(propId == PropId_HorizontalScrollBarVisibility ? 0 : 1);

        case PropId_TextTrimming:
            if (str::EqualsIgnoreCase(rawValue, "CharacterEllipsis") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "WordEllipsis") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0);

        case PropId_ClickMode:
            if (str::EqualsIgnoreCase(rawValue, "Press") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Hover") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0);

        case PropId_GridRow:
        case PropId_GridColumn:
        case PropId_GridRowSpan:
        case PropId_GridColumnSpan:
        case PropId_SelectedIndex:
        case PropId_MaxLength:
        case PropId_CaretIndex:
        case PropId_UniformRows:
        case PropId_UniformColumns:
        case PropId_FirstColumn:
        case PropId_BackgroundGradientId:
        case PropId_ForegroundGradientId:
        case PropId_BorderGradientId:
        case PropId_ZIndex:
        case PropId_AlternationCount:
        case PropId_AlternationIndex:
        case PropId_TabIndex:
        case PropId_CustomIndexInt32:
        case PropId_CustomStateInt32:
            return value::FromInt(ParseInt64Literal(rawValue));

        case PropId_ExpandDirection:
            if (str::EqualsIgnoreCase(rawValue, "Up") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Left") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Down

        case PropId_TabStripPlacement:
            if (str::EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "1") return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Left") || rawValue == "2") return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Top

        case PropId_Margin:
        case PropId_Padding:
        case PropId_BorderThickness:
            return value::FromThickness(ParseThicknessLiteral(rawValue));

        case PropId_CornerRadius: {
            const auto nums = detail::ParseFloatList(rawValue);
            if (nums.size() == 1) return value::FromCornerRadius(CornerRadius(nums[0]));
            if (nums.size() >= 4) {
                return value::FromCornerRadius(CornerRadius(nums[0], nums[1], nums[2], nums[3]));
            }
            return value::FromCornerRadius(CornerRadius{});
        }

        case PropId_RenderTransformOrigin: {
            const auto nums = detail::ParseFloatList(rawValue);
            if (nums.size() == 1) {
                return UnifiedValue{Float4Value{nums[0], nums[0], 0.0f, 0.0f}};
            }
            if (nums.size() >= 2) {
                return UnifiedValue{Float4Value{nums[0], nums[1], 0.0f, 0.0f}};
            }
            return UnifiedValue{Float4Value{0.0f, 0.0f, 0.0f, 0.0f}};
        }

        case PropId_RenderTransform: {
            const Matrix2D m = ParseXamlMatrix2DLiteral(rawValue);
            if (std::fabs(m.tx) > 1e-6f || std::fabs(m.ty) > 1e-6f) {
                return value::FromString(std::string(rawValue));
            }
            return UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        }

        case PropId_LayoutTransform: {
            const Matrix2D m = ParseXamlMatrix2DLiteral(rawValue);
            return UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        }

        case PropId_RenderTransformTranslation: {
            const auto nums = detail::ParseFloatList(rawValue);
            if (nums.size() >= 2) {
                return UnifiedValue{Float4Value{nums[0], nums[1], 0.0f, 0.0f}};
            }
            return UnifiedValue{Float4Value{0.0f, 0.0f, 0.0f, 0.0f}};
        }

        case PropId_RenderTransformMatrix: {
            const auto nums = detail::ParseFloatList(rawValue);
            if (nums.size() >= 4) {
                return UnifiedValue{Float4Value{nums[0], nums[1], nums[2], nums[3]}};
            }
            return UnifiedValue{Float4Value{1.0f, 1.0f, 0.0f, 0.0f}};
        }

        case PropId_Background:
        case PropId_Foreground:
        case PropId_BorderBrush:
            if (IsGradientBrushSpec(rawValue)) {
                return value::FromString(std::string(rawValue));
            }
            return value::FromColor(ParseXamlColorLiteral(rawValue));

        case PropId_EffectColor:
            return value::FromColor(ParseXamlColorLiteral(rawValue));

        case PropId_IsHitTestVisible:
        case PropId_IsDirectionReversed:
        case PropId_IsSnapToTickEnabled:
        case PropId_IsMoveToPointEnabled:
        case PropId_AcceptsReturn:
        case PropId_IsEditable:
        case PropId_IsMuted:
        case PropId_MouseDragConstrain:
            return UnifiedValue{str::EqualsIgnoreCase(rawValue, "true") || rawValue == "1"};

        case PropId_TickFrequency:
        case PropId_LineHeight:
        case PropId_CharacterSpacing:
        case PropId_MaxDropDownHeight:
        case PropId_EffectAmountX:
        case PropId_EffectAngle:
        case PropId_PathTrimStart:
        case PropId_ShaderTime:
            return MakeFloat(detail::ParseFloat32Token(rawValue, 0.0f));

        case PropId_Volume:
        case PropId_Saturation:
        case PropId_PathTrimEnd:
            return MakeFloat(detail::ParseFloat32Token(rawValue, 1.0f));

        case PropId_EasingExponent:
            return MakeFloat(detail::ParseFloat32Token(rawValue, 2.0f));

        case PropId_MediaPosition:
            return MakeFloat(ParseXamlTimeSpanSeconds(rawValue, 0.0f));

        case PropId_MaxLines:
            return value::FromInt(static_cast<std::int64_t>(std::llround(detail::ParseFloat32Token(rawValue, 0.0f))));

        case PropId_TickPlacement:
            if (str::EqualsIgnoreCase(rawValue, "TopLeft")) return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "BottomRight")) return value::FromInt(2);
            if (str::EqualsIgnoreCase(rawValue, "Both")) return value::FromInt(3);
            return value::FromInt(0);

        case PropId_TextDecorations: {
            std::int64_t mask = 0;
            std::string_view rem = rawValue;
            while (!rem.empty()) {
                const auto sep = rem.find_first_of(", \t\r\n");
                const std::string_view tok = (sep == std::string_view::npos) ? rem : rem.substr(0u, sep);
                if (str::EqualsIgnoreCase(tok, "Underline")) {
                    mask |= 1;
                } else if (str::EqualsIgnoreCase(tok, "Strikethrough")) {
                    mask |= 2;
                } else if (str::EqualsIgnoreCase(tok, "OverLine") || str::EqualsIgnoreCase(tok, "Overline")) {
                    mask |= 4;
                } else if (str::EqualsIgnoreCase(tok, "None")) {
                    return value::FromInt(0);
                }
                if (sep == std::string_view::npos) {
                    break;
                }
                rem = str::Trim(rem.substr(sep + 1u));
            }
            return value::FromInt(mask);
        }

        case PropId_SelectionMode:
            if (str::EqualsIgnoreCase(rawValue, "Multiple")) return value::FromInt(1);
            if (str::EqualsIgnoreCase(rawValue, "Extended")) return value::FromInt(2);
            return value::FromInt(0);

        default: {
            if (IsCustomPropertyId(propId)) {
                CustomPropertyValueKind kind = CustomPropertyValueKind::String;
                if (TryGetCustomPropertyValueKind(propId, kind)) {
                    switch (kind) {
                        case CustomPropertyValueKind::Float:
                            return MakeFloat(detail::ParseFloat32Token(rawValue, 0.0f));
                        case CustomPropertyValueKind::Int:
                            return value::FromInt(ParseInt64Literal(rawValue));
                        case CustomPropertyValueKind::Bool:
                            return UnifiedValue{str::EqualsIgnoreCase(rawValue, "true") || rawValue == "1"};
                        case CustomPropertyValueKind::Color:
                            return value::FromColor(ParseXamlColorLiteral(rawValue));
                        case CustomPropertyValueKind::Brush:
                            if (IsGradientBrushSpec(rawValue)) {
                                return value::FromString(std::string(rawValue));
                            }
                            return value::FromColor(ParseXamlColorLiteral(rawValue));
                        case CustomPropertyValueKind::Thickness:
                            return value::FromThickness(ParseThicknessLiteral(rawValue));
                        case CustomPropertyValueKind::String:
                            break;
                    }
                }
            }
            return value::FromString(DecodeXamlXmlEntities(rawValue));
        }
    }
}

std::string FormatColorHexArgb(const Color& c) {
    const auto toByte = [](float v) -> unsigned int {
        return static_cast<unsigned int>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", toByte(c.a), toByte(c.r), toByte(c.g), toByte(c.b));
    return std::string(buf);
}

KeyCode ParseXamlKeyCode(std::string_view raw) noexcept {
    std::string_view s = str::Trim(raw);
    if (const auto dot = s.rfind('.'); dot != std::string_view::npos) {
        s = s.substr(dot + 1u);
    }
    if (s.size() == 1u) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(ch - 'A'));
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::D0) + static_cast<std::uint16_t>(ch - '0'));
        }
    }
    if (s.size() == 2u && (s[0] == 'D' || s[0] == 'd') && s[1] >= '0' && s[1] <= '9') {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::D0) + static_cast<std::uint16_t>(s[1] - '0'));
    }
    if ((s.size() == 2u || s.size() == 3u) && (s[0] == 'F' || s[0] == 'f')) {
        int fn = 0;
        for (std::size_t i = 1u; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) { fn = 0; break; }
            fn = fn * 10 + (s[i] - '0');
        }
        if (fn >= 1 && fn <= 12) {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::F1) + static_cast<std::uint16_t>(fn - 1));
        }
    }
    if (str::EqualsIgnoreCase(s, "Tab")) return KeyCode::Tab;
    if (str::EqualsIgnoreCase(s, "Enter") || str::EqualsIgnoreCase(s, "Return")) return KeyCode::Enter;
    if (str::EqualsIgnoreCase(s, "Space")) return KeyCode::Space;
    if (str::EqualsIgnoreCase(s, "Escape") || str::EqualsIgnoreCase(s, "Esc")) return KeyCode::Escape;
    if (str::EqualsIgnoreCase(s, "Back") || str::EqualsIgnoreCase(s, "Backspace")) return KeyCode::Backspace;
    if (str::EqualsIgnoreCase(s, "Delete") || str::EqualsIgnoreCase(s, "Del")) return KeyCode::Delete;
    if (str::EqualsIgnoreCase(s, "Left")) return KeyCode::Left;
    if (str::EqualsIgnoreCase(s, "Right")) return KeyCode::Right;
    if (str::EqualsIgnoreCase(s, "Up")) return KeyCode::Up;
    if (str::EqualsIgnoreCase(s, "Down")) return KeyCode::Down;
    if (str::EqualsIgnoreCase(s, "Home")) return KeyCode::Home;
    if (str::EqualsIgnoreCase(s, "End")) return KeyCode::End;
    if (str::EqualsIgnoreCase(s, "PageUp") || str::EqualsIgnoreCase(s, "Prior")) return KeyCode::PageUp;
    if (str::EqualsIgnoreCase(s, "PageDown") || str::EqualsIgnoreCase(s, "Next")) return KeyCode::PageDown;
    return KeyCode::None;
}

std::uint8_t ParseXamlKeyModifiers(std::string_view raw) noexcept {
    std::uint8_t mask = KeyMod_None;
    std::size_t pos = 0u;
    while (pos < raw.size()) {
        while (pos < raw.size() && (raw[pos] == '+' || raw[pos] == ',' || raw[pos] == '|' ||
                                    str::detail::IsAsciiSpace(raw[pos]))) {
            ++pos;
        }
        if (pos >= raw.size()) break;
        std::size_t end = pos;
        while (end < raw.size() && raw[end] != '+' && raw[end] != ',' && raw[end] != '|' &&
               !str::detail::IsAsciiSpace(raw[end])) {
            ++end;
        }
        std::string_view tok = raw.substr(pos, end - pos);
        if (const auto dot = tok.rfind('.'); dot != std::string_view::npos) {
            tok = tok.substr(dot + 1u);
        }
        if (str::EqualsIgnoreCase(tok, "Ctrl") || str::EqualsIgnoreCase(tok, "Control")) mask |= KeyMod_Ctrl;
        else if (str::EqualsIgnoreCase(tok, "Shift")) mask |= KeyMod_Shift;
        else if (str::EqualsIgnoreCase(tok, "Alt") || str::EqualsIgnoreCase(tok, "Menu")) mask |= KeyMod_Alt;
        pos = end;
    }
    return mask;
}

std::string KeyCodeToName(KeyCode key) {
    const auto kv = static_cast<std::uint16_t>(key);
    if (kv >= static_cast<std::uint16_t>(KeyCode::A) && kv <= static_cast<std::uint16_t>(KeyCode::Z)) {
        return std::string(1u, static_cast<char>('A' + (kv - static_cast<std::uint16_t>(KeyCode::A))));
    }
    if (kv >= static_cast<std::uint16_t>(KeyCode::D0) && kv <= static_cast<std::uint16_t>(KeyCode::D9)) {
        return "D" + std::to_string(kv - static_cast<std::uint16_t>(KeyCode::D0));
    }
    if (kv >= static_cast<std::uint16_t>(KeyCode::F1) && kv <= static_cast<std::uint16_t>(KeyCode::F12)) {
        return "F" + std::to_string(1 + kv - static_cast<std::uint16_t>(KeyCode::F1));
    }
    switch (key) {
        case KeyCode::Tab: return "Tab";
        case KeyCode::Enter: return "Enter";
        case KeyCode::Space: return "Space";
        case KeyCode::Escape: return "Escape";
        case KeyCode::Backspace: return "Backspace";
        case KeyCode::Delete: return "Delete";
        case KeyCode::Left: return "Left";
        case KeyCode::Right: return "Right";
        case KeyCode::Up: return "Up";
        case KeyCode::Down: return "Down";
        case KeyCode::Home: return "Home";
        case KeyCode::End: return "End";
        case KeyCode::PageUp: return "PageUp";
        case KeyCode::PageDown: return "PageDown";
        default: return "None";
    }
}

DecomposedTransform2D PolarDecomposeMatrix2D(const Matrix2D& m) noexcept {
    DecomposedTransform2D out{};
    out.translateX = m.tx;
    out.translateY = m.ty;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    const float sx = std::sqrt(m.m00 * m.m00 + m.m10 * m.m10);
    if (sx <= 1e-7f) {
        out.scaleX = 0.0f;
        out.scaleY = std::sqrt(m.m01 * m.m01 + m.m11 * m.m11);
        return out;
    }

    out.scaleX = sx;
    const float rad = std::atan2(m.m10, m.m00);
    out.rotateDeg = rad * kRadToDeg;

    const float c = m.m00 / sx;
    const float s = m.m10 / sx;
    // Un-rotate second column (m01, m11)^T by R(-rad):
    // u = k * sy, v = sy
    const float u = c * m.m01 + s * m.m11;
    const float sy = -s * m.m01 + c * m.m11;
    out.scaleY = sy;
    if (std::fabs(sy) > 1e-7f) {
        out.skewXDeg = std::atan(u / sy) * kRadToDeg;
    }
    return out;
}

} // namespace aero
