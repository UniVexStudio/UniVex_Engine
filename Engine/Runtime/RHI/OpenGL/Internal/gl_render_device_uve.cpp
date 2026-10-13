// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_opengl/gl_render_device_uve.h"

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

#include "gl_command_buffer_uve.h"

// The anisotropy enums live in glext.h on desktop GL; this fallback keeps exotic platform
// headers (Android NDK configurations without the EXT declaration) compiling — the value is
// the registered 0x84FE on every API.
#if !defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#include "gl_error_check_uve.h"
#include "gl_render_device_state_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Render {

namespace {

#if defined(__ANDROID__)
constexpr char kAndroidGlLogTagUVE[] = "UVEAndroidGLES";

void LogAndroidGlFailureUVE(const char* operation, const char* detail) noexcept {
    __android_log_print(ANDROID_LOG_ERROR, kAndroidGlLogTagUVE, "%s: %s", operation, detail);
}

void* EglProcAddressBridgeUVE(const char* name) {
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
void* GlfwProcAddressBridgeUVE(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}
#endif

[[nodiscard]] GLenum BufferUsageToGlTargetUVE(BufferUsageUVE usage) noexcept {
    switch (usage) {
        case BufferUsageUVE::Vertex:
            return GL_ARRAY_BUFFER;
        case BufferUsageUVE::Index:
            return GL_ELEMENT_ARRAY_BUFFER;
        case BufferUsageUVE::Uniform:
            return GL_UNIFORM_BUFFER;
        case BufferUsageUVE::Storage:
        // CS7: an IndirectStorage buffer's HOME target is the SSBO one - that is how it is
        // created, updated and read back, and how the compute kernel that fills it binds it.
        // GL_DRAW_INDIRECT_BUFFER is a transient bind made at draw time by
        // DrawIndexedIndirectUVE, not the buffer's resting place; GL buffer objects are
        // untyped, so the same name binds legally to both targets.
        case BufferUsageUVE::IndirectStorage:
#if !defined(__ANDROID__)
            return GL_SHADER_STORAGE_BUFFER; // M2f: desktop GL 4.3+ (gated at bind time)
#else
            return 0U; // the fixed ES 3.0 baseline has no SSBO target; creation fails loudly
#endif
    }
    return GL_ARRAY_BUFFER;
}

[[nodiscard]] GLenum BufferTargetToBindingQueryUVE(GLenum target) noexcept {
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
constexpr GLenum kGlNumCompressedTextureFormatsUVE = 0x86A2;
constexpr GLenum kGlCompressedTextureFormatsUVE = 0x86A3;
constexpr GLint kGlCompressedRgbS3tcDxt1UVE = 0x83F0;
constexpr GLint kGlCompressedSrgbS3tcDxt1UVE = 0x8C4C;
constexpr GLint kGlCompressedRgbaS3tcDxt5UVE = 0x83F3;
constexpr GLint kGlCompressedSrgbAlphaS3tcDxt5UVE = 0x8C4F;
constexpr GLint kGlCompressedRgbaBptcUnormUVE = 0x8E8C;
constexpr GLint kGlCompressedSrgbAlphaBptcUnormUVE = 0x8E8D;
constexpr GLint kGlCompressedRgb8Etc2UVE = 0x9274;
constexpr GLint kGlCompressedSrgb8Etc2UVE = 0x9275;
constexpr GLint kGlCompressedRgba8Etc2EacUVE = 0x9278;
constexpr GLint kGlCompressedSrgb8Alpha8Etc2EacUVE = 0x9279;
constexpr GLint kGlCompressedRgbaAstc4x4UVE = 0x93B0;
constexpr GLint kGlCompressedSrgb8Alpha8Astc4x4UVE = 0x93D0;

struct GlTextureFormatUVE {
    GLint internalFormat;
    GLenum format;
    GLenum type;
};

[[nodiscard]] GlTextureFormatUVE TextureFormatToGlUVE(TextureFormatUVE format,
                                                        TextureColorSpaceUVE colorSpace) noexcept {
    switch (format) {
        case TextureFormatUVE::RGBA8Unorm:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                    GL_RGBA, GL_UNSIGNED_BYTE};
        case TextureFormatUVE::RGBA16Float:
            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case TextureFormatUVE::Depth32Float:
            return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
        case TextureFormatUVE::BC1RGB:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgbS3tcDxt1UVE
                                                              : kGlCompressedRgbS3tcDxt1UVE,
                    0U, 0U};
        case TextureFormatUVE::BC3RGBA:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgbAlphaS3tcDxt5UVE
                                                              : kGlCompressedRgbaS3tcDxt5UVE,
                    0U, 0U};
        case TextureFormatUVE::BC7RGBA:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgbAlphaBptcUnormUVE
                                                              : kGlCompressedRgbaBptcUnormUVE,
                    0U, 0U};
        case TextureFormatUVE::ETC2RGB8:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgb8Etc2UVE
                                                              : kGlCompressedRgb8Etc2UVE,
                    0U, 0U};
        case TextureFormatUVE::ETC2RGBA8:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgb8Alpha8Etc2EacUVE
                                                              : kGlCompressedRgba8Etc2EacUVE,
                    0U, 0U};
        case TextureFormatUVE::ASTC4x4RGBA:
            return {colorSpace == TextureColorSpaceUVE::Srgb ? kGlCompressedSrgb8Alpha8Astc4x4UVE
                                                              : kGlCompressedRgbaAstc4x4UVE,
                    0U, 0U};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

[[nodiscard]] GLenum ShaderStageToGlUVE(ShaderStageUVE stage) noexcept {
    switch (stage) {
        case ShaderStageUVE::Vertex:
            return GL_VERTEX_SHADER;
        case ShaderStageUVE::Fragment:
            return GL_FRAGMENT_SHADER;
        case ShaderStageUVE::Compute:
        case ShaderStageUVE::Geometry:
#if !defined(__ANDROID__)
            return stage == ShaderStageUVE::Compute ? GL_COMPUTE_SHADER : GL_GEOMETRY_SHADER;
#else
            return 0U;
#endif
    }
    return GL_VERTEX_SHADER;
}

[[nodiscard]] bool IsSamplerUniformTypeUVE(GLenum glType) noexcept {
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
/// ShaderDataTypeUVE. Supported sampler types report as Int, since a sampler uniform receives a
/// texture-unit index through glUniform1i regardless of its concrete sampler type. Unsupported
/// shapes remain explicit instead of being silently collapsed into Int, preventing invalid setter
/// calls such as glUniform1i on uint or vector uniforms.
[[nodiscard]] ShaderDataTypeUVE GlUniformTypeToShaderDataTypeUVE(GLenum glType) noexcept {
    if (IsSamplerUniformTypeUVE(glType)) {
        return ShaderDataTypeUVE::Int;
    }
    switch (glType) {
        case GL_FLOAT:
            return ShaderDataTypeUVE::Float;
        case GL_FLOAT_VEC2:
            return ShaderDataTypeUVE::Vec2;
        case GL_FLOAT_VEC3:
            return ShaderDataTypeUVE::Vec3;
        case GL_FLOAT_VEC4:
            return ShaderDataTypeUVE::Vec4;
        case GL_FLOAT_MAT3:
            return ShaderDataTypeUVE::Mat3;
        case GL_FLOAT_MAT4:
            return ShaderDataTypeUVE::Mat4;
        case GL_BOOL:
            return ShaderDataTypeUVE::Bool;
        case GL_INT:
            return ShaderDataTypeUVE::Int;
        default:
            return ShaderDataTypeUVE::Unsupported;
    }
}

/// Reflects every active uniform in `glProgram` (already linked) into `outUniforms`, keyed by
/// name with a trailing "[0]" (glGetActiveUniform's own convention for the first element of an
/// array uniform) stripped so callers can address an array uniform by its bare declared name.
void ReflectPipelineUniformsUVE(
    const Detail::GlFunctionsUVE& gl, GLuint glProgram,
    std::unordered_map<std::string, Detail::GlDeviceStateUVE::PipelineRecordUVE::UniformRecordUVE,
                       Detail::TransparentStringHashUVE, Detail::TransparentStringEqualUVE>& outUniforms) {
    outUniforms.clear();

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
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0) {
            name.resize(name.size() - 3);
        }

        const GLint location = gl.glGetUniformLocation(glProgram, name.c_str());
        outUniforms.emplace(std::move(name), Detail::GlDeviceStateUVE::PipelineRecordUVE::UniformRecordUVE{
                                                  GlUniformTypeToShaderDataTypeUVE(glType), location,
                                                  static_cast<std::uint32_t>(arraySize),
                                                  glType == GL_IMAGE_2D});
    }
}

[[nodiscard]] std::string GetShaderInfoLogUVE(const Detail::GlFunctionsUVE& gl, GLuint shader) {
    GLint logLength = 0;
    gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 0) {
        return {};
    }
    std::string log(static_cast<std::size_t>(logLength), '\0');
    gl.glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    return log;
}

[[nodiscard]] std::string GetProgramInfoLogUVE(const Detail::GlFunctionsUVE& gl, GLuint program) {
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

struct GlRenderDeviceUVE::ImplUVE {
    Detail::GlDeviceStateUVE state;
};

GlRenderDeviceUVE::GlRenderDeviceUVE(Window::IWindowManagerUVE& windowManager)
    : m_impl(std::make_unique<ImplUVE>()) {
    m_impl->state.windowManager = &windowManager;
    if (!windowManager.IsValidUVE()) {
        UVE_WARNING("GlRenderDeviceUVE: window manager has no usable graphics context; backend disabled");
        return;
    }
#if defined(__ANDROID__)
    m_impl->state.gl = Detail::LoadGlFunctionsUVE(&EglProcAddressBridgeUVE);
#else
    m_impl->state.gl = Detail::LoadGlFunctionsUVE(&GlfwProcAddressBridgeUVE);
#endif

    if (!m_impl->state.gl.IsCompleteUVE()) {
        UVE_WARNING("GlRenderDeviceUVE: required GL function pointers are unavailable; backend disabled");
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
                if (extensionName != nullptr &&
                    std::strcmp(extensionName, "GL_EXT_texture_filter_anisotropic") == 0) {
                    m_impl->state.samplerAnisotropySupported = true;
                    break;
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
        GLint compressedFormatCount = 0;
        glGetIntegerv(kGlNumCompressedTextureFormatsUVE, &compressedFormatCount);
        if (compressedFormatCount > 0 && compressedFormatCount <= 4096) {
            m_impl->state.compressedTextureFormats.resize(static_cast<std::size_t>(compressedFormatCount));
            glGetIntegerv(kGlCompressedTextureFormatsUVE, m_impl->state.compressedTextureFormats.data());
        }
#if !defined(__ANDROID__)
        // GL_MAJOR_VERSION/GL_MINOR_VERSION are valid integer queries from GL 3.0 onward, which
        // this engine's desktop baseline already requires - safe to call unconditionally here.
        GLint contextMajorVersion = 0;
        GLint contextMinorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &contextMajorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &contextMinorVersion);
        m_impl->state.supportsComputeShadersUVE =
            contextMajorVersion > 4 || (contextMajorVersion == 4 && contextMinorVersion >= 3);
        if (m_impl->state.supportsComputeShadersUVE) {
            // M2f: SSBO binding points share compute's GL 4.3 floor; queried once here so
            // BindStorageBufferUVE validates slots against the real driver limit.
            glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,
                          &m_impl->state.maxShaderStorageBindings);
        }
#endif
        Detail::RegisterGlDebugCallbackUVE(m_impl->state.gl);
        UVE_INFO("GlRenderDeviceUVE: initialized, backend GL_VERSION={}",
                  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    }
}

GlRenderDeviceUVE::~GlRenderDeviceUVE() {
    if (!IsUsableUVE()) {
        return;
    }
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
        // glDeleteSamplers is non-null whenever this map is non-empty: CreateSamplerUVE fails
        // closed without the entry points, so a sampler record implies a loaded deleter.
        m_impl->state.gl.glDeleteSamplers(1, &record.glSampler);
    }
    m_impl->state.samplers.clear();
}

BufferHandleUVE GlRenderDeviceUVE::CreateBufferUVE(const BufferDescUVE& desc, std::span<const std::byte> initialData) {
    if (!ValidateBufferUploadUVE(desc, initialData)) {
        UVE_ERROR("GlRenderDeviceUVE: CreateBufferUVE initial data exceeds buffer size");
        return kInvalidBufferHandleUVE;
    }
    if (!IsBufferUsageValidUVE(desc.usage)) {
        UVE_ERROR("GlRenderDeviceUVE: CreateBufferUVE received an unknown buffer usage");
        return kInvalidBufferHandleUVE;
    }
    if (IsStorageBindableUsageUVE(desc.usage) && !m_impl->state.supportsComputeShadersUVE) {
        // SSBOs are desktop GL 4.3 core (the same floor as compute shaders; the fixed ES 3.0
        // Android baseline has neither). Fail before touching GL_SHADER_STORAGE_BUFFER - on an
        // older context the enum itself would raise GL_INVALID_ENUM at glBufferData time.
        UVE_ERROR("GlRenderDeviceUVE: CreateBufferUVE(Storage) needs a desktop GL 4.3+ context "
                  "(shader storage buffers); this context does not offer them");
        return kInvalidBufferHandleUVE;
    }
    if (desc.sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max())) {
        UVE_ERROR("GlRenderDeviceUVE: CreateBufferUVE size exceeds the GLsizeiptr range");
        return kInvalidBufferHandleUVE;
    }
    const GLenum target = BufferUsageToGlTargetUVE(desc.usage);
    const GLenum bindingQuery = BufferTargetToBindingQueryUVE(target);
    if (bindingQuery == 0U) {
        UVE_ERROR("GlRenderDeviceUVE: CreateBufferUVE resolved an unsupported GL buffer target");
        return kInvalidBufferHandleUVE;
    }
    // Resource creation temporarily binds the object. Preserve the binding because a live
    // command buffer caches vertex/index handles and may otherwise skip its next required rebind.
    GLint previousBufferBinding = 0;
    glGetIntegerv(bindingQuery, &previousBufferBinding);

    GLuint glBuffer = 0;
    m_impl->state.gl.glGenBuffers(1, &glBuffer);
    m_impl->state.gl.glBindBuffer(target, glBuffer);
    m_impl->state.gl.glBufferData(target, static_cast<GLsizeiptr>(desc.sizeBytes),
                                    initialData.empty() ? nullptr : initialData.data(), GL_STATIC_DRAW);
    UVE_GL_CHECK_ERROR_UVE("CreateBufferUVE");
    m_impl->state.gl.glBindBuffer(target, static_cast<GLuint>(previousBufferBinding));

    const std::uint32_t handleValue = m_impl->state.nextBufferHandle++;
    m_impl->state.buffers.emplace(handleValue,
                                    Detail::GlDeviceStateUVE::BufferRecordUVE{glBuffer, target, desc.sizeBytes, desc.usage});
    return BufferHandleUVE{handleValue};
}

void GlRenderDeviceUVE::DestroyBufferUVE(BufferHandleUVE buffer) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDeviceUVE: DestroyBufferUVE called with an unknown or already-destroyed handle ({})",
                   buffer.value);
        return;
    }
    m_impl->state.gl.glDeleteBuffers(1, &it->second.glBuffer);
    m_impl->state.buffers.erase(it);
}

bool GlRenderDeviceUVE::UpdateBufferUVE(BufferHandleUVE buffer, std::span<const std::byte> data,
                                          std::uint64_t offsetBytes) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDeviceUVE: UpdateBufferUVE called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdateUVE(it->second.sizeBytes, data.size(), offsetBytes)) {
        UVE_ERROR("GlRenderDeviceUVE: UpdateBufferUVE write of {} bytes at offset {} exceeds buffer size {}",
                   data.size(), offsetBytes, it->second.sizeBytes);
        return false;
    }
    const GLenum bindingQuery = BufferTargetToBindingQueryUVE(it->second.target);
    if (bindingQuery == 0U) {
        UVE_ERROR("GlRenderDeviceUVE: UpdateBufferUVE resolved an unsupported GL buffer target");
        return false;
    }
    GLint previousBufferBinding = 0;
    glGetIntegerv(bindingQuery, &previousBufferBinding);
    m_impl->state.gl.glBindBuffer(it->second.target, it->second.glBuffer);
    m_impl->state.gl.glBufferSubData(it->second.target, static_cast<GLintptr>(offsetBytes),
                                       static_cast<GLsizeiptr>(data.size()), data.data());
    UVE_GL_CHECK_ERROR_UVE("UpdateBufferUVE");
    m_impl->state.gl.glBindBuffer(it->second.target, static_cast<GLuint>(previousBufferBinding));
    return true;
}

bool GlRenderDeviceUVE::ReadbackBufferUVE(BufferHandleUVE buffer, std::span<std::byte> outData,
                                            std::uint64_t offsetBytes) {
    const auto it = m_impl->state.buffers.find(buffer.value);
    if (it == m_impl->state.buffers.end()) {
        UVE_ERROR("GlRenderDeviceUVE: ReadbackBufferUVE called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdateUVE(it->second.sizeBytes, outData.size(), offsetBytes)) {
        UVE_ERROR("GlRenderDeviceUVE: ReadbackBufferUVE read of {} bytes at offset {} exceeds buffer size {}",
                   outData.size(), offsetBytes, it->second.sizeBytes);
        return false;
    }
    if (outData.empty()) {
        return true;
    }
    const GLenum bindingQuery = BufferTargetToBindingQueryUVE(it->second.target);
    if (bindingQuery == 0U) {
        UVE_ERROR("GlRenderDeviceUVE: ReadbackBufferUVE resolved an unsupported GL buffer target");
        return false;
    }
    // Cold-path synchronization, exactly what the interface promises. This backend executes its
    // commands at record time, but a compute dispatch's SSBO writes become visible to a
    // client-side read only after GL_BUFFER_UPDATE_BARRIER_BIT (writes seen by buffer queries)
    // and GL_SHADER_STORAGE_BARRIER_BIT (the SSBO writes themselves). glGetBufferSubData below
    // is then a blocking client read, which is itself a synchronization point - no glFinish is
    // needed on top of it, and issuing one would only widen the stall beyond this buffer.
    if (m_impl->state.gl.glMemoryBarrier != nullptr) {
        m_impl->state.gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
    GLint previousBufferBinding = 0;
    glGetIntegerv(bindingQuery, &previousBufferBinding);
    m_impl->state.gl.glBindBuffer(it->second.target, it->second.glBuffer);
    m_impl->state.gl.glGetBufferSubData(it->second.target, static_cast<GLintptr>(offsetBytes),
                                          static_cast<GLsizeiptr>(outData.size()), outData.data());
    UVE_GL_CHECK_ERROR_UVE("ReadbackBufferUVE");
    m_impl->state.gl.glBindBuffer(it->second.target, static_cast<GLuint>(previousBufferBinding));
    return true;
}

TextureHandleUVE GlRenderDeviceUVE::CreateTextureUVE(const TextureDescUVE& desc,
                                                       std::span<const std::byte> initialData) {
    if (!ValidateTextureUploadUVE(desc, initialData)) {
        UVE_ERROR("GlRenderDeviceUVE: CreateTextureUVE received an invalid descriptor or initial upload");
        return kInvalidTextureHandleUVE;
    }
    if (!SupportsTextureFormatUVE(desc.format, desc.colorSpace)) {
        UVE_ERROR("GlRenderDeviceUVE: requested texture format or color space is unsupported by this context");
        return kInvalidTextureHandleUVE;
    }
    if (desc.width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        desc.height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlRenderDeviceUVE: CreateTextureUVE dimensions exceed the GLsizei range");
        return kInvalidTextureHandleUVE;
    }
    std::array<GLsizei, 32U> mipImageSizes{};
    TextureMipExtentUVE mipExtent{desc.width, desc.height};
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        std::uint64_t levelBytes = 0U;
        if (!CalculateTextureMipByteCountUVE(desc.format, mipExtent.width, mipExtent.height, levelBytes) ||
            levelBytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizei>::max())) {
            UVE_ERROR("GlRenderDeviceUVE: texture mip upload exceeds the GLsizei image-size range");
            return kInvalidTextureHandleUVE;
        }
        mipImageSizes[level] = static_cast<GLsizei>(levelBytes);
        mipExtent.width = mipExtent.width > 1U ? mipExtent.width / 2U : 1U;
        mipExtent.height = mipExtent.height > 1U ? mipExtent.height / 2U : 1U;
    }

    const GlTextureFormatUVE glFormat = TextureFormatToGlUVE(desc.format, desc.colorSpace);
    // Texture creation temporarily binds the object on the current active unit. Preserve both
    // pieces of caller/command-buffer state so a live GlCommandBufferUVE texture cache cannot
    // falsely skip a later rebind after a resource is created between draws.
    // Tier 2.3: dimensional targets. Cubes upload face-by-face in CubemapFaceUVE order
    // (POSITIVE_X + face); arrays upload whole levels, all layers contiguous.
    const bool isCube = desc.type == TextureTypeUVE::Cubemap;
    const bool isArray = desc.type == TextureTypeUVE::Texture2DArray;
    const GLenum textureTarget = isCube ? GL_TEXTURE_CUBE_MAP
                                 : isArray ? GL_TEXTURE_2D_ARRAY
                                           : GL_TEXTURE_2D;
    const GLenum bindingQuery = isCube ? GL_TEXTURE_BINDING_CUBE_MAP
                                 : isArray ? GL_TEXTURE_BINDING_2D_ARRAY
                                           : GL_TEXTURE_BINDING_2D;
    if (isArray || isCube) {
        GLint maxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        GLint maxCubeSize = 0;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeSize);
        if ((isArray && desc.arrayLayers > static_cast<std::uint32_t>(maxLayers)) ||
            (isCube && desc.width > static_cast<std::uint32_t>(maxCubeSize))) {
            UVE_ERROR("GlRenderDeviceUVE: CreateTextureUVE array/cubemap exceeds GL limits");
            return kInvalidTextureHandleUVE;
        }
        if (isArray && m_impl->state.gl.glTexImage3D == nullptr &&
            !IsTextureFormatCompressedUVE(desc.format)) {
            UVE_ERROR("GlRenderDeviceUVE: CreateTextureUVE needs glTexImage3D for arrays");
            return kInvalidTextureHandleUVE;
        }
        if (isArray && m_impl->state.gl.glCompressedTexImage3D == nullptr &&
            IsTextureFormatCompressedUVE(desc.format)) {
            UVE_ERROR("GlRenderDeviceUVE: CreateTextureUVE needs glCompressedTexImage3D");
            return kInvalidTextureHandleUVE;
        }
    }

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(bindingQuery, &previousTextureBinding);

    GLuint glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(textureTarget, glTexture);
    std::size_t dataOffset = 0U;
    std::uint32_t levelWidth = desc.width;
    std::uint32_t levelHeight = desc.height;
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        const std::byte* const levelData =
            initialData.empty() ? nullptr : initialData.data() + dataOffset;
        if (isCube) {
            // Cube faces upload consecutively per level in CubemapFaceUVE order.
            for (std::uint32_t face = 0U; face < 6U; ++face) {
                const std::byte* faceData = nullptr;
                if (levelData != nullptr) {
                    faceData =
                        levelData + static_cast<std::size_t>(mipImageSizes[level]) * face;
                }
                const GLenum faceTarget =
                    static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
                if (IsTextureFormatCompressedUVE(desc.format)) {
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
        } else if (isArray) {
            // Whole-level upload: all layers contiguous (level-major, per the RHI contract).
            // The size_t intermediate keeps huge levels out of signed-overflow UB; the driver
            // reports oversized allocations through the standard GL error path below.
            const GLsizei levelBytesAllLayers = static_cast<GLsizei>(
                static_cast<std::size_t>(mipImageSizes[level]) * desc.arrayLayers);
            if (IsTextureFormatCompressedUVE(desc.format)) {
                m_impl->state.gl.glCompressedTexImage3D(
                    GL_TEXTURE_2D_ARRAY, static_cast<GLint>(level),
                    static_cast<GLenum>(glFormat.internalFormat), static_cast<GLsizei>(levelWidth),
                    static_cast<GLsizei>(levelHeight), static_cast<GLsizei>(desc.arrayLayers), 0,
                    levelBytesAllLayers, levelData);
            } else {
                m_impl->state.gl.glTexImage3D(
                    GL_TEXTURE_2D_ARRAY, static_cast<GLint>(level), glFormat.internalFormat,
                    static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight),
                    static_cast<GLsizei>(desc.arrayLayers), 0, glFormat.format, glFormat.type,
                    levelData);
            }
            dataOffset += static_cast<std::size_t>(mipImageSizes[level]) * desc.arrayLayers;
        } else {
            if (IsTextureFormatCompressedUVE(desc.format)) {
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
    glTexParameteri(textureTarget, GL_TEXTURE_MIN_FILTER,
                    desc.mipLevels > 1U ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (isArray) {
        glTexParameteri(textureTarget, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(desc.mipLevels - 1U));
    UVE_GL_CHECK_ERROR_UVE("CreateTextureUVE mip allocation/upload and sampler state");

    m_impl->state.gl.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindTexture(textureTarget, static_cast<GLuint>(previousTextureBinding));

    const std::uint32_t handleValue = m_impl->state.nextTextureHandle++;
    m_impl->state.textures.emplace(handleValue, Detail::GlDeviceStateUVE::TextureRecordUVE{glTexture, desc});
    return TextureHandleUVE{handleValue};
}

bool GlRenderDeviceUVE::SupportsTextureFormatUVE(const TextureFormatUVE format,
                                                  const TextureColorSpaceUVE colorSpace) const noexcept {
    if (!IsUsableUVE() || GetTextureFormatBlockInfoUVE(format).bytes == 0U) {
        return false;
    }
    switch (colorSpace) {
        case TextureColorSpaceUVE::Linear:
            break;
        case TextureColorSpaceUVE::Srgb:
            if (!IsTextureFormatSrgbCapableUVE(format)) {
                return false;
            }
            break;
        default:
            return false;
    }
    if (!IsTextureFormatCompressedUVE(format)) {
        return true;
    }
    if (m_impl->state.gl.glCompressedTexImage2D == nullptr) {
        return false;
    }
    const GLint internalFormat = TextureFormatToGlUVE(format, colorSpace).internalFormat;
    return std::find(m_impl->state.compressedTextureFormats.begin(),
                     m_impl->state.compressedTextureFormats.end(), internalFormat) !=
           m_impl->state.compressedTextureFormats.end();
}

void GlRenderDeviceUVE::DestroyTextureUVE(TextureHandleUVE texture) {
    const auto it = m_impl->state.textures.find(texture.value);
    if (it == m_impl->state.textures.end()) {
        UVE_ERROR("GlRenderDeviceUVE: DestroyTextureUVE called with an unknown or already-destroyed handle ({})",
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

SamplerHandleUVE GlRenderDeviceUVE::CreateSamplerUVE(const SamplerDescUVE& desc) {
    if (!IsSamplerDescValidUVE(desc)) {
        UVE_ERROR("GlRenderDeviceUVE: CreateSamplerUVE received an invalid sampler descriptor");
        return kInvalidSamplerHandleUVE;
    }
    if (m_impl->state.gl.glGenSamplers == nullptr || m_impl->state.gl.glDeleteSamplers == nullptr ||
        m_impl->state.gl.glBindSampler == nullptr || m_impl->state.gl.glSamplerParameteri == nullptr ||
        m_impl->state.gl.glSamplerParameterf == nullptr) {
        UVE_ERROR("GlRenderDeviceUVE: CreateSamplerUVE needs GL 3.3+ sampler objects, "
                  "unavailable on this context");
        return kInvalidSamplerHandleUVE;
    }
    const auto toWrap = [](const SamplerWrapUVE wrap) noexcept {
        switch (wrap) {
            case SamplerWrapUVE::Repeat:         return GL_REPEAT;
            case SamplerWrapUVE::MirroredRepeat: return GL_MIRRORED_REPEAT;
            case SamplerWrapUVE::ClampToEdge:    return GL_CLAMP_TO_EDGE;
        }
        return GL_CLAMP_TO_EDGE; // unreachable; desc was validated
    };
    // GL's MIN_FILTER fuses minification and mip blending into one enum; the desc keeps them
    // separate (Vulkan's shape), so the fusion happens here.
    GLint minFilter = GL_LINEAR_MIPMAP_LINEAR;
    if (desc.mipMode == SamplerMipModeUVE::None) {
        minFilter = desc.minFilter == SamplerFilterUVE::Point ? GL_NEAREST : GL_LINEAR;
    } else if (desc.minFilter == SamplerFilterUVE::Point) {
        minFilter = desc.mipMode == SamplerMipModeUVE::Point ? GL_NEAREST_MIPMAP_NEAREST
                                                             : GL_NEAREST_MIPMAP_LINEAR;
    } else {
        minFilter = desc.mipMode == SamplerMipModeUVE::Point ? GL_LINEAR_MIPMAP_NEAREST
                                                             : GL_LINEAR_MIPMAP_LINEAR;
    }
    GLuint glSampler = 0;
    m_impl->state.gl.glGenSamplers(1, &glSampler);
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_MIN_FILTER, minFilter);
    m_impl->state.gl.glSamplerParameteri(
        glSampler, GL_TEXTURE_MAG_FILTER,
        desc.magFilter == SamplerFilterUVE::Point ? GL_NEAREST : GL_LINEAR);
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_S, toWrap(desc.wrapU));
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_T, toWrap(desc.wrapV));
    m_impl->state.gl.glSamplerParameteri(glSampler, GL_TEXTURE_WRAP_R, toWrap(desc.wrapW));
    if (desc.maxAnisotropy > 1.0F) {
        if (!m_impl->state.samplerAnisotropySupported) {
            if (!m_impl->state.warnedSamplerAnisotropyClamped) {
                m_impl->state.warnedSamplerAnisotropyClamped = true;
                UVE_WARNING("GlRenderDeviceUVE: CreateSamplerUVE without GL_EXT_texture_filter_anisotropic; "
                            "anisotropy clamps to 1.0");
            }
        } else {
            m_impl->state.gl.glSamplerParameterf(
                glSampler, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                std::min(desc.maxAnisotropy, m_impl->state.maxSamplerAnisotropy));
        }
    }
    UVE_GL_CHECK_ERROR_UVE("CreateSamplerUVE sampler state");

    const std::uint32_t handleValue = m_impl->state.nextSamplerHandle++;
    m_impl->state.samplers.emplace(handleValue,
                                   Detail::GlDeviceStateUVE::SamplerRecordUVE{glSampler, desc});
    return SamplerHandleUVE{handleValue};
}

void GlRenderDeviceUVE::DestroySamplerUVE(SamplerHandleUVE sampler) {
    const auto it = m_impl->state.samplers.find(sampler.value);
    if (it == m_impl->state.samplers.end()) {
        UVE_ERROR("GlRenderDeviceUVE: DestroySamplerUVE called with an unknown or already-destroyed handle ({})",
                   sampler.value);
        return;
    }
    // A sampler deleted while bound is implicitly unbound from every unit by the driver
    // (core GL 3.3 semantics) — no unit bookkeeping to repair here.
    m_impl->state.gl.glDeleteSamplers(1, &it->second.glSampler);
    m_impl->state.samplers.erase(it);
}

ShaderHandleUVE GlRenderDeviceUVE::CreateShaderUVE(const ShaderDescUVE& desc, std::string* outInfoLog) {
    if (!IsShaderStageValidUVE(desc.stage)) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Unknown shader stage.";
        }
        UVE_ERROR("GlRenderDeviceUVE: CreateShaderUVE received an unknown shader stage");
        return kInvalidShaderHandleUVE;
    }
#if defined(__ANDROID__)
    if (desc.stage == ShaderStageUVE::Compute || desc.stage == ShaderStageUVE::Geometry) {
        UVE_ERROR("GlRenderDeviceUVE: GLES3 does not support requested compute/geometry shader stage");
        return kInvalidShaderHandleUVE;
    }
#else
    // Geometry shaders are core since GL 3.2, below this engine's desktop baseline, so no gate is
    // needed there - but compute shaders are GL 4.3+ only (Phase 2a), and the negotiated context
    // is not guaranteed to have cleared that bar just because this isn't Android.
    if (desc.stage == ShaderStageUVE::Compute && !m_impl->state.supportsComputeShadersUVE) {
        UVE_ERROR("GlRenderDeviceUVE: compute shaders require an OpenGL 4.3+ context; the negotiated "
                  "context does not support them");
        return kInvalidShaderHandleUVE;
    }
#endif
    if (desc.sourceCode.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Shader source exceeds the GLsizei length range.";
        }
        UVE_ERROR("GlRenderDeviceUVE: shader source exceeds the GLsizei length range");
        return kInvalidShaderHandleUVE;
    }
    const GLenum stage = ShaderStageToGlUVE(desc.stage);
    const GLuint glShader = m_impl->state.gl.glCreateShader(stage);

    const char* sourcePointer = desc.sourceCode.c_str();
    const auto sourceLength = static_cast<GLint>(desc.sourceCode.size());
    m_impl->state.gl.glShaderSource(glShader, 1, &sourcePointer, &sourceLength);
    m_impl->state.gl.glCompileShader(glShader);

    const std::string infoLog = GetShaderInfoLogUVE(m_impl->state.gl, glShader);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint compiled = GL_FALSE;
    m_impl->state.gl.glGetShaderiv(glShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        UVE_ERROR("GlRenderDeviceUVE: CreateShaderUVE compilation failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailureUVE("shader compile failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteShader(glShader);
        return kInvalidShaderHandleUVE;
    }

    const std::uint32_t handleValue = m_impl->state.nextShaderHandle++;
    m_impl->state.shaders.emplace(handleValue,
                                  Detail::GlDeviceStateUVE::ShaderRecordUVE{glShader, desc.stage});
    return ShaderHandleUVE{handleValue};
}

void GlRenderDeviceUVE::DestroyShaderUVE(ShaderHandleUVE shader) {
    const auto it = m_impl->state.shaders.find(shader.value);
    if (it == m_impl->state.shaders.end()) {
        UVE_ERROR("GlRenderDeviceUVE: DestroyShaderUVE called with an unknown or already-destroyed handle ({})",
                   shader.value);
        return;
    }
    m_impl->state.gl.glDeleteShader(it->second.glShader);
    m_impl->state.shaders.erase(it);
}

PipelineHandleUVE GlRenderDeviceUVE::CreatePipelineUVE(const PipelineDescUVE& desc, std::string* outInfoLog) {
    if (!IsVertexLayoutValidUVE(desc.vertexLayout)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown vertex attribute format");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPipelineBlendModeValidUVE(desc.blendMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown blend mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPrimitiveTopologyValidUVE(desc.topology)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown primitive topology");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsCullModeValidUVE(desc.cullMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown cull mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFrontFaceValidUVE(desc.frontFace)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown front-face winding");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFillModeValidUVE(desc.fillMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown fill mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsDepthCompareValidUVE(desc.depthCompare)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE received an unknown depth comparison");
        return kInvalidPipelineHandleUVE;
    }
    if (m_impl->state.maxVertexAttribs <= 0 ||
        desc.vertexLayout.size() > static_cast<std::size_t>(m_impl->state.maxVertexAttribs)) {
        UVE_ERROR("GlRenderDeviceUVE: pipeline vertex layout exceeds GL vertex-attrib limits");
        return kInvalidPipelineHandleUVE;
    }
    if (!desc.vertexLayout.empty() &&
        (!IsVertexLayoutWithinStrideUVE(desc.vertexLayout, desc.vertexStride) || desc.vertexStride == 0U ||
         desc.vertexStride > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE vertex attributes exceed the GL stride range");
        return kInvalidPipelineHandleUVE;
    }
    const auto vertexIt = m_impl->state.shaders.find(desc.vertexShader.value);
    const auto fragmentIt = m_impl->state.shaders.find(desc.fragmentShader.value);
    if (vertexIt == m_impl->state.shaders.end() || fragmentIt == m_impl->state.shaders.end()) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE referenced an unknown vertex or fragment shader handle");
        return kInvalidPipelineHandleUVE;
    }

    const GLuint glProgram = m_impl->state.gl.glCreateProgram();
    m_impl->state.gl.glAttachShader(glProgram, vertexIt->second.glShader);
    m_impl->state.gl.glAttachShader(glProgram, fragmentIt->second.glShader);
    m_impl->state.gl.glLinkProgram(glProgram);

    const std::string infoLog = GetProgramInfoLogUVE(m_impl->state.gl, glProgram);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint linked = GL_FALSE;
    m_impl->state.gl.glGetProgramiv(glProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineUVE link failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailureUVE("program link failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandleUVE;
    }

    GLuint glVao = 0;
    m_impl->state.gl.glGenVertexArrays(1, &glVao);

    Detail::GlDeviceStateUVE::PipelineRecordUVE record;
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
    record.topology = desc.topology;
    ReflectPipelineUniformsUVE(m_impl->state.gl, glProgram, record.uniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandleUVE{handleValue};
}

PipelineHandleUVE GlRenderDeviceUVE::CreateComputePipelineUVE(const ComputePipelineDescUVE& desc,
                                                              std::string* outInfoLog) {
    const auto shaderIt = m_impl->state.shaders.find(desc.computeShader.value);
    if (shaderIt == m_impl->state.shaders.end()) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Unknown compute shader handle.";
        }
        UVE_ERROR("GlRenderDeviceUVE: CreateComputePipelineUVE referenced an unknown shader handle");
        return kInvalidPipelineHandleUVE;
    }
    if (shaderIt->second.stage != ShaderStageUVE::Compute) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Shader handle is not a Compute-stage shader.";
        }
        UVE_ERROR("GlRenderDeviceUVE: CreateComputePipelineUVE requires a Compute-stage shader");
        return kInvalidPipelineHandleUVE;
    }
    // CreateShaderUVE already gated the stage on supportsComputeShadersUVE, but the dispatch pair
    // is loaded separately — re-check here so a Compute-stage GLSL ES shader can never slip a
    // pre-4.3 context into a link it cannot dispatch.
    if (m_impl->state.gl.glDispatchCompute == nullptr || m_impl->state.gl.glMemoryBarrier == nullptr) {
        if (outInfoLog != nullptr) {
            *outInfoLog = "Compute pipelines require an OpenGL 4.3+ context (glDispatchCompute unavailable).";
        }
        UVE_ERROR("GlRenderDeviceUVE: CreateComputePipelineUVE requires an OpenGL 4.3+ context");
        return kInvalidPipelineHandleUVE;
    }

    const GLuint glProgram = m_impl->state.gl.glCreateProgram();
    m_impl->state.gl.glAttachShader(glProgram, shaderIt->second.glShader);
    m_impl->state.gl.glLinkProgram(glProgram);

    const std::string infoLog = GetProgramInfoLogUVE(m_impl->state.gl, glProgram);
    if (outInfoLog != nullptr) {
        *outInfoLog = infoLog;
    }

    GLint linked = GL_FALSE;
    m_impl->state.gl.glGetProgramiv(glProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        UVE_ERROR("GlRenderDeviceUVE: CreateComputePipelineUVE link failed: {}", infoLog);
#if defined(__ANDROID__)
        LogAndroidGlFailureUVE("compute program link failed", infoLog.empty() ? "<empty driver log>" : infoLog.c_str());
#endif
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandleUVE;
    }

    // No VAO for compute records — glDeleteVertexArrays silently ignores name 0 on destroy, and
    // GlCommandBufferUVE skips glBindVertexArray() for isCompute pipelines entirely.
    Detail::GlDeviceStateUVE::PipelineRecordUVE record{
        glProgram, /*glVao=*/0U, {}, /*vertexStride=*/0U, /*depthTestEnabled=*/true,
        /*depthWriteEnabled=*/true, PipelineBlendModeUVE::Opaque, CullModeUVE::None,
        FrontFaceUVE::CounterClockwise, FillModeUVE::Fill, /*depthBiasEnabled=*/false,
        /*depthBiasConstantFactor=*/0.0F, /*depthBiasSlopeFactor=*/0.0F, DepthCompareUVE::Less,
        PrimitiveTopologyUVE::Triangles, /*isCompute=*/true, {}};
    // Active-uniform reflection works identically for compute programs, so SetUniform*UVE on a
    // compute pipeline needs no special-casing.
    ReflectPipelineUniformsUVE(m_impl->state.gl, glProgram, record.uniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandleUVE{handleValue};
}

void GlRenderDeviceUVE::DestroyPipelineUVE(PipelineHandleUVE pipeline) {
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        UVE_ERROR("GlRenderDeviceUVE: DestroyPipelineUVE called with an unknown or already-destroyed handle ({})",
                   pipeline.value);
        return;
    }
    m_impl->state.gl.glDeleteProgram(it->second.glProgram);
    m_impl->state.gl.glDeleteVertexArrays(1, &it->second.glVao);
    m_impl->state.pipelines.erase(it);
}

std::vector<UniformReflectionUVE> GlRenderDeviceUVE::GetPipelineUniformsUVE(PipelineHandleUVE pipeline) const {
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        return {};
    }
    std::vector<UniformReflectionUVE> result;
    result.reserve(it->second.uniforms.size());
    for (const auto& [name, record] : it->second.uniforms) {
        result.push_back(UniformReflectionUVE{name, record.type, record.location, record.arraySize});
    }
    return result;
}

bool GlRenderDeviceUVE::GetPipelineBinaryUVE(PipelineHandleUVE pipeline, std::vector<std::byte>& outBinary,
                                              std::uint32_t& outFormat) const {
    if (m_impl->state.gl.glGetProgramBinary == nullptr || m_impl->state.gl.glProgramBinary == nullptr) {
        UVE_WARNING("GlRenderDeviceUVE: program-binary export is unavailable on this driver; using source compilation");
        return false;
    }
    const auto it = m_impl->state.pipelines.find(pipeline.value);
    if (it == m_impl->state.pipelines.end()) {
        UVE_ERROR("GlRenderDeviceUVE: GetPipelineBinaryUVE called with an unknown handle ({})", pipeline.value);
        return false;
    }

    GLint binaryLength = 0;
    m_impl->state.gl.glGetProgramiv(it->second.glProgram, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0) {
        UVE_WARNING("GlRenderDeviceUVE: GetPipelineBinaryUVE - driver reports no binary available for pipeline {}",
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

PipelineHandleUVE GlRenderDeviceUVE::CreatePipelineFromBinaryUVE(std::span<const std::byte> binary,
                                                                  std::uint32_t format,
                                                                  const PipelineBinaryDescUVE& desc) {
    if (m_impl->state.gl.glGetProgramBinary == nullptr || m_impl->state.gl.glProgramBinary == nullptr) {
        UVE_WARNING("GlRenderDeviceUVE: program-binary import is unavailable on this driver; treating cache as a miss");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsVertexLayoutValidUVE(desc.vertexLayout)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown vertex attribute format");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPipelineBlendModeValidUVE(desc.blendMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown blend mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPrimitiveTopologyValidUVE(desc.topology)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown primitive topology");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsCullModeValidUVE(desc.cullMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown cull mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFrontFaceValidUVE(desc.frontFace)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown front-face winding");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFillModeValidUVE(desc.fillMode)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown fill mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsDepthCompareValidUVE(desc.depthCompare)) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown depth comparison");
        return kInvalidPipelineHandleUVE;
    }
    if (m_impl->state.maxVertexAttribs <= 0 ||
        desc.vertexLayout.size() > static_cast<std::size_t>(m_impl->state.maxVertexAttribs)) {
        UVE_ERROR("GlRenderDeviceUVE: pipeline vertex layout exceeds GL vertex-attrib limits");
        return kInvalidPipelineHandleUVE;
    }
    if (!desc.vertexLayout.empty() &&
        (!IsVertexLayoutWithinStrideUVE(desc.vertexLayout, desc.vertexStride) || desc.vertexStride == 0U ||
         desc.vertexStride > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE vertex attributes exceed the GL stride range");
        return kInvalidPipelineHandleUVE;
    }
    if (binary.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE binary exceeds the GLsizei range");
        return kInvalidPipelineHandleUVE;
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
        UVE_WARNING("GlRenderDeviceUVE: CreatePipelineFromBinaryUVE - driver rejected the supplied binary: {}",
                     GetProgramInfoLogUVE(m_impl->state.gl, glProgram));
        m_impl->state.gl.glDeleteProgram(glProgram);
        return kInvalidPipelineHandleUVE;
    }

    GLuint glVao = 0;
    m_impl->state.gl.glGenVertexArrays(1, &glVao);

    Detail::GlDeviceStateUVE::PipelineRecordUVE record;
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
    record.topology = desc.topology;
    // Uniform locations are not guaranteed portable across a binary load even though behavior
    // is - reflection must always be re-run here, never assumed inherited from the original
    // compile that produced this binary.
    ReflectPipelineUniformsUVE(m_impl->state.gl, glProgram, record.uniforms);

    const std::uint32_t handleValue = m_impl->state.nextPipelineHandle++;
    m_impl->state.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandleUVE{handleValue};
}

std::unique_ptr<ICommandBufferUVE> GlRenderDeviceUVE::CreateCommandBufferUVE() {
    return std::make_unique<GlCommandBufferUVE>(m_impl->state);
}

void GlRenderDeviceUVE::SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) {
    UVE_ASSERT(commandBuffer != nullptr);
    auto* const glCommandBuffer = dynamic_cast<GlCommandBufferUVE*>(commandBuffer.get());
    UVE_ASSERT(glCommandBuffer != nullptr); // only this device's own CreateCommandBufferUVE() ever produces one
    // Every GL call already executed during recording (OpenGL has no separate replay step) — the
    // command buffer is simply released here.
}

void GlRenderDeviceUVE::PresentUVE() {
    if (IsUsableUVE()) {
        m_impl->state.windowManager->SwapBuffersUVE();
    }
}

std::string_view GlRenderDeviceUVE::GetBackendNameUVE() const noexcept {
    return "OpenGL";
}

bool GlRenderDeviceUVE::IsUsableUVE() const noexcept {
    return m_impl != nullptr && m_impl->state.windowManager != nullptr &&
           m_impl->state.windowManager->IsValidUVE() && m_impl->state.gl.IsCompleteUVE();
}

std::size_t GlRenderDeviceUVE::GetLiveResourceCountUVE() const noexcept {
    return m_impl->state.buffers.size() + m_impl->state.textures.size() + m_impl->state.samplers.size() +
           m_impl->state.shaders.size() + m_impl->state.pipelines.size() + m_impl->state.fences.size() +
           m_impl->state.queries.size();
}

std::uint32_t GlRenderDeviceUVE::GetNativeTextureIdUVE(const TextureHandleUVE texture) const noexcept {
    const auto it = m_impl->state.textures.find(texture.value);
    return it == m_impl->state.textures.end() ? 0U : it->second.glTexture;
}

} // namespace UVE::Render

namespace UVE::Render {
namespace {
RenderOperationResultUVE FinishDeviceOperationUVE() {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return RenderOperationResultUVE::Success;
    UVE_ERROR("GL device operation failed (error {})", static_cast<unsigned>(error));
    return RenderOperationResultUVE::SystemError;
}
GLenum OperationTextureTargetUVE(TextureTypeUVE type) {
    if (type == TextureTypeUVE::Cubemap)
        return GL_TEXTURE_CUBE_MAP;
    if (type == TextureTypeUVE::Texture2DArray)
        return GL_TEXTURE_2D_ARRAY;
    return GL_TEXTURE_2D;
}
} // namespace
RenderDeviceCapabilitiesUVE GlRenderDeviceUVE::GetCapabilitiesUVE() const noexcept {
    RenderDeviceCapabilitiesUVE caps;
    if (!IsUsableUVE())
        return caps;
    const auto native = RenderFeatureSupportUVE::Native;
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
    if (m_impl->state.supportsComputeShadersUVE && gl.glMemoryBarrier)
        caps.memoryBarriers = native;
#endif
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
RenderOperationResultUVE GlRenderDeviceUVE::GetTextureDescUVE(TextureHandleUVE texture,
                                                              TextureDescUVE &out) const {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    const auto found = m_impl->state.textures.find(texture.value);
    if (found == m_impl->state.textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    out = found->second.desc;
    return RenderOperationResultUVE::Success;
}
RenderOperationResultUVE GlRenderDeviceUVE::CopyBufferUVE(const BufferCopyDescUVE &copy) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    if (GetCapabilitiesUVE().bufferCopy != RenderFeatureSupportUVE::Native)
        return RenderOperationResultUVE::Unsupported;
    auto &state = m_impl->state;
    const auto source = state.buffers.find(copy.source.value),
               target = state.buffers.find(copy.destination.value);
    if (source == state.buffers.end() || target == state.buffers.end())
        return RenderOperationResultUVE::InvalidHandle;
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<GLintptr>::max());
    if (!IsBufferCopyRangeValidUVE(copy, source->second.sizeBytes, target->second.sizeBytes) ||
        copy.sourceOffset > maximum || copy.destinationOffset > maximum || copy.sizeBytes > maximum)
        return RenderOperationResultUVE::InvalidArgument;
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
    return FinishDeviceOperationUVE();
}
RenderOperationResultUVE GlRenderDeviceUVE::BlitTextureUVE(const TextureBlitDescUVE &blit) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    if (GetCapabilitiesUVE().textureBlit != RenderFeatureSupportUVE::Native)
        return RenderOperationResultUVE::Unsupported;
    auto &state = m_impl->state;
    const auto source = state.textures.find(blit.source.value),
               target = state.textures.find(blit.destination.value);
    if (source == state.textures.end() || target == state.textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (!IsTextureBlitValidUVE(blit, source->second.desc, target->second.desc))
        return RenderOperationResultUVE::InvalidArgument;
    if ((source->second.desc.type == TextureTypeUVE::Texture2DArray ||
         target->second.desc.type == TextureTypeUVE::Texture2DArray) &&
        !state.gl.glFramebufferTextureLayer)
        return RenderOperationResultUVE::Unsupported;
    GLint oldRead = 0, oldDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
#if !defined(__ANDROID__)
    const GLboolean oldSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (target->second.desc.colorSpace == TextureColorSpaceUVE::Srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    const GLboolean oldScissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    GLuint framebuffers[2]{};
    state.gl.glGenFramebuffers(2, framebuffers);
    const auto attach = [&](GLenum binding, const auto &texture, const TextureRegionUVE &region, GLuint fbo) {
        state.gl.glBindFramebuffer(binding, fbo);
        if (texture.desc.type == TextureTypeUVE::Texture2DArray) {
            state.gl.glFramebufferTextureLayer(binding, GL_COLOR_ATTACHMENT0, texture.glTexture,
                                               static_cast<GLint>(region.mip),
                                               static_cast<GLint>(region.layer));
        } else {
            const GLenum type = texture.desc.type == TextureTypeUVE::Cubemap
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
            blit.filter == BlitFilterUVE::Point ? GL_NEAREST : GL_LINEAR);
    }
    state.gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(oldRead));
    state.gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(oldDraw));
    state.gl.glDeleteFramebuffers(2, framebuffers);
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
    const auto result = FinishDeviceOperationUVE();
    return readReady && drawReady ? result : RenderOperationResultUVE::SystemError;
}
RenderOperationResultUVE GlRenderDeviceUVE::GenerateMipmapsUVE(TextureHandleUVE texture) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    if (GetCapabilitiesUVE().mipGeneration != RenderFeatureSupportUVE::Native)
        return RenderOperationResultUVE::Unsupported;
    auto &state = m_impl->state;
    const auto found = state.textures.find(texture.value);
    if (found == state.textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    const auto &desc = found->second.desc;
    if (desc.mipLevels < 2U ||
        (desc.format != TextureFormatUVE::RGBA8Unorm && desc.format != TextureFormatUVE::RGBA16Float))
        return RenderOperationResultUVE::InvalidArgument;
    const GLenum target = OperationTextureTargetUVE(desc.type);
    const GLenum query = desc.type == TextureTypeUVE::Cubemap          ? GL_TEXTURE_BINDING_CUBE_MAP
                         : desc.type == TextureTypeUVE::Texture2DArray ? GL_TEXTURE_BINDING_2D_ARRAY
                                                                       : GL_TEXTURE_BINDING_2D;
    GLint previous = 0;
    glGetIntegerv(query, &previous);
    glBindTexture(target, found->second.glTexture);
    state.gl.glGenerateMipmap(target);
    glBindTexture(target, static_cast<GLuint>(previous));
    return FinishDeviceOperationUVE();
}
RenderOperationResultUVE GlRenderDeviceUVE::InsertFenceUVE(FenceHandleUVE &output) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    if (GetCapabilitiesUVE().fences != RenderFeatureSupportUVE::Native)
        return RenderOperationResultUVE::Unsupported;
    auto &state = m_impl->state;
    if (state.nextFence == 0U)
        return RenderOperationResultUVE::SystemError;
    const FenceHandleUVE handle{state.nextFence++};
    auto [entry, inserted] = state.fences.emplace(handle.value, nullptr);
    static_cast<void>(inserted);
    entry->second = state.gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
    if (entry->second == nullptr) {
        state.fences.erase(entry);
        return RenderOperationResultUVE::SystemError;
    }
    output = handle;
    return RenderOperationResultUVE::Success;
}
RenderOperationResultUVE GlRenderDeviceUVE::WaitFenceUVE(FenceHandleUVE handle, std::uint64_t timeoutNs) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.fences.find(handle.value);
    if (found == state.fences.end())
        return RenderOperationResultUVE::InvalidHandle;
    const GLenum result = state.gl.glClientWaitSync(found->second, GL_SYNC_FLUSH_COMMANDS_BIT, timeoutNs);
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
        return RenderOperationResultUVE::Success;
    if (result == GL_TIMEOUT_EXPIRED)
        return RenderOperationResultUVE::TimedOut;
    return RenderOperationResultUVE::SystemError;
}
RenderOperationResultUVE GlRenderDeviceUVE::DestroyFenceUVE(FenceHandleUVE handle) {
    const auto waited = WaitFenceUVE(handle, 0U);
    if (waited == RenderOperationResultUVE::TimedOut)
        return RenderOperationResultUVE::NotReady;
    if (waited != RenderOperationResultUVE::Success)
        return waited;
    m_impl->state.gl.glDeleteSync(m_impl->state.fences.at(handle.value));
    m_impl->state.fences.erase(handle.value);
    return FinishDeviceOperationUVE();
}
RenderOperationResultUVE GlRenderDeviceUVE::WaitIdleUVE() {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    glFinish();
    return FinishDeviceOperationUVE();
}
RenderOperationResultUVE GlRenderDeviceUVE::CreateQueryUVE(RenderQueryKindUVE kind, QueryHandleUVE &output) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    if (kind != RenderQueryKindUVE::Timestamp && kind != RenderQueryKindUVE::AnySamplesPassed)
        return RenderOperationResultUVE::InvalidArgument;
    const auto caps = GetCapabilitiesUVE();
    if ((kind == RenderQueryKindUVE::Timestamp ? caps.timestampQueries : caps.occlusionQueries) !=
        RenderFeatureSupportUVE::Native)
        return RenderOperationResultUVE::Unsupported;
    auto &state = m_impl->state;
    if (state.nextQuery == 0U)
        return RenderOperationResultUVE::SystemError;
    const QueryHandleUVE handle{state.nextQuery++};
    auto [entry, inserted] =
        state.queries.emplace(handle.value, Detail::GlDeviceStateUVE::QueryRecordUVE{0U, kind, false});
    static_cast<void>(inserted);
    state.gl.glGenQueries(1, &entry->second.native);
    if (entry->second.native == 0U) {
        state.queries.erase(entry);
        return RenderOperationResultUVE::SystemError;
    }
    output = handle;
    return RenderOperationResultUVE::Success;
}
RenderOperationResultUVE GlRenderDeviceUVE::BeginQueryUVE(QueryHandleUVE handle) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::AnySamplesPassed)
        return RenderOperationResultUVE::InvalidArgument;
    GLint active = 0;
    state.gl.glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &active);
    if (active != 0 || state.activeOcclusion != 0U)
        return RenderOperationResultUVE::InvalidState;
    state.gl.glBeginQuery(GL_ANY_SAMPLES_PASSED, found->second.native);
    const auto result = FinishDeviceOperationUVE();
    if (result == RenderOperationResultUVE::Success) {
        state.activeOcclusion = handle.value;
        found->second.issued = false;
    }
    return result;
}
RenderOperationResultUVE GlRenderDeviceUVE::EndQueryUVE(QueryHandleUVE handle) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (state.activeOcclusion != handle.value)
        return RenderOperationResultUVE::InvalidState;
    state.gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
    state.activeOcclusion = 0U;
    const auto result = FinishDeviceOperationUVE();
    found->second.issued = result == RenderOperationResultUVE::Success;
    return result;
}
RenderOperationResultUVE GlRenderDeviceUVE::WriteTimestampUVE(QueryHandleUVE handle) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::Timestamp)
        return RenderOperationResultUVE::InvalidArgument;
#if defined(__ANDROID__)
    return RenderOperationResultUVE::Unsupported;
#else
    state.gl.glQueryCounter(found->second.native, GL_TIMESTAMP);
    const auto result = FinishDeviceOperationUVE();
    found->second.issued = result == RenderOperationResultUVE::Success;
    return result;
#endif
}
RenderOperationResultUVE GlRenderDeviceUVE::GetQueryResultUVE(QueryHandleUVE handle,
                                                              RenderQueryResultUVE &output, bool wait) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (!found->second.issued || state.activeOcclusion == handle.value)
        return RenderOperationResultUVE::NotReady;
    GLuint available = GL_FALSE;
    state.gl.glGetQueryObjectuiv(found->second.native, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!wait && available == GL_FALSE)
        return RenderOperationResultUVE::NotReady;
    RenderQueryResultUVE candidate{found->second.kind, 0U};
    if (found->second.kind == RenderQueryKindUVE::AnySamplesPassed) {
        GLuint value = 0U;
        state.gl.glGetQueryObjectuiv(found->second.native, GL_QUERY_RESULT, &value);
        candidate.value = value != 0U ? 1U : 0U;
    } else {
#if defined(__ANDROID__)
        return RenderOperationResultUVE::Unsupported;
#else
        GLuint64 value = 0U;
        state.gl.glGetQueryObjectui64v(found->second.native, GL_QUERY_RESULT, &value);
        candidate.value = value;
#endif
    }
    const auto result = FinishDeviceOperationUVE();
    if (result == RenderOperationResultUVE::Success)
        output = candidate;
    return result;
}
RenderOperationResultUVE GlRenderDeviceUVE::DestroyQueryUVE(QueryHandleUVE handle) {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    auto &state = m_impl->state;
    const auto found = state.queries.find(handle.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (state.activeOcclusion == handle.value)
        return RenderOperationResultUVE::InvalidState;
    state.gl.glDeleteQueries(1, &found->second.native);
    state.queries.erase(found);
    return FinishDeviceOperationUVE();
}
RenderOperationResultUVE GlRenderDeviceUVE::GetSurfaceInfoUVE(RenderSurfaceInfoUVE &output) const {
    if (!IsUsableUVE())
        return RenderOperationResultUVE::DeviceUnavailable;
    RenderSurfaceInfoUVE candidate;
    candidate.width = m_impl->state.windowManager->GetWidthUVE();
    candidate.height = m_impl->state.windowManager->GetHeightUVE();
    candidate.drawable = candidate.width != 0U && candidate.height != 0U;
#if !defined(__ANDROID__)
    GLboolean doubleBuffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
    candidate.imageCount = doubleBuffered ? 2U : 1U;
#endif
    output = candidate;
    return RenderOperationResultUVE::Success;
}
} // namespace UVE::Render
