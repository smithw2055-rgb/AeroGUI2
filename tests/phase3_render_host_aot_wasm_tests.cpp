#include <array>
#include <atomic>
#include <cassert>
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
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/aot_cpp/AotRuntime.h"

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

void* operator new(std::size_t size) {
    if (gTrackAllocations.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
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
// Test 1: 24B AeroVertex, 4KB PagedRenderTree & Incremental Dirty Page Upload
// ============================================================================
void TestAeroVertex24BAndPagedRenderTreeIncrementalUpdate() {
    static_assert(sizeof(AeroVertex) == 24, "AeroVertex must be 24B");
    static_assert(sizeof(CommandPageHeader) == 64, "CommandPageHeader must be 64B");
    static_assert(sizeof(CommandPage) == 4096, "CommandPage must be 4KB (4096B)");

    PagedRenderTree tree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 64);

    // 100 nodes = 400 vertices -> spans 3 4KB CommandPages (42 quads = 168 vertices per page)
    const std::uint32_t firstId = tree.AllocateHudBarBatch(
        100,
        16.0f,
        16.0f,
        200.0f,
        12.0f,
        4.0f,
        Color{0.2f, 0.8f, 0.3f, 1.0f},
        4.0f);
    AERO_CHECK(firstId == 0u, "First allocated HUD bar ID is 0");
    AERO_CHECK(tree.NodeCount() == 100u, "100 render nodes allocated");
    AERO_CHECK(tree.PageCount() == 3u, "100 quads span exactly 3 4KB CommandPages");
    AERO_CHECK(tree.DirtyPageCount() == 3u, "All 3 pages initially dirty");

    // Also allocate a pre-cached digit HUD counter node on the 3rd page
    const std::uint32_t digitsNodeId = tree.AllocateHudDigitsNode(
        300.0f,
        24.0f,
        10.0f,
        18.0f,
        6u,
        Color{1.0f, 0.95f, 0.2f, 1.0f});
    AERO_CHECK(digitsNodeId == 100u, "Digits node allocated at index 100");

    // Initial full submit clears all dirty page flags
    const auto initStats = gpuStream.SubmitDirtyPages(tree);
    AERO_CHECK(initStats.dirtyPagesUploaded == 3u, "Initial upload streamed 3 dirty pages");
    AERO_CHECK(initStats.drawCallCount == 1u, "Entire UI tree drawn in 1 UberShader draw call");
    AERO_CHECK(tree.DirtyPageCount() == 0u, "Dirty pages cleared after submit");

    // Modify ONLY node #50 (lives in Page 1: nodes 42..83) inside zero-allocation scope
    {
        AllocationTrackingScope allocScope;
        const bool changed = tree.UpdateNodeWidth(50u, 145.5f);
        AERO_CHECK(changed, "Node #50 width updated");
        AERO_CHECK(allocScope.count() == 0u, "0 heap allocations on partial node width update");
    }

    AERO_CHECK(tree.DirtyPageCount() == 1u, "Only 1 4KB CommandPage marked dirty");
    AERO_CHECK(!tree.IsPageDirty(0u), "Page 0 remains clean");
    AERO_CHECK(tree.IsPageDirty(1u), "Page 1 is dirty");
    AERO_CHECK(!tree.IsPageDirty(2u), "Page 2 remains clean");

    const auto incrementalStats = gpuStream.SubmitDirtyPages(tree);
    AERO_CHECK(incrementalStats.dirtyPagesUploaded == 1u, "Only Page 1 uploaded to GPU ring buffer");
    AERO_CHECK(incrementalStats.verticesStreamed == 168u, "Only Page 1 vertices streamed");

    const AeroVertex* mappedPage1 = gpuStream.MappedPageVertices(1u);
    AERO_CHECK(mappedPage1 != nullptr, "Mapped ring buffer pointer valid for Page 1");
    const std::size_t localQuadIdx = 50u - 42u;
    const AeroVertex& v1 = mappedPage1[localQuadIdx * 4u + 1u];
    AERO_CHECK(std::fabs(v1.x - (16.0f + 145.5f)) < 0.01f, "Mapped GPU vertex reflects updated width 145.5f");

    // Update pre-cached HUD digits text on Page 2 with 0 heap allocations
    {
        AllocationTrackingScope allocScope;
        const bool ok = tree.UpdateHudDigitsText(digitsNodeId, "98765");
        AERO_CHECK(ok, "HUD digits text updated");
        AERO_CHECK(allocScope.count() == 0u, "0 heap allocations on pre-cached digit glyph UV update");
    }
    AERO_CHECK(tree.DirtyPageCount() == 1u && tree.IsPageDirty(2u), "Only Page 2 marked dirty by digit update");
    (void)gpuStream.SubmitDirtyPages(tree);

    // Edge cases: redundant update & out-of-bounds node ID
    AERO_CHECK(!tree.UpdateNodeWidth(50u, 145.5f), "Redundant same-value update is a no-op");
    AERO_CHECK(tree.DirtyPageCount() == 0u, "No page dirtied by redundant update");
    AERO_CHECK(!tree.UpdateNodeWidth(99999u, 50.0f), "Out-of-range node ID safely rejected");

    // Full-page 42-digit HUD node allocation & dirtyQuadMask verification
    const std::uint32_t maxDigitsNodeId = tree.AllocateHudDigitsNode(
        0.0f, 100.0f, 8.0f, 16.0f, 42u, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AERO_CHECK(tree.GetNode(maxDigitsNodeId) != nullptr &&
                   tree.GetNode(maxDigitsNodeId)->vertexCount == 168u,
               "42-digit HUD node occupies full 168-vertex 4KB CommandPage");
    const std::uint64_t expected42BitMask = (1ULL << 42u) - 1ULL;
    AERO_CHECK((tree.GetPage(tree.GetNode(maxDigitsNodeId)->pageIndex).header.dirtyQuadMask & expected42BitMask) == expected42BitMask,
               "All 42 quad bits marked in dirtyQuadMask for 42-digit HUD node");
    (void)gpuStream.SubmitDirtyPages(tree);

    // Ring buffer wrap-around (pageIndex >= maxPagesInRing)
    ZeroCopyGpuStream smallRingStream(ShaderBackend::DesktopGL33, 16);
    PagedRenderTree wrapTree;
    (void)wrapTree.AllocateHudBarBatch(18u * 42u, 0.0f, 0.0f, 80.0f, 6.0f, 1.0f, Color{0.5f, 0.5f, 0.5f, 1.0f}, 0.0f);
    AERO_CHECK(wrapTree.PageCount() == 18u, "18 pages allocated (> 16-page ring capacity)");
    const auto wrapStats = smallRingStream.SubmitDirtyPages(wrapTree);
    AERO_CHECK(wrapStats.dirtyPagesUploaded == 18u, "Ring buffer wrap-around streamed all 18 pages");
    AERO_CHECK(smallRingStream.MappedPageVertices(17u) == smallRingStream.MappedPageVertices(1u),
               "Page 17 wraps to ring slot 1 (17 % 16 == 1)");

    std::cout << "[PASS] TestAeroVertex24BAndPagedRenderTreeIncrementalUpdate\n";
}

// ============================================================================
// Test 2: 4-Vertex Quad Analytical SDF RoundedRect & Multi-Backend UberShaders
// ============================================================================
void TestSdfRoundedRectQuadAndUberShaders() {
    const std::uint16_t packed = PackSdfRoundedRectParams(12.5f, 2.0f);
    float unpackedRadius = 0.0f;
    float unpackedBorder = 0.0f;
    UnpackSdfRoundedRectParams(packed, unpackedRadius, unpackedBorder);
    AERO_CHECK(std::fabs(unpackedRadius - 12.5f) < 0.01f, "SDF cornerRadius 12.5f round-trip");
    AERO_CHECK(std::fabs(unpackedBorder - 2.0f) < 0.01f, "SDF borderThickness 2.0f round-trip");

    // Analytical SDF coverage verification (replaces 256-vertex CPU corner tessellation)
    const float centerCov = EvaluateSdfRoundedRectCoverage(0.0f, 0.0f, 100.0f, 40.0f, 16.0f, 1.0f);
    const float outsideCov = EvaluateSdfRoundedRectCoverage(110.0f, 50.0f, 100.0f, 40.0f, 16.0f, 1.0f);
    const float cornerCutoutCov = EvaluateSdfRoundedRectCoverage(99.0f, 39.0f, 100.0f, 40.0f, 16.0f, 1.0f);
    const float edgeAaCov = EvaluateSdfRoundedRectCoverage(100.0f, 0.0f, 100.0f, 40.0f, 16.0f, 1.0f);

    AERO_CHECK(centerCov > 0.99f, "Interior of SDF rounded rect has 1.0 coverage");
    AERO_CHECK(outsideCov < 0.01f, "Exterior of SDF rounded rect has 0.0 coverage");
    AERO_CHECK(cornerCutoutCov < 0.01f, "Corner outside arc is analytically clipped by SDF");
    AERO_CHECK(edgeAaCov > 0.45f && edgeAaCov < 0.55f, "Boundary edge has ~0.5 analytical AA coverage");

    // Verify Desktop GL 3.3, WebGL2 (#version 300 es), and WebGPU WGSL UberShader sources
    const std::string_view gl33Vs = ZeroCopyGpuStream::UberVertexShaderSource(ShaderBackend::DesktopGL33);
    const std::string_view gl33Ps = ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::DesktopGL33);
    const std::string_view webgl2Vs = ZeroCopyGpuStream::UberVertexShaderSource(ShaderBackend::WebGL2);
    const std::string_view webgl2Ps = ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::WebGL2);
    const std::string_view wgslSrc = ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::WebGPU_WGSL);

    AERO_CHECK(gl33Vs.find("#version 330 core") != std::string_view::npos, "DesktopGL33 VS header");
    AERO_CHECK(gl33Ps.find("sdRoundedBox") != std::string_view::npos, "DesktopGL33 PS analytical SDF");
    AERO_CHECK(webgl2Vs.find("#version 300 es") != std::string_view::npos, "WebGL2 VS #version 300 es");
    AERO_CHECK(webgl2Ps.find("AERO_WEBGL2_GRAYSCALE_SDF_AA") != std::string_view::npos, "WebGL2 grayscale SDF AA macro");
    AERO_CHECK(wgslSrc.find("@vertex") != std::string_view::npos &&
               wgslSrc.find("@fragment") != std::string_view::npos, "WebGPU WGSL unified entry points");
    AERO_CHECK(wgslSrc.find("borderThickness") != std::string_view::npos,
               "WebGPU WGSL fragment shader includes SDF borderThickness ring evaluation");

    // Verify WebGL2 stream automatically routes grayscale SDF AA and glBufferSubData spans
    PagedRenderTree tree;
    (void)tree.AllocateRenderNode(10.0f, 10.0f, 120.0f, 36.0f, Color{0.1f, 0.5f, 0.9f, 1.0f}, 8.0f, 1.5f);
    ZeroCopyGpuStream webglStream(ShaderBackend::WebGL2, 16);
    const auto webglStats = webglStream.SubmitDirtyPages(tree);
    AERO_CHECK(webglStats.mechanism == RingUploadMechanism::WebGL2BufferSubData, "WebGL2 uses glBufferSubData ring");
    AERO_CHECK(webglStats.grayscaleSdfTextActive, "WebGL2 enables grayscale SDF AA text");
    AERO_CHECK(webglStream.LastBufferSubDataSpans().size() == 1u, "WebGL2 recorded 1 glBufferSubData span");

    std::cout << "[PASS] TestSdfRoundedRectQuadAndUberShaders\n";
}

// ============================================================================
// Test 3: 1,000 ECS HUD Nodes Refresh Benchmark (< 0.08 ms & 0 Heap Allocations)
// ============================================================================
void TestEcsHud1000NodesSub80MicrosecondsZeroAllocBenchmark() {
    struct EcsUnitHudState {
        float healthWidth;
        float shieldWidth;
        float energy;
        std::uint32_t unitFlags;
    };

    constexpr std::size_t kHudNodeCount = 1000;
    std::vector<EcsUnitHudState> ecsColumn(kHudNodeCount);
    for (std::size_t i = 0; i < kHudNodeCount; ++i) {
        ecsColumn[i] = EcsUnitHudState{
            80.0f + static_cast<float>(i % 120u),
            50.0f,
            100.0f,
            1u};
    }

    PagedRenderTree renderTree;
    const std::uint32_t baseNodeId = renderTree.AllocateHudBarBatch(
        kHudNodeCount,
        20.0f,
        20.0f,
        100.0f,
        8.0f,
        2.0f,
        Color{0.9f, 0.2f, 0.2f, 1.0f},
        3.0f);
    renderTree.ClearDirtyPages();

    // Mutate all 1,000 ECS health bars for the new frame
    for (std::size_t i = 0; i < kHudNodeCount; ++i) {
        ecsColumn[i].healthWidth = 110.0f + static_cast<float>((i * 3u) % 90u);
    }

    std::size_t updatedNodes = 0;
    std::size_t heapAllocsDuringRefresh = 0;
    float elapsedMs = 0.0f;

    {
        AllocationTrackingScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();

        updatedNodes = renderTree.BatchUpdateWidthsFromRawColumn(
            baseNodeId,
            kHudNodeCount,
            ecsColumn.data(),
            sizeof(EcsUnitHudState),
            offsetof(EcsUnitHudState, healthWidth),
            1.0f);

        const auto t1 = std::chrono::steady_clock::now();
        heapAllocsDuringRefresh = allocScope.count();
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        elapsedMs = static_cast<float>(elapsedNs) / 1000000.0f;
    }

    std::cout << "[BENCH] 1,000 ECS HUD Nodes PagedRenderTree Refresh: "
              << elapsedMs << " ms (target < 0.08 ms), updated=" << updatedNodes
              << ", dirtyPages=" << renderTree.DirtyPageCount()
              << ", heapAllocs=" << heapAllocsDuringRefresh << "\n";

    AERO_CHECK(updatedNodes == kHudNodeCount, "All 1,000 ECS HUD nodes updated");
    AERO_CHECK(heapAllocsDuringRefresh == 0u, "Strictly 0 heap allocations during 1,000 HUD node refresh");
    AERO_CHECK(elapsedMs < 0.08f, "1,000 ECS HUD nodes refreshed in < 0.08 ms");

    std::cout << "[PASS] TestEcsHud1000NodesSub80MicrosecondsZeroAllocBenchmark\n";
}

// ============================================================================
// Test 4: Tri-Host Drivers (EmbeddedGameHost, DesktopEventHost, WebCanvasHost)
// ============================================================================
void TestTriHostDriversEmbeddedDesktopAndWebCanvas() {
    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 64);

    auto rootPanel = MakeIntrusive<StackPanel>();
    rootPanel->SetWidth(640.0f);
    rootPanel->SetHeight(360.0f);
    auto titleText = MakeIntrusive<TextBlock>();
    titleText->SetName("HeaderTitle");
    titleText->SetText("Aero Tri-Host Ready");
    rootPanel->AddChild(titleText.Get());
    auto actionBtn = MakeIntrusive<Button>();
    actionBtn->SetName("PrimaryButton");
    actionBtn->SetContentText("Launch");
    rootPanel->AddChild(actionBtn.Get());

    // 1. EmbeddedGameHost: verify activePhaseMask skips Style & Layout on pure HUD frames
    (void)renderTree.BuildFromVisualTree(rootPanel.Get());
    EmbeddedGameHost gameHost(rootPanel.Get(), &bindingEngine, &layoutQueue, &renderTree);
    gameHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(gameHost.Telemetry().stylePassesRun == 1u, "Full tick runs Style phase");
    AERO_CHECK(gameHost.Telemetry().layoutPassesRun == 1u, "Full tick runs Layout phase");

    // Pure HUD refresh frame skips Style and Layout phases completely
    gameHost.TickUi(0.016f, UiPhase_HudFastRefresh);
    AERO_CHECK(gameHost.Telemetry().stylePassesRun == 1u, "HUD fast refresh skipped Style phase");
    AERO_CHECK(gameHost.Telemetry().layoutPassesRun == 1u, "HUD fast refresh skipped Layout phase");
    const auto gameGpuStats = gameHost.RecordGpuCommands(renderTree, gpuStream);
    AERO_CHECK(gameGpuStats.drawCallCount == 1u, "EmbeddedGameHost recorded single-draw-call GPU stream");

    // 2. DesktopEventHost: verify idle sleep (0.0% CPU / 0.0% GPU polling) when clean
    DesktopEventHost desktopHost(rootPanel.Get(), &bindingEngine, &layoutQueue);
    desktopHost.SetStateFlags(false, false, false, false);
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost enters sleep idle when no dirty state");
    AERO_CHECK(desktopHost.CpuPollingUsagePercent() == 0.0f, "0.0% CPU polling when idle");
    AERO_CHECK(desktopHost.GpuPollingUsagePercent() == 0.0f, "0.0% GPU polling when idle");

    desktopHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(desktopHost.Telemetry().frameCount == 0u, "Idle DesktopEventHost does not tick frames");

    // Wake on dirty layout event, process 1 frame, then return to idle sleep automatically
    desktopHost.NotifyDirtyLayout();
    AERO_CHECK(!desktopHost.CanSleepIdle(), "DesktopEventHost wakes immediately on dirty notification");
    desktopHost.TickUi(0.016f, UiPhase_All);
    AERO_CHECK(desktopHost.Telemetry().frameCount == 1u, "DesktopEventHost processed wake frame");
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost returns to idle sleep after processing frame");

    // 3. WebCanvasHost: non-ASYNCIFY rAF state machine, Chinese IME composition, Clipboard, ARIA & CJK Font Shards
    static_assert(!WebCanvasHost::IsAsyncifyEnabled(), "WebCanvasHost must never use ASYNCIFY");
    WebCanvasHost webHost(rootPanel.Get(), &bindingEngine, &layoutQueue, ShaderBackend::WebGL2);

    // 3a. Chinese/Japanese IME composition & cursor tracking
    WebImeAdapter& ime = webHost.ImeAdapter();
    ime.FocusTextInput(titleText.Get(), Rect{120.0f, 240.0f, 180.0f, 24.0f});
    AERO_CHECK(ime.IsImeActive(), "WebImeAdapter focused");
    const std::string css = ime.GenerateOverlayCssStyle();
    AERO_CHECK(css.find("left:120.0px;top:240.0px") != std::string::npos, "Transparent DOM input CSS aligned");

    ime.OnCompositionStart();
    AERO_CHECK(ime.IsComposing(), "IME composition started");
    ime.OnCompositionUpdate("zh", 2u);
    AERO_CHECK(std::fabs(ime.OverlayDomInputRect().x - 124.0f) < 0.01f,
               "IME overlay cursor at baseCaret(120) + 2*2.0 = 124.0");
    ime.OnCompositionUpdate("zhong", 5u);
    AERO_CHECK(std::fabs(ime.OverlayDomInputRect().x - 130.0f) < 0.01f,
               "IME overlay cursor at baseCaret(120) + 5*2.0 = 130.0 (no cumulative drift)");
    ime.OnCompositionUpdate("zhongwen", 8u);
    AERO_CHECK(std::fabs(ime.OverlayDomInputRect().x - 136.0f) < 0.01f,
               "IME overlay cursor at baseCaret(120) + 8*2.0 = 136.0");
    AERO_CHECK(titleText->GetText() == "zhongwen", "Focused control shows live preedit text");
    ime.OnCompositionEnd("中文输入完成");
    AERO_CHECK(!ime.IsComposing(), "IME composition ended");
    AERO_CHECK(titleText->GetText() == "中文输入完成", "Committed Chinese text applied to control");

    // 3b. Browser Clipboard pre-cache & ARIA shadow semantic tree
    ime.SyncClipboardFromDomEvent("ClipboardPayload_2026");
    AERO_CHECK(ime.ReadCachedClipboard() == "ClipboardPayload_2026", "Clipboard pre-cached from DOM");

    const AriaShadowNode ariaRoot = ime.BuildAriaShadowTree(rootPanel.Get());
    AERO_CHECK(ariaRoot.children.size() == 2u, "ARIA shadow tree contains 2 child semantics");
    AERO_CHECK(ariaRoot.children[0].role == "text" && ariaRoot.children[0].value == "中文输入完成",
               "ARIA text node reflects live Chinese text");
    AERO_CHECK(ariaRoot.children[1].role == "button" && ariaRoot.children[1].focusable,
               "ARIA button node marked focusable");

    // 3c. CJK Font Unicode shard on-demand streaming loader
    WebFontStreamProvider& fontStream = webHost.FontStream();
    const std::size_t asciiQueued = fontStream.InspectTextAndQueueMissingShards("ASCII 12345", 0u);
    AERO_CHECK(asciiQueued == 0u, "Basic ASCII shard is pre-loaded (0 network fetches)");

    const std::size_t cjkQueued = fontStream.InspectTextAndQueueMissingShards("中文渲染测试", 0u);
    AERO_CHECK(cjkQueued > 0u, "Missing CJK Unicode shards queued asynchronously without blocking");
    AERO_CHECK(!fontStream.PendingFetchRequests().empty(), "Pending CJK shard requests recorded");

    const std::uint32_t firstCjkShardId = fontStream.PendingFetchRequests().front().shardId;
    AERO_CHECK(fontStream.GetCodePointShardState(firstCjkShardId << 8u) == ShardLoadState::FetchingAsync,
               "CJK shard state is FetchingAsync");

    renderTree.ClearDirtyPages();
    const std::vector<std::uint8_t> dummyGlyphShard(2048, 0xAAu);
    const bool shardApplied = fontStream.OnShardDataArrived(firstCjkShardId, dummyGlyphShard, &renderTree);
    AERO_CHECK(shardApplied, "Arrived CJK font shard hot-patched");
    AERO_CHECK(fontStream.GetCodePointShardState(firstCjkShardId << 8u) == ShardLoadState::Loaded,
               "CJK shard transitioned to Loaded");
    AERO_CHECK(renderTree.DirtyPageCount() == 1u, "Arrived CJK shard marked owning 4KB page dirty");

    const auto webGpuStats = webHost.RecordGpuCommands(renderTree, gpuStream);
    AERO_CHECK(webGpuStats.mechanism == RingUploadMechanism::WebGL2BufferSubData,
               "WebCanvasHost streams dirty pages via WebGL2 glBufferSubData");
    AERO_CHECK(webGpuStats.grayscaleSdfTextActive, "WebCanvasHost routes grayscale SDF AA text");

    std::cout << "[PASS] TestTriHostDriversEmbeddedDesktopAndWebCanvas\n";
}

// ============================================================================
// Test 5: aero-compiler --aot-cpp & RealScript rsaot C++17 AOT Closed Loop
// ============================================================================
SymbolId gHudTypeIdForAotTest = 0;

bool DeployCommandAotEntry(
    rs_aot::ExecutionContext& ctx,
    const UnifiedValue* arguments,
    std::size_t argumentCount,
    UnifiedValue& result) {
    std::array<UnifiedValue, 2> argBuf{};
    std::array<UnifiedValue, 2> localBuf{};
    std::array<UnifiedValue, 2> regBuf{};
    for (std::size_t i = 0; i < argumentCount && i < argBuf.size(); ++i) {
        argBuf[i] = arguments[i];
    }
    rs_aot::FrameScope frame(
        ctx,
        ValueSpan(argBuf.data(), argumentCount),
        ValueSpan(localBuf.data(), localBuf.size()),
        ValueSpan(regBuf.data(), regBuf.size()));

    UnifiedValue curVal{};
    if (!ctx.loadField(gHudTypeIdForAotTest, 1u, argBuf[0], curVal)) {
        return false;
    }
    const std::int64_t nextVal = std::get<std::int64_t>(curVal) + std::get<std::int64_t>(argBuf[1]);
    if (!ctx.storeField(gHudTypeIdForAotTest, 1u, argBuf[0], value::FromInt(nextVal))) {
        return false;
    }
    result = std::monostate{};
    return true;
}

void TestAeroCompilerAotCppAndRealScriptAotClosedLoop() {
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlPath = "SquadTacticalHud.xaml";
    input.emitAotCpp = true;
    input.aotCppNamespace = "aero::aot_test";
    input.xamlContent = R"(
<UserControl x:Class="Game.UI.SquadTacticalHud" Width="800" Height="450">
    <StackPanel x:Name="MainStack" Spacing="6">
        <TextBlock x:Name="SquadNameText" Text="{Binding SquadTitle}" FontSize="20" />
        <Border x:Name="HealthBar" Width="{EcsBind Game.Sim.UnitHealth.CurrentWidth}" Height="14" CornerRadius="4" Background="#22CC55" />
        <Button x:Name="DeployBtn" Content="Deploy" Click="RequestDeploy" Width="140" Height="36" />
    </StackPanel>
</UserControl>
)";
    input.scriptSources.push_back(SourceFile{
        "SquadTacticalHud.rs",
        R"(
module Game.UI;

[SimState]
public partial class SquadTacticalHud
{
    [Observable]
    public string SquadTitle { get; set; }

    [Observable]
    public int DeployedUnits { get; set; }

    public SquadTacticalHud()
    {
        SquadTitle = "Alpha Vanguard";
        DeployedUnits = 0;
    }

    [SimCommand]
    public void RequestDeploy(int squadSize)
    {
        DeployedUnits = DeployedUnits + squadSize;
    }
}
)"});

    const AeroCompileResult res = compiler.Compile(input);
    if (!res.succeeded) {
        for (const auto& d : res.diagnostics) {
            std::cerr << "  " << d << "\n";
        }
    }
    AERO_CHECK(res.succeeded, "AeroCompiler --aot-cpp compilation succeeded");
    AERO_CHECK(!res.aotCpp.header.empty() && !res.aotCpp.source.empty(),
               "Generated zero-XML C++17 view builder header & source");
    AERO_CHECK(res.aotCpp.source.find("BuildView_Game_UI_SquadTacticalHud") != std::string::npos,
               "Generated C++17 view builder function name");
    AERO_CHECK(res.aotCpp.source.find("BindChain") != std::string::npos &&
               res.aotCpp.source.find("BindEcsColumn") != std::string::npos &&
               res.aotCpp.source.find("BindScriptClickHandler") != std::string::npos,
               "Generated C++17 source contains pre-resolved Track A, Track B & Event bindings");

    // Verify RealScript C++17 AOT output includes partial class merge, [Observable] bit index,
    // [SimCommand] metadata, and zero-allocation std::array local frames
    AERO_CHECK(!res.aotCpp.scriptAotSource.empty(), "Generated RealScript C++17 AOT source");
    AERO_CHECK(res.aotCpp.scriptAotSource.find("__nativeRootHandle") != std::string::npos &&
               res.aotCpp.scriptAotSource.find("SquadNameText") != std::string::npos,
               "partial class merged XAML x:Name control handles into AOT TypeDescriptor");
    AERO_CHECK(res.aotCpp.scriptAotSource.find("std::array<") != std::string::npos &&
               res.aotCpp.scriptAotSource.find("ValueSpan(") != std::string::npos,
               "AOT functions use zero-allocation stack std::array frames instead of std::vector");

    // Verify RealScript AOT Runtime ExecutionContext closed loop:
    // 1) [Observable] 64-bit dirtyFieldMask weaving on AOT storeField
    // 2) Cross-Heap Read-Only write protection from UiPresentation domain
    // 3) [SimCommand] automatic cross-domain command enqueueing from UiPresentation domain
    // 4) Zero-allocation FrameScope(ExecutionContext&, ValueSpan, ValueSpan, ValueSpan)
    ExecutionDomainContext domains;
    const SymbolId hudTypeId = rs_sem::stableTypeId("Game.UI::SquadTacticalHud");
    gHudTypeIdForAotTest = hudTypeId;
    AERO_CHECK(!res.axbPackage.eventBindings.empty() && res.axbPackage.eventBindings.front().handlerSymbolId != 0,
               "AeroCompiler resolved RequestDeploy handlerSymbolId");
    const SymbolId deployMethodId = res.axbPackage.eventBindings.front().handlerSymbolId;

    static const rs_aot::FieldDescriptor kHudFields[] = {
        {"SquadTitle", PrimitiveType::String, 0u, 0u, false, true, 0u, true},
        {"DeployedUnits", PrimitiveType::Int, 0u, 1u, false, true, 1u, true},
    };
    static const rs_aot::TypeDescriptor kHudTypes[] = {
        {hudTypeId,
         rs_sem::TypeKind::Class,
         0u,
         "Game.UI",
         "SquadTacticalHud",
         kHudFields,
         2u,
         nullptr,
         0u,
         nullptr,
         0u,
         false,
         nullptr,
         0u,
         false,
         true,  // observable
         true,  // simState
         true}, // partialType
    };

    static const PrimitiveType kDeployParamTypes[] = {
        PrimitiveType::Object,
        PrimitiveType::Int,
    };
    static const SymbolId kDeployParamTypeIds[] = {
        hudTypeId,
        0u,
    };
    static const rs_aot::FunctionDescriptor kHudFunctions[] = {
        {deployMethodId,
         "Game.UI::SquadTacticalHud::RequestDeploy",
         PrimitiveType::Void,
         0u,
         kDeployParamTypes,
         kDeployParamTypeIds,
         2u,
         &DeployCommandAotEntry,
         nullptr,
         true}, // simCommand
    };
    static const char* const kModuleNames[] = {"Game.UI"};
    static const rs_aot::ProgramDescriptor kAotProgramDesc{
        rs_aot::RuntimeAbiMajor,
        rs_aot::RuntimeAbiMinor,
        "SquadTacticalHudAot",
        0x12345678ULL,
        kModuleNames,
        1u,
        kHudTypes,
        1u,
        kHudFunctions,
        1u,
        nullptr,
        0u};

    rs_aot::ExecutionContext aotSimCtx(kAotProgramDesc, domains.simHeapPtr(), nullptr);
    aotSimCtx.setDomainContext(&domains, DomainKind::SimDeterministic);

    // Allocate SquadTacticalHud instance on SimHeap via AOT ExecutionContext
    UnifiedValue simHudVal{};
    AERO_CHECK(aotSimCtx.newObject(hudTypeId, simHudVal), "Allocated AOT object on SimHeap");
    const ObjectRef simHudObj = std::get<ObjectRef>(simHudVal);
    AERO_CHECK(aotSimCtx.storeField(hudTypeId, 0u, simHudVal, value::FromString("Alpha Vanguard")),
               "Initialized SquadTitle");
    AERO_CHECK(aotSimCtx.storeField(hudTypeId, 1u, simHudVal, value::FromInt(2)),
               "Initialized DeployedUnits");
    (void)domains.simHeap().consumeDirtyObjects();
    domains.simHeap().clearDirtyFieldMask(simHudObj);

    // Mutate [Observable] field in Simulation AOT context -> weaves 64-bit dirtyFieldMask!
    AERO_CHECK(aotSimCtx.storeField(hudTypeId, 1u, simHudVal, value::FromInt(5)),
               "AOT storeField succeeded on SimHeap");
    AERO_CHECK((domains.simHeap().dirtyFieldMask(simHudObj) & (1ULL << 1u)) != 0ULL,
               "AOT storeField wove bit 1 into 64-bit dirtyFieldMask");
    const auto dirtyObjs = domains.simHeap().consumeDirtyObjects();
    AERO_CHECK(dirtyObjs.size() == 1u && dirtyObjs[0] == simHudObj,
               "AOT storeField registered object in ManagedHeap dirty object queue");

    // Verify 0-heap-allocation AOT function invocation using stack std::array FrameScope
    {
        AllocationTrackingScope allocScope;
        const std::array<UnifiedValue, 2> callArgs{simHudVal, value::FromInt(3)};
        UnifiedValue invokeOut{};
        const bool ok = aotSimCtx.invoke(deployMethodId, callArgs.data(), callArgs.size(), invokeOut);
        AERO_CHECK(ok, "AOT function invocation succeeded");
        AERO_CHECK(allocScope.count() == 0u, "AOT function invocation with SpanFrame performed 0 heap allocations");
    }
    UnifiedValue loadedUnits{};
    AERO_CHECK(aotSimCtx.loadField(hudTypeId, 1u, simHudVal, loadedUnits), "Loaded DeployedUnits");
    AERO_CHECK(std::get<std::int64_t>(loadedUnits) == 8, "AOT Simulation invoke updated DeployedUnits to 8");

    // Now create a UiPresentation AOT context sharing the ExecutionDomainContext
    rs_aot::ExecutionContext aotUiCtx(kAotProgramDesc, domains.uiHeapPtr(), nullptr);
    aotUiCtx.setDomainContext(&domains, DomainKind::UiPresentation);

    // 1) Cross-Heap Read-Only: UiPresentation can read SimHeap object fields in O(1)
    UnifiedValue uiReadUnits{};
    AERO_CHECK(aotUiCtx.loadField(hudTypeId, 1u, simHudVal, uiReadUnits),
               "UiPresentation AOT context reads SimHeap field");
    AERO_CHECK(std::get<std::int64_t>(uiReadUnits) == 8,
               "UiPresentation AOT context reads SimHeap field directly with zero copy");

    // 1b) Cross-Heap typeOperation (is/as) and virtual dispatch on SimHeap object from UiPresentation
    UnifiedValue isMatchOut{};
    AERO_CHECK(aotUiCtx.typeOperation(
                   PrimitiveType::Object,
                   PrimitiveType::Object,
                   hudTypeId,
                   simHudVal,
                   false,
                   isMatchOut) &&
                   std::holds_alternative<bool>(isMatchOut) &&
                   std::get<bool>(isMatchOut),
               "UiPresentation AOT typeOperation ('is') resolves cross-heap SimHeap ObjectRef type");
    UnifiedValue asCastOut{};
    AERO_CHECK(aotUiCtx.typeOperation(
                   PrimitiveType::Object,
                   PrimitiveType::Object,
                   hudTypeId,
                   simHudVal,
                   true,
                   asCastOut) &&
                   std::holds_alternative<ObjectRef>(asCastOut) &&
                   std::get<ObjectRef>(asCastOut) == simHudObj,
               "UiPresentation AOT typeOperation ('as') preserves cross-heap SimHeap ObjectRef");

    // 1c) Multi-module partial class merging in CppGenerator::generate
    {
        rs_mir::Module modA;
        modA.name = "Game.UI";
        rs_sem::TypeSymbol partA{};
        partA.id = hudTypeId;
        partA.moduleName = "Game.UI";
        partA.name = "SquadTacticalHud";
        partA.kind = rs_sem::TypeKind::Class;
        partA.observable = true;
        partA.simState = false;
        partA.partialType = true;
        rs_sem::FieldSymbol fA1{};
        fA1.name = "FieldA";
        fA1.type = PrimitiveType::Int;
        fA1.index = 0u;
        fA1.observable = true;
        fA1.observableBitIndex = 0u;
        partA.fields.push_back(fA1);
        modA.types.push_back(partA);

        rs_mir::Module modB;
        modB.name = "Game.UI.Generated";
        rs_sem::TypeSymbol partB{};
        partB.id = hudTypeId;
        partB.moduleName = "Game.UI";
        partB.name = "SquadTacticalHud";
        partB.kind = rs_sem::TypeKind::Class;
        partB.observable = false;
        partB.simState = true;
        partB.partialType = true;
        rs_sem::FieldSymbol fB1{};
        fB1.name = "FieldA";
        fB1.type = PrimitiveType::Int;
        fB1.index = 0u;
        rs_sem::FieldSymbol fB2{};
        fB2.name = "FieldB";
        fB2.type = PrimitiveType::Float;
        fB2.index = 1u;
        fB2.simState = true;
        partB.fields.push_back(fB1);
        partB.fields.push_back(fB2);
        modB.types.push_back(partB);

        rs_mir::Function dummyFn{};
        dummyFn.symbolId = 0x99887766ULL;
        dummyFn.name = "DummyEntry";
        dummyFn.returnType = PrimitiveType::Void;
        rs_mir::BasicBlock bb{};
        bb.id = 0;
        bb.terminator.kind = rs_mir::TerminatorKind::ReturnVoid;
        dummyFn.blocks.push_back(bb);
        modA.functions.push_back(dummyFn);

        rs_diag::DiagnosticBag diags;
        rs_aot::GenerationOptions genOpts{};
        genOpts.programName = "PartialMergeTest";
        genOpts.cppNamespace = "aero::partial_test";
        genOpts.emitLineDirectives = false;
        rs_aot::CppGenerator cppGen;
        const auto mergedOut = cppGen.generate({modA, modB}, diags, genOpts);
        AERO_CHECK(!diags.hasErrors(), "Multi-module partial class AOT generation succeeded");
        AERO_CHECK(mergedOut.source.find("\"FieldA\"") != std::string::npos &&
                   mergedOut.source.find("\"FieldB\"") != std::string::npos,
                   "Merged partial class TypeDescriptor contains both FieldA and FieldB");
        AERO_CHECK(mergedOut.source.find("true, true, true") != std::string::npos,
                   "Merged partial class TypeDescriptor preserved observable=true, simState=true, partialType=true");
    }

    // 2) Cross-Heap Write Protection: UiPresentation direct storeField to SimHeap is rejected!
    {
        rs_aot::ExecutionContext aotUiWriteAttempt(kAotProgramDesc, domains.uiHeapPtr(), nullptr);
        aotUiWriteAttempt.setDomainContext(&domains, DomainKind::UiPresentation);
        const bool writeAllowed = aotUiWriteAttempt.storeField(
            hudTypeId, 1u, simHudVal, value::FromInt(999));
        AERO_CHECK(!writeAllowed,
                   "UiPresentation direct write to SimHeap object rejected by AOT runtime");
        AERO_CHECK(aotUiWriteAttempt.error().code == ErrorCode::CapabilityDenied,
                   "ErrorCode::CapabilityDenied raised on cross-heap write");
    }
    AERO_CHECK(aotSimCtx.loadField(hudTypeId, 1u, simHudVal, loadedUnits) &&
               std::get<std::int64_t>(loadedUnits) == 8,
               "SimHeap field remained untouched at 8");

    // 3) [SimCommand] invocation from UiPresentation AOT context automatically enqueues command!
    const std::array<UnifiedValue, 2> uiCmdArgs{simHudVal, value::FromInt(4)};
    UnifiedValue uiCmdOut{};
    AERO_CHECK(aotUiCtx.invoke(deployMethodId, uiCmdArgs.data(), uiCmdArgs.size(), uiCmdOut),
               "UiPresentation [SimCommand] invoke succeeded");
    AERO_CHECK(domains.pendingSimCommandCount() == 1u,
               "UiPresentation AOT invoke enqueued 1 SimCommandEnvelope");
    AERO_CHECK(aotSimCtx.loadField(hudTypeId, 1u, simHudVal, loadedUnits) &&
               std::get<std::int64_t>(loadedUnits) == 8,
               "SimHeap state not mutated until Simulation frame boundary flush");

    // Flush SimCommands on Simulation domain via AOT ExecutionContext
    const auto drained = domains.consumeSimCommands();
    AERO_CHECK(drained.size() == 1u && drained[0].commandTypeId == deployMethodId,
               "Drained SimCommandEnvelope matches RequestDeploy symbolId");
    {
        std::vector<UnifiedValue> dispatchArgs;
        dispatchArgs.push_back(UnifiedValue{drained[0].targetObject});
        dispatchArgs.insert(dispatchArgs.end(), drained[0].payload.begin(), drained[0].payload.end());
        UnifiedValue simFlushOut{};
        AERO_CHECK(aotSimCtx.invoke(
                       drained[0].commandTypeId,
                       dispatchArgs.data(),
                       dispatchArgs.size(),
                       simFlushOut),
                   "Flushed SimCommand executed on Simulation AOT context");
    }
    AERO_CHECK(aotSimCtx.loadField(hudTypeId, 1u, simHudVal, loadedUnits) &&
               std::get<std::int64_t>(loadedUnits) == 12,
               "Flushed AOT [SimCommand] updated SimHeap DeployedUnits from 8 to 12");

    std::cout << "[PASS] TestAeroCompilerAotCppAndRealScriptAotClosedLoop\n";
}

} // namespace aero::tests

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    aero::tests::TestAeroVertex24BAndPagedRenderTreeIncrementalUpdate();
    aero::tests::TestSdfRoundedRectQuadAndUberShaders();
    aero::tests::TestEcsHud1000NodesSub80MicrosecondsZeroAllocBenchmark();
    aero::tests::TestTriHostDriversEmbeddedDesktopAndWebCanvas();
    aero::tests::TestAeroCompilerAotCppAndRealScriptAotClosedLoop();

    std::cout << "All AeroGUI 2.0 Phase 3 tests passed 100%!\n";
    return 0;
}
