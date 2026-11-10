#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

enum class Orientation : std::uint8_t {
    Vertical = 0,
    Horizontal = 1,
};

enum class GridUnitType : std::uint8_t {
    Auto = 0,
    Pixel = 1,
    Star = 2,
};

enum class Stretch : std::uint8_t {
    None = 0,
    Fill = 1,
    Uniform = 2,
    UniformToFill = 3,
};

struct RowDefinition {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
    float computedHeight = 0.0f;
    float offsetY = 0.0f;
};

struct ColumnDefinition {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
    float computedWidth = 0.0f;
    float offsetX = 0.0f;
};

class StackPanel : public Panel {
public:
    StackPanel();
    ~StackPanel() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetOrientation(Orientation o);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetSpacing(float spacing);
    [[nodiscard]] float GetSpacing() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class Grid : public Panel {
public:
    Grid();
    ~Grid() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void AddRowDefinition(GridUnitType unit, float value = 1.0f);
    void AddColumnDefinition(GridUnitType unit, float value = 1.0f);
    void ClearDefinitions() noexcept {
        if (!rows_.empty() || !cols_.empty()) {
            rows_.clear();
            cols_.clear();
            InvalidateLayout();
        }
    }
    [[nodiscard]] std::size_t RowDefinitionCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t ColumnDefinitionCount() const noexcept { return cols_.size(); }
    [[nodiscard]] const std::vector<RowDefinition>& Rows() const noexcept { return rows_; }
    [[nodiscard]] const std::vector<ColumnDefinition>& Columns() const noexcept { return cols_; }

    static void SetRow(UIElement* element, std::int32_t row);
    [[nodiscard]] static std::int32_t GetRow(const UIElement* element) noexcept;
    static void SetColumn(UIElement* element, std::int32_t col);
    [[nodiscard]] static std::int32_t GetColumn(const UIElement* element) noexcept;
    static void SetRowSpan(UIElement* element, std::int32_t span);
    [[nodiscard]] static std::int32_t GetRowSpan(const UIElement* element) noexcept;
    static void SetColumnSpan(UIElement* element, std::int32_t span);
    [[nodiscard]] static std::int32_t GetColumnSpan(const UIElement* element) noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    std::vector<RowDefinition> rows_;
    std::vector<ColumnDefinition> cols_;
};

class Canvas : public Panel {
public:
    Canvas();
    ~Canvas() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    static void SetLeft(UIElement* element, float left);
    [[nodiscard]] static float GetLeft(const UIElement* element) noexcept;
    static void SetTop(UIElement* element, float top);
    [[nodiscard]] static float GetTop(const UIElement* element) noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class Border : public Panel {
public:
    Border();
    ~Border() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetBackground(const Color& color);
    [[nodiscard]] Color GetBackground() const noexcept;
    void SetBorderBrush(const Color& color);
    [[nodiscard]] Color GetBorderBrush() const noexcept;
    void SetBorderThickness(const Thickness& thickness);
    [[nodiscard]] Thickness GetBorderThickness() const noexcept;
    void SetCornerRadius(const CornerRadius& radius);
    [[nodiscard]] CornerRadius GetCornerRadius() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class TextBlock : public UIElement {
public:
    TextBlock();
    ~TextBlock() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetText(std::string_view text);
    [[nodiscard]] std::string GetText() const;

    void SetFontSize(float fontSize);
    [[nodiscard]] float GetFontSize() const noexcept;

    void SetForeground(const Color& color);
    [[nodiscard]] Color GetForeground() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
};

class Path : public UIElement {
public:
    Path();
    ~Path() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetData(std::string_view svgPathData);
    [[nodiscard]] std::string GetData() const;

    void SetFill(const Color& color);
    [[nodiscard]] Color GetFill() const noexcept;
    [[nodiscard]] bool HasExplicitFill() const noexcept;

    void SetStroke(const Color& color);
    [[nodiscard]] Color GetStroke() const noexcept;
    [[nodiscard]] bool HasExplicitStroke() const noexcept;

    void SetStrokeThickness(float thickness);
    [[nodiscard]] float GetStrokeThickness() const noexcept;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
};

class Image : public UIElement {
public:
    Image();
    ~Image() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetSource(std::string_view sourceUri);
    [[nodiscard]] std::string GetSource() const;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
};

class ItemsControl : public Panel {
public:
    using ItemGeneratorFn = std::function<IntrusivePtr<UIElement>(std::size_t index, const UnifiedValue& item)>;

    ItemsControl();
    ~ItemsControl() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    void SetItemGenerator(ItemGeneratorFn generator) {
        itemGenerator_ = std::move(generator);
    }

    void SetDomains(ExecutionDomainContext* domains) noexcept {
        domains_ = domains;
    }

    void SetItemsSource(
        const std::vector<UnifiedValue>& items,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    ExecutionDomainContext* domains_ = nullptr;
    ItemGeneratorFn itemGenerator_;
    std::vector<UnifiedValue> items_;
};

// Self-contained template & MTSDF vector icon registry for automatic .axb4 mounting
class AxbTemplateRegistry {
public:
    static AxbTemplateRegistry& Instance() noexcept;

    void Register(std::string className, AxbPackage package);
    void RegisterResources(const AxbPackage& package);
    void RegisterResource(std::string key, UnifiedValue value);
    void RegisterMtsdfPath(AxbMtsdfPathEntry entry);
    [[nodiscard]] const UnifiedValue* FindResource(std::string_view key) const noexcept;
    [[nodiscard]] const AxbPackage* Find(std::string_view className) const noexcept;
    [[nodiscard]] const AxbMtsdfPathEntry* FindMtsdfPath(std::uint64_t pathHash) const noexcept;
    [[nodiscard]] const AxbMtsdfPathEntry* FindOrBakeMtsdfPath(std::string_view pathData);
    [[nodiscard]] std::size_t RegisteredMtsdfPathCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return mtsdfPaths_.size();
    }
    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_.clear();
        resources_.clear();
        mtsdfPaths_.clear();
        nextMtsdfAtlasSlot_ = 0u;
    }

private:
    const AxbMtsdfPathEntry* InsertMtsdfPathLocked(AxbMtsdfPathEntry entry);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AxbPackage> templates_;
    std::unordered_map<std::string, UnifiedValue> resources_;
    std::unordered_map<std::uint64_t, AxbMtsdfPathEntry> mtsdfPaths_;
    std::uint32_t nextMtsdfAtlasSlot_ = 0u;
};

class UserControl : public Panel {
public:
    UserControl();
    explicit UserControl(
        std::string_view className,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr);
    ~UserControl() override = default;

    static const TypeClass* StaticTypeClass() noexcept;
    [[nodiscard]] const TypeClass* GetTypeClass() const noexcept override {
        return StaticTypeClass();
    }

    [[nodiscard]] const std::string& ControlClassName() const noexcept {
        return controlClassName_;
    }
    [[nodiscard]] bool IsTemplateMounted() const noexcept {
        return templateMounted_;
    }

    // Mount compiled .axb4 template directly onto this control (self-contained, no external tree walk)
    bool MountTemplate(
        const AxbPackage& package,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = {},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr);

private:
    std::string controlClassName_;
    bool templateMounted_ = false;
};

// Factory and self-contained .axb4 instruction VM mounting
[[nodiscard]] IntrusivePtr<UIElement> CreateElementByTypeName(std::string_view typeName);

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains = nullptr,
    DualTrackBindingEngine* bindingEngine = nullptr,
    ObjectRef dataContext = {},
    const Interpreter* eventInterpreter = nullptr,
    rs_aot::ExecutionContext* eventAotContext = nullptr);

void EnsureControlTypesRegistered() noexcept;

} // namespace aero
