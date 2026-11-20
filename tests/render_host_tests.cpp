#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aero/asset/GradientBrush.hpp"
#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/drawing/Geometry.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/RiveRenderer.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/SkylinePacker.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualRenderAdapter.hpp"
#include "aero/visual/VisualTree.hpp"

namespace {

int gFailureCount = 0;

#define AERO_CHECK(cond)                                                                      \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::fprintf(stderr, "CHECK FAILED (%s:%d): %s\n", __FILE__, __LINE__, #cond);   \
            ++gFailureCount;                                                                  \
            return false;                                                                     \
        }                                                                                     \
    } while (false)

// ============================================================================
// Mock OpenGL Function Table & Call Spy (no GL driver / window required)
// ============================================================================

struct MockGlSpyState {
    void Reset() {
        nextId = 1u;
        boundFbo = 0u;
        boundProgram = 0u;
        genFboCalls = 0;
        deleteFboCalls = 0;
        bindFboCalls = 0;
        fbTex2dCalls = 0;
        checkFbStatusCalls = 0;
        drawCalls = 0;
        texImage2dCalls = 0;
        texSubImage2dCalls = 0;
        lastGlViewportW = 0;
        lastGlViewportH = 0;
        lastUniformViewportW = 0.0f;
        lastUniformViewportH = 0.0f;
        scissorHistory.clear();
        fboBindHistory.clear();
        programBindHistory.clear();
    }

    aero::AeroGLuint nextId = 1u;
    aero::AeroGLuint boundFbo = 0u;
    aero::AeroGLuint boundProgram = 0u;
    int genFboCalls = 0;
    int deleteFboCalls = 0;
    int bindFboCalls = 0;
    int fbTex2dCalls = 0;
    int checkFbStatusCalls = 0;
    int drawCalls = 0;
    int texImage2dCalls = 0;
    int texSubImage2dCalls = 0;
    aero::AeroGLsizei lastGlViewportW = 0;
    aero::AeroGLsizei lastGlViewportH = 0;
    aero::AeroGLfloat lastUniformViewportW = 0.0f;
    aero::AeroGLfloat lastUniformViewportH = 0.0f;
    struct ScissorRecord {
        aero::AeroGLint x = 0;
        aero::AeroGLint y = 0;
        aero::AeroGLsizei w = 0;
        aero::AeroGLsizei h = 0;
    };
    std::vector<ScissorRecord> scissorHistory;
    std::vector<aero::AeroGLuint> fboBindHistory;
    std::vector<aero::AeroGLuint> programBindHistory;
};

MockGlSpyState gMockGl;

void AERO_GL_APIENTRY MockViewport(aero::AeroGLint, aero::AeroGLint, aero::AeroGLsizei w, aero::AeroGLsizei h) {
    gMockGl.lastGlViewportW = w;
    gMockGl.lastGlViewportH = h;
}
void AERO_GL_APIENTRY MockScissor(aero::AeroGLint x, aero::AeroGLint y, aero::AeroGLsizei w, aero::AeroGLsizei h) {
    gMockGl.scissorHistory.push_back(MockGlSpyState::ScissorRecord{x, y, w, h});
}
void AERO_GL_APIENTRY MockEnable(aero::AeroGLenum) {}
void AERO_GL_APIENTRY MockDisable(aero::AeroGLenum) {}
void AERO_GL_APIENTRY MockBlendFuncSeparate(aero::AeroGLenum, aero::AeroGLenum, aero::AeroGLenum, aero::AeroGLenum) {}
void AERO_GL_APIENTRY MockClearColor(aero::AeroGLfloat, aero::AeroGLfloat, aero::AeroGLfloat, aero::AeroGLfloat) {}
void AERO_GL_APIENTRY MockClear(aero::AeroGLbitfield) {}

void AERO_GL_APIENTRY MockGenBuffers(aero::AeroGLsizei n, aero::AeroGLuint* out) {
    for (aero::AeroGLsizei i = 0; i < n; ++i) out[i] = gMockGl.nextId++;
}
void AERO_GL_APIENTRY MockDeleteBuffers(aero::AeroGLsizei, const aero::AeroGLuint*) {}
void AERO_GL_APIENTRY MockBindBuffer(aero::AeroGLenum, aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockBufferData(aero::AeroGLenum, aero::AeroGLsizeiptr, const void*, aero::AeroGLenum) {}
void AERO_GL_APIENTRY MockBufferSubData(aero::AeroGLenum, aero::AeroGLintptr, aero::AeroGLsizeiptr, const void*) {}

void AERO_GL_APIENTRY MockGenVertexArrays(aero::AeroGLsizei n, aero::AeroGLuint* out) {
    for (aero::AeroGLsizei i = 0; i < n; ++i) out[i] = gMockGl.nextId++;
}
void AERO_GL_APIENTRY MockDeleteVertexArrays(aero::AeroGLsizei, const aero::AeroGLuint*) {}
void AERO_GL_APIENTRY MockBindVertexArray(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockEnableVertexAttribArray(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockVertexAttribPointer(aero::AeroGLuint, aero::AeroGLint, aero::AeroGLenum, aero::AeroGLboolean, aero::AeroGLsizei, const void*) {}
void AERO_GL_APIENTRY MockVertexAttribIPointer(aero::AeroGLuint, aero::AeroGLint, aero::AeroGLenum, aero::AeroGLsizei, const void*) {}

aero::AeroGLuint AERO_GL_APIENTRY MockCreateShader(aero::AeroGLenum) { return gMockGl.nextId++; }
void AERO_GL_APIENTRY MockDeleteShader(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockShaderSource(aero::AeroGLuint, aero::AeroGLsizei, const aero::AeroGLchar* const*, const aero::AeroGLint*) {}
void AERO_GL_APIENTRY MockCompileShader(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockGetShaderiv(aero::AeroGLuint, aero::AeroGLenum pname, aero::AeroGLint* params) {
    if (params) *params = (pname == 0x8B81u) ? 1 : 0; // GL_COMPILE_STATUS
}
void AERO_GL_APIENTRY MockGetShaderInfoLog(aero::AeroGLuint, aero::AeroGLsizei, aero::AeroGLsizei* len, aero::AeroGLchar*) {
    if (len) *len = 0;
}

aero::AeroGLuint AERO_GL_APIENTRY MockCreateProgram() { return gMockGl.nextId++; }
void AERO_GL_APIENTRY MockDeleteProgram(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockAttachShader(aero::AeroGLuint, aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockLinkProgram(aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockGetProgramiv(aero::AeroGLuint, aero::AeroGLenum pname, aero::AeroGLint* params) {
    if (params) *params = (pname == 0x8B82u) ? 1 : 0; // GL_LINK_STATUS
}
void AERO_GL_APIENTRY MockGetProgramInfoLog(aero::AeroGLuint, aero::AeroGLsizei, aero::AeroGLsizei* len, aero::AeroGLchar*) {
    if (len) *len = 0;
}
void AERO_GL_APIENTRY MockUseProgram(aero::AeroGLuint prog) {
    gMockGl.boundProgram = prog;
    gMockGl.programBindHistory.push_back(prog);
}
aero::AeroGLint AERO_GL_APIENTRY MockGetUniformLocation(aero::AeroGLuint, const aero::AeroGLchar* name) {
    if (!name) {
        return -1;
    }
    return (std::string_view(name) == "u_ViewportSize") ? 2 : 1;
}
void AERO_GL_APIENTRY MockUniform1i(aero::AeroGLint, aero::AeroGLint) {}
void AERO_GL_APIENTRY MockUniform2f(aero::AeroGLint loc, aero::AeroGLfloat x, aero::AeroGLfloat y) {
    if (loc == 2) {
        gMockGl.lastUniformViewportW = x;
        gMockGl.lastUniformViewportH = y;
    }
}

void AERO_GL_APIENTRY MockGenTextures(aero::AeroGLsizei n, aero::AeroGLuint* out) {
    for (aero::AeroGLsizei i = 0; i < n; ++i) out[i] = gMockGl.nextId++;
}
void AERO_GL_APIENTRY MockDeleteTextures(aero::AeroGLsizei, const aero::AeroGLuint*) {}
void AERO_GL_APIENTRY MockActiveTexture(aero::AeroGLenum) {}
void AERO_GL_APIENTRY MockBindTexture(aero::AeroGLenum, aero::AeroGLuint) {}
void AERO_GL_APIENTRY MockTexImage2D(aero::AeroGLenum, aero::AeroGLint, aero::AeroGLint, aero::AeroGLsizei, aero::AeroGLsizei, aero::AeroGLint, aero::AeroGLenum, aero::AeroGLenum, const void*) {
    ++gMockGl.texImage2dCalls;
}
void AERO_GL_APIENTRY MockTexSubImage2D(aero::AeroGLenum, aero::AeroGLint, aero::AeroGLint, aero::AeroGLint, aero::AeroGLsizei, aero::AeroGLsizei, aero::AeroGLenum, aero::AeroGLenum, const void*) {
    ++gMockGl.texSubImage2dCalls;
}
void AERO_GL_APIENTRY MockTexParameteri(aero::AeroGLenum, aero::AeroGLenum, aero::AeroGLint) {}
void AERO_GL_APIENTRY MockPixelStorei(aero::AeroGLenum, aero::AeroGLint) {}
void AERO_GL_APIENTRY MockDrawElements(aero::AeroGLenum, aero::AeroGLsizei, aero::AeroGLenum, const void*) {
    ++gMockGl.drawCalls;
}
void AERO_GL_APIENTRY MockReadPixels(aero::AeroGLint, aero::AeroGLint, aero::AeroGLsizei w, aero::AeroGLsizei h, aero::AeroGLenum, aero::AeroGLenum, void* data) {
    if (data && w > 0 && h > 0) {
        std::memset(data, 128, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    }
}
aero::AeroGLenum AERO_GL_APIENTRY MockGetError() { return 0u; }
const std::uint8_t* AERO_GL_APIENTRY MockGetString(aero::AeroGLenum) {
    return reinterpret_cast<const std::uint8_t*>("MockGL 3.3 Core");
}

void AERO_GL_APIENTRY MockGenFramebuffers(aero::AeroGLsizei n, aero::AeroGLuint* out) {
    ++gMockGl.genFboCalls;
    for (aero::AeroGLsizei i = 0; i < n; ++i) out[i] = gMockGl.nextId++;
}
void AERO_GL_APIENTRY MockDeleteFramebuffers(aero::AeroGLsizei n, const aero::AeroGLuint*) {
    gMockGl.deleteFboCalls += n;
}
void AERO_GL_APIENTRY MockBindFramebuffer(aero::AeroGLenum, aero::AeroGLuint fbo) {
    ++gMockGl.bindFboCalls;
    gMockGl.boundFbo = fbo;
    gMockGl.fboBindHistory.push_back(fbo);
}
void AERO_GL_APIENTRY MockFramebufferTexture2D(aero::AeroGLenum, aero::AeroGLenum, aero::AeroGLenum, aero::AeroGLuint, aero::AeroGLint) {
    ++gMockGl.fbTex2dCalls;
}
aero::AeroGLenum AERO_GL_APIENTRY MockCheckFramebufferStatus(aero::AeroGLenum) {
    ++gMockGl.checkFbStatusCalls;
    return 0x8CD5u; // GL_FRAMEBUFFER_COMPLETE
}

aero::GlFunctionTable MakeMinimalMockGlTable() {
    aero::GlFunctionTable gl{};
    gl.Viewport = &MockViewport;
    gl.Scissor = &MockScissor;
    gl.Enable = &MockEnable;
    gl.Disable = &MockDisable;
    gl.BlendFuncSeparate = &MockBlendFuncSeparate;
    gl.ClearColor = &MockClearColor;
    gl.Clear = &MockClear;
    gl.GenBuffers = &MockGenBuffers;
    gl.DeleteBuffers = &MockDeleteBuffers;
    gl.BindBuffer = &MockBindBuffer;
    gl.BufferData = &MockBufferData;
    gl.BufferSubData = &MockBufferSubData;
    gl.GenVertexArrays = &MockGenVertexArrays;
    gl.DeleteVertexArrays = &MockDeleteVertexArrays;
    gl.BindVertexArray = &MockBindVertexArray;
    gl.EnableVertexAttribArray = &MockEnableVertexAttribArray;
    gl.VertexAttribPointer = &MockVertexAttribPointer;
    gl.VertexAttribIPointer = &MockVertexAttribIPointer;
    gl.CreateShader = &MockCreateShader;
    gl.DeleteShader = &MockDeleteShader;
    gl.ShaderSource = &MockShaderSource;
    gl.CompileShader = &MockCompileShader;
    gl.GetShaderiv = &MockGetShaderiv;
    gl.GetShaderInfoLog = &MockGetShaderInfoLog;
    gl.CreateProgram = &MockCreateProgram;
    gl.DeleteProgram = &MockDeleteProgram;
    gl.AttachShader = &MockAttachShader;
    gl.LinkProgram = &MockLinkProgram;
    gl.GetProgramiv = &MockGetProgramiv;
    gl.GetProgramInfoLog = &MockGetProgramInfoLog;
    gl.UseProgram = &MockUseProgram;
    gl.GetUniformLocation = &MockGetUniformLocation;
    gl.Uniform1i = &MockUniform1i;
    gl.Uniform2f = &MockUniform2f;
    gl.GenTextures = &MockGenTextures;
    gl.DeleteTextures = &MockDeleteTextures;
    gl.ActiveTexture = &MockActiveTexture;
    gl.BindTexture = &MockBindTexture;
    gl.TexImage2D = &MockTexImage2D;
    gl.TexSubImage2D = &MockTexSubImage2D;
    gl.TexParameteri = &MockTexParameteri;
    gl.PixelStorei = &MockPixelStorei;
    gl.DrawElements = &MockDrawElements;
    gl.ReadPixels = &MockReadPixels;
    gl.GetError = &MockGetError;
    gl.GetString = &MockGetString;
    return gl;
}

// ============================================================================
// Test 1: GradientBrushRegistry Pointer Stability & Thread-Safe TryGetBrush
// ============================================================================

bool TestGradientBrushRegistryConcurrencyAndStability() {
    aero::GradientBrushRegistry reg;

    aero::GradientBrushDesc initial{};
    initial.kind = aero::GradientBrushKind::Linear;
    initial.startPoint = aero::Point{0.0f, 0.0f};
    initial.endPoint = aero::Point{1.0f, 1.0f};
    initial.stops = {
        {0.0f, aero::Color{1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, aero::Color{0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const std::uint32_t firstId = reg.RegisterBrush(initial);
    AERO_CHECK(firstId == 1u);

    const aero::GradientBrushDesc* stablePtr = reg.Find(firstId);
    AERO_CHECK(stablePtr != nullptr);

    // Concurrent writers + concurrent TryGetBrush readers
    std::atomic<bool> failed{false};
    std::atomic<std::uint32_t> highestId{firstId};

    std::thread writer1([&]() {
        for (int i = 0; i < 150; ++i) {
            aero::GradientBrushDesc d{};
            d.kind = aero::GradientBrushKind::Linear;
            d.startPoint = aero::Point{0.0f, 0.0f};
            d.endPoint = aero::Point{1.0f, 0.0f};
            d.stops = {
                {0.0f, aero::Color{0.1f * static_cast<float>((i % 9) + 1), 0.25f, 0.5f, 1.0f}},
                {1.0f, aero::Color{0.2f, 0.8f, 0.1f * static_cast<float>((i % 7) + 1), 1.0f}},
            };
            const std::uint32_t id = reg.RegisterBrush(d);
            if (id == 0u) {
                failed.store(true);
            } else {
                std::uint32_t cur = highestId.load(std::memory_order_relaxed);
                while (id > cur && !highestId.compare_exchange_weak(cur, id)) {
                }
            }
        }
    });

    std::thread writer2([&]() {
        for (int i = 0; i < 150; ++i) {
            aero::GradientBrushDesc d{};
            d.kind = aero::GradientBrushKind::Radial;
            d.center = aero::Point{0.5f, 0.5f};
            d.gradientOrigin = aero::Point{0.5f, 0.5f};
            d.radiusX = 0.5f;
            d.radiusY = 0.5f;
            d.stops = {
                {0.0f, aero::Color{1.0f, 1.0f, 0.0f, 1.0f}},
                {1.0f, aero::Color{0.0f, 1.0f, 1.0f, 1.0f}},
            };
            const std::uint32_t id = reg.RegisterBrush(d);
            if (id == 0u) {
                failed.store(true);
            } else {
                std::uint32_t cur = highestId.load(std::memory_order_relaxed);
                while (id > cur && !highestId.compare_exchange_weak(cur, id)) {
                }
            }
        }
    });

    std::thread reader([&]() {
        aero::GradientBrushDesc snap{};
        for (int iter = 0; iter < 1000; ++iter) {
            const std::uint32_t maxId = highestId.load(std::memory_order_acquire);
            const std::uint32_t probe = 1u + static_cast<std::uint32_t>(iter) % maxId;
            if (reg.TryGetBrush(probe, snap)) {
                if (snap.brushId != probe || snap.stops.size() < 2u ||
                    snap.bakedRampRgba8[3] == 0u) {
                    failed.store(true);
                }
            }
        }
    });

    writer1.join();
    writer2.join();
    reader.join();

    AERO_CHECK(!failed.load());
    // deque storage guarantees firstId address never invalidated across 300+ pushes
    AERO_CHECK(reg.Find(firstId) == stablePtr);
    AERO_CHECK(stablePtr->brushId == firstId);

    aero::GradientBrushDesc copyOut{};
    AERO_CHECK(reg.TryGetBrush(firstId, copyOut));
    AERO_CHECK(copyOut.brushId == firstId);
    AERO_CHECK(copyOut.bakedRampRgba8[0] == 255u && copyOut.bakedRampRgba8[2] == 0u);
    AERO_CHECK(!reg.TryGetBrush(0u, copyOut));
    AERO_CHECK(!reg.TryGetBrush(999999u, copyOut));
    return true;
}

// ============================================================================
// Test 2: Shared SVG Path & W3C Elliptical Arc Parser + MtsdfPathBaker + PagedRenderTree
// ============================================================================

bool TestSharedSvgParserAndPathStrokes() {
    // Path combining M, L, H, V, C, Q, A (elliptical arc), relative l/a, and Z
    constexpr std::string_view kArcAndBezierSvg =
        "M 10 50 L 30 50 A 20 20 0 0 1 70 50 h 10 v 20 Q 50 90 10 70 Z";

    const auto subpaths = aero::ParseSvgPathToSubpaths(kArcAndBezierSvg);
    AERO_CHECK(subpaths.size() == 1u);
    AERO_CHECK(subpaths[0].size() >= 10u); // elliptical arc produces >= 4 intermediate points

    // Direct W3C AppendSvgArcPoints check (semi-circle from (0,50) to (100,50) radius 50)
    std::vector<aero::Point> arcPts;
    arcPts.push_back(aero::Point{0.0f, 50.0f});
    aero::AppendSvgArcPoints(
        aero::Point{0.0f, 50.0f},
        50.0f,
        50.0f,
        0.0f,
        false,
        true,
        aero::Point{100.0f, 50.0f},
        arcPts);
    AERO_CHECK(arcPts.size() >= 8u);
    // Midpoint near (50, 0) for upper sweep=1 arc in Y-down coordinates (9 o'clock -> 12 o'clock -> 3 o'clock)
    bool foundTopApex = false;
    for (const auto& pt : arcPts) {
        if (std::fabs(pt.x - 50.0f) < 8.0f && pt.y < 10.0f) {
            foundTopApex = true;
            break;
        }
    }
    AERO_CHECK(foundTopApex);

    // MtsdfPathBaker ParseSvgPathGeometry & BakeSvgPathToMtsdfEntry
    const aero::ParsedSvgGeometry geom = aero::ParseSvgPathGeometry("M 0 0 L 100 0 L 100 100 L 0 100 Z");
    AERO_CHECK(geom.contours.size() == 1u);
    AERO_CHECK(geom.contours[0].closed);
    AERO_CHECK(geom.bounds.width > 99.0f && geom.bounds.height > 99.0f);

    const aero::AxbMtsdfPathEntry entry = aero::BakeSvgPathToMtsdfEntry(
        "M 0 0 L 100 0 L 100 100 L 0 100 Z", 48u, 4.0f, 0u, 1024u);
    AERO_CHECK(entry.cellWidth == 48u && entry.cellHeight == 48u);
    AERO_CHECK(entry.mtsdfRgbaPixels.size() == 48u * 48u * 4u);
    // Center texel (24,24) should be deep inside (> 128); corner texel (0,0) outside (< 128)
    const std::size_t centerOff = (24u * 48u + 24u) * 4u;
    const std::size_t cornerOff = 0u;
    AERO_CHECK(entry.mtsdfRgbaPixels[centerOff + 3u] > 180u);
    AERO_CHECK(entry.mtsdfRgbaPixels[cornerOff + 3u] < 128u);

    // PagedRenderTree::AllocateSvgPathStrokeNodes uses shared ParseSvgPathToSubpaths & renders strokes
    aero::PagedRenderTree tree;
    const std::uint16_t emittedFull = tree.AllocateSvgPathStrokeNodes(
        kArcAndBezierSvg,
        aero::Rect{10.0f, 10.0f, 140.0f, 140.0f},
        aero::Stretch::Uniform,
        4.0f,
        aero::Color{0.2f, 0.9f, 0.4f, 1.0f});
    AERO_CHECK(emittedFull >= 8u);

    aero::PagedRenderTree trimmedTree;
    const std::uint16_t emittedTrim = trimmedTree.AllocateSvgPathStrokeNodes(
        "M 0 0 L 100 0 L 100 100",
        aero::Rect{0.0f, 0.0f, 100.0f, 100.0f},
        aero::Stretch::Fill,
        4.0f,
        aero::Color{1.0f, 0.5f, 0.0f, 1.0f},
        0u,
        1.0f,
        nullptr,
        0.0f,
        0.4f);
    AERO_CHECK(emittedTrim == 1u); // Only first horizontal segment inside 0..40% of total 200px length

    std::vector<std::uint8_t> fb;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
        tree, 160u, 160u, fb, aero::Color{0.0f, 0.0f, 0.0f, 1.0f});
    bool anyGreen = false;
    for (std::size_t i = 0; i + 3u < fb.size(); i += 4u) {
        if (fb[i + 1u] > 150u && fb[i + 0u] < 100u) {
            anyGreen = true;
            break;
        }
    }
    AERO_CHECK(anyGreen);
    return true;
}

// ============================================================================
// Test 3: SkylinePacker + FontAtlas + ImageAtlas Reuse
// ============================================================================

bool TestSkylinePackerAndAtlases() {
    struct PlacedBox {
        std::uint32_t x, y, w, h;
    };
    aero::SkylinePacker packer(192u, 192u, 4u, 4u);
    std::vector<PlacedBox> placed;
    for (int i = 0; i < 40; ++i) {
        const std::uint32_t w = 12u + static_cast<std::uint32_t>((i * 7) % 19);
        const std::uint32_t h = 10u + static_cast<std::uint32_t>((i * 5) % 17);
        std::uint32_t ox = 0u, oy = 0u;
        AERO_CHECK(packer.TryAllocate(w, h, ox, oy));
        AERO_CHECK(ox >= 4u && oy >= 4u);
        AERO_CHECK(ox + w <= 196u && oy + h <= 196u);
        for (const PlacedBox& b : placed) {
            const bool overlapX = (ox < b.x + b.w) && (ox + w > b.x);
            const bool overlapY = (oy < b.y + b.h) && (oy + h > b.y);
            AERO_CHECK(!(overlapX && overlapY));
        }
        placed.push_back(PlacedBox{ox, oy, w, h});
    }

    std::uint32_t badX = 0u, badY = 0u;
    AERO_CHECK(!packer.TryAllocate(250u, 10u, badX, badY));
    packer.Clear();
    AERO_CHECK(packer.TryAllocate(192u, 192u, badX, badY));
    AERO_CHECK(badX == 4u && badY == 4u);

    // ImageAtlas skyline reuse verification
    aero::ImageAtlas imgAtlas;
    for (int i = 0; i < 12; ++i) {
        aero::DecodedImage img{};
        img.width = 24u + static_cast<std::uint32_t>(i * 2);
        img.height = 24u + static_cast<std::uint32_t>(i);
        img.format = aero::ImageFormat::RawRgba8;
        img.rgbaPixels.assign(static_cast<std::size_t>(img.width) * img.height * 4u, 220u);
        const aero::ImageAtlasEntry* e =
            imgAtlas.RegisterDecodedImage("mem://sprite_" + std::to_string(i), std::move(img), false);
        AERO_CHECK(e != nullptr);
        AERO_CHECK(e->textureId == aero::ImageAtlas::kSharedUiAtlasTextureId);
        AERO_CHECK(e->u1 > e->u0 && e->v1 > e->v0);
    }

    // FontAtlas skyline reuse verification
    aero::FontAtlas fontAtlas(512u);
    const aero::FontAtlasGlyph gA = fontAtlas.EnsureGlyph("", 'A');
    const aero::FontAtlasGlyph gBox = fontAtlas.EnsureGlyph("", 0x4E2Du);
    AERO_CHECK(gA.valid);
    AERO_CHECK(gBox.valid);
    AERO_CHECK(gA.u1 > gA.u0 && gA.v0 > 0.0f && gA.v1 <= 0.5f);
    return true;
}

// ============================================================================
// Test 4: PagedRenderTree + Per-View UIResourceStore + Unified Shaders + SoftwareRasterizer
// ============================================================================

bool TestShadersStoreIsolationAndSoftwareRasterizer() {
    // Check unified GLSL shader generation
    const std::string_view gl33Vs = aero::ZeroCopyGpuStream::UberVertexShaderSource(aero::ShaderBackend::DesktopGL33);
    const std::string_view gl33Ps = aero::ZeroCopyGpuStream::UberPixelShaderSource(aero::ShaderBackend::DesktopGL33);
    const std::string_view es300Vs = aero::ZeroCopyGpuStream::UberVertexShaderSource(aero::ShaderBackend::WebGL2);
    const std::string_view es300Ps = aero::ZeroCopyGpuStream::UberPixelShaderSource(aero::ShaderBackend::WebGL2);
    const std::string_view wgsl = aero::ZeroCopyGpuStream::UberVertexShaderSource(aero::ShaderBackend::WebGPU_WGSL);

    AERO_CHECK(gl33Vs.find("#version 330 core") != std::string_view::npos);
    AERO_CHECK(es300Vs.find("#version 300 es") != std::string_view::npos);
    AERO_CHECK(gl33Vs.find("a_ClipW") != std::string_view::npos);
    AERO_CHECK(es300Vs.find("a_ClipW") != std::string_view::npos);
    AERO_CHECK(gl33Ps.find("#define AERO_WEBGL2_GRAYSCALE_SDF_AA 0") != std::string_view::npos);
    AERO_CHECK(es300Ps.find("#define AERO_WEBGL2_GRAYSCALE_SDF_AA 1") != std::string_view::npos);
    AERO_CHECK(gl33Ps.find("sampleAtlasGradientBrush") != std::string_view::npos);
    AERO_CHECK(wgsl.find("fs_main") != std::string_view::npos);
    AERO_CHECK(wgsl.find("clipW") != std::string_view::npos);

    // Per-view UIResourceStore gradient isolation (never registered on global Instance!)
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::GradientBrushDesc grad{};
    grad.kind = aero::GradientBrushKind::Linear;
    grad.startPoint = aero::Point{0.0f, 0.5f};
    grad.endPoint = aero::Point{1.0f, 0.5f};
    grad.stops = {
        {0.0f, aero::Color{1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, aero::Color{0.0f, 1.0f, 0.0f, 1.0f}},
    };
    const std::uint32_t localBrushId = store->GradientBrushes().RegisterBrush(grad);
    AERO_CHECK(localBrushId == 1u);

    aero::PagedRenderTree tree;
    tree.SetResourceStore(store.get());
    (void)tree.AllocateGradientRoundedRectNode(
        0.0f,
        0.0f,
        120.0f,
        60.0f,
        localBrushId,
        aero::CornerRadius{4.0f});
    (void)tree.AllocateTextRunNode(
        10.0f,
        75.0f,
        18.0f,
        "Hello AeroGUI",
        aero::Color{1.0f, 1.0f, 1.0f, 1.0f});

    aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 16u);
    const aero::ZeroCopySubmissionStats stats = stream.SubmitDirtyPages(tree);
    AERO_CHECK(stats.dirtyPagesUploaded >= 1u);
    AERO_CHECK(stats.drawCallCount >= 1u);

    std::vector<std::uint8_t> rgba;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
        tree, 128u, 128u, rgba, aero::Color{0.0f, 0.0f, 0.0f, 1.0f});
    // Left side of gradient rect should be red-dominant, right side green-dominant
    const std::size_t leftOff = (30u * 128u + 8u) * 4u;
    const std::size_t rightOff = (30u * 128u + 112u) * 4u;
    AERO_CHECK(rgba[leftOff + 0u] > 180u && rgba[leftOff + 1u] < 80u);
    AERO_CHECK(rgba[rightOff + 1u] > 180u && rgba[rightOff + 0u] < 80u);
    return true;
}

// ============================================================================
// Test 5: OpenGlDeviceBackend Mock GlFunctionTable (Software Fallback & FBO Path)
// ============================================================================

bool TestMockGlBackendFboAndFallback() {
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 200.0f, 200.0f);

    const aero::VisualNodeId blurBox = world.CreateNode(aero::VisualKind::Box);
    world.AddChild(root, blurBox);
    world.SetSize(blurBox, 80.0f, 60.0f);
    world.SetBoxStyle(
        blurBox,
        aero::ParseColorRgba32("#FFFF4444"),
        0u,
        0.0f, 0.0f, 0.0f, 0.0f,
        8.0f, 8.0f, 8.0f, 8.0f);
    world.SetEffect(blurBox, aero::UIEffectKind::Blur, 12.0f, 0.0f, 0.0f, 0u, 1.0f);
    world.UpdateLayout(200.0f, 200.0f);

    aero::DrawingCommandBuffer drawBuf;
    aero::VisualRenderAdapter adapter;
    aero::PagedRenderTree tree;
    tree.SetResourceStore(store.get());
    const std::size_t synced = adapter.SyncPagedRenderTree(world, &drawBuf, tree, store.get());
    AERO_CHECK(synced >= 1u);
    if (tree.BlurLayerSpans().empty()) {
        aero::BlurLayerSpan span{};
        span.firstNodeId = 0u;
        span.endNodeId = static_cast<std::uint32_t>(tree.NodeCount());
        span.effectMode = aero::UIEffectKind::Blur;
        span.radiusPx = 12.0f;
        span.effectOpacity = 1.0f;
        tree.PushBlurLayerSpan(span);
    }
    AERO_CHECK(!tree.BlurLayerSpans().empty());

    aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 16u);
    (void)stream.SubmitDirtyPages(tree);

    // Case A: Minimal GlFunctionTable WITHOUT optional FBO entry points -> SoftwareRasterizer patch fallback
    {
        gMockGl.Reset();
        const aero::GlFunctionTable minimalGl = MakeMinimalMockGlTable();
        AERO_CHECK(minimalGl.IsComplete());
        AERO_CHECK(!minimalGl.HasFramebufferSupport());

        aero::OpenGlDeviceBackend backend(minimalGl);
        aero::GpuBackendConfig cfg{};
        cfg.initialViewportWidth = 200u;
        cfg.initialViewportHeight = 200u;
        AERO_CHECK(backend.Initialize(cfg));

        const aero::GpuBackendFrameStats frame = backend.ExecuteSubmission(
            tree,
            stream.LastBufferSubDataSpans(),
            stream.LastClipDrawBatches(),
            16u,
            200u,
            200u);
        AERO_CHECK(frame.succeeded);
        AERO_CHECK(gMockGl.bindFboCalls == 0);
        AERO_CHECK(gMockGl.genFboCalls == 0);
        AERO_CHECK(gMockGl.drawCalls >= 1);
        backend.Shutdown();
        AERO_CHECK(gMockGl.deleteFboCalls == 0);
    }

    // Case B: Full Mock GlFunctionTable WITH optional FBO entry points -> GPU FBO Blur Path + State Restore
    {
        gMockGl.Reset();
        aero::GlFunctionTable fboGl = MakeMinimalMockGlTable();
        fboGl.GenFramebuffers = &MockGenFramebuffers;
        fboGl.DeleteFramebuffers = &MockDeleteFramebuffers;
        fboGl.BindFramebuffer = &MockBindFramebuffer;
        fboGl.FramebufferTexture2D = &MockFramebufferTexture2D;
        fboGl.CheckFramebufferStatus = &MockCheckFramebufferStatus;
        AERO_CHECK(fboGl.IsComplete());
        AERO_CHECK(fboGl.HasFramebufferSupport());

        aero::OpenGlDeviceBackend backend(fboGl);
        aero::GpuBackendConfig cfg{};
        cfg.initialViewportWidth = 200u;
        cfg.initialViewportHeight = 200u;
        AERO_CHECK(backend.Initialize(cfg));

        const aero::GpuBackendFrameStats frame = backend.ExecuteSubmission(
            tree,
            stream.LastBufferSubDataSpans(),
            stream.LastClipDrawBatches(),
            16u,
            200u,
            200u);
        AERO_CHECK(frame.succeeded);
        AERO_CHECK(gMockGl.genFboCalls >= 1);
        AERO_CHECK(gMockGl.fbTex2dCalls >= 2);
        AERO_CHECK(gMockGl.checkFbStatusCalls >= 2);
        AERO_CHECK(gMockGl.bindFboCalls >= 4);
        // Must restore default framebuffer (0) and primary UberShader after offscreen FBO passes
        AERO_CHECK(!gMockGl.fboBindHistory.empty() && gMockGl.fboBindHistory.back() == 0u);
        AERO_CHECK(gMockGl.boundFbo == 0u);
        AERO_CHECK(gMockGl.boundProgram == backend.ProgramHandle());
        // Node draw + H blur pass + V blur composite pass
        AERO_CHECK(gMockGl.drawCalls >= 3);

        backend.Shutdown();
        AERO_CHECK(gMockGl.deleteFboCalls >= 2);
    }

    // Smoke-check headless ProbeOpenGlDynamicLibrary does not crash
    (void)aero::ProbeOpenGlDynamicLibrary();
    return true;
}

// ============================================================================
// Test 6: NativeWindowHost Key Mapping, Headless Clipboard & HostDrivers Triad
// ============================================================================

bool TestNativeWindowHostKeysClipboardAndDrivers() {
    // Win32 Virtual-Key completeness: A..Z (0x41..0x5A), 0..9 (0x30..0x39), F1..F12 (0x70..0x7B)
    for (std::uint32_t offset = 0u; offset < 26u; ++offset) {
        const auto expected = static_cast<aero::KeyCode>(
            static_cast<std::uint16_t>(aero::KeyCode::A) + static_cast<std::uint16_t>(offset));
        AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x41u + offset) == expected);
    }
    for (std::uint32_t offset = 0u; offset < 10u; ++offset) {
        const auto expected = static_cast<aero::KeyCode>(
            static_cast<std::uint16_t>(aero::KeyCode::D0) + static_cast<std::uint16_t>(offset));
        AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x30u + offset) == expected);
    }
    for (std::uint32_t offset = 0u; offset < 12u; ++offset) {
        const auto expected = static_cast<aero::KeyCode>(
            static_cast<std::uint16_t>(aero::KeyCode::F1) + static_cast<std::uint16_t>(offset));
        AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x70u + offset) == expected);
    }
    AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x09u) == aero::KeyCode::Tab);
    AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x0Du) == aero::KeyCode::Enter);
    AERO_CHECK(aero::NativeWindowHost::TranslateWin32VirtualKey(0x1Bu) == aero::KeyCode::Escape);

    // DesktopEventHost + NativeWindowHost Headless Clipboard & Hooks Integration
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 320.0f, 240.0f);
    world.SetBoxStyle(root, aero::ParseColorRgba32("#FF1E293B"), 0u, 0, 0, 0, 0, 0, 0, 0, 0);

    aero::DrawingCommandBuffer drawBuf;
    aero::VisualAnimationEngine animEngine;
    aero::VisualInputRouter inputRouter(&world);
    aero::PagedRenderTree tree;

    aero::DesktopEventHost desktopHost(&world, &drawBuf, &animEngine, &inputRouter, &tree, store);
    AERO_CHECK(tree.ResourceStore() == store.get());

    aero::NativeWindowHost windowHost;
    windowHost.BindDesktopHost(&desktopHost, &tree, nullptr, &inputRouter);
    AERO_CHECK(inputRouter.PlatformHooks() != nullptr);
    AERO_CHECK(static_cast<bool>(inputRouter.PlatformHooks()->getClipboardText));
    AERO_CHECK(static_cast<bool>(inputRouter.PlatformHooks()->setClipboardText));

    aero::NativeWindowConfig winCfg{};
    winCfg.width = 320u;
    winCfg.height = 240u;
    winCfg.forceHeadless = true;
    AERO_CHECK(windowHost.Initialize(winCfg));
    AERO_CHECK(windowHost.IsHeadless());

    // Clipboard round-trip through UIHostPlatformHooks -> NativeWindowHost headless buffer
    const std::string clipPayload = "AeroGUI_Clipboard_UTF8_RoundTrip_123";
    inputRouter.PlatformHooks()->setClipboardText(clipPayload);
    AERO_CHECK(windowHost.GetClipboardText() == clipPayload);
    AERO_CHECK(inputRouter.PlatformHooks()->getClipboardText() == clipPayload);

    // Input & frame presentation pipeline
    windowHost.OnPlatformKey(aero::KeyCode::A, true, aero::KeyMod_None);
    windowHost.OnPlatformTextInput("Z");
    const aero::GpuBackendFrameStats stats = windowHost.TickAndPresentFrame(0.016f, true);
    AERO_CHECK(stats.succeeded);

    std::vector<std::uint8_t> fb;
    std::uint32_t fbW = 0u, fbH = 0u;
    AERO_CHECK(windowHost.CaptureFramebufferRgba8(fb, fbW, fbH));
    AERO_CHECK(fbW == 320u && fbH == 240u);
    AERO_CHECK(fb.size() == 320u * 240u * 4u);

    windowHost.Shutdown();

    // Verify EmbeddedGameHost and WebCanvasHost drive VisualWorld cleanly
    aero::PagedRenderTree embeddedTree;
    aero::ZeroCopyGpuStream embeddedStream(aero::ShaderBackend::DesktopGL33, 16u);
    aero::EmbeddedGameHost gameHost(&world, &drawBuf, &animEngine, &inputRouter, &embeddedTree, store);
    gameHost.SetViewportSize(aero::Size{320.0f, 240.0f});
    world.MarkDirty(root, aero::VisualDirty_Visual);
    gameHost.TickUi(0.016f);
    const aero::ZeroCopySubmissionStats gameSub = gameHost.RecordGpuCommands(embeddedTree, embeddedStream);
    AERO_CHECK(gameSub.dirtyPagesUploaded >= 1u);

    aero::PagedRenderTree webTree;
    aero::ZeroCopyGpuStream webStream(aero::ShaderBackend::WebGL2, 16u);
    aero::WebCanvasHost webHost(&world, &drawBuf, &animEngine, &inputRouter, aero::ShaderBackend::WebGL2, &webTree, store);
    webHost.SetViewportSize(aero::Size{320.0f, 240.0f});
    world.MarkDirty(root, static_cast<aero::VisualDirtyFlags>(aero::VisualDirty_Layout | aero::VisualDirty_Visual));
    webHost.TickUi(0.016f);
    const aero::ZeroCopySubmissionStats webSub = webHost.RecordGpuCommands(webTree, webStream);
    AERO_CHECK(webSub.grayscaleSdfTextActive);
    AERO_CHECK(!webHost.CanSleepIdle());
    webHost.TickUi(0.016f);
    AERO_CHECK(webHost.CanSleepIdle());
    return true;
}

bool TestHighDpiMockGlViewportAndScissor() {
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 1280.0f, 800.0f);

    const aero::VisualNodeId clipBox = world.CreateNode(aero::VisualKind::Box);
    world.AddChild(root, clipBox);
    world.SetAlignment(clipBox, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    world.SetMargin(clipBox, 100.0f, 50.0f, 0.0f, 0.0f);
    world.SetSize(clipBox, 300.0f, 200.0f);
    world.SetClipToBounds(clipBox, true);
    world.SetBoxStyle(
        clipBox,
        aero::ParseColorRgba32("#FF3366CC"),
        0u,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);
    world.UpdateLayout(1280.0f, 800.0f);

    aero::DrawingCommandBuffer drawBuf;
    aero::VisualRenderAdapter adapter;
    aero::PagedRenderTree tree;
    tree.SetResourceStore(store.get());
    AERO_CHECK(adapter.SyncPagedRenderTree(world, &drawBuf, tree, store.get()) >= 1u);
    tree.SetRootDpiScale(2.0f);

    aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 16u);
    (void)stream.SubmitDirtyPages(tree);

    gMockGl.Reset();
    const aero::GlFunctionTable gl = MakeMinimalMockGlTable();
    aero::OpenGlDeviceBackend backend(gl);
    aero::GpuBackendConfig cfg{};
    cfg.initialViewportWidth = 2560u;
    cfg.initialViewportHeight = 1600u;
    AERO_CHECK(backend.Initialize(cfg));

    const aero::GpuBackendFrameStats frame = backend.PresentStream(
        tree,
        stream,
        2560u,
        1600u,
        aero::Color{0.0f, 0.0f, 0.0f, 1.0f});
    AERO_CHECK(frame.succeeded);
    AERO_CHECK(gMockGl.lastGlViewportW == 2560 && gMockGl.lastGlViewportH == 1600);
    AERO_CHECK(std::fabs(gMockGl.lastUniformViewportW - 1280.0f) < 1e-3f);
    AERO_CHECK(std::fabs(gMockGl.lastUniformViewportH - 800.0f) < 1e-3f);
    AERO_CHECK(!gMockGl.scissorHistory.empty());
    const auto sc = gMockGl.scissorHistory.front();
    // Logical (x=100, y=50, w=300, h=200) @ 2.0x -> physical (x=200, bottomY=1600-(50+200)*2=1100, w=600, h=400)
    AERO_CHECK(sc.x == 200 && sc.y == 1100 && sc.w == 600 && sc.h == 400);
    backend.Shutdown();
    return true;
}

bool TestWindowExactClientSizeNoDpiScaling() {
    aero::NativeWindowConfig defCfg{};
    AERO_CHECK(!defCfg.enableHighDpi);

    defCfg.width = 1280u;
    defCfg.height = 800u;
    defCfg.forceHeadless = true;

    aero::NativeWindowHost host1;
    AERO_CHECK(host1.Initialize(defCfg));
    AERO_CHECK(host1.Width() == 1280u);
    AERO_CHECK(host1.Height() == 800u);
    AERO_CHECK(host1.LogicalWidth() == 1280u);
    AERO_CHECK(host1.LogicalHeight() == 800u);
    AERO_CHECK(std::fabs(host1.DpiScale() - 1.0f) < 1e-4f);
    host1.Shutdown();

    aero::NativeWindowConfig explicitScaleCfg = defCfg;
    explicitScaleCfg.dpiScale = 2.0f;
    aero::NativeWindowHost host2;
    AERO_CHECK(host2.Initialize(explicitScaleCfg));
    AERO_CHECK(host2.Width() == 1280u);
    AERO_CHECK(host2.Height() == 800u);
    AERO_CHECK(host2.LogicalWidth() == 640u);
    AERO_CHECK(host2.LogicalHeight() == 400u);
    AERO_CHECK(std::fabs(host2.DpiScale() - 2.0f) < 1e-4f);
    host2.Shutdown();
    return true;
}

bool TestRiveRendererAndBackdropBlur() {
    AERO_CHECK(aero::IsRiveAssetUri("background.riv"));
    AERO_CHECK(aero::IsRiveAssetUri("rive://start.riv?Hover=0"));
    AERO_CHECK(!aero::IsRiveAssetUri("texture.png"));
#ifdef AERO_SAMPLES_DIR
    aero::ImageAtlas riveAtlas;
    aero::DecodedImage riveImg{};
    const std::string riveSampleDir = std::string(AERO_SAMPLES_DIR) + "/Rive/Data";
    const bool okRive = aero::RenderRiveUriToImage("rive://background.riv?States=0", riveAtlas, riveSampleDir, riveImg);
    AERO_CHECK(okRive);
    AERO_CHECK(riveImg.width >= 640u && riveImg.height >= 360u);
    const std::size_t midTop = (10u * riveImg.width + riveImg.width / 2u) * 4u;
    AERO_CHECK(riveImg.rgbaPixels[midTop + 3u] > 200u);
#endif
    return true;
}

bool TestPlasmaSlotReuseLoopAndLru() {
    aero::ImageAtlas atlas;
    const std::string baseParams = "sx=64&sy=64&seed=10&color=%23FF01BCFF&vw=64&vh=64";
    const aero::ImageAtlasEntry* e0 = atlas.LoadOrGet("shader://plasma?" + baseParams + "&time=0.0");
    AERO_CHECK(e0 != nullptr && e0->textureId > 0u);
    const std::uint32_t slotId = e0->textureId;

    std::uint32_t w0 = 0u, h0 = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> snap0;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(slotId, w0, h0, snap0) && snap0 != nullptr);
    const std::vector<std::uint8_t> frame0Copy = *snap0;

    // Frame 1: time changes -> updates same slot in-place with different pixels
    const aero::ImageAtlasEntry* e1 = atlas.LoadOrGet("shader://plasma?" + baseParams + "&time=1.0");
    AERO_CHECK(e1 != nullptr && e1->textureId == slotId);
    std::uint32_t w1 = 0u, h1 = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> snap1;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(slotId, w1, h1, snap1) && snap1 != nullptr);
    AERO_CHECK(*snap1 != frame0Copy);

    // Loop back to time=0.0 -> must NOT return stale cached entry from time=1.0 slot!
    const aero::ImageAtlasEntry* eLoop = atlas.LoadOrGet("shader://plasma?" + baseParams + "&time=0.0");
    AERO_CHECK(eLoop != nullptr && eLoop->textureId == slotId);
    std::uint32_t w2 = 0u, h2 = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> snapLoop;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(slotId, w2, h2, snapLoop) && snapLoop != nullptr);
    AERO_CHECK(*snapLoop == frame0Copy);

    // Distinct ScaleX -> distinct slot ID (must not collide)
    const aero::ImageAtlasEntry* eScale = atlas.LoadOrGet(
        "shader://plasma?sx=128&sy=64&seed=10&color=%23FF01BCFF&vw=64&vh=64&time=0.0");
    AERO_CHECK(eScale != nullptr && eScale->textureId > 0u && eScale->textureId != slotId);

    // Bounding: loading 80 distinct seeds stays within 64-slot plasma LRU cap
    for (int s = 100; s < 180; ++s) {
        const aero::ImageAtlasEntry* es = atlas.LoadOrGet(
            "shader://plasma?sx=64&sy=64&seed=" + std::to_string(s) + "&color=%23FF01BCFF&vw=32&vh=32&time=0.0");
        AERO_CHECK(es != nullptr && es->textureId > 0u);
    }
    AERO_CHECK(atlas.StandaloneTextureCount() <= 64u);
    return true;
}

bool TestCompositeMaskedPathImageTintAndTile() {
    aero::UIResourceStore store;
    aero::DecodedImage solidImg{};
    solidImg.width = 16u;
    solidImg.height = 16u;
    solidImg.format = aero::ImageFormat::RawRgba8;
    solidImg.rgbaPixels.assign(16u * 16u * 4u, 255u);
    AERO_CHECK(store.Images().RegisterDecodedImage("solid://white16", solidImg, false) != nullptr);

    const aero::Rect r64{0.0f, 0.0f, 64.0f, 64.0f};
    const std::string sqPath = "M 0,0 L 64,0 L 64,64 L 0,64 Z";

    // Tint uses 0xAABBGGRR: 0xFF0000FF = pure Red, 0xFFFF0000 = pure Blue
    aero::RasterizedPathMaskEntry redEntry{};
    AERO_CHECK(aero::CompositeMaskedPathImage(
        store, store.Images(), 0u, sqPath, r64, r64, r64, 64u, 64u, 1u,
        "solid://white16", aero::Stretch::Fill, 0xFF0000FFu, false, redEntry));
    const aero::Color cRed = store.Images().SampleBilinearRgba(redEntry.textureId, 0.5f, 0.5f);
    AERO_CHECK(cRed.r > 0.9f && cRed.g < 0.05f && cRed.b < 0.05f && cRed.a > 0.9f);

    aero::RasterizedPathMaskEntry blueEntry{};
    AERO_CHECK(aero::CompositeMaskedPathImage(
        store, store.Images(), 0u, sqPath, r64, r64, r64, 64u, 64u, 1u,
        "solid://white16", aero::Stretch::Fill, 0xFFFF0000u, false, blueEntry));
    const aero::Color cBlue = store.Images().SampleBilinearRgba(blueEntry.textureId, 0.5f, 0.5f);
    AERO_CHECK(cBlue.b > 0.9f && cBlue.r < 0.05f && cBlue.g < 0.05f && cBlue.a > 0.9f);

    // TileMode wrap vs clip with Stretch::None on 16x16 centered inside 64x64
    aero::RasterizedPathMaskEntry untiledEntry{};
    aero::RasterizedPathMaskEntry tiledEntry{};
    AERO_CHECK(aero::CompositeMaskedPathImage(
        store, store.Images(), 0u, sqPath, r64, r64, r64, 64u, 64u, 1u,
        "solid://white16", aero::Stretch::None, 0xFFFFFFFFu, false, untiledEntry));
    AERO_CHECK(aero::CompositeMaskedPathImage(
        store, store.Images(), 0u, sqPath, r64, r64, r64, 64u, 64u, 1u,
        "solid://white16", aero::Stretch::None, 0xFFFFFFFFu, true, tiledEntry));
    const aero::Color cOutsideUntiled = store.Images().SampleBilinearRgba(untiledEntry.textureId, 0.10f, 0.5f);
    const aero::Color cOutsideTiled = store.Images().SampleBilinearRgba(tiledEntry.textureId, 0.10f, 0.5f);
    AERO_CHECK(cOutsideUntiled.a < 0.05f);
    AERO_CHECK(cOutsideTiled.a > 0.9f);
    return true;
}

bool TestArabicRtlAndBidiFixes() {
    aero::FontAtlas fa;
    const aero::TextLayoutResult rtlLatin = fa.MeasureLayout(
        "Hello", "", 16.0f, 1000.0f, aero::TextWrapping::NoWrap, 0.0f, 400, false, 0.0f, true);
    AERO_CHECK(rtlLatin.baseRtl);

#ifdef AERO_SAMPLES_DIR
    const std::string locDir = std::string(AERO_SAMPLES_DIR) + "/Localization/Data";
    AERO_CHECK(fa.LoadTrueTypeFontFromFile(locDir + "/A_Nefel_Sereke.ttf", "ArabicTest", false));
    AERO_CHECK(fa.LoadTrueTypeFontFromFile(locDir + "/PTSans-Regular.ttf", "PtReg", false));
    AERO_CHECK(fa.LoadTrueTypeFontFromFile(locDir + "/PTSans-Bold.ttf", "PtBold", false));

    // Lam (U+0644) + Alef (U+0627) compaction
    const auto* arabicFace = fa.FindFontFace("ArabicTest");
    if (arabicFace && (arabicFace->HasArabicGsub() || arabicFace->FindGlyphIndex(0xFEFBu) != 0u)) {
        const aero::TextLayoutResult lamAlef = fa.MeasureRichLayout(
            {{"\xD9\x84\xD8\xA7", "ArabicTest", 24.0f, 400, 0, 0, false, 0}},
            "ArabicTest", 24.0f, 400, false, 1000.0f, aero::TextWrapping::NoWrap, 0.0f, 0.0f, true);
        AERO_CHECK(lamAlef.glyphs.size() == 1u);
    }

    // NSM combining mark stays after base glyph in visual RTL order and ignores charSpacing
    // U+0628 (Beh, idx=0) + U+064E (Fatha NSM, idx=1) + U+062A (Teh, idx=2)
    const aero::TextLayoutResult nsmRes = fa.MeasureRichLayout(
        {{"\xD8\xA8\xD9\x8E\xD8\xAA", "ArabicTest", 24.0f, 400, 0, 0, false, 0}},
        "ArabicTest", 24.0f, 400, false, 1000.0f, aero::TextWrapping::NoWrap, 200.0f, 0.0f, true);
    AERO_CHECK(nsmRes.glyphs.size() == 3u);
    if (nsmRes.glyphs.size() == 3u) {
        AERO_CHECK(nsmRes.glyphs[0].sourceCodePointIndex == 2u);
        AERO_CHECK(nsmRes.glyphs[1].sourceCodePointIndex == 0u);
        AERO_CHECK(nsmRes.glyphs[2].sourceCodePointIndex == 1u);
        AERO_CHECK(std::fabs(nsmRes.glyphs[2].penX - (nsmRes.glyphs[1].penX + nsmRes.glyphs[1].advanceX)) < 1e-3f);
    }

    // Per-run bracket mirroring uses run's Bold font glyph rather than defaultChain Regular glyph
    const auto* regFace = fa.FindFontFace("PtReg");
    const auto* boldFace = fa.FindFontFace("PtBold");
    AERO_CHECK(regFace != nullptr && boldFace != nullptr);
    if (regFace && boldFace) {
        const aero::TextLayoutResult mirrorRes = fa.MeasureRichLayout(
            {{"(", "PtBold", 24.0f, 700, 0, 0, false, 0}},
            "PtReg", 24.0f, 400, false, 1000.0f, aero::TextWrapping::NoWrap, 0.0f, 0.0f, true);
        AERO_CHECK(mirrorRes.glyphs.size() == 1u);
        if (mirrorRes.glyphs.size() == 1u) {
            AERO_CHECK(mirrorRes.glyphs[0].codePoint == ')');
            AERO_CHECK(mirrorRes.glyphs[0].glyphIndex == boldFace->FindGlyphIndex(')'));
        }
    }

    // GSUB LookupType 7 (Extension Substitution wrapping SingleSubst)
    std::ifstream ptIn(locDir + "/PTSans-Regular.ttf", std::ios::binary | std::ios::ate);
    AERO_CHECK(ptIn.is_open());
    if (ptIn.is_open()) {
        const auto sz = ptIn.tellg();
        std::vector<std::uint8_t> ttf(static_cast<std::size_t>(sz));
        ptIn.seekg(0, std::ios::beg);
        ptIn.read(reinterpret_cast<char*>(ttf.data()), sz);
        while ((ttf.size() & 3u) != 0u) ttf.push_back(0u);
        if (ttf.size() > 12u) {
            const std::uint16_t numTables = static_cast<std::uint16_t>((ttf[4] << 8u) | ttf[5]);
            std::uint32_t gsubRecOff = 0u;
            for (std::uint16_t ti = 0u; ti < numTables; ++ti) {
                const std::uint32_t rec = 12u + 16u * ti;
                if (rec + 16u <= ttf.size() && std::memcmp(ttf.data() + rec, "GSUB", 4u) == 0) {
                    gsubRecOff = rec;
                    break;
                }
            }
            if (gsubRecOff > 0u && regFace) {
                const auto gA = static_cast<std::uint16_t>(regFace->FindGlyphIndex('A'));
                const auto gB = static_cast<std::uint16_t>(regFace->FindGlyphIndex('B'));
                const auto delta = static_cast<std::uint16_t>(gB - gA);
                const auto newOff = static_cast<std::uint32_t>(ttf.size());
                const std::uint8_t gsubType7[60] = {
                    // Header: v1.0, scriptOff=10, featOff=14, lookupListOff=28
                    0x00,0x01, 0x00,0x00, 0x00,0x0A, 0x00,0x0E, 0x00,0x1C,
                    // ScriptList (4 bytes @ 10): count=0
                    0x00,0x00, 0x00,0x00,
                    // FeatureList @ 14 (14 bytes): count=1, 'init' -> off=8 (@22)
                    0x00,0x01, 'i','n','i','t', 0x00,0x08,
                    0x00,0x00, 0x00,0x01, 0x00,0x00,
                    // LookupList @ 28 (4 bytes): count=1, off=4 (@32)
                    0x00,0x01, 0x00,0x04,
                    // Lookup @ 32 (8 bytes): type=7, flag=0, subCount=1, off=8 (@40)
                    0x00,0x07, 0x00,0x00, 0x00,0x01, 0x00,0x08,
                    // ExtensionSubst1 @ 40 (8 bytes): fmt=1, extType=1, extOff=8 (@48)
                    0x00,0x01, 0x00,0x01, 0x00,0x00,0x00,0x08,
                    // SingleSubst1 @ 48 (6 bytes): fmt=1, covOff=6 (@54), delta
                    0x00,0x01, 0x00,0x06,
                    static_cast<std::uint8_t>((delta >> 8u) & 0xFFu),
                    static_cast<std::uint8_t>(delta & 0xFFu),
                    // Coverage1 @ 54 (6 bytes): fmt=1, count=1, gA
                    0x00,0x01, 0x00,0x01,
                    static_cast<std::uint8_t>((gA >> 8u) & 0xFFu),
                    static_cast<std::uint8_t>(gA & 0xFFu),
                };
                ttf.insert(ttf.end(), gsubType7, gsubType7 + sizeof(gsubType7));
                auto wu32 = [&](std::uint32_t off, std::uint32_t v) {
                    ttf[off + 0u] = static_cast<std::uint8_t>((v >> 24u) & 0xFFu);
                    ttf[off + 1u] = static_cast<std::uint8_t>((v >> 16u) & 0xFFu);
                    ttf[off + 2u] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
                    ttf[off + 3u] = static_cast<std::uint8_t>(v & 0xFFu);
                };
                wu32(gsubRecOff + 8u, newOff);
                wu32(gsubRecOff + 12u, static_cast<std::uint32_t>(sizeof(gsubType7)));

                aero::TrueTypeFontFace customFace;
                AERO_CHECK(customFace.LoadFromMemory(ttf.data(), ttf.size()));
                AERO_CHECK(customFace.HasArabicGsub());
                AERO_CHECK(customFace.ApplyArabicGsubFeature(0x696E6974u /* 'init' */, gA) == gB);
            }
        }
    }
#endif
    return true;
}

bool TestRiveOverflowGuardAndPartialTrim() {
    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path() / "aerogui_rive_regtest";
    std::error_code ec;
    fs::create_directories(tmpDir, ec);

    auto pushVarUint = [](std::vector<std::uint8_t>& out, std::uint64_t v) {
        do {
            std::uint8_t b = static_cast<std::uint8_t>(v & 0x7Fu);
            v >>= 7u;
            if (v != 0u) b |= 0x80u;
            out.push_back(b);
        } while (v != 0u);
    };
    auto pushF32 = [](std::vector<std::uint8_t>& out, float f) {
        std::uint8_t buf[4]{};
        std::memcpy(buf, &f, 4u);
        out.insert(out.end(), buf, buf + 4u);
    };
    auto pushU32 = [](std::vector<std::uint8_t>& out, std::uint32_t u) {
        std::uint8_t buf[4]{};
        std::memcpy(buf, &u, 4u);
        out.insert(out.end(), buf, buf + 4u);
    };

    // 1. Varuint string overflow attack must cleanly reject
    {
        std::vector<std::uint8_t> evil = {'R', 'I', 'V', 'E'};
        pushVarUint(evil, 7u);
        pushVarUint(evil, 0u);
        pushVarUint(evil, 0u);
        pushVarUint(evil, 0u); // TOC end
        pushVarUint(evil, 4u); // object type
        pushVarUint(evil, 4u); // propKey 4 = string
        pushVarUint(evil, 0xFFFFFFFFFFFFFFF0ULL);
        evil.resize(evil.size() + 32u, 0x41u);

        const fs::path badPath = tmpDir / "overflow.riv";
        {
            std::ofstream ofs(badPath, std::ios::binary | std::ios::trunc);
            ofs.write(reinterpret_cast<const char*>(evil.data()), static_cast<std::streamsize>(evil.size()));
        }
        aero::ImageAtlas atlas;
        aero::DecodedImage badOut{};
        AERO_CHECK(!aero::RenderRiveUriToImage(badPath.string(), atlas, "", badOut));
    }

    // 2. Partial TrimPath on Rectangle Stroke (end=0.0 vs 0.5 vs 1.0)
    auto buildTrimmedStrokeRiv = [&](float trimStart, float trimEnd, const fs::path& outPath) {
        std::vector<std::uint8_t> b = {'R', 'I', 'V', 'E'};
        pushVarUint(b, 7u); // major
        pushVarUint(b, 0u); // minor
        pushVarUint(b, 0u); // fileId
        pushVarUint(b, 0u); // empty TOC -> uses CorePropBackingTypes
        // Obj 0: Artboard (1): width=64, height=64
        pushVarUint(b, 1u); pushVarUint(b, 7u); pushF32(b, 64.0f); pushVarUint(b, 8u); pushF32(b, 64.0f); pushVarUint(b, 0u);
        // Obj 1: Shape (3): parent=0, x=32, y=32
        pushVarUint(b, 3u); pushVarUint(b, 5u); pushVarUint(b, 0u); pushVarUint(b, 13u); pushF32(b, 32.0f); pushVarUint(b, 14u); pushF32(b, 32.0f); pushVarUint(b, 0u);
        // Obj 2: Rectangle (7): parent=1, w=40, h=40
        pushVarUint(b, 7u); pushVarUint(b, 5u); pushVarUint(b, 1u); pushVarUint(b, 20u); pushF32(b, 40.0f); pushVarUint(b, 21u); pushF32(b, 40.0f); pushVarUint(b, 0u);
        // Obj 3: Stroke (24): parent=1, thickness=4
        pushVarUint(b, 24u); pushVarUint(b, 5u); pushVarUint(b, 1u); pushVarUint(b, 47u); pushF32(b, 4.0f); pushVarUint(b, 0u);
        // Obj 4: SolidColor (18): parent=3, color=0xFFFFFFFF
        pushVarUint(b, 18u); pushVarUint(b, 5u); pushVarUint(b, 3u); pushVarUint(b, 37u); pushU32(b, 0xFFFFFFFFu); pushVarUint(b, 0u);
        // Obj 5: TrimPath (47): parent=3, start, end
        pushVarUint(b, 47u); pushVarUint(b, 5u); pushVarUint(b, 3u); pushVarUint(b, 114u); pushF32(b, trimStart); pushVarUint(b, 115u); pushF32(b, trimEnd); pushVarUint(b, 0u);

        std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
        ofs.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    };
    auto countOpaquePixels = [](const aero::DecodedImage& img) -> std::size_t {
        std::size_t cnt = 0u;
        for (std::size_t i = 3u; i < img.rgbaPixels.size(); i += 4u) {
            if (img.rgbaPixels[i] > 128u) ++cnt;
        }
        return cnt;
    };

    const fs::path fullPath = tmpDir / "trim_full.riv";
    const fs::path halfPath = tmpDir / "trim_half.riv";
    buildTrimmedStrokeRiv(0.0f, 1.0f, fullPath);
    buildTrimmedStrokeRiv(0.0f, 0.5f, halfPath);

    aero::ImageAtlas atlas;
    aero::DecodedImage fullImg{}, halfImg{};
    AERO_CHECK(aero::RenderRiveUriToImage(fullPath.string(), atlas, "", fullImg));
    AERO_CHECK(aero::RenderRiveUriToImage(halfPath.string(), atlas, "", halfImg));
    const std::size_t fullPx = countOpaquePixels(fullImg);
    const std::size_t halfPx = countOpaquePixels(halfImg);
    AERO_CHECK(fullPx > 100u && halfPx > 20u && halfPx < fullPx * 3u / 4u);

    fs::remove_all(tmpDir, ec);
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        bool (*fn)();
    };
    const TestCase cases[] = {
        {"GradientBrushRegistryConcurrencyAndStability", &TestGradientBrushRegistryConcurrencyAndStability},
        {"SharedSvgParserAndPathStrokes", &TestSharedSvgParserAndPathStrokes},
        {"SkylinePackerAndAtlases", &TestSkylinePackerAndAtlases},
        {"ShadersStoreIsolationAndSoftwareRasterizer", &TestShadersStoreIsolationAndSoftwareRasterizer},
        {"MockGlBackendFboAndFallback", &TestMockGlBackendFboAndFallback},
        {"NativeWindowHostKeysClipboardAndDrivers", &TestNativeWindowHostKeysClipboardAndDrivers},
        {"HighDpiMockGlViewportAndScissor", &TestHighDpiMockGlViewportAndScissor},
        {"WindowExactClientSizeNoDpiScaling", &TestWindowExactClientSizeNoDpiScaling},
        {"RiveRendererAndBackdropBlur", &TestRiveRendererAndBackdropBlur},
        {"PlasmaSlotReuseLoopAndLru", &TestPlasmaSlotReuseLoopAndLru},
        {"CompositeMaskedPathImageTintAndTile", &TestCompositeMaskedPathImageTintAndTile},
        {"ArabicRtlAndBidiFixes", &TestArabicRtlAndBidiFixes},
        {"RiveOverflowGuardAndPartialTrim", &TestRiveOverflowGuardAndPartialTrim},
    };

    int passed = 0;
    const int total = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    for (const TestCase& tc : cases) {
        const int before = gFailureCount;
        const bool ok = tc.fn();
        if (ok && gFailureCount == before) {
            ++passed;
            std::printf("[PASS] %s\n", tc.name);
        } else {
            std::printf("[FAIL] %s\n", tc.name);
        }
    }
    std::printf("render_host_tests: %d/%d passed\n", passed, total);
    return (passed == total && gFailureCount == 0) ? 0 : 1;
}
