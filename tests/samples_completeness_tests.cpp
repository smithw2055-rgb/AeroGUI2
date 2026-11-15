#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/compiler/XamlApplication.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

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
    FontAtlas::Instance().ClearCustomFonts();
    UiResourceContext::Current().ClearAll();
}

constexpr Color kTestClearColor{0.06f, 0.08f, 0.11f, 1.0f};

struct SampleRuntimeHarness {
    ExecutionDomainContext domains;
    AnimationEngine anim;
    DualTrackBindingEngine binding{domains};
    StyleTriggerEngine style{&anim, &binding};
    InputEventRouter input;

    SampleRuntimeHarness() {
        binding.SetAnimationEngine(&anim);
        style.ImportRegistryStyles();
        input.SetStyleTriggerEngine(&style);
        input.SetAnimationEngine(&anim);
    }
};

void LoadFontsInDirectory(const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (ext == ".ttf" || ext == ".otf") {
            const std::string stem = entry.path().stem().string();
            (void)FontAtlas::Instance().LoadTrueTypeFontFromFile(
                entry.path().string(),
                stem,
                false,
                nullptr);
        }
    }
}

[[nodiscard]] bool RoundTripAxbPackage(const AxbPackage& src, AxbPackage& outPkg) {
    const std::vector<std::uint8_t> bytes = src.Serialize();
    if (bytes.empty()) {
        return false;
    }
    return AxbPackage::Deserialize(bytes, outPkg, nullptr);
}

[[nodiscard]] bool CompileAndRoundTripXaml(
    const AeroCompiler& compiler,
    const fs::path& xamlPath,
    const fs::path& resourceRoot,
    const std::vector<fs::path>& searchPaths,
    AxbPackage& outPkg,
    const std::vector<fs::path>& preloadDicts = {}) {
    for (const fs::path& dictPath : preloadDicts) {
        if (!fs::exists(dictPath)) {
            return false;
        }
        AeroCompileInput din{};
        din.xamlPath = dictPath.string();
        din.resourceSearchRoot = resourceRoot.string();
        for (const auto& sp : searchPaths) {
            din.resourceSearchPaths.push_back(sp.string());
        }
        const AeroCompileResult dRes = compiler.Compile(din);
        if (!dRes.succeeded) {
            return false;
        }
        AxbPackage roundDict;
        if (!RoundTripAxbPackage(dRes.axbPackage, roundDict)) {
            return false;
        }
        AxbTemplateRegistry::Instance().RegisterResources(roundDict);
    }

    AeroCompileInput in{};
    in.xamlPath = xamlPath.string();
    in.resourceSearchRoot = resourceRoot.string();
    for (const auto& sp : searchPaths) {
        in.resourceSearchPaths.push_back(sp.string());
    }
    const AeroCompileResult res = compiler.Compile(in);
    if (!res.succeeded) {
        return false;
    }
    return RoundTripAxbPackage(res.axbPackage, outPkg);
}

[[nodiscard]] bool CompileAndRoundTripInlineXaml(
    const AeroCompiler& compiler,
    std::string_view virtualPath,
    std::string_view xamlText,
    AxbPackage& outPkg) {
    AeroCompileInput in{};
    in.xamlPath = std::string(virtualPath);
    in.xamlContent = std::string(xamlText);
    const AeroCompileResult res = compiler.Compile(in);
    if (!res.succeeded) {
        return false;
    }
    return RoundTripAxbPackage(res.axbPackage, outPkg);
}

[[nodiscard]] IntrusivePtr<UIElement> MountAndPrepareSampleRoot(
    SampleRuntimeHarness& rt,
    const AxbPackage& pkg,
    float width,
    float height,
    std::string_view fallbackType = "Page") {
    AxbTemplateRegistry::Instance().RegisterResources(pkg);
    rt.style.RegisterPackageStyles(pkg);
    const std::string_view typeName =
        pkg.rootTypeName.empty() ? fallbackType : std::string_view(pkg.rootTypeName);
    IntrusivePtr<UIElement> root = CreateElementByTypeName(typeName);
    if (!root) {
        root = CreateElementByTypeName(fallbackType);
    }
    if (!root) {
        return {};
    }
    if (!MountAxbTemplate(
            root.Get(),
            pkg,
            &rt.domains,
            &rt.binding,
            ObjectRef{},
            nullptr,
            nullptr,
            &rt.anim)) {
        return {};
    }
    (void)rt.style.BindSubtreeStyles(root.Get(), true);
    (void)rt.style.EvaluateAndApply();
    (void)rt.binding.FlushTrackA();
    rt.input.SetRootVisual(root.Get());
    root->Measure(Size{width, height});
    root->Arrange(Rect{0.0f, 0.0f, width, height});
    return root;
}

[[nodiscard]] std::vector<std::uint8_t> RasterizeRgba8(
    const UIElement* root,
    std::uint32_t w,
    std::uint32_t h,
    Color clear = kTestClearColor) {
    std::vector<std::uint8_t> rgba;
    if (!root || w == 0u || h == 0u) {
        return rgba;
    }
    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root);
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, w, h, rgba, clear);
    return rgba;
}

[[nodiscard]] std::array<std::uint8_t, 4> ClearRgba8Bytes(Color clear = kTestClearColor) noexcept {
    auto toByte = [](float c) noexcept -> std::uint8_t {
        const float clamped = std::clamp(c, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
    };
    return {toByte(clear.r), toByte(clear.g), toByte(clear.b), toByte(clear.a)};
}

[[nodiscard]] std::size_t CountNonClearInRect(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t imgW,
    std::uint32_t imgH,
    int x0,
    int y0,
    int x1,
    int y1,
    Color clear = kTestClearColor) noexcept {
    if (rgba.size() < static_cast<std::size_t>(imgW) * imgH * 4u) {
        return 0u;
    }
    const auto bg = ClearRgba8Bytes(clear);
    const int cx0 = std::max(0, x0);
    const int cy0 = std::max(0, y0);
    const int cx1 = std::min(static_cast<int>(imgW), x1);
    const int cy1 = std::min(static_cast<int>(imgH), y1);
    std::size_t count = 0u;
    for (int y = cy0; y < cy1; ++y) {
        for (int x = cx0; x < cx1; ++x) {
            const std::size_t off = (static_cast<std::size_t>(y) * imgW + static_cast<std::size_t>(x)) * 4u;
            if (std::abs(static_cast<int>(rgba[off + 0u]) - static_cast<int>(bg[0])) > 3 ||
                std::abs(static_cast<int>(rgba[off + 1u]) - static_cast<int>(bg[1])) > 3 ||
                std::abs(static_cast<int>(rgba[off + 2u]) - static_cast<int>(bg[2])) > 3) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] bool BuffersDiffer(
    const std::vector<std::uint8_t>& a,
    const std::vector<std::uint8_t>& b,
    std::size_t* outDiffPixels = nullptr) noexcept {
    if (a.size() != b.size()) {
        if (outDiffPixels) {
            *outDiffPixels = std::max(a.size(), b.size()) / 4u;
        }
        return true;
    }
    std::size_t diff = 0u;
    for (std::size_t i = 0u; i + 3u < a.size(); i += 4u) {
        if (std::abs(static_cast<int>(a[i + 0u]) - static_cast<int>(b[i + 0u])) > 2 ||
            std::abs(static_cast<int>(a[i + 1u]) - static_cast<int>(b[i + 1u])) > 2 ||
            std::abs(static_cast<int>(a[i + 2u]) - static_cast<int>(b[i + 2u])) > 2 ||
            std::abs(static_cast<int>(a[i + 3u]) - static_cast<int>(b[i + 3u])) > 2) {
            ++diff;
        }
    }
    if (outDiffPixels) {
        *outDiffPixels = diff;
    }
    return diff > 0u;
}

[[nodiscard]] double MeanChromaInRect(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t imgW,
    std::uint32_t imgH,
    int x0,
    int y0,
    int x1,
    int y1) noexcept {
    if (rgba.size() < static_cast<std::size_t>(imgW) * imgH * 4u) {
        return 0.0;
    }
    const int cx0 = std::max(0, x0);
    const int cy0 = std::max(0, y0);
    const int cx1 = std::min(static_cast<int>(imgW), x1);
    const int cy1 = std::min(static_cast<int>(imgH), y1);
    double sumChroma = 0.0;
    std::size_t count = 0u;
    for (int y = cy0; y < cy1; ++y) {
        for (int x = cx0; x < cx1; ++x) {
            const std::size_t off = (static_cast<std::size_t>(y) * imgW + static_cast<std::size_t>(x)) * 4u;
            const int r = rgba[off + 0u];
            const int g = rgba[off + 1u];
            const int b = rgba[off + 2u];
            sumChroma += static_cast<double>(std::max({r, g, b}) - std::min({r, g, b}));
            ++count;
        }
    }
    return count > 0u ? (sumChroma / static_cast<double>(count)) : 0.0;
}

[[nodiscard]] std::vector<UIElement*> CollectElementsByTypeSubstr(
    UIElement* root,
    std::string_view substr) {
    std::vector<UIElement*> result;
    if (!root) {
        return result;
    }
    std::vector<UIElement*> stack{root};
    while (!stack.empty()) {
        UIElement* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        if (cur->DeclaredTypeName().find(substr) != std::string_view::npos ||
            (cur->GetTypeClass() != nullptr && cur->GetTypeClass()->typeName != nullptr &&
             std::string_view(cur->GetTypeClass()->typeName).find(substr) != std::string_view::npos)) {
            result.push_back(cur);
        }
        for (UIElement* c = cur->FirstChild(); c != nullptr; c = c->NextSibling()) {
            stack.push_back(c);
        }
    }
    return result;
}

[[nodiscard]] Point ElementScreenCenter(const UIElement* elem) noexcept {
    if (!elem) {
        return Point{0.0f, 0.0f};
    }
    const Rect screen = elem->LocalToScreenBounds(elem->FinalRect());
    return Point{screen.x + screen.width * 0.5f, screen.y + screen.height * 0.5f};
}

[[nodiscard]] UIElement* FindNameInAnyNamescope(UIElement* root, std::string_view name) {
    if (!root || name.empty()) {
        return nullptr;
    }
    std::vector<UIElement*> stack{root};
    while (!stack.empty()) {
        UIElement* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        if (cur->Name() == name) {
            return cur;
        }
        if (UIElement* found = cur->FindName(name)) {
            return found;
        }
        if (UIElement* tmplRoot = cur->TemplateRoot()) {
            stack.push_back(tmplRoot);
        }
        for (UIElement* c = cur->FirstChild(); c != nullptr; c = c->NextSibling()) {
            stack.push_back(c);
        }
    }
    return nullptr;
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

    // RadialNeedle angle calculation, zero double-rotation rasterization, & child-transform mode
    {
        const std::uint16_t angleId = FindCustomProperty("RadialNeedle", "Angle");
        const std::uint16_t minAngleId = FindCustomProperty("RadialNeedle", "MinAngle");
        const std::uint16_t maxAngleId = FindCustomProperty("RadialNeedle", "MaxAngle");
        AERO_CHECK(IsCustomPropertyId(angleId));
        AERO_CHECK(IsCustomPropertyId(minAngleId));
        AERO_CHECK(IsCustomPropertyId(maxAngleId));

        // 1. Childless RadialNeedle (OnRender draws already-rotated line; PropId_AnimRotateAngle must stay 0)
        auto needle = CreateElementByTypeName("noesis:RadialNeedle");
        AERO_CHECK(needle.Get() != nullptr);
        (void)needle->SetValue(PropId_Minimum, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needle->SetValue(PropId_Maximum, value::FromFloat(12.0f), ValueSourceRank::Local);
        (void)needle->SetValue(minAngleId, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needle->SetValue(maxAngleId, value::FromFloat(360.0f), ValueSourceRank::Local);
        (void)needle->SetValue(PropId_Value, value::FromFloat(3.0f), ValueSourceRank::Local);
        needle->SetWidth(200.0f);
        needle->SetHeight(200.0f);
        needle->Measure(Size{200.0f, 200.0f});
        needle->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});

        float angleVal = 0.0f;
        float animRot = -1.0f;
        AERO_CHECK(value::TryGetFloat(needle->GetValue(angleId), angleVal) && ApproxEqual(angleVal, 90.0f));
        AERO_CHECK(
            !value::TryGetFloat(needle->GetValue(PropId_AnimRotateAngle), animRot) ||
            ApproxEqual(animRot, 0.0f));
        AERO_CHECK(needle->HasCustomRender());

        // Rasterize childless needle at Value=3 (90 deg = 3 o'clock: horizontal right from center (100,100)).
        // Double-rotation would mistakenly draw at 180 deg (6 o'clock: downward).
        const auto rgba90 = RasterizeRgba8(needle.Get(), 200u, 200u);
        const std::size_t threeOClockPx = CountNonClearInRect(rgba90, 200u, 200u, 120, 96, 175, 105);
        const std::size_t sixOClockAt90Px = CountNonClearInRect(rgba90, 200u, 200u, 96, 120, 105, 175);
        AERO_CHECK_MSG(threeOClockPx > 10u, "Childless RadialNeedle at 90 deg must draw along 3 o'clock");
        AERO_CHECK_MSG(sixOClockAt90Px == 0u, "Childless RadialNeedle must NOT double-rotate to 6 o'clock");

        (void)needle->SetValue(PropId_Value, value::FromFloat(6.0f), ValueSourceRank::Local);
        needle->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});
        AERO_CHECK(value::TryGetFloat(needle->GetValue(angleId), angleVal) && ApproxEqual(angleVal, 180.0f));
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
        const auto rgba180 = RasterizeRgba8(needle.Get(), 200u, 200u);
        const std::size_t sixOClockAt180Px = CountNonClearInRect(rgba180, 200u, 200u, 96, 120, 105, 175);
        AERO_CHECK_MSG(sixOClockAt180Px > 10u, "Childless RadialNeedle at 180 deg must draw along 6 o'clock");

        // 2. Child-bearing RadialNeedle (syncs PropId_AnimRotateAngle cleanly even when child attaches later)
        auto needleWithChild = CreateElementByTypeName("noesis:RadialNeedle");
        AERO_CHECK(needleWithChild.Get() != nullptr);
        (void)needleWithChild->SetValue(PropId_Minimum, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needleWithChild->SetValue(PropId_Maximum, value::FromFloat(12.0f), ValueSourceRank::Local);
        (void)needleWithChild->SetValue(minAngleId, value::FromFloat(0.0f), ValueSourceRank::Local);
        (void)needleWithChild->SetValue(maxAngleId, value::FromFloat(360.0f), ValueSourceRank::Local);
        (void)needleWithChild->SetValue(PropId_Value, value::FromFloat(3.0f), ValueSourceRank::Local);
        auto childMarker = MakeIntrusive<Border>();
        childMarker->SetWidth(4.0f);
        childMarker->SetHeight(80.0f);
        childMarker->SetBackground(Color{1.0f, 1.0f, 1.0f, 1.0f});
        needleWithChild->AddChild(childMarker.Get());
        needleWithChild->SetWidth(200.0f);
        needleWithChild->SetHeight(200.0f);
        needleWithChild->Measure(Size{200.0f, 200.0f});
        needleWithChild->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});
        AERO_CHECK(!needleWithChild->HasCustomRender());
        AERO_CHECK(
            value::TryGetFloat(needleWithChild->GetValue(PropId_AnimRotateAngle), animRot) &&
            ApproxEqual(animRot, 90.0f));
    }

    // Compile, .axb4 round-trip, mount, lay out, animate, and rasterize Samples/CustomControl/Data/MainPage.xaml
    if (!samplesRoot.empty()) {
        ResetSharedRegistries();
        const fs::path ccXaml = samplesRoot / "CustomControl" / "Data" / "MainPage.xaml";
        LoadFontsInDirectory(ccXaml.parent_path());
        AeroCompiler compiler;
        AxbPackage ccPkg;
        const bool compiled = CompileAndRoundTripXaml(
            compiler, ccXaml, ccXaml.parent_path(), {ccXaml.parent_path()}, ccPkg);
        AERO_CHECK_MSG(compiled, "CustomControl MainPage.xaml failed to compile / .axb4 round-trip");
        if (compiled) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> host = MountAndPrepareSampleRoot(rt, ccPkg, 800.0f, 600.0f, "Page");
            AERO_CHECK(host.Get() != nullptr);
            if (host) {
                std::vector<UIElement*> dateTimes = CollectElementsByTypeSubstr(host.Get(), "DateTime");
                std::vector<UIElement*> ticksList = CollectElementsByTypeSubstr(host.Get(), "RadialTicks");
                std::vector<UIElement*> needlesList = CollectElementsByTypeSubstr(host.Get(), "RadialNeedle");
                AERO_CHECK(dateTimes.size() >= 2u);
                AERO_CHECK(!ticksList.empty());
                AERO_CHECK(needlesList.size() >= 3u);

                // Verify Digital DateTime displays Hour/Minute/Day/Month/Year via TemplatedParent + StringFormat + MathConverter
                std::vector<std::string> texts;
                for (UIElement* tbElem : CollectElementsByTypeSubstr(host.Get(), "TextBlock")) {
                    if (auto* tb = tbElem->DynamicCast<TextBlock>()) {
                        texts.push_back(tb->GetText());
                    }
                }
                auto hasText = [&](std::string_view expected) -> bool {
                    return std::find(texts.begin(), texts.end(), expected) != texts.end();
                };
                AERO_CHECK_MSG(hasText("19"), "Digital DateTime Hour (floor + StringFormat) missing '19'");
                AERO_CHECK_MSG(hasText("12"), "Digital DateTime Minute (floor + 00) missing '12'");
                AERO_CHECK_MSG(hasText("06"), "Digital DateTime Day (00) missing '06'");
                AERO_CHECK_MSG(hasText("02"), "Digital DateTime Month (00) missing '02'");
                AERO_CHECK_MSG(hasText("2012"), "Digital DateTime Year missing '2012'");

                // Verify Analog DateTime Hour needle evaluates 19 % 12 -> 7 -> 210 deg without double-rotation
                bool foundHour210 = false;
                const std::uint16_t needleAngleId = FindCustomProperty("RadialNeedle", "Angle");
                for (UIElement* n : needlesList) {
                    float a = 0.0f;
                    float rot = 0.0f;
                    if (value::TryGetFloat(n->GetValue(needleAngleId), a) &&
                        ApproxEqual(a, 210.0f, 0.5f) &&
                        value::TryGetFloat(n->GetValue(PropId_AnimRotateAngle), rot) &&
                        ApproxEqual(rot, 210.0f, 0.5f)) {
                        foundHour210 = true;
                        break;
                    }
                }
                AERO_CHECK_MSG(foundHour210, "Analog DateTime Hour RadialNeedle should resolve 19%12 to 210 deg");

                const auto rgbaBefore = RasterizeRgba8(host.Get(), 800u, 600u);
                const Rect ticksScreen = ticksList.front()->LocalToScreenBounds(ticksList.front()->FinalRect());
                const std::size_t ticksPixels = CountNonClearInRect(
                    rgbaBefore,
                    800u,
                    600u,
                    static_cast<int>(std::floor(ticksScreen.x)),
                    static_cast<int>(std::floor(ticksScreen.y)),
                    static_cast<int>(std::ceil(ticksScreen.x + ticksScreen.width)),
                    static_cast<int>(std::ceil(ticksScreen.y + ticksScreen.height)));
                AERO_CHECK_MSG(ticksPixels > 20u, "Analog RadialTicks rendered zero non-clear pixels");

                // Update Second on all DateTime controls (analog + digital) and verify animated needle update + pixel change
                if (!dateTimes.empty()) {
                    for (UIElement* dt : dateTimes) {
                        (void)dt->SetValue(secId, value::FromFloat(15.0f), ValueSourceRank::Local);
                    }
                    (void)rt.binding.FlushTrackA();
                    bool foundSecond90 = false;
                    for (UIElement* n : needlesList) {
                        float a = 0.0f;
                        if (value::TryGetFloat(n->GetValue(needleAngleId), a) && ApproxEqual(a, 90.0f, 0.5f)) {
                            foundSecond90 = true;
                            break;
                        }
                    }
                    AERO_CHECK(foundSecond90);
                    const auto rgbaAfter = RasterizeRgba8(host.Get(), 800u, 600u);
                    AERO_CHECK_MSG(
                        BuffersDiffer(rgbaBefore, rgbaAfter),
                        "Updating analog DateTime Second should update RadialNeedle rendered pixels");
                }

                // Exercise TickControl(dt) live system clock path
                (void)TickLiveControlsInSubtree(host.Get(), 0.25f);
                (void)rt.binding.FlushTrackA();
                AERO_CHECK(RasterizeRgba8(host.Get(), 800u, 600u).size() == 800u * 600u * 4u);
            }
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

    // Compile, .axb4 round-trip, mount, lay out, interact, tick, and rasterize Samples/CustomRender/Data/MainWindow.xaml
    if (!samplesRoot.empty()) {
        ResetSharedRegistries();
        const fs::path crXaml = samplesRoot / "CustomRender" / "Data" / "MainWindow.xaml";
        LoadFontsInDirectory(crXaml.parent_path());
        AeroCompiler compiler;
        AxbPackage crPkg;
        const bool compiled = CompileAndRoundTripXaml(
            compiler, crXaml, crXaml.parent_path(), {crXaml.parent_path()}, crPkg);
        AERO_CHECK_MSG(compiled, "CustomRender MainWindow.xaml failed to compile / .axb4 round-trip");
        if (compiled) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> win = MountAndPrepareSampleRoot(rt, crPkg, 800.0f, 600.0f, "Window");
            AERO_CHECK(win.Get() != nullptr);
            if (win) {
                UIElement* gameElem = win->FindName("GameControl");
                AERO_CHECK(gameElem != nullptr);
                aero::samples::BreakoutGameControl* mountedGame = nullptr;
                if (gameElem && gameElem->HasCustomRender() && gameElem->NeedsContinuousTick()) {
                    mountedGame = static_cast<aero::samples::BreakoutGameControl*>(gameElem);
                }
                AERO_CHECK(mountedGame != nullptr);

                // Collect bound HUD TextBlocks for ActionText & Score
                bool sawStartPrompt = false;
                bool sawZeroScore = false;
                for (UIElement* tbElem : CollectElementsByTypeSubstr(win.Get(), "TextBlock")) {
                    if (auto* tb = tbElem->DynamicCast<TextBlock>()) {
                        if (tb->GetText() == "PRESS SPACE TO START") {
                            sawStartPrompt = true;
                        } else if (tb->GetText() == "0") {
                            sawZeroScore = true;
                        }
                    }
                }
                AERO_CHECK_MSG(sawStartPrompt, "CustomRender should start displaying 'PRESS SPACE TO START'");
                AERO_CHECK_MSG(sawZeroScore, "CustomRender should initialize bound Score TextBlock to '0'");

                const auto rgbaStart = RasterizeRgba8(win.Get(), 800u, 600u);
                AERO_CHECK_MSG(
                    CountNonClearInRect(rgbaStart, 800u, 600u, 0, 0, 800, 600) > 5000u,
                    "Breakout start screen rendered unexpectedly few pixels");

                if (mountedGame) {
                    (void)rt.input.SetFocus(mountedGame);
                    AERO_CHECK(rt.input.DispatchKeyDown(KeyCode::Space, KeyMod_None));
                    AERO_CHECK(mountedGame->GetState() == aero::samples::BreakoutGameState::Playing);
                    (void)rt.binding.FlushTrackA();

                    // Verify PRESS SPACE TO START cleared and hit a brick via TickLiveControlsInSubtree
                    bool startPromptCleared = true;
                    for (UIElement* tbElem : CollectElementsByTypeSubstr(win.Get(), "TextBlock")) {
                        if (auto* tb = tbElem->DynamicCast<TextBlock>()) {
                            if (tb->GetText() == "PRESS SPACE TO START") {
                                startPromptCleared = false;
                            }
                        }
                    }
                    AERO_CHECK(startPromptCleared);

                    const auto& brick = mountedGame->GetBrick(4, 6);
                    mountedGame->SetBallStateForTest(
                        brick.x + brick.w * 0.5f,
                        brick.y + brick.h + 5.0f,
                        0.0f,
                        -4.0f);
                    (void)TickLiveControlsInSubtree(win.Get(), 1.0f / 60.0f);
                    (void)rt.binding.FlushTrackA();
                    AERO_CHECK(mountedGame->GetScore() == 10);

                    bool sawScore10 = false;
                    for (UIElement* tbElem : CollectElementsByTypeSubstr(win.Get(), "TextBlock")) {
                        if (auto* tb = tbElem->DynamicCast<TextBlock>()) {
                            if (tb->GetText() == "10") {
                                sawScore10 = true;
                                break;
                            }
                        }
                    }
                    AERO_CHECK_MSG(sawScore10, "Score TextBlock did not update to '10' after brick break");

                    const auto rgbaPlaying = RasterizeRgba8(win.Get(), 800u, 600u);
                    AERO_CHECK(CountNonClearInRect(rgbaPlaying, 800u, 600u, 0, 0, 800, 600) > 5000u);
                    AERO_CHECK_MSG(
                        BuffersDiffer(rgbaStart, rgbaPlaying),
                        "Breakout start vs playing+brick-break frames must differ in rendered pixels");
                }
            }
        }
    }
}

void TestCustomAnimationSample(const fs::path& samplesRoot) {
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

    // End-to-end XAML compile + .axb4 round-trip + mount + Loaded & Click storyboards + pixel diff
    if (!samplesRoot.empty()) {
        ResetSharedRegistries();
        const fs::path caXaml = samplesRoot / "CustomAnimation" / "Data" / "MainPage.xaml";
        LoadFontsInDirectory(caXaml.parent_path());
        AeroCompiler compiler;
        AxbPackage caPkg;
        const bool compiled = CompileAndRoundTripXaml(
            compiler, caXaml, caXaml.parent_path(), {caXaml.parent_path()}, caPkg);
        AERO_CHECK_MSG(compiled, "CustomAnimation MainPage.xaml failed to compile / .axb4 round-trip");
        if (compiled) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, caPkg, 1000.0f, 700.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* bounceTarget = root->FindName("BounceTarget");
                UIElement* elasticTarget = root->FindName("ElasticTarget");
                UIElement* backTarget = root->FindName("BackTarget");
                UIElement* gaugeNeedle = root->FindName("GaugeNeedle");
                UIElement* dot1 = root->FindName("Dot1");
                UIElement* lockContainer = root->FindName("LockContainer");
                AERO_CHECK(bounceTarget != nullptr);
                AERO_CHECK(elasticTarget != nullptr);
                AERO_CHECK(backTarget != nullptr);
                AERO_CHECK(gaugeNeedle != nullptr);
                AERO_CHECK(dot1 != nullptr);
                AERO_CHECK(lockContainer != nullptr);

                const auto rgbaT0 = RasterizeRgba8(root.Get(), 1000u, 700u);
                AERO_CHECK(CountNonClearInRect(rgbaT0, 1000u, 700u, 0, 0, 1000, 700) > 2000u);

                // Loaded already triggered CircleTimeline on Dot1..Dot3; advance and check Dot1 orbit
                (void)rt.anim.Tick(0.25f);
                (void)rt.anim.Tick(0.25f);
                float dotX = 0.0f;
                float dotY = 0.0f;
                if (dot1) {
                    (void)value::TryGetFloat(dot1->GetValue(PropId_AnimTranslateX), dotX);
                    (void)value::TryGetFloat(dot1->GetValue(PropId_AnimTranslateY), dotY);
                    AERO_CHECK_MSG(
                        std::hypot(dotX, dotY) > 1.0f,
                        "CircleAnimation on Dot1 did not advance after Loaded + Tick");
                }

                // Trigger PLAY ALL ANIMATIONS Click EventTrigger -> starts Bounce/Elastic/Back/Expo/Shake
                Button* playBtn = nullptr;
                for (UIElement* b : CollectElementsByTypeSubstr(root.Get(), "Button")) {
                    if (auto* btn = b->DynamicCast<Button>()) {
                        playBtn = btn;
                        break;
                    }
                }
                AERO_CHECK(playBtn != nullptr);
                if (playBtn) {
                    AERO_CHECK(playBtn->PerformClick());
                    (void)rt.style.EvaluateAndApply();
                }

                float shakeMaxAbs = 0.0f;
                float backMidX = 0.0f;
                float bounceMidScale = 0.0f;
                float elasticMidAngle = 0.0f;
                float gaugeMidAngle = 0.0f;
                std::vector<std::uint8_t> rgbaMid;
                for (int step = 0; step < 6; ++step) {
                    (void)rt.anim.Tick(0.05f);
                    if (lockContainer) {
                        float sx = 0.0f;
                        if (value::TryGetFloat(lockContainer->GetValue(PropId_AnimTranslateX), sx)) {
                            shakeMaxAbs = std::max(shakeMaxAbs, std::fabs(sx));
                        }
                    }
                    if (step == 2) {
                        if (backTarget) {
                            (void)value::TryGetFloat(backTarget->GetValue(PropId_AnimTranslateX), backMidX);
                        }
                        if (bounceTarget) {
                            (void)value::TryGetFloat(bounceTarget->GetValue(PropId_AnimScaleX), bounceMidScale);
                        }
                        if (elasticTarget) {
                            (void)value::TryGetFloat(elasticTarget->GetValue(PropId_AnimRotateAngle), elasticMidAngle);
                        }
                        if (gaugeNeedle) {
                            (void)value::TryGetFloat(gaugeNeedle->GetValue(PropId_AnimRotateAngle), gaugeMidAngle);
                        }
                        rgbaMid = RasterizeRgba8(root.Get(), 1000u, 700u);
                    }
                }
                AERO_CHECK_MSG(shakeMaxAbs > 0.5f, "ShakeAnimation did not displace LockContainer");
                AERO_CHECK_MSG(backMidX < -1.0f || backMidX > 0.1f, "BackAnimation did not move BackTarget");
                AERO_CHECK_MSG(bounceMidScale > 0.01f, "BounceAnimation did not scale BounceTarget");
                AERO_CHECK_MSG(
                    !ApproxEqual(elasticMidAngle, 0.0f, 0.25f),
                    "ElasticAnimation did not rotate ElasticTarget");
                AERO_CHECK_MSG(
                    std::fabs(gaugeMidAngle) > 0.1f,
                    "ExponentialEase keyframes did not rotate GaugeNeedle");

                // Advance further and compare rendered frames
                for (int step = 0; step < 10; ++step) {
                    (void)rt.anim.Tick(0.05f);
                }
                const auto rgbaLate = RasterizeRgba8(root.Get(), 1000u, 700u);
                AERO_CHECK(BuffersDiffer(rgbaT0, rgbaMid));
                AERO_CHECK(BuffersDiffer(rgbaMid, rgbaLate));
            }
        }
    }
}

void TestVideoEffectSample(const fs::path& samplesRoot) {
    if (samplesRoot.empty()) {
        return;
    }

    // 1. Samples/VideoEffect/Data/MainPage.xaml end-to-end (MediaElement + PlayMediaAction + Tint/ChromaticAberration/DirectionalBlur/Pixelate)
    {
        ResetSharedRegistries();
        const fs::path veXaml = samplesRoot / "VideoEffect" / "Data" / "MainPage.xaml";
        LoadFontsInDirectory(veXaml.parent_path());
        AeroCompiler compiler;
        AxbPackage vePkg;
        const bool compiled = CompileAndRoundTripXaml(
            compiler, veXaml, veXaml.parent_path(), {veXaml.parent_path()}, vePkg);
        AERO_CHECK_MSG(compiled, "VideoEffect MainPage.xaml failed to compile / .axb4 round-trip");
        if (compiled) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, vePkg, 640.0f, 360.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* videoElem = root->FindName("Video");
                UIElement* videoTint = root->FindName("VideoTint");
                UIElement* textAberration = root->FindName("NoesisTextAberration");
                UIElement* textPixelate = root->FindName("NoesisTextPixelate");
                auto* media = videoElem ? videoElem->DynamicCast<MediaElement>() : nullptr;
                AERO_CHECK(media != nullptr);
                AERO_CHECK(videoTint != nullptr);
                AERO_CHECK(textAberration != nullptr);
                AERO_CHECK(textPixelate != nullptr);

                // Loaded trigger fired PlayMediaAction on Video
                if (media) {
                    AERO_CHECK_MSG(media->IsPlaying(), "PlayMediaAction on Loaded did not start MediaElement");
                }

                (void)rt.anim.Tick(0.15f);
                (void)TickLiveControlsInSubtree(root.Get(), 0.15f);
                const float pos1 = media ? media->PositionSeconds() : 0.0f;
                AERO_CHECK(pos1 > 0.0f);
                const auto rgba1 = RasterizeRgba8(root.Get(), 640u, 360u);
                AERO_CHECK(CountNonClearInRect(rgba1, 640u, 360u, 0, 0, 640, 360) > 5000u);

                // Advance to ~2.4s where ChromaticAberration AmountX, Pixelate Size, DirectionalBlur Radius & Tint animate
                for (int i = 0; i < 15; ++i) {
                    (void)rt.anim.Tick(0.15f);
                    (void)TickLiveControlsInSubtree(root.Get(), 0.15f);
                }
                const auto rgba2 = RasterizeRgba8(root.Get(), 640u, 360u);
                AERO_CHECK(CountNonClearInRect(rgba2, 640u, 360u, 0, 0, 640, 360) > 5000u);
                AERO_CHECK_MSG(
                    BuffersDiffer(rgba1, rgba2),
                    "VideoEffect rendered pixels must advance across animated effect + video frames");

                // Advance further (~8.0s total) for Tint/Loop3 and verify distinct frame
                for (int i = 0; i < 36; ++i) {
                    (void)rt.anim.Tick(0.15f);
                    (void)TickLiveControlsInSubtree(root.Get(), 0.15f);
                }
                const auto rgba3 = RasterizeRgba8(root.Get(), 640u, 360u);
                AERO_CHECK(BuffersDiffer(rgba2, rgba3));
            }
        }
    }

    // 2. SaturationEffect pixel desaturation + StopMediaAction via MediaEnded trigger
    {
        ResetSharedRegistries();
        const std::string_view effectAndMediaXaml = R"(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
      xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions">
  <StackPanel Orientation="Horizontal" Width="200" Height="100">
    <Border x:Name="FullColorBox" Width="100" Height="100" Background="#FFE62020"/>
    <Border x:Name="DesatBox" Width="100" Height="100" Background="#FFE62020">
      <Border.Effect>
        <noesis:SaturationEffect Saturation="0.0"/>
      </Border.Effect>
    </Border>
    <MediaElement x:Name="Clip" Width="1" Height="1" LoadedBehavior="Manual">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Loaded">
          <noesis:PlayMediaAction/>
        </b:EventTrigger>
        <b:EventTrigger EventName="MediaEnded">
          <noesis:StopMediaAction/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </MediaElement>
  </StackPanel>
</Page>)";
        AeroCompiler compiler;
        AxbPackage pkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineVideoEffectTest.xaml", effectAndMediaXaml, pkg);
        AERO_CHECK_MSG(ok, "Inline SaturationEffect + StopMediaAction XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, pkg, 200.0f, 100.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                const auto rgba = RasterizeRgba8(root.Get(), 200u, 100u);
                const double leftChroma = MeanChromaInRect(rgba, 200u, 100u, 10, 10, 90, 90);
                const double rightChroma = MeanChromaInRect(rgba, 200u, 100u, 110, 10, 190, 90);
                AERO_CHECK_MSG(leftChroma > 140.0, "Control box should retain strong red chroma");
                AERO_CHECK_MSG(rightChroma < 5.0, "SaturationEffect(0.0) must desaturate rendered pixels to gray");
                AERO_CHECK(CountNonClearInRect(rgba, 200u, 100u, 110, 10, 190, 90) > 1000u);

                UIElement* clipNode = root->FindName("Clip");
                auto* clip = clipNode ? clipNode->DynamicCast<MediaElement>() : nullptr;
                AERO_CHECK(clip != nullptr);
                if (clip) {
                    AERO_CHECK(clip->IsPlaying());
                    clip->SetDurationSeconds(0.5f);
                    (void)TickLiveControlsInSubtree(root.Get(), 0.75f);
                    (void)rt.style.EvaluateAndApply();
                    AERO_CHECK_MSG(!clip->IsPlaying(), "MediaEnded StopMediaAction must stop MediaElement");
                    AERO_CHECK_MSG(
                        ApproxEqual(clip->PositionSeconds(), 0.0f, 1e-3f),
                        "StopMediaAction must rewind MediaElement Position to 0");
                }
            }
        }
    }
}

void TestBrushesShadersAndPathTrim(const fs::path& samplesRoot) {
    if (samplesRoot.empty()) {
        return;
    }
    AeroCompiler compiler;

    // 1. Samples/BackgroundBlur (BackgroundEffectBehavior)
    {
        ResetSharedRegistries();
        const fs::path bbDir = samplesRoot / "BackgroundBlur" / "Data";
        LoadFontsInDirectory(bbDir);
        AxbPackage bbPkg;
        const bool ok = CompileAndRoundTripXaml(
            compiler, bbDir / "MainPage.xaml", bbDir, {bbDir}, bbPkg, {bbDir / "Resources.xaml"});
        AERO_CHECK_MSG(ok, "BackgroundBlur MainPage.xaml failed to compile / .axb4 round-trip");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, bbPkg, 800.0f, 600.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                const auto rgba = RasterizeRgba8(root.Get(), 800u, 600u);
                AERO_CHECK(CountNonClearInRect(rgba, 800u, 600u, 0, 0, 800, 600) > 20000u);
            }
        }
    }

    // 2. Samples/BrushShaders (PlasmaBrush animated Time)
    {
        ResetSharedRegistries();
        const fs::path bsDir = samplesRoot / "BrushShaders" / "Data";
        LoadFontsInDirectory(bsDir);
        AxbPackage bsPkg;
        const bool ok = CompileAndRoundTripXaml(compiler, bsDir / "MainPage.xaml", bsDir, {bsDir}, bsPkg);
        AERO_CHECK_MSG(ok, "BrushShaders MainPage.xaml failed to compile / .axb4 round-trip");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, bsPkg, 800.0f, 450.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* bd = FindNameInAnyNamescope(root.Get(), "bd");
                AERO_CHECK(bd != nullptr);
                float tEarly = -1.0f;
                float tLate = -1.0f;
                (void)rt.anim.Tick(0.5f);
                if (bd) {
                    (void)value::TryGetFloat(bd->GetValue(PropId_ShaderTime), tEarly);
                }
                (void)rt.anim.Tick(2.0f);
                if (bd) {
                    (void)value::TryGetFloat(bd->GetValue(PropId_ShaderTime), tLate);
                }
                AERO_CHECK_MSG(
                    tEarly >= 0.0f && tLate >= 0.0f && !ApproxEqual(tEarly, tLate, 0.05f),
                    "BrushShaders PlasmaBrush.Time animation did not advance PropId_ShaderTime");
                const auto rgba = RasterizeRgba8(root.Get(), 800u, 450u);
                AERO_CHECK(CountNonClearInRect(rgba, 800u, 450u, 0, 0, 800, 450) > 10000u);
            }
        }
    }

    // 3. Samples/Gallery/Data/Samples/Brushes.xaml (SaturationBrush + ConicGradientBrush + PlasmaBrush)
    {
        ResetSharedRegistries();
        const fs::path galleryDataDir = samplesRoot / "Gallery" / "Data";
        LoadFontsInDirectory(galleryDataDir);
        AxbPackage brushesPkg;
        const bool ok = CompileAndRoundTripXaml(
            compiler,
            galleryDataDir / "Samples" / "Brushes.xaml",
            galleryDataDir,
            {galleryDataDir, galleryDataDir / "Samples"},
            brushesPkg,
            {galleryDataDir / "Resources.xaml"});
        AERO_CHECK_MSG(ok, "Gallery Brushes.xaml failed to compile / .axb4 round-trip");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(
                rt, brushesPkg, 800.0f, 600.0f, "HeaderedContentControl");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                const auto rgba = RasterizeRgba8(root.Get(), 800u, 600u);
                AERO_CHECK(CountNonClearInRect(rgba, 800u, 600u, 0, 0, 800, 600) > 5000u);
            }
        }
    }

    // Focused ConicGradientBrush + SaturationBrush pixel check (multi-quadrant color + chroma reduction)
    {
        ResetSharedRegistries();
        const std::string_view shaderBrushesXaml = R"(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions">
  <StackPanel Orientation="Horizontal" Width="360" Height="120">
    <Ellipse x:Name="ConicEl" Width="120" Height="120">
      <Ellipse.Fill>
        <ImageBrush>
          <noesis:Brush.Shader>
            <noesis:ConicGradientBrush>
              <GradientStop Offset="0.0" Color="#FFFF0000"/>
              <GradientStop Offset="0.25" Color="#FF00FF00"/>
              <GradientStop Offset="0.50" Color="#FF0000FF"/>
              <GradientStop Offset="0.75" Color="#FFFFFF00"/>
              <GradientStop Offset="1.0" Color="#FFFF0000"/>
            </noesis:ConicGradientBrush>
          </noesis:Brush.Shader>
        </ImageBrush>
      </Ellipse.Fill>
    </Ellipse>
    <Rectangle x:Name="FullRedRect" Width="120" Height="120" Fill="#FFFF2020"/>
    <Rectangle x:Name="LowSatRect" Width="120" Height="120" Fill="#FFFF2020">
      <Rectangle.Fill>
        <ImageBrush>
          <noesis:Brush.Shader>
            <noesis:SaturationBrush Saturation="0.1"/>
          </noesis:Brush.Shader>
        </ImageBrush>
      </Rectangle.Fill>
    </Rectangle>
  </StackPanel>
</Page>)";
        AxbPackage sbPkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineBrushesTest.xaml", shaderBrushesXaml, sbPkg);
        AERO_CHECK_MSG(ok, "Inline ConicGradientBrush + SaturationBrush XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, sbPkg, 360.0f, 120.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                const auto rgba = RasterizeRgba8(root.Get(), 360u, 120u);
                // ConicGradientBrush: compare pixels in two different angular sectors inside [0..120, 0..120]
                auto samplePixel = [&](int x, int y) -> std::array<std::uint8_t, 4> {
                    const std::size_t off = (static_cast<std::size_t>(y) * 360u + static_cast<std::size_t>(x)) * 4u;
                    return {rgba[off], rgba[off + 1u], rgba[off + 2u], rgba[off + 3u]};
                };
                const auto pRight = samplePixel(95, 60);
                const auto pBottom = samplePixel(60, 95);
                const auto pLeft = samplePixel(25, 60);
                const auto pTop = samplePixel(60, 25);
                AERO_CHECK(pRight[3] > 200u && pBottom[3] > 200u);
                const bool conicDiffers =
                    (std::abs(static_cast<int>(pRight[0]) - static_cast<int>(pLeft[0])) > 30 ||
                     std::abs(static_cast<int>(pRight[1]) - static_cast<int>(pLeft[1])) > 30 ||
                     std::abs(static_cast<int>(pRight[2]) - static_cast<int>(pLeft[2])) > 30 ||
                     std::abs(static_cast<int>(pTop[0]) - static_cast<int>(pBottom[0])) > 30 ||
                     std::abs(static_cast<int>(pTop[1]) - static_cast<int>(pBottom[1])) > 30 ||
                     std::abs(static_cast<int>(pTop[2]) - static_cast<int>(pBottom[2])) > 30);
                AERO_CHECK_MSG(conicDiffers, "ConicGradientBrush must vary colors around angular sectors");
                AERO_CHECK(MeanChromaInRect(rgba, 360u, 120u, 130, 10, 230, 110) > 150.0);
                AERO_CHECK(MeanChromaInRect(rgba, 360u, 120u, 250, 10, 350, 110) < 35.0);
            }
        }
    }

    // 4. Samples/Gallery/Data/Samples/ProgressBar.xaml (Path.TrimStart / Path.TrimEnd radial arc)
    {
        ResetSharedRegistries();
        const fs::path galleryDataDir = samplesRoot / "Gallery" / "Data";
        const fs::path themeDataDir = samplesRoot / "Theme" / "Data";
        const fs::path darkBlueTheme = themeDataDir / "Theme" / "NoesisTheme.DarkBlue.xaml";
        LoadFontsInDirectory(galleryDataDir);
        std::vector<fs::path> pbPreload;
        if (fs::exists(darkBlueTheme)) {
            pbPreload.push_back(darkBlueTheme);
        }
        pbPreload.push_back(galleryDataDir / "Resources.xaml");
        AxbPackage pbPkg;
        const bool ok = CompileAndRoundTripXaml(
            compiler,
            galleryDataDir / "Samples" / "ProgressBar.xaml",
            galleryDataDir,
            {galleryDataDir, galleryDataDir / "Samples", themeDataDir, themeDataDir / "Theme"},
            pbPkg,
            pbPreload);
        AERO_CHECK_MSG(ok, "Gallery ProgressBar.xaml failed to compile / .axb4 round-trip");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(
                rt, pbPkg, 800.0f, 1400.0f, "HeaderedContentControl");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* progress3Node = root->FindName("progress3");
                auto* progress3 = progress3Node ? progress3Node->DynamicCast<ProgressBar>() : nullptr;
                AERO_CHECK(progress3 != nullptr);
                if (progress3) {
                    const Rect pRect = progress3->LocalToScreenBounds(progress3->FinalRect());
                    const int px0 = static_cast<int>(std::floor(pRect.x));
                    const int py0 = static_cast<int>(std::floor(pRect.y));
                    const int px1 = static_cast<int>(std::ceil(pRect.x + pRect.width));
                    const int py1 = static_cast<int>(std::ceil(pRect.y + pRect.height));

                    auto countBlueAccentInRect = [&](const std::vector<std::uint8_t>& img) noexcept -> std::size_t {
                        const int cx0 = std::max(0, px0);
                        const int cy0 = std::max(0, py0);
                        const int cx1 = std::min(800, px1);
                        const int cy1 = std::min(1400, py1);
                        std::size_t count = 0u;
                        for (int y = cy0; y < cy1; ++y) {
                            for (int x = cx0; x < cx1; ++x) {
                                const std::size_t off =
                                    (static_cast<std::size_t>(y) * 800u + static_cast<std::size_t>(x)) * 4u;
                                const int r = img[off + 0u];
                                const int b = img[off + 2u];
                                const int a = img[off + 3u];
                                if (a > 0 && b > r + 20) {
                                    ++count;
                                }
                            }
                        }
                        return count;
                    };

                    progress3->SetValue(PropId_Value, value::FromFloat(0.25f), ValueSourceRank::Local);
                    (void)rt.binding.FlushTrackA();
                    const auto rgba25 = RasterizeRgba8(root.Get(), 800u, 1400u);
                    const std::size_t arcPx25 = CountNonClearInRect(rgba25, 800u, 1400u, px0, py0, px1, py1);
                    const std::size_t accent25 = countBlueAccentInRect(rgba25);

                    progress3->SetValue(PropId_Value, value::FromFloat(0.75f), ValueSourceRank::Local);
                    (void)rt.binding.FlushTrackA();
                    const auto rgba75 = RasterizeRgba8(root.Get(), 800u, 1400u);
                    const std::size_t arcPx75 = CountNonClearInRect(rgba75, 800u, 1400u, px0, py0, px1, py1);
                    const std::size_t accent75 = countBlueAccentInRect(rgba75);

                    UIElement* progressEllipse = progress3->FindName("Progress");
                    if (progressEllipse) {
                        float trimEnd = -1.0f;
                        AERO_CHECK(
                            value::TryGetFloat(progressEllipse->GetValue(PropId_PathTrimEnd), trimEnd) &&
                            ApproxEqual(trimEnd, 0.75f, 0.05f));
                    }
                    AERO_CHECK(arcPx25 > 30u && arcPx75 >= arcPx25);
                    AERO_CHECK(accent25 > 30u);
                    AERO_CHECK_MSG(
                        accent75 > accent25 && BuffersDiffer(rgba25, rgba75),
                        "Radial ProgressBar Path.TrimEnd=0.75 must draw more arc pixels than 0.25");
                }
            }
        }
    }
}

void TestInteractiveTriggersAndBehaviors(const fs::path& samplesRoot) {
    if (samplesRoot.empty()) {
        return;
    }
    AeroCompiler compiler;

    // 1. Login/Data/MainPage.xaml: SetFocusAction, SelectAllAction, & KeyTrigger(Return, ActiveOnFocus)
    {
        ResetSharedRegistries();
        const fs::path loginDir = samplesRoot / "Login" / "Data";
        LoadFontsInDirectory(loginDir);
        AxbPackage loginPkg;
        const bool ok = CompileAndRoundTripXaml(
            compiler, loginDir / "MainPage.xaml", loginDir, {loginDir}, loginPkg, {loginDir / "Resources.xaml"});
        AERO_CHECK_MSG(ok, "Login MainPage.xaml failed to compile / .axb4 round-trip");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, loginPkg, 800.0f, 600.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                // Advance Intro animation so Dialog & UserBox/PassBox become hit-testable (Opacity > 0)
                (void)rt.anim.Tick(1.0f);
                root->Measure(Size{800.0f, 600.0f});
                root->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});

                UIElement* userNode = root->FindName("UserBox");
                UIElement* passNode = root->FindName("PassBox");
                auto* userBox = userNode ? userNode->DynamicCast<TextBox>() : nullptr;
                auto* passBox = passNode ? passNode->DynamicCast<TextBox>() : nullptr;
                AERO_CHECK(userBox != nullptr);
                AERO_CHECK(passBox != nullptr);
                if (userBox) {
                    userBox->SetText("AgentZero");
                    const Point hoverPt = ElementScreenCenter(userBox);
                    (void)rt.input.DispatchPointerMove(hoverPt);
                    (void)rt.style.EvaluateAndApply();
                    AERO_CHECK_MSG(userBox->IsFocused(), "MouseEnter SetFocusAction should focus UserBox");
                    AERO_CHECK_MSG(
                        userBox->GetSelectionLength() == 9u && userBox->HasAllSelected(),
                        "GotKeyboardFocus SelectAllAction should select all UserBox text");
                }
                if (passBox) {
                    const Point passPt = ElementScreenCenter(passBox);
                    (void)rt.input.DispatchPointerMove(passPt);
                    (void)rt.style.EvaluateAndApply();
                    AERO_CHECK(passBox->IsFocused());
                    (void)rt.input.DispatchKeyDown(KeyCode::Enter, KeyMod_None);
                    // LoginOnEnter starts WaitCredentials storyboard
                    AERO_CHECK(rt.anim.ActiveTrackCount() > 0u);
                }
            }
        }
    }

    // 2. Comprehensive KeyTrigger (Key, Modifiers, FiredOn=KeyUp, ActiveOnFocus) + TimerTrigger (Loaded vs MouseLeftButtonDown regression)
    {
        ResetSharedRegistries();
        const std::string_view triggersXaml = R"(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors">
  <StackPanel Width="400" Height="300">
    <Border x:Name="KeyScope" Width="200" Height="60" Focusable="True" Background="#FF202020">
      <b:Interaction.Triggers>
        <b:KeyTrigger Key="S" Modifiers="Ctrl" FiredOn="KeyUp" ActiveOnFocus="True">
          <b:ChangePropertyAction TargetName="KeyResult" PropertyName="Text" Value="KEY_FIRED"/>
        </b:KeyTrigger>
      </b:Interaction.Triggers>
      <TextBlock x:Name="KeyResult" Text="KEY_IDLE"/>
    </Border>
    <Border x:Name="LoadedTimerHost" Width="200" Height="60" Background="#FF202020">
      <b:Interaction.Triggers>
        <b:TimerTrigger EventName="Loaded" TotalTicks="1" MillisecondsPerTick="50">
          <b:ChangePropertyAction TargetName="LoadedTimerResult" PropertyName="Text" Value="LOADED_FIRED"/>
        </b:TimerTrigger>
      </b:Interaction.Triggers>
      <TextBlock x:Name="LoadedTimerResult" Text="IDLE"/>
    </Border>
    <Border x:Name="ClickTimerHost" Width="200" Height="60" Background="#FF303030">
      <b:Interaction.Triggers>
        <b:TimerTrigger EventName="MouseLeftButtonDown" TotalTicks="1" MillisecondsPerTick="50">
          <b:ChangePropertyAction TargetName="ClickTimerResult" PropertyName="Text" Value="CLICK_FIRED"/>
        </b:TimerTrigger>
      </b:Interaction.Triggers>
      <TextBlock x:Name="ClickTimerResult" Text="IDLE"/>
    </Border>
  </StackPanel>
</Page>)";
        AxbPackage trigPkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineTriggersTest.xaml", triggersXaml, trigPkg);
        AERO_CHECK_MSG(ok, "Inline KeyTrigger + TimerTrigger XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, trigPkg, 400.0f, 300.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* scope = root->FindName("KeyScope");
                auto* keyResult = root->FindName("KeyResult") ? root->FindName("KeyResult")->DynamicCast<TextBlock>() : nullptr;
                auto* loadedRes = root->FindName("LoadedTimerResult") ? root->FindName("LoadedTimerResult")->DynamicCast<TextBlock>() : nullptr;
                UIElement* clickHost = root->FindName("ClickTimerHost");
                auto* clickRes = root->FindName("ClickTimerResult") ? root->FindName("ClickTimerResult")->DynamicCast<TextBlock>() : nullptr;
                AERO_CHECK(scope && keyResult && loadedRes && clickHost && clickRes);
                if (scope && keyResult && loadedRes && clickHost && clickRes) {
                    // KeyTrigger must NOT fire when unfocused
                    rt.input.ClearFocus();
                    (void)rt.input.DispatchKeyDown(KeyCode::S, KeyMod_Ctrl);
                    (void)rt.input.DispatchKeyUp(KeyCode::S, KeyMod_Ctrl);
                    AERO_CHECK(keyResult->GetText() == "KEY_IDLE");

                    // Focus scope: S without Ctrl -> must NOT fire; Ctrl+S KeyDown (FiredOn=KeyUp) -> must NOT fire yet
                    (void)rt.input.SetFocus(scope);
                    (void)rt.input.DispatchKeyDown(KeyCode::S, KeyMod_None);
                    (void)rt.input.DispatchKeyUp(KeyCode::S, KeyMod_None);
                    AERO_CHECK(keyResult->GetText() == "KEY_IDLE");
                    (void)rt.input.DispatchKeyDown(KeyCode::S, KeyMod_Ctrl);
                    AERO_CHECK(keyResult->GetText() == "KEY_IDLE");
                    (void)rt.input.DispatchKeyUp(KeyCode::S, KeyMod_Ctrl);
                    AERO_CHECK_MSG(
                        keyResult->GetText() == "KEY_FIRED",
                        "KeyTrigger(Ctrl+S, FiredOn=KeyUp, ActiveOnFocus=True) did not fire on KeyUp");

                    // TimerTrigger: MouseLeftButtonDown MUST NOT fire on Loaded even after ticks, while Loaded fires after >= 50ms
                    (void)rt.style.TickTimers(0.03f);
                    AERO_CHECK(loadedRes->GetText() == "IDLE");
                    AERO_CHECK(clickRes->GetText() == "IDLE");
                    (void)rt.style.TickTimers(0.03f);
                    AERO_CHECK_MSG(loadedRes->GetText() == "LOADED_FIRED", "TimerTrigger(Loaded) did not fire after 60ms");
                    for (int i = 0; i < 5; ++i) {
                        (void)rt.style.TickTimers(0.10f);
                    }
                    AERO_CHECK_MSG(
                        clickRes->GetText() == "IDLE",
                        "Regression: TimerTrigger(MouseLeftButtonDown) must NOT fire before MouseLeftButtonDown");

                    const Point clickPt = ElementScreenCenter(clickHost);
                    (void)rt.input.DispatchPointerDown(clickPt, PointerButton::Left);
                    (void)rt.input.DispatchPointerUp(clickPt, PointerButton::Left);
                    (void)rt.style.TickTimers(0.06f);
                    AERO_CHECK_MSG(
                        clickRes->GetText() == "CLICK_FIRED",
                        "TimerTrigger(MouseLeftButtonDown) did not fire after pointer down + 60ms tick");
                }
            }
        }
    }

    // 3. MouseDragElementBehavior (ConstrainToParentBounds="True") on deterministic panel & BackgroundBlur Panel
    {
        ResetSharedRegistries();
        const std::string_view dragXaml = R"(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
      Width="200" Height="200">
  <Border x:Name="DragBox" Width="40" Height="40" HorizontalAlignment="Left" VerticalAlignment="Top" Background="#FF30C5E3">
    <b:Interaction.Behaviors>
      <b:MouseDragElementBehavior ConstrainToParentBounds="True"/>
    </b:Interaction.Behaviors>
  </Border>
</Grid>)";
        AxbPackage dragPkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineDragTest.xaml", dragXaml, dragPkg);
        AERO_CHECK_MSG(ok, "Inline MouseDragElementBehavior XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, dragPkg, 200.0f, 200.0f, "Grid");
            AERO_CHECK(root.Get() != nullptr);
            UIElement* dragBox = root ? root->FindName("DragBox") : nullptr;
            AERO_CHECK(dragBox != nullptr);
            if (dragBox) {
                (void)rt.input.DispatchPointerDown(Point{20.0f, 20.0f}, PointerButton::Left);
                (void)rt.input.DispatchPointerMove(Point{500.0f, -80.0f});
                (void)rt.input.DispatchPointerUp(Point{500.0f, -80.0f}, PointerButton::Left);
                float tx = 0.0f;
                float ty = 0.0f;
                (void)value::TryGetFloat(dragBox->GetValue(PropId_AnimTranslateX), tx);
                (void)value::TryGetFloat(dragBox->GetValue(PropId_AnimTranslateY), ty);
                AERO_CHECK_MSG(tx > 50.0f && tx <= 160.1f, "Drag X should move and clamp at parent right edge (160)");
                AERO_CHECK_MSG(ty >= -0.1f && ty <= 160.1f, "Drag Y should clamp at parent top edge (>= 0)");
            }
        }
    }

    // 4. GoToStateAction (Inventory/Data/Resources.xaml + inline state transition & pixel diff)
    {
        ResetSharedRegistries();
        const fs::path invDir = samplesRoot / "Inventory" / "Data";
        AxbPackage invResPkg;
        AERO_CHECK_MSG(
            CompileAndRoundTripXaml(compiler, invDir / "Resources.xaml", invDir, {invDir}, invResPkg),
            "Inventory Resources.xaml (GoToStateAction) failed to compile / .axb4 round-trip");

        ResetSharedRegistries();
        const std::string_view stateXaml = R"(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors">
  <Grid x:Name="StateRoot" Width="200" Height="100">
    <VisualStateManager.VisualStateGroups>
      <VisualStateGroup x:Name="HoverStates">
        <VisualState x:Name="Base"/>
        <VisualState x:Name="Hover">
          <Storyboard>
            <DoubleAnimation Storyboard.TargetName="Indicator" Storyboard.TargetProperty="Opacity" To="0.2" Duration="0:0:0"/>
          </Storyboard>
        </VisualState>
      </VisualStateGroup>
    </VisualStateManager.VisualStateGroups>
    <Border x:Name="Indicator" Width="100" Height="80" Background="#FF00E0FF" Opacity="1.0"/>
    <Button x:Name="GoBtn" Width="60" Height="30" Content="GO">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Click">
          <b:GoToStateAction StateName="Hover"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </Button>
  </Grid>
</Page>)";
        AxbPackage stPkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineGoToState.xaml", stateXaml, stPkg);
        AERO_CHECK_MSG(ok, "Inline GoToStateAction XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, stPkg, 200.0f, 100.0f, "Page");
            UIElement* indicator = root ? root->FindName("Indicator") : nullptr;
            UIElement* goBtnNode = root ? root->FindName("GoBtn") : nullptr;
            auto* goBtn = goBtnNode ? goBtnNode->DynamicCast<Button>() : nullptr;
            AERO_CHECK(indicator && goBtn);
            if (indicator && goBtn) {
                const auto rgbaBefore = RasterizeRgba8(root.Get(), 200u, 100u);
                AERO_CHECK(goBtn->PerformClick());
                (void)rt.style.EvaluateAndApply();
                (void)rt.anim.Tick(0.05f);
                float op = 1.0f;
                AERO_CHECK(value::TryGetFloat(indicator->GetValue(PropId_Opacity), op) && ApproxEqual(op, 0.2f, 0.05f));
                const auto rgbaAfter = RasterizeRgba8(root.Get(), 200u, 100u);
                AERO_CHECK(BuffersDiffer(rgbaBefore, rgbaAfter));
            }
        }
    }

    // 5. SelectAction on ListBoxItem & TreeViewItem + explicit PropId_IsChecked NOT set check (+ QuestLog end-to-end)
    {
        ResetSharedRegistries();
        const fs::path questDir = samplesRoot / "QuestLog" / "Data";
        LoadFontsInDirectory(questDir);
        AxbPackage questPkg;
        const bool questOk = CompileAndRoundTripXaml(
            compiler, questDir / "MainPage.xaml", questDir, {questDir}, questPkg, {questDir / "Resources.xaml"});
        AERO_CHECK_MSG(questOk, "QuestLog MainPage.xaml failed to compile / .axb4 round-trip");
        if (questOk) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> qRoot = MountAndPrepareSampleRoot(rt, questPkg, 800.0f, 600.0f, "Page");
            AERO_CHECK(qRoot.Get() != nullptr);
            if (qRoot) {
                (void)rt.anim.Tick(1.0f);
                AERO_CHECK(CountNonClearInRect(RasterizeRgba8(qRoot.Get(), 800u, 600u), 800u, 600u, 0, 0, 800, 600) > 5000u);
            }
        }

        ResetSharedRegistries();
        const std::string_view selectXaml = R"(
<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
      xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions">
  <StackPanel Width="400" Height="300">
    <Button x:Name="SelectRow1Btn" Width="120" Height="30" Content="SelectRow1">
      <b:Interaction.Triggers>
        <b:EventTrigger EventName="Click">
          <noesis:SelectAction TargetName="ListRow1"/>
        </b:EventTrigger>
      </b:Interaction.Triggers>
    </Button>
    <ListBox x:Name="MyList" Width="200" Height="90">
      <ListBoxItem x:Name="ListRow0" Width="200" Height="40" Content="Row 0" Background="#FF202020"/>
      <ListBoxItem x:Name="ListRow1" Width="200" Height="40" Content="Row 1" Background="#FF202020"/>
    </ListBox>
    <TreeView x:Name="MyTree" Width="200" Height="150">
      <TreeViewItem x:Name="TreeRootNode" Header="Root" IsExpanded="True">
        <TreeViewItem x:Name="TreeLeafNode" Header="Leaf" Width="200" Height="40" Background="#FF202020">
          <b:Interaction.Triggers>
            <b:EventTrigger EventName="MouseEnter">
              <noesis:SelectAction/>
            </b:EventTrigger>
          </b:Interaction.Triggers>
        </TreeViewItem>
      </TreeViewItem>
    </TreeView>
  </StackPanel>
</Page>)";
        AxbPackage selPkg;
        const bool ok = CompileAndRoundTripInlineXaml(compiler, "InlineSelectAction.xaml", selectXaml, selPkg);
        AERO_CHECK_MSG(ok, "Inline SelectAction XAML failed to compile");
        if (ok) {
            SampleRuntimeHarness rt;
            IntrusivePtr<UIElement> root = MountAndPrepareSampleRoot(rt, selPkg, 400.0f, 300.0f, "Page");
            AERO_CHECK(root.Get() != nullptr);
            if (root) {
                UIElement* selBtnNode = root->FindName("SelectRow1Btn");
                UIElement* listNode = root->FindName("MyList");
                UIElement* row1 = root->FindName("ListRow1");
                UIElement* treeLeafNode = root->FindName("TreeLeafNode");
                auto* selBtn = selBtnNode ? selBtnNode->DynamicCast<Button>() : nullptr;
                auto* myList = listNode ? listNode->DynamicCast<ListBox>() : nullptr;
                auto* treeLeaf = treeLeafNode ? treeLeafNode->DynamicCast<TreeViewItem>() : nullptr;
                AERO_CHECK(selBtn && myList && row1 && treeLeaf);
                if (selBtn && myList && row1 && treeLeaf) {
                    AERO_CHECK(selBtn->PerformClick());
                    (void)rt.style.EvaluateAndApply();
                    std::int64_t selIdx = -1;
                    AERO_CHECK(myList->GetValue(PropId_SelectedIndex).tryGetInt64(selIdx) && selIdx == 1);
                    bool tmpBool = false;
                    AERO_CHECK_MSG(
                        !myList->GetValue(PropId_IsChecked).tryGetBool(tmpBool) &&
                            myList->GetValue(PropId_IsChecked).index() == 0u,
                        "SelectAction MUST NOT set PropId_IsChecked on ListBox");
                    AERO_CHECK_MSG(
                        !row1->GetValue(PropId_IsChecked).tryGetBool(tmpBool) &&
                            row1->GetValue(PropId_IsChecked).index() == 0u,
                        "SelectAction MUST NOT set PropId_IsChecked on ListBoxItem");

                    const Point leafPt = ElementScreenCenter(treeLeaf);
                    (void)rt.input.DispatchPointerMove(leafPt);
                    (void)rt.style.EvaluateAndApply();
                    UIElement* treeNode = root->FindName("MyTree");
                    auto* myTree = treeNode ? treeNode->DynamicCast<TreeView>() : nullptr;
                    AERO_CHECK(myTree != nullptr && myTree->SelectedContainer() == treeLeaf);
                    AERO_CHECK_MSG(treeLeaf->IsSelected(), "SelectAction on TreeViewItem should select TreeLeafNode");
                    AERO_CHECK_MSG(
                        !treeNode || (!treeNode->GetValue(PropId_IsChecked).tryGetBool(tmpBool) &&
                                      treeNode->GetValue(PropId_IsChecked).index() == 0u),
                        "SelectAction MUST NOT set PropId_IsChecked on TreeView");
                }
            }
        }
    }

    // 6. Also verify Gallery/Data/Resources.xaml (TimerTrigger with TemplatedParent TabIndex) compiles & round-trips
    {
        ResetSharedRegistries();
        const fs::path galleryDataDir = samplesRoot / "Gallery" / "Data";
        AxbPackage galResPkg;
        AERO_CHECK_MSG(
            CompileAndRoundTripXaml(compiler, galleryDataDir / "Resources.xaml", galleryDataDir, {galleryDataDir}, galResPkg),
            "Gallery Resources.xaml (TimerTrigger) failed to compile / .axb4 round-trip");
    }
}

void TestSampleXamlsAndWpfQuickStart(const fs::path& samplesRoot) {
    if (samplesRoot.empty()) {
        AERO_CHECK_MSG(false, "Failed to discover Samples/ directory");
        return;
    }

    // Compile + .axb4 round-trip Integration, Theme (+ DarkBlue theme resources)
    {
        AeroCompiler compiler;
        const fs::path themeDataDir = samplesRoot / "Theme" / "Data";
        const fs::path darkBlueTheme = themeDataDir / "Theme" / "NoesisTheme.DarkBlue.xaml";

        ResetSharedRegistries();
        AxbPackage themePkg;
        std::vector<fs::path> preload;
        if (fs::exists(darkBlueTheme)) {
            preload.push_back(darkBlueTheme);
        }
        AERO_CHECK_MSG(
            CompileAndRoundTripXaml(
                compiler,
                themeDataDir / "MainWindow.xaml",
                themeDataDir,
                {themeDataDir, themeDataDir / "Theme"},
                themePkg,
                preload),
            "Theme/Data/MainWindow.xaml failed to compile / .axb4 round-trip");

        ResetSharedRegistries();
        const fs::path intXaml = samplesRoot / "Integration" / "Data" / "MainPage.xaml";
        AxbPackage intPkg;
        AERO_CHECK_MSG(
            CompileAndRoundTripXaml(compiler, intXaml, intXaml.parent_path(), {intXaml.parent_path()}, intPkg),
            "Integration/Data/MainPage.xaml failed to compile / .axb4 round-trip");
    }

    // WpfQuickStart full XamlApplication load, layout, and GPU stream pixel verification with SprintSummaryCard
    {
        ResetSharedRegistries();
        const fs::path csproj = samplesRoot / "WpfQuickStart" / "Data" / "WpfQuickStart.csproj";
        AERO_CHECK(fs::exists(csproj));
        LoadFontsInDirectory(csproj.parent_path());
        XamlApplication app;
        std::string err;
        const bool loaded = app.Load(csproj.string(), &err);
        AERO_CHECK_MSG(loaded, "WpfQuickStart XamlApplication::Load failed: " + err);
        if (loaded && app.MainWindowElement()) {
            UIElement* root = app.MainWindowElement();
            AERO_CHECK(root->FindName("HeaderBanner") != nullptr);
            AERO_CHECK(root->FindName("TasksList") != nullptr);
            AERO_CHECK(root->FindName("TasksGrid") != nullptr);

            root->Measure(Size{960.0f, 640.0f});
            root->Arrange(Rect{0.0f, 0.0f, 960.0f, 640.0f});

            UIElement* card = root->FindName("SprintSummary");
            AERO_CHECK_MSG(card != nullptr, "SprintSummaryCard should be mounted in WpfQuickStart MainWindow");
            if (card) {
                AERO_CHECK(card->FinalRect().width > 0.0f && card->FinalRect().height > 0.0f);
                const auto rgbaBefore = RasterizeRgba8(root, 960u, 640u);
                const Rect cardRect = card->LocalToScreenBounds(card->FinalRect());
                const std::size_t cardPixels = CountNonClearInRect(
                    rgbaBefore,
                    960u,
                    640u,
                    static_cast<int>(std::floor(cardRect.x)),
                    static_cast<int>(std::floor(cardRect.y)),
                    static_cast<int>(std::ceil(cardRect.x + cardRect.width)),
                    static_cast<int>(std::ceil(cardRect.y + cardRect.height)));
                AERO_CHECK_MSG(cardPixels > 500u, "SprintSummaryCard rendered unexpectedly few pixels");

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
                    root->Measure(Size{960.0f, 640.0f});
                    root->Arrange(Rect{0.0f, 0.0f, 960.0f, 640.0f});
                    const auto rgbaAfter = RasterizeRgba8(root, 960u, 640u);
                    AERO_CHECK_MSG(
                        BuffersDiffer(rgbaBefore, rgbaAfter),
                        "SprintSummaryCard click update should change rendered pixels");
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
    TestCustomAnimationSample(samplesRoot);
    TestVideoEffectSample(samplesRoot);
    TestBrushesShadersAndPathTrim(samplesRoot);
    TestInteractiveTriggersAndBehaviors(samplesRoot);
    TestSampleXamlsAndWpfQuickStart(samplesRoot);

    std::printf(
        "[samples_completeness_tests] %d/%d checks passed.\n",
        gTotalChecks - gFailedChecks,
        gTotalChecks);
    return (gFailedChecks == 0) ? 0 : 1;
}
