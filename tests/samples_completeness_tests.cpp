#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/compiler/XamlApplication.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/core/UnifiedValue.hpp"

#include "../Samples/CustomAnimation/AeroCustomAnimationSample.hpp"
#include "../Samples/CustomControl/AeroCustomControlSample.hpp"
#include "../Samples/CustomRender/AeroCustomRenderSample.hpp"

namespace fs = std::filesystem;
using namespace aero;

namespace {

int gTotalChecks = 0;
int gFailedChecks = 0;

void CheckImpl(bool cond, const char* expr, int line, const std::string& msg = {}) {
    ++gTotalChecks;
    if (!cond) {
        ++gFailedChecks;
        std::fprintf(
            stderr,
            "[FAIL] samples_completeness_tests.cpp:%d: %s%s%s\n",
            line,
            expr,
            msg.empty() ? "" : " -- ",
            msg.c_str());
    }
}

#define AERO_CHECK(cond) CheckImpl((cond), #cond, __LINE__)
#define AERO_CHECK_MSG(cond, msg) CheckImpl((cond), #cond, __LINE__, (msg))

[[nodiscard]] bool ApproxEqual(float a, float b, float eps = 1e-3f) noexcept {
    return std::fabs(a - b) <= eps;
}

[[nodiscard]] fs::path DiscoverSamplesRoot() {
    std::vector<fs::path> candidates;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        candidates.emplace_back(envDir);
    }
#ifdef AERO_SAMPLES_DIR
    candidates.emplace_back(AERO_SAMPLES_DIR);
#endif
    static constexpr const char* kRelProbes[] = {
        "Samples",
        "../Samples",
        "../../Samples",
        "../../../Samples",
    };
    for (const char* rel : kRelProbes) {
        candidates.emplace_back(rel);
    }
    std::error_code ec;
    for (const auto& cand : candidates) {
        if (fs::is_directory(cand, ec) &&
            fs::exists(cand / "CustomControl" / "Data" / "MainPage.xaml", ec)) {
            return fs::weakly_canonical(cand, ec);
        }
    }
    return {};
}

class RecordingDrawingContext : public CustomDrawingContext {
public:
    struct RectCall {
        RectF rect{};
        float rx = 0.0f;
        float ry = 0.0f;
        Color color{};
        float strokeWidth = 0.0f;
        bool filled = false;
        bool rounded = false;
    };
    struct EllipseCall {
        PointF center{};
        float rx = 0.0f;
        float ry = 0.0f;
        Color color{};
        float strokeWidth = 0.0f;
        bool filled = false;
    };
    struct LineCall {
        PointF start{};
        PointF end{};
        Color color{};
        float strokeWidth = 0.0f;
    };
    struct TextCall {
        std::string text;
        RectF layoutRect{};
        Color color{};
        float fontSize = 0.0f;
    };

    std::vector<RectCall> rects;
    std::vector<EllipseCall> ellipses;
    std::vector<LineCall> lines;
    std::vector<TextCall> texts;

    void FillRect(const RectF& rect, const Color& color) override {
        rects.push_back(RectCall{rect, 0.0f, 0.0f, color, 0.0f, true, false});
    }

    void DrawRect(const RectF& rect, const Color& strokeColor, float strokeWidth = 1.0f) override {
        rects.push_back(RectCall{rect, 0.0f, 0.0f, strokeColor, strokeWidth, false, false});
    }

    void FillRoundedRect(
        const RectF& rect,
        float radiusX,
        float radiusY,
        const Color& color) override {
        rects.push_back(RectCall{rect, radiusX, radiusY, color, 0.0f, true, true});
    }

    void DrawRoundedRect(
        const RectF& rect,
        float radiusX,
        float radiusY,
        const Color& strokeColor,
        float strokeWidth = 1.0f) override {
        rects.push_back(RectCall{rect, radiusX, radiusY, strokeColor, strokeWidth, false, true});
    }

    void FillEllipse(
        const PointF& center,
        float radiusX,
        float radiusY,
        const Color& color) override {
        ellipses.push_back(EllipseCall{center, radiusX, radiusY, color, 0.0f, true});
    }

    void DrawEllipse(
        const PointF& center,
        float radiusX,
        float radiusY,
        const Color& strokeColor,
        float strokeWidth = 1.0f) override {
        ellipses.push_back(EllipseCall{center, radiusX, radiusY, strokeColor, strokeWidth, false});
    }

    void DrawLine(
        const PointF& start,
        const PointF& end,
        const Color& strokeColor,
        float strokeWidth = 1.0f) override {
        lines.push_back(LineCall{start, end, strokeColor, strokeWidth});
    }

    void DrawText(
        std::string_view text,
        const RectF& layoutRect,
        const Color& color,
        float fontSize = 14.0f,
        HorizontalAlignment hAlign = HorizontalAlignment::Left,
        VerticalAlignment vAlign = VerticalAlignment::Top) override {
        (void)hAlign;
        (void)vAlign;
        texts.push_back(TextCall{std::string(text), layoutRect, color, fontSize});
    }
};

void ResetSharedRegistries() {
    AxbTemplateRegistry::Instance().Clear();
    GradientBrushRegistry::Instance().Clear();
    UiResourceContext::Current().ClearXamlSearchDirectories();
}

void TestCustomControlSample(const fs::path& samplesRoot) {
    aero::samples::RegisterCustomControlSampleTypes();
    aero::samples::RegisterCustomControlSampleTypes(); // idempotent second call

    bool isDateTimeTemplated = false;
    AERO_CHECK(IsCustomControlRegistered("DateTime", &isDateTimeTemplated));
    AERO_CHECK(isDateTimeTemplated);

    bool isTicksTemplated = true;
    AERO_CHECK(IsCustomControlRegistered("RadialTicks", &isTicksTemplated));
    AERO_CHECK(!isTicksTemplated);

    bool isNeedleTemplated = true;
    AERO_CHECK(IsCustomControlRegistered("RadialNeedle", &isNeedleTemplated));
    AERO_CHECK(!isNeedleTemplated);

    const std::uint16_t useSysId = FindCustomProperty("DateTime", "UseSystemTime");
    const std::uint16_t dayId = FindCustomProperty("DateTime", "Day");
    const std::uint16_t monthId = FindCustomProperty("DateTime", "Month");
    const std::uint16_t yearId = FindCustomProperty("DateTime", "Year");
    const std::uint16_t hourId = FindCustomProperty("DateTime", "Hour");
    const std::uint16_t minId = FindCustomProperty("DateTime", "Minute");
    const std::uint16_t secId = FindCustomProperty("DateTime", "Second");
    AERO_CHECK(IsCustomPropertyId(useSysId));
    AERO_CHECK(IsCustomPropertyId(dayId));
    AERO_CHECK(IsCustomPropertyId(monthId));
    AERO_CHECK(IsCustomPropertyId(yearId));
    AERO_CHECK(IsCustomPropertyId(hourId));
    AERO_CHECK(IsCustomPropertyId(minId));
    AERO_CHECK(IsCustomPropertyId(secId));

    CustomPropertyDesc useSysDesc{};
    AERO_CHECK(TryGetCustomPropertyDesc(useSysId, useSysDesc));
    AERO_CHECK(useSysDesc.valueKind == CustomPropertyValueKind::Bool);
    UnifiedValue defaultUseSys{};
    AERO_CHECK(TryGetCustomPropertyDefaultValue(useSysId, defaultUseSys));
    bool defUseSysBool = false;
    AERO_CHECK(defaultUseSys.tryGetBool(defUseSysBool) && defUseSysBool == true);

    // DateTime static preservation before tick & when UseSystemTime=false
    {
        auto staticClock = CreateElementByTypeName("local:DateTime");
        AERO_CHECK(staticClock.Get() != nullptr);
        AERO_CHECK(staticClock->NeedsContinuousTick());
        (void)staticClock->SetValue(dayId, value::FromInt(6), ValueSourceRank::Local);
        (void)staticClock->SetValue(monthId, value::FromInt(2), ValueSourceRank::Local);
        (void)staticClock->SetValue(yearId, value::FromInt(2012), ValueSourceRank::Local);
        (void)staticClock->SetValue(hourId, value::FromFloat(19.0f), ValueSourceRank::Local);
        (void)staticClock->SetValue(minId, value::FromFloat(12.0f), ValueSourceRank::Local);
        (void)staticClock->SetValue(secId, value::FromFloat(51.0f), ValueSourceRank::Local);

        // Preserve explicit static values when UseSystemTime=false on tick
        (void)staticClock->SetValue(useSysId, value::FromBool(false), ValueSourceRank::Local);
        AERO_CHECK(!staticClock->NeedsContinuousTick());
        staticClock->TickControl(0.016f);
        std::int64_t d = 0, m = 0, y = 0;
        AERO_CHECK(staticClock->GetValue(dayId).tryGetInt64(d) && d == 6);
        AERO_CHECK(staticClock->GetValue(monthId).tryGetInt64(m) && m == 2);
        AERO_CHECK(staticClock->GetValue(yearId).tryGetInt64(y) && y == 2012);
        float h = 0.0f;
        AERO_CHECK(value::TryGetFloat(staticClock->GetValue(hourId), h) && ApproxEqual(h, 19.0f));
    }

    // DateTime system clock update when UseSystemTime=true
    {
        auto liveClock = CreateElementByTypeName("local:DateTime");
        AERO_CHECK(liveClock.Get() != nullptr);
        AERO_CHECK(liveClock->NeedsContinuousTick());
        liveClock->TickControl(0.016f);
        std::int64_t yearVal = 0, monthVal = 0, dayVal = 0;
        AERO_CHECK(liveClock->GetValue(yearId).tryGetInt64(yearVal) && yearVal >= 2024);
        AERO_CHECK(liveClock->GetValue(monthId).tryGetInt64(monthVal) && monthVal >= 1 && monthVal <= 12);
        AERO_CHECK(liveClock->GetValue(dayId).tryGetInt64(dayVal) && dayVal >= 1 && dayVal <= 31);
        float sec = -1.0f;
        AERO_CHECK(value::TryGetFloat(liveClock->GetValue(secId), sec) && sec >= 0.0f && sec < 60.0f);
    }

    // RadialTicks 12-o'clock clockwise tick rendering
    {
        const std::uint16_t tickLenId = FindCustomProperty("RadialTicks", "TickLength");
        const std::uint16_t tickWidthId = FindCustomProperty("RadialTicks", "TickWidth");
        const std::uint16_t tickCountId = FindCustomProperty("RadialTicks", "TickCount");
        AERO_CHECK(IsCustomPropertyId(tickLenId));
        AERO_CHECK(IsCustomPropertyId(tickWidthId));
        AERO_CHECK(IsCustomPropertyId(tickCountId));

        auto ticks = CreateElementByTypeName("noesis:RadialTicks");
        AERO_CHECK(ticks.Get() != nullptr);
        AERO_CHECK(ticks->HasCustomRender());
        (void)ticks->SetValue(PropId_Minimum, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)ticks->SetValue(PropId_Maximum, value::FromFloat(60.0f), ValueSourceRank::Local);
        (void)ticks->SetValue(PropId_Spacing, value::FromFloat(5.0f), ValueSourceRank::Local);
        (void)ticks->SetValue(tickLenId, value::FromFloat(10.0f), ValueSourceRank::Local);
        (void)ticks->SetValue(tickWidthId, value::FromFloat(2.0f), ValueSourceRank::Local);
        ticks->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});

        RecordingDrawingContext dc;
        ticks->OnRender(dc);
        AERO_CHECK_MSG(dc.lines.size() == 12u, "Expected 12 hour ticks on 0..60 spacing 5");
        if (dc.lines.size() == 12u) {
            // Tick 0: 12 o'clock -> (100, 0) to (100, 10)
            AERO_CHECK(ApproxEqual(dc.lines[0].start.x, 100.0f));
            AERO_CHECK(ApproxEqual(dc.lines[0].start.y, 0.0f));
            AERO_CHECK(ApproxEqual(dc.lines[0].end.x, 100.0f));
            AERO_CHECK(ApproxEqual(dc.lines[0].end.y, 10.0f));
            // Tick 3: 3 o'clock (90 deg) -> (200, 100) to (190, 100)
            AERO_CHECK(ApproxEqual(dc.lines[3].start.x, 200.0f));
            AERO_CHECK(ApproxEqual(dc.lines[3].start.y, 100.0f));
            AERO_CHECK(ApproxEqual(dc.lines[3].end.x, 190.0f));
            AERO_CHECK(ApproxEqual(dc.lines[3].end.y, 100.0f));
            AERO_CHECK(ApproxEqual(dc.lines[0].strokeWidth, 2.0f));
        }

        auto ticksByCount = CreateElementByTypeName("RadialTicks");
        AERO_CHECK(ticksByCount.Get() != nullptr);
        (void)ticksByCount->SetValue(tickCountId, value::FromInt(4), ValueSourceRank::Local);
        (void)ticksByCount->SetValue(
            FindCustomProperty("RadialTicks", "MaxAngle"),
            value::FromFloat(270.0f),
            ValueSourceRank::Local);
        ticksByCount->Arrange(Rect{0.0f, 0.0f, 100.0f, 100.0f});
        RecordingDrawingContext dcCount;
        ticksByCount->OnRender(dcCount);
        AERO_CHECK(dcCount.lines.size() == 4u);
    }

    // RadialNeedle angle calculation & rendering
    {
        const std::uint16_t angleId = FindCustomProperty("RadialNeedle", "Angle");
        const std::uint16_t minAngleId = FindCustomProperty("RadialNeedle", "MinAngle");
        const std::uint16_t maxAngleId = FindCustomProperty("RadialNeedle", "MaxAngle");
        AERO_CHECK(IsCustomPropertyId(angleId));
        AERO_CHECK(IsCustomPropertyId(minAngleId));
        AERO_CHECK(IsCustomPropertyId(maxAngleId));

        auto needle = CreateElementByTypeName("noesis:RadialNeedle");
        AERO_CHECK(needle.Get() != nullptr);
        (void)needle->SetValue(PropId_Minimum, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needle->SetValue(PropId_Maximum, value::FromFloat(12.0f), ValueSourceRank::Local);
        (void)needle->SetValue(minAngleId, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needle->SetValue(maxAngleId, value::FromFloat(360.0f), ValueSourceRank::Local);
        (void)needle->SetValue(PropId_Value, value::FromFloat(3.0f), ValueSourceRank::Local);

        float angleVal = 0.0f;
        float animRot = 0.0f;
        AERO_CHECK(value::TryGetFloat(needle->GetValue(angleId), angleVal) && ApproxEqual(angleVal, 90.0f));
        AERO_CHECK(
            value::TryGetFloat(needle->GetValue(PropId_AnimRotateAngle), animRot) &&
            ApproxEqual(animRot, 90.0f));

        (void)needle->SetValue(PropId_Value, value::FromFloat(6.0f), ValueSourceRank::Local);
        AERO_CHECK(value::TryGetFloat(needle->GetValue(angleId), angleVal) && ApproxEqual(angleVal, 180.0f));
        needle->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});
        AERO_CHECK(needle->HasCustomRender());
        RecordingDrawingContext dcNeedle;
        needle->OnRender(dcNeedle);
        AERO_CHECK(dcNeedle.lines.size() == 1u);
        if (!dcNeedle.lines.empty()) {
            // 180 deg = 6 o'clock (downward from center (100,100))
            AERO_CHECK(ApproxEqual(dcNeedle.lines[0].start.x, 100.0f));
            AERO_CHECK(ApproxEqual(dcNeedle.lines[0].start.y, 100.0f));
            AERO_CHECK(ApproxEqual(dcNeedle.lines[0].end.x, 100.0f));
            AERO_CHECK(dcNeedle.lines[0].end.y > 150.0f);
        }
    }

    // Compile and mount Samples/CustomControl/Data/MainPage.xaml
    if (!samplesRoot.empty()) {
        ResetSharedRegistries();
        const fs::path ccXaml = samplesRoot / "CustomControl" / "Data" / "MainPage.xaml";
        AeroCompiler compiler;
        AeroCompileInput in{};
        in.xamlPath = ccXaml.string();
        in.resourceSearchRoot = ccXaml.parent_path().string();
        in.resourceSearchPaths = {ccXaml.parent_path().string()};
        const AeroCompileResult res = compiler.Compile(in);
        AERO_CHECK_MSG(res.succeeded, "CustomControl MainPage.xaml failed to compile");
        if (res.succeeded) {
            auto host = std::make_shared<UserControl>("MainPage");
            AERO_CHECK(MountAxbTemplate(host.get(), res.axbPackage));
            UIElement* staticDt = host->FindName("DateTime");
            // Even when unnamed in XAML, verify at least one DateTime & RadialTicks node exists
            bool foundDateTime = false;
            bool foundRadialTicks = false;
            bool foundRadialNeedle = false;
            std::vector<UIElement*> stack{host.get()};
            while (!stack.empty()) {
                UIElement* cur = stack.back();
                stack.pop_back();
                if (!cur) continue;
                if (cur->DeclaredTypeName().find("DateTime") != std::string_view::npos) {
                    foundDateTime = true;
                } else if (cur->DeclaredTypeName().find("RadialTicks") != std::string_view::npos) {
                    foundRadialTicks = true;
                } else if (cur->DeclaredTypeName().find("RadialNeedle") != std::string_view::npos) {
                    foundRadialNeedle = true;
                }
                for (UIElement* c = cur->FirstChild(); c != nullptr; c = c->NextSibling()) {
                    stack.push_back(c);
                }
            }
            (void)staticDt;
            AERO_CHECK(foundDateTime);
            AERO_CHECK(foundRadialTicks);
            AERO_CHECK(foundRadialNeedle);
        }
    }
}

void TestCustomRenderSample(const fs::path& samplesRoot) {
    aero::samples::RegisterCustomRenderSampleTypes();
    aero::samples::RegisterCustomRenderSampleTypes(); // idempotent

    bool isGameTemplated = true;
    AERO_CHECK(IsCustomControlRegistered("Game", &isGameTemplated));
    AERO_CHECK(!isGameTemplated);

    const std::uint16_t scoreId = FindCustomProperty("Game", "Score");
    const std::uint16_t statusId = FindCustomProperty("Game", "StatusText");
    const std::uint16_t actionId = FindCustomProperty("Game", "ActionText");
    AERO_CHECK(IsCustomPropertyId(scoreId));
    AERO_CHECK(IsCustomPropertyId(statusId));
    AERO_CHECK(IsCustomPropertyId(actionId));

    auto gameElem = CreateElementByTypeName("local:Game");
    AERO_CHECK(gameElem.Get() != nullptr);
    auto* game = static_cast<aero::samples::BreakoutGameControl*>(gameElem.Get());

    AERO_CHECK(game->HasCustomRender());
    AERO_CHECK(game->NeedsContinuousTick());
    game->Measure(Size{1280.0f, 720.0f});
    AERO_CHECK(ApproxEqual(game->DesiredSize().width, 800.0f));
    AERO_CHECK(ApproxEqual(game->DesiredSize().height, 600.0f));
    game->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});

    AERO_CHECK(game->GetState() == aero::samples::BreakoutGameState::Start);
    AERO_CHECK(game->GetScore() == 0);
    AERO_CHECK(game->GetAliveBrickCount() == 65);
    AERO_CHECK(ApproxEqual(game->GetPaddleX(), 400.0f));
    AERO_CHECK(ApproxEqual(game->GetBallPos().x, 400.0f));
    AERO_CHECK(ApproxEqual(game->GetBallPos().y, 528.0f));

    // Render idle start screen
    {
        RecordingDrawingContext dc;
        game->OnRender(dc);
        AERO_CHECK(dc.rects.size() >= 66u); // background + border + 65 bricks + shadows + paddle
        AERO_CHECK(dc.ellipses.size() >= 2u); // ball + shadow
        AERO_CHECK(dc.texts.size() >= 2u); // score + "PRESS SPACE TO START"
    }

    // Spacebar starts the game
    AERO_CHECK(game->OnKeyDown(KeyCode::Space, 0));
    AERO_CHECK(game->GetState() == aero::samples::BreakoutGameState::Playing);

    // Ball velocity (-2, -4) * dt * 60
    const float dtFrame = 1.0f / 60.0f;
    game->TickControl(dtFrame);
    AERO_CHECK(ApproxEqual(game->GetBallPos().x, 398.0f));
    AERO_CHECK(ApproxEqual(game->GetBallPos().y, 524.0f));

    // Paddle pointer and keyboard controls
    AERO_CHECK(game->OnPointerMove(Point{180.0f, 550.0f}, 0));
    AERO_CHECK(ApproxEqual(game->GetPaddleX(), 180.0f));
    AERO_CHECK(game->OnKeyDown(KeyCode::Right, 0));
    game->TickControl(dtFrame);
    AERO_CHECK(ApproxEqual(game->GetPaddleX(), 190.0f));
    AERO_CHECK(game->OnKeyUp(KeyCode::Right, 0));

    // Brick hit -> +10 score, brick destroyed, 10 gravity particles spawned
    const auto& targetBrick = game->GetBrick(4, 6);
    game->SetBallStateForTest(
        targetBrick.x + targetBrick.w * 0.5f,
        targetBrick.y + targetBrick.h + 5.0f,
        0.0f,
        -4.0f);
    game->TickControl(dtFrame);
    AERO_CHECK(game->GetScore() == 10);
    AERO_CHECK(game->GetAliveBrickCount() == 64);
    AERO_CHECK(!game->GetBrick(4, 6).alive);
    AERO_CHECK(game->GetBallVelocity().y > 0.0f);
    AERO_CHECK(game->GetActiveParticleCount() == 10);
    std::int64_t scorePropVal = -1;
    AERO_CHECK(game->GetValue(scoreId).tryGetInt64(scorePropVal) && scorePropVal == 10);

    // Particles obey gravity vy += 900 * dt
    const float vyBefore = game->GetParticles().front().vy;
    game->TickControl(dtFrame);
    const float vyAfter = game->GetParticles().front().vy;
    AERO_CHECK(ApproxEqual(vyAfter - vyBefore, 900.0f * dtFrame, 0.5f));

    // Falling past bottom transitions to GameOver, Spacebar restarts cleanly
    game->SetBallStateForTest(400.0f, 610.0f, 0.0f, 4.0f);
    game->TickControl(dtFrame);
    AERO_CHECK(game->GetState() == aero::samples::BreakoutGameState::GameOver);
    AERO_CHECK(game->OnKeyDown(KeyCode::Space, 0));
    AERO_CHECK(game->GetState() == aero::samples::BreakoutGameState::Playing);
    AERO_CHECK(game->GetScore() == 0);
    AERO_CHECK(game->GetAliveBrickCount() == 65);

    // Compile and mount Samples/CustomRender/Data/MainWindow.xaml
    if (!samplesRoot.empty()) {
        ResetSharedRegistries();
        const fs::path crXaml = samplesRoot / "CustomRender" / "Data" / "MainWindow.xaml";
        AeroCompiler compiler;
        AeroCompileInput in{};
        in.xamlPath = crXaml.string();
        in.resourceSearchRoot = crXaml.parent_path().string();
        in.resourceSearchPaths = {crXaml.parent_path().string()};
        const AeroCompileResult res = compiler.Compile(in);
        AERO_CHECK_MSG(res.succeeded, "CustomRender MainWindow.xaml failed to compile");
        if (res.succeeded) {
            auto win = std::make_shared<Window>();
            AERO_CHECK(MountAxbTemplate(win.get(), res.axbPackage));
            bool foundGame = false;
            std::vector<UIElement*> stack{win.get()};
            while (!stack.empty()) {
                UIElement* cur = stack.back();
                stack.pop_back();
                if (!cur) continue;
                if (cur->HasCustomRender() && cur->NeedsContinuousTick()) {
                    foundGame = true;
                    break;
                }
                for (UIElement* c = cur->FirstChild(); c != nullptr; c = c->NextSibling()) {
                    stack.push_back(c);
                }
            }
            AERO_CHECK(foundGame);
        }
    }
}

void TestCustomAnimationSample() {
    aero::samples::RegisterCustomAnimationSampleTypes();
    aero::samples::RegisterCustomAnimationSampleTypes(); // idempotent

    // 1. BounceAnimation
    const CustomAnimationSpec* bounce = FindCustomAnimationType("local:BounceAnimation");
    AERO_CHECK(bounce != nullptr && bounce->evaluate != nullptr && bounce->parseAttr != nullptr);
    if (bounce) {
        AERO_CHECK(bounce->requiresFromTo);
        AERO_CHECK(GetCustomAnimationTypeById(bounce->defaultParams.animTypeId) == bounce);
        AERO_CHECK(ApproxEqual(bounce->defaultParams.params[0], 5.0f));
        AERO_CHECK(ApproxEqual(bounce->defaultParams.params[1], 3.0f));
        AERO_CHECK(ApproxEqual(
            bounce->evaluate(bounce->defaultParams, EasingMode::EaseInOut, 0.0f, 10.0f, 110.0f, 0.0f),
            10.0f));
        AERO_CHECK(ApproxEqual(
            bounce->evaluate(bounce->defaultParams, EasingMode::EaseInOut, 1.0f, 10.0f, 110.0f, 0.0f),
            110.0f));

        CustomAnimationParams p = bounce->defaultParams;
        AERO_CHECK(bounce->parseAttr("EdgeBehavior", "EaseOut", p));
        AERO_CHECK(bounce->parseAttr("Bounces", "4", p));
        AERO_CHECK(bounce->parseAttr("Bounciness", "2", p));
        AERO_CHECK(!bounce->parseAttr("Duration", "0:0:1", p));
        const float t = 0.25f;
        const float expectedOut =
            100.0f - std::fabs(std::pow(0.75f, 2.0f) * std::cos(6.2831853f * t * 4.0f)) * 100.0f;
        AERO_CHECK(ApproxEqual(
            bounce->evaluate(p, EasingMode::EaseOut, t, 0.0f, 100.0f, 0.0f),
            expectedOut,
            1e-2f));
    }

    // 2. ElasticAnimation
    const CustomAnimationSpec* elastic = FindCustomAnimationType("ElasticAnimation");
    AERO_CHECK(elastic != nullptr && elastic->evaluate != nullptr);
    if (elastic) {
        AERO_CHECK(elastic->requiresFromTo);
        AERO_CHECK(ApproxEqual(elastic->defaultParams.params[0], 3.0f));
        AERO_CHECK(ApproxEqual(elastic->defaultParams.params[1], 10.0f));
        CustomAnimationParams p = elastic->defaultParams;
        AERO_CHECK(elastic->parseAttr("EdgeBehavior", "EaseOut", p));
        AERO_CHECK(elastic->parseAttr("Oscillations", "1", p));
        AERO_CHECK(elastic->parseAttr("Springiness", "1", p));
        // At t=0.5 with cos(pi) = -1, EaseOut without fabs overshoots above endValue!
        const float mid = elastic->evaluate(p, EasingMode::EaseOut, 0.5f, 0.0f, 100.0f, 0.0f);
        AERO_CHECK_MSG(mid > 100.0f, "ElasticAnimation EaseOut should overshoot past endValue");
    }

    // 3. BackAnimation
    const CustomAnimationSpec* back = FindCustomAnimationType("BackAnimation");
    AERO_CHECK(back != nullptr && back->evaluate != nullptr);
    if (back) {
        AERO_CHECK(back->requiresFromTo);
        const float pull = back->evaluate(back->defaultParams, EasingMode::EaseIn, 0.5f, 0.0f, 100.0f, 0.0f);
        AERO_CHECK_MSG(pull < 0.0f, "BackAnimation EaseIn should pull back below startValue");
        AERO_CHECK(ApproxEqual(
            back->evaluate(back->defaultParams, EasingMode::EaseIn, 1.0f, 0.0f, 100.0f, 0.0f),
            100.0f));
    }

    // 4. ExponentialAnimation
    const CustomAnimationSpec* expo = FindCustomAnimationType("ExponentialAnimation");
    AERO_CHECK(expo != nullptr && expo->evaluate != nullptr);
    if (expo) {
        AERO_CHECK(expo->requiresFromTo);
        AERO_CHECK(ApproxEqual(
            expo->evaluate(expo->defaultParams, EasingMode::EaseIn, 0.5f, 0.0f, 100.0f, 0.0f),
            25.0f));
        CustomAnimationParams pOut = expo->defaultParams;
        AERO_CHECK(expo->parseAttr("EdgeBehavior", "EaseOut", pOut));
        AERO_CHECK(ApproxEqual(
            expo->evaluate(pOut, EasingMode::EaseOut, 0.25f, 0.0f, 100.0f, 0.0f),
            50.0f));
    }

    // 5. CircleAnimation
    const CustomAnimationSpec* circle = FindCustomAnimationType("CircleAnimation");
    AERO_CHECK(circle != nullptr && circle->evaluate != nullptr);
    if (circle) {
        AERO_CHECK(!circle->requiresFromTo);
        const float origin = 100.0f;
        AERO_CHECK(ApproxEqual(
            circle->evaluate(circle->defaultParams, EasingMode::EaseOut, 0.0f, 0.0f, 0.0f, origin),
            100.0f));
        AERO_CHECK(ApproxEqual(
            circle->evaluate(circle->defaultParams, EasingMode::EaseOut, 0.5f, 0.0f, 0.0f, origin),
            120.0f));
        CustomAnimationParams pY = circle->defaultParams;
        AERO_CHECK(circle->parseAttr("Direction", "YDirection", pY));
        AERO_CHECK(circle->parseAttr("Radius", "20", pY));
        AERO_CHECK(ApproxEqual(
            circle->evaluate(pY, EasingMode::EaseOut, 0.25f, 0.0f, 0.0f, origin),
            80.0f));
        AERO_CHECK(ApproxEqual(
            circle->evaluate(pY, EasingMode::EaseOut, 0.75f, 0.0f, 0.0f, origin),
            120.0f));
    }

    // 6. ShakeAnimation
    const CustomAnimationSpec* shake = FindCustomAnimationType("ShakeAnimation");
    AERO_CHECK(shake != nullptr && shake->evaluate != nullptr);
    if (shake) {
        AERO_CHECK(!shake->requiresFromTo);
        AERO_CHECK(ApproxEqual(
            shake->evaluate(shake->defaultParams, EasingMode::EaseOut, 0.0f, 5.0f, 0.0f, 5.0f),
            5.0f));
        AERO_CHECK(ApproxEqual(
            shake->evaluate(shake->defaultParams, EasingMode::EaseOut, 1.0f, 5.0f, 0.0f, 5.0f),
            5.0f));
        const float early =
            shake->evaluate(shake->defaultParams, EasingMode::EaseOut, 0.05f, 0.0f, 0.0f, 0.0f);
        AERO_CHECK(std::fabs(early) > 10.0f);
    }
}

void TestSampleXamlsAndWpfQuickStart(const fs::path& samplesRoot) {
    if (samplesRoot.empty()) {
        AERO_CHECK_MSG(false, "Failed to discover Samples/ directory");
        return;
    }

    // Compile Integration, Theme (+ DarkBlue theme resources), VideoEffect XAMLs
    {
        AeroCompiler compiler;
        const fs::path themeDataDir = samplesRoot / "Theme" / "Data";
        const fs::path darkBlueTheme = themeDataDir / "Theme" / "NoesisTheme.DarkBlue.xaml";

        ResetSharedRegistries();
        if (fs::exists(darkBlueTheme)) {
            AeroCompileInput dictIn{};
            dictIn.xamlPath = darkBlueTheme.string();
            dictIn.resourceSearchRoot = themeDataDir.string();
            dictIn.resourceSearchPaths = {themeDataDir.string(), (themeDataDir / "Theme").string()};
            const AeroCompileResult dictRes = compiler.Compile(dictIn);
            AERO_CHECK(dictRes.succeeded);
            if (dictRes.succeeded) {
                AxbTemplateRegistry::Instance().RegisterResources(dictRes.axbPackage);
            }
        }

        const fs::path themeMain = themeDataDir / "MainWindow.xaml";
        AeroCompileInput themeIn{};
        themeIn.xamlPath = themeMain.string();
        themeIn.resourceSearchRoot = themeDataDir.string();
        themeIn.resourceSearchPaths = {themeDataDir.string(), (themeDataDir / "Theme").string()};
        const AeroCompileResult themeRes = compiler.Compile(themeIn);
        AERO_CHECK_MSG(themeRes.succeeded, "Theme/Data/MainWindow.xaml failed to compile");

        for (const char* rel : {
                 "Integration/Data/MainPage.xaml",
                 "VideoEffect/Data/MainPage.xaml"}) {
            ResetSharedRegistries();
            const fs::path p = samplesRoot / rel;
            AeroCompileInput in{};
            in.xamlPath = p.string();
            in.resourceSearchRoot = p.parent_path().string();
            in.resourceSearchPaths = {p.parent_path().string()};
            const AeroCompileResult r = compiler.Compile(in);
            AERO_CHECK_MSG(r.succeeded, std::string("Sample XAML failed to compile: ") + rel);
        }
    }

    // WpfQuickStart full XamlApplication load with mounted SprintSummaryCard
    {
        ResetSharedRegistries();
        const fs::path csproj = samplesRoot / "WpfQuickStart" / "Data" / "WpfQuickStart.csproj";
        AERO_CHECK(fs::exists(csproj));
        XamlApplication app;
        std::string err;
        const bool loaded = app.Load(csproj.string(), &err);
        AERO_CHECK_MSG(loaded, "WpfQuickStart XamlApplication::Load failed: " + err);
        if (loaded && app.MainWindowElement()) {
            UIElement* root = app.MainWindowElement();
            AERO_CHECK(root->FindName("HeaderBanner") != nullptr);
            AERO_CHECK(root->FindName("TasksList") != nullptr);
            AERO_CHECK(root->FindName("TasksGrid") != nullptr);

            UIElement* card = root->FindName("SprintSummary");
            AERO_CHECK_MSG(card != nullptr, "SprintSummaryCard should be mounted in WpfQuickStart MainWindow");
            if (card) {
                UIElement* badgeNode = card->FindName("BadgeLabel");
                UIElement* boostNode = card->FindName("BoostBtn");
                auto* badge = badgeNode ? badgeNode->DynamicCast<TextBlock>() : nullptr;
                auto* boostBtn = boostNode ? boostNode->DynamicCast<Button>() : nullptr;
                AERO_CHECK(badge != nullptr);
                AERO_CHECK(boostBtn != nullptr);
                if (badge && boostBtn) {
                    AERO_CHECK(badge->GetText() == "Sprint Velocity: Normal");
                    AERO_CHECK(boostBtn->PerformClick());
                    AERO_CHECK(badge->GetText() == "Sprint Velocity: x2");
                }
            }
        }
    }
}

} // namespace

int main() {
    const fs::path samplesRoot = DiscoverSamplesRoot();
    TestCustomControlSample(samplesRoot);
    TestCustomRenderSample(samplesRoot);
    TestCustomAnimationSample();
    TestSampleXamlsAndWpfQuickStart(samplesRoot);

    std::printf(
        "[samples_completeness_tests] %d/%d checks passed.\n",
        gTotalChecks - gFailedChecks,
        gTotalChecks);
    return (gFailedChecks == 0) ? 0 : 1;
}
