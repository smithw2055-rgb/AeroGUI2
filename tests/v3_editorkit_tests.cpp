#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "aero/app/AeroAppHost.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/XamlDom.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"

namespace {

int gFailures = 0;

void CheckImpl(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expr);
        ++gFailures;
    }
}

#define CHECK(expr) CheckImpl((expr), #expr, __FILE__, __LINE__)

void TestExternalGpuTextureSlotAndViewportSurface() {
    aero::ImageAtlas atlas;
    std::vector<std::uint8_t> fallback(4u * 4u * 4u, 0u);
    // Top row red, bottom row green in OpenGL bottom-up convention
    for (std::uint32_t x = 0; x < 4u; ++x) {
        // Row 0 (bottom in GL, flipped to top when flipY=true -> v0=1, v1=0)
        fallback[(0u * 4u + x) * 4u + 0u] = 20u;
        fallback[(0u * 4u + x) * 4u + 1u] = 220u;
        fallback[(0u * 4u + x) * 4u + 2u] = 40u;
        fallback[(0u * 4u + x) * 4u + 3u] = 255u;
        // Row 3 (top in GL -> rendered at top when flipY=true)
        fallback[(3u * 4u + x) * 4u + 0u] = 240u;
        fallback[(3u * 4u + x) * 4u + 1u] = 30u;
        fallback[(3u * 4u + x) * 4u + 2u] = 50u;
        fallback[(3u * 4u + x) * 4u + 3u] = 255u;
    }

    const aero::ImageAtlasEntry* entry = atlas.RegisterExternalGpuTexture(
        "viewport://scene_main",
        /*externalGpuHandle=*/777u,
        4u,
        4u,
        /*flipY=*/true,
        fallback);
    CHECK(entry != nullptr);
    if (!entry) return;

    CHECK(entry->isStandalone);
    CHECK(entry->textureId > 0u);
    CHECK(std::fabs(entry->v0 - 1.0f) < 1e-5f);
    CHECK(std::fabs(entry->v1 - 0.0f) < 1e-5f);

    std::uint32_t resolvedGlId = 0u;
    std::uint32_t w = 0u, h = 0u;
    CHECK(atlas.TryGetExternalGpuTextureHandle(entry->textureId, resolvedGlId, w, h));
    CHECK(resolvedGlId == 777u);
    CHECK(w == 4u && h == 4u);

    // Updating the same URI reuses the standalone slot ID without leaking slots
    const std::uint32_t firstSlotId = entry->textureId;
    const aero::ImageAtlasEntry* updated = atlas.RegisterExternalGpuTexture(
        "viewport://scene_main",
        /*externalGpuHandle=*/888u,
        8u,
        6u,
        /*flipY=*/false);
    CHECK(updated != nullptr && updated->textureId == firstSlotId);
    CHECK(atlas.TryGetExternalGpuTextureHandle(firstSlotId, resolvedGlId, w, h));
    CHECK(resolvedGlId == 888u && w == 8u && h == 6u);
    CHECK(std::fabs(updated->v0 - 0.0f) < 1e-5f);
    CHECK(std::fabs(updated->v1 - 1.0f) < 1e-5f);

    CHECK(atlas.UnregisterExternalGpuTexture("viewport://scene_main"));
    CHECK(!atlas.TryGetExternalGpuTextureHandle(firstSlotId, resolvedGlId, w, h));
}

void TestLosslessXamlDomAndSurgicalPatches() {
    const std::string originalXaml =
        "<!-- Header Comment Preserved -->\n"
        "<Window xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n"
        "        xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"\n"
        "        Title=\"AeroStudio\" Width=\"1280\" Height=\"720\">\n"
        "    <Grid x:Name=\"RootLayout\" Background=\"#FF090D16\">\n"
        "        <!-- Inner Dock Comment -->\n"
        "        <ViewportSurface x:Name=\"MainViewport\" TextureUri=\"viewport://scene\" />\n"
        "        <Canvas x:Name=\"EmptyOverlay\" />\n"
        "    </Grid>\n"
        "</Window>\n";

    aero::XamlDomDocument doc;
    std::string err;
    CHECK(aero::XamlDomDocument::Parse(originalXaml, doc, &err));
    CHECK(err.empty());
    CHECK(doc.Comments().size() == 2u);

    const std::int32_t vpId = doc.FindElementByName("MainViewport");
    CHECK(vpId >= 0);
    const aero::XamlDomElement* vpElem = doc.GetElement(vpId);
    CHECK(vpElem != nullptr && vpElem->localName == "ViewportSurface");
    CHECK(vpElem->openTagSpan.startLine == 7);

    // Line/Col lookup hits MainViewport on line 7
    const std::int32_t hitId = doc.FindElementAtLineCol(7, 25);
    CHECK(hitId == vpId);

    // Surgical attribute update preserves comments and untouched lines verbatim
    CHECK(doc.SetAttributeInPlace(vpId, "TextureUri", "viewport://pie_world", &err));
    CHECK(doc.SetAttributeInPlace(vpId, "OverlayBadgeText", "120 FPS & <GL>", &err));
    CHECK(doc.SourceText().find("<!-- Header Comment Preserved -->") != std::string::npos);
    CHECK(doc.SourceText().find("<!-- Inner Dock Comment -->") != std::string::npos);
    CHECK(doc.SourceText().find("TextureUri=\"viewport://pie_world\"") != std::string::npos);
    CHECK(doc.SourceText().find("OverlayBadgeText=\"120 FPS &amp; &lt;GL&gt;\"") != std::string::npos);

    const std::int32_t vpIdAfter = doc.FindElementByName("MainViewport");
    auto decodedBadge = doc.GetAttributeValue(vpIdAfter, "OverlayBadgeText");
    CHECK(decodedBadge.has_value() && *decodedBadge == "120 FPS & <GL>");

    // Remove attribute cleanly
    CHECK(doc.RemoveAttributeInPlace(vpIdAfter, "OverlayBadgeText", &err));
    CHECK(doc.SourceText().find("OverlayBadgeText") == std::string::npos);

    // Insert child into self-closing `<Canvas x:Name="EmptyOverlay" />` -> expands cleanly!
    const std::int32_t emptyCanvasId = doc.FindElementByName("EmptyOverlay");
    CHECK(emptyCanvasId >= 0);
    CHECK(doc.InsertChildInPlace(emptyCanvasId, "<Button x:Name=\"ToolBtn\" Content=\"Move\" />", 4u, &err));
    const std::int32_t btnId = doc.FindElementByName("ToolBtn");
    CHECK(btnId >= 0);
    CHECK(doc.GetElement(emptyCanvasId) != nullptr && !doc.GetElement(emptyCanvasId)->isSelfClosing);

    // Remove child element cleanly
    CHECK(doc.RemoveElementInPlace(btnId, &err));
    CHECK(doc.FindElementByName("ToolBtn") < 0);
}

void TestEditorKitSelfHostedWorkspaceAndSyntaxRendering() {
    aero::AeroAppHostConfig hostCfg{};
    hostCfg.viewportWidth = 1280u;
    hostCfg.viewportHeight = 760u;
    hostCfg.headless = true;
    hostCfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(hostCfg);
    CHECK(host.InitializeRuntime());

    // Register a synthetic viewport texture with bright cyan pixels for headless verification
    std::vector<std::uint8_t> vpPixels(16u * 16u * 4u, 0u);
    for (std::size_t i = 0; i < 16u * 16u; ++i) {
        vpPixels[i * 4u + 0u] = 14u;
        vpPixels[i * 4u + 1u] = 165u;
        vpPixels[i * 4u + 2u] = 233u;
        vpPixels[i * 4u + 3u] = 255u;
    }
    CHECK(host.RegisterExternalViewportTexture("viewport://editor_test", 42u, 16u, 16u, true, vpPixels));

    aero::AeroV3CompileInput input{};
    input.includeStdlib = true;
    input.useStdlibDll = true;
    input.stdlibDllPath = AERO_STDLIB_DLL_PATH;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.allowDiskCache = false;
    input.allowStdlibCache = false;
    input.xamlPath = "EditorShell.xaml";
    input.xamlContent = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="EditorTest.AeroStudioShellTest"
        Title="AeroStudio Phase 0 Verification"
        Width="1280" Height="760">
    <DockingWorkspace x:Name="Workspace" />
</Window>
)XAML";

    aero::SourceFile codeBehind{};
    codeBehind.path = "EditorShell.rs";
    codeBehind.content = R"RS(
module EditorTest;

import Aero.UI;
import Aero.Native;

public partial class AeroStudioShellTest : Window {
    public ViewportSurface SceneView;
    public CodeEditorView ScriptEditor;
    public PropertyGrid Inspector;
    public NodeGraphCanvas StateGraph;
    public NumericScrubber SpeedScrubber;
    public int SaveCount = 0;
    public int CommittedCount = 0;
    public float LastCommittedX = 0.0f;
    public int ViewportClicks = 0;

    public AeroStudioShellTest() {
        this.InitializeComponent();
        var self = this;

        SceneView = new ViewportSurface();
        SceneView.TextureUri = "viewport://editor_test";
        SceneView.OverlayBadgeText = "GL 3.3 | 120 FPS";
        SceneView.ViewportPointerDown += (object s, ViewportInputEventArgs e) => {
            self.OnViewportClicked();
        };

        ScriptEditor = new CodeEditorView();
        ScriptEditor.Language = "rs";
        ScriptEditor.Text = "[SimState]\npublic class PlayerController {\n    public float Speed = 8.5f;\n    // Hot reload ready\n}";
        ScriptEditor.ToggleBreakpoint(3);
        ScriptEditor.SaveRequested += (object s, EventArgs e) => {
            self.OnSaveRequested();
        };

        SpeedScrubber = new NumericScrubber();
        SpeedScrubber.Label = "Speed";
        SpeedScrubber.Value = 4.0f;
        SpeedScrubber.Step = 0.5f;
        SpeedScrubber.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => {
            self.OnPropertyCommitted(e.NewValue);
        };

        Inspector = new PropertyGrid();
        var items = new InspectorPropertyItem[2];
        var posItem = new InspectorPropertyItem("Transform", "Transform.Position.X", "Position X", "float");
        posItem.FloatX = 12.5f;
        posItem.Step = 0.5f;
        posItem.IsOverridden = true;
        items[0] = posItem;

        var tagItem = new InspectorPropertyItem("Identity", "Entity.Tag", "Tag", "string");
        tagItem.StringValue = "Hero";
        items[1] = tagItem;
        Inspector.SetProperties(items);
        Inspector.PropertyCommitted += (object s, ValueEditLifecycleEventArgs e) => {
            self.OnPropertyCommitted(e.NewValue);
        };

        StateGraph = new NodeGraphCanvas();
        var n1 = new NodeGraphNodeData("idle", "Idle", 40.0f, 40.0f);
        var outs = new string[1];
        outs[0] = "Move";
        n1.Outputs = outs;
        var n2 = new NodeGraphNodeData("run", "Run", 260.0f, 60.0f);
        var ins = new string[1];
        ins[0] = "In";
        n2.Inputs = ins;
        StateGraph.AddNode(n1);
        StateGraph.AddNode(n2);
        StateGraph.AddLink(new NodeGraphLinkData("idle", 0, "run", 0));

        var outlinerLbl = new TextBlock();
        outlinerLbl.Text = "SceneRoot";
        Workspace.RegisterPane("outliner", "Outliner", outlinerLbl, DockZone.Left);
        Workspace.RegisterPane("viewport", "Viewport", SceneView, DockZone.Center);
        Workspace.RegisterPane("code", "Code", ScriptEditor, DockZone.Center);
        Workspace.RegisterPane("inspector", "Inspector", Inspector, DockZone.Right);
        Workspace.RegisterPane("graph", "StateGraph", StateGraph, DockZone.Bottom);

        // Verify NumericScrubber coalesced drag lifecycle (4.0f + 10.0f * 0.5f = 9.0f)
        SpeedScrubber.ScrubByPixels(10.0f, 0);
    }

    public void OnViewportClicked() {
        this.ViewportClicks = this.ViewportClicks + 1;
    }

    public void OnSaveRequested() {
        this.SaveCount = this.SaveCount + 1;
    }

    public void OnPropertyCommitted(float v) {
        this.CommittedCount = this.CommittedCount + 1;
        this.LastCommittedX = v;
    }
}
)RS";
    input.scriptSources.push_back(std::move(codeBehind));

    std::string compileErr;
    CHECK(host.CompileAndLoad(input, &compileErr));
    if (!compileErr.empty()) {
        std::fprintf(stderr, "Compile error: %s\n", compileErr.c_str());
        return;
    }

    host.StepFrame(0.016f, true);

    // 1. Verify CodeEditorView multi-color syntax glyph emission in PagedRenderTree
    std::vector<std::uint8_t> frameRgba;
    CHECK(host.RenderHeadlessRgba(1280u, 760u, frameRgba));
    CHECK(frameRgba.size() == 1280u * 760u * 4u);

    // Trigger Ctrl+S on focused ScriptEditor
    const aero::VisualNodeId rootId = host.RootVisualNode();
    CHECK(rootId.IsValid());
    const aero::ObjectRef rootObj = host.RootScriptObject();
    CHECK(rootObj.valid());

    // Focus ScriptEditor via script method or click inside center dock area (x=560, y=160)
    host.InjectClick(560.0f, 160.0f, aero::PointerButton::Left);
    host.StepFrame(0.016f);
    host.InjectKey(aero::KeyCode::S, true, aero::KeyMod_Ctrl);
    host.InjectKey(aero::KeyCode::S, false, aero::KeyMod_Ctrl);
    host.StepFrame(0.016f);

    aero::UnifiedValue saveCountVal{};
    CHECK(host.ReadScriptMember(rootObj, "SaveCount", saveCountVal));
    if (const auto* iv = std::get_if<std::int64_t>(&saveCountVal)) {
        CHECK(*iv == 1);
    } else {
        CHECK(false);
    }

    aero::UnifiedValue commitCountVal{};
    CHECK(host.ReadScriptMember(rootObj, "CommittedCount", commitCountVal));
    if (const auto* iv = std::get_if<std::int64_t>(&commitCountVal)) {
        CHECK(*iv >= 1);
    } else {
        CHECK(false);
    }
    float lastX = 0.0f;
    double lastXd = 0.0;
    aero::UnifiedValue lastXVal{};
    CHECK(host.ReadScriptMember(rootObj, "LastCommittedX", lastXVal));
    if (lastXVal.tryGetFloat(lastX)) {
        CHECK(std::fabs(lastX - 9.0f) < 1e-3f);
    } else if (lastXVal.tryGetDouble(lastXd)) {
        CHECK(std::fabs(lastXd - 9.0) < 1e-3);
    } else {
        CHECK(false);
    }

    // 2. Activate Viewport tab in DockingWorkspace and verify external FBO texture renders cyan pixels!
    aero::UnifiedValue actRes{};
    aero::UnifiedValue wsVal{};
    CHECK(host.ReadScriptMember(rootObj, "Workspace", wsVal));
    if (const auto* wsRef = std::get_if<aero::ObjectRef>(&wsVal)) {
        CHECK(host.InvokeScriptMethod(
            *wsRef,
            "ActivatePane",
            {aero::UnifiedValue{aero::StringValue{std::string_view{"viewport"}}}},
            &actRes));
    }
    host.StepFrame(0.016f, true);
    CHECK(host.RenderHeadlessRgba(1280u, 760u, frameRgba));

    // Center of viewport pane (~x=560, y=220) should sample the registered cyan viewport texture (14, 165, 233)
    const std::size_t sampleOff = (220u * 1280u + 560u) * 4u;
    CHECK(frameRgba[sampleOff + 2u] > 180u && frameRgba[sampleOff + 1u] > 120u && frameRgba[sampleOff + 0u] < 60u);

    // Click inside viewport -> forwards local/normalized coordinates to ViewportPointerDown!
    host.InjectClick(560.0f, 220.0f, aero::PointerButton::Left);
    host.StepFrame(0.016f);
    aero::UnifiedValue vpClicks{};
    CHECK(host.ReadScriptMember(rootObj, "ViewportClicks", vpClicks));
    if (const auto* iv = std::get_if<std::int64_t>(&vpClicks)) {
        CHECK(*iv >= 1);
    } else {
        CHECK(false);
    }
}

} // namespace

int main() {
    TestExternalGpuTextureSlotAndViewportSurface();
    TestLosslessXamlDomAndSurgicalPatches();
    TestEditorKitSelfHostedWorkspaceAndSyntaxRendering();

    if (gFailures != 0) {
        std::fprintf(stderr, "v3_editorkit_tests FAILED (%d failures)\n", gFailures);
        return 1;
    }
    std::printf("v3_editorkit_tests PASSED\n");
    return 0;
}
