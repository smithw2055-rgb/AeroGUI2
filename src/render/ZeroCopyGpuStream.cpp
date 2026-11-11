#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

namespace {

constexpr std::string_view kUberVertexShaderGL33 = R"(#version 330 core
// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (Desktop OpenGL 3.3 Core)
layout(location = 0) in vec2 a_Position;   // float x, y (offset 0, 8B)
layout(location = 1) in vec2 a_TexCoord;   // float u, v or SDF local coords (offset 8, 8B)
layout(location = 2) in uint a_PackedRgba; // uint32_t rgba (offset 16, 4B)
layout(location = 3) in uint a_PrimFlags;  // low 16b: primType|sdfExtra, high 16b: flags (offset 20, 4B)

uniform vec2 u_ViewportSize;

out vec2 v_UvOrLocalPos;
out vec4 v_Color;
flat out uint v_PrimType;
flat out uint v_Flags;
flat out vec2 v_QuadHalfSize;

void main() {
    vec2 ndc = (a_Position / max(u_ViewportSize, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_UvOrLocalPos = a_TexCoord;
    v_QuadHalfSize = abs(a_TexCoord);
    v_Color = vec4(
        float((a_PackedRgba >>  0u) & 0xFFu) / 255.0,
        float((a_PackedRgba >>  8u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 16u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 24u) & 0xFFu) / 255.0
    );
    uint basePrim = a_PrimFlags & 0x0007u;
    v_PrimType = (basePrim == 0u && (a_PrimFlags & 0x0008u) != 0u) ? 8u : basePrim;
    v_Flags    = ((a_PrimFlags >> 16u) & 0xFFFFu) | ((a_PrimFlags & 0xFFF8u) << 13u);
}
)";

constexpr std::string_view kUberPixelShaderGL33 = R"(#version 330 core
// AeroGUI 2.0 Single-Draw-Call Analytical SDF, Text & 4-Channel MTSDF UberPixelShader (Desktop GL 3.3 Core)
in vec2 v_UvOrLocalPos;
in vec4 v_Color;
flat in uint v_PrimType;
flat in uint v_Flags;
flat in vec2 v_QuadHalfSize;

uniform sampler2D u_GlyphAndUiAtlas;
uniform vec2 u_QuadHalfExtent;

out vec4 o_FragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    float cr = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - cr;
}

// r = vec4(tr, br, tl, bl)
float sdRoundedBox4(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    float cr = (p.y > 0.0) ? r.y : r.x;
    cr = min(cr, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - cr;
}

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float sdEllipseApprox(vec2 p, vec2 ab) {
    vec2 safeAb = max(ab, vec2(0.5));
    vec2 q = p / safeAb;
    float k0 = length(q);
    if (k0 < 1e-4) {
        return -min(safeAb.x, safeAb.y);
    }
    float k1 = length(q / safeAb);
    return k0 * (k0 - 1.0) / max(k1, 1e-5);
}

float unpackAtlasFloat32(vec4 texel) {
    uint b0 = uint(texel.r * 255.0 + 0.5);
    uint b1 = uint(texel.g * 255.0 + 0.5);
    uint b2 = uint(texel.b * 255.0 + 0.5);
    uint b3 = uint(texel.a * 255.0 + 0.5);
    return uintBitsToFloat(b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u));
}

float applyGradientSpread(float rawT, uint spreadMethod) {
    if (spreadMethod == 2u) {
        return rawT - floor(rawT);
    }
    if (spreadMethod == 1u) {
        float m = mod(abs(rawT), 2.0);
        return (m > 1.0) ? (2.0 - m) : m;
    }
    return clamp(rawT, 0.0, 1.0);
}

void main() {
    // 0: SolidQuad
    if (v_PrimType == 0u) {
        o_FragColor = v_Color;
        return;
    }

    // 1: SdfRoundedRect (Uniform fast path + Soft DropShadow + Asymmetric 4-corner sdRoundedBox4 + Hollow BorderBrush Ring)
    if (v_PrimType == 1u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        uint sdfMode = (v_Flags >> 16u) & 0x3u;
        uint payload27 = (v_Flags & 0xFFFFu) | (((v_Flags >> 18u) & 0x7FFu) << 16u);
        if (sdfMode == 0u) {
            float cornerRadius    = float(v_Flags & 0xFFu) * 0.5;
            float borderThickness = float((v_Flags >> 8u) & 0xFFu) * 0.5;
            float softShadowPad   = float((payload27 >> 16u) & 0x7FFu) * 0.5;
            vec2 boxHalf = (softShadowPad > 0.0) ? max(vec2(0.5), halfSize - vec2(softShadowPad)) : halfSize;
            float dist = sdRoundedBox(v_UvOrLocalPos, boxHalf, cornerRadius);
            float aa = max(max(fwidth(dist) * 0.75, softShadowPad * 0.8), 1e-4);
            float outerCoverage = smoothstep(aa, -aa, dist);
            if (borderThickness > 0.0) {
                float innerDist = dist + borderThickness;
                float innerCoverage = smoothstep(aa, -aa, innerDist);
                float borderMask = clamp(outerCoverage - innerCoverage * 0.35, 0.0, 1.0);
                o_FragColor = vec4(v_Color.rgb, v_Color.a * borderMask);
            } else {
                o_FragColor = vec4(v_Color.rgb, v_Color.a * outerCoverage);
            }
            return;
        }
        if (sdfMode == 1u) {
            uint asymBt = (payload27 >> 26u) & 1u;
            float r = (asymBt == 0u) ? (float(payload27 & 0xFFu) * 0.5) : float(payload27 & 0x3Fu);
            vec4 btVec = (asymBt == 0u)
                ? vec4(float((payload27 >> 8u) & 0xFFu) * 0.5)
                : vec4(
                    float((payload27 >>  6u) & 0x1Fu),
                    float((payload27 >> 11u) & 0x1Fu),
                    float((payload27 >> 16u) & 0x1Fu),
                    float((payload27 >> 21u) & 0x1Fu));
            float dist = sdRoundedBox(v_UvOrLocalPos, halfSize, r);
            float aa = max(fwidth(dist) * 0.75, 1e-4);
            float outerCoverage = smoothstep(aa, -aa, dist);
            vec2 innerCenter = vec2(0.5 * (btVec.x - btVec.z), 0.5 * (btVec.y - btVec.w));
            vec2 innerHalf = halfSize - vec2(0.5 * (btVec.x + btVec.z), 0.5 * (btVec.y + btVec.w));
            vec4 innerRadii = max(
                vec4(0.0),
                vec4(r) - vec4(
                    max(btVec.z, btVec.y),
                    max(btVec.z, btVec.w),
                    max(btVec.x, btVec.y),
                    max(btVec.x, btVec.w)));
            float innerCoverage = (innerHalf.x > 0.0 && innerHalf.y > 0.0)
                ? smoothstep(aa, -aa, sdRoundedBox4(v_UvOrLocalPos - innerCenter, innerHalf, innerRadii))
                : 0.0;
            o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0));
            return;
        }
        if (sdfMode == 2u) {
            uint scaleSel = (payload27 >> 24u) & 0x3u;
            float rStep = 0.5 * float(1u << scaleSel);
            float tl = float((payload27 >>  0u) & 0x3Fu) * rStep;
            float tr = float((payload27 >>  6u) & 0x3Fu) * rStep;
            float br = float((payload27 >> 12u) & 0x3Fu) * rStep;
            float bl = float((payload27 >> 18u) & 0x3Fu) * rStep;
            float dist = sdRoundedBox4(v_UvOrLocalPos, halfSize, vec4(tr, br, tl, bl));
            float aa = max(fwidth(dist) * 0.75, 1e-4);
            o_FragColor = vec4(v_Color.rgb, v_Color.a * smoothstep(aa, -aa, dist));
            return;
        }
        float rStep = (((payload27 >> 26u) & 1u) != 0u) ? 2.0 : 1.0;
        float tl = float((payload27 >>  0u) & 0x1Fu) * rStep;
        float tr = float((payload27 >>  5u) & 0x1Fu) * rStep;
        float br = float((payload27 >> 10u) & 0x1Fu) * rStep;
        float bl = float((payload27 >> 15u) & 0x1Fu) * rStep;
        float borderThickness = float((payload27 >> 20u) & 0x3Fu) * 0.5;
        vec4 outerRadii = vec4(tr, br, tl, bl);
        float dist = sdRoundedBox4(v_UvOrLocalPos, halfSize, outerRadii);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float outerCoverage = smoothstep(aa, -aa, dist);
        vec2 innerHalf = halfSize - vec2(borderThickness);
        float innerCoverage = (innerHalf.x > 0.0 && innerHalf.y > 0.0)
            ? smoothstep(aa, -aa, sdRoundedBox4(v_UvOrLocalPos, innerHalf, max(vec4(0.0), outerRadii - vec4(borderThickness))))
            : 0.0;
        o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0));
        return;
    }

    // 2: SdfTextGlyph (Analytical SDF Font AA)
    if (v_PrimType == 2u) {
        float sdfSample = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos).r;
        float edgeWidth = max(fwidth(sdfSample), 0.04);
        float alpha = smoothstep(0.5 - edgeWidth, 0.5 + edgeWidth, sdfSample);
        o_FragColor = vec4(v_Color.rgb, v_Color.a * alpha);
        return;
    }

    // 4: MsdfVectorPath (4-Channel MTSDF SVG <Path>, sharp corners + anisotropic screenPxRange AA)
    if (v_PrimType == 4u) {
        vec4 mtsdf = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos);
        float uvRange = max(float(v_Flags & 0xFFu), 1.0) / 4096.0;
        vec2 screenTexSize = vec2(1.0) / max(fwidth(v_UvOrLocalPos), vec2(1e-5));
        float screenPxRange = max(0.5 * dot(vec2(uvRange), screenTexSize), 1.0);
        float sd = median3(mtsdf.r, mtsdf.g, mtsdf.b);
        bool strokeOnly = (v_Flags & 0x8000u) != 0u;
        float fillCov = strokeOnly ? 0.0 : clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);
        float strokePx = float((v_Flags >> 8u) & 0x7Fu) * 0.5;
        if (strokePx > 0.0) {
            float trueDistPx = (mtsdf.a - 0.5) * screenPxRange;
            float strokeCov = clamp(0.5 + strokePx * 0.5 - abs(trueDistPx), 0.0, 1.0);
            fillCov = max(fillCov, strokeCov);
        }
        o_FragColor = vec4(v_Color.rgb, v_Color.a * fillCov);
        return;
    }

    // 5: SdfRadialArc (Analytical SDF Ring/Arc/Progressbar, 0 CPU tessellation)
    if (v_PrimType == 5u) {
        float startRad = float(v_Flags & 0x3Fu) * (6.2831853 / 64.0);
        float sweepRad = float((v_Flags >> 6u) & 0x3Fu) * (6.2831853 / 63.0);
        float innerRatio = float((v_Flags >> 12u) & 0x0Fu) * (1.0 / 16.0);
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float outerR = max(1.0, min(halfSize.x, halfSize.y));
        float innerR = innerRatio * outerR;
        float midR = 0.5 * (outerR + innerR);
        float halfThick = max(0.5, 0.5 * (outerR - innerR));
        float midAngle = startRad + 0.5 * sweepRad;
        float halfSweep = clamp(0.5 * sweepRad, 0.001, 3.14159265);
        float cs = cos(midAngle);
        float sn = sin(midAngle);
        vec2 q = vec2(
            v_UvOrLocalPos.x * cs + v_UvOrLocalPos.y * sn,
            abs(-v_UvOrLocalPos.x * sn + v_UvOrLocalPos.y * cs));
        vec2 sc = vec2(cos(halfSweep), sin(halfSweep));
        float dist = (sc.y * q.x > sc.x * q.y)
            ? (abs(length(q) - midR) - halfThick)
            : (length(q - sc * midR) - halfThick);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float cov = (sweepRad <= 0.001) ? 0.0 : smoothstep(aa, -aa, dist);
        o_FragColor = vec4(v_Color.rgb, v_Color.a * cov);
        return;
    }


    // 7: SdfEllipse (Analytical SDF Ellipse/Circle AA fill & hollow stroke)
    if (v_PrimType == 7u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float strokeThickness = float(v_Flags & 0x7FFFu) * 0.5;
        bool hollowStroke = (v_Flags & 0x8000u) != 0u;
        float dist = sdEllipseApprox(v_UvOrLocalPos, halfSize);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float outerCov = smoothstep(aa, -aa, dist);
        if (hollowStroke) {
            vec2 innerAb = halfSize - vec2(strokeThickness);
            float innerCov = (innerAb.x > 0.0 && innerAb.y > 0.0)
                ? smoothstep(aa, -aa, sdEllipseApprox(v_UvOrLocalPos, innerAb))
                : 0.0;
            o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCov - innerCov, 0.0, 1.0));
        } else {
            o_FragColor = vec4(v_Color.rgb, v_Color.a * outerCov);
        }
        return;
    }

    // 8: GradientRoundedRect (Multi-stop LinearGradientBrush / RadialGradientBrush 1D LUT + SDF AA)
    if (v_PrimType == 8u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float cornerRadius = float(v_Flags & 0x7FFFu) * 0.5;
        bool isEllipse = (v_Flags & 0x8000u) != 0u;
        uint brushId = (v_Flags >> 17u) & 0x0FFFu;
        float dist = isEllipse
            ? sdEllipseApprox(v_UvOrLocalPos, halfSize)
            : sdRoundedBox(v_UvOrLocalPos, halfSize, cornerRadius);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float shapeCov = smoothstep(aa, -aa, dist);

        ivec2 atlasDim = textureSize(u_GlyphAndUiAtlas, 0);
        int rampRow = atlasDim.y - 2 - (int(brushId) - 1) * 2;
        int metaRow = rampRow - 1;
        vec4 gradSample = vec4(1.0);
        if (brushId > 0u && metaRow >= 0 && atlasDim.x >= 8) {
            vec4 m0 = texelFetch(u_GlyphAndUiAtlas, ivec2(0, metaRow), 0);
            uint kind = uint(m0.r * 255.0 + 0.5);
            uint mappingMode = uint(m0.g * 255.0 + 0.5);
            uint spreadMethod = uint(m0.b * 255.0 + 0.5);
            float brushOpacity = m0.a;
            float p0x = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(1, metaRow), 0));
            float p0y = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(2, metaRow), 0));
            float p1x = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(3, metaRow), 0));
            float p1y = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(4, metaRow), 0));
            float rx  = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(5, metaRow), 0));
            float ry  = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(6, metaRow), 0));

            vec2 quadSize = max(halfSize * 2.0, vec2(1e-4));
            vec2 normUv = v_UvOrLocalPos / quadSize + vec2(0.5);
            vec2 samplePt = (mappingMode == 1u) ? (normUv * quadSize) : normUv;

            float rawT = 0.0;
            if (kind == 0u) {
                vec2 dir = vec2(p1x - p0x, p1y - p0y);
                float lenSq = dot(dir, dir);
                rawT = (lenSq > 1e-8) ? (dot(samplePt - vec2(p0x, p0y), dir) / lenSq) : 0.0;
            } else {
                vec2 safeRad = max(abs(vec2(rx, ry)), vec2(1e-5));
                vec2 sp = (samplePt - vec2(p0x, p0y)) / safeRad;
                vec2 fp = (vec2(p1x, p1y) - vec2(p0x, p0y)) / safeRad;
                float fLen = length(fp);
                if (fLen > 0.99) {
                    fp *= (0.99 / fLen);
                }
                vec2 d = sp - fp;
                float aCoef = dot(d, d);
                if (aCoef > 1e-8) {
                    float bCoef = 2.0 * dot(fp, d);
                    float cCoef = dot(fp, fp) - 1.0;
                    float disc = max(0.0, bCoef * bCoef - 4.0 * aCoef * cCoef);
                    float s = (-bCoef + sqrt(disc)) / (2.0 * aCoef);
                    rawT = (s > 1e-6) ? (1.0 / s) : 0.0;
                }
            }
            float t = applyGradientSpread(rawT, spreadMethod);
            float rampSpan = min(float(atlasDim.x), 256.0);
            float uTex = (0.5 + t * max(1.0, rampSpan - 1.0)) / float(atlasDim.x);
            float vTex = (float(rampRow) + 0.5) / float(atlasDim.y);
            gradSample = textureLod(u_GlyphAndUiAtlas, vec2(uTex, vTex), 0.0);
            gradSample.a *= brushOpacity;
        }
        o_FragColor = vec4(gradSample.rgb * v_Color.rgb, gradSample.a * v_Color.a * shapeCov);
        return;
    }

    // 3: TexturedQuad / 6: DynamicMesh
    vec4 texel = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos);
    o_FragColor = texel * v_Color;
}
)";

constexpr std::string_view kUberVertexShaderWebGL2 = R"(#version 300 es
precision highp float;
precision highp int;
// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (WebGL2 #version 300 es + glBufferSubData Ring Stream)
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in uint a_PackedRgba;
layout(location = 3) in uint a_PrimFlags;

uniform vec2 u_ViewportSize;

out vec2 v_UvOrLocalPos;
out vec4 v_Color;
flat out uint v_PrimType;
flat out uint v_Flags;
flat out vec2 v_QuadHalfSize;

void main() {
    vec2 ndc = (a_Position / max(u_ViewportSize, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_UvOrLocalPos = a_TexCoord;
    v_QuadHalfSize = abs(a_TexCoord);
    v_Color = vec4(
        float((a_PackedRgba >>  0u) & 0xFFu) / 255.0,
        float((a_PackedRgba >>  8u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 16u) & 0xFFu) / 255.0,
        float((a_PackedRgba >> 24u) & 0xFFu) / 255.0
    );
    uint basePrim = a_PrimFlags & 0x0007u;
    v_PrimType = (basePrim == 0u && (a_PrimFlags & 0x0008u) != 0u) ? 8u : basePrim;
    v_Flags    = ((a_PrimFlags >> 16u) & 0xFFFFu) | ((a_PrimFlags & 0xFFF8u) << 13u);
}
)";

constexpr std::string_view kUberPixelShaderWebGL2 = R"(#version 300 es
precision highp float;
precision highp int;
#define AERO_WEBGL2_GRAYSCALE_SDF_AA 1
#define AERO_WEBGL2_RING_BUFFER_SUBDATA 1
// AeroGUI 2.0 WebGL2 UberPixelShader (Grayscale SDF AA Text + 4-Channel MTSDF Vector Paths)
in vec2 v_UvOrLocalPos;
in vec4 v_Color;
flat in uint v_PrimType;
flat in uint v_Flags;
flat in vec2 v_QuadHalfSize;

uniform sampler2D u_GlyphAndUiAtlas;
uniform vec2 u_QuadHalfExtent;

out vec4 o_FragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    float cr = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - cr;
}

// r = vec4(tr, br, tl, bl)
float sdRoundedBox4(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    float cr = (p.y > 0.0) ? r.y : r.x;
    cr = min(cr, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - cr;
}

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float sdEllipseApprox(vec2 p, vec2 ab) {
    vec2 safeAb = max(ab, vec2(0.5));
    vec2 q = p / safeAb;
    float k0 = length(q);
    if (k0 < 1e-4) {
        return -min(safeAb.x, safeAb.y);
    }
    float k1 = length(q / safeAb);
    return k0 * (k0 - 1.0) / max(k1, 1e-5);
}

float unpackAtlasFloat32(vec4 texel) {
    uint b0 = uint(texel.r * 255.0 + 0.5);
    uint b1 = uint(texel.g * 255.0 + 0.5);
    uint b2 = uint(texel.b * 255.0 + 0.5);
    uint b3 = uint(texel.a * 255.0 + 0.5);
    return uintBitsToFloat(b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u));
}

float applyGradientSpread(float rawT, uint spreadMethod) {
    if (spreadMethod == 2u) {
        return rawT - floor(rawT);
    }
    if (spreadMethod == 1u) {
        float m = mod(abs(rawT), 2.0);
        return (m > 1.0) ? (2.0 - m) : m;
    }
    return clamp(rawT, 0.0, 1.0);
}

void main() {
    if (v_PrimType == 0u) {
        o_FragColor = v_Color;
        return;
    }

    if (v_PrimType == 1u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        uint sdfMode = (v_Flags >> 16u) & 0x3u;
        uint payload27 = (v_Flags & 0xFFFFu) | (((v_Flags >> 18u) & 0x7FFu) << 16u);
        if (sdfMode == 0u) {
            float cornerRadius    = float(v_Flags & 0xFFu) * 0.5;
            float borderThickness = float((v_Flags >> 8u) & 0xFFu) * 0.5;
            float softShadowPad   = float((payload27 >> 16u) & 0x7FFu) * 0.5;
            vec2 boxHalf = (softShadowPad > 0.0) ? max(vec2(0.5), halfSize - vec2(softShadowPad)) : halfSize;
            float dist = sdRoundedBox(v_UvOrLocalPos, boxHalf, cornerRadius);
            float aa = max(max(fwidth(dist) * 0.75, softShadowPad * 0.8), 1e-4);
            float outerCoverage = smoothstep(aa, -aa, dist);
            if (borderThickness > 0.0) {
                float innerDist = dist + borderThickness;
                float innerCoverage = smoothstep(aa, -aa, innerDist);
                o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCoverage - innerCoverage * 0.35, 0.0, 1.0));
            } else {
                o_FragColor = vec4(v_Color.rgb, v_Color.a * outerCoverage);
            }
            return;
        }
        if (sdfMode == 1u) {
            uint asymBt = (payload27 >> 26u) & 1u;
            float r = (asymBt == 0u) ? (float(payload27 & 0xFFu) * 0.5) : float(payload27 & 0x3Fu);
            vec4 btVec = (asymBt == 0u)
                ? vec4(float((payload27 >> 8u) & 0xFFu) * 0.5)
                : vec4(
                    float((payload27 >>  6u) & 0x1Fu),
                    float((payload27 >> 11u) & 0x1Fu),
                    float((payload27 >> 16u) & 0x1Fu),
                    float((payload27 >> 21u) & 0x1Fu));
            float dist = sdRoundedBox(v_UvOrLocalPos, halfSize, r);
            float aa = max(fwidth(dist) * 0.75, 1e-4);
            float outerCoverage = smoothstep(aa, -aa, dist);
            vec2 innerCenter = vec2(0.5 * (btVec.x - btVec.z), 0.5 * (btVec.y - btVec.w));
            vec2 innerHalf = halfSize - vec2(0.5 * (btVec.x + btVec.z), 0.5 * (btVec.y + btVec.w));
            vec4 innerRadii = max(
                vec4(0.0),
                vec4(r) - vec4(
                    max(btVec.z, btVec.y),
                    max(btVec.z, btVec.w),
                    max(btVec.x, btVec.y),
                    max(btVec.x, btVec.w)));
            float innerCoverage = (innerHalf.x > 0.0 && innerHalf.y > 0.0)
                ? smoothstep(aa, -aa, sdRoundedBox4(v_UvOrLocalPos - innerCenter, innerHalf, innerRadii))
                : 0.0;
            o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0));
            return;
        }
        if (sdfMode == 2u) {
            uint scaleSel = (payload27 >> 24u) & 0x3u;
            float rStep = 0.5 * float(1u << scaleSel);
            float tl = float((payload27 >>  0u) & 0x3Fu) * rStep;
            float tr = float((payload27 >>  6u) & 0x3Fu) * rStep;
            float br = float((payload27 >> 12u) & 0x3Fu) * rStep;
            float bl = float((payload27 >> 18u) & 0x3Fu) * rStep;
            float dist = sdRoundedBox4(v_UvOrLocalPos, halfSize, vec4(tr, br, tl, bl));
            float aa = max(fwidth(dist) * 0.75, 1e-4);
            o_FragColor = vec4(v_Color.rgb, v_Color.a * smoothstep(aa, -aa, dist));
            return;
        }
        float rStep = (((payload27 >> 26u) & 1u) != 0u) ? 2.0 : 1.0;
        float tl = float((payload27 >>  0u) & 0x1Fu) * rStep;
        float tr = float((payload27 >>  5u) & 0x1Fu) * rStep;
        float br = float((payload27 >> 10u) & 0x1Fu) * rStep;
        float bl = float((payload27 >> 15u) & 0x1Fu) * rStep;
        float borderThickness = float((payload27 >> 20u) & 0x3Fu) * 0.5;
        vec4 outerRadii = vec4(tr, br, tl, bl);
        float dist = sdRoundedBox4(v_UvOrLocalPos, halfSize, outerRadii);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float outerCoverage = smoothstep(aa, -aa, dist);
        vec2 innerHalf = halfSize - vec2(borderThickness);
        float innerCoverage = (innerHalf.x > 0.0 && innerHalf.y > 0.0)
            ? smoothstep(aa, -aa, sdRoundedBox4(v_UvOrLocalPos, innerHalf, max(vec4(0.0), outerRadii - vec4(borderThickness))))
            : 0.0;
        o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0));
        return;
    }

    if (v_PrimType == 2u) {
        // WebGL2 grayscale SDF AA text path (avoids dual-source blending requirement in WebGL2)
        float sdfDist = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos).r;
        float fw = clamp(fwidth(sdfDist) * 0.85, 0.025, 0.22);
        float grayscaleAlpha = smoothstep(0.5 - fw, 0.5 + fw, sdfDist);
        o_FragColor = vec4(v_Color.rgb, v_Color.a * grayscaleAlpha);
        return;
    }

    if (v_PrimType == 4u) {
        vec4 mtsdf = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos);
        float uvRange = max(float(v_Flags & 0xFFu), 1.0) / 4096.0;
        vec2 screenTexSize = vec2(1.0) / max(fwidth(v_UvOrLocalPos), vec2(1e-5));
        float screenPxRange = max(0.5 * dot(vec2(uvRange), screenTexSize), 1.0);
        float sd = median3(mtsdf.r, mtsdf.g, mtsdf.b);
        bool strokeOnly = (v_Flags & 0x8000u) != 0u;
        float fillCov = strokeOnly ? 0.0 : clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);
        float strokePx = float((v_Flags >> 8u) & 0x7Fu) * 0.5;
        if (strokePx > 0.0) {
            float trueDistPx = (mtsdf.a - 0.5) * screenPxRange;
            fillCov = max(fillCov, clamp(0.5 + strokePx * 0.5 - abs(trueDistPx), 0.0, 1.0));
        }
        o_FragColor = vec4(v_Color.rgb, v_Color.a * fillCov);
        return;
    }

    if (v_PrimType == 5u) {
        float startRad = float(v_Flags & 0x3Fu) * (6.2831853 / 64.0);
        float sweepRad = float((v_Flags >> 6u) & 0x3Fu) * (6.2831853 / 63.0);
        float innerRatio = float((v_Flags >> 12u) & 0x0Fu) * (1.0 / 16.0);
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float outerR = max(1.0, min(halfSize.x, halfSize.y));
        float innerR = innerRatio * outerR;
        float midR = 0.5 * (outerR + innerR);
        float halfThick = max(0.5, 0.5 * (outerR - innerR));
        float midAngle = startRad + 0.5 * sweepRad;
        float halfSweep = clamp(0.5 * sweepRad, 0.001, 3.14159265);
        float cs = cos(midAngle);
        float sn = sin(midAngle);
        vec2 q = vec2(
            v_UvOrLocalPos.x * cs + v_UvOrLocalPos.y * sn,
            abs(-v_UvOrLocalPos.x * sn + v_UvOrLocalPos.y * cs));
        vec2 sc = vec2(cos(halfSweep), sin(halfSweep));
        float dist = (sc.y * q.x > sc.x * q.y)
            ? (abs(length(q) - midR) - halfThick)
            : (length(q - sc * midR) - halfThick);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float cov = (sweepRad <= 0.001) ? 0.0 : smoothstep(aa, -aa, dist);
        o_FragColor = vec4(v_Color.rgb, v_Color.a * cov);
        return;
    }


    // 7: SdfEllipse (Analytical SDF Ellipse/Circle AA fill & hollow stroke)
    if (v_PrimType == 7u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float strokeThickness = float(v_Flags & 0x7FFFu) * 0.5;
        bool hollowStroke = (v_Flags & 0x8000u) != 0u;
        float dist = sdEllipseApprox(v_UvOrLocalPos, halfSize);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float outerCov = smoothstep(aa, -aa, dist);
        if (hollowStroke) {
            vec2 innerAb = halfSize - vec2(strokeThickness);
            float innerCov = (innerAb.x > 0.0 && innerAb.y > 0.0)
                ? smoothstep(aa, -aa, sdEllipseApprox(v_UvOrLocalPos, innerAb))
                : 0.0;
            o_FragColor = vec4(v_Color.rgb, v_Color.a * clamp(outerCov - innerCov, 0.0, 1.0));
        } else {
            o_FragColor = vec4(v_Color.rgb, v_Color.a * outerCov);
        }
        return;
    }

    // 8: GradientRoundedRect (Multi-stop LinearGradientBrush / RadialGradientBrush 1D LUT + SDF AA)
    if (v_PrimType == 8u) {
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float cornerRadius = float(v_Flags & 0x7FFFu) * 0.5;
        bool isEllipse = (v_Flags & 0x8000u) != 0u;
        uint brushId = (v_Flags >> 17u) & 0x0FFFu;
        float dist = isEllipse
            ? sdEllipseApprox(v_UvOrLocalPos, halfSize)
            : sdRoundedBox(v_UvOrLocalPos, halfSize, cornerRadius);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
        float shapeCov = smoothstep(aa, -aa, dist);

        ivec2 atlasDim = textureSize(u_GlyphAndUiAtlas, 0);
        int rampRow = atlasDim.y - 2 - (int(brushId) - 1) * 2;
        int metaRow = rampRow - 1;
        vec4 gradSample = vec4(1.0);
        if (brushId > 0u && metaRow >= 0 && atlasDim.x >= 8) {
            vec4 m0 = texelFetch(u_GlyphAndUiAtlas, ivec2(0, metaRow), 0);
            uint kind = uint(m0.r * 255.0 + 0.5);
            uint mappingMode = uint(m0.g * 255.0 + 0.5);
            uint spreadMethod = uint(m0.b * 255.0 + 0.5);
            float brushOpacity = m0.a;
            float p0x = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(1, metaRow), 0));
            float p0y = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(2, metaRow), 0));
            float p1x = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(3, metaRow), 0));
            float p1y = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(4, metaRow), 0));
            float rx  = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(5, metaRow), 0));
            float ry  = unpackAtlasFloat32(texelFetch(u_GlyphAndUiAtlas, ivec2(6, metaRow), 0));

            vec2 quadSize = max(halfSize * 2.0, vec2(1e-4));
            vec2 normUv = v_UvOrLocalPos / quadSize + vec2(0.5);
            vec2 samplePt = (mappingMode == 1u) ? (normUv * quadSize) : normUv;

            float rawT = 0.0;
            if (kind == 0u) {
                vec2 dir = vec2(p1x - p0x, p1y - p0y);
                float lenSq = dot(dir, dir);
                rawT = (lenSq > 1e-8) ? (dot(samplePt - vec2(p0x, p0y), dir) / lenSq) : 0.0;
            } else {
                vec2 safeRad = max(abs(vec2(rx, ry)), vec2(1e-5));
                vec2 sp = (samplePt - vec2(p0x, p0y)) / safeRad;
                vec2 fp = (vec2(p1x, p1y) - vec2(p0x, p0y)) / safeRad;
                float fLen = length(fp);
                if (fLen > 0.99) {
                    fp *= (0.99 / fLen);
                }
                vec2 d = sp - fp;
                float aCoef = dot(d, d);
                if (aCoef > 1e-8) {
                    float bCoef = 2.0 * dot(fp, d);
                    float cCoef = dot(fp, fp) - 1.0;
                    float disc = max(0.0, bCoef * bCoef - 4.0 * aCoef * cCoef);
                    float s = (-bCoef + sqrt(disc)) / (2.0 * aCoef);
                    rawT = (s > 1e-6) ? (1.0 / s) : 0.0;
                }
            }
            float t = applyGradientSpread(rawT, spreadMethod);
            float rampSpan = min(float(atlasDim.x), 256.0);
            float uTex = (0.5 + t * max(1.0, rampSpan - 1.0)) / float(atlasDim.x);
            float vTex = (float(rampRow) + 0.5) / float(atlasDim.y);
            gradSample = textureLod(u_GlyphAndUiAtlas, vec2(uTex, vTex), 0.0);
            gradSample.a *= brushOpacity;
        }
        o_FragColor = vec4(gradSample.rgb * v_Color.rgb, gradSample.a * v_Color.a * shapeCov);
        return;
    }

    o_FragColor = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos) * v_Color;
}
)";

constexpr std::string_view kUberShaderWGSL = R"(// AeroGUI 2.0 Unified WebGPU WGSL UberShader (24B AeroVertex + Analytical SDF & 4-Channel MTSDF AA)
struct AeroUniforms {
    viewportSize : vec2<f32>,
    quadHalfExtent : vec2<f32>,
};
@group(0) @binding(0) var<uniform> u_Globals : AeroUniforms;
@group(0) @binding(1) var u_AtlasSampler : sampler;
@group(0) @binding(2) var u_AtlasTex : texture_2d<f32>;

struct VertexIn {
    @location(0) position : vec2<f32>,
    @location(1) uvOrLocal : vec2<f32>,
    @location(2) packedRgba : u32,
    @location(3) primFlags : u32,
};

struct VertexOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) uvOrLocal : vec2<f32>,
    @location(1) color : vec4<f32>,
    @location(2) @interpolate(flat) primType : u32,
    @location(3) @interpolate(flat) flags : u32,
    @location(4) @interpolate(flat) quadHalfSize : vec2<f32>,
};

fn sdRoundedBox(p : vec2<f32>, b : vec2<f32>, r : f32) -> f32 {
    let cr = min(r, min(b.x, b.y));
    let q = abs(p) - b + vec2<f32>(cr, cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0, 0.0))) - cr;
}

// r = vec4<f32>(tr, br, tl, bl)
fn sdRoundedBox4(p : vec2<f32>, b : vec2<f32>, r : vec4<f32>) -> f32 {
    let rx = select(r.zw, r.xy, p.x > 0.0);
    var cr = select(rx.x, rx.y, p.y > 0.0);
    cr = min(cr, min(b.x, b.y));
    let q = abs(p) - b + vec2<f32>(cr, cr);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0, 0.0))) - cr;
}

fn median3(r : f32, g : f32, b : f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

fn sdEllipseApprox(p : vec2<f32>, ab : vec2<f32>) -> f32 {
    let safeAb = max(ab, vec2<f32>(0.5, 0.5));
    let q = p / safeAb;
    let k0 = length(q);
    if (k0 < 1e-4) {
        return -min(safeAb.x, safeAb.y);
    }
    let k1 = length(q / safeAb);
    return k0 * (k0 - 1.0) / max(k1, 1e-5);
}

fn unpackAtlasFloat32(texel : vec4<f32>) -> f32 {
    let b0 = u32(texel.r * 255.0 + 0.5);
    let b1 = u32(texel.g * 255.0 + 0.5);
    let b2 = u32(texel.b * 255.0 + 0.5);
    let b3 = u32(texel.a * 255.0 + 0.5);
    return bitcast<f32>(b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u));
}

fn applyGradientSpread(rawT : f32, spreadMethod : u32) -> f32 {
    if (spreadMethod == 2u) {
        return rawT - floor(rawT);
    }
    if (spreadMethod == 1u) {
        let a = abs(rawT);
        let m = a - 2.0 * floor(a * 0.5);
        return select(m, 2.0 - m, m > 1.0);
    }
    return clamp(rawT, 0.0, 1.0);
}

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
    var out : VertexOut;
    let vp = max(u_Globals.viewportSize, vec2<f32>(1.0, 1.0));
    var ndc = (in.position / vp) * 2.0 - vec2<f32>(1.0, 1.0);
    ndc.y = -ndc.y;
    out.clipPos = vec4<f32>(ndc, 0.0, 1.0);
    out.uvOrLocal = in.uvOrLocal;
    out.quadHalfSize = abs(in.uvOrLocal);
    out.color = vec4<f32>(
        f32((in.packedRgba >>  0u) & 0xFFu) / 255.0,
        f32((in.packedRgba >>  8u) & 0xFFu) / 255.0,
        f32((in.packedRgba >> 16u) & 0xFFu) / 255.0,
        f32((in.packedRgba >> 24u) & 0xFFu) / 255.0
    );
    let basePrim = in.primFlags & 0x0007u;
    out.primType = select(basePrim, 8u, basePrim == 0u && (in.primFlags & 0x0008u) != 0u);
    out.flags = ((in.primFlags >> 16u) & 0xFFFFu) | ((in.primFlags & 0xFFF8u) << 13u);
    return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
    if (in.primType == 0u) {
        return in.color;
    }
    if (in.primType == 1u) {
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let sdfMode = (in.flags >> 16u) & 0x3u;
        let payload27 = (in.flags & 0xFFFFu) | (((in.flags >> 18u) & 0x7FFu) << 16u);
        if (sdfMode == 0u) {
            let radius = f32(in.flags & 0xFFu) * 0.5;
            let borderThickness = f32((in.flags >> 8u) & 0xFFu) * 0.5;
            let softShadowPad = f32((payload27 >> 16u) & 0x7FFu) * 0.5;
            let boxHalf = select(halfSize, max(vec2<f32>(0.5, 0.5), halfSize - vec2<f32>(softShadowPad, softShadowPad)), softShadowPad > 0.0);
            let dist = sdRoundedBox(in.uvOrLocal, boxHalf, radius);
            let aa = max(max(fwidth(dist) * 0.75, softShadowPad * 0.8), 1e-4);
            let outerCov = smoothstep(aa, -aa, dist);
            if (borderThickness > 0.0) {
                let innerDist = dist + borderThickness;
                let innerCov = smoothstep(aa, -aa, innerDist);
                return vec4<f32>(in.color.rgb, in.color.a * clamp(outerCov - innerCov * 0.35, 0.0, 1.0));
            }
            return vec4<f32>(in.color.rgb, in.color.a * outerCov);
        }
        if (sdfMode == 1u) {
            let asymBt = (payload27 >> 26u) & 1u;
            let r = select(f32(payload27 & 0xFFu) * 0.5, f32(payload27 & 0x3Fu), asymBt != 0u);
            let btVec = select(
                vec4<f32>(f32((payload27 >> 8u) & 0xFFu) * 0.5),
                vec4<f32>(
                    f32((payload27 >>  6u) & 0x1Fu),
                    f32((payload27 >> 11u) & 0x1Fu),
                    f32((payload27 >> 16u) & 0x1Fu),
                    f32((payload27 >> 21u) & 0x1Fu)),
                asymBt != 0u);
            let dist = sdRoundedBox(in.uvOrLocal, halfSize, r);
            let aa = max(fwidth(dist) * 0.75, 1e-4);
            let outerCov = smoothstep(aa, -aa, dist);
            let innerCenter = vec2<f32>(0.5 * (btVec.x - btVec.z), 0.5 * (btVec.y - btVec.w));
            let innerHalf = halfSize - vec2<f32>(0.5 * (btVec.x + btVec.z), 0.5 * (btVec.y + btVec.w));
            let innerRadii = max(
                vec4<f32>(0.0),
                vec4<f32>(r) - vec4<f32>(
                    max(btVec.z, btVec.y),
                    max(btVec.z, btVec.w),
                    max(btVec.x, btVec.y),
                    max(btVec.x, btVec.w)));
            var innerCov = 0.0;
            if (innerHalf.x > 0.0 && innerHalf.y > 0.0) {
                innerCov = smoothstep(aa, -aa, sdRoundedBox4(in.uvOrLocal - innerCenter, innerHalf, innerRadii));
            }
            return vec4<f32>(in.color.rgb, in.color.a * clamp(outerCov - innerCov, 0.0, 1.0));
        }
        if (sdfMode == 2u) {
            let scaleSel = (payload27 >> 24u) & 0x3u;
            let rStep = 0.5 * f32(1u << scaleSel);
            let tl = f32((payload27 >>  0u) & 0x3Fu) * rStep;
            let tr = f32((payload27 >>  6u) & 0x3Fu) * rStep;
            let br = f32((payload27 >> 12u) & 0x3Fu) * rStep;
            let bl = f32((payload27 >> 18u) & 0x3Fu) * rStep;
            let dist = sdRoundedBox4(in.uvOrLocal, halfSize, vec4<f32>(tr, br, tl, bl));
            let aa = max(fwidth(dist) * 0.75, 1e-4);
            return vec4<f32>(in.color.rgb, in.color.a * smoothstep(aa, -aa, dist));
        }
        let rStep = select(1.0, 2.0, ((payload27 >> 26u) & 1u) != 0u);
        let tl = f32((payload27 >>  0u) & 0x1Fu) * rStep;
        let tr = f32((payload27 >>  5u) & 0x1Fu) * rStep;
        let br = f32((payload27 >> 10u) & 0x1Fu) * rStep;
        let bl = f32((payload27 >> 15u) & 0x1Fu) * rStep;
        let borderThickness = f32((payload27 >> 20u) & 0x3Fu) * 0.5;
        let outerRadii = vec4<f32>(tr, br, tl, bl);
        let dist = sdRoundedBox4(in.uvOrLocal, halfSize, outerRadii);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        let outerCov = smoothstep(aa, -aa, dist);
        let innerHalf = halfSize - vec2<f32>(borderThickness, borderThickness);
        var innerCov = 0.0;
        if (innerHalf.x > 0.0 && innerHalf.y > 0.0) {
            innerCov = smoothstep(
                aa,
                -aa,
                sdRoundedBox4(in.uvOrLocal, innerHalf, max(vec4<f32>(0.0), outerRadii - vec4<f32>(borderThickness))));
        }
        return vec4<f32>(in.color.rgb, in.color.a * clamp(outerCov - innerCov, 0.0, 1.0));
    }
    if (in.primType == 2u) {
        let sdf = textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal).r;
        let fw = max(fwidth(sdf), 0.04);
        let alpha = smoothstep(0.5 - fw, 0.5 + fw, sdf);
        return vec4<f32>(in.color.rgb, in.color.a * alpha);
    }
    if (in.primType == 4u) {
        let mtsdf = textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal);
        let uvRange = max(f32(in.flags & 0xFFu), 1.0) / 4096.0;
        let screenTexSize = vec2<f32>(1.0, 1.0) / max(fwidth(in.uvOrLocal), vec2<f32>(1e-5, 1e-5));
        let screenPxRange = max(0.5 * dot(vec2<f32>(uvRange, uvRange), screenTexSize), 1.0);
        let sd = median3(mtsdf.r, mtsdf.g, mtsdf.b);
        let strokeOnly = (in.flags & 0x8000u) != 0u;
        var fillCov = select(clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0), 0.0, strokeOnly);
        let strokePx = f32((in.flags >> 8u) & 0x7Fu) * 0.5;
        if (strokePx > 0.0) {
            let trueDistPx = (mtsdf.a - 0.5) * screenPxRange;
            fillCov = max(fillCov, clamp(0.5 + strokePx * 0.5 - abs(trueDistPx), 0.0, 1.0));
        }
        return vec4<f32>(in.color.rgb, in.color.a * fillCov);
    }
    if (in.primType == 5u) {
        let startRad = f32(in.flags & 0x3Fu) * (6.2831853 / 64.0);
        let sweepRad = f32((in.flags >> 6u) & 0x3Fu) * (6.2831853 / 63.0);
        let innerRatio = f32((in.flags >> 12u) & 0x0Fu) * (1.0 / 16.0);
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let outerR = max(1.0, min(halfSize.x, halfSize.y));
        let innerR = innerRatio * outerR;
        let midR = 0.5 * (outerR + innerR);
        let halfThick = max(0.5, 0.5 * (outerR - innerR));
        let midAngle = startRad + 0.5 * sweepRad;
        let halfSweep = clamp(0.5 * sweepRad, 0.001, 3.14159265);
        let cs = cos(midAngle);
        let sn = sin(midAngle);
        let q = vec2<f32>(
            in.uvOrLocal.x * cs + in.uvOrLocal.y * sn,
            abs(-in.uvOrLocal.x * sn + in.uvOrLocal.y * cs));
        let sc = vec2<f32>(cos(halfSweep), sin(halfSweep));
        let dist = select(
            length(q - sc * midR) - halfThick,
            abs(length(q) - midR) - halfThick,
            sc.y * q.x > sc.x * q.y);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        let cov = select(smoothstep(aa, -aa, dist), 0.0, sweepRad <= 0.001);
        return vec4<f32>(in.color.rgb, in.color.a * cov);
    }
    if (in.primType == 7u) {
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let strokeThickness = f32(in.flags & 0x7FFFu) * 0.5;
        let hollowStroke = (in.flags & 0x8000u) != 0u;
        let dist = sdEllipseApprox(in.uvOrLocal, halfSize);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        let outerCov = smoothstep(aa, -aa, dist);
        if (hollowStroke) {
            let innerAb = halfSize - vec2<f32>(strokeThickness, strokeThickness);
            var innerCov = 0.0;
            if (innerAb.x > 0.0 && innerAb.y > 0.0) {
                innerCov = smoothstep(aa, -aa, sdEllipseApprox(in.uvOrLocal, innerAb));
            }
            return vec4<f32>(in.color.rgb, in.color.a * clamp(outerCov - innerCov, 0.0, 1.0));
        }
        return vec4<f32>(in.color.rgb, in.color.a * outerCov);
    }
    if (in.primType == 8u) {
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let cornerRadius = f32(in.flags & 0x7FFFu) * 0.5;
        let isEllipse = (in.flags & 0x8000u) != 0u;
        let brushId = (in.flags >> 17u) & 0x0FFFu;
        let dist = select(
            sdRoundedBox(in.uvOrLocal, halfSize, cornerRadius),
            sdEllipseApprox(in.uvOrLocal, halfSize),
            isEllipse);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        let shapeCov = smoothstep(aa, -aa, dist);

        let atlasDim = vec2<i32>(textureDimensions(u_AtlasTex, 0));
        let rampRow = atlasDim.y - 2 - (i32(brushId) - 1) * 2;
        let metaRow = rampRow - 1;
        var gradSample = vec4<f32>(1.0, 1.0, 1.0, 1.0);
        if (brushId > 0u && metaRow >= 0 && atlasDim.x >= 8) {
            let m0 = textureLoad(u_AtlasTex, vec2<i32>(0, metaRow), 0);
            let kind = u32(m0.r * 255.0 + 0.5);
            let mappingMode = u32(m0.g * 255.0 + 0.5);
            let spreadMethod = u32(m0.b * 255.0 + 0.5);
            let brushOpacity = m0.a;
            let p0x = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(1, metaRow), 0));
            let p0y = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(2, metaRow), 0));
            let p1x = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(3, metaRow), 0));
            let p1y = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(4, metaRow), 0));
            let rx  = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(5, metaRow), 0));
            let ry  = unpackAtlasFloat32(textureLoad(u_AtlasTex, vec2<i32>(6, metaRow), 0));

            let quadSize = max(halfSize * 2.0, vec2<f32>(1e-4, 1e-4));
            let normUv = in.uvOrLocal / quadSize + vec2<f32>(0.5, 0.5);
            let samplePt = select(normUv, normUv * quadSize, mappingMode == 1u);

            var rawT = 0.0;
            if (kind == 0u) {
                let dir = vec2<f32>(p1x - p0x, p1y - p0y);
                let lenSq = dot(dir, dir);
                if (lenSq > 1e-8) {
                    rawT = dot(samplePt - vec2<f32>(p0x, p0y), dir) / lenSq;
                }
            } else {
                let safeRad = max(abs(vec2<f32>(rx, ry)), vec2<f32>(1e-5, 1e-5));
                let sp = (samplePt - vec2<f32>(p0x, p0y)) / safeRad;
                var fp = (vec2<f32>(p1x, p1y) - vec2<f32>(p0x, p0y)) / safeRad;
                let fLen = length(fp);
                if (fLen > 0.99) {
                    fp = fp * (0.99 / fLen);
                }
                let d = sp - fp;
                let aCoef = dot(d, d);
                if (aCoef > 1e-8) {
                    let bCoef = 2.0 * dot(fp, d);
                    let cCoef = dot(fp, fp) - 1.0;
                    let disc = max(0.0, bCoef * bCoef - 4.0 * aCoef * cCoef);
                    let s = (-bCoef + sqrt(disc)) / (2.0 * aCoef);
                    if (s > 1e-6) {
                        rawT = 1.0 / s;
                    }
                }
            }
            let t = applyGradientSpread(rawT, spreadMethod);
            let rampSpan = min(f32(atlasDim.x), 256.0);
            let uTex = (0.5 + t * max(1.0, rampSpan - 1.0)) / f32(atlasDim.x);
            let vTex = (f32(rampRow) + 0.5) / f32(atlasDim.y);
            gradSample = textureSampleLevel(u_AtlasTex, u_AtlasSampler, vec2<f32>(uTex, vTex), 0.0);
            gradSample.a = gradSample.a * brushOpacity;
        }
        return vec4<f32>(gradSample.rgb * in.color.rgb, gradSample.a * in.color.a * shapeCov);
    }
    return textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal) * in.color;
}
)";

[[nodiscard]] bool SameBatchScissor(const GpuClipDrawBatch& batch, const CommandPageHeader& hdr) noexcept {
    if (batch.textureId != hdr.reserved[0]) {
        return false;
    }
    const bool hdrScissor = (hdr.flags & CommandPageFlag_ScissorClipActive) != 0u;
    if (batch.scissorActive != hdrScissor) {
        return false;
    }
    if (!hdrScissor) {
        return true;
    }
    return std::fabs(batch.scissorRect.x - hdr.clipX) < 0.01f &&
           std::fabs(batch.scissorRect.y - hdr.clipY) < 0.01f &&
           std::fabs(batch.scissorRect.width - hdr.clipW) < 0.01f &&
           std::fabs(batch.scissorRect.height - hdr.clipH) < 0.01f;
}

void BlendPixelOver(
    std::uint8_t* dstRgba,
    std::uint8_t srcR,
    std::uint8_t srcG,
    std::uint8_t srcB,
    float srcAlpha01) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const float invA = 1.0f - a;
    dstRgba[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcR) * a + static_cast<float>(dstRgba[0]) * invA + 0.5f, 0.0f, 255.0f));
    dstRgba[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcG) * a + static_cast<float>(dstRgba[1]) * invA + 0.5f, 0.0f, 255.0f));
    dstRgba[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcB) * a + static_cast<float>(dstRgba[2]) * invA + 0.5f, 0.0f, 255.0f));
    dstRgba[3] = 255u;
}

struct PremulOffscreenLayer {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<float> premulRgba;
};

void BlendPremulOver(
    PremulOffscreenLayer& layer,
    int px,
    int py,
    float premulR,
    float premulG,
    float premulB,
    float srcAlpha01) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const int lx = px - layer.originX;
    const int ly = py - layer.originY;
    if (lx < 0 || lx >= layer.width || ly < 0 || ly >= layer.height) {
        return;
    }
    float* dst = layer.premulRgba.data() +
        (static_cast<std::size_t>(ly) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(lx)) * 4u;
    const float invA = 1.0f - a;
    dst[0] = premulR + dst[0] * invA;
    dst[1] = premulG + dst[1] * invA;
    dst[2] = premulB + dst[2] * invA;
    dst[3] = a + dst[3] * invA;
}

void SeparableBoxBlurPremul3Pass(PremulOffscreenLayer& layer, int boxRadius) {
    if (boxRadius <= 0 || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const int w = layer.width;
    const int h = layer.height;
    const float invKernel = 1.0f / static_cast<float>(boxRadius * 2 + 1);
    std::vector<float> scratch(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0.0f);

    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal 1D sliding-window box blur: premulRgba -> scratch
        for (int y = 0; y < h; ++y) {
            const float* srcRow = layer.premulRgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
            float* dstRow = scratch.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
            float sumR = 0.0f;
            float sumG = 0.0f;
            float sumB = 0.0f;
            float sumA = 0.0f;
            for (int k = -boxRadius; k <= boxRadius; ++k) {
                const int sx = std::clamp(k, 0, w - 1);
                const float* p = srcRow + static_cast<std::size_t>(sx) * 4u;
                sumR += p[0];
                sumG += p[1];
                sumB += p[2];
                sumA += p[3];
            }
            for (int x = 0; x < w; ++x) {
                float* outP = dstRow + static_cast<std::size_t>(x) * 4u;
                outP[0] = sumR * invKernel;
                outP[1] = sumG * invKernel;
                outP[2] = sumB * invKernel;
                outP[3] = sumA * invKernel;

                const int removeX = std::clamp(x - boxRadius, 0, w - 1);
                const int addX = std::clamp(x + boxRadius + 1, 0, w - 1);
                const float* remP = srcRow + static_cast<std::size_t>(removeX) * 4u;
                const float* addP = srcRow + static_cast<std::size_t>(addX) * 4u;
                sumR += addP[0] - remP[0];
                sumG += addP[1] - remP[1];
                sumB += addP[2] - remP[2];
                sumA += addP[3] - remP[3];
            }
        }

        // Vertical 1D sliding-window box blur: scratch -> premulRgba
        const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
        for (int x = 0; x < w; ++x) {
            float sumR = 0.0f;
            float sumG = 0.0f;
            float sumB = 0.0f;
            float sumA = 0.0f;
            for (int k = -boxRadius; k <= boxRadius; ++k) {
                const int sy = std::clamp(k, 0, h - 1);
                const float* p = scratch.data() + static_cast<std::size_t>(sy) * rowFloats + static_cast<std::size_t>(x) * 4u;
                sumR += p[0];
                sumG += p[1];
                sumB += p[2];
                sumA += p[3];
            }
            for (int y = 0; y < h; ++y) {
                float* outP = layer.premulRgba.data() + static_cast<std::size_t>(y) * rowFloats + static_cast<std::size_t>(x) * 4u;
                outP[0] = sumR * invKernel;
                outP[1] = sumG * invKernel;
                outP[2] = sumB * invKernel;
                outP[3] = sumA * invKernel;

                const int removeY = std::clamp(y - boxRadius, 0, h - 1);
                const int addY = std::clamp(y + boxRadius + 1, 0, h - 1);
                const float* remP = scratch.data() + static_cast<std::size_t>(removeY) * rowFloats + static_cast<std::size_t>(x) * 4u;
                const float* addP = scratch.data() + static_cast<std::size_t>(addY) * rowFloats + static_cast<std::size_t>(x) * 4u;
                sumR += addP[0] - remP[0];
                sumG += addP[1] - remP[1];
                sumB += addP[2] - remP[2];
                sumA += addP[3] - remP[3];
            }
        }
    }
}

void RasterizeSingleNode(
    const PagedRenderTree& tree,
    std::uint32_t nid,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint8_t* outRgba8,
    PremulOffscreenLayer* optPremulLayer) {
    const RenderNodeDescriptor* nd = tree.GetNode(nid);
    if (!nd || nd->pageIndex >= tree.PageCount() || nd->opacity <= 0.001f) {
        return;
    }
    const CommandPage& page = tree.GetPage(nd->pageIndex);
    const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
    int clipMinX = clipOn ? std::max(0, static_cast<int>(std::floor(page.header.clipX))) : 0;
    int clipMinY = clipOn ? std::max(0, static_cast<int>(std::floor(page.header.clipY))) : 0;
    int clipMaxX = clipOn
        ? std::min(static_cast<int>(viewportWidth), static_cast<int>(std::ceil(page.header.clipX + page.header.clipW)))
        : static_cast<int>(viewportWidth);
    int clipMaxY = clipOn
        ? std::min(static_cast<int>(viewportHeight), static_cast<int>(std::ceil(page.header.clipY + page.header.clipH)))
        : static_cast<int>(viewportHeight);

    if (optPremulLayer) {
        clipMinX = std::max(clipMinX, optPremulLayer->originX);
        clipMinY = std::max(clipMinY, optPremulLayer->originY);
        clipMaxX = std::min(clipMaxX, optPremulLayer->originX + optPremulLayer->width);
        clipMaxY = std::min(clipMaxY, optPremulLayer->originY + optPremulLayer->height);
    }
    if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
        return;
    }

    const std::size_t quadCount = nd->vertexCount / 4u;
    for (std::size_t q = 0; q < quadCount; ++q) {
        const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
        if (vIdx + 4u > kVerticesPerCommandPage) {
            break;
        }
        const AeroVertex& v0 = page.vertices[vIdx + 0u];
        const AeroVertex& v1 = page.vertices[vIdx + 1u];
        const AeroVertex& v2 = page.vertices[vIdx + 2u];
        const AeroVertex& v3 = page.vertices[vIdx + 3u];
        if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) {
            continue;
        }
        const bool uniformVertexColor =
            (v0.rgba == v1.rgba) && (v0.rgba == v2.rgba) && (v0.rgba == v3.rgba);
        const std::uint8_t a8 = static_cast<std::uint8_t>((v0.rgba >> 24u) & 0xFFu);
        const std::uint8_t r8 = static_cast<std::uint8_t>(v0.rgba & 0xFFu);
        const std::uint8_t g8 = static_cast<std::uint8_t>((v0.rgba >> 8u) & 0xFFu);
        const std::uint8_t b8 = static_cast<std::uint8_t>((v0.rgba >> 16u) & 0xFFu);
        const float baseAlpha = static_cast<float>(a8) / 255.0f;
        const std::uint32_t boundTextureId = page.header.reserved[0];
        const bool hasBoundTexture = ImageAtlas::Instance().HasTextureSlot(boundTextureId);

        const float minX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
        const float maxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
        const float minY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
        const float maxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));

        const AeroPrimType prim = DecodeVertexPrimType(v0.primType);

        float e1x = v1.x - v0.x;
        float e1y = v1.y - v0.y;
        float e3x = v3.x - v0.x;
        float e3y = v3.y - v0.y;
        if (nd->hasPerspective3D) {
            e1x = 0.5f * ((v1.x - v0.x) + (v2.x - v3.x));
            e1y = 0.5f * ((v1.y - v0.y) + (v2.y - v3.y));
            e3x = 0.5f * ((v3.x - v0.x) + (v2.x - v1.x));
            e3y = 0.5f * ((v3.y - v0.y) + (v2.y - v1.y));
        }
        const float det = e1x * e3y - e1y * e3x;
        if (std::fabs(det) <= 1e-4f) {
            continue;
        }
        const float invDet = 1.0f / det;
        const float localQuadW = std::sqrt(e1x * e1x + e1y * e1y);
        const float localQuadH = std::sqrt(e3x * e3x + e3y * e3y);
        if (localQuadW <= 0.01f || localQuadH <= 0.01f) {
            continue;
        }

        bool usePerspectiveInverse = false;
        float perspOrigX = nd->x;
        float perspOrigY = nd->y;
        float perspSpanW = nd->width;
        float perspSpanH = nd->height;
        if (nd->hasPerspective3D) {
            if (quadCount == 1u && prim != AeroPrimType::SdfTextGlyph &&
                nd->width > 1e-3f && nd->height > 1e-3f) {
                usePerspectiveInverse = true;
            } else {
                Point pLoc0{};
                Point pLoc2{};
                if (nd->worldMatrix3D.InverseProjectScreenToLocalPlane(Point{v0.x, v0.y}, pLoc0) &&
                    nd->worldMatrix3D.InverseProjectScreenToLocalPlane(Point{v2.x, v2.y}, pLoc2)) {
                    perspOrigX = pLoc0.x;
                    perspOrigY = pLoc0.y;
                    perspSpanW = pLoc2.x - pLoc0.x;
                    perspSpanH = pLoc2.y - pLoc0.y;
                    if (std::fabs(perspSpanW) > 1e-3f && std::fabs(perspSpanH) > 1e-3f) {
                        usePerspectiveInverse = true;
                    }
                }
            }
        }

        const int pxStart = std::max(clipMinX, static_cast<int>(std::floor(minX)));
        const int pyStart = std::max(clipMinY, static_cast<int>(std::floor(minY)));
        const int pxEnd = std::min(clipMaxX, static_cast<int>(std::ceil(maxX)));
        const int pyEnd = std::min(clipMaxY, static_cast<int>(std::ceil(maxY)));

        const AxbMtsdfPathEntry* mtsdfEntry = (nd->primType == AeroPrimType::MsdfVectorPath && nd->mtsdfPathHash != 0)
            ? AxbTemplateRegistry::Instance().FindMtsdfPath(nd->mtsdfPathHash)
            : nullptr;

        CornerRadius sdfRadii = nd->cornerRadii;
        Thickness sdfBorder = nd->borderThicknesses;
        bool sdfHollowBorder = nd->isHollowBorder;
        if (prim == AeroPrimType::SdfRoundedRect) {
            const auto expectedPrim = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(nd->primType) | (nd->encodedPrimExtra & 0xFFF8u));
            if (v0.primType != expectedPrim || v0.flags != nd->encodedSdfParams) {
                UnpackSdfRoundedRectVertexParams(
                    v0.primType,
                    v0.flags,
                    sdfRadii,
                    sdfBorder,
                    sdfHollowBorder);
            }
        }

        for (int py = pyStart; py < pyEnd; ++py) {
            for (int px = pxStart; px < pxEnd; ++px) {
                const float sx = static_cast<float>(px) + 0.5f;
                const float sy = static_cast<float>(py) + 0.5f;
                float su = 0.0f;
                float sv = 0.0f;
                if (usePerspectiveInverse) {
                    Point localPt{};
                    if (!nd->worldMatrix3D.InverseProjectScreenToLocalPlane(Point{sx, sy}, localPt)) {
                        continue;
                    }
                    su = (localPt.x - perspOrigX) / perspSpanW;
                    sv = (localPt.y - perspOrigY) / perspSpanH;
                } else {
                    const float dx = sx - v0.x;
                    const float dy = sy - v0.y;
                    su = (dx * e3y - dy * e3x) * invDet;
                    sv = (e1x * dy - e1y * dx) * invDet;
                }
                if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) {
                    continue;
                }

                float coverage = 1.0f;
                if (prim == AeroPrimType::SdfRoundedRect) {
                    const float unscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
                    const float unscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
                    const float halfW = (unscaledHalfW > 1e-4f) ? unscaledHalfW : (localQuadW * 0.5f);
                    const float halfH = (unscaledHalfH > 1e-4f) ? unscaledHalfH : (localQuadH * 0.5f);
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = std::max(
                        1e-4f,
                        (halfW * localQuadH + halfH * localQuadW) * std::fabs(invDet));

                    if (nd->softFeatherPx > 0.0f) {
                        const float pad = std::max(0.0f, nd->sdfPadPx);
                        const float boxHalfW = std::max(0.5f, halfW - pad);
                        const float boxHalfH = std::max(0.5f, halfH - pad);
                        const float softFeather = std::max(aaFeather, nd->softFeatherPx * 1.6f);
                        if (IsUniformCornerRadius(sdfRadii)) {
                            coverage = EvaluateSdfRoundedRectCoverage(
                                localX,
                                localY,
                                boxHalfW,
                                boxHalfH,
                                sdfRadii.topLeft,
                                softFeather);
                        } else {
                            coverage = EvaluateSdfRoundedRectCoverage(
                                localX,
                                localY,
                                boxHalfW,
                                boxHalfH,
                                sdfRadii,
                                softFeather);
                        }
                    } else if (sdfHollowBorder) {
                        coverage = EvaluateSdfRoundedBorderCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            sdfRadii,
                            sdfBorder,
                            aaFeather);
                    } else if (IsUniformCornerRadius(sdfRadii)) {
                        const float rad = sdfRadii.topLeft;
                        const float border = sdfBorder.left;
                        const float outerCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            rad,
                            aaFeather);
                        if (border > 0.0f) {
                            const float innerHalfW = std::max(0.0f, halfW - border);
                            const float innerHalfH = std::max(0.0f, halfH - border);
                            const float innerRad = std::max(0.0f, rad - border);
                            const float innerCov = (innerHalfW > 0.0f && innerHalfH > 0.0f)
                                ? EvaluateSdfRoundedRectCoverage(localX, localY, innerHalfW, innerHalfH, innerRad, aaFeather)
                                : 0.0f;
                            coverage = std::clamp(outerCov - innerCov * 0.35f, 0.0f, 1.0f);
                        } else {
                            coverage = outerCov;
                        }
                    } else {
                        coverage = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            sdfRadii,
                            aaFeather);
                    }
                } else if (prim == AeroPrimType::MsdfVectorPath && mtsdfEntry &&
                           mtsdfEntry->cellWidth > 0 && mtsdfEntry->cellHeight > 0 &&
                           mtsdfEntry->mtsdfRgbaPixels.size() >=
                               static_cast<std::size_t>(mtsdfEntry->cellWidth) * mtsdfEntry->cellHeight * 4u) {
                    const float cellW = static_cast<float>(mtsdfEntry->cellWidth);
                    const float cellH = static_cast<float>(mtsdfEntry->cellHeight);
                    const float uMin = std::min(v0.u, v2.u);
                    const float uMax = std::max(v0.u, v2.u);
                    const float vMin = std::min(v0.v, v2.v);
                    const float vMax = std::max(v0.v, v2.v);
                    const float uInterp = v0.u + su * (v1.u - v0.u) + sv * (v3.u - v0.u);
                    const float vInterp = v0.v + su * (v1.v - v0.v) + sv * (v3.v - v0.v);

                    const float entrySpanU = std::max(1e-6f, mtsdfEntry->u1 - mtsdfEntry->u0);
                    const float entrySpanV = std::max(1e-6f, mtsdfEntry->v1 - mtsdfEntry->v0);
                    const float cellTexX = 0.5f + ((uInterp - mtsdfEntry->u0) / entrySpanU) * std::max(1.0f, cellW - 1.0f);
                    const float cellTexY = 0.5f + ((vInterp - mtsdfEntry->v0) / entrySpanV) * std::max(1.0f, cellH - 1.0f);
                    const float normX = std::clamp(cellTexX / cellW, 0.0f, 1.0f);
                    const float normY = std::clamp(cellTexY / cellH, 0.0f, 1.0f);

                    const float perpQuadW = std::fabs(det) / std::max(localQuadH, 1e-4f);
                    const float perpQuadH = std::fabs(det) / std::max(localQuadW, 1e-4f);
                    const float quadTexSpanX = std::max(1e-3f, ((uMax - uMin) / entrySpanU) * std::max(1.0f, cellW - 1.0f));
                    const float quadTexSpanY = std::max(1e-3f, ((vMax - vMin) / entrySpanV) * std::max(1.0f, cellH - 1.0f));
                    const float fullCellW = cellW * (perpQuadW / quadTexSpanX);
                    const float fullCellH = cellH * (perpQuadH / quadTexSpanY);

                    float unpackedUvRange = 0.0f;
                    float strokePx = 0.0f;
                    UnpackMtsdfParams(v0.flags, unpackedUvRange, strokePx);
                    const bool strokeOnly = IsMtsdfStrokeOnly(v0.flags);

                    if (nd->softFeatherPx > 0.0f) {
                        const float u = normX * cellW - 0.5f;
                        const float v = normY * cellH - 0.5f;
                        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                        const int x1 = std::min(x0 + 1, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                        const int y1 = std::min(y0 + 1, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                        const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
                        const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);
                        auto sampleA = [&](int cx, int cy) noexcept -> float {
                            const std::size_t idx =
                                (static_cast<std::size_t>(cy) * mtsdfEntry->cellWidth + static_cast<std::size_t>(cx)) * 4u + 3u;
                            return static_cast<float>(mtsdfEntry->mtsdfRgbaPixels[idx]) / 255.0f;
                        };
                        const float topA = sampleA(x0, y0) + (sampleA(x1, y0) - sampleA(x0, y0)) * fx;
                        const float botA = sampleA(x0, y1) + (sampleA(x1, y1) - sampleA(x0, y1)) * fx;
                        const float sdTrue = topA + (botA - topA) * fy;
                        const float scaleX = std::max(1.0f, fullCellW) / cellW;
                        const float scaleY = std::max(1.0f, fullCellH) / cellH;
                        const float screenPxRange = std::max(0.5f * mtsdfEntry->pxRange * (scaleX + scaleY), 1.0f);
                        const float trueDistPx = (sdTrue - 0.5f) * screenPxRange;
                        const float feather = std::max(1.0f, nd->softFeatherPx * 1.25f);
                        const float t = std::clamp(0.5f + (trueDistPx / feather), 0.0f, 1.0f);
                        coverage = t * t * (3.0f - 2.0f * t);
                    } else {
                        float fillCov = strokeOnly
                            ? 0.0f
                            : EvaluateMtsdfCoverage(*mtsdfEntry, normX, normY, fullCellW, fullCellH, false);

                        if (strokePx > 0.0f) {
                            const float u = normX * cellW - 0.5f;
                            const float v = normY * cellH - 0.5f;
                            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                            const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                            const int x1 = std::min(x0 + 1, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                            const int y1 = std::min(y0 + 1, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                            const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
                            const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);
                            auto sampleA = [&](int cx, int cy) noexcept -> float {
                                const std::size_t idx =
                                    (static_cast<std::size_t>(cy) * mtsdfEntry->cellWidth + static_cast<std::size_t>(cx)) * 4u + 3u;
                                return static_cast<float>(mtsdfEntry->mtsdfRgbaPixels[idx]) / 255.0f;
                            };
                            const float topA = sampleA(x0, y0) + (sampleA(x1, y0) - sampleA(x0, y0)) * fx;
                            const float botA = sampleA(x0, y1) + (sampleA(x1, y1) - sampleA(x0, y1)) * fx;
                            const float sdTrue = topA + (botA - topA) * fy;
                            const float scaleX = std::max(1.0f, fullCellW) / cellW;
                            const float scaleY = std::max(1.0f, fullCellH) / cellH;
                            const float screenPxRange = std::max(0.5f * mtsdfEntry->pxRange * (scaleX + scaleY), 1.0f);
                            const float trueDistPx = (sdTrue - 0.5f) * screenPxRange;
                            const float strokeCov = std::clamp(0.5f + strokePx * 0.5f - std::fabs(trueDistPx), 0.0f, 1.0f);
                            fillCov = std::max(fillCov, strokeCov);
                        }
                        coverage = fillCov;
                    }
                } else if (prim == AeroPrimType::SdfRadialArc) {
                    float startDeg = 0.0f;
                    float sweepDeg = 0.0f;
                    float innerRatio = 0.0f;
                    UnpackSdfRadialArcParams(v0.flags, startDeg, sweepDeg, innerRatio);
                    const float unscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
                    const float unscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
                    const float halfW = (unscaledHalfW > 1e-4f) ? unscaledHalfW : (localQuadW * 0.5f);
                    const float halfH = (unscaledHalfH > 1e-4f) ? unscaledHalfH : (localQuadH * 0.5f);
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = std::max(
                        1e-4f,
                        (halfW * localQuadH + halfH * localQuadW) * std::fabs(invDet));
                    coverage = EvaluateSdfRadialArcCoverage(
                        localX,
                        localY,
                        halfW,
                        halfH,
                        startDeg,
                        sweepDeg,
                        innerRatio,
                        aaFeather);
                } else if (prim == AeroPrimType::SdfTextGlyph) {
                    const float uInterp = v0.u + su * (v1.u - v0.u) + sv * (v3.u - v0.u);
                    const float vInterp = v0.v + su * (v1.v - v0.v) + sv * (v3.v - v0.v);
                    const float uSpan = v1.u - v0.u;
                    const float vSpan = v3.v - v0.v;
                    const float perpQuadW = std::fabs(det) / std::max(localQuadH, 1e-4f);
                    const float perpQuadH = std::fabs(det) / std::max(localQuadW, 1e-4f);
                    coverage = FontAtlas::Instance().EvaluateGlyphSdfCoverage(
                        uInterp,
                        vInterp,
                        uSpan,
                        vSpan,
                        perpQuadW,
                        perpQuadH);
                } else if (prim == AeroPrimType::TexturedQuad) {
                    const float topU = v0.u + su * (v1.u - v0.u);
                    const float botU = v3.u + su * (v2.u - v3.u);
                    const float uInterp = topU + sv * (botU - topU);

                    const float topV = v0.v + su * (v1.v - v0.v);
                    const float botV = v3.v + su * (v2.v - v3.v);
                    const float vInterp = topV + sv * (botV - topV);

                    const Color texel = hasBoundTexture
                        ? ImageAtlas::Instance().SampleBilinearRgba(boundTextureId, uInterp, vInterp)
                        : Color{1.0f, 1.0f, 1.0f, 1.0f};

                    float vertR = static_cast<float>(r8) * (1.0f / 255.0f);
                    float vertG = static_cast<float>(g8) * (1.0f / 255.0f);
                    float vertB = static_cast<float>(b8) * (1.0f / 255.0f);
                    float vertA = baseAlpha;
                    if (!uniformVertexColor) {
                        auto bilerpVertByte = [&](std::uint32_t shift) noexcept -> float {
                            const float c0 = static_cast<float>((v0.rgba >> shift) & 0xFFu);
                            const float c1 = static_cast<float>((v1.rgba >> shift) & 0xFFu);
                            const float c2 = static_cast<float>((v2.rgba >> shift) & 0xFFu);
                            const float c3 = static_cast<float>((v3.rgba >> shift) & 0xFFu);
                            const float top = c0 + su * (c1 - c0);
                            const float bot = c3 + su * (c2 - c3);
                            return (top + sv * (bot - top)) * (1.0f / 255.0f);
                        };
                        vertR = bilerpVertByte(0u);
                        vertG = bilerpVertByte(8u);
                        vertB = bilerpVertByte(16u);
                        vertA = bilerpVertByte(24u);
                    }

                    const float finalAlpha = std::clamp(texel.a * vertA, 0.0f, 1.0f);
                    if (finalAlpha > 0.002f) {
                        const bool isPremul = (v0.flags & AeroVertexFlag_PremultipliedAlpha) != 0u;
                        if (optPremulLayer) {
                            const float pR = isPremul ? (texel.r * vertR) : (texel.r * vertR * finalAlpha);
                            const float pG = isPremul ? (texel.g * vertG) : (texel.g * vertG * finalAlpha);
                            const float pB = isPremul ? (texel.b * vertB) : (texel.b * vertB * finalAlpha);
                            BlendPremulOver(*optPremulLayer, px, py, pR, pG, pB, finalAlpha);
                        } else {
                            const auto outR = static_cast<std::uint8_t>(
                                std::clamp(texel.r * vertR * 255.0f + 0.5f, 0.0f, 255.0f));
                            const auto outG = static_cast<std::uint8_t>(
                                std::clamp(texel.g * vertG * 255.0f + 0.5f, 0.0f, 255.0f));
                            const auto outB = static_cast<std::uint8_t>(
                                std::clamp(texel.b * vertB * 255.0f + 0.5f, 0.0f, 255.0f));
                            std::uint8_t* dst = outRgba8 +
                                (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                            if (isPremul) {
                                const float invA = 1.0f - finalAlpha;
                                dst[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outR) + static_cast<float>(dst[0]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outG) + static_cast<float>(dst[1]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outB) + static_cast<float>(dst[2]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[3] = 255u;
                            } else {
                                BlendPixelOver(dst, outR, outG, outB, finalAlpha);
                            }
                        }
                    }
                    continue;
                } else if (prim == AeroPrimType::SdfEllipse) {
                    float strokePx = 0.0f;
                    bool hollowStroke = false;
                    UnpackSdfEllipseParams(v0.flags, strokePx, hollowStroke);
                    const float unscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
                    const float unscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
                    const float halfW = (unscaledHalfW > 1e-4f) ? unscaledHalfW : (localQuadW * 0.5f);
                    const float halfH = (unscaledHalfH > 1e-4f) ? unscaledHalfH : (localQuadH * 0.5f);
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = std::max(
                        1e-4f,
                        (halfW * localQuadH + halfH * localQuadW) * std::fabs(invDet));
                    coverage = EvaluateSdfEllipseCoverage(
                        localX,
                        localY,
                        halfW,
                        halfH,
                        strokePx,
                        hollowStroke,
                        aaFeather);
                } else if (prim == AeroPrimType::GradientRoundedRect) {
                    std::uint32_t brushId = 0u;
                    float gradRadius = 0.0f;
                    bool isEllipse = false;
                    UnpackGradientRoundedRectVertexParams(
                        v0.primType,
                        v0.flags,
                        brushId,
                        gradRadius,
                        isEllipse);
                    if (brushId == 0u) {
                        brushId = nd->gradientBrushId;
                    }
                    const float unscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
                    const float unscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
                    const float halfW = (unscaledHalfW > 1e-4f) ? unscaledHalfW : (localQuadW * 0.5f);
                    const float halfH = (unscaledHalfH > 1e-4f) ? unscaledHalfH : (localQuadH * 0.5f);
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = std::max(
                        1e-4f,
                        (halfW * localQuadH + halfH * localQuadW) * std::fabs(invDet));

                    float shapeCov = 1.0f;
                    if (isEllipse || nd->isEllipseShape) {
                        shapeCov = EvaluateSdfEllipseCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            0.0f,
                            false,
                            aaFeather);
                    } else if (!IsUniformCornerRadius(nd->cornerRadii)) {
                        shapeCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            nd->cornerRadii,
                            aaFeather);
                    } else {
                        shapeCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            gradRadius,
                            aaFeather);
                    }

                    if (shapeCov > 0.002f) {
                        const GradientBrushDesc* brush = GradientBrushRegistry::Instance().Find(brushId);
                        const Color gradCol = brush
                            ? EvaluateGradientBrushAtNormalizedUv(
                                  *brush,
                                  su,
                                  sv,
                                  2.0f * halfW,
                                  2.0f * halfH)
                            : Color{1.0f, 1.0f, 1.0f, 1.0f};
                        const float modR = static_cast<float>(r8) * (1.0f / 255.0f);
                        const float modG = static_cast<float>(g8) * (1.0f / 255.0f);
                        const float modB = static_cast<float>(b8) * (1.0f / 255.0f);
                        const float finalAlpha = std::clamp(gradCol.a * baseAlpha * shapeCov, 0.0f, 1.0f);
                        if (finalAlpha > 0.002f) {
                            if (optPremulLayer) {
                                BlendPremulOver(
                                    *optPremulLayer,
                                    px,
                                    py,
                                    gradCol.r * modR * finalAlpha,
                                    gradCol.g * modG * finalAlpha,
                                    gradCol.b * modB * finalAlpha,
                                    finalAlpha);
                            } else {
                                const auto outR = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.r * modR * 255.0f + 0.5f, 0.0f, 255.0f));
                                const auto outG = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.g * modG * 255.0f + 0.5f, 0.0f, 255.0f));
                                const auto outB = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.b * modB * 255.0f + 0.5f, 0.0f, 255.0f));
                                std::uint8_t* dst = outRgba8 +
                                    (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                                BlendPixelOver(dst, outR, outG, outB, finalAlpha);
                            }
                        }
                    }
                    continue;
                }

                if (coverage > 0.002f) {
                    const float effAlpha = std::clamp(baseAlpha * coverage, 0.0f, 1.0f);
                    if (optPremulLayer) {
                        const float inv255 = 1.0f / 255.0f;
                        BlendPremulOver(
                            *optPremulLayer,
                            px,
                            py,
                            static_cast<float>(r8) * inv255 * effAlpha,
                            static_cast<float>(g8) * inv255 * effAlpha,
                            static_cast<float>(b8) * inv255 * effAlpha,
                            effAlpha);
                    } else {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        BlendPixelOver(dst, r8, g8, b8, effAlpha);
                    }
                }
            }
        }
    }
}

bool ComputeNodeRangeScreenBounds(
    const PagedRenderTree& tree,
    std::uint32_t startNodeId,
    std::uint32_t endNodeId,
    float& outMinX,
    float& outMinY,
    float& outMaxX,
    float& outMaxY) noexcept {
    bool any = false;
    float minX = 1e9f;
    float minY = 1e9f;
    float maxX = -1e9f;
    float maxY = -1e9f;
    for (std::uint32_t nid = startNodeId; nid < endNodeId; ++nid) {
        const RenderNodeDescriptor* nd = tree.GetNode(nid);
        if (!nd || nd->pageIndex >= tree.PageCount() || nd->opacity <= 0.001f) {
            continue;
        }
        const CommandPage& page = tree.GetPage(nd->pageIndex);
        const std::size_t quadCount = nd->vertexCount / 4u;
        for (std::size_t q = 0; q < quadCount; ++q) {
            const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
            if (vIdx + 4u > kVerticesPerCommandPage) {
                break;
            }
            const AeroVertex& v0 = page.vertices[vIdx + 0u];
            const AeroVertex& v1 = page.vertices[vIdx + 1u];
            const AeroVertex& v2 = page.vertices[vIdx + 2u];
            const AeroVertex& v3 = page.vertices[vIdx + 3u];
            if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) {
                continue;
            }
            minX = std::min(minX, std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x)));
            maxX = std::max(maxX, std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x)));
            minY = std::min(minY, std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y)));
            maxY = std::max(maxY, std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y)));
            any = true;
        }
    }
    outMinX = minX;
    outMinY = minY;
    outMaxX = maxX;
    outMaxY = maxY;
    return any;
}

void RasterizeNodeSpanRecursive(
    const PagedRenderTree& tree,
    std::uint32_t startNodeId,
    std::uint32_t endNodeId,
    std::size_t& ioBlurCursor,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint8_t* outRgba8,
    PremulOffscreenLayer* optPremulLayer) {
    const auto& blurSpans = tree.BlurLayerSpans();
    std::uint32_t nid = startNodeId;
    while (nid < endNodeId) {
        while (ioBlurCursor < blurSpans.size() &&
               (blurSpans[ioBlurCursor].endNodeId <= nid ||
                blurSpans[ioBlurCursor].firstNodeId < nid)) {
            ++ioBlurCursor;
        }
        if (ioBlurCursor < blurSpans.size() &&
            blurSpans[ioBlurCursor].firstNodeId == nid &&
            blurSpans[ioBlurCursor].endNodeId > nid &&
            blurSpans[ioBlurCursor].endNodeId <= endNodeId &&
            blurSpans[ioBlurCursor].radiusPx > 0.25f) {
            const BlurLayerSpan span = blurSpans[ioBlurCursor];
            ++ioBlurCursor;

            float uMinX = 0.0f;
            float uMinY = 0.0f;
            float uMaxX = 0.0f;
            float uMaxY = 0.0f;
            if (ComputeNodeRangeScreenBounds(tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
                const float clampedRadius = std::clamp(span.radiusPx, 0.5f, 128.0f);
                const int boxR = std::max(1, static_cast<int>(std::lround(clampedRadius * 0.5f)));
                const int padPx = boxR * 3 + 2;
                const int bx0 = std::max(0, static_cast<int>(std::floor(uMinX)) - padPx);
                const int by0 = std::max(0, static_cast<int>(std::floor(uMinY)) - padPx);
                const int bx1 = std::min(static_cast<int>(viewportWidth), static_cast<int>(std::ceil(uMaxX)) + padPx);
                const int by1 = std::min(static_cast<int>(viewportHeight), static_cast<int>(std::ceil(uMaxY)) + padPx);

                if (bx1 > bx0 && by1 > by0) {
                    PremulOffscreenLayer subLayer{};
                    subLayer.originX = bx0;
                    subLayer.originY = by0;
                    subLayer.width = bx1 - bx0;
                    subLayer.height = by1 - by0;
                    subLayer.premulRgba.assign(
                        static_cast<std::size_t>(subLayer.width) * static_cast<std::size_t>(subLayer.height) * 4u,
                        0.0f);

                    RasterizeNodeSpanRecursive(
                        tree,
                        span.firstNodeId,
                        span.endNodeId,
                        ioBlurCursor,
                        viewportWidth,
                        viewportHeight,
                        outRgba8,
                        &subLayer);

                    SeparableBoxBlurPremul3Pass(subLayer, boxR);

                    int compX0 = bx0;
                    int compY0 = by0;
                    int compX1 = bx1;
                    int compY1 = by1;
                    if (span.clipActive) {
                        compX0 = std::max(compX0, static_cast<int>(std::floor(span.clipRect.x)));
                        compY0 = std::max(compY0, static_cast<int>(std::floor(span.clipRect.y)));
                        compX1 = std::min(compX1, static_cast<int>(std::ceil(span.clipRect.x + span.clipRect.width)));
                        compY1 = std::min(compY1, static_cast<int>(std::ceil(span.clipRect.y + span.clipRect.height)));
                    }
                    if (optPremulLayer) {
                        compX0 = std::max(compX0, optPremulLayer->originX);
                        compY0 = std::max(compY0, optPremulLayer->originY);
                        compX1 = std::min(compX1, optPremulLayer->originX + optPremulLayer->width);
                        compY1 = std::min(compY1, optPremulLayer->originY + optPremulLayer->height);
                    }

                    for (int py = compY0; py < compY1; ++py) {
                        const float* srcRow = subLayer.premulRgba.data() +
                            static_cast<std::size_t>(py - subLayer.originY) * static_cast<std::size_t>(subLayer.width) * 4u;
                        for (int px = compX0; px < compX1; ++px) {
                            const float* sp = srcRow + static_cast<std::size_t>(px - subLayer.originX) * 4u;
                            const float pa = std::clamp(sp[3], 0.0f, 1.0f);
                            if (pa <= 0.001f) {
                                continue;
                            }
                            if (optPremulLayer) {
                                BlendPremulOver(*optPremulLayer, px, py, sp[0], sp[1], sp[2], pa);
                            } else {
                                std::uint8_t* dst = outRgba8 +
                                    (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                                const float invA = 1.0f - pa;
                                dst[0] = static_cast<std::uint8_t>(
                                    std::clamp(sp[0] * 255.0f + static_cast<float>(dst[0]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[1] = static_cast<std::uint8_t>(
                                    std::clamp(sp[1] * 255.0f + static_cast<float>(dst[1]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[2] = static_cast<std::uint8_t>(
                                    std::clamp(sp[2] * 255.0f + static_cast<float>(dst[2]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[3] = 255u;
                            }
                        }
                    }
                }
            }
            nid = span.endNodeId;
            continue;
        }

        RasterizeSingleNode(tree, nid, viewportWidth, viewportHeight, outRgba8, optPremulLayer);
        ++nid;
    }
}

} // namespace

ZeroCopyGpuStream::ZeroCopyGpuStream(ShaderBackend backend, std::size_t maxPagesInRing)
    : maxPagesInRing_(std::max<std::size_t>(16u, maxPagesInRing)) {
    const std::size_t pageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    ringCapacityBytes_ = maxPagesInRing_ * pageVertexBytes;
    ownedRingStorage_.resize(ringCapacityBytes_, 0u);
    mappedRingBase_ = ownedRingStorage_.data();
    subDataSpans_.reserve(maxPagesInRing_);
    clipDrawBatches_.reserve(16u);
    SetBackend(backend);
}

void ZeroCopyGpuStream::SetBackend(ShaderBackend backend) noexcept {
    backend_ = backend;
    switch (backend_) {
        case ShaderBackend::DesktopGL33:
            uploadMechanism_ = RingUploadMechanism::PersistentCoherentMap;
            break;
        case ShaderBackend::WebGL2:
            uploadMechanism_ = RingUploadMechanism::WebGL2BufferSubData;
            break;
        case ShaderBackend::WebGPU_WGSL:
            uploadMechanism_ = RingUploadMechanism::WebGpuWriteBuffer;
            break;
    }
}

void ZeroCopyGpuStream::BindExternalMappedRing(void* mappedPtr, std::size_t capacityBytes) noexcept {
    if (mappedPtr && capacityBytes >= kVerticesPerCommandPage * sizeof(AeroVertex)) {
        mappedRingBase_ = static_cast<std::uint8_t*>(mappedPtr);
        ringCapacityBytes_ = capacityBytes;
        maxPagesInRing_ = capacityBytes / (kVerticesPerCommandPage * sizeof(AeroVertex));
    } else {
        mappedRingBase_ = ownedRingStorage_.data();
        ringCapacityBytes_ = ownedRingStorage_.size();
        maxPagesInRing_ = ringCapacityBytes_ / (kVerticesPerCommandPage * sizeof(AeroVertex));
    }
}

ZeroCopySubmissionStats ZeroCopyGpuStream::SubmitDirtyPages(PagedRenderTree& tree) noexcept {
    ++frameCounter_;
    ZeroCopySubmissionStats stats{};
    stats.frameIndex = frameCounter_;
    stats.mechanism = uploadMechanism_;
    stats.grayscaleSdfTextActive = IsGrayscaleSdfTextRouted();

    const auto& dirtyIndices = tree.DirtyPageIndices();
    const std::size_t dirtyCount = dirtyIndices.size();
    if (dirtyCount > subDataSpans_.capacity()) {
        subDataSpans_.reserve(dirtyCount);
    }
    subDataSpans_.resize(dirtyCount);

    constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    const auto* pagesPtr = tree.Pages().data();
    const std::size_t totalTreePages = tree.PageCount();

    for (std::size_t i = 0; i < dirtyCount; ++i) {
        const std::uint32_t pageIdx = dirtyIndices[i];
        if (pageIdx >= totalTreePages) {
            continue;
        }
        const CommandPage& srcPage = pagesPtr[pageIdx];
        const std::size_t activeVerts = std::min<std::size_t>(
            srcPage.header.vertexCount,
            kVerticesPerCommandPage);
        const std::size_t activeBytes = activeVerts * sizeof(AeroVertex);
        const std::size_t slotIdx = pageIdx % maxPagesInRing_;
        const std::size_t byteOffset = slotIdx * kPageVertexBytes;

        if (mappedRingBase_ && (byteOffset + kPageVertexBytes <= ringCapacityBytes_)) {
            if (activeBytes > 0u) {
                std::memcpy(
                    mappedRingBase_ + byteOffset,
                    srcPage.vertices.data(),
                    activeBytes);
            }
            if (activeBytes < kPageVertexBytes) {
                std::memset(
                    mappedRingBase_ + byteOffset + activeBytes,
                    0,
                    kPageVertexBytes - activeBytes);
            }
        }

        subDataSpans_[i] = GlBufferSubDataSpan{
            pageIdx,
            byteOffset,
            activeBytes,
            srcPage.vertices.data()};

        stats.dirtyPagesUploaded += 1u;
        stats.verticesStreamed += activeVerts;
        stats.bytesStreamed += activeBytes;
    }

    // Coalesce strictly contiguous CommandPages sharing identical scissor clip state into UberShader draw batches,
    // accounting for fixed 168-vertex page stride across multi-page spans so trailing quads are never truncated.
    clipDrawBatches_.clear();
    if (tree.NodeCount() > 0 && totalTreePages > 0) {
        if (totalTreePages > clipDrawBatches_.capacity()) {
            clipDrawBatches_.reserve(totalTreePages);
        }
        std::size_t lastMergedPage = static_cast<std::size_t>(-1);
        std::uint32_t lastMergedPageVerts = 0u;
        for (std::size_t p = 0; p < totalTreePages; ++p) {
            const CommandPageHeader& hdr = pagesPtr[p].header;
            const std::uint32_t clampedVerts = std::min<std::uint32_t>(
                hdr.vertexCount,
                static_cast<std::uint32_t>(kVerticesPerCommandPage));
            if (clampedVerts == 0u) {
                continue;
            }
            const bool ringContiguous = !clipDrawBatches_.empty() &&
                (p == lastMergedPage + 1u) &&
                ((p % maxPagesInRing_) == ((lastMergedPage % maxPagesInRing_) + 1u));
            if (ringContiguous && SameBatchScissor(clipDrawBatches_.back(), hdr)) {
                const std::uint32_t prevTailPad =
                    static_cast<std::uint32_t>(kVerticesPerCommandPage) - lastMergedPageVerts;
                clipDrawBatches_.back().pageCount += 1u;
                clipDrawBatches_.back().vertexCount += prevTailPad + clampedVerts;
            } else {
                GpuClipDrawBatch batch{};
                batch.firstPageIndex = static_cast<std::uint32_t>(p);
                batch.pageCount = 1u;
                batch.vertexCount = clampedVerts;
                batch.textureId = hdr.reserved[0];
                batch.scissorActive = (hdr.flags & CommandPageFlag_ScissorClipActive) != 0u;
                batch.scissorRect = Rect{hdr.clipX, hdr.clipY, hdr.clipW, hdr.clipH};
                clipDrawBatches_.push_back(batch);
            }
            lastMergedPage = p;
            lastMergedPageVerts = clampedVerts;
        }
    }

    stats.drawCallCount = clipDrawBatches_.size();
    lastStats_ = stats;
    tree.ClearDirtyPages();
    return stats;
}

const AeroVertex* ZeroCopyGpuStream::MappedPageVertices(std::size_t pageIndex) const noexcept {
    if (!mappedRingBase_ || maxPagesInRing_ == 0) {
        return nullptr;
    }
    constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    const std::size_t slotIdx = pageIndex % maxPagesInRing_;
    const std::size_t byteOffset = slotIdx * kPageVertexBytes;
    if (byteOffset + kPageVertexBytes > ringCapacityBytes_) {
        return nullptr;
    }
    return reinterpret_cast<const AeroVertex*>(mappedRingBase_ + byteOffset);
}

void ZeroCopyGpuStream::RasterizeTreeToRgba8(
    const PagedRenderTree& tree,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::vector<std::uint8_t>& outRgba8,
    Color clearColor) {
    const std::size_t pixelCount = static_cast<std::size_t>(viewportWidth) * viewportHeight;
    outRgba8.resize(pixelCount * 4u);
    if (pixelCount == 0u) {
        return;
    }

    const std::uint32_t clearPacked = PackColorRgba8(clearColor, 1.0f);
    const auto cr = static_cast<std::uint8_t>(clearPacked & 0xFFu);
    const auto cg = static_cast<std::uint8_t>((clearPacked >> 8u) & 0xFFu);
    const auto cb = static_cast<std::uint8_t>((clearPacked >> 16u) & 0xFFu);
    const auto ca = static_cast<std::uint8_t>((clearPacked >> 24u) & 0xFFu);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        outRgba8[i * 4u + 0u] = cr;
        outRgba8[i * 4u + 1u] = cg;
        outRgba8[i * 4u + 2u] = cb;
        outRgba8[i * 4u + 3u] = ca;
    }

    std::size_t blurCursor = 0u;
    RasterizeNodeSpanRecursive(
        tree,
        0u,
        static_cast<std::uint32_t>(tree.NodeCount()),
        blurCursor,
        viewportWidth,
        viewportHeight,
        outRgba8.data(),
        nullptr);
}

std::string_view ZeroCopyGpuStream::UberVertexShaderSource(ShaderBackend backend) noexcept {
    switch (backend) {
        case ShaderBackend::DesktopGL33:
            return kUberVertexShaderGL33;
        case ShaderBackend::WebGL2:
            return kUberVertexShaderWebGL2;
        case ShaderBackend::WebGPU_WGSL:
            return kUberShaderWGSL;
    }
    return kUberVertexShaderGL33;
}

std::string_view ZeroCopyGpuStream::UberPixelShaderSource(ShaderBackend backend) noexcept {
    switch (backend) {
        case ShaderBackend::DesktopGL33:
            return kUberPixelShaderGL33;
        case ShaderBackend::WebGL2:
            return kUberPixelShaderWebGL2;
        case ShaderBackend::WebGPU_WGSL:
            return kUberShaderWGSL;
    }
    return kUberPixelShaderGL33;
}

} // namespace aero
