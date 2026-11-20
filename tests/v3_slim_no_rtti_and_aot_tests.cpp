#if defined(_CPPRTTI)
#error "v3_slim_no_rtti_and_aot_tests MUST be compiled with /GR- (RTTI disabled)"
#endif

#include "aero/core/RsAliases.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualHostBindings.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualRenderAdapter.hpp"
#include "aero/visual/VisualTree.hpp"

#include <realscript/aot_cpp/AotRuntime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

int gFailureCount = 0;

void Require(bool cond, std::string_view msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailureCount;
    }
}

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& p, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::size_t CountDistinctColors(const std::vector<std::uint8_t>& rgba) {
    std::unordered_set<std::uint32_t> colors;
    const std::size_t pixelCount = rgba.size() / 4u;
    const std::size_t step = std::max<std::size_t>(1u, pixelCount / 10000u);
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
    std::size_t diff = 0u;
    for (std::size_t i = 0u; i + 3u < a.size(); i += 4u) {
        const int delta =
            std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) +
            std::abs(static_cast<int>(a[i + 1u]) - static_cast<int>(b[i + 1u])) +
            std::abs(static_cast<int>(a[i + 2u]) - static_cast<int>(b[i + 2u]));
        if (delta > 6) {
            ++diff;
        }
    }
    return diff;
}

[[nodiscard]] int RunCommandDirect(const std::string& cmdLine) {
#if defined(_WIN32)
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmdLine;
    if (!CreateProcessA(
            nullptr,
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
#else
    return std::system(cmdLine.c_str());
#endif
}

void TestCliCompileO2AotAndMsvcNoRttiCompile() {
    std::cout << "[Test] CLI --O2 --aot-cpp emission & MSVC /std:c++17 /GR- compile check...\n";
    namespace fs = std::filesystem;
    const fs::path workDir = fs::path(AERO_TEST_TEMP_DIR) / "aot_slim_probe";
    const fs::path outDir = workDir / "out";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(outDir, ec);

    const fs::path xamlPath = workDir / "MainWindow.xaml";
    const fs::path rsPath = workDir / "MainWindow.rs";

    const char* kProbeXaml = R"XML(
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Samples.AotProbe.MainWindow"
        Title="Slim AOT Probe"
        Width="480"
        Height="320"
        Background="#FF0F172A">
  <Border Padding="18" Background="#FF1E293B" CornerRadius="10">
    <StackPanel Orientation="Vertical" Spacing="10">
      <TextBlock x:Name="BannerText" Text="AeroGUI Slim C++17 AOT" FontSize="18" Foreground="#FFF8FAFC"/>
      <Border Height="60" CornerRadius="8">
        <Border.Background>
          <LinearGradientBrush StartPoint="0,0" EndPoint="1,0">
            <GradientStop Offset="0.0" Color="#FF2563EB"/>
            <GradientStop Offset="1.0" Color="#FF10B981"/>
          </LinearGradientBrush>
        </Border.Background>
      </Border>
      <Button x:Name="ActionBtn" Content="Run AOT" Width="130" Height="36" Click="OnActionClick"/>
    </StackPanel>
  </Border>
</Window>
)XML";

    const char* kProbeRs = R"RS(
module Samples.AotProbe;

import Aero.UI;

public partial class MainWindow : Window {
    public int ClickCount { get; private set; }

    public MainWindow() {
        InitializeComponent();
    }

    public void OnActionClick(object sender, RoutedEventArgs e) {
        ClickCount = ClickCount + 1;
        if (BannerText != null) {
            BannerText.Text = "Clicks: " + ClickCount;
        }
    }
}
)RS";

    Require(WriteTextFile(xamlPath, kProbeXaml), "Failed to write MainWindow.xaml probe");
    Require(WriteTextFile(rsPath, kProbeRs), "Failed to write MainWindow.rs probe");

    const fs::path aotStem = outDir / "AotProbeUi";
    std::ostringstream cmd;
    cmd << "\"" << AERO_COMPILER_EXE << "\""
        << " --stdlib \"" << AERO_STDLIB_DIR << "\""
        << " --O2"
        << " --xaml \"" << xamlPath.string() << "\""
        << " --rs \"" << rsPath.string() << "\""
        << " --aot-cpp \"" << aotStem.string() << "\""
        << " --out-dir \"" << outDir.string() << "\"";

    const auto compT0 = std::chrono::steady_clock::now();
    const int compRc = RunCommandDirect(cmd.str());
    const auto compMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - compT0).count();
    Require(compRc == 0, "AERO_COMPILER_EXE --O2 --aot-cpp failed with exit code " + std::to_string(compRc));

    const fs::path aotHeader = outDir / "AotProbeUi.h";
    const fs::path aotSource = outDir / "AotProbeUi.cpp";
    Require(fs::exists(aotHeader) && fs::file_size(aotHeader) > 0u, "Emitted AOT header missing or empty");
    Require(fs::exists(aotSource) && fs::file_size(aotSource) > 0u, "Emitted AOT source missing or empty");
    std::cout << "  [AOT] aero-compiler --O2 --aot-cpp elapsed=" << compMs << " ms, cpp="
              << (fs::exists(aotSource) ? fs::file_size(aotSource) : 0u) << " bytes\n";

    const std::string headerText = ReadTextFile(aotHeader);
    const std::string sourceText = ReadTextFile(aotSource);
    Require(headerText.find("ProgramDescriptor") != std::string::npos,
        "AOT header does not declare rs_aot::ProgramDescriptor");
    Require(sourceText.find("InitializeComponent") != std::string::npos,
        "AOT source does not include compiled InitializeComponent");
    Require(sourceText.find("rs_rt::Interpreter") == std::string::npos,
        "Emitted C++17 AOT source must NOT reference bytecode Interpreter");

#if defined(_WIN32)
    // Verify emitted C++17 AOT source compiles cleanly under MSVC /std:c++17 /GR- (zero RTTI)
    const fs::path rsIncludeDir("C:/Projects/RealScript/include");
    if (fs::exists(rsIncludeDir / "realscript" / "aot_cpp" / "RuntimeAbi.h")) {
        const fs::path objOut = outDir / "AotProbeUi.obj";
        const fs::path vcvarsCandidates[] = {
            fs::path("C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat"),
            fs::path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"),
            fs::path("C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvars64.bat"),
            fs::path("C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvars64.bat"),
            fs::path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"),
        };
        fs::path vcvarsPath;
        for (const auto& cand : vcvarsCandidates) {
            if (fs::exists(cand)) {
                vcvarsPath = cand;
                break;
            }
        }
        const bool hasClInEnv = (RunCommandDirect("cmd.exe /c where cl >nul 2>nul") == 0);
        if (hasClInEnv || !vcvarsPath.empty()) {
            const fs::path compileBat = workDir / "compile_aot.bat";
            std::ostringstream bat;
            bat << "@echo off\r\n";
            if (!hasClInEnv) {
                bat << "call \"" << vcvarsPath.string() << "\" >nul\r\n";
            }
            bat << "cl /nologo /bigobj /c /std:c++17 /GR- /EHsc"
                << " /I\"" << rsIncludeDir.string() << "\""
                << " /I\"" << outDir.string() << "\""
                << " /Fo\"" << objOut.string() << "\""
                << " \"" << aotSource.string() << "\"\r\n";
            Require(WriteTextFile(compileBat, bat.str()), "Failed to write compile_aot.bat");
            const auto clT0 = std::chrono::steady_clock::now();
            const int clRc = RunCommandDirect("cmd.exe /c \"\"" + compileBat.string() + "\"\"");
            const auto clMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - clT0).count();
            std::cout << "  [AOT] cl.exe /std:c++17 /GR- elapsed=" << clMs << " ms, obj="
                      << (fs::exists(objOut) ? fs::file_size(objOut) : 0u) << " bytes\n";
            Require(clRc == 0 && fs::exists(objOut) && fs::file_size(objOut) > 0u,
                "Emitted C++17 AOT source failed MSVC /std:c++17 /GR- compilation");
        }
    }
#endif
}

// ============================================================================
// Live C++17 AOT Execution Harness (ZERO Interpreter, ZERO RTTI)
// ============================================================================

struct LiveAotAppState {
    aero::rs_rt::BindingRegistry* bindings = nullptr;
    aero::VisualWorld* world = nullptr;
    aero::rs_rt::NativeHandle rootHandle{};
    aero::rs_rt::NativeHandle cardHandle{};
    aero::rs_rt::NativeHandle buttonHandle{};
    aero::rs_rt::NativeHandle labelHandle{};
    aero::rs_rt::NativeHandle canvasHandle{};
    int clickCount = 0;
    int canvasDrawCount = 0;
};

LiveAotAppState* gAotApp = nullptr;

[[nodiscard]] bool InvokeHostNative(
    const char* name,
    std::initializer_list<aero::rs_rt::Value> args,
    aero::rs_rt::Value* outVal = nullptr) {
    if (!gAotApp || !gAotApp->bindings) {
        return false;
    }
    std::vector<aero::rs_rt::Value> vec(args);
    aero::rs_bc::FunctionReference fnRef{};
    fnRef.name = std::string("Aero.Native::") + name;
    aero::rs_rt::RuntimeError err{};
    const auto res = gAotApp->bindings->invoke(
        fnRef, aero::rs_rt::ValueSpan(vec), err);
    if (!res.has_value()) {
        return false;
    }
    if (outVal) {
        *outVal = *res;
    }
    return true;
}

bool AotEntryBuildUi(
    aero::rs_aot::ExecutionContext& ctx,
    const aero::rs_rt::Value*,
    std::size_t,
    aero::rs_rt::Value& outResult) {
    if (!gAotApp) {
        return false;
    }

    auto createNode = [](std::int64_t kindInt, aero::rs_rt::NativeHandle& outNh) -> bool {
        aero::rs_rt::Value res{};
        if (!InvokeHostNative("Visual_Create", {aero::rs_rt::Value{kindInt}}, &res)) {
            return false;
        }
        return res.tryGetNativeHandle(outNh) && outNh.valid();
    };

    if (!createNode(0, gAotApp->rootHandle) ||
        !createNode(0, gAotApp->cardHandle) ||
        !createNode(0, gAotApp->buttonHandle) ||
        !createNode(1, gAotApp->labelHandle) ||
        !createNode(4, gAotApp->canvasHandle)) {
        return false;
    }

    auto f = [](float v) { return aero::rs_rt::Value{aero::rs_rt::FloatValue{v}}; };
    auto i = [](std::int64_t v) { return aero::rs_rt::Value{v}; };
    auto u = [](std::uint32_t v) { return aero::rs_rt::Value{aero::rs_rt::UIntValue{v}}; };
    auto s = [&](std::string_view sv) -> aero::rs_rt::Value {
        const auto interned = ctx.heap()->internString(sv);
        return interned ? aero::rs_rt::Value{*interned} : aero::rs_rt::Value{aero::rs_rt::StringValue{std::string(sv)}};
    };

    // Root vertical StackPanel (480x320)
    (void)InvokeHostNative("Visual_SetSize", {aero::rs_rt::Value{gAotApp->rootHandle}, f(480.0f), f(320.0f)});
    (void)InvokeHostNative("Visual_SetLayoutMode", {aero::rs_rt::Value{gAotApp->rootHandle}, i(1), f(14.0f), f(0.0f), i(1), i(0)});
    (void)InvokeHostNative("Visual_SetPadding", {aero::rs_rt::Value{gAotApp->rootHandle}, f(24.0f), f(24.0f), f(24.0f), f(24.0f)});
    (void)InvokeHostNative("Visual_SetBoxStyle", {
        aero::rs_rt::Value{gAotApp->rootHandle}, u(0xFF1A100Bu), u(0xFF334155u),
        f(0.0f), f(0.0f), f(0.0f), f(0.0f), f(0.0f), f(0.0f), f(0.0f), f(0.0f)});

    // Card header (blue gradient + border)
    (void)InvokeHostNative("Visual_SetSize", {aero::rs_rt::Value{gAotApp->cardHandle}, f(420.0f), f(84.0f)});
    (void)InvokeHostNative("Visual_SetBoxStyle", {
        aero::rs_rt::Value{gAotApp->cardHandle}, u(0xFF3B291Eu), u(0xFF60A5FAu),
        f(2.0f), f(2.0f), f(2.0f), f(2.0f), f(10.0f), f(10.0f), f(10.0f), f(10.0f)});

    // Interactive Button
    (void)InvokeHostNative("Visual_SetSize", {aero::rs_rt::Value{gAotApp->buttonHandle}, f(180.0f), f(48.0f)});
    (void)InvokeHostNative("Visual_SetPadding", {aero::rs_rt::Value{gAotApp->buttonHandle}, f(12.0f), f(10.0f), f(12.0f), f(10.0f)});
    (void)InvokeHostNative("Visual_SetBoxStyle", {
        aero::rs_rt::Value{gAotApp->buttonHandle}, u(0xFFEB6325u), u(0xFFFCA5A5u),
        f(1.5f), f(1.5f), f(1.5f), f(1.5f), f(8.0f), f(8.0f), f(8.0f), f(8.0f)});
    (void)InvokeHostNative("Visual_SetInteractiveFlags", {
        aero::rs_rt::Value{gAotApp->buttonHandle}, aero::rs_rt::Value{true}, aero::rs_rt::Value{true}, aero::rs_rt::Value{true}});
    const std::uint32_t standardInputMask =
        aero::VisualCallback_PointerMove |
        aero::VisualCallback_PointerDown |
        aero::VisualCallback_PointerUp |
        aero::VisualCallback_PointerEnter |
        aero::VisualCallback_PointerLeave |
        aero::VisualCallback_MouseWheel |
        aero::VisualCallback_KeyDown |
        aero::VisualCallback_KeyUp |
        aero::VisualCallback_TextInput |
        aero::VisualCallback_FocusChanged;
    (void)InvokeHostNative("Visual_BindOwner", {
        aero::rs_rt::Value{gAotApp->buttonHandle},
        aero::rs_rt::Value{aero::rs_rt::NullObject{}},
        i(static_cast<std::int64_t>(standardInputMask))});

    // Label inside button (pass pointer events straight to parent button)
    (void)InvokeHostNative("Visual_SetSize", {aero::rs_rt::Value{gAotApp->labelHandle}, f(150.0f), f(24.0f)});
    (void)InvokeHostNative("Visual_SetInteractiveFlags", {
        aero::rs_rt::Value{gAotApp->labelHandle}, aero::rs_rt::Value{true}, aero::rs_rt::Value{false}, aero::rs_rt::Value{false}});
    (void)InvokeHostNative("Visual_SetText", {
        aero::rs_rt::Value{gAotApp->labelHandle}, s("AOT Click Me"), s("Segoe UI"), f(15.0f), u(0xFFFFFFFFu), i(0), i(1), i(0)});

    // Retained CanvasDraw element
    (void)InvokeHostNative("Visual_SetSize", {aero::rs_rt::Value{gAotApp->canvasHandle}, f(420.0f), f(96.0f)});
    (void)InvokeHostNative("Visual_BindOwner", {
        aero::rs_rt::Value{gAotApp->canvasHandle},
        aero::rs_rt::Value{aero::rs_rt::NullObject{}},
        i(static_cast<std::int64_t>(aero::VisualCallback_Draw))});

    (void)InvokeHostNative("Visual_AddChild", {aero::rs_rt::Value{gAotApp->buttonHandle}, aero::rs_rt::Value{gAotApp->labelHandle}});
    (void)InvokeHostNative("Visual_AddChild", {aero::rs_rt::Value{gAotApp->rootHandle}, aero::rs_rt::Value{gAotApp->cardHandle}});
    (void)InvokeHostNative("Visual_AddChild", {aero::rs_rt::Value{gAotApp->rootHandle}, aero::rs_rt::Value{gAotApp->buttonHandle}});
    (void)InvokeHostNative("Visual_AddChild", {aero::rs_rt::Value{gAotApp->rootHandle}, aero::rs_rt::Value{gAotApp->canvasHandle}});

    // Initial retained CanvasDraw pass via AOT
    (void)InvokeHostNative("Draw_Begin", {aero::rs_rt::Value{gAotApp->canvasHandle}});
    (void)InvokeHostNative("Draw_FillRoundedRect", {
        aero::rs_rt::Value{gAotApp->canvasHandle}, f(8.0f), f(8.0f), f(404.0f), f(80.0f), f(8.0f), f(8.0f), u(0xFF4F46E5u), i(-1)});
    (void)InvokeHostNative("Draw_End", {aero::rs_rt::Value{gAotApp->canvasHandle}});
    ++gAotApp->canvasDrawCount;

    outResult = aero::rs_rt::Value{gAotApp->rootHandle};
    return true;
}

bool AotHandlePointer(
    aero::rs_aot::ExecutionContext&,
    const aero::rs_rt::Value* args,
    std::size_t argCount,
    aero::rs_rt::Value& outResult) {
    if (!gAotApp || argCount < 2u) {
        outResult = aero::rs_rt::Value{false};
        return true;
    }
    aero::rs_rt::NativeHandle targetNh{};
    std::int64_t flag = 0;
    if (!args[0].tryGetNativeHandle(targetNh) || !args[1].tryGetInt64(flag)) {
        outResult = aero::rs_rt::Value{false};
        return true;
    }
    if (flag == static_cast<std::int64_t>(aero::VisualCallback_PointerDown) &&
        targetNh.slot == gAotApp->buttonHandle.slot) {
        ++gAotApp->clickCount;
        // Mutate card color + launch short float opacity animation via AOT binding
        (void)InvokeHostNative("Visual_SetBoxStyle", {
            aero::rs_rt::Value{gAotApp->cardHandle},
            aero::rs_rt::Value{aero::rs_rt::UIntValue{0xFF81B910u}},
            aero::rs_rt::Value{aero::rs_rt::UIntValue{0xFFFDE047u}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{3.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{3.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{3.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{3.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{12.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{12.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{12.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{12.0f}}});
        (void)InvokeHostNative("Anim_StartFloat", {
            aero::rs_rt::Value{gAotApp->cardHandle},
            aero::rs_rt::Value{static_cast<std::int64_t>(aero::VisualAnimProp::Opacity)},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.35f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{1.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.10f}},
            aero::rs_rt::Value{0LL},
            aero::rs_rt::Value{0LL}});
        outResult = aero::rs_rt::Value{true};
        return true;
    }
    outResult = aero::rs_rt::Value{false};
    return true;
}

void TestLiveAotExecutionAndHeadlessRenderWithoutInterpreter() {
    std::cout << "[Test] Live rs_aot::ExecutionContext + VisualWorld + NativeWindowHost (zero Interpreter, zero RTTI)...\n";
    using namespace aero;

    constexpr SymbolId kEntryFnId = 91001u;
    constexpr SymbolId kPointerFnId = 91002u;

    static const rs_sem::PrimitiveType kPointerParams[] = {
        rs_sem::PrimitiveType::Handle,
        rs_sem::PrimitiveType::Int,
    };
    static const rs_aot::FunctionDescriptor kFunctions[] = {
        {kEntryFnId, "SlimAot::BuildMainView", rs_sem::PrimitiveType::Handle, 0u, nullptr, nullptr, 0u, &AotEntryBuildUi, nullptr, nullptr},
        {kPointerFnId, "SlimAot::OnPointer", rs_sem::PrimitiveType::Bool, 0u, kPointerParams, nullptr, 2u, &AotHandlePointer, nullptr, nullptr},
    };
    rs_aot::ProgramDescriptor progDesc{};
    progDesc.name = "SlimAotProgram";
    progDesc.functions = kFunctions;
    progDesc.functionCount = 2u;

    auto resources = std::make_shared<UIResourceStore>();

    UIHostPlatformHooks hooks{};
    ExecutionDomainContext domains{};
    domains.registerAotProgramAttributes(progDesc);

    VisualWorld world(&domains.nativeHandles(), resources.get(), &domains.uiHeap());
    DrawingCommandBuffer drawBuffer;
    VisualAnimationEngine animEngine;
    VisualInputRouter inputRouter(&world, nullptr, &hooks);
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream;
    DesktopEventHost desktopHost(&world, &drawBuffer, &animEngine, &inputRouter, &renderTree, resources);
    NativeWindowHost windowHost;

    auto bindings = std::make_shared<BindingRegistry>();
    rs_aot::ExecutionContext aotCtx(progDesc, domains.uiHeapPtr(), bindings);
    domains.attachAotContext(aotCtx, DomainKind::UiPresentation);

    windowHost.InstallPlatformHooks(hooks);
    Require(RegisterVisualHostBindings(
        *bindings, world, drawBuffer, animEngine, inputRouter, domains, nullptr, nullptr, &aotCtx, &hooks),
        "RegisterVisualHostBindings failed in slim AOT test");

    LiveAotAppState appState{};
    appState.bindings = bindings.get();
    appState.world = &world;
    gAotApp = &appState;

    inputRouter.SetScriptEventDispatcher(
        [&](VisualNodeId nodeId, VisualCallbackFlags flag, const NativeInputEvent&) -> bool {
            if (flag == VisualCallback_PointerDown) {
                const VisualNode* n = world.GetNode(nodeId);
                if (!n || !n->nativeHandle.valid()) {
                    return false;
                }
                const UnifiedValue args[2] = {
                    UnifiedValue{n->nativeHandle},
                    UnifiedValue{static_cast<std::int64_t>(flag)},
                };
                UnifiedValue outVal{};
                if (aotCtx.invoke(kPointerFnId, args, 2u, outVal)) {
                    bool handled = false;
                    return outVal.tryGetBool(handled) && handled;
                }
            }
            return false;
        });

    windowHost.BindDesktopHost(&desktopHost, &renderTree, &gpuStream, &inputRouter);
    NativeWindowConfig winCfg{};
    winCfg.width = 480u;
    winCfg.height = 320u;
    winCfg.preferHardwareGpu = false;
    winCfg.allowHeadlessFallback = true;
    winCfg.forceHeadless = true;
    Require(windowHost.Initialize(winCfg), "NativeWindowHost headless init failed");

    // 1. Invoke AOT entry builder directly with ZERO Interpreter
    UnifiedValue rootVal{};
    Require(aotCtx.invoke(kEntryFnId, nullptr, 0u, rootVal),
        "AOT BuildMainView failed: " + aotCtx.error().message);
    const VisualNodeId rootId = world.ResolveHandle(appState.rootHandle);
    Require(rootId.IsValid(), "AOT root VisualNodeId invalid");
    world.SetRoot(rootId);
    desktopHost.SetViewportSize(Size{480.0f, 320.0f});

    // Sync initial UI + render frame cleanly
    VisualRenderAdapter renderAdapter;
    world.UpdateLayout(480.0f, 320.0f);
    (void)renderAdapter.SyncPagedRenderTree(world, &drawBuffer, renderTree, resources.get());
    desktopHost.SetStateFlags(false, false, false, false);
    Require(desktopHost.CanSleepIdle(), "DesktopEventHost should allow 0% CPU idle sleep when clean & not animating");

    (void)windowHost.TickAndPresentFrame(0.016f, true);
    std::vector<std::uint8_t> frame0;
    std::uint32_t capW = 0u, capH = 0u;
    Require(windowHost.CaptureFramebufferRgba8(frame0, capW, capH) && capW == 480u && capH == 320u,
        "AOT initial CaptureFramebufferRgba8 failed");
    Require(CountDistinctColors(frame0) >= 5u, "AOT initial render appears blank/uniform");

    // 2. Inject pointer click onto button -> invokes AOT click handler + launches Anim_StartFloat
    const VisualNodeId btnId = world.ResolveHandle(appState.buttonHandle);
    const VisualNode* btnNode = world.GetNode(btnId);
    Require(btnNode != nullptr && btnNode->layout.layoutRect.width > 10.0f,
        "AOT button layoutRect invalid");
    const Point btnLocalCenter{
        btnNode->layout.layoutRect.x + btnNode->layout.layoutRect.width * 0.5f,
        btnNode->layout.layoutRect.y + btnNode->layout.layoutRect.height * 0.5f};
    const Point btnViewport = btnNode->layout.hasPerspective3D
        ? btnNode->WorldMatrix3D().ProjectPoint2D(btnLocalCenter)
        : btnNode->layout.worldTransform.TransformPoint(btnLocalCenter);
    const float btnCx = btnViewport.x;
    const float btnCy = btnViewport.y;

    windowHost.OnPlatformMouseMove(btnCx, btnCy, KeyMod_None);
    windowHost.OnPlatformMouseButton(btnCx, btnCy, PointerButton::Left, true, KeyMod_None);
    windowHost.OnPlatformMouseButton(btnCx, btnCy, PointerButton::Left, false, KeyMod_None);
    Require(inputRouter.FlushQueuedEvents() > 0u, "AOT pointer click should dispatch queued events");
    Require(appState.clickCount == 1, "AOT pointer handler was not invoked");
    Require(animEngine.HasActiveAnimations(), "AOT Anim_StartFloat should have active animation");

    desktopHost.SetActiveAnimations(animEngine.HasActiveAnimations());
    Require(!desktopHost.CanSleepIdle(), "DesktopEventHost must stay awake while AOT animation is active");

    // Step animation to completion and verify CanSleepIdle returns true + pixels updated
    for (int f = 0; f < 10; ++f) {
        (void)animEngine.Tick(0.025f, world);
    }

    world.UpdateLayout(480.0f, 320.0f);
    (void)renderAdapter.SyncPagedRenderTree(world, &drawBuffer, renderTree, resources.get());
    desktopHost.SetActiveAnimations(animEngine.HasActiveAnimations());
    desktopHost.SetStateFlags(animEngine.HasActiveAnimations(), false, false, false);
    Require(!animEngine.HasActiveAnimations() && desktopHost.CanSleepIdle(),
        "DesktopEventHost must return to CanSleepIdle()==true once AOT animation finishes");

    (void)windowHost.TickAndPresentFrame(0.016f, true);

    std::vector<std::uint8_t> frame1;
    Require(windowHost.CaptureFramebufferRgba8(frame1, capW, capH),
        "AOT post-click CaptureFramebufferRgba8 failed");
    Require(CountChangedPixels(frame0, frame1) > 100u,
        "AOT pointer click + color mutation did not update rendered pixels");

    windowHost.Shutdown();
    domains.detachAotContext(aotCtx);
    gAotApp = nullptr;
}

} // namespace

int main() {
    const char* runOpt = std::getenv("AERO_TEST_ENABLE_AOT_OPT");
    if (runOpt != nullptr && runOpt[0] != '\0' && std::string_view(runOpt) != "0") {
        TestCliCompileO2AotAndMsvcNoRttiCompile();
    } else {
        std::cout << "[Skip] CLI --O2 --aot-cpp + cl.exe check skipped in pure interpreter mode (set AERO_TEST_ENABLE_AOT_OPT=1 to enable)\n";
    }
    TestLiveAotExecutionAndHeadlessRenderWithoutInterpreter();

    if (gFailureCount > 0) {
        std::cerr << gFailureCount << " v3_slim_no_rtti_and_aot_tests assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All slim zero-RTTI & AOT C++17 tests passed!\n";
    return 0;
}
