// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gl_render_device.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>

#if !defined(__ANDROID__)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#else
#include <EGL/egl.h>
#include <dlfcn.h>
#include <android/log.h>
#endif

#include "gl_command_buffer.h"

// The anisotropy enums live in glext.h on desktop GL; this fallback keeps exotic platform
// headers (Android NDK configurations without the EXT declaration) compiling — the value is
// the registered 0x84FE on every API.
#if !defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#if !defined(GL_PROGRAM_BINARY_RETRIEVABLE_HINT)
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
#include "gl_error_check.h"
#include "gl_render_device_state.h"
#include "engine_assert.h"
#include "logging_macros.h"

namespace UVE::Render {

namespace {

/// GL binding target of an RHI texture type, and the glGet query that reads that target's
/// current binding (shared by creation, mip generation and blits).
GLenum OperationTextureTarget(TextureType type) {
    if (type == TextureType::Cubemap)
        return GL_TEXTURE_CUBE_MAP;
    if (type == TextureType::Texture2DArray)
        return GL_TEXTURE_2D_ARRAY;
    if (type == TextureType::Texture3D)
        return GL_TEXTURE_3D;
    return GL_TEXTURE_2D;
}
GLenum TextureBindingQueryForTarget(GLenum target) {
    switch (target) {
        case GL_TEXTURE_CUBE_MAP:
            return GL_TEXTURE_BINDING_CUBE_MAP;
        case GL_TEXTURE_2D_ARRAY:
            return GL_TEXTURE_BINDING_2D_ARRAY;
        case GL_TEXTURE_3D:
            return GL_TEXTURE_BINDING_3D;
        default:
            return GL_TEXTURE_BINDING_2D;
    }
}

#if defined(__ANDROID__)
constexpr char kAndroidGlLogTag[] = "UVEAndroidGLES";

void LogAndroidGlFailure(const char* operation, const char* detail) noexcept {
    __android_log_print(ANDROID_LOG_ERROR, kAndroidGlLogTag, "%s: %s", operation, detail);
}

void* EglProcAddressBridge(const char* name) {
    void* address = reinterpret_cast<void*>(eglGetProcAddress(name));
    if (address == nullptr) {
        // Some Android GLES drivers export core entry points from libGLESv3.so but do not
        // return every core symbol through eglGetProcAddress. Resolve that second legal path
        // before declaring the backend unusable and falling back to Null.
        address = dlsym(RTLD_DEFAULT, name);
    }
    return address;
}
#else
// Bridges GLFW's proc-address lookup (GLFWglproc, a void(*)(void)) to gl_functions_uve.h's
// injected `void* (*)(const char*)` shape, so the loader itself never needs to include GLFW.
void* GlfwProcAddressBridge(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}
#endif

[[nodiscard]] GLenum BufferUsageToGlTarget(BufferUsage usage) noexcept {
    switch (usage) {
        case BufferUsage::Vertex:
            return GL_ARRAY_BUFFER;
        case BufferUsage::Index:
            return GL_ELEMENT_ARRAY_BUFFER;
        case BufferUsage::Uniform:
            return GL_UNIFORM_BUFFER;
        case BufferUsage::Storage:
        // CS7: an IndirectStorage buffer's HOME target is the SSBO one - that is how it is
        // created, updated and read back, and how the compute kernel that fills it binds it.
        // GL_DRAW_INDIRECT_BUFFER is a transient bind made at draw time by
        // DrawIndexedIndirect, not the buffer's resting place; GL buffer objects are
        // untyped, so the same name binds legally to both targets.
        case BufferUsage::IndirectStorage:
#if !defined(__ANDROID__)
            return GL_SHADER_STORAGE_BUFFER; // M2f: desktop GL 4.3+ (gated at bind time)
#else
            return 0U; // the fixed ES 3.0 baseline has no SSBO target; creation fails loudly
#endif
    }
    return GL_ARRAY_BUFFER;
}

[[nodiscard]] GLenum BufferTargetToBindingQuery(GLenum target) noexcept {
    switch (target) {
        case GL_ARRAY_BUFFER:
            return GL_ARRAY_BUFFER_BINDING;
        case GL_ELEMENT_ARRAY_BUFFER:
            return GL_ELEMENT_ARRAY_BUFFER_BINDING;
        case GL_UNIFORM_BUFFER:
            return GL_UNIFORM_BUFFER_BINDING;
#if !defined(__ANDROID__)
        case GL_SHADER_STORAGE_BUFFER:
            return GL_SHADER_STORAGE_BUFFER_BINDING;
#endif
        default:
            return 0U;
    }
}

// Use the Khronos-assigned tokens directly so the same mapping compiles against desktop GL
// headers and the smaller Android GLES extension headers, which do not declare desktop-only
// BC1/BC3/BC7 tokens even though the runtime format list can advertise extensions for them.
constexpr GLenum kGlNumCompressedTextureFormats = 0x86A2;
constexpr GLenum kGlCompressedTextureFormats = 0x86A3;
constexpr GLint kGlCompressedRgbS3tcDxt1 = 0x83F0;
constexpr GLint kGlCompressedSrgbS3tcDxt1 = 0x8C4C;
constexpr GLint kGlCompressedRgbaS3tcDxt5 = 0x83F3;
constexpr GLint kGlCompressedSrgbAlphaS3tcDxt5 = 0x8C4F;
constexpr GLint kGlCompressedRgbaBptcUnorm = 0x8E8C;
constexpr GLint kGlCompressedSrgbAlphaBptcUnorm = 0x8E8D;
constexpr GLint kGlCompressedRgb8Etc2 = 0x9274;
constexpr GLint kGlCompressedSrgb8Etc2 = 0x9275;
constexpr GLint kGlCompressedRgba8Etc2Eac = 0x9278;
constexpr GLint kGlCompressedSrgb8Alpha8Etc2Eac = 0x9279;
constexpr GLint kGlCompressedRgbaAstc4x4 = 0x93B0;
constexpr GLint kGlCompressedSrgb8Alpha8Astc4x4 = 0x93D0;

struct GlTextureFormat {
    GLint internalFormat;
    GLenum format;
    GLenum type;
};

/// glBufferData's usage token is exactly the (frequency x access) product of the two RHI hints:
/// `Readback` is the one GPU-written/CPU-read case (*_READ); every other placement is CPU-written
/// (*_DRAW). `Auto` with the default `Static` frequency is GL_STATIC_DRAW — the token every
/// buffer used before the hints existed — so unchanged callers allocate exactly as before.
[[nodiscard]] GLenum BufferHintsToGlUsage(const BufferMemory memory,
                                             const BufferUpdateFrequency frequency) noexcept {
    const bool read = memory == BufferMemory::Readback;
    switch (frequency) {
        case BufferUpdateFrequency::Static:
            return read ? GL_STATIC_READ : GL_STATIC_DRAW;
        case BufferUpdateFrequency::Dynamic:
            return read ? GL_DYNAMIC_READ : GL_DYNAMIC_DRAW;
        case BufferUpdateFrequency::Stream:
            return read ? GL_STREAM_READ : GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

[[nodiscard]] GlTextureFormat TextureFormatToGl(TextureFormat format,
                                                        TextureColorSpace colorSpace) noexcept {
    switch (format) {
        case TextureFormat::RGBA8Unorm:
            return {colorSpace == TextureColorSpace::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                    GL_RGBA, GL_UNSIGNED_BYTE};
        case TextureFormat::RGBA16Float:
            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case TextureFormat::R8Unorm:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case TextureFormat::RG8Unorm:
            return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
        case TextureFormat::R16Float:
            return {GL_R16F, GL_RED, GL_HALF_FLOAT};
        case TextureFormat::RG16Float:
            return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
        case TextureFormat::R32Float:
            return {GL_R32F, GL_RED, GL_FLOAT};
        case TextureFormat::RG32Float:
            return {GL_RG32F, GL_RG, GL_FLOAT};
        case TextureFormat::RGBA32Float:
            return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
        case TextureFormat::Depth32Float:
            return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
        case TextureFormat::Depth24Stencil8:
            return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
        case TextureFormat::BC1RGB:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgbS3tcDxt1
                                                              : kGlCompressedRgbS3tcDxt1,
                    0U, 0U};
        case TextureFormat::BC3RGBA:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgbAlphaS3tcDxt5
                                                              : kGlCompressedRgbaS3tcDxt5,
                    0U, 0U};
        case TextureFormat::BC7RGBA:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgbAlphaBptcUnorm
                                                              : kGlCompressedRgbaBptcUnorm,
                    0U, 0U};
        case TextureFormat::ETC2RGB8:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgb8Etc2
                                                              : kGlCompressedRgb8Etc2,
                    0U, 0U};
        case TextureFormat::ETC2RGBA8:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgb8Alpha8Etc2Eac
                                                              : kGlCompressedRgba8Etc2Eac,
                    0U, 0U};
        case TextureFormat::ASTC4x4RGBA:
            return {colorSpace == TextureColorSpace::Srgb ? kGlCompressedSrgb8Alpha8Astc4x4
                                                              : kGlCompressedRgbaAstc4x4,
                    0U, 0U};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

[[nodiscard]] GLenum ShaderStageToGl(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:
            return GL_VERTEX_SHADER;
        case ShaderStage::Fragment:
            return GL_FRAGMENT_SHADER;
        case ShaderStage::Compute:
        case ShaderStage::Geometry:
#if !defined(__ANDROID__)
            return stage == ShaderStage::Compute ? GL_COMPUTE_SHADER : GL_GEOMETRY_SHADER;
#else
            return 0U;
#endif
    }
    return GL_VERTEX_SHADER;
}

[[nodiscard]] bool IsSamplerUniformType(GLenum glType) noexcept {
    switch (glType) {
        case GL_IMAGE_2D: // M5b: image uniforms carry a unit index exactly like samplers do
        case GL_SAMPLER_2D:
        case GL_SAMPLER_3D:
        case GL_SAMPLER_CUBE:
        case GL_SAMPLER_2D_SHADOW:
        case GL_SAMPLER_2D_ARRAY:
        case GL_SAMPLER_2D_ARRAY_SHADOW:
        case GL_SAMPLER_CUBE_SHADOW:
        case GL_INT_SAMPLER_2D:
        case GL_INT_SAMPLER_3D:
        case GL_INT_SAMPLER_CUBE:
        case GL_INT_SAMPLER_2D_ARRAY:
        case GL_UNSIGNED_INT_SAMPLER_2D:
        case GL_UNSIGNED_INT_SAMPLER_3D:
        case GL_UNSIGNED_INT_SAMPLER_CUBE:
        case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
#if defined(GL_SAMPLER_2D_MULTISAMPLE)
        case GL_SAMPLER_2D_MULTISAMPLE:
#endif
#if defined(GL_SAMPLER_2D_MULTISAMPLE_ARRAY)
        case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
#endif
#if defined(GL_INT_SAMPLER_2D_MULTISAMPLE)
        case GL_INT_SAMPLER_2D_MULTISAMPLE:
#endif
#if defined(GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY)
        case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
#endif
#if defined(GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE)
        case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
#endif
#if defined(GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY)
        case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
#endif
            return true;
        default:
            return false;
    }
}

/// Maps a GLenum uniform type (as reported by glGetActiveUniform) to the engine-native
/// ShaderDataType. Supported sampler types report as Int, since a sampler uniform receives a
/// texture-unit index through glUniform1i regardless of its concrete sampler type. Unsupported
/// shapes remain explicit instead of being silently collapsed into Int, preventing invalid setter
/// calls such as glUniform1i on uint or vector uniforms.
[[nodiscard]] ShaderDataType GlUniformTypeToShaderDataType(GLenum glType) noexcept {
    if (IsSamplerUniformType(glType)) {
        return ShaderDataType::Int;
    }
    switch (glType) {
        case GL_FLOAT:
            return ShaderDataType::Float;
        case GL_FLOAT_VEC2:
            return ShaderDataType::Vec2;
        case GL_FLOAT_VEC3:
            return ShaderDataType::Vec3;
        case GL_FLOAT_VEC4:
            return ShaderDataType::Vec4;
        case GL_FLOAT_MAT3:
            return ShaderDataType::Mat3;
        case GL_FLOAT_MAT4:
            return ShaderDataType::Mat4;
        case GL_BOOL:
            return ShaderDataType::Bool;
        case GL_INT:
            return ShaderDataType::Int;
        default:
            return ShaderDataType::Unsupported;
    }
}

/// Reflects every active uniform in `glProgram` (already linked) into `outUniforms`, keyed by
/// name with a trailing "[0]" (glGetActiveUniform's own convention for the first element of an
/// array uniform) stripped so callers can address an array uniform by its bare declared name.
void ReflectPipelineUniforms(
    const Detail::GlFunctions& gl, GLuint glProgram,
    std::unordered_map<std::string, Detail::GlDeviceState::PipelineRecord::UniformRecord,
                       Detail::TransparentStringHash, Detail::TransparentStringEqual>& outUniforms,
    decltype(outUniforms) outElements,
    bool& outHasImageUniforms) {
    outUniforms.clear();
    outElements.clear();
    outHasImageUniforms = false;

    GLint activeUniformCount = 0;
    gl.glGetProgramiv(glProgram, GL_ACTIVE_UNIFORMS, &activeUniformCount);
    GLint maxNameLength = 0;
    gl.glGetProgramiv(glProgram, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLength);
    if (activeUniformCount <= 0 || maxNameLength <= 0) {
        return;
    }

    std::string nameBuffer(static_cast<std::size_t>(maxNameLength), '\0');
    for (GLint index = 0; index < activeUniformCount; ++index) {
        GLsizei writtenLength = 0;
        GLint arraySize = 0;
        GLenum glType = 0;
        gl.glGetActiveUniform(glProgram, static_cast<GLuint>(index), maxNameLength, &writtenLength, &arraySize,
                               &glType, nameBuffer.data());
        std::string name(nameBuffer.data(), static_cast<std::size_t>(writtenLength));
        const bool isArray = name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0;
        if (isArray) {
            name.resize(name.size() - 3);
        }

        const GLint location = gl.glGetUniformLocation(glProgram, name.c_str());
        const bool isImageUniform = glType == GL_IMAGE_2D;
        if (isImageUniform) {
            outHasImageUniforms = true;
        }
        const Detail::GlDeviceState::PipelineRecord::UniformRecord record{
            GlUniformTypeToShaderDataType(glType), location,
            static_cast<std::uint32_t>(arraySize), isImageUniform};
        outUniforms.emplace(name, record);
        if (isArray && location >= 0) {
            for (GLint element = 0; element < arraySize; ++element) {
                const std::string elementName = name + "[" + std::to_string(element) + "]";
                const GLint elementLocation = gl.glGetUniformLocation(glProgram, elementName.c_str());
                if (elementLocation < 0) continue;
                auto elementRecord = record;
                elementRecord.location = elementLocation;
                elementRecord.arraySize = 1U;
                outElements.emplace(elementName, elementRecord);
            }
        }
    }
}

[[nodiscard]] std::string GetShaderInfoLog(const Detail::GlFunctions& gl, GLuint shader) {
    GLint logLength = 0;
    gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 0) {
        return {};
    }
    std::string log(static_cast<std::size_t>(logLength), '\0');
    gl.glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    return log;
}

[[nodiscard]] std::string GetProgramInfoLog(const Detail::GlFunctions& gl, GLuint program) {
    GLint logLength = 0;
    gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 0) {
        return {};
    }
    std::string log(static_cast<std::size_t>(logLength), '\0');
    gl.glGetProgramInfoLog(program, logLength, nullptr, log.data());
    return log;
}

} // namespace

struct GlRenderDevice::Impl {
    Detail::GlDeviceState state;
};

GlRenderDevice::GlRenderDevice(Window::IWindowManager& windowManager)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->state.windowManager = &windowManager;
    if (!windowManager.IsValid()) {
        UVE_WARNING("GlRenderDevice: window manager has no usable graphics context; backend disabled");
        return;
    }
#if defined(__ANDROID__)
    m_impl->state.gl = Detail::LoadGlFunctions(&EglProcAddressBridge);
#else
    m_impl->state.gl = Detail::LoadGlFunctions(&GlfwProcAddressBridge);
#endif

    if (!m_impl->state.gl.IsComplete()) {
        UVE_WARNING("GlRenderDevice: required GL function pointers are unavailable; backend disabled");
    } else {
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_impl->state.maxCombinedTextureImageUnits);
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &m_impl->state.maxUniformBufferBindings);
        // Tier 2.2 anisotropy probe: GL_EXT_texture_filter_anisotropic is an EXTENSION on every
        // API (never core), so support is a string scan, not a version check. glGetStringi is
        // core since GL 3.0 — the desktop baseline — and equally present on GLES 3.0.
        if (m_impl->state.gl.glGetStringi != nullptr) {
            GLint extensionCount = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
            for (GLint extensionIndex = 0; extensionIndex < extensionCount; ++extensionIndex) {
                const char* const extensionName = reinterpret_cast<const char*>(
                    m_impl->state.gl.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(extensionIndex)));
                if (extensionName == nullptr) {
                    continue;
                }
                if (std::strcmp(extensionName, "GL_EXT_texture_filter_anisotropic") == 0) {
                    m_impl->state.samplerAnisotropySupported = true;
                } else if (std::strcmp(extensionName, "GL_EXT_color_buffer_float") == 0 ||
                           std::strcmp(extensionName, "GL_EXT_color_buffer_half_float") == 0) {
                    m_impl->state.halfFloatColorTargetsSupported = true;
                }
            }
            if (m_impl->state.samplerAnisotropySupported) {
                GLfloat maxAnisotropy = 1.0F;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
                if (maxAnisotropy >= 1.0F) {
                    m_impl->state.maxSamplerAnisotropy = maxAnisotropy;
                }
            }
        }
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &m_impl->state.maxVertexAttribs);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &m_impl->state.maxArrayTextureLayers);
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &m_impl->state.maxCubeMapTextureSize);
        glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &m_impl->state.max3DTextureSize);
        GLint compressedFormatCount = 0;
        glGetIntegerv(kGlNumCompressedTextureFormats, &compressedFormatCount);
        if (compressedFormatCount > 0 && compressedFormatCount <= 4096) {
            m_impl->state.compressedTextureFormats.resize(static_cast<std::size_t>(compressedFormatCount));
            glGetIntegerv(kGlCompressedTextureFormats, m_impl->state.compressedTextureFormats.data());
        }
#if !defined(__ANDROID__)
        // Desktop OpenGL 3.3+ guarantees RGBA16F color-renderable FBO attachments in core.
        m_impl->state.halfFloatColorTargetsSupported = true;
        // GL_MAJOR_VERSION/GL_MINOR_VERSION are valid integer queries from GL 3.0 onward, which
        // this engine's desktop baseline already requires - safe to call unconditionally here.
        GLint contextMajorVersion = 0;
        GLint contextMinorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &contextMajorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &contextMinorVersion);
        m_impl->state.supportsComputeShaders =
            contextMajorVersion > 4 || (contextMajorVersion == 4 && contextMinorVersion >= 3);
        if (m_impl->state.supportsComputeShaders) {
            // M2f: SSBO binding points share compute's GL 4.3 floor; queried once here so
            // BindStorageBuffer validates slots against the real driver limit.
            glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,
                          &m_impl->state.maxShaderStorageBindings);
        }
#endif
        m_impl->state.cachedCapabilities = GetCapabilities();
        m_impl->state.capabilitiesCached = true;
        Detail::RegisterGlDebugCallback(m_impl->state.gl);
        UVE_INFO("GlRenderDevice: initialized, backend GL_VERSION={}",
                  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    }
}

GlRenderDevice::~GlRenderDevice() {
    if (!IsUsable()) {
        return;
    }
    CollectRetiredTextures();
    if (m_impl->state.activeOcclusion != 0U)
        m_impl->state.gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
    for (const auto &[id, query] : m_impl->state.queries) {
        static_cast<void>(id);
        m_impl->state.gl.glDeleteQueries(1, &query.native);
    }
    for (const auto &[id, fence] : m_impl->state.fences) {
        static_cast<void>(id);
        m_impl->state.gl.glDeleteSync(fence);
    }
    for (const auto& [key, framebuffer] : m_impl->state.framebufferCache) {
        static_cast<void>(key);
        m_impl->state.gl.glDeleteFramebuffers(1, &framebuffer);
    }
    m_impl->state.framebufferCache.clear();
    if (m_impl->state.blitReadFbo != 0U) {
        m_impl->state.gl.glDeleteFramebuffers(1, &m_impl->state.blitReadFbo);
        m_impl->state.blitReadFbo = 0U;
    }
    if (m_impl->state.blitDrawFbo != 0U) {
        m_impl->state.gl.glDeleteFramebuffers(1, &m_impl->state.blitDrawFbo);
        m_impl->state.blitDrawFbo = 0U;
    }

    // Release every device-owned GL object while the context is still owned by the window manager.
    // Relying on context teardown leaks resources when a renderer is recreated within one context.
    for (const auto& [handle, record] : m_impl->state.pipelines) {
        static_cast<void>(handle);
        m_impl->state.gl.glDeleteProgram(record.glProgram);
        m_impl->state.gl.glDeleteVertexArrays(1, &record.glVao);
    }
    m_impl->state.pipelines.clear();
    for (const auto& [handle, record] : m_impl->state.shaders) {
        static_cast<void>(handle);
        m_impl->state.gl.glDeleteShader(record.glShader);
    }
    m_impl->state.shaders.clear();
    for (const auto& [handle, record] : m_impl->state.buffers) {
        static_cast<void>(handle);
        m_impl->state.gl.glDeleteBuffers(1, &record.glBuffer);
    }
    m_impl->state.buffers.clear();
    for (const auto& [handle, record] : m_impl->state.textures) {
        static_cast<void>(handle);
        glDeleteTextures(1, &record.glTexture);
    }
    m_impl->state.textures.clear();
    for (const auto& [handle, record] : m_impl->state.samplers) {
        static_cast<void>(handle);
        // glDeleteSamplers is non-null whenever this map is non-empty: CreateSampler fails
        // closed without the entry points, so a sampler record implies a loaded deleter.
        m_impl->state.gl.glDeleteSamplers(1, &record.glSampler);
    }
    m_impl->state.samplers.clear();
}

BufferHandle GlRenderDevice::CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initialData) {
    if (!ValidateBufferUpload(desc, initialData)) {
        UVE_ERROR("GlRenderDevice: CreateBuffer initial data exceeds buffer size");
        return kInvalidBufferHandle;
    }
    if (!IsBufferDescValid(desc)) {
        UVE_ERROR("GlRenderDevice: CreateBuffer received an unknown buffer usage, memory or "
                  "update-frequency value");
        return kInvalidBufferHandle;
    }
    if (IsStorageBindableUsage(desc.usage) && !m_impl->state.supportsComputeShaders) {
        // SSBOs are desktop GL 4.3 core (the same floor as compute shaders; the fixed ES 3.0
        // Android baseline has neither). Fail before touching GL_SHADER_STORAGE_BUFFER - on an
        // older context the enum itself would raise GL_INVALID_ENUM at glBufferData time.
        UVE_ERROR("GlRenderDevice: CreateBuffer(Storage) needs a desktop GL 4.3+ context "
                  "(shader storage buffers); this context does not offer them");
        return kInvalidBufferHandle;
    }
    if (desc.sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max())) {
        UVE_ERROR("GlRenderDevice: CreateBuffer size exceeds the GLsizeiptr range");
        return kInvalidBufferHandle;
    }
    const GLenum target = BufferUsageToGlTarget(desc.usage);
    const GLenum bindingQuery = BufferTargetToBindingQuery(target);
    if (bindingQuery == 0U) {
        UVE_ERROR("GlRenderDevice: CreateBuffer resolved an unsupported GL buffer target");
        return kInvalidBufferHandle;
    }
    // Resource creation temporarily binds the object. Preserve the binding because a live
    // command buffer caches vertex/index handles and may otherwise skip its next required rebind.
    GLint previousBufferBinding = 0;
    glGetIntegerv(bindingQuery, &previousBufferBinding);

    GLuint glBuffer = 0;
    m_impl->state.gl.glGenBuffers(1, &glBuffer);
    m_impl->state.gl.glBindBuffer(target, glBuffer);
    m_impl->state.gl.glBufferData(target, static_cast<GLsizeiptr>(desc.sizeBytes),
                                    initialData.empty() ? nullptr : initialData.data(),
                                    BufferHintsToGlUsage(desc.memory, desc.updateFrequency));
    UVE_GL_CHECK_ERROR_UVE("CreateBuffer");
    m_impl->state.gl.glBindBuffer(target, static_cast<GLuint>(previousBufferBinding));

    const std::uint32_t handleValue = m_impl->state.nextBufferHandle++;
    m_impl->state.buffers.emplace(handleValue,
                                    Detail::GlDeviceState::BufferRecord{glBuffer, target, desc.sizeBytes, desc.usage,
                                                                               desc.memory, desc.updateFrequency});
    return BufferHandle{handleValue};
}

void GlRenderDevice::DestroyBuffer(BufferHandle buffer) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDevice: DestroyBuffer called with an unknown or already-destroyed handle ({})",
                   buffer.value);
        return;
    }
    m_impl->state.gl.glDeleteBuffers(1, &it->second.glBuffer);
    m_impl->state.buffers.erase(it);
}

bool GlRenderDevice::UpdateBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                          std::uint64_t offsetBytes) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDevice: UpdateBuffer called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdate(it->second.sizeBytes, data.size(), offsetBytes)) {
        UVE_ERROR("GlRenderDevice: UpdateBuffer write of {} bytes at offset {} exceeds buffer size {}",
                   data.size(), offsetBytes, it->second.sizeBytes);
        return false;
    }
    // Upload via GL_COPY_WRITE_BUFFER (core in GL 3.1+ and GLES 3.0) so we never disturb VAO
    // vertex/index bindings and avoid a synchronous glGetIntegerv stall on every dynamic upload.
    m_impl->state.gl.glBindBuffer(GL_COPY_WRITE_BUFFER, it->second.glBuffer);
    m_impl->state.gl.glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offsetBytes),
                                     static_cast<GLsizeiptr>(data.size()), data.data());
    UVE_GL_CHECK_ERROR_UVE("UpdateBuffer");
    m_impl->state.gl.glBindBuffer(GL_COPY_WRITE_BUFFER, 0U);
    return true;
}

bool GlRenderDevice::ReadbackBuffer(BufferHandle buffer, std::span<std::byte> outData,
                                            std::uint64_t offsetBytes) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDevice: ReadbackBuffer called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdate(it->second.sizeBytes, outData.size(), offsetBytes)) {
        UVE_ERROR("GlRenderDevice: ReadbackBuffer read of {} bytes at offset {} exceeds buffer size {}",
                   outData.size(), offsetBytes, it->second.sizeBytes);
        return false;
    }
    if (outData.empty()) {
        return true;
    }
    const GLenum bindingQuery = BufferTargetToBindingQuery(it->second.target);
    if (bindingQuery == 0U) {
        UVE_ERROR("GlRenderDevice: ReadbackBuffer resolved an unsupported GL buffer target");
        return false;
    }
    // Cold-path synchronization, exactly what the interface promises. This backend executes its
    // commands at record time, but a compute dispatch's SSBO writes become visible to a
    // client-side read only after GL_BUFFER_UPDATE_BARRIER_BIT (writes seen by buffer queries)
    // and GL_SHADER_STORAGE_BARRIER_BIT (the SSBO writes themselves). glGetBufferSubData below
    // is then a blocking client read, which is itself a synchronization point - no glFinish is
    // needed on top of it, and issuing one would only widen the stall beyond this buffer.
    if (m_impl->state.supportsComputeShaders && m_impl->state.gl.glMemoryBarrier != nullptr) {
        m_impl->state.gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
#if defined(__ANDROID__)
    std::vector<std::byte> candidate(outData.size());
#endif
    GLint previousBufferBinding = 0;
    glGetIntegerv(bindingQuery, &previousBufferBinding);
    m_impl->state.gl.glBindBuffer(it->second.target, it->second.glBuffer);
#if defined(__ANDROID__)
    const void* mapped = glMapBufferRange(it->second.target, static_cast<GLintptr>(offsetBytes),
                                        static_cast<GLsizeiptr>(outData.size()), GL_MAP_READ_BIT);
    if (mapped == nullptr) {
        m_impl->state.gl.glBindBuffer(it->second.target, static_cast<GLuint>(previousBufferBinding));
        return false;
    }
    std::memcpy(candidate.data(), mapped, outData.size());
    const bool unmapped = glUnmapBuffer(it->second.target) == GL_TRUE;
    m_impl->state.gl.glBindBuffer(it->second.target, static_cast<GLuint>(previousBufferBinding));
    if (unmapped) std::copy(candidate.begin(), candidate.end(), outData.begin());
    return unmapped;
#else
    m_impl->state.gl.glGetBufferSubData(it->second.target, static_cast<GLintptr>(offsetBytes),
                                          static_cast<GLsizeiptr>(outData.size()), outData.data());
    UVE_GL_CHECK_ERROR_UVE("ReadbackBuffer");
    m_impl->state.gl.glBindBuffer(it->second.target, static_cast<GLuint>(previousBufferBinding));
    return true;
#endif
}

TextureHandle GlRenderDevice::CreateTexture(const TextureDesc& desc,
                                                       std::span<const std::byte> initialData) {
    if (!ValidateTextureUpload(desc, initialData)) {
        UVE_ERROR("GlRenderDevice: CreateTexture received an invalid descriptor or initial upload");
        return kInvalidTextureHandle;
    }
    if (!SupportsTextureFormat(desc.format, desc.colorSpace)) {
        UVE_ERROR("GlRenderDevice: requested texture format or color space is unsupported by this context");
        return kInvalidTextureHandle;
    }
    if (desc.width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        desc.height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlRenderDevice: CreateTexture dimensions exceed the GLsizei range");
        return kInvalidTextureHandle;
    }
    std::array<GLsizei, 32U> mipImageSizes{};
    TextureMipExtent mipExtent{desc.width, desc.height};
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        std::uint64_t levelBytes = 0U;
        if (!CalculateTextureMipByteCount(desc.format, mipExtent.width, mipExtent.height, levelBytes) ||
            levelBytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizei>::max())) {
            UVE_ERROR("GlRenderDevice: texture mip upload exceeds the GLsizei image-size range");
            return kInvalidTextureHandle;
        }
        mipImageSizes[level] = static_cast<GLsizei>(levelBytes);
        mipExtent.width = mipExtent.width > 1U ? mipExtent.width / 2U : 1U;
        mipExtent.height = mipExtent.height > 1U ? mipExtent.height / 2U : 1U;
    }

    const GlTextureFormat glFormat = TextureFormatToGl(desc.format, desc.colorSpace);
    // Texture creation temporarily binds the object on the current active unit. Preserve both
    // pieces of caller/command-buffer state so a live GlCommandBuffer texture cache cannot
    // falsely skip a later rebind after a resource is created between draws.
    // Tier 2.3: dimensional targets. Cubes upload face-by-face in CubemapFace order
    // (POSITIVE_X + face); arrays upload whole levels, all layers contiguous.
    const bool isCube = desc.type == TextureType::Cubemap;
    const bool isArray = desc.type == TextureType::Texture2DArray;
    const bool isVolume = desc.type == TextureType::Texture3D;
    const GLenum textureTarget = OperationTextureTarget(desc.type);
    const GLenum bindingQuery = TextureBindingQueryForTarget(textureTarget);
    if (isArray || isCube || isVolume) {
        const GLint maxLayers = m_impl->state.maxArrayTextureLayers;
        const GLint maxCubeSize = m_impl->state.maxCubeMapTextureSize;
        const GLint max3DSize = m_impl->state.max3DTextureSize;
        if ((isArray && desc.arrayLayers > static_cast<std::uint32_t>(maxLayers)) ||
            (isCube && desc.width > static_cast<std::uint32_t>(maxCubeSize)) ||
            (isVolume && (desc.width > static_cast<std::uint32_t>(max3DSize) ||
                          desc.height > static_cast<std::uint32_t>(max3DSize) ||
                          desc.depth > static_cast<std::uint32_t>(max3DSize)))) {
            UVE_ERROR("GlRenderDevice: CreateTexture array/cubemap/volume exceeds GL limits");
            return kInvalidTextureHandle;
        }
        if ((isArray || isVolume) && m_impl->state.gl.glTexImage3D == nullptr &&
            !IsTextureFormatCompressed(desc.format)) {
            UVE_ERROR("GlRenderDevice: CreateTexture needs glTexImage3D for arrays and volumes");
            return kInvalidTextureHandle;
        }
        if ((isArray || isVolume) && m_impl->state.gl.glCompressedTexImage3D == nullptr &&
            IsTextureFormatCompressed(desc.format)) {
            UVE_ERROR("GlRenderDevice: CreateTexture needs glCompressedTexImage3D");
            return kInvalidTextureHandle;
        }
    }

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(bindingQuery, &previousTextureBinding);

    GLuint glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(textureTarget, glTexture);
    // The upload contract is tightly packed rows. GL's default unpack alignment of 4 would
    // misread one- and two-byte texel rows whose width is not a multiple of 4 (a 3-wide
    // R8Unorm level is 3 bytes per row), so align to the byte for the upload and restore after.
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::size_t dataOffset = 0U;
    std::uint32_t levelWidth = desc.width;
    std::uint32_t levelHeight = desc.height;
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        const std::byte* const levelData =
            initialData.empty() ? nullptr : initialData.data() + dataOffset;
        if (isCube) {
            // Cube faces upload consecutively per level in CubemapFace order.
            for (std::uint32_t face = 0U; face < 6U; ++face) {
                const std::byte* faceData = nullptr;
                if (levelData != nullptr) {
                    faceData =
                        levelData + static_cast<std::size_t>(mipImageSizes[level]) * face;
                }
                const GLenum faceTarget =
                    static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
                if (IsTextureFormatCompressed(desc.format)) {
                    m_impl->state.gl.glCompressedTexImage2D(
                        faceTarget, static_cast<GLint>(level),
                        static_cast<GLenum>(glFormat.internalFormat),
                        static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight), 0,
                        mipImageSizes[level], faceData);
                } else {
                    glTexImage2D(faceTarget, static_cast<GLint>(level), glFormat.internalFormat,
                                 static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight),
                                 0, glFormat.format, glFormat.type, faceData);
                }
            }
            dataOffset += static_cast<std::size_t>(mipImageSizes[level]) * 6U;
        } else if (isArray || isVolume) {
            // Whole-level upload: all layers (or, for a volume, all slices of this level's
            // depth) contiguous — level-major, per the RHI contract. The size_t intermediate
            // keeps huge levels out of signed-overflow UB; the driver reports oversized
            // allocations through the standard GL error path below.
            const std::uint32_t levelSlices = isVolume ? GetTextureMipDepth(desc.depth, level) : desc.arrayLayers;
            const GLsizei levelBytesAllSlices = static_cast<GLsizei>(
                static_cast<std::size_t>(mipImageSizes[level]) * levelSlices);
            if (IsTextureFormatCompressed(desc.format)) {
                m_impl->state.gl.glCompressedTexImage3D(
                    textureTarget, static_cast<GLint>(level),
                    static_cast<GLenum>(glFormat.internalFormat), static_cast<GLsizei>(levelWidth),
                    static_cast<GLsizei>(levelHeight), static_cast<GLsizei>(levelSlices), 0,
                    levelBytesAllSlices, levelData);
            } else {
                m_impl->state.gl.glTexImage3D(
                    textureTarget, static_cast<GLint>(level), glFormat.internalFormat,
                    static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight),
                    static_cast<GLsizei>(levelSlices), 0, glFormat.format, glFormat.type,
                    levelData);
            }
            dataOffset += static_cast<std::size_t>(mipImageSizes[level]) * levelSlices;
        } else {
            if (IsTextureFormatCompressed(desc.format)) {
                m_impl->state.gl.glCompressedTexImage2D(
                    GL_TEXTURE_2D, static_cast<GLint>(level),
                    static_cast<GLenum>(glFormat.internalFormat), static_cast<GLsizei>(levelWidth),
                    static_cast<GLsizei>(levelHeight), 0, mipImageSizes[level], levelData);
            } else {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), glFormat.internalFormat,
                             static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight), 0,
                             glFormat.format, glFormat.type, levelData);
            }
            dataOffset += static_cast<std::size_t>(mipImageSizes[level]);
        }
        levelWidth = levelWidth > 1U ? levelWidth / 2U : 1U;
        levelHeight = levelHeight > 1U ? levelHeight / 2U : 1U;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    const bool isDepthTexture =
        desc.format == TextureFormat::Depth32Float || desc.format == TextureFormat::Depth24Stencil8;
    const GLint defaultMinFilter =
        isDepthTexture ? GL_NEAREST : (desc.mipLevels > 1U ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    const GLint defaultMagFilter = isDepthTexture ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(textureTarget, GL_TEXTURE_MIN_FILTER, defaultMinFilter);
    glTexParameteri(textureTarget, GL_TEXTURE_MAG_FILTER, defaultMagFilter);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (isArray || isVolume) {
        glTexParameteri(textureTarget, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(desc.mipLevels - 1U));
    UVE_GL_CHECK_ERROR_UVE("CreateTexture mip allocation/upload and sampler state");

    m_impl->state.gl.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindTexture(textureTarget, static_cast<GLuint>(previousTextureBinding));

    const std::uint32_t handleValue = m_impl->state.nextTextureHandle++;
    m_impl->state.textures.emplace(handleValue, Detail::GlDeviceState::TextureRecord{glTexture, desc});
    return TextureHandle{handleValue};
}

bool GlRenderDevice::SupportsTextureFormat(const TextureFormat format,
                                                  const TextureColorSpace colorSpace) const noexcept {
    if (!IsUsable() || GetTextureFormatBlockInfo(format).bytes == 0U) {
        return false;
    }
    switch (colorSpace) {
        case TextureColorSpace::Linear:
            break;
        case TextureColorSpace::Srgb:
            if (!IsTextureFormatSrgbCapable(format)) {
                return false;
            }
            break;
        default:
            return false;
    }
    if (!IsTextureFormatCompressed(format)) {
        return true;
    }
    if (m_impl->state.gl.glCompressedTexImage2D == nullptr) {
        return false;
    }
    const GLint internalFormat = TextureFormatToGl(format, colorSpace).internalFormat;
    return std::find(m_impl->state.compressedTextureFormats.begin(),
                     m_impl->state.compressedTextureFormats.end(), internalFormat) !=
           m_impl->state.compressedTextureFormats.end();
}

void GlRenderDevice::DestroyTexture(TextureHandle texture) {
    const auto it = m_impl->state.textures.find(texture.value);
    if (it == m_impl->state.textures.end()) {
        UVE_ERROR("GlRenderDevice: DestroyTexture called with an unknown or already-destroyed handle ({})",
                   texture.value);
        return;
    }
    glDeleteTextures(1, &it->second.glTexture);
    for (auto framebufferIt = m_impl->state.framebufferCache.begin();
         framebufferIt != m_impl->state.framebufferCache.end();) {
        const std::uint32_t colorHandle = framebufferIt->first.color;
        const std::uint32_t depthHandle = framebufferIt->first.depth;
        // Tier 2.4: extras pin their FBOs too — a destroyed extra must evict the cache entry
        // (unused slots hold 0, which no live handle takes, so they never match).
        bool extraMatches = false;
        for (const std::uint32_t extraHandle : framebufferIt->first.extraColors) {
            if (extraHandle == texture.value) {
                extraMatches = true;
                break;
            }
        }
        if (colorHandle == texture.value || depthHandle == texture.value || extraMatches) {
            m_impl->state.gl.glDeleteFramebuffers(1, &framebufferIt->second);
            framebufferIt = m_impl->state.framebufferCache.erase(framebufferIt);
        } else {
            ++framebufferIt;
        }
    }
    m_impl->state.textures.erase(it);
}

SamplerHandle GlRenderDevice::CreateSampler(const SamplerDesc& desc) {
    if (!IsSamplerDescValid(desc)) {
        UVE_ERROR("GlRenderDevice: CreateSampler received an invalid sampler descriptor");
        return kInvalidSamplerHandle;
    }
    if (m_impl->state.gl.glGenSamplers == nullptr || m_impl->state.gl.glDeleteSamplers == nullptr ||
        m_impl->state.gl.glBindSampler == nullptr || m_impl->state.gl.glSamplerParameteri == nullptr ||
        m_impl->state.gl.glSamplerParameterf == nullptr) {
        UVE_ERROR("GlRenderDevice: CreateSampler needs GL 3.3+ sampler objects, "
                  "unavailable on this context");
        return kInvalidSamplerHandle;
    }
    const auto toWrap = [](const SamplerWrap wrap) noexcept {
        switch (wrap) {
            case SamplerWrap::Repeat:         return GL_REPEAT;
            case SamplerWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
            case SamplerWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
        }
        return GL_CLAMP_TO_EDGE; // unreachable; desc was validated
    };
    // GL's MIN_FILTER fuses minification and mip blending into one enum; the desc keeps them
    // separate (Vulkan's shape), so the fusion happens here.
    GLint minFilter = GL_LINEAR_MIPMAP_LINEAR;
    if (desc.mipMode == SamplerMipMode::None) {
        minFilter = desc.minFilter == SamplerFilter::Point ? GL_NEAREST : GL_LINEAR;
    } else if (desc.minFilter == SamplerFilter::Point) {
        minFilter = desc.mipMode == SamplerMipMode::Point ? GL_NEAREST_MIPMAP_NEAREST
                                                             : GL_NEAREST_MIPMAP_LINEAR;
    } else {
        minFilter = desc.mipMode == SamplerMipMode::Point ? GL_LINEAR_MIPMAP_NEAREST
                                                             : GL_LINEAR_MIPMAP_LINEAR;
    }
    GLuint glSampler = 0;
    m_impl->state.gl.glGenSamplers(1, &glSampler);
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_MIN_FILTER, minFilter);
    m_impl->state.gl.glSamplerParameteri(
        glSampler, GL_TEXTURE_MAG_FILTER,
        desc.magFilter == SamplerFilter::Point ? GL_NEAREST : GL_LINEAR);
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_S, toWrap(desc.wrapU));
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_T, toWrap(desc.wrapV));
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_R, toWrap(desc.wrapW));
    if (desc.maxAnisotropy > 1.0F) {
        if (!m_impl->state.samplerAnisotropySupported) {
            if (!m_impl->state.warnedSamplerAnisotropyClamped) {
                m_impl->state.warnedSamplerAnisotropyClamped = true;
                UVE_WARNING("GlRenderDevice: CreateSampler without GL_EXT_texture_filter_anisotropic; "
                            "anisotropy clamps to 1.0");
            }
        } else {
            m_impl->state.gl.glSamplerParameterf(
                glSampler, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                std::min(desc.maxAnisotropy, m_impl->state.maxSamplerAnisotropy));
        }
    }
    UVE_GL_CHECK_ERROR_UVE("CreateSampler sampler state");

    const std::uint32_t handleValue = m_impl->state.nextSamplerHandle++;
    m_impl->state.samplers.emplace(handleValue,
                                   Detail::GlDeviceState::SamplerRecord{glSampler, desc});
    return SamplerHandle{handleValue};
}

void GlRenderDevice::DestroySampler(SamplerHandle sampler) {
    const auto it = m_impl->state.samplers.find(sampler.value);
    if (it == m_impl->state.samplers.end()) {
        UVE_ERROR("GlRenderDevice: DestroySampler called with an unknown or already-destroyed handle ({})",
                   sampler.value);
        return;
    }
    // A sampler deleted while bound is implicitly unbound from every unit by the driver
    // (core GL 3.3 semantics) — no unit bookkeeping to repair here.
    m_impl->state.gl.glDeleteSamplers(1, &it->second.glSampler);
    m_impl->state.samplers.erase(it);
}

ShaderHandle GlRenderDevice::CreateShader(const ShaderDesc& desc, std::string* outInfoLog) {
    if (!IsShaderStageValid(desc.stage)) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Unknown shader stage.";
        }
        UVE_ERROR("GlRenderDevice: CreateShader received an unknown shader stage");
        return kInvalidShaderHandle;
    }
#if defined(__ANDROID__)
    if (desc.stage == ShaderStage::Compute || desc.stage == ShaderStage::Geometry) {
        if (outInfoLog != nullptr) *outInfoLog = "GLES3 does not support requested compute/geometry shader stage.";
        UVE_ERROR("GlRenderDevice: GLES3 does not support requested compute/geometry shader stage");
        return kInvalidShaderHandle;
    }
#else
    // Compute shaders need GL 4.3+, not merely a requested modern desktop context.
    if (desc.stage == ShaderStage::Compute && !m_impl->state.supportsComputeShaders) {
        UVE_ERROR("GlRenderDevice: compute shaders require an OpenGL 4.3+ context; the negotiated "
                  "context does not support them");
        return kInvalidShaderHandle;
    }
#endif
    if (desc.stage == ShaderStage::Geometry &&
        GetCapabilities().geometryShaders != RenderFeatureSupport::Native) {
        if (outInfoLog != nullptr) *outInfoLog = "Geometry shaders require a usable desktop GL 3.2+ context.";
        UVE_ERROR("GlRenderDevice: geometry shaders are unsupported by this context");
        return kInvalidShaderHandle;
    }
    if (desc.sourceCode.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Shader source exceeds the GLsizei length range.";
        }
        UVE_ERROR("GlRenderDevice: shader source exceeds the GLsizei length range");
        return kInvalidShaderHandle;
    }
    const GLenum stage = ShaderStageToGl(desc.stage);
    const GLuint glShader = m_impl->state.gl.glCreateShader(stage);

    const char* sourcePointer = desc.sourceCode.c_str();
    const auto sourceLength = static_cast<GLint>(desc.sourceCode.size());
    m_impl->state.gl.glShaderSource(glShader, 1, &sourcePointer, &sourceLength);
    m_impl->state.gl.glCompileShader(glShader);

    const std::string infoLog = GetShaderInfoLog(m_impl->state.gl, glShader);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint compiled = GL_FALSE;
    m_impl->state.gl.glGetShaderiv(glShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        UVE_ERROR("GlRenderDevice: CreateShader compilation failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailure("shader compile failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteShader(glShader);
        return kInvalidShaderHandle;
    }

    const std::uint32_t handleValue = m_impl->state.nextShaderHandle++;
    m_impl->state.shaders.emplace(handleValue,
                                  Detail::GlDeviceState::ShaderRecord{glShader, desc.stage});
    return ShaderHandle{handleValue};
}

void GlRenderDevice::DestroyShader(ShaderHandle shader) {
    const auto it = m_impl->state.shaders.find(shader.value);
    if (it == m_impl->state.shaders.end()) {
        UVE_ERROR("GlRenderDevice: DestroyShader called with an unknown or already-destroyed handle ({})",
                   shader.value);
        return;
    }
    m_impl->state.gl.glDeleteShader(it->second.glShader);
    m_impl->state.shaders.erase(it);
}

PipelineHandle GlRenderDevice::CreatePipeline(const PipelineDesc& desc, std::string* outInfoLog) {
    const auto rejectStage = [outInfoLog](const char* message) {
        UVE_ERROR("GlRenderDevice: CreatePipeline {}", message);
        if (outInfoLog != nullptr) *outInfoLog = message;
        return kInvalidPipelineHandle;
    };
    if (desc.geometryShader != kInvalidShaderHandle &&
        GetCapabilities().geometryShaders != RenderFeatureSupport::Native) {
        return rejectStage("geometry shader pipelines are unsupported by this context");
    }
    if (!IsVertexLayoutValid(desc.vertexLayout)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown vertex attribute format");
        return kInvalidPipelineHandle;
    }
    if (m_impl->state.gl.glVertexAttribIPointer == nullptr &&
        std::any_of(desc.vertexLayout.begin(), desc.vertexLayout.end(), [](const VertexAttribute& attribute) {
            return IsVertexAttributeFormatInteger(attribute.format);
        })) {
        UVE_ERROR("GlRenderDevice: CreatePipeline needs glVertexAttribIPointer for an integer vertex attribute "
                  "format, which this context does not provide");
        return kInvalidPipelineHandle;
    }
    if (!IsPipelineBlendModeValid(desc.blendMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown blend mode");
        return kInvalidPipelineHandle;
    }
    if (!IsPrimitiveTopologyValid(desc.topology)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown primitive topology");
        return kInvalidPipelineHandle;
    }
    if (!IsCullModeValid(desc.cullMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown cull mode");
        return kInvalidPipelineHandle;
    }
    if (!IsFrontFaceValid(desc.frontFace)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown front-face winding");
        return kInvalidPipelineHandle;
    }
#if defined(__ANDROID__)
    if (desc.fillMode == FillMode::Wireframe) {
        UVE_WARNING("GlRenderDevice: GLES does not support polygon wireframe mode");
        return kInvalidPipelineHandle;
    }
#endif
    if (!IsFillModeValid(desc.fillMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown fill mode");
        return kInvalidPipelineHandle;
    }
    if (!IsDepthCompareValid(desc.depthCompare)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown depth comparison");
        return kInvalidPipelineHandle;
    }
    if (!IsStencilStateValid(desc.stencil)) {
        UVE_ERROR("GlRenderDevice: CreatePipeline received an unknown stencil state");
        return kInvalidPipelineHandle;
    }
    if (m_impl->state.maxVertexAttribs <= 0 ||
        desc.vertexLayout.size() > static_cast<std::size_t>(m_impl->state.maxVertexAttribs)) {
        UVE_ERROR("GlRenderDevice: pipeline vertex layout exceeds GL vertex-attrib limits");
        return kInvalidPipelineHandle;
    }
    if (!desc.vertexLayout.empty() &&
        (!IsVertexLayoutWithinStride(desc.vertexLayout, desc.vertexStride) || desc.vertexStride == 0U ||
         desc.vertexStride > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))) {
        UVE_ERROR("GlRenderDevice: CreatePipeline vertex attributes exceed the GL stride range");
        return kInvalidPipelineHandle;
    }
    const auto vertexIt = m_impl->state.shaders.find(desc.vertexShader.value);
    const auto fragmentIt = m_impl->state.shaders.find(desc.fragmentShader.value);
    if (vertexIt == m_impl->state.shaders.end() || fragmentIt == m_impl->state.shaders.end()) {
        UVE_ERROR("GlRenderDevice: CreatePipeline referenced an unknown vertex or fragment shader handle");
        return kInvalidPipelineHandle;
    }

    if (vertexIt->second.stage != ShaderStage::Vertex || fragmentIt->second.stage != ShaderStage::Fragment) {
        return rejectStage("vertex/fragment shader handles are bound to the wrong stages");
    }
    const auto geometryIt = m_impl->state.shaders.find(desc.geometryShader.value);
    if (desc.geometryShader != kInvalidShaderHandle) {
        if (geometryIt == m_impl->state.shaders.end()) {
            return rejectStage("geometry shader handle does not reference a live shader");
        }
        if (geometryIt->second.stage != ShaderStage::Geometry) {
            return rejectStage("geometry shader slot requires a Geometry-stage shader");
        }
    }

    const GLuint glProgram = m_impl->state.gl.glCreateProgram();
    m_impl->state.gl.glAttachShader(glProgram, vertexIt->second.glShader);
    m_impl->state.gl.glAttachShader(glProgram, fragmentIt->second.glShader);
    if (geometryIt != m_impl->state.shaders.end()) {
        m_impl->state.gl.glAttachShader(glProgram, geometryIt->second.glShader);
    }
    if (m_impl->state.gl.glProgramParameteri != nullptr &&
        m_impl->state.gl.glGetProgramBinary != nullptr) {
        m_impl->state.gl.glProgramParameteri(glProgram, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }
    m_impl->state.gl.glLinkProgram(glProgram);

    const std::string infoLog = GetProgramInfoLog(m_impl->state.gl, glProgram);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint linked = GL_FALSE;
    m_impl->state.gl.glGetProgramiv(glProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        UVE_ERROR("GlRenderDevice: CreatePipeline link failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailure("program link failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandle;
    }

    GLuint glVao = 0;
    m_impl->state.gl.glGenVertexArrays(1, &glVao);

    Detail::GlDeviceState::PipelineRecord record;
    record.glProgram = glProgram;
    record.glVao = glVao;
    record.vertexLayout = desc.vertexLayout;
    record.vertexStride = desc.vertexStride;
    record.depthTestEnabled = desc.depthTestEnabled;
    record.depthWriteEnabled = desc.depthWriteEnabled;
    record.blendMode = desc.blendMode;
    record.cullMode = desc.cullMode;
    record.frontFace = desc.frontFace;
    record.fillMode = desc.fillMode;
    record.depthBiasEnabled = desc.depthBiasEnabled;
    record.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    record.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    record.depthCompare = desc.depthCompare;
    record.stencil = desc.stencil;
    record.topology = desc.topology;
    ReflectPipelineUniforms(m_impl->state.gl, glProgram, record.uniforms, record.uniformElements,
                               record.hasImageUniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandle{handleValue};
}

PipelineHandle GlRenderDevice::CreateComputePipeline(const ComputePipelineDesc& desc,
                                                              std::string* outInfoLog) {
    const auto shaderIt = m_impl->state.shaders.find(desc.computeShader.value);
    if (shaderIt == m_impl->state.shaders.end()) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Unknown compute shader handle.";
        }
        UVE_ERROR("GlRenderDevice: CreateComputePipeline referenced an unknown shader handle");
        return kInvalidPipelineHandle;
    }
    if (shaderIt->second.stage != ShaderStage::Compute) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Shader handle is not a Compute-stage shader.";
        }
        UVE_ERROR("GlRenderDevice: CreateComputePipeline requires a Compute-stage shader");
        return kInvalidPipelineHandle;
    }
    // CreateShader already gated the stage on supportsComputeShaders, but the dispatch pair
    // is loaded separately — re-check here so a Compute-stage GLSL ES shader can never slip a
    // pre-4.3 context into a link it cannot dispatch.
    if (m_impl->state.gl.glDispatchCompute == nullptr || m_impl->state.gl.glMemoryBarrier == nullptr) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Compute pipelines require an OpenGL 4.3+ context (glDispatchCompute unavailable).";
        }
        UVE_ERROR("GlRenderDevice: CreateComputePipeline requires an OpenGL 4.3+ context");
        return kInvalidPipelineHandle;
    }

    const GLuint glProgram = m_impl->state.gl.glCreateProgram();
    m_impl->state.gl.glAttachShader(glProgram, shaderIt->second.glShader);
    m_impl->state.gl.glLinkProgram(glProgram);

    const std::string infoLog = GetProgramInfoLog(m_impl->state.gl, glProgram);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint linked = GL_FALSE;
    m_impl->state.gl.glGetProgramiv(glProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        UVE_ERROR("GlRenderDevice: CreateComputePipeline link failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailure("compute program link failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandle;
    }

    // No VAO for compute records — glDeleteVertexArrays silently ignores name 0 on destroy, and
    // GlCommandBuffer skips glBindVertexArray() for isCompute pipelines entirely.
    Detail::GlDeviceState::PipelineRecord record;
    record.glProgram = glProgram;
    record.isCompute = true;
    // Active-uniform reflection works identically for compute programs, so SetUniform*UVE on a
    // compute pipeline needs no special-casing.
    ReflectPipelineUniforms(m_impl->state.gl, glProgram, record.uniforms, record.uniformElements,
                               record.hasImageUniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandle{handleValue};
}

void GlRenderDevice::DestroyPipeline(PipelineHandle pipeline) {
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        UVE_ERROR("GlRenderDevice: DestroyPipeline called with an unknown or already-destroyed handle ({})",
                   pipeline.value);
        return;
    }
    m_impl->state.gl.glDeleteProgram(it->second.glProgram);
    m_impl->state.gl.glDeleteVertexArrays(1, &it->second.glVao);
    m_impl->state.pipelines.erase(it);
}

std::vector<UniformReflection> GlRenderDevice::GetPipelineUniforms(PipelineHandle pipeline) const {
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        return {};
    }
    std::vector<UniformReflection> result;
    result.reserve(it->second.uniforms.size());
    for (const auto& [name, record] : it->second.uniforms) {
        result.push_back(UniformReflection{name, record.type, record.location, record.arraySize});
    }
    return result;
}

bool GlRenderDevice::GetPipelineBinary(PipelineHandle pipeline, std::vector<std::byte>& outBinary,
                                              std::uint32_t& outFormat) const {
    if (m_impl->state.gl.glGetProgramBinary == nullptr || m_impl->state.gl.glProgramBinary == nullptr) {
        UVE_WARNING("GlRenderDevice: program-binary export is unavailable on this driver; using source compilation");
        return false;
    }
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        UVE_ERROR("GlRenderDevice: GetPipelineBinary called with an unknown handle ({})", pipeline.value);
        return false;
    }

    GLint binaryLength = 0;
    m_impl->state.gl.glGetProgramiv(it->second.glProgram, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0) {
        UVE_WARNING("GlRenderDevice: GetPipelineBinary - driver reports no binary available for pipeline {}",
                     pipeline.value);
        return false;
    }

    outBinary.resize(static_cast<std::size_t>(binaryLength));
    GLsizei writtenLength = 0;
    GLenum glBinaryFormat = 0;
    m_impl->state.gl.glGetProgramBinary(it->second.glProgram, binaryLength, &writtenLength, &glBinaryFormat,
                                          outBinary.data());
    outBinary.resize(static_cast<std::size_t>(writtenLength));
    outFormat = static_cast<std::uint32_t>(glBinaryFormat);
    return true;
}

PipelineHandle GlRenderDevice::CreatePipelineFromBinary(std::span<const std::byte> binary,
                                                                  std::uint32_t format,
                                                                  const PipelineBinaryDesc& desc) {
    if (m_impl->state.gl.glGetProgramBinary == nullptr || m_impl->state.gl.glProgramBinary == nullptr) {
        UVE_WARNING("GlRenderDevice: program-binary import is unavailable on this driver; treating cache as a miss");
        return kInvalidPipelineHandle;
    }
    if (!IsVertexLayoutValid(desc.vertexLayout)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown vertex attribute format");
        return kInvalidPipelineHandle;
    }
    if (m_impl->state.gl.glVertexAttribIPointer == nullptr &&
        std::any_of(desc.vertexLayout.begin(), desc.vertexLayout.end(), [](const VertexAttribute& attribute) {
            return IsVertexAttributeFormatInteger(attribute.format);
        })) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary needs glVertexAttribIPointer for an integer vertex attribute "
                  "format, which this context does not provide");
        return kInvalidPipelineHandle;
    }
    if (!IsPipelineBlendModeValid(desc.blendMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown blend mode");
        return kInvalidPipelineHandle;
    }
    if (!IsPrimitiveTopologyValid(desc.topology)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown primitive topology");
        return kInvalidPipelineHandle;
    }
    if (!IsCullModeValid(desc.cullMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown cull mode");
        return kInvalidPipelineHandle;
    }
    if (!IsFrontFaceValid(desc.frontFace)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown front-face winding");
        return kInvalidPipelineHandle;
    }
#if defined(__ANDROID__)
    if (desc.fillMode == FillMode::Wireframe) {
        UVE_WARNING("GlRenderDevice: GLES does not support polygon wireframe mode");
        return kInvalidPipelineHandle;
    }
#endif
    if (!IsFillModeValid(desc.fillMode)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown fill mode");
        return kInvalidPipelineHandle;
    }
    if (!IsDepthCompareValid(desc.depthCompare)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown depth comparison");
        return kInvalidPipelineHandle;
    }
    if (!IsStencilStateValid(desc.stencil)) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary received an unknown stencil state");
        return kInvalidPipelineHandle;
    }
    if (m_impl->state.maxVertexAttribs <= 0 ||
        desc.vertexLayout.size() > static_cast<std::size_t>(m_impl->state.maxVertexAttribs)) {
        UVE_ERROR("GlRenderDevice: pipeline vertex layout exceeds GL vertex-attrib limits");
        return kInvalidPipelineHandle;
    }
    if (!desc.vertexLayout.empty() &&
        (!IsVertexLayoutWithinStride(desc.vertexLayout, desc.vertexStride) || desc.vertexStride == 0U ||
         desc.vertexStride > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary vertex attributes exceed the GL stride range");
        return kInvalidPipelineHandle;
    }
    if (binary.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlRenderDevice: CreatePipelineFromBinary binary exceeds the GLsizei range");
        return kInvalidPipelineHandle;
    }
    const GLuint glProgram = m_impl->state.gl.glCreateProgram();
    m_impl->state.gl.glProgramBinary(glProgram, static_cast<GLenum>(format), binary.data(),
                                       static_cast<GLsizei>(binary.size()));

    GLint linked = GL_FALSE;
    m_impl->state.gl.glGetProgramiv(glProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        // A driver-rejected binary (e.g. stale after a driver/GPU update) is an expected,
        // recoverable condition - callers must treat this exactly like a cache miss, never a
        // hard compile failure, so this logs at WARNING rather than ERROR.
        UVE_WARNING("GlRenderDevice: CreatePipelineFromBinary - driver rejected the supplied binary: {}",
                     GetProgramInfoLog(m_impl->state.gl, glProgram));
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandle;
    }

    GLuint glVao = 0;
    m_impl->state.gl.glGenVertexArrays(1, &glVao);

    Detail::GlDeviceState::PipelineRecord record;
    record.glProgram = glProgram;
    record.glVao = glVao;
    record.vertexLayout = desc.vertexLayout;
    record.vertexStride = desc.vertexStride;
    record.depthTestEnabled = desc.depthTestEnabled;
    record.depthWriteEnabled = desc.depthWriteEnabled;
    record.blendMode = desc.blendMode;
    record.cullMode = desc.cullMode;
    record.frontFace = desc.frontFace;
    record.fillMode = desc.fillMode;
    record.depthBiasEnabled = desc.depthBiasEnabled;
    record.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    record.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    record.depthCompare = desc.depthCompare;
    record.stencil = desc.stencil;
    record.topology = desc.topology;
    // Uniform locations are not guaranteed portable across a binary load even though behavior
    // is - reflection must always be re-run here, never assumed inherited from the original
    // compile that produced this binary.
    ReflectPipelineUniforms(m_impl->state.gl, glProgram, record.uniforms, record.uniformElements,
                               record.hasImageUniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandle{handleValue};
}

std::unique_ptr<ICommandBuffer> GlRenderDevice::CreateCommandBuffer() {
    return std::make_unique<GlCommandBuffer>(m_impl->state);
}

void GlRenderDevice::Submit(std::unique_ptr<ICommandBuffer> commandBuffer) {
    UVE_ASSERT(commandBuffer != nullptr);
    auto* const glCommandBuffer = dynamic_cast<GlCommandBuffer*>(commandBuffer.get());
    UVE_ASSERT(glCommandBuffer != nullptr); // only this device's own CreateCommandBuffer() ever produces one
    // Every GL call already executed during recording (OpenGL has no separate replay step) — the
    // command buffer is simply released here.
}

void GlRenderDevice::Present() {
    if (IsUsable()) {
        CollectRetiredTextures();
        m_impl->state.windowManager->SwapBuffers();
    }
}

std::string_view GlRenderDevice::GetBackendName() const noexcept {
    return "OpenGL";
}

bool GlRenderDevice::IsUsable() const noexcept {
    return m_impl != nullptr && m_impl->state.windowManager != nullptr &&
           m_impl->state.windowManager->IsValid() && m_impl->state.gl.IsComplete();
}

std::size_t GlRenderDevice::GetLiveResourceCount() const noexcept {
    return m_impl->state.buffers.size() + m_impl->state.textures.size() + m_impl->state.samplers.size() +
           m_impl->state.shaders.size() + m_impl->state.pipelines.size() + m_impl->state.fences.size() +
           m_impl->state.queries.size();
}

std::uint32_t GlRenderDevice::GetNativeTextureId(const TextureHandle texture) const noexcept {
    const auto it = m_impl->state.textures.find(texture.value);
    return it == m_impl->state.textures.end() ? 0U : it->second.glTexture;
}

} // namespace UVE::Render

namespace UVE::Render {
namespace {
RenderOperationResult FinishDeviceOperation() {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return RenderOperationResult::Success;
    UVE_ERROR("GL device operation failed (error {})", static_cast<unsigned>(error));
    return RenderOperationResult::SystemError;
}
} // namespace
RenderDeviceCapabilities GlRenderDevice::GetCapabilities() const noexcept {
    RenderDeviceCapabilities caps;
    if (!IsUsable())
        return caps;
    if (m_impl->state.capabilitiesCached)
        return m_impl->state.cachedCapabilities;
    const auto native = RenderFeatureSupport::Native;
    caps.retainedTextures = native;
    const auto &gl = m_impl->state.gl;
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
#if defined(__ANDROID__)
    const bool copyVersion = major >= 3, syncVersion = major >= 3, queryVersion = major >= 3;
#else
    const bool copyVersion = major > 3 || (major == 3 && minor >= 1);
    const bool syncVersion = major > 3 || (major == 3 && minor >= 2);
    const bool queryVersion = major > 3 || (major == 3 && minor >= 3);
    if (syncVersion) caps.geometryShaders = native; // Geometry is core since desktop GL 3.2.
    if (m_impl->state.supportsComputeShaders) {
        caps.storageBuffers = native;
        if (gl.glMemoryBarrier)
            caps.memoryBarriers = native;
    }
#endif
    if (m_impl->state.halfFloatColorTargetsSupported)
        caps.halfFloatColorTargets = native;
    if (copyVersion && gl.glCopyBufferSubData)
        caps.bufferCopy = native;
    if (gl.glBlitFramebuffer && gl.glGenFramebuffers && gl.glDeleteFramebuffers && gl.glBindFramebuffer &&
        gl.glFramebufferTexture2D && gl.glCheckFramebufferStatus && gl.glDrawBuffers)
        caps.textureBlit = native;
    if (gl.glGenerateMipmap)
        caps.mipGeneration = native;
    if (syncVersion && gl.glFenceSync && gl.glClientWaitSync && gl.glDeleteSync)
        caps.fences = native;
    if (queryVersion && gl.glGenQueries && gl.glDeleteQueries && gl.glBeginQuery && gl.glEndQuery &&
        gl.glGetQueryiv && gl.glGetQueryObjectuiv) {
        caps.occlusionQueries = native;
#if !defined(__ANDROID__)
        if (gl.glQueryCounter && gl.glGetQueryObjectui64v) {
            GLint bits = 0;
            gl.glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
            if (bits > 0)
                caps.timestampQueries = native;
        }
#endif
    }
    caps.surface = native;
    return caps;
}
RenderOperationResult GlRenderDevice::GetTextureDesc(TextureHandle texture,
                                                              TextureDesc &out) const {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    const auto found = m_impl->state.textures.find(texture.value);
    if (found == m_impl->state.textures.end())
        return RenderOperationResult::InvalidHandle;
    out = found->second.desc;
    return RenderOperationResult::Success;
}
RenderOperationResult GlRenderDevice::GetBufferDesc(BufferHandle buffer,
                                                             BufferDesc &out) const {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    const auto found = m_impl->state.buffers.find(buffer.value);
    if (found == m_impl->state.buffers.end())
        return RenderOperationResult::InvalidHandle;
    out = BufferDesc{found->second.sizeBytes, found->second.usage, found->second.memory,
                        found->second.updateFrequency};
    return RenderOperationResult::Success;
}
RenderOperationResult GlRenderDevice::CopyBuffer(const BufferCopyDesc &copy) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    if (GetCapabilities().bufferCopy != RenderFeatureSupport::Native)
        return RenderOperationResult::Unsupported;
    auto &state = m_impl->state;
    const auto source = state.buffers.find(copy.source.value),
               target = state.buffers.find(copy.destination.value);
    if (source == state.buffers.end() || target == state.buffers.end())
        return RenderOperationResult::InvalidHandle;
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<GLintptr>::max());
    if (!IsBufferCopyRangeValid(copy, source->second.sizeBytes, target->second.sizeBytes) ||
        copy.sourceOffset > maximum || copy.destinationOffset > maximum || copy.sizeBytes > maximum)
        return RenderOperationResult::InvalidArgument;
    GLint oldRead = 0, oldWrite = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &oldWrite);
    state.gl.glBindBuffer(GL_COPY_READ_BUFFER, source->second.glBuffer);
    state.gl.glBindBuffer(GL_COPY_WRITE_BUFFER, target->second.glBuffer);
    state.gl.glCopyBufferSubData(
        GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(copy.sourceOffset),
        static_cast<GLintptr>(copy.destinationOffset), static_cast<GLsizeiptr>(copy.sizeBytes));
    state.gl.glBindBuffer(GL_COPY_READ_BUFFER, static_cast<GLuint>(oldRead));
    state.gl.glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<GLuint>(oldWrite));
    return FinishDeviceOperation();
}
RenderOperationResult GlRenderDevice::BlitTexture(const TextureBlitDesc &blit) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    if (GetCapabilities().textureBlit != RenderFeatureSupport::Native)
        return RenderOperationResult::Unsupported;
    auto &state = m_impl->state;
    const auto source = state.textures.find(blit.source.value),
               target = state.textures.find(blit.destination.value);
    if (source == state.textures.end() || target == state.textures.end())
        return RenderOperationResult::InvalidHandle;
    if (!IsTextureBlitValid(blit, source->second.desc, target->second.desc))
        return RenderOperationResult::InvalidArgument;
    if ((source->second.desc.type == TextureType::Texture2DArray ||
         target->second.desc.type == TextureType::Texture2DArray) &&
        !state.gl.glFramebufferTextureLayer)
        return RenderOperationResult::Unsupported;
    GLint oldRead = 0, oldDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
#if !defined(__ANDROID__)
    const GLboolean oldSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (target->second.desc.colorSpace == TextureColorSpace::Srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    const GLboolean oldScissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    if (state.blitReadFbo == 0U)
        state.gl.glGenFramebuffers(1, &state.blitReadFbo);
    if (state.blitDrawFbo == 0U)
        state.gl.glGenFramebuffers(1, &state.blitDrawFbo);
    const GLuint framebuffers[2]{state.blitReadFbo, state.blitDrawFbo};
    const auto attach = [&](GLenum binding, const auto &texture, const TextureRegion &region, GLuint fbo) {
        state.gl.glBindFramebuffer(binding, fbo);
        if (texture.desc.type == TextureType::Texture2DArray) {
            state.gl.glFramebufferTextureLayer(binding, GL_COLOR_ATTACHMENT0, texture.glTexture,
                                               static_cast<GLint>(region.mip),
                                               static_cast<GLint>(region.layer));
        } else {
            const GLenum type = texture.desc.type == TextureType::Cubemap
                                    ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.layer
                                    : GL_TEXTURE_2D;
            state.gl.glFramebufferTexture2D(binding, GL_COLOR_ATTACHMENT0, type, texture.glTexture,
                                            static_cast<GLint>(region.mip));
        }
        return state.gl.glCheckFramebufferStatus(binding) == GL_FRAMEBUFFER_COMPLETE;
    };
    const bool readReady = framebuffers[0] != 0U &&
                           attach(GL_READ_FRAMEBUFFER, source->second, blit.sourceRegion, framebuffers[0]);
    const bool drawReady = framebuffers[1] != 0U && attach(GL_DRAW_FRAMEBUFFER, target->second,
                                                           blit.destinationRegion, framebuffers[1]);
    if (readReady && drawReady) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        const GLenum attachment = GL_COLOR_ATTACHMENT0;
        state.gl.glDrawBuffers(1, &attachment);
        const auto &a = blit.sourceRegion;
        const auto &b = blit.destinationRegion;
        state.gl.glBlitFramebuffer(
            static_cast<GLint>(a.x), static_cast<GLint>(a.y), static_cast<GLint>(a.x + a.width),
            static_cast<GLint>(a.y + a.height), static_cast<GLint>(b.x), static_cast<GLint>(b.y),
            static_cast<GLint>(b.x + b.width), static_cast<GLint>(b.y + b.height), GL_COLOR_BUFFER_BIT,
            blit.filter == BlitFilter::Point ? GL_NEAREST : GL_LINEAR);
    }
    const auto detach = [&](GLenum binding, const auto &texture, GLuint fbo) {
        if (fbo == 0U)
            return;
        state.gl.glBindFramebuffer(binding, fbo);
        if (texture.desc.type == TextureType::Texture2DArray)
            state.gl.glFramebufferTextureLayer(binding, GL_COLOR_ATTACHMENT0, 0U, 0, 0);
        else
            state.gl.glFramebufferTexture2D(binding, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0U, 0);
    };
    detach(GL_READ_FRAMEBUFFER, source->second, framebuffers[0]);
    detach(GL_DRAW_FRAMEBUFFER, target->second, framebuffers[1]);
    state.gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(oldRead));
    state.gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(oldDraw));
    if (oldScissor)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
#if !defined(__ANDROID__)
    if (oldSrgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    const auto result = FinishDeviceOperation();
    return readReady && drawReady ? result : RenderOperationResult::SystemError;
}
RenderOperationResult GlRenderDevice::GenerateMipmaps(TextureHandle texture) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    if (GetCapabilities().mipGeneration != RenderFeatureSupport::Native)
        return RenderOperationResult::Unsupported;
    auto &state = m_impl->state;
    const auto found = state.textures.find(texture.value);
    if (found == state.textures.end())
        return RenderOperationResult::InvalidHandle;
    const auto &desc = found->second.desc;
    if (!CanGenerateMipmaps(desc))
        return RenderOperationResult::InvalidArgument;
    const GLenum target = OperationTextureTarget(desc.type);
    const GLenum query = TextureBindingQueryForTarget(target);
    GLint previous = 0;
    glGetIntegerv(query, &previous);
    glBindTexture(target, found->second.glTexture);
    state.gl.glGenerateMipmap(target);
    glBindTexture(target, static_cast<GLuint>(previous));
    return FinishDeviceOperation();
}
RenderOperationResult GlRenderDevice::InsertFence(FenceHandle &output) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    if (GetCapabilities().fences != RenderFeatureSupport::Native)
        return RenderOperationResult::Unsupported;
    auto &state = m_impl->state;
    if (state.nextFence == 0U)
        return RenderOperationResult::SystemError;
    const FenceHandle handle{state.nextFence++};
    auto [entry, inserted] = state.fences.emplace(handle.value, nullptr);
    static_cast<void>(inserted);
    entry->second = state.gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
    if (entry->second == nullptr) {
        state.fences.erase(entry);
        return RenderOperationResult::SystemError;
    }
    output = handle;
    return RenderOperationResult::Success;
}
RenderOperationResult GlRenderDevice::WaitFence(FenceHandle handle, std::uint64_t timeoutNs) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.fences.find(handle.value);
    if (found == state.fences.end())
        return RenderOperationResult::InvalidHandle;
    const GLenum result = state.gl.glClientWaitSync(found->second, GL_SYNC_FLUSH_COMMANDS_BIT, timeoutNs);
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
        return RenderOperationResult::Success;
    if (result == GL_TIMEOUT_EXPIRED)
        return RenderOperationResult::TimedOut;
    return RenderOperationResult::SystemError;
}
RenderOperationResult GlRenderDevice::DestroyFence(FenceHandle handle) {
    const auto waited = WaitFence(handle, 0U);
    if (waited == RenderOperationResult::TimedOut)
        return RenderOperationResult::NotReady;
    if (waited != RenderOperationResult::Success)
        return waited;
    m_impl->state.gl.glDeleteSync(m_impl->state.fences.at(handle.value));
    m_impl->state.fences.erase(handle.value);
    return FinishDeviceOperation();
}
RenderOperationResult GlRenderDevice::WaitIdle() {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    glFinish();
    const auto waited = FinishDeviceOperation();
    if (waited == RenderOperationResult::Success) CollectRetiredTextures();
    return waited;
}
RenderOperationResult GlRenderDevice::CreateQuery(RenderQueryKind kind, QueryHandle &output) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    if (kind != RenderQueryKind::Timestamp && kind != RenderQueryKind::AnySamplesPassed)
        return RenderOperationResult::InvalidArgument;
    const auto caps = GetCapabilities();
    if ((kind == RenderQueryKind::Timestamp ? caps.timestampQueries : caps.occlusionQueries) !=
        RenderFeatureSupport::Native)
        return RenderOperationResult::Unsupported;
    auto &state = m_impl->state;
    if (state.nextQuery == 0U)
        return RenderOperationResult::SystemError;
    const QueryHandle handle{state.nextQuery++};
    auto [entry, inserted] =
        state.queries.emplace(handle.value, Detail::GlDeviceState::QueryRecord{0U, kind, false});
    static_cast<void>(inserted);
    state.gl.glGenQueries(1, &entry->second.native);
    if (entry->second.native == 0U) {
        state.queries.erase(entry);
        return RenderOperationResult::SystemError;
    }
    output = handle;
    return RenderOperationResult::Success;
}
RenderOperationResult GlRenderDevice::BeginQuery(QueryHandle handle) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::AnySamplesPassed)
        return RenderOperationResult::InvalidArgument;
    GLint active = 0;
    state.gl.glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &active);
    if (active != 0 || state.activeOcclusion != 0U)
        return RenderOperationResult::InvalidState;
    state.gl.glBeginQuery(GL_ANY_SAMPLES_PASSED, found->second.native);
    const auto result = FinishDeviceOperation();
    if (result == RenderOperationResult::Success) {
        state.activeOcclusion = handle.value;
        found->second.issued = false;
    }
    return result;
}
RenderOperationResult GlRenderDevice::EndQuery(QueryHandle handle) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (state.activeOcclusion != handle.value)
        return RenderOperationResult::InvalidState;
    state.gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
    state.activeOcclusion = 0U;
    const auto result = FinishDeviceOperation();
    found->second.issued = result == RenderOperationResult::Success;
    return result;
}
RenderOperationResult GlRenderDevice::WriteTimestamp(QueryHandle handle) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::Timestamp)
        return RenderOperationResult::InvalidArgument;
#if defined(__ANDROID__)
    return RenderOperationResult::Unsupported;
#else
    state.gl.glQueryCounter(found->second.native, GL_TIMESTAMP);
    const auto result = FinishDeviceOperation();
    found->second.issued = result == RenderOperationResult::Success;
    return result;
#endif
}
RenderOperationResult GlRenderDevice::GetQueryResult(QueryHandle handle,
                                                              RenderQueryResult &output, bool wait) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (!found->second.issued || state.activeOcclusion == handle.value)
        return RenderOperationResult::NotReady;
    GLuint available = GL_FALSE;
    state.gl.glGetQueryObjectuiv(found->second.native, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!wait && available == GL_FALSE)
        return RenderOperationResult::NotReady;
    RenderQueryResult candidate{found->second.kind, 0U};
    if (found->second.kind == RenderQueryKind::AnySamplesPassed) {
        GLuint value = 0U;
        state.gl.glGetQueryObjectuiv(found->second.native, GL_QUERY_RESULT, &value);
        candidate.value = value != 0U ? 1U : 0U;
    } else {
#if defined(__ANDROID__)
        return RenderOperationResult::Unsupported;
#else
        GLuint64 value = 0U;
        state.gl.glGetQueryObjectui64v(found->second.native, GL_QUERY_RESULT, &value);
        candidate.value = value;
#endif
    }
    const auto result = FinishDeviceOperation();
    if (result == RenderOperationResult::Success)
        output = candidate;
    return result;
}
RenderOperationResult GlRenderDevice::DestroyQuery(QueryHandle handle) {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (state.activeOcclusion == handle.value)
        return RenderOperationResult::InvalidState;
    state.gl.glDeleteQueries(1, &found->second.native);
    state.queries.erase(found);
    return FinishDeviceOperation();
}
RenderOperationResult GlRenderDevice::GetSurfaceInfo(RenderSurfaceInfo &output) const {
    if (!IsUsable())
        return RenderOperationResult::DeviceUnavailable;
    RenderSurfaceInfo candidate;
    candidate.width = m_impl->state.windowManager->GetWidth();
    candidate.height = m_impl->state.windowManager->GetHeight();
    candidate.drawable = candidate.width != 0U && candidate.height != 0U;
#if !defined(__ANDROID__)
    GLboolean doubleBuffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
    candidate.imageCount = doubleBuffered ? 2U : 1U;
#endif
    output = candidate;
    return RenderOperationResult::Success;
}
} // namespace UVE::Render
