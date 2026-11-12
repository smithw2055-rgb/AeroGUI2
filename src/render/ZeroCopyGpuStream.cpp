#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/core/UiResourceContext.hpp"
#include <condition_variable>
#include <functional>
#include <thread>

namespace aero {

namespace {

thread_local int gRasterBandY0 = 0;
thread_local int gRasterBandY1 = 0x3FFFFFFF;
thread_local bool gInScanlineBand = false;

class ScanlineWorkerPool {
public:
    static constexpr std::size_t kNumWorkers = 3u; // +1 caller thread = 4 parallel horizontal bands

    static ScanlineWorkerPool& Instance() {
        static ScanlineWorkerPool pool;
        return pool;
    }

    template <typename Fn>
    void ParallelBands4(int totalHeight, Fn&& bandFn) {
        if (totalHeight < 64 || gInScanlineBand) {
            bandFn(0, totalHeight);
            return;
        }
        std::unique_lock<std::mutex> dispatchLock(dispatchMtx_, std::try_to_lock);
        if (!dispatchLock.owns_lock()) {
            bandFn(0, totalHeight);
            return;
        }
        const int step = (totalHeight + 3) / 4;
        std::function<void(int)> taskFn = [&](int bandIdx) {
            const int y0 = bandIdx * step;
            const int y1 = std::min(totalHeight, y0 + step);
            if (y0 < y1) {
                bandFn(y0, y1);
            }
        };
        {
            std::lock_guard<std::mutex> lk(mtx_);
            currentTask_ = &taskFn;
            remaining_.store(static_cast<int>(kNumWorkers), std::memory_order_release);
            ++epoch_;
        }
        cvStart_.notify_all();

        // Band 0 on calling thread
        gInScanlineBand = true;
        try {
            taskFn(0);
        } catch (...) {
            gInScanlineBand = false;
            std::unique_lock<std::mutex> lk(mtx_);
            cvDone_.wait(lk, [&]() { return remaining_.load(std::memory_order_acquire) == 0; });
            currentTask_ = nullptr;
            throw;
        }
        gInScanlineBand = false;

        // Wait for bands 1..3
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cvDone_.wait(lk, [&]() { return remaining_.load(std::memory_order_acquire) == 0; });
            currentTask_ = nullptr;
        }
    }

private:
    ScanlineWorkerPool() {
        for (std::size_t i = 0u; i < kNumWorkers; ++i) {
            const int bandIdx = static_cast<int>(i + 1u);
            workers_[i] = std::thread([this, bandIdx]() {
                std::uint64_t seenEpoch = 0u;
                for (;;) {
                    const std::function<void(int)>* fn = nullptr;
                    {
                        std::unique_lock<std::mutex> lk(mtx_);
                        cvStart_.wait(lk, [&]() { return stop_ || epoch_ != seenEpoch; });
                        if (stop_) return;
                        seenEpoch = epoch_;
                        fn = currentTask_;
                    }
                    if (fn) {
                        gInScanlineBand = true;
                        try {
                            (*fn)(bandIdx);
                        } catch (...) {
                        }
                        gInScanlineBand = false;
                    }
                    if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        std::lock_guard<std::mutex> lk(mtx_);
                        cvDone_.notify_one();
                    }
                }
            });
        }
    }

    ~ScanlineWorkerPool() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cvStart_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    std::thread workers_[kNumWorkers];
    std::mutex dispatchMtx_;
    std::mutex mtx_;
    std::condition_variable cvStart_;
    std::condition_variable cvDone_;
    const std::function<void(int)>* currentTask_ = nullptr;
    std::uint64_t epoch_ = 0u;
    std::atomic<int> remaining_{0};
    bool stop_ = false;
};

constexpr std::string_view kUberVertexShaderGL33 = R"(#version 330 core
// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (Desktop OpenGL 3.3 Core)
layout(location = 0) in vec2 a_Position;   // float x, y (offset 0, 8B)
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

constexpr std::string_view kUberPixelShaderGL33 = R"(#version 330 core
// AeroGUI 2.0 Single-Draw-Call Analytical SDF, Text & 4-Channel MTSDF UberPixelShader (Desktop GL 3.3 Core)
in vec2 v_UvOrLocalPos;
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
        float edgeWidth = max(fwidth(sdfSample), 0.04);
        float alpha = smoothstep(0.5 - edgeWidth, 0.5 + edgeWidth, sdfSample);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = v_Color.a * alpha;
        vec3 rgb = isPremul ? (v_Color.rgb * alpha) : (v_Color.rgb * outA);
        o_FragColor = vec4(rgb, outA);
        return;
    }

    // 3: TexturedQuad (<Image> sprites & standalone textures on u_ImageAtlas)
    if (v_PrimType == 3u) {
        vec4 texel = texture(u_ImageAtlas, v_UvOrLocalPos);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = texel.a * v_Color.a;
        vec3 rgb = isPremul ? (texel.rgb * v_Color.rgb) : (texel.rgb * v_Color.rgb * outA);
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

constexpr std::string_view kUberPixelShaderWebGL2 = R"(#version 300 es
precision highp float;
precision highp int;
#define AERO_WEBGL2_GRAYSCALE_SDF_AA 1
#define AERO_WEBGL2_RING_BUFFER_SUBDATA 1
// AeroGUI 2.0 WebGL2 UberPixelShader (Grayscale SDF AA Text + 4-Channel MTSDF Vector Paths)
in vec2 v_UvOrLocalPos;
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
    if (v_PrimType == 0u) {
        bool isPremul = (v_Flags & 4u) != 0u;
        vec3 rgb = isPremul ? v_Color.rgb : (v_Color.rgb * v_Color.a);
        o_FragColor = vec4(rgb, v_Color.a);
        return;
    }

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

    if (v_PrimType == 2u) {
        // WebGL2 grayscale SDF AA text path (avoids dual-source blending requirement in WebGL2)
        float sdfDist = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos).r;
        float fw = clamp(fwidth(sdfDist) * 0.85, 0.025, 0.22);
        float grayscaleAlpha = smoothstep(0.5 - fw, 0.5 + fw, sdfDist);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = v_Color.a * grayscaleAlpha;
        vec3 rgb = isPremul ? (v_Color.rgb * grayscaleAlpha) : (v_Color.rgb * outA);
        o_FragColor = vec4(rgb, outA);
        return;
    }

    if (v_PrimType == 3u) {
        vec4 texel = texture(u_ImageAtlas, v_UvOrLocalPos);
        bool isPremul = (v_Flags & 4u) != 0u;
        float outA = texel.a * v_Color.a;
        vec3 rgb = isPremul ? (texel.rgb * v_Color.rgb) : (texel.rgb * v_Color.rgb * outA);
        o_FragColor = vec4(rgb, outA);
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
        uint pathBrushId = (v_Flags >> 17u) & 0x0FFFu;
        vec2 approxQuadPx = vec2(1.0) / max(fwidth(v_QuadNormUv), vec2(1e-4));
        vec4 gradCol = (pathBrushId > 0u) ? sampleAtlasGradientBrush(pathBrushId, v_QuadNormUv, approxQuadPx) : vec4(1.0);
        float outA = gradCol.a * v_Color.a * fillCov;
        o_FragColor = vec4(gradCol.rgb * v_Color.rgb * outA, outA);
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
        float outA = gradSample.a * v_Color.a * shapeCov;
        o_FragColor = vec4(gradSample.rgb * v_Color.rgb * outA, outA);
        return;
    }

    vec4 texel = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos);
    bool isPremul = (v_Flags & 4u) != 0u;
    float outA = texel.a * v_Color.a;
    vec3 rgb = isPremul ? (texel.rgb * v_Color.rgb) : (texel.rgb * v_Color.rgb * outA);
    o_FragColor = vec4(rgb, outA);
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
        let isPremul = (in.flags & 4u) != 0u;
        let outA = texel.a * in.color.a;
        let rgb = select(texel.rgb * in.color.rgb * outA, texel.rgb * in.color.rgb, isPremul);
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

void BlendPixelOver(
    std::uint8_t* dstRgba,
    std::uint8_t srcR,
    std::uint8_t srcG,
    std::uint8_t srcB,
    float srcAlpha01,
    BlendingMode blendMode = BlendingMode::Normal) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const float invA = 1.0f - a;
    if (blendMode == BlendingMode::Normal) {
        dstRgba[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcR) * a + static_cast<float>(dstRgba[0]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcG) * a + static_cast<float>(dstRgba[1]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcB) * a + static_cast<float>(dstRgba[2]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + static_cast<float>(dstRgba[3]) * invA + 0.5f, 0.0f, 255.0f));
        return;
    }
    const float sr = static_cast<float>(srcR) / 255.0f;
    const float sg = static_cast<float>(srcG) / 255.0f;
    const float sb = static_cast<float>(srcB) / 255.0f;
    const float dr = static_cast<float>(dstRgba[0]) / 255.0f;
    const float dg = static_cast<float>(dstRgba[1]) / 255.0f;
    const float db = static_cast<float>(dstRgba[2]) / 255.0f;
    float br = sr, bg = sg, bb = sb;
    if (blendMode == BlendingMode::Multiply) {
        br = sr * dr;
        bg = sg * dg;
        bb = sb * db;
    } else if (blendMode == BlendingMode::Screen) {
        br = sr + dr - sr * dr;
        bg = sg + dg - sg * dg;
        bb = sb + db - sb * db;
    } else if (blendMode == BlendingMode::Additive) {
        dstRgba[0] = static_cast<std::uint8_t>(std::clamp((dr + sr * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[1] = static_cast<std::uint8_t>(std::clamp((dg + sg * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[2] = static_cast<std::uint8_t>(std::clamp((db + sb * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[3] = static_cast<std::uint8_t>(std::clamp((a + (static_cast<float>(dstRgba[3]) / 255.0f) * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
        return;
    }
    dstRgba[0] = static_cast<std::uint8_t>(std::clamp((br * a + dr * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[1] = static_cast<std::uint8_t>(std::clamp((bg * a + dg * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[2] = static_cast<std::uint8_t>(std::clamp((bb * a + db * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + static_cast<float>(dstRgba[3]) * invA + 0.5f, 0.0f, 255.0f));
}

[[nodiscard]] int SafeFloorToInt(float v, int lo, int hi) noexcept {
    if (!std::isfinite(v)) {
        return lo;
    }
    const float f = std::floor(v);
    if (f <= static_cast<float>(lo)) {
        return lo;
    }
    if (f >= static_cast<float>(hi)) {
        return hi;
    }
    return static_cast<int>(f);
}

[[nodiscard]] int SafeCeilToInt(float v, int lo, int hi) noexcept {
    if (!std::isfinite(v)) {
        return lo;
    }
    const float c = std::ceil(v);
    if (c <= static_cast<float>(lo)) {
        return lo;
    }
    if (c >= static_cast<float>(hi)) {
        return hi;
    }
    return static_cast<int>(c);
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

[[nodiscard]] Color SampleTextureSlotBilinear(
    const TextureSlotBuffer& slot,
    float u,
    float v) noexcept {
    if (slot.width == 0u || slot.height == 0u ||
        slot.rgbaPixels.size() < static_cast<std::size_t>(slot.width) * slot.height * 4u) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const float clampedU = std::clamp(u, 0.0f, 1.0f);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const float texX = clampedU * static_cast<float>(slot.width) - 0.5f;
    const float texY = clampedV * static_cast<float>(slot.height) - 0.5f;

    const int maxX = static_cast<int>(slot.width) - 1;
    const int maxY = static_cast<int>(slot.height) - 1;
    const float flX = std::floor(texX);
    const float flY = std::floor(texY);
    const int x0 = std::clamp(static_cast<int>(flX), 0, maxX);
    const int y0 = std::clamp(static_cast<int>(flY), 0, maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);
    const float fx = std::clamp(texX - flX, 0.0f, 1.0f);
    const float fy = std::clamp(texY - flY, 0.0f, 1.0f);

    const std::size_t stride = static_cast<std::size_t>(slot.width) * 4u;
    const std::uint8_t* p00 = slot.rgbaPixels.data() + static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p10 = slot.rgbaPixels.data() + static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x1) * 4u;
    const std::uint8_t* p01 = slot.rgbaPixels.data() + static_cast<std::size_t>(y1) * stride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p11 = slot.rgbaPixels.data() + static_cast<std::size_t>(y1) * stride + static_cast<std::size_t>(x1) * 4u;

    constexpr float kInv255 = 1.0f / 255.0f;
    auto bilerpCh = [&](std::size_t ch) noexcept -> float {
        const float c00 = static_cast<float>(p00[ch]);
        const float c10 = static_cast<float>(p10[ch]);
        const float c01 = static_cast<float>(p01[ch]);
        const float c11 = static_cast<float>(p11[ch]);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return (top + (bot - top) * fy) * kInv255;
    };
    return Color{bilerpCh(0u), bilerpCh(1u), bilerpCh(2u), bilerpCh(3u)};
}

void SeparableBoxBlurPremul3Pass(PremulOffscreenLayer& layer, int boxRadius) {
    if (boxRadius <= 0 || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const int w = layer.width;
    const int h = layer.height;
    const float invKernel = 1.0f / static_cast<float>(boxRadius * 2 + 1);
    const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
    thread_local std::vector<float> tlScratch;
    thread_local std::vector<float> tlColSums;
    const std::size_t totalFloats = rowFloats * static_cast<std::size_t>(h);
    if (tlScratch.size() < totalFloats) {
        tlScratch.resize(totalFloats);
    }
    if (tlColSums.size() < rowFloats) {
        tlColSums.resize(rowFloats);
    }

    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal 1D sliding-window box blur: premulRgba -> tlScratch
        for (int y = 0; y < h; ++y) {
            const float* srcRow = layer.premulRgba.data() + static_cast<std::size_t>(y) * rowFloats;
            float* dstRow = tlScratch.data() + static_cast<std::size_t>(y) * rowFloats;
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

        // Cache-friendly row-major vertical 1D sliding-window box blur: tlScratch -> premulRgba
        float* colSums = tlColSums.data();
        std::fill_n(colSums, rowFloats, 0.0f);
        for (int k = -boxRadius; k <= boxRadius; ++k) {
            const int sy = std::clamp(k, 0, h - 1);
            const float* srcRow = tlScratch.data() + static_cast<std::size_t>(sy) * rowFloats;
            for (std::size_t i = 0u; i < rowFloats; ++i) {
                colSums[i] += srcRow[i];
            }
        }
        for (int y = 0; y < h; ++y) {
            float* dstRow = layer.premulRgba.data() + static_cast<std::size_t>(y) * rowFloats;
            const int removeY = std::clamp(y - boxRadius, 0, h - 1);
            const int addY = std::clamp(y + boxRadius + 1, 0, h - 1);
            const float* remRow = tlScratch.data() + static_cast<std::size_t>(removeY) * rowFloats;
            const float* addRow = tlScratch.data() + static_cast<std::size_t>(addY) * rowFloats;
            for (std::size_t i = 0u; i < rowFloats; ++i) {
                dstRow[i] = colSums[i] * invKernel;
                colSums[i] += addRow[i] - remRow[i];
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
    if (!nd || nd->pageIndex >= tree.PageCount() || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
        return;
    }
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const CommandPage& page = tree.GetPage(nd->pageIndex);
    const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
    if (clipOn &&
        (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
         !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
        return;
    }
    int clipMinX = clipOn ? SafeFloorToInt(page.header.clipX, 0, vpW) : 0;
    int clipMinY = clipOn ? SafeFloorToInt(page.header.clipY, 0, vpH) : 0;
    int clipMaxX = clipOn
        ? SafeCeilToInt(page.header.clipX + std::max(0.0f, page.header.clipW), 0, vpW)
        : vpW;
    int clipMaxY = clipOn
        ? SafeCeilToInt(page.header.clipY + std::max(0.0f, page.header.clipH), 0, vpH)
        : vpH;

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
        if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
            !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
            !std::isfinite(v2.x) || !std::isfinite(v2.y) ||
            !std::isfinite(v3.x) || !std::isfinite(v3.y)) {
            continue;
        }
        const bool uniformVertexColor =
            (v0.rgba == v1.rgba) && (v0.rgba == v2.rgba) && (v0.rgba == v3.rgba);
        const std::uint8_t a8 = static_cast<std::uint8_t>((v0.rgba >> 24u) & 0xFFu);
        const std::uint8_t r8 = static_cast<std::uint8_t>(v0.rgba & 0xFFu);
        const std::uint8_t g8 = static_cast<std::uint8_t>((v0.rgba >> 8u) & 0xFFu);
        const std::uint8_t b8 = static_cast<std::uint8_t>((v0.rgba >> 16u) & 0xFFu);
        const float baseAlpha = static_cast<float>(a8) / 255.0f;
        const AeroPrimType primPreCheck = DecodeVertexPrimType(v0.primType);
        const std::uint32_t boundTextureId = page.header.reserved[0];
        const TextureSlotBuffer* boundTexSlot = (primPreCheck == AeroPrimType::TexturedQuad)
            ? ImageAtlas::Instance().GetTextureSlot(boundTextureId)
            : nullptr;
        const bool hasBoundTexture = (boundTexSlot != nullptr);

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
        if (!std::isfinite(det) || std::fabs(det) <= 1e-4f) {
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

        const int pxStart = SafeFloorToInt(minX, clipMinX, clipMaxX);
        const int pyStart = SafeFloorToInt(minY, clipMinY, clipMaxY);
        const int pxEnd = SafeCeilToInt(maxX, clipMinX, clipMaxX);
        const int pyEnd = SafeCeilToInt(maxY, clipMinY, clipMaxY);

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

        if (pxEnd <= pxStart || pyEnd <= pyStart) {
            continue;
        }
        const float quadUnscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
        const float quadUnscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
        const float quadHalfW = (quadUnscaledHalfW > 1e-4f) ? quadUnscaledHalfW : (localQuadW * 0.5f);
        const float quadHalfH = (quadUnscaledHalfH > 1e-4f) ? quadUnscaledHalfH : (localQuadH * 0.5f);
        const float quadAaFeather = std::max(
            1e-4f,
            (quadHalfW * localQuadH + quadHalfH * localQuadW) * std::fabs(invDet));
        const float perpQuadW = std::fabs(det) / std::max(localQuadH, 1e-4f);
        const float perpQuadH = std::fabs(det) / std::max(localQuadW, 1e-4f);
        const bool uniformSdfCr = IsUniformCornerRadius(sdfRadii);

        const std::uint32_t pathGradId = (prim == AeroPrimType::MsdfVectorPath)
            ? ((nd->gradientBrushId != 0u)
                   ? nd->gradientBrushId
                   : static_cast<std::uint32_t>((v0.primType >> 4u) & 0x0FFFu))
            : 0u;
        const GradientBrushDesc* hoistedPathGradBrush = (pathGradId != 0u)
            ? GradientBrushRegistry::Instance().Find(pathGradId)
            : nullptr;

        std::uint32_t hoistedRectGradId = 0u;
        float hoistedGradRadius = 0.0f;
        bool hoistedGradIsEllipse = false;
        const GradientBrushDesc* hoistedRectGradBrush = nullptr;
        if (prim == AeroPrimType::GradientRoundedRect) {
            UnpackGradientRoundedRectVertexParams(
                v0.primType,
                v0.flags,
                hoistedRectGradId,
                hoistedGradRadius,
                hoistedGradIsEllipse);
            if (hoistedRectGradId == 0u) {
                hoistedRectGradId = nd->gradientBrushId;
            }
            if (hoistedRectGradId != 0u) {
                hoistedRectGradBrush = GradientBrushRegistry::Instance().Find(hoistedRectGradId);
            }
        }

        float hoistedMtsdfStrokePx = 0.0f;
        bool hoistedMtsdfStrokeOnly = false;
        float hoistedFullCellW = 0.0f;
        float hoistedFullCellH = 0.0f;
        float hoistedEntrySpanU = 1.0f;
        float hoistedEntrySpanV = 1.0f;
        if (prim == AeroPrimType::MsdfVectorPath && mtsdfEntry &&
            mtsdfEntry->cellWidth > 0 && mtsdfEntry->cellHeight > 0) {
            float dummyUvRange = 0.0f;
            UnpackMtsdfParams(v0.flags, dummyUvRange, hoistedMtsdfStrokePx);
            hoistedMtsdfStrokeOnly = IsMtsdfStrokeOnly(v0.flags);
            const float cellW = static_cast<float>(mtsdfEntry->cellWidth);
            const float cellH = static_cast<float>(mtsdfEntry->cellHeight);
            const float uMin = std::min(v0.u, v2.u);
            const float uMax = std::max(v0.u, v2.u);
            const float vMin = std::min(v0.v, v2.v);
            const float vMax = std::max(v0.v, v2.v);
            hoistedEntrySpanU = std::max(1e-6f, mtsdfEntry->u1 - mtsdfEntry->u0);
            hoistedEntrySpanV = std::max(1e-6f, mtsdfEntry->v1 - mtsdfEntry->v0);
            const float quadTexSpanX = std::max(1e-3f, ((uMax - uMin) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f));
            const float quadTexSpanY = std::max(1e-3f, ((vMax - vMin) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f));
            hoistedFullCellW = cellW * (perpQuadW / quadTexSpanX);
            hoistedFullCellH = cellH * (perpQuadH / quadTexSpanY);
        }

        const FontAtlas& fontAtlasRef = FontAtlas::Instance();

        const int bandClampedPyStart = (optPremulLayer == nullptr) ? std::max(pyStart, gRasterBandY0) : pyStart;
        const int bandClampedPyEnd = (optPremulLayer == nullptr) ? std::min(pyEnd, gRasterBandY1) : pyEnd;
        if (bandClampedPyStart >= bandClampedPyEnd) {
            continue;
        }
        for (int py = bandClampedPyStart; py < bandClampedPyEnd; ++py) {
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

                std::uint8_t shadeR = r8;
                std::uint8_t shadeG = g8;
                std::uint8_t shadeB = b8;
                float coverage = 1.0f;
                if (prim == AeroPrimType::SdfRoundedRect) {
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;

                    if (nd->softFeatherPx > 0.0f) {
                        const float pad = std::max(0.0f, nd->sdfPadPx);
                        const float boxHalfW = std::max(0.5f, halfW - pad);
                        const float boxHalfH = std::max(0.5f, halfH - pad);
                        const float softFeather = std::max(aaFeather, nd->softFeatherPx * 1.6f);
                        if (uniformSdfCr) {
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
                    } else if (uniformSdfCr) {
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
                            const float ringCov = std::clamp(outerCov - innerCov, 0.0f, 1.0f);
                            if (ringCov > 0.001f) {
                                auto accentCh = [&](std::uint8_t ch) noexcept -> std::uint8_t {
                                    const float cf = static_cast<float>(ch) * (1.0f / 255.0f);
                                    const float acc = std::min(cf * 1.35f + 0.08f, 1.0f);
                                    const float mixed = cf + (acc - cf) * ringCov;
                                    return static_cast<std::uint8_t>(std::clamp(mixed * 255.0f + 0.5f, 0.0f, 255.0f));
                                };
                                shadeR = accentCh(r8);
                                shadeG = accentCh(g8);
                                shadeB = accentCh(b8);
                            }
                            coverage = outerCov;
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
                    const float uInterp = v0.u + su * (v1.u - v0.u) + sv * (v3.u - v0.u);
                    const float vInterp = v0.v + su * (v1.v - v0.v) + sv * (v3.v - v0.v);
                    const float cellTexX = 0.5f + ((uInterp - mtsdfEntry->u0) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f);
                    const float cellTexY = 0.5f + ((vInterp - mtsdfEntry->v0) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f);
                    const float normX = std::clamp(cellTexX / cellW, 0.0f, 1.0f);
                    const float normY = std::clamp(cellTexY / cellH, 0.0f, 1.0f);
                    const float fullCellW = hoistedFullCellW;
                    const float fullCellH = hoistedFullCellH;
                    const float strokePx = hoistedMtsdfStrokePx;
                    const bool strokeOnly = hoistedMtsdfStrokeOnly;

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
                    if (hoistedPathGradBrush != nullptr && coverage > 0.0f) {
                        if (const GradientBrushDesc* gBrush = hoistedPathGradBrush) {
                            const Color gc = EvaluateGradientBrushAtNormalizedUv(
                                *gBrush,
                                std::clamp(su, 0.0f, 1.0f),
                                std::clamp(sv, 0.0f, 1.0f),
                                std::max(1.0f, perpQuadW),
                                std::max(1.0f, perpQuadH));
                            shadeR = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeR) / 255.0f) * gc.r * 255.0f + 0.5f, 0.0f, 255.0f));
                            shadeG = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeG) / 255.0f) * gc.g * 255.0f + 0.5f, 0.0f, 255.0f));
                            shadeB = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeB) / 255.0f) * gc.b * 255.0f + 0.5f, 0.0f, 255.0f));
                            coverage *= std::clamp(gc.a, 0.0f, 1.0f);
                        }
                    }
                } else if (prim == AeroPrimType::SdfRadialArc) {
                    float startDeg = 0.0f;
                    float sweepDeg = 0.0f;
                    float innerRatio = 0.0f;
                    UnpackSdfRadialArcParams(v0.flags, startDeg, sweepDeg, innerRatio);
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;
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
                    coverage = fontAtlasRef.EvaluateGlyphSdfCoverage(
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
                        ? SampleTextureSlotBilinear(*boundTexSlot, uInterp, vInterp)
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
                                dst[3] = static_cast<std::uint8_t>(std::clamp(finalAlpha * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f, 0.0f, 255.0f));
                            } else {
                                BlendPixelOver(dst, outR, outG, outB, finalAlpha, nd->blendMode);
                            }
                        }
                    }
                    continue;
                } else if (prim == AeroPrimType::SdfEllipse) {
                    float strokePx = 0.0f;
                    bool hollowStroke = false;
                    UnpackSdfEllipseParams(v0.flags, strokePx, hollowStroke);
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;
                    coverage = EvaluateSdfEllipseCoverage(
                        localX,
                        localY,
                        halfW,
                        halfH,
                        strokePx,
                        hollowStroke,
                        aaFeather);
                } else if (prim == AeroPrimType::GradientRoundedRect) {
                    const float gradRadius = hoistedGradRadius;
                    const bool isEllipse = hoistedGradIsEllipse;
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;

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
                        const GradientBrushDesc* brush = hoistedRectGradBrush;
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
                                BlendPixelOver(dst, outR, outG, outB, finalAlpha, nd->blendMode);
                            }
                        }
                    }
                    continue;
                }

                if (coverage > 0.002f) {
                    const bool isPremulPrim =
                        (prim == AeroPrimType::SolidQuad ||
                         prim == AeroPrimType::SdfTextGlyph ||
                         prim == AeroPrimType::DynamicMesh) &&
                        ((v0.flags & AeroVertexFlag_PremultipliedAlpha) != 0u);
                    const float effAlpha = std::clamp(baseAlpha * coverage, 0.0f, 1.0f);
                    if (optPremulLayer) {
                        const float inv255 = 1.0f / 255.0f;
                        const float rgbScale = isPremulPrim ? (inv255 * coverage) : (inv255 * effAlpha);
                        BlendPremulOver(
                            *optPremulLayer,
                            px,
                            py,
                            static_cast<float>(shadeR) * rgbScale,
                            static_cast<float>(shadeG) * rgbScale,
                            static_cast<float>(shadeB) * rgbScale,
                            effAlpha);
                    } else if (isPremulPrim) {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        const float invA = 1.0f - effAlpha;
                        dst[0] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeR) * coverage + static_cast<float>(dst[0]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[1] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeG) * coverage + static_cast<float>(dst[1]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[2] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeB) * coverage + static_cast<float>(dst[2]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[3] = static_cast<std::uint8_t>(std::clamp(
                            effAlpha * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                    } else {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        BlendPixelOver(dst, shadeR, shadeG, shadeB, effAlpha, nd->blendMode);
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
        if (!nd || nd->pageIndex >= tree.PageCount() || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
            continue;
        }
        const CommandPage& page = tree.GetPage(nd->pageIndex);
        const bool pageClip = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
        if (pageClip &&
            (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
             !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
            continue;
        }
        const float pClipX0 = page.header.clipX;
        const float pClipY0 = page.header.clipY;
        const float pClipX1 = page.header.clipX + std::max(0.0f, page.header.clipW);
        const float pClipY1 = page.header.clipY + std::max(0.0f, page.header.clipH);
        if (pageClip && (pClipX1 <= pClipX0 || pClipY1 <= pClipY0)) {
            continue;
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
            if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
                !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
                !std::isfinite(v2.x) || !std::isfinite(v2.y) ||
                !std::isfinite(v3.x) || !std::isfinite(v3.y)) {
                continue;
            }
            float qMinX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
            float qMaxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
            float qMinY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
            float qMaxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));
            if (pageClip) {
                qMinX = std::max(qMinX, pClipX0);
                qMaxX = std::min(qMaxX, pClipX1);
                qMinY = std::max(qMinY, pClipY0);
                qMaxY = std::min(qMaxY, pClipY1);
                if (qMaxX <= qMinX || qMaxY <= qMinY) {
                    continue;
                }
            }
            minX = std::min(minX, qMinX);
            maxX = std::max(maxX, qMaxX);
            minY = std::min(minY, qMinY);
            maxY = std::max(maxY, qMaxY);
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
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const auto& blurSpans = tree.BlurLayerSpans();
    std::uint32_t nid = startNodeId;
    while (nid < endNodeId) {
        while (ioBlurCursor < blurSpans.size() &&
               (blurSpans[ioBlurCursor].firstNodeId < nid ||
                (blurSpans[ioBlurCursor].endNodeId <= nid &&
                 blurSpans[ioBlurCursor].effectMode != UIEffectKind::BackdropBlur))) {
            ++ioBlurCursor;
        }
        if (ioBlurCursor < blurSpans.size() &&
            blurSpans[ioBlurCursor].firstNodeId == nid &&
            (blurSpans[ioBlurCursor].endNodeId > nid ||
             blurSpans[ioBlurCursor].effectMode == UIEffectKind::BackdropBlur) &&
            blurSpans[ioBlurCursor].endNodeId <= endNodeId &&
            std::isfinite(blurSpans[ioBlurCursor].radiusPx) &&
            blurSpans[ioBlurCursor].radiusPx > 0.25f) {
            const BlurLayerSpan span = blurSpans[ioBlurCursor];
            ++ioBlurCursor;

            if (span.effectMode == UIEffectKind::BackdropBlur && (outRgba8 != nullptr || optPremulLayer != nullptr)) {
                const Rect mr = span.maskDestRect;
                if (mr.width > 1.0f && mr.height > 1.0f) {
                    const int surfX0 = optPremulLayer ? optPremulLayer->originX : 0;
                    const int surfY0 = optPremulLayer ? optPremulLayer->originY : 0;
                    const int surfX1 = optPremulLayer ? (optPremulLayer->originX + optPremulLayer->width) : vpW;
                    const int surfY1 = optPremulLayer ? (optPremulLayer->originY + optPremulLayer->height) : vpH;
                    const float clampedRadius = std::clamp(span.radiusPx, 0.5f, 128.0f);
                    const int boxR = std::max(1, static_cast<int>(std::lround(clampedRadius * 0.5f)));
                    const int padPx = boxR * 3 + 2;
                    const int bx0 = std::clamp(SafeFloorToInt(mr.x, surfX0 - padPx, surfX1 + padPx) - padPx, surfX0, surfX1);
                    const int by0 = std::clamp(SafeFloorToInt(mr.y, surfY0 - padPx, surfY1 + padPx) - padPx, surfY0, surfY1);
                    const int bx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, surfX0 - padPx, surfX1 + padPx) + padPx, surfX0, surfX1);
                    const int by1 = std::clamp(SafeCeilToInt(mr.y + mr.height, surfY0 - padPx, surfY1 + padPx) + padPx, surfY0, surfY1);
                    if (bx1 > bx0 && by1 > by0) {
                        PremulOffscreenLayer bgLayer{};
                        bgLayer.originX = bx0;
                        bgLayer.originY = by0;
                        bgLayer.width = bx1 - bx0;
                        bgLayer.height = by1 - by0;
                        bgLayer.premulRgba.resize(
                            static_cast<std::size_t>(bgLayer.width) * static_cast<std::size_t>(bgLayer.height) * 4u);
                        for (int py = by0; py < by1; ++py) {
                            float* dstRow = bgLayer.premulRgba.data() +
                                static_cast<std::size_t>(py - by0) * static_cast<std::size_t>(bgLayer.width) * 4u;
                            if (optPremulLayer) {
                                const float* srcRow = optPremulLayer->premulRgba.data() +
                                    static_cast<std::size_t>(py - optPremulLayer->originY) *
                                        static_cast<std::size_t>(optPremulLayer->width) * 4u;
                                for (int px = bx0; px < bx1; ++px) {
                                    const float* sp = srcRow + static_cast<std::size_t>(px - optPremulLayer->originX) * 4u;
                                    float* dp = dstRow + static_cast<std::size_t>(px - bx0) * 4u;
                                    dp[0] = sp[0];
                                    dp[1] = sp[1];
                                    dp[2] = sp[2];
                                    dp[3] = sp[3];
                                }
                            } else {
                                const std::uint8_t* srcRow = outRgba8 +
                                    static_cast<std::size_t>(py) * viewportWidth * 4u;
                                for (int px = bx0; px < bx1; ++px) {
                                    const std::uint8_t* sp = srcRow + static_cast<std::size_t>(px) * 4u;
                                    float* dp = dstRow + static_cast<std::size_t>(px - bx0) * 4u;
                                    dp[0] = static_cast<float>(sp[0]) / 255.0f;
                                    dp[1] = static_cast<float>(sp[1]) / 255.0f;
                                    dp[2] = static_cast<float>(sp[2]) / 255.0f;
                                    dp[3] = static_cast<float>(sp[3]) / 255.0f;
                                }
                            }
                        }
                        SeparableBoxBlurPremul3Pass(bgLayer, boxR);
                        const AxbMtsdfPathEntry* maskEntry = (span.maskMtsdfHash != 0u)
                            ? AxbTemplateRegistry::Instance().FindMtsdfPath(span.maskMtsdfHash)
                            : nullptr;
                        int mx0 = std::clamp(SafeFloorToInt(mr.x, surfX0, surfX1), bx0, bx1);
                        int my0 = std::clamp(SafeFloorToInt(mr.y, surfY0, surfY1), by0, by1);
                        int mx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, surfX0, surfX1), bx0, bx1);
                        int my1 = std::clamp(SafeCeilToInt(mr.y + mr.height, surfY0, surfY1), by0, by1);
                        if (span.clipActive) {
                            if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
                                !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
                                mx1 = mx0;
                            } else {
                                mx0 = std::max(mx0, SafeFloorToInt(span.clipRect.x, surfX0, surfX1));
                                my0 = std::max(my0, SafeFloorToInt(span.clipRect.y, surfY0, surfY1));
                                mx1 = std::min(mx1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), surfX0, surfX1));
                                my1 = std::min(my1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), surfY0, surfY1));
                            }
                        }
                        const float screenScale = std::max(1.0f, 0.5f * (mr.width / std::max(1.0f, maskEntry ? maskEntry->boundsW : mr.width) +
                                                                         mr.height / std::max(1.0f, maskEntry ? maskEntry->boundsH : mr.height)));
                        for (int py = my0; py < my1; ++py) {
                            const float normY = ((static_cast<float>(py) + 0.5f) - mr.y) / std::max(1.0f, mr.height);
                            const float* blurRow = bgLayer.premulRgba.data() +
                                static_cast<std::size_t>(py - by0) * static_cast<std::size_t>(bgLayer.width) * 4u;
                            for (int px = mx0; px < mx1; ++px) {
                                const float normX = ((static_cast<float>(px) + 0.5f) - mr.x) / std::max(1.0f, mr.width);
                                float cov = 1.0f;
                                if (maskEntry) {
                                    cov = EvaluateMtsdfCoverage(*maskEntry, normX, normY, screenScale, 0.0f, false);
                                }
                                const float effMask = std::clamp(cov * span.maskOpacity, 0.0f, 1.0f);
                                if (effMask <= 0.002f) continue;
                                const float* bp = blurRow + static_cast<std::size_t>(px - bx0) * 4u;
                                const float pa = std::clamp(bp[3] * effMask, 0.0f, 1.0f);
                                if (pa <= 0.001f) continue;
                                const float pr = bp[0] * effMask;
                                const float pg = bp[1] * effMask;
                                const float pb = bp[2] * effMask;
                                if (optPremulLayer) {
                                    BlendPremulOver(*optPremulLayer, px, py, pr, pg, pb, pa);
                                } else {
                                    std::uint8_t* dst = outRgba8 +
                                        (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                                    const float invA = 1.0f - pa;
                                    dst[0] = static_cast<std::uint8_t>(
                                        std::clamp(pr * 255.0f + static_cast<float>(dst[0]) * invA + 0.5f, 0.0f, 255.0f));
                                    dst[1] = static_cast<std::uint8_t>(
                                        std::clamp(pg * 255.0f + static_cast<float>(dst[1]) * invA + 0.5f, 0.0f, 255.0f));
                                    dst[2] = static_cast<std::uint8_t>(
                                        std::clamp(pb * 255.0f + static_cast<float>(dst[2]) * invA + 0.5f, 0.0f, 255.0f));
                                    dst[3] = static_cast<std::uint8_t>(
                                        std::clamp(pa * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f, 0.0f, 255.0f));
                                }
                            }
                        }
                    }
                }
                // Do NOT skip [span.firstNodeId, span.endNodeId): the frosted-glass element's own
                // tint/border and child controls must still render on top of the blurred backdrop.
                continue;
            }

            float uMinX = 0.0f;
            float uMinY = 0.0f;
            float uMaxX = 0.0f;
            float uMaxY = 0.0f;
            if (ComputeNodeRangeScreenBounds(tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
                const float clampedRadius = std::clamp(span.radiusPx, 0.5f, 128.0f);
                const int boxR = std::max(1, static_cast<int>(std::lround(clampedRadius * 0.5f)));
                const int padPx = boxR * 3 + 2;
                const int bx0 = std::clamp(SafeFloorToInt(uMinX, -padPx, vpW + padPx) - padPx, 0, vpW);
                const int by0 = std::clamp(SafeFloorToInt(uMinY, -padPx, vpH + padPx) - padPx, 0, vpH);
                const int bx1 = std::clamp(SafeCeilToInt(uMaxX, -padPx, vpW + padPx) + padPx, 0, vpW);
                const int by1 = std::clamp(SafeCeilToInt(uMaxY, -padPx, vpH + padPx) + padPx, 0, vpH);

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

                    if (span.effectMode == UIEffectKind::Pixelate) {
                        const int blockSz = std::max(2, static_cast<int>(std::lround(span.radiusPx)));
                        for (int by = 0; by < subLayer.height; by += blockSz) {
                            const int bh = std::min(blockSz, subLayer.height - by);
                            for (int bx = 0; bx < subLayer.width; bx += blockSz) {
                                const int bw = std::min(blockSz, subLayer.width - bx);
                                const int cx = bx + bw / 2;
                                const int cy = by + bh / 2;
                                const float* cp = subLayer.premulRgba.data() +
                                    (static_cast<std::size_t>(cy) * subLayer.width + cx) * 4u;
                                const float cr = cp[0], cg = cp[1], cb = cp[2], ca = cp[3];
                                for (int yy = by; yy < by + bh; ++yy) {
                                    float* row = subLayer.premulRgba.data() +
                                        static_cast<std::size_t>(yy) * subLayer.width * 4u;
                                    for (int xx = bx; xx < bx + bw; ++xx) {
                                        float* dp = row + static_cast<std::size_t>(xx) * 4u;
                                        dp[0] = cr; dp[1] = cg; dp[2] = cb; dp[3] = ca;
                                    }
                                }
                            }
                        }
                    } else {
                        SeparableBoxBlurPremul3Pass(subLayer, boxR);
                    }

                    int compX0 = bx0;
                    int compY0 = by0;
                    int compX1 = bx1;
                    int compY1 = by1;
                    if (span.clipActive) {
                        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
                            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
                            compX1 = compX0;
                        } else {
                            compX0 = std::max(compX0, SafeFloorToInt(span.clipRect.x, 0, vpW));
                            compY0 = std::max(compY0, SafeFloorToInt(span.clipRect.y, 0, vpH));
                            compX1 = std::min(compX1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
                            compY1 = std::min(compY1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
                        }
                    }
                    if (const RenderNodeDescriptor* firstNd = tree.GetNode(span.firstNodeId);
                        firstNd && firstNd->pageIndex < tree.PageCount()) {
                        const CommandPage& firstPage = tree.GetPage(firstNd->pageIndex);
                        if ((firstPage.header.flags & CommandPageFlag_ScissorClipActive) != 0u) {
                            if (!std::isfinite(firstPage.header.clipX) || !std::isfinite(firstPage.header.clipY) ||
                                !std::isfinite(firstPage.header.clipW) || !std::isfinite(firstPage.header.clipH)) {
                                compX1 = compX0;
                            } else {
                                compX0 = std::max(compX0, SafeFloorToInt(firstPage.header.clipX, 0, vpW));
                                compY0 = std::max(compY0, SafeFloorToInt(firstPage.header.clipY, 0, vpH));
                                compX1 = std::min(compX1, SafeCeilToInt(firstPage.header.clipX + std::max(0.0f, firstPage.header.clipW), 0, vpW));
                                compY1 = std::min(compY1, SafeCeilToInt(firstPage.header.clipY + std::max(0.0f, firstPage.header.clipH), 0, vpH));
                            }
                        }
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
                                dst[3] = static_cast<std::uint8_t>(
                                    std::clamp(pa * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f, 0.0f, 255.0f));
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

        // Auto-grow internal ring buffer when tree.PageCount() exceeds maxPagesInRing_ so pageIndex % maxPagesInRing_ never aliases
        bool fullRingReupload = false;
        const bool usingOwnedRing =
            !ownedRingStorage_.empty() && (mappedRingBase_ == ownedRingStorage_.data());
        if (totalTreePages > maxPagesInRing_ && usingOwnedRing) {
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

        // Coalesce strictly contiguous CommandPages sharing identical scissor clip & texture state into UberShader draw batches,
        // accounting for fixed 168-vertex page stride across multi-page spans so trailing quads are never truncated.
        clipDrawBatches_.clear();
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

    if (!UiResourceContext::HasActiveThreadOverride() &&
        tree.BlurLayerSpans().empty() &&
        pixelCount >= 16384u &&
        tree.NodeCount() >= 8u &&
        viewportHeight >= 64u) {
        std::uint8_t* rawOut = outRgba8.data();
        const std::uint32_t nNodes = static_cast<std::uint32_t>(tree.NodeCount());
        ScanlineWorkerPool::Instance().ParallelBands4(
            static_cast<int>(viewportHeight),
            [&](int y0, int y1) {
                const int prevY0 = gRasterBandY0;
                const int prevY1 = gRasterBandY1;
                gRasterBandY0 = y0;
                gRasterBandY1 = y1;
                std::size_t localBlurCursor = 0u;
                RasterizeNodeSpanRecursive(
                    tree, 0u, nNodes, localBlurCursor, viewportWidth, viewportHeight, rawOut, nullptr);
                gRasterBandY0 = prevY0;
                gRasterBandY1 = prevY1;
            });
        return;
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

bool ZeroCopyGpuStream::RasterizeBlurSpanPatchPremulRgba8(
    const PagedRenderTree& tree,
    const BlurLayerSpan& span,
    std::size_t& ioBlurCursor,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    int& outPatchX,
    int& outPatchY,
    int& outPatchW,
    int& outPatchH,
    std::vector<std::uint8_t>& outPremulRgba8) {
    outPatchX = 0;
    outPatchY = 0;
    outPatchW = 0;
    outPatchH = 0;
    outPremulRgba8.clear();
    if (viewportWidth == 0u || viewportHeight == 0u ||
        span.endNodeId <= span.firstNodeId ||
        !std::isfinite(span.radiusPx) || span.radiusPx <= 0.25f) {
        return false;
    }

    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    float uMinX = 0.0f;
    float uMinY = 0.0f;
    float uMaxX = 0.0f;
    float uMaxY = 0.0f;
    if (!ComputeNodeRangeScreenBounds(tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
        return false;
    }

    const float clampedRadius = std::clamp(span.radiusPx, 0.5f, 128.0f);
    const int boxR = std::max(1, static_cast<int>(std::lround(clampedRadius * 0.5f)));
    const int padPx = boxR * 3 + 2;
    const int bx0 = std::clamp(SafeFloorToInt(uMinX, -padPx, vpW + padPx) - padPx, 0, vpW);
    const int by0 = std::clamp(SafeFloorToInt(uMinY, -padPx, vpH + padPx) - padPx, 0, vpH);
    const int bx1 = std::clamp(SafeCeilToInt(uMaxX, -padPx, vpW + padPx) + padPx, 0, vpW);
    const int by1 = std::clamp(SafeCeilToInt(uMaxY, -padPx, vpH + padPx) + padPx, 0, vpH);
    if (bx1 <= bx0 || by1 <= by0) {
        return false;
    }

    PremulOffscreenLayer subLayer{};
    subLayer.originX = bx0;
    subLayer.originY = by0;
    subLayer.width = bx1 - bx0;
    subLayer.height = by1 - by0;
    subLayer.premulRgba.assign(
        static_cast<std::size_t>(subLayer.width) * static_cast<std::size_t>(subLayer.height) * 4u,
        0.0f);

    const auto& blurSpans = tree.BlurLayerSpans();
    if (ioBlurCursor < blurSpans.size() &&
        blurSpans[ioBlurCursor].firstNodeId == span.firstNodeId &&
        blurSpans[ioBlurCursor].endNodeId == span.endNodeId &&
        blurSpans[ioBlurCursor].effectMode == span.effectMode) {
        ++ioBlurCursor;
    }

    RasterizeNodeSpanRecursive(
        tree,
        span.firstNodeId,
        span.endNodeId,
        ioBlurCursor,
        viewportWidth,
        viewportHeight,
        nullptr,
        &subLayer);

    if (span.effectMode == UIEffectKind::Pixelate) {
        const int blockSz = std::max(2, static_cast<int>(std::lround(span.radiusPx)));
        for (int by = 0; by < subLayer.height; by += blockSz) {
            const int bh = std::min(blockSz, subLayer.height - by);
            for (int bx = 0; bx < subLayer.width; bx += blockSz) {
                const int bw = std::min(blockSz, subLayer.width - bx);
                const int cx = bx + bw / 2;
                const int cy = by + bh / 2;
                const float* cp = subLayer.premulRgba.data() +
                    (static_cast<std::size_t>(cy) * static_cast<std::size_t>(subLayer.width) + static_cast<std::size_t>(cx)) * 4u;
                const float cr = cp[0], cg = cp[1], cb = cp[2], ca = cp[3];
                for (int yy = by; yy < by + bh; ++yy) {
                    float* row = subLayer.premulRgba.data() +
                        static_cast<std::size_t>(yy) * static_cast<std::size_t>(subLayer.width) * 4u;
                    for (int xx = bx; xx < bx + bw; ++xx) {
                        float* dp = row + static_cast<std::size_t>(xx) * 4u;
                        dp[0] = cr; dp[1] = cg; dp[2] = cb; dp[3] = ca;
                    }
                }
            }
        }
    } else {
        SeparableBoxBlurPremul3Pass(subLayer, boxR);
    }

    int compX0 = bx0;
    int compY0 = by0;
    int compX1 = bx1;
    int compY1 = by1;
    if (span.clipActive) {
        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
            return false;
        }
        compX0 = std::max(compX0, SafeFloorToInt(span.clipRect.x, 0, vpW));
        compY0 = std::max(compY0, SafeFloorToInt(span.clipRect.y, 0, vpH));
        compX1 = std::min(compX1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
        compY1 = std::min(compY1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
    }
    if (const RenderNodeDescriptor* firstNd = tree.GetNode(span.firstNodeId);
        firstNd && firstNd->pageIndex < tree.PageCount()) {
        const CommandPage& firstPage = tree.GetPage(firstNd->pageIndex);
        if ((firstPage.header.flags & CommandPageFlag_ScissorClipActive) != 0u) {
            if (!std::isfinite(firstPage.header.clipX) || !std::isfinite(firstPage.header.clipY) ||
                !std::isfinite(firstPage.header.clipW) || !std::isfinite(firstPage.header.clipH)) {
                return false;
            }
            compX0 = std::max(compX0, SafeFloorToInt(firstPage.header.clipX, 0, vpW));
            compY0 = std::max(compY0, SafeFloorToInt(firstPage.header.clipY, 0, vpH));
            compX1 = std::min(compX1, SafeCeilToInt(firstPage.header.clipX + std::max(0.0f, firstPage.header.clipW), 0, vpW));
            compY1 = std::min(compY1, SafeCeilToInt(firstPage.header.clipY + std::max(0.0f, firstPage.header.clipH), 0, vpH));
        }
    }
    if (compX1 <= compX0 || compY1 <= compY0) {
        return false;
    }

    const int pw = compX1 - compX0;
    const int ph = compY1 - compY0;
    outPremulRgba8.resize(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4u);
    bool anyVisible = false;
    for (int py = compY0; py < compY1; ++py) {
        const float* srcRow = subLayer.premulRgba.data() +
            static_cast<std::size_t>(py - subLayer.originY) * static_cast<std::size_t>(subLayer.width) * 4u;
        std::uint8_t* dstRow = outPremulRgba8.data() +
            static_cast<std::size_t>(py - compY0) * static_cast<std::size_t>(pw) * 4u;
        for (int px = compX0; px < compX1; ++px) {
            const float* sp = srcRow + static_cast<std::size_t>(px - subLayer.originX) * 4u;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(px - compX0) * 4u;
            const float pa = std::clamp(sp[3], 0.0f, 1.0f);
            if (pa > 0.001f) {
                anyVisible = true;
            }
            dp[0] = static_cast<std::uint8_t>(std::clamp(sp[0] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[1] = static_cast<std::uint8_t>(std::clamp(sp[1] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[2] = static_cast<std::uint8_t>(std::clamp(sp[2] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[3] = static_cast<std::uint8_t>(std::clamp(pa * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    if (!anyVisible) {
        return false;
    }
    outPatchX = compX0;
    outPatchY = compY0;
    outPatchW = pw;
    outPatchH = ph;
    return true;
}

bool ZeroCopyGpuStream::ComputeBackdropBlurPatchPremulRgba8(
    const BlurLayerSpan& span,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    int readX,
    int readY,
    int readW,
    int readH,
    const std::uint8_t* topDownReadbackRgba8,
    int& outPatchX,
    int& outPatchY,
    int& outPatchW,
    int& outPatchH,
    std::vector<std::uint8_t>& outPremulRgba8) {
    outPatchX = 0;
    outPatchY = 0;
    outPatchW = 0;
    outPatchH = 0;
    outPremulRgba8.clear();
    if (!topDownReadbackRgba8 || readW <= 0 || readH <= 0 ||
        viewportWidth == 0u || viewportHeight == 0u ||
        !std::isfinite(span.radiusPx) || span.radiusPx <= 0.25f) {
        return false;
    }
    const Rect mr = span.maskDestRect;
    if (!std::isfinite(mr.x) || !std::isfinite(mr.y) ||
        !std::isfinite(mr.width) || !std::isfinite(mr.height) ||
        mr.width <= 1.0f || mr.height <= 1.0f) {
        return false;
    }

    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const float clampedRadius = std::clamp(span.radiusPx, 0.5f, 128.0f);
    const int boxR = std::max(1, static_cast<int>(std::lround(clampedRadius * 0.5f)));

    PremulOffscreenLayer bgLayer{};
    bgLayer.originX = readX;
    bgLayer.originY = readY;
    bgLayer.width = readW;
    bgLayer.height = readH;
    bgLayer.premulRgba.resize(
        static_cast<std::size_t>(readW) * static_cast<std::size_t>(readH) * 4u);
    for (int ry = 0; ry < readH; ++ry) {
        const std::uint8_t* srcRow = topDownReadbackRgba8 +
            static_cast<std::size_t>(ry) * static_cast<std::size_t>(readW) * 4u;
        float* dstRow = bgLayer.premulRgba.data() +
            static_cast<std::size_t>(ry) * static_cast<std::size_t>(readW) * 4u;
        for (int rx = 0; rx < readW; ++rx) {
            const std::uint8_t* sp = srcRow + static_cast<std::size_t>(rx) * 4u;
            float* dp = dstRow + static_cast<std::size_t>(rx) * 4u;
            dp[0] = static_cast<float>(sp[0]) / 255.0f;
            dp[1] = static_cast<float>(sp[1]) / 255.0f;
            dp[2] = static_cast<float>(sp[2]) / 255.0f;
            dp[3] = static_cast<float>(sp[3]) / 255.0f;
        }
    }
    SeparableBoxBlurPremul3Pass(bgLayer, boxR);

    int mx0 = std::clamp(SafeFloorToInt(mr.x, 0, vpW), readX, readX + readW);
    int my0 = std::clamp(SafeFloorToInt(mr.y, 0, vpH), readY, readY + readH);
    int mx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, 0, vpW), readX, readX + readW);
    int my1 = std::clamp(SafeCeilToInt(mr.y + mr.height, 0, vpH), readY, readY + readH);
    if (span.clipActive) {
        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
            return false;
        }
        mx0 = std::max(mx0, SafeFloorToInt(span.clipRect.x, 0, vpW));
        my0 = std::max(my0, SafeFloorToInt(span.clipRect.y, 0, vpH));
        mx1 = std::min(mx1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
        my1 = std::min(my1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
    }
    if (mx1 <= mx0 || my1 <= my0) {
        return false;
    }

    const AxbMtsdfPathEntry* maskEntry = (span.maskMtsdfHash != 0u)
        ? AxbTemplateRegistry::Instance().FindMtsdfPath(span.maskMtsdfHash)
        : nullptr;
    const float screenScale = std::max(
        1.0f,
        0.5f * (mr.width / std::max(1.0f, maskEntry ? maskEntry->boundsW : mr.width) +
                mr.height / std::max(1.0f, maskEntry ? maskEntry->boundsH : mr.height)));

    const int pw = mx1 - mx0;
    const int ph = my1 - my0;
    outPremulRgba8.assign(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4u, 0u);
    bool anyVisible = false;
    for (int py = my0; py < my1; ++py) {
        const float normY = ((static_cast<float>(py) + 0.5f) - mr.y) / std::max(1.0f, mr.height);
        const float* blurRow = bgLayer.premulRgba.data() +
            static_cast<std::size_t>(py - readY) * static_cast<std::size_t>(readW) * 4u;
        std::uint8_t* dstRow = outPremulRgba8.data() +
            static_cast<std::size_t>(py - my0) * static_cast<std::size_t>(pw) * 4u;
        for (int px = mx0; px < mx1; ++px) {
            const float normX = ((static_cast<float>(px) + 0.5f) - mr.x) / std::max(1.0f, mr.width);
            float cov = 1.0f;
            if (maskEntry) {
                cov = EvaluateMtsdfCoverage(*maskEntry, normX, normY, screenScale, 0.0f, false);
            }
            const float effMask = std::clamp(cov * span.maskOpacity, 0.0f, 1.0f);
            if (effMask <= 0.002f) {
                continue;
            }
            const float* bp = blurRow + static_cast<std::size_t>(px - readX) * 4u;
            const float pa = std::clamp(bp[3] * effMask, 0.0f, 1.0f);
            if (pa <= 0.001f) {
                continue;
            }
            anyVisible = true;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(px - mx0) * 4u;
            dp[0] = static_cast<std::uint8_t>(std::clamp(bp[0] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[1] = static_cast<std::uint8_t>(std::clamp(bp[1] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[2] = static_cast<std::uint8_t>(std::clamp(bp[2] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[3] = static_cast<std::uint8_t>(std::clamp(pa * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    if (!anyVisible) {
        return false;
    }
    outPatchX = mx0;
    outPatchY = my0;
    outPatchW = pw;
    outPatchH = ph;
    return true;
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
