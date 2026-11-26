// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gl_functions.h"
#include "render_resource_descs.h"
#include "device_operations.h"
#include "shader_data_type.h"
#include "i_window_manager.h"

namespace UVE::Render::Detail {

struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept { return lhs == rhs; }
};

/// The GL resource maps and function table shared between GlRenderDevice and
/// GlCommandBuffer (module-private — never under include/). GlRenderDevice owns exactly one
/// of these; every GlCommandBuffer it hands out (via CreateCommandBuffer()) holds a
/// reference to it for the lifetime of one frame's recording, resolving UVE resource handles
/// (which are opaque synthetic uint32_t counters, exactly like NullRenderDevice's own handles)
/// to the real GL object names they map to.
struct GlDeviceState {
    Window::IWindowManager* windowManager;
    GlFunctions gl;
    std::unordered_map<std::uint32_t, GLsync> fences;
    std::uint32_t nextFence = 1U;
    struct QueryRecord {
        GLuint native = 0U;
        RenderQueryKind kind = RenderQueryKind::Timestamp;
        bool issued = false;
    };
    std::unordered_map<std::uint32_t, QueryRecord> queries;
    std::uint32_t nextQuery = 1U, activeOcclusion = 0U;
    GLint maxCombinedTextureImageUnits = 0;
    GLint maxUniformBufferBindings = 0;
    GLint maxVertexAttribs = 0;
    GLint maxArrayTextureLayers = 0;
    GLint maxCubeMapTextureSize = 0;
    GLint max3DTextureSize = 0;
    RenderDeviceCapabilities cachedCapabilities{};
    bool capabilitiesCached = false;
    GLuint blitReadFbo = 0U;
    GLuint blitDrawFbo = 0U;
    // GL_COMPRESSED_TEXTURE_FORMATS is the driver-authoritative list for compressed image
    // allocation. Keep the queried internal-format tokens so Basis target selection never
    // requests a format the active context cannot sample/upload.
    std::vector<GLint> compressedTextureFormats;

    /// GL_SHADER_STORAGE_BUFFER_BINDINGS, queried only when supportsComputeShaders (SSBOs
    /// share compute's GL 4.3 core floor; M2f binds them from GRAPHICS-stage shaders). Stays 0
    /// on older/GLES contexts — BindStorageBuffer then refuses loudly instead of calling
    /// into an enum the context doesn't know.
    GLint maxShaderStorageBindings = 0;

    /// True only once the negotiated context is queried and found to be desktop OpenGL 4.3+
    /// (GL_COMPUTE_SHADER's minimum core version) - GLES contexts (compute needs ES 3.1, this
    /// engine's Android baseline is a fixed ES 3.0) are never true. Phase 2a capability gate:
    /// requesting a desktop GL context version (e.g. via --gl-version) is no guarantee the driver
    /// grants it or that it clears the compute-shader threshold, so CreateShader() must check
    /// this cached fact rather than assume "not Android" means compute is always available.
    bool supportsComputeShaders = false;

    struct BufferRecord {
        GLuint glBuffer = 0;
        GLenum target = 0;
        std::uint64_t sizeBytes = 0;
        /// CS7: kept because `target` can no longer identify the usage on its own - an
        /// IndirectStorage buffer's home target is GL_SHADER_STORAGE_BUFFER, exactly like a plain
        /// Storage buffer's, and DrawIndexedIndirect must be able to tell them apart.
        BufferUsage usage = BufferUsage::Vertex;
        /// The placement/update hints as requested (GL folds them into the glBufferData token;
        /// GetBufferDesc reports them back unchanged).
        BufferMemory memory = BufferMemory::Auto;
        BufferUpdateFrequency updateFrequency = BufferUpdateFrequency::Static;
    };
    std::unordered_map<std::uint32_t, BufferRecord> buffers;
    std::uint32_t nextBufferHandle = 1;

    struct TextureRecord {
        GLuint glTexture = 0;
        TextureDesc desc;
    };
    std::unordered_map<std::uint32_t, TextureRecord> textures;
    std::uint32_t nextTextureHandle = 1;

    struct SamplerRecord {
        GLuint glSampler = 0;
        SamplerDesc desc;
    };
    std::unordered_map<std::uint32_t, SamplerRecord> samplers;
    std::uint32_t nextSamplerHandle = 1;

    /// Tier 2.2 anisotropy capability, probed once at init from the extension string.
    bool samplerAnisotropySupported = false;
    float maxSamplerAnisotropy = 1.0F;
    /// Warn-once latch for anisotropy clamps (this backend has no VK-style bitmask infra).
    bool warnedSamplerAnisotropyClamped = false;
    /// True when RGBA16Float color attachments are renderable (core on desktop GL 3.3+, or via
    /// GL_EXT_color_buffer_float / GL_EXT_color_buffer_half_float on GLES 3.0).
    bool halfFloatColorTargetsSupported = false;

    /// Cached FBO names keyed by the pair of UVE texture handles attached to them. The render
    /// device invalidates entries when a dependent texture is destroyed; the cache itself owns no
    /// texture lifetime and is released while the GL context is still current.
    // Tier 2.3: FBOs are keyed by the attachment pair AND the rendered layers (one cached
    // FBO per layer — a layer is baked into the attachment at glFramebufferTextureLayer).
    struct FramebufferKey {
        std::uint32_t color = 0U;
        std::uint32_t depth = 0U;
        std::uint32_t colorLayer = 0U;
        std::uint32_t depthLayer = 0U;
        // Tier 2.4: locations 1..3 ride the key (handles AND layers — one cached FBO per
        // attachment set, same as the 2.3 layer rule; unused slots stay 0).
        std::array<std::uint32_t, kExtraColorAttachmentCount> extraColors{};
        std::array<std::uint32_t, kExtraColorAttachmentCount> extraColorLayers{};
        bool operator==(const FramebufferKey& other) const noexcept {
            return color == other.color && depth == other.depth &&
                   colorLayer == other.colorLayer && depthLayer == other.depthLayer &&
                   extraColors == other.extraColors && extraColorLayers == other.extraColorLayers;
        }
    };
    struct FramebufferKeyHash {
        std::size_t operator()(const FramebufferKey& key) const noexcept {
            std::size_t hash = std::hash<std::uint32_t>{}(key.color);
            hash = hash * 31U + std::hash<std::uint32_t>{}(key.depth);
            hash = hash * 31U + std::hash<std::uint32_t>{}(key.colorLayer);
            hash = hash * 31U + std::hash<std::uint32_t>{}(key.depthLayer);
            for (const std::uint32_t extra : key.extraColors) {
                hash = hash * 31U + std::hash<std::uint32_t>{}(extra);
            }
            for (const std::uint32_t extraLayer : key.extraColorLayers) {
                hash = hash * 31U + std::hash<std::uint32_t>{}(extraLayer);
            }
            return hash;
        }
    };
    std::unordered_map<FramebufferKey, GLuint, FramebufferKeyHash> framebufferCache;

    struct ShaderRecord {
        GLuint glShader = 0;
        ShaderStage stage = ShaderStage::Vertex; // M5a: CreateComputePipeline validates it
    };
    std::unordered_map<std::uint32_t, ShaderRecord> shaders;
    std::uint32_t nextShaderHandle = 1;

    struct PipelineRecord {
        GLuint glProgram = 0;
        GLuint glVao = 0; // One VAO per pipeline; vertex attrib pointers are configured against
                           // it lazily, when BindVertexBuffer() runs (GL ties
                           // glVertexAttribPointer to whichever buffer is bound at the time it's
                           // called, which this RHI's Bind-then-Draw command buffer flow only
                           // knows once BindVertexBuffer() actually executes).
        std::vector<VertexAttribute> vertexLayout;
        std::uint32_t vertexStride = 0;
        bool depthTestEnabled = true;
        bool depthWriteEnabled = true;
        PipelineBlendMode blendMode = PipelineBlendMode::Opaque;

        // Tier 2.1: rasterizer tail mirroring PipelineDesc — defaults reproduce the pre-2.1
        // effective state (no culling, CCW front, fill, no polygon offset, LESS compare).
        CullMode cullMode = CullMode::None;
        FrontFace frontFace = FrontFace::CounterClockwise;
        FillMode fillMode = FillMode::Fill;
        bool depthBiasEnabled = false;
        float depthBiasConstantFactor = 0.0F;
        float depthBiasSlopeFactor = 0.0F;
        DepthCompare depthCompare = DepthCompare::Less;
        StencilState stencil;

        /// Tier 2.8: the draw mode, read by Draw*UVE at record time — GL takes topology as a
        /// draw-call parameter rather than pipeline state, so the record carries what Vulkan
        /// bakes into the pipeline object.
        PrimitiveTopology topology = PrimitiveTopology::Triangles;

        /// M5a: true for programs linked from CreateComputePipeline(). Such records carry no
        /// VAO/vertex layout and no render state — GlCommandBuffer skips the graphics-side
        /// setup for them and Dispatch() requires one to be the currently bound pipeline.
        bool isCompute = false;

        /// Cached at uniform reflection time: true if at least one uniform is GL_IMAGE_2D, so
        /// BindTexture can skip scanning the uniform map when false.
        bool hasImageUniforms = false;

        /// Every uniform reflected right after this pipeline's program successfully linked
        /// (Increment 21) — GlCommandBuffer's SetUniform*UVE calls look a name up here instead
        /// of calling glGetUniformLocation per draw. Empty for a pipeline created via
        /// CreatePipelineFromBinary() before its own post-load reflection pass runs.
        struct UniformRecord {
            ShaderDataType type = ShaderDataType::Float;
            GLint location = -1;
            std::uint32_t arraySize = 1;
            // M5b: true for GL_IMAGE_2D uniforms (imageLoad/imageStore). Reported as Int like
            // samplers (the value is the image-unit index, settable through SetUniformInt);
            // BindTexture double-binds image-using programs through glBindImageTexture.
            bool isImageUniform = false;
        };
        std::unordered_map<std::string, UniformRecord, TransparentStringHash, TransparentStringEqual> uniforms;
        // Indexed aliases stay separate so public reflection still reports one array record.
        decltype(uniforms) uniformElements;

        /// Names already reported by GlCommandBuffer::FindUniform's "not an active uniform"
        /// warning. A renderer sets frame uniforms on every program variant, and a variant that
        /// compiled a given uniform away (a #if-guarded feature block) would otherwise format and
        /// write one log line per frame per program - real I/O on the render thread. One warning
        /// per program is enough to diagnose the mismatch.
        mutable std::unordered_set<std::string> warnedUnknownUniforms;
    };
    std::unordered_map<std::uint32_t, PipelineRecord> pipelines;
    std::uint32_t nextPipelineHandle = 1;
};

} // namespace UVE::Render::Detail
