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
    const std::string data0 = pNode0 ? pNode0->Vector().pathOrParams : std::string{};
    const std::size_t activeBefore = host.AnimEngine().ActiveTrackCount();
    Require(pNode0 && pNode0->Image().tileWrap, "AOT Path ImageBrush TileMode=Tile should set tileWrap");

    for (int f = 0; f < 10; ++f) {
        host.StepFrame(0.020f, true);
    }
    const aero::VisualNode* pNodeMid = host.World().GetNode(plasmaPathId);
    const aero::VisualNode* bNodeMid = host.World().GetNode(innerBorderId);
    const std::string uriMid = pNodeMid ? pNodeMid->Image().uri : std::string{};
    const std::string dataMid = pNodeMid ? pNodeMid->Vector().pathOrParams : std::string{};
    Require(uriMid != uri0,
        "Infinite Storyboard Fill.PlasmaTime did not tick: tracksBefore=" + std::to_string(activeBefore) +
        " after=" + std::to_string(host.AnimEngine().ActiveTrackCount()) + " uri0=" + uri0 + " uriMid=" + uriMid);
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
  <StackPanel x:Name="RuntimeHost" Orientation="Vertical" Spacing="8" Margin="16"/>
</Window>)XML";

    const std::string aotRs = R"RS(
module GalleryRuntimeFixDemo;
import Aero.UI;

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

    public MainWindow() {
        InitializeComponent();
        Vm = new GalleryFixVm();
        DataContext = Vm;

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
            "</StackPanel.Resources>" +
            "<TextBlock x:Name='EntTb'>&#38;lt;ok&#38;gt;</TextBlock>" +
            "<TextBlock x:Name='InlineTb'><Bold Foreground='{Binding AccentBrush}'>foo </Bold>bar</TextBlock>" +
            "<ToggleButton x:Name='TriTb' IsThreeState='True'/>" +
            "<ToolBar x:Name='HostTb'><Button x:Name='TbChildBtn' Content='Act' Style='{DynamicResource {x:Static ToolBar.ButtonStyleKey}}'/></ToolBar>" +
            "<TreeView x:Name='TvCheck'><TransformGroup Selector.IsSelected='True'/></TreeView>" +
            "<ComboBox x:Name='CbComposite' SelectedIndex='0'><ComboBoxItem><StackPanel Orientation='Horizontal'><TextBlock Text='Alpha'/><TextBlock Text='Beta'/></StackPanel></ComboBoxItem></ComboBox>" +
            "</StackPanel>";
        UIElement parsed = XamlReader.Parse(rtXml);
        if (parsed != null) {
            RuntimeHost.AddChild(parsed);
            EntTb = parsed.FindName("EntTb") as TextBlock;
            InlineTb = parsed.FindName("InlineTb") as TextBlock;
            TriTb = parsed.FindName("TriTb") as ToggleButton;
            HostTb = parsed.FindName("HostTb") as ToolBar;
            TbChildBtn = parsed.FindName("TbChildBtn") as Button;
            TvCheck = parsed.FindName("TvCheck") as TreeView;
            CbComposite = parsed.FindName("CbComposite") as ComboBox;
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

    if (gFailureCount > 0) {
        std::cerr << gFailureCount << " v3_controls_and_samples_tests assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All 5 V3 samples & control tests passed!\n";
    return 0;
}
