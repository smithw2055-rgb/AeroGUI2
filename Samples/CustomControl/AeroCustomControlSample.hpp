#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string_view>

#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero::samples {

struct CustomControlSamplePropIds {
    std::uint16_t dtUseSystemTime = 0xFFFFu;
    std::uint16_t dtDay = 0xFFFFu;
    std::uint16_t dtMonth = 0xFFFFu;
    std::uint16_t dtYear = 0xFFFFu;
    std::uint16_t dtHour = 0xFFFFu;
    std::uint16_t dtMinute = 0xFFFFu;
    std::uint16_t dtSecond = 0xFFFFu;

    std::uint16_t ticksMinimum = 0xFFFFu;
    std::uint16_t ticksMaximum = 0xFFFFu;
    std::uint16_t ticksSpacing = 0xFFFFu;
    std::uint16_t ticksCount = 0xFFFFu;
    std::uint16_t ticksLength = 0xFFFFu;
    std::uint16_t ticksWidth = 0xFFFFu;
    std::uint16_t ticksMinAngle = 0xFFFFu;
    std::uint16_t ticksMaxAngle = 0xFFFFu;
    std::uint16_t ticksBrush = 0xFFFFu;
    std::uint16_t ticksStroke = 0xFFFFu;
    std::uint16_t ticksStrokeThickness = 0xFFFFu;

    std::uint16_t needleMinimum = 0xFFFFu;
    std::uint16_t needleMaximum = 0xFFFFu;
    std::uint16_t needleValue = 0xFFFFu;
    std::uint16_t needleMinAngle = 0xFFFFu;
    std::uint16_t needleMaxAngle = 0xFFFFu;
    std::uint16_t needleAngle = 0xFFFFu;
    std::uint16_t needleBrush = 0xFFFFu;
};

[[nodiscard]] inline CustomControlSamplePropIds& GetCustomControlSamplePropIds() noexcept {
    static CustomControlSamplePropIds ids{};
    return ids;
}

class DateTimeControl : public Panel {
public:
    DateTimeControl() noexcept = default;

    [[nodiscard]] bool GetUseSystemTime() const noexcept {
        const auto& ids = GetCustomControlSamplePropIds();
        if (ids.dtUseSystemTime == 0xFFFFu) {
            return true;
        }
        bool b = true;
        if (GetValue(ids.dtUseSystemTime).tryGetBool(b)) {
            return b;
        }
        return true;
    }

    [[nodiscard]] bool NeedsContinuousTick() const noexcept override {
        return GetUseSystemTime();
    }

    void TickControl(float dtSeconds) override {
        (void)dtSeconds;
        if (!GetUseSystemTime()) {
            return;
        }

        const auto& ids = GetCustomControlSamplePropIds();
        const auto now = std::chrono::system_clock::now();
        const auto msTotal =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &tt);
#else
        localtime_r(&tt, &localTm);
#endif
        const int subMs = static_cast<int>(((msTotal.count() % 1000) + 1000) % 1000);
        const float sec = static_cast<float>(localTm.tm_sec) + static_cast<float>(subMs) / 1000.0f;
        const float min = static_cast<float>(localTm.tm_min) + sec / 60.0f;
        const float hour = static_cast<float>(localTm.tm_hour) + min / 60.0f;
        const std::int64_t day = static_cast<std::int64_t>(localTm.tm_mday);
        const std::int64_t month = static_cast<std::int64_t>(localTm.tm_mon + 1);
        const std::int64_t year = static_cast<std::int64_t>(localTm.tm_year + 1900);

        if (ids.dtYear != 0xFFFFu) {
            (void)SetValue(ids.dtYear, value::FromInt(year), ValueSourceRank::Local);
        }
        if (ids.dtMonth != 0xFFFFu) {
            (void)SetValue(ids.dtMonth, value::FromInt(month), ValueSourceRank::Local);
        }
        if (ids.dtDay != 0xFFFFu) {
            (void)SetValue(ids.dtDay, value::FromInt(day), ValueSourceRank::Local);
        }
        if (ids.dtHour != 0xFFFFu) {
            (void)SetValue(ids.dtHour, value::FromFloat(hour), ValueSourceRank::Local);
        }
        if (ids.dtMinute != 0xFFFFu) {
            (void)SetValue(ids.dtMinute, value::FromFloat(min), ValueSourceRank::Local);
        }
        if (ids.dtSecond != 0xFFFFu) {
            (void)SetValue(ids.dtSecond, value::FromFloat(sec), ValueSourceRank::Local);
        }
        InvalidateVisual();
    }
};

class RadialTicksControl : public UIElement {
public:
    RadialTicksControl() noexcept = default;

    [[nodiscard]] bool HasCustomRender() const override {
        return true;
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override {
        UIElement::OnPropertyChanged(propId, newValue);
        const auto& ids = GetCustomControlSamplePropIds();
        if (propId == PropId_Spacing || propId == ids.ticksSpacing) {
            hasExplicitSpacing_ = true;
        } else if (propId == ids.ticksCount) {
            hasExplicitTickCount_ = true;
        } else if (propId == ids.ticksBrush) {
            hasExplicitBrush_ = true;
        } else if (propId == PropId_BorderBrush || propId == ids.ticksStroke) {
            hasExplicitStroke_ = true;
        } else if (propId == ids.ticksWidth) {
            hasExplicitTickWidth_ = true;
        } else if (propId == PropId_StrokeThickness || propId == ids.ticksStrokeThickness) {
            hasExplicitStrokeThickness_ = true;
        }
        InvalidateVisual();
    }

    void OnRender(CustomDrawingContext& dc) override {
        const auto& ids = GetCustomControlSamplePropIds();
        const RectF rect = FinalRect();
        float width = rect.width;
        float height = rect.height;
        if (width <= 0.0f) {
            width = GetWidth();
        }
        if (height <= 0.0f) {
            height = GetHeight();
        }
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        const Thickness pad = GetPadding();
        const float x0 = pad.left;
        const float y0 = pad.top;
        const float innerW = std::max(0.0f, width - (pad.left + pad.right));
        const float innerH = std::max(0.0f, height - (pad.top + pad.bottom));
        if (innerW <= 0.0f || innerH <= 0.0f) {
            return;
        }

        const float cx = x0 + innerW * 0.5f;
        const float cy = y0 + innerH * 0.5f;
        const float radius = 0.5f * std::min(innerW, innerH);

        const float minVal = ReadFloatPropWithFallback(PropId_Minimum, ids.ticksMinimum, 0.0f);
        const float maxVal = ReadFloatPropWithFallback(PropId_Maximum, ids.ticksMaximum, 100.0f);
        const float minAngle = ReadCustomFloat(ids.ticksMinAngle, 0.0f);
        const float maxAngle = ReadCustomFloat(ids.ticksMaxAngle, 360.0f);
        const float tickLength = std::max(0.0f, ReadCustomFloat(ids.ticksLength, 10.0f));
        const float tickWidth = ReadEffectiveTickWidth();
        const Color strokeColor = ReadEffectiveColor();

        const float innerRadius = std::max(0.0f, radius - tickLength);
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        auto drawTickAtAngle = [&](float deg) {
            const float rad = deg * kDegToRad;
            const float dirX = std::sin(rad);
            const float dirY = -std::cos(rad);
            const PointF outer{cx + dirX * radius, cy + dirY * radius};
            const PointF inner{cx + dirX * innerRadius, cy + dirY * innerRadius};
            dc.DrawLine(outer, inner, strokeColor, tickWidth);
        };

        const std::int64_t tickCount = ReadCustomInt(ids.ticksCount, 0);
        const float angleSpan = maxAngle - minAngle;
        const bool isFullCircle = std::fabs(std::fabs(angleSpan) - 360.0f) < 1e-2f;

        if (tickCount > 0 && (hasExplicitTickCount_ || !hasExplicitSpacing_)) {
            if (tickCount == 1) {
                drawTickAtAngle(minAngle);
                return;
            }
            const float denom = isFullCircle
                ? static_cast<float>(tickCount)
                : static_cast<float>(tickCount - 1);
            for (std::int64_t i = 0; i < tickCount; ++i) {
                const float ratio = static_cast<float>(i) / denom;
                drawTickAtAngle(minAngle + ratio * angleSpan);
            }
            return;
        }

        const float spacing = ReadFloatPropWithFallback(PropId_Spacing, ids.ticksSpacing, 10.0f);
        const float range = maxVal - minVal;
        if (spacing <= 1e-5f || range < 0.0f) {
            return;
        }
        if (range <= 1e-5f) {
            drawTickAtAngle(minAngle);
            return;
        }

        const float eps = spacing * 1e-3f;
        const std::int32_t maxIterations = 2048;
        std::int32_t iter = 0;
        for (float v = minVal; iter < maxIterations; v += spacing, ++iter) {
            if (isFullCircle) {
                if (v >= maxVal - eps) {
                    break;
                }
            } else {
                if (v > maxVal + eps) {
                    break;
                }
            }
            const float ratio = (v - minVal) / range;
            drawTickAtAngle(minAngle + ratio * angleSpan);
        }
    }

private:
    [[nodiscard]] float ReadCustomFloat(std::uint16_t propId, float fallback) const noexcept {
        if (propId == 0xFFFFu) {
            return fallback;
        }
        float out = fallback;
        if (value::TryGetFloat(GetValue(propId), out) && std::isfinite(out)) {
            return out;
        }
        return fallback;
    }

    [[nodiscard]] std::int64_t ReadCustomInt(std::uint16_t propId, std::int64_t fallback) const noexcept {
        if (propId == 0xFFFFu) {
            return fallback;
        }
        const UnifiedValue v = GetValue(propId);
        std::int64_t iv = 0;
        if (v.tryGetInt64(iv)) {
            return iv;
        }
        float f = 0.0f;
        if (value::TryGetFloat(v, f) && std::isfinite(f)) {
            return static_cast<std::int64_t>(f);
        }
        return fallback;
    }

    [[nodiscard]] float ReadFloatPropWithFallback(
        std::uint16_t primaryId,
        std::uint16_t customId,
        float fallback) const noexcept {
        float out = fallback;
        if (value::TryGetFloat(GetValue(primaryId), out) && std::isfinite(out)) {
            return out;
        }
        return ReadCustomFloat(customId, fallback);
    }

    [[nodiscard]] float ReadEffectiveTickWidth() const noexcept {
        const auto& ids = GetCustomControlSamplePropIds();
        if (hasExplicitTickWidth_) {
            return std::max(0.25f, ReadCustomFloat(ids.ticksWidth, 1.0f));
        }
        if (hasExplicitStrokeThickness_) {
            float st = 1.0f;
            if (value::TryGetFloat(GetValue(PropId_StrokeThickness), st) && std::isfinite(st)) {
                return std::max(0.25f, st);
            }
            return std::max(0.25f, ReadCustomFloat(ids.ticksStrokeThickness, 1.0f));
        }
        float st = 1.0f;
        if (value::TryGetFloat(GetValue(PropId_StrokeThickness), st) && std::isfinite(st)) {
            return std::max(0.25f, st);
        }
        return std::max(0.25f, ReadCustomFloat(ids.ticksWidth, 1.0f));
    }

    [[nodiscard]] Color ReadEffectiveColor() const noexcept {
        const auto& ids = GetCustomControlSamplePropIds();
        if (hasExplicitBrush_ && ids.ticksBrush != 0xFFFFu) {
            if (auto c = value::TryGetColor(GetValue(ids.ticksBrush))) {
                return *c;
            }
        }
        if (hasExplicitStroke_) {
            if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
                return *c;
            }
            if (ids.ticksStroke != 0xFFFFu) {
                if (auto c = value::TryGetColor(GetValue(ids.ticksStroke))) {
                    return *c;
                }
            }
        }
        if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
            return *c;
        }
        if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
            return *c;
        }
        if (ids.ticksBrush != 0xFFFFu) {
            if (auto c = value::TryGetColor(GetValue(ids.ticksBrush))) {
                return *c;
            }
        }
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }

    bool hasExplicitSpacing_ = false;
    bool hasExplicitTickCount_ = false;
    bool hasExplicitBrush_ = false;
    bool hasExplicitStroke_ = false;
    bool hasExplicitTickWidth_ = false;
    bool hasExplicitStrokeThickness_ = false;
};

class RadialNeedleControl : public Panel {
public:
    RadialNeedleControl() noexcept {
        (void)SetValue(
            PropId_RenderTransformOrigin,
            Float4Value{0.5f, 0.5f, 0.0f, 0.0f},
            ValueSourceRank::Default);
    }

    [[nodiscard]] bool HasCustomRender() const override {
        return FirstChild() == nullptr;
    }

    Size MeasureCore(Size availableSize) noexcept override {
        SyncRotateTransformFromChildState();
        return Panel::MeasureCore(availableSize);
    }

    void ArrangeCore(Rect finalRect) noexcept override {
        SyncRotateTransformFromChildState();
        Panel::ArrangeCore(finalRect);
    }

    [[nodiscard]] float GetComputedAngle() const noexcept {
        const auto& ids = GetCustomControlSamplePropIds();
        float ang = 0.0f;
        if (ids.needleAngle != 0xFFFFu &&
            value::TryGetFloat(GetValue(ids.needleAngle), ang) &&
            std::isfinite(ang)) {
            return ang;
        }
        if (value::TryGetFloat(GetValue(PropId_AnimRotateAngle), ang) && std::isfinite(ang)) {
            return ang;
        }
        return 0.0f;
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override {
        Panel::OnPropertyChanged(propId, newValue);
        if (updatingAngle_) {
            return;
        }
        const auto& ids = GetCustomControlSamplePropIds();
        if (propId == PropId_Value ||
            propId == PropId_Minimum ||
            propId == PropId_Maximum ||
            propId == ids.needleValue ||
            propId == ids.needleMinimum ||
            propId == ids.needleMaximum ||
            propId == ids.needleMinAngle ||
            propId == ids.needleMaxAngle) {
            UpdateAngle();
        }
    }

    void OnRender(CustomDrawingContext& dc) override {
        if (FirstChild() != nullptr) {
            return;
        }
        const RectF rect = FinalRect();
        const float w = (rect.width > 0.0f) ? rect.width : ((GetWidth() > 0.0f) ? GetWidth() : 200.0f);
        const float h = (rect.height > 0.0f) ? rect.height : ((GetHeight() > 0.0f) ? GetHeight() : 200.0f);
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }

        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float len = 0.5f * std::min(w, h) * 0.85f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = GetComputedAngle() * kDegToRad;
        const PointF tip{cx + std::sin(rad) * len, cy - std::cos(rad) * len};

        const auto& ids = GetCustomControlSamplePropIds();
        Color col{1.0f, 0.25f, 0.25f, 1.0f};
        if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
            col = *c;
        } else if (ids.needleBrush != 0xFFFFu) {
            if (auto c2 = value::TryGetColor(GetValue(ids.needleBrush))) {
                col = *c2;
            }
        }
        dc.DrawLine(PointF{cx, cy}, tip, col, 2.0f);
    }

private:
    void SyncRotateTransformFromChildState() noexcept {
        if (updatingAngle_) {
            return;
        }
        const float target = (FirstChild() != nullptr) ? GetComputedAngle() : 0.0f;
        float current = 0.0f;
        const bool hasCur = value::TryGetFloat(GetValue(PropId_AnimRotateAngle), current) && std::isfinite(current);
        if (std::fabs((hasCur ? current : 0.0f) - target) > 1e-4f) {
            updatingAngle_ = true;
            (void)SetValue(PropId_AnimRotateAngle, value::FromFloat(target), ValueSourceRank::Local);
            updatingAngle_ = false;
        }
    }

    void UpdateAngle() {
        const auto& ids = GetCustomControlSamplePropIds();
        const float minVal = ReadFloatWithFallback(PropId_Minimum, ids.needleMinimum, 0.0f);
        const float maxVal = ReadFloatWithFallback(PropId_Maximum, ids.needleMaximum, 100.0f);
        const float val = ReadFloatWithFallback(PropId_Value, ids.needleValue, 0.0f);
        const float minAngle = ReadCustomFloat(ids.needleMinAngle, 0.0f);
        const float maxAngle = ReadCustomFloat(ids.needleMaxAngle, 360.0f);

        const float denom = maxVal - minVal;
        const float ratio = (std::fabs(denom) > 1e-6f) ? ((val - minVal) / denom) : 0.0f;
        const float angle = minAngle + ratio * (maxAngle - minAngle);

        updatingAngle_ = true;
        if (ids.needleAngle != 0xFFFFu) {
            (void)SetValue(ids.needleAngle, value::FromFloat(angle), ValueSourceRank::Local);
        }
        const float transformAngle = (FirstChild() != nullptr) ? angle : 0.0f;
        (void)SetValue(PropId_AnimRotateAngle, value::FromFloat(transformAngle), ValueSourceRank::Local);
        updatingAngle_ = false;
        InvalidateVisual();
    }

    [[nodiscard]] float ReadCustomFloat(std::uint16_t propId, float fallback) const noexcept {
        if (propId == 0xFFFFu) {
            return fallback;
        }
        float out = fallback;
        if (value::TryGetFloat(GetValue(propId), out) && std::isfinite(out)) {
            return out;
        }
        return fallback;
    }

    [[nodiscard]] float ReadFloatWithFallback(
        std::uint16_t primaryId,
        std::uint16_t customId,
        float fallback) const noexcept {
        float out = fallback;
        if (value::TryGetFloat(GetValue(primaryId), out) && std::isfinite(out)) {
            return out;
        }
        return ReadCustomFloat(customId, fallback);
    }

    bool updatingAngle_ = false;
};

[[nodiscard]] inline std::shared_ptr<UIElement> CreateDateTimeControl() {
    return std::make_shared<DateTimeControl>();
}

[[nodiscard]] inline std::shared_ptr<UIElement> CreateRadialTicksControl() {
    return std::make_shared<RadialTicksControl>();
}

[[nodiscard]] inline std::shared_ptr<UIElement> CreateRadialNeedleControl() {
    return std::make_shared<RadialNeedleControl>();
}

inline void RegisterCustomControlSampleTypes() {
    auto& ids = GetCustomControlSamplePropIds();

    ids.dtUseSystemTime = RegisterCustomProperty(
        "DateTime", "UseSystemTime", CustomPropertyValueKind::Bool, value::FromBool(true));
    ids.dtDay = RegisterCustomProperty(
        "DateTime", "Day", CustomPropertyValueKind::Int, value::FromInt(1));
    ids.dtMonth = RegisterCustomProperty(
        "DateTime", "Month", CustomPropertyValueKind::Int, value::FromInt(1));
    ids.dtYear = RegisterCustomProperty(
        "DateTime", "Year", CustomPropertyValueKind::Int, value::FromInt(2000));
    ids.dtHour = RegisterCustomProperty(
        "DateTime", "Hour", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.dtMinute = RegisterCustomProperty(
        "DateTime", "Minute", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.dtSecond = RegisterCustomProperty(
        "DateTime", "Second", CustomPropertyValueKind::Float, value::FromFloat(0.0f));

    ids.ticksMinimum = RegisterCustomProperty(
        "RadialTicks", "Minimum", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.ticksMaximum = RegisterCustomProperty(
        "RadialTicks", "Maximum", CustomPropertyValueKind::Float, value::FromFloat(100.0f));
    ids.ticksSpacing = RegisterCustomProperty(
        "RadialTicks", "Spacing", CustomPropertyValueKind::Float, value::FromFloat(10.0f));
    ids.ticksCount = RegisterCustomProperty(
        "RadialTicks", "TickCount", CustomPropertyValueKind::Int, value::FromInt(0));
    ids.ticksLength = RegisterCustomProperty(
        "RadialTicks", "TickLength", CustomPropertyValueKind::Float, value::FromFloat(10.0f));
    ids.ticksWidth = RegisterCustomProperty(
        "RadialTicks", "TickWidth", CustomPropertyValueKind::Float, value::FromFloat(1.0f));
    ids.ticksMinAngle = RegisterCustomProperty(
        "RadialTicks", "MinAngle", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.ticksMaxAngle = RegisterCustomProperty(
        "RadialTicks", "MaxAngle", CustomPropertyValueKind::Float, value::FromFloat(360.0f));
    ids.ticksBrush = RegisterCustomProperty(
        "RadialTicks", "Brush", CustomPropertyValueKind::Brush, value::FromColor(Color{1.0f, 1.0f, 1.0f, 1.0f}));
    ids.ticksStroke = RegisterCustomProperty(
        "RadialTicks", "Stroke", CustomPropertyValueKind::Brush, value::FromColor(Color{1.0f, 1.0f, 1.0f, 1.0f}));
    ids.ticksStrokeThickness = RegisterCustomProperty(
        "RadialTicks", "StrokeThickness", CustomPropertyValueKind::Float, value::FromFloat(1.0f));

    ids.needleMinimum = RegisterCustomProperty(
        "RadialNeedle", "Minimum", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.needleMaximum = RegisterCustomProperty(
        "RadialNeedle", "Maximum", CustomPropertyValueKind::Float, value::FromFloat(100.0f));
    ids.needleValue = RegisterCustomProperty(
        "RadialNeedle", "Value", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.needleMinAngle = RegisterCustomProperty(
        "RadialNeedle", "MinAngle", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.needleMaxAngle = RegisterCustomProperty(
        "RadialNeedle", "MaxAngle", CustomPropertyValueKind::Float, value::FromFloat(360.0f));
    ids.needleAngle = RegisterCustomProperty(
        "RadialNeedle", "Angle", CustomPropertyValueKind::Float, value::FromFloat(0.0f));
    ids.needleBrush = RegisterCustomProperty(
        "RadialNeedle", "Brush", CustomPropertyValueKind::Brush, value::FromColor(Color{1.0f, 1.0f, 1.0f, 1.0f}));

    RegisterCustomControlType("DateTime", &CreateDateTimeControl, true);
    RegisterCustomControlType("local:DateTime", &CreateDateTimeControl, true);
    RegisterCustomControlType("CustomControl.DateTime", &CreateDateTimeControl, true);

    RegisterCustomControlType("RadialTicks", &CreateRadialTicksControl, false);
    RegisterCustomControlType("noesis:RadialTicks", &CreateRadialTicksControl, false);
    RegisterCustomControlType("local:RadialTicks", &CreateRadialTicksControl, false);
    RegisterCustomControlType("NoesisGUIExtensions.RadialTicks", &CreateRadialTicksControl, false);

    RegisterCustomControlType("RadialNeedle", &CreateRadialNeedleControl, false);
    RegisterCustomControlType("noesis:RadialNeedle", &CreateRadialNeedleControl, false);
    RegisterCustomControlType("local:RadialNeedle", &CreateRadialNeedleControl, false);
    RegisterCustomControlType("NoesisGUIExtensions.RadialNeedle", &CreateRadialNeedleControl, false);
}

} // namespace aero::samples

namespace aero {
using samples::RegisterCustomControlSampleTypes;
} // namespace aero
