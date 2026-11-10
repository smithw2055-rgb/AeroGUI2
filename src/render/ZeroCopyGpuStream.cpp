#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <cstring>

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
// AeroGUI 2.0 Single-Draw-Call Analytical SDF & Text UberPixelShader (Desktop GL 3.3 Core)
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
        float aa = max(fwidth(dist), 0.75);
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
// AeroGUI 2.0 WebGL2 UberPixelShader (Automatic Grayscale SDF AA Text Routing)
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
        float aa = max(fwidth(dist), 0.75);
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

    o_FragColor = texture(u_GlyphAndUiAtlas, v_UvOrLocalPos) * v_Color;
}
)";

constexpr std::string_view kUberShaderWGSL = R"(// AeroGUI 2.0 Unified WebGPU WGSL UberShader (24B AeroVertex + Analytical SDF AA)
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
        let aa = max(fwidth(dist), 0.75);
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
    return textureSample(u_AtlasTex, u_AtlasSampler, in.uvOrLocal) * in.color;
}
)";

} // namespace

ZeroCopyGpuStream::ZeroCopyGpuStream(ShaderBackend backend, std::size_t maxPagesInRing)
    : maxPagesInRing_(std::max<std::size_t>(16u, maxPagesInRing)) {
    const std::size_t pageVertexBytes = kVerticesPerCommandPage * sizeof(AeroVertex);
    ringCapacityBytes_ = maxPagesInRing_ * pageVertexBytes;
    ownedRingStorage_.resize(ringCapacityBytes_, 0u);
    mappedRingBase_ = ownedRingStorage_.data();
    subDataSpans_.reserve(maxPagesInRing_);
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

        if (mappedRingBase_ && (byteOffset + activeBytes <= ringCapacityBytes_)) {
            std::memcpy(
                mappedRingBase_ + byteOffset,
                srcPage.vertices.data(),
                activeBytes);
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

    // Single instanced/batched UberShader draw call covers all active pages without state change
    stats.drawCallCount = (tree.NodeCount() > 0) ? 1u : 0u;
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
