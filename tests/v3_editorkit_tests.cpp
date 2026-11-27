#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#define AERO_TEST_WITH_APP_HOST 1
#include "common/AeroTestHarness.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/XamlDom.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"

namespace {

int gFailures = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            aero::test::ReportFailure(gFailures, #expr, __FILE__, __LINE__); \
        }                                                                   \
    } while (false)

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

    // 3. Verify Window + DockingWorkspace resize reflow when host viewport changes
    aero::ObjectRef wsObj{};
    if (const auto* wsRef = std::get_if<aero::ObjectRef>(&wsVal)) {
        wsObj = *wsRef;
    }
    aero::UnifiedValue wsHandleVal{};
    aero::VisualNodeId wsNodeId{};
    if (wsObj.valid() && host.ReadScriptMember(wsObj, "__nativeHandle", wsHandleVal)) {
        aero::NativeHandle wsNh{};
        if (wsHandleVal.tryGetNativeHandle(wsNh)) {
            wsNodeId = host.World().ResolveHandle(wsNh);
        }
    }
    CHECK(wsNodeId.IsValid());

    host.InjectResize(1600u, 960u);
    host.StepFrame(0.016f, true);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(rootId, aero::VisualLayoutRectQuery::Width) - 1600.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(rootId, aero::VisualLayoutRectQuery::Height) - 960.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(wsNodeId, aero::VisualLayoutRectQuery::Width) - 1600.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(wsNodeId, aero::VisualLayoutRectQuery::Height) - 960.0f) < 1.0f);

    host.InjectResize(900u, 600u);
    host.StepFrame(0.016f, true);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(rootId, aero::VisualLayoutRectQuery::Width) - 900.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(rootId, aero::VisualLayoutRectQuery::Height) - 600.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(wsNodeId, aero::VisualLayoutRectQuery::Width) - 900.0f) < 1.0f);
    CHECK(std::fabs(host.World().GetLayoutRectScalar(wsNodeId, aero::VisualLayoutRectQuery::Height) - 600.0f) < 1.0f);

    // 4. Verify CodeEditorView mouse drag multi-line selection and Backspace deletion
    if (wsObj.valid()) {
        CHECK(host.InvokeScriptMethod(
            wsObj,
            "ActivatePane",
            {aero::UnifiedValue{aero::StringValue{std::string_view{"code"}}}},
            &actRes));
    }
    host.StepFrame(0.016f, true);

    aero::UnifiedValue edVal{};
    aero::ObjectRef edObj{};
    CHECK(host.ReadScriptMember(rootObj, "ScriptEditor", edVal));
    if (const auto* edRef = std::get_if<aero::ObjectRef>(&edVal)) {
        edObj = *edRef;
    }
    CHECK(edObj.valid());
    aero::UnifiedValue edHandleVal{};
    aero::VisualNodeId edNodeId{};
    if (edObj.valid() && host.ReadScriptMember(edObj, "__nativeHandle", edHandleVal)) {
        aero::NativeHandle edNh{};
        if (edHandleVal.tryGetNativeHandle(edNh)) {
            edNodeId = host.World().ResolveHandle(edNh);
        }
    }
    CHECK(edNodeId.IsValid());

    const float edX = host.World().GetLayoutRectScalar(edNodeId, aero::VisualLayoutRectQuery::X);
    const float edY = host.World().GetLayoutRectScalar(edNodeId, aero::VisualLayoutRectQuery::Y);
    const float edW = host.World().GetLayoutRectScalar(edNodeId, aero::VisualLayoutRectQuery::Width);
    const float edH = host.World().GetLayoutRectScalar(edNodeId, aero::VisualLayoutRectQuery::Height);
    CHECK(edW > 150.0f && edH > 100.0f);

    const float dragStartX = edX + 76.0f;
    const float dragStartY = edY + 12.0f;
    const float dragEndX = edX + 140.0f;
    const float dragEndY = edY + 52.0f;
    host.InjectPointerMove(dragStartX, dragStartY);
    host.InjectPointerButton(dragStartX, dragStartY, aero::PointerButton::Left, true);
    host.InjectPointerMove(dragEndX, dragEndY);
    host.InjectPointerButton(dragEndX, dragEndY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);

    aero::UnifiedValue hasSelVal{};
    CHECK(host.ReadScriptMember(edObj, "HasSelection", hasSelVal));
    if (const auto* b = std::get_if<bool>(&hasSelVal)) {
        CHECK(*b);
    } else {
        CHECK(false);
    }

    aero::UnifiedValue selStartLineVal{};
    aero::UnifiedValue selEndLineVal{};
    CHECK(host.ReadScriptMember(edObj, "SelectionStartLine", selStartLineVal));
    CHECK(host.ReadScriptMember(edObj, "SelectionEndLine", selEndLineVal));
    if (const auto* sl = std::get_if<std::int64_t>(&selStartLineVal)) {
        if (const auto* el = std::get_if<std::int64_t>(&selEndLineVal)) {
            CHECK(*sl == 0 && *el >= 2);
        } else {
            CHECK(false);
        }
    } else {
        CHECK(false);
    }

    aero::UnifiedValue selTextVal{};
    CHECK(host.InvokeScriptMethod(edObj, "GetSelectedText", {}, &selTextVal));
    std::string selText;
    if (const auto* sp = std::get_if<std::string>(&selTextVal)) {
        selText = *sp;
    } else if (const auto* sref = std::get_if<aero::rs_rt::ObjectRef>(&selTextVal)) {
        if (auto sv = host.Domains().stringViewAcrossDomains(*sref)) {
            selText.assign(sv->data(), sv->size());
        }
    }
    CHECK(selText.find('\n') != std::string::npos);

    aero::UnifiedValue prevLinesVal{};
    CHECK(host.InvokeScriptMethod(edObj, "LineCount", {}, &prevLinesVal));
    std::int64_t prevLines = 0;
    if (const auto* iv = std::get_if<std::int64_t>(&prevLinesVal)) {
        prevLines = *iv;
    }
    CHECK(prevLines == 5);

    host.InjectKey(aero::KeyCode::Backspace, true);
    host.InjectKey(aero::KeyCode::Backspace, false);
    host.StepFrame(0.016f, true);

    aero::UnifiedValue afterLinesVal{};
    CHECK(host.InvokeScriptMethod(edObj, "LineCount", {}, &afterLinesVal));
    if (const auto* iv = std::get_if<std::int64_t>(&afterLinesVal)) {
        CHECK(*iv < prevLines);
    } else {
        CHECK(false);
    }
}

std::string ReadRsString(aero::AeroAppHost& host, const aero::UnifiedValue& val) {
    if (const auto* sp = std::get_if<std::string>(&val)) {
        return *sp;
    }
    if (const auto* sref = std::get_if<aero::rs_rt::ObjectRef>(&val)) {
        if (auto sv = host.Domains().stringViewAcrossDomains(*sref)) {
            return std::string(sv->data(), sv->size());
        }
    }
    return {};
}

bool ReadRsBool(const aero::UnifiedValue& val, bool fallback = false) {
    if (const auto* b = std::get_if<bool>(&val)) {
        return *b;
    }
    return fallback;
}

std::int64_t ReadRsInt(const aero::UnifiedValue& val, std::int64_t fallback = 0) {
    if (const auto* iv = std::get_if<std::int64_t>(&val)) {
        return *iv;
    }
    return fallback;
}

float ReadRsFloat(const aero::UnifiedValue& val, float fallback = 0.0f) {
    float f = fallback;
    double d = fallback;
    if (val.tryGetFloat(f)) return f;
    if (val.tryGetDouble(d)) return static_cast<float>(d);
    return fallback;
}

void TestPropertyGrid20IncrementalFocusAndEditors() {
    aero::AeroAppHostConfig hostCfg{};
    hostCfg.viewportWidth = 1000u;
    hostCfg.viewportHeight = 820u;
    hostCfg.headless = true;
    hostCfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(hostCfg);
    CHECK(host.InitializeRuntime());

    aero::AeroV3CompileInput input{};
    input.includeStdlib = true;
    input.useStdlibDll = true;
    input.stdlibDllPath = AERO_STDLIB_DLL_PATH;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.allowDiskCache = false;
    input.allowStdlibCache = false;
    input.xamlPath = "PropGrid20.xaml";
    input.xamlContent = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="EditorTest.PropertyGrid20Harness"
        Title="PropertyGrid 2.0 Test"
        Width="1000" Height="820">
    <Grid>
        <PropertyGrid x:Name="Grid20" />
    </Grid>
</Window>
)XAML";

    aero::SourceFile codeBehind{};
    codeBehind.path = "PropGrid20.rs";
    codeBehind.content = R"RS(
module EditorTest;

import Aero.UI;
import Aero.Native;

public partial class PropertyGrid20Harness : Window {
    public int EditCount = 0;
    public int CommitCount = 0;
    public string LastCommitPath = "";
    public float LastCommitVal = 0.0f;
    public int LastCommitInt = 0;
    public string LastCommitText = "";
    public float LastCommitColorR = 0.0f;
    public string LastCardAction = "";
    public string LastCardKey = "";
    public string LastAssetAction = "";
    public string LastAssetFilter = "";
    public string LastAssetPath = "";
    public Button CustomBtn;
    public handle TagEditorHandle;
    public handle ScrubberEditorHandle;

    public PropertyGrid20Harness() {
        this.InitializeComponent();
        var self = this;
        this.CustomBtn = new Button();
        var ctxt = new TextBlock();
        ctxt.Text = "CustomWidget";
        this.CustomBtn.Content = ctxt;

        this.Grid20.PropertyEdited += (object s, ValueEditLifecycleEventArgs e) => {
            self.EditCount = self.EditCount + 1;
        };
        this.Grid20.PropertyCommitted += (object s, ValueEditLifecycleEventArgs e) => {
            self.CommitCount = self.CommitCount + 1;
            self.LastCommitPath = e.PropertyPath;
            self.LastCommitVal = e.NewValue;
            self.LastCommitInt = e.IntValue;
            self.LastCommitText = e.NewText;
            self.LastCommitColorR = e.ColorR;
        };
        this.Grid20.PropertyCardActionRequested += (object s, PropertyCardActionEventArgs e) => {
            self.LastCardAction = e.Action;
            self.LastCardKey = e.GroupKey;
        };
        this.Grid20.PropertyAssetPickRequested += (object s, PropertyAssetActionEventArgs e) => {
            self.LastAssetAction = e.Action;
            self.LastAssetFilter = e.AssetFilter;
            self.LastAssetPath = e.AssetPath;
        };

        this.LoadInitialItems();
    }

    public void LoadInitialItems() {
        var items = new InspectorPropertyItem[2];
        var p0 = new InspectorPropertyItem("Transform", "Transform.Position.X", "Pos X", "float");
        p0.GroupKey = "comp_transform";
        p0.FloatX = 10.0f;
        p0.Step = 0.5f;
        items[0] = p0;

        var p1 = new InspectorPropertyItem("Identity", "Entity.Tag", "Tag", "string");
        p1.GroupKey = "comp_identity";
        p1.StringValue = "Hero";
        items[1] = p1;

        this.Grid20.SetProperties(items);
        UIElement tagEd = this.Grid20.GetRowEditor("Entity.Tag");
        if (tagEd != null) { this.TagEditorHandle = tagEd.__nativeHandle; }
        UIElement scEd = this.Grid20.GetRowEditor("Transform.Position.X");
        if (scEd != null) { this.ScrubberEditorHandle = scEd.__nativeHandle; }
    }

    public bool FocusTagEditorAndType(string txt) {
        UIElement ed = this.Grid20.GetRowEditor("Entity.Tag");
        if (!(ed is TextBox)) { return false; }
        var tb = ed as TextBox;
        tb.Focus();
        tb.Text = txt;
        return tb.IsFocused;
    }

    public bool VerifyTagEditorPreserved(string expectedText) {
        UIElement ed = this.Grid20.GetRowEditor("Entity.Tag");
        if (!(ed is TextBox)) { return false; }
        var tb = ed as TextBox;
        return tb.__nativeHandle == this.TagEditorHandle && tb.IsFocused && tb.Text == expectedText;
    }

    public void PushIncrementalUpdateWhileFocused() {
        var items = new InspectorPropertyItem[3];
        var p0 = new InspectorPropertyItem("Transform", "Transform.Position.X", "Pos X", "float");
        p0.GroupKey = "comp_transform";
        p0.FloatX = 99.5f;
        items[0] = p0;

        var pNew = new InspectorPropertyItem("Transform", "Transform.Position.Y", "Pos Y", "float");
        pNew.GroupKey = "comp_transform";
        pNew.FloatX = 22.0f;
        items[1] = pNew;

        var p1 = new InspectorPropertyItem("Identity", "Entity.Tag", "Tag", "string");
        p1.GroupKey = "comp_identity";
        p1.StringValue = "StaleFromModel";
        items[2] = p1;

        this.Grid20.SetProperties(items);
    }

    public float GetPosXScrubberValue() {
        UIElement ed = this.Grid20.GetRowEditor("Transform.Position.X");
        if (ed is NumericScrubber) { return (ed as NumericScrubber).Value; }
        return -9999.0f;
    }

    public bool ExerciseScrubberInlineFocusAndBlurCommit() {
        UIElement ed = this.Grid20.GetRowEditor("Transform.Position.X");
        if (!(ed is NumericScrubber)) { return false; }
        var sc = ed as NumericScrubber;
        sc.Value = 12.0f;
        // Simulate click -> inline edit via Scrubber inline box
        TextBox ib = sc.InlineTextBox();
        sc.ScrubByPixels(0.0f, 0);
        // Enter inline edit by sending down/up on scrubber handle or checking direct inline focus
        ib.Focus();
        return ib.IsFocused || sc.IsFocused;
    }

    public void LoadAllNewEditors() {
        var items = new InspectorPropertyItem[11];

        var pFloatMixed = new InspectorPropertyItem("Mixed", "Multi.Speed", "Speed", "float");
        pFloatMixed.GroupKey = "card_mixed";
        pFloatMixed.IsMixedValue = true;
        pFloatMixed.FloatX = 5.0f;
        items[0] = pFloatMixed;

        var pStrMixed = new InspectorPropertyItem("Mixed", "Multi.Label", "Label", "string");
        pStrMixed.GroupKey = "card_mixed";
        pStrMixed.IsMixedValue = true;
        items[1] = pStrMixed;

        var pColor = new InspectorPropertyItem("Material", "Mat.Tint", "Tint", "color");
        pColor.GroupKey = "card_material";
        pColor.HeaderText = "Surface Optics";
        pColor.ColorR = 2.5f;
        pColor.ColorG = 0.5f;
        pColor.ColorB = 0.25f;
        pColor.ColorA = 1.0f;
        pColor.ColorHdr = true;
        pColor.CanRemoveCard = true;
        items[2] = pColor;

        var pAsset = new InspectorPropertyItem("Material", "Mat.Albedo", "Albedo Map", "asset");
        pAsset.GroupKey = "card_material";
        pAsset.AssetFilter = "*.amat";
        pAsset.StringValue = "materials/default.amat";
        items[3] = pAsset;

        var pEnum = new InspectorPropertyItem("Physics", "Body.MotionType", "Motion", "enum");
        pEnum.GroupKey = "card_physics";
        var opts = new string[3];
        opts[0] = "Static=0";
        opts[1] = "Dynamic=1";
        opts[2] = "Kinematic=2";
        pEnum.EnumOptions = opts;
        pEnum.IntValue = 1;
        items[4] = pEnum;

        var pVec2 = new InspectorPropertyItem("Physics", "Body.Drag", "Drag XY", "vec2");
        pVec2.GroupKey = "card_physics";
        pVec2.FloatX = 1.5f;
        pVec2.FloatY = 2.5f;
        items[5] = pVec2;

        var pFRange = new InspectorPropertyItem("Physics", "Body.Friction", "Friction", "float_range");
        pFRange.GroupKey = "card_physics";
        pFRange.MinValue = 0.0f;
        pFRange.MaxValue = 1.0f;
        pFRange.Step = 0.05f;
        pFRange.FloatX = 0.4f;
        items[6] = pFRange;

        var pIRange = new InspectorPropertyItem("Physics", "Body.SolverIter", "Solver Iter", "int_range");
        pIRange.GroupKey = "card_physics";
        pIRange.MinValue = 1.0f;
        pIRange.MaxValue = 16.0f;
        pIRange.Step = 1.0f;
        pIRange.IntValue = 6;
        items[7] = pIRange;

        var pMask = new InspectorPropertyItem("Physics", "Body.Layers", "Layers", "layermask");
        pMask.GroupKey = "card_physics";
        pMask.IntValue = 0;
        items[8] = pMask;

        var pHost = new InspectorPropertyItem("Custom", "Plugin.Custom", "Plugin UI", "custom_host");
        pHost.GroupKey = "card_custom";
        pHost.CustomHostId = "my_custom_slot";
        items[9] = pHost;

        var pVec3Mixed = new InspectorPropertyItem("Mixed", "Multi.Scale", "Scale", "vec3");
        pVec3Mixed.GroupKey = "card_mixed";
        pVec3Mixed.IsMixedValue = true;
        items[10] = pVec3Mixed;

        this.Grid20.SetProperties(items);
        this.Grid20.SetCustomHostContent("my_custom_slot", this.CustomBtn);
    }

    public bool CheckMixedDisplays() {
        UIElement eF = this.Grid20.GetRowEditor("Multi.Speed");
        UIElement eS = this.Grid20.GetRowEditor("Multi.Label");
        UIElement eV = this.Grid20.GetRowEditor("Multi.Scale");
        if (!(eF is NumericScrubber) || !(eS is TextBox) || !(eV is Vector3Field)) { return false; }
        var sc = eF as NumericScrubber;
        var tb = eS as TextBox;
        var v3 = eV as Vector3Field;
        return sc.DisplayedValueText() == "—" && tb.PlaceholderText == "—" && tb.Text == "" && v3.IsMixed;
    }

    public bool ScrubMixedFloatAndVerifyCleared() {
        UIElement eF = this.Grid20.GetRowEditor("Multi.Speed");
        if (!(eF is NumericScrubber)) { return false; }
        var sc = eF as NumericScrubber;
        sc.ScrubByPixels(5.0f, 0);
        return !sc.IsMixed && this.LastCommitPath == "Multi.Speed";
    }

    public bool ExerciseColorEnumVec2Range() {
        // 1. Color HDR + Hex commit
        UIElement eCol = this.Grid20.GetRowEditor("Mat.Tint");
        if (!(eCol is ColorPicker)) { return false; }
        var cp = eCol as ColorPicker;
        if (!cp.IsHdr || !cp.FormatColorPayload().StartsWith("2.50")) { return false; }
        cp.SetColor(1.5f, 0.25f, 0.5f, 1.0f, true);
        if (this.LastCommitPath != "Mat.Tint" || this.LastCommitColorR < 1.4f) { return false; }

        // Check HeaderText border above Mat.Tint row is Visible
        UIElement rowGrid = cp.Parent;
        UIElement rowBorder = rowGrid != null ? rowGrid.Parent : null;
        UIElement wrapper = rowBorder != null ? rowBorder.Parent : null;
        if (!(wrapper is StackPanel)) { return false; }
        UIElement[] wch = (wrapper as StackPanel).Children();
        if (wch.Length < 2 || wch[0].Visibility != Visibility.Visible) { return false; }

        // 2. Asset drop
        this.Grid20.AssignAssetPath("Mat.Albedo", "textures/wood.png");
        if (this.LastAssetAction != "Drop" || this.LastCommitPath != "Mat.Albedo" || this.LastCommitText != "textures/wood.png") {
            return false;
        }

        // 3. Enum ComboBox
        UIElement eEnum = this.Grid20.GetRowEditor("Body.MotionType");
        if (!(eEnum is ComboBox)) { return false; }
        var cb = eEnum as ComboBox;
        cb.SelectedIndex = 2;
        if (this.LastCommitPath != "Body.MotionType" || this.LastCommitInt != 2 || this.LastCommitText != "Kinematic") {
            return false;
        }

        // 4. Vec2
        UIElement eV2 = this.Grid20.GetRowEditor("Body.Drag");
        if (!(eV2 is Vector2Field)) { return false; }
        var v2 = eV2 as Vector2Field;
        v2.ScrubberX().ScrubByPixels(10.0f, 0);
        if (this.LastCommitPath != "Body.Drag") { return false; }

        // 5. FloatRange & IntRange clamping + sync
        UIElement eFR = this.Grid20.GetRowEditor("Body.Friction");
        if (!(eFR is Grid)) { return false; }
        UIElement[] frCh = (eFR as Grid).Children();
        if (frCh.Length < 2 || !(frCh[0] is Slider) || !(frCh[1] is NumericScrubber)) { return false; }
        var sl = frCh[0] as Slider;
        var rsc = frCh[1] as NumericScrubber;
        rsc.ScrubByPixels(100.0f, 0);
        if (sl.Value < 0.99f || rsc.Value > 1.001f) { return false; }

        UIElement eIR = this.Grid20.GetRowEditor("Body.SolverIter");
        if (!(eIR is Grid)) { return false; }
        UIElement[] irCh = (eIR as Grid).Children();
        if (irCh.Length < 2 || !(irCh[0] is Slider) || !(irCh[1] is NumericScrubber)) { return false; }
        (irCh[0] as Slider).Value = 12.0f;
        if ((irCh[1] as NumericScrubber).Value < 11.5f) { return false; }

        return true;
    }

    public handle GetAssetButtonHandle(int btnIndex) {
        UIElement eAsset = this.Grid20.GetRowEditor("Mat.Albedo");
        if (!(eAsset is Grid)) { return ParseUtil.EmptyHandle(); }
        UIElement[] ach = (eAsset as Grid).Children();
        if (btnIndex < 0 || btnIndex >= ach.Length) { return ParseUtil.EmptyHandle(); }
        return ach[btnIndex].__nativeHandle;
    }

    public handle GetLayerMaskBitButtonHandle(int bitIdx) {
        UIElement eMask = this.Grid20.GetRowEditor("Body.Layers");
        if (!(eMask is WrapPanel)) { return ParseUtil.EmptyHandle(); }
        UIElement[] mCh = (eMask as WrapPanel).Children();
        if (bitIdx < 0 || bitIdx >= mCh.Length) { return ParseUtil.EmptyHandle(); }
        return mCh[bitIdx].__nativeHandle;
    }

    public handle GetMaterialCardActionButtonHandle(int btnIndex) {
        UIElement eCol = this.Grid20.GetRowEditor("Mat.Tint");
        if (eCol == null) { return ParseUtil.EmptyHandle(); }
        UIElement wrapper = eCol.Parent.Parent.Parent;
        UIElement body = wrapper != null ? wrapper.Parent : null;
        UIElement cardStack = body != null ? body.Parent : null;
        if (!(cardStack is StackPanel)) { return ParseUtil.EmptyHandle(); }
        UIElement hdrBorder = (cardStack as StackPanel).ChildAt(0);
        if (!(hdrBorder is Border)) { return ParseUtil.EmptyHandle(); }
        UIElement hdrGrid = (hdrBorder as Border).Child;
        if (!(hdrGrid is Grid)) { return ParseUtil.EmptyHandle(); }
        UIElement actions = (hdrGrid as Grid).ChildAt(2);
        if (!(actions is StackPanel)) { return ParseUtil.EmptyHandle(); }
        UIElement[] btns = (actions as StackPanel).Children();
        if (btnIndex < 0 || btnIndex >= btns.Length) { return ParseUtil.EmptyHandle(); }
        return btns[btnIndex].__nativeHandle;
    }

    public bool CheckCustomHostSurvivalAfterRefresh() {
        this.LoadAllNewEditors();
        UIElement container = this.Grid20.GetCustomHostContainer("my_custom_slot");
        if (!(container is Border)) { return false; }
        var b = container as Border;
        if (b.Child == null) { return false; }
        return b.Child.__nativeHandle == this.CustomBtn.__nativeHandle;
    }

    public void LoadLayerMaskValue(int val) {
        var items = new InspectorPropertyItem[1];
        var pMask = new InspectorPropertyItem("Physics", "Body.Layers", "Layers", "layermask");
        pMask.GroupKey = "card_physics";
        pMask.IntValue = val;
        items[0] = pMask;
        this.Grid20.SetProperties(items);
    }

    public bool IsLayerMaskBitActive(int bitIdx) {
        UIElement eMask = this.Grid20.GetRowEditor("Body.Layers");
        if (!(eMask is WrapPanel)) { return false; }
        UIElement[] mCh = (eMask as WrapPanel).Children();
        if (bitIdx < 0 || bitIdx >= mCh.Length) { return false; }
        if (!(mCh[bitIdx] is Button)) { return false; }
        var b = mCh[bitIdx] as Button;
        if (!(b.Background is SolidColorBrush)) { return false; }
        uint col = (b.Background as SolidColorBrush).ColorRgba;
        return col == ParseUtil.ParseColor("#FF0284C7");
    }

    public void LoadLayerMaskValue16(int val) {
        var items = new InspectorPropertyItem[1];
        var pMask = new InspectorPropertyItem("Physics", "Body.Layers", "Layers", "layermask");
        pMask.GroupKey = "card_physics";
        pMask.IntValue = val;
        var opts = new string[16];
        for (int i = 0; i < 16; i += 1) {
            opts[i] = "L" + EditorMath.IntToStr(i);
        }
        pMask.EnumOptions = opts;
        items[0] = pMask;
        this.Grid20.SetProperties(items);
    }

    public int ActiveLayerMaskBitCount(int count) {
        int active = 0;
        for (int i = 0; i < count; i += 1) {
            if (this.IsLayerMaskBitActive(i)) { active = active + 1; }
        }
        return active;
    }

    public string LayerMaskSummaryText() {
        UIElement eMask = this.Grid20.GetRowEditor("Body.Layers");
        if (!(eMask is WrapPanel)) { return ""; }
        UIElement[] mCh = (eMask as WrapPanel).Children();
        if (mCh.Length == 0) { return ""; }
        UIElement last = mCh[mCh.Length - 1];
        if (last is TextBlock) { return (last as TextBlock).Text; }
        return "";
    }

    public int SetNullFilterAndCountRows() {
        this.Grid20.FilterText = null;
        return this.Grid20.VisibleRowCount();
    }
}
)RS";
    input.scriptSources.push_back(std::move(codeBehind));

    std::string compileErr;
    CHECK(host.CompileAndLoad(input, &compileErr));
    if (!compileErr.empty()) {
        std::fprintf(stderr, "PropGrid20 compile error: %s\n", compileErr.c_str());
        return;
    }
    host.StepFrame(0.016f, true);

    const aero::ObjectRef rootObj = host.RootScriptObject();
    CHECK(rootObj.valid());
    aero::UnifiedValue gridVal{};
    CHECK(host.ReadScriptMember(rootObj, "Grid20", gridVal));
    aero::ObjectRef gridObj{};
    if (const auto* r = std::get_if<aero::ObjectRef>(&gridVal)) { gridObj = *r; }
    CHECK(gridObj.valid());

    // 1. Keyed incremental refresh preserves focus and typed text
    aero::UnifiedValue res{};
    CHECK(host.InvokeScriptMethod(
        rootObj,
        "FocusTagEditorAndType",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"Hero_Edit"}}}},
        &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);

    CHECK(host.InvokeScriptMethod(rootObj, "PushIncrementalUpdateWhileFocused", {}, &res));
    host.StepFrame(0.016f, true);

    CHECK(host.InvokeScriptMethod(
        rootObj,
        "VerifyTagEditorPreserved",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"Hero_Edit"}}}},
        &res));
    CHECK(ReadRsBool(res));

    CHECK(host.InvokeScriptMethod(rootObj, "GetPosXScrubberValue", {}, &res));
    CHECK(std::fabs(ReadRsFloat(res) - 99.5f) < 0.05f);
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 3);

    // 2. Search filter preserves focused matching row when filtered and cleared
    CHECK(host.InvokeScriptMethod(
        gridObj,
        "set_FilterText",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"Tag"}}}},
        &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(
        rootObj,
        "VerifyTagEditorPreserved",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"Hero_Edit"}}}},
        &res));
    CHECK(ReadRsBool(res));

    CHECK(host.InvokeScriptMethod(
        gridObj,
        "set_FilterText",
        {aero::UnifiedValue{aero::StringValue{std::string_view{""}}}},
        &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 3);

    // 3. NumericScrubber pointer click -> inline edit -> click away LostFocus commits!
    aero::UnifiedValue scHandleVal{};
    CHECK(host.ReadScriptMember(rootObj, "ScrubberEditorHandle", scHandleVal));
    aero::NativeHandle scNh{};
    CHECK(scHandleVal.tryGetNativeHandle(scNh));
    const aero::VisualNodeId scNodeId = host.World().ResolveHandle(scNh);
    CHECK(scNodeId.IsValid());
    const float scX = host.World().GetLayoutRectScalar(scNodeId, aero::VisualLayoutRectQuery::ViewportX);
    const float scY = host.World().GetLayoutRectScalar(scNodeId, aero::VisualLayoutRectQuery::ViewportY);
    const float scW = host.World().GetLayoutRectScalar(scNodeId, aero::VisualLayoutRectQuery::Width);
    const float scH = host.World().GetLayoutRectScalar(scNodeId, aero::VisualLayoutRectQuery::Height);
    CHECK(scW > 20.0f && scH > 10.0f);

    host.InjectClick(scX + scW * 0.5f, scY + scH * 0.5f, aero::PointerButton::Left);
    host.StepFrame(0.016f, true);
    host.InjectKey(aero::KeyCode::A, true, aero::KeyMod_Ctrl);
    host.InjectKey(aero::KeyCode::A, false, aero::KeyMod_Ctrl);
    host.InjectTextInput("42");
    host.StepFrame(0.016f, true);
    // Click PropertyGrid searchBox at top (Focusable TextBox) to trigger LostFocus commit
    host.InjectClick(200.0f, 18.0f, aero::PointerButton::Left);
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "GetPosXScrubberValue", {}, &res));
    CHECK(std::fabs(ReadRsFloat(res) - 42.0f) < 0.1f);

    // 4. Load all new editor types + mixed values + custom host + cards
    CHECK(host.InvokeScriptMethod(rootObj, "LoadAllNewEditors", {}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 11);

    CHECK(host.InvokeScriptMethod(rootObj, "CheckMixedDisplays", {}, &res));
    CHECK(ReadRsBool(res));
    CHECK(host.InvokeScriptMethod(rootObj, "ScrubMixedFloatAndVerifyCleared", {}, &res));
    CHECK(ReadRsBool(res));

    CHECK(host.InvokeScriptMethod(rootObj, "ExerciseColorEnumVec2Range", {}, &res));
    CHECK(ReadRsBool(res));

    auto clickNativeHandle = [&](const aero::UnifiedValue& hVal) {
        aero::NativeHandle nh{};
        CHECK(hVal.tryGetNativeHandle(nh));
        const aero::VisualNodeId nid = host.World().ResolveHandle(nh);
        CHECK(nid.IsValid());
        const float cx = host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::ViewportX) +
                         host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::Width) * 0.5f;
        const float cy = host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::ViewportY) +
                         host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::Height) * 0.5f;
        host.InjectClick(cx, cy, aero::PointerButton::Left);
        host.StepFrame(0.016f, true);
    };

    // Asset Pick & Clear buttons via real UI clicks
    CHECK(host.InvokeScriptMethod(rootObj, "GetAssetButtonHandle", {aero::UnifiedValue{std::int64_t{1}}}, &res));
    clickNativeHandle(res);
    CHECK(host.ReadScriptMember(rootObj, "LastAssetAction", res));
    CHECK(ReadRsString(host, res) == "Pick");
    CHECK(host.ReadScriptMember(rootObj, "LastAssetFilter", res));
    CHECK(ReadRsString(host, res) == "*.amat");

    CHECK(host.InvokeScriptMethod(rootObj, "GetAssetButtonHandle", {aero::UnifiedValue{std::int64_t{2}}}, &res));
    clickNativeHandle(res);
    CHECK(host.ReadScriptMember(rootObj, "LastAssetAction", res));
    CHECK(ReadRsString(host, res) == "Clear");
    CHECK(host.ReadScriptMember(rootObj, "LastCommitText", res));
    CHECK(ReadRsString(host, res).empty());

    // LayerMask bit 0 and bit 2 clicks -> mask == 5
    CHECK(host.InvokeScriptMethod(rootObj, "GetLayerMaskBitButtonHandle", {aero::UnifiedValue{std::int64_t{0}}}, &res));
    clickNativeHandle(res);
    CHECK(host.InvokeScriptMethod(rootObj, "GetLayerMaskBitButtonHandle", {aero::UnifiedValue{std::int64_t{2}}}, &res));
    clickNativeHandle(res);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitPath", res));
    CHECK(ReadRsString(host, res) == "Body.Layers");
    CHECK(host.ReadScriptMember(rootObj, "LastCommitInt", res));
    CHECK(ReadRsInt(res) == 5);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitText", res));
    CHECK(ReadRsString(host, res) == "5");

    // Card Reset / Copy / Paste / Remove buttons via real UI clicks
    const char* cardActions[4] = {"Reset", "Copy", "Paste", "Remove"};
    for (int ai = 0; ai < 4; ++ai) {
        CHECK(host.InvokeScriptMethod(rootObj, "GetMaterialCardActionButtonHandle", {aero::UnifiedValue{std::int64_t{ai}}}, &res));
        clickNativeHandle(res);
        CHECK(host.ReadScriptMember(rootObj, "LastCardAction", res));
        CHECK(ReadRsString(host, res) == cardActions[ai]);
        CHECK(host.ReadScriptMember(rootObj, "LastCardKey", res));
        CHECK(ReadRsString(host, res) == "card_material");
    }

    // Check card collapse persistence across SetProperties
    CHECK(host.InvokeScriptMethod(
        gridObj,
        "SetCardExpanded",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"card_physics"}}}, aero::UnifiedValue{false}},
        &res));
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 6);
    CHECK(host.InvokeScriptMethod(rootObj, "CheckCustomHostSurvivalAfterRefresh", {}, &res));
    CHECK(ReadRsBool(res));
    CHECK(host.InvokeScriptMethod(
        gridObj,
        "IsCardExpanded",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"card_physics"}}}},
        &res));
    CHECK(!ReadRsBool(res, true));
    CHECK(host.InvokeScriptMethod(
        gridObj,
        "SetCardExpanded",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"card_physics"}}}, aero::UnifiedValue{true}},
        &res));
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 11);

    // 5. Regression: FilterText = null normalization + negative LayerMask (-1 "Everything") preservation
    CHECK(host.InvokeScriptMethod(
        gridObj,
        "set_FilterText",
        {aero::UnifiedValue{aero::StringValue{std::string_view{"Layers"}}}},
        &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(gridObj, "VisibleRowCount", {}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(rootObj, "SetNullFilterAndCountRows", {}, &res));
    CHECK(ReadRsInt(res) == 11);

    CHECK(host.InvokeScriptMethod(rootObj, "LoadLayerMaskValue", {aero::UnifiedValue{std::int64_t{-1}}}, &res));
    host.StepFrame(0.016f, true);
    for (int bi = 0; bi < 8; ++bi) {
        CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{bi}}}, &res));
        CHECK(ReadRsBool(res));
    }
    CHECK(host.InvokeScriptMethod(rootObj, "GetLayerMaskBitButtonHandle", {aero::UnifiedValue{std::int64_t{0}}}, &res));
    clickNativeHandle(res);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitInt", res));
    CHECK(ReadRsInt(res) == -2);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitText", res));
    CHECK(ReadRsString(host, res) == "-2");
    CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{0}}}, &res));
    CHECK(!ReadRsBool(res, true));
    CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{1}}}, &res));
    CHECK(ReadRsBool(res));

    CHECK(host.InvokeScriptMethod(rootObj, "GetLayerMaskBitButtonHandle", {aero::UnifiedValue{std::int64_t{0}}}, &res));
    clickNativeHandle(res);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitInt", res));
    CHECK(ReadRsInt(res) == -1);
    CHECK(host.ReadScriptMember(rootObj, "LastCommitText", res));
    CHECK(ReadRsString(host, res) == "-1");
    CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{0}}}, &res));
    CHECK(ReadRsBool(res));

    // 6. Regression (Iter 3): LayerMask IntValue = -2147483648 (INT32_MIN, bit 31 only)
    //    both unfocused AND right after clicking a layer button (button holds keyboard focus)
    constexpr std::int64_t kInt32Min = -2147483647LL - 1LL;
    CHECK(host.InvokeScriptMethod(rootObj, "LoadLayerMaskValue16", {aero::UnifiedValue{kInt32Min}}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "ActiveLayerMaskBitCount", {aero::UnifiedValue{std::int64_t{16}}}, &res));
    CHECK(ReadRsInt(res) == 0);
    CHECK(host.InvokeScriptMethod(rootObj, "LayerMaskSummaryText", {}, &res));
    CHECK(ReadRsString(host, res) == "0x-2147483648");

    // Click bit 15 so layer button holds keyboard focus -> mask becomes -2147450880, bit 15 active
    CHECK(host.InvokeScriptMethod(rootObj, "GetLayerMaskBitButtonHandle", {aero::UnifiedValue{std::int64_t{15}}}, &res));
    clickNativeHandle(res);
    CHECK(host.InvokeScriptMethod(rootObj, "ActiveLayerMaskBitCount", {aero::UnifiedValue{std::int64_t{16}}}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{15}}}, &res));
    CHECK(ReadRsBool(res));

    // Push SetProperties with -2147483648 while bit 15 button STILL has keyboard focus -> must refresh visuals to 0 active bits!
    CHECK(host.InvokeScriptMethod(rootObj, "LoadLayerMaskValue16", {aero::UnifiedValue{kInt32Min}}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "ActiveLayerMaskBitCount", {aero::UnifiedValue{std::int64_t{16}}}, &res));
    CHECK(ReadRsInt(res) == 0);
    CHECK(host.InvokeScriptMethod(rootObj, "IsLayerMaskBitActive", {aero::UnifiedValue{std::int64_t{15}}}, &res));
    CHECK(!ReadRsBool(res, true));
    CHECK(host.InvokeScriptMethod(rootObj, "LayerMaskSummaryText", {}, &res));
    CHECK(ReadRsString(host, res) == "0x-2147483648");
}

void TestDockingWorkspaceV2() {
    aero::AeroAppHostConfig hostCfg{};
    hostCfg.viewportWidth = 1280u;
    hostCfg.viewportHeight = 760u;
    hostCfg.headless = true;
    hostCfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(hostCfg);
    CHECK(host.InitializeRuntime());

    aero::AeroV3CompileInput input{};
    input.includeStdlib = true;
    input.useStdlibDll = true;
    input.stdlibDllPath = AERO_STDLIB_DLL_PATH;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.allowDiskCache = false;
    input.allowStdlibCache = false;
    input.xamlPath = "DockV2.xaml";
    input.xamlContent = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="EditorTest.DockingV2Harness"
        Title="DockingWorkspace v2 Test"
        Width="1280" Height="760">
    <DockingWorkspace x:Name="Workspace" />
</Window>
)XAML";

    aero::SourceFile codeBehind{};
    codeBehind.path = "DockV2.rs";
    codeBehind.content = R"RS(
module EditorTest;

import Aero.UI;
import Aero.Native;

public partial class DockingV2Harness : Window {
    public Border OutlinerView;
    public Border ViewportView;
    public Border CodeView;
    public Border InspectorView;
    public Border ConsoleView;
    public Border ExtraTabView;
    public int MaxChangedCount = 0;

    public DockingV2Harness() {
        this.InitializeComponent();
        var self = this;
        this.Workspace.MaximizedStateChanged += (object s, EventArgs e) => {
            self.MaxChangedCount = self.MaxChangedCount + 1;
        };

        this.OutlinerView = new Border();
        this.OutlinerView.Background = new SolidColorBrush("#FF1E293B");
        this.ViewportView = new Border();
        this.ViewportView.Background = new SolidColorBrush("#FF0F172A");
        this.CodeView = new Border();
        this.CodeView.Background = new SolidColorBrush("#FF111827");
        this.InspectorView = new Border();
        this.InspectorView.Background = new SolidColorBrush("#FF172554");
        this.ConsoleView = new Border();
        this.ConsoleView.Background = new SolidColorBrush("#FF090D16");
        this.ExtraTabView = new Border();
        this.ExtraTabView.Background = new SolidColorBrush("#FF1E1B4B");

        this.Workspace.RegisterPane("outliner", "Outliner", this.OutlinerView, DockZone.Left);
        this.Workspace.RegisterPane("viewport", "Viewport", this.ViewportView, DockZone.Center);
        this.Workspace.RegisterPane("code", "Code", this.CodeView, DockZone.Center);
        this.Workspace.RegisterPane("inspector", "Inspector", this.InspectorView, DockZone.Right);
        this.Workspace.RegisterPane("console", "Console", this.ConsoleView, DockZone.Bottom);
        this.Workspace.ActivatePane("viewport");
    }

    public string CenterPaneOrder() {
        DockTabGroup grp = this.Workspace.ZoneGroup(DockZone.Center);
        if (grp == null) { return ""; }
        DockPane[] ps = grp.Panes();
        string acc = "";
        for (int i = 0; i < ps.Length; i += 1) {
            if (i > 0) { acc = acc + ","; }
            acc = acc + ps[i].PaneId;
        }
        return acc;
    }

    public int CenterPaneCount() {
        DockTabGroup grp = this.Workspace.ZoneGroup(DockZone.Center);
        return grp != null ? grp.PaneCount() : 0;
    }

    public void RegisterExtraCenterTab() {
        this.Workspace.RegisterPane("extra", "ExtraTab", this.ExtraTabView, DockZone.Center);
    }

    public void ReRegisterOutlinerLeft() {
        this.Workspace.RegisterPane("outliner2", "Outliner2", this.OutlinerView, DockZone.Left);
    }

    public handle GetCenterTabButtonHandle(int idx) {
        DockTabGroup grp = this.Workspace.ZoneGroup(DockZone.Center);
        if (grp == null) { return ParseUtil.EmptyHandle(); }
        UIElement[] btns = grp.TabButtons();
        if (idx < 0 || idx >= btns.Length) { return ParseUtil.EmptyHandle(); }
        return btns[idx].__nativeHandle;
    }

    public handle GetRightGroupHandle() {
        DockTabGroup grp = this.Workspace.ZoneGroup(DockZone.Right);
        return grp != null ? grp.__nativeHandle : ParseUtil.EmptyHandle();
    }

    public int HoveredDropZoneInt() {
        DockZone z = this.Workspace.HoveredDropZone;
        if (z == DockZone.Left) { return 1; }
        if (z == DockZone.Right) { return 2; }
        if (z == DockZone.Top) { return 3; }
        if (z == DockZone.Bottom) { return 4; }
        return 0;
    }

    private int CountGroupsRec(UIElement node) {
        if (node == null) { return 0; }
        if (node is DockTabGroup) { return 1; }
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            return CountGroupsRec(sp.First) + CountGroupsRec(sp.Second);
        }
        return 0;
    }

    private int CountEmptyGroupsRec(UIElement node) {
        if (node == null) { return 0; }
        if (node is DockTabGroup) { return (node as DockTabGroup).PaneCount() == 0 ? 1 : 0; }
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            return CountEmptyGroupsRec(sp.First) + CountEmptyGroupsRec(sp.Second);
        }
        return 0;
    }

    private int CountPaneInTreeRec(UIElement node, string paneId) {
        if (node == null) { return 0; }
        if (node is DockTabGroup) {
            return (node as DockTabGroup).IndexOfPane(paneId) >= 0 ? 1 : 0;
        }
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            return CountPaneInTreeRec(sp.First, paneId) + CountPaneInTreeRec(sp.Second, paneId);
        }
        return 0;
    }

    public int TotalGroupsInTree() { return CountGroupsRec(this.Workspace.RootDockNode()); }
    public int EmptyGroupsInTree() { return CountEmptyGroupsRec(this.Workspace.RootDockNode()); }
    public int CountPaneOccurrencesInTree(string paneId) { return CountPaneInTreeRec(this.Workspace.RootDockNode(), paneId); }

    public void RegisterDuplicateTestPane(bool toRight) {
        DockZone z = toRight ? DockZone.Right : DockZone.Left;
        this.Workspace.RegisterPane("dup_pane", "DupPane", this.OutlinerView, z);
    }

    public void RegisterFreshCenterPane() {
        this.Workspace.RegisterPane("fresh_center", "FreshCenter", this.ViewportView, DockZone.Center);
    }

    public void SetupLeftCenterRightBottomOrder() {
        this.Workspace.RegisterPane("re_l", "LeftPane", this.OutlinerView, DockZone.Left);
        this.Workspace.RegisterPane("re_c", "CenterPane", this.ViewportView, DockZone.Center);
        this.Workspace.RegisterPane("re_r", "RightPane", this.InspectorView, DockZone.Right);
        this.Workspace.RegisterPane("re_b", "BottomPane", this.ConsoleView, DockZone.Bottom);
    }

    public void SetupUnevenCenterTabs() {
        this.Workspace.RegisterPane("s1", "A", this.OutlinerView, DockZone.Center);
        this.Workspace.RegisterPane("long1", "VeryLongDetailedTitle", this.ViewportView, DockZone.Center);
        this.Workspace.RegisterPane("s2", "B", this.CodeView, DockZone.Center);
        this.Workspace.RegisterPane("med1", "MediumTab", this.ExtraTabView, DockZone.Center);
    }
}
)RS";
    input.scriptSources.push_back(std::move(codeBehind));

    std::string compileErr;
    CHECK(host.CompileAndLoad(input, &compileErr));
    if (!compileErr.empty()) {
        std::fprintf(stderr, "DockingV2 compile error: %s\n", compileErr.c_str());
        return;
    }
    host.StepFrame(0.016f, true);

    const aero::ObjectRef rootObj = host.RootScriptObject();
    CHECK(rootObj.valid());
    aero::UnifiedValue wsVal{};
    CHECK(host.ReadScriptMember(rootObj, "Workspace", wsVal));
    aero::ObjectRef wsObj{};
    if (const auto* r = std::get_if<aero::ObjectRef>(&wsVal)) { wsObj = *r; }
    CHECK(wsObj.valid());

    auto getNodeFromMember = [&](const char* memberName) {
        aero::UnifiedValue hv{};
        aero::VisualNodeId nid{};
        if (host.ReadScriptMember(rootObj, memberName, hv)) {
            if (const auto* oref = std::get_if<aero::ObjectRef>(&hv)) {
                aero::UnifiedValue nhv{};
                if (host.ReadScriptMember(*oref, "__nativeHandle", nhv)) {
                    aero::NativeHandle nh{};
                    if (nhv.tryGetNativeHandle(nh)) { nid = host.World().ResolveHandle(nh); }
                }
            }
        }
        return nid;
    };

    const aero::VisualNodeId vpNode = getNodeFromMember("ViewportView");
    CHECK(vpNode.IsValid());
    const float initialVpW = host.World().GetLayoutRectScalar(vpNode, aero::VisualLayoutRectQuery::Width);
    CHECK(initialVpW > 400.0f && initialVpW < 850.0f);

    // 1. UnregisterPane collapses empty split and re-register re-attaches zone
    aero::UnifiedValue res{};
    CHECK(host.InvokeScriptMethod(
        wsObj, "UnregisterPane", {aero::UnifiedValue{aero::StringValue{std::string_view{"outliner"}}}}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);
    const float expandedVpW = host.World().GetLayoutRectScalar(vpNode, aero::VisualLayoutRectQuery::Width);
    CHECK(expandedVpW > initialVpW + 150.0f);

    CHECK(host.InvokeScriptMethod(rootObj, "ReRegisterOutlinerLeft", {}, &res));
    host.StepFrame(0.016f, true);
    const float restoredVpW = host.World().GetLayoutRectScalar(vpNode, aero::VisualLayoutRectQuery::Width);
    CHECK(restoredVpW < expandedVpW - 100.0f);

    // 2. Pointer-driven Tab DnD reordering in Center group
    CHECK(host.InvokeScriptMethod(rootObj, "RegisterExtraCenterTab", {}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    CHECK(ReadRsString(host, res) == "viewport,code,extra");

    aero::UnifiedValue btn0HandleVal{};
    aero::UnifiedValue btn1HandleVal{};
    aero::UnifiedValue btn2HandleVal{};
    CHECK(host.InvokeScriptMethod(rootObj, "GetCenterTabButtonHandle", {aero::UnifiedValue{std::int64_t{0}}}, &btn0HandleVal));
    CHECK(host.InvokeScriptMethod(rootObj, "GetCenterTabButtonHandle", {aero::UnifiedValue{std::int64_t{1}}}, &btn1HandleVal));
    CHECK(host.InvokeScriptMethod(rootObj, "GetCenterTabButtonHandle", {aero::UnifiedValue{std::int64_t{2}}}, &btn2HandleVal));
    aero::NativeHandle nh0{}, nh1{}, nh2{};
    CHECK(btn0HandleVal.tryGetNativeHandle(nh0));
    CHECK(btn1HandleVal.tryGetNativeHandle(nh1));
    CHECK(btn2HandleVal.tryGetNativeHandle(nh2));
    const aero::VisualNodeId b0Node = host.World().ResolveHandle(nh0);
    const aero::VisualNodeId b1Node = host.World().ResolveHandle(nh1);
    const aero::VisualNodeId b2Node = host.World().ResolveHandle(nh2);
    CHECK(b0Node.IsValid() && b1Node.IsValid() && b2Node.IsValid());

    const float b0Cx = host.World().GetLayoutRectScalar(b0Node, aero::VisualLayoutRectQuery::ViewportX) +
                       host.World().GetLayoutRectScalar(b0Node, aero::VisualLayoutRectQuery::Width) * 0.5f;
    const float b0Cy = host.World().GetLayoutRectScalar(b0Node, aero::VisualLayoutRectQuery::ViewportY) +
                       host.World().GetLayoutRectScalar(b0Node, aero::VisualLayoutRectQuery::Height) * 0.5f;
    const float b1Cx = host.World().GetLayoutRectScalar(b1Node, aero::VisualLayoutRectQuery::ViewportX) +
                       host.World().GetLayoutRectScalar(b1Node, aero::VisualLayoutRectQuery::Width) * 0.5f;
    const float b2RightX = host.World().GetLayoutRectScalar(b2Node, aero::VisualLayoutRectQuery::ViewportX) +
                           host.World().GetLayoutRectScalar(b2Node, aero::VisualLayoutRectQuery::Width) - 4.0f;

    // 2a. Rapid same-frame intra-group drag: tab0 -> tab1 -> back to tab0 without intermediate StepFrame
    host.InjectPointerMove(b0Cx, b0Cy);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(b0Cx, b0Cy, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    host.InjectPointerMove(b1Cx, b0Cy);
    host.InjectPointerMove(b0Cx, b0Cy);
    host.InjectPointerButton(b0Cx, b0Cy, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    CHECK(ReadRsString(host, res) == "viewport,code,extra");

    // 2b. Drag tab0 -> tab2 end
    host.InjectPointerMove(b0Cx, b0Cy);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(b0Cx, b0Cy, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    host.InjectPointerMove(b2RightX, b0Cy);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(b2RightX, b0Cy, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);

    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    const std::string reordered = ReadRsString(host, res);
    CHECK(reordered == "code,extra,viewport");

    // 3. Cross-zone pointer DnD from Center ("code" tab at index 0) to Right group bottom edge + overlay hints
    CHECK(host.InvokeScriptMethod(rootObj, "GetCenterTabButtonHandle", {aero::UnifiedValue{std::int64_t{0}}}, &btn0HandleVal));
    CHECK(btn0HandleVal.tryGetNativeHandle(nh0));
    const aero::VisualNodeId codeBtnNode = host.World().ResolveHandle(nh0);
    const float codeCx = host.World().GetLayoutRectScalar(codeBtnNode, aero::VisualLayoutRectQuery::ViewportX) +
                         host.World().GetLayoutRectScalar(codeBtnNode, aero::VisualLayoutRectQuery::Width) * 0.5f;
    const float codeCy = host.World().GetLayoutRectScalar(codeBtnNode, aero::VisualLayoutRectQuery::ViewportY) +
                         host.World().GetLayoutRectScalar(codeBtnNode, aero::VisualLayoutRectQuery::Height) * 0.5f;

    aero::UnifiedValue rgHandleVal{};
    CHECK(host.InvokeScriptMethod(rootObj, "GetRightGroupHandle", {}, &rgHandleVal));
    aero::NativeHandle rgNh{};
    CHECK(rgHandleVal.tryGetNativeHandle(rgNh));
    const aero::VisualNodeId rgNode = host.World().ResolveHandle(rgNh);
    const float rgX = host.World().GetLayoutRectScalar(rgNode, aero::VisualLayoutRectQuery::ViewportX);
    const float rgY = host.World().GetLayoutRectScalar(rgNode, aero::VisualLayoutRectQuery::ViewportY);
    const float rgW = host.World().GetLayoutRectScalar(rgNode, aero::VisualLayoutRectQuery::Width);
    const float rgH = host.World().GetLayoutRectScalar(rgNode, aero::VisualLayoutRectQuery::Height);

    const float dropX = rgX + rgW * 0.5f;
    const float dropY = rgY + rgH * 0.88f; // Bottom dock hint zone

    host.InjectPointerMove(codeCx, codeCy);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(codeCx, codeCy, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    host.InjectPointerMove(dropX, dropY);
    host.StepFrame(0.016f, true);

    // Mid-drag assertions
    aero::UnifiedValue dragVal{};
    CHECK(host.ReadScriptMember(wsObj, "IsDraggingPane", dragVal));
    CHECK(ReadRsBool(dragVal));
    CHECK(host.ReadScriptMember(wsObj, "DragPaneId", dragVal));
    CHECK(ReadRsString(host, dragVal) == "code");
    CHECK(host.ReadScriptMember(wsObj, "IsDockOverlayVisible", dragVal));
    CHECK(ReadRsBool(dragVal));
    CHECK(host.InvokeScriptMethod(rootObj, "HoveredDropZoneInt", {}, &res));
    CHECK(ReadRsInt(res) == 4); // DockZone.Bottom

    host.InjectPointerButton(dropX, dropY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);

    CHECK(host.ReadScriptMember(wsObj, "IsDraggingPane", dragVal));
    CHECK(!ReadRsBool(dragVal, true));
    CHECK(host.ReadScriptMember(wsObj, "IsDockOverlayVisible", dragVal));
    CHECK(!ReadRsBool(dragVal, true));

    // "code" is now split below "inspector"
    const aero::VisualNodeId codeNode = getNodeFromMember("CodeView");
    const aero::VisualNodeId inspNode = getNodeFromMember("InspectorView");
    CHECK(codeNode.IsValid() && inspNode.IsValid());
    CHECK(host.World().GetLayoutRectScalar(codeNode, aero::VisualLayoutRectQuery::ViewportY) >
          host.World().GetLayoutRectScalar(inspNode, aero::VisualLayoutRectQuery::ViewportY) + 50.0f);

    // 4. MaximizePane / ToggleMaximizeActivePane / RestoreMaximizedPane
    CHECK(host.InvokeScriptMethod(
        wsObj, "MaximizePane", {aero::UnifiedValue{aero::StringValue{std::string_view{"viewport"}}}}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);

    CHECK(host.ReadScriptMember(wsObj, "IsPaneMaximized", dragVal));
    CHECK(ReadRsBool(dragVal));
    CHECK(host.ReadScriptMember(wsObj, "MaximizedPaneId", dragVal));
    CHECK(ReadRsString(host, dragVal) == "viewport");
    CHECK(host.World().GetLayoutRectScalar(vpNode, aero::VisualLayoutRectQuery::Width) > 1200.0f);

    // SerializeLayout v2 (including maximized state, dynamic split, reordered tabs) -> NO "|" char!
    aero::UnifiedValue serVal{};
    CHECK(host.InvokeScriptMethod(wsObj, "SerializeLayout", {}, &serVal));
    const std::string v2Spec = ReadRsString(host, serVal);
    CHECK(v2Spec.rfind("aero-dock-v2:", 0) == 0);
    CHECK(v2Spec.find('|') == std::string::npos);

    // Toggle back to normal split
    CHECK(host.InvokeScriptMethod(wsObj, "ToggleMaximizeActivePane", {}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);
    CHECK(host.ReadScriptMember(wsObj, "IsPaneMaximized", dragVal));
    CHECK(!ReadRsBool(dragVal, true));
    CHECK(host.World().GetLayoutRectScalar(vpNode, aero::VisualLayoutRectQuery::Width) < 950.0f);

    // Reset to default IDE layout, then restore v2Spec and confirm exact state + invalid spec rejection
    CHECK(host.InvokeScriptMethod(wsObj, "ResetDefaultIdeLayout", {}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(
        wsObj, "DeserializeLayout", {aero::UnifiedValue{aero::StringValue{std::string_view{"aero-dock-v2:INVALID"}}}}, &res));
    CHECK(!ReadRsBool(res, true));

    CHECK(host.InvokeScriptMethod(
        wsObj, "DeserializeLayout", {aero::UnifiedValue{aero::StringValue{v2Spec}}}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);
    CHECK(host.ReadScriptMember(wsObj, "MaximizedPaneId", dragVal));
    CHECK(ReadRsString(host, dragVal) == "viewport");

    CHECK(host.InvokeScriptMethod(wsObj, "RestoreMaximizedPane", {}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);
    // Verify dynamic split (code below inspector) survived roundtrip!
    CHECK(host.World().GetLayoutRectScalar(codeNode, aero::VisualLayoutRectQuery::ViewportY) >
          host.World().GetLayoutRectScalar(inspNode, aero::VisualLayoutRectQuery::ViewportY) + 50.0f);

    // 5. Backward compatibility with aero-dock-v1:
    CHECK(host.InvokeScriptMethod(
        wsObj, "DeserializeLayout", {aero::UnifiedValue{aero::StringValue{std::string_view{"aero-dock-v1:250,700,680"}}}}, &res));
    CHECK(ReadRsBool(res));
    host.StepFrame(0.016f, true);
    CHECK(host.ReadScriptMember(wsObj, "LeftSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.25f) < 0.01f);
    CHECK(host.ReadScriptMember(wsObj, "RightSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.70f) < 0.01f);
    CHECK(host.ReadScriptMember(wsObj, "BottomSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.68f) < 0.01f);

    // Unregistering a maximized pane cleanly restores workspace
    CHECK(host.InvokeScriptMethod(
        wsObj, "MaximizePane", {aero::UnifiedValue{aero::StringValue{std::string_view{"extra"}}}}, &res));
    CHECK(ReadRsBool(res));

    // Regression: malformed v1 & v2 specs must return false atomically WITHOUT clearing live MaximizedPaneId
    const char* badSpecs[] = {
        "aero-dock-v1:bad",
        "aero-dock-v1:abc,def,ghi",
        "aero-dock-v1:250,700",
        "aero-dock-v1:0,700,680",
        "aero-dock-v1:250,1000,680",
        "aero-dock-v2:S(H,500abc,G(C,),G(R,))",
        "aero-dock-v2:G(C,a);badmeta",
        "aero-dock-v2:G(C,a);",
        "aero-dock-v2:G(C,a);max=",
        "aero-dock-v2:G(C,a);max=a;max=b"
    };
    for (int bi = 0; bi < 10; ++bi) {
        CHECK(host.InvokeScriptMethod(
            wsObj, "DeserializeLayout", {aero::UnifiedValue{aero::StringValue{std::string_view{badSpecs[bi]}}}}, &res));
        CHECK(!ReadRsBool(res, true));
        CHECK(host.ReadScriptMember(wsObj, "MaximizedPaneId", dragVal));
        CHECK(ReadRsString(host, dragVal) == "extra");
    }

    CHECK(host.InvokeScriptMethod(
        wsObj, "UnregisterPane", {aero::UnifiedValue{aero::StringValue{std::string_view{"extra"}}}}, &res));
    CHECK(ReadRsBool(res));
    CHECK(host.ReadScriptMember(wsObj, "IsPaneMaximized", dragVal));
    CHECK(!ReadRsBool(dragVal, true));

    // Regression: duplicate RegisterPane with existing paneId moves cleanly without leaving duplicates or empty groups
    CHECK(host.InvokeScriptMethod(rootObj, "RegisterDuplicateTestPane", {aero::UnifiedValue{false}}, &res));
    CHECK(host.InvokeScriptMethod(rootObj, "CountPaneOccurrencesInTree", {aero::UnifiedValue{aero::StringValue{std::string_view{"dup_pane"}}}}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(rootObj, "RegisterDuplicateTestPane", {aero::UnifiedValue{true}}, &res));
    CHECK(host.InvokeScriptMethod(rootObj, "CountPaneOccurrencesInTree", {aero::UnifiedValue{aero::StringValue{std::string_view{"dup_pane"}}}}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(rootObj, "EmptyGroupsInTree", {}, &res));
    CHECK(ReadRsInt(res) == 0);

    // Regression: unregister ALL panes then RegisterPane("fresh_center", DockZone.Center) -> 1 group, 0 empty groups
    const char* allPanes[] = {"outliner2", "viewport", "code", "inspector", "console", "dup_pane"};
    for (int pi = 0; pi < 6; ++pi) {
        CHECK(host.InvokeScriptMethod(
            wsObj, "UnregisterPane", {aero::UnifiedValue{aero::StringValue{std::string_view{allPanes[pi]}}}}, &res));
    }
    CHECK(host.InvokeScriptMethod(rootObj, "RegisterFreshCenterPane", {}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "TotalGroupsInTree", {}, &res));
    CHECK(ReadRsInt(res) == 1);
    CHECK(host.InvokeScriptMethod(rootObj, "EmptyGroupsInTree", {}, &res));
    CHECK(ReadRsInt(res) == 0);
    CHECK(host.InvokeScriptMethod(rootObj, "CountPaneOccurrencesInTree", {aero::UnifiedValue{aero::StringValue{std::string_view{"fresh_center"}}}}, &res));
    CHECK(ReadRsInt(res) == 1);

    // Regression (Iter 3): L->C->R->B re-attach after unregistering all panes binds _mainHorizSplit/_centerRightSplit/_centerBottomSplit
    CHECK(host.InvokeScriptMethod(wsObj, "UnregisterPane", {aero::UnifiedValue{aero::StringValue{std::string_view{"fresh_center"}}}}, &res));
    CHECK(ReadRsBool(res));
    CHECK(host.InvokeScriptMethod(rootObj, "SetupLeftCenterRightBottomOrder", {}, &res));
    CHECK(host.InvokeScriptMethod(wsObj, "set_LeftSplitRatio", {aero::UnifiedValue{aero::FloatValue{0.28f}}}, &res));
    CHECK(host.InvokeScriptMethod(wsObj, "set_RightSplitRatio", {aero::UnifiedValue{aero::FloatValue{0.81f}}}, &res));
    CHECK(host.InvokeScriptMethod(wsObj, "set_BottomSplitRatio", {aero::UnifiedValue{aero::FloatValue{0.64f}}}, &res));
    host.StepFrame(0.016f, true);

    CHECK(host.ReadScriptMember(wsObj, "LeftSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.28f) < 0.01f);
    CHECK(host.ReadScriptMember(wsObj, "RightSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.81f) < 0.01f);
    CHECK(host.ReadScriptMember(wsObj, "BottomSplitRatio", dragVal));
    CHECK(std::fabs(ReadRsFloat(dragVal) - 0.64f) < 0.01f);

    CHECK(host.InvokeScriptMethod(wsObj, "SerializeLayout", {}, &serVal));
    const std::string lcrbSpec = ReadRsString(host, serVal);
    CHECK(lcrbSpec.find("S(V,640,S(H,810,S(H,280,") != std::string::npos);
    CHECK(lcrbSpec.find(",500,") == std::string::npos);
    const aero::VisualNodeId reLeftNode = getNodeFromMember("OutlinerView");
    const aero::VisualNodeId reCenterNode = getNodeFromMember("ViewportView");
    CHECK(reLeftNode.IsValid() && reCenterNode.IsValid());
    const float reLeftW = host.World().GetLayoutRectScalar(reLeftNode, aero::VisualLayoutRectQuery::Width);
    const float reCenterW = host.World().GetLayoutRectScalar(reCenterNode, aero::VisualLayoutRectQuery::Width);
    CHECK(reLeftW > 240.0f && reLeftW < 340.0f);
    CHECK(reCenterW > reLeftW * 2.0f);

    // Regression (Iter 3): Uneven same-frame intra-group tab DnD (exact midpoint forward + wide-tab return across narrow tabs)
    const char* rePanes[] = {"re_l", "re_c", "re_r", "re_b"};
    for (int pi = 0; pi < 4; ++pi) {
        CHECK(host.InvokeScriptMethod(wsObj, "UnregisterPane", {aero::UnifiedValue{aero::StringValue{std::string_view{rePanes[pi]}}}}, &res));
    }
    CHECK(host.InvokeScriptMethod(rootObj, "SetupUnevenCenterTabs", {}, &res));
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    CHECK(ReadRsString(host, res) == "s1,long1,s2,med1");

    auto getTabCenter = [&](int idx, float* outX, float* outY) {
        aero::UnifiedValue hv{};
        CHECK(host.InvokeScriptMethod(rootObj, "GetCenterTabButtonHandle", {aero::UnifiedValue{std::int64_t{idx}}}, &hv));
        aero::NativeHandle nh{};
        CHECK(hv.tryGetNativeHandle(nh));
        const aero::VisualNodeId nid = host.World().ResolveHandle(nh);
        CHECK(nid.IsValid());
        *outX = host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::ViewportX) +
                host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::Width) * 0.5f;
        *outY = host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::ViewportY) +
                host.World().GetLayoutRectScalar(nid, aero::VisualLayoutRectQuery::Height) * 0.5f;
    };

    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f, tabY = 0.0f;
    getTabCenter(0, &c0, &tabY);
    getTabCenter(1, &c1, &tabY);
    getTabCenter(2, &c2, &tabY);
    getTabCenter(3, &c3, &tabY);

    // 4a: Short tab "s1" dragged to c2 -> exact c3 midpoint in one frame reaches index 3
    host.InjectPointerMove(c0, tabY);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(c0, tabY, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    host.InjectPointerMove(c2, tabY);
    host.InjectPointerMove(c3, tabY);
    host.InjectPointerButton(c3, tabY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    CHECK(ReadRsString(host, res) == "long1,s2,med1,s1");

    // 4b: Wide tab "long1" (now at idx 0) grabbed at its center c0 -> dragged to c2 -> c3 -> back to c0 in one frame returns to idx 0
    getTabCenter(0, &c0, &tabY);
    getTabCenter(1, &c1, &tabY);
    getTabCenter(2, &c2, &tabY);
    getTabCenter(3, &c3, &tabY);
    host.InjectPointerMove(c0, tabY);
    host.StepFrame(0.016f, true);
    host.InjectPointerButton(c0, tabY, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    host.InjectPointerMove(c2, tabY);
    host.InjectPointerMove(c3, tabY);
    host.InjectPointerMove(c0, tabY);
    host.InjectPointerButton(c0, tabY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);
    CHECK(host.InvokeScriptMethod(rootObj, "CenterPaneOrder", {}, &res));
    CHECK(ReadRsString(host, res) == "long1,s2,med1,s1");
}

} // namespace

int main() {
    TestExternalGpuTextureSlotAndViewportSurface();
    TestLosslessXamlDomAndSurgicalPatches();
    TestEditorKitSelfHostedWorkspaceAndSyntaxRendering();
    TestPropertyGrid20IncrementalFocusAndEditors();
    TestDockingWorkspaceV2();

    if (gFailures != 0) {
        std::fprintf(stderr, "v3_editorkit_tests FAILED (%d failures)\n", gFailures);
        return 1;
    }
    std::printf("v3_editorkit_tests PASSED\n");
    return 0;
}
