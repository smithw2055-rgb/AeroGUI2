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
#include "aero/render/UIResourceStore.hpp"
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

#include "common/AeroTestHarness.hpp"

namespace {

int gFailureCount = 0;

#define AERO_CHECK(cond)                                                            \
    do {                                                                            \
        if (!(cond)) {                                                              \
            aero::test::ReportFailure(gFailureCount, #cond, __FILE__, __LINE__);    \
            return false;                                                           \
        }                                                                           \
    } while (false)

// ============================================================================
// Mock OpenGL Function Table & Call Spy (no GL driver / window required)
// ============================================================================

struct MockGlSpyState {
    void Reset() {
        nextId = 1u;
        boundFbo = 0u;
        boundProgram = 0u;
        boundArrayBuffer = 0u;
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
        bufferMemory.clear();
    }

    aero::AeroGLuint nextId = 1u;
    aero::AeroGLuint boundFbo = 0u;
    aero::AeroGLuint boundProgram = 0u;
    aero::AeroGLuint boundArrayBuffer = 0u;
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
    std::unordered_map<aero::AeroGLuint, std::vector<std::uint8_t>> bufferMemory;
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
void AERO_GL_APIENTRY MockBindBuffer(aero::AeroGLenum target, aero::AeroGLuint buf) {
    if (target == 0x8892u) {
        gMockGl.boundArrayBuffer = buf;
    }
}
void AERO_GL_APIENTRY MockBufferData(aero::AeroGLenum target, aero::AeroGLsizeiptr size, const void* data, aero::AeroGLenum) {
    if (target == 0x8892u && gMockGl.boundArrayBuffer != 0u && size >= 0) {
        auto& mem = gMockGl.bufferMemory[gMockGl.boundArrayBuffer];
        mem.assign(static_cast<std::size_t>(size), static_cast<std::uint8_t>(0xCDu));
        if (data != nullptr && size > 0) {
            std::memcpy(mem.data(), data, static_cast<std::size_t>(size));
        }
    }
}
void AERO_GL_APIENTRY MockBufferSubData(aero::AeroGLenum target, aero::AeroGLintptr offset, aero::AeroGLsizeiptr size, const void* data) {
    if (target == 0x8892u && gMockGl.boundArrayBuffer != 0u && data != nullptr && offset >= 0 && size > 0) {
        auto it = gMockGl.bufferMemory.find(gMockGl.boundArrayBuffer);
        if (it != gMockGl.bufferMemory.end()) {
            const std::size_t off = static_cast<std::size_t>(offset);
            const std::size_t sz = static_cast<std::size_t>(size);
            if (off + sz <= it->second.size()) {
                std::memcpy(it->second.data() + off, data, sz);
            }
        }
    }
}

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

    // Verify PlasmaBrush dark regions are transparent and have no opaque-black background pixels
    const aero::ImageAtlasEntry* eFull = atlas.LoadOrGet(
        "shader://plasma?sx=64&sy=64&seed=100&color=%23FF01BCFF&vw=300&vh=90&time=0.0");
    AERO_CHECK(eFull != nullptr && eFull->textureId > 0u && eFull->textureId != slotId);
    std::uint32_t wf = 0u, hf = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> snapFull;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(eFull->textureId, wf, hf, snapFull) && snapFull != nullptr);
    bool hasTransparentPx = false;
    bool hasBrightGlowPx = false;
    std::size_t opaqueBlackCount = 0u;
    for (std::size_t i = 0u; i + 3u < snapFull->size(); i += 4u) {
        const std::uint8_t r = (*snapFull)[i + 0u];
        const std::uint8_t g = (*snapFull)[i + 1u];
        const std::uint8_t b = (*snapFull)[i + 2u];
        const std::uint8_t a = (*snapFull)[i + 3u];
        if (a == 0u) {
            hasTransparentPx = true;
        }
        if (a >= 200u) {
            hasBrightGlowPx = true;
            if (r < 10u && g < 10u && b < 10u) {
                ++opaqueBlackCount;
            }
        }
    }
    AERO_CHECK(hasTransparentPx);
    AERO_CHECK(hasBrightGlowPx);
    AERO_CHECK(opaqueBlackCount == 0u);

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

bool TestModalLiveResizeAndTextEllipsis() {
    // 1. #18 NativeWindowHost live modal resize/paint frame callback & re-entrancy guard
    struct ModalProbe {
        aero::NativeWindowHost* host = nullptr;
        int liveTicks = 0;
        bool nestedGuarded = false;
    };
    aero::NativeWindowHost winHost;
    aero::NativeWindowConfig nwCfg{};
    nwCfg.width = 320u;
    nwCfg.height = 240u;
    nwCfg.forceHeadless = true;
    AERO_CHECK(winHost.Initialize(nwCfg));
    ModalProbe probe{&winHost, 0, false};
    winHost.SetModalFrameCallback(
        [](float /*dtSeconds*/, void* ud) noexcept {
            auto* p = static_cast<ModalProbe*>(ud);
            ++p->liveTicks;
            const int beforeNested = p->liveTicks;
            p->host->DispatchModalLiveFrame(0.016f);
            if (p->liveTicks == beforeNested) {
                p->nestedGuarded = true;
            }
        },
        &probe);
    winHost.DispatchModalLiveFrame(0.016f);
    winHost.DispatchModalLiveFrame(0.016f);
    AERO_CHECK(probe.liveTicks == 2 && probe.nestedGuarded);
    winHost.Shutdown();

    // 2. #13 Standard CSS/WPF named colors (including BurlyWood #FFDEB887)
    const aero::Color burly = aero::ParseColorLiteral("BurlyWood");
    AERO_CHECK(std::fabs(burly.r - (222.0f / 255.0f)) < 0.01f);
    AERO_CHECK(std::fabs(burly.g - (184.0f / 255.0f)) < 0.01f);
    AERO_CHECK(std::fabs(burly.b - (135.0f / 255.0f)) < 0.01f);
    AERO_CHECK(std::fabs(burly.a - 1.0f) < 0.01f);
    const aero::Color cornflower = aero::ParseColorLiteral("CornflowerBlue");
    const aero::Color crimson = aero::ParseColorLiteral("Crimson");
    AERO_CHECK(cornflower.b > 0.85f && crimson.r > 0.80f);

    // 3. #12 FontAtlas single-line & multi-line height-constrained CharacterEllipsis / WordEllipsis
    aero::FontAtlas fontAtlas(512u);
    const std::string longText =
        "The quick brown fox jumps over the lazy dog again and again across a narrow box";
    const aero::TextLayoutResult untrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 120.0f, aero::TextWrapping::NoWrap,
        0.0f, 400, false, 0.0f, false, aero::TextTrimming::None, 1000000.0f);
    const aero::TextLayoutResult charTrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 120.0f, aero::TextWrapping::NoWrap,
        0.0f, 400, false, 0.0f, false, aero::TextTrimming::CharacterEllipsis, 1000000.0f);
    const aero::TextLayoutResult wordTrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 120.0f, aero::TextWrapping::NoWrap,
        0.0f, 400, false, 0.0f, false, aero::TextTrimming::WordEllipsis, 1000000.0f);
    AERO_CHECK(untrimmed.glyphs.size() > charTrimmed.glyphs.size());
    AERO_CHECK(charTrimmed.glyphs.size() >= 4u && wordTrimmed.glyphs.size() >= 4u);
    AERO_CHECK(charTrimmed.totalWidth <= 122.0f && wordTrimmed.totalWidth <= 122.0f);
    const std::size_t nChar = charTrimmed.glyphs.size();
    AERO_CHECK(charTrimmed.glyphs[nChar - 1].codePoint == '.' &&
               charTrimmed.glyphs[nChar - 2].codePoint == '.' &&
               charTrimmed.glyphs[nChar - 3].codePoint == '.');

    const aero::TextLayoutResult multiUntrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 140.0f, aero::TextWrapping::Wrap,
        0.0f, 400, false, 0.0f, false, aero::TextTrimming::None, 1000000.0f);
    const aero::TextLayoutResult multiTrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 140.0f, aero::TextWrapping::Wrap,
        0.0f, 400, false, 0.0f, false, aero::TextTrimming::WordEllipsis, 42.0f);
    AERO_CHECK(multiUntrimmed.lines.size() >= 3u);
    AERO_CHECK(multiTrimmed.lines.size() <= 2u && !multiTrimmed.glyphs.empty());
    const std::size_t nMulti = multiTrimmed.glyphs.size();
    AERO_CHECK(nMulti >= 3u &&
               multiTrimmed.glyphs[nMulti - 1].codePoint == '.' &&
               multiTrimmed.glyphs[nMulti - 2].codePoint == '.' &&
               multiTrimmed.glyphs[nMulti - 3].codePoint == '.');

    const aero::TextLayoutResult rtlCharTrimmed = fontAtlas.MeasureLayout(
        longText, "", 16.0f, 90.0f, aero::TextWrapping::NoWrap,
        0.0f, 400, false, 0.0f, true, aero::TextTrimming::CharacterEllipsis, 1000000.0f);
    AERO_CHECK(rtlCharTrimmed.glyphs.size() >= 4u);
    AERO_CHECK(rtlCharTrimmed.totalWidth <= 92.0f);
    AERO_CHECK(rtlCharTrimmed.glyphs[0].codePoint == '.' &&
               rtlCharTrimmed.glyphs[1].codePoint == '.' &&
               rtlCharTrimmed.glyphs[2].codePoint == '.');
#ifdef AERO_SAMPLES_DIR
    {
        aero::FontAtlas arAtlas(512u);
        const std::string arPath = std::string(AERO_SAMPLES_DIR) + "/Localization/Data/A_Nefel_Sereke.ttf";
        if (arAtlas.LoadTrueTypeFontFromFile(arPath, "ArabicEllipsis", false)) {
            const std::string arLong =
                "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 \xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85 "
                "\xD9\x88\xD8\xB1\xD8\xAD\xD9\x85\xD8\xA9 \xD8\xA7\xD9\x84\xD9\x84\xD9\x87 "
                "\xD9\x88\xD8\xA8\xD8\xB1\xD9\x83\xD8\xA7\xD8\xAA\xD9\x87";
            const aero::TextLayoutResult arTrimmed = arAtlas.MeasureLayout(
                arLong, "ArabicEllipsis", 20.0f, 90.0f, aero::TextWrapping::NoWrap,
                0.0f, 400, false, 0.0f, true, aero::TextTrimming::CharacterEllipsis, 1000000.0f);
            AERO_CHECK(arTrimmed.glyphs.size() >= 4u);
            AERO_CHECK(arTrimmed.totalWidth <= 92.0f);
            AERO_CHECK(arTrimmed.glyphs[0].codePoint == '.' &&
                       arTrimmed.glyphs[1].codePoint == '.' &&
                       arTrimmed.glyphs[2].codePoint == '.');
        }
    }
#endif

    // 4. #02 UIResourceStore per-group (<=2) & byte-budget eviction for zoom-animated path masks
    aero::UIResourceStore store;
    const std::string svgRect = "M0,0 L200,0 L200,200 L0,200 Z";
    for (std::uint32_t side = 128u; side <= 640u; side += 64u) {
        aero::RasterizedPathMaskEntry entry{};
        AERO_CHECK(store.GetOrRasterizePathMask(
            0x987654321ULL, svgRect, aero::Rect{0.0f, 0.0f, 200.0f, 200.0f}, side, side, 2u, entry));
        AERO_CHECK(entry.textureId != 0u);
    }
    AERO_CHECK(store.PathMaskCount() <= 2u);
    AERO_CHECK(store.PathMaskTotalBytes() <= 16u * 1024u * 1024u);
    AERO_CHECK(store.Images().StandaloneTextureCount() <= 2u);
    return true;
}

bool TestWindowHighDpiLogicalSizing() {
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 1280.0f, 800.0f);
    world.SetInteractiveFlags(root, true, true, false);

    const aero::VisualNodeId target = world.CreateNode(aero::VisualKind::Box);
    world.AddChild(root, target);
    world.SetAlignment(target, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    world.SetMargin(target, 600.0f, 360.0f, 0.0f, 0.0f);
    world.SetSize(target, 80.0f, 80.0f);
    world.SetInteractiveFlags(target, true, true, false);
    world.SetBoxStyle(target, 0xFF336699u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    if (aero::VisualNode* tn = world.GetNode(target)) {
        tn->callbackMask = aero::VisualCallback_PointerMove;
    }
    world.UpdateLayout(1280.0f, 800.0f);

    aero::DesktopEventHost desktopHost;
    aero::PagedRenderTree tree;
    tree.SetResourceStore(store.get());
    aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 8u);
    aero::VisualInputRouter router(&world);

    float dispatchedX = -1.0f;
    float dispatchedY = -1.0f;
    int dispatchCount = 0;
    router.SetScriptEventDispatcher([&](
        aero::VisualNodeId nodeId,
        aero::VisualCallbackFlags kind,
        const aero::NativeInputEvent& ev) {
        if (nodeId == target && kind == aero::VisualCallback_PointerMove) {
            dispatchedX = ev.x;
            dispatchedY = ev.y;
            ++dispatchCount;
            return true;
        }
        return false;
    });

    aero::NativeWindowHost host;
    host.BindDesktopHost(&desktopHost, &tree, &stream, &router);
    aero::NativeWindowConfig cfg{};
    cfg.forceHeadless = true;
    cfg.enableHighDpi = true;
    cfg.dpiScale = 1.5f;
    cfg.width = 1280u;
    cfg.height = 800u;
    AERO_CHECK(host.Initialize(cfg));
    AERO_CHECK(host.LogicalWidth() == 1280u);
    AERO_CHECK(host.LogicalHeight() == 800u);
    AERO_CHECK(host.Width() == 1920u);
    AERO_CHECK(host.Height() == 1200u);
    AERO_CHECK(std::fabs(host.DpiScale() - 1.5f) < 1e-3f);

    host.OnPlatformMouseMove(960.0f, 600.0f, 0u);
    (void)router.FlushQueuedEvents();
    AERO_CHECK(router.HoveredNode() == target);
    AERO_CHECK(dispatchCount >= 1);
    AERO_CHECK(std::fabs(dispatchedX - 640.0f) < 0.25f);
    AERO_CHECK(std::fabs(dispatchedY - 400.0f) < 0.25f);
    return true;
}

bool TestMockGlClipWResetAndIncrementalTailPad() {
    // Subtest A: Switching from 3D perspective node to 2D node on the same ring slot must reset clipW back to 1.0f
    {
        gMockGl.Reset();
        const aero::GlFunctionTable gl = MakeMinimalMockGlTable();
        aero::OpenGlDeviceBackend backend(gl);
        aero::GpuBackendConfig cfg{};
        cfg.initialViewportWidth = 400u;
        cfg.initialViewportHeight = 300u;
        AERO_CHECK(backend.Initialize(cfg));

        auto store = std::make_shared<aero::UIResourceStore>();
        aero::VisualWorld world(nullptr, store.get());
        const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
        world.SetRoot(root);
        world.SetSize(root, 400.0f, 300.0f);
        const aero::VisualNodeId box = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(root, box);
        world.SetSize(box, 120.0f, 80.0f);
        world.SetBoxStyle(box, 0xFF336699u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        world.SetTransform3D(box, 0.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 300.0f);
        world.UpdateLayout(400.0f, 300.0f);

        aero::VisualRenderAdapter adapter;
        aero::PagedRenderTree tree;
        tree.SetResourceStore(store.get());
        AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, tree, store.get()) >= 1u);
        AERO_CHECK(tree.PageCount() == 1u);
        AERO_CHECK((tree.GetPage(0u).header.flags & aero::CommandPageFlag_HasPerspective3D) != 0u);

        aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 8u);
        (void)stream.SubmitDirtyPages(tree);
        const aero::GpuBackendFrameStats f1 = backend.ExecuteSubmission(
            tree, stream.LastBufferSubDataSpans(), stream.LastClipDrawBatches(), 8u, 400u, 300u);
        AERO_CHECK(f1.succeeded);

        const auto clipWIt1 = gMockGl.bufferMemory.find(backend.ClipWRingVboHandle());
        AERO_CHECK(clipWIt1 != gMockGl.bufferMemory.end());
        AERO_CHECK(clipWIt1->second.size() >= 4u * sizeof(float));
        float wVals1[4]{};
        std::memcpy(wVals1, clipWIt1->second.data(), sizeof(wVals1));
        AERO_CHECK(wVals1[0] != 1.0f || wVals1[1] != 1.0f || wVals1[2] != 1.0f || wVals1[3] != 1.0f);

        // Now disable 3D transform -> pure 2D page in slot 0 must reset clipW in VRAM back to 1.0f
        world.SetTransform3D(box, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        world.UpdateLayout(400.0f, 300.0f);
        AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, tree, store.get()) >= 1u);
        AERO_CHECK((tree.GetPage(0u).header.flags & aero::CommandPageFlag_HasPerspective3D) == 0u);

        (void)stream.SubmitDirtyPages(tree);
        const aero::GpuBackendFrameStats f2 = backend.ExecuteSubmission(
            tree, stream.LastBufferSubDataSpans(), stream.LastClipDrawBatches(), 8u, 400u, 300u);
        AERO_CHECK(f2.succeeded);

        const auto clipWIt2 = gMockGl.bufferMemory.find(backend.ClipWRingVboHandle());
        AERO_CHECK(clipWIt2 != gMockGl.bufferMemory.end());
        float wVals2[4]{};
        std::memcpy(wVals2, clipWIt2->second.data(), sizeof(wVals2));
        for (int i = 0; i < 4; ++i) {
            AERO_CHECK(std::fabs(wVals2[i] - 1.0f) < 1e-6f);
        }
        backend.Shutdown();
    }

    // Subtest B: Single-page batch in frame 1 followed by incremental dirty Page 1 coalescing with clean Page 0 in frame 2
    {
        gMockGl.Reset();
        const aero::GlFunctionTable gl = MakeMinimalMockGlTable();
        aero::OpenGlDeviceBackend backend(gl);
        aero::GpuBackendConfig cfg{};
        cfg.initialViewportWidth = 400u;
        cfg.initialViewportHeight = 300u;
        AERO_CHECK(backend.Initialize(cfg));

        auto store = std::make_shared<aero::UIResourceStore>();
        aero::PagedRenderTree tree;
        tree.SetResourceStore(store.get());
        (void)tree.AllocateRenderNode(0.0f, 0.0f, 20.0f, 20.0f, aero::Color{1.0f, 0.0f, 0.0f, 1.0f});
        AERO_CHECK(tree.PageCount() == 1u);

        aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 8u);
        (void)stream.SubmitDirtyPages(tree);
        const aero::GpuBackendFrameStats f1 = backend.ExecuteSubmission(
            tree, stream.LastBufferSubDataSpans(), stream.LastClipDrawBatches(), 8u, 400u, 300u);
        AERO_CHECK(f1.succeeded);

        // Append a full-page HUD digits node -> spills onto Page 1 while Page 0 remains clean!
        (void)tree.AllocateHudDigitsNode(30.0f, 0.0f, 10.0f, 12.0f, 42u, aero::Color{0.0f, 1.0f, 0.0f, 1.0f});
        AERO_CHECK(tree.PageCount() == 2u);
        (void)stream.SubmitDirtyPages(tree);
        AERO_CHECK(stream.LastBufferSubDataSpans().size() == 1u);
        AERO_CHECK(stream.LastBufferSubDataSpans()[0].pageIndex == 1u);
        AERO_CHECK(stream.LastClipDrawBatches().size() == 1u);
        AERO_CHECK(stream.LastClipDrawBatches()[0].pageCount == 2u);

        const aero::GpuBackendFrameStats f2 = backend.ExecuteSubmission(
            tree, stream.LastBufferSubDataSpans(), stream.LastClipDrawBatches(), 8u, 400u, 300u);
        AERO_CHECK(f2.succeeded);

        const auto ringIt = gMockGl.bufferMemory.find(backend.RingVboHandle());
        AERO_CHECK(ringIt != gMockGl.bufferMemory.end());
        const std::size_t activeBytes = 4u * sizeof(aero::AeroVertex);
        const std::size_t pageBytes = aero::kVerticesPerCommandPage * sizeof(aero::AeroVertex);
        AERO_CHECK(ringIt->second.size() >= pageBytes);
        for (std::size_t b = activeBytes; b < pageBytes; ++b) {
            AERO_CHECK(ringIt->second[b] == 0u);
        }
        backend.Shutdown();
    }
    return true;
}

bool TestSharedAtlasMaskQuotaNoLeak() {
    auto store = std::make_shared<aero::UIResourceStore>();
    // Small 64x64 shared atlas -> max 2048 texels mask budget -> only 1 30x30 (+2px border = 32x32 = 1024) or 2 fit
    store->Images().ConfigureAtlas(64u, 64u, 64u);
    const aero::Rect rawBounds{0.0f, 0.0f, 24.0f, 24.0f};
    const std::string pathData = "M 2 2 L 22 2 L 22 22 L 2 22 Z";

    std::size_t packedSharedCount = 0u;
    std::unordered_map<std::uint32_t, bool> uniqueStandaloneIds;
    for (std::uint64_t i = 1u; i <= 10u; ++i) {
        aero::RasterizedPathMaskEntry entry{};
        const std::uint64_t hash = 0x100000000ULL + i;
        AERO_CHECK(store->GetOrRasterizePathMask(hash, pathData, rawBounds, 30u, 30u, 1u, entry));
        if (entry.textureId == aero::ImageAtlas::kSharedUiAtlasTextureId) {
            ++packedSharedCount;
        } else {
            uniqueStandaloneIds[entry.textureId] = true;
        }
    }
    AERO_CHECK(packedSharedCount >= 1u);
    AERO_CHECK(!uniqueStandaloneIds.empty());
    AERO_CHECK(store->Images().StandaloneTextureCount() == uniqueStandaloneIds.size());
    return true;
}

bool TestLeafTextClipEliminationAndOverflowClip() {
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 400.0f, 200.0f);
    world.SetLayoutMode(root, aero::LayoutMode::Stack, 4.0f, 0.0f, 1, 0);

    // Case A: Two adjacent leaf TextBlocks with clipToBounds=true whose text fits strictly inside their boxes
    // must NOT push per-node activeClipRect_ scissor splits, coalescing into a single CommandPage & draw batch.
    const aero::VisualNodeId t1 = world.CreateNode(aero::VisualKind::Text);
    const aero::VisualNodeId t2 = world.CreateNode(aero::VisualKind::Text);
    world.AddChild(root, t1);
    world.AddChild(root, t2);
    world.SetAlignment(t1, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    world.SetMargin(t1, 10.0f, 10.0f, 0.0f, 0.0f);
    world.SetSize(t1, 180.0f, 32.0f);
    world.SetClipToBounds(t1, true);
    world.SetText(t1, "Row 1", "", 14.0f, 0xFFFFFFFFu, aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::CharacterEllipsis);

    world.SetAlignment(t2, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    world.SetMargin(t2, 10.0f, 10.0f, 0.0f, 0.0f);
    world.SetSize(t2, 180.0f, 32.0f);
    world.SetClipToBounds(t2, true);
    world.SetText(t2, "Row 2", "", 14.0f, 0xFFFFFFFFu, aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::CharacterEllipsis);

    world.UpdateLayout(400.0f, 200.0f);

    aero::VisualRenderAdapter adapter;
    aero::PagedRenderTree fitTree;
    fitTree.SetResourceStore(store.get());
    AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, fitTree, store.get()) >= 2u);
    AERO_CHECK(fitTree.PageCount() == 1u);
    AERO_CHECK((fitTree.GetPage(0u).header.flags & aero::CommandPageFlag_ScissorClipActive) == 0u);

    aero::ZeroCopyGpuStream fitStream(aero::ShaderBackend::DesktopGL33, 8u);
    const aero::ZeroCopySubmissionStats fitStats = fitStream.SubmitDirtyPages(fitTree);
    AERO_CHECK(fitStats.drawCallCount == 1u);

    // Case B: Overflowing leaf TextBlock with clipToBounds=true & TextTrimming::None MUST still push active scissor clip.
    world.SetSize(t2, 20.0f, 18.0f);
    world.SetText(t2, "VeryLongOverflowingTextString", "", 14.0f, 0xFFFFFFFFu, aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::None);
    world.UpdateLayout(400.0f, 200.0f);

    aero::PagedRenderTree ovfTree;
    ovfTree.SetResourceStore(store.get());
    AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, ovfTree, store.get()) >= 2u);
    bool foundScissorPage = false;
    for (std::size_t p = 0u; p < ovfTree.PageCount(); ++p) {
        const auto& hdr = ovfTree.GetPage(p).header;
        if ((hdr.flags & aero::CommandPageFlag_ScissorClipActive) != 0u &&
            std::fabs(hdr.clipW - 20.0f) < 0.25f &&
            std::fabs(hdr.clipH - 18.0f) < 0.25f) {
            foundScissorPage = true;
        }
    }
    AERO_CHECK(foundScissorPage);
    return true;
}

bool TestFractionalScissorPixelCenterAndNoOverflowSeam() {
    // Verify ComputeGlScissorBox uses pixel-center top-left coverage: ceil(edge - 0.5).
    {
        const aero::GlScissorBox s1 = aero::ComputeGlScissorBox(aero::Rect{263.25f, 147.25f, 32.0f, 32.0f}, 600u, 600u);
        AERO_CHECK(s1.x == 263 && s1.width == 32 && s1.y == (600 - 179) && s1.height == 32);

        const aero::GlScissorBox s2 = aero::ComputeGlScissorBox(aero::Rect{263.50f, 147.50f, 32.0f, 32.0f}, 600u, 600u);
        AERO_CHECK(s2.x == 263 && s2.width == 32 && s2.y == (600 - 179) && s2.height == 32);

        const aero::GlScissorBox s3 = aero::ComputeGlScissorBox(aero::Rect{263.75f, 147.75f, 32.0f, 32.0f}, 600u, 600u);
        AERO_CHECK(s3.x == 264 && s3.width == 32 && s3.y == (600 - 180) && s3.height == 32);
    }

    // Verify SolidQuad + fractional overflow scissor clipping never leaks an underlying wider rectangle's edge pixel.
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld world(nullptr, store.get());
    const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
    world.SetRoot(root);
    world.SetSize(root, 128.0f, 64.0f);

    const std::uint32_t dodgerRgba = aero::ParseColorRgba32("#FF1E90FF");
    const std::uint32_t limeRgba = aero::ParseColorRgba32("#FFADFF2F");
    auto addClippedPair = [&](float mx, float my) {
        const aero::VisualNodeId cell = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(root, cell);
        world.SetAlignment(cell, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(cell, mx, my, 0.0f, 0.0f);
        world.SetSize(cell, 32.0f, 32.0f);

        const aero::VisualNodeId back = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(cell, back);
        world.SetAlignment(back, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetSize(back, 40.0f, 32.0f);
        world.SetBoxStyle(back, dodgerRgba, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

        const aero::VisualNodeId front = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(cell, front);
        world.SetAlignment(front, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetSize(front, 32.0f, 32.0f);
        world.SetBoxStyle(front, limeRgba, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    };
    addClippedPair(20.25f, 10.25f);
    addClippedPair(64.50f, 10.50f);
    world.UpdateLayout(128.0f, 64.0f);

    aero::DrawingCommandBuffer drawBuf;
    aero::VisualRenderAdapter adapter;
    aero::PagedRenderTree tree;
    tree.SetResourceStore(store.get());
    AERO_CHECK(adapter.SyncPagedRenderTree(world, &drawBuf, tree, store.get()) >= 4u);

    std::vector<std::uint8_t> rgba;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
        tree, 128u, 64u, rgba, aero::Color{0.96f, 0.96f, 0.96f, 1.0f});

    int dodgerCount = 0;
    for (std::size_t i = 0u; i + 3u < rgba.size(); i += 4u) {
        if (rgba[i + 0u] < 60u && rgba[i + 1u] > 110u && rgba[i + 2u] > 220u) {
            ++dodgerCount;
        }
    }
    AERO_CHECK(dodgerCount == 0);

    auto countGreenSpan = [&](int py, int xStart, int xEnd) -> int {
        int cnt = 0;
        for (int px = xStart; px < xEnd; ++px) {
            const std::size_t off = (static_cast<std::size_t>(py) * 128u + static_cast<std::size_t>(px)) * 4u;
            if (rgba[off + 0u] == 0xADu && rgba[off + 1u] == 0xFFu && rgba[off + 2u] == 0x2Fu) {
                ++cnt;
            }
        }
        return cnt;
    };
    AERO_CHECK(countGreenSpan(26, 18, 60) == 32);
    AERO_CHECK(countGreenSpan(26, 62, 110) == 32);
    return true;
}

bool TestStandaloneCpuEvictionAndLazyReload() {
    aero::ImageAtlas atlas;
    atlas.ConfigureAtlas(256u, 255u, 255u);
    // Construct an uncompressed 24-bit BMP larger than configured max packable extent (255)
    constexpr std::uint32_t kW = 300u;
    constexpr std::uint32_t kH = 300u;
    constexpr std::uint32_t rowBytes = kW * 3u; // 900 is multiple of 4
    constexpr std::uint32_t pixelBytes = rowBytes * kH;
    constexpr std::uint32_t fileBytes = 54u + pixelBytes;
    std::vector<std::uint8_t> bmp(fileBytes, 0u);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = static_cast<std::uint8_t>(fileBytes & 0xFFu);
    bmp[3] = static_cast<std::uint8_t>((fileBytes >> 8u) & 0xFFu);
    bmp[4] = static_cast<std::uint8_t>((fileBytes >> 16u) & 0xFFu);
    bmp[5] = static_cast<std::uint8_t>((fileBytes >> 24u) & 0xFFu);
    bmp[10] = 54u;
    bmp[14] = 40u;
    bmp[18] = static_cast<std::uint8_t>(kW & 0xFFu);
    bmp[19] = static_cast<std::uint8_t>((kW >> 8u) & 0xFFu);
    bmp[22] = static_cast<std::uint8_t>(kH & 0xFFu);
    bmp[23] = static_cast<std::uint8_t>((kH >> 8u) & 0xFFu);
    bmp[26] = 1u;
    bmp[28] = 24u;
    for (std::size_t i = 54u; i + 2u < bmp.size(); i += 3u) {
        bmp[i + 0u] = 0x33u; // B
        bmp[i + 1u] = 0x99u; // G
        bmp[i + 2u] = 0xCCu; // R
    }

    AERO_CHECK(atlas.RegisterEncodedMemoryAsset("mem://big_bmp.bmp", bmp.data(), bmp.size()));
    const aero::ImageAtlasEntry* entry = atlas.LoadOrGet("mem://big_bmp.bmp");
    AERO_CHECK(entry != nullptr && entry->isStandalone && entry->textureId > 0u);
    const std::uint32_t texId = entry->textureId;
    const std::size_t bytesBefore = atlas.EstimatedMemoryBytes();
    const std::size_t rgbaSize = static_cast<std::size_t>(kW) * kH * 4u;
    AERO_CHECK(bytesBefore >= rgbaSize);

    std::uint32_t knownRev = static_cast<std::uint32_t>(atlas.GetTextureSlotRevision(texId));
    AERO_CHECK(knownRev != 0u);
    AERO_CHECK(atlas.EvictStandaloneCpuPixelsAfterGpuUpload(texId, knownRev));

    const std::size_t bytesAfterEvict = atlas.EstimatedMemoryBytes();
    AERO_CHECK(bytesAfterEvict + rgbaSize <= bytesBefore + 64u);

    // Matching revision upload check must short-circuit WITHOUT reloading CPU pixels
    std::uint32_t sw = 0u, sh = 0u, dx = 0u, dy = 0u, dw = 0u, dh = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> fullSnap;
    std::vector<std::uint8_t> scratch;
    bool wasUpdated = true;
    AERO_CHECK(atlas.AcquireTextureSlotUploadIfModified(
        texId, knownRev, sw, sh, dx, dy, dw, dh, fullSnap, scratch, wasUpdated));
    AERO_CHECK(!wasUpdated && !fullSnap && sw == kW && sh == kH);
    AERO_CHECK(atlas.EstimatedMemoryBytes() == bytesAfterEvict);

    // Software sampler / snapshot access transparently reloads the evicted pixels
    const aero::Color sampled = atlas.SampleBilinearRgba(texId, 0.5f, 0.5f);
    AERO_CHECK(std::fabs(sampled.r - (0xCC / 255.0f)) < 0.02f);
    AERO_CHECK(std::fabs(sampled.g - (0x99 / 255.0f)) < 0.02f);
    AERO_CHECK(std::fabs(sampled.b - (0x33 / 255.0f)) < 0.02f);
    std::shared_ptr<const std::vector<std::uint8_t>> reloadedSnap;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(texId, sw, sh, reloadedSnap) && reloadedSnap != nullptr);
    AERO_CHECK(reloadedSnap->size() == rgbaSize);
    return true;
}

bool TestNumericPlasmaAndRiveAtlasPaths() {
    aero::ImageAtlas atlas;
    aero::PlasmaRenderParams p1{};
    p1.sx = 64.0f;
    p1.sy = 64.0f;
    p1.seed = 42.0f;
    p1.timeSec = 0.25f;
    p1.vw = 128.0f;
    p1.vh = 64.0f;
    p1.colorRgba = 0xFF00FF88u;

    const aero::ImageAtlasEntry* e1 = atlas.LoadOrGetPlasma(p1);
    AERO_CHECK(e1 != nullptr && e1->textureId > 0u);
    const std::uint32_t texId = e1->textureId;
    const std::uint64_t rev1 = atlas.GetTextureSlotRevision(texId);
    AERO_CHECK(rev1 > 0u);

    std::uint32_t w = 0u, h = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> snap1;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(texId, w, h, snap1) && snap1 != nullptr && !snap1->empty());
    bool anyLit = false;
    for (std::size_t i = 3u; i < snap1->size(); i += 4u) {
        if ((*snap1)[i] > 0u) {
            anyLit = true;
            break;
        }
    }
    AERO_CHECK(anyLit);
    const std::vector<std::uint8_t> frame1Copy = *snap1;

    // Identical params must hit cache without bumping revision
    const aero::ImageAtlasEntry* e1Cached = atlas.LoadOrGetPlasma(p1);
    AERO_CHECK(e1Cached != nullptr && e1Cached->textureId == texId);
    AERO_CHECK(atlas.GetTextureSlotRevision(texId) == rev1);

    // Mutating timeSec reuses the same texture slot and updates pixels
    p1.timeSec = 1.50f;
    const aero::ImageAtlasEntry* e2 = atlas.LoadOrGetPlasma(p1);
    AERO_CHECK(e2 != nullptr && e2->textureId == texId);
    AERO_CHECK(atlas.GetTextureSlotRevision(texId) > rev1);
    std::shared_ptr<const std::vector<std::uint8_t>> snap2;
    AERO_CHECK(atlas.TryGetTextureSlotSnapshot(texId, w, h, snap2) && snap2 != nullptr);
    AERO_CHECK(*snap2 != frame1Copy);

    AERO_CHECK(atlas.FindLoadedRive(77u, "nonexistent.riv") == nullptr);
    return true;
}

bool TestPhase4MultiPageAtlasScissorElisionAndDirtyRects() {
    // 1. Multi-page tiered Skyline Atlas: pack 10 x 256x256 sprites into 512x512 base -> overflows to page >= 2
    {
        aero::ImageAtlas atlas;
        atlas.ConfigureAtlas(512u, 512u, 512u);
        std::unordered_set<std::uint32_t> texIds;
        for (int i = 0; i < 10; ++i) {
            aero::DecodedImage img;
            img.width = 256u;
            img.height = 256u;
            img.format = aero::ImageFormat::RawRgba8;
            img.rgbaPixels.assign(256u * 256u * 4u, static_cast<std::uint8_t>(40 + i * 17));
            const aero::ImageAtlasEntry* entry = atlas.RegisterDecodedImage(
                "mem://tile_" + std::to_string(i) + ".png", std::move(img), false);
            AERO_CHECK(entry != nullptr);
            AERO_CHECK(!entry->isStandalone);
            texIds.insert(entry->textureId);
        }
        AERO_CHECK(atlas.SharedAtlasPageCount() >= 2u);
        AERO_CHECK(texIds.size() >= 2u);
        AERO_CHECK(texIds.count(aero::ImageAtlas::kSharedUiAtlasTextureId) == 1u);
    }

    // 2. PagedRenderTree strict-interior scissor elision (1px strict inset skips redundant scissor split)
    {
        auto store = std::make_shared<aero::UIResourceStore>();
        aero::VisualWorld world(nullptr, store.get());
        const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
        world.SetRoot(root);
        world.SetSize(root, 600.0f, 400.0f);

        const aero::VisualNodeId outerClip = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(root, outerClip);
        world.SetAlignment(outerClip, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(outerClip, 100.0f, 100.0f, 0.0f, 0.0f);
        world.SetSize(outerClip, 400.0f, 240.0f);
        world.SetClipToBounds(outerClip, true);

        const aero::VisualNodeId innerBox = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(outerClip, innerBox);
        world.SetAlignment(innerBox, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(innerBox, 40.0f, 40.0f, 0.0f, 0.0f);
        world.SetSize(innerBox, 80.0f, 50.0f);
        world.SetBoxStyle(innerBox, aero::ParseColorRgba32("#FF44AAFF"), 0u, 0, 0, 0, 0, 0, 0, 0, 0);

        world.UpdateLayout(600.0f, 400.0f);

        aero::VisualRenderAdapter adapter;
        aero::PagedRenderTree tree;
        tree.SetResourceStore(store.get());
        AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, tree, store.get()) >= 1u);
        AERO_CHECK(tree.PageCount() == 1u);
        AERO_CHECK((tree.GetPage(0u).header.flags & aero::CommandPageFlag_ScissorClipActive) == 0u);
    }

    // 3. Bit-exact 0-byte memcmp SoftwareSurfaceBackend incremental dirty-rect + SIMD rasterization
    {
        constexpr std::uint32_t W = 512u;
        constexpr std::uint32_t H = 384u;
        const aero::Color clearCol{0.05f, 0.08f, 0.14f, 1.0f};

        auto store = std::make_shared<aero::UIResourceStore>();
        aero::DecodedImage tex;
        tex.width = 32u;
        tex.height = 32u;
        tex.format = aero::ImageFormat::RawRgba8;
        tex.rgbaPixels.resize(32u * 32u * 4u);
        for (std::uint32_t y = 0u; y < 32u; ++y) {
            for (std::uint32_t x = 0u; x < 32u; ++x) {
                const bool chk = (((x / 4u) + (y / 4u)) & 1u) != 0u;
                const std::size_t off = (static_cast<std::size_t>(y) * 32u + x) * 4u;
                tex.rgbaPixels[off + 0u] = chk ? 220u : 30u;
                tex.rgbaPixels[off + 1u] = chk ? 120u : 190u;
                tex.rgbaPixels[off + 2u] = 240u;
                tex.rgbaPixels[off + 3u] = ((x + y) & 1u) ? 255u : 180u;
            }
        }
        const auto* texEntry = store->Images().RegisterDecodedImage("mem://dirty_chk.png", std::move(tex), false);
        AERO_CHECK(texEntry != nullptr);

        aero::VisualWorld world(nullptr, store.get());
        const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
        world.SetRoot(root);
        world.SetSize(root, static_cast<float>(W), static_cast<float>(H));

        const aero::VisualNodeId bgPanel = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(root, bgPanel);
        world.SetAlignment(bgPanel, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(bgPanel, 24.0f, 20.0f, 0.0f, 0.0f);
        world.SetSize(bgPanel, 260.0f, 120.0f);
        world.SetBoxStyle(bgPanel, aero::ParseColorRgba32("#883B82F6"), 0u, 8, 8, 8, 8, 0, 0, 0, 0);

        const aero::VisualNodeId imgNode = world.CreateNode(aero::VisualKind::Image);
        world.AddChild(root, imgNode);
        world.SetAlignment(imgNode, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(imgNode, 300.0f, 24.0f, 0.0f, 0.0f);
        world.SetSize(imgNode, 72.0f, 72.0f);
        world.SetImage(imgNode, "mem://dirty_chk.png", aero::Stretch::Fill, aero::StretchDirection::Both, 0xFFFFFFFFu);

        const aero::VisualNodeId mover = world.CreateNode(aero::VisualKind::Box);
        world.AddChild(root, mover);
        world.SetAlignment(mover, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(mover, 36.0f, 200.0f, 0.0f, 0.0f);
        world.SetSize(mover, 56.0f, 36.0f);
        world.SetBoxStyle(mover, aero::ParseColorRgba32("#FF10B981"), 0u, 6, 6, 6, 6, 0, 0, 0, 0);

        world.UpdateLayout(static_cast<float>(W), static_cast<float>(H));
        aero::VisualRenderAdapter adapter;
        aero::PagedRenderTree tree;
        tree.SetResourceStore(store.get());
        AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, tree, store.get()) >= 3u);

        aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 16u);
        aero::SoftwareSurfaceBackend backend;
        aero::GpuBackendConfig cfg{};
        cfg.initialViewportWidth = W;
        cfg.initialViewportHeight = H;
        AERO_CHECK(backend.Initialize(cfg));

        const auto f1 = backend.SubmitAndPresent(tree, stream, W, H, clearCol);
        AERO_CHECK(f1.succeeded);

        // Update moving box in-place so treeId and page topology stay preserved for dirty-rect rasterization
        const auto* moverDesc = world.GetNode(mover);
        AERO_CHECK(moverDesc != nullptr);
        AERO_CHECK(tree.UpdateSolidBoxNodeInPlace(
            moverDesc->layout.renderNodeId,
            aero::Rect{0.0f, 0.0f, 56.0f, 36.0f},
            aero::Color{16.0f / 255.0f, 185.0f / 255.0f, 129.0f / 255.0f, 1.0f},
            aero::CornerRadius{6.0f},
            aero::Matrix2D::Translation(320.0f, 240.0f),
            aero::BlendingMode::Normal));

        const auto f2 = backend.SubmitAndPresent(tree, stream, W, H, clearCol);
        AERO_CHECK(f2.succeeded);
        const aero::SoftwareDirtyRectStats dStats = backend.LastDirtyRasterStats();
        AERO_CHECK(!dStats.fellBackToFullFrame);
        AERO_CHECK(dStats.dirtyTiles > 0u && dStats.dirtyTiles < dStats.totalTiles);

        std::vector<std::uint8_t> fullRef;
        aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, W, H, fullRef, clearCol);
        AERO_CHECK(backend.FramebufferRgba8().size() == fullRef.size());
        AERO_CHECK(std::memcmp(backend.FramebufferRgba8().data(), fullRef.data(), fullRef.size()) == 0);
        backend.Shutdown();
    }

    return true;
}

bool TestPhase1SimdBlendSpansAndParallelDirtyTiles() {
    // 1. 4-pixel SIMD + odd tail widths (1, 3, 4, 7, 19, 65) in BlendSpanOverNormalFast
    {
        constexpr std::uint32_t W = 240u;
        constexpr std::uint32_t H = 180u;
        const aero::Color clearCol{0.11f, 0.19f, 0.29f, 1.0f};

        auto buildBaseWorld = [&](aero::VisualWorld& world, aero::VisualNodeId& outRoot) {
            outRoot = world.CreateNode(aero::VisualKind::Box);
            world.SetRoot(outRoot);
            world.SetSize(outRoot, static_cast<float>(W), static_cast<float>(H));

            const std::uint32_t stripeCols[4]{
                aero::ParseColorRgba32("#FF1E3A8A"),
                aero::ParseColorRgba32("#FF065F46"),
                aero::ParseColorRgba32("#FF7C2D12"),
                aero::ParseColorRgba32("#FF581C87"),
            };
            for (int s = 0; s < 4; ++s) {
                const aero::VisualNodeId stripe = world.CreateNode(aero::VisualKind::Box);
                world.AddChild(outRoot, stripe);
                world.SetAlignment(stripe, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
                world.SetMargin(stripe, static_cast<float>(s * 55 + 5), 6.0f, 0.0f, 0.0f);
                world.SetSize(stripe, 52.0f, 168.0f);
                world.SetBoxStyle(stripe, stripeCols[s], 0u, 0, 0, 0, 0, 0, 0, 0, 0);
            }
        };

        aero::VisualWorld bgWorld;
        aero::VisualNodeId bgRoot{};
        buildBaseWorld(bgWorld, bgRoot);
        bgWorld.UpdateLayout(static_cast<float>(W), static_cast<float>(H));

        aero::VisualRenderAdapter bgAdapter;
        aero::PagedRenderTree bgTree;
        AERO_CHECK(bgAdapter.SyncPagedRenderTree(bgWorld, nullptr, bgTree, nullptr) >= 4u);
        std::vector<std::uint8_t> bgRgba;
        aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(bgTree, W, H, bgRgba, clearCol);

        aero::VisualWorld fgWorld;
        aero::VisualNodeId fgRoot{};
        buildBaseWorld(fgWorld, fgRoot);

        constexpr int kOddWidths[6]{1, 3, 4, 7, 19, 65};
        constexpr std::uint8_t kSrcR = 0xE8u;
        constexpr std::uint8_t kSrcG = 0x5Du;
        constexpr std::uint8_t kSrcB = 0x2Au;
        constexpr std::uint8_t kSrcA = 0x96u;
        const std::uint32_t overlayCol = aero::ParseColorRgba32("#96E85D2A");

        for (int idx = 0; idx < 6; ++idx) {
            // Sharp translucent box (SolidQuad span path)
            const aero::VisualNodeId sharp = fgWorld.CreateNode(aero::VisualKind::Box);
            fgWorld.AddChild(fgRoot, sharp);
            fgWorld.SetAlignment(sharp, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
            fgWorld.SetMargin(sharp, 13.0f + static_cast<float>(idx * 3), static_cast<float>(10 + idx * 12), 0.0f, 0.0f);
            fgWorld.SetSize(sharp, static_cast<float>(kOddWidths[idx]), 8.0f);
            fgWorld.SetBoxStyle(sharp, overlayCol, 0u, 0, 0, 0, 0, 0, 0, 0, 0);

            // Rounded translucent box (SdfRoundedRect safe-interior span path)
            const aero::VisualNodeId rounded = fgWorld.CreateNode(aero::VisualKind::Box);
            fgWorld.AddChild(fgRoot, rounded);
            fgWorld.SetAlignment(rounded, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
            fgWorld.SetMargin(rounded, 11.0f + static_cast<float>(idx * 5), static_cast<float>(92 + idx * 14), 0.0f, 0.0f);
            fgWorld.SetSize(rounded, static_cast<float>(kOddWidths[idx] + 10), 10.0f);
            fgWorld.SetBoxStyle(rounded, overlayCol, 0u, 2, 2, 2, 2, 0, 0, 0, 0);
        }

        fgWorld.UpdateLayout(static_cast<float>(W), static_cast<float>(H));
        aero::VisualRenderAdapter fgAdapter;
        aero::PagedRenderTree fgTree;
        AERO_CHECK(fgAdapter.SyncPagedRenderTree(fgWorld, nullptr, fgTree, nullptr) >= 16u);
        std::vector<std::uint8_t> fgRgba;
        aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(fgTree, W, H, fgRgba, clearCol);

        const float a = static_cast<float>(kSrcA) / 255.0f;
        const float invA = 1.0f - a;
        auto expectedChan = [&](std::uint8_t srcC, std::uint8_t dstC) noexcept -> std::uint8_t {
            return static_cast<std::uint8_t>(
                std::clamp(static_cast<float>(srcC) * a + 0.5f + static_cast<float>(dstC) * invA, 0.0f, 255.0f));
        };

        std::size_t mismatchCount = 0u;
        for (int idx = 0; idx < 6; ++idx) {
            const int x0 = 13 + idx * 3;
            const int w = kOddWidths[idx];
            const int py = 10 + idx * 12 + 4;
            for (int px = x0; px < x0 + w; ++px) {
                const std::size_t off = (static_cast<std::size_t>(py) * W + static_cast<std::size_t>(px)) * 4u;
                if (fgRgba[off + 0u] != expectedChan(kSrcR, bgRgba[off + 0u]) ||
                    fgRgba[off + 1u] != expectedChan(kSrcG, bgRgba[off + 1u]) ||
                    fgRgba[off + 2u] != expectedChan(kSrcB, bgRgba[off + 2u]) ||
                    fgRgba[off + 3u] != expectedChan(255u, bgRgba[off + 3u])) {
                    ++mismatchCount;
                }
            }

            const int rx0 = 11 + idx * 5 + 5;
            const int rw = kOddWidths[idx];
            const int rpy = 92 + idx * 14 + 5;
            for (int px = rx0; px < rx0 + rw; ++px) {
                const std::size_t off = (static_cast<std::size_t>(rpy) * W + static_cast<std::size_t>(px)) * 4u;
                if (fgRgba[off + 0u] != expectedChan(kSrcR, bgRgba[off + 0u]) ||
                    fgRgba[off + 1u] != expectedChan(kSrcG, bgRgba[off + 1u]) ||
                    fgRgba[off + 2u] != expectedChan(kSrcB, bgRgba[off + 2u]) ||
                    fgRgba[off + 3u] != expectedChan(255u, bgRgba[off + 3u])) {
                    ++mismatchCount;
                }
            }
        }
        AERO_CHECK(mismatchCount == 0u);
    }

    // 2. Multi-threaded RasterizeTreeDirtyRectsToRgba8 on 384x320 (6x5 = 30 tiles, 8 dirty tiles across 4 bands)
    {
        constexpr std::uint32_t W = 384u;
        constexpr std::uint32_t H = 320u;
        const aero::Color clearCol{0.07f, 0.11f, 0.18f, 1.0f};

        auto store = std::make_shared<aero::UIResourceStore>();
        aero::DecodedImage tex;
        tex.width = 24u;
        tex.height = 24u;
        tex.format = aero::ImageFormat::RawRgba8;
        tex.rgbaPixels.resize(24u * 24u * 4u);
        for (std::uint32_t y = 0u; y < 24u; ++y) {
            for (std::uint32_t x = 0u; x < 24u; ++x) {
                const std::size_t off = (static_cast<std::size_t>(y) * 24u + x) * 4u;
                tex.rgbaPixels[off + 0u] = static_cast<std::uint8_t>(40u + x * 8u);
                tex.rgbaPixels[off + 1u] = static_cast<std::uint8_t>(60u + y * 7u);
                tex.rgbaPixels[off + 2u] = 210u;
                tex.rgbaPixels[off + 3u] = 220u;
            }
        }
        AERO_CHECK(store->Images().RegisterDecodedImage("mem://mt_dirty_tex.png", std::move(tex), false) != nullptr);

        aero::VisualWorld world(nullptr, store.get());
        const aero::VisualNodeId root = world.CreateNode(aero::VisualKind::Box);
        world.SetRoot(root);
        world.SetSize(root, static_cast<float>(W), static_cast<float>(H));

        for (int row = 0; row < 5; ++row) {
            const aero::VisualNodeId card = world.CreateNode(aero::VisualKind::Box);
            world.AddChild(root, card);
            world.SetAlignment(card, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
            world.SetMargin(card, 12.0f, static_cast<float>(row * 64 + 8), 0.0f, 0.0f);
            world.SetSize(card, 356.0f, 48.0f);
            world.SetBoxStyle(card, aero::ParseColorRgba32("#AA2563EB"), 0u, 8, 8, 8, 8, 0, 0, 0, 0);
        }

        const aero::VisualNodeId img = world.CreateNode(aero::VisualKind::Image);
        world.AddChild(root, img);
        world.SetAlignment(img, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
        world.SetMargin(img, 196.0f, 70.0f, 0.0f, 0.0f);
        world.SetSize(img, 112.0f, 52.0f);
        world.SetImage(img, "mem://mt_dirty_tex.png", aero::Stretch::Fill, aero::StretchDirection::Both, 0xFFFFFFFFu);

        world.UpdateLayout(static_cast<float>(W), static_cast<float>(H));
        aero::VisualRenderAdapter adapter;
        aero::PagedRenderTree tree;
        tree.SetResourceStore(store.get());
        AERO_CHECK(adapter.SyncPagedRenderTree(world, nullptr, tree, store.get()) >= 6u);

        std::vector<std::uint8_t> fullRef;
        aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, W, H, fullRef, clearCol);

        std::vector<std::uint8_t> partialBuf(static_cast<std::size_t>(W) * H * 4u);
        for (std::size_t p = 0u; p < static_cast<std::size_t>(W) * H; ++p) {
            partialBuf[p * 4u + 0u] = 0x13u;
            partialBuf[p * 4u + 1u] = 0x37u;
            partialBuf[p * 4u + 2u] = 0x5Au;
            partialBuf[p * 4u + 3u] = 0xFFu;
        }

        const std::vector<aero::Rect> dirtyRects{
            aero::Rect{10.0f, 10.0f, 90.0f, 36.0f},    // tx 0..1, ty 0 (2 tiles)
            aero::Rect{200.0f, 76.0f, 100.0f, 40.0f},  // tx 3..4, ty 1 (2 tiles)
            aero::Rect{72.0f, 140.0f, 110.0f, 40.0f},  // tx 1..2, ty 2 (2 tiles)
            aero::Rect{264.0f, 266.0f, 96.0f, 40.0f},  // tx 4..5, ty 4 (2 tiles)
        };

        aero::SoftwareDirtyRectStats dStats{};
        aero::ZeroCopyGpuStream::RasterizeTreeDirtyRectsToRgba8(
            tree, W, H, dirtyRects, partialBuf, clearCol, &dStats);

        AERO_CHECK(!dStats.fellBackToFullFrame);
        AERO_CHECK(dStats.totalTiles == 30u);
        AERO_CHECK(dStats.dirtyTiles == 8u);
        AERO_CHECK(dStats.mergedStrips == 4u);

        auto isTileExpectedDirty = [](std::uint32_t tx, std::uint32_t ty) noexcept -> bool {
            return (ty == 0u && (tx == 0u || tx == 1u)) ||
                   (ty == 1u && (tx == 3u || tx == 4u)) ||
                   (ty == 2u && (tx == 1u || tx == 2u)) ||
                   (ty == 4u && (tx == 4u || tx == 5u));
        };

        std::size_t dirtyDiffPixels = 0u;
        std::size_t cleanTouchedPixels = 0u;
        for (std::uint32_t py = 0u; py < H; ++py) {
            for (std::uint32_t px = 0u; px < W; ++px) {
                const std::size_t off = (static_cast<std::size_t>(py) * W + px) * 4u;
                if (isTileExpectedDirty(px / 64u, py / 64u)) {
                    if (std::memcmp(partialBuf.data() + off, fullRef.data() + off, 4u) != 0) {
                        ++dirtyDiffPixels;
                    }
                } else {
                    if (partialBuf[off + 0u] != 0x13u ||
                        partialBuf[off + 1u] != 0x37u ||
                        partialBuf[off + 2u] != 0x5Au ||
                        partialBuf[off + 3u] != 0xFFu) {
                        ++cleanTouchedPixels;
                    }
                }
            }
        }
        AERO_CHECK(dirtyDiffPixels == 0u);
        AERO_CHECK(cleanTouchedPixels == 0u);

        // Verify lowered 0.50 full-frame fallback threshold (18 / 30 = 0.60 > 0.50)
        aero::SoftwareDirtyRectStats fbStats{};
        const std::vector<aero::Rect> largeDirty{aero::Rect{4.0f, 4.0f, 360.0f, 180.0f}};
        aero::ZeroCopyGpuStream::RasterizeTreeDirtyRectsToRgba8(
            tree, W, H, largeDirty, partialBuf, clearCol, &fbStats);
        AERO_CHECK(fbStats.fellBackToFullFrame);
        AERO_CHECK(std::memcmp(partialBuf.data(), fullRef.data(), fullRef.size()) == 0);
    }

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
        {"WindowHighDpiLogicalSizing", &TestWindowHighDpiLogicalSizing},
        {"RiveRendererAndBackdropBlur", &TestRiveRendererAndBackdropBlur},
        {"PlasmaSlotReuseLoopAndLru", &TestPlasmaSlotReuseLoopAndLru},
        {"CompositeMaskedPathImageTintAndTile", &TestCompositeMaskedPathImageTintAndTile},
        {"ArabicRtlAndBidiFixes", &TestArabicRtlAndBidiFixes},
        {"RiveOverflowGuardAndPartialTrim", &TestRiveOverflowGuardAndPartialTrim},
        {"ModalLiveResizeAndTextEllipsis", &TestModalLiveResizeAndTextEllipsis},
        {"MockGlClipWResetAndIncrementalTailPad", &TestMockGlClipWResetAndIncrementalTailPad},
        {"SharedAtlasMaskQuotaNoLeak", &TestSharedAtlasMaskQuotaNoLeak},
        {"LeafTextClipEliminationAndOverflowClip", &TestLeafTextClipEliminationAndOverflowClip},
        {"FractionalScissorPixelCenterAndNoOverflowSeam", &TestFractionalScissorPixelCenterAndNoOverflowSeam},
        {"StandaloneCpuEvictionAndLazyReload", &TestStandaloneCpuEvictionAndLazyReload},
        {"NumericPlasmaAndRiveAtlasPaths", &TestNumericPlasmaAndRiveAtlasPaths},
        {"Phase4MultiPageAtlasScissorElisionAndDirtyRects", &TestPhase4MultiPageAtlasScissorElisionAndDirtyRects},
        {"Phase1SimdBlendSpansAndParallelDirtyTiles", &TestPhase1SimdBlendSpansAndParallelDirtyTiles},
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
