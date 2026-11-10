#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace {

std::atomic<bool> gTrackAllocations{false};
std::atomic<std::size_t> gAllocationCount{0};

struct AllocationTrackingScope {
    AllocationTrackingScope() noexcept {
        gAllocationCount.store(0, std::memory_order_seq_cst);
        gTrackAllocations.store(true, std::memory_order_seq_cst);
    }
    ~AllocationTrackingScope() noexcept {
        gTrackAllocations.store(false, std::memory_order_seq_cst);
    }
    [[nodiscard]] std::size_t count() const noexcept {
        return gAllocationCount.load(std::memory_order_seq_cst);
    }
};

} // namespace

#if defined(__GNUC__) || defined(__clang__)
#define AERO_TEST_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define AERO_TEST_NOINLINE __declspec(noinline)
#else
#define AERO_TEST_NOINLINE
#endif

AERO_TEST_NOINLINE void* operator new(std::size_t size) {
    if (gTrackAllocations.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

AERO_TEST_NOINLINE void* operator new[](std::size_t size) {
    if (gTrackAllocations.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

AERO_TEST_NOINLINE void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

AERO_TEST_NOINLINE void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

#define AERO_CHECK(cond, msg)                                                      \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::tests {

// ============================================================================
// Test 1: O(1) UIElement::RenderNodeId() & Zero-Allocation SyncVisualTree Benchmark
// ============================================================================
void TestO1RenderNodeSyncAndZeroAllocDirtySubtreePrune() {
    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 128);

    // Construct a 500-card visual tree (root + 500 Border cards)
    auto root = MakeIntrusive<StackPanel>();
    root->SetWidth(800.0f);
    std::vector<IntrusivePtr<Border>> cards;
    cards.reserve(500u);
    for (std::size_t i = 0; i < 500u; ++i) {
        auto card = MakeIntrusive<Border>();
        card->SetWidth(400.0f);
        card->SetHeight(12.0f);
        card->SetCornerRadius(CornerRadius{3.0f});
        card->SetBackground(Color{0.2f, 0.3f, 0.5f, 1.0f});
        root->AddChild(card.Get());
        cards.push_back(card);
    }

    EmbeddedGameHost host(root.Get(), &bindingEngine, &layoutQueue, &renderTree);
    host.SetViewportSize(Size{1280.0f, 720.0f});

    // Initial tick lays out 501 elements and builds PagedRenderTree automatically
    host.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(renderTree.NodeCount() == 501u, "EmbeddedGameHost built 501 render nodes on initial tick");
    AERO_CHECK(cards[250]->RenderTreeId() == renderTree.TreeId() &&
                   cards[250]->RenderNodeId() == 251u &&
                   cards[250]->RenderNodeSpan() == 1u,
               "Card #250 bound O(1) RenderNodeId=251");

    const auto initStats = host.RecordGpuCommands(renderTree, gpuStream);
    AERO_CHECK(initStats.dirtyPagesUploaded == renderTree.PageCount(),
               "Initial submit uploaded all CommandPages");
    AERO_CHECK(renderTree.DirtyPageCount() == 0u, "All pages clean after submit");

    // Clean frame: 0 heap allocations, 0 dirty pages, O(1) root flag short-circuit
    {
        AllocationTrackingScope allocScope;
        host.TickUi(0.016f, UiPhase_All);
        AERO_CHECK(allocScope.count() == 0u, "0 heap allocations on clean host TickUi + SyncVisualTree");
    }
    AERO_CHECK(renderTree.DirtyPageCount() == 0u, "Clean frame touched 0 CommandPages");

    // Mutate ONLY visual color & opacity of card #250 (no layout change!) -> O(1) render sync, 0 heap allocs!
    float syncElapsedMs = 0.0f;
    std::size_t syncAllocs = 0;
    {
        cards[250]->SetOpacity(0.5f);
        AERO_CHECK((root->GetLayoutDirtyFlags() & SubtreeRenderDirty) != 0u,
                   "Card #250 SetOpacity bubbled SubtreeRenderDirty up to root");

        AllocationTrackingScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t synced = renderTree.SyncVisualTree(root.Get());
        const auto t1 = std::chrono::steady_clock::now();
        syncAllocs = allocScope.count();
        syncElapsedMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / 1000000.0f;

        AERO_CHECK(synced == 1u, "SyncVisualTree updated ONLY 1 dirty card out of 501 elements");
    }
    std::cout << "[BENCH] 500-Card Single Dirty Node SyncVisualTree: "
              << syncElapsedMs << " ms, heapAllocs=" << syncAllocs
              << ", dirtyPages=" << renderTree.DirtyPageCount() << "\n";

    AERO_CHECK(syncAllocs == 0u, "0 heap allocations during O(1) SyncVisualTree");
    AERO_CHECK(renderTree.DirtyPageCount() == 1u, "Only 1 4KB CommandPage dirtied out of 12 pages");
    AERO_CHECK((root->GetLayoutDirtyFlags() & (RenderDirty | SubtreeRenderDirty)) == 0u,
               "SyncVisualTree cleared RenderDirty and SubtreeRenderDirty flags");

    // Also verify layout-driven sibling shift marks shifted siblings RenderDirty automatically!
    (void)host.RecordGpuCommands(renderTree, gpuStream);
    cards[0]->SetHeight(42.0f); // Shifts cards[1..499] down by +30px during Arrange!
    host.TickUi(0.016f, UiPhase_All);
    const RenderNodeDescriptor* card1Node = renderTree.GetNode(cards[1]->RenderNodeId());
    AERO_CHECK(card1Node != nullptr && std::fabs(card1Node->y - 42.0f) < 0.01f,
               "Arrange shift on sibling Card #1 automatically synced new Y=42.0f to PagedRenderTree");

    std::cout << "[PASS] TestO1RenderNodeSyncAndZeroAllocDirtySubtreePrune\n";
}

// ============================================================================
// Test 2: UTF-8 + CJK SdfTextGlyph Emission & Live In-Place Text Sync
// ============================================================================
void TestUtf8AndCjkSdfTextGlyphEmissionAndLiveSync() {
    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::WebGL2, 64);

    auto root = MakeIntrusive<StackPanel>();
    root->SetWidth(480.0f);
    root->SetHeight(240.0f);

    auto headerTb = MakeIntrusive<TextBlock>();
    headerTb->SetFontSize(20.0f);
    headerTb->SetForeground(Color{1.0f, 0.9f, 0.3f, 1.0f});
    headerTb->SetText("HP: 95");
    root->AddChild(headerTb.Get());

    auto cjkBtn = MakeIntrusive<Button>();
    cjkBtn->SetWidth(180.0f);
    cjkBtn->SetHeight(40.0f);
    cjkBtn->SetContentText("立即出击");
    root->AddChild(cjkBtn.Get());

    WebCanvasHost webHost(root.Get(), &bindingEngine, &layoutQueue, ShaderBackend::WebGL2, &renderTree);
    webHost.SetViewportSize(Size{480.0f, 240.0f});
    webHost.TickUi(0.016f, UiPhase_All);

    // root (1 SolidQuad) + headerTb (1 SdfTextGlyph) + cjkBtn (1 SdfRoundedRect box + 1 SdfTextGlyph label) = 4 nodes
    AERO_CHECK(renderTree.NodeCount() == 4u,
               "StackPanel(1) + TextBlock(1 glyph run) + Button(1 box + 1 CJK glyph run) = 4 render nodes");

    const RenderNodeDescriptor* tbNode = renderTree.GetNode(headerTb->RenderNodeId());
    AERO_CHECK(tbNode != nullptr && tbNode->primType == AeroPrimType::SdfTextGlyph,
               "TextBlock emitted SdfTextGlyph primitive run");
    const CommandPage& page0 = renderTree.GetPage(tbNode->pageIndex);
    // "HP: 95" has 5 non-space glyphs ('H', 'P', ':', '9', '5') -> first 5 quads have non-zero alpha!
    std::size_t activeGlyphQuads = 0;
    for (std::size_t q = 0; q < tbNode->vertexCount / 4u; ++q) {
        const AeroVertex& v = page0.vertices[tbNode->vertexOffsetInPage + q * 4u];
        if (((v.rgba >> 24u) & 0xFFu) > 0u) {
            ++activeGlyphQuads;
        }
    }
    AERO_CHECK(activeGlyphQuads == 5u, "'HP: 95' emitted exactly 5 visible glyph quads (space advances pen)");

    // Button has span=2: box (SdfRoundedRect) + CJK label ("立即出击" = 4 wide CJK glyphs)
    AERO_CHECK(cjkBtn->RenderNodeSpan() == 2u, "Button bound 2-node span (background + label)");
    const RenderNodeDescriptor* btnLabelNode = renderTree.GetNode(cjkBtn->RenderNodeId() + 1u);
    AERO_CHECK(btnLabelNode != nullptr && btnLabelNode->primType == AeroPrimType::SdfTextGlyph,
               "Button label emitted SdfTextGlyph run for CJK text");

    (void)webHost.RecordGpuCommands(renderTree, gpuStream);

    // Update TextBlock in-place within reserved capacity ("HP: 95" -> "HP: 100") with 0 tree rebuilds
    headerTb->SetText("HP: 100");
    webHost.RequestWakeFrame();
    webHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(renderTree.NodeCount() == 4u, "Short text update reused pre-reserved glyph span in-place");

    // Grow TextBlock past initial 16-slot capacity -> seamless span expansion without full tree rebuild!
    headerTb->SetText("This long dynamic status line exceeds sixteen glyphs easily!");
    webHost.RequestWakeFrame();
    webHost.TickUi(0.016f, UiPhase_All);
    const RenderNodeDescriptor* expandedTbNode = renderTree.GetNode(headerTb->RenderNodeId());
    AERO_CHECK(expandedTbNode != nullptr && expandedTbNode->vertexCount == 168u,
               "Expanded TextBlock seamlessly allocated full 42-glyph CommandPage span");

    std::cout << "[PASS] TestUtf8AndCjkSdfTextGlyphEmissionAndLiveSync\n";
}

// ============================================================================
// Test 3: Offline 4-Channel MTSDF <Path> Baking, AOT C++ Codegen & 8x Sharp Corner Proof
// ============================================================================
void TestOfflineMtsdfPathBakingAotAndSharpCornerScaling() {
    AxbTemplateRegistry::Instance().Clear();

    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlPath = "FactionEmblemView.xaml";
    input.emitAotCpp = true;
    input.aotCppNamespace = "aero::phase4_aot";
    input.xamlContent = R"(
<UserControl x:Class="Game.UI.FactionEmblemView" Width="300" Height="300">
    <UserControl.Resources>
        <StreamGeometry x:Key="SharpSquareIcon">F1 M 4,4 L 28,4 L 28,28 L 4,28 Z</StreamGeometry>
    </UserControl.Resources>
    <StackPanel Spacing="8">
        <Path x:Name="ResourceIcon" Data="{StaticResource SharpSquareIcon}" Width="256" Height="256" Fill="#FFCC00" StrokeThickness="1.5" />
        <Path x:Name="InlineDiamond" Data="M 16,2 L 30,16 L 16,30 L 2,16 Z" Width="32" Height="32" Fill="#22AAFF" />
    </StackPanel>
</UserControl>
)";

    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled FactionEmblemView.xaml with <StreamGeometry> & <Path>");
    AERO_CHECK(res.axbPackage.mtsdfPaths.size() == 2u,
               "AeroCompiler offline-baked both <StreamGeometry> resource and inline <Path> into 2 MTSDF cells");
    AERO_CHECK(res.aotCpp.source.find("RegisterMtsdfPath") != std::string::npos &&
                   res.aotCpp.source.find("kMtsdfPixels_0") != std::string::npos,
               "AeroCompiler --aot-cpp emitted static constexpr RGBA8 MTSDF pixel arrays");

    // Verify .axb4 binary round-trip preserves MTSDF cells
    AxbPackage decoded{};
    std::string err;
    AERO_CHECK(AxbPackage::Deserialize(res.axbBytes, decoded, &err),
               ".axb4 Deserialize succeeded with MTSDF section");
    AERO_CHECK(decoded.mtsdfPaths.size() == 2u, "Deserialized both 32x32 RGBA8 MTSDF entries");

    // Verify UberShaders (GL33, WebGL2, WGSL) all implement PrimType 4 (4-channel MTSDF median3 + screenPxRange)
    for (ShaderBackend backend : {ShaderBackend::DesktopGL33, ShaderBackend::WebGL2, ShaderBackend::WebGPU_WGSL}) {
        const std::string_view ps = ZeroCopyGpuStream::UberPixelShaderSource(backend);
        AERO_CHECK(ps.find("median3") != std::string_view::npos &&
                       ps.find("screenPxRange") != std::string_view::npos,
                   "UberShader backend contains 4-channel MTSDF median3 + fwidth screenPxRange evaluation");
    }

    // Quantitative 8x Magnification Sharp-Corner Proof (32x32 cell upscaled to 256x256):
    // SharpSquareIcon occupies [4..28] x [4..28] in SVG space, mapped into 32x32 cell with pad=5 -> [5..27] in cell texels!
    // Check a sample just inside the top-left 90-degree corner where bilinear single-channel SDF rounds off!
    const AxbMtsdfPathEntry& squareEntry = decoded.mtsdfPaths[0];
    // Find exact normalized corner boundary where inside begins (~5.0 / 32.0 = 0.15625) and probe 0.35 texels inside corner
    const float cornerProbeNorm = (5.0f + 0.06f) / 32.0f;
    const float mtsdfCornerCov = EvaluateMtsdfCoverage(
        squareEntry, cornerProbeNorm, cornerProbeNorm, 256.0f, 256.0f, false);
    const float singleSdfCornerCov = EvaluateMtsdfCoverage(
        squareEntry, cornerProbeNorm, cornerProbeNorm, 256.0f, 256.0f, true);

    std::cout << "[MTSDF 8x Corner Proof] 32->256px sharp corner coverage: MTSDF="
              << mtsdfCornerCov << " vs Single-Channel SDF=" << singleSdfCornerCov << "\n";
    AERO_CHECK(mtsdfCornerCov > singleSdfCornerCov + 0.15f && mtsdfCornerCov > 0.70f,
               "3-channel median(R,G,B) MTSDF preserves sharp 90-degree corner under 8x magnification where single-channel SDF rounds off");

    // Mount, lay out, build PagedRenderTree, and verify 4-vertex MsdfVectorPath quad (0 CPU triangulation!)
    auto view = MakeIntrusive<UserControl>();
    AERO_CHECK(view->MountTemplate(decoded), "Mounted FactionEmblemView from deserialized .axb4");
    view->Measure(Size{300.0f, 300.0f});
    view->Arrange(Rect{0.0f, 0.0f, 300.0f, 300.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(view.Get());
    auto* resIconElem = view->FindName("ResourceIcon");
    AERO_CHECK(resIconElem != nullptr && resIconElem->IsA<Path>(), "ResourceIcon mounted as first-class Path");
    const RenderNodeDescriptor* pathNode = tree.GetNode(resIconElem->RenderNodeId());
    AERO_CHECK(pathNode != nullptr &&
                   pathNode->primType == AeroPrimType::MsdfVectorPath &&
                   pathNode->vertexCount == 4u,
               "Complex <Path> rendered as single 4-vertex MsdfVectorPath quad with 0 CPU triangulation");

    std::cout << "[PASS] TestOfflineMtsdfPathBakingAotAndSharpCornerScaling\n";
}

// ============================================================================
// Test 4: ClipToBounds / ScrollViewer Scissor Page Isolation & Headless Golden Image
// ============================================================================
void TestClipToBoundsScissorBatchingAndGoldenImagePixels() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Canvas Width="200" Height="200" Background="#10141C">
    <ScrollViewer x:Name="ClipBox" Canvas.Left="40" Canvas.Top="40" Width="80" Height="80" Background="#1E2634">
        <Canvas Width="200" Height="200">
            <Border x:Name="OverflowBar" Canvas.Left="10" Canvas.Top="20" Width="140" Height="30" Background="#FF3344" />
        </Canvas>
    </ScrollViewer>
</Canvas>
)";

    const AeroCompileResult res = compiler.Compile(input);
    AERO_CHECK(res.succeeded, "Compiled ScrollViewer ClipToBounds XAML");

    auto root = CreateElementByTypeName(res.axbPackage.rootTypeName);
    AERO_CHECK(MountAxbTemplate(root.Get(), res.axbPackage), "Mounted ScrollViewer XAML");
    root->Measure(Size{200.0f, 200.0f});
    root->Arrange(Rect{0.0f, 0.0f, 200.0f, 200.0f});

    // OverflowBar extends from X=50..190, but ClipBox clips X to [40..120], Y to [40..120]!
    // Verify HitTest respects ClipToBounds:
    AERO_CHECK(root->HitTest(Point{80.0f, 70.0f}) == root->FindName("OverflowBar"),
               "HitTest inside ScrollViewer clip rect (80, 70) hits OverflowBar");
    AERO_CHECK(root->HitTest(Point{150.0f, 70.0f}) == root.Get(),
               "HitTest outside ScrollViewer clip rect (150, 70) is clipped and falls back to root Canvas");

    // Verify PagedRenderTree isolates clipped children onto a scissor CommandPage and ZeroCopyGpuStream records clip batches
    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 32);
    const auto stats = gpuStream.SubmitDirtyPages(tree);
    AERO_CHECK(stats.drawCallCount == 2u && gpuStream.LastClipDrawBatches().size() == 2u,
               "Unclipped outer pages and clipped ScrollViewer inner page form 2 coalesced draw batches");
    AERO_CHECK(gpuStream.LastClipDrawBatches()[1].scissorActive &&
                   std::fabs(gpuStream.LastClipDrawBatches()[1].scissorRect.x - 40.0f) < 0.1f &&
                   std::fabs(gpuStream.LastClipDrawBatches()[1].scissorRect.width - 80.0f) < 0.1f,
               "Batch #1 carries exact [40, 40, 80, 80] Scissor rect");

    // Headless Golden Image verification: pixel (80, 70) MUST be red (#FF3344), while pixel (150, 70) MUST be root dark (#10141C)!
    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 200u, rgba);
    const std::size_t insideIdx = (70u * 200u + 80u) * 4u;
    const std::size_t clippedOutIdx = (70u * 200u + 150u) * 4u;
    AERO_CHECK(rgba[insideIdx + 0u] > 230u && rgba[insideIdx + 1u] < 70u,
               "Golden Image pixel (80, 70) inside clip rect rendered bright red OverflowBar");
    AERO_CHECK(rgba[clippedOutIdx + 0u] < 30u && rgba[clippedOutIdx + 2u] < 40u,
               "Golden Image pixel (150, 70) outside clip rect strictly clipped OverflowBar");

    std::cout << "[PASS] TestClipToBoundsScissorBatchingAndGoldenImagePixels\n";
}


// ============================================================================
// Test 5: Cross-Dictionary <Geometry> Resource + Implicit-L SVG Polygon/Bezier End-to-End
// ============================================================================
void TestCrossDictionaryGeometryResourceMtsdfEndToEnd() {
    AxbTemplateRegistry::Instance().Clear();
    AeroCompiler compiler;

    // 1. Compile standalone ResourceDictionary (modeled after Buttons/Data/Resources.xaml)
    AeroCompileInput dictInput{};
    dictInput.xamlPath = "Resources.xaml";
    dictInput.xamlContent = R"(
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <Geometry x:Key="LogoChevronGeometry">M64.5,61.4 L90.5,64.6 65.9,97.2 96.8,170.2 151.3,176.4 176.6,144.7 186.3,167.7 161.6,200.6 80.8,191.6 39.8,94.9 Z</Geometry>
    <SolidColorBrush x:Key="BrandAccentBrush" Color="#1E90FF" />
</ResourceDictionary>
)";
    const AeroCompileResult dictRes = compiler.Compile(dictInput);
    AERO_CHECK(dictRes.succeeded && dictRes.axbPackage.mtsdfPaths.size() == 1u,
               "Standalone ResourceDictionary baked <Geometry> with implicit L continuations into MTSDF");

    // 2. Compile MainWindow referencing {StaticResource LogoChevronGeometry} across dictionaries
    AeroCompileInput viewInput{};
    viewInput.xamlPath = "MainWindow.xaml";
    viewInput.xamlContent = R"(
<Grid Width="240" Height="180" Background="#0F1722">
    <StackPanel Spacing="6" Padding="12">
        <Path x:Name="BrandLogo" Data="{StaticResource LogoChevronGeometry}" Width="96" Height="96" Fill="{StaticResource BrandAccentBrush}" />
        <TextBlock x:Name="Caption" Text="AeroGUI Phase 4 Ready" FontSize="16" Foreground="#FFFFFF" />
    </StackPanel>
</Grid>
)";
    const AeroCompileResult viewRes = compiler.Compile(viewInput);
    AERO_CHECK(viewRes.succeeded, "Compiled MainWindow referencing cross-dictionary Geometry resource");

    // Simulate clean runtime startup: clear registry, load ONLY .axb4 binary packages
    AxbTemplateRegistry::Instance().Clear();
    AxbPackage runtimeDictPkg{};
    AxbPackage runtimeViewPkg{};
    AERO_CHECK(AxbPackage::Deserialize(dictRes.axbBytes, runtimeDictPkg), "Runtime decoded Resources.axb4");
    AERO_CHECK(AxbPackage::Deserialize(viewRes.axbBytes, runtimeViewPkg), "Runtime decoded MainWindow.axb4");
    AxbTemplateRegistry::Instance().RegisterResources(runtimeDictPkg);

    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 32);

    auto root = CreateElementByTypeName(runtimeViewPkg.rootTypeName);
    AERO_CHECK(MountAxbTemplate(root.Get(), runtimeViewPkg, &domains, &bindingEngine),
               "Runtime mounted MainWindow.axb4 against Resources.axb4");

    EmbeddedGameHost host(root.Get(), &bindingEngine, &layoutQueue, &renderTree);
    host.SetViewportSize(Size{240.0f, 180.0f});
    host.TickUi(0.016f, UiPhase_All);

    auto* logoElem = root->FindName("BrandLogo");
    AERO_CHECK(logoElem != nullptr && logoElem->IsA<Path>(), "BrandLogo mounted as Path");
    const RenderNodeDescriptor* logoNode = renderTree.GetNode(logoElem->RenderNodeId());
    AERO_CHECK(logoNode != nullptr &&
                   logoNode->primType == AeroPrimType::MsdfVectorPath &&
                   logoNode->mtsdfPathHash == runtimeDictPkg.mtsdfPaths[0].pathHash,
               "BrandLogo bound to pre-baked cross-dictionary MTSDF cell hash");

    const auto gpuStats = host.RecordGpuCommands(renderTree, gpuStream);
    AERO_CHECK(gpuStats.drawCallCount == 1u, "Entire view (Grid + StackPanel + MTSDF Path + UTF-8 TextBlock) rendered in 1 draw call");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 240u, 180u, rgba);
    std::size_t nonBgBluePixels = 0;
    for (std::size_t i = 0; i < 240u * 180u; ++i) {
        if (rgba[i * 4u + 2u] > 180u && rgba[i * 4u + 0u] < 80u) {
            ++nonBgBluePixels;
        }
    }
    AERO_CHECK(nonBgBluePixels > 100u, "Golden Image rendered DodgerBlue (#1E90FF) MTSDF BrandLogo pixels");
    std::cout << "[PASS] TestCrossDictionaryGeometryResourceMtsdfEndToEnd\n";
}

// ============================================================================
// Test 6: Comprehensive Phase 4 Review Bugfix Regressions
// ============================================================================
void TestPhase4ReviewBugfixRegressions() {
    AxbTemplateRegistry::Instance().Clear();

    // 1. SVG hash delimiter preservation & compact arc / smooth T / open polyline / single-corner teardrop
    AERO_CHECK(ComputeSvgPathHash("M 1 23 L 4 5") != ComputeSvgPathHash("M 12 3 L 4 5"),
               "ComputeSvgPathHash preserves token-separating whitespace");
    AERO_CHECK(ComputeSvgPathHash("  M 4,4  \n  L 28,4 ") == ComputeSvgPathHash("M 4,4 L 28,4"),
               "ComputeSvgPathHash normalizes redundant whitespace");

    // Compact SVG arc flags without spaces ("a10,10 0 10 20,0" + "a10,10 0 10 -20,0") -> full circle of radius 10
    const Rect circleBounds = MeasureSvgPathBounds("M 6,16 a10,10 0 1020,0 a10,10 0 10-20,0 Z");
    AERO_CHECK(circleBounds.width >= 18.0f && circleBounds.height >= 18.0f,
               "Compact SVG arc flags ('1020,0') parsed accurately into full circle bounds instead of zero-height line");

    // Smooth quadratic Bezier 'T' command
    const Rect smoothQuadBounds = MeasureSvgPathBounds("M 2,16 Q 10,2 16,16 T 30,16 Z");
    AERO_CHECK(smoothQuadBounds.width >= 27.0f && smoothQuadBounds.height >= 12.0f,
               "Smooth quadratic Bezier 'T' command tessellates reflected control curve");

    // Single-corner teardrop (1 sharp apex at (16,2) + smooth cubic Bezier loop) preserves sharp corner coloring
    const AxbMtsdfPathEntry teardropEntry = BakeSvgPathToMtsdfEntry(
        "M 16,2 C 28,14 26,30 16,30 C 6,30 4,14 16,2 Z", 32u, 4.0f, 0u, 512u);
    bool hasDistinctRgbChannels = false;
    for (std::size_t px = 0; px + 3u < teardropEntry.mtsdfRgbaPixels.size(); px += 4u) {
        if (teardropEntry.mtsdfRgbaPixels[px + 0u] != teardropEntry.mtsdfRgbaPixels[px + 1u] ||
            teardropEntry.mtsdfRgbaPixels[px + 1u] != teardropEntry.mtsdfRgbaPixels[px + 2u]) {
            hasDistinctRgbChannels = true;
            break;
        }
    }
    AERO_CHECK(hasDistinctRgbChannels,
               "Single-corner teardrop contour assigns multi-channel directional colors instead of degenerating to 1-channel SDF");

    // Centered negative coordinates in Path::MeasureCore
    auto centeredPath = MakeIntrusive<Path>();
    centeredPath->SetData("M -16,-16 L 16,-16 L 16,16 L -16,16 Z");
    centeredPath->Measure(Size{200.0f, 200.0f});
    AERO_CHECK(std::fabs(centeredPath->DesiredSize().width - 32.0f) < 0.1f &&
                   std::fabs(centeredPath->DesiredSize().height - 32.0f) < 0.1f,
               "Path::MeasureCore handles negative origin-centered SVG bounds (-16..16 -> 32x32)");

    // 2. Non-square 5:1 <Path> aspect ratio, stroke-only open polyline, and cross-package UV isolation
    auto canvas = MakeIntrusive<Canvas>();
    canvas->SetWidth(200.0f);
    canvas->SetHeight(160.0f);
    canvas->SetValue(PropId_Background, value::FromColor(Color{0.05f, 0.05f, 0.08f, 1.0f}));

    // Wide 5:1 bar path [0..100] x [0..20] placed at (20, 10) with Width=100, Height=20
    auto widePath = MakeIntrusive<Path>();
    widePath->SetData("M 0,0 L 100,0 L 100,20 L 0,20 Z");
    widePath->SetWidth(100.0f);
    widePath->SetHeight(20.0f);
    widePath->SetFill(Color{0.0f, 1.0f, 0.2f, 1.0f});
    widePath->SetValue(PropId_CanvasLeft, MakeFloat(20.0f));
    widePath->SetValue(PropId_CanvasTop, MakeFloat(10.0f));
    canvas->AddChild(widePath.Get());

    // Open V-chevron polyline with ONLY Stroke (no Fill!) placed at (20, 50)
    auto openChevron = MakeIntrusive<Path>();
    openChevron->SetData("M 0,0 L 20,24 L 40,0");
    openChevron->SetWidth(40.0f);
    openChevron->SetHeight(24.0f);
    openChevron->SetStroke(Color{1.0f, 0.1f, 0.4f, 1.0f});
    openChevron->SetStrokeThickness(3.0f);
    openChevron->SetValue(PropId_CanvasLeft, MakeFloat(20.0f));
    openChevron->SetValue(PropId_CanvasTop, MakeFloat(50.0f));
    canvas->AddChild(openChevron.Get());

    canvas->Measure(Size{200.0f, 160.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(canvas.Get());

    // Verify atlas UV isolation: MTSDF icons live in bottom half (v >= 0.5), glyphs live in top half (v < 0.5),
    // and two distinct icons never share overlapping UVs.
    const auto* wideEntry = AxbTemplateRegistry::Instance().FindMtsdfPath(ComputeSvgPathHash(widePath->GetData()));
    const auto* chevEntry = AxbTemplateRegistry::Instance().FindMtsdfPath(ComputeSvgPathHash(openChevron->GetData()));
    AERO_CHECK(wideEntry != nullptr && chevEntry != nullptr, "Both runtime MTSDF entries registered");
    AERO_CHECK(wideEntry->v0 >= 0.5f && chevEntry->v0 >= 0.5f,
               "MTSDF icon UVs are strictly isolated in bottom half [0.5..1.0) away from font glyphs");
    AERO_CHECK(std::fabs(wideEntry->u0 - chevEntry->u0) > 0.01f || std::fabs(wideEntry->v0 - chevEntry->v0) > 0.01f,
               "Distinct MTSDF icons receive non-overlapping atlas UV slots");
    AERO_CHECK(tree.LookupCodePointGlyph(U'A').v1 <= 0.5f && tree.LookupCodePointGlyph(0x4E2Du).v1 <= 0.5f,
               "ASCII and CJK glyph UVs stay strictly in top half [0.0..0.5)");

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 160u, fb);
    // Wide 5:1 path at Y=[10..30]: sample Y=12 (near top) and Y=28 (near bottom) at X=70 -> BOTH must be bright green!
    // (Previously squashed 25:1 to only Y=[18.6..21.4]!)
    const std::size_t wideTopIdx = (12u * 200u + 70u) * 4u;
    const std::size_t wideBotIdx = (28u * 200u + 70u) * 4u;
    AERO_CHECK(fb[wideTopIdx + 1u] > 200u && fb[wideBotIdx + 1u] > 200u,
               "5:1 non-square <Path> renders full 20px height without (W/H)^2 aspect-ratio squash");

    // Open chevron at (20,50), size (40,24): vertex tip (40, 74) has pink stroke, while top-center (40, 53) MUST stay dark (not auto-closed triangle!)
    const std::size_t chevStrokeTipIdx = (73u * 200u + 40u) * 4u;
    const std::size_t chevOpenInteriorIdx = (53u * 200u + 40u) * 4u;
    AERO_CHECK(fb[chevStrokeTipIdx + 0u] > 180u, "Stroke-only open <Path> rendered pink stroke along polyline");
    AERO_CHECK(fb[chevOpenInteriorIdx + 0u] < 40u, "Open <Path> was NOT falsely auto-closed into a filled triangle");

    // 3. Container Collapse / Opacity propagation, >42-glyph multi-node TextBlock, and Button label growth/shrink
    auto subPanel = MakeIntrusive<StackPanel>();
    subPanel->SetValue(PropId_CanvasLeft, MakeFloat(10.0f));
    subPanel->SetValue(PropId_CanvasTop, MakeFloat(90.0f));
    subPanel->SetWidth(180.0f);
    subPanel->SetHeight(60.0f);

    auto backedTb = MakeIntrusive<TextBlock>();
    backedTb->SetValue(PropId_Background, value::FromColor(Color{0.8f, 0.2f, 0.1f, 1.0f}));
    backedTb->SetText("Short");
    subPanel->AddChild(backedTb.Get());

    auto growBtn = MakeIntrusive<Button>();
    growBtn->SetWidth(160.0f);
    growBtn->SetHeight(28.0f);
    growBtn->SetContentText("OK");
    subPanel->AddChild(growBtn.Get());

    canvas->AddChild(subPanel.Get());
    canvas->Measure(Size{200.0f, 160.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});
    (void)tree.SyncVisualTree(canvas.Get());

    // Expand backedTb beyond 42 visible glyphs -> preserves background quad AND emits >= 2 glyph nodes (no 42-char truncation!)
    backedTb->SetText("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"); // 62 visible glyphs!
    growBtn->SetContentText("ExpandedButtonCaptionExceedingSixteenSlots");
    canvas->Measure(Size{200.0f, 160.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});
    (void)tree.SyncVisualTree(canvas.Get());
    AERO_CHECK(backedTb->RenderNodeSpan() == 3u,
               "Backed 62-glyph TextBlock bound 3-node span (1 background + 2 SdfTextGlyph pages, 0 truncation)");
    AERO_CHECK(growBtn->RenderNodeSpan() == 2u,
               "Expanded Button preserved contiguous 2-node span (box + expanded label)");

    // Shrink Button back to "Go" -> must update expanded span cleanly without leaving ghost text!
    growBtn->SetContentText("Go");
    (void)tree.SyncVisualTree(canvas.Get());
    const RenderNodeDescriptor* btnLbl = tree.GetNode(growBtn->RenderNodeId() + 1u);
    AERO_CHECK(btnLbl != nullptr &&
                   ((tree.GetPage(btnLbl->pageIndex).vertices[btnLbl->vertexOffsetInPage + 2u * 4u].rgba >> 24u) & 0xFFu) == 0u,
               "Button label shrink cleared trailing glyph vertices with zero ghost text");

    // Collapse non-empty container subPanel -> 0 heap allocs and ALL child vertices zeroed!
    subPanel->SetVisibility(Visibility::Collapsed);
    canvas->Measure(Size{200.0f, 160.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});
    {
        AllocationTrackingScope allocScope;
        (void)tree.SyncVisualTree(canvas.Get());
        AERO_CHECK(allocScope.count() == 0u, "Collapsing container subtree performed 0 heap allocations");
    }
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 200u, 160u, fb);
    const std::size_t collapsedRegionIdx = (100u * 200u + 30u) * 4u;
    AERO_CHECK(fb[collapsedRegionIdx + 0u] < 30u && fb[collapsedRegionIdx + 2u] < 30u,
               "Collapsed container hid all descendant TextBlock/Button primitives (zero ghost pixels)");

    // 4. Multi-page GPU draw batch stride across partial CommandPages + RenderTransformMatrix HitTest + DesktopEventHost wake
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 64);
    const auto batchStats = gpuStream.SubmitDirtyPages(tree);
    if (!gpuStream.LastClipDrawBatches().empty() && gpuStream.LastClipDrawBatches()[0].pageCount > 1u) {
        const auto& b0 = gpuStream.LastClipDrawBatches()[0];
        const std::uint32_t minExpected =
            (b0.pageCount - 1u) * static_cast<std::uint32_t>(kVerticesPerCommandPage) +
            tree.GetPage(b0.firstPageIndex + b0.pageCount - 1u).header.vertexCount;
        AERO_CHECK(b0.vertexCount == minExpected,
                   "Multi-page GpuClipDrawBatch accounts for 168-vertex page stride without truncating trailing quads");
    }
    (void)batchStats;

    // RenderTransformMatrix HitTest symmetry
    auto rtmBtn = MakeIntrusive<Button>();
    rtmBtn->SetWidth(40.0f);
    rtmBtn->SetHeight(20.0f);
    rtmBtn->SetValue(PropId_CanvasLeft, MakeFloat(10.0f));
    rtmBtn->SetValue(PropId_CanvasTop, MakeFloat(120.0f));
    rtmBtn->SetValue(PropId_RenderTransformMatrix, UnifiedValue{Float4Value{1.0f, 1.0f, 80.0f, 0.0f}});
    canvas->AddChild(rtmBtn.Get());
    canvas->Measure(Size{200.0f, 160.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 200.0f, 160.0f});
    AERO_CHECK(canvas->HitTest(Point{105.0f, 130.0f}) == rtmBtn.Get(),
               "HitTest inverts RenderTransformMatrix (+80px X translation) accurately");
    AERO_CHECK(canvas->HitTest(Point{25.0f, 130.0f}) == canvas.Get(),
               "HitTest misses unshifted pre-transform coordinates");

    // DesktopEventHost wakes automatically on direct visual property mutation when renderTree is attached
    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    DesktopEventHost desktopHost(canvas.Get(), &bindingEngine, &layoutQueue, &tree);
    desktopHost.SetViewportSize(Size{200.0f, 160.0f});
    desktopHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost sleeps idle after syncing renderTree");
    rtmBtn->SetOpacity(0.4f);
    AERO_CHECK(!desktopHost.CanSleepIdle(), "DesktopEventHost wakes automatically on UIElement::SetOpacity");
    desktopHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost returns to sleep after syncing dirty opacity");

    // 5. wasm32-safe overflow guard on malformed .axb4 string length & malformed UTF-8 continuation preservation
    const std::uint8_t badAxb4[] = {
        0x41, 0x58, 0x42, 0x34, // "AXB4"
        0x01, 0x00, 0x00, 0x00, // version 1
        0xFF, 0xFF, 0xFF, 0xFF  // className len = 0xFFFFFFFF (would wrap 32-bit offset + len!)
    };
    AxbPackage badOut{};
    AERO_CHECK(!AxbPackage::Deserialize(badAxb4, sizeof(badAxb4), badOut),
               "AxbPackage::Deserialize safely rejects 0xFFFFFFFF string length without 32-bit overflow");

    const std::string brokenUtf8 = "\xE4" "OK"; // truncated 3-byte UTF-8 lead followed by ASCII "OK"
    AERO_CHECK(CountVisibleUtf8Glyphs(brokenUtf8) == 3u,
               "DecodeNextUtf8CodePoint emits U+FFFD for truncated lead byte without swallowing following 'O','K'");

    std::cout << "[PASS] TestPhase4ReviewBugfixRegressions\n";
}

} // namespace aero::tests

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    aero::tests::TestO1RenderNodeSyncAndZeroAllocDirtySubtreePrune();
    aero::tests::TestUtf8AndCjkSdfTextGlyphEmissionAndLiveSync();
    aero::tests::TestOfflineMtsdfPathBakingAotAndSharpCornerScaling();
    aero::tests::TestClipToBoundsScissorBatchingAndGoldenImagePixels();
    aero::tests::TestCrossDictionaryGeometryResourceMtsdfEndToEnd();
    aero::tests::TestPhase4ReviewBugfixRegressions();

    std::cout << "All AeroGUI 2.0 Phase 4 Runtime/Render/MTSDF tests passed 100%!\n";
    return 0;
}
