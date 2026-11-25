#include "aero/render/GpuBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

#include "RenderInternal.hpp"

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
constexpr AeroGLenum kGlTexture1 = 0x84C1u;
constexpr AeroGLenum kGlArrayBuffer = 0x8892u;
constexpr std::uint32_t kUnifiedUiAtlasSize = 1024u;
constexpr AeroGLenum kGlElementArrayBuffer = 0x8893u;
constexpr AeroGLenum kGlStaticDraw = 0x88E4u;
constexpr AeroGLenum kGlDynamicDraw = 0x88E8u;
constexpr AeroGLenum kGlFragmentShader = 0x8B30u;
constexpr AeroGLenum kGlVertexShader = 0x8B31u;
constexpr AeroGLenum kGlCompileStatus = 0x8B81u;
constexpr AeroGLenum kGlLinkStatus = 0x8B82u;
constexpr AeroGLenum kGlInfoLogLength = 0x8B84u;
constexpr AeroGLenum kGlFramebuffer = 0x8D40u;
constexpr AeroGLenum kGlColorAttachment0 = 0x8CE0u;
constexpr AeroGLenum kGlFramebufferComplete = 0x8CD5u;
constexpr AeroGLbitfield kGlColorBufferBit = 0x00004000u;

constexpr std::string_view kEffectVertexShaderBodyGlsl = R"(layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in uint a_PackedRgba;
layout(location = 3) in uint a_PrimFlags;

uniform vec2 u_ViewportSize;

out vec2 v_Uv;
out vec4 v_Color;
out vec2 v_PhysPos;

void main() {
    vec2 ndc = (a_Position / max(u_ViewportSize, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_Uv = a_TexCoord;
    v_PhysPos = a_Position;
    v_Color = vec4(
        float((a_PackedRgba >>  0u) & 0xFFu) / 255.0,
        float((a_PackedRgba >>  8u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 16u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 24u) & 0xFFu) / 255.0
    );
}
)";

constexpr std::string_view kEffectPixelShaderBodyGlsl = R"(in vec2 v_Uv;
in vec4 v_Color;
in vec2 v_PhysPos;

uniform sampler2D u_SrcTex;
uniform sampler2D u_OrigTex;
uniform vec2 u_TexelSize;
uniform int u_EffectMode;
uniform vec2 u_EffectParams;
uniform vec2 u_ShadowOffsetUv;
uniform vec2 u_EffectUvMin;
uniform vec2 u_EffectUvMax;
uniform vec4 u_MaskMatRow0;
uniform vec4 u_MaskMatRow1;
uniform vec4 u_MaskSizeGrad;
uniform vec4 u_MaskGradPts;
uniform vec4 u_MaskStopOffsets;
uniform vec4 u_MaskStopAlphas;
uniform int u_MaskStopCount;

out vec4 o_FragColor;

vec4 sampleClamped(vec2 uv) {
    return texture(u_SrcTex, clamp(uv, u_EffectUvMin, u_EffectUvMax));
}

vec4 sampleInBounds(vec2 uv) {
    if (uv.x < u_EffectUvMin.x || uv.x > u_EffectUvMax.x ||
        uv.y < u_EffectUvMin.y || uv.y > u_EffectUvMax.y) {
        return vec4(0.0);
    }
    return texture(u_SrcTex, uv);
}

void main() {
    if (u_EffectMode == 1) {
        float radius = clamp(u_EffectParams.x, 0.5, 256.0);
        float sigma = max(0.35, radius * 0.38);
        float stepPx = max(1.0, radius / 12.0);
        vec4 acc = vec4(0.0);
        float wSum = 0.0;
        for (int k = -12; k <= 12; ++k) {
            float d = float(k) * stepPx;
            if (abs(d) > radius + 0.5) {
                continue;
            }
            float w = exp(-0.5 * (d * d) / (sigma * sigma));
            acc += sampleClamped(v_Uv + u_TexelSize * d) * w;
            wSum += w;
        }
        vec4 res = (wSum > 0.0) ? (acc / wSum) : sampleClamped(v_Uv);
        o_FragColor = res * v_Color.a;
        return;
    }
    if (u_EffectMode == 8) {
        vec2 baseUv = v_Uv + u_ShadowOffsetUv;
        float radius = clamp(u_EffectParams.x, 0.0, 256.0);
        float blurA = 0.0;
        if (radius <= 0.5) {
            blurA = sampleInBounds(baseUv).a;
        } else {
            float sigma = max(0.35, radius * 0.38);
            float stepPx = max(1.0, radius / 12.0);
            float accA = 0.0;
            float wSum = 0.0;
            for (int k = -12; k <= 12; ++k) {
                float d = float(k) * stepPx;
                if (abs(d) > radius + 0.5) {
                    continue;
                }
                float w = exp(-0.5 * (d * d) / (sigma * sigma));
                accA += sampleInBounds(baseUv + u_TexelSize * d).a * w;
                wSum += w;
            }
            blurA = (wSum > 0.0) ? (accA / wSum) : sampleInBounds(baseUv).a;
        }
        o_FragColor = vec4(0.0, 0.0, 0.0, clamp(blurA, 0.0, 1.0));
        return;
    }
    if (u_EffectMode == 9) {
        float radius = clamp(u_EffectParams.x, 0.0, 256.0);
        float blurA = 0.0;
        if (radius <= 0.5) {
            blurA = sampleInBounds(v_Uv).a;
        } else {
            float sigma = max(0.35, radius * 0.38);
            float stepPx = max(1.0, radius / 12.0);
            float accA = 0.0;
            float wSum = 0.0;
            for (int k = -12; k <= 12; ++k) {
                float d = float(k) * stepPx;
                if (abs(d) > radius + 0.5) {
                    continue;
                }
                float w = exp(-0.5 * (d * d) / (sigma * sigma));
                accA += sampleInBounds(v_Uv + u_TexelSize * d).a * w;
                wSum += w;
            }
            blurA = (wSum > 0.0) ? (accA / wSum) : sampleInBounds(v_Uv).a;
        }
        vec4 src = texture(u_OrigTex, clamp(v_Uv, u_EffectUvMin, u_EffectUvMax));
        float shA = clamp(blurA * v_Color.a, 0.0, 1.0);
        vec4 shadow = vec4(v_Color.rgb * shA, shA);
        vec4 combined = src + shadow * (1.0 - clamp(src.a, 0.0, 1.0));
        o_FragColor = combined * clamp(u_EffectParams.y, 0.0, 1.0);
        return;
    }
    if (u_EffectMode == 2) {
        float radius = clamp(u_EffectParams.x, 0.5, 128.0);
        float stepPx = radius / 6.0;
        vec4 acc = vec4(0.0);
        for (int k = -6; k <= 6; ++k) {
            acc += sampleClamped(v_Uv + u_TexelSize * (float(k) * stepPx));
        }
        o_FragColor = (acc / 13.0) * v_Color.a;
        return;
    }
    if (u_EffectMode == 3) {
        float blockPx = max(1.5, u_EffectParams.x);
        vec2 cellUv = max(u_TexelSize * blockPx, vec2(1e-6));
        vec2 snapped = floor((v_Uv - u_EffectUvMin) / cellUv) * cellUv + 0.5 * cellUv + u_EffectUvMin;
        o_FragColor = sampleClamped(min(snapped, u_EffectUvMax)) * v_Color.a;
        return;
    }
    vec4 c = sampleClamped(v_Uv);
    if (u_EffectMode == 4) {
        float strength = clamp(u_EffectParams.x, 0.0, 1.0);
        c.rgb = clamp(c.rgb * mix(vec3(1.0), v_Color.rgb, strength), vec3(0.0), vec3(c.a));
    } else if (u_EffectMode == 5) {
        float shift = clamp(u_EffectParams.x, -64.0, 64.0);
        vec4 sR = sampleClamped(v_Uv - u_TexelSize * shift);
        vec4 sB = sampleClamped(v_Uv + u_TexelSize * shift);
        float outA = max(c.a, max(sR.a, sB.a));
        c = vec4(min(sR.r, outA), min(c.g, outA), min(sB.b, outA), outA);
    } else if (u_EffectMode == 6) {
        float sat = max(0.0, u_EffectParams.x);
        if (c.a > 1e-4) {
            vec3 straight = c.rgb / c.a;
            float lum = dot(straight, vec3(0.2126, 0.7152, 0.0722));
            vec3 satRgb = clamp(mix(vec3(lum), straight, sat), 0.0, 1.0);
            c.rgb = satRgb * c.a;
        }
    } else if (u_EffectMode == 7) {
        if (u_MaskStopCount <= 0) {
            c *= clamp(u_EffectParams.x, 0.0, 1.0);
        } else {
            vec2 localPt = vec2(
                dot(u_MaskMatRow0.xyz, vec3(v_PhysPos, 1.0)),
                dot(u_MaskMatRow1.xyz, vec3(v_PhysPos, 1.0)));
            float normU = (localPt.x - u_MaskMatRow0.w) / max(1e-4, u_MaskSizeGrad.x);
            float normV = (localPt.y - u_MaskMatRow1.w) / max(1e-4, u_MaskSizeGrad.y);
            bool isRadialMask = (u_MaskSizeGrad.z >= 1.5);
            float absMapping = isRadialMask ? (u_MaskSizeGrad.z - 2.0) : u_MaskSizeGrad.z;
            vec2 samplePos = (absMapping > 0.5)
                ? vec2(normU * u_MaskSizeGrad.x, normV * u_MaskSizeGrad.y)
                : vec2(normU, normV);
            float rawT = 0.0;
            if (isRadialMask) {
                vec2 rel = (samplePos - u_MaskGradPts.xy) / max(u_MaskGradPts.zw, vec2(1e-5));
                rawT = length(rel);
            } else {
                vec2 d = u_MaskGradPts.zw - u_MaskGradPts.xy;
                float lenSq = dot(d, d);
                rawT = (lenSq > 1e-8) ? dot(samplePos - u_MaskGradPts.xy, d) / lenSq : 0.0;
            }
            float t01 = clamp(rawT, 0.0, 1.0);
            if (u_MaskSizeGrad.w > 1.5) {
                t01 = clamp(rawT - floor(rawT), 0.0, 1.0);
            } else if (u_MaskSizeGrad.w > 0.5) {
                float m = mod(abs(rawT), 2.0);
                t01 = clamp((m > 1.0) ? (2.0 - m) : m, 0.0, 1.0);
            }
            float maskA = u_MaskStopAlphas.x;
            int sc = min(u_MaskStopCount, 4);
            float lastOff = u_MaskStopOffsets.x;
            float lastA = u_MaskStopAlphas.x;
            if (sc >= 2) { lastOff = u_MaskStopOffsets.y; lastA = u_MaskStopAlphas.y; }
            if (sc >= 3) { lastOff = u_MaskStopOffsets.z; lastA = u_MaskStopAlphas.z; }
            if (sc >= 4) { lastOff = u_MaskStopOffsets.w; lastA = u_MaskStopAlphas.w; }
            if (t01 <= u_MaskStopOffsets.x || sc == 1) {
                maskA = u_MaskStopAlphas.x;
            } else if (t01 >= lastOff) {
                maskA = lastA;
            } else if (sc >= 2 && t01 <= u_MaskStopOffsets.y) {
                float sp = u_MaskStopOffsets.y - u_MaskStopOffsets.x;
                float f = (sp > 1e-6) ? clamp((t01 - u_MaskStopOffsets.x) / sp, 0.0, 1.0) : 0.0;
                maskA = mix(u_MaskStopAlphas.x, u_MaskStopAlphas.y, f);
            } else if (sc >= 3 && t01 <= u_MaskStopOffsets.z) {
                float sp = u_MaskStopOffsets.z - u_MaskStopOffsets.y;
                float f = (sp > 1e-6) ? clamp((t01 - u_MaskStopOffsets.y) / sp, 0.0, 1.0) : 0.0;
                maskA = mix(u_MaskStopAlphas.y, u_MaskStopAlphas.z, f);
            } else if (sc >= 4) {
                float sp = u_MaskStopOffsets.w - u_MaskStopOffsets.z;
                float f = (sp > 1e-6) ? clamp((t01 - u_MaskStopOffsets.z) / sp, 0.0, 1.0) : 0.0;
                maskA = mix(u_MaskStopAlphas.z, u_MaskStopAlphas.w, f);
            }
            c *= clamp(maskA * u_EffectParams.x, 0.0, 1.0);
        }
    }
    o_FragColor = c * v_Color.a;
}
)";

[[nodiscard]] std::string BuildEffectShaderSource(ShaderBackend backend, std::string_view body) {
    if (backend == ShaderBackend::WebGL2) {
        return std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + std::string(body);
    }
    return std::string("#version 330 core\n") + std::string(body);
}

constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
constexpr std::size_t kPageClipWBytes = kVerticesPerCommandPage * sizeof(float);
constexpr std::size_t kQuadsPerPage = kVerticesPerCommandPage / 4u;

struct EffectQuadBufferInit {
    AeroVertex verts[4]{};
    float clipW[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

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

void BakeProceduralGlyphHalf(
    const PagedRenderTree& referenceTree,
    std::uint32_t safeSize,
    std::vector<std::uint8_t>& outRgba8) {
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
}

[[nodiscard]] const GradientBrushRegistry& ResolveGradientRegistry(
    const PagedRenderTree* optTree) noexcept {
    if (optTree != nullptr) {
        if (const UIResourceStore* store = optTree->ResourceStore();
            store != nullptr && store->GradientBrushes().BrushCount() > 0u) {
            return store->GradientBrushes();
        }
    }
    return GradientBrushRegistry::Instance();
}

[[nodiscard]] const FontAtlas& ResolveFontAtlas(const PagedRenderTree* optTree) noexcept {
    if (optTree != nullptr) {
        if (const UIResourceStore* store = optTree->ResourceStore();
            store != nullptr && store->TryFonts() != nullptr) {
            return *store->TryFonts();
        }
    }
    return FontAtlas::Instance();
}

void BakeGradientRowsIntoBuffer(
    const GradientBrushRegistry& registry,
    std::uint32_t atlasSize,
    std::uint32_t bufferStartRow,
    std::uint32_t bufferRowCount,
    std::uint32_t minAllowedRow,
    std::uint8_t* bufferRgba8,
    std::uint32_t* outLowestWrittenRow = nullptr) {
    if (!bufferRgba8 || atlasSize == 0u || bufferRowCount == 0u) {
        if (outLowestWrittenRow) {
            *outLowestWrittenRow = (atlasSize > 0u) ? (atlasSize - 1u) : 0u;
        }
        return;
    }

    // Fill neutral opaque white texel at bottom-right corner (atlasSize - 1, atlasSize - 1) for untextured fallbacks
    const std::uint32_t bottomRow = atlasSize - 1u;
    if (bottomRow >= bufferStartRow && (bottomRow - bufferStartRow) < bufferRowCount) {
        const std::size_t lastTexel =
            (static_cast<std::size_t>(bottomRow - bufferStartRow) * atlasSize + (atlasSize - 1u)) * 4u;
        bufferRgba8[lastTexel + 0u] = 255u;
        bufferRgba8[lastTexel + 1u] = 255u;
        bufferRgba8[lastTexel + 2u] = 255u;
        bufferRgba8[lastTexel + 3u] = 255u;
    }

    auto writeFloatTexel = [&](std::uint32_t tx, std::uint32_t atlasRow, float val) noexcept {
        if (tx >= atlasSize || atlasRow < bufferStartRow || (atlasRow - bufferStartRow) >= bufferRowCount) {
            return;
        }
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &val, sizeof(float));
        const std::size_t off =
            (static_cast<std::size_t>(atlasRow - bufferStartRow) * atlasSize + tx) * 4u;
        bufferRgba8[off + 0u] = static_cast<std::uint8_t>(bits & 0xFFu);
        bufferRgba8[off + 1u] = static_cast<std::uint8_t>((bits >> 8u) & 0xFFu);
        bufferRgba8[off + 2u] = static_cast<std::uint8_t>((bits >> 16u) & 0xFFu);
        bufferRgba8[off + 3u] = static_cast<std::uint8_t>((bits >> 24u) & 0xFFu);
    };

    std::uint32_t lowestRow = bottomRow;
    const std::size_t brushCount = registry.BrushCount();
    GradientEvalPod brushDesc{};
    for (std::size_t idx = 0; idx < brushCount; ++idx) {
        if (atlasSize < 4u + static_cast<std::uint32_t>(idx * 2u)) {
            break;
        }
        const std::uint32_t rampRow = atlasSize - 2u - static_cast<std::uint32_t>(idx * 2u);
        if (rampRow == 0u || rampRow - 1u < minAllowedRow) {
            break;
        }
        const std::uint32_t metaRow = rampRow - 1u;
        lowestRow = std::min(lowestRow, metaRow);

        const auto brushId = static_cast<std::uint32_t>(idx + 1u);
        if (!registry.TryGetEvalPod(brushId, brushDesc)) {
            continue;
        }

        if (rampRow >= bufferStartRow && (rampRow - bufferStartRow) < bufferRowCount) {
            const std::size_t localRampRow = static_cast<std::size_t>(rampRow - bufferStartRow);
            const std::uint32_t rampWidth = std::min<std::uint32_t>(atlasSize, GradientEvalPod::kLutWidth);
            if (rampWidth == GradientEvalPod::kLutWidth) {
                std::memcpy(
                    bufferRgba8 + localRampRow * atlasSize * 4u,
                    brushDesc.bakedRampRgba8.data(),
                    GradientEvalPod::kLutWidth * 4u);
            } else if (rampWidth > 1u) {
                for (std::uint32_t x = 0; x < rampWidth; ++x) {
                    const float t = static_cast<float>(x) / static_cast<float>(rampWidth - 1u);
                    const Color c = SampleGradientPodAtT(brushDesc, t);
                    const std::uint32_t packed = PackColorRgba8(c, 1.0f);
                    const std::size_t off = (localRampRow * atlasSize + x) * 4u;
                    bufferRgba8[off + 0u] = static_cast<std::uint8_t>(packed & 0xFFu);
                    bufferRgba8[off + 1u] = static_cast<std::uint8_t>((packed >> 8u) & 0xFFu);
                    bufferRgba8[off + 2u] = static_cast<std::uint8_t>((packed >> 16u) & 0xFFu);
                    bufferRgba8[off + 3u] = static_cast<std::uint8_t>((packed >> 24u) & 0xFFu);
                }
            }
        }

        if (metaRow >= bufferStartRow && (metaRow - bufferStartRow) < bufferRowCount) {
            const std::size_t m0Off = static_cast<std::size_t>(metaRow - bufferStartRow) * atlasSize * 4u;
            bufferRgba8[m0Off + 0u] = static_cast<std::uint8_t>(brushDesc.kind);
            bufferRgba8[m0Off + 1u] = static_cast<std::uint8_t>(brushDesc.mappingMode);
            bufferRgba8[m0Off + 2u] = static_cast<std::uint8_t>(brushDesc.spreadMethod);
            bufferRgba8[m0Off + 3u] = static_cast<std::uint8_t>(
                std::clamp(brushDesc.opacity, 0.0f, 1.0f) * 255.0f + 0.5f);

            if (brushDesc.kind == GradientBrushKind::Linear) {
                writeFloatTexel(1u, metaRow, brushDesc.startPoint.x);
                writeFloatTexel(2u, metaRow, brushDesc.startPoint.y);
                writeFloatTexel(3u, metaRow, brushDesc.endPoint.x);
                writeFloatTexel(4u, metaRow, brushDesc.endPoint.y);
            } else {
                writeFloatTexel(1u, metaRow, brushDesc.center.x);
                writeFloatTexel(2u, metaRow, brushDesc.center.y);
                writeFloatTexel(3u, metaRow, brushDesc.gradientOrigin.x);
                writeFloatTexel(4u, metaRow, brushDesc.gradientOrigin.y);
            }
            writeFloatTexel(5u, metaRow, brushDesc.radiusX);
            writeFloatTexel(6u, metaRow, brushDesc.radiusY);
        }
    }

    if (outLowestWrittenRow) {
        *outLowestWrittenRow = lowestRow;
    }
}

void BuildUnifiedUiAtlas1024(
    const PagedRenderTree& referenceTree,
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t* outLowestGradRow = nullptr) {
    outRgba8.assign(static_cast<std::size_t>(kUnifiedUiAtlasSize) * kUnifiedUiAtlasSize * 4u, 0u);

    // 1. Top half [y: 0..511]: live FontAtlas SDF glyphs (built-in ASCII + CJK + loaded TrueType + AGFS shards)
    std::vector<std::uint8_t> fontRegion;
    std::uint32_t fontW = 0u;
    std::uint32_t fontRows = 0u;
    ResolveFontAtlas(&referenceTree).CopyGlyphRegionRgba8(512u, fontRegion, fontW, fontRows);
    if (fontW == kUnifiedUiAtlasSize && fontRows > 0u) {
        const std::size_t copyBytes =
            static_cast<std::size_t>(kUnifiedUiAtlasSize) * std::min<std::uint32_t>(512u, fontRows) * 4u;
        std::memcpy(outRgba8.data(), fontRegion.data(), copyBytes);
    } else {
        BakeProceduralGlyphHalf(referenceTree, kUnifiedUiAtlasSize, outRgba8);
    }

    // 2. Lower half [y: 512..959]: PagedRenderTree 4-channel MTSDF vector icon cells
    const std::uint32_t mtsdfRows = std::min<std::uint32_t>(448u, referenceTree.MtsdfAtlasPopulatedRows());
    if (mtsdfRows > 0u) {
        const auto& mtsdfBuf = referenceTree.MtsdfAtlasRgba8();
        const std::size_t mtsdfBytes = static_cast<std::size_t>(mtsdfRows) * kUnifiedUiAtlasSize * 4u;
        if (mtsdfBuf.size() >= mtsdfBytes) {
            std::memcpy(
                outRgba8.data() + static_cast<std::size_t>(512u) * kUnifiedUiAtlasSize * 4u,
                mtsdfBuf.data(),
                mtsdfBytes);
        }
    }

    // 3. Bottom band [y: minAllowedRow..1022]: GradientBrushRegistry 1D LUT & float32 metadata rows + (1023,1023) white texel
    const std::uint32_t minAllowedRow = 512u + mtsdfRows;
    BakeGradientRowsIntoBuffer(
        ResolveGradientRegistry(&referenceTree),
        kUnifiedUiAtlasSize,
        0u,
        kUnifiedUiAtlasSize,
        minAllowedRow,
        outRgba8.data(),
        outLowestGradRow);
}

template <typename FnPtr>
void AssignRawProc(FnPtr& outTyped, void* rawSym) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(void*), "Function pointer must match pointer width");
    std::memcpy(&outTyped, &rawSym, sizeof(void*));
}

struct DynamicGlLibrarySession {
    void* glHandle = nullptr;
    void* eglHandle = nullptr;
    void* glesHandle = nullptr;
    bool loadedGl = false;
    bool loadedEgl = false;
    bool resolverReady = false;
    bool activeContext = false;
    std::string primaryLibName;

#if defined(_WIN32)
    PROC(WINAPI* wglGetProc)(LPCSTR) = nullptr;
    HGLRC(WINAPI* wglGetCur)() = nullptr;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    using GlxGetProcFn = void (*(*)(const std::uint8_t*))();
    using EglGetProcFn = void (*(*)(const char*))();
    using GetCurCtxFn = void* (*)();
    GlxGetProcFn glxGetProc = nullptr;
    EglGetProcFn eglGetProc = nullptr;
    GetCurCtxFn glxGetCur = nullptr;
    GetCurCtxFn eglGetCur = nullptr;
#endif

    bool Open(bool includeFallbackEglAndGles, std::string& outError) noexcept {
        outError.clear();
#if defined(__EMSCRIPTEN__)
        (void)includeFallbackEglAndGles;
        loadedGl = true;
        resolverReady = true;
        primaryLibName = "Emscripten-WebGL2";
        activeContext = (glGetString(GL_VERSION) != nullptr);
        return true;
#elif defined(_WIN32)
        (void)includeFallbackEglAndGles;
        HMODULE glMod = LoadLibraryA("opengl32.dll");
        if (!glMod) {
            outError = "Win32 opengl32.dll unavailable.";
            return false;
        }
        glHandle = static_cast<void*>(glMod);
        loadedGl = true;
        primaryLibName = "opengl32.dll";
        wglGetProc = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(GetProcAddress(glMod, "wglGetProcAddress"));
        wglGetCur = reinterpret_cast<HGLRC(WINAPI*)()>(GetProcAddress(glMod, "wglGetCurrentContext"));
        resolverReady = (wglGetProc != nullptr);
        activeContext = (wglGetCur != nullptr && wglGetCur() != nullptr);
        return true;
#elif defined(__linux__)
        glHandle = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (glHandle) {
            loadedGl = true;
            primaryLibName = "libGL.so.1";
        } else {
            glHandle = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
            if (glHandle) {
                loadedGl = true;
                primaryLibName = "libGL.so";
            }
        }
        eglHandle = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!eglHandle && includeFallbackEglAndGles) {
            eglHandle = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (eglHandle) {
            loadedEgl = true;
            if (primaryLibName.empty()) {
                primaryLibName = "libEGL.so.1";
            }
        }
        if (includeFallbackEglAndGles) {
            glesHandle = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
        }
        if (!glHandle && !eglHandle && !glesHandle) {
            outError = includeFallbackEglAndGles
                ? "dlopen failed for libGL.so.1, libEGL.so.1, and libGLESv2.so.2."
                : "Neither libGL.so.1 nor libEGL.so.1 found via dlopen.";
            return false;
        }
        if (glHandle) {
            AssignRawProc(glxGetProc, dlsym(glHandle, "glXGetProcAddressARB"));
            if (!glxGetProc) {
                AssignRawProc(glxGetProc, dlsym(glHandle, "glXGetProcAddress"));
            }
            AssignRawProc(glxGetCur, dlsym(glHandle, "glXGetCurrentContext"));
        }
        if (eglHandle) {
            AssignRawProc(eglGetProc, dlsym(eglHandle, "eglGetProcAddress"));
            AssignRawProc(eglGetCur, dlsym(eglHandle, "eglGetCurrentContext"));
        }
        resolverReady = (glxGetProc != nullptr) || (eglGetProc != nullptr);
        activeContext = (glxGetCur && glxGetCur() != nullptr) || (eglGetCur && eglGetCur() != nullptr);
        return true;
#else
        (void)includeFallbackEglAndGles;
        outError = "OpenGL dynamic probe unsupported on target OS.";
        return false;
#endif
    }

    [[nodiscard]] void* ResolveSymbol(const char* name, bool useExtGetProcAddress) const noexcept {
#if defined(_WIN32)
        void* p = nullptr;
        if (useExtGetProcAddress && wglGetProc) {
            PROC extP = wglGetProc(name);
            const auto rawVal = reinterpret_cast<std::uintptr_t>(extP);
            if (rawVal > 3u && rawVal != static_cast<std::uintptr_t>(-1)) {
                p = reinterpret_cast<void*>(extP);
            }
        }
        if (!p && glHandle) {
            p = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(glHandle), name));
        }
        return p;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
        (void)useExtGetProcAddress;
        void* sym = nullptr;
        if (glHandle) {
            sym = dlsym(glHandle, name);
        }
        if (!sym && glesHandle) {
            sym = dlsym(glesHandle, name);
        }
        if (!sym && glxGetProc) {
            auto fn = glxGetProc(reinterpret_cast<const std::uint8_t*>(name));
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        if (!sym && eglGetProc) {
            auto fn = eglGetProc(name);
            AssignRawProc(sym, reinterpret_cast<void*>(fn));
        }
        return sym;
#else
        (void)name;
        (void)useExtGetProcAddress;
        return nullptr;
#endif
    }

    void Close() noexcept {
#if defined(_WIN32)
        if (glHandle) {
            FreeLibrary(static_cast<HMODULE>(glHandle));
            glHandle = nullptr;
        }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
        if (glesHandle) {
            dlclose(glesHandle);
            glesHandle = nullptr;
        }
        if (eglHandle) {
            dlclose(eglHandle);
            eglHandle = nullptr;
        }
        if (glHandle) {
            dlclose(glHandle);
            glHandle = nullptr;
        }
#endif
    }

    void TransferHandlesTo(void*& outGl, void*& outEgl, void*& outGles) noexcept {
        outGl = glHandle;
        outEgl = eglHandle;
        outGles = glesHandle;
        glHandle = nullptr;
        eglHandle = nullptr;
        glesHandle = nullptr;
    }
};

void PopulateGlFunctionTableFromSession(
    const DynamicGlLibrarySession& session,
    GlFunctionTable& table) noexcept {
    auto bind = [&](auto& outFn, const char* name) noexcept {
        AssignRawProc(outFn, session.ResolveSymbol(name, true));
    };
    bind(table.Viewport, "glViewport");
    bind(table.Scissor, "glScissor");
    bind(table.Enable, "glEnable");
    bind(table.Disable, "glDisable");
    bind(table.BlendFuncSeparate, "glBlendFuncSeparate");
    bind(table.ClearColor, "glClearColor");
    bind(table.Clear, "glClear");
    bind(table.GenBuffers, "glGenBuffers");
    bind(table.DeleteBuffers, "glDeleteBuffers");
    bind(table.BindBuffer, "glBindBuffer");
    bind(table.BufferData, "glBufferData");
    bind(table.BufferSubData, "glBufferSubData");
    bind(table.GenVertexArrays, "glGenVertexArrays");
    bind(table.DeleteVertexArrays, "glDeleteVertexArrays");
    bind(table.BindVertexArray, "glBindVertexArray");
    bind(table.EnableVertexAttribArray, "glEnableVertexAttribArray");
    bind(table.VertexAttribPointer, "glVertexAttribPointer");
    bind(table.VertexAttribIPointer, "glVertexAttribIPointer");
    bind(table.CreateShader, "glCreateShader");
    bind(table.DeleteShader, "glDeleteShader");
    bind(table.ShaderSource, "glShaderSource");
    bind(table.CompileShader, "glCompileShader");
    bind(table.GetShaderiv, "glGetShaderiv");
    bind(table.GetShaderInfoLog, "glGetShaderInfoLog");
    bind(table.CreateProgram, "glCreateProgram");
    bind(table.DeleteProgram, "glDeleteProgram");
    bind(table.AttachShader, "glAttachShader");
    bind(table.LinkProgram, "glLinkProgram");
    bind(table.GetProgramiv, "glGetProgramiv");
    bind(table.GetProgramInfoLog, "glGetProgramInfoLog");
    bind(table.UseProgram, "glUseProgram");
    bind(table.GetUniformLocation, "glGetUniformLocation");
    bind(table.Uniform1i, "glUniform1i");
    bind(table.Uniform2f, "glUniform2f");
    bind(table.Uniform4f, "glUniform4f");
    bind(table.GenTextures, "glGenTextures");
    bind(table.DeleteTextures, "glDeleteTextures");
    bind(table.ActiveTexture, "glActiveTexture");
    bind(table.BindTexture, "glBindTexture");
    bind(table.TexImage2D, "glTexImage2D");
    bind(table.TexSubImage2D, "glTexSubImage2D");
    bind(table.TexParameteri, "glTexParameteri");
    bind(table.PixelStorei, "glPixelStorei");
    bind(table.DrawElements, "glDrawElements");
    bind(table.ReadPixels, "glReadPixels");
    bind(table.GetError, "glGetError");
    bind(table.GetString, "glGetString");
    bind(table.GenFramebuffers, "glGenFramebuffers");
    bind(table.DeleteFramebuffers, "glDeleteFramebuffers");
    bind(table.BindFramebuffer, "glBindFramebuffer");
    bind(table.FramebufferTexture2D, "glFramebufferTexture2D");
    bind(table.CheckFramebufferStatus, "glCheckFramebufferStatus");
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

bool GlFunctionTable::HasFramebufferSupport() const noexcept {
    return GenFramebuffers != nullptr &&
           DeleteFramebuffers != nullptr &&
           BindFramebuffer != nullptr &&
           FramebufferTexture2D != nullptr &&
           CheckFramebufferStatus != nullptr;
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

[[nodiscard]] Rect ScaleRectByDpi(const Rect& r, float dpiScale) noexcept {
    return Rect{r.x * dpiScale, r.y * dpiScale, r.width * dpiScale, r.height * dpiScale};
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
    if (safeSize == kUnifiedUiAtlasSize) {
        BuildUnifiedUiAtlas1024(referenceTree, outRgba8, nullptr);
        return;
    }

    outRgba8.assign(static_cast<std::size_t>(safeSize) * safeSize * 4u, 0u);
    BakeProceduralGlyphHalf(referenceTree, safeSize, outRgba8);
    BakeGradientRowsIntoBuffer(
        ResolveGradientRegistry(&referenceTree),
        safeSize,
        0u,
        safeSize,
        safeSize / 2u,
        outRgba8.data(),
        nullptr);
}

GlDynamicProbeResult ProbeOpenGlDynamicLibrary() noexcept {
    GlDynamicProbeResult res{};
    DynamicGlLibrarySession session;
    std::string openErr;
    if (!session.Open(false, openErr)) {
        res.diagnostics = std::move(openErr);
        return res;
    }

    res.libGlLoaded = session.loadedGl;
    res.libEglLoaded = session.loadedEgl;
    res.procAddressResolverReady = session.resolverReady;
    res.hasCurrentContext = session.activeContext;
    res.loadedLibraryName = session.primaryLibName;

#if defined(__EMSCRIPTEN__)
    res.coreEntryPointsFound = true;
    res.resolvedSymbolCount = 43u;
    if (const auto* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION))) {
        res.glVersionString = ver;
    }
    res.diagnostics = "Emscripten WebGL2 static-import symbols ready.";
#elif defined(_WIN32)
    const char* kCoreNames[] = {
        "glViewport", "glScissor", "glEnable", "glDisable",
        "glClearColor", "glClear", "glDrawElements", "glGetString"
    };
    for (const char* sym : kCoreNames) {
        if (session.ResolveSymbol(sym, false) != nullptr) {
            ++res.resolvedSymbolCount;
        }
    }
    res.coreEntryPointsFound = (res.resolvedSymbolCount >= 8u);
    res.diagnostics = res.hasCurrentContext
        ? "Win32 opengl32.dll loaded with active WGL context."
        : "Win32 opengl32.dll loaded (no active WGL context on calling thread).";
#elif defined(__linux__)
    const char* kProbeSymbols[] = {
        "glViewport", "glScissor", "glEnable", "glDisable",
        "glBlendFuncSeparate", "glClearColor", "glClear",
        "glGenBuffers", "glBindBuffer", "glBufferData", "glBufferSubData",
        "glGenVertexArrays", "glBindVertexArray", "glVertexAttribPointer", "glVertexAttribIPointer",
        "glCreateShader", "glCompileShader", "glCreateProgram", "glLinkProgram", "glUseProgram",
        "glGenTextures", "glBindTexture", "glTexImage2D", "glTexSubImage2D", "glDrawElements"
    };
    for (const char* name : kProbeSymbols) {
        if (session.ResolveSymbol(name, false) != nullptr) {
            ++res.resolvedSymbolCount;
        }
    }
    res.coreEntryPointsFound = (res.resolvedSymbolCount >= 20u);
    if (res.hasCurrentContext) {
        const std::uint8_t* (AERO_GL_APIENTRY *glGetStr)(AeroGLenum) = nullptr;
        AssignRawProc(glGetStr, session.ResolveSymbol("glGetString", false));
        if (glGetStr) {
            if (const auto* v = glGetStr(kGlVersion)) {
                res.glVersionString = reinterpret_cast<const char*>(v);
            }
            if (const auto* r = glGetStr(kGlRenderer)) {
                res.glRendererString = reinterpret_cast<const char*>(r);
            }
        }
    }
    res.diagnostics = "Linux dlopen probe succeeded (" + res.loadedLibraryName +
        ", resolved " + std::to_string(res.resolvedSymbolCount) + "/25 probe symbols, activeContext=" +
        (res.hasCurrentContext ? "true" : "false") + ").";
#endif
    session.Close();
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
    if (filePath.empty() || width == 0u || height == 0u || !rgba8Pixels || height > 0x7FFFFFFFu) {
        return false;
    }
    const std::uint64_t pixelBytes64 =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (pixelBytes64 > (0xFFFFFFFFULL - 54ULL)) {
        return false;
    }
    const std::string pathStr(filePath);
    std::FILE* fp = std::fopen(pathStr.c_str(), "wb");
    if (!fp) {
        return false;
    }

    const auto pixelDataBytes = static_cast<std::uint32_t>(pixelBytes64);
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
    gl_.GenFramebuffers = &glGenFramebuffers;
    gl_.DeleteFramebuffers = &glDeleteFramebuffers;
    gl_.BindFramebuffer = &glBindFramebuffer;
    gl_.FramebufferTexture2D = &glFramebufferTexture2D;
    gl_.CheckFramebufferStatus = &glCheckFramebufferStatus;
    return gl_.IsComplete();
#elif defined(_WIN32) || defined(__linux__)
    DynamicGlLibrarySession session;
    std::string openErr;
    if (!session.Open(true, openErr)) {
        diagnostics_ = std::move(openErr);
        return false;
    }
    session.TransferHandlesTo(dynGlLibHandle_, dynEglLibHandle_, dynGlesLibHandle_);
    if (!session.activeContext) {
#if defined(_WIN32)
        diagnostics_ = "No active WGL context on current thread when initializing OpenGlDeviceBackend.";
#else
        diagnostics_ = "OpenGL dynamic libraries loaded, but no active GLX/EGL context is bound on this thread.";
#endif
        return false;
    }

    DynamicGlLibrarySession lookupSession{};
    lookupSession.glHandle = dynGlLibHandle_;
    lookupSession.eglHandle = dynEglLibHandle_;
    lookupSession.glesHandle = dynGlesLibHandle_;
#if defined(_WIN32)
    lookupSession.wglGetProc = session.wglGetProc;
#endif
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    lookupSession.glxGetProc = session.glxGetProc;
    lookupSession.eglGetProc = session.eglGetProc;
#endif
    PopulateGlFunctionTableFromSession(lookupSession, gl_);
    if (!gl_.IsComplete()) {
        diagnostics_ = "Failed to resolve complete OpenGL 3.3 / GLES3 symbol table.";
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
        const std::uint64_t fontRev = FontAtlas::Instance().Revision();
        const std::uint32_t gradRev = GradientBrushRegistry::Instance().Revision();
        std::uint32_t lowestGradRow = kUnifiedUiAtlasSize - 1u;
        std::vector<std::uint8_t> defaultAtlas;
        if (config.defaultAtlasSize == kUnifiedUiAtlasSize) {
            BuildUnifiedUiAtlas1024(refTree, defaultAtlas, &lowestGradRow);
        } else {
            BuildDefaultUiAtlasRgba8(refTree, config.defaultAtlasSize, defaultAtlas);
        }
        if (!UploadAtlasTexture(
                config.defaultAtlasSize,
                config.defaultAtlasSize,
                defaultAtlas.data())) {
            Shutdown();
            return false;
        }
        if (config.defaultAtlasSize == kUnifiedUiAtlasSize) {
            syncedUiAtlasReady_ = true;
            syncedFontRevision_ = fontRev;
            syncedMtsdfOwnerId_ = refTree.MtsdfAtlasOwnerId();
            syncedMtsdfRevision_ = refTree.MtsdfAtlasRevision();
            syncedMtsdfRows_ = 0u;
            syncedGradientRevision_ = gradRev;
            syncedMinGradRow_ = lowestGradRow;
        }
    }

    (void)EnsureFallbackWhiteTexture();

    lastViewportWidth_ = config.initialViewportWidth;
    lastViewportHeight_ = config.initialViewportHeight;
    initialized_ = true;
    diagnostics_ = "OpenGlDeviceBackend initialized successfully.";
    return true;
}

void OpenGlDeviceBackend::DestroyEffectGpuResources() noexcept {
    if (gl_.DeleteFramebuffers) {
        for (AeroGLuint& fboId : effectFboIds_) {
            if (fboId != 0u) {
                gl_.DeleteFramebuffers(1, &fboId);
                fboId = 0u;
            }
        }
    } else {
        effectFboIds_[0] = 0u;
        effectFboIds_[1] = 0u;
    }
    if (gl_.DeleteTextures) {
        for (AeroGLuint& texId : effectFboTexIds_) {
            if (texId != 0u) {
                gl_.DeleteTextures(1, &texId);
                texId = 0u;
            }
        }
    } else {
        effectFboTexIds_[0] = 0u;
        effectFboTexIds_[1] = 0u;
    }
    if (gl_.DeleteProgram && effectProgramId_ != 0u) {
        gl_.DeleteProgram(effectProgramId_);
    }
    effectProgramId_ = 0u;
    effectFboW_ = 0u;
    effectFboH_ = 0u;
    uEffectViewportSizeLoc_ = -1;
    uEffectSrcTexLoc_ = -1;
    uEffectOrigTexLoc_ = -1;
    uEffectTexelSizeLoc_ = -1;
    uEffectModeLoc_ = -1;
    uEffectParamsLoc_ = -1;
    uEffectShadowOffsetLoc_ = -1;
    uEffectUvMinLoc_ = -1;
    uEffectUvMaxLoc_ = -1;
}

void OpenGlDeviceBackend::Shutdown() noexcept {
    DestroyEffectGpuResources();
    if (gl_.IsComplete()) {
        for (const GlImageSlotState& slot : imageSlotTextures_) {
            if (slot.texId != 0u && !slot.isExternal) {
                gl_.DeleteTextures(1, &slot.texId);
            }
        }
        if (fallbackWhiteTexId_ != 0u) {
            gl_.DeleteTextures(1, &fallbackWhiteTexId_);
        }
        if (effectScratchTexId_ != 0u) {
            gl_.DeleteTextures(1, &effectScratchTexId_);
        }
        if (atlasTextureId_ != 0u) {
            gl_.DeleteTextures(1, &atlasTextureId_);
        }
        if (effectQuadVboId_ != 0u) {
            gl_.DeleteBuffers(1, &effectQuadVboId_);
        }
        if (quadEboId_ != 0u) {
            gl_.DeleteBuffers(1, &quadEboId_);
        }
        if (ringVboId_ != 0u) {
            gl_.DeleteBuffers(1, &ringVboId_);
        }
        if (clipWRingVboId_ != 0u) {
            gl_.DeleteBuffers(1, &clipWRingVboId_);
        }
        if (vaoId_ != 0u) {
            gl_.DeleteVertexArrays(1, &vaoId_);
        }
        if (programId_ != 0u) {
            gl_.DeleteProgram(programId_);
        }
    }

    atlasTextureId_ = 0u;
    fallbackWhiteTexId_ = 0u;
    effectScratchTexId_ = 0u;
    effectQuadVboId_ = 0u;
    effectScratchTexW_ = 0u;
    effectScratchTexH_ = 0u;
    effectGpuPipelineFailed_ = false;
    quadEboId_ = 0u;
    ringVboId_ = 0u;
    clipWRingVboId_ = 0u;
    vaoId_ = 0u;
    programId_ = 0u;
    uViewportSizeLoc_ = -1;
    uQuadHalfExtentLoc_ = -1;
    uAtlasSamplerLoc_ = -1;
    uImageSamplerLoc_ = -1;
    maxPagesInRing_ = 0u;
    ringBufferBytes_ = 0u;
    clipWRingBufferBytes_ = 0u;
    quadCapacity_ = 0u;
    atlasWidth_ = 0u;
    atlasHeight_ = 0u;
    syncedUiAtlasReady_ = false;
    syncedFontRevision_ = 0u;
    syncedMtsdfOwnerId_ = 0u;
    syncedMtsdfRevision_ = 0u;
    syncedMtsdfRows_ = 0u;
    syncedGradientRevision_ = 0u;
    syncedMinGradRow_ = kUnifiedUiAtlasSize - 1u;
    syncedImageAtlasSweepRev_ = 0u;
    imageSlotTextures_.clear();
    ringSlotTailZeroed_.clear();
    ringSlotHasCustomClipW_.clear();
    pageNeedsTailPad_.clear();
    uiAtlasScratchRgba8_.clear();
    imageUploadScratchRgba8_.clear();
    effectReadbackBottomUp_.clear();
    effectRowScratch_.clear();
    effectPatchScratch_.clear();
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

bool OpenGlDeviceBackend::CompileShaderProgramInternal(
    std::string_view vertexShaderSrc,
    std::string_view pixelShaderSrc,
    const char* shaderLabelPrefix,
    AeroGLuint& outProgramId) {
    outProgramId = 0u;
    if (!gl_.IsComplete()) {
        diagnostics_ = "Cannot compile shader: GL function table is incomplete.";
        return false;
    }
    if (vertexShaderSrc.empty() || pixelShaderSrc.empty()) {
        diagnostics_ = "Cannot compile shader: empty vertex or fragment shader source.";
        return false;
    }

    auto compileStage = [&](AeroGLenum stageType, std::string_view src, const char* stageSuffix, AeroGLuint& outShader) -> bool {
        const AeroGLuint sh = gl_.CreateShader(stageType);
        if (sh == 0u) {
            diagnostics_ = std::string("glCreateShader failed for ") + shaderLabelPrefix + stageSuffix;
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
            diagnostics_ = std::string(shaderLabelPrefix) + stageSuffix + " compile error: " + infoLog;
            gl_.DeleteShader(sh);
            return false;
        }
        outShader = sh;
        return true;
    };

    AeroGLuint vs = 0u;
    if (!compileStage(kGlVertexShader, vertexShaderSrc, "VertexShader", vs)) {
        return false;
    }

    AeroGLuint fs = 0u;
    if (!compileStage(kGlFragmentShader, pixelShaderSrc, "PixelShader", fs)) {
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
        diagnostics_ = std::string(shaderLabelPrefix) + " program link error: " + infoLog;
        gl_.DeleteProgram(prog);
        return false;
    }

    outProgramId = prog;
    return true;
}

bool OpenGlDeviceBackend::CompileAndLinkUberShader(
    std::string_view vertexShaderSrc,
    std::string_view pixelShaderSrc) {
    AeroGLuint prog = 0u;
    if (!CompileShaderProgramInternal(vertexShaderSrc, pixelShaderSrc, "Uber", prog)) {
        return false;
    }

    if (programId_ != 0u) {
        gl_.DeleteProgram(programId_);
    }
    programId_ = prog;
    uViewportSizeLoc_ = gl_.GetUniformLocation(programId_, "u_ViewportSize");
    uQuadHalfExtentLoc_ = gl_.GetUniformLocation(programId_, "u_QuadHalfExtent");
    uAtlasSamplerLoc_ = gl_.GetUniformLocation(programId_, "u_GlyphAndUiAtlas");
    uImageSamplerLoc_ = gl_.GetUniformLocation(programId_, "u_ImageAtlas");
    gl_.UseProgram(programId_);
    if (uAtlasSamplerLoc_ >= 0) {
        gl_.Uniform1i(uAtlasSamplerLoc_, 0);
    }
    if (uImageSamplerLoc_ >= 0) {
        gl_.Uniform1i(uImageSamplerLoc_, 1);
    }
    diagnostics_ = "UberShader compiled and linked cleanly.";
    return true;
}

bool OpenGlDeviceBackend::EnsureEffectGpuResources(
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) {
    if (!initialized_ || !gl_.IsComplete() || !gl_.HasFramebufferSupport() ||
        effectGpuPipelineFailed_ || viewportWidth == 0u || viewportHeight == 0u) {
        return false;
    }

    if (effectProgramId_ == 0u) {
        const std::string vsSrc = BuildEffectShaderSource(shaderBackend_, kEffectVertexShaderBodyGlsl);
        const std::string psSrc = BuildEffectShaderSource(shaderBackend_, kEffectPixelShaderBodyGlsl);
        AeroGLuint prog = 0u;
        if (!CompileShaderProgramInternal(vsSrc, psSrc, "EffectFbo", prog)) {
            effectGpuPipelineFailed_ = true;
            return false;
        }
        effectProgramId_ = prog;
        uEffectViewportSizeLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_ViewportSize");
        uEffectSrcTexLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_SrcTex");
        uEffectOrigTexLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_OrigTex");
        uEffectTexelSizeLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_TexelSize");
        uEffectModeLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_EffectMode");
        uEffectParamsLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_EffectParams");
        uEffectShadowOffsetLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_ShadowOffsetUv");
        uEffectUvMinLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_EffectUvMin");
        uEffectUvMaxLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_EffectUvMax");
        uEffectMaskMatRow0Loc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskMatRow0");
        uEffectMaskMatRow1Loc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskMatRow1");
        uEffectMaskSizeGradLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskSizeGrad");
        uEffectMaskGradPtsLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskGradPts");
        uEffectMaskStopOffsetsLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskStopOffsets");
        uEffectMaskStopAlphasLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskStopAlphas");
        uEffectMaskStopCountLoc_ = gl_.GetUniformLocation(effectProgramId_, "u_MaskStopCount");
    }

    if (effectQuadVboId_ == 0u) {
        gl_.GenBuffers(1, &effectQuadVboId_);
        if (effectQuadVboId_ == 0u) {
            return false;
        }
        const EffectQuadBufferInit initBuf{};
        gl_.BindBuffer(kGlArrayBuffer, effectQuadVboId_);
        gl_.BufferData(
            kGlArrayBuffer,
            static_cast<AeroGLsizeiptr>(sizeof(initBuf)),
            &initBuf,
            kGlDynamicDraw);
    }

    if (effectFboIds_[0] == 0u || effectFboIds_[1] == 0u) {
        gl_.GenFramebuffers(2, effectFboIds_);
        gl_.GenTextures(2, effectFboTexIds_);
        if (effectFboIds_[0] == 0u || effectFboIds_[1] == 0u ||
            effectFboTexIds_[0] == 0u || effectFboTexIds_[1] == 0u) {
            DestroyEffectGpuResources();
            return false;
        }
        effectFboW_ = 0u;
        effectFboH_ = 0u;
    }

    if (effectFboW_ != viewportWidth || effectFboH_ != viewportHeight) {
        gl_.ActiveTexture(kGlTexture0);
        for (int i = 0; i < 2; ++i) {
            gl_.BindTexture(kGlTexture2D, effectFboTexIds_[i]);
            gl_.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<AeroGLint>(kGlLinear));
            gl_.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<AeroGLint>(kGlLinear));
            gl_.TexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<AeroGLint>(kGlClampToEdge));
            gl_.TexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<AeroGLint>(kGlClampToEdge));
            gl_.TexImage2D(
                kGlTexture2D,
                0,
                static_cast<AeroGLint>(kGlRgba8),
                static_cast<AeroGLsizei>(viewportWidth),
                static_cast<AeroGLsizei>(viewportHeight),
                0,
                kGlRgba,
                kGlUnsignedByte,
                nullptr);
            gl_.BindFramebuffer(kGlFramebuffer, effectFboIds_[i]);
            gl_.FramebufferTexture2D(
                kGlFramebuffer,
                kGlColorAttachment0,
                kGlTexture2D,
                effectFboTexIds_[i],
                0);
            if (gl_.CheckFramebufferStatus(kGlFramebuffer) != kGlFramebufferComplete) {
                gl_.BindFramebuffer(kGlFramebuffer, 0u);
                DestroyEffectGpuResources();
                effectGpuPipelineFailed_ = true;
                return false;
            }
        }
        gl_.BindFramebuffer(kGlFramebuffer, 0u);
        gl_.BindTexture(kGlTexture2D, atlasTextureId_);
        effectFboW_ = viewportWidth;
        effectFboH_ = viewportHeight;
    }

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
    syncedUiAtlasReady_ = false;
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
    if (clipWRingVboId_ == 0u) {
        gl_.GenBuffers(1, &clipWRingVboId_);
        if (clipWRingVboId_ == 0u) {
            diagnostics_ = "glGenBuffers failed for clipW ring VBO.";
            return false;
        }
    }

    maxPagesInRing_ = maxPagesInRing;
    ringBufferBytes_ = maxPagesInRing_ * kPageVertexBytes;
    clipWRingBufferBytes_ = maxPagesInRing_ * kPageClipWBytes;
    if (vaoId_ != 0u) {
        gl_.BindVertexArray(vaoId_);
    }
    gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
    gl_.BufferData(
        kGlArrayBuffer,
        static_cast<AeroGLsizeiptr>(ringBufferBytes_),
        nullptr,
        kGlDynamicDraw);

    std::vector<float> defaultClipW(maxPagesInRing_ * kVerticesPerCommandPage, 1.0f);
    gl_.BindBuffer(kGlArrayBuffer, clipWRingVboId_);
    gl_.BufferData(
        kGlArrayBuffer,
        static_cast<AeroGLsizeiptr>(clipWRingBufferBytes_),
        defaultClipW.data(),
        kGlDynamicDraw);

    ringSlotTailZeroed_.assign(maxPagesInRing_, 0u);
    ringSlotHasCustomClipW_.assign(maxPagesInRing_, 0u);

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
    if (clipWRingVboId_ != 0u) {
        // layout(location = 4) in float a_ClipW (sidecar homogeneous clip-space W)
        gl_.BindBuffer(kGlArrayBuffer, clipWRingVboId_);
        gl_.EnableVertexAttribArray(4u);
        gl_.VertexAttribPointer(4u, 1, kGlFloat, 0u, static_cast<AeroGLsizei>(sizeof(float)), nullptr);
        gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
    }
}

void OpenGlDeviceBackend::SyncUnifiedUiAtlas(const PagedRenderTree& tree) {
    if (!gl_.IsComplete()) {
        return;
    }

    const FontAtlas& fontAtlas = ResolveFontAtlas(&tree);
    const GradientBrushRegistry& gradReg = ResolveGradientRegistry(&tree);

    auto trimAtlasScratch = [&]() {
        if (uiAtlasScratchRgba8_.capacity() > 65536u) {
            std::vector<std::uint8_t>().swap(uiAtlasScratchRgba8_);
        }
    };

    if (!syncedUiAtlasReady_ ||
        atlasTextureId_ == 0u ||
        atlasWidth_ != kUnifiedUiAtlasSize ||
        atlasHeight_ != kUnifiedUiAtlasSize) {
        const std::uint64_t fontRev = fontAtlas.Revision();
        const std::uint32_t gradRev = gradReg.Revision();
        std::uint32_t lowestGradRow = kUnifiedUiAtlasSize - 1u;
        BuildUnifiedUiAtlas1024(tree, uiAtlasScratchRgba8_, &lowestGradRow);
        if (UploadAtlasTexture(kUnifiedUiAtlasSize, kUnifiedUiAtlasSize, uiAtlasScratchRgba8_.data())) {
            syncedUiAtlasReady_ = true;
            syncedFontRevision_ = fontRev;
            syncedMtsdfOwnerId_ = tree.MtsdfAtlasOwnerId();
            syncedMtsdfRevision_ = tree.MtsdfAtlasRevision();
            syncedMtsdfRows_ = std::min<std::uint32_t>(448u, tree.MtsdfAtlasPopulatedRows());
            syncedGradientRevision_ = gradRev;
            syncedMinGradRow_ = lowestGradRow;
        }
        trimAtlasScratch();
        return;
    }

    // 1. Incremental FontAtlas SDF glyph band sync [y: 0..511]
    const std::uint64_t curFontRev = fontAtlas.Revision();
    if (curFontRev != syncedFontRevision_) {
        std::uint32_t fontW = 0u;
        std::uint32_t fontYOff = 0u;
        std::uint32_t fontRows = 0u;
        bool fullBand = false;
        std::uint64_t snapRev = syncedFontRevision_;
        fontAtlas.CopyGlyphDirtyBandRgba8(
            512u, snapRev, uiAtlasScratchRgba8_, fontW, fontYOff, fontRows, fullBand);
        if (fontW == kUnifiedUiAtlasSize && fontRows > 0u && fontYOff < 512u) {
            const std::uint32_t clampedRows = std::min<std::uint32_t>(512u - fontYOff, fontRows);
            (void)UpdateAtlasSubRegion(
                0u,
                fontYOff,
                fontW,
                clampedRows,
                uiAtlasScratchRgba8_.data());
        }
        syncedFontRevision_ = snapRev;
        trimAtlasScratch();
    }

    // 2. Incremental MTSDF vector path band sync [y: 512..959]
    const std::uint32_t curMtsdfOwner = tree.MtsdfAtlasOwnerId();
    const std::uint64_t curMtsdfRev = tree.MtsdfAtlasRevision();
    const std::uint32_t newMtsdfRows = std::min<std::uint32_t>(448u, tree.MtsdfAtlasPopulatedRows());
    if (curMtsdfOwner != syncedMtsdfOwnerId_ || curMtsdfRev != syncedMtsdfRevision_) {
        const std::uint32_t dirtyRows = std::min<std::uint32_t>(448u, std::max(newMtsdfRows, syncedMtsdfRows_));
        if (dirtyRows > 0u) {
            const auto& mtsdfBuf = tree.MtsdfAtlasRgba8();
            const std::size_t neededBytes = static_cast<std::size_t>(dirtyRows) * kUnifiedUiAtlasSize * 4u;
            if (mtsdfBuf.size() >= neededBytes) {
                (void)UpdateAtlasSubRegion(0u, 512u, kUnifiedUiAtlasSize, dirtyRows, mtsdfBuf.data());
            } else {
                uiAtlasScratchRgba8_.assign(neededBytes, 0u);
                if (!mtsdfBuf.empty()) {
                    std::memcpy(
                        uiAtlasScratchRgba8_.data(),
                        mtsdfBuf.data(),
                        std::min(neededBytes, mtsdfBuf.size()));
                }
                (void)UpdateAtlasSubRegion(
                    0u,
                    512u,
                    kUnifiedUiAtlasSize,
                    dirtyRows,
                    uiAtlasScratchRgba8_.data());
                trimAtlasScratch();
            }
        }
        syncedMtsdfOwnerId_ = curMtsdfOwner;
        syncedMtsdfRevision_ = curMtsdfRev;
        syncedMtsdfRows_ = newMtsdfRows;
    }

    // 3. Incremental GradientBrushRegistry LUT & float32 metadata band sync [y: minAllowedRow..1023]
    const std::uint32_t curGradRev = gradReg.Revision();
    if (curGradRev != syncedGradientRevision_) {
        const std::uint32_t minAllowedRow = 512u + syncedMtsdfRows_;
        const std::size_t brushCount = gradReg.BrushCount();
        std::uint32_t estMinRow = kUnifiedUiAtlasSize - 1u;
        if (brushCount > 0u) {
            const std::uint32_t maxFitBrushes =
                (kUnifiedUiAtlasSize - 2u > minAllowedRow)
                    ? ((kUnifiedUiAtlasSize - 1u - minAllowedRow) / 2u)
                    : 0u;
            const std::uint32_t applied = static_cast<std::uint32_t>(
                std::min<std::size_t>(brushCount, maxFitBrushes));
            if (applied > 0u) {
                estMinRow = (kUnifiedUiAtlasSize - 2u - (applied - 1u) * 2u) - 1u;
            }
        }
        const std::uint32_t subStartY = std::max(minAllowedRow, std::min(estMinRow, syncedMinGradRow_));
        const std::uint32_t subRows = kUnifiedUiAtlasSize - subStartY;
        uiAtlasScratchRgba8_.assign(static_cast<std::size_t>(subRows) * kUnifiedUiAtlasSize * 4u, 0u);
        std::uint32_t newLowestRow = kUnifiedUiAtlasSize - 1u;
        BakeGradientRowsIntoBuffer(
            gradReg,
            kUnifiedUiAtlasSize,
            subStartY,
            subRows,
            minAllowedRow,
            uiAtlasScratchRgba8_.data(),
            &newLowestRow);
        (void)UpdateAtlasSubRegion(0u, subStartY, kUnifiedUiAtlasSize, subRows, uiAtlasScratchRgba8_.data());
        syncedMinGradRow_ = newLowestRow;
        syncedGradientRevision_ = curGradRev;
        trimAtlasScratch();
    }

    const UIResourceStore* optStore = tree.ResourceStore();
    const ImageAtlas& imageAtlas = (optStore != nullptr && optStore->TryImages() != nullptr)
        ? *optStore->TryImages()
        : ImageAtlas::Instance();
    const std::uint64_t curImgRev = imageAtlas.Revision();
    if (curImgRev != syncedImageAtlasSweepRev_) {
        syncedImageAtlasSweepRev_ = curImgRev;
        for (std::size_t tid = 1u; tid < imageSlotTextures_.size(); ++tid) {
            if (imageSlotTextures_[tid].texId != 0u &&
                !imageAtlas.HasTextureSlot(static_cast<std::uint32_t>(tid))) {
                if (!imageSlotTextures_[tid].isExternal && gl_.DeleteTextures) {
                    gl_.DeleteTextures(1, &imageSlotTextures_[tid].texId);
                }
                imageSlotTextures_[tid] = GlImageSlotState{};
            }
        }
    }
}

AeroGLuint OpenGlDeviceBackend::EnsureFallbackWhiteTexture() {
    if (fallbackWhiteTexId_ != 0u || !gl_.IsComplete()) {
        return fallbackWhiteTexId_;
    }
    gl_.GenTextures(1, &fallbackWhiteTexId_);
    if (fallbackWhiteTexId_ == 0u) {
        return atlasTextureId_;
    }
    constexpr std::uint8_t kWhitePixel[4]{255u, 255u, 255u, 255u};
    gl_.ActiveTexture(kGlTexture1);
    gl_.BindTexture(kGlTexture2D, fallbackWhiteTexId_);
    gl_.PixelStorei(kGlUnpackAlignment, 1);
    gl_.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<AeroGLint>(kGlLinear));
    gl_.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<AeroGLint>(kGlLinear));
    gl_.TexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<AeroGLint>(kGlClampToEdge));
    gl_.TexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<AeroGLint>(kGlClampToEdge));
    gl_.TexImage2D(
        kGlTexture2D,
        0,
        static_cast<AeroGLint>(kGlRgba8),
        1,
        1,
        0,
        kGlRgba,
        kGlUnsignedByte,
        kWhitePixel);
    gl_.ActiveTexture(kGlTexture0);
    return fallbackWhiteTexId_;
}

AeroGLuint OpenGlDeviceBackend::ResolveBatchImageTexture(
    std::uint32_t textureId,
    const UIResourceStore* optStore) {
    const AeroGLuint fallbackTex = EnsureFallbackWhiteTexture();
    if (!gl_.IsComplete() || textureId > 16384u) {
        return fallbackTex != 0u ? fallbackTex : atlasTextureId_;
    }

    const ImageAtlas& imageAtlas = (optStore != nullptr && optStore->TryImages() != nullptr)
        ? *optStore->TryImages()
        : ImageAtlas::Instance();
    const std::uint64_t globalRev = imageAtlas.Revision();
    if (textureId < imageSlotTextures_.size()) {
        const GlImageSlotState& cached = imageSlotTextures_[textureId];
        if (cached.texId != 0u && cached.lastGlobalRevision == globalRev) {
            return cached.texId;
        }
    }

    if (textureId >= imageSlotTextures_.size()) {
        imageSlotTextures_.resize(static_cast<std::size_t>(textureId) + 1u);
    }
    GlImageSlotState& slot = imageSlotTextures_[textureId];

    std::uint32_t extGpuHandle = 0u;
    std::uint32_t extW = 0u;
    std::uint32_t extH = 0u;
    if (imageAtlas.TryGetExternalGpuTextureHandle(textureId, extGpuHandle, extW, extH) && extGpuHandle != 0u) {
        if (slot.texId != 0u && !slot.isExternal && gl_.DeleteTextures) {
            gl_.DeleteTextures(1, &slot.texId);
        }
        slot.texId = static_cast<AeroGLuint>(extGpuHandle);
        slot.width = extW;
        slot.height = extH;
        slot.isExternal = true;
        slot.lastGlobalRevision = globalRev;
        return slot.texId;
    }
    if (slot.isExternal) {
        slot = GlImageSlotState{};
    }

    std::uint32_t slotW = 0u;
    std::uint32_t slotH = 0u;
    std::uint32_t dirtyX = 0u, dirtyY = 0u, dirtyW = 0u, dirtyH = 0u;
    bool wasUpdated = false;
    const bool needsInitialFull = (slot.texId == 0u);
    if (needsInitialFull) {
        slot.slotRevision = 0u;
    }
    if (!imageAtlas.CopyTextureSlotDirtySubRectIfModified(
            textureId,
            slot.slotRevision,
            slotW,
            slotH,
            dirtyX,
            dirtyY,
            dirtyW,
            dirtyH,
            imageUploadScratchRgba8_,
            wasUpdated) ||
        slotW == 0u ||
        slotH == 0u) {
        if (slot.texId != 0u) {
            if (!slot.isExternal && gl_.DeleteTextures) {
                gl_.DeleteTextures(1, &slot.texId);
            }
            slot = GlImageSlotState{};
        }
        return fallbackTex != 0u ? fallbackTex : atlasTextureId_;
    }

    slot.lastGlobalRevision = globalRev;
    const bool needsAlloc = (slot.texId == 0u) || (slot.width != slotW) || (slot.height != slotH);
    if (needsAlloc && (!wasUpdated || dirtyW != slotW || dirtyH != slotH)) {
        slot.slotRevision = 0u;
        if (!imageAtlas.CopyTextureSlotDirtySubRectIfModified(
                textureId,
                slot.slotRevision,
                slotW,
                slotH,
                dirtyX,
                dirtyY,
                dirtyW,
                dirtyH,
                imageUploadScratchRgba8_,
                wasUpdated)) {
            return fallbackTex != 0u ? fallbackTex : atlasTextureId_;
        }
    }

    if (needsAlloc || wasUpdated) {
        if (slot.texId == 0u) {
            gl_.GenTextures(1, &slot.texId);
            if (slot.texId == 0u) {
                return fallbackTex != 0u ? fallbackTex : atlasTextureId_;
            }
        }
        gl_.ActiveTexture(kGlTexture1);
        gl_.BindTexture(kGlTexture2D, slot.texId);
        gl_.PixelStorei(kGlUnpackAlignment, 1);
        if (needsAlloc) {
            gl_.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<AeroGLint>(kGlLinear));
            gl_.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<AeroGLint>(kGlLinear));
            gl_.TexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<AeroGLint>(kGlClampToEdge));
            gl_.TexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<AeroGLint>(kGlClampToEdge));
            gl_.TexImage2D(
                kGlTexture2D,
                0,
                static_cast<AeroGLint>(kGlRgba8),
                static_cast<AeroGLsizei>(slotW),
                static_cast<AeroGLsizei>(slotH),
                0,
                kGlRgba,
                kGlUnsignedByte,
                imageUploadScratchRgba8_.data());
            slot.width = slotW;
            slot.height = slotH;
        } else if (dirtyW > 0u && dirtyH > 0u) {
            gl_.TexSubImage2D(
                kGlTexture2D,
                0,
                static_cast<AeroGLint>(dirtyX),
                static_cast<AeroGLint>(dirtyY),
                static_cast<AeroGLsizei>(dirtyW),
                static_cast<AeroGLsizei>(dirtyH),
                kGlRgba,
                kGlUnsignedByte,
                imageUploadScratchRgba8_.data());
        }
    }
    if (imageUploadScratchRgba8_.capacity() > 256u * 1024u) {
        std::vector<std::uint8_t>().swap(imageUploadScratchRgba8_);
    }

    return slot.texId;
}

GpuBackendFrameStats OpenGlDeviceBackend::ExecuteSubmission(
    const PagedRenderTree& tree,
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

    const std::size_t targetRingPages = std::max(ringPageCapacity, tree.PageCount());
    bool ringReallocated = false;
    if (targetRingPages > maxPagesInRing_) {
        std::size_t nextRingPages = std::max<std::size_t>(16u, maxPagesInRing_);
        while (nextRingPages < targetRingPages) {
            nextRingPages *= 2u;
        }
        if (!AllocateRingVertexBuffer(nextRingPages)) {
            return stats;
        }
        ringReallocated = true;
    }

    const std::size_t effRingPages = std::max<std::size_t>(1u, maxPagesInRing_);
    if (ringSlotTailZeroed_.size() != effRingPages) {
        ringSlotTailZeroed_.assign(effRingPages, 0u);
    }
    if (ringSlotHasCustomClipW_.size() != effRingPages) {
        ringSlotHasCustomClipW_.assign(effRingPages, 0u);
    }
    lastViewportWidth_ = viewportWidth;
    lastViewportHeight_ = viewportHeight;

    // 1. Stream dirty 4KB CommandPage vertex spans into the Ring VBO via glBufferSubData
    std::vector<std::uint8_t> uploadedSlots;
    if (ringReallocated) {
        uploadedSlots.assign(effRingPages, 0u);
    }

    // Only interior pages of a multi-page coalesced GpuClipDrawBatch require trailing zero-padding;
    // single-page batches and the final page of a batch draw exact clampedVerts via glDrawElements!
    pageNeedsTailPad_.assign(tree.PageCount(), 0u);
    for (const GpuClipDrawBatch& batch : drawBatches) {
        if (batch.pageCount > 1u) {
            for (std::uint32_t i = 0u; i + 1u < batch.pageCount; ++i) {
                const std::size_t pIdx = static_cast<std::size_t>(batch.firstPageIndex) + i;
                if (pIdx < pageNeedsTailPad_.size()) {
                    pageNeedsTailPad_[pIdx] = 1u;
                }
            }
        }
    }

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

        const bool mustPadTail = (span.pageIndex < pageNeedsTailPad_.size() && pageNeedsTailPad_[span.pageIndex] != 0u);
        if (mustPadTail && clampedBytes < kPageVertexBytes) {
            std::uint8_t paddedPage[kPageVertexBytes]{};
            std::memcpy(paddedPage, span.sourcePageVertexPtr, clampedBytes);
            gl_.BufferSubData(
                kGlArrayBuffer,
                static_cast<AeroGLintptr>(dstOffset),
                static_cast<AeroGLsizeiptr>(kPageVertexBytes),
                paddedPage);
            ringSlotTailZeroed_[slotIdx] = 1u;
        } else {
            gl_.BufferSubData(
                kGlArrayBuffer,
                static_cast<AeroGLintptr>(dstOffset),
                static_cast<AeroGLsizeiptr>(clampedBytes),
                span.sourcePageVertexPtr);
            ringSlotTailZeroed_[slotIdx] = (clampedBytes >= kPageVertexBytes) ? 1u : 0u;
        }
        if (ringReallocated && slotIdx < uploadedSlots.size()) {
            uploadedSlots[slotIdx] = 1u;
        }
        stats.dirtySpansUploaded += 1u;
        stats.bytesUploaded += clampedBytes;
    }

    // If Ring VBO expanded this frame, re-upload any clean active pages from tree not already in uploadSpans
    if (ringReallocated) {
        const std::size_t treePages = tree.PageCount();
        for (std::size_t p = 0; p < treePages; ++p) {
            const std::size_t slotIdx = p % effRingPages;
            if (slotIdx < uploadedSlots.size() && uploadedSlots[slotIdx] != 0u) {
                continue;
            }
            const CommandPage& page = tree.GetPage(p);
            const std::size_t activeVerts = std::min<std::size_t>(
                page.header.vertexCount,
                kVerticesPerCommandPage);
            if (activeVerts == 0u) {
                continue;
            }
            const std::size_t activeBytes = activeVerts * sizeof(AeroVertex);
            const std::size_t dstOffset = slotIdx * kPageVertexBytes;
            const bool mustPadTail = (p < pageNeedsTailPad_.size() && pageNeedsTailPad_[p] != 0u);
            if (mustPadTail && activeBytes < kPageVertexBytes && dstOffset + kPageVertexBytes <= ringBufferBytes_) {
                std::uint8_t paddedPage[kPageVertexBytes]{};
                std::memcpy(paddedPage, page.vertices.data(), activeBytes);
                gl_.BufferSubData(
                    kGlArrayBuffer,
                    static_cast<AeroGLintptr>(dstOffset),
                    static_cast<AeroGLsizeiptr>(kPageVertexBytes),
                    paddedPage);
                ringSlotTailZeroed_[slotIdx] = 1u;
                stats.dirtySpansUploaded += 1u;
                stats.bytesUploaded += activeBytes;
            } else if (dstOffset + activeBytes <= ringBufferBytes_) {
                gl_.BufferSubData(
                    kGlArrayBuffer,
                    static_cast<AeroGLintptr>(dstOffset),
                    static_cast<AeroGLsizeiptr>(activeBytes),
                    page.vertices.data());
                ringSlotTailZeroed_[slotIdx] = (activeBytes >= kPageVertexBytes) ? 1u : 0u;
                stats.dirtySpansUploaded += 1u;
                stats.bytesUploaded += activeBytes;
            }
        }
    }

    // Ensure any pre-existing interior page of a newly coalesced multi-page batch has its tail zeroed
    for (std::size_t pIdx = 0u; pIdx < pageNeedsTailPad_.size(); ++pIdx) {
        if (pageNeedsTailPad_[pIdx] == 0u) {
            continue;
        }
        const std::size_t slotIdx = pIdx % effRingPages;
        if (ringSlotTailZeroed_[slotIdx] != 0u) {
            continue;
        }
        const CommandPage& page = tree.GetPage(pIdx);
        const std::size_t activeVerts = std::min<std::size_t>(page.header.vertexCount, kVerticesPerCommandPage);
        const std::size_t activeBytes = activeVerts * sizeof(AeroVertex);
        const std::size_t dstOffset = slotIdx * kPageVertexBytes;
        if (activeBytes < kPageVertexBytes && dstOffset + kPageVertexBytes <= ringBufferBytes_) {
            static const std::uint8_t kZeroVertices[kPageVertexBytes]{};
            gl_.BufferSubData(
                kGlArrayBuffer,
                static_cast<AeroGLintptr>(dstOffset + activeBytes),
                static_cast<AeroGLsizeiptr>(kPageVertexBytes - activeBytes),
                kZeroVertices);
        }
        ringSlotTailZeroed_[slotIdx] = 1u;
    }

    bool needClipWUpload = false;
    for (const GlBufferSubDataSpan& span : uploadSpans) {
        if (!span.sourcePageVertexPtr || span.byteSize == 0u) {
            continue;
        }
        const std::size_t slotIdx = span.pageIndex % effRingPages;
        const bool pageHas3D = (span.pageIndex < tree.PageCount()) &&
            ((tree.GetPage(span.pageIndex).header.flags & CommandPageFlag_HasPerspective3D) != 0u);
        if (pageHas3D || ringSlotHasCustomClipW_[slotIdx] != 0u) {
            needClipWUpload = true;
            break;
        }
    }
    if (!needClipWUpload && ringReallocated) {
        const std::size_t treePages = tree.PageCount();
        for (std::size_t p = 0u; p < treePages; ++p) {
            if ((tree.GetPage(p).header.flags & CommandPageFlag_HasPerspective3D) != 0u) {
                needClipWUpload = true;
                break;
            }
        }
    }
    if (clipWRingVboId_ != 0u && needClipWUpload) {
        gl_.BindBuffer(kGlArrayBuffer, clipWRingVboId_);
        float paddedClipW[kVerticesPerCommandPage];
        for (const GlBufferSubDataSpan& span : uploadSpans) {
            if (!span.sourcePageVertexPtr || span.byteSize == 0u) {
                continue;
            }
            const std::size_t slotIdx = span.pageIndex % effRingPages;
            const bool pageHas3D = (span.pageIndex < tree.PageCount()) &&
                ((tree.GetPage(span.pageIndex).header.flags & CommandPageFlag_HasPerspective3D) != 0u);
            if (!pageHas3D && ringSlotHasCustomClipW_[slotIdx] == 0u) {
                continue;
            }
            const std::size_t dstOffset = slotIdx * kPageClipWBytes;
            if (dstOffset + kPageClipWBytes > clipWRingBufferBytes_) {
                continue;
            }
            if (!pageHas3D) {
                std::fill(std::begin(paddedClipW), std::end(paddedClipW), 1.0f);
                gl_.BufferSubData(
                    kGlArrayBuffer,
                    static_cast<AeroGLintptr>(dstOffset),
                    static_cast<AeroGLsizeiptr>(kPageClipWBytes),
                    paddedClipW);
                ringSlotHasCustomClipW_[slotIdx] = 0u;
                continue;
            }
            const float* srcClipW = span.sourcePageClipWPtr ? span.sourcePageClipWPtr : tree.PageVertexClipW(span.pageIndex);
            if (!srcClipW) {
                continue;
            }
            const std::size_t srcBytes = (span.clipWByteSize > 0u)
                ? span.clipWByteSize
                : ((span.byteSize / sizeof(AeroVertex)) * sizeof(float));
            const std::size_t clampedBytes = std::min<std::size_t>(srcBytes, kPageClipWBytes);
            if (clampedBytes < kPageClipWBytes) {
                std::fill(std::begin(paddedClipW), std::end(paddedClipW), 1.0f);
                if (clampedBytes > 0u) {
                    std::memcpy(paddedClipW, srcClipW, clampedBytes);
                }
                gl_.BufferSubData(
                    kGlArrayBuffer,
                    static_cast<AeroGLintptr>(dstOffset),
                    static_cast<AeroGLsizeiptr>(kPageClipWBytes),
                    paddedClipW);
            } else {
                gl_.BufferSubData(
                    kGlArrayBuffer,
                    static_cast<AeroGLintptr>(dstOffset),
                    static_cast<AeroGLsizeiptr>(clampedBytes),
                    srcClipW);
            }
            ringSlotHasCustomClipW_[slotIdx] = 1u;
        }
        if (ringReallocated) {
            const std::size_t treePages = tree.PageCount();
            for (std::size_t p = 0; p < treePages; ++p) {
                const std::size_t slotIdx = p % effRingPages;
                if (slotIdx < uploadedSlots.size() && uploadedSlots[slotIdx] != 0u) {
                    continue;
                }
                const CommandPage& page = tree.GetPage(p);
                if ((page.header.flags & CommandPageFlag_HasPerspective3D) == 0u) {
                    continue;
                }
                const std::size_t activeVerts = std::min<std::size_t>(
                    page.header.vertexCount,
                    kVerticesPerCommandPage);
                if (activeVerts == 0u) {
                    continue;
                }
                const float* srcClipW = tree.PageVertexClipW(p);
                const std::size_t dstOffset = slotIdx * kPageClipWBytes;
                if (srcClipW != nullptr && dstOffset + kPageClipWBytes <= clipWRingBufferBytes_) {
                    gl_.BufferSubData(
                        kGlArrayBuffer,
                        static_cast<AeroGLintptr>(dstOffset),
                        static_cast<AeroGLsizeiptr>(kPageClipWBytes),
                        srcClipW);
                    ringSlotHasCustomClipW_[slotIdx] = 1u;
                }
            }
        }
        gl_.BindBuffer(kGlArrayBuffer, ringVboId_);
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

    // 3. Synchronize Unit 0 (1024x1024 Glyph/MTSDF/Gradient UI Atlas) & Unit 1 (ImageAtlas sprites/standalone slots)
    SyncUnifiedUiAtlas(tree);

    const float dpiScale = std::clamp(
        (std::isfinite(tree.RootDpiScale()) && tree.RootDpiScale() > 0.0f) ? tree.RootDpiScale() : 1.0f,
        0.25f,
        8.0f);
    const float logicalVpW = std::max(1.0f, static_cast<float>(viewportWidth) / dpiScale);
    const float logicalVpH = std::max(1.0f, static_cast<float>(viewportHeight) / dpiScale);

    gl_.Viewport(0, 0, static_cast<AeroGLsizei>(viewportWidth), static_cast<AeroGLsizei>(viewportHeight));
    gl_.Disable(kGlDepthTest);
    gl_.Disable(kGlCullFace);
    gl_.Disable(kGlScissorTest);
    gl_.ClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    gl_.Clear(kGlColorBufferBit);

    gl_.Enable(kGlBlend);
    gl_.BlendFuncSeparate(kGlOne, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);

    gl_.UseProgram(programId_);
    if (uViewportSizeLoc_ >= 0) {
        gl_.Uniform2f(uViewportSizeLoc_, logicalVpW, logicalVpH);
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

    std::uint32_t boundBatchTextureId = 0u;
    AeroGLuint boundUnit1Tex = ResolveBatchImageTexture(0u, tree.ResourceStore());
    gl_.ActiveTexture(kGlTexture1);
    gl_.BindTexture(kGlTexture2D, boundUnit1Tex);
    if (uImageSamplerLoc_ >= 0) {
        gl_.Uniform1i(uImageSamplerLoc_, 1);
    }

    // 4. Issue indexed GL_TRIANGLES draw calls over coalesced GpuClipDrawBatches
    gl_.BindVertexArray(vaoId_);
    gl_.BindBuffer(kGlElementArrayBuffer, quadEboId_);
    bool scissorCurrentlyEnabled = false;
    BlendingMode activeGlBlend = BlendingMode::Normal;
    constexpr AeroGLenum kGlDstColor = 0x0306u;
    constexpr AeroGLenum kGlOneMinusSrcColor = 0x0301u;

    auto setGlBlendMode = [&](BlendingMode mode) noexcept {
        if (mode == activeGlBlend) {
            return;
        }
        switch (mode) {
            case BlendingMode::Multiply:
                gl_.BlendFuncSeparate(kGlDstColor, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
                break;
            case BlendingMode::Screen:
                gl_.BlendFuncSeparate(kGlOne, kGlOneMinusSrcColor, kGlOne, kGlOneMinusSrcAlpha);
                break;
            case BlendingMode::Additive:
                gl_.BlendFuncSeparate(kGlOne, kGlOne, kGlOne, kGlOneMinusSrcAlpha);
                break;
            case BlendingMode::Normal:
            default:
                gl_.BlendFuncSeparate(kGlOne, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
                break;
        }
        activeGlBlend = mode;
    };

    if (tree.BlurLayerSpans().empty()) {
        for (const GpuClipDrawBatch& batch : drawBatches) {
            const std::size_t quadCount = batch.vertexCount / 4u;
            if (quadCount == 0u) {
                continue;
            }
            stats.clipBatchesProcessed += 1u;

            setGlBlendMode(batch.blendMode);

            if (batch.textureId != boundBatchTextureId) {
                const AeroGLuint nextTex = ResolveBatchImageTexture(batch.textureId, tree.ResourceStore());
                if (nextTex != boundUnit1Tex) {
                    gl_.ActiveTexture(kGlTexture1);
                    gl_.BindTexture(kGlTexture2D, nextTex);
                    boundUnit1Tex = nextTex;
                }
                boundBatchTextureId = batch.textureId;
            }

            if (batch.scissorActive) {
                if (!scissorCurrentlyEnabled) {
                    gl_.Enable(kGlScissorTest);
                    scissorCurrentlyEnabled = true;
                }
                const GlScissorBox sc = ComputeGlScissorBox(
                    ScaleRectByDpi(batch.scissorRect, dpiScale),
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
    } else {
        auto flushPageQuadSubBatch = [&](
            std::size_t pageIdx,
            std::size_t absQuadStart,
            std::size_t quadCount,
            bool forceNormalBlend = false) {
            if (quadCount == 0u || pageIdx >= tree.PageCount()) {
                return;
            }
            if (!EnsureQuadIndexCapacity(absQuadStart + quadCount)) {
                return;
            }
            stats.clipBatchesProcessed += 1u;
            const CommandPageHeader& hdr = tree.GetPage(pageIdx).header;
            const BlendingMode bm = forceNormalBlend
                ? BlendingMode::Normal
                : ((hdr.reserved[1] <= 3u)
                       ? static_cast<BlendingMode>(hdr.reserved[1])
                       : BlendingMode::Normal);
            setGlBlendMode(bm);

            const std::uint32_t texId = hdr.reserved[0];
            if (texId != boundBatchTextureId) {
                const AeroGLuint nextTex = ResolveBatchImageTexture(texId, tree.ResourceStore());
                if (nextTex != boundUnit1Tex) {
                    gl_.ActiveTexture(kGlTexture1);
                    gl_.BindTexture(kGlTexture2D, nextTex);
                    boundUnit1Tex = nextTex;
                }
                boundBatchTextureId = texId;
            }

            const bool scActive = (hdr.flags & CommandPageFlag_ScissorClipActive) != 0u;
            if (scActive) {
                if (!scissorCurrentlyEnabled) {
                    gl_.Enable(kGlScissorTest);
                    scissorCurrentlyEnabled = true;
                }
                const GlScissorBox sc = ComputeGlScissorBox(
                    ScaleRectByDpi(Rect{hdr.clipX, hdr.clipY, hdr.clipW, hdr.clipH}, dpiScale),
                    viewportWidth,
                    viewportHeight);
                gl_.Scissor(sc.x, sc.y, sc.width, sc.height);
                stats.scissorBatchCount += 1u;
                if (sc.width <= 0 || sc.height <= 0) {
                    return;
                }
            } else if (scissorCurrentlyEnabled) {
                gl_.Disable(kGlScissorTest);
                scissorCurrentlyEnabled = false;
            }

            const std::size_t indexCount = quadCount * 6u;
            const std::uintptr_t indexByteOffset = absQuadStart * 6u * sizeof(std::uint32_t);
            gl_.DrawElements(
                kGlTriangles,
                static_cast<AeroGLsizei>(indexCount),
                kGlUnsignedInt,
                reinterpret_cast<const void*>(indexByteOffset));

            stats.drawCallsIssued += 1u;
            stats.quadsSubmitted += quadCount;
            stats.indicesSubmitted += indexCount;
            stats.trianglesSubmitted += quadCount * 2u;
        };

        auto flushNodeRange = [&](
            std::uint32_t rangeStart,
            std::uint32_t rangeEnd,
            bool forceRootNormalBlend = false) {
            bool hasPending = false;
            std::size_t pendingPage = 0u;
            std::size_t pendingQuadStart = 0u;
            std::size_t pendingQuadCount = 0u;
            bool pendingForceNormal = false;
            const void* rootOwner = nullptr;
            if (forceRootNormalBlend && rangeStart < tree.NodeCount()) {
                if (const RenderNodeDescriptor* rootNd = tree.GetNode(rangeStart)) {
                    rootOwner = rootNd->sourceElement;
                }
            }

            for (std::uint32_t i = rangeStart; i < rangeEnd; ++i) {
                const RenderNodeDescriptor* nd = tree.GetNode(i);
                if (!nd || nd->pageIndex >= tree.PageCount() || nd->vertexCount < 4u) {
                    continue;
                }
                const std::size_t qInPage = nd->vertexOffsetInPage / 4u;
                if (qInPage >= kQuadsPerPage) {
                    continue;
                }
                const std::size_t qCount = std::min<std::size_t>(nd->vertexCount / 4u, kQuadsPerPage - qInPage);
                if (qCount == 0u) {
                    continue;
                }
                const bool forceNormal = forceRootNormalBlend &&
                    (rootOwner != nullptr ? (nd->sourceElement == rootOwner) : (i == rangeStart));
                const std::size_t absQuad = (nd->pageIndex % effRingPages) * kQuadsPerPage + qInPage;
                if (hasPending &&
                    nd->pageIndex == pendingPage &&
                    forceNormal == pendingForceNormal &&
                    absQuad == pendingQuadStart + pendingQuadCount) {
                    pendingQuadCount += qCount;
                } else {
                    if (hasPending) {
                        flushPageQuadSubBatch(pendingPage, pendingQuadStart, pendingQuadCount, pendingForceNormal);
                    }
                    hasPending = true;
                    pendingPage = nd->pageIndex;
                    pendingQuadStart = absQuad;
                    pendingQuadCount = qCount;
                    pendingForceNormal = forceNormal;
                }
            }
            if (hasPending) {
                flushPageQuadSubBatch(pendingPage, pendingQuadStart, pendingQuadCount, pendingForceNormal);
            }
        };

        auto bindEffectQuadVboAttribs = [&]() noexcept {
            gl_.BindVertexArray(vaoId_);
            gl_.BindBuffer(kGlArrayBuffer, effectQuadVboId_);
            constexpr auto kStride = static_cast<AeroGLsizei>(sizeof(AeroVertex));
            gl_.VertexAttribPointer(0u, 2, kGlFloat, 0u, kStride, reinterpret_cast<const void*>(std::uintptr_t{0}));
            gl_.VertexAttribPointer(1u, 2, kGlFloat, 0u, kStride, reinterpret_cast<const void*>(std::uintptr_t{8}));
            gl_.VertexAttribIPointer(2u, 1, kGlUnsignedInt, kStride, reinterpret_cast<const void*>(std::uintptr_t{16}));
            gl_.VertexAttribIPointer(3u, 1, kGlUnsignedInt, kStride, reinterpret_cast<const void*>(std::uintptr_t{20}));
            if (clipWRingVboId_ != 0u) {
                gl_.VertexAttribPointer(
                    4u,
                    1,
                    kGlFloat,
                    0u,
                    static_cast<AeroGLsizei>(sizeof(float)),
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(4u * sizeof(AeroVertex))));
            }
            gl_.BindBuffer(kGlElementArrayBuffer, quadEboId_);
        };

        auto drawPremulEffectPatch = [&](int patchX,
                                         int patchY,
                                         int patchW,
                                         int patchH,
                                         const std::uint8_t* premulRgba8,
                                         bool coordsArePhysical,
                                         BlendingMode blendMode = BlendingMode::Normal) {
            if (patchW <= 0 || patchH <= 0 || !premulRgba8 || !EnsureQuadIndexCapacity(1u)) {
                return;
            }
            if (effectScratchTexId_ == 0u) {
                gl_.GenTextures(1, &effectScratchTexId_);
                if (effectScratchTexId_ == 0u) {
                    return;
                }
                effectScratchTexW_ = 0u;
                effectScratchTexH_ = 0u;
            }
            if (effectQuadVboId_ == 0u) {
                gl_.GenBuffers(1, &effectQuadVboId_);
                if (effectQuadVboId_ == 0u) {
                    return;
                }
                const EffectQuadBufferInit initBuf{};
                gl_.BindBuffer(kGlArrayBuffer, effectQuadVboId_);
                gl_.BufferData(
                    kGlArrayBuffer,
                    static_cast<AeroGLsizeiptr>(sizeof(initBuf)),
                    &initBuf,
                    kGlDynamicDraw);
            }

            gl_.ActiveTexture(kGlTexture1);
            gl_.BindTexture(kGlTexture2D, effectScratchTexId_);
            gl_.PixelStorei(kGlUnpackAlignment, 1);
            const auto uw = static_cast<std::uint32_t>(patchW);
            const auto uh = static_cast<std::uint32_t>(patchH);
            if (effectScratchTexW_ != uw || effectScratchTexH_ != uh) {
                gl_.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<AeroGLint>(kGlLinear));
                gl_.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<AeroGLint>(kGlLinear));
                gl_.TexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<AeroGLint>(kGlClampToEdge));
                gl_.TexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<AeroGLint>(kGlClampToEdge));
                gl_.TexImage2D(
                    kGlTexture2D,
                    0,
                    static_cast<AeroGLint>(kGlRgba8),
                    static_cast<AeroGLsizei>(patchW),
                    static_cast<AeroGLsizei>(patchH),
                    0,
                    kGlRgba,
                    kGlUnsignedByte,
                    premulRgba8);
                effectScratchTexW_ = uw;
                effectScratchTexH_ = uh;
            } else {
                gl_.TexSubImage2D(
                    kGlTexture2D,
                    0,
                    0,
                    0,
                    static_cast<AeroGLsizei>(patchW),
                    static_cast<AeroGLsizei>(patchH),
                    kGlRgba,
                    kGlUnsignedByte,
                    premulRgba8);
            }
            boundUnit1Tex = effectScratchTexId_;
            boundBatchTextureId = 0xFFFFFFFFu;

            setGlBlendMode(blendMode);
            if (scissorCurrentlyEnabled) {
                gl_.Disable(kGlScissorTest);
                scissorCurrentlyEnabled = false;
            }

            const float coordScale = coordsArePhysical ? (1.0f / dpiScale) : 1.0f;
            const float x0 = static_cast<float>(patchX) * coordScale;
            const float y0 = static_cast<float>(patchY) * coordScale;
            const float x1 = static_cast<float>(patchX + patchW) * coordScale;
            const float y1 = static_cast<float>(patchY + patchH) * coordScale;
            const auto prim = static_cast<std::uint16_t>(AeroPrimType::TexturedQuad);
            const AeroVertex quadVerts[4]{
                {x0, y0, 0.0f, 0.0f, 0xFFFFFFFFu, prim, AeroVertexFlag_PremultipliedAlpha},
                {x1, y0, 1.0f, 0.0f, 0xFFFFFFFFu, prim, AeroVertexFlag_PremultipliedAlpha},
                {x1, y1, 1.0f, 1.0f, 0xFFFFFFFFu, prim, AeroVertexFlag_PremultipliedAlpha},
                {x0, y1, 0.0f, 1.0f, 0xFFFFFFFFu, prim, AeroVertexFlag_PremultipliedAlpha},
            };

            bindEffectQuadVboAttribs();
            gl_.BufferSubData(
                kGlArrayBuffer,
                0,
                static_cast<AeroGLsizeiptr>(sizeof(quadVerts)),
                quadVerts);
            gl_.DrawElements(kGlTriangles, 6, kGlUnsignedInt, nullptr);

            BindAeroVertexLayout();

            stats.clipBatchesProcessed += 1u;
            stats.drawCallsIssued += 1u;
            stats.quadsSubmitted += 1u;
            stats.indicesSubmitted += 6u;
            stats.trianglesSubmitted += 2u;
        };

        const auto& blurSpans = tree.BlurLayerSpans();
        const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
        const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));

        auto executeBlurSpanViaFbo = [&](const BlurLayerSpan& span, std::size_t nextBlurCursor) -> bool {
            if (!gl_.HasFramebufferSupport() || effectGpuPipelineFailed_) {
                return false;
            }
            const bool isPureOpacityMask = (span.effectMode == UIEffectKind::OpacityMask);
            if (span.hasOpacityMask && (!isPureOpacityMask || !gl_.Uniform4f)) {
                return false;
            }
            GradientBrushDesc maskGrad{};
            std::vector<GradientStop> sortedMaskStops;
            if (isPureOpacityMask && span.opacityMaskGradId > 0u) {
                const bool found =
                    (tree.ResourceStore() != nullptr &&
                     tree.ResourceStore()->GradientBrushes().TryGetBrush(span.opacityMaskGradId, maskGrad)) ||
                    GradientBrushRegistry::Instance().TryGetBrush(span.opacityMaskGradId, maskGrad);
                const bool supportedKind =
                    (maskGrad.kind == GradientBrushKind::Linear ||
                     maskGrad.kind == GradientBrushKind::Radial);
                if (!found || !supportedKind || maskGrad.stops.empty() ||
                    maskGrad.stops.size() > 4u) {
                    return false;
                }
                sortedMaskStops = maskGrad.stops;
                for (std::size_t i = 0u; i < sortedMaskStops.size() && i < span.opacityMaskStopOffsets.size(); ++i) {
                    if (std::isfinite(span.opacityMaskStopOffsets[i])) {
                        sortedMaskStops[i].offset = std::clamp(span.opacityMaskStopOffsets[i], 0.0f, 1.0f);
                    }
                }
                std::stable_sort(
                    sortedMaskStops.begin(),
                    sortedMaskStops.end(),
                    [](const GradientStop& a, const GradientStop& b) noexcept {
                        return a.offset < b.offset;
                    });
            }
            for (std::size_t k = nextBlurCursor; k < blurSpans.size(); ++k) {
                if (blurSpans[k].firstNodeId >= span.endNodeId) {
                    break;
                }
                if (aero::render::detail::SpanNeedsEffectPass(blurSpans[k])) {
                    return false;
                }
            }
            float uMinX = 0.0f, uMinY = 0.0f, uMaxX = 0.0f, uMaxY = 0.0f;
            if (!aero::render::detail::ComputeNodeRangeScreenBounds(
                    tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
                return true;
            }
            const int logicalPadPx = aero::render::detail::ComputeSpanPaddingPx(span);
            const int padPx = static_cast<int>(std::ceil(static_cast<float>(logicalPadPx) * dpiScale));
            const int bx0 = std::clamp(
                aero::render::detail::SafeFloorToInt(uMinX * dpiScale, -padPx, vpW + padPx) - padPx, 0, vpW);
            const int by0 = std::clamp(
                aero::render::detail::SafeFloorToInt(uMinY * dpiScale, -padPx, vpH + padPx) - padPx, 0, vpH);
            const int bx1 = std::clamp(
                aero::render::detail::SafeCeilToInt(uMaxX * dpiScale, -padPx, vpW + padPx) + padPx, 0, vpW);
            const int by1 = std::clamp(
                aero::render::detail::SafeCeilToInt(uMaxY * dpiScale, -padPx, vpH + padPx) + padPx, 0, vpH);
            if (bx1 <= bx0 || by1 <= by0) {
                return true;
            }
            if (!EnsureEffectGpuResources(viewportWidth, viewportHeight) || !EnsureQuadIndexCapacity(1u)) {
                return false;
            }

            // 1. Render child span subtree directly into offscreen FBO #0 using main UberShader
            const int bw = bx1 - bx0;
            const int bh = by1 - by0;
            gl_.BindFramebuffer(kGlFramebuffer, effectFboIds_[0]);
            gl_.Enable(kGlScissorTest);
            scissorCurrentlyEnabled = true;
            gl_.Scissor(
                static_cast<AeroGLint>(bx0),
                static_cast<AeroGLint>(vpH - by1),
                static_cast<AeroGLsizei>(bw),
                static_cast<AeroGLsizei>(bh));
            gl_.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl_.Clear(kGlColorBufferBit);
            gl_.Disable(kGlScissorTest);
            scissorCurrentlyEnabled = false;
            setGlBlendMode(BlendingMode::Normal);
            flushNodeRange(span.firstNodeId, span.endNodeId, span.blendMode != BlendingMode::Normal);

            // 2. Post-process via FBO effect shader and composite back to default FBO (0)
            const float fVpW = static_cast<float>(vpW);
            const float fVpH = static_cast<float>(vpH);
            const float u0 = static_cast<float>(bx0) / fVpW;
            const float u1 = static_cast<float>(bx1) / fVpW;
            const float vTop = 1.0f - static_cast<float>(by0) / fVpH;
            const float vBot = 1.0f - static_cast<float>(by1) / fVpH;
            const float uvMinX = (static_cast<float>(bx0) + 0.5f) / fVpW;
            const float uvMaxX = std::max(uvMinX, (static_cast<float>(bx1) - 0.5f) / fVpW);
            const float uvMinY = 1.0f - (static_cast<float>(by1) - 0.5f) / fVpH;
            const float uvMaxY = std::max(uvMinY, 1.0f - (static_cast<float>(by0) + 0.5f) / fVpH);

            gl_.UseProgram(effectProgramId_);
            if (uEffectViewportSizeLoc_ >= 0) {
                gl_.Uniform2f(uEffectViewportSizeLoc_, fVpW, fVpH);
            }
            if (uEffectSrcTexLoc_ >= 0) {
                gl_.Uniform1i(uEffectSrcTexLoc_, 0);
            }
            if (uEffectUvMinLoc_ >= 0) {
                gl_.Uniform2f(uEffectUvMinLoc_, uvMinX, uvMinY);
            }
            if (uEffectUvMaxLoc_ >= 0) {
                gl_.Uniform2f(uEffectUvMaxLoc_, uvMaxX, uvMaxY);
            }

            auto issueEffectPass = [&](
                AeroGLuint targetFbo,
                AeroGLint mode,
                AeroGLuint srcTexId,
                float texelX,
                float texelY,
                float paramX,
                float paramY,
                std::uint32_t packedColor,
                bool enableBlend,
                bool applyClipScissor) {
                gl_.BindFramebuffer(kGlFramebuffer, targetFbo);
                if (enableBlend) {
                    gl_.Enable(kGlBlend);
                    setGlBlendMode((targetFbo == 0u) ? span.blendMode : BlendingMode::Normal);
                } else {
                    gl_.Disable(kGlBlend);
                }
                if (applyClipScissor && span.clipActive) {
                    if (!scissorCurrentlyEnabled) {
                        gl_.Enable(kGlScissorTest);
                        scissorCurrentlyEnabled = true;
                    }
                    const GlScissorBox sc = ComputeGlScissorBox(
                        ScaleRectByDpi(span.clipRect, dpiScale), viewportWidth, viewportHeight);
                    gl_.Scissor(sc.x, sc.y, sc.width, sc.height);
                } else if (scissorCurrentlyEnabled) {
                    gl_.Disable(kGlScissorTest);
                    scissorCurrentlyEnabled = false;
                }

                gl_.ActiveTexture(kGlTexture0);
                gl_.BindTexture(kGlTexture2D, srcTexId);
                if (uEffectModeLoc_ >= 0) {
                    gl_.Uniform1i(uEffectModeLoc_, mode);
                }
                if (uEffectTexelSizeLoc_ >= 0) {
                    gl_.Uniform2f(uEffectTexelSizeLoc_, texelX, texelY);
                }
                if (uEffectParamsLoc_ >= 0) {
                    gl_.Uniform2f(uEffectParamsLoc_, paramX, paramY);
                }

                const float fx0 = static_cast<float>(bx0);
                const float fy0 = static_cast<float>(by0);
                const float fx1 = static_cast<float>(bx1);
                const float fy1 = static_cast<float>(by1);
                const auto prim = static_cast<std::uint16_t>(AeroPrimType::TexturedQuad);
                const AeroVertex quadVerts[4]{
                    {fx0, fy0, u0, vTop, packedColor, prim, AeroVertexFlag_PremultipliedAlpha},
                    {fx1, fy0, u1, vTop, packedColor, prim, AeroVertexFlag_PremultipliedAlpha},
                    {fx1, fy1, u1, vBot, packedColor, prim, AeroVertexFlag_PremultipliedAlpha},
                    {fx0, fy1, u0, vBot, packedColor, prim, AeroVertexFlag_PremultipliedAlpha},
                };
                bindEffectQuadVboAttribs();
                gl_.BufferSubData(kGlArrayBuffer, 0, static_cast<AeroGLsizeiptr>(sizeof(quadVerts)), quadVerts);
                gl_.DrawElements(kGlTriangles, 6, kGlUnsignedInt, nullptr);

                stats.clipBatchesProcessed += 1u;
                stats.drawCallsIssued += 1u;
                stats.quadsSubmitted += 1u;
                stats.indicesSubmitted += 6u;
                stats.trianglesSubmitted += 2u;
            };

            const float effSpanOpacity = std::clamp(
                std::isfinite(span.effectOpacity) ? span.effectOpacity : 1.0f, 0.0f, 1.0f);
            const std::uint32_t opacityColor = PackColorRgba8(Color{1.0f, 1.0f, 1.0f, 1.0f}, effSpanOpacity);
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            const float angRad = (std::isfinite(span.angleDeg) ? span.angleDeg : 0.0f) * kDegToRad;
            const float ws = (std::isfinite(span.worldScale) && span.worldScale > 0.0f) ? span.worldScale : 1.0f;
            const float effRadiusPhys = std::max(0.0f, span.radiusPx) * ws * dpiScale;
            const float invW = 1.0f / fVpW;
            const float invH = 1.0f / fVpH;

            switch (span.effectMode) {
                case UIEffectKind::Blur: {
                    gl_.BindFramebuffer(kGlFramebuffer, effectFboIds_[1]);
                    gl_.Enable(kGlScissorTest);
                    scissorCurrentlyEnabled = true;
                    gl_.Scissor(
                        static_cast<AeroGLint>(bx0),
                        static_cast<AeroGLint>(vpH - by1),
                        static_cast<AeroGLsizei>(bw),
                        static_cast<AeroGLsizei>(bh));
                    gl_.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                    gl_.Clear(kGlColorBufferBit);
                    gl_.Disable(kGlScissorTest);
                    scissorCurrentlyEnabled = false;
                    issueEffectPass(effectFboIds_[1], 1, effectFboTexIds_[0], invW, 0.0f, effRadiusPhys, 1.0f, 0xFFFFFFFFu, false, false);
                    issueEffectPass(0u, 1, effectFboTexIds_[1], 0.0f, invH, effRadiusPhys, 1.0f, opacityColor, true, true);
                    break;
                }
                case UIEffectKind::DropShadow: {
                    const float depthPx = (std::isfinite(span.amountX) ? span.amountX : 0.0f) * ws * dpiScale;
                    const float dxPx = std::cos(angRad) * depthPx;
                    const float dyPx = -std::sin(angRad) * depthPx;
                    if (uEffectShadowOffsetLoc_ >= 0) {
                        gl_.Uniform2f(uEffectShadowOffsetLoc_, -dxPx * invW, dyPx * invH);
                    }
                    gl_.BindFramebuffer(kGlFramebuffer, effectFboIds_[1]);
                    gl_.Enable(kGlScissorTest);
                    scissorCurrentlyEnabled = true;
                    gl_.Scissor(
                        static_cast<AeroGLint>(bx0),
                        static_cast<AeroGLint>(vpH - by1),
                        static_cast<AeroGLsizei>(bw),
                        static_cast<AeroGLsizei>(bh));
                    gl_.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                    gl_.Clear(kGlColorBufferBit);
                    gl_.Disable(kGlScissorTest);
                    scissorCurrentlyEnabled = false;
                    issueEffectPass(effectFboIds_[1], 8, effectFboTexIds_[0], invW, 0.0f, effRadiusPhys, 1.0f, 0xFFFFFFFFu, false, false);

                    gl_.ActiveTexture(kGlTexture1);
                    gl_.BindTexture(kGlTexture2D, effectFboTexIds_[0]);
                    if (uEffectOrigTexLoc_ >= 0) {
                        gl_.Uniform1i(uEffectOrigTexLoc_, 1);
                    }
                    gl_.ActiveTexture(kGlTexture0);

                    const std::uint32_t shadowPacked = PackColorRgba8(span.effectColor, 1.0f);
                    issueEffectPass(0u, 9, effectFboTexIds_[1], 0.0f, invH, effRadiusPhys, effSpanOpacity, shadowPacked, true, true);
                    gl_.ActiveTexture(kGlTexture1);
                    gl_.BindTexture(kGlTexture2D, 0u);
                    gl_.ActiveTexture(kGlTexture0);
                    break;
                }
                case UIEffectKind::DirectionalBlur:
                    issueEffectPass(
                        0u,
                        2,
                        effectFboTexIds_[0],
                        std::cos(angRad) * invW,
                        std::sin(angRad) * invH,
                        effRadiusPhys,
                        1.0f,
                        opacityColor,
                        true,
                        true);
                    break;
                case UIEffectKind::Pixelate:
                    issueEffectPass(0u, 3, effectFboTexIds_[0], invW, invH, effRadiusPhys, 1.0f, opacityColor, true, true);
                    break;
                case UIEffectKind::Tint: {
                    const float tintStrength = std::clamp(
                        (std::isfinite(span.effectColor.a) ? span.effectColor.a : 1.0f) * effSpanOpacity,
                        0.0f,
                        1.0f);
                    const std::uint32_t tintPacked = PackColorRgba8(
                        Color{span.effectColor.r, span.effectColor.g, span.effectColor.b, 1.0f},
                        1.0f);
                    issueEffectPass(0u, 4, effectFboTexIds_[0], invW, invH, tintStrength, 1.0f, tintPacked, true, true);
                    break;
                }
                case UIEffectKind::ChromaticAberration:
                    issueEffectPass(
                        0u,
                        5,
                        effectFboTexIds_[0],
                        std::cos(angRad) * invW,
                        std::sin(angRad) * invH,
                        span.amountX,
                        1.0f,
                        opacityColor,
                        true,
                        true);
                    break;
                case UIEffectKind::Saturation:
                    issueEffectPass(0u, 6, effectFboTexIds_[0], invW, invH, span.saturation, 1.0f, opacityColor, true, true);
                    break;
                case UIEffectKind::OpacityMask: {
                    const float maskOp = std::clamp(span.opacityMaskOpacity, 0.0f, 1.0f);
                    if (span.opacityMaskGradId == 0u) {
                        const float solidA =
                            (static_cast<float>((span.opacityMaskSolidRgba >> 24u) & 0xFFu) / 255.0f) * maskOp;
                        if (uEffectMaskStopCountLoc_ >= 0) {
                            gl_.Uniform1i(uEffectMaskStopCountLoc_, 0);
                        }
                        issueEffectPass(0u, 7, effectFboTexIds_[0], invW, invH, solidA, 1.0f, 0xFFFFFFFFu, true, true);
                    } else {
                        const float invDpi = 1.0f / std::max(0.25f, dpiScale);
                        const Matrix2D& invW2 = span.opacityMaskInvWorld2D;
                        if (uEffectMaskMatRow0Loc_ >= 0 && gl_.Uniform4f) {
                            gl_.Uniform4f(
                                uEffectMaskMatRow0Loc_,
                                invW2.m00 * invDpi,
                                invW2.m01 * invDpi,
                                invW2.tx,
                                span.opacityMaskLocalRect.x);
                        }
                        if (uEffectMaskMatRow1Loc_ >= 0 && gl_.Uniform4f) {
                            gl_.Uniform4f(
                                uEffectMaskMatRow1Loc_,
                                invW2.m10 * invDpi,
                                invW2.m11 * invDpi,
                                invW2.ty,
                                span.opacityMaskLocalRect.y);
                        }
                        if (uEffectMaskSizeGradLoc_ >= 0 && gl_.Uniform4f) {
                            const float modeVal =
                                ((maskGrad.mappingMode == BrushMappingMode::Absolute) ? 1.0f : 0.0f) +
                                ((maskGrad.kind == GradientBrushKind::Radial) ? 2.0f : 0.0f);
                            gl_.Uniform4f(
                                uEffectMaskSizeGradLoc_,
                                std::max(1e-4f, span.opacityMaskLocalRect.width),
                                std::max(1e-4f, span.opacityMaskLocalRect.height),
                                modeVal,
                                static_cast<float>(static_cast<int>(maskGrad.spreadMethod)));
                        }
                        if (uEffectMaskGradPtsLoc_ >= 0 && gl_.Uniform4f) {
                            if (maskGrad.kind == GradientBrushKind::Radial) {
                                gl_.Uniform4f(
                                    uEffectMaskGradPtsLoc_,
                                    maskGrad.center.x,
                                    maskGrad.center.y,
                                    std::max(1e-5f, std::fabs(maskGrad.radiusX)),
                                    std::max(1e-5f, std::fabs(maskGrad.radiusY)));
                            } else {
                                gl_.Uniform4f(
                                    uEffectMaskGradPtsLoc_,
                                    maskGrad.startPoint.x,
                                    maskGrad.startPoint.y,
                                    maskGrad.endPoint.x,
                                    maskGrad.endPoint.y);
                            }
                        }
                        float offs[4]{0.0f, 1.0f, 1.0f, 1.0f};
                        float alphas[4]{1.0f, 1.0f, 1.0f, 1.0f};
                        const int sc = static_cast<int>(std::min<std::size_t>(4u, sortedMaskStops.size()));
                        for (int si = 0; si < sc; ++si) {
                            offs[si] = sortedMaskStops[static_cast<std::size_t>(si)].offset;
                            alphas[si] = std::clamp(sortedMaskStops[static_cast<std::size_t>(si)].color.a, 0.0f, 1.0f);
                        }
                        if (uEffectMaskStopOffsetsLoc_ >= 0 && gl_.Uniform4f) {
                            gl_.Uniform4f(uEffectMaskStopOffsetsLoc_, offs[0], offs[1], offs[2], offs[3]);
                        }
                        if (uEffectMaskStopAlphasLoc_ >= 0 && gl_.Uniform4f) {
                            gl_.Uniform4f(uEffectMaskStopAlphasLoc_, alphas[0], alphas[1], alphas[2], alphas[3]);
                        }
                        if (uEffectMaskStopCountLoc_ >= 0) {
                            gl_.Uniform1i(uEffectMaskStopCountLoc_, sc);
                        }
                        const float totalMaskOp = std::clamp(maskGrad.opacity, 0.0f, 1.0f) * maskOp;
                        issueEffectPass(0u, 7, effectFboTexIds_[0], invW, invH, totalMaskOp, 1.0f, 0xFFFFFFFFu, true, true);
                    }
                    break;
                }
                default:
                    issueEffectPass(0u, 0, effectFboTexIds_[0], invW, invH, 1.0f, 1.0f, opacityColor, true, true);
                    break;
            }

            // 3. Restore primary UberShader + atlas binding + 24B vertex layout on default framebuffer (0)
            gl_.BindFramebuffer(kGlFramebuffer, 0u);
            gl_.Enable(kGlBlend);
            gl_.BlendFuncSeparate(kGlOne, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
            activeGlBlend = BlendingMode::Normal;
            if (scissorCurrentlyEnabled) {
                gl_.Disable(kGlScissorTest);
                scissorCurrentlyEnabled = false;
            }
            gl_.UseProgram(programId_);
            gl_.ActiveTexture(kGlTexture0);
            gl_.BindTexture(kGlTexture2D, atlasTextureId_);
            boundBatchTextureId = 0xFFFFFFFFu;
            boundUnit1Tex = 0u;
            BindAeroVertexLayout();
            return true;
        };

        auto executeBackdropBlurOnGpu = [&](const BlurLayerSpan& span) {
            if (!gl_.ReadPixels || !aero::render::detail::SpanNeedsEffectPass(span)) {
                return;
            }
            BlurLayerSpan physSpan = span;
            physSpan.maskDestRect = ScaleRectByDpi(span.maskDestRect, dpiScale);
            physSpan.clipRect = ScaleRectByDpi(span.clipRect, dpiScale);
            physSpan.radiusPx = span.radiusPx * dpiScale;
            const Rect mr = physSpan.maskDestRect;
            if (!std::isfinite(mr.x) || !std::isfinite(mr.y) ||
                !std::isfinite(mr.width) || !std::isfinite(mr.height) ||
                mr.width <= 1.0f || mr.height <= 1.0f) {
                return;
            }
            const int boxR = aero::render::detail::ComputeSpanBoxRadius(physSpan);
            const int padPx = (boxR > 0) ? (boxR * 3 + 2) : 0;
            const int bx0 = std::clamp(static_cast<int>(std::floor(mr.x)) - padPx, 0, vpW);
            const int by0 = std::clamp(static_cast<int>(std::floor(mr.y)) - padPx, 0, vpH);
            const int bx1 = std::clamp(static_cast<int>(std::ceil(mr.x + mr.width)) + padPx, 0, vpW);
            const int by1 = std::clamp(static_cast<int>(std::ceil(mr.y + mr.height)) + padPx, 0, vpH);
            const int bw = bx1 - bx0;
            const int bh = by1 - by0;
            if (bw <= 0 || bh <= 0) {
                return;
            }

            if (scissorCurrentlyEnabled) {
                gl_.Disable(kGlScissorTest);
                scissorCurrentlyEnabled = false;
            }
            if (gl_.PixelStorei) {
                gl_.PixelStorei(kGlPackAlignment, 1);
            }
            const std::size_t rowBytes = static_cast<std::size_t>(bw) * 4u;
            effectReadbackBottomUp_.resize(rowBytes * static_cast<std::size_t>(bh));
            gl_.ReadPixels(
                static_cast<AeroGLint>(bx0),
                static_cast<AeroGLint>(vpH - by1),
                static_cast<AeroGLsizei>(bw),
                static_cast<AeroGLsizei>(bh),
                kGlRgba,
                kGlUnsignedByte,
                effectReadbackBottomUp_.data());

            effectRowScratch_.resize(rowBytes);
            for (int ry = 0; ry < bh / 2; ++ry) {
                const int oppRow = (bh - 1) - ry;
                std::uint8_t* topPtr = effectReadbackBottomUp_.data() + static_cast<std::size_t>(ry) * rowBytes;
                std::uint8_t* botPtr = effectReadbackBottomUp_.data() + static_cast<std::size_t>(oppRow) * rowBytes;
                std::memcpy(effectRowScratch_.data(), topPtr, rowBytes);
                std::memcpy(topPtr, botPtr, rowBytes);
                std::memcpy(botPtr, effectRowScratch_.data(), rowBytes);
            }
            if (clearColor.a >= 0.999f) {
                const std::size_t pxCount = static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh);
                for (std::size_t i = 0u; i < pxCount; ++i) {
                    effectReadbackBottomUp_[i * 4u + 3u] = 255u;
                }
            }

            int patchX = 0, patchY = 0, patchW = 0, patchH = 0;
            if (ZeroCopyGpuStream::ComputeBackdropBlurPatchPremulRgba8(
                    physSpan,
                    viewportWidth,
                    viewportHeight,
                    bx0,
                    by0,
                    bw,
                    bh,
                    effectReadbackBottomUp_.data(),
                    patchX,
                    patchY,
                    patchW,
                    patchH,
                    effectPatchScratch_,
                    tree.ResourceStore())) {
                drawPremulEffectPatch(
                    patchX, patchY, patchW, patchH, effectPatchScratch_.data(), true, BlendingMode::Normal);
            }
        };

        const auto totalNodes = static_cast<std::uint32_t>(tree.NodeCount());
        std::size_t blurCursor = 0u;
        std::uint32_t regularSegStart = 0u;
        std::uint32_t nid = 0u;

        while (nid < totalNodes) {
            while (blurCursor < blurSpans.size() &&
                   (blurSpans[blurCursor].firstNodeId < nid ||
                    (blurSpans[blurCursor].endNodeId <= nid &&
                     blurSpans[blurCursor].effectMode != UIEffectKind::BackdropBlur))) {
                ++blurCursor;
            }
            if (blurCursor < blurSpans.size() &&
                blurSpans[blurCursor].firstNodeId == nid &&
                (blurSpans[blurCursor].endNodeId > nid ||
                 blurSpans[blurCursor].effectMode == UIEffectKind::BackdropBlur) &&
                blurSpans[blurCursor].endNodeId <= totalNodes &&
                aero::render::detail::SpanNeedsEffectPass(blurSpans[blurCursor])) {
                const BlurLayerSpan span = blurSpans[blurCursor];
                ++blurCursor;

                flushNodeRange(regularSegStart, nid);

                if (span.effectMode == UIEffectKind::BackdropBlur) {
                    executeBackdropBlurOnGpu(span);
                    regularSegStart = nid;
                    continue;
                }

                if (!executeBlurSpanViaFbo(span, blurCursor)) {
                    const auto logVpW = static_cast<std::uint32_t>(std::max(1.0f, std::round(logicalVpW)));
                    const auto logVpH = static_cast<std::uint32_t>(std::max(1.0f, std::round(logicalVpH)));
                    int patchX = 0, patchY = 0, patchW = 0, patchH = 0;
                    if (ZeroCopyGpuStream::RasterizeBlurSpanPatchPremulRgba8(
                            tree,
                            span,
                            blurCursor,
                            logVpW,
                            logVpH,
                            patchX,
                            patchY,
                            patchW,
                            patchH,
                            effectPatchScratch_)) {
                        drawPremulEffectPatch(
                            patchX, patchY, patchW, patchH, effectPatchScratch_.data(), false, span.blendMode);
                    }
                }
                while (blurCursor < blurSpans.size() &&
                       blurSpans[blurCursor].firstNodeId < span.endNodeId) {
                    ++blurCursor;
                }
                nid = span.endNodeId;
                regularSegStart = nid;
                continue;
            }
            ++nid;
        }

        flushNodeRange(regularSegStart, totalNodes);

        while (blurCursor < blurSpans.size()) {
            const BlurLayerSpan& span = blurSpans[blurCursor++];
            if (span.effectMode == UIEffectKind::BackdropBlur &&
                span.firstNodeId == totalNodes) {
                executeBackdropBlurOnGpu(span);
            }
        }
    }

    if (activeGlBlend != BlendingMode::Normal) {
        gl_.BlendFuncSeparate(kGlOne, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
    }
    if (scissorCurrentlyEnabled) {
        gl_.Disable(kGlScissorTest);
    }
    gl_.ActiveTexture(kGlTexture0);

    if (effectReadbackBottomUp_.capacity() > 512u * 1024u) {
        std::vector<std::uint8_t>().swap(effectReadbackBottomUp_);
    }
    if (effectRowScratch_.capacity() > 64u * 1024u) {
        std::vector<std::uint8_t>().swap(effectRowScratch_);
    }
    if (effectPatchScratch_.capacity() > 512u * 1024u) {
        std::vector<std::uint8_t>().swap(effectPatchScratch_);
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

    framebufferWidth_ = config.initialViewportWidth;
    framebufferHeight_ = config.initialViewportHeight;
    lastRenderedTreeId_ = 0xFFFFFFFFu;
    lastRenderedNodeCount_ = 0u;
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
    lastRenderedTreeId_ = 0xFFFFFFFFu;
    lastRenderedNodeCount_ = 0u;
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
    return true;
}

bool SoftwareSurfaceBackend::EnsureQuadIndexCapacity(std::size_t requiredQuadCount) {
    if (requiredQuadCount <= quadCapacity_) {
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
    quadIndexMirror_.clear();
    quadCapacity_ = nextCap;
    if (expanded) {
        telemetry_.quadEboExpansionCount += 1u;
    }
    return true;
}

const std::vector<std::uint32_t>& SoftwareSurfaceBackend::QuadIndexCpuMirror() const noexcept {
    if (quadIndexMirror_.empty() && quadCapacity_ > 0u) {
        BuildStaticQuadIndexBuffer(quadCapacity_, quadIndexMirror_);
    }
    return quadIndexMirror_;
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

    const std::size_t targetRingPages = std::max(ringPageCapacity, tree.PageCount());
    if (targetRingPages > maxPagesInRing_) {
        std::size_t nextRingPages = std::max<std::size_t>(16u, maxPagesInRing_);
        while (nextRingPages < targetRingPages) {
            nextRingPages *= 2u;
        }
        (void)AllocateRingVertexBuffer(nextRingPages);
    }
    const std::size_t effRingPages = std::max<std::size_t>(1u, maxPagesInRing_);

    for (const GlBufferSubDataSpan& span : uploadSpans) {
        if (!span.sourcePageVertexPtr || span.byteSize == 0u) {
            continue;
        }
        const std::size_t clampedBytes = std::min<std::size_t>(span.byteSize, kPageVertexBytes);
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
    const bool clearedToEmpty = (tree.NodeCount() == 0u && lastRenderedNodeCount_ > 0u);

    if (stats.dirtySpansUploaded > 0u ||
        viewportChanged ||
        clearChanged ||
        treeChanged ||
        clearedToEmpty ||
        framebufferRgba8_.empty()) {
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
        lastRenderedNodeCount_ = tree.NodeCount();
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
