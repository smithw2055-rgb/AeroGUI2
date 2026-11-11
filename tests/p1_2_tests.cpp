#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace {

int gTotalChecks = 0;
int gPassedChecks = 0;

void Check(bool cond, const char* expr, int line, const char* msg = "") {
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

#define AERO_CHECK(cond) Check((cond), #cond, __LINE__)
#define AERO_CHECK_MSG(cond, msg) Check((cond), #cond, __LINE__, (msg))

constexpr const char* kSamplesRoot = "/working_dir/c_1a21a30fb41e1c84/scratch/samples";
constexpr const char* kLocalizationDir = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Localization/Data";
constexpr const char* kInventoryDir = "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Inventory/Data";

const aero::UnifiedValue* FindPkgResource(const aero::AxbPackage& pkg, std::string_view key) noexcept {
    for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
        if (it->first == key) {
            return &it->second;
        }
    }
    return nullptr;
}

bool HasPkgStyle(const aero::AxbPackage& pkg, std::string_view key) noexcept {
    for (const auto& st : pkg.styles) {
        if (st.key == key) {
            return true;
        }
    }
    return false;
}

bool HasPkgDataTemplate(const aero::AxbPackage& pkg, std::string_view key) noexcept {
    for (const auto& dt : pkg.dataTemplates) {
        if (dt.key == key && dt.templatePackage != nullptr) {
            return true;
        }
    }
    return false;
}

bool HasPkgStoryboard(const aero::AxbPackage& pkg, std::string_view key) noexcept {
    for (const auto& sb : pkg.storyboards) {
        if (sb.key == key) {
            return true;
        }
    }
    return false;
}

aero::UIElement* FindByNameRecursive(aero::UIElement* root, std::string_view name) noexcept {
    if (!root) {
        return nullptr;
    }
    if (root->Name() == name) {
        return root;
    }
    for (aero::UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (aero::UIElement* hit = FindByNameRecursive(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

void CollectTextBlocksRecursive(aero::UIElement* root, std::vector<aero::TextBlock*>& out) {
    if (!root) {
        return;
    }
    if (auto* tb = root->DynamicCast<aero::TextBlock>()) {
        out.push_back(tb);
    }
    for (aero::UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        CollectTextBlocksRecursive(c, out);
    }
}

void CollectPathsRecursive(aero::UIElement* root, std::vector<aero::Path*>& out) {
    if (!root) {
        return;
    }
    if (auto* p = root->DynamicCast<aero::Path>()) {
        out.push_back(p);
    }
    for (aero::UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        CollectPathsRecursive(c, out);
    }
}

void CollectBordersRecursive(aero::UIElement* root, std::vector<aero::Border*>& out) {
    if (!root) {
        return;
    }
    if (auto* b = root->DynamicCast<aero::Border>()) {
        out.push_back(b);
    }
    for (aero::UIElement* c = root->FirstChild(); c != nullptr; c = c->NextSibling()) {
        CollectBordersRecursive(c, out);
    }
}

bool ContainsTextBlockWithText(const std::vector<aero::TextBlock*>& tbs, std::string_view expected) {
    for (const aero::TextBlock* tb : tbs) {
        if (tb && tb->GetText() == expected) {
            return true;
        }
    }
    return false;
}

bool WriteTextFileNoThrow(const std::filesystem::path& path, std::string_view content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        return false;
    }
    const std::size_t n = std::fwrite(content.data(), 1u, content.size(), fp);
    std::fclose(fp);
    return n == content.size();
}

// ============================================================================
// Test 1: Official Samples/Localization/Data/MainPage.xaml + Language-*.xaml
//         Cross-file MergedDictionaries, MTSDF Vector Icons, Styles & Primitives
// ============================================================================
void TestLocalizationSampleCompilationAndPrimitives() {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::AeroCompiler compiler;

    // 1. Verify exclusive BuiltinPropertyId allocation [96..99] & SetResourceProperty flags
    AERO_CHECK(aero::PropId_MergedDictionarySource == 96u);
    AERO_CHECK(aero::PropId_LocSource == 97u);
    AERO_CHECK(aero::PropId_FlowDirection == 98u);
    AERO_CHECK(aero::AxbResourceFlag_StaticResource == 0x00u);
    AERO_CHECK(aero::AxbResourceFlag_DynamicResource == 0x02u);
    AERO_CHECK(aero::AxbResourceFlag_Loc == 0x04u);

    // 2. Compile MainPage.xaml directly from disk (auto-merges co-located Resources.xaml via Localization.noesis)
    aero::AeroCompileInput mainIn{};
    mainIn.xamlPath = std::string(kLocalizationDir) + "/MainPage.xaml";
    const aero::AeroCompileResult mainRes = compiler.Compile(mainIn);
    AERO_CHECK_MSG(mainRes.succeeded, "Localization/Data/MainPage.xaml should compile cleanly");

    const aero::AxbPackage& mainPkg = mainRes.axbPackage;
    AERO_CHECK_MSG(!mainPkg.mergedDictionarySources.empty(), "Resources.xaml should be recorded in mergedDictionarySources");
    bool hasResourcesXamlMerged = false;
    for (const auto& src : mainPkg.mergedDictionarySources) {
        if (src.find("Resources.xaml") != std::string::npos) {
            hasResourcesXamlMerged = true;
            break;
        }
    }
    AERO_CHECK_MSG(hasResourcesXamlMerged, "mergedDictionarySources must contain Resources.xaml");

    // Verify cross-file brushes, geometries (MTSDF), styles, storyboards, and DataTemplates from Resources.xaml
    AERO_CHECK(FindPkgResource(mainPkg, "NoesisLogoBg") != nullptr);
    AERO_CHECK(FindPkgResource(mainPkg, "NoesisLogoBlueBg") != nullptr);
    AERO_CHECK(FindPkgResource(mainPkg, "IndentBorder") != nullptr);
    AERO_CHECK(FindPkgResource(mainPkg, "NoesisLogoGeometry") != nullptr);
    AERO_CHECK(FindPkgResource(mainPkg, "NoesisTextGeometry") != nullptr);
    AERO_CHECK(FindPkgResource(mainPkg, "NoesisGuiTextGeometry") != nullptr);
    AERO_CHECK_MSG(mainPkg.mtsdfPaths.size() >= 3u, "3 Noesis vector logo Geometries from Resources.xaml must be baked into MTSDF");
    AERO_CHECK(HasPkgStyle(mainPkg, "SliderStyle"));
    AERO_CHECK(HasPkgStyle(mainPkg, "FocusStyle"));
    AERO_CHECK(HasPkgStyle(mainPkg, "ColoredText"));
    AERO_CHECK(HasPkgDataTemplate(mainPkg, "LanguageItemTemplate"));
    AERO_CHECK(HasPkgStoryboard(mainPkg, "Intro"));

    // Verify SetResourceProperty flags: 0x00 StaticResource, 0x02 DynamicResource, 0x04 {noesis:Loc}
    std::uint32_t staticCount = 0u;
    std::uint32_t dynamicCount = 0u;
    std::uint32_t locCount = 0u;
    for (const auto& inst : mainPkg.instructions) {
        if (inst.opcode == aero::AxbOpcode::SetResourceProperty) {
            if (inst.flags == aero::AxbResourceFlag_StaticResource) ++staticCount;
            else if (inst.flags == aero::AxbResourceFlag_DynamicResource) ++dynamicCount;
            else if (inst.flags == aero::AxbResourceFlag_Loc) ++locCount;
        }
    }
    AERO_CHECK_MSG(staticCount >= 4u, "MainPage.xaml StaticResource instructions (0x00)");
    AERO_CHECK_MSG(dynamicCount >= 2u, "MainPage.xaml DynamicResource instructions (0x02)");
    AERO_CHECK_MSG(locCount >= 7u, "MainPage.xaml {noesis:Loc} instructions (0x04)");

    // 3. Compile Language-en.xaml, Language-fr.xaml (UTF-8 BOM), Language-jp.xaml (UTF-8 BOM), Language-ar.xaml (UTF-8 BOM)
    auto compileLang = [&](const char* fileName) -> aero::AeroCompileResult {
        aero::AeroCompileInput in{};
        in.xamlPath = std::string(kLocalizationDir) + "/" + fileName;
        return compiler.Compile(in);
    };

    const auto enRes = compileLang("Language-en.xaml");
    const auto frRes = compileLang("Language-fr.xaml");
    const auto jpRes = compileLang("Language-jp.xaml");
    const auto arRes = compileLang("Language-ar.xaml");
    AERO_CHECK(enRes.succeeded && frRes.succeeded && jpRes.succeeded && arRes.succeeded);

    auto expectStrRes = [](const aero::AxbPackage& p, std::string_view key, std::string_view expected) {
        const aero::UnifiedValue* v = FindPkgResource(p, key);
        AERO_CHECK(v != nullptr);
        const auto* sv = std::get_if<aero::StringValue>(v);
        AERO_CHECK(sv != nullptr);
        AERO_CHECK(sv->view() == expected);
    };

    expectStrRes(enRes.axbPackage, "Flag", "Flag-en.png");
    expectStrRes(enRes.axbPackage, "Flow", "LeftToRight");
    expectStrRes(enRes.axbPackage, "SelectLabel", "Select language");
    expectStrRes(enRes.axbPackage, "SoundLabel", "Sound");
    expectStrRes(enRes.axbPackage, "MusicLabel", "Music");
    expectStrRes(enRes.axbPackage, "TitleLabel", "[b]LOCALIZATION SAMPLE[/b]");

    expectStrRes(frRes.axbPackage, "Flag", "Flag-fr.png");
    expectStrRes(frRes.axbPackage, "SelectLabel", "Choisir la langue");
    expectStrRes(frRes.axbPackage, "SoundLabel", "Son");
    expectStrRes(frRes.axbPackage, "MusicLabel", "Musique");
    expectStrRes(frRes.axbPackage, "TitleLabel", "[b]EXEMPLE DE LOCALISATION[/b]");

    expectStrRes(jpRes.axbPackage, "Flag", "Flag-jp.png");
    expectStrRes(jpRes.axbPackage, "SelectLabel", "言語を選択する");
    expectStrRes(jpRes.axbPackage, "SoundLabel", "音");
    expectStrRes(jpRes.axbPackage, "MusicLabel", "音楽");
    expectStrRes(jpRes.axbPackage, "TitleLabel", "[b]局地化 見本[/b]");

    expectStrRes(arRes.axbPackage, "Flag", "Flag-ar.png");
    expectStrRes(arRes.axbPackage, "Flow", "RightToLeft");

    // 4. Test all primitive & media tags in ResourceDictionary:
    //    <sys:String>, <Color>, <SolidColorBrush>, <ImageSource>, <BitmapImage>, <sys:Double>, <sys:Int32>, <sys:Boolean>
    aero::AeroCompileInput primIn{};
    primIn.xamlPath = "PrimitiveDict.xaml";
    primIn.autoMergeGlobalResources = false;
    primIn.xamlContent = R"XAML(
<ResourceDictionary
  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
  xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <sys:String x:Key="AppTitle">AeroGUI 2.0</sys:String>
  <sys:String x:Key="EmptyStr" />
  <Color x:Key="PrimaryColor">#FF1E90FF</Color>
  <Color x:Key="ChannelColor" A="128" R="255" G="64" B="32" />
  <SolidColorBrush x:Key="PrimaryBrush" Color="{StaticResource PrimaryColor}" Opacity="0.5" />
  <SolidColorBrush x:Key="DirectBrush" Color="#FF00FF80" />
  <ImageSource x:Key="HeroBanner">Assets/Banner.png</ImageSource>
  <BitmapImage x:Key="QuestIcon" UriSource="Images/Icon01.png" />
  <BitmapImage x:Key="NestedBitmap">
    <BitmapImage.UriSource>Images/Icon02.png</BitmapImage.UriSource>
  </BitmapImage>
  <sys:Double x:Key="HeaderHeight">48.5</sys:Double>
  <sys:Int32 x:Key="MaxSlots">16</sys:Int32>
  <sys:Boolean x:Key="ShowHud">True</sys:Boolean>
</ResourceDictionary>
)XAML";
    const auto primRes = compiler.Compile(primIn);
    AERO_CHECK(primRes.succeeded);
    expectStrRes(primRes.axbPackage, "AppTitle", "AeroGUI 2.0");
    expectStrRes(primRes.axbPackage, "EmptyStr", "");
    expectStrRes(primRes.axbPackage, "HeroBanner", "Assets/Banner.png");
    expectStrRes(primRes.axbPackage, "QuestIcon", "Images/Icon01.png");
    expectStrRes(primRes.axbPackage, "NestedBitmap", "Images/Icon02.png");

    const auto* pCol = FindPkgResource(primRes.axbPackage, "PrimaryColor");
    AERO_CHECK(pCol != nullptr && aero::value::TryGetColor(*pCol).has_value());
    const auto* cCol = FindPkgResource(primRes.axbPackage, "ChannelColor");
    AERO_CHECK(cCol != nullptr && aero::value::TryGetColor(*cCol).has_value());
    const aero::Color cc = *aero::value::TryGetColor(*cCol);
    AERO_CHECK(std::fabs(cc.r - 1.0f) < 0.01f && std::fabs(cc.a - (128.0f / 255.0f)) < 0.01f);

    const auto* pBrush = FindPkgResource(primRes.axbPackage, "PrimaryBrush");
    AERO_CHECK(pBrush != nullptr && aero::value::TryGetColor(*pBrush).has_value());
    AERO_CHECK(std::fabs(aero::value::TryGetColor(*pBrush)->a - 0.5f) < 0.01f);

    float hh = 0.0f;
    AERO_CHECK(FindPkgResource(primRes.axbPackage, "HeaderHeight") != nullptr);
    AERO_CHECK(aero::TryGetFloat(*FindPkgResource(primRes.axbPackage, "HeaderHeight"), hh) && std::fabs(hh - 48.5f) < 1e-3f);
    const auto* maxSlots = FindPkgResource(primRes.axbPackage, "MaxSlots");
    AERO_CHECK(maxSlots != nullptr && std::holds_alternative<std::int64_t>(*maxSlots) && std::get<std::int64_t>(*maxSlots) == 16);
    const auto* showHud = FindPkgResource(primRes.axbPackage, "ShowHud");
    AERO_CHECK(showHud != nullptr && std::holds_alternative<bool>(*showHud) && std::get<bool>(*showHud) == true);

    std::printf("[PASS] TestLocalizationSampleCompilationAndPrimitives\n");
}

// ============================================================================
// Test 2: Runtime Hot-Switching EN -> FR -> JP -> AR on Localization MainPage
//         without visual tree rebuild + real-time TextBlock & Flag Image refresh
// ============================================================================
void TestLocalizationLiveHotSwitchEnFrJpArAndFlagImage() {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::ImageAtlas::Instance().Clear();
    aero::ImageAtlas::Instance().SetBaseDirectory(kLocalizationDir);

    aero::AeroCompiler compiler;

    // Compile language dictionaries first and start with English active
    auto compileFile = [&](const char* relFile) {
        aero::AeroCompileInput in{};
        in.xamlPath = std::string(kLocalizationDir) + "/" + relFile;
        return compiler.Compile(in);
    };
    const auto enRes = compileFile("Language-en.xaml");
    const auto frRes = compileFile("Language-fr.xaml");
    const auto jpRes = compileFile("Language-jp.xaml");
    const auto arRes = compileFile("Language-ar.xaml");
    const auto mainRes = compileFile("MainPage.xaml");
    AERO_CHECK(enRes.succeeded && frRes.succeeded && jpRes.succeeded && arRes.succeeded && mainRes.succeeded);

    // Apply English dictionary before mounting MainPage
    (void)aero::AxbTemplateRegistry::Instance().ApplyDynamicDictionary(enRes.axbPackage);

    auto pageRoot = aero::MakeIntrusive<aero::UserControl>();
    AERO_CHECK(pageRoot->MountTemplate(mainRes.axbPackage));

    // Also attach a companion <Image Source="{noesis:Loc Flag}"/> inside LayoutRoot so both
    // MainPage's <Border Background="{noesis:Loc ResourceKey=Flag}"/> AND explicit <Image>
    // control + PagedRenderTree RGBA8 texture rendering are verified live!
    aero::AeroCompileInput badgeIn{};
    badgeIn.xamlPath = "FlagBadgeOverlay.xaml";
    badgeIn.autoMergeGlobalResources = false;
    badgeIn.xamlContent = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            xmlns:noesis="clr-namespace:NoesisGUIExtensions"
            Width="96" Height="64">
  <Image x:Name="LiveFlagImage" Width="64" Height="32" Stretch="Fill" Source="{noesis:Loc Flag}" />
  <TextBlock x:Name="ShortLocLabel" Text="{Loc SelectLabel}" />
</StackPanel>
)XAML";
    const auto badgeRes = compiler.Compile(badgeIn);
    AERO_CHECK(badgeRes.succeeded);
    auto badgePanel = aero::MakeIntrusive<aero::StackPanel>();
    AERO_CHECK(aero::MountAxbTemplate(badgePanel.Get(), badgeRes.axbPackage));

    aero::UIElement* layoutRoot = FindByNameRecursive(pageRoot.Get(), "LayoutRoot");
    AERO_CHECK(layoutRoot != nullptr);
    layoutRoot->AddChild(badgePanel.Get());

    auto* liveFlagImg = FindByNameRecursive(pageRoot.Get(), "LiveFlagImage")->DynamicCast<aero::Image>();
    auto* shortLocTb = FindByNameRecursive(pageRoot.Get(), "ShortLocLabel")->DynamicCast<aero::TextBlock>();
    AERO_CHECK(liveFlagImg != nullptr && shortLocTb != nullptr);

    std::vector<aero::TextBlock*> textBlocksBefore;
    CollectTextBlocksRecursive(pageRoot.Get(), textBlocksBefore);
    AERO_CHECK(textBlocksBefore.size() >= 6u);

    std::vector<aero::Path*> logoPaths;
    CollectPathsRecursive(pageRoot.Get(), logoPaths);
    AERO_CHECK_MSG(logoPaths.size() == 3u, "MainPage Logo has 3 vector Path elements");

    // Locate the Flag Border in MainPage (bound to Background="{noesis:Loc ResourceKey=Flag}")
    std::vector<aero::Border*> allBorders;
    CollectBordersRecursive(pageRoot.Get(), allBorders);
    aero::Border* flagBorder = nullptr;
    for (aero::Border* b : allBorders) {
        const auto v = b->GetValue(aero::PropId_ImageSource);
        if (const auto* sv = std::get_if<aero::StringValue>(&v)) {
            if (sv->view() == "Flag-en.png") {
                flagBorder = b;
                break;
            }
        }
    }
    AERO_CHECK_MSG(flagBorder != nullptr, "MainPage Flag Border must receive initial Flag-en.png path");

    // Perform initial layout & render sync
    aero::DepthBucketLayoutQueue layoutQueue;
    aero::PagedRenderTree renderTree;
    const aero::Size viewport{1280.0f, 720.0f};
    layoutQueue.EnqueueDirtySubtree(pageRoot.Get());
    (void)layoutQueue.FlushLayout(viewport);
    (void)renderTree.SyncVisualTree(pageRoot.Get());

    // Verify initial English state
    AERO_CHECK(ContainsTextBlockWithText(textBlocksBefore, "Select language"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksBefore, "Sound"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksBefore, "Music"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksBefore, "[b]LOCALIZATION SAMPLE[/b]"));
    AERO_CHECK(shortLocTb->GetText() == "Select language");
    AERO_CHECK(liveFlagImg->GetSource() == "Flag-en.png");
    AERO_CHECK(std::get<aero::StringValue>(layoutRoot->GetValue(aero::PropId_FlowDirection)).view() == "LeftToRight");

    // Hot-switch EN -> FR (without rebuilding visual tree!)
    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t frUpdated = aero::AxbTemplateRegistry::Instance().ApplyDynamicDictionary(frRes.axbPackage);
    const auto t1 = std::chrono::steady_clock::now();
    const auto switchUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    AERO_CHECK_MSG(frUpdated >= 8u, "All subscribed Loc properties updated on FR switch");
    AERO_CHECK_MSG(switchUs < 5000, "Hot language switch must complete in sub-millisecond/few-ms budget");

    // Verify visual tree node pointers are 100% identical (NO visual tree rebuild!)
    std::vector<aero::TextBlock*> textBlocksAfterFr;
    CollectTextBlocksRecursive(pageRoot.Get(), textBlocksAfterFr);
    AERO_CHECK(textBlocksAfterFr == textBlocksBefore);
    AERO_CHECK(FindByNameRecursive(pageRoot.Get(), "LayoutRoot") == layoutRoot);

    // Verify live French text & Flag-fr.png path + dirty flags
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterFr, "Choisir la langue"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterFr, "Son"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterFr, "Musique"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterFr, "[b]EXEMPLE DE LOCALISATION[/b]"));
    AERO_CHECK(shortLocTb->GetText() == "Choisir la langue");
    AERO_CHECK(liveFlagImg->GetSource() == "Flag-fr.png");
    AERO_CHECK(std::get<aero::StringValue>(flagBorder->GetValue(aero::PropId_ImageSource)).view() == "Flag-fr.png");
    AERO_CHECK(std::get<aero::StringValue>(flagBorder->GetValue(aero::PropId_Background)).view() == "Flag-fr.png");
    AERO_CHECK(liveFlagImg->GetLayoutDirtyFlags() != 0u);

    layoutQueue.EnqueueDirtySubtree(pageRoot.Get());
    (void)layoutQueue.FlushLayout(viewport);
    const std::size_t frSyncedNodes = renderTree.SyncVisualTree(pageRoot.Get());
    AERO_CHECK_MSG(frSyncedNodes > 0u, "PagedRenderTree incrementally syncs updated FR text & Flag-fr.png");

    std::vector<std::uint8_t> rgbaFr;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 1280u, 720u, rgbaFr);
    AERO_CHECK(rgbaFr.size() == 1280u * 720u * 4u);

    // Hot-switch FR -> JP
    const std::size_t jpUpdated = aero::AxbTemplateRegistry::Instance().ApplyDynamicDictionary(jpRes.axbPackage);
    AERO_CHECK(jpUpdated >= 8u);

    std::vector<aero::TextBlock*> textBlocksAfterJp;
    CollectTextBlocksRecursive(pageRoot.Get(), textBlocksAfterJp);
    AERO_CHECK(textBlocksAfterJp == textBlocksBefore);
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterJp, "言語を選択する"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterJp, "音"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterJp, "音楽"));
    AERO_CHECK(ContainsTextBlockWithText(textBlocksAfterJp, "[b]局地化 見本[/b]"));
    AERO_CHECK(shortLocTb->GetText() == "言語を選択する");
    AERO_CHECK(liveFlagImg->GetSource() == "Flag-jp.png");
    AERO_CHECK(std::get<aero::StringValue>(flagBorder->GetValue(aero::PropId_ImageSource)).view() == "Flag-jp.png");

    layoutQueue.EnqueueDirtySubtree(pageRoot.Get());
    (void)layoutQueue.FlushLayout(viewport);
    const std::size_t jpSyncedNodes = renderTree.SyncVisualTree(pageRoot.Get());
    AERO_CHECK(jpSyncedNodes > 0u);

    std::vector<std::uint8_t> rgbaJp;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 1280u, 720u, rgbaJp);
    AERO_CHECK_MSG(rgbaJp != rgbaFr, "Rendered framebuffer updates when Flag-fr.png switches to Flag-jp.png");

    // Hot-switch via noesis:Loc.Source property (PropId_LocSource = 97) to Language-ar.xaml!
    pageRoot->SetValue(
        aero::PropId_LocSource,
        aero::value::FromString("Language-ar.xaml"),
        aero::ValueSourceRank::Local);
    AERO_CHECK(liveFlagImg->GetSource() == "Flag-ar.png");
    AERO_CHECK(std::get<aero::StringValue>(layoutRoot->GetValue(aero::PropId_FlowDirection)).view() == "RightToLeft");

    // Hot-switch DynamicResource theme color (NoesisLogoBlueBg) while verifying StaticResource (NoesisLogoBg) stays fixed!
    const aero::Color staticBgBefore = layoutRoot->DynamicCast<aero::Grid>()
        ? *aero::value::TryGetColor(layoutRoot->GetValue(aero::PropId_Background))
        : aero::Color{};
    const aero::Color newThemeAccent{0.95f, 0.35f, 0.15f, 1.0f};
    const std::size_t themeUpdated = aero::AxbTemplateRegistry::Instance().SetDynamicResource(
        "NoesisLogoBlueBg",
        aero::value::FromColor(newThemeAccent));
    AERO_CHECK_MSG(themeUpdated == 2u, "2 Noesis logo Path elements subscribe to DynamicResource NoesisLogoBlueBg");
    AERO_CHECK(std::fabs(logoPaths[0]->GetFill().r - 0.95f) < 1e-3f);
    AERO_CHECK(std::fabs(logoPaths[2]->GetFill().r - 0.95f) < 1e-3f);
    // Middle logo Path uses literal #FFFFFFFF and LayoutRoot uses StaticResource NoesisLogoBg -> both unchanged!
    AERO_CHECK(std::fabs(logoPaths[1]->GetFill().r - 1.0f) < 1e-3f);
    const aero::Color staticBgAfter = *aero::value::TryGetColor(layoutRoot->GetValue(aero::PropId_Background));
    AERO_CHECK(std::fabs(staticBgBefore.r - staticBgAfter.r) < 1e-4f &&
               std::fabs(staticBgBefore.b - staticBgAfter.b) < 1e-4f);

    std::printf(
        "[PASS] TestLocalizationLiveHotSwitchEnFrJpArAndFlagImage (EN->FR hot switch: %lld us)\n",
        static_cast<long long>(switchUs));
}

// ============================================================================
// Test 3: Explicit <ResourceDictionary.MergedDictionaries>, pack:// URIs,
//         ExternalXamlResolverFn, Cycle Detection, .axb4 Round-Trip & Inventory
// ============================================================================
void TestMergedDictionariesPackUriCycleAxbRoundTripAndInventory() {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::AeroCompiler compiler;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "aerogui_p1_2_unit";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    // 1. Multi-level filesystem + pack://application:,,,/ThemeAsm;component/... MergedDictionaries
    AERO_CHECK(WriteTextFileNoThrow(
        tempRoot / "ThemeAsm" / "Themes" / "BasePalette.xaml",
        R"XAML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                    xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <Color x:Key="BrandAccent">#FF1144AA</Color>
  <Color x:Key="OverrideTarget">#FF101010</Color>
  <Geometry x:Key="StarBadgeIcon">M 10,0 L 20,20 L 0,20 Z</Geometry>
  <sys:String x:Key="GreetingKey">Base Greeting</sys:String>
</ResourceDictionary>
)XAML"));

    AERO_CHECK(WriteTextFileNoThrow(
        tempRoot / "Views" / "SharedStyles.xaml",
        R"XAML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <ResourceDictionary.MergedDictionaries>
    <ResourceDictionary Source="pack://application:,,,/ThemeAsm;component/Themes/BasePalette.xaml" />
  </ResourceDictionary.MergedDictionaries>
  <SolidColorBrush x:Key="OverrideTarget" Color="#FF00CC66" />
  <Style x:Key="PrimaryBtnStyle" TargetType="Button">
    <Setter Property="Width" Value="180" />
    <Setter Property="Height" Value="42" />
  </Style>
</ResourceDictionary>
)XAML"));

    AERO_CHECK(WriteTextFileNoThrow(
        tempRoot / "Views" / "DashboardView.xaml",
        R"XAML(
<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:noesis="clr-namespace:NoesisGUIExtensions">
  <UserControl.Resources>
    <ResourceDictionary>
      <ResourceDictionary.MergedDictionaries>
        <ResourceDictionary Source="SharedStyles.xaml" />
      </ResourceDictionary.MergedDictionaries>
    </ResourceDictionary>
  </UserControl.Resources>
  <StackPanel>
    <Button x:Name="ActionBtn" Style="{StaticResource PrimaryBtnStyle}" Background="{StaticResource OverrideTarget}" />
    <Path x:Name="StarPath" Data="{StaticResource StarBadgeIcon}" Fill="{DynamicResource BrandAccent}" Width="24" Height="24" />
    <TextBlock x:Name="GreetText" Text="{noesis:Loc GreetingKey}" />
  </StackPanel>
</UserControl>
)XAML"));

    aero::AeroCompileInput dashIn{};
    dashIn.xamlPath = (tempRoot / "Views" / "DashboardView.xaml").string();
    dashIn.resourceSearchRoot = tempRoot.string();
    dashIn.emitAotCpp = true;
    const auto dashRes = compiler.Compile(dashIn);
    AERO_CHECK(dashRes.succeeded);
    AERO_CHECK_MSG(dashRes.axbPackage.mergedDictionarySources.size() == 2u, "Transitive + direct merged dictionaries recorded");
    AERO_CHECK(HasPkgStyle(dashRes.axbPackage, "PrimaryBtnStyle"));
    AERO_CHECK(!dashRes.axbPackage.mtsdfPaths.empty());

    // Verify SharedStyles.xaml overrode BasePalette.xaml's OverrideTarget (#FF00CC66 > #FF101010)
    const auto* ovVal = FindPkgResource(dashRes.axbPackage, "OverrideTarget");
    AERO_CHECK(ovVal != nullptr);
    const aero::Color ovColor = *aero::value::TryGetColor(*ovVal);
    AERO_CHECK(std::fabs(ovColor.g - (204.0f / 255.0f)) < 0.02f);

    // Verify --aot-cpp emitted SubscribeDynamicResource calls for BrandAccent (0x02) and GreetingKey (0x04)
    AERO_CHECK(dashRes.aotCpp.source.find("SubscribeDynamicResource") != std::string::npos);
    AERO_CHECK(dashRes.aotCpp.source.find("\"BrandAccent\", 2u") != std::string::npos);
    AERO_CHECK(dashRes.aotCpp.source.find("\"GreetingKey\", 4u") != std::string::npos);

    // Verify .axb4 Serialize / Deserialize round-trip preserves mergedDictionarySources & instruction flags
    aero::AxbPackage roundTripPkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(dashRes.axbBytes.data(), dashRes.axbBytes.size(), roundTripPkg));
    AERO_CHECK(roundTripPkg.mergedDictionarySources == dashRes.axbPackage.mergedDictionarySources);
    bool sawFlag2 = false;
    bool sawFlag4 = false;
    for (const auto& inst : roundTripPkg.instructions) {
        if (inst.opcode == aero::AxbOpcode::SetResourceProperty) {
            if (inst.flags == aero::AxbResourceFlag_DynamicResource) sawFlag2 = true;
            if (inst.flags == aero::AxbResourceFlag_Loc) sawFlag4 = true;
        }
    }
    AERO_CHECK(sawFlag2 && sawFlag4);

    // 2. Test ExternalXamlResolverFn in-memory VFS callback + cycle detection (A -> B -> A)
    std::unordered_map<std::string, std::string> virtualVfs{
        {"vfs://DictA.xaml", R"XAML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                    xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <ResourceDictionary.MergedDictionaries>
    <ResourceDictionary Source="vfs://DictB.xaml" />
  </ResourceDictionary.MergedDictionaries>
  <sys:String x:Key="FromA">Alpha</sys:String>
</ResourceDictionary>
)XAML"},
        {"vfs://DictB.xaml", R"XAML(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                    xmlns:sys="clr-namespace:System;assembly=mscorlib">
  <ResourceDictionary.MergedDictionaries>
    <ResourceDictionary Source="vfs://DictA.xaml" />
  </ResourceDictionary.MergedDictionaries>
  <sys:String x:Key="FromB">Beta</sys:String>
</ResourceDictionary>
)XAML"}};

    aero::AeroCompileInput cycleIn{};
    cycleIn.xamlPath = "vfs://DictA.xaml";
    cycleIn.autoMergeGlobalResources = false;
    cycleIn.externalXamlResolver = [&](std::string_view req,
                                       std::string_view /*cur*/,
                                       std::string& outPath,
                                       std::string& outXml) -> bool {
        const auto it = virtualVfs.find(std::string(req));
        if (it == virtualVfs.end()) {
            return false;
        }
        outPath = it->first;
        outXml = it->second;
        return true;
    };
    const auto cycleRes = compiler.Compile(cycleIn);
    AERO_CHECK_MSG(cycleRes.succeeded, "Cycle detection must break cycle safely without failing valid resources");
    AERO_CHECK(FindPkgResource(cycleRes.axbPackage, "FromA") != nullptr);
    AERO_CHECK(FindPkgResource(cycleRes.axbPackage, "FromB") != nullptr);
    bool sawCycleDiagnostic = false;
    for (const auto& d : cycleRes.diagnostics) {
        if (d.find("AERO_XAML_MERGED_DICT_CYCLE") != std::string::npos) {
            sawCycleDiagnostic = true;
            break;
        }
    }
    AERO_CHECK_MSG(sawCycleDiagnostic, "Circular MergedDictionaries must emit AERO_XAML_MERGED_DICT_CYCLE diagnostic");

    // 3. Compile official Samples/Inventory/Data/MainPage.xaml (auto-merging Inventory/Data/Resources.xaml)
    aero::AeroCompileInput invIn{};
    invIn.xamlPath = std::string(kInventoryDir) + "/MainPage.xaml";
    const auto invRes = compiler.Compile(invIn);
    AERO_CHECK_MSG(invRes.succeeded, "Samples/Inventory/Data/MainPage.xaml should compile and merge Resources.xaml");
    AERO_CHECK(!invRes.axbPackage.mergedDictionarySources.empty());
    AERO_CHECK(FindPkgResource(invRes.axbPackage, "Inventory.Brush.TitleBarText") != nullptr);
    AERO_CHECK(FindPkgResource(invRes.axbPackage, "Inventory.Brush.SlotBorder") != nullptr);
    AERO_CHECK(HasPkgStyle(invRes.axbPackage, "Inventory.Style.TitleBarText"));
    AERO_CHECK(HasPkgDataTemplate(invRes.axbPackage, "Inventory.Template.ListBox.Item"));
    AERO_CHECK(!invRes.axbPackage.mtsdfPaths.empty());

    std::filesystem::remove_all(tempRoot, ec);
    std::printf("[PASS] TestMergedDictionariesPackUriCycleAxbRoundTripAndInventory\n");
}

// ============================================================================
// Test 4: Weak Subscription Auto-Cleanup (Detached & Destroyed Nodes) + Host Wakeup
// ============================================================================
void TestWeakSubscriptionCleanupAndHostWakeup() {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::AeroCompiler compiler;

    aero::AeroCompileInput viewIn{};
    viewIn.xamlPath = "DynamicCleanupView.xaml";
    viewIn.autoMergeGlobalResources = false;
    viewIn.xamlContent = R"XAML(
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
            xmlns:noesis="clr-namespace:NoesisGUIExtensions">
  <TextBlock x:Name="KeptLabel" Text="{noesis:Loc LiveStatus}" />
  <TextBlock x:Name="DetachedLabel" Text="{noesis:Loc LiveStatus}" />
  <Border x:Name="ThemeBox" Background="{DynamicResource BoxAccent}" Width="80" Height="30" />
</StackPanel>
)XAML";
    const auto viewRes = compiler.Compile(viewIn);
    AERO_CHECK(viewRes.succeeded);

    int customWakeCount = 0;
    aero::AxbTemplateRegistry::Instance().SetWakeHostCallback(
        [](void* ud) noexcept {
            *static_cast<int*>(ud) += 1;
        },
        &customWakeCount);

    {
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        AERO_CHECK(aero::MountAxbTemplate(root.Get(), viewRes.axbPackage));
        AERO_CHECK(aero::AxbTemplateRegistry::Instance().ActiveDynamicSubscriptionCount() == 3u);

        auto* keptTb = FindByNameRecursive(root.Get(), "KeptLabel")->DynamicCast<aero::TextBlock>();
        auto* detachedTbRaw = FindByNameRecursive(root.Get(), "DetachedLabel")->DynamicCast<aero::TextBlock>();
        AERO_CHECK(keptTb != nullptr && detachedTbRaw != nullptr);

        // Hold an external IntrusivePtr to DetachedLabel, then detach it from the visual tree
        aero::IntrusivePtr<aero::TextBlock> heldDetached(detachedTbRaw, true);
        (void)aero::AxbTemplateRegistry::Instance().SetDynamicResource(
            "LiveStatus",
            aero::value::FromString("Online-V1"));
        AERO_CHECK(keptTb->GetText() == "Online-V1");
        AERO_CHECK(heldDetached->GetText() == "Online-V1");
        AERO_CHECK(customWakeCount == 1);

        // Detach heldDetached from root
        AERO_CHECK(root->RemoveChild(heldDetached.Get()));

        // Wire DesktopEventHost & WebCanvasHost to verify idle sleep wake-up on dynamic dictionary switch!
        aero::DepthBucketLayoutQueue layoutQ;
        aero::PagedRenderTree renderTree;
        int desktopWakeSignals = 0;
        aero::DesktopEventHost desktopHost(root.Get(), nullptr, &layoutQ, &renderTree);
        desktopHost.SetViewportSize(aero::Size{400.0f, 300.0f});
        desktopHost.SetWakeCallback([](void* ud) noexcept { *static_cast<int*>(ud) += 1; }, &desktopWakeSignals);
        desktopHost.TickUi(0.016f);
        desktopHost.TickUi(0.016f);
        AERO_CHECK_MSG(desktopHost.CanSleepIdle(), "DesktopEventHost enters 0% CPU idle sleep when clean");
        desktopWakeSignals = 0;

        int webWakeSignals = 0;
        aero::WebCanvasHost webHost(root.Get(), nullptr, &layoutQ, aero::ShaderBackend::WebGPU_WGSL, &renderTree);
        webHost.SetWakeCallback([](void* ud) noexcept { *static_cast<int*>(ud) += 1; }, &webWakeSignals);
        webHost.TickUi(0.016f);
        webHost.TickUi(0.016f);
        AERO_CHECK_MSG(webHost.RafState() == aero::WebRafState::SuspendedIdle, "WebCanvasHost suspends rAF when idle");
        webWakeSignals = 0;

        // Push V2 update -> detached node must NOT receive V2 and must be pruned, while hosts wake up!
        const std::size_t updatedV2 = aero::AxbTemplateRegistry::Instance().SetDynamicResource(
            "LiveStatus",
            aero::value::FromString("Online-V2"));
        AERO_CHECK_MSG(updatedV2 == 1u, "Only attached KeptLabel should be updated after DetachedLabel removal");
        AERO_CHECK(keptTb->GetText() == "Online-V2");
        AERO_CHECK_MSG(heldDetached->GetText() == "Online-V1", "Detached node must not receive updates");
        AERO_CHECK(aero::AxbTemplateRegistry::Instance().ActiveDynamicSubscriptionCount() == 2u);

        AERO_CHECK_MSG(!desktopHost.CanSleepIdle(), "DesktopEventHost must wake from idle sleep on DynamicResource update");
        AERO_CHECK(desktopWakeSignals == 1);
        AERO_CHECK_MSG(webHost.RafState() == aero::WebRafState::ActiveTicking, "WebCanvasHost must resume rAF on DynamicResource update");
        AERO_CHECK(webWakeSignals == 1);
        AERO_CHECK(customWakeCount == 2);
    }

    // After `root` goes out of scope and all UIElements are destroyed, 0 active subscriptions remain!
    AERO_CHECK_MSG(
        aero::AxbTemplateRegistry::Instance().ActiveDynamicSubscriptionCount() == 0u,
        "Destroyed UIElements must be automatically unsubscribed via ~UIElement hook");

    aero::AxbTemplateRegistry::Instance().SetWakeHostCallback(nullptr, nullptr);
    std::printf("[PASS] TestWeakSubscriptionCleanupAndHostWakeup\n");
}

} // namespace

int main() {
    std::printf("=== Running AeroGUI 2.0 P1-2 MergedDictionaries & DynamicResource/Loc Tests ===\n");
    TestLocalizationSampleCompilationAndPrimitives();
    TestLocalizationLiveHotSwitchEnFrJpArAndFlagImage();
    TestMergedDictionariesPackUriCycleAxbRoundTripAndInventory();
    TestWeakSubscriptionCleanupAndHostWakeup();
    std::printf("================================================================\n");
    std::printf("P1-2 Tests Summary: %d/%d checks passed (100%%).\n", gPassedChecks, gTotalChecks);
    std::printf("================================================================\n");
    return 0;
}
