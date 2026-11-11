#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace aero {

enum class GpuBackendKind : std::uint8_t {
    OpenGL = 0,
    Software = 1,
};

struct GpuBackendConfig {
    ShaderBackend shaderBackend = ShaderBackend::DesktopGL33;
    std::size_t maxPagesInRing = 256u;
    std::size_t initialQuadCapacity = 256u * (kVerticesPerCommandPage / 4u);
    std::uint32_t defaultAtlasSize = 256u;
    std::uint32_t initialViewportWidth = 1280u;
    std::uint32_t initialViewportHeight = 720u;
    bool autoBuildDefaultAtlas = true;
    bool preferHardwareGpu = true;
};

struct GlScissorBox {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct GpuBackendFrameStats {
    std::uint64_t frameIndex = 0;
    std::size_t dirtySpansUploaded = 0;
    std::size_t bytesUploaded = 0;
    std::size_t clipBatchesProcessed = 0;
    std::size_t drawCallsIssued = 0;
    std::size_t quadsSubmitted = 0;
    std::size_t indicesSubmitted = 0;
    std::size_t trianglesSubmitted = 0;
    std::size_t scissorBatchCount = 0;
    bool quadEboExpanded = false;
    bool succeeded = false;
};

struct GpuBackendTelemetry {
    std::uint64_t framesPresented = 0;
    std::uint64_t totalDirtySpansUploaded = 0;
    std::uint64_t totalBytesUploaded = 0;
    std::uint64_t totalDrawCallsIssued = 0;
    std::uint64_t totalQuadsSubmitted = 0;
    std::uint64_t totalTrianglesSubmitted = 0;
    std::uint32_t atlasUploadCount = 0;
    std::uint32_t atlasSubUpdateCount = 0;
    std::uint32_t quadEboExpansionCount = 0;
    GpuBackendFrameStats lastFrame{};
};

struct GlDynamicProbeResult {
    bool libGlLoaded = false;
    bool libEglLoaded = false;
    bool procAddressResolverReady = false;
    bool coreEntryPointsFound = false;
    bool hasCurrentContext = false;
    std::uint32_t resolvedSymbolCount = 0;
    std::string loadedLibraryName;
    std::string glVersionString;
    std::string glRendererString;
    std::string diagnostics;
};

// Minimal portable OpenGL 3.3 Core / WebGL2 C ABI types & function table (zero external GL headers required)
using AeroGLenum = std::uint32_t;
using AeroGLboolean = std::uint8_t;
using AeroGLbitfield = std::uint32_t;
using AeroGLint = std::int32_t;
using AeroGLuint = std::uint32_t;
using AeroGLsizei = std::int32_t;
using AeroGLfloat = float;
using AeroGLchar = char;
using AeroGLintptr = std::ptrdiff_t;
using AeroGLsizeiptr = std::ptrdiff_t;

struct GlFunctionTable {
    void (*Viewport)(AeroGLint x, AeroGLint y, AeroGLsizei width, AeroGLsizei height) = nullptr;
    void (*Scissor)(AeroGLint x, AeroGLint y, AeroGLsizei width, AeroGLsizei height) = nullptr;
    void (*Enable)(AeroGLenum cap) = nullptr;
    void (*Disable)(AeroGLenum cap) = nullptr;
    void (*BlendFuncSeparate)(
        AeroGLenum srcRGB,
        AeroGLenum dstRGB,
        AeroGLenum srcAlpha,
        AeroGLenum dstAlpha) = nullptr;
    void (*ClearColor)(AeroGLfloat r, AeroGLfloat g, AeroGLfloat b, AeroGLfloat a) = nullptr;
    void (*Clear)(AeroGLbitfield mask) = nullptr;

    void (*GenBuffers)(AeroGLsizei n, AeroGLuint* buffers) = nullptr;
    void (*DeleteBuffers)(AeroGLsizei n, const AeroGLuint* buffers) = nullptr;
    void (*BindBuffer)(AeroGLenum target, AeroGLuint buffer) = nullptr;
    void (*BufferData)(
        AeroGLenum target,
        AeroGLsizeiptr size,
        const void* data,
        AeroGLenum usage) = nullptr;
    void (*BufferSubData)(
        AeroGLenum target,
        AeroGLintptr offset,
        AeroGLsizeiptr size,
        const void* data) = nullptr;

    void (*GenVertexArrays)(AeroGLsizei n, AeroGLuint* arrays) = nullptr;
    void (*DeleteVertexArrays)(AeroGLsizei n, const AeroGLuint* arrays) = nullptr;
    void (*BindVertexArray)(AeroGLuint array) = nullptr;
    void (*EnableVertexAttribArray)(AeroGLuint index) = nullptr;
    void (*VertexAttribPointer)(
        AeroGLuint index,
        AeroGLint size,
        AeroGLenum type,
        AeroGLboolean normalized,
        AeroGLsizei stride,
        const void* pointer) = nullptr;
    void (*VertexAttribIPointer)(
        AeroGLuint index,
        AeroGLint size,
        AeroGLenum type,
        AeroGLsizei stride,
        const void* pointer) = nullptr;

    AeroGLuint (*CreateShader)(AeroGLenum shaderType) = nullptr;
    void (*DeleteShader)(AeroGLuint shader) = nullptr;
    void (*ShaderSource)(
        AeroGLuint shader,
        AeroGLsizei count,
        const AeroGLchar* const* string,
        const AeroGLint* length) = nullptr;
    void (*CompileShader)(AeroGLuint shader) = nullptr;
    void (*GetShaderiv)(AeroGLuint shader, AeroGLenum pname, AeroGLint* params) = nullptr;
    void (*GetShaderInfoLog)(
        AeroGLuint shader,
        AeroGLsizei maxLength,
        AeroGLsizei* length,
        AeroGLchar* infoLog) = nullptr;

    AeroGLuint (*CreateProgram)() = nullptr;
    void (*DeleteProgram)(AeroGLuint program) = nullptr;
    void (*AttachShader)(AeroGLuint program, AeroGLuint shader) = nullptr;
    void (*LinkProgram)(AeroGLuint program) = nullptr;
    void (*GetProgramiv)(AeroGLuint program, AeroGLenum pname, AeroGLint* params) = nullptr;
    void (*GetProgramInfoLog)(
        AeroGLuint program,
        AeroGLsizei maxLength,
        AeroGLsizei* length,
        AeroGLchar* infoLog) = nullptr;
    void (*UseProgram)(AeroGLuint program) = nullptr;
    AeroGLint (*GetUniformLocation)(AeroGLuint program, const AeroGLchar* name) = nullptr;
    void (*Uniform1i)(AeroGLint location, AeroGLint v0) = nullptr;
    void (*Uniform2f)(AeroGLint location, AeroGLfloat v0, AeroGLfloat v1) = nullptr;

    void (*GenTextures)(AeroGLsizei n, AeroGLuint* textures) = nullptr;
    void (*DeleteTextures)(AeroGLsizei n, const AeroGLuint* textures) = nullptr;
    void (*ActiveTexture)(AeroGLenum texture) = nullptr;
    void (*BindTexture)(AeroGLenum target, AeroGLuint texture) = nullptr;
    void (*TexImage2D)(
        AeroGLenum target,
        AeroGLint level,
        AeroGLint internalFormat,
        AeroGLsizei width,
        AeroGLsizei height,
        AeroGLint border,
        AeroGLenum format,
        AeroGLenum type,
        const void* data) = nullptr;
    void (*TexSubImage2D)(
        AeroGLenum target,
        AeroGLint level,
        AeroGLint xoffset,
        AeroGLint yoffset,
        AeroGLsizei width,
        AeroGLsizei height,
        AeroGLenum format,
        AeroGLenum type,
        const void* pixels) = nullptr;
    void (*TexParameteri)(AeroGLenum target, AeroGLenum pname, AeroGLint param) = nullptr;
    void (*PixelStorei)(AeroGLenum pname, AeroGLint param) = nullptr;

    void (*DrawElements)(
        AeroGLenum mode,
        AeroGLsizei count,
        AeroGLenum type,
        const void* indices) = nullptr;
    void (*ReadPixels)(
        AeroGLint x,
        AeroGLint y,
        AeroGLsizei width,
        AeroGLsizei height,
        AeroGLenum format,
        AeroGLenum type,
        void* data) = nullptr;
    AeroGLenum (*GetError)() = nullptr;
    const std::uint8_t* (*GetString)(AeroGLenum name) = nullptr;

    [[nodiscard]] bool IsComplete() const noexcept;
};

// Build static Quad Index Buffer mapping each 4-vertex quad (v0, v1, v2, v3)
// into 6 triangle indices (v0, v1, v2, v0, v2, v3) for GL_TRIANGLES rendering.
void BuildStaticQuadIndexBuffer(
    std::size_t quadCount,
    std::vector<std::uint32_t>& outIndices);

// Convert AeroGUI top-left scissor clip rect into OpenGL bottom-left glScissor box.
[[nodiscard]] GlScissorBox ComputeGlScissorBox(
    const Rect& clipRectTopLeft,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) noexcept;

// Synthesize an RGBA8 UI & SDF glyph atlas matching PagedRenderTree's 16x16 glyph UVs
// (top half v in [0..0.5)) and registered AxbTemplateRegistry MTSDF icon cells (bottom half).
void BuildDefaultUiAtlasRgba8(
    const PagedRenderTree& referenceTree,
    std::uint32_t atlasSize,
    std::vector<std::uint8_t>& outRgba8);

// Probe dynamic OpenGL / EGL shared libraries on the current platform without crashing when headless.
[[nodiscard]] GlDynamicProbeResult ProbeOpenGlDynamicLibrary() noexcept;

// Zero-dependency image writers (PPM P6 and 32-bit BGRA BMP) for CI & Demo frame verification.
[[nodiscard]] bool WriteRgba8ToPpm(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) noexcept;

[[nodiscard]] bool WriteRgba8ToBmp(
    std::string_view filePath,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8Pixels) noexcept;

// ============================================================================
// IGpuDeviceBackend: Unified Real GPU & Software Fallback Device Interface
// ============================================================================
class IGpuDeviceBackend {
public:
    virtual ~IGpuDeviceBackend() = default;

    [[nodiscard]] virtual GpuBackendKind Kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view BackendName() const noexcept = 0;
    [[nodiscard]] virtual ShaderBackend ActiveShaderBackend() const noexcept = 0;
    [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;

    virtual bool Initialize(const GpuBackendConfig& config = GpuBackendConfig{}) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool CompileAndLinkUberShader(
        std::string_view vertexShaderSrc,
        std::string_view pixelShaderSrc) = 0;

    virtual bool UploadAtlasTexture(
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) = 0;

    virtual bool UpdateAtlasSubRegion(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) = 0;

    virtual bool AllocateRingVertexBuffer(std::size_t maxPagesInRing) = 0;
    virtual bool EnsureQuadIndexCapacity(std::size_t requiredQuadCount) = 0;

    virtual GpuBackendFrameStats ExecuteSubmission(
        const PagedRenderTree& tree,
        const std::vector<GlBufferSubDataSpan>& uploadSpans,
        const std::vector<GpuClipDrawBatch>& drawBatches,
        std::size_t ringPageCapacity,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f}) = 0;

    // Consume the dirty spans and coalesced clip batches already recorded into ZeroCopyGpuStream
    // (e.g. immediately after IHostDriver::RecordGpuCommands).
    GpuBackendFrameStats PresentStream(
        const PagedRenderTree& tree,
        const ZeroCopyGpuStream& stream,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f});

    // Convenience one-step submit + present for standalone usage without IHostDriver.
    GpuBackendFrameStats SubmitAndPresent(
        PagedRenderTree& tree,
        ZeroCopyGpuStream& stream,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f});

    virtual bool ReadbackFramebufferRgba8(
        std::vector<std::uint8_t>& outRgba8,
        std::uint32_t& outWidth,
        std::uint32_t& outHeight) const = 0;

    [[nodiscard]] virtual std::size_t QuadIndexCapacity() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::uint32_t>& QuadIndexCpuMirror() const noexcept = 0;
    [[nodiscard]] virtual std::string_view LastDiagnostics() const noexcept = 0;
    [[nodiscard]] virtual const GpuBackendTelemetry& Telemetry() const noexcept = 0;
};

// ============================================================================
// 1. OpenGlDeviceBackend (Desktop OpenGL 3.3 Core & Emscripten WebGL2)
// ============================================================================
class OpenGlDeviceBackend final : public IGpuDeviceBackend {
public:
    OpenGlDeviceBackend() = default;
    explicit OpenGlDeviceBackend(const GlFunctionTable& customFunctions) noexcept;
    ~OpenGlDeviceBackend() override;

    OpenGlDeviceBackend(const OpenGlDeviceBackend&) = delete;
    OpenGlDeviceBackend& operator=(const OpenGlDeviceBackend&) = delete;

    void SetCustomFunctionTable(const GlFunctionTable& customFunctions) noexcept;
    [[nodiscard]] const GlFunctionTable& FunctionTable() const noexcept { return gl_; }

    [[nodiscard]] GpuBackendKind Kind() const noexcept override { return GpuBackendKind::OpenGL; }
    [[nodiscard]] std::string_view BackendName() const noexcept override {
        return (shaderBackend_ == ShaderBackend::WebGL2)
            ? "OpenGlDeviceBackend(WebGL2)"
            : "OpenGlDeviceBackend(GL3.3)";
    }
    [[nodiscard]] ShaderBackend ActiveShaderBackend() const noexcept override {
        return shaderBackend_;
    }
    [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

    bool Initialize(const GpuBackendConfig& config = GpuBackendConfig{}) override;
    void Shutdown() noexcept override;

    bool CompileAndLinkUberShader(
        std::string_view vertexShaderSrc,
        std::string_view pixelShaderSrc) override;

    bool UploadAtlasTexture(
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) override;

    bool UpdateAtlasSubRegion(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) override;

    bool AllocateRingVertexBuffer(std::size_t maxPagesInRing) override;
    bool EnsureQuadIndexCapacity(std::size_t requiredQuadCount) override;

    GpuBackendFrameStats ExecuteSubmission(
        const PagedRenderTree& tree,
        const std::vector<GlBufferSubDataSpan>& uploadSpans,
        const std::vector<GpuClipDrawBatch>& drawBatches,
        std::size_t ringPageCapacity,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f}) override;

    bool ReadbackFramebufferRgba8(
        std::vector<std::uint8_t>& outRgba8,
        std::uint32_t& outWidth,
        std::uint32_t& outHeight) const override;

    [[nodiscard]] std::size_t QuadIndexCapacity() const noexcept override {
        return quadCapacity_;
    }
    [[nodiscard]] const std::vector<std::uint32_t>& QuadIndexCpuMirror() const noexcept override {
        return quadIndexMirror_;
    }
    [[nodiscard]] std::string_view LastDiagnostics() const noexcept override {
        return diagnostics_;
    }
    [[nodiscard]] const GpuBackendTelemetry& Telemetry() const noexcept override {
        return telemetry_;
    }

    [[nodiscard]] AeroGLuint ProgramHandle() const noexcept { return programId_; }
    [[nodiscard]] AeroGLuint VaoHandle() const noexcept { return vaoId_; }
    [[nodiscard]] AeroGLuint RingVboHandle() const noexcept { return ringVboId_; }
    [[nodiscard]] AeroGLuint QuadEboHandle() const noexcept { return quadEboId_; }
    [[nodiscard]] AeroGLuint AtlasTextureHandle() const noexcept { return atlasTextureId_; }
    [[nodiscard]] std::size_t RingBufferByteCapacity() const noexcept { return ringBufferBytes_; }

private:
    bool LoadDynamicGlEntryPoints();
    void BindAeroVertexLayout() noexcept;

    GlFunctionTable gl_{};
    bool hasCustomTable_ = false;
    void* dynGlLibHandle_ = nullptr;
    void* dynEglLibHandle_ = nullptr;
    void* dynGlesLibHandle_ = nullptr;

    ShaderBackend shaderBackend_ = ShaderBackend::DesktopGL33;
    bool initialized_ = false;

    AeroGLuint programId_ = 0;
    AeroGLuint vaoId_ = 0;
    AeroGLuint ringVboId_ = 0;
    AeroGLuint quadEboId_ = 0;
    AeroGLuint atlasTextureId_ = 0;

    AeroGLint uViewportSizeLoc_ = -1;
    AeroGLint uQuadHalfExtentLoc_ = -1;
    AeroGLint uAtlasSamplerLoc_ = -1;

    std::size_t maxPagesInRing_ = 0;
    std::size_t ringBufferBytes_ = 0;
    std::size_t quadCapacity_ = 0;
    std::uint32_t atlasWidth_ = 0;
    std::uint32_t atlasHeight_ = 0;
    std::uint32_t lastViewportWidth_ = 0;
    std::uint32_t lastViewportHeight_ = 0;

    std::vector<std::uint32_t> quadIndexMirror_;
    std::string diagnostics_;
    GpuBackendTelemetry telemetry_{};
};

// ============================================================================
// 2. SoftwareSurfaceBackend (Headless Framebuffer Presenter & Fallback Backend)
// ============================================================================
class SoftwareSurfaceBackend final : public IGpuDeviceBackend {
public:
    SoftwareSurfaceBackend() = default;
    ~SoftwareSurfaceBackend() override = default;

    [[nodiscard]] GpuBackendKind Kind() const noexcept override { return GpuBackendKind::Software; }
    [[nodiscard]] std::string_view BackendName() const noexcept override {
        return "SoftwareSurfaceBackend";
    }
    [[nodiscard]] ShaderBackend ActiveShaderBackend() const noexcept override {
        return shaderBackend_;
    }
    [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

    bool Initialize(const GpuBackendConfig& config = GpuBackendConfig{}) override;
    void Shutdown() noexcept override;

    bool CompileAndLinkUberShader(
        std::string_view vertexShaderSrc,
        std::string_view pixelShaderSrc) override;

    bool UploadAtlasTexture(
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) override;

    bool UpdateAtlasSubRegion(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8Pixels) override;

    bool AllocateRingVertexBuffer(std::size_t maxPagesInRing) override;
    bool EnsureQuadIndexCapacity(std::size_t requiredQuadCount) override;

    GpuBackendFrameStats ExecuteSubmission(
        const PagedRenderTree& tree,
        const std::vector<GlBufferSubDataSpan>& uploadSpans,
        const std::vector<GpuClipDrawBatch>& drawBatches,
        std::size_t ringPageCapacity,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f}) override;

    bool ReadbackFramebufferRgba8(
        std::vector<std::uint8_t>& outRgba8,
        std::uint32_t& outWidth,
        std::uint32_t& outHeight) const override;

    [[nodiscard]] const std::vector<std::uint8_t>& FramebufferRgba8() const noexcept {
        return framebufferRgba8_;
    }
    [[nodiscard]] std::uint32_t FramebufferWidth() const noexcept { return framebufferWidth_; }
    [[nodiscard]] std::uint32_t FramebufferHeight() const noexcept { return framebufferHeight_; }

    [[nodiscard]] const std::vector<std::uint8_t>& AtlasRgba8() const noexcept {
        return atlasRgba8_;
    }
    [[nodiscard]] std::uint32_t AtlasWidth() const noexcept { return atlasWidth_; }
    [[nodiscard]] std::uint32_t AtlasHeight() const noexcept { return atlasHeight_; }

    [[nodiscard]] std::size_t QuadIndexCapacity() const noexcept override {
        return quadCapacity_;
    }
    [[nodiscard]] const std::vector<std::uint32_t>& QuadIndexCpuMirror() const noexcept override {
        return quadIndexMirror_;
    }
    [[nodiscard]] std::string_view LastDiagnostics() const noexcept override {
        return diagnostics_;
    }
    [[nodiscard]] const GpuBackendTelemetry& Telemetry() const noexcept override {
        return telemetry_;
    }

private:
    ShaderBackend shaderBackend_ = ShaderBackend::DesktopGL33;
    bool initialized_ = false;
    bool shaderValidated_ = false;
    std::size_t maxPagesInRing_ = 0;
    std::size_t quadCapacity_ = 0;
    std::uint32_t atlasWidth_ = 0;
    std::uint32_t atlasHeight_ = 0;
    std::uint32_t framebufferWidth_ = 0;
    std::uint32_t framebufferHeight_ = 0;
    std::uint32_t lastRenderedTreeId_ = 0;
    Color lastClearColor_{0.0f, 0.0f, 0.0f, 0.0f};

    std::vector<std::uint8_t> ringMirrorBytes_;
    std::vector<std::uint32_t> quadIndexMirror_;
    std::vector<std::uint8_t> atlasRgba8_;
    std::vector<std::uint8_t> framebufferRgba8_;
    std::string diagnostics_;
    GpuBackendTelemetry telemetry_{};
};

// Automatically create and initialize OpenGlDeviceBackend when an active GL/WebGL2 context
// is ready (and preferHardwareGpu is true), or gracefully fall back to SoftwareSurfaceBackend.
[[nodiscard]] std::unique_ptr<IGpuDeviceBackend> CreateBestAvailableGpuBackend(
    const GpuBackendConfig& config = GpuBackendConfig{},
    std::string* outDiagnostics = nullptr);

} // namespace aero
