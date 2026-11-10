#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"

namespace aero {

namespace {

constexpr std::string_view kUberVertexShaderGL33 = R"(#version 330 core
// AeroGUI 2.0 Zero-Copy 24B Vertex UberShader (Desktop OpenGL 3.3 Core)
layout(location = 0) in vec2 a_Position;   // float x, y (offset 0, 8B)
layout(location = 1) in vec2 a_TexCoord;   // float u, v or SDF local coords (offset 8, 8B)
layout(location = 2) in uint a_PackedRgba; // uint32_t rgba (offset 16, 4B)
layout(location = 3) in uint a_PrimFlags;  // low 16b: primType, high 16b: flags (offset 20, 4B)

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
    v_PrimType = a_PrimFlags & 0xFFFFu;
    v_Flags    = (a_PrimFlags >> 16u) & 0xFFFFu;
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
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // 0: SolidQuad
    if (v_PrimType == 0u) {
        o_FragColor = v_Color;
        return;
    }

    // 1: SdfRoundedRect (4-vertex Quad + Analytical SDF AA, 0 CPU tessellation)
    if (v_PrimType == 1u) {
        float cornerRadius    = float(v_Flags & 0xFFu) * 0.5;
        float borderThickness = float((v_Flags >> 8u) & 0xFFu) * 0.5;
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float dist = sdRoundedBox(v_UvOrLocalPos, halfSize, cornerRadius);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
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

    // 3: TexturedQuad
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
    v_PrimType = a_PrimFlags & 0xFFFFu;
    v_Flags    = (a_PrimFlags >> 16u) & 0xFFFFu;
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
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    if (v_PrimType == 0u) {
        o_FragColor = v_Color;
        return;
    }

    if (v_PrimType == 1u) {
        float cornerRadius    = float(v_Flags & 0xFFu) * 0.5;
        float borderThickness = float((v_Flags >> 8u) & 0xFFu) * 0.5;
        vec2 halfSize = max(abs(u_QuadHalfExtent), v_QuadHalfSize);
        float dist = sdRoundedBox(v_UvOrLocalPos, halfSize, cornerRadius);
        float aa = max(fwidth(dist) * 0.75, 1e-4);
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
    let q = abs(p) - b + vec2<f32>(r, r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0, 0.0))) - r;
}

fn median3(r : f32, g : f32, b : f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
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
    out.primType = in.primFlags & 0xFFFFu;
    out.flags = (in.primFlags >> 16u) & 0xFFFFu;
    return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
    if (in.primType == 0u) {
        return in.color;
    }
    if (in.primType == 1u) {
        let radius = f32(in.flags & 0xFFu) * 0.5;
        let borderThickness = f32((in.flags >> 8u) & 0xFFu) * 0.5;
        let halfSize = max(abs(u_Globals.quadHalfExtent), in.quadHalfSize);
        let dist = sdRoundedBox(in.uvOrLocal, halfSize, radius);
        let aa = max(fwidth(dist) * 0.75, 1e-4);
        var cov = smoothstep(aa, -aa, dist);
        if (borderThickness > 0.0) {
            let innerDist = dist + borderThickness;
            cov = cov * smoothstep(-aa, aa, innerDist);
        }
        return vec4<f32>(in.color.rgb, in.color.a * cov);
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
    return textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal) * in.color;
}
)";

[[nodiscard]] bool SameBatchScissor(const GpuClipDrawBatch& batch, const CommandPageHeader& hdr) noexcept {
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

    for (std::uint32_t nid = 0; nid < static_cast<std::uint32_t>(tree.NodeCount()); ++nid) {
        const RenderNodeDescriptor* nd = tree.GetNode(nid);
        if (!nd || nd->pageIndex >= tree.PageCount() || nd->opacity <= 0.001f) {
            continue;
        }
        const CommandPage& page = tree.GetPage(nd->pageIndex);
        const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
        const int clipMinX = clipOn ? std::max(0, static_cast<int>(std::floor(page.header.clipX))) : 0;
        const int clipMinY = clipOn ? std::max(0, static_cast<int>(std::floor(page.header.clipY))) : 0;
        const int clipMaxX = clipOn
            ? std::min(static_cast<int>(viewportWidth), static_cast<int>(std::ceil(page.header.clipX + page.header.clipW)))
            : static_cast<int>(viewportWidth);
        const int clipMaxY = clipOn
            ? std::min(static_cast<int>(viewportHeight), static_cast<int>(std::ceil(page.header.clipY + page.header.clipH)))
            : static_cast<int>(viewportHeight);

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
            const std::uint8_t a8 = static_cast<std::uint8_t>((v0.rgba >> 24u) & 0xFFu);
            if (a8 == 0u) {
                continue;
            }
            const std::uint8_t r8 = static_cast<std::uint8_t>(v0.rgba & 0xFFu);
            const std::uint8_t g8 = static_cast<std::uint8_t>((v0.rgba >> 8u) & 0xFFu);
            const std::uint8_t b8 = static_cast<std::uint8_t>((v0.rgba >> 16u) & 0xFFu);
            const float baseAlpha = static_cast<float>(a8) / 255.0f;

            const float minX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
            const float maxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
            const float minY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
            const float maxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));

            const float e1x = v1.x - v0.x;
            const float e1y = v1.y - v0.y;
            const float e3x = v3.x - v0.x;
            const float e3y = v3.y - v0.y;
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

            const int pxStart = std::max(clipMinX, static_cast<int>(std::floor(minX)));
            const int pyStart = std::max(clipMinY, static_cast<int>(std::floor(minY)));
            const int pxEnd = std::min(clipMaxX, static_cast<int>(std::ceil(maxX)));
            const int pyEnd = std::min(clipMaxY, static_cast<int>(std::ceil(maxY)));

            const AxbMtsdfPathEntry* mtsdfEntry = (nd->primType == AeroPrimType::MsdfVectorPath && nd->mtsdfPathHash != 0)
                ? AxbTemplateRegistry::Instance().FindMtsdfPath(nd->mtsdfPathHash)
                : nullptr;

            for (int py = pyStart; py < pyEnd; ++py) {
                for (int px = pxStart; px < pxEnd; ++px) {
                    const float sx = static_cast<float>(px) + 0.5f;
                    const float sy = static_cast<float>(py) + 0.5f;
                    const float dx = sx - v0.x;
                    const float dy = sy - v0.y;
                    const float su = (dx * e3y - dy * e3x) * invDet;
                    const float sv = (e1x * dy - e1y * dx) * invDet;
                    if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) {
                        continue;
                    }

                    float coverage = 1.0f;
                    const auto prim = static_cast<AeroPrimType>(v0.primType);
                    if (prim == AeroPrimType::SdfRoundedRect) {
                        float rad = 0.0f;
                        float border = 0.0f;
                        UnpackSdfRoundedRectParams(v0.flags, rad, border);
                        const float unscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
                        const float unscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
                        const float halfW = (unscaledHalfW > 1e-4f) ? unscaledHalfW : (localQuadW * 0.5f);
                        const float halfH = (unscaledHalfH > 1e-4f) ? unscaledHalfH : (localQuadH * 0.5f);
                        const float localX = (su - 0.5f) * (2.0f * halfW);
                        const float localY = (sv - 0.5f) * (2.0f * halfH);
                        const float aaFeather = std::max(
                            1e-4f,
                            (halfW * localQuadH + halfH * localQuadW) * std::fabs(invDet));
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
                    } else if (prim == AeroPrimType::SdfTextGlyph) {
                        const bool inGlyphBody = (su >= 0.15f && su <= 0.85f && sv >= 0.15f && sv <= 0.85f);
                        coverage = inGlyphBody ? 0.92f : 0.0f;
                    }

                    if (coverage > 0.002f) {
                        std::uint8_t* dst = outRgba8.data() +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        BlendPixelOver(dst, r8, g8, b8, baseAlpha * coverage);
                    }
                }
            }
        }
    }
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
