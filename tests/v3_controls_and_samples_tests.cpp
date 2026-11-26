#include "aero/app/AeroAppHost.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/core/RsAliases.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

int gFailureCount = 0;

void Require(bool cond, std::string_view msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailureCount;
    }
}

[[nodiscard]] std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] aero::AeroV3CompileInput BuildSampleCompileInput(
    std::string_view sampleSubdir,
    aero::rs_comp::CompilerSession&) {
    namespace fs = std::filesystem;
    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.compilerSession = nullptr;

    const fs::path sampleDir = fs::path(AERO_SAMPLES_DIR) / "V3" / std::string(sampleSubdir);
    std::vector<fs::path> entries;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(sampleDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());

    for (const auto& p : entries) {
        const std::string ext = p.extension().string();
        const std::string content = ReadFileText(p);
        if (ext == ".xaml" || ext == ".axaml") {
            input.xamlPath = p.string();
            input.xamlContent = content;
        } else if (ext == ".rs" || ext == ".cs") {
            input.scriptSources.push_back(aero::SourceFile{p.string(), content});
        }
    }
    return input;
}

[[nodiscard]] std::size_t CountDistinctColors(const std::vector<std::uint8_t>& rgba) {
    std::unordered_set<std::uint32_t> colors;
    const std::size_t pixelCount = rgba.size() / 4u;
    const std::size_t step = std::max<std::size_t>(1u, pixelCount / 12000u);
    for (std::size_t i = 0u; i < pixelCount; i += step) {
        const std::size_t off = i * 4u;
        const std::uint32_t packed =
            (static_cast<std::uint32_t>(rgba[off]) << 24) |
            (static_cast<std::uint32_t>(rgba[off + 1u]) << 16) |
            (static_cast<std::uint32_t>(rgba[off + 2u]) << 8) |
            static_cast<std::uint32_t>(rgba[off + 3u]);
        colors.insert(packed);
        if (colors.size() >= 64u) {
            break;
        }
    }
    return colors.size();
}

[[nodiscard]] std::size_t CountChangedPixels(
    const std::vector<std::uint8_t>& a,
    const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return a.size() / 4u;
    }
    std::size_t diffPixels = 0u;
    for (std::size_t i = 0u; i + 3u < a.size(); i += 4u) {
        const int dr = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        const int dg = std::abs(static_cast<int>(a[i + 1u]) - static_cast<int>(b[i + 1u]));
        const int db = std::abs(static_cast<int>(a[i + 2u]) - static_cast<int>(b[i + 2u]));
        const int da = std::abs(static_cast<int>(a[i + 3u]) - static_cast<int>(b[i + 3u]));
        if (dr + dg + db + da > 6) {
            ++diffPixels;
        }
    }
    return diffPixels;
}

[[nodiscard]] bool RequireNodeCenter(
    const aero::AeroAppHost& host,
    std::string_view name,
    float& outCx,
    float& outCy,
    aero::VisualNodeId* outId = nullptr,
    aero::Rect* outRect = nullptr) {
    const aero::VisualNodeId id = host.FindVisualByOwnerName(name);
    if (!id.IsValid()) {
        Require(false, "Expected named VisualNode '" + std::string(name) + "' not found");
        return false;
    }
    const aero::VisualNode* node = host.World().GetNode(id);
    if (!node) {
        Require(false, "Named VisualNode '" + std::string(name) + "' resolved to null node");
        return false;
    }
    const aero::Rect r = node->layout.layoutRect;
    if (r.width <= 1.0f || r.height <= 1.0f) {
        Require(false, "Named VisualNode '" + std::string(name) + "' has degenerate layoutRect (" +
            std::to_string(r.width) + "x" + std::to_string(r.height) + ")");
        return false;
    }
    if (outId) {
        *outId = id;
    }
    if (outRect) {
        *outRect = r;
    }
    const aero::Point localCenter{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const aero::Point vpCenter = node->layout.hasPerspective3D
        ? node->WorldMatrix3D().ProjectPoint2D(localCenter)
        : node->layout.worldTransform.TransformPoint(localCenter);
    outCx = vpCenter.x;
    outCy = vpCenter.y;
    return true;
}

[[nodiscard]] bool ReadIntMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    std::int64_t& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    return v.tryGetInt64(outVal);
}

[[nodiscard]] bool ReadFloatMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    float& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (v.tryGetFloat(outVal)) {
        return true;
    }
    double dv = 0.0;
    if (v.tryGetDouble(dv)) {
        outVal = static_cast<float>(dv);
        return true;
    }
    std::int64_t iv = 0;
    if (v.tryGetInt64(iv)) {
        outVal = static_cast<float>(iv);
        return true;
    }
    return false;
}

[[nodiscard]] bool ReadBoolMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    bool& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    return v.tryGetBool(outVal);
}

[[nodiscard]] bool ReadStringMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    std::string& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&v)) {
        outVal = sv->str();
        return true;
    }
    if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&v);
        ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
        if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
            outVal.assign(opt->data(), opt->size());
            return true;
        }
    }
    return false;
}

void TestSample01CodeFirstCounter(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] 01_CodeFirstCounter...\n";
    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 640u;
    cfg.viewportHeight = 440u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(BuildSampleCompileInput("01_CodeFirstCounter", session), &err),
        "01_CodeFirstCounter CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(640u, 440u, frame0), "01 initial RenderHeadlessRgba failed");
    Require(CountDistinctColors(frame0) >= 6u, "01 initial render appears blank/uniform");

    const aero::ObjectRef vm = host.RootDataContext();
    Require(vm.valid(), "01 RootDataContext missing");
    std::int64_t count = -1;
    Require(ReadIntMember(host, vm, "Count", count) && count == 0, "01 initial Count != 0");

    float incX = 0.0f, incY = 0.0f;
    float decX = 0.0f, decY = 0.0f;
    float resetX = 0.0f, resetY = 0.0f;
    float chkX = 0.0f, chkY = 0.0f;
    Require(RequireNodeCenter(host, "IncBtn", incX, incY), "01 IncBtn rect invalid");
    Require(RequireNodeCenter(host, "DecBtn", decX, decY), "01 DecBtn rect invalid");
    Require(RequireNodeCenter(host, "ResetBtn", resetX, resetY), "01 ResetBtn rect invalid");
    Require(RequireNodeCenter(host, "Step2Check", chkX, chkY), "01 Step2Check rect invalid");

    // Hover + Click Increment
    aero::VisualNodeId incId{};
    (void)RequireNodeCenter(host, "IncBtn", incX, incY, &incId);
    const aero::VisualNode* incNode = host.World().GetNode(incId);
    const aero::ObjectRef incObj = incNode ? incNode->scriptOwner : aero::ObjectRef{};
    host.InjectPointerMove(incX, incY);
    host.StepFrame(0.016f, true);
    bool overAfterMove = false;
    Require(ReadBoolMember(host, incObj, "IsMouseOver", overAfterMove) && overAfterMove,
        "01 expected IncBtn.IsMouseOver == true after pointer move");
    host.InjectClick(incX, incY);
    host.StepFrame(0.016f, true);
    Require(ReadIntMember(host, vm, "Count", count) && count == 1,
        "01 expected Count == 1 after clicking IncBtn");

    std::string countDisp;
    Require(ReadStringMember(host, vm, "CountDisplay", countDisp) && countDisp == "Count: 1",
        "01 CountDisplay mismatch");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(640u, 440u, frame1), "01 post-inc render failed");
    Require(CountChangedPixels(frame0, frame1) > 10u, "01 rendered pixels did not change after Increment");

    // Enable DoubleStep checkbox & increment by +2
    host.InjectClick(chkX, chkY);
    host.StepFrame(0.016f, true);
    bool doubleStep = false;
    Require(ReadBoolMember(host, vm, "DoubleStep", doubleStep) && doubleStep,
        "01 Step2Check click did not toggle DoubleStep");

    host.InjectClick(incX, incY);
    host.StepFrame(0.016f, true);
    Require(ReadIntMember(host, vm, "Count", count) && count == 3,
        "01 expected Count == 3 after x2 Increment");

    // Decrement by 2 -> 1, then Reset -> 0
    host.InjectClick(decX, decY);
    host.StepFrame(0.016f, true);
    Require(ReadIntMember(host, vm, "Count", count) && count == 1,
        "01 expected Count == 1 after x2 Decrement");

    host.InjectClick(resetX, resetY);
    host.StepFrame(0.016f, true);
    Require(ReadIntMember(host, vm, "Count", count) && count == 0,
        "01 expected Count == 0 after ResetBtn");
}

void TestSample02CompiledXamlMvvm(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] 02_CompiledXamlMvvm...\n";
    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 760u;
    cfg.viewportHeight = 560u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(BuildSampleCompileInput("02_CompiledXamlMvvm", session), &err),
        "02_CompiledXamlMvvm CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(760u, 560u, frame0), "02 initial render failed");
    Require(CountDistinctColors(frame0) >= 6u, "02 initial render blank/uniform");

    const aero::ObjectRef vm = host.RootDataContext();
    Require(vm.valid(), "02 RootDataContext missing");

    float tbX = 0.0f, tbY = 0.0f;
    float addX = 0.0f, addY = 0.0f;
    float popX = 0.0f, popY = 0.0f;
    float chkX = 0.0f, chkY = 0.0f;
    aero::Rect sliderRect{};
    float slX = 0.0f, slY = 0.0f;
    aero::VisualNodeId listNodeId{};
    float lstX = 0.0f, lstY = 0.0f;

    Require(RequireNodeCenter(host, "TitleInput", tbX, tbY), "02 TitleInput missing");
    Require(RequireNodeCenter(host, "AddTaskBtn", addX, addY), "02 AddTaskBtn missing");
    Require(RequireNodeCenter(host, "PopTaskBtn", popX, popY), "02 PopTaskBtn missing");
    Require(RequireNodeCenter(host, "PrioritySlider", slX, slY, nullptr, &sliderRect), "02 PrioritySlider missing");
    Require(RequireNodeCenter(host, "UrgentCheck", chkX, chkY), "02 UrgentCheck missing");
    Require(RequireNodeCenter(host, "TaskList", lstX, lstY, &listNodeId), "02 TaskList missing");

    // 1. TwoWay TextBox keyboard/text input
    host.InjectClick(tbX, tbY);
    host.InjectTextInput("ShipV3");
    host.StepFrame(0.016f, true);
    std::string draftTitle;
    Require(ReadStringMember(host, vm, "DraftTitle", draftTitle) &&
            draftTitle.find("ShipV3") != std::string::npos,
        "02 TwoWay TextBox failed to update VM.DraftTitle (got '" + draftTitle + "')");

    // 2. TwoWay Slider drag -> VM.PriorityScore + ProgressBar
    float initialPriority = 0.0f;
    (void)ReadFloatMember(host, vm, "PriorityScore", initialPriority);
    const float dragStartX = sliderRect.x + sliderRect.width * 0.25f;
    const float dragEndX = sliderRect.x + sliderRect.width * 0.85f;
    host.InjectPointerButton(dragStartX, slY, aero::PointerButton::Left, true);
    host.InjectPointerMove(dragEndX, slY);
    host.InjectPointerButton(dragEndX, slY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);
    float updatedPriority = 0.0f;
    Require(ReadFloatMember(host, vm, "PriorityScore", updatedPriority) &&
            updatedPriority > initialPriority + 10.0f,
        "02 Slider drag did not update VM.PriorityScore");

    // 3. TwoWay CheckBox click
    bool isUrgent = false;
    (void)ReadBoolMember(host, vm, "IsUrgent", isUrgent);
    host.InjectClick(chkX, chkY);
    host.StepFrame(0.016f, true);
    bool toggledUrgent = false;
    Require(ReadBoolMember(host, vm, "IsUrgent", toggledUrgent) && toggledUrgent != isUrgent,
        "02 CheckBox click did not toggle VM.IsUrgent");

    // 4. RelayCommand + ObservableList<T> Add & Remove updating ListBox + pixels
    aero::UnifiedValue tasksObjVal{};
    Require(host.ReadScriptMember(vm, "Tasks", tasksObjVal), "02 could not read VM.Tasks");
    const auto* listPtr = host.World().GetNode(listNodeId);
    const std::size_t initialVisualCount = listPtr ? listPtr->children.size() : 0u;
    std::int64_t countBefore = 0;
    if (const auto* tasksRef = std::get_if<aero::rs_rt::ObjectRef>(&tasksObjVal); tasksRef && tasksRef->valid()) {
        (void)host.InvokeScriptMethod(*tasksRef, "ItemCount", {}, nullptr);
        aero::UnifiedValue cntRes{};
        if (host.InvokeScriptMethod(*tasksRef, "ItemCount", {}, &cntRes)) {
            (void)cntRes.tryGetInt64(countBefore);
        }
    }

    std::vector<std::uint8_t> beforeAddPixels;
    Require(host.RenderHeadlessRgba(760u, 560u, beforeAddPixels), "02 pre-add render failed");

    host.InjectClick(addX, addY);
    host.StepFrame(0.016f, true);

    std::int64_t countAfterAdd = 0;
    if (const auto* tasksRef = std::get_if<aero::rs_rt::ObjectRef>(&tasksObjVal); tasksRef && tasksRef->valid()) {
        aero::UnifiedValue cntRes{};
        if (host.InvokeScriptMethod(*tasksRef, "ItemCount", {}, &cntRes)) {
            (void)cntRes.tryGetInt64(countAfterAdd);
        }
    }
    Require(countAfterAdd == countBefore + 1,
        "02 AddTaskCommand did not mutate ObservableList<TaskItem>");

    std::vector<std::uint8_t> afterAddPixels;
    Require(host.RenderHeadlessRgba(760u, 560u, afterAddPixels), "02 post-add render failed");
    Require(CountChangedPixels(beforeAddPixels, afterAddPixels) > 15u,
        "02 ObservableList Add did not change rendered UI pixels");

    // Remove first item via RelayCommand
    host.InjectClick(popX, popY);
    host.StepFrame(0.016f, true);
    std::int64_t countAfterPop = 0;
    if (const auto* tasksRef = std::get_if<aero::rs_rt::ObjectRef>(&tasksObjVal); tasksRef && tasksRef->valid()) {
        aero::UnifiedValue cntRes{};
        if (host.InvokeScriptMethod(*tasksRef, "ItemCount", {}, &cntRes)) {
            (void)cntRes.tryGetInt64(countAfterPop);
        }
    }
    Require(countAfterPop == countBefore,
        "02 RemoveFirstCommand did not remove item from ObservableList<TaskItem>");
    (void)initialVisualCount;
}

void TestSample03ControlGallery(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] 03_ControlGallery...\n";
    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 860u;
    cfg.viewportHeight = 640u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(BuildSampleCompileInput("03_ControlGallery", session), &err),
        "03_ControlGallery CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(860u, 640u, frame0), "03 initial render failed");
    Require(CountDistinctColors(frame0) >= 8u, "03 initial render blank/uniform");

    const aero::ObjectRef vm = host.RootDataContext();
    Require(vm.valid(), "03 RootDataContext missing");

    float pillX = 0.0f, pillY = 0.0f;
    float chkX = 0.0f, chkY = 0.0f;
    float scX = 0.0f, scY = 0.0f;
    aero::VisualNodeId scrollerId{};
    aero::VisualNodeId badgeId{};
    float badgeX = 0.0f, badgeY = 0.0f;

    Require(RequireNodeCenter(host, "TemplatedPillBtn", pillX, pillY), "03 TemplatedPillBtn missing");
    Require(RequireNodeCenter(host, "CompactModeCheck", chkX, chkY), "03 CompactModeCheck missing");
    Require(RequireNodeCenter(host, "CardScroller", scX, scY, &scrollerId), "03 CardScroller missing");
    Require(RequireNodeCenter(host, "CompactStatusBadge", badgeX, badgeY, &badgeId), "03 CompactStatusBadge missing");

    // Hover & Press ControlTemplate Style Triggers + Click
    host.InjectPointerMove(pillX, pillY);
    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> hoverFrame;
    Require(host.RenderHeadlessRgba(860u, 640u, hoverFrame), "03 hover render failed");

    host.InjectPointerButton(pillX, pillY, aero::PointerButton::Left, true);
    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> pressedFrame;
    Require(host.RenderHeadlessRgba(860u, 640u, pressedFrame), "03 pressed render failed");
    Require(CountChangedPixels(hoverFrame, pressedFrame) > 10u,
        "03 Style Trigger IsPressed did not change pill appearance");

    host.InjectPointerButton(pillX, pillY, aero::PointerButton::Left, false);
    host.StepFrame(0.016f, true);
    std::int64_t clicks = 0;
    Require(ReadIntMember(host, vm, "ActionCount", clicks) && clicks == 1,
        "03 TemplatedPillBtn Click did not increment ActionCount");

    // DataTrigger via CheckBox CompactMode
    host.InjectClick(chkX, chkY);
    host.StepFrame(0.016f, true);
    bool compactMode = false;
    Require(ReadBoolMember(host, vm, "CompactMode", compactMode) && compactMode,
        "03 CompactModeCheck did not toggle VM.CompactMode");

    // Refresh Style DataTrigger & flush bindings
    (void)host.InvokeScriptMethod(host.RootScriptObject(), "OnMenuCompactToggle", {
        aero::UnifiedValue{host.RootScriptObject()}, aero::UnifiedValue{aero::rs_rt::NullObject{}}});
    host.StepFrame(0.016f, true);

    // MouseWheel scrolling inside ScrollViewer (scroll offset lives on its inner ScrollViewport visual node)
    aero::VisualNodeId viewportId = scrollerId;
    {
        std::vector<aero::VisualNodeId> stack{scrollerId};
        while (!stack.empty()) {
            const aero::VisualNodeId curId = stack.back();
            stack.pop_back();
            const aero::VisualNode* n = host.World().GetNode(curId);
            if (!n) continue;
            if (n->layout.layoutMode == aero::LayoutMode::ScrollViewport) {
                viewportId = curId;
                break;
            }
            for (const aero::VisualNodeId cid : n->children) {
                stack.push_back(cid);
            }
        }
    }
    const aero::VisualNode* scrollNode = host.World().GetNode(viewportId);
    const float prevScrollY = scrollNode ? scrollNode->layout.scrollOffsetY : 0.0f;
    host.InjectMouseWheel(scX, scY, 0.0f, -120.0f);
    host.StepFrame(0.016f, true);
    scrollNode = host.World().GetNode(viewportId);
    const aero::VisualNode* scrollerCtrlNode = host.World().GetNode(scrollerId);
    float scrollerOffsetY = 0.0f;
    const bool gotOffsetProp = scrollerCtrlNode &&
        ReadFloatMember(host, scrollerCtrlNode->scriptOwner, "VerticalOffset", scrollerOffsetY);
    Require(scrollNode && scrollNode->layout.scrollOffsetY > prevScrollY + 1.0f &&
            (!gotOffsetProp || scrollerOffsetY > 1.0f),
        "03 ScrollViewer mouse wheel did not change scrollOffsetY");

    // TabControl switch to Hierarchy Tab & verify TreeView becomes laid out
    (void)host.InvokeScriptMethod(host.RootScriptObject(), "OnMenuSelectHierarchyTab", {
        aero::UnifiedValue{host.RootScriptObject()}, aero::UnifiedValue{aero::rs_rt::NullObject{}}});
    host.StepFrame(0.016f, true);
    float treeX = 0.0f, treeY = 0.0f;
    Require(RequireNodeCenter(host, "ComponentTree", treeX, treeY),
        "03 TreeView should have non-zero layout bounds when Hierarchy tab is active");

    std::vector<std::uint8_t> tabFrame;
    Require(host.RenderHeadlessRgba(860u, 640u, tabFrame), "03 hierarchy tab render failed");
    Require(CountChangedPixels(frame0, tabFrame) > 500u,
        "03 Switching to Hierarchy tab did not substantially update rendered pixels");
}

void TestSample04VectorAndCanvas(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] 04_VectorAndCanvas...\n";
    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 860u;
    cfg.viewportHeight = 580u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(BuildSampleCompileInput("04_VectorAndCanvas", session), &err),
        "04_VectorAndCanvas CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(860u, 580u, frame0), "04 initial render failed");
    Require(CountDistinctColors(frame0) >= 12u, "04 initial vector/canvas render lacks color variety");

    float animX = 0.0f, animY = 0.0f;
    float pulseX = 0.0f, pulseY = 0.0f;
    float badgeX = 0.0f, badgeY = 0.0f;
    aero::VisualNodeId card3DId{};
    float cardX = 0.0f, cardY = 0.0f;

    Require(RequireNodeCenter(host, "AnimateBtn", animX, animY), "04 AnimateBtn missing");
    Require(RequireNodeCenter(host, "PulseCanvasBtn", pulseX, pulseY), "04 PulseCanvasBtn missing");
    Require(RequireNodeCenter(host, "HeroVectorBadge", badgeX, badgeY), "04 HeroVectorBadge Path missing");
    Require(RequireNodeCenter(host, "Card3D", cardX, cardY, &card3DId), "04 Card3D missing");

    aero::VisualNodeId canvasHostId{};
    float chX = 0.0f, chY = 0.0f;
    Require(RequireNodeCenter(host, "CanvasHost", chX, chY, &canvasHostId), "04 CanvasHost missing");
    const aero::VisualNode* canvasHostNode = host.World().GetNode(canvasHostId);
    const aero::VisualNodeId canvasElemId =
        (canvasHostNode && !canvasHostNode->children.empty()) ? canvasHostNode->children[0] : aero::VisualNodeId{};
    Require(canvasElemId.IsValid() && host.DrawBuffer().HasCommands(canvasElemId),
        "04 CustomDrawElement should record DrawingCommandBuffer commands");

    // Step Storyboard + Spring animations across multiple frames
    const aero::VisualNode* cardNode = host.World().GetNode(card3DId);
    const float op0 = cardNode ? cardNode->layout.opacity : 0.0f;
    for (int f = 0; f < 10; ++f) {
        host.StepFrame(0.045f, true);
    }
    cardNode = host.World().GetNode(card3DId);
    const float op1 = cardNode ? cardNode->layout.opacity : 0.0f;
    Require(op1 > op0 || (cardNode && std::fabs(cardNode->transform2D.tx) > 0.5f),
        "04 Storyboard / SpringAnimation did not animate Card3D over 10 frames");

    std::vector<std::uint8_t> animFrame;
    Require(host.RenderHeadlessRgba(860u, 580u, animFrame), "04 animated render failed");
    Require(CountChangedPixels(frame0, animFrame) > 30u,
        "04 Storyboard animation produced no visual change across frames");

    // Click PulseCanvasBtn -> triggers retained DrawingContext redraw & 3D rotation
    host.InjectClick(pulseX, pulseY);
    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> pulseFrame;
    Require(host.RenderHeadlessRgba(860u, 580u, pulseFrame), "04 post-pulse render failed");
    Require(CountChangedPixels(animFrame, pulseFrame) > 30u,
        "04 Retained CustomDrawElement redraw did not update framebuffer pixels");
}

void TestSample05DualDomainSimHud(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] 05_DualDomainSimHud...\n";
    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 860u;
    cfg.viewportHeight = 560u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(BuildSampleCompileInput("05_DualDomainSimHud", session), &err),
        "05_DualDomainSimHud CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    const aero::ObjectRef simObj = host.SimScriptObject();
    Require(simObj.valid(), "05 CreateSimState should populate SimScriptObject in SimDeterministic domain");
    Require(host.Domains().heapForObject(simObj) == &host.Domains().simHeap(),
        "05 SimTelemetryState must reside on SimHeap, NOT UiHeap");

    host.StepFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(860u, 560u, frame0), "05 initial render failed");
    Require(CountDistinctColors(frame0) >= 8u, "05 initial HUD render blank/uniform");

    float boostX = 0.0f, boostY = 0.0f;
    aero::VisualNodeId healthId{};
    aero::VisualNodeId energyId{};
    aero::VisualNodeId needleId{};
    float hx = 0.0f, hy = 0.0f, ex = 0.0f, ey = 0.0f, nx = 0.0f, ny = 0.0f;

    Require(RequireNodeCenter(host, "BoostBtn", boostX, boostY), "05 BoostBtn missing");
    Require(RequireNodeCenter(host, "HealthFill", hx, hy, &healthId), "05 HealthFill missing");
    Require(RequireNodeCenter(host, "EnergyFill", ex, ey, &energyId), "05 EnergyFill missing");
    Require(RequireNodeCenter(host, "SpeedNeedle", nx, ny, &needleId), "05 SpeedNeedle missing");

    // 1. Click BoostBtn -> triggers [SimCommand] ApplyAfterburner across domains
    float thrustBefore = 0.0f;
    (void)ReadFloatMember(host, simObj, "Thrust", thrustBefore);
    host.InjectClick(boostX, boostY);
    host.StepFrame(0.016f, true);
    float thrustAfter = 0.0f;
    std::int64_t cmdCount = 0;
    Require(ReadFloatMember(host, simObj, "Thrust", thrustAfter) && thrustAfter > thrustBefore + 20.0f,
        "05 [SimCommand] ApplyAfterburner did not update SimTelemetryState.Thrust");
    Require(ReadIntMember(host, simObj, "CommandCount", cmdCount) && cmdCount >= 1,
        "05 [SimCommand] CommandCount did not increment");

    // Capture pre-Track-B frame BEFORE binding/mutating ECS float columns
    std::vector<std::uint8_t> preTrackBFrame;
    Require(host.RenderHeadlessRgba(860u, 560u, preTrackBFrame), "05 pre-TrackB render failed");

    // 2. Track-B zero-alloc C++ ECS columns bound to HUD bars & attitude needle
    std::vector<float> ecsHealth{112.0f};
    std::vector<float> ecsEnergy{340.0f};
    std::vector<float> ecsHeading{142.0f};

    Require(host.BindSimFloatColumn(healthId, aero::VisualAnimProp::Width, ecsHealth.data(), 0u, 1u),
        "05 BindSimFloatColumn HealthFill failed");
    Require(host.BindSimFloatColumn(energyId, aero::VisualAnimProp::Width, ecsEnergy.data(), 0u, 1u),
        "05 BindSimFloatColumn EnergyFill failed");
    Require(host.BindSimFloatColumn(needleId, aero::VisualAnimProp::RotateDeg, ecsHeading.data(), 0u, 1u),
        "05 BindSimFloatColumn SpeedNeedle failed");

    bool tickInvoked = false;
    host.SetSimTickCallback([&](float, aero::AeroAppHost&) {
        tickInvoked = true;
        ecsHealth[0] = 88.0f;
        ecsEnergy[0] = 356.0f;
        ecsHeading[0] = 175.0f;
    });

    host.StepFrame(0.016f, true);
    Require(tickInvoked, "05 SimTickCallback was not invoked");

    // Ensure Track-B applied immediately without allocation
    (void)host.World().FlushTrackB();
    host.World().UpdateLayout(860.0f, 560.0f);
    const aero::VisualNode* healthNode = host.World().GetNode(healthId);
    const aero::VisualNode* needleNode = host.World().GetNode(needleId);
    Require(healthNode && std::fabs(healthNode->layout.width - 88.0f) < 0.1f,
        "05 Track-B HealthFill width did not update to 88.0f");
    Require(needleNode && std::fabs(needleNode->transform2D.rotDeg - 175.0f) < 0.1f,
        "05 Track-B SpeedNeedle rotation did not update to 175.0f");

    std::vector<std::uint8_t> postTrackBFrame;
    Require(host.RenderHeadlessRgba(860u, 560u, postTrackBFrame), "05 post-TrackB render failed");
    Require(CountChangedPixels(preTrackBFrame, postTrackBFrame) > 50u,
        "05 Track-B HUD mutation did not alter rendered framebuffer pixels");
}

void TestRegressionsFixed(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] RegressionsFixed (ManagedAnim / SVG / TextBlock / XamlReader / Lifecycle)...\n";

    const std::string aotXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
        x:Class="RegTestDemo.MainWindow"
        Title="Regression Tests" Width="800" Height="600">
  <Window.Resources>
    <Style x:Key="AnimatedBtnStyle" TargetType="Button">
      <Setter Property="Background" Value="#FFFF0000"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="InnerBorder" Background="{TemplateBinding Background}" Width="120" Height="40"/>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>
  <StackPanel Orientation="Vertical" Spacing="8" Margin="16">
    <TextBlock x:Name="TbNoSpace"><Run Text="Alpha"/><Run Text="Beta"/></TextBlock>
    <TextBlock x:Name="TbSpaced"><Run Text="Alpha"/> <Run Text="Beta"/></TextBlock>
    <TextBlock x:Name="TbMixed">Hello <Bold>World</Bold>!</TextBlock>
    <TextBlock x:Name="TbCjk"><Run Text="中"/><Run Text="文"/> <Run Text="字"/></TextBlock>

    <Path x:Name="PlasmaPath" Width="120" Height="60"
          Data="M 0,0 C 10,20 30,40 50,60 L .5.25 M 5,5 L 15,25">
      <Path.Fill>
        <ImageBrush Stretch="Fill" TileMode="Tile" Viewport="0,0,120,60">
          <noesis:Brush.Shader>
            <noesis:PlasmaBrush ScaleX="48" ScaleY="48" Seed="77" Color="#FF00FF88" Time="0.1"/>
          </noesis:Brush.Shader>
        </ImageBrush>
      </Path.Fill>
    </Path>

    <Button x:Name="TplBtn" Style="{StaticResource AnimatedBtnStyle}"/>

    <Border x:Name="RuntimeHost"/>
    <StackPanel x:Name="DisposeHost">
      <TextBlock x:Name="DisposedTb" Text="{Binding Label}"/>
    </StackPanel>
  </StackPanel>
</Window>)XML";

    const std::string aotRs = R"RS(
module RegTestDemo;
import Aero.UI;

public class RegVm {
    public string Label { get; set; } = "Initial";
}

public partial class MainWindow : Window {
    public RegVm Vm;
    public TextBlock RtNoSpace;
    public TextBlock RtSpaced;
    public TextBlock RtMixed;
    public Path RtPlasma;
    public RiveControl RtRive;
    public Border RtTrigBorder;
    public TextBlock RtDisposedTb;

    public MainWindow() {
        InitializeComponent();
        Vm = new RegVm();
        DataContext = Vm;

        var infSb = new Storyboard();
        infSb.RepeatForever = true;
        var pa = new DoubleAnimation(0.1f, 5.0f, 1.0f);
        Storyboard.SetTargetName(pa, "PlasmaPath");
        Storyboard.SetTargetProperty(pa, "Fill.PlasmaTime");
        infSb.Add(pa);

        var pathSb = new Storyboard();
        var da1 = new DoubleAnimation(50.0f, 90.0f, 0.4f);
        Storyboard.SetTargetName(da1, "PlasmaPath");
        Storyboard.SetTargetProperty(da1, "Data.Fig0.Seg0.Point.X");
        pathSb.Add(da1);
        var da2 = new DoubleAnimation(25.0f, 75.0f, 0.4f);
        Storyboard.SetTargetName(da2, "PlasmaPath");
        Storyboard.SetTargetProperty(da2, "Data.Fig1.Seg0.Point.Y");
        pathSb.Add(da2);
        var ca = new ColorAnimation("#FFFF0000", "#FF0000FF", 0.4f);
        Storyboard.SetTargetName(ca, "TplBtn");
        Storyboard.SetTargetProperty(ca, "Background");
        pathSb.Add(ca);

        Loaded += (object s, RoutedEventArgs e) => {
            infSb.Begin(this);
            pathSb.Begin(this);
        };

        string rtXml = "<StackPanel xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' " +
            "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' " +
            "xmlns:noesis='clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions'>" +
            "<TextBlock x:Name='RtNoSpace'><Run Text='Alpha'/><Run Text='Beta'/></TextBlock>" +
            "<TextBlock x:Name='RtSpaced'><Run Text='Alpha'/> <Run Text='Beta'/></TextBlock>" +
            "<TextBlock x:Name='RtMixed'>Hello <Bold>World</Bold>!</TextBlock>" +
            "<Path x:Name='RtPlasma' Width='80' Height='40' Data='M 0,0 L 80,40'>" +
            "<Path.Fill><ImageBrush TileMode='Tile' Viewport='0,0,80,40'><noesis:Brush.Shader><noesis:PlasmaBrush Seed='42' Color='#FFFF2200'/></noesis:Brush.Shader></ImageBrush></Path.Fill>" +
            "</Path>" +
            "<noesis:RiveControl x:Name='RtRive' Source='rive://logo.riv'><noesis:RiveInput InputName='Progress' InputValue='42'/></noesis:RiveControl>" +
            "<Border x:Name='RtTrigBorder' Opacity='1.0' Tag='collapse'>" +
            "<Border.Style><Style TargetType='Border'><Style.Triggers>" +
            "<Trigger Property='FrameworkElement.Tag' Value='collapse'><Setter Property='UIElement.Opacity' Value='0.25'/></Trigger>" +
            "</Style.Triggers></Style></Border.Style>" +
            "</Border></StackPanel>";
        UIElement parsed = XamlReader.Parse(rtXml);
        if (parsed != null) {
            RuntimeHost.AddChild(parsed);
            RtNoSpace = parsed.FindName("RtNoSpace") as TextBlock;
            RtSpaced = parsed.FindName("RtSpaced") as TextBlock;
            RtMixed = parsed.FindName("RtMixed") as TextBlock;
            RtPlasma = parsed.FindName("RtPlasma") as Path;
            RtRive = parsed.FindName("RtRive") as RiveControl;
            RtTrigBorder = parsed.FindName("RtTrigBorder") as Border;
        }
        RtDisposedTb = DisposedTb;
    }

    public string RunDisposeCheck() {
        BindingEngine.Flush();
        string before = RtDisposedTb != null ? RtDisposedTb.Text : "";
        DisposeHost.DisposeSubtree();
        Vm.Label = "AfterDispose";
        BindingEngine.Flush();
        string after = RtDisposedTb != null ? RtDisposedTb.Text : "";
        return before + "|" + after;
    }

    public string GetRtNoSpaceText() { return RtNoSpace != null ? RtNoSpace.Text : ""; }
    public string GetRtSpacedText() { return RtSpaced != null ? RtSpaced.Text : ""; }
    public string GetRtMixedText() { return RtMixed != null ? RtMixed.Text : ""; }
    public string GetRtPlasmaUri() {
        return (RtPlasma != null && RtPlasma.Fill is ImageBrush) ? (RtPlasma.Fill as ImageBrush).ImageSource : "";
    }
    public string GetRtPlasmaTile() {
        return (RtPlasma != null && RtPlasma.Fill is ImageBrush) ? (RtPlasma.Fill as ImageBrush).TileMode : "";
    }
    public int GetRtRiveInputCount() { return RtRive != null ? RtRive.Inputs().Length : -1; }
    public string GetRtRiveInputName() {
        return (RtRive != null && RtRive.Inputs().Length > 0) ? RtRive.Inputs()[0].InputName : "";
    }
    public float GetRtTrigOpacity() { return RtTrigBorder != null ? RtTrigBorder.Opacity : -1.0f; }
    public string GetDisposedTbText() { return RtDisposedTb != null ? RtDisposedTb.Text : ""; }
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "RegTestDemo/MainWindow.xaml";
    input.xamlContent = aotXaml;
    input.scriptSources.push_back(aero::SourceFile{"RegTestDemo/MainWindow.rs", aotRs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 800u;
    cfg.viewportHeight = 600u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    Require(host.CompileAndLoad(input, &err), "RegTestDemo CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);

    auto getNodeText = [&](std::string_view name) -> std::string {
        const aero::VisualNodeId nid = host.FindVisualByOwnerName(name);
        const aero::VisualNode* nd = host.World().GetNode(nid);
        return nd ? nd->Text().text : std::string{};
    };
    Require(getNodeText("TbNoSpace") == "AlphaBeta", "AOT TextBlock adjacent Runs unexpected: " + getNodeText("TbNoSpace"));
    Require(getNodeText("TbSpaced") == "Alpha Beta", "AOT TextBlock spaced Runs unexpected: " + getNodeText("TbSpaced"));
    Require(getNodeText("TbMixed") == "Hello World!", "AOT TextBlock mixed text/Bold unexpected: " + getNodeText("TbMixed"));
    Require(getNodeText("TbCjk") == "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97",
        "AOT TextBlock CJK unexpected: " + getNodeText("TbCjk"));

    // Check XamlReader.Parse results via UI root object
    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "RegTestDemo RootScriptObject invalid");
    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callInt = [&](std::string_view fn) -> std::int64_t {
        aero::UnifiedValue rv{};
        std::int64_t out = -1;
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            (void)rv.tryGetInt64(out);
        }
        return out;
    };
    auto callFloat = [&](std::string_view fn) -> float {
        aero::UnifiedValue rv{};
        float out = -1.0f;
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (rv.tryGetFloat(out)) { return out; }
            double dv = 0.0;
            if (rv.tryGetDouble(dv)) { return static_cast<float>(dv); }
        }
        return out;
    };

    Require(callStr("GetRtNoSpaceText") == "AlphaBeta", "Runtime XamlReader NoSpace failed");
    Require(callStr("GetRtSpacedText") == "Alpha Beta", "Runtime XamlReader Spaced failed");
    Require(callStr("GetRtMixedText") == "Hello World!", "Runtime XamlReader Mixed failed");
    Require(callStr("GetRtPlasmaUri").rfind("shader://plasma?", 0) == 0, "Runtime XamlReader PlasmaBrush failed");
    Require(callStr("GetRtPlasmaTile") == "Tile", "Runtime XamlReader TileMode failed");
    Require(callInt("GetRtRiveInputCount") == 1 && callStr("GetRtRiveInputName") == "Progress",
        "Runtime XamlReader RiveControl/RiveInput failed");
    Require(std::fabs(callFloat("GetRtTrigOpacity") - 0.25f) < 0.02f,
        "Runtime XamlReader NormalizeSetterProp Trigger failed");
    const std::string disposedTxt = callStr("RunDisposeCheck");
    Require(disposedTxt == "Initial|Initial",
        "DisposeSubtree() + BindingEngine.Flush() should ignore updates after disposal, got='" + disposedTxt + "'");

    // Step 0.20s across frames to verify managed infinite Storyboard (PlasmaTime), SVG Path Data anim, and TplBtn ColorAnimation
    const aero::VisualNodeId plasmaPathId = host.FindVisualByOwnerName("PlasmaPath");
    aero::VisualNodeId innerBorderId = host.FindVisualByOwnerName("InnerBorder");
    if (!innerBorderId.IsValid()) {
        const aero::VisualNodeId btnId = host.FindVisualByOwnerName("TplBtn");
        if (const aero::VisualNode* btnNode = host.World().GetNode(btnId); btnNode && !btnNode->children.empty()) {
            innerBorderId = btnNode->children[0];
        }
    }
    const aero::VisualNode* pNode0 = host.World().GetNode(plasmaPathId);
    const std::string uri0 = pNode0 ? pNode0->Image().uri : std::string{};
    const bool plasmaActive0 = pNode0 && pNode0->Image().plasma.active;
    const float plasmaTime0 = pNode0 ? pNode0->Image().plasma.timeSec : 0.0f;
    const std::string data0 = pNode0 ? pNode0->Vector().pathOrParams : std::string{};
    const std::size_t activeBefore = host.AnimEngine().ActiveTrackCount();
    Require(pNode0 && pNode0->Image().tileWrap, "AOT Path ImageBrush TileMode=Tile should set tileWrap");

    for (int f = 0; f < 10; ++f) {
        host.StepFrame(0.020f, true);
    }
    const aero::VisualNode* pNodeMid = host.World().GetNode(plasmaPathId);
    const aero::VisualNode* bNodeMid = host.World().GetNode(innerBorderId);
    const std::string uriMid = pNodeMid ? pNodeMid->Image().uri : std::string{};
    const bool plasmaActiveMid = pNodeMid && pNodeMid->Image().plasma.active;
    const float plasmaTimeMid = pNodeMid ? pNodeMid->Image().plasma.timeSec : 0.0f;
    const std::string dataMid = pNodeMid ? pNodeMid->Vector().pathOrParams : std::string{};
    if (plasmaActive0 || plasmaActiveMid) {
        Require(plasmaActive0 && plasmaActiveMid && std::fabs(plasmaTimeMid - plasmaTime0) > 0.05f,
            "Infinite Storyboard Fill.PlasmaTime did not tick: tracksBefore=" + std::to_string(activeBefore) +
            " after=" + std::to_string(host.AnimEngine().ActiveTrackCount()) +
            " t0=" + std::to_string(plasmaTime0) + " tMid=" + std::to_string(plasmaTimeMid));
    } else {
        Require(uriMid != uri0,
            "Infinite Storyboard Fill.PlasmaTime did not tick: tracksBefore=" + std::to_string(activeBefore) +
            " after=" + std::to_string(host.AnimEngine().ActiveTrackCount()) + " uri0=" + uri0 + " uriMid=" + uriMid);
    }
    Require(dataMid != data0 && dataMid.find(".5.25") != std::string::npos,
        "Command-aware SVG Path animation failed: data0=" + data0 + " dataMid=" + dataMid);
    if (bNodeMid) {
        const std::uint32_t midCol = bNodeMid->box.bgRgba;
        const std::uint32_t midR = midCol & 0xFFu;
        const std::uint32_t midB = (midCol >> 16u) & 0xFFu;
        Require(midR > 15u && midR < 240u && midB > 15u && midB < 240u,
            "Templated Control Background ColorAnimation unexpected bgRgba=0x" + std::to_string(midCol));
    } else {
        Require(false, "InnerBorder from ControlTemplate not found");
    }
}

void TestGalleryEngineRuntimeFixes(aero::rs_comp::CompilerSession& session) {
    (void)session;
    std::cout << "[Test] GalleryEngineRuntimeFixes (DecodeXmlText / InlineBinding / ThreeState / Style.Resources / TreeView / ComboBox)...\n";

    const std::string aotXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="GalleryRuntimeFixDemo.MainWindow"
        Title="Gallery Runtime Fixes" Width="800" Height="600">
  <StackPanel x:Name="RuntimeHost" Orientation="Vertical" Spacing="8" Margin="16">
    <Button x:Name="AotTipBtn" Content="TipHost" ToolTip="AotTipText" Width="80" Height="28"/>
    <Border x:Name="RotNoOriginBorder" Width="40" Height="40"/>
  </StackPanel>
</Window>)XML";

    const std::string aotRs = R"RS(
module GalleryRuntimeFixDemo;
import Aero.UI;
import RealScript.Math;

public class GalleryFixVm {
    public string AccentBrush { get; set; } = "#FFFF0000";
}

public partial class MainWindow : Window {
    public GalleryFixVm Vm;
    public TextBlock EntTb;
    public TextBlock InlineTb;
    public ToggleButton TriTb;
    public ToolBar HostTb;
    public Button TbChildBtn;
    public TreeView TvCheck;
    public ComboBox CbComposite;
    public TextBlock TrigSrcTb;
    public Border TrigBindTarget;
    public ToggleButton StyledTb;
    public bool PopLogicalCleared = false;
    public bool MaxLinesResetOk = false;
    public bool RotateMathOk = false;

    public MainWindow() {
        InitializeComponent();
        Vm = new GalleryFixVm();
        DataContext = Vm;

        RotNoOriginBorder.RenderTransform = new RotateTransform(90.0f, 0.0f, 0.0f);
        var rtd = new TransformData();
        var rCheck = new RotateTransform(90.0f, 10.0f, 10.0f);
        rCheck.Accumulate(rtd);
        RotateMathOk = rtd.HasExplicitCenter && Math.Absf(rtd.TX - 20.0f) < 0.02f && Math.Absf(rtd.TY) < 0.02f;

        var tbLines = new TextBox();
        tbLines.MaxLines = 2;
        bool boundedH = ParseUtil.IsFinite(tbLines.MaxHeight);
        tbLines.MaxLines = 0;
        MaxLinesResetOk = boundedH && !ParseUtil.IsFinite(tbLines.MaxHeight);

        var pop = new Popup();
        pop.PlacementTarget = AotTipBtn;
        bool popSet = (pop.LogicalParent != null && pop.LogicalParent.Name == "AotTipBtn");
        pop.PlacementTarget = null;
        PopLogicalCleared = popSet && (pop.LogicalParent == null);

        string rtXml = "<StackPanel xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' " +
            "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>" +
            "<StackPanel.Resources>" +
              "<Style TargetType='ToolBar'>" +
                "<Style.Resources>" +
                  "<Style x:Key='{x:Static ToolBar.ButtonStyleKey}' TargetType='Button'>" +
                    "<Setter Property='Tag' Value='ToolBarBtnOk'/>" +
                  "</Style>" +
                "</Style.Resources>" +
              "</Style>" +
              "<Style TargetType='ToggleButton'>" +
                "<Setter Property='CornerRadius' Value='2'/>" +
                "<Setter Property='Background' Value='#FF112233'/>" +
              "</Style>" +
            "</StackPanel.Resources>" +
            "<TextBlock x:Name='EntTb'>&#38;lt;ok&#38;gt;</TextBlock>" +
            "<TextBlock x:Name='InlineTb'><Bold Foreground='{Binding AccentBrush}'>foo </Bold>bar</TextBlock>" +
            "<ToggleButton x:Name='TriTb' IsThreeState='True'/>" +
            "<ToggleButton x:Name='StyledTb' Content='Styled'/>" +
            "<ToolBar x:Name='HostTb'><Button x:Name='TbChildBtn' Content='Act' Style='{DynamicResource {x:Static ToolBar.ButtonStyleKey}}'/></ToolBar>" +
            "<TreeView x:Name='TvCheck'><TransformGroup Selector.IsSelected='True'/></TreeView>" +
            "<ComboBox x:Name='CbComposite' SelectedIndex='0'><ComboBoxItem><StackPanel Orientation='Horizontal'><TextBlock Text='Alpha'/><TextBlock Text='Beta'/></StackPanel></ComboBoxItem></ComboBox>" +
            "<TextBlock x:Name='TrigSrcTb' Text='FromSrc'/>" +
            "<Border x:Name='TrigBindTarget' Tag='BaseTag'>" +
              "<Border.Style>" +
                "<Style TargetType='Border'>" +
                  "<Style.Triggers>" +
                    "<Trigger Property='IsEnabled' Value='False'>" +
                      "<Setter Property='Tag' Value='{Binding Text, ElementName=TrigSrcTb}'/>" +
                    "</Trigger>" +
                  "</Style.Triggers>" +
                "</Style>" +
              "</Border.Style>" +
            "</Border>" +
            "</StackPanel>";
        UIElement parsed = XamlReader.Parse(rtXml);
        if (parsed != null) {
            RuntimeHost.AddChild(parsed);
            EntTb = parsed.FindName("EntTb") as TextBlock;
            InlineTb = parsed.FindName("InlineTb") as TextBlock;
            TriTb = parsed.FindName("TriTb") as ToggleButton;
            StyledTb = parsed.FindName("StyledTb") as ToggleButton;
            HostTb = parsed.FindName("HostTb") as ToolBar;
            TbChildBtn = parsed.FindName("TbChildBtn") as Button;
            TvCheck = parsed.FindName("TvCheck") as TreeView;
            CbComposite = parsed.FindName("CbComposite") as ComboBox;
            TrigSrcTb = parsed.FindName("TrigSrcTb") as TextBlock;
            TrigBindTarget = parsed.FindName("TrigBindTarget") as Border;
        }
    }

    public string GetEntText() { return EntTb != null ? EntTb.Text : ""; }
    public string GetInlineText() { return InlineTb != null ? InlineTb.Text : ""; }
    public int CheckInlineBindingRgba() {
        BindingEngine.Flush();
        if (InlineTb == null || InlineTb.Inlines().Length == 0) { return -1; }
        Brush fg = InlineTb.Inlines()[0].Foreground;
        if (fg is SolidColorBrush) {
            return (fg as SolidColorBrush).ResolveRgba() == ParseUtil.ParseColor("#FFFF0000") ? 1 : 0;
        }
        return -2;
    }
    public string CheckTriIndeterminateNull() {
        if (TriTb == null) { return "MISSING"; }
        TriTb.IsIndeterminate = true;
        BindingEngine.Flush();
        DependencyObject box = BindingEngine.ReadPathBox(TriTb, "IsChecked");
        return box == null ? "NULL" : "NOTNULL";
    }
    public string GetToolBarBtnTag() {
        if (TbChildBtn == null && HostTb != null) {
            TbChildBtn = HostTb.FindName("TbChildBtn") as Button;
        }
        if (TbChildBtn == null) { return "MISSING"; }
        DependencyObject res = TbChildBtn.FindResourceBox("ToolBar.ButtonStyleKey");
        string hasRes = res != null ? "FOUND|" : "NORES|";
        string tagStr = DependencyObject.UnboxString(TbChildBtn.GetPropertyValue("Tag"), "");
        return hasRes + tagStr;
    }
    public string CheckTreeViewNotSelected() {
        if (TvCheck == null) { return "MISSING"; }
        return (TvCheck.ItemCount() == 1 && TvCheck.GetPropertyValue("SelectedItem") == null) ? "OK" : "SELECTED";
    }
    public string GetComboCompositeText() {
        return CbComposite != null ? CbComposite.Text : "";
    }
    public string CheckNewReviewFixes() {
        if (AotTipBtn == null || !AotTipBtn.HasToolTip) { return "ERR_TIP_MISSING"; }
        string tipVal = DependencyObject.UnboxString(AotTipBtn.GetPropertyValue("ToolTip"), "");
        if (tipVal != "AotTipText") { return "ERR_TIP_VAL=" + tipVal; }
        var dynBtn = new Button();
        dynBtn.SetToolTip("DynTipText");
        if (!dynBtn.HasToolTip) { return "ERR_DYN_TIP"; }
        if (!RotateMathOk) { return "ERR_ROT_MATH"; }
        if (!MaxLinesResetOk) { return "ERR_MAXLINES"; }
        if (!PopLogicalCleared) { return "ERR_POP_LP"; }
        if (StyledTb == null) { return "ERR_STYLED_TB"; }
        StyledTb.IsChecked = true;
        bool crOk = StyledTb.CornerRadius != null && Math.Absf(StyledTb.CornerRadius.TopLeft - 2.0f) < 0.01f;
        bool bgOk = StyledTb.Background is SolidColorBrush &&
            (StyledTb.Background as SolidColorBrush).ResolveRgba() == ParseUtil.ParseColor("#FF112233");
        if (!crOk || !bgOk) { return "ERR_IMPLICIT_TOGGLE"; }
        if (TrigBindTarget == null || TrigSrcTb == null) { return "ERR_TRIG_NULL"; }
        if (BindingEngine.HasTriggerBinding(TrigBindTarget, "Tag")) { return "ERR_TRIG_PREBOUND"; }
        TrigBindTarget.IsEnabled = false;
        TrigBindTarget.ApplyStyleInternal();
        BindingEngine.Flush();
        string onVal1 = DependencyObject.UnboxString(TrigBindTarget.GetPropertyValue("Tag"), "");
        if (!BindingEngine.HasTriggerBinding(TrigBindTarget, "Tag") || onVal1 != "FromSrc") {
            return "ERR_TRIG_ON1=" + onVal1;
        }
        TrigSrcTb.Text = "UpdatedSrc";
        TrigBindTarget.ApplyStyleInternal();
        BindingEngine.Flush();
        string onVal2 = DependencyObject.UnboxString(TrigBindTarget.GetPropertyValue("Tag"), "");
        if (onVal2 != "UpdatedSrc") { return "ERR_TRIG_ON2=" + onVal2; }
        TrigBindTarget.IsEnabled = true;
        TrigBindTarget.ApplyStyleInternal();
        BindingEngine.Flush();
        string offVal = DependencyObject.UnboxString(TrigBindTarget.GetPropertyValue("Tag"), "");
        if (BindingEngine.HasTriggerBinding(TrigBindTarget, "Tag") || offVal != "BaseTag") {
            return "ERR_TRIG_OFF=" + offVal;
        }
        TrigSrcTb.Text = "AfterDeact";
        BindingEngine.Flush();
        string afterVal = DependencyObject.UnboxString(TrigBindTarget.GetPropertyValue("Tag"), "");
        if (afterVal != "BaseTag") { return "ERR_TRIG_LEAK=" + afterVal; }
        return "OK";
    }
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "GalleryRuntimeFixDemo/MainWindow.xaml";
    input.xamlContent = aotXaml;
    input.scriptSources.push_back(aero::SourceFile{"GalleryRuntimeFixDemo/MainWindow.rs", aotRs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 800u;
    cfg.viewportHeight = 600u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    const bool loaded = host.CompileAndLoad(input, &err);
    Require(loaded, "GalleryRuntimeFixDemo CompileAndLoad failed: " + err);
    if (!host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);

    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "GalleryRuntimeFixDemo RootScriptObject invalid");
    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callInt = [&](std::string_view fn) -> std::int64_t {
        aero::UnifiedValue rv{};
        std::int64_t out = -1;
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            (void)rv.tryGetInt64(out);
        }
        return out;
    };

    Require(callStr("GetEntText") == "&lt;ok&gt;",
        "Single-pass DecodeXmlText unexpected: " + callStr("GetEntText"));
    Require(callStr("GetInlineText") == "foo bar",
        "Inline trailing whitespace unexpected: " + callStr("GetInlineText"));
    Require(callInt("CheckInlineBindingRgba") == 1,
        "Inline inherited Foreground binding failed: code=" + std::to_string(callInt("CheckInlineBindingRgba")));
    Require(callStr("CheckTriIndeterminateNull") == "NULL",
        "ThreeState indeterminate ToggleButton IsChecked binding read should be NULL: " + callStr("CheckTriIndeterminateNull"));
    Require(callStr("GetToolBarBtnTag") == "FOUND|ToolBarBtnOk",
        "Style.Resources x:Static ToolBar.ButtonStyleKey resolution unexpected: " + callStr("GetToolBarBtnTag"));
    Require(callStr("CheckTreeViewNotSelected") == "OK",
        "TreeView TransformGroup Selector.IsSelected must not spuriously select TreeViewItem: " + callStr("CheckTreeViewNotSelected"));
    const std::string cbText = callStr("GetComboCompositeText");
    Require(cbText.find("Alpha") != std::string::npos && cbText.find("Beta") != std::string::npos,
        "ComboBox composite StackPanel preview text unexpected: " + cbText);
    const std::string fixRes = callStr("CheckNewReviewFixes");
    Require(fixRes == "OK", "CheckNewReviewFixes failed: " + fixRes);
    const aero::VisualNode* rotNode = host.World().GetNode(host.FindVisualByOwnerName("RotNoOriginBorder"));
    Require(rotNode && std::fabs(rotNode->transform2D.originX) < 1e-4f &&
            std::fabs(rotNode->transform2D.originY) < 1e-4f &&
            std::fabs(rotNode->transform2D.rotDeg - 90.0f) < 1e-3f,
            "RotateTransform with explicit Center(0,0) and no explicit RenderTransformOrigin must pivot at (0,0)");
}

void TestPhase1OptimizationRegressions(aero::rs_comp::CompilerSession& /*sharedSession*/) {
    std::cout << "[Test] Phase1OptimizationRegressions (Window [Observable] / BumpPropEpoch / DataTrigger overlap / BehaviorDataTrigger dispose)...\n";

    const std::string aotXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:b="http://schemas.microsoft.com/xaml/behaviors"
        x:Name="RootWin"
        x:Class="Phase1RegDemo.MainWindow"
        Title="Phase1 Regression Demo" Width="640" Height="480">
  <Window.Resources>
    <Style x:Key="TrigBorderStyle" TargetType="Border">
      <Setter Property="Background" Value="#FF444444"/>
      <Style.Triggers>
        <DataTrigger Binding="{Binding FlagA}" Value="True">
          <Setter Property="Background" Value="#FFFFD700"/>
        </DataTrigger>
        <DataTrigger Binding="{Binding FlagB}" Value="True">
          <Setter Property="Background" Value="#FF0088FF"/>
        </DataTrigger>
      </Style.Triggers>
    </Style>
  </Window.Resources>
  <StackPanel Orientation="Vertical" Spacing="8">
    <TextBlock x:Name="WinDcBannerTxt" Text="{Binding WinBanner}"/>
    <TextBlock x:Name="WinElBannerTxt" Text="{Binding WinBanner, ElementName=RootWin}"/>

    <Button x:Name="FgBtn" Content="Btn" Foreground="#FF112233">
      <Button.Template>
        <ControlTemplate TargetType="Button">
          <Border x:Name="BtnTplBd" Background="{TemplateBinding Foreground}"/>
        </ControlTemplate>
      </Button.Template>
    </Button>

    <GroupBox x:Name="HdrBox" Header="H1">
      <GroupBox.Template>
        <ControlTemplate TargetType="GroupBox">
          <TextBlock x:Name="GrpTplHdrTb" Text="{TemplateBinding Header}"/>
        </ControlTemplate>
      </GroupBox.Template>
    </GroupBox>

    <Border x:Name="TrigBorder" Style="{StaticResource TrigBorderStyle}"/>

    <StackPanel x:Name="BehHost">
      <Border x:Name="BehChild1" Tag="Init1">
        <b:Interaction.Triggers>
          <b:DataTrigger Binding="{Binding PulseCount}" Value="2">
            <b:ChangePropertyAction PropertyName="Tag" Value="Fired1"/>
          </b:DataTrigger>
        </b:Interaction.Triggers>
      </Border>
      <Border x:Name="BehChild2" Tag="Init2">
        <b:Interaction.Triggers>
          <b:DataTrigger Binding="{Binding PulseCount}" Value="2">
            <b:ChangePropertyAction PropertyName="Tag" Value="Fired2"/>
          </b:DataTrigger>
        </b:Interaction.Triggers>
      </Border>
    </StackPanel>
  </StackPanel>
</Window>)XML";

    const std::string aotRs = R"RS(
module Phase1RegDemo;
import Aero.UI;

[Observable]
public class Phase1Vm {
    public bool FlagA { get; set; } = true;
    public bool FlagB { get; set; } = true;
    public int PulseCount { get; set; } = 0;
}

public partial class MainWindow : Window {
    public string WinBanner { get; set; } = "Init";
    public Phase1Vm Vm;

    public MainWindow() {
        InitializeComponent();
        Vm = new Phase1Vm();
        DataContext = this;
        TrigBorder.DataContext = Vm;
        BehHost.DataContext = Vm;
    }

    public string GetWinBannerDc() { return WinDcBannerTxt != null ? WinDcBannerTxt.Text : ""; }
    public string GetWinBannerEl() { return WinElBannerTxt != null ? WinElBannerTxt.Text : ""; }

    public string GetBtnTplColor() {
        UIElement b = FgBtn != null ? FgBtn.GetTemplateChild("BtnTplBd") : null;
        if (b is Border) {
            Border bd = b as Border;
            if (bd.Background is SolidColorBrush) {
                SolidColorBrush sb = bd.Background as SolidColorBrush;
                if (sb.ResolveRgba() == ParseUtil.ParseColor("#FFAABBCC")) {
                    return "UPDATED";
                }
                return "OTHER";
            }
        }
        return "MISSING";
    }

    public string GetGrpTplHeader() {
        UIElement tb = HdrBox != null ? HdrBox.GetTemplateChild("GrpTplHdrTb") : null;
        if (tb is TextBlock) { return (tb as TextBlock).Text; }
        return "";
    }

    public string GetTrigColorTag() {
        if (TrigBorder != null && TrigBorder.Background is SolidColorBrush) {
            SolidColorBrush sb = TrigBorder.Background as SolidColorBrush;
            uint rgba = sb.ResolveRgba();
            if (rgba == ParseUtil.ParseColor("#FF0088FF")) { return "BLUE"; }
            if (rgba == ParseUtil.ParseColor("#FFFFD700")) { return "GOLD"; }
            if (rgba == ParseUtil.ParseColor("#FF444444")) { return "GRAY"; }
            return "OTHER";
        }
        return "MISSING";
    }

    public string GetBehChild2Tag() {
        return BehChild2 != null ? DependencyObject.UnboxString(BehChild2.GetPropertyValue("Tag"), "") : "";
    }

    public void MutateWinBtnGrpAndTriggerDropB() {
        WinBanner = "UpdatedBanner";
        FgBtn.Foreground = new SolidColorBrush("#FFAABBCC");
        HdrBox.SetHeader("H2");
        Vm.FlagB = false;
    }

    public void DisposeBehChild1() {
        if (BehChild1 != null) {
            BehHost.RemoveChild(BehChild1);
            BehChild1.DisposeSubtree();
        }
    }

    public void SetPulseTwo() {
        Vm.PulseCount = 2;
    }

    public void TriggerActivateAWhileBActive() {
        Vm.FlagA = false;
        Vm.FlagB = true;
    }

    public void TriggerEnableAAlso() {
        Vm.FlagA = true;
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "Phase1RegDemo/MainWindow.xaml";
    input.xamlContent = aotXaml;
    input.scriptSources.push_back(aero::SourceFile{"Phase1RegDemo/MainWindow.rs", aotRs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 800u;
    cfg.viewportHeight = 600u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    const bool loaded = host.CompileAndLoad(input, &err);
    Require(loaded, "Phase1RegDemo CompileAndLoad failed: " + err);
    if (!loaded || !host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);

    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "Phase1RegDemo RootScriptObject invalid");
    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callVoid = [&](std::string_view fn) {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation),
            std::string("Phase1RegDemo method failed: ") + std::string(fn));
    };

    // Frame 1 checks
    const std::string initDcBanner = callStr("GetWinBannerDc");
    const std::string initElBanner = callStr("GetWinBannerEl");
    Require(initDcBanner == "Init", "Window initial WinBanner DataContext binding failed: got='" + initDcBanner + "'");
    Require(initElBanner == "Init", "Window initial WinBanner ElementName binding failed: got='" + initElBanner + "'");
    Require(callStr("GetGrpTplHeader") == "H1", "GroupBox initial TemplateBinding Header failed");
    const std::string initTrigColor = callStr("GetTrigColorTag");
    Require(initTrigColor == "BLUE", "Initial overlapping DataTrigger B should win with BLUE, got=" + initTrigColor);

    // Mutate Window prop, Button.Foreground, GroupBox.Header, and deactivate FlagB while FlagA stays True
    callVoid("MutateWinBtnGrpAndTriggerDropB");
    host.StepFrame(0.016f, true);
    Require(callStr("GetWinBannerDc") == "UpdatedBanner",
        "Window custom [Observable] WinBanner DataContext binding did not update: " + callStr("GetWinBannerDc"));
    Require(callStr("GetWinBannerEl") == "UpdatedBanner",
        "Window custom [Observable] WinBanner ElementName binding did not update: " + callStr("GetWinBannerEl"));
    Require(callStr("GetBtnTplColor") == "UPDATED",
        "Button.Foreground TemplateBinding did not update after BumpPropEpoch: " + callStr("GetBtnTplColor"));
    Require(callStr("GetGrpTplHeader") == "H2",
        "GroupBox.Header TemplateBinding did not update after SetHeader: " + callStr("GetGrpTplHeader"));
    const std::string goldTrig = callStr("GetTrigColorTag");
    Require(goldTrig == "GOLD",
        "Surviving DataTrigger A did not re-apply GOLD when overlapping DataTrigger B deactivated, got=" + goldTrig);

    // Verify activating earlier FlagA while later FlagB stays active keeps FlagB (Blue) on top
    callVoid("TriggerActivateAWhileBActive");
    host.StepFrame(0.016f, true);
    const std::string bAloneTrig = callStr("GetTrigColorTag");
    Require(bAloneTrig == "BLUE", "DataTrigger B alone should apply BLUE, got=" + bAloneTrig);
    callVoid("TriggerEnableAAlso");
    host.StepFrame(0.016f, true);
    const std::string bStillTrig = callStr("GetTrigColorTag");
    Require(bStillTrig == "BLUE",
        "Higher-precedence active DataTrigger B should remain BLUE when earlier DataTrigger A activates, got=" + bStillTrig);

    // Verify BehaviorDataTrigger parallel cache arrays stay aligned after disposing first child target
    callVoid("DisposeBehChild1");
    host.StepFrame(0.016f, true);
    callVoid("SetPulseTwo");
    host.StepFrame(0.016f, true);
    Require(callStr("GetBehChild2Tag") == "Fired2",
        "Surviving BehaviorDataTrigger target failed to fire after BehChild1 DisposeSubtree: " + callStr("GetBehChild2Tag"));
}

void TestCollapsedSubtreeCatchupAndSelfDataContext(aero::rs_comp::CompilerSession& /*sharedSession*/) {
    std::cout << "[Test] CollapsedSubtreeCatchupAndSelfDataContext...\n";

    const std::string xaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="CollapsedRegDemo.MainWindow"
        Title="Collapsed Regression Demo" Width="640" Height="480">
  <Window.Resources>
    <BoolToVisibilityConverter x:Key="BoolToVis"/>
  </Window.Resources>
  <StackPanel Orientation="Vertical" Spacing="8">
    <StackPanel x:Name="PanelBox" Visibility="{Binding IsPanelOpen, Converter={StaticResource BoolToVis}}">
      <TextBlock x:Name="InnerText" Text="{Binding Counter}"/>
    </StackPanel>
    <Border x:Name="SelfCollapsedBorder"
            DataContext="{Binding Sub}"
            Visibility="{Binding Show, Converter={StaticResource BoolToVis}}">
      <TextBlock x:Name="SelfInnerText" Text="{Binding Label}"/>
    </Border>
  </StackPanel>
</Window>)XML";

    const std::string rs = R"RS(
module CollapsedRegDemo;
import Aero.UI;

[Observable]
public class SubVm {
    public string Label { get; set; } = "InitSub";
    public bool Show { get; set; } = false;
    public SubVm(string label, bool show) {
        Label = label;
        Show = show;
    }
}

[Observable]
public class MainVm {
    public bool IsPanelOpen { get; set; } = false;
    public string Counter { get; set; } = "InitCount";
    public SubVm Sub { get; set; }
    public MainVm() {
        Sub = new SubVm("HiddenSub", false);
    }
}

public partial class MainWindow : Window {
    public MainVm Vm;

    public MainWindow() {
        InitializeComponent();
        Vm = new MainVm();
        DataContext = Vm;
    }

    public string GetInnerText() {
        return InnerText != null ? InnerText.Text : "";
    }

    public string GetSelfInnerText() {
        return SelfInnerText != null ? SelfInnerText.Text : "";
    }

    public string GetSelfVisible() {
        return (SelfCollapsedBorder != null && SelfCollapsedBorder.Visibility == Visibility.Visible) ? "VISIBLE" : "COLLAPSED";
    }

    public void MutateWhileCollapsed() {
        Vm.Counter = "Updated42";
        Vm.Sub = new SubVm("LiveSub", true);
    }

    public void OpenPanel() {
        Vm.IsPanelOpen = true;
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "CollapsedRegDemo/MainWindow.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"CollapsedRegDemo/MainWindow.rs", rs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 640u;
    cfg.viewportHeight = 480u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    const bool loaded = host.CompileAndLoad(input, &err);
    Require(loaded, "CollapsedRegDemo CompileAndLoad failed: " + err);
    if (!loaded || !host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "CollapsedRegDemo RootScriptObject invalid");

    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callVoid = [&](std::string_view fn) {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation),
            std::string("CollapsedRegDemo method failed: ") + std::string(fn));
    };

    Require(callStr("GetInnerText") == "InitCount", "Initial collapsed PanelBox InnerText unexpected");
    Require(callStr("GetSelfVisible") == "COLLAPSED", "SelfCollapsedBorder should start Collapsed");

    // Mutate Counter & Sub while PanelBox is collapsed and SelfCollapsedBorder is self-collapsed
    callVoid("MutateWhileCollapsed");
    host.StepFrame(0.016f, true);

    // SelfCollapsedBorder must evaluate its DataContext & Visibility even while it was self-Collapsed
    Require(callStr("GetSelfVisible") == "VISIBLE", "SelfCollapsedBorder did not transition from Collapsed to Visible");
    Require(callStr("GetSelfInnerText") == "LiveSub", "SelfCollapsedBorder child TextBlock did not catch up on uncollapse");
    // PanelBox is still collapsed, so InnerText remained skipped
    Require(callStr("GetInnerText") == "InitCount", "Collapsed PanelBox child should stay skipped while collapsed");

    // Uncollapse PanelBox -> BindingEngine.FlushSubtree must immediately catch InnerText up to Updated42
    callVoid("OpenPanel");
    host.StepFrame(0.016f, true);
    Require(callStr("GetInnerText") == "Updated42", "InnerText did not catch up after PanelBox transitioned Collapsed -> Visible");
}

void TestPhase0MemoryAndBindingShortCircuit(aero::rs_comp::CompilerSession&) {
    std::cout << "[v3_controls_and_samples_tests] TestPhase0MemoryAndBindingShortCircuit...\n";
    const std::string xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Phase0Reg.MainWindow"
        Width="640" Height="480" Title="Phase0">
    <StackPanel>
        <TextBlock x:Name="TitleText" Text="{Binding Title}" />
        <TextBlock x:Name="SrcText" Text="InitElem" />
        <TextBlock x:Name="MirrorText" Text="{Binding Text, ElementName=SrcText}" />
    </StackPanel>
</Window>
)XAML";

    const std::string rs = R"RS(
module Phase0Reg;
import Aero.UI;

[Observable]
public class Phase0Vm {
    public string Title { get; set; } = "InitTitle";
    public int Count { get; set; } = 1;
}

public partial class MainWindow : Window {
    public Phase0Vm Vm;
    private BoxedFloat _heldBoxedFloat;
    private Thickness _heldThickness;

    public MainWindow() {
        InitializeComponent();
        Vm = new Phase0Vm();
        DataContext = Vm;
        _heldBoxedFloat = new BoxedFloat(42.5f);
        _heldThickness = new Thickness(1.0f, 2.0f, 3.0f, 4.0f);
    }

    public void ResetFlushStats() { BindingEngine.DebugResetFlushStats(); }
    public int GetShortCircuitCount() { return BindingEngine.DebugShortCircuitFlushCount(); }
    public int GetEvaluatedCount() { return BindingEngine.DebugEvaluatedEntryCount(); }
    public string GetTitleText() { return TitleText != null ? TitleText.Text : ""; }
    public string GetMirrorText() { return MirrorText != null ? MirrorText.Text : ""; }

    public void MutateTitle(string nextTitle) {
        Vm.Title = nextTitle;
    }

    public void MutateSourceElement(string nextText) {
        if (SrcText != null) {
            SrcText.Text = nextText;
        }
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "Phase0Reg/MainWindow.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"Phase0Reg/MainWindow.rs", rs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 640u;
    cfg.viewportHeight = 480u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    const bool loaded = host.CompileAndLoad(input, &err);
    Require(loaded, "Phase0Reg CompileAndLoad failed: " + err);
    if (!loaded || !host.RootVisualNode().IsValid()) {
        return;
    }

    // Settle initial bindings (capture fast-clean snapshot + _pollOnlyEntries)
    host.StepFrame(0.016f, true);
    host.StepFrame(0.016f, true);
    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "Phase0Reg RootScriptObject invalid");

    // Verify Phase 0.1 heap object layout size for BoxedFloat & Thickness
    if (const auto prog = host.Program()) {
        const auto snap = host.Domains().uiHeap().snapshot();
        bool foundBoxedFloat = false;
        bool foundThickness = false;
        for (const auto& obj : snap.objects) {
            const auto* ts = prog->resolveType(obj.typeId);
            if (!ts) { continue; }
            if (ts->name == "BoxedFloat") {
                foundBoxedFloat = true;
                Require(obj.sizeBytes <= 48u,
                    "Expected BoxedFloat <= 48B after Phase 0.1 static exclusion, got " +
                    std::to_string(obj.sizeBytes));
            } else if (ts->name == "Thickness") {
                foundThickness = true;
                Require(obj.sizeBytes <= 96u,
                    "Expected Thickness <= 96B after Phase 0.1 static exclusion, got " +
                    std::to_string(obj.sizeBytes));
            }
        }
        Require(foundBoxedFloat, "Expected live BoxedFloat object in UI heap snapshot");
        Require(foundThickness, "Expected live Thickness object in UI heap snapshot");
    }

    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callInt = [&](std::string_view fn) -> std::int64_t {
        aero::UnifiedValue rv{};
        std::int64_t out = -1;
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            (void)rv.tryGetInt64(out);
        }
        return out;
    };
    auto callVoidStr = [&](std::string_view fn, const std::string& arg) {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(
                    uiObj, fn, {aero::UnifiedValue{aero::StringValue{arg}}}, &rv, aero::DomainKind::UiPresentation),
            "Phase0Reg method failed: " + std::string(fn));
    };

    Require(callStr("GetTitleText") == "InitTitle", "Phase0Reg initial TitleText mismatch");
    Require(callStr("GetMirrorText") == "InitElem", "Phase0Reg initial MirrorText mismatch");

    // 1) Phase 0.3A + 0.3B: idle StepFrame(0.016f, false) skips FlushBindings once pendingScriptSync_ clears
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, "ResetFlushStats", {}, &rv, aero::DomainKind::UiPresentation),
            "Phase0Reg ResetFlushStats failed");
    }
    for (int f = 0; f < 6; ++f) {
        host.StepFrame(0.016f, false);
    }
    const std::int64_t scIdle = callInt("GetShortCircuitCount");
    const std::int64_t evalIdle = callInt("GetEvaluatedCount");
    Require(scIdle == 1 && evalIdle == 0,
        "Expected 1 short-circuited flush and 0 evaluated entries across 6 idle frames, got sc=" +
        std::to_string(scIdle) + " eval=" + std::to_string(evalIdle));

    // 2) Phase 0.3B: forced FlushBindings hits O(1) whole-flush short-circuit with 0 evaluated entries
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, "ResetFlushStats", {}, &rv, aero::DomainKind::UiPresentation),
            "Phase0Reg ResetFlushStats (forced) failed");
    }
    for (int f = 0; f < 5; ++f) {
        host.StepFrame(0.016f, true);
    }
    const std::int64_t scForced = callInt("GetShortCircuitCount");
    const std::int64_t evalForced = callInt("GetEvaluatedCount");
    Require(scForced == 5 && evalForced == 0,
        "Expected 5 short-circuited flushes and 0 evaluated entries on forced frames, got sc=" +
        std::to_string(scForced) + " eval=" + std::to_string(evalForced));

    // 3) Observable VM mutation still propagates immediately
    callVoidStr("MutateTitle", "UpdatedTitle");
    host.StepFrame(0.016f, false);
    Require(callStr("GetTitleText") == "UpdatedTitle", "Phase0Reg VM mutation failed to propagate");

    // 4) ElementName source property mutation still propagates immediately
    callVoidStr("MutateSourceElement", "UpdatedElem");
    host.StepFrame(0.016f, true);
    Require(callStr("GetMirrorText") == "UpdatedElem", "Phase0Reg ElementName mutation failed to propagate");
}

void TestPhase1PushBindingPropIdMathAndPlasma(aero::rs_comp::CompilerSession&) {
    std::cout << "[v3_controls_and_samples_tests] TestPhase1PushBindingPropIdMathAndPlasma...\n";
    const std::string xaml = R"XAML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions"
        x:Class="Phase1Perf.MainWindow"
        Width="640" Height="480" Title="Phase1">
    <StackPanel x:Name="RootPanel">
        <TextBlock x:Name="MathOutText" Text="0" />
        <Border x:Name="PropBorder" Width="100" Height="40" Background="#FF000000" Opacity="1.0" />
        <Border x:Name="PlasmaBorder" Width="120" Height="80">
            <Border.Background>
                <ImageBrush Stretch="Fill">
                    <noesis:Brush.Shader>
                        <noesis:PlasmaBrush ScaleX="48" ScaleY="48" Seed="15" Color="#FF00FF88" Time="0.2" />
                    </noesis:Brush.Shader>
                </ImageBrush>
            </Border.Background>
        </Border>
    </StackPanel>
</Window>
)XAML";

    const std::string rs = R"RS(
module Phase1Perf;
import Aero.UI;

[Observable]
public class Phase1ItemVm {
    public string Label { get; set; } = "Init";
}

[Observable]
public class Phase1MathVm {
    public float Num { get; set; } = 5.0f;
    public float Op { get; set; } = 0.75f;
}

public partial class MainWindow : Window {
    public Phase1ItemVm[] Vms;
    public TextBlock[] VmLabels;
    public TextBlock[] SrcLabels;
    public TextBlock[] MirrorLabels;
    public Phase1MathVm MathVm;

    public MainWindow() {
        InitializeComponent();
        MathVm = new Phase1MathVm();
        DataContext = MathVm;
        Vms = new Phase1ItemVm[50];
        VmLabels = new TextBlock[50];
        for (int i = 0; i < 50; i += 1) {
            var vm = new Phase1ItemVm();
            vm.Label = "VmInit_" + i;
            Vms[i] = vm;
            var tb = new TextBlock();
            VmLabels[i] = tb;
            RootPanel.AddChild(tb);
            BindingEngine.BindPath(tb, "Text", vm, "Label");
        }
        SrcLabels = new TextBlock[20];
        MirrorLabels = new TextBlock[20];
        for (int j = 0; j < 20; j += 1) {
            string sName = "SrcEl_" + j;
            var src = new TextBlock();
            src.Name = sName;
            src.Text = "SrcInit_" + j;
            SrcLabels[j] = src;
            var mir = new TextBlock();
            MirrorLabels[j] = mir;
            RootPanel.AddChild(src);
            RootPanel.AddChild(mir);
            BindingEngine.BindElement(mir, "Text", sName, "Text");
        }
        BindingEngine.BindPathEx(MathOutText, "Text", MathVm, "Num", 0, new MathConverter(), "", "x * 2 + 10", "");
        BindingEngine.BindPath(PropBorder, "Opacity", MathVm, "Op");
    }

    public void ResetFlushStats() { BindingEngine.DebugResetFlushStats(); }
    public void ResetMathStats() { MathConverter.DebugResetCompileCount(); }
    public int GetPushFlushCount() { return BindingEngine.DebugPushFlushCount(); }
    public int GetEvaluatedCount() { return BindingEngine.DebugEvaluatedEntryCount(); }
    public int GetMathCompileCount() { return MathConverter.DebugCompileCount(); }
    public string GetVmText(int idx) { return VmLabels[idx].Text; }
    public string GetMirrorText(int idx) { return MirrorLabels[idx].Text; }
    public string GetMathText() { return MathOutText.Text; }
    public float GetBorderOpacity() { return PropBorder.Opacity; }

    public void MutateOneVm(int idx, string nextVal) {
        Vms[idx].Label = nextVal;
    }

    public void MutateOneSrc(int idx, string nextVal) {
        SrcLabels[idx].Text = nextVal;
    }

    public void SetMathNum(float v) {
        MathVm.Num = v;
    }

    public void SetPlasmaTime(float t) {
        if (PlasmaBorder != null) {
            PlasmaBorder.SetPropertyValue("Background.PlasmaTime", DependencyObject.BoxFloat(t));
        }
    }

    public string GetPlasmaLazyUri() {
        return (PlasmaBorder != null && PlasmaBorder.Background is ImageBrush)
            ? (PlasmaBorder.Background as ImageBrush).ImageSource
            : "";
    }

    public int CheckPropIdAndBoxedColor() {
        int idOp = AeroPropId.Resolve("Opacity");
        int idBg = AeroPropId.Resolve("Background.Color");
        if (idOp <= 0 || idBg <= 0) { return 1; }
        uint col = ParseUtil.PackArgb(255u, 17u, 170u, 68u);
        if (!PropBorder.TryWriteFloatById(idOp, 0.35f)) { return 2; }
        if (!PropBorder.TryWriteColorById(idBg, col)) { return 3; }
        DependencyObject opBox = PropBorder.GetPropertyById(idOp, "Opacity");
        DependencyObject bgBox = PropBorder.GetPropertyById(idBg, "Background.Color");
        if (!(bgBox is BoxedColor)) { return 4; }
        if ((bgBox as BoxedColor).Value != col) { return 5; }
        if (DependencyObject.UnboxFloat(opBox, -1.0f) < 0.34f || DependencyObject.UnboxFloat(opBox, -1.0f) > 0.36f) { return 6; }
        return 0;
    }
}

public static Window CreateMainWindow() {
    return new MainWindow();
}
)RS";

    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.treeShakeStdlib = false;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.xamlPath = "Phase1Perf/MainWindow.xaml";
    input.xamlContent = xaml;
    input.scriptSources.push_back(aero::SourceFile{"Phase1Perf/MainWindow.rs", rs});

    aero::AeroAppHostConfig cfg;
    cfg.viewportWidth = 640u;
    cfg.viewportHeight = 480u;
    cfg.headless = true;
    cfg.preferSoftwareRasterizer = true;

    aero::AeroAppHost host(cfg);
    std::string err;
    const bool loaded = host.CompileAndLoad(input, &err);
    Require(loaded, "Phase1Perf CompileAndLoad failed: " + err);
    if (!loaded || !host.RootVisualNode().IsValid()) {
        return;
    }

    host.StepFrame(0.016f, true);
    host.StepFrame(0.016f, true);
    const aero::ObjectRef uiObj = host.RootScriptObject();
    Require(uiObj.valid(), "Phase1Perf RootScriptObject invalid");

    auto callStr = [&](std::string_view fn) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };
    auto callInt = [&](std::string_view fn) -> std::int64_t {
        aero::UnifiedValue rv{};
        std::int64_t out = -1;
        if (host.InvokeScriptMethod(uiObj, fn, {}, &rv, aero::DomainKind::UiPresentation)) {
            (void)rv.tryGetInt64(out);
        }
        return out;
    };
    auto callIdxStr = [&](std::string_view fn, std::int64_t idx) -> std::string {
        aero::UnifiedValue rv{};
        if (host.InvokeScriptMethod(
                uiObj, fn, {aero::UnifiedValue{idx}}, &rv, aero::DomainKind::UiPresentation)) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&rv)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&rv);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
        }
        return {};
    };

    Require(callIdxStr("GetVmText", 17) == "VmInit_17", "Phase1Perf initial VmText mismatch");
    Require(callIdxStr("GetMirrorText", 9) == "SrcInit_9", "Phase1Perf initial MirrorText mismatch");
    Require(callStr("GetMathText") == "20", "Phase1Perf initial MathConverter text mismatch, got " + callStr("GetMathText"));

    // 1) Tier 2 VM Push Fast Path (mutate 1 out of 50 VM bindings + 20 ElementName bindings)
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, "ResetFlushStats", {}, &rv, aero::DomainKind::UiPresentation),
            "ResetFlushStats failed");
        Require(host.InvokeScriptMethod(
                    uiObj, "MutateOneVm",
                    {aero::UnifiedValue{std::int64_t{17}}, aero::UnifiedValue{aero::StringValue{std::string{"Pushed17"}}}},
                    &rv, aero::DomainKind::UiPresentation),
            "MutateOneVm failed");
    }
    host.StepFrame(0.016f, false);
    Require(callIdxStr("GetVmText", 17) == "Pushed17", "Phase1Perf pushed VM text mismatch");
    const std::int64_t pushVmCount = callInt("GetPushFlushCount");
    const std::int64_t evalVmDelta = callInt("GetEvaluatedCount");
    Require(pushVmCount >= 1 && evalVmDelta <= 8,
        "Expected VM Tier 2 Push Fast Path (push>=1, eval<=8), got push=" +
        std::to_string(pushVmCount) + " eval=" + std::to_string(evalVmDelta));

    // 2) Tier 2 UI ElementName Push Fast Path (mutate 1 out of 20 ElementName sources)
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, "ResetFlushStats", {}, &rv, aero::DomainKind::UiPresentation),
            "ResetFlushStats (UI) failed");
        Require(host.InvokeScriptMethod(
                    uiObj, "MutateOneSrc",
                    {aero::UnifiedValue{std::int64_t{9}}, aero::UnifiedValue{aero::StringValue{std::string{"ElemPush9"}}}},
                    &rv, aero::DomainKind::UiPresentation),
            "MutateOneSrc failed");
    }
    host.StepFrame(0.016f, true);
    Require(callIdxStr("GetMirrorText", 9) == "ElemPush9", "Phase1Perf pushed ElementName text mismatch");
    const std::int64_t pushUiCount = callInt("GetPushFlushCount");
    const std::int64_t evalUiDelta = callInt("GetEvaluatedCount");
    Require(pushUiCount >= 1 && evalUiDelta <= 8,
        "Expected UI Tier 2 Push Fast Path (push>=1, eval<=8), got push=" +
        std::to_string(pushUiCount) + " eval=" + std::to_string(evalUiDelta));

    // 3) MathConverter compiled RPN reuse (compile at most once across repeated value mutations)
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(uiObj, "ResetMathStats", {}, &rv, aero::DomainKind::UiPresentation),
            "ResetMathStats failed");
    }
    const float testInputs[] = {15.0f, 25.0f, -3.0f};
    const char* expectedMath[] = {"40", "60", "4"};
    for (int k = 0; k < 3; ++k) {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(
                    uiObj, "SetMathNum", {aero::UnifiedValue{aero::FloatValue{testInputs[k]}}},
                    &rv, aero::DomainKind::UiPresentation),
            "SetMathNum failed");
        host.StepFrame(0.016f, false);
        Require(callStr("GetMathText") == expectedMath[k],
            "Phase1Perf MathConverter output mismatch: expected " + std::string(expectedMath[k]) +
            ", got " + callStr("GetMathText"));
    }
    Require(callInt("GetMathCompileCount") == 0,
        "Expected 0 additional MathExprParser compiles via RPN cache, got " +
        std::to_string(callInt("GetMathCompileCount")));

    // 4) AeroPropId + BoxedColor parity
    Require(callInt("CheckPropIdAndBoxedColor") == 0,
        "Phase1Perf CheckPropIdAndBoxedColor failed with code " + std::to_string(callInt("CheckPropIdAndBoxedColor")));

    // 5) PlasmaBrush numeric channel animation + lazy ImageSource
    std::vector<std::uint8_t> plasmaFrame0;
    Require(host.RenderHeadlessRgba(640u, 480u, plasmaFrame0), "Phase1Perf plasma frame0 render failed");
    {
        aero::UnifiedValue rv{};
        Require(host.InvokeScriptMethod(
                    uiObj, "SetPlasmaTime", {aero::UnifiedValue{aero::FloatValue{1.4f}}}, &rv, aero::DomainKind::UiPresentation),
            "SetPlasmaTime failed");
    }
    host.StepFrame(0.016f, false);
    std::vector<std::uint8_t> plasmaFrame1;
    Require(host.RenderHeadlessRgba(640u, 480u, plasmaFrame1), "Phase1Perf plasma frame1 render failed");
    Require(CountChangedPixels(plasmaFrame0, plasmaFrame1) > 40u,
        "Phase1Perf numeric PlasmaBrush animation did not change rendered pixels");
    const std::string lazyUri = callStr("GetPlasmaLazyUri");
    Require(lazyUri.rfind("shader://plasma?", 0) == 0,
        "Phase1Perf lazy PlasmaBrush ImageSource mismatch: " + lazyUri);
}

} // namespace

int main() {
    aero::rs_comp::CompilerSession sharedSession;
    TestSample01CodeFirstCounter(sharedSession);
    TestSample02CompiledXamlMvvm(sharedSession);
    TestSample03ControlGallery(sharedSession);
    TestSample04VectorAndCanvas(sharedSession);
    TestSample05DualDomainSimHud(sharedSession);
    TestRegressionsFixed(sharedSession);
    TestGalleryEngineRuntimeFixes(sharedSession);
    TestPhase1OptimizationRegressions(sharedSession);
    TestCollapsedSubtreeCatchupAndSelfDataContext(sharedSession);
    TestPhase0MemoryAndBindingShortCircuit(sharedSession);
    TestPhase1PushBindingPropIdMathAndPlasma(sharedSession);

    if (gFailureCount > 0) {
        std::cerr << gFailureCount << " v3_controls_and_samples_tests assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All 5 V3 samples & control tests passed!\n";
    return 0;
}
