#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace aero {

namespace {

constexpr std::string_view kUberVertexShaderBodyGlsl = R"(layout(location = 0) in vec2 a_Position;   // float x, y (offset 0, 8B)
layout(location = 1) in vec2 a_TexCoord;   // float u, v or SDF local coords (offset 8, 8B)
layout(location = 2) in uint a_PackedRgba; // uint32_t rgba (offset 16, 4B)
layout(location = 3) in uint a_PrimFlags;  // low 16b: primType|sdfExtra, high 16b: flags (offset 20, 4B)

uniform vec2 u_ViewportSize;

out vec2 v_UvOrLocalPos;
out vec2 v_QuadNormUv;
out vec4 v_Color;
flat out uint v_PrimType;
flat out uint v_Flags;
flat out vec2 v_QuadHalfSize;

void main() {
    vec2 ndc = (a_Position / max(u_ViewportSize, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    int qCorner = gl_VertexID & 3;
    v_QuadNormUv = vec2((qCorner == 1 || qCorner == 2) ? 1.0 : 0.0, (qCorner >= 2) ? 1.0 : 0.0);
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

constexpr std::string_view kUberPixelShaderBodyGlsl = R"(in vec2 v_UvOrLocalPos;
in vec2 v_QuadNormUv;
in vec4 v_Color;
flat in uint v_PrimType;
flat in uint v_Flags;
flat in vec2 v_QuadHalfSize;

uniform sampler2D u_GlyphAndUiAtlas;
uniform sampler2D u_ImageAtlas;
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

vec4 sampleAtlasGradientBrush(uint brushId, vec2 normUv, vec2 quadSize) {
    ivec2 atlasDim = textureSize(u_GlyphAndUiAtlas, 0);
    int rampRow = atlasDim.y - 2 - (int(brushId) - 1) * 2;
    int metaRow = rampRow - 1;
    if (brushId == 0u || metaRow < 0 || atlasDim.x < 8) {
        return vec4(1.0);
    }
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
    vec2 samplePt = (mappingMode == 1u) ? (normUv * quadSize) : normUv;
    float rawT = 0.0;
    if (kind == 0u) {
        vec2 dir = vec2(p1x - p0x, p1y - p0y);
        float lenSq = dot(dir, dir);
        rawT = (lenSq > 1e-8) ? (dot(samplePt - vec2(p0x, p0y), dir) / lenSq) : 0.0;
    } else if (kind == 2u) {
        float dx = samplePt.x - p0x;
        float dy = samplePt.y - p0y;
        if (abs(dx) + abs(dy) > 1e-6) {
            float angle = atan(dy, dx);
            if (angle < 0.0) {
                angle += 6.283185307179586;
            }
            rawT = angle / 6.283185307179586;
        }
    } else {
        vec2 safeRad = max(abs(vec2(rx, ry)), vec2(1e-5));
        vec2 sp = (samplePt - vec2(p0x, p0y)) / safeRad;
        vec2 fp = (vec2(p1x, p1y) - vec2(p0x, p0y)) / safeRad;
        float fLen = length(fp);
        if (fLen > 0.99) fp *= (0.99 / fLen);
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
    vec4 gs = textureLod(u_GlyphAndUiAtlas, vec2(uTex, vTex), 0.0);
    gs.a *= brushOpacity;
    return gs;
}

void main() {
    // 0: SolidQuad (outputs premultiplied RGBA; respects AeroVertexFlag_PremultipliedAlpha = 4u)
    if (v_PrimType == 0u) {
        bool isPremul = (v_Flags & 4u) != 0u;
        vec3 rgb = isPremul ? v_Color.rgb : (v_Color.rgb * v_Color.a);
        o_FragColor = vec4(rgb, v_Color.a);
        return;
    }

    // 1: SdfRoundedRect (Uniform fast path + Soft DropShadow + Asymmetric 4-corner sdRoundedBox4 + Hollow BorderBrush Ring)
    if (v_PrimType == 1u) {
        if (v_Color.a <= 0.0001) {
            o_FragColor = vec4(0.0);
            return;
        }
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
            float outA = v_Color.a * outerCoverage;
            if (borderThickness > 0.0) {
                float innerDist = dist + borderThickness;
                float innerCoverage = smoothstep(aa, -aa, innerDist);
                float ringCov = clamp(outerCoverage - innerCoverage, 0.0, 1.0);
                vec3 hiRgb = min(vec3(1.0), v_Color.rgb * 1.25 + vec3(0.08));
                vec3 rgb = mix(v_Color.rgb, hiRgb, ringCov);
                o_FragColor = vec4(rgb * outA, outA);
            } else {
                o_FragColor = vec4(v_Color.rgb * outA, outA);
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
            float outA = v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0);
            o_FragColor = vec4(v_Color.rgb * outA, outA);
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
            float outA = v_Color.a * smoothstep(aa, -aa, dist);
            o_FragColor = vec4(v_Color.rgb * outA, outA);
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
        float outA = v_Color.a * clamp(outerCoverage - innerCoverage, 0.0, 1.0);
        o_FragColor = vec4(v_Color.rgb * outA, outA);
        return;
    }

    // 2: SdfTextGlyph (Analytical SDF Font AA on u_GlyphAndUiAtlas)
    if (v_PrimType == 2u) {
        float sdfSample = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos).r;
#if AERO_WEBGL2_GRAYSCALE_SDF_AA
        float edgeWidth = clamp(fwidth(sdfSample) * 0.85, 0.025, 0.22);
#else
        float edgeWidth = max(fwidth(sdfSample), 0.04);
#endif
        float alpha = smoothstep(0.5 - edgeWidth, 0.5 + edgeWidth, sdfSample);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = v_Color.a * alpha;
        vec3 rgb = isPremul ? (v_Color.rgb * alpha) : (v_Color.rgb * outA);
        o_FragColor = vec4(rgb, outA);
        return;
    }

    // 3: TexturedQuad (<Image> sprites & standalone textures/SVG masks on u_ImageAtlas)
    if (v_PrimType == 3u) {
        vec4 texel = texture(u_ImageAtlas, v_UvOrLocalPos);
        uint pathBrushId = (v_Flags >> 17u) & 0x0FFFu;
        vec2 approxQuadPx = vec2(1.0) / max(fwidth(v_QuadNormUv), vec2(1e-4));
        vec4 gradCol = (pathBrushId > 0u) ? sampleAtlasGradientBrush(pathBrushId, v_QuadNormUv, approxQuadPx) : vec4(1.0);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = texel.a * v_Color.a * gradCol.a;
        vec3 rgb = isPremul
            ? (texel.rgb * v_Color.rgb * gradCol.rgb * gradCol.a)
            : (texel.rgb * v_Color.rgb * gradCol.rgb * outA);
        o_FragColor = vec4(rgb, outA);
        return;
    }

    // 4: MsdfVectorPath (4-Channel MTSDF SVG <Path>, sharp corners + anisotropic screenPxRange AA + GradientBrush)
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
        uint pathBrushId = (v_Flags >> 17u) & 0x0FFFu;
        vec2 approxQuadPx = vec2(1.0) / max(fwidth(v_QuadNormUv), vec2(1e-4));
        vec4 gradCol = (pathBrushId > 0u) ? sampleAtlasGradientBrush(pathBrushId, v_QuadNormUv, approxQuadPx) : vec4(1.0);
        float outA = gradCol.a * v_Color.a * fillCov;
        o_FragColor = vec4(gradCol.rgb * v_Color.rgb * outA, outA);
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
        float outA = v_Color.a * cov;
        o_FragColor = vec4(v_Color.rgb * outA, outA);
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
        float cov = outerCov;
        if (hollowStroke) {
            vec2 innerAb = halfSize - vec2(strokeThickness);
            float innerCov = (innerAb.x > 0.0 && innerAb.y > 0.0)
                ? smoothstep(aa, -aa, sdEllipseApprox(v_UvOrLocalPos, innerAb))
                : 0.0;
            cov = clamp(outerCov - innerCov, 0.0, 1.0);
        }
        float outA = v_Color.a * cov;
        o_FragColor = vec4(v_Color.rgb * outA, outA);
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

        vec2 quadSize = max(halfSize * 2.0, vec2(1e-4));
        vec2 normUv = v_UvOrLocalPos / quadSize + vec2(0.5);
        vec4 gradSample = sampleAtlasGradientBrush(brushId, normUv, quadSize);
        float outA = gradSample.a * v_Color.a * shapeCov;
        o_FragColor = vec4(gradSample.rgb * v_Color.rgb * outA, outA);
        return;
    }

    // 6: DynamicMesh fallback on u_GlyphAndUiAtlas
    vec4 texel = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos);
    bool isPremul = (v_Flags & 4u) != 0u;
    float outA = texel.a * v_Color.a;
    vec3 rgb = isPremul ? (texel.rgb * v_Color.rgb) : (texel.rgb * v_Color.rgb * outA);
    o_FragColor = vec4(rgb, outA);
}
)";

constexpr std::string_view kUberShaderWGSL = R"(// AeroGUI Unified WebGPU WGSL UberShader (24B AeroVertex + Analytical SDF & 4-Channel MTSDF AA)
struct AeroUniforms {
    viewportSize : vec2<f32>,
    quadHalfExtent : vec2<f32>,
};
@group(0) @binding(0) var<uniform> u_Globals : AeroUniforms;
@group(0) @binding(1) var u_AtlasSampler : sampler;
@group(0) @binding(2) var u_AtlasTex : texture_2d<f32>;
@group(0) @binding(3) var u_ImageSampler : sampler;
@group(0) @binding(4) var u_ImageTex : texture_2d<f32>;

struct VertexIn {
    @builtin(vertex_index) vertexIndex : u32,
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
    @location(5) quadNormUv : vec2<f32>,
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

fn sampleAtlasGradientBrush(brushId : u32, normUv : vec2<f32>, quadSize : vec2<f32>) -> vec4<f32> {
    let atlasDim = vec2<i32>(textureDimensions(u_AtlasTex, 0));
    let rampRow = atlasDim.y - 2 - (i32(brushId) - 1) * 2;
    let metaRow = rampRow - 1;
    if (brushId == 0u || metaRow < 0 || atlasDim.x < 8) {
        return vec4<f32>(1.0, 1.0, 1.0, 1.0);
    }
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
    let samplePt = select(normUv, normUv * quadSize, mappingMode == 1u);
    var rawT = 0.0;
    if (kind == 0u) {
        let dir = vec2<f32>(p1x - p0x, p1y - p0y);
        let lenSq = dot(dir, dir);
        if (lenSq > 1e-8) {
            rawT = dot(samplePt - vec2<f32>(p0x, p0y), dir) / lenSq;
        }
    } else if (kind == 2u) {
        let dx = samplePt.x - p0x;
        let dy = samplePt.y - p0y;
        if (abs(dx) + abs(dy) > 1e-6) {
            var angle = atan2(dy, dx);
            if (angle < 0.0) {
                angle = angle + 6.2831853;
            }
            rawT = angle / 6.2831853;
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
    var gs = textureSampleLevel(u_AtlasTex, u_AtlasSampler, vec2<f32>(uTex, vTex), 0.0);
    gs.a = gs.a * brushOpacity;
    return gs;
}

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
    var out : VertexOut;
    let vp = max(u_Globals.viewportSize, vec2<f32>(1.0, 1.0));
    var ndc = (in.position / vp) * 2.0 - vec2<f32>(1.0, 1.0);
    ndc.y = -ndc.y;
    out.clipPos = vec4<f32>(ndc, 0.0, 1.0);
    let qCorner = in.vertexIndex & 3u;
    out.quadNormUv = vec2<f32>(
        select(0.0, 1.0, qCorner == 1u || qCorner == 2u),
        select(0.0, 1.0, qCorner >= 2u));
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
        let isPremul = (in.flags & 4u) != 0u;
        let rgb = select(in.color.rgb * in.color.a, in.color.rgb, isPremul);
        return vec4<f32>(rgb, in.color.a);
    }
    if (in.primType == 1u) {
        if (in.color.a <= 0.0001) {
            return vec4<f32>(0.0, 0.0, 0.0, 0.0);
        }
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
            let outA = in.color.a * outerCov;
            if (borderThickness > 0.0) {
                let innerDist = dist + borderThickness;
                let innerCov = smoothstep(aa, -aa, innerDist);
                let ringCov = clamp(outerCov - innerCov, 0.0, 1.0);
                let hiRgb = min(vec3<f32>(1.0), in.color.rgb * 1.25 + vec3<f32>(0.08));
                let rgb = mix(in.color.rgb, hiRgb, ringCov);
                return vec4<f32>(rgb * outA, outA);
            }
            return vec4<f32>(in.color.rgb * outA, outA);
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
            let outA = in.color.a * clamp(outerCov - innerCov, 0.0, 1.0);
            return vec4<f32>(in.color.rgb * outA, outA);
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
            let outA = in.color.a * smoothstep(aa, -aa, dist);
            return vec4<f32>(in.color.rgb * outA, outA);
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
        let outA = in.color.a * clamp(outerCov - innerCov, 0.0, 1.0);
        return vec4<f32>(in.color.rgb * outA, outA);
    }
    if (in.primType == 2u) {
        let sdf = textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal).r;
        let fw = max(fwidth(sdf), 0.04);
        let alpha = smoothstep(0.5 - fw, 0.5 + fw, sdf);
        let isPremul = (in.flags & 4u) != 0u;
        let outA = in.color.a * alpha;
        let rgb = select(in.color.rgb * outA, in.color.rgb * alpha, isPremul);
        return vec4<f32>(rgb, outA);
    }
    if (in.primType == 3u) {
        let texel = textureSample(u_ImageTex, u_ImageSampler, in.uvOrLocal);
        let pathBrushId = (in.flags >> 17u) & 0x0FFFu;
        var gradCol = vec4<f32>(1.0, 1.0, 1.0, 1.0);
        if (pathBrushId > 0u) {
            let approxQuadPx = vec2<f32>(1.0, 1.0) / max(fwidth(in.quadNormUv), vec2<f32>(1e-4, 1e-4));
            gradCol = sampleAtlasGradientBrush(pathBrushId, in.quadNormUv, approxQuadPx);
        }
        let isPremul = (in.flags & 4u) != 0u;
        let outA = texel.a * in.color.a * gradCol.a;
        let rgb = select(
            texel.rgb * in.color.rgb * gradCol.rgb * outA,
            texel.rgb * in.color.rgb * gradCol.rgb * gradCol.a,
            isPremul);
        return vec4<f32>(rgb, outA);
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
        let pathBrushId = (in.flags >> 17u) & 0x0FFFu;
        let approxQuadPx = vec2<f32>(1.0, 1.0) / max(fwidth(in.quadNormUv), vec2<f32>(1e-4, 1e-4));
        var gradCol = vec4<f32>(1.0, 1.0, 1.0, 1.0);
        if (pathBrushId > 0u) {
            gradCol = sampleAtlasGradientBrush(pathBrushId, in.quadNormUv, approxQuadPx);
        }
        let outA = gradCol.a * in.color.a * fillCov;
        return vec4<f32>(gradCol.rgb * in.color.rgb * outA, outA);
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
        let outA = in.color.a * cov;
        return vec4<f32>(in.color.rgb * outA, outA);
    }
    if (in.primType == 7u) {
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let strokeThickness = f32(in.flags & 0x7FFFu) * 0.5;
        let hollowStroke = (in.flags & 0x8000u) != 0u;
        let dist = sdEllipseApprox(in.uvOrLocal, halfSize);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        let outerCov = smoothstep(aa, -aa, dist);
        var cov = outerCov;
        if (hollowStroke) {
            let innerAb = halfSize - vec2<f32>(strokeThickness, strokeThickness);
            var innerCov = 0.0;
            if (innerAb.x > 0.0 && innerAb.y > 0.0) {
                innerCov = smoothstep(aa, -aa, sdEllipseApprox(in.uvOrLocal, innerAb));
            }
            cov = clamp(outerCov - innerCov, 0.0, 1.0);
        }
        let outA = in.color.a * cov;
        return vec4<f32>(in.color.rgb * outA, outA);
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

        let quadSize = max(halfSize * 2.0, vec2<f32>(1e-4, 1e-4));
        let normUv = in.uvOrLocal / quadSize + vec2<f32>(0.5, 0.5);
        let gradSample = sampleAtlasGradientBrush(brushId, normUv, quadSize);
        let outA = gradSample.a * in.color.a * shapeCov;
        return vec4<f32>(gradSample.rgb * in.color.rgb * outA, outA);
    }
    let texel = textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal);
    let isPremul = (in.flags & 4u) != 0u;
    let outA = texel.a * in.color.a;
    let rgb = select(texel.rgb * in.color.rgb * outA, texel.rgb * in.color.rgb, isPremul);
    return vec4<f32>(rgb, outA);
}
)";

[[nodiscard]] bool SameBatchScissor(const GpuClipDrawBatch& batch, const CommandPageHeader& hdr) noexcept {
    if (batch.textureId != hdr.reserved[0] ||
        static_cast<std::uint32_t>(batch.blendMode) != hdr.reserved[1]) {
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

} // namespace

ZeroCopyGpuStream::ZeroCopyGpuStream(ShaderBackend backend, std::size_t maxPagesInRing)
    : maxPagesInRing_(std::max<std::size_t>(16u, maxPagesInRing)) {
    const std::size_t pageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    ringCapacityBytes_ = maxPagesInRing_ * pageVertexBytes;
    mappedRingBase_ = nullptr;
    prevUploadedBytes_.assign(maxPagesInRing_, pageVertexBytes);
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
    constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    if (mappedPtr && capacityBytes >= kPageVertexBytes) {
        mappedRingBase_ = static_cast<std::uint8_t*>(mappedPtr);
        ringCapacityBytes_ = capacityBytes;
        maxPagesInRing_ = capacityBytes / kPageVertexBytes;
    } else {
        mappedRingBase_ = ownedRingStorage_.data();
        ringCapacityBytes_ = ownedRingStorage_.size();
        maxPagesInRing_ = ringCapacityBytes_ / kPageVertexBytes;
    }
    cachedBatchTreeId_ = 0u;
    cachedBatchPageCount_ = 0u;
    try {
        prevUploadedBytes_.assign(maxPagesInRing_, kPageVertexBytes);
    } catch (...) {
    }
}

ZeroCopySubmissionStats ZeroCopyGpuStream::SubmitDirtyPages(PagedRenderTree& tree) noexcept {
    ++frameCounter_;
    ZeroCopySubmissionStats stats{};
    stats.frameIndex = frameCounter_;
    stats.mechanism = uploadMechanism_;
    stats.grayscaleSdfTextActive = IsGrayscaleSdfTextRouted();

    try {
        constexpr std::size_t kPageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
        const auto* pagesPtr = tree.Pages().data();
        const std::size_t totalTreePages = tree.PageCount();

        // Auto-grow / lazily initialize internal ring buffer when needed so pageIndex % maxPagesInRing_ never aliases
        bool fullRingReupload = false;
        const bool usingOwnedRing =
            (mappedRingBase_ == nullptr) ||
            (!ownedRingStorage_.empty() && mappedRingBase_ == ownedRingStorage_.data());
        if (usingOwnedRing && totalTreePages > 0u &&
            (ownedRingStorage_.empty() || totalTreePages > maxPagesInRing_)) {
            std::size_t newCap = std::max<std::size_t>(16u, maxPagesInRing_);
            while (newCap < totalTreePages) {
                newCap *= 2u;
            }
            ownedRingStorage_.assign(newCap * kPageVertexBytes, 0u);
            mappedRingBase_ = ownedRingStorage_.data();
            ringCapacityBytes_ = ownedRingStorage_.size();
            maxPagesInRing_ = newCap;
            prevUploadedBytes_.assign(newCap, kPageVertexBytes);
            fullRingReupload = true;
        } else if (prevUploadedBytes_.size() < maxPagesInRing_) {
            prevUploadedBytes_.resize(maxPagesInRing_, kPageVertexBytes);
        }

        const auto& dirtyIndices = tree.DirtyPageIndices();
        const std::size_t candidateCount = fullRingReupload ? totalTreePages : dirtyIndices.size();
        subDataSpans_.clear();
        if (candidateCount > subDataSpans_.capacity()) {
            subDataSpans_.reserve(candidateCount);
        }

        for (std::size_t i = 0; i < candidateCount; ++i) {
            const std::uint32_t pageIdx = fullRingReupload
                ? static_cast<std::uint32_t>(i)
                : dirtyIndices[i];
            if (pageIdx >= totalTreePages) {
                continue;
            }
            const CommandPage& srcPage = pagesPtr[pageIdx];
            const std::size_t activeVerts = std::min<std::size_t>(
                srcPage.header.vertexCount,
                kVerticesPerCommandPage);
            const std::size_t activeBytes = activeVerts * sizeof(AeroVertex);
            const std::size_t slotIdx = pageIdx % std::max<std::size_t>(1u, maxPagesInRing_);
            const std::size_t byteOffset = slotIdx * kPageVertexBytes;
            const bool hasRingSlot =
                mappedRingBase_ != nullptr &&
                (byteOffset + kPageVertexBytes <= ringCapacityBytes_) &&
                (slotIdx < prevUploadedBytes_.size());

            if (hasRingSlot) {
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

            const std::size_t uploadBytes = hasRingSlot
                ? std::max(activeBytes, prevUploadedBytes_[slotIdx])
                : activeBytes;
            const void* uploadSrcPtr = hasRingSlot
                ? static_cast<const void*>(mappedRingBase_ + byteOffset)
                : static_cast<const void*>(srcPage.vertices.data());
            if (hasRingSlot) {
                prevUploadedBytes_[slotIdx] = activeBytes;
            }

            subDataSpans_.push_back(GlBufferSubDataSpan{
                pageIdx,
                byteOffset,
                uploadBytes,
                uploadSrcPtr});

            stats.dirtyPagesUploaded += 1u;
            stats.verticesStreamed += activeVerts;
            stats.bytesStreamed += activeBytes;
        }

        // Fast O(1) clean-frame short-circuit: if zero pages were dirty and tree topology matches, reuse cached draw batches!
        if (candidateCount == 0u &&
            !fullRingReupload &&
            cachedBatchTreeId_ == tree.TreeId() &&
            cachedBatchPageCount_ == totalTreePages &&
            (tree.NodeCount() == 0u) == clipDrawBatches_.empty()) {
            stats.drawCallCount = clipDrawBatches_.size();
            lastStats_ = stats;
            tree.ClearDirtyPages();
            return stats;
        }

        // Coalesce strictly contiguous CommandPages sharing identical scissor clip & texture state into UberShader draw batches,
        // accounting for fixed 168-vertex page stride across multi-page spans so trailing quads are never truncated.
        clipDrawBatches_.clear();
        cachedBatchTreeId_ = tree.TreeId();
        cachedBatchPageCount_ = totalTreePages;
        if (tree.NodeCount() > 0 && totalTreePages > 0) {
            if (totalTreePages > clipDrawBatches_.capacity()) {
                clipDrawBatches_.reserve(totalTreePages);
            }
            const std::size_t effRing = std::max<std::size_t>(1u, maxPagesInRing_);
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
                    ((p % effRing) == ((lastMergedPage % effRing) + 1u));
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
                    batch.blendMode = (hdr.reserved[1] <= 3u)
                        ? static_cast<BlendingMode>(hdr.reserved[1])
                        : BlendingMode::Normal;
                    batch.scissorActive = (hdr.flags & CommandPageFlag_ScissorClipActive) != 0u;
                    batch.scissorRect = Rect{hdr.clipX, hdr.clipY, hdr.clipW, hdr.clipH};
                    clipDrawBatches_.push_back(batch);
                }
                lastMergedPage = p;
                lastMergedPageVerts = clampedVerts;
            }
        }
    } catch (...) {
        subDataSpans_.clear();
        clipDrawBatches_.clear();
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

std::string_view ZeroCopyGpuStream::UberVertexShaderSource(ShaderBackend backend) noexcept {
    static const std::string kUberVertexShaderGL33 = []() {
        std::string s =
            "#version 330 core\n"
            "// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (Desktop OpenGL 3.3 Core)\n";
        s.append(kUberVertexShaderBodyGlsl);
        return s;
    }();
    static const std::string kUberVertexShaderWebGL2 = []() {
        std::string s =
            "#version 300 es\n"
            "precision highp float;\n"
            "precision highp int;\n"
            "// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (WebGL2 #version 300 es + glBufferSubData Ring Stream)\n";
        s.append(kUberVertexShaderBodyGlsl);
        return s;
    }();
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
    static const std::string kUberPixelShaderGL33 = []() {
        std::string s =
            "#version 330 core\n"
            "#define AERO_WEBGL2_GRAYSCALE_SDF_AA 0\n"
            "// AeroGUI 2.0 Single-Draw-Call Analytical SDF, Text & 4-Channel MTSDF UberPixelShader (Desktop GL 3.3 Core)\n";
        s.append(kUberPixelShaderBodyGlsl);
        return s;
    }();
    static const std::string kUberPixelShaderWebGL2 = []() {
        std::string s =
            "#version 300 es\n"
            "precision highp float;\n"
            "precision highp int;\n"
            "#define AERO_WEBGL2_GRAYSCALE_SDF_AA 1\n"
            "#define AERO_WEBGL2_RING_BUFFER_SUBDATA 1\n"
            "// AeroGUI 2.0 WebGL2 UberPixelShader (Grayscale SDF AA Text + 4-Channel MTSDF Vector Paths)\n";
        s.append(kUberPixelShaderBodyGlsl);
        return s;
    }();
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
