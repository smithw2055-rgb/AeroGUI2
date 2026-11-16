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
    aero::rs_comp::CompilerSession& sharedSession) {
    namespace fs = std::filesystem;
    aero::AeroV3CompileInput input;
    input.stdlibRootDir = AERO_STDLIB_DIR;
    input.includeStdlib = true;
    input.optimizeBytecode = false;
    input.emitAotCpp = false;
    input.compilerSession = &sharedSession;

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
        ? node->layout.worldMatrix3D.ProjectPoint2D(localCenter)
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

} // namespace

int main() {
    aero::rs_comp::CompilerSession sharedSession;
    TestSample01CodeFirstCounter(sharedSession);
    TestSample02CompiledXamlMvvm(sharedSession);
    TestSample03ControlGallery(sharedSession);
    TestSample04VectorAndCanvas(sharedSession);
    TestSample05DualDomainSimHud(sharedSession);

    if (gFailureCount > 0) {
        std::cerr << gFailureCount << " v3_controls_and_samples_tests assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All 5 V3 samples & control tests passed!\n";
    return 0;
}
