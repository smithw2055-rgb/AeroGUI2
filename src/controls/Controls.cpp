#include "aero/controls/Controls.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

namespace {

TypeClass gStackPanelTypeClass{};
TypeClass gGridTypeClass{};
TypeClass gCanvasTypeClass{};
TypeClass gBorderTypeClass{};
TypeClass gTextBlockTypeClass{};
TypeClass gItemsControlTypeClass{};
TypeClass gUserControlTypeClass{};
bool gControlTypesInitialized = false;

std::string_view StripNamespacePrefix(std::string_view name) noexcept {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1);
    }
    if (const auto dot = name.rfind('.'); dot != std::string_view::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}

} // namespace

void EnsureControlTypesRegistered() noexcept {
    EnsureBuiltinTypesRegistered();
    if (gControlTypesInitialized) {
        return;
    }
    gControlTypesInitialized = true;
    auto& reg = TypeRegistry::Instance();
    const auto* uiElem = UIElement::StaticTypeClass();
    const auto* panel = Panel::StaticTypeClass();

    reg.RegisterType(gStackPanelTypeClass, "Aero.UI.StackPanel", panel);
    reg.RegisterType(gGridTypeClass, "Aero.UI.Grid", panel);
    reg.RegisterType(gCanvasTypeClass, "Aero.UI.Canvas", panel);
    reg.RegisterType(gBorderTypeClass, "Aero.UI.Border", panel);
    reg.RegisterType(gTextBlockTypeClass, "Aero.UI.TextBlock", uiElem);
    reg.RegisterType(gItemsControlTypeClass, "Aero.UI.ItemsControl", panel);
    reg.RegisterType(gUserControlTypeClass, "Aero.UI.UserControl", panel);
}

const TypeClass* StackPanel::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gStackPanelTypeClass;
}

const TypeClass* Grid::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gGridTypeClass;
}

const TypeClass* Canvas::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gCanvasTypeClass;
}

const TypeClass* Border::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gBorderTypeClass;
}

const TypeClass* TextBlock::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gTextBlockTypeClass;
}

const TypeClass* ItemsControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gItemsControlTypeClass;
}

const TypeClass* UserControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gUserControlTypeClass;
}

// ============================================================================
// StackPanel
// ============================================================================

StackPanel::StackPanel() {
    EnsureControlTypesRegistered();
}

void StackPanel::SetOrientation(Orientation o) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(o)),
        ValueSourceRank::Local);
}

Orientation StackPanel::GetOrientation() const noexcept {
    const auto v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 1) ? Orientation::Horizontal : Orientation::Vertical;
    }
    return Orientation::Vertical;
}

void StackPanel::SetSpacing(float spacing) {
    SetValue(PropId_Spacing, MakeFloat(spacing), ValueSourceRank::Local);
}

float StackPanel::GetSpacing() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Spacing), out)) {
        return out;
    }
    return 0.0f;
}

Size StackPanel::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    const Orientation orient = GetOrientation();
    const float spacing = GetSpacing();

    float accumMain = 0.0f;
    float maxCross = 0.0f;
    std::size_t visibleCount = 0;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Measure(childAvail);
            continue;
        }
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            accumMain += d.height;
            maxCross = std::max(maxCross, d.width);
        } else {
            accumMain += d.width;
            maxCross = std::max(maxCross, d.height);
        }
        ++visibleCount;
    }

    if (visibleCount > 1) {
        accumMain += spacing * static_cast<float>(visibleCount - 1);
    }

    if (orient == Orientation::Vertical) {
        return Size{maxCross + padW, accumMain + padH};
    }
    return Size{accumMain + padW, maxCross + padH};
}

void StackPanel::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const Orientation orient = GetOrientation();
    const float spacing = GetSpacing();

    float cursor = (orient == Orientation::Vertical) ? startY : startX;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
            continue;
        }
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            c->Arrange(Rect{startX, cursor, innerW, d.height});
            cursor += d.height + spacing;
        } else {
            c->Arrange(Rect{cursor, startY, d.width, innerH});
            cursor += d.width + spacing;
        }
    }
}

// ============================================================================
// Grid
// ============================================================================

Grid::Grid() {
    EnsureControlTypesRegistered();
}

void Grid::AddRowDefinition(GridUnitType unit, float value) {
    rows_.push_back(RowDefinition{unit, value, 0.0f, 0.0f});
}

void Grid::AddColumnDefinition(GridUnitType unit, float value) {
    cols_.push_back(ColumnDefinition{unit, value, 0.0f, 0.0f});
}

void Grid::SetRow(UIElement* element, std::int32_t row) {
    if (element) {
        element->SetValue(PropId_GridRow, value::FromInt(row), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetRow(const UIElement* element) noexcept {
    if (!element) return 0;
    const auto v = element->GetValue(PropId_GridRow);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, *i));
    }
    return 0;
}

void Grid::SetColumn(UIElement* element, std::int32_t col) {
    if (element) {
        element->SetValue(PropId_GridColumn, value::FromInt(col), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetColumn(const UIElement* element) noexcept {
    if (!element) return 0;
    const auto v = element->GetValue(PropId_GridColumn);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, *i));
    }
    return 0;
}

Size Grid::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size innerAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    if (rows_.empty() && cols_.empty()) {
        return UIElement::MeasureCore(availableSize);
    }

    const std::size_t rowCount = std::max<std::size_t>(1, rows_.size());
    const std::size_t colCount = std::max<std::size_t>(1, cols_.size());

    std::vector<RowDefinition> workRows = rows_;
    std::vector<ColumnDefinition> workCols = cols_;
    if (workRows.empty()) {
        workRows.push_back(RowDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }
    if (workCols.empty()) {
        workCols.push_back(ColumnDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }

    for (auto& r : workRows) {
        r.computedHeight = (r.unit == GridUnitType::Pixel) ? r.value : 0.0f;
    }
    for (auto& c : workCols) {
        c.computedWidth = (c.unit == GridUnitType::Pixel) ? c.value : 0.0f;
    }

    // Measure all children
    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        child->Measure(innerAvail);
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), rowCount - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), colCount - 1);
        const Size d = child->DesiredSize();
        if (workRows[rIdx].unit != GridUnitType::Pixel) {
            workRows[rIdx].computedHeight = std::max(workRows[rIdx].computedHeight, d.height);
        }
        if (workCols[cIdx].unit != GridUnitType::Pixel) {
            workCols[cIdx].computedWidth = std::max(workCols[cIdx].computedWidth, d.width);
        }
    }

    float totalW = 0.0f;
    for (const auto& c : workCols) totalW += c.computedWidth;
    float totalH = 0.0f;
    for (const auto& r : workRows) totalH += r.computedHeight;

    return Size{totalW + padW, totalH + padH};
}

void Grid::ArrangeCore(Rect finalRect) noexcept {
    if (rows_.empty() && cols_.empty()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }

    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    std::vector<RowDefinition> workRows = rows_;
    std::vector<ColumnDefinition> workCols = cols_;
    if (workRows.empty()) {
        workRows.push_back(RowDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }
    if (workCols.empty()) {
        workCols.push_back(ColumnDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }

    float fixedW = 0.0f;
    float totalStarW = 0.0f;
    for (auto& c : workCols) {
        if (c.unit == GridUnitType::Pixel) {
            c.computedWidth = c.value;
            fixedW += c.computedWidth;
        } else if (c.unit == GridUnitType::Auto) {
            c.computedWidth = 0.0f;
        } else {
            totalStarW += std::max(0.0f, c.value);
        }
    }

    float fixedH = 0.0f;
    float totalStarH = 0.0f;
    for (auto& r : workRows) {
        if (r.unit == GridUnitType::Pixel) {
            r.computedHeight = r.value;
            fixedH += r.computedHeight;
        } else if (r.unit == GridUnitType::Auto) {
            r.computedHeight = 0.0f;
        } else {
            totalStarH += std::max(0.0f, r.value);
        }
    }

    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), workRows.size() - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), workCols.size() - 1);
        const Size d = child->DesiredSize();
        if (workRows[rIdx].unit == GridUnitType::Auto) {
            workRows[rIdx].computedHeight = std::max(workRows[rIdx].computedHeight, d.height);
        }
        if (workCols[cIdx].unit == GridUnitType::Auto) {
            workCols[cIdx].computedWidth = std::max(workCols[cIdx].computedWidth, d.width);
        }
    }

    for (const auto& c : workCols) {
        if (c.unit == GridUnitType::Auto) fixedW += c.computedWidth;
    }
    for (const auto& r : workRows) {
        if (r.unit == GridUnitType::Auto) fixedH += r.computedHeight;
    }

    const float remW = std::max(0.0f, innerW - fixedW);
    const float remH = std::max(0.0f, innerH - fixedH);

    float offsetX = startX;
    for (auto& c : workCols) {
        if (c.unit == GridUnitType::Star) {
            c.computedWidth = (totalStarW > 0.0f) ? (remW * (c.value / totalStarW)) : 0.0f;
        }
        c.offsetX = offsetX;
        offsetX += c.computedWidth;
    }

    float offsetY = startY;
    for (auto& r : workRows) {
        if (r.unit == GridUnitType::Star) {
            r.computedHeight = (totalStarH > 0.0f) ? (remH * (r.value / totalStarH)) : 0.0f;
        }
        r.offsetY = offsetY;
        offsetY += r.computedHeight;
    }

    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), workRows.size() - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), workCols.size() - 1);
        child->Arrange(Rect{
            workCols[cIdx].offsetX,
            workRows[rIdx].offsetY,
            workCols[cIdx].computedWidth,
            workRows[rIdx].computedHeight});
    }
}

// ============================================================================
// Canvas
// ============================================================================

Canvas::Canvas() {
    EnsureControlTypesRegistered();
}

void Canvas::SetLeft(UIElement* element, float left) {
    if (element) {
        element->SetValue(PropId_CanvasLeft, MakeFloat(left), ValueSourceRank::Local);
    }
}

float Canvas::GetLeft(const UIElement* element) noexcept {
    if (!element) return 0.0f;
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasLeft), out)) {
        return out;
    }
    return 0.0f;
}

void Canvas::SetTop(UIElement* element, float top) {
    if (element) {
        element->SetValue(PropId_CanvasTop, MakeFloat(top), ValueSourceRank::Local);
    }
}

float Canvas::GetTop(const UIElement* element) noexcept {
    if (!element) return 0.0f;
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasTop), out)) {
        return out;
    }
    return 0.0f;
}

Size Canvas::MeasureCore(Size availableSize) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(availableSize);
    }
    return Size{0.0f, 0.0f};
}

void Canvas::ArrangeCore(Rect finalRect) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const float left = GetLeft(c);
        const float top = GetTop(c);
        const Size d = c->DesiredSize();
        c->Arrange(Rect{finalRect.x + left, finalRect.y + top, d.width, d.height});
    }
}

// ============================================================================
// Border
// ============================================================================

Border::Border() {
    EnsureControlTypesRegistered();
}

void Border::SetBackground(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Border::GetBackground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Border::SetBorderBrush(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Border::GetBorderBrush() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Border::SetBorderThickness(const Thickness& thickness) {
    SetValue(PropId_BorderThickness, value::FromThickness(thickness), ValueSourceRank::Local);
}

Thickness Border::GetBorderThickness() const noexcept {
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return *t;
    }
    return Thickness{};
}

void Border::SetCornerRadius(const CornerRadius& radius) {
    SetValue(PropId_CornerRadius, value::FromCornerRadius(radius), ValueSourceRank::Local);
}

CornerRadius Border::GetCornerRadius() const noexcept {
    if (auto r = value::TryGetCornerRadius(GetValue(PropId_CornerRadius))) {
        return *r;
    }
    return CornerRadius{};
}

Size Border::MeasureCore(Size availableSize) noexcept {
    const Thickness bt = GetBorderThickness();
    const float borderW = bt.left + bt.right;
    const float borderH = bt.top + bt.bottom;
    const Size innerAvail{
        std::max(0.0f, availableSize.width - borderW),
        std::max(0.0f, availableSize.height - borderH)};
    const Size contentSize = UIElement::MeasureCore(innerAvail);
    return Size{contentSize.width + borderW, contentSize.height + borderH};
}

void Border::ArrangeCore(Rect finalRect) noexcept {
    const Thickness bt = GetBorderThickness();
    const Rect innerRect{
        finalRect.x + bt.left,
        finalRect.y + bt.top,
        std::max(0.0f, finalRect.width - (bt.left + bt.right)),
        std::max(0.0f, finalRect.height - (bt.top + bt.bottom))};
    UIElement::ArrangeCore(innerRect);
}

// ============================================================================
// TextBlock
// ============================================================================

TextBlock::TextBlock() {
    EnsureControlTypesRegistered();
}

void TextBlock::SetText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string TextBlock::GetText() const {
    const auto v = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    return {};
}

void TextBlock::SetFontSize(float fontSize) {
    SetValue(PropId_FontSize, MakeFloat(fontSize), ValueSourceRank::Local);
}

float TextBlock::GetFontSize() const noexcept {
    float out = 14.0f;
    if (TryGetFloat(GetValue(PropId_FontSize), out)) {
        return out;
    }
    return 14.0f;
}

Size TextBlock::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float fontSize = GetFontSize();
    const std::string text = GetText();
    const std::size_t codePoints = rs_rt::utf8CodePointLength(text);
    const float advancePerCodePoint = fontSize * 0.6f;
    const float textW = static_cast<float>(codePoints) * advancePerCodePoint;
    const float textH = (codePoints > 0) ? (fontSize * 1.25f) : 0.0f;
    return Size{textW + p.left + p.right, textH + p.top + p.bottom};
}

// ============================================================================
// ItemsControl
// ============================================================================

ItemsControl::ItemsControl() {
    EnsureControlTypesRegistered();
}

void ItemsControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    items_ = items;
    while (FirstChild()) {
        RemoveChild(FirstChild(), registry, uiHeap);
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        IntrusivePtr<UIElement> itemVisual;
        if (itemGenerator_) {
            itemVisual = itemGenerator_(i, items_[i]);
        } else {
            auto tb = MakeIntrusive<TextBlock>();
            tb->SetText(rs_rt::valueToString(items_[i]));
            itemVisual = tb;
        }
        if (itemVisual) {
            AddChild(itemVisual.Get(), registry, uiHeap);
        }
    }
}

void ItemsControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Panel::OnPropertyChanged(propId, newValue);
    if (propId == PropId_ItemsSource) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (const auto* arrRef = std::get_if<ObjectRef>(&newValue)) {
            if (arrRef->valid() && arrRef->kind == ObjectKind::Array && domains_) {
                if (auto* heap = domains_->heapForObject(*arrRef)) {
                    const std::size_t len = heap->arrayLength(*arrRef).value_or(0);
                    std::vector<UnifiedValue> extracted;
                    extracted.reserve(len);
                    for (std::size_t i = 0; i < len; ++i) {
                        UnifiedValue elem = heap->arrayGet(*arrRef, i).value_or(UnifiedValue{});
                        if (const auto* strRef = std::get_if<ObjectRef>(&elem)) {
                            if (strRef->kind == ObjectKind::String) {
                                if (const auto sv = domains_->stringViewAcrossDomains(*strRef)) {
                                    elem = value::FromString(std::string(*sv));
                                }
                            }
                        }
                        extracted.push_back(std::move(elem));
                    }
                    SetItemsSource(extracted, registry, uiHeap);
                    return;
                }
            }
        } else if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const std::string_view raw = sv->str();
            std::vector<UnifiedValue> extracted;
            std::size_t start = 0;
            while (start < raw.size()) {
                std::size_t sep = raw.find('|', start);
                if (sep == std::string_view::npos) {
                    sep = raw.size();
                }
                const std::string_view item = raw.substr(start, sep - start);
                if (!item.empty()) {
                    extracted.push_back(value::FromString(std::string(item)));
                }
                start = sep + 1;
            }
            SetItemsSource(extracted, registry, uiHeap);
            return;
        } else if (std::holds_alternative<std::monostate>(newValue) ||
                   std::holds_alternative<NullObject>(newValue)) {
            SetItemsSource({}, registry, uiHeap);
            return;
        }
    }
}

Size ItemsControl::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    float totalH = 0.0f;
    float maxW = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        totalH += d.height;
        maxW = std::max(maxW, d.width);
    }
    return Size{maxW + padW, totalH + padH};
}

void ItemsControl::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    float cursorY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const Size d = c->DesiredSize();
        c->Arrange(Rect{startX, cursorY, innerW, d.height});
        cursorY += d.height;
    }
}

// ============================================================================
// AxbTemplateRegistry & UserControl Self-Contained Mounting
// ============================================================================

AxbTemplateRegistry& AxbTemplateRegistry::Instance() noexcept {
    static AxbTemplateRegistry instance;
    return instance;
}

void AxbTemplateRegistry::Register(std::string className, AxbPackage package) {
    const std::string shortName(StripNamespacePrefix(className));
    templates_[className] = package;
    if (shortName != className) {
        templates_[shortName] = std::move(package);
    }
}

const AxbPackage* AxbTemplateRegistry::Find(std::string_view className) const noexcept {
    auto it = templates_.find(std::string(className));
    if (it != templates_.end()) {
        return &it->second;
    }
    const std::string shortName(StripNamespacePrefix(className));
    it = templates_.find(shortName);
    if (it != templates_.end()) {
        return &it->second;
    }
    return nullptr;
}

UserControl::UserControl() {
    EnsureControlTypesRegistered();
}

UserControl::UserControl(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter)
    : controlClassName_(className) {
    EnsureControlTypesRegistered();
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(*pkg, domains, bindingEngine, dataContext, eventInterpreter);
    }
}

bool UserControl::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter) {
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter);
    return templateMounted_;
}

Window::Window(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter)
    : windowClassName_(className) {
    EnsureControlTypesRegistered();
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(*pkg, domains, bindingEngine, dataContext, eventInterpreter);
    }
}

bool Window::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter) {
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter);
    return templateMounted_;
}

IntrusivePtr<UIElement> CreateElementByTypeName(std::string_view typeName) {
    EnsureControlTypesRegistered();
    const std::string_view shortName = StripNamespacePrefix(typeName);
    if (shortName == "Window") return MakeIntrusive<Window>();
    if (shortName == "Panel") return MakeIntrusive<Panel>();
    if (shortName == "StackPanel") return MakeIntrusive<StackPanel>();
    if (shortName == "Grid") return MakeIntrusive<Grid>();
    if (shortName == "Canvas") return MakeIntrusive<Canvas>();
    if (shortName == "Border") return MakeIntrusive<Border>();
    if (shortName == "TextBlock") return MakeIntrusive<TextBlock>();
    if (shortName == "Button") return MakeIntrusive<Button>();
    if (shortName == "ItemsControl") return MakeIntrusive<ItemsControl>();
    if (shortName == "UserControl") return MakeIntrusive<UserControl>();
    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr) {
        return MakeIntrusive<UserControl>(typeName);
    }
    return MakeIntrusive<UIElement>();
}

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter) {
    if (!rootControl) {
        return false;
    }

    NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;
    const ObjectRef effectiveContext =
        dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();

    std::vector<UIElement*> elementStack;
    bool rootVisited = false;

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                UIElement* currentElem = nullptr;

                if (!rootVisited) {
                    rootVisited = true;
                    currentElem = rootControl;
                } else {
                    IntrusivePtr<UIElement> createdChild;
                    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr &&
                        StripNamespacePrefix(typeName) != "UserControl") {
                        createdChild = MakeIntrusive<UserControl>(
                            typeName,
                            domains,
                            bindingEngine,
                            effectiveContext,
                            eventInterpreter);
                    } else {
                        createdChild = CreateElementByTypeName(typeName);
                    }
                    if (!elementStack.empty() && createdChild) {
                        elementStack.back()->AddChild(createdChild.Get(), registry, uiHeap);
                        currentElem = createdChild.Get();
                    }
                }

                if (auto* ic = currentElem ? currentElem->DynamicCast<ItemsControl>() : nullptr) {
                    ic->SetDomains(domains);
                }

                if (currentElem && !elemName.empty()) {
                    currentElem->SetName(std::string(elemName));

                    // Populate x:Name field slot on the partial class script instance if present
                    const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                        ? rootControl->BoundScriptWrapper()
                        : effectiveContext;
                    if (domains && registry && wrapperObj.valid()) {
                        for (const auto& namedSlot : package.namedControls) {
                            if (namedSlot.name == elemName &&
                                namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                                const auto h = currentElem->ToNativeHandle(*registry);
                                (void)domains->writeObjectField(
                                    DomainKind::Ui,
                                    wrapperObj,
                                    namedSlot.fieldIndexInClass,
                                    h,
                                    nullptr);
                                break;
                            }
                        }
                    }
                }

                elementStack.push_back(currentElem);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elementStack.empty()) {
                    elementStack.pop_back();
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!elementStack.empty() && elementStack.back()) {
                    elementStack.back()->SetValue(
                        inst.propId,
                        inst.literalValue,
                        ValueSourceRank::Local);
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!elementStack.empty() && elementStack.back() &&
                    bindingEngine && effectiveContext.valid()) {
                    if (inst.strIdx0 < package.bindingChains.size()) {
                        const auto& chain = package.bindingChains[inst.strIdx0];
                        IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                        if (!chain.hops.empty()) {
                            (void)bindingEngine->BindChain(
                                effectiveContext,
                                chain,
                                targetPtr);
                        } else if (!chain.rawPath.empty()) {
                            (void)bindingEngine->BindPath(
                                effectiveContext,
                                chain.rawPath,
                                targetPtr,
                                inst.propId,
                                chain.mode);
                        }
                    }
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!elementStack.empty() && elementStack.back() && bindingEngine) {
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : rs_sem::stableTypeId(std::string(compName));
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    (void)bindingEngine->BindEcsColumn(
                        compId,
                        fieldName,
                        0,
                        targetPtr,
                        inst.propId);
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elementStack.empty() && elementStack.back()) {
                    if (auto* btn = elementStack.back()->DynamicCast<Button>()) {
                        const ObjectRef targetObj = rootControl->BoundScriptWrapper().valid()
                            ? rootControl->BoundScriptWrapper()
                            : effectiveContext;
                        if (eventInterpreter && targetObj.valid() && inst.symbolId != 0) {
                            btn->BindScriptClickHandler(
                                eventInterpreter,
                                targetObj,
                                inst.symbolId);
                        }
                    }
                }
                break;
            }
        }
    }

    return rootVisited;
}

} // namespace aero
