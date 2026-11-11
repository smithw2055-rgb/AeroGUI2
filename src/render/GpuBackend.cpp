#include "aero/render/GpuBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "aero/controls/Controls.hpp"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace aero {

namespace {

// Standard OpenGL 3.3 Core / WebGL2 constants (matching Khronos GL/GLES3 headers)
constexpr AeroGLenum kGlTriangles = 0x0004u;
constexpr AeroGLenum kGlDepthTest = 0x0B71u;
constexpr AeroGLenum kGlCullFace = 0x0B44u;
constexpr AeroGLenum kGlBlend = 0x0BE2u;
constexpr AeroGLenum kGlScissorTest = 0x0C11u;
constexpr AeroGLenum kGlUnpackAlignment = 0x0CF5u;
constexpr AeroGLenum kGlPackAlignment = 0x0D05u;
constexpr AeroGLenum kGlTexture2D = 0x0DE1u;
constexpr AeroGLenum kGlUnsignedByte = 0x1401u;
constexpr AeroGLenum kGlUnsignedInt = 0x1405u;
constexpr AeroGLenum kGlFloat = 0x1406u;
constexpr AeroGLenum kGlRgba = 0x1908u;
constexpr AeroGLenum kGlVendor = 0x1F00u;
constexpr AeroGLenum kGlRenderer = 0x1F01u;
constexpr AeroGLenum kGlVersion = 0x1F02u;
constexpr AeroGLenum kGlOne = 1u;
constexpr AeroGLenum kGlSrcAlpha = 0x0302u;
constexpr AeroGLenum kGlOneMinusSrcAlpha = 0x0303u;
constexpr AeroGLenum kGlLinear = 0x2601u;
constexpr AeroGLenum kGlTextureMagFilter = 0x2800u;
constexpr AeroGLenum kGlTextureMinFilter = 0x2801u;
constexpr AeroGLenum kGlTextureWrapS = 0x2802u;
constexpr AeroGLenum kGlTextureWrapT = 0x2803u;
constexpr AeroGLenum kGlClampToEdge = 0x812Fu;
constexpr AeroGLenum kGlRgba8 = 0x8058u;
constexpr AeroGLenum kGlTexture0 = 0x84C0u;
constexpr AeroGLenum kGlArrayBuffer = 0x8892u;
constexpr AeroGLenum kGlElementArrayBuffer = 0x8893u;
constexpr AeroGLenum kGlStaticDraw = 0x88E4u;
constexpr AeroGLenum kGlDynamicDraw = 0x88E8u;
constexpr AeroGLenum kGlFragmentShader = 0x8B30u;
constexpr AeroGLenum kGlVertexShader = 0x8B31u;
constexpr AeroGLenum kGlCompileStatus = 0x8B81u;
constexpr AeroGLenum kGlLinkStatus = 0x8B82u;
constexpr AeroGLenum kGlInfoLogLength = 0x8B84u;
constexpr AeroGLbitfield kGlColorBufferBit = 0x00004000u;

constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
constexpr std::size_t kQuadsPerPage = kVerticesPerCommandPage / 4u;

// Compact procedural 5x7 glyph bitmap generator for ASCII printable glyphs so real OpenGL / WebGL2
// SdfTextGlyph draws legible crisp characters out-of-the-box without external FreeType binaries.
[[nodiscard]] std::uint8_t SampleProceduralGlyphBit(char ch, int gx, int gy) noexcept {
    if (gx < 0 || gx >= 5 || gy < 0 || gy >= 7) {
        return 0u;
    }
    std::uint8_t rows[7]{0, 0, 0, 0, 0, 0, 0};
    switch (ch) {
        case '0': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x13; rows[3]=0x15; rows[4]=0x19; rows[5]=0x11; rows[6]=0x0E; break;
        case '1': rows[0]=0x04; rows[1]=0x0C; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x0E; break;
        case '2': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x01; rows[3]=0x06; rows[4]=0x08; rows[5]=0x10; rows[6]=0x1F; break;
        case '3': rows[0]=0x1F; rows[1]=0x02; rows[2]=0x04; rows[3]=0x02; rows[4]=0x01; rows[5]=0x11; rows[6]=0x0E; break;
        case '4': rows[0]=0x02; rows[1]=0x06; rows[2]=0x0A; rows[3]=0x12; rows[4]=0x1F; rows[5]=0x02; rows[6]=0x02; break;
        case '5': rows[0]=0x1F; rows[1]=0x10; rows[2]=0x1E; rows[3]=0x01; rows[4]=0x01; rows[5]=0x11; rows[6]=0x0E; break;
        case '6': rows[0]=0x06; rows[1]=0x08; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case '7': rows[0]=0x1F; rows[1]=0x01; rows[2]=0x02; rows[3]=0x04; rows[4]=0x08; rows[5]=0x08; rows[6]=0x08; break;
        case '8': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x0E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case '9': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x0F; rows[4]=0x01; rows[5]=0x02; rows[6]=0x0C; break;
        case '.': rows[5]=0x0C; rows[6]=0x0C; break;
        case ':': rows[1]=0x0C; rows[2]=0x0C; rows[4]=0x0C; rows[5]=0x0C; break;
        case '-': rows[3]=0x1F; break;
        case '+': rows[1]=0x04; rows[2]=0x04; rows[3]=0x1F; rows[4]=0x04; rows[5]=0x04; break;
        case '/': rows[0]=0x01; rows[1]=0x02; rows[2]=0x02; rows[3]=0x04; rows[4]=0x08; rows[5]=0x08; rows[6]=0x10; break;
        case '%': rows[0]=0x19; rows[1]=0x19; rows[2]=0x02; rows[3]=0x04; rows[4]=0x08; rows[5]=0x13; rows[6]=0x13; break;
        case ' ': break;
        default: {
            const char up = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch;
            if (up >= 'A' && up <= 'Z') {
                const unsigned seed = static_cast<unsigned>(up - 'A' + 1);
                rows[0] = 0x1Eu;
                rows[1] = static_cast<std::uint8_t>(0x11u | ((seed & 1u) ? 0x04u : 0x00u));
                rows[2] = 0x11u;
                rows[3] = static_cast<std::uint8_t>((seed & 2u) ? 0x1Fu : 0x19u);
                rows[4] = 0x11u;
                rows[5] = static_cast<std::uint8_t>(0x11u | ((seed & 4u) ? 0x02u : 0x00u));
                rows[6] = static_cast<std::uint8_t>((seed & 8u) ? 0x1Eu : 0x11u);
            } else {
                rows[1] = 0x0Eu;
                rows[2] = 0x0Au;
                rows[3] = 0x0Eu;
                rows[4] = 0x0Au;
                rows[5] = 0x0Eu;
            }
            break;
        }
    }
    return ((rows[gy] >> (4 - gx)) & 1u) != 0u ? 1u : 0u;
}

void BakeSdfCellFrom5x7(
    char ch,
    std::uint32_t cellX,
    std::uint32_t cellY,
    std::uint32_t cellW,
    std::uint32_t cellH,
    std::uint32_t atlasStride,
    std::vector<std::uint8_t>& ioAtlas) noexcept {
    for (std::uint32_t py = 0; py < cellH; ++py) {
        for (std::uint32_t px = 0; px < cellW; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(std::max(1u, cellW));
            const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(std::max(1u, cellH));
            const float gx = u * 7.0f - 1.0f;
            const float gy = v * 9.0f - 1.0f;

            float minInsideDistSq = 100.0f;
            float minOutsideDistSq = 100.0f;
            const int centerOn = SampleProceduralGlyphBit(
                ch,
                static_cast<int>(std::floor(gx)),
                static_cast<int>(std::floor(gy)));

            for (int sy = -1; sy <= 7; ++sy) {
                for (int sx = -1; sx <= 5; ++sx) {
                    const std::uint8_t bit = SampleProceduralGlyphBit(ch, sx, sy);
                    const float dx = gx - (static_cast<float>(sx) + 0.5f);
                    const float dy = gy - (static_cast<float>(sy) + 0.5f);
                    const float d2 = dx * dx + dy * dy;
                    if (bit != 0u) {
                        minOutsideDistSq = std::min(minOutsideDistSq, d2);
                    } else {
                        minInsideDistSq = std::min(minInsideDistSq, d2);
                    }
                }
            }

            float sdf01 = 0.0f;
            if (ch != ' ') {
                const float dist = (centerOn != 0)
                    ? (std::sqrt(minInsideDistSq) - 0.35f)
                    : -(std::sqrt(minOutsideDistSq) - 0.35f);
                sdf01 = std::clamp(0.5f + dist * 0.28f, 0.0f, 1.0f);
            }
            const auto byteVal = static_cast<std::uint8_t>(sdf01 * 255.0f + 0.5f);
            const std::size_t idx =
                (static_cast<std::size_t>(cellY + py) * atlasStride + (cellX + px)) * 4u;
            if (idx + 3u < ioAtlas.size()) {
                ioAtlas[idx + 0u] = byteVal;
                ioAtlas[idx + 1u] = byteVal;
                ioAtlas[idx + 2u] = byteVal;
                ioAtlas[idx + 3u] = 255u;
            }
        }
    }
}

template <typename FnPtr>
void AssignRawProc(FnPtr& outTyped, void* rawSym) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(void*), "Function pointer must match pointer width");
    std::memcpy(&outTyped, &rawSym, sizeof(void*));
}

} // namespace

bool GlFunctionTable::IsComplete() const noexcept {
    return Viewport != nullptr &&
           Scissor != nullptr &&
           Enable != nullptr &&
           Disable != nullptr &&
           BlendFuncSeparate != nullptr &&
           ClearColor != nullptr &&
           Clear != nullptr &&
           GenBuffers != nullptr &&
           DeleteBuffers != nullptr &&
           BindBuffer != nullptr &&
           BufferData != nullptr &&
           BufferSubData != nullptr &&
           GenVertexArrays != nullptr &&
           DeleteVertexArrays != nullptr &&
           BindVertexArray != nullptr &&
           EnableVertexAttribArray != nullptr &&
           VertexAttribPointer != nullptr &&
           VertexAttribIPointer != nullptr &&
           CreateShader != nullptr &&
           DeleteShader != nullptr &&
           ShaderSource != nullptr &&
           CompileShader != nullptr &&
           GetShaderiv != nullptr &&
           GetShaderInfoLog != nullptr &&
           CreateProgram != nullptr &&
           DeleteProgram != nullptr &&
           AttachShader != nullptr &&
           LinkProgram != nullptr &&
           GetProgramiv != nullptr &&
           GetProgramInfoLog != nullptr &&
           UseProgram != nullptr &&
           GetUniformLocation != nullptr &&
           Uniform1i != nullptr &&
           Uniform2f != nullptr &&
           GenTextures != nullptr &&
           DeleteTextures != nullptr &&
           ActiveTexture != nullptr &&
           BindTexture != nullptr &&
           TexImage2D != nullptr &&
           TexSubImage2D != nullptr &&
           TexParameteri != nullptr &&
           PixelStorei != nullptr &&
           DrawElements != nullptr;
}

void BuildStaticQuadIndexBuffer(
    std::size_t quadCount,
    std::vector<std::uint32_t>& outIndices) {
    outIndices.resize(quadCount * 6u);
    std::uint32_t* __restrict dst = outIndices.data();
    for (std::size_t q = 0; q < quadCount; ++q) {
        const auto baseVertex = static_cast<std::uint32_t>(q * 4u);
        const std::size_t idxOffset = q * 6u;
        // Triangle 0: v0 -> v1 -> v2
        dst[idxOffset + 0u] = baseVertex + 0u;
        dst[idxOffset + 1u] = baseVertex + 1u;
        dst[idxOffset + 2u] = baseVertex + 2u;
        // Triangle 1: v0 -> v2 -> v3
        dst[idxOffset + 3u] = baseVertex + 0u;
        dst[idxOffset + 4u] = baseVertex + 2u;
        dst[idxOffset + 5u] = baseVertex + 3u;
    }
}

GlScissorBox ComputeGlScissorBox(
    const Rect& clipRectTopLeft,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) noexcept {
    const float vpW = static_cast<float>(viewportWidth);
    const float vpH = static_cast<float>(viewportHeight);

    const float x0 = std::clamp(clipRectTopLeft.x, 0.0f, vpW);
    const float y0 = std::clamp(clipRectTopLeft.y, 0.0f, vpH);
    const float x1 = std::clamp(clipRectTopLeft.x + std::max(0.0f, clipRectTopLeft.width), x0, vpW);
    const float y1 = std::clamp(clipRectTopLeft.y + std::max(0.0f, clipRectTopLeft.height), y0, vpH);

    const auto leftPx = static_cast<std::int32_t>(std::floor(x0));
    const auto rightPx = static_cast<std::int32_t>(std::ceil(x1));
    const auto topFromTopPx = static_cast<std::int32_t>(std::floor(y0));
    const auto bottomFromTopPx = static_cast<std::int32_t>(std::ceil(y1));

    GlScissorBox box{};
    box.x = leftPx;
    box.width = std::max<std::int32_t>(0, rightPx - leftPx);
    box.height = std::max<std::int32_t>(0, bottomFromTopPx - topFromTopPx);
    box.y = std::max<std::int32_t>(0, static_cast<std::int32_t>(viewportHeight) - bottomFromTopPx);
    return box;
}

void BuildDefaultUiAtlasRgba8(
    const PagedRenderTree& referenceTree,
    std::uint32_t atlasSize,
    std::vector<std::uint8_t>& outRgba8) {
    const std::uint32_t safeSize = std::max<std::uint32_t>(64u, atlasSize);
    outRgba8.assign(static_cast<std::size_t>(safeSize) * safeSize * 4u, 0u);

    // Fill neutral opaque white texel at bottom-right corner (1,1) for untextured fallbacks
    const std::size_t lastTexel = (static_cast<std::size_t>(safeSize) * safeSize - 1u) * 4u;
    outRgba8[lastTexel + 0u] = 255u;
    outRgba8[lastTexel + 1u] = 255u;
    outRgba8[lastTexel + 2u] = 255u;
    outRgba8[lastTexel + 3u] = 255u;

    // Bake ASCII printable SDF glyphs into top-half 16x16 grid slots matching LookupPreCachedGlyph
    for (std::uint32_t cp = 0x20u; cp <= 0x7Eu; ++cp) {
        const GlyphUvRect uv = referenceTree.LookupPreCachedGlyph(static_cast<char>(cp));
        if (!uv.valid) {
            continue;
        }
        const auto cx = static_cast<std::uint32_t>(uv.u0 * static_cast<float>(safeSize) + 0.5f);
        const auto cy = static_cast<std::uint32_t>(uv.v0 * static_cast<float>(safeSize) + 0.5f);
        const auto cw = std::max<std::uint32_t>(
            4u,
            static_cast<std::uint32_t>((uv.u1 - uv.u0) * static_cast<float>(safeSize) + 0.5f));
        const auto ch = std::max<std::uint32_t>(
            4u,
            static_cast<std::uint32_t>((uv.v1 - uv.v0) * static_cast<float>(safeSize) + 0.5f));
        if (cx + cw <= safeSize && cy + ch <= safeSize) {
            BakeSdfCellFrom5x7(static_cast<char>(cp), cx, cy, cw, ch, safeSize, outRgba8);
        }
    }

    // Populate rows 6..7 generic CJK/Unicode hashed glyph fallback slots with a clean geometric glyph
    for (std::uint32_t slot = 0; slot < 32u; ++slot) {
        const std::uint32_t col = slot & 0x0Fu;
        const std::uint32_t row = 6u + ((slot >> 4u) & 0x01u);
        const auto cx = static_cast<std::uint32_t>(static_cast<float>(col) * 0.0625f * static_cast<float>(safeSize));
        const auto cy = static_cast<std::uint32_t>(static_cast<float>(row) * 0.0625f * static_cast<float>(safeSize));
        const auto cw = std::max<std::uint32_t>(4u, static_cast<std::uint32_t>(0.058f * static_cast<float>(safeSize)));
        const auto ch = cw;
        if (cx + cw <= safeSize && cy + ch <= safeSize) {
            BakeSdfCellFrom5x7('#', cx, cy, cw, ch, safeSize, outRgba8);
        }
    }

    // P1-3: Bake registered LinearGradientBrush / RadialGradientBrush 1D LUT rows + float32 descriptor rows
    // into bottom half of the UI atlas so GL3.3 / WebGL2 / WGSL UberShaders sample multi-stop gradients in O(1).
    const std::size_t brushCount = GradientBrushRegistry::Instance().BrushCount();
    const std::uint32_t minAllowedRow = safeSize / 2u;
    auto writeFloatTexel = [&](std::uint32_t tx, std::uint32_t ty, float val) noexcept {
        if (tx >= safeSize || ty >= safeSize) {
            return;
        }
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &val, sizeof(float));
        const std::size_t off = (static_cast<std::size_t>(ty) * safeSize + tx) * 4u;
        outRgba8[off + 0u] = static_cast<std::uint8_t>(bits & 0xFFu);
        outRgba8[off + 1u] = static_cast<std::uint8_t>((bits >> 8u) & 0xFFu);
        outRgba8[off + 2u] = static_cast<std::uint8_t>((bits >> 16u) & 0xFFu);
        outRgba8[off + 3u] = static_cast<std::uint8_t>((bits >> 24u) & 0xFFu);
    };

    for (std::size_t idx = 0; idx < brushCount; ++idx) {
        const auto brushId = static_cast<std::uint32_t>(idx + 1u);
        if (safeSize < 4u + static_cast<std::uint32_t>(idx * 2u)) {
            break;
        }
        const std::uint32_t rampRow = safeSize - 2u - static_cast<std::uint32_t>(idx * 2u);
        if (rampRow == 0u || rampRow - 1u < minAllowedRow) {
            break;
        }
        const std::uint32_t metaRow = rampRow - 1u;
        const GradientBrushDesc* brush = GradientBrushRegistry::Instance().Find(brushId);
        if (!brush) {
            continue;
        }

        const std::uint32_t rampWidth = std::min<std::uint32_t>(safeSize, GradientBrushDesc::kLutWidth);
        if (rampWidth == GradientBrushDesc::kLutWidth) {
            std::memcpy(
                outRgba8.data() + static_cast<std::size_t>(rampRow) * safeSize * 4u,
                brush->bakedRampRgba8.data(),
                GradientBrushDesc::kLutWidth * 4u);
        } else if (rampWidth > 1u) {
            for (std::uint32_t x = 0; x < rampWidth; ++x) {
                const float t = static_cast<float>(x) / static_cast<float>(rampWidth - 1u);
                const Color c = SampleGradientRampAtT(*brush, t);
                const std::uint32_t packed = PackColorRgba8(c, 1.0f);
                const std::size_t off = (static_cast<std::size_t>(rampRow) * safeSize + x) * 4u;
                outRgba8[off + 0u] = static_cast<std::uint8_t>(packed & 0xFFu);
                outRgba8[off + 1u] = static_cast<std::uint8_t>((packed >> 8u) & 0xFFu);
                outRgba8[off + 2u] = static_cast<std::uint8_t>((packed >> 16u) & 0xFFu);
                outRgba8[off + 3u] = static_cast<std::uint8_t>((packed >> 24u) & 0xFFu);
            }
        }

        const std::size_t m0Off = static_cast<std::size_t>(metaRow) * safeSize * 4u;
        outRgba8[m0Off + 0u] = static_cast<std::uint8_t>(brush->kind);
        outRgba8[m0Off + 1u] = static_cast<std::uint8_t>(brush->mappingMode);
        outRgba8[m0Off + 2u] = static_cast<std::uint8_t>(brush->spreadMethod);
        outRgba8[m0Off + 3u] = static_cast<std::uint8_t>(
            std::clamp(brush->opacity, 0.0f, 1.0f) * 255.0f + 0.5f);

        if (brush->kind == GradientBrushKind::Linear) {
            writeFloatTexel(1u, metaRow, brush->startPoint.x);
            writeFloatTexel(2u, metaRow, brush->startPoint.y);
            writeFloatTexel(3u, metaRow, brush->endPoint.x);
            writeFloatTexel(4u, metaRow, brush->endPoint.y);
        } else {
            writeFloatTexel(1u, metaRow, brush->center.x);
            writeFloatTexel(2u, metaRow, brush->center.y);
            writeFloatTexel(3u, metaRow, brush->gradientOrigin.x);
            writeFloatTexel(4u, metaRow, brush->gradientOrigin.y);
        }
        writeFloatTexel(5u, metaRow, brush->radiusX);
        writeFloatTexel(6u, metaRow, brush->radiusY);
    }
}

GlDynamicProbeResult ProbeOpenGlDynamicLibrary() noexcept {
    GlDynamicProbeResult res{};
#if defined(__EMSCRIPTEN__)
    res.libGlLoaded = true;
    res.procAddressResolverReady = true;
    res.coreEntryPointsFound = true;
    res.resolvedSymbolCount = 43u;
    res.loadedLibraryName = "Emscripten-WebGL2";
    const auto* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (ver) {
        res.hasCurrentContext = true;
        res.glVersionString = ver;
    }
    res.diagnostics = "Emscripten WebGL2 static-import symbols ready.";
#elif defined(_WIN32)
    HMODULE glMod = LoadLibraryA("opengl32.dll");
    if (glMod) {
        res.libGlLoaded = true;
        res.loadedLibraryName = "opengl32.dll";
        auto wglGetProc = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
            GetProcAddress(glMod, "wglGetProcAddress"));
        auto wglGetCur = reinterpret_cast<HGLRC (WINAPI*)()>(
            GetProcAddress(glMod, "wglGetCurrentContext"));
        res.procAddressResolverReady = (wglGetProc != nullptr);
        res.hasCurrentContext = (wglGetCur != nullptr && wglGetCur() != nullptr);
        const char* kCoreNames[] = {
            "glViewport", "glScissor", "glEnable", "glDisable",
            "glClearColor", "glClear", "glDrawElements", "glGetString"
        };
        for (const char* sym : kCoreNames) {
            if (GetProcAddress(glMod, sym) != nullptr) {
                ++res.resolvedSymbolCount;
            }
        }
        res.coreEntryPointsFound = (res.resolvedSymbolCount >= 8u);
        FreeLibrary(glMod);
        res.diagnostics = res.hasCurrentContext
            ? "Win32 opengl32.dll loaded with active WGL context."
            : "Win32 opengl32.dll loaded (no active WGL context on calling thread).";
    } else {
        res.diagnostics = "Win32 opengl32.dll unavailable.";
    }
#elif defined(__linux__)
    void* glHandle = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (glHandle) {
        res.libGlLoaded = true;
        res.loadedLibraryName = "libGL.so.1";
    } else {
        glHandle = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
        if (glHandle) {
            res.libGlLoaded = true;
            res.loadedLibraryName = "libGL.so";
        }
    }

    void* eglHandle = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (eglHandle) {
        res.libEglLoaded = true;
        if (res.loadedLibraryName.empty()) {
            res.loadedLibraryName = "libEGL.so.1";
        }
    }

    using GlxGetProcFn = void (*(*)(const std::uint8_t*))();
    using EglGetProcFn = void (*(*)(const char*))();
    using GlxGetCurrentContextFn = void* (*)();
    using EglGetCurrentContextFn = void* (*)();

    GlxGetProcFn glxGetProc = nullptr;
    GlxGetCurrentContextFn glxGetCur = nullptr;
    if (glHandle) {
        AssignRawProc(glxGetProc, dlsym(glHandle, "glXGetProcAddressARB"));
        if (!glxGetProc) {
            AssignRawProc(glxGetProc, dlsym(glHandle, "glXGetProcAddress"));
        }
        AssignRawProc(glxGetCur, dlsym(glHandle, "glXGetCurrentContext"));
    }

    EglGetProcFn eglGetProc = nullptr;
    EglGetCurrentContextFn eglGetCur = nullptr;
    if (eglHandle) {
        AssignRawProc(eglGetProc, dlsym(eglHandle, "eglGetProcAddress"));
        AssignRawProc(eglGetCur, dlsym(eglHandle, "eglGetCurrentContext"));
    }

    res.procAddressResolverReady = (glxGetProc != nullptr) || (eglGetProc != nullptr);
    if ((glxGetCur && glxGetCur() != nullptr) || (eglGetCur && eglGetCur() != nullptr)) {
        res.hasCurrentContext = true;
    }

    const char* kProbeSymbols[] = {
        "glViewport", "glScissor", "glEnable", "glDisable",
        "glBlendFuncSeparate", "glClearColor", "glClear",
        "glGenBuffers", "glBindBuffer", "glBufferData", "glBufferSubData",
        "glGenVertexArrays", "glBindVertexArray", "glVertexAttribPointer", "glVertexAttribIPointer",
        "glCreateShader", "glCompileShader", "glCreateProgram", "glLinkProgram", "glUseProgram",
        "glGenTextures", "glBindTexture", "glTexImage2D", "glTexSubImage2D", "glDrawElements"
    };
    for (const char* name : kProbeSymbols) {
        void* sym = nullptr;
        if (glHandle) {
            sym = dlsym(glHandle, name);
        }
        if (!sym && glxGetProc) {
            auto fn = glxGetProc(reinterpret_cast<const std::uint8_t*>(name));
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        if (!sym && eglGetProc) {
            auto fn = eglGetProc(name);
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        if (sym) {
            ++res.resolvedSymbolCount;
        }
    }
    res.coreEntryPointsFound = (res.resolvedSymbolCount >= 20u);

    if (res.hasCurrentContext && glHandle) {
        const std::uint8_t* (*glGetStr)(AeroGLenum) = nullptr;
        AssignRawProc(glGetStr, dlsym(glHandle, "glGetString"));
        if (glGetStr) {
            if (const auto* v = glGetStr(kGlVersion)) {
                res.glVersionString = reinterpret_cast<const char*>(v);
            }
            if (const auto* r = glGetStr(kGlRenderer)) {
                res.glRendererString = reinterpret_cast<const char*>(r);
            }
        }
    }

    if (eglHandle) {
        dlclose(eglHandle);
    }
    if (glHandle) {
        dlclose(glHandle);
    }

    if (res.libGlLoaded || res.libEglLoaded) {
        res.diagnostics = "Linux dlopen probe succeeded (" + res.loadedLibraryName +
            ", resolved " + std::to_string(res.resolvedSymbolCount) + "/25 probe symbols, activeContext=" +
            (res.hasCurrentContext ? "true" : "false") + ").";
    } else {
        res.diagnostics = "Neither libGL.so.1 nor libEGL.so.1 found via dlopen.";
    }
#else
    res.diagnostics = "OpenGL dynamic probe unsupported on target OS.";
#endif
    return res;
}

bool WriteRgba8ToPpm(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) noexcept {
    if (filePath.empty() || width == 0u || height == 0u || !rgba8Pixels) {
        return false;
    }
    const std::string pathStr(filePath);
    std::FILE* fp = std::fopen(pathStr.c_str(), "wb");
    if (!fp) {
        return false;
    }

    char header[64]{};
    const int headerLen = std::snprintf(
        header,
        sizeof(header),
        "P6\n%u %u\n255\n",
        width,
        height);
    if (headerLen <= 0 ||
        std::fwrite(header, 1u, static_cast<std::size_t>(headerLen), fp) !=
            static_cast<std::size_t>(headerLen)) {
        std::fclose(fp);
        return false;
    }

    std::vector<std::uint8_t> rowRgb(static_cast<std::size_t>(width) * 3u);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* srcRow =
            rgba8Pixels + static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0; x < width; ++x) {
            rowRgb[x * 3u + 0u] = srcRow[x * 4u + 0u];
            rowRgb[x * 3u + 1u] = srcRow[x * 4u + 1u];
            rowRgb[x * 3u + 2u] = srcRow[x * 4u + 2u];
        }
        if (std::fwrite(rowRgb.data(), 1u, rowRgb.size(), fp) != rowRgb.size()) {
            std::fclose(fp);
            return false;
        }
    }

    std::fclose(fp);
    return true;
}

bool WriteRgba8ToBmp(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) noexcept {
    if (filePath.empty() || width == 0u || height == 0u || !rgba8Pixels) {
        return false;
    }
    const std::string pathStr(filePath);
    std::FILE* fp = std::fopen(pathStr.c_str(), "wb");
    if (!fp) {
        return false;
    }

    const std::uint32_t pixelDataBytes = width * height * 4u;
    const std::uint32_t fileSize = 54u + pixelDataBytes;

    std::uint8_t bmpHeader[54]{};
    bmpHeader[0] = 'B';
    bmpHeader[1] = 'M';
    auto writeLe32 = [&](std::size_t off, std::uint32_t val) noexcept {
        bmpHeader[off + 0u] = static_cast<std::uint8_t>(val & 0xFFu);
        bmpHeader[off + 1u] = static_cast<std::uint8_t>((val >> 8u) & 0xFFu);
        bmpHeader[off + 2u] = static_cast<std::uint8_t>((val >> 16u) & 0xFFu);
        bmpHeader[off + 3u] = static_cast<std::uint8_t>((val >> 24u) & 0xFFu);
    };
    auto writeLe16 = [&](std::size_t off, std::uint16_t val) noexcept {
        bmpHeader[off + 0u] = static_cast<std::uint8_t>(val & 0xFFu);
        bmpHeader[off + 1u] = static_cast<std::uint8_t>((val >> 8u) & 0xFFu);
    };

    writeLe32(2u, fileSize);
    writeLe32(10u, 54u);
    writeLe32(14u, 40u); // BITMAPINFOHEADER
    writeLe32(18u, width);
    // Negative biHeight denotes top-down row order matching AeroGUI top-left origin
    writeLe32(22u, static_cast<std::uint32_t>(-static_cast<std::int32_t>(height)));
    writeLe16(26u, 1u);
    writeLe16(28u, 32u);
    writeLe32(34u, pixelDataBytes);

    if (std::fwrite(bmpHeader, 1u, sizeof(bmpHeader), fp) != sizeof(bmpHeader)) {
        std::fclose(fp);
        return false;
    }

    std::vector<std::uint8_t> rowBgra(static_cast<std::size_t>(width) * 4u);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* srcRow =
            rgba8Pixels + static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0; x < width; ++x) {
            rowBgra[x * 4u + 0u] = srcRow[x * 4u + 2u]; // B
            rowBgra[x * 4u + 1u] = srcRow[x * 4u + 1u]; // G
            rowBgra[x * 4u + 2u] = srcRow[x * 4u + 0u]; // R
            rowBgra[x * 4u + 3u] = srcRow[x * 4u + 3u]; // A
        }
        if (std::fwrite(rowBgra.data(), 1u, rowBgra.size(), fp) != rowBgra.size()) {
            std::fclose(fp);
            return false;
        }
    }

    std::fclose(fp);
    return true;
}

// ============================================================================
// IGpuDeviceBackend Shared Helpers
// ============================================================================

GpuBackendFrameStats IGpuDeviceBackend::PresentStream(
    const PagedRenderTree& tree,
    const ZeroCopyGpuStream& stream,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    Color clearColor) {
    const std::size_t ringPages = std::max<std::size_t>(
        1u,
        stream.RingCapacityBytes() / kPageVertexBytes);
    return ExecuteSubmission(
        tree,
        stream.LastBufferSubDataSpans(),
        stream.LastClipDrawBatches(),
        ringPages,
        viewportWidth,
        viewportHeight,
        clearColor);
}

GpuBackendFrameStats IGpuDeviceBackend::SubmitAndPresent(
    PagedRenderTree& tree,
    ZeroCopyGpuStream& stream,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    Color clearColor) {
    (void)stream.SubmitDirtyPages(tree);
    return PresentStream(tree, stream, viewportWidth, viewportHeight, clearColor);
}

// ============================================================================
// 1. OpenGlDeviceBackend Implementation
// ============================================================================

OpenGlDeviceBackend::OpenGlDeviceBackend(const GlFunctionTable& customFunctions) noexcept
    : gl_(customFunctions),
      hasCustomTable_(true) {}

OpenGlDeviceBackend::~OpenGlDeviceBackend() {
    Shutdown();
}

void OpenGlDeviceBackend::SetCustomFunctionTable(const GlFunctionTable& customFunctions) noexcept {
    gl_ = customFunctions;
    hasCustomTable_ = true;
}

bool OpenGlDeviceBackend::LoadDynamicGlEntryPoints() {
    if (hasCustomTable_) {
        if (!gl_.IsComplete()) {
            diagnostics_ = "Custom GlFunctionTable is missing required OpenGL 3.3 / WebGL2 entry points.";
            return false;
        }
        return true;
    }

#if defined(__EMSCRIPTEN__)
    gl_.Viewport = &glViewport;
    gl_.Scissor = &glScissor;
    gl_.Enable = &glEnable;
    gl_.Disable = &glDisable;
    gl_.BlendFuncSeparate = &glBlendFuncSeparate;
    gl_.ClearColor = &glClearColor;
    gl_.Clear = &glClear;
    gl_.GenBuffers = &glGenBuffers;
    gl_.DeleteBuffers = &glDeleteBuffers;
    gl_.BindBuffer = &glBindBuffer;
    gl_.BufferData = &glBufferData;
    gl_.BufferSubData = &glBufferSubData;
    gl_.GenVertexArrays = &glGenVertexArrays;
    gl_.DeleteVertexArrays = &glDeleteVertexArrays;
    gl_.BindVertexArray = &glBindVertexArray;
    gl_.EnableVertexAttribArray = &glEnableVertexAttribArray;
    gl_.VertexAttribPointer = &glVertexAttribPointer;
    gl_.VertexAttribIPointer = &glVertexAttribIPointer;
    gl_.CreateShader = &glCreateShader;
    gl_.DeleteShader = &glDeleteShader;
    gl_.ShaderSource = &glShaderSource;
    gl_.CompileShader = &glCompileShader;
    gl_.GetShaderiv = &glGetShaderiv;
    gl_.GetShaderInfoLog = &glGetShaderInfoLog;
    gl_.CreateProgram = &glCreateProgram;
    gl_.DeleteProgram = &glDeleteProgram;
    gl_.AttachShader = &glAttachShader;
    gl_.LinkProgram = &glLinkProgram;
    gl_.GetProgramiv = &glGetProgramiv;
    gl_.GetProgramInfoLog = &glGetProgramInfoLog;
    gl_.UseProgram = &glUseProgram;
    gl_.GetUniformLocation = &glGetUniformLocation;
    gl_.Uniform1i = &glUniform1i;
    gl_.Uniform2f = &glUniform2f;
    gl_.GenTextures = &glGenTextures;
    gl_.DeleteTextures = &glDeleteTextures;
    gl_.ActiveTexture = &glActiveTexture;
    gl_.BindTexture = &glBindTexture;
    gl_.TexImage2D = &glTexImage2D;
    gl_.TexSubImage2D = &glTexSubImage2D;
    gl_.TexParameteri = &glTexParameteri;
    gl_.PixelStorei = &glPixelStorei;
    gl_.DrawElements = &glDrawElements;
    gl_.ReadPixels = &glReadPixels;
    gl_.GetError = &glGetError;
    gl_.GetString = &glGetString;
    return gl_.IsComplete();

#elif defined(_WIN32)
    HMODULE glMod = LoadLibraryA("opengl32.dll");
    if (!glMod) {
        diagnostics_ = "LoadLibraryA(opengl32.dll) failed.";
        return false;
    }
    dynGlLibHandle_ = static_cast<void*>(glMod);

    auto wglGetCur = reinterpret_cast<HGLRC (WINAPI*)()>(
        GetProcAddress(glMod, "wglGetCurrentContext"));
    if (!wglGetCur || wglGetCur() == nullptr) {
        diagnostics_ = "No active WGL context on current thread when initializing OpenGlDeviceBackend.";
        return false;
    }

    auto wglGetProc = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
        GetProcAddress(glMod, "wglGetProcAddress"));

    auto resolveWinGl = [&](auto& outFn, const char* name) noexcept {
        void* p = nullptr;
        if (wglGetProc) {
            PROC extP = wglGetProc(name);
            const auto rawVal = reinterpret_cast<std::uintptr_t>(extP);
            if (rawVal > 3u && rawVal != static_cast<std::uintptr_t>(-1)) {
                p = reinterpret_cast<void*>(extP);
            }
        }
        if (!p) {
            p = reinterpret_cast<void*>(GetProcAddress(glMod, name));
        }
        AssignRawProc(outFn, p);
    };

    resolveWinGl(gl_.Viewport, "glViewport");
    resolveWinGl(gl_.Scissor, "glScissor");
    resolveWinGl(gl_.Enable, "glEnable");
    resolveWinGl(gl_.Disable, "glDisable");
    resolveWinGl(gl_.BlendFuncSeparate, "glBlendFuncSeparate");
    resolveWinGl(gl_.ClearColor, "glClearColor");
    resolveWinGl(gl_.Clear, "glClear");
    resolveWinGl(gl_.GenBuffers, "glGenBuffers");
    resolveWinGl(gl_.DeleteBuffers, "glDeleteBuffers");
    resolveWinGl(gl_.BindBuffer, "glBindBuffer");
    resolveWinGl(gl_.BufferData, "glBufferData");
    resolveWinGl(gl_.BufferSubData, "glBufferSubData");
    resolveWinGl(gl_.GenVertexArrays, "glGenVertexArrays");
    resolveWinGl(gl_.DeleteVertexArrays, "glDeleteVertexArrays");
    resolveWinGl(gl_.BindVertexArray, "glBindVertexArray");
    resolveWinGl(gl_.EnableVertexAttribArray, "glEnableVertexAttribArray");
    resolveWinGl(gl_.VertexAttribPointer, "glVertexAttribPointer");
    resolveWinGl(gl_.VertexAttribIPointer, "glVertexAttribIPointer");
    resolveWinGl(gl_.CreateShader, "glCreateShader");
    resolveWinGl(gl_.DeleteShader, "glDeleteShader");
    resolveWinGl(gl_.ShaderSource, "glShaderSource");
    resolveWinGl(gl_.CompileShader, "glCompileShader");
    resolveWinGl(gl_.GetShaderiv, "glGetShaderiv");
    resolveWinGl(gl_.GetShaderInfoLog, "glGetShaderInfoLog");
    resolveWinGl(gl_.CreateProgram, "glCreateProgram");
    resolveWinGl(gl_.DeleteProgram, "glDeleteProgram");
    resolveWinGl(gl_.AttachShader, "glAttachShader");
    resolveWinGl(gl_.LinkProgram, "glLinkProgram");
    resolveWinGl(gl_.GetProgramiv, "glGetProgramiv");
    resolveWinGl(gl_.GetProgramInfoLog, "glGetProgramInfoLog");
    resolveWinGl(gl_.UseProgram, "glUseProgram");
    resolveWinGl(gl_.GetUniformLocation, "glGetUniformLocation");
    resolveWinGl(gl_.Uniform1i, "glUniform1i");
    resolveWinGl(gl_.Uniform2f, "glUniform2f");
    resolveWinGl(gl_.GenTextures, "glGenTextures");
    resolveWinGl(gl_.DeleteTextures, "glDeleteTextures");
    resolveWinGl(gl_.ActiveTexture, "glActiveTexture");
    resolveWinGl(gl_.BindTexture, "glBindTexture");
    resolveWinGl(gl_.TexImage2D, "glTexImage2D");
    resolveWinGl(gl_.TexSubImage2D, "glTexSubImage2D");
    resolveWinGl(gl_.TexParameteri, "glTexParameteri");
    resolveWinGl(gl_.PixelStorei, "glPixelStorei");
    resolveWinGl(gl_.DrawElements, "glDrawElements");
    resolveWinGl(gl_.ReadPixels, "glReadPixels");
    resolveWinGl(gl_.GetError, "glGetError");
    resolveWinGl(gl_.GetString, "glGetString");

    if (!gl_.IsComplete()) {
        diagnostics_ = "Failed to resolve all required OpenGL 3.3 Core entry points via WGL.";
        return false;
    }
    return true;

#elif defined(__linux__)
    dynGlLibHandle_ = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!dynGlLibHandle_) {
        dynGlLibHandle_ = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
    }
    dynEglLibHandle_ = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!dynEglLibHandle_) {
        dynEglLibHandle_ = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    }
    dynGlesLibHandle_ = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);

    if (!dynGlLibHandle_ && !dynEglLibHandle_ && !dynGlesLibHandle_) {
        diagnostics_ = "dlopen failed for libGL.so.1, libEGL.so.1, and libGLESv2.so.2.";
        return false;
    }

    using GlxGetProcFn = void (*(*)(const std::uint8_t*))();
    using EglGetProcFn = void (*(*)(const char*))();
    using GetCurCtxFn = void* (*)();

    GlxGetProcFn glxGetProc = nullptr;
    GetCurCtxFn glxGetCur = nullptr;
    if (dynGlLibHandle_) {
        AssignRawProc(glxGetProc, dlsym(dynGlLibHandle_, "glXGetProcAddressARB"));
        if (!glxGetProc) {
            AssignRawProc(glxGetProc, dlsym(dynGlLibHandle_, "glXGetProcAddress"));
        }
        AssignRawProc(glxGetCur, dlsym(dynGlLibHandle_, "glXGetCurrentContext"));
    }

    EglGetProcFn eglGetProc = nullptr;
    GetCurCtxFn eglGetCur = nullptr;
    if (dynEglLibHandle_) {
        AssignRawProc(eglGetProc, dlsym(dynEglLibHandle_, "eglGetProcAddress"));
        AssignRawProc(eglGetCur, dlsym(dynEglLibHandle_, "eglGetCurrentContext"));
    }

    const bool hasCtx =
        (glxGetCur && glxGetCur() != nullptr) ||
        (eglGetCur && eglGetCur() != nullptr);
    if (!hasCtx) {
        diagnostics_ = "OpenGL dynamic libraries loaded, but no active GLX/EGL context is bound on this thread.";
        return false;
    }

    auto resolvePosixGl = [&](auto& outFn, const char* name) noexcept {
        void* sym = nullptr;
        if (dynGlLibHandle_) {
            sym = dlsym(dynGlLibHandle_, name);
        }
        if (!sym && dynGlesLibHandle_) {
            sym = dlsym(dynGlesLibHandle_, name);
        }
        if (!sym && glxGetProc) {
            auto fn = glxGetProc(reinterpret_cast<const std::uint8_t*>(name));
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        if (!sym && eglGetProc) {
            auto fn = eglGetProc(name);
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        AssignRawProc(outFn, sym);
    };

    resolvePosixGl(gl_.Viewport, "glViewport");
    resolvePosixGl(gl_.Scissor, "glScissor");
    resolvePosixGl(gl_.Enable, "glEnable");
    resolvePosixGl(gl_.Disable, "glDisable");
    resolvePosixGl(gl_.BlendFuncSeparate, "glBlendFuncSeparate");
    resolvePosixGl(gl_.ClearColor, "glClearColor");
    resolvePosixGl(gl_.Clear, "glClear");
    resolvePosixGl(gl_.GenBuffers, "glGenBuffers");
    resolvePosixGl(gl_.DeleteBuffers, "glDeleteBuffers");
    resolvePosixGl(gl_.BindBuffer, "glBindBuffer");
    resolvePosixGl(gl_.BufferData, "glBufferData");
    resolvePosixGl(gl_.BufferSubData, "glBufferSubData");
    resolvePosixGl(gl_.GenVertexArrays, "glGenVertexArrays");
    resolvePosixGl(gl_.DeleteVertexArrays, "glDeleteVertexArrays");
    resolvePosixGl(gl_.BindVertexArray, "glBindVertexArray");
    resolvePosixGl(gl_.EnableVertexAttribArray, "glEnableVertexAttribArray");
    resolvePosixGl(gl_.VertexAttribPointer, "glVertexAttribPointer");
    resolvePosixGl(gl_.VertexAttribIPointer, "glVertexAttribIPointer");
    resolvePosixGl(gl_.CreateShader, "glCreateShader");
    resolvePosixGl(gl_.DeleteShader, "glDeleteShader");
    resolvePosixGl(gl_.ShaderSource, "glShaderSource");
    resolvePosixGl(gl_.CompileShader, "glCompileShader");
    resolvePosixGl(gl_.GetShaderiv, "glGetShaderiv");
    resolvePosixGl(gl_.GetShaderInfoLog, "glGetShaderInfoLog");
    resolvePosixGl(gl_.CreateProgram, "glCreateProgram");
    resolvePosixGl(gl_.DeleteProgram, "glDeleteProgram");
    resolvePosixGl(gl_.AttachShader, "glAttachShader");
    resolvePosixGl(gl_.LinkProgram, "glLinkProgram");
    resolvePosixGl(gl_.GetProgramiv, "glGetProgramiv");
    resolvePosixGl(gl_.GetProgramInfoLog, "glGetProgramInfoLog");
    resolvePosixGl(gl_.UseProgram, "glUseProgram");
    resolvePosixGl(gl_.GetUniformLocation, "glGetUniformLocation");
    resolvePosixGl(gl_.Uniform1i, "glUniform1i");
    resolvePosixGl(gl_.Uniform2f, "glUniform2f");
    resolvePosixGl(gl_.GenTextures, "glGenTextures");
    resolvePosixGl(gl_.DeleteTextures, "glDeleteTextures");
    resolvePosixGl(gl_.ActiveTexture, "glActiveTexture");
    resolvePosixGl(gl_.BindTexture, "glBindTexture");
    resolvePosixGl(gl_.TexImage2D, "glTexImage2D");
    resolvePosixGl(gl_.TexSubImage2D, "glTexSubImage2D");
    resolvePosixGl(gl_.TexParameteri, "glTexParameteri");
    resolvePosixGl(gl_.PixelStorei, "glPixelStorei");
    resolvePosixGl(gl_.DrawElements, "glDrawElements");
    resolvePosixGl(gl_.ReadPixels, "glReadPixels");
    resolvePosixGl(gl_.GetError, "glGetError");
    resolvePosixGl(gl_.GetString, "glGetString");

    if (!gl_.IsComplete()) {
        diagnostics_ = "Failed to resolve complete GL 3.3 / GLES3 symbol table via dlopen.";
        return false;
    }
    return true;
#else
    diagnostics_ = "OpenGlDeviceBackend dynamic loader not implemented for target platform.";
    return false;
#endif
}

bool OpenGlDeviceBackend::Initialize(const GpuBackendConfig& config) {
    Shutdown();

#if defined(__EMSCRIPTEN__)
    shaderBackend_ = ShaderBackend::WebGL2;
#else
    shaderBackend_ = (config.shaderBackend == ShaderBackend::WebGPU_WGSL)
        ? ShaderBackend::DesktopGL33
        : config.shaderBackend;
#endif

    if (!LoadDynamicGlEntryPoints()) {
        Shutdown();
        return false;
    }

    const std::string_view vsSrc = ZeroCopyGpuStream::UberVertexShaderSource(shaderBackend_);
    const std::string_view psSrc = ZeroCopyGpuStream::UberPixelShaderSource(shaderBackend_);
    if (!CompileAndLinkUberShader(vsSrc, psSrc)) {
        Shutdown();
        return false;
    }

    gl_.GenVertexArrays(1, &vaoId_);
    if (vaoId_ == 0u) {
        diagnostics_ = "glGenVertexArrays returned 0.";
        Shutdown();
        return false;
    }

    if (!AllocateRingVertexBuffer(std::max<std::size_t>(16u, config.maxPagesInRing))) {
        Shutdown();
        return false;
    }

    const std::size_t minQuads = std::max<std::size_t>(
        config.initialQuadCapacity,
        maxPagesInRing_ * kQuadsPerPage);
    if (!EnsureQuadIndexCapacity(minQuads)) {
        Shutdown();
        return false;
    }

    BindAeroVertexLayout();

    if (config.autoBuildDefaultAtlas) {
        PagedRenderTree refTree;
        std::vector<std::uint8_t> defaultAtlas;
        BuildDefaultUiAtlasRgba8(refTree, config.defaultAtlasSize, defaultAtlas);
        if (!UploadAtlasTexture(
                config.defaultAtlasSize,
                config.defaultAtlasSize,
                defaultAtlas.data())) {
            Shutdown();
            return false;
        }
    }

    lastViewportWidth_ = config.initialViewportWidth;
    lastViewportHeight_ = config.initialViewportHeight;
    initialized_ = true;
    diagnostics_ = "OpenGlDeviceBackend initialized successfully.";
    return true;
}

void OpenGlDeviceBackend::Shutdown() noexcept {
    if (gl_.IsComplete()) {
        if (atlasTextureId_ != 0u) {
            gl_.DeleteTextures(1, &atlasTextureId_);
        }
        if (quadEboId_ != 0u) {
            gl_.DeleteBuffers(1, &quadEboId_);
        }
        if (ringVboId_ != 0u) {
            gl_.DeleteBuffers(1, &ringVboId_);
        }
        if (vaoId_ != 0u) {
            gl_.DeleteVertexArrays(1, &vaoId_);
        }
        if (programId_ != 0u) {
            gl_.DeleteProgram(programId_);
        }
    }

    atlasTextureId_ = 0u;
    quadEboId_ = 0u;
    ringVboId_ = 0u;
    vaoId_ = 0u;
    programId_ = 0u;
    uViewportSizeLoc_ = -1;
    uQuadHalfExtentLoc_ = -1;
    uAtlasSamplerLoc_ = -1;
    maxPagesInRing_ = 0u;
    ringBufferBytes_ = 0u;
    quadCapacity_ = 0u;
    atlasWidth_ = 0u;
    atlasHeight_ = 0u;
    quadIndexMirror_.clear();
    initialized_ = false;

#if defined(_WIN32)
    if (dynGlLibHandle_) {
        FreeLibrary(static_cast<HMODULE>(dynGlLibHandle_));
        dynGlLibHandle_ = nullptr;
    }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (dynGlesLibHandle_) {
        dlclose(dynGlesLibHandle_);
        dynGlesLibHandle_ = nullptr;
    }
    if (dynEglLibHandle_) {
        dlclose(dynEglLibHandle_);
        dynEglLibHandle_ = nullptr;
    }
    if (dynGlLibHandle_) {
        dlclose(dynGlLibHandle_);
        dynGlLibHandle_ = nullptr;
    }
#endif
    if (!hasCustomTable_) {
        gl_ = GlFunctionTable{};
    }
}

bool OpenGlDeviceBackend::CompileAndLinkUberShader(
    std::string_view vertexShaderSrc,
    std::string_view pixelShaderSrc) {
    if (!gl_.IsComplete()) {
        diagnostics_ = "Cannot compile shader: GL function table is incomplete.";
        return false;
    }
    if (vertexShaderSrc.empty() || pixelShaderSrc.empty()) {
        diagnostics_ = "Cannot compile shader: empty vertex or fragment shader source.";
        return false;
    }

    auto compileStage = [&](AeroGLenum stageType, std::string_view src, const char* stageLabel, AeroGLuint& outShader) -> bool {
        const AeroGLuint sh = gl_.CreateShader(stageType);
        if (sh == 0u) {
            diagnostics_ = std::string("glCreateShader failed for ") + stageLabel;
            return false;
        }
        const AeroGLchar* srcPtr = src.data();
        const auto srcLen = static_cast<AeroGLint>(src.size());
        gl_.ShaderSource(sh, 1, &srcPtr, &srcLen);
        gl_.CompileShader(sh);

        AeroGLint status = 0;
        gl_.GetShaderiv(sh, kGlCompileStatus, &status);
        if (status == 0) {
            AeroGLint logLen = 0;
            gl_.GetShaderiv(sh, kGlInfoLogLength, &logLen);
            std::string infoLog(static_cast<std::size_t>(std::max<AeroGLint>(logLen, 256)), '\0');
            AeroGLsizei written = 0;
            gl_.GetShaderInfoLog(sh, static_cast<AeroGLsizei>(infoLog.size()), &written, infoLog.data());
            infoLog.resize(static_cast<std::size_t>(std::max<AeroGLsizei>(0, written)));
            diagnostics_ = std::string(stageLabel) + " compile error: " + infoLog;
            gl_.DeleteShader(sh);
            return false;
        }
        outShader = sh;
        return true;
    };

    AeroGLuint vs = 0u;
    if (!compileStage(kGlVertexShader, vertexShaderSrc, "UberVertexShader", vs)) {
        return false;
    }

    AeroGLuint fs = 0u;
    if (!compileStage(kGlFragmentShader, pixelShaderSrc, "UberPixelShader", fs)) {
        gl_.DeleteShader(vs);
        return false;
    }

    const AeroGLuint prog = gl_.CreateProgram();
    if (prog == 0u) {
        gl_.DeleteShader(vs);
        gl_.DeleteShader(fs);
        diagnostics_ = "glCreateProgram returned 0.";
        return false;
    }

    gl_.AttachShader(prog, vs);
    gl_.AttachShader(prog, fs);
    gl_.LinkProgram(prog);
    gl_.DeleteShader(vs);
    gl_.DeleteShader(fs);

    AeroGLint linkOk = 0;
    gl_.GetProgramiv(prog, kGlLinkStatus, &linkOk);
    if (linkOk == 0) {
        AeroGLint logLen = 0;
        gl_.GetProgramiv(prog, kGlInfoLogLength, &logLen);
        std::string infoLog(static_cast<std::size_t>(std::max<AeroGLint>(logLen, 256)), '\0');
        AeroGLsizei written = 0;
        gl_.GetProgramInfoLog(prog, static_cast<AeroGLsizei>(infoLog.size()), &written, infoLog.data());
        infoLog.resize(static_cast<std::size_t>(std::max<AeroGLsizei>(0, written)));
        diagnostics_ = "UberShader program link error: " + infoLog;
        gl_.DeleteProgram(prog);
        return false;
    }

    if (programId_ != 0u) {
        gl_.DeleteProgram(programId_);
    }
    programId_ = prog;
    uViewportSizeLoc_ = gl_.GetUniformLocation(programId_, "u_ViewportSize");
    uQuadHalfExtentLoc_ = gl_.GetUniformLocation(programId_, "u_QuadHalfExtent");
    uAtlasSamplerLoc_ = gl_.GetUniformLocation(programId_, "u_GlyphAndUiAtlas");
    diagnostics_ = "UberShader compiled and linked cleanly.";
    return true;
}

bool OpenGlDeviceBackend::UploadAtlasTexture(
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) {
    if (!gl_.IsComplete() || width == 0u || height == 0u || !rgba8Pixels) {
        diagnostics_ = "UploadAtlasTexture: invalid dimensions or null pixel pointer.";
        return false;
    }
    if (atlasTextureId_ == 0u) {
        gl_.GenTextures(1, &atlasTextureId_);
        if (atlasTextureId_ == 0u) {
            diagnostics_ = "glGenTextures returned 0.";
            return false;
        }
    }

    gl_.ActiveTexture(kGlTexture0);
    gl_.BindTexture(kGlTexture2D, atlasTextureId_);
    gl_.PixelStorei(kGlUnpackAlignment, 1);
    gl_.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<AeroGLint>(kGlLinear));
    gl_.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<AeroGLint>(kGlLinear));
    gl_.TexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<AeroGLint>(kGlClampToEdge));
    gl_.TexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<AeroGLint>(kGlClampToEdge));
    gl_.TexImage2D(
        kGlTexture2D,
        0,
        static_cast<AeroGLint>(kGlRgba8),
        static_cast<AeroGLsizei>(width),
        static_cast<AeroGLsizei>(height),
        0,
        kGlRgba,
        kGlUnsignedByte,
        rgba8Pixels);

    atlasWidth_ = width;
    atlasHeight_ = height;
    telemetry_.atlasUploadCount += 1u;
    return true;
}

bool OpenGlDeviceBackend::UpdateAtlasSubRegion(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) {
    if (!gl_.IsComplete() || atlasTextureId_ == 0u || !rgba8Pixels ||
        width == 0u || height == 0u ||
        x + width > atlasWidth_ || y + height > atlasHeight_) {
        diagnostics_ = "UpdateAtlasSubRegion: out-of-bounds region or uninitialized atlas texture.";
        return false;
    }

    gl_.ActiveTexture(kGlTexture0);
    gl_.BindTexture(kGlTexture2D, atlasTextureId_);
    gl_.PixelStorei(kGlUnpackAlignment, 1);
    gl_.TexSubImage2D(
        kGlTexture2D,
        0,
        static_cast<AeroGLint>(x),
        static_cast<AeroGLint>(y),
        static_cast<AeroGLsizei>(width),
        static_cast<AeroGLsizei>(height),
        kGlRgba,
        kGlUnsignedByte,
        rgba8Pixels);
    telemetry_.atlasSubUpdateCount += 1u;
    return true;
}

bool OpenGlDeviceBackend::AllocateRingVertexBuffer(std::size_t maxPagesInRing) {
    if (!gl_.IsComplete() || maxPagesInRing == 0u) {
        return false;
    }
    if (ringVboId_ == 0u) {
        gl_.GenBuffers(1, &ringVboId_);
        if (ringVboId_ == 0u) {
            diagnostics_ = "glGenBuffers failed for ring VBO.";
            return false;
        }
    }

    maxPagesInRing_ = maxPagesInRing;
    ringBufferBytes_ = maxPagesInRing_ * kPageVertexBytes;
    if (vaoId_ != 0u) {
        gl_.BindVertexArray(vaoId_);
    }
    gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
    gl_.BufferData(
        kGlArrayBuffer,
        static_cast<AeroGLsizeiptr>(ringBufferBytes_),
        nullptr,
        kGlDynamicDraw);

    if (vaoId_ != 0u && quadEboId_ != 0u) {
        BindAeroVertexLayout();
    }
    return true;
}

bool OpenGlDeviceBackend::EnsureQuadIndexCapacity(std::size_t requiredQuadCount) {
    if (!gl_.IsComplete()) {
        return false;
    }
    if (requiredQuadCount <= quadCapacity_ && quadEboId_ != 0u) {
        return true;
    }

    std::size_t nextCap = std::max<std::size_t>(64u, quadCapacity_);
    while (nextCap < requiredQuadCount) {
        nextCap *= 2u;
    }
    if (quadCapacity_ == 0u) {
        nextCap = std::max<std::size_t>(64u, requiredQuadCount);
    }

    const bool wasAllocated = (quadCapacity_ > 0u);
    BuildStaticQuadIndexBuffer(nextCap, quadIndexMirror_);

    if (quadEboId_ == 0u) {
        gl_.GenBuffers(1, &quadEboId_);
        if (quadEboId_ == 0u) {
            diagnostics_ = "glGenBuffers failed for static Quad EBO.";
            return false;
        }
    }

    if (vaoId_ != 0u) {
        gl_.BindVertexArray(vaoId_);
    }
    gl_.BindBuffer(kGlElementArrayBuffer, quadEboId_);
    gl_.BufferData(
        kGlElementArrayBuffer,
        static_cast<AeroGLsizeiptr>(quadIndexMirror_.size() * sizeof(std::uint32_t)),
        quadIndexMirror_.data(),
        kGlStaticDraw);

    quadCapacity_ = nextCap;
    if (wasAllocated) {
        telemetry_.quadEboExpansionCount += 1u;
    }
    return true;
}

void OpenGlDeviceBackend::BindAeroVertexLayout() noexcept {
    if (!gl_.IsComplete() || vaoId_ == 0u || ringVboId_ == 0u || quadEboId_ == 0u) {
        return;
    }
    gl_.BindVertexArray(vaoId_);
    gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
    gl_.BindBuffer(kGlElementArrayBuffer, quadEboId_);

    constexpr auto kStride = static_cast<AeroGLsizei>(sizeof(AeroVertex));
    // layout(location = 0) in vec2 a_Position (float x, y @ offset 0)
    gl_.EnableVertexAttribArray(0u);
    gl_.VertexAttribPointer(0u, 2, kGlFloat, 0u, kStride, reinterpret_cast<const void*>(std::uintptr_t{0}));
    // layout(location = 1) in vec2 a_TexCoord (float u, v @ offset 8)
    gl_.EnableVertexAttribArray(1u);
    gl_.VertexAttribPointer(1u, 2, kGlFloat, 0u, kStride, reinterpret_cast<const void*>(std::uintptr_t{8}));
    // layout(location = 2) in uint a_PackedRgba (uint32_t rgba @ offset 16)
    gl_.EnableVertexAttribArray(2u);
    gl_.VertexAttribIPointer(2u, 1, kGlUnsignedInt, kStride, reinterpret_cast<const void*>(std::uintptr_t{16}));
    // layout(location = 3) in uint a_PrimFlags (uint16_t primType + uint16_t flags @ offset 20)
    gl_.EnableVertexAttribArray(3u);
    gl_.VertexAttribIPointer(3u, 1, kGlUnsignedInt, kStride, reinterpret_cast<const void*>(std::uintptr_t{20}));
}

GpuBackendFrameStats OpenGlDeviceBackend::ExecuteSubmission(
    const PagedRenderTree& /*tree*/,
    const std::vector<GlBufferSubDataSpan>& uploadSpans,
    const std::vector<GpuClipDrawBatch>& drawBatches,
    std::size_t ringPageCapacity,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    Color clearColor) {
    GpuBackendFrameStats stats{};
    if (!initialized_ || !gl_.IsComplete() || viewportWidth == 0u || viewportHeight == 0u) {
        return stats;
    }

    if (ringPageCapacity > maxPagesInRing_) {
        if (!AllocateRingVertexBuffer(ringPageCapacity)) {
            return stats;
        }
    }

    const std::size_t effRingPages = std::max<std::size_t>(1u, maxPagesInRing_);
    lastViewportWidth_ = viewportWidth;
    lastViewportHeight_ = viewportHeight;

    // 1. Stream dirty 4KB CommandPage vertex spans into the Ring VBO via glBufferSubData
    gl_.BindVertexArray(vaoId_);
    gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
    for (const GlBufferSubDataSpan& span : uploadSpans) {
        if (!span.sourcePageVertexPtr || span.byteSize == 0u) {
            continue;
        }
        const std::size_t slotIdx = span.pageIndex % effRingPages;
        const std::size_t dstOffset = slotIdx * kPageVertexBytes;
        const std::size_t clampedBytes = std::min<std::size_t>(span.byteSize, kPageVertexBytes);
        if (dstOffset + kPageVertexBytes > ringBufferBytes_) {
            continue;
        }

        if (clampedBytes < kPageVertexBytes) {
            // Pad trailing unused quad vertices in this page slot with zeroed transparent vertices
            // so coalesced multi-page draws never render stale vertex garbage from an earlier frame.
            AeroVertex paddedPage[kVerticesPerCommandPage]{};
            std::memcpy(paddedPage, span.sourcePageVertexPtr, clampedBytes);
            gl_.BufferSubData(
                kGlArrayBuffer,
                static_cast<AeroGLintptr>(dstOffset),
                static_cast<AeroGLsizeiptr>(kPageVertexBytes),
                paddedPage);
        } else {
            gl_.BufferSubData(
                kGlArrayBuffer,
                static_cast<AeroGLintptr>(dstOffset),
                static_cast<AeroGLsizeiptr>(clampedBytes),
                span.sourcePageVertexPtr);
        }
        stats.dirtySpansUploaded += 1u;
        stats.bytesUploaded += clampedBytes;
    }

    // 2. Ensure Static Quad Index Buffer (0,1,2, 0,2,3 ...) covers every batch's highest quad index
    const std::uint32_t expansionsBefore = telemetry_.quadEboExpansionCount;
    for (const GpuClipDrawBatch& batch : drawBatches) {
        const std::size_t firstSlot = batch.firstPageIndex % effRingPages;
        const std::size_t firstQuad = firstSlot * kQuadsPerPage;
        const std::size_t batchQuads = batch.vertexCount / 4u;
        if (batchQuads > 0u && !EnsureQuadIndexCapacity(firstQuad + batchQuads)) {
            return stats;
        }
    }
    stats.quadEboExpanded = (telemetry_.quadEboExpansionCount > expansionsBefore);

    // 3. Configure viewport, clear target, pipeline state, and UberShader uniforms
    gl_.Viewport(0, 0, static_cast<AeroGLsizei>(viewportWidth), static_cast<AeroGLsizei>(viewportHeight));
    gl_.Disable(kGlDepthTest);
    gl_.Disable(kGlCullFace);
    gl_.Disable(kGlScissorTest);
    gl_.ClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    gl_.Clear(kGlColorBufferBit);

    gl_.Enable(kGlBlend);
    gl_.BlendFuncSeparate(kGlSrcAlpha, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);

    gl_.UseProgram(programId_);
    if (uViewportSizeLoc_ >= 0) {
        gl_.Uniform2f(
            uViewportSizeLoc_,
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight));
    }
    if (uQuadHalfExtentLoc_ >= 0) {
        gl_.Uniform2f(uQuadHalfExtentLoc_, 0.0f, 0.0f);
    }
    if (atlasTextureId_ != 0u) {
        gl_.ActiveTexture(kGlTexture0);
        gl_.BindTexture(kGlTexture2D, atlasTextureId_);
        if (uAtlasSamplerLoc_ >= 0) {
            gl_.Uniform1i(uAtlasSamplerLoc_, 0);
        }
    }

    // 4. Issue indexed GL_TRIANGLES draw calls over coalesced GpuClipDrawBatches
    gl_.BindBuffer(kGlElementArrayBuffer, quadEboId_);
    bool scissorCurrentlyEnabled = false;

    for (const GpuClipDrawBatch& batch : drawBatches) {
        const std::size_t quadCount = batch.vertexCount / 4u;
        if (quadCount == 0u) {
            continue;
        }
        stats.clipBatchesProcessed += 1u;

        if (batch.scissorActive) {
            if (!scissorCurrentlyEnabled) {
                gl_.Enable(kGlScissorTest);
                scissorCurrentlyEnabled = true;
            }
            const GlScissorBox sc = ComputeGlScissorBox(
                batch.scissorRect,
                viewportWidth,
                viewportHeight);
            gl_.Scissor(sc.x, sc.y, sc.width, sc.height);
            stats.scissorBatchCount += 1u;
            if (sc.width <= 0 || sc.height <= 0) {
                continue;
            }
        } else if (scissorCurrentlyEnabled) {
            gl_.Disable(kGlScissorTest);
            scissorCurrentlyEnabled = false;
        }

        const std::size_t firstSlot = batch.firstPageIndex % effRingPages;
        const std::size_t firstQuad = firstSlot * kQuadsPerPage;
        const std::size_t indexCount = quadCount * 6u;
        const std::uintptr_t indexByteOffset = firstQuad * 6u * sizeof(std::uint32_t);

        gl_.DrawElements(
            kGlTriangles,
            static_cast<AeroGLsizei>(indexCount),
            kGlUnsignedInt,
            reinterpret_cast<const void*>(indexByteOffset));

        stats.drawCallsIssued += 1u;
        stats.quadsSubmitted += quadCount;
        stats.indicesSubmitted += indexCount;
        stats.trianglesSubmitted += quadCount * 2u;
    }

    if (scissorCurrentlyEnabled) {
        gl_.Disable(kGlScissorTest);
    }

    telemetry_.framesPresented += 1u;
    stats.frameIndex = telemetry_.framesPresented;
    stats.succeeded = true;

    telemetry_.totalDirtySpansUploaded += stats.dirtySpansUploaded;
    telemetry_.totalBytesUploaded += stats.bytesUploaded;
    telemetry_.totalDrawCallsIssued += stats.drawCallsIssued;
    telemetry_.totalQuadsSubmitted += stats.quadsSubmitted;
    telemetry_.totalTrianglesSubmitted += stats.trianglesSubmitted;
    telemetry_.lastFrame = stats;
    return stats;
}

bool OpenGlDeviceBackend::ReadbackFramebufferRgba8(
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight) const {
    if (!initialized_ || !gl_.ReadPixels || lastViewportWidth_ == 0u || lastViewportHeight_ == 0u) {
        return false;
    }
    outWidth = lastViewportWidth_;
    outHeight = lastViewportHeight_;
    const std::size_t rowBytes = static_cast<std::size_t>(outWidth) * 4u;
    std::vector<std::uint8_t> bottomUp(rowBytes * outHeight, 0u);
    if (gl_.PixelStorei) {
        gl_.PixelStorei(kGlPackAlignment, 1);
    }
    gl_.ReadPixels(
        0,
        0,
        static_cast<AeroGLsizei>(outWidth),
        static_cast<AeroGLsizei>(outHeight),
        kGlRgba,
        kGlUnsignedByte,
        bottomUp.data());

    // Flip OpenGL bottom-up rows into AeroGUI top-down RGBA8 layout
    outRgba8.resize(bottomUp.size());
    for (std::uint32_t y = 0; y < outHeight; ++y) {
        const std::uint32_t srcY = (outHeight - 1u) - y;
        std::memcpy(
            outRgba8.data() + static_cast<std::size_t>(y) * rowBytes,
            bottomUp.data() + static_cast<std::size_t>(srcY) * rowBytes,
            rowBytes);
    }
    return true;
}

// ============================================================================
// 2. SoftwareSurfaceBackend Implementation
// ============================================================================

bool SoftwareSurfaceBackend::Initialize(const GpuBackendConfig& config) {
    Shutdown();
    shaderBackend_ = config.shaderBackend;

    const std::string_view vsSrc = ZeroCopyGpuStream::UberVertexShaderSource(shaderBackend_);
    const std::string_view psSrc = ZeroCopyGpuStream::UberPixelShaderSource(shaderBackend_);
    if (!CompileAndLinkUberShader(vsSrc, psSrc)) {
        return false;
    }

    if (!AllocateRingVertexBuffer(std::max<std::size_t>(16u, config.maxPagesInRing))) {
        return false;
    }

    const std::size_t minQuads = std::max<std::size_t>(
        config.initialQuadCapacity,
        maxPagesInRing_ * kQuadsPerPage);
    if (!EnsureQuadIndexCapacity(minQuads)) {
        return false;
    }

    if (config.autoBuildDefaultAtlas) {
        PagedRenderTree refTree;
        std::vector<std::uint8_t> defaultAtlas;
        BuildDefaultUiAtlasRgba8(refTree, config.defaultAtlasSize, defaultAtlas);
        (void)UploadAtlasTexture(config.defaultAtlasSize, config.defaultAtlasSize, defaultAtlas.data());
    }

    framebufferWidth_ = config.initialViewportWidth;
    framebufferHeight_ = config.initialViewportHeight;
    initialized_ = true;
    diagnostics_ = "SoftwareSurfaceBackend initialized (CPU RGBA8 UberShader fallback active).";
    return true;
}

void SoftwareSurfaceBackend::Shutdown() noexcept {
    initialized_ = false;
    shaderValidated_ = false;
    maxPagesInRing_ = 0u;
    quadCapacity_ = 0u;
    atlasWidth_ = 0u;
    atlasHeight_ = 0u;
    framebufferWidth_ = 0u;
    framebufferHeight_ = 0u;
    lastRenderedTreeId_ = 0u;
    ringMirrorBytes_.clear();
    quadIndexMirror_.clear();
    atlasRgba8_.clear();
    framebufferRgba8_.clear();
}

bool SoftwareSurfaceBackend::CompileAndLinkUberShader(
    std::string_view vertexShaderSrc,
    std::string_view pixelShaderSrc) {
    if (vertexShaderSrc.empty() || pixelShaderSrc.empty()) {
        diagnostics_ = "SoftwareSurfaceBackend: empty shader source rejected.";
        shaderValidated_ = false;
        return false;
    }
    const bool hasMainEntry =
        (vertexShaderSrc.find("main") != std::string_view::npos ||
         vertexShaderSrc.find("vs_main") != std::string_view::npos) &&
        (pixelShaderSrc.find("main") != std::string_view::npos ||
         pixelShaderSrc.find("fs_main") != std::string_view::npos);
    if (!hasMainEntry) {
        diagnostics_ = "SoftwareSurfaceBackend: UberShader source missing entry point.";
        shaderValidated_ = false;
        return false;
    }
    shaderValidated_ = true;
    diagnostics_ = "SoftwareSurfaceBackend validated UberShader sources.";
    return true;
}

bool SoftwareSurfaceBackend::UploadAtlasTexture(
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) {
    if (width == 0u || height == 0u || !rgba8Pixels) {
        return false;
    }
    atlasWidth_ = width;
    atlasHeight_ = height;
    const std::size_t totalBytes = static_cast<std::size_t>(width) * height * 4u;
    atlasRgba8_.assign(rgba8Pixels, rgba8Pixels + totalBytes);
    telemetry_.atlasUploadCount += 1u;
    return true;
}

bool SoftwareSurfaceBackend::UpdateAtlasSubRegion(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) {
    if (!rgba8Pixels || width == 0u || height == 0u ||
        x + width > atlasWidth_ || y + height > atlasHeight_) {
        return false;
    }
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t dstOff =
            (static_cast<std::size_t>(y + row) * atlasWidth_ + x) * 4u;
        const std::size_t srcOff = static_cast<std::size_t>(row) * width * 4u;
        std::memcpy(atlasRgba8_.data() + dstOff, rgba8Pixels + srcOff, static_cast<std::size_t>(width) * 4u);
    }
    telemetry_.atlasSubUpdateCount += 1u;
    return true;
}

bool SoftwareSurfaceBackend::AllocateRingVertexBuffer(std::size_t maxPagesInRing) {
    if (maxPagesInRing == 0u) {
        return false;
    }
    maxPagesInRing_ = maxPagesInRing;
    ringMirrorBytes_.assign(maxPagesInRing_ * kPageVertexBytes, 0u);
    return true;
}

bool SoftwareSurfaceBackend::EnsureQuadIndexCapacity(std::size_t requiredQuadCount) {
    if (requiredQuadCount <= quadCapacity_ && !quadIndexMirror_.empty()) {
        return true;
    }
    std::size_t nextCap = std::max<std::size_t>(64u, quadCapacity_);
    while (nextCap < requiredQuadCount) {
        nextCap *= 2u;
    }
    if (quadCapacity_ == 0u) {
        nextCap = std::max<std::size_t>(64u, requiredQuadCount);
    }
    const bool expanded = (quadCapacity_ > 0u);
    BuildStaticQuadIndexBuffer(nextCap, quadIndexMirror_);
    quadCapacity_ = nextCap;
    if (expanded) {
        telemetry_.quadEboExpansionCount += 1u;
    }
    return true;
}

GpuBackendFrameStats SoftwareSurfaceBackend::ExecuteSubmission(
    const PagedRenderTree& tree,
    const std::vector<GlBufferSubDataSpan>& uploadSpans,
    const std::vector<GpuClipDrawBatch>& drawBatches,
    std::size_t ringPageCapacity,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    Color clearColor) {
    GpuBackendFrameStats stats{};
    if (!initialized_ || !shaderValidated_ || viewportWidth == 0u || viewportHeight == 0u) {
        return stats;
    }

    if (ringPageCapacity > maxPagesInRing_) {
        (void)AllocateRingVertexBuffer(ringPageCapacity);
    }
    const std::size_t effRingPages = std::max<std::size_t>(1u, maxPagesInRing_);

    for (const GlBufferSubDataSpan& span : uploadSpans) {
        if (!span.sourcePageVertexPtr || span.byteSize == 0u) {
            continue;
        }
        const std::size_t slotIdx = span.pageIndex % effRingPages;
        const std::size_t dstOffset = slotIdx * kPageVertexBytes;
        const std::size_t clampedBytes = std::min<std::size_t>(span.byteSize, kPageVertexBytes);
        if (dstOffset + kPageVertexBytes <= ringMirrorBytes_.size()) {
            std::memcpy(ringMirrorBytes_.data() + dstOffset, span.sourcePageVertexPtr, clampedBytes);
            if (clampedBytes < kPageVertexBytes) {
                std::memset(
                    ringMirrorBytes_.data() + dstOffset + clampedBytes,
                    0,
                    kPageVertexBytes - clampedBytes);
            }
        }
        stats.dirtySpansUploaded += 1u;
        stats.bytesUploaded += clampedBytes;
    }

    const std::uint32_t expansionsBefore = telemetry_.quadEboExpansionCount;
    for (const GpuClipDrawBatch& batch : drawBatches) {
        const std::size_t firstSlot = batch.firstPageIndex % effRingPages;
        const std::size_t firstQuad = firstSlot * kQuadsPerPage;
        const std::size_t quadCount = batch.vertexCount / 4u;
        if (quadCount == 0u) {
            continue;
        }
        (void)EnsureQuadIndexCapacity(firstQuad + quadCount);
        stats.clipBatchesProcessed += 1u;
        if (batch.scissorActive) {
            stats.scissorBatchCount += 1u;
            const GlScissorBox sc = ComputeGlScissorBox(batch.scissorRect, viewportWidth, viewportHeight);
            if (sc.width <= 0 || sc.height <= 0) {
                continue;
            }
        }
        stats.drawCallsIssued += 1u;
        stats.quadsSubmitted += quadCount;
        stats.indicesSubmitted += quadCount * 6u;
        stats.trianglesSubmitted += quadCount * 2u;
    }
    stats.quadEboExpanded = (telemetry_.quadEboExpansionCount > expansionsBefore);

    const bool viewportChanged =
        (framebufferWidth_ != viewportWidth) ||
        (framebufferHeight_ != viewportHeight) ||
        (framebufferRgba8_.size() != static_cast<std::size_t>(viewportWidth) * viewportHeight * 4u);
    const bool clearChanged = !(lastClearColor_ == clearColor);
    const bool treeChanged = (lastRenderedTreeId_ != tree.TreeId());

    if (stats.dirtySpansUploaded > 0u || viewportChanged || clearChanged || treeChanged || framebufferRgba8_.empty()) {
        ZeroCopyGpuStream::RasterizeTreeToRgba8(
            tree,
            viewportWidth,
            viewportHeight,
            framebufferRgba8_,
            clearColor);
        framebufferWidth_ = viewportWidth;
        framebufferHeight_ = viewportHeight;
        lastClearColor_ = clearColor;
        lastRenderedTreeId_ = tree.TreeId();
    }

    telemetry_.framesPresented += 1u;
    stats.frameIndex = telemetry_.framesPresented;
    stats.succeeded = true;

    telemetry_.totalDirtySpansUploaded += stats.dirtySpansUploaded;
    telemetry_.totalBytesUploaded += stats.bytesUploaded;
    telemetry_.totalDrawCallsIssued += stats.drawCallsIssued;
    telemetry_.totalQuadsSubmitted += stats.quadsSubmitted;
    telemetry_.totalTrianglesSubmitted += stats.trianglesSubmitted;
    telemetry_.lastFrame = stats;
    return stats;
}

bool SoftwareSurfaceBackend::ReadbackFramebufferRgba8(
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight) const {
    if (!initialized_ || framebufferRgba8_.empty() || framebufferWidth_ == 0u || framebufferHeight_ == 0u) {
        return false;
    }
    outWidth = framebufferWidth_;
    outHeight = framebufferHeight_;
    outRgba8 = framebufferRgba8_;
    return true;
}

std::unique_ptr<IGpuDeviceBackend> CreateBestAvailableGpuBackend(
    const GpuBackendConfig& config,
    std::string* outDiagnostics) {
    if (config.preferHardwareGpu) {
        const GlDynamicProbeResult probe = ProbeOpenGlDynamicLibrary();
        if (probe.hasCurrentContext) {
            auto glBackend = std::make_unique<OpenGlDeviceBackend>();
            if (glBackend->Initialize(config)) {
                if (outDiagnostics) {
                    *outDiagnostics = std::string(glBackend->LastDiagnostics()) + " [" + probe.diagnostics + "]";
                }
                return glBackend;
            }
        }
        auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
        (void)swBackend->Initialize(config);
        if (outDiagnostics) {
            *outDiagnostics = "Gracefully degraded to SoftwareSurfaceBackend (" + probe.diagnostics + ")";
        }
        return swBackend;
    }

    auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
    (void)swBackend->Initialize(config);
    if (outDiagnostics) {
        *outDiagnostics = std::string(swBackend->LastDiagnostics());
    }
    return swBackend;
}

} // namespace aero
