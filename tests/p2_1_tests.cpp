#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/Compilation.h"

namespace {

int gTotalChecks = 0;
int gPassedChecks = 0;

void CheckImpl(bool cond, const char* expr, int line, const char* msg = "") {
    ++gTotalChecks;
    if (cond) {
        ++gPassedChecks;
    } else {
        std::fprintf(
            stderr,
            "[FAIL] Line %d: %s %s\n",
            line,
            expr,
            msg ? msg : "");
        std::exit(1);
    }
}

#define AERO_CHECK(cond, msg) CheckImpl((cond), #cond, __LINE__, (msg))

constexpr const char* kRobotoTtfPath =
    "/working_dir/c_1a21a30fb41e1c84/scratch/samples/DataBinding/Data/Roboto-Regular.ttf";
constexpr const char* kAeroMaticsTtfPath =
    "/working_dir/c_1a21a30fb41e1c84/scratch/samples/HelloWorld/Data/Aero Matics Regular.ttf";

aero::DecodedImage MakeSolidImage(
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a = 255u) {
    aero::DecodedImage img{};
    img.width = width;
    img.height = height;
    img.format = aero::ImageFormat::RawRgba8;
    img.rgbaPixels.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t i = 0u; i < static_cast<std::size_t>(width) * height; ++i) {
        img.rgbaPixels[i * 4u + 0u] = r;
        img.rgbaPixels[i * 4u + 1u] = g;
        img.rgbaPixels[i * 4u + 2u] = b;
        img.rgbaPixels[i * 4u + 3u] = a;
    }
    return img;
}

struct SampledPixel {
    std::uint8_t r = 0u;
    std::uint8_t g = 0u;
    std::uint8_t b = 0u;
    std::uint8_t a = 0u;
};

SampledPixel ReadPixel(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t strideW,
    std::uint32_t x,
    std::uint32_t y) {
    const std::size_t off = (static_cast<std::size_t>(y) * strideW + x) * 4u;
    return SampledPixel{rgba[off + 0u], rgba[off + 1u], rgba[off + 2u], rgba[off + 3u]};
}

std::shared_ptr<aero::ProgramImage> CompileRealScriptSources(
    const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<aero::rs_comp::SourceFile> sources;
    sources.reserve(files.size());
    for (const auto& kv : files) {
        sources.push_back(aero::rs_comp::SourceFile{kv.first, kv.second});
    }
    aero::rs_comp::Compilation compilation(std::move(sources));
    const auto build = compilation.build();
    AERO_CHECK(!build.diagnostics.hasErrors(), "RealScript test module compiled without diagnostics");
    aero::rs_bc::Lowerer lowerer;
    std::vector<aero::rs_bc::Module> modules;
    for (const auto& mir : build.modules) {
        modules.push_back(lowerer.lower(mir));
    }
    aero::RuntimeError linkErr{};
    auto linked = aero::ProgramImage::link(std::move(modules), linkErr);
    AERO_CHECK(linked.has_value(), "ProgramImage::link succeeded");
    return std::make_shared<aero::ProgramImage>(std::move(*linked));
}

// ============================================================================
// Test 1: Multi-Instance Stack/Heap Construction & Nested ScopedUiResourceContext
// ============================================================================
void TestMultiInstanceRegistriesAndNestedThreadLocalScopes() {
    using namespace aero;

    UiResourceContext::Default().ClearAll();
    AERO_CHECK(UiResourceContext::ActiveOverride() == nullptr, "No active ScopedUiResourceContext initially");
    AERO_CHECK(&UiResourceContext::Current() == &UiResourceContext::Default(), "Current() defaults to Default()");

    // Verify public stack construction of all 4 formerly singleton registries
    AxbTemplateRegistry localTemplates;
    FontAtlas localFonts(512u);
    ImageAtlas localImages;
    GradientBrushRegistry localGradients;

    localTemplates.RegisterResource("LocalOnlyKey", value::FromString("StackRegistryVal"));
    AERO_CHECK(localTemplates.FindResource("LocalOnlyKey") != nullptr, "Stack AxbTemplateRegistry stores resource");
    AERO_CHECK(
        AxbTemplateRegistry::Instance().FindResource("LocalOnlyKey") == nullptr,
        "Stack AxbTemplateRegistry does not pollute Default() singleton");

    GradientBrushDesc localGrad{};
    localGrad.kind = GradientBrushKind::Linear;
    localGrad.stops = {{0.0f, Color{1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, Color{0.0f, 1.0f, 0.0f, 1.0f}}};
    const std::uint32_t localBrushId = localGradients.RegisterBrush(localGrad);
    AERO_CHECK(localBrushId == 1u && localGradients.BrushCount() == 1u, "Stack GradientBrushRegistry works");
    AERO_CHECK(GradientBrushRegistry::Instance().BrushCount() == 0u, "Default GradientBrushRegistry untouched");

    (void)localImages.RegisterDecodedImage("Mem/Test.png", MakeSolidImage(4u, 4u, 200u, 100u, 50u));
    AERO_CHECK(localImages.LoadedEntryCount() == 1u, "Stack ImageAtlas stores texture");
    AERO_CHECK(ImageAtlas::Instance().LoadedEntryCount() == 0u, "Default ImageAtlas untouched");
    AERO_CHECK(localFonts.AtlasWidth() == 512u, "Stack FontAtlas honors custom atlas size");

    // Verify nested RAII ScopedUiResourceContext routing on current thread
    auto ctxA = std::make_shared<UiResourceContext>();
    auto ctxB = std::make_shared<UiResourceContext>();

    {
        ScopedUiResourceContext scopeA(ctxA);
        AERO_CHECK(&UiResourceContext::Current() == ctxA.get(), "Outer scope routes to ctxA");
        AERO_CHECK(&AxbTemplateRegistry::Instance() == &ctxA->Templates(), "AxbTemplateRegistry routes to ctxA");
        AERO_CHECK(&FontAtlas::Instance() == &ctxA->Fonts(), "FontAtlas routes to ctxA");
        AERO_CHECK(&ImageAtlas::Instance() == &ctxA->Images(), "ImageAtlas routes to ctxA");
        AERO_CHECK(&GradientBrushRegistry::Instance() == &ctxA->GradientBrushes(), "GradientBrushRegistry routes to ctxA");

        AxbTemplateRegistry::Instance().RegisterResource("CtxName", value::FromString("ViewportA"));

        {
            ScopedUiResourceContext scopeB(*ctxB);
            AERO_CHECK(&UiResourceContext::Current() == ctxB.get(), "Nested scope routes to ctxB");
            AERO_CHECK(
                AxbTemplateRegistry::Instance().FindResource("CtxName") == nullptr,
                "ctxB cannot see ctxA's resource");
            AxbTemplateRegistry::Instance().RegisterResource("CtxName", value::FromString("ViewportB"));
        }

        // Restored to ctxA automatically on scopeB exit
        AERO_CHECK(&UiResourceContext::Current() == ctxA.get(), "Exiting nested scope restores ctxA");
        const UnifiedValue* valA = AxbTemplateRegistry::Instance().FindResource("CtxName");
        AERO_CHECK(
            valA != nullptr && std::get<StringValue>(*valA).view() == "ViewportA",
            "ctxA resource preserved without overwrite from ctxB");
    }

    AERO_CHECK(&UiResourceContext::Current() == &UiResourceContext::Default(), "Exiting outer scope restores Default()");
    AERO_CHECK(
        AxbTemplateRegistry::Instance().FindResource("CtxName") == nullptr,
        "Default() remained completely untouched by ctxA and ctxB");

    std::cout << "[PASS] TestMultiInstanceRegistriesAndNestedThreadLocalScopes\n";
}

// ============================================================================
// Test 2: Dual Viewport Isolation (Editor Viewport A vs Game Viewport B)
// Concurrent Compile, Same-Key Style/ControlTemplate/DynamicResource/Font/Texture,
// Live Localization Switch, Headless Rasterization & Context Teardown Release
// ============================================================================
void TestDualViewportZeroCrosstalkAndIndependentTeardown() {
    using namespace aero;

    UiResourceContext::Default().ClearAll();
    const std::size_t baselineLiveCount = UiResourceContext::LiveInstanceCount();

    auto editorCtx = std::make_shared<UiResourceContext>();
    auto gameCtx = std::make_shared<UiResourceContext>();
    std::weak_ptr<UiResourceContext> weakEditorCtx = editorCtx;
    std::weak_ptr<UiResourceContext> weakGameCtx = gameCtx;

    AERO_CHECK(
        UiResourceContext::LiveInstanceCount() == baselineLiveCount + 2u,
        "Two dedicated UiResourceContext instances alive");

    // Shared XAML markup compiled independently in Editor Viewport A vs Game Viewport B:
    // Both use the EXACT SAME resource keys:
    //   - Style key: "SharedCardStyle"
    //   - ControlTemplate key: "SharedBadgeButtonTemplate"
    //   - DynamicResource keys: "LocalizedHeader", "AccentGradient"
    //   - Font alias: "SharedViewportFont"
    //   - Texture URI: "SharedAssets/Banner.png"
    const char* sharedViewXaml = R"(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="Studio.PreviewCard"
             Width="240" Height="160">
    <StackPanel Width="240" Height="160" Spacing="0">
        <Border x:Name="AccentHeaderBar" Width="240" Height="48" Background="{DynamicResource AccentGradient}">
            <TextBlock x:Name="HeaderTitle"
                       Text="{DynamicResource LocalizedHeader}"
                       FontFamily="SharedViewportFont"
                       FontSize="20"
                       Foreground="#FFFFFFFF"/>
        </Border>
        <Image x:Name="BannerImage"
               Source="SharedAssets/Banner.png"
               Width="240"
               Height="56"
               Stretch="Fill"/>
        <Button x:Name="StyledActionBtn"
                Style="{StaticResource SharedCardStyle}"
                Template="{StaticResource SharedBadgeButtonTemplate}"
                Content="ACT"
                Width="240"
                Height="56"/>
    </StackPanel>
</UserControl>
)";

    const char* editorThemeDictXaml = R"(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <System:String xmlns:System="clr-namespace:System;assembly=mscorlib" x:Key="LocalizedHeader">Editor EN</System:String>
    <LinearGradientBrush x:Key="AccentGradient" StartPoint="0,0" EndPoint="1,0">
        <GradientStop Offset="0.0" Color="#FFE62626"/>
        <GradientStop Offset="1.0" Color="#FFE62626"/>
    </LinearGradientBrush>
    <ControlTemplate x:Key="SharedBadgeButtonTemplate" TargetType="Button">
        <Border x:Name="TplRoot" Background="#FF1E9E3E" Width="240" Height="56">
            <ContentPresenter/>
        </Border>
    </ControlTemplate>
    <Style x:Key="SharedCardStyle" TargetType="Button">
        <Setter Property="Opacity" Value="1.0"/>
        <Setter Property="Tag" Value="EditorStyleTag"/>
    </Style>
</ResourceDictionary>
)";

    const char* gameThemeDictXaml = R"(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <System:String xmlns:System="clr-namespace:System;assembly=mscorlib" x:Key="LocalizedHeader">Game EN</System:String>
    <LinearGradientBrush x:Key="AccentGradient" StartPoint="0,0" EndPoint="1,0">
        <GradientStop Offset="0.0" Color="#FF1E5AE6"/>
        <GradientStop Offset="1.0" Color="#FF1E5AE6"/>
    </LinearGradientBrush>
    <ControlTemplate x:Key="SharedBadgeButtonTemplate" TargetType="Button">
        <Border x:Name="TplRoot" Background="#FFE6A814" Width="240" Height="56">
            <ContentPresenter/>
        </Border>
    </ControlTemplate>
    <Style x:Key="SharedCardStyle" TargetType="Button">
        <Setter Property="Opacity" Value="0.85"/>
        <Setter Property="Tag" Value="GameStyleTag"/>
    </Style>
</ResourceDictionary>
)";

    const char* editorLocFrXaml = R"(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <System:String xmlns:System="clr-namespace:System;assembly=mscorlib" x:Key="LocalizedHeader">Editeur FR</System:String>
    <LinearGradientBrush x:Key="AccentGradient" StartPoint="0,0" EndPoint="1,0">
        <GradientStop Offset="0.0" Color="#FF14C850"/>
        <GradientStop Offset="1.0" Color="#FF14C850"/>
    </LinearGradientBrush>
</ResourceDictionary>
)";

    const char* gameLocFrXaml = R"(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <System:String xmlns:System="clr-namespace:System;assembly=mscorlib" x:Key="LocalizedHeader">Jeu FR</System:String>
    <LinearGradientBrush x:Key="AccentGradient" StartPoint="0,0" EndPoint="1,0">
        <GradientStop Offset="0.0" Color="#FFD228DC"/>
        <GradientStop Offset="1.0" Color="#FFD228DC"/>
    </LinearGradientBrush>
</ResourceDictionary>
)";

    // Concurrently populate & compile Editor Viewport A and Game Viewport B on two worker threads!
    std::atomic<bool> threadAOk{false};
    std::atomic<bool> threadBOk{false};

    std::thread workerA([&]() {
        ScopedUiResourceContext scope(editorCtx);
        const bool fontOk = FontAtlas::Instance().LoadTrueTypeFontFromFile(
            kRobotoTtfPath,
            "SharedViewportFont",
            true);
        // Pure crimson-cyan banner texture in Editor Viewport A: RGB(220, 30, 40)
        const auto* texEntry = ImageAtlas::Instance().RegisterDecodedImage(
            "SharedAssets/Banner.png",
            MakeSolidImage(16u, 16u, 220u, 30u, 40u));

        AeroCompiler compiler;
        auto themeRes = compiler.Compile(AeroCompileInput{"EditorTheme.xaml", editorThemeDictXaml, {}});
        auto locFrRes = compiler.Compile(AeroCompileInput{"SharedLoc_fr.xaml", editorLocFrXaml, {}});
        auto viewRes = compiler.Compile(AeroCompileInput{"PreviewCard.xaml", sharedViewXaml, {}});
        if (fontOk && texEntry != nullptr && themeRes.succeeded && locFrRes.succeeded && viewRes.succeeded) {
            (void)AxbTemplateRegistry::Instance().ApplyDynamicDictionary(themeRes.axbPackage);
            AxbTemplateRegistry::Instance().RegisterDictionary("SharedLoc_fr.xaml", locFrRes.axbPackage);
            threadAOk.store(true, std::memory_order_release);
        }
    });

    std::thread workerB([&]() {
        ScopedUiResourceContext scope(gameCtx);
        const bool fontOk = FontAtlas::Instance().LoadTrueTypeFontFromFile(
            kAeroMaticsTtfPath,
            "SharedViewportFont",
            true);
        // Pure azure banner texture in Game Viewport B: RGB(20, 180, 235)
        const auto* texEntry = ImageAtlas::Instance().RegisterDecodedImage(
            "SharedAssets/Banner.png",
            MakeSolidImage(16u, 16u, 20u, 180u, 235u));

        AeroCompiler compiler;
        auto themeRes = compiler.Compile(AeroCompileInput{"GameTheme.xaml", gameThemeDictXaml, {}});
        auto locFrRes = compiler.Compile(AeroCompileInput{"SharedLoc_fr.xaml", gameLocFrXaml, {}});
        auto viewRes = compiler.Compile(AeroCompileInput{"PreviewCard.xaml", sharedViewXaml, {}});
        if (fontOk && texEntry != nullptr && themeRes.succeeded && locFrRes.succeeded && viewRes.succeeded) {
            (void)AxbTemplateRegistry::Instance().ApplyDynamicDictionary(themeRes.axbPackage);
            AxbTemplateRegistry::Instance().RegisterDictionary("SharedLoc_fr.xaml", locFrRes.axbPackage);
            threadBOk.store(true, std::memory_order_release);
        }
    });

    workerA.join();
    workerB.join();

    AERO_CHECK(threadAOk.load() && threadBOk.load(), "Concurrent multi-context XAML+TTF+Atlas setup succeeded");

    // Verify Default() context stayed 100% clean during concurrent worker compilation
    AERO_CHECK(
        UiResourceContext::Default().Templates().Find("Studio.PreviewCard") == nullptr &&
            UiResourceContext::Default().Templates().FindStyle("SharedCardStyle") == nullptr &&
            UiResourceContext::Default().Images().LoadedEntryCount() == 0u &&
            UiResourceContext::Default().GradientBrushes().BrushCount() == 0u &&
            !UiResourceContext::Default().Fonts().HasFontFamily("SharedViewportFont"),
        "Default UiResourceContext experienced zero state pollution");

    // Verify distinct font metrics under identical alias "SharedViewportFont"
    const TextLayoutResult metricsA = editorCtx->Fonts().MeasureLayout("WWWWMMMM", "SharedViewportFont", 24.0f);
    const TextLayoutResult metricsB = gameCtx->Fonts().MeasureLayout("WWWWMMMM", "SharedViewportFont", 24.0f);
    AERO_CHECK(
        metricsA.usedTrueTypeMetrics && metricsB.usedTrueTypeMetrics &&
            std::fabs(metricsA.totalWidth - metricsB.totalWidth) > 0.5f,
        "Editor (Roboto) and Game (Aero Matics) measure distinct glyph advances under identical font alias");

    // Mount Editor Viewport A (via ExecutionDomainContext binding + DesktopEventHost)
    // and Game Viewport B (via ScopedUiResourceContext + DesktopEventHost) side-by-side!
    ExecutionDomainContext domainsA;
    ExecutionDomainContext domainsB;
    UiResourceContext::BindToDomains(domainsA, editorCtx);
    UiResourceContext::BindToDomains(domainsB, gameCtx);

    std::uint32_t wakeCountA = 0u;
    std::uint32_t wakeCountB = 0u;

    std::vector<std::uint8_t> frameEditorEn;
    std::vector<std::uint8_t> frameGameEn;
    std::vector<std::uint8_t> frameEditorFr;
    std::vector<std::uint8_t> frameGameFr;

    // Create Game Viewport B outside so it outlives Editor Viewport A teardown
    DualTrackBindingEngine bindingB(domainsB);
    DepthBucketLayoutQueue layoutB;
    PagedRenderTree renderTreeB;
    StyleTriggerEngine styleB;
    // Notice UserControl mounts WITHOUT an explicit ScopedUiResourceContext because domainsB is bound to gameCtx!
    auto rootB = MakeIntrusive<UserControl>("Studio.PreviewCard", &domainsB, &bindingB);
    AERO_CHECK(rootB->IsTemplateMounted(), "Game Viewport B mounted via domain-bound UiResourceContext");
    {
        ScopedUiResourceContext scopeB(gameCtx);
        styleB.ImportRegistryStyles();
        (void)styleB.BindSubtreeStyles(rootB.Get());
    }

    DesktopHostDriver hostB(rootB.Get(), &bindingB, &layoutB, &renderTreeB, gameCtx);
    hostB.SetStyleEngine(&styleB);
    hostB.SetViewportSize(Size{240.0f, 160.0f});
    hostB.SetWakeCallback(
        [](void* ud) noexcept { *static_cast<std::uint32_t*>(ud) += 1u; },
        &wakeCountB);

    {
        // Editor Viewport A lifecycle block (will be destroyed mid-test to prove clean release!)
        DualTrackBindingEngine bindingA(domainsA);
        DepthBucketLayoutQueue layoutA;
        PagedRenderTree renderTreeA;
        StyleTriggerEngine styleA;

        auto rootA = MakeIntrusive<UserControl>("Studio.PreviewCard", &domainsA, &bindingA);
        AERO_CHECK(rootA->IsTemplateMounted(), "Editor Viewport A mounted via domain-bound UiResourceContext");
        {
            ScopedUiResourceContext scopeA(editorCtx);
            styleA.ImportRegistryStyles();
            (void)styleA.BindSubtreeStyles(rootA.Get());
        }

        DesktopHostDriver hostA(rootA.Get(), &bindingA, &layoutA, &renderTreeA);
        hostA.SetResourceContext(editorCtx);
        hostA.SetStyleEngine(&styleA);
        hostA.SetViewportSize(Size{240.0f, 160.0f});
        hostA.SetWakeCallback(
            [](void* ud) noexcept { *static_cast<std::uint32_t*>(ud) += 1u; },
            &wakeCountA);

        // Interleave TickUi across both viewports (no manual ScopedUiResourceContext needed; HostDrivers scope automatically!)
        hostA.NotifyDirtyLayout();
        hostB.NotifyDirtyLayout();
        hostA.TickUi(0.016f, UiPhase_All);
        hostB.TickUi(0.016f, UiPhase_All);

        auto* titleA = rootA->FindName("HeaderTitle")->DynamicCast<TextBlock>();
        auto* titleB = rootB->FindName("HeaderTitle")->DynamicCast<TextBlock>();
        auto* btnA = rootA->FindName("StyledActionBtn")->DynamicCast<Button>();
        auto* btnB = rootB->FindName("StyledActionBtn")->DynamicCast<Button>();
        AERO_CHECK(titleA != nullptr && titleB != nullptr && btnA != nullptr && btnB != nullptr, "Named controls resolved");

        AERO_CHECK(titleA->GetText() == "Editor EN", "Editor Viewport A initial DynamicResource text");
        AERO_CHECK(titleB->GetText() == "Game EN", "Game Viewport B initial DynamicResource text");
        AERO_CHECK(
            std::get<StringValue>(btnA->GetValue(PropId_Tag)).view() == "EditorStyleTag" &&
                std::fabs(btnA->GetOpacity() - 1.0f) < 1e-4f,
            "Editor Viewport A applied SharedCardStyle from editorCtx");
        AERO_CHECK(
            std::get<StringValue>(btnB->GetValue(PropId_Tag)).view() == "GameStyleTag" &&
                std::fabs(btnB->GetOpacity() - 0.85f) < 1e-4f,
            "Game Viewport B applied SharedCardStyle from gameCtx");

        // Rasterize both viewports in their respective resource contexts and inspect pixels!
        {
            ScopedUiResourceContext scopeA(editorCtx);
            ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTreeA, 240u, 160u, frameEditorEn);
        }
        {
            ScopedUiResourceContext scopeB(gameCtx);
            ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTreeB, 240u, 160u, frameGameEn);
        }

        // Row y=40 (inside AccentHeaderBar [0..48), below text):
        // Editor A = red gradient (#E62626 -> R>200, B<60), Game B = blue gradient (#1E5AE6 -> B>200, R<60)
        const SampledPixel headerPxA = ReadPixel(frameEditorEn, 240u, 220u, 40u);
        const SampledPixel headerPxB = ReadPixel(frameGameEn, 240u, 220u, 40u);
        AERO_CHECK(headerPxA.r > 200u && headerPxA.b < 60u, "Editor EN header gradient rasterized crimson red");
        AERO_CHECK(headerPxB.b > 200u && headerPxB.r < 60u, "Game EN header gradient rasterized royal blue");

        // Row y=76 (inside BannerImage [48..104)):
        // Editor A = RGB(220, 30, 40), Game B = RGB(20, 180, 235)
        const SampledPixel bannerPxA = ReadPixel(frameEditorEn, 240u, 120u, 76u);
        const SampledPixel bannerPxB = ReadPixel(frameGameEn, 240u, 120u, 76u);
        AERO_CHECK(bannerPxA.r > 200u && bannerPxA.g < 60u && bannerPxA.b < 60u, "Editor BannerImage sampled red texture");
        AERO_CHECK(bannerPxB.r < 50u && bannerPxB.g > 150u && bannerPxB.b > 200u, "Game BannerImage sampled cyan-blue texture");

        // Row y=148 (inside ControlTemplate Border [104..160)):
        // Editor A ControlTemplate = green (#1E9E3E), Game B ControlTemplate = gold (#E6A814)
        const SampledPixel tplPxA = ReadPixel(frameEditorEn, 240u, 220u, 148u);
        const SampledPixel tplPxB = ReadPixel(frameGameEn, 240u, 220u, 148u);
        AERO_CHECK(tplPxA.g > 130u && tplPxA.r < 60u, "Editor ControlTemplate rasterized emerald green");
        AERO_CHECK(tplPxB.r > 180u && tplPxB.g > 120u && tplPxB.b < 60u, "Game ControlTemplate rasterized gold");

        // Switch ONLY Editor Viewport A to French dictionary ("SharedLoc_fr.xaml"):
        // Verify wake listener wakes ONLY hostA, NOT hostB, and Game Viewport B stays in English!
        const std::uint32_t wakeBeforeSwitchA = wakeCountA;
        const std::uint32_t wakeBeforeSwitchB = wakeCountB;
        const std::size_t updatedA = editorCtx->Templates().ApplyRegisteredDictionary("SharedLoc_fr.xaml");
        AERO_CHECK(updatedA == 2u, "Editor Viewport A updated 2 DynamicResource properties");
        AERO_CHECK(wakeCountA > wakeBeforeSwitchA, "Editor hostA woken by editorCtx dictionary switch");
        AERO_CHECK(wakeCountB == wakeBeforeSwitchB, "Game hostB NOT woken by editorCtx dictionary switch");
        AERO_CHECK(titleA->GetText() == "Editeur FR", "Editor Viewport A switched to French");
        AERO_CHECK(titleB->GetText() == "Game EN", "Game Viewport B unaffected by Editor language switch");

        hostA.TickUi(0.016f, UiPhase_All);
        {
            ScopedUiResourceContext scopeA(editorCtx);
            ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTreeA, 240u, 160u, frameEditorFr);
        }
        const SampledPixel headerFrPxA = ReadPixel(frameEditorFr, 240u, 220u, 40u);
        AERO_CHECK(
            headerFrPxA.g > 170u && headerFrPxA.r < 50u,
            "Editor FR header gradient dynamically switched to green without explicit scope on ApplyRegisteredDictionary");

        // Release editorCtx external shared_ptr inside block after unbinding from hostA & domainsA
        UiResourceContext::UnbindFromDomains(domainsA);
        hostA.SetResourceContext(nullptr);
        editorCtx.reset();
        AERO_CHECK(weakEditorCtx.expired(), "Editor Viewport A UiResourceContext immediately destroyed & freed");
        AERO_CHECK(
            UiResourceContext::LiveInstanceCount() == baselineLiveCount + 1u,
            "LiveInstanceCount decremented immediately upon Editor context destruction");
    }

    // After Editor Viewport A (and its UIElements + UiResourceContext) is completely destroyed,
    // verify Game Viewport B still switches language, ticks, and rasterizes with 0 dangling hooks or corruption!
    AERO_CHECK(!weakGameCtx.expired(), "Game Viewport B UiResourceContext remains alive");
    const std::size_t updatedB = gameCtx->Templates().ApplyRegisteredDictionary("SharedLoc_fr.xaml");
    AERO_CHECK(updatedB == 2u, "Game Viewport B dynamically switched to French after Editor Viewport A destruction");

    auto* titleBAfter = rootB->FindName("HeaderTitle")->DynamicCast<TextBlock>();
    AERO_CHECK(titleBAfter != nullptr && titleBAfter->GetText() == "Jeu FR", "Game Viewport B text == 'Jeu FR'");

    hostB.TickUi(0.016f, UiPhase_All);
    {
        ScopedUiResourceContext scopeB(gameCtx);
        ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTreeB, 240u, 160u, frameGameFr);
    }
    const SampledPixel headerFrPxB = ReadPixel(frameGameFr, 240u, 220u, 40u);
    const SampledPixel bannerAfterPxB = ReadPixel(frameGameFr, 240u, 120u, 76u);
    AERO_CHECK(
        headerFrPxB.r > 180u && headerFrPxB.b > 180u && headerFrPxB.g < 80u,
        "Game FR header gradient rasterized magenta");
    AERO_CHECK(
        bannerAfterPxB.r < 50u && bannerAfterPxB.g > 150u && bannerAfterPxB.b > 200u,
        "Game BannerImage texture intact after Editor context destruction");

    UiResourceContext::UnbindFromDomains(domainsB);
    hostB.SetResourceContext(nullptr);
    gameCtx.reset();
    AERO_CHECK(weakGameCtx.expired(), "Game Viewport B UiResourceContext cleanly released");
    AERO_CHECK(
        UiResourceContext::LiveInstanceCount() == baselineLiveCount,
        "All temporary UiResourceContext instances released");

    std::cout << "[PASS] TestDualViewportZeroCrosstalkAndIndependentTeardown\n";
}

// ============================================================================
// Test 3: Unified ScriptUiBridge Delegating to DualTrackBindingEngine
// (OneWay, TwoWay, ValueConverter, Multi-Hop BindPath, Shared Engine Injection)
// ============================================================================
void TestUnifiedScriptUiBridgeOnDualTrackEngine() {
    using namespace aero;

    auto program = CompileRealScriptSources({
        {"HudBridgeModel.rs",
         R"(
module Game.BridgeTest;

public class SubStats
{
    [Observable]
    public string RankLabel { get; set; }

    public SubStats(string label)
    {
        RankLabel = label;
    }
}

public class PlayerHudVm
{
    [Observable]
    public float Shield { get; set; }

    [Observable]
    public float VolumeNormalized { get; set; }

    [Observable]
    public SubStats Stats { get; set; }

    public PlayerHudVm(float shieldVal, float vol, SubStats s)
    {
        Shield = shieldVal;
        VolumeNormalized = vol;
        Stats = s;
    }
}

public void UpdatePlayerHud(PlayerHudVm vm, float nextShield, string nextRank)
{
    vm.Shield = nextShield;
    vm.Stats.RankLabel = nextRank;
}
)"}});

    ExecutionDomainContext domains;
    Interpreter uiInterp(program, domains.uiHeapPtr());

    const SymbolId subTypeId = rs_sem::stableTypeId("Game.BridgeTest::SubStats");
    const SymbolId vmTypeId = rs_sem::stableTypeId("Game.BridgeTest::PlayerHudVm");

    auto rankStr = domains.uiHeap().allocateString("Bronze");
    AERO_CHECK(rankStr.has_value(), "Allocated initial RankLabel string");
    auto subOpt = domains.uiHeap().allocateObject(subTypeId, {*rankStr}, {0u});
    AERO_CHECK(subOpt.has_value(), "Allocated SubStats on UiHeap");
    auto vmOpt = domains.uiHeap().allocateObject(
        vmTypeId,
        {MakeFloat(75.0f), MakeFloat(0.4f), *subOpt},
        {2u});
    AERO_CHECK(vmOpt.has_value(), "Allocated PlayerHudVm on UiHeap");

    const ObjectRef subRef = *subOpt;
    const ObjectRef vmRef = *vmOpt;
    auto vmRoot = domains.uiHeap().retain(vmRef);

    // Clear initial allocation dirty state
    domains.uiHeap().clearDirtyFieldMask(subRef);
    domains.uiHeap().clearDirtyFieldMask(vmRef);
    (void)domains.uiHeap().consumeDirtyObjects();

    const auto shieldSlot = domains.resolvePropertySlot(*program, vmTypeId, "Shield");
    const auto volSlot = domains.resolvePropertySlot(*program, vmTypeId, "VolumeNormalized");
    AERO_CHECK(shieldSlot.has_value() && volSlot.has_value(), "Resolved Shield and VolumeNormalized slots");

    // 1. Owned DualTrackBindingEngine mode + ValueConverter + TwoWay sync
    ScriptUiBridge bridge(domains, program.get(), &uiInterp);
    bridge.UnderlyingEngine().RegisterValueConverter(
        "Percent100Converter",
        [](const UnifiedValue& val, std::string_view) -> UnifiedValue {
            float v = 0.0f;
            return TryGetFloat(val, v) ? MakeFloat(v * 100.0f) : MakeFloat(0.0f);
        },
        [](const UnifiedValue& val, std::string_view) -> UnifiedValue {
            float v = 0.0f;
            return TryGetFloat(val, v) ? MakeFloat(v * 0.01f) : MakeFloat(0.0f);
        });

    auto shieldBar = MakeIntrusive<Border>();
    auto volumeSlider = MakeIntrusive<Slider>();
    volumeSlider->SetMinimum(0.0f);
    volumeSlider->SetMaximum(100.0f);
    auto rankText = MakeIntrusive<TextBlock>();

    RuntimeError err{};
    AERO_CHECK(
        bridge.BindObservableProperty(vmRef, *shieldSlot, shieldBar, PropId_Width, &err),
        "OneWay BindObservableProperty Shield -> Border.Width");
    AERO_CHECK(
        bridge.BindObservableProperty(
            vmRef,
            *volSlot,
            volumeSlider,
            PropId_Value,
            BindingMode::TwoWay,
            ValueConverterKind::CustomScriptOrHost,
            "Percent100Converter",
            {},
            &err),
        "TwoWay + Converter BindObservableProperty VolumeNormalized -> Slider.Value");
    AERO_CHECK(
        bridge.BindPath(vmRef, "Stats.RankLabel", rankText, PropId_Text, BindingMode::OneWay, &err),
        "Multi-hop BindPath Stats.RankLabel -> TextBlock.Text");

    // Verify all 3 bindings live directly inside UnderlyingEngine()
    AERO_CHECK(bridge.BindingCount() == 3u, "ScriptUiBridge reports 3 unified bindings");
    AERO_CHECK(
        bridge.UnderlyingEngine().ReactiveBindings().size() == 3u,
        "Underlying DualTrackBindingEngine holds all 3 reactive bindings");
    AERO_CHECK(bridge.Bindings().size() == 3u, "Compatibility Bindings() view projects 3 entries");

    // Verify initial synchronous values (including Percent100Converter: 0.4 * 100 = 40.0f)
    AERO_CHECK(std::fabs(shieldBar->GetWidth() - 75.0f) < 1e-4f, "Initial Shield Width == 75");
    AERO_CHECK(std::fabs(volumeSlider->GetSliderValue() - 40.0f) < 1e-4f, "Initial converted Slider.Value == 40");
    AERO_CHECK(rankText->GetText() == "Bronze", "Initial multi-hop RankLabel == 'Bronze'");

    // Mutate ViewModel via RealScript and flush once through ScriptUiBridge
    auto nextRankStr = domains.uiHeap().allocateString("Diamond");
    auto mutRes = uiInterp.invoke(
        "Game.BridgeTest::UpdatePlayerHud",
        {vmRef, MakeFloat(190.0f), *nextRankStr});
    AERO_CHECK(mutRes.succeeded, "Executed UpdatePlayerHud in RealScript");

    const std::size_t flushed = bridge.FlushReactiveBindings(&err);
    AERO_CHECK(flushed == 2u, "FlushReactiveBindings updated exactly the 2 mutated bindings");
    AERO_CHECK(std::fabs(shieldBar->GetWidth() - 190.0f) < 1e-4f, "Shield Width updated to 190");
    AERO_CHECK(rankText->GetText() == "Diamond", "RankLabel updated to 'Diamond'");
    AERO_CHECK(bridge.FlushReactiveBindings(&err) == 0u, "Subsequent flush on clean heap updates 0 properties");

    // Mutate TwoWay Slider on UI side via SetSliderValueInteractive (triggers NotifyTwoWayPropertyMutated -> UnderlyingEngine)
    AERO_CHECK(volumeSlider->SetSliderValueInteractive(85.0f), "Interactive slider drag to 85.0f");
    UnifiedValue syncedVol{};
    AERO_CHECK(
        domains.readPropertySlotFast(DomainKind::Ui, vmRef, *volSlot, syncedVol, &err, &uiInterp),
        "Read back VolumeNormalized from ViewModel");
    float syncedVolFloat = 0.0f;
    AERO_CHECK(
        TryGetFloat(syncedVol, syncedVolFloat) && std::fabs(syncedVolFloat - 0.85f) < 1e-4f,
        "TwoWay convertBack pushed 85.0 -> 0.85 back into ViewModel slot");

    // Also verify explicit PushTwoWayToSource facade method on ScriptUiBridge
    volumeSlider->SetSliderValue(25.0f);
    AERO_CHECK(bridge.PushTwoWayToSource(volumeSlider.Get(), PropId_Value, &err), "Explicit PushTwoWayToSource succeeded");
    AERO_CHECK(
        domains.readPropertySlotFast(DomainKind::Ui, vmRef, *volSlot, syncedVol, &err, &uiInterp) &&
            TryGetFloat(syncedVol, syncedVolFloat) && std::fabs(syncedVolFloat - 0.25f) < 1e-4f,
        "Explicit PushTwoWayToSource pushed 25.0 -> 0.25 back into ViewModel slot");

    // 2. Verify externally injected DualTrackBindingEngine sharing (0 duplicate polling)
    DualTrackBindingEngine sharedEngine(domains, program.get(), &uiInterp);
    ScriptUiBridge externalFacade(sharedEngine);
    AERO_CHECK(
        &externalFacade.UnderlyingEngine() == &sharedEngine,
        "External engine constructor binds directly to shared DualTrackBindingEngine");

    auto sharedLabel = MakeIntrusive<Border>();
    (void)domains.uiHeap().consumeDirtyObjects();
    domains.uiHeap().clearDirtyFieldMask(vmRef);
    AERO_CHECK(
        externalFacade.BindObservableProperty(vmRef, *shieldSlot, sharedLabel, PropId_Height, &err),
        "Bound Shield -> Border.Height on shared engine facade");
    AERO_CHECK(sharedEngine.ReactiveBindings().size() == 1u, "Shared DualTrackBindingEngine registered binding");
    AERO_CHECK(std::fabs(sharedLabel->GetHeight() - 190.0f) < 1e-4f, "Initial Height == 190");

    AERO_CHECK(
        domains.writePropertySlotFast(DomainKind::Ui, vmRef, *shieldSlot, MakeFloat(240.0f), &uiInterp, &err),
        "Updated Shield to 240 on UiHeap");
    AERO_CHECK(sharedEngine.FlushTrackA(&err) == 1u, "Flushing sharedEngine directly updates ScriptUiBridge binding");
    AERO_CHECK(std::fabs(sharedLabel->GetHeight() - 240.0f) < 1e-4f, "Border.Height updated to 240 via sharedEngine");

    std::cout << "[PASS] TestUnifiedScriptUiBridgeOnDualTrackEngine\n";
}

} // namespace

int main() {
    std::cout << "=== AeroGUI 2.0 P2-1 Isolation & Unified Bridge Tests ===\n";
    TestMultiInstanceRegistriesAndNestedThreadLocalScopes();
    TestDualViewportZeroCrosstalkAndIndependentTeardown();
    TestUnifiedScriptUiBridgeOnDualTrackEngine();
    std::cout << "=== ALL P2-1 TESTS PASSED (" << gPassedChecks << "/" << gTotalChecks << " checks) ===\n";
    return 0;
}
