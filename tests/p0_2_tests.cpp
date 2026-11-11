#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#define AERO_CHECK(cond, msg)                                                      \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::p0_2_tests {

namespace {

// ============================================================================
// Mock OpenGL 3.3 / WebGL2 Spy Driver for Deterministic GPU Pipeline Verification
// ============================================================================
struct MockGlSpyState {
    bool forceVertexCompileFail = false;
    bool forceProgramLinkFail = false;
    AeroGLuint nextId = 1u;

    std::size_t compileCount = 0;
    std::size_t linkCount = 0;
    std::size_t bufferDataCalls = 0;
    std::size_t bufferSubDataCalls = 0;
    std::size_t drawElementsCalls = 0;
    std::size_t texImage2DCalls = 0;
    std::size_t texSubImage2DCalls = 0;
    std::size_t attribFloatCalls = 0;
    std::size_t attribIntCalls = 0;

    AeroGLint lastViewportW = 0;
    AeroGLint lastViewportH = 0;
    bool blendEnabled = false;
    bool scissorEnabledDuringDraw = false;
    GlScissorBox lastScissor{};

    AeroGLenum lastDrawMode = 0;
    AeroGLsizei lastDrawIndexCount = 0;
    AeroGLenum lastDrawIndexType = 0;
    std::uintptr_t lastDrawByteOffset = 0;

    std::vector<std::uint8_t> uploadedEboBytes;
    std::vector<std::uint8_t> uploadedVboBytes;
    std::string lastShaderSource;

    void ResetFrameCounters() noexcept {
        bufferSubDataCalls = 0;
        drawElementsCalls = 0;
        scissorEnabledDuringDraw = false;
    }
};

MockGlSpyState* gSpy = nullptr;
bool gSpyScissorCurrentlyEnabled = false;

void SpyViewport(AeroGLint, AeroGLint, AeroGLsizei w, AeroGLsizei h) {
    gSpy->lastViewportW = w;
    gSpy->lastViewportH = h;
}
void SpyScissor(AeroGLint x, AeroGLint y, AeroGLsizei w, AeroGLsizei h) {
    gSpy->lastScissor = GlScissorBox{x, y, w, h};
}
void SpyEnable(AeroGLenum cap) {
    if (cap == 0x0BE2u) gSpy->blendEnabled = true;
    if (cap == 0x0C11u) gSpyScissorCurrentlyEnabled = true;
}
void SpyDisable(AeroGLenum cap) {
    if (cap == 0x0BE2u) gSpy->blendEnabled = false;
    if (cap == 0x0C11u) gSpyScissorCurrentlyEnabled = false;
}
void SpyBlendFuncSeparate(AeroGLenum, AeroGLenum, AeroGLenum, AeroGLenum) {}
void SpyClearColor(AeroGLfloat, AeroGLfloat, AeroGLfloat, AeroGLfloat) {}
void SpyClear(AeroGLbitfield) {}

void SpyGenIds(AeroGLsizei n, AeroGLuint* out) {
    for (AeroGLsizei i = 0; i < n; ++i) {
        out[i] = gSpy->nextId++;
    }
}
void SpyDeleteIds(AeroGLsizei, const AeroGLuint*) {}
AeroGLenum gBoundBufferTarget = 0;
void SpyBindBuffer(AeroGLenum target, AeroGLuint) {
    gBoundBufferTarget = target;
}
void SpyBufferData(AeroGLenum target, AeroGLsizeiptr size, const void* data, AeroGLenum) {
    ++gSpy->bufferDataCalls;
    if (target == 0x8892u && size > 0) {
        gSpy->uploadedVboBytes.assign(static_cast<std::size_t>(size), 0u);
        if (data) {
            std::memcpy(gSpy->uploadedVboBytes.data(), data, static_cast<std::size_t>(size));
        }
    } else if (target == 0x8893u && size > 0 && data) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        gSpy->uploadedEboBytes.assign(bytes, bytes + static_cast<std::size_t>(size));
    }
}
void SpyBufferSubData(AeroGLenum target, AeroGLintptr offset, AeroGLsizeiptr size, const void* data) {
    ++gSpy->bufferSubDataCalls;
    if (target == 0x8892u && data && offset >= 0 && size > 0 &&
        static_cast<std::size_t>(offset + size) <= gSpy->uploadedVboBytes.size()) {
        std::memcpy(
            gSpy->uploadedVboBytes.data() + static_cast<std::size_t>(offset),
            data,
            static_cast<std::size_t>(size));
    }
}
void SpyBindVertexArray(AeroGLuint) {}
void SpyEnableVertexAttribArray(AeroGLuint) {}
void SpyVertexAttribPointer(AeroGLuint, AeroGLint, AeroGLenum, AeroGLboolean, AeroGLsizei, const void*) {
    ++gSpy->attribFloatCalls;
}
void SpyVertexAttribIPointer(AeroGLuint, AeroGLint, AeroGLenum, AeroGLsizei, const void*) {
    ++gSpy->attribIntCalls;
}
AeroGLuint SpyCreateShader(AeroGLenum) { return gSpy->nextId++; }
void SpyDeleteShader(AeroGLuint) {}
void SpyShaderSource(AeroGLuint, AeroGLsizei count, const AeroGLchar* const* strs, const AeroGLint* lens) {
    if (count > 0 && strs && strs[0]) {
        const std::size_t n = (lens && lens[0] > 0)
            ? static_cast<std::size_t>(lens[0])
            : std::strlen(strs[0]);
        gSpy->lastShaderSource.assign(strs[0], n);
    }
}
void SpyCompileShader(AeroGLuint) { ++gSpy->compileCount; }
void SpyGetShaderiv(AeroGLuint, AeroGLenum pname, AeroGLint* params) {
    if (pname == 0x8B81u) {
        *params = gSpy->forceVertexCompileFail ? 0 : 1;
    } else if (pname == 0x8B84u) {
        *params = 64;
    }
}
void SpyGetShaderInfoLog(AeroGLuint, AeroGLsizei maxLen, AeroGLsizei* len, AeroGLchar* log) {
    constexpr const char* kMsg = "MOCK_GLSL_SYNTAX_ERR: line 12 unexpected token";
    const auto msgLen = static_cast<AeroGLsizei>(std::strlen(kMsg));
    const AeroGLsizei copyLen = std::min(maxLen - 1, msgLen);
    if (copyLen > 0) {
        std::memcpy(log, kMsg, static_cast<std::size_t>(copyLen));
        log[copyLen] = '\0';
    }
    if (len) *len = copyLen;
}
AeroGLuint SpyCreateProgram() { return gSpy->nextId++; }
void SpyDeleteProgram(AeroGLuint) {}
void SpyAttachShader(AeroGLuint, AeroGLuint) {}
void SpyLinkProgram(AeroGLuint) { ++gSpy->linkCount; }
void SpyGetProgramiv(AeroGLuint, AeroGLenum pname, AeroGLint* params) {
    if (pname == 0x8B82u) {
        *params = gSpy->forceProgramLinkFail ? 0 : 1;
    } else if (pname == 0x8B84u) {
        *params = 64;
    }
}
void SpyGetProgramInfoLog(AeroGLuint, AeroGLsizei maxLen, AeroGLsizei* len, AeroGLchar* log) {
    constexpr const char* kMsg = "MOCK_GLSL_LINK_ERR: varying mismatch";
    const auto msgLen = static_cast<AeroGLsizei>(std::strlen(kMsg));
    const AeroGLsizei copyLen = std::min(maxLen - 1, msgLen);
    if (copyLen > 0) {
        std::memcpy(log, kMsg, static_cast<std::size_t>(copyLen));
        log[copyLen] = '\0';
    }
    if (len) *len = copyLen;
}
void SpyUseProgram(AeroGLuint) {}
AeroGLint SpyGetUniformLocation(AeroGLuint, const AeroGLchar* name) {
    if (std::strcmp(name, "u_ViewportSize") == 0) return 0;
    if (std::strcmp(name, "u_QuadHalfExtent") == 0) return 1;
    if (std::strcmp(name, "u_GlyphAndUiAtlas") == 0) return 2;
    return -1;
}
void SpyUniform1i(AeroGLint, AeroGLint) {}
void SpyUniform2f(AeroGLint, AeroGLfloat, AeroGLfloat) {}
void SpyActiveTexture(AeroGLenum) {}
void SpyBindTexture(AeroGLenum, AeroGLuint) {}
void SpyTexImage2D(AeroGLenum, AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei, AeroGLint, AeroGLenum, AeroGLenum, const void*) {
    ++gSpy->texImage2DCalls;
}
void SpyTexSubImage2D(AeroGLenum, AeroGLint, AeroGLint, AeroGLint, AeroGLsizei, AeroGLsizei, AeroGLenum, AeroGLenum, const void*) {
    ++gSpy->texSubImage2DCalls;
}
void SpyTexParameteri(AeroGLenum, AeroGLenum, AeroGLint) {}
void SpyPixelStorei(AeroGLenum, AeroGLint) {}
void SpyDrawElements(AeroGLenum mode, AeroGLsizei count, AeroGLenum type, const void* indices) {
    ++gSpy->drawElementsCalls;
    gSpy->lastDrawMode = mode;
    gSpy->lastDrawIndexCount = count;
    gSpy->lastDrawIndexType = type;
    gSpy->lastDrawByteOffset = reinterpret_cast<std::uintptr_t>(indices);
    if (gSpyScissorCurrentlyEnabled) {
        gSpy->scissorEnabledDuringDraw = true;
    }
}
void SpyReadPixels(AeroGLint, AeroGLint, AeroGLsizei w, AeroGLsizei h, AeroGLenum, AeroGLenum, void* data) {
    if (data && w > 0 && h > 0) {
        std::memset(data, 0x7Fu, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    }
}
AeroGLenum SpyGetError() { return 0u; }
const std::uint8_t* SpyGetString(AeroGLenum) {
    return reinterpret_cast<const std::uint8_t*>("MockGL 3.3 Core AeroSpy");
}

GlFunctionTable MakeMockGlFunctionTable() noexcept {
    GlFunctionTable t{};
    t.Viewport = &SpyViewport;
    t.Scissor = &SpyScissor;
    t.Enable = &SpyEnable;
    t.Disable = &SpyDisable;
    t.BlendFuncSeparate = &SpyBlendFuncSeparate;
    t.ClearColor = &SpyClearColor;
    t.Clear = &SpyClear;
    t.GenBuffers = &SpyGenIds;
    t.DeleteBuffers = &SpyDeleteIds;
    t.BindBuffer = &SpyBindBuffer;
    t.BufferData = &SpyBufferData;
    t.BufferSubData = &SpyBufferSubData;
    t.GenVertexArrays = &SpyGenIds;
    t.DeleteVertexArrays = &SpyDeleteIds;
    t.BindVertexArray = &SpyBindVertexArray;
    t.EnableVertexAttribArray = &SpyEnableVertexAttribArray;
    t.VertexAttribPointer = &SpyVertexAttribPointer;
    t.VertexAttribIPointer = &SpyVertexAttribIPointer;
    t.CreateShader = &SpyCreateShader;
    t.DeleteShader = &SpyDeleteShader;
    t.ShaderSource = &SpyShaderSource;
    t.CompileShader = &SpyCompileShader;
    t.GetShaderiv = &SpyGetShaderiv;
    t.GetShaderInfoLog = &SpyGetShaderInfoLog;
    t.CreateProgram = &SpyCreateProgram;
    t.DeleteProgram = &SpyDeleteProgram;
    t.AttachShader = &SpyAttachShader;
    t.LinkProgram = &SpyLinkProgram;
    t.GetProgramiv = &SpyGetProgramiv;
    t.GetProgramInfoLog = &SpyGetProgramInfoLog;
    t.UseProgram = &SpyUseProgram;
    t.GetUniformLocation = &SpyGetUniformLocation;
    t.Uniform1i = &SpyUniform1i;
    t.Uniform2f = &SpyUniform2f;
    t.GenTextures = &SpyGenIds;
    t.DeleteTextures = &SpyDeleteIds;
    t.ActiveTexture = &SpyActiveTexture;
    t.BindTexture = &SpyBindTexture;
    t.TexImage2D = &SpyTexImage2D;
    t.TexSubImage2D = &SpyTexSubImage2D;
    t.TexParameteri = &SpyTexParameteri;
    t.PixelStorei = &SpyPixelStorei;
    t.DrawElements = &SpyDrawElements;
    t.ReadPixels = &SpyReadPixels;
    t.GetError = &SpyGetError;
    t.GetString = &SpyGetString;
    return t;
}

float TriangleAreaAbs(const AeroVertex& a, const AeroVertex& b, const AeroVertex& c) noexcept {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return 0.5f * std::fabs(cross);
}

} // namespace

// ============================================================================
// Test 1: Quad EBO Index Mapping (4 Vertices -> 6 Indices Dual Triangles)
// ============================================================================
void TestQuadEboIndexMappingAndAreaConservation() {
    std::vector<std::uint32_t> indices;
    BuildStaticQuadIndexBuffer(128u, indices);
    AERO_CHECK(indices.size() == 128u * 6u, "128 quads produce exactly 768 uint32 indices");

    for (std::size_t q = 0; q < 128u; ++q) {
        const auto baseV = static_cast<std::uint32_t>(q * 4u);
        const std::size_t off = q * 6u;
        AERO_CHECK(indices[off + 0u] == baseV + 0u, "Tri0.i0 == v0");
        AERO_CHECK(indices[off + 1u] == baseV + 1u, "Tri0.i1 == v1");
        AERO_CHECK(indices[off + 2u] == baseV + 2u, "Tri0.i2 == v2");
        AERO_CHECK(indices[off + 3u] == baseV + 0u, "Tri1.i0 == v0");
        AERO_CHECK(indices[off + 4u] == baseV + 2u, "Tri1.i1 == v2");
        AERO_CHECK(indices[off + 5u] == baseV + 3u, "Tri1.i2 == v3");
    }

    // Verify exact geometric match with PagedRenderTree::AllocateRenderNode 4-vertex layout
    PagedRenderTree tree;
    const std::uint32_t nid = tree.AllocateRenderNode(
        24.0f,
        36.0f,
        180.0f,
        64.0f,
        Color{0.2f, 0.7f, 1.0f, 1.0f},
        8.0f);
    AERO_CHECK(nid == 0u && tree.PageCount() == 1u, "Allocated 1 quad render node");

    const CommandPage& page = tree.GetPage(0u);
    const AeroVertex& v0 = page.vertices[indices[0]];
    const AeroVertex& v1 = page.vertices[indices[1]];
    const AeroVertex& v2 = page.vertices[indices[2]];
    const AeroVertex& v3 = page.vertices[indices[5]];

    const float areaTri0 = TriangleAreaAbs(v0, v1, v2);
    const float areaTri1 = TriangleAreaAbs(v0, v2, v3);
    const float expectedRectArea = 180.0f * 64.0f;
    AERO_CHECK(std::fabs((areaTri0 + areaTri1) - expectedRectArea) < 0.01f,
               "Dual triangles (0,1,2) + (0,2,3) tile exact 180x64 quad area with 0 gap/overlap");

    // Verify OpenGL bottom-left glScissor Y-inversion from top-left UI coordinates
    const GlScissorBox sc = ComputeGlScissorBox(Rect{50.0f, 100.0f, 300.0f, 200.0f}, 800u, 600u);
    AERO_CHECK(sc.x == 50 && sc.width == 300 && sc.height == 200, "Scissor X/W/H preserved");
    AERO_CHECK(sc.y == (600 - (100 + 200)), "Scissor Y inverted from top-left (100..300) to bottom-left 300");

    std::cout << "[PASS] TestQuadEboIndexMappingAndAreaConservation\n";
}

// ============================================================================
// Test 2: OpenGlDeviceBackend Full Pipeline, Shader Diagnostics & EBO Auto-Growth
// ============================================================================
void TestOpenGlDeviceBackendPipelineAndDiagnostics() {
    MockGlSpyState spyState{};
    gSpy = &spyState;
    gSpyScissorCurrentlyEnabled = false;

    const GlFunctionTable mockTable = MakeMockGlFunctionTable();
    AERO_CHECK(mockTable.IsComplete(), "Mock GlFunctionTable complete");

    // 2a. Verify shader compile error diagnostic capture
    {
        spyState.forceVertexCompileFail = true;
        OpenGlDeviceBackend failBackend(mockTable);
        GpuBackendConfig cfg{};
        cfg.maxPagesInRing = 16u;
        cfg.initialQuadCapacity = 64u;
        AERO_CHECK(!failBackend.Initialize(cfg), "Shader compile failure cleanly aborts Initialize");
        AERO_CHECK(
            failBackend.LastDiagnostics().find("MOCK_GLSL_SYNTAX_ERR") != std::string_view::npos,
            "Shader compile infoLog captured into diagnostics");
        spyState.forceVertexCompileFail = false;
    }

    // 2b. Verify program link error diagnostic capture
    {
        spyState.forceProgramLinkFail = true;
        OpenGlDeviceBackend linkFailBackend(mockTable);
        GpuBackendConfig cfg{};
        cfg.maxPagesInRing = 16u;
        AERO_CHECK(!linkFailBackend.Initialize(cfg), "Program link failure cleanly aborts Initialize");
        AERO_CHECK(
            linkFailBackend.LastDiagnostics().find("MOCK_GLSL_LINK_ERR") != std::string_view::npos,
            "Program link infoLog captured into diagnostics");
        spyState.forceProgramLinkFail = false;
    }

    // 2c. Full GL3.3 + WebGL2 UberShader init, Atlas sub-region update, EBO expansion & Scissor draw
    OpenGlDeviceBackend glBackend(mockTable);
    GpuBackendConfig cfg{};
    cfg.shaderBackend = ShaderBackend::DesktopGL33;
    cfg.maxPagesInRing = 16u; // 16 * 42 = 672 quads initial EBO
    cfg.initialQuadCapacity = 672u;
    cfg.defaultAtlasSize = 128u;
    AERO_CHECK(glBackend.Initialize(cfg), "OpenGlDeviceBackend initialized cleanly");
    AERO_CHECK(spyState.attribFloatCalls >= 2u && spyState.attribIntCalls >= 2u,
               "Bound 2 float attributes (pos, uv) and 2 integer uint attributes (rgba, primFlags)");
    AERO_CHECK(glBackend.QuadIndexCapacity() == 672u, "Initial static Quad EBO capacity == 672 quads");
    AERO_CHECK(spyState.uploadedEboBytes.size() == 672u * 6u * sizeof(std::uint32_t),
               "GPU EBO uploaded 672 * 6 uint32 indices");

    // Test UpdateAtlasSubRegion
    std::vector<std::uint8_t> subPatch(16u * 16u * 4u, 0xEEu);
    AERO_CHECK(glBackend.UpdateAtlasSubRegion(8u, 8u, 16u, 16u, subPatch.data()),
               "UpdateAtlasSubRegion succeeded");
    AERO_CHECK(!glBackend.UpdateAtlasSubRegion(120u, 120u, 16u, 16u, subPatch.data()),
               "Out-of-bounds UpdateAtlasSubRegion safely rejected");
    AERO_CHECK(glBackend.Telemetry().atlasSubUpdateCount == 1u, "Atlas sub-update telemetry incremented");

    // Build clipped ScrollViewer + children so scissor batch & multi-quad EBO draw are exercised
    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(640.0f);
    root->SetHeight(480.0f);

    auto scroll = MakeIntrusive<ScrollViewer>();
    Canvas::SetLeft(scroll.Get(), 40.0f);
    Canvas::SetTop(scroll.Get(), 50.0f);
    scroll->SetWidth(300.0f);
    scroll->SetHeight(180.0f);
    scroll->SetValue(PropId_ClipToBounds, UnifiedValue{true});

    auto innerStack = MakeIntrusive<StackPanel>();
    for (int i = 0; i < 6; ++i) {
        auto item = MakeIntrusive<Border>();
        item->SetWidth(260.0f);
        item->SetHeight(40.0f);
        item->SetBackground(Color{0.2f, 0.5f, 0.9f, 1.0f});
        item->SetCornerRadius(CornerRadius{6.0f});
        innerStack->AddChild(item.Get());
    }
    scroll->AddChild(innerStack.Get());
    root->AddChild(scroll.Get());
    root->Measure(Size{640.0f, 480.0f});
    root->Arrange(Rect{0.0f, 0.0f, 640.0f, 480.0f});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    ZeroCopyGpuStream stream(ShaderBackend::DesktopGL33, 16u);

    spyState.ResetFrameCounters();
    const GpuBackendFrameStats frame1 = glBackend.SubmitAndPresent(tree, stream, 640u, 480u);
    AERO_CHECK(frame1.succeeded && frame1.drawCallsIssued >= 1u, "Frame 1 submitted GL_TRIANGLES draw calls");
    AERO_CHECK(frame1.indicesSubmitted == frame1.quadsSubmitted * 6u, "Every quad drew 6 EBO indices");
    AERO_CHECK(frame1.trianglesSubmitted == frame1.quadsSubmitted * 2u, "Every quad drew 2 triangles");
    AERO_CHECK(spyState.lastDrawMode == 0x0004u && spyState.lastDrawIndexType == 0x1405u,
               "glDrawElements used GL_TRIANGLES + GL_UNSIGNED_INT");
    AERO_CHECK(spyState.blendEnabled, "GL_BLEND enabled during UberShader submission");
    AERO_CHECK(spyState.scissorEnabledDuringDraw && frame1.scissorBatchCount >= 1u,
               "GL_SCISSOR_TEST enabled and glScissor applied for ScrollViewer clip batch");

    // Incremental clean frame: 0 dirty pages -> 0 glBufferSubData calls!
    spyState.ResetFrameCounters();
    const GpuBackendFrameStats frame2 = glBackend.SubmitAndPresent(tree, stream, 640u, 480u);
    AERO_CHECK(frame2.dirtySpansUploaded == 0u && spyState.bufferSubDataCalls == 0u,
               "Clean frame issued 0 glBufferSubData uploads");

    // Dynamic EBO expansion check (> 672 quads -> expands to >= 2000 quads)
    AERO_CHECK(glBackend.EnsureQuadIndexCapacity(2000u), "Dynamic Quad EBO expansion succeeded");
    AERO_CHECK(glBackend.QuadIndexCapacity() >= 2000u, "Quad EBO capacity expanded >= 2000");
    AERO_CHECK(glBackend.Telemetry().quadEboExpansionCount == 1u, "Recorded 1 Quad EBO expansion");
    const auto& mirror = glBackend.QuadIndexCpuMirror();
    AERO_CHECK(mirror[1999u * 6u + 0u] == 1999u * 4u + 0u &&
               mirror[1999u * 6u + 5u] == 1999u * 4u + 3u,
               "Expanded Quad #1999 indices match (7996..7999)");

    // Verify WebGL2 UberShader compilation through OpenGlDeviceBackend
    AERO_CHECK(glBackend.CompileAndLinkUberShader(
                   ZeroCopyGpuStream::UberVertexShaderSource(ShaderBackend::WebGL2),
                   ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend::WebGL2)),
               "WebGL2 #version 300 es UberShader compiled and linked cleanly");

    glBackend.Shutdown();
    gSpy = nullptr;
    std::cout << "[PASS] TestOpenGlDeviceBackendPipelineAndDiagnostics\n";
}

// ============================================================================
// Test 3: Dynamic OpenGL dlopen Probe & Headless SoftwareSurfaceBackend Fallback
// ============================================================================
void TestDynamicGlProbeAndHeadlessFallback() {
    const GlDynamicProbeResult probe = ProbeOpenGlDynamicLibrary();
    std::cout << "[INFO] OpenGL Dynamic Probe: " << probe.diagnostics << "\n";
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    // On Linux container with /usr/lib/x86_64-linux-gnu/libGL.so.1 installed, verify dlopen found it!
    if (probe.libGlLoaded) {
        AERO_CHECK(probe.coreEntryPointsFound && probe.resolvedSymbolCount >= 20u,
                   "dlopen(libGL.so.1) dynamically resolved >= 20 core GL entry points without -lGL link");
    }
#endif

    std::string backendDiag;
    GpuBackendConfig cfg{};
    cfg.initialViewportWidth = 320u;
    cfg.initialViewportHeight = 200u;
    cfg.preferHardwareGpu = true;
    auto backend = CreateBestAvailableGpuBackend(cfg, &backendDiag);
    AERO_CHECK(backend != nullptr && backend->IsInitialized(),
               "CreateBestAvailableGpuBackend returned initialized backend");
    if (!probe.hasCurrentContext) {
        AERO_CHECK(backend->Kind() == GpuBackendKind::Software,
                   "Without active GL context, backend automatically degraded to SoftwareSurfaceBackend");
    }
    std::cout << "[INFO] Selected Backend: " << backend->BackendName() << " -> " << backendDiag << "\n";
    std::cout << "[PASS] TestDynamicGlProbeAndHeadlessFallback\n";
}

// ============================================================================
// Test 4: NativeWindowHost Zero-CPU Blocking Pipe Wake, Input Routing, IME & RealScript Button Click
// ============================================================================
void TestNativeWindowHostRealScriptClickImeAndScreenshots() {
    AxbTemplateRegistry::Instance().Clear();

    AeroCompiler compiler;
    AeroCompileInput compileInput{};
    compileInput.xamlPath = "CommandDeckWindow.xaml";
    compileInput.xamlContent = R"(
<Window x:Class="Aero.Demo.CommandDeckWindow" Width="640" Height="360">
    <Border Width="640" Height="360" Background="#121824" Padding="24">
        <StackPanel Spacing="14">
            <Border Width="592" Height="52" Background="#1D283A" CornerRadius="8" Padding="12">
                <TextBlock x:Name="TitleBanner" Text="{Binding StatusTitle}" FontSize="18" Foreground="#E8F1FF" />
            </Border>
            <Border Width="592" Height="28" Background="#182232" CornerRadius="6" Padding="4">
                <Border x:Name="EnergyFillBar" Width="{Binding EnergyBarWidth}" Height="20" Background="#2AD882" CornerRadius="4" HorizontalAlignment="Left" />
            </Border>
            <Button x:Name="BoostBtn" Width="220" Height="44" Content="Boost Reactor" Background="#2563EB" Foreground="#FFFFFF" CornerRadius="8" HorizontalAlignment="Left" Click="OnBoostReactorClick" />
            <TextBox x:Name="CallsignInput" Width="320" Height="36" Text="Commander" FontSize="15" Background="#1E293B" Foreground="#F8FAFC" HorizontalAlignment="Left" />
        </StackPanel>
    </Border>
</Window>
)";
    compileInput.scriptSources.push_back(SourceFile{
        "CommandDeckWindow.rs",
        R"(
module Aero.Demo;

public partial class CommandDeckWindow
{
    [Observable]
    public int ClickCount { get; set; }

    [Observable]
    public float EnergyBarWidth { get; set; }

    [Observable]
    public string StatusTitle { get; set; }

    public CommandDeckWindow()
    {
        ClickCount = 0;
        EnergyBarWidth = 120.0f;
        StatusTitle = "REACTOR STANDBY [0]";
    }

    public void OnBoostReactorClick()
    {
        ClickCount = ClickCount + 1;
        EnergyBarWidth = EnergyBarWidth + 220.0f;
        StatusTitle = "REACTOR ONLINE [BOOSTED]";
    }
}

public CommandDeckWindow CreateCommandDeck()
{
    return new CommandDeckWindow();
}
)"});

    const AeroCompileResult compiled = compiler.Compile(compileInput);
    if (!compiled.succeeded) {
        for (const auto& d : compiled.diagnostics) {
            std::cerr << "Compile diag: " << d << "\n";
        }
    }
    AERO_CHECK(compiled.succeeded && compiled.programImage != nullptr,
               "Compiled XAML + RealScript CommandDeckWindow");

    ExecutionDomainContext domains;
    domains.registerProgramObservableMetadata(*compiled.programImage);
    Interpreter uiInterpreter(compiled.programImage, domains.uiHeapPtr());
    uiInterpreter.setExecutionDomainContext(&domains, DomainKind::UiPresentation);

    const auto createRes = uiInterpreter.invoke("Aero.Demo::CreateCommandDeck");
    AERO_CHECK(createRes.succeeded, "Instantiated CommandDeckWindow ViewModel on UiHeap");
    const ObjectRef vmRef = std::get<ObjectRef>(createRes.value);
    auto vmRoot = domains.uiHeap().retain(vmRef);
    domains.uiHeap().clearDirtyFieldMask(vmRef);
    (void)domains.uiHeap().consumeDirtyObjects();

    DualTrackBindingEngine bindingEngine(domains, compiled.programImage.get(), &uiInterpreter);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 128u);

    auto windowVisual = MakeIntrusive<Window>();
    AERO_CHECK(
        windowVisual->MountTemplate(
            compiled.axbPackage,
            &domains,
            &bindingEngine,
            vmRef,
            &uiInterpreter),
        "Mounted compiled .axb4 package onto Window");
    windowVisual->SetAsRootVisual(true, &domains.nativeHandles(), &domains.uiHeap());

    InputEventRouter inputRouter(windowVisual.Get());
    DesktopEventHost desktopHost(windowVisual.Get(), &bindingEngine, &layoutQueue, &renderTree);
    desktopHost.SetInputRouter(&inputRouter);

    NativeWindowHost windowHost;
    windowHost.BindDesktopHost(&desktopHost, &renderTree, &gpuStream, &inputRouter);

    NativeWindowConfig winCfg{};
    winCfg.title = "AeroGUI 2.0 p0-2 Verification";
    winCfg.width = 640u;
    winCfg.height = 360u;
    winCfg.forceHeadless = true; // Deterministic headless verification + self-pipe wake
    AERO_CHECK(windowHost.Initialize(winCfg), "NativeWindowHost initialized cleanly");
    AERO_CHECK(windowHost.GpuBackend() != nullptr &&
               windowHost.GpuBackend()->Kind() == GpuBackendKind::Software,
               "Headless mode backed by SoftwareSurfaceBackend");

    // 1. Render initial frame & verify idle 0% CPU sleep state
    AERO_CHECK(windowHost.PumpEventsAndRender(0.016f, 0), "Initial frame pumped & rendered");
    AERO_CHECK(desktopHost.CanSleepIdle(), "DesktopEventHost enters 0% CPU CanSleepIdle() after clean frame");
    AERO_CHECK(desktopHost.CpuPollingUsagePercent() == 0.0f, "0.0% idle CPU polling usage");

    auto* boostBtn = windowVisual->FindName("BoostBtn")->DynamicCast<Button>();
    auto* energyBar = windowVisual->FindName("EnergyFillBar")->DynamicCast<Border>();
    auto* titleBanner = windowVisual->FindName("TitleBanner")->DynamicCast<TextBlock>();
    auto* callsignInput = windowVisual->FindName("CallsignInput")->DynamicCast<TextBox>();
    AERO_CHECK(boostBtn && energyBar && titleBanner && callsignInput, "All named controls resolved");
    AERO_CHECK(std::fabs(energyBar->FinalRect().width - 120.0f) < 0.5f, "Initial EnergyFillBar width == 120px");

    const std::string beforePpm = "/working_dir/c_1a21a30fb41e1c84/scratch/subagent_work/p0_2/test_frame_before.ppm";
    AERO_CHECK(windowHost.SaveScreenshotPpm(beforePpm), "Exported initial frame PPM screenshot");

    std::vector<std::uint8_t> frameBeforeRgba;
    std::uint32_t fbW = 0u;
    std::uint32_t fbH = 0u;
    AERO_CHECK(windowHost.CaptureFramebufferRgba8(frameBeforeRgba, fbW, fbH), "Readback initial RGBA8 frame");

    // 2a. Test real 0%-CPU blocking OS pipe sleep + cross-thread dirty wake signal!
    const auto sleepStart = std::chrono::steady_clock::now();
    std::thread asyncWaker([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        desktopHost.NotifyDirtyObjects();
    });
    // Main thread blocks inside poll() with a 2000ms ceiling, waking immediately when asyncWaker writes wake pipe!
    AERO_CHECK(windowHost.PumpEventsAndRender(0.016f, 2000), "Woke from blocking OS wait on pipe signal");
    asyncWaker.join();

    // 2b. Dispatch platform mouse move + click on BoostBtn -> InputEventRouter -> RealScript callback!
    const Rect btnRect = boostBtn->LocalToScreenBounds(boostBtn->FinalRect());
    const float clickX = btnRect.x + btnRect.width * 0.5f;
    const float clickY = btnRect.y + btnRect.height * 0.5f;
    windowHost.OnPlatformMouseMove(clickX, clickY, KeyMod_None);
    windowHost.OnPlatformMouseButton(clickX, clickY, PointerButton::Left, true, KeyMod_None);
    windowHost.OnPlatformMouseButton(clickX, clickY, PointerButton::Left, false, KeyMod_None);
    AERO_CHECK(windowHost.PumpEventsAndRender(0.016f, 0), "Dispatched queued button click and rendered dirty page");
    const auto wakeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sleepStart).count();
    AERO_CHECK(wakeElapsedMs >= 15 && wakeElapsedMs < 1000,
               "Blocking event loop slept until pipe wake signal without timing out");
    AERO_CHECK(windowHost.Telemetry().idleSleepEntries >= 1u &&
               windowHost.Telemetry().pipeOrEventWakeCount >= 1u,
               "Recorded idle sleep entry and OS wake pipe signal");

    // Verify RealScript callback mutated [Observable] properties AND reactive Track A + Layout + Render updated!
    const SymbolId vmTypeId = rs_sem::stableTypeId("Aero.Demo::CommandDeckWindow");
    const auto clickSlot = *domains.resolvePropertySlot(*compiled.programImage, vmTypeId, "ClickCount");
    UnifiedValue clickCountVal{};
    AERO_CHECK(domains.readPropertySlotFast(DomainKind::Ui, vmRef, clickSlot, clickCountVal),
               "Read ClickCount slot from RealScript ViewModel");
    AERO_CHECK(std::get<std::int64_t>(clickCountVal) == 1,
               "Button click executed RealScript OnBoostReactorClick (ClickCount == 1)");
    AERO_CHECK(titleBanner->GetText() == "REACTOR ONLINE [BOOSTED]",
               "Reactive Track A updated TitleBanner TextBlock");
    AERO_CHECK(std::fabs(energyBar->FinalRect().width - 340.0f) < 0.5f,
               "EnergyFillBar width expanded from 120px -> 340px after RealScript click");

    // 3. Test TextBox focus, ITextInputMethodHost caret rect sync, keyboard + UTF-8 CJK text input!
    const Rect tbRect = callsignInput->LocalToScreenBounds(callsignInput->FinalRect());
    windowHost.OnPlatformMouseButton(tbRect.x + 12.0f, tbRect.y + 12.0f, PointerButton::Left, true, KeyMod_None);
    windowHost.OnPlatformMouseButton(tbRect.x + 12.0f, tbRect.y + 12.0f, PointerButton::Left, false, KeyMod_None);
    windowHost.OnPlatformKey(KeyCode::End, true, KeyMod_None);
    windowHost.OnPlatformKey(KeyCode::End, false, KeyMod_None);
    windowHost.OnPlatformTextInput("-7号");
    AERO_CHECK(windowHost.PumpEventsAndRender(0.016f, 0), "Processed TextBox focus + UTF-8 input");

    AERO_CHECK(inputRouter.FocusedElement() == callsignInput, "CallsignInput TextBox acquired focus");
    AERO_CHECK(windowHost.DesktopIme().IsImeActive() && windowHost.Telemetry().lastImeActive,
               "DesktopImeAdapter activated upon TextBox focus");
    AERO_CHECK(windowHost.DesktopIme().CaretScreenRect().x >= tbRect.x &&
               windowHost.DesktopIme().CaretScreenRect().height > 0.0f,
               "ITextInputMethodHost caret screen rectangle synchronized accurately");
    AERO_CHECK(callsignInput->GetText() == "Commander-7号",
               "UTF-8 ASCII + CJK text appended into focused TextBox");

    // 4. Verify rendered pixel change & export PPM + BMP post-click screenshots
    std::vector<std::uint8_t> frameAfterRgba;
    AERO_CHECK(windowHost.CaptureFramebufferRgba8(frameAfterRgba, fbW, fbH), "Readback post-click RGBA8 frame");

    // Sample pixel at x = 24 (padding) + 4 (bar padding) + 250 = 278 inside EnergyFillBar:
    // Before click (width=120 -> ends at x=148), x=278 was dark track #182232;
    // After click (width=340 -> ends at x=368), x=278 is bright green #2AD882!
    const Rect barScreen = energyBar->LocalToScreenBounds(energyBar->FinalRect());
    const auto probeX = static_cast<std::uint32_t>(barScreen.x + 220.0f);
    const auto probeY = static_cast<std::uint32_t>(barScreen.y + barScreen.height * 0.5f);
    const std::size_t pxIdx = (static_cast<std::size_t>(probeY) * fbW + probeX) * 4u;
    AERO_CHECK(pxIdx + 3u < frameAfterRgba.size(), "Probe pixel inside 640x360 framebuffer");
    const std::uint8_t greenBefore = frameBeforeRgba[pxIdx + 1u];
    const std::uint8_t greenAfter = frameAfterRgba[pxIdx + 1u];
    AERO_CHECK(greenAfter > greenBefore + 120u,
               "Framebuffer pixel at expanded EnergyFillBar turned bright green (#2AD882) after RealScript click");

    const std::string afterPpm = "/working_dir/c_1a21a30fb41e1c84/scratch/subagent_work/p0_2/test_frame_after.ppm";
    const std::string afterBmp = "/working_dir/c_1a21a30fb41e1c84/scratch/subagent_work/p0_2/test_frame_after.bmp";
    AERO_CHECK(windowHost.SaveScreenshotPpm(afterPpm), "Saved post-click PPM screenshot");
    AERO_CHECK(windowHost.SaveScreenshotBmp(afterBmp), "Saved post-click BMP screenshot");

    // Verify BMP magic bytes & exact file size (54B header + 640*360*4 = 921,654 bytes)
    std::FILE* bmpFp = std::fopen(afterBmp.c_str(), "rb");
    AERO_CHECK(bmpFp != nullptr, "Opened exported BMP file");
    std::uint8_t magic[2]{};
    AERO_CHECK(std::fread(magic, 1u, 2u, bmpFp) == 2u && magic[0] == 'B' && magic[1] == 'M',
               "Exported BMP starts with valid 'BM' signature");
    std::fseek(bmpFp, 0, SEEK_END);
    const long bmpBytes = std::ftell(bmpFp);
    std::fclose(bmpFp);
    AERO_CHECK(bmpBytes == static_cast<long>(54u + 640u * 360u * 4u),
               "Exported 32-bit BGRA BMP matches exact expected byte size");

    windowHost.Shutdown();
    std::cout << "[PASS] TestNativeWindowHostRealScriptClickImeAndScreenshots\n";
}

// ============================================================================
// Test 5: WebCanvasHost On-Demand rAF State Machine & WebImeAdapter Sync
// ============================================================================
void TestWebCanvasHostRafStateMachineBridge() {
    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(480.0f);
    root->SetHeight(270.0f);

    auto tbox = MakeIntrusive<TextBox>();
    Canvas::SetLeft(tbox.Get(), 30.0f);
    Canvas::SetTop(tbox.Get(), 40.0f);
    tbox->SetWidth(200.0f);
    tbox->SetHeight(32.0f);
    tbox->SetText("WebGL2");
    root->AddChild(tbox.Get());

    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream webStream(ShaderBackend::WebGL2, 64u);
    WebCanvasHost webHost(root.Get(), nullptr, &layoutQueue, ShaderBackend::WebGL2, &renderTree);
    InputEventRouter router(root.Get());
    webHost.SetInputRouter(&router);

    NativeWindowHost host;
    host.BindWebCanvasHost(&webHost, &renderTree, &webStream, &router);

    NativeWindowConfig cfg{};
    cfg.width = 480u;
    cfg.height = 270u;
    cfg.forceHeadless = true;
    AERO_CHECK(host.Initialize(cfg), "Initialized NativeWindowHost bound to WebCanvasHost");
    AERO_CHECK(router.ImeHost() == &webHost.ImeAdapter(), "InputEventRouter auto-wired to WebImeAdapter");

    // Frame 1 lays out & syncs visual tree; Frame 2 has 0 changes -> auto-suspends rAF!
    (void)host.TickAndPresentFrame(0.016f, true);
    (void)host.TickAndPresentFrame(0.016f, false);
    AERO_CHECK(webHost.RafState() == WebRafState::SuspendedIdle && webHost.CanSleepIdle(),
               "WebCanvasHost auto-suspended rAF when clean (0% idle CPU/GPU)");

    // Mouse click on TextBox wakes rAF and syncs WebImeAdapter overlay CSS rect!
    host.OnPlatformMouseButton(50.0f, 52.0f, PointerButton::Left, true, KeyMod_None);
    host.OnPlatformMouseButton(50.0f, 52.0f, PointerButton::Left, false, KeyMod_None);
    AERO_CHECK(webHost.RafState() == WebRafState::ActiveTicking,
               "Platform input immediately transitioned WebCanvasHost rAF to ActiveTicking");
    (void)host.TickAndPresentFrame(0.016f, false);
    AERO_CHECK(webHost.ImeAdapter().IsImeActive(), "WebImeAdapter active after clicking TextBox");
    AERO_CHECK(webHost.ImeAdapter().GenerateOverlayCssStyle().find("position:fixed;") != std::string::npos,
               "WebImeAdapter generated valid DOM overlay CSS style");

    host.Shutdown();
    std::cout << "[PASS] TestWebCanvasHostRafStateMachineBridge\n";
}

} // namespace aero::p0_2_tests

int main() {
    std::cout << "=== AeroGUI 2.0 p0-2 GPU Backend & Native/Web Window Host Tests ===\n";
    aero::EnsureBuiltinTypesRegistered();

    aero::p0_2_tests::TestQuadEboIndexMappingAndAreaConservation();
    aero::p0_2_tests::TestOpenGlDeviceBackendPipelineAndDiagnostics();
    aero::p0_2_tests::TestDynamicGlProbeAndHeadlessFallback();
    aero::p0_2_tests::TestNativeWindowHostRealScriptClickImeAndScreenshots();
    aero::p0_2_tests::TestWebCanvasHostRafStateMachineBridge();

    std::cout << "=== ALL p0-2 TESTS PASSED ===\n";
    return 0;
}
