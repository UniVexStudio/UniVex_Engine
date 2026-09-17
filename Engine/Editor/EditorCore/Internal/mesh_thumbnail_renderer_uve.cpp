// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/editor/mesh_thumbnail_renderer_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// See uve/rhi_opengl/gl_functions_uve.h for why glext.h is safe to include here: it only
// supplies Khronos PFNGL*PROC typedefs and GL_* constants, never a loader library. The GL proc
// loading itself is NOT hand-rolled here anymore - this class shares the engine's one contract
// loader (AUDIT 5.6, EditorCore half). The GL surface stays confined to this .cpp: the public
// header below never includes a GL header.
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include "uve/rhi_opengl/gl_functions_uve.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Editor {

namespace {

constexpr float kFieldOfViewYRadiansUVE = 0.6981317F; // 40 degrees
constexpr float kCameraDistanceMarginUVE = 1.35F;
constexpr float kMinimumBoundingRadiusUVE = 0.25F;
constexpr float kNearPlaneUVE = 0.05F;

// Fixed default viewing angle (a gentle 3/4 elevated view) and a single fixed key light -
// deliberately not derived from the mesh's own assigned material (there may not be one; see this
// class's header comment), matching a generic asset-browser preview rather than an accurate
// material render.
constexpr Math::Vector3UVE kBaseColorUVE{0.65F, 0.68F, 0.72F};

constexpr const char* kVertexShaderSourceUVE = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProjection;
out vec3 vNormal;
void main() {
    vNormal = aNormal;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";

constexpr const char* kFragmentShaderSourceUVE = R"GLSL(#version 450 core
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uLightDirection;
uniform vec3 uBaseColor;
void main() {
    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, -uLightDirection), 0.0);
    FragColor = vec4(uBaseColor * (0.35 + 0.65 * diffuse), 1.0);
}
)GLSL";

void SetGlCapabilityUVE(const GLenum capability, const GLboolean enabled) noexcept {
    if (enabled == GL_TRUE) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

/// Bridges GLFW's proc-address lookup (GLFWglproc, a void(*)(void)) to the shared GL loader's
/// void*-based signature - the same one-line adapter GlRenderDeviceUVE passes (see
/// gl_render_device_uve.cpp); all actual per-function loading lives in gl_functions_uve.cpp.
[[nodiscard]] void* GrabGlProcAddressUVE(const char* const name) noexcept {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

} // namespace

/// The GL function pointers, compiled shader program, and scratch render target this renderer
/// needs, kept out of the public header (see the header's class doc comment). Hand-rolled loader
/// scoped to exactly what a single-mesh, no-material preview render needs - mirrors
/// engine/render/src/gl_functions_uve.h's precedent rather than depending on it, since that header
/// is private to engine/render and this class is deliberately independent of engine/render.
/// The shared GL proc table (see uve/rhi_opengl/gl_functions_uve.h), compiled shader program, and
/// scratch render target this renderer needs, kept out of the public header (see the header's class
/// doc comment). Historical note: this class used to keep its own hand-rolled loader struct here -
/// collapsed into the engine's one shared contract (AUDIT 5.6); only the genuinely preview-specific
/// state below remains local.
struct MeshThumbnailRendererUVE::GlStateUVE {
    Render::Detail::GlFunctionsUVE table;

    bool functionsLoaded = false;

    GLuint shaderProgram = 0U;
    GLint viewProjectionUniform = -1;
    GLint lightDirectionUniform = -1;
    GLint baseColorUniform = -1;
    bool programLinked = false;

    GLuint scratchFramebuffer = 0U;
    GLuint scratchDepthRenderbuffer = 0U;
    int scratchWidth = 0;
    int scratchHeight = 0;
};

namespace {

[[nodiscard]] bool CompileShaderStageUVE(MeshThumbnailRendererUVE::GlStateUVE& gl, const GLenum stage,
                                          const char* const source, GLuint& outShader) noexcept {
    outShader = gl.table.glCreateShader(stage);
    if (outShader == 0U) {
        return false;
    }
    gl.table.glShaderSource(outShader, 1, &source, nullptr);
    gl.table.glCompileShader(outShader);
    GLint compiled = GL_FALSE;
    gl.table.glGetShaderiv(outShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        gl.table.glDeleteShader(outShader);
        outShader = 0U;
        return false;
    }
    return true;
}

/// Creates (once) and resizes (as needed) the scratch framebuffer + depth-renderbuffer that every
/// RenderThumbnailUVE() call reuses. The depth renderbuffer is attached exactly once, right after
/// creation - resizing only calls glRenderbufferStorage, which preserves the object's id and thus
/// its existing attachment. Leaves GL_FRAMEBUFFER/GL_RENDERBUFFER bound to the scratch objects;
/// the caller is responsible for saving/restoring those bindings around the whole render.
[[nodiscard]] bool EnsureScratchTargetUVE(MeshThumbnailRendererUVE::GlStateUVE& gl, const int width,
                                           const int height) noexcept {
    const bool firstTime = gl.scratchFramebuffer == 0U;
    if (firstTime) {
        gl.table.glGenFramebuffers(1, &gl.scratchFramebuffer);
        gl.table.glGenRenderbuffers(1, &gl.scratchDepthRenderbuffer);
        if (gl.scratchFramebuffer == 0U || gl.scratchDepthRenderbuffer == 0U) {
            return false;
        }
    }
    if (firstTime || gl.scratchWidth != width || gl.scratchHeight != height) {
        gl.table.glBindRenderbuffer(GL_RENDERBUFFER, gl.scratchDepthRenderbuffer);
        gl.table.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        gl.scratchWidth = width;
        gl.scratchHeight = height;
    }
    if (firstTime) {
        gl.table.glBindFramebuffer(GL_FRAMEBUFFER, gl.scratchFramebuffer);
        gl.table.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      gl.scratchDepthRenderbuffer);
    }
    return true;
}

} // namespace

MeshThumbnailRendererUVE::MeshThumbnailRendererUVE() = default;
MeshThumbnailRendererUVE::~MeshThumbnailRendererUVE() = default;

void MeshThumbnailRendererUVE::InitializeUVE() {
    m_gl = std::make_unique<GlStateUVE>();
    GlStateUVE& gl = *m_gl;

    gl.table = Render::Detail::LoadGlFunctionsUVE(&GrabGlProcAddressUVE);
    gl.functionsLoaded = gl.table.IsCompleteUVE();
    if (!gl.functionsLoaded) {
        return;
    }

    GLuint vertexShader = 0U;
    GLuint fragmentShader = 0U;
    const bool vertexCompiled = CompileShaderStageUVE(gl, GL_VERTEX_SHADER, kVertexShaderSourceUVE, vertexShader);
    const bool fragmentCompiled =
        vertexCompiled && CompileShaderStageUVE(gl, GL_FRAGMENT_SHADER, kFragmentShaderSourceUVE, fragmentShader);
    if (!fragmentCompiled) {
        if (vertexShader != 0U) {
            gl.table.glDeleteShader(vertexShader);
        }
        return;
    }

    gl.shaderProgram = gl.table.glCreateProgram();
    gl.table.glAttachShader(gl.shaderProgram, vertexShader);
    gl.table.glAttachShader(gl.shaderProgram, fragmentShader);
    gl.table.glLinkProgram(gl.shaderProgram);
    GLint linked = GL_FALSE;
    gl.table.glGetProgramiv(gl.shaderProgram, GL_LINK_STATUS, &linked);
    gl.table.glDeleteShader(vertexShader);
    gl.table.glDeleteShader(fragmentShader);
    if (linked == GL_FALSE) {
        gl.table.glDeleteProgram(gl.shaderProgram);
        gl.shaderProgram = 0U;
        return;
    }

    gl.viewProjectionUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uViewProjection");
    gl.lightDirectionUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uLightDirection");
    gl.baseColorUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uBaseColor");
    gl.programLinked = true;
}

void MeshThumbnailRendererUVE::ShutdownUVE() noexcept {
    if (!m_gl) {
        return;
    }
    GlStateUVE& gl = *m_gl;
    if (gl.functionsLoaded) {
        if (gl.scratchDepthRenderbuffer != 0U) {
            gl.table.glDeleteRenderbuffers(1, &gl.scratchDepthRenderbuffer);
            gl.scratchDepthRenderbuffer = 0U;
        }
        if (gl.scratchFramebuffer != 0U) {
            gl.table.glDeleteFramebuffers(1, &gl.scratchFramebuffer);
            gl.scratchFramebuffer = 0U;
        }
        if (gl.shaderProgram != 0U) {
            gl.table.glDeleteProgram(gl.shaderProgram);
            gl.shaderProgram = 0U;
        }
    }
    gl.programLinked = false;
    m_gl.reset();
}

std::uintptr_t MeshThumbnailRendererUVE::RenderThumbnailUVE(const Asset::MeshAssetUVE& mesh, const int width,
                                                             const int height) {
    if (!m_gl || !m_gl->functionsLoaded || !m_gl->programLinked) {
        return 0U;
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || width <= 0 || height <= 0) {
        return 0U;
    }
    GlStateUVE& gl = *m_gl;

    const Math::Vector3UVE center = mesh.localBounds.GetCenterUVE();
    const Math::Vector3UVE extents = mesh.localBounds.GetExtentsUVE();
    const float boundingRadius =
        std::max(kMinimumBoundingRadiusUVE,
                 std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z));
    const float distance = (boundingRadius / std::sin(kFieldOfViewYRadiansUVE * 0.5F)) * kCameraDistanceMarginUVE;

    const Math::Vector3UVE viewDirection = Math::NormalizeUVE(Math::Vector3UVE{1.0F, 0.85F, 1.0F});
    const Math::Vector3UVE cameraPosition = center + viewDirection * distance;

    Math::QuaternionUVE cameraRotation{};
    if (!Math::TryMakeLookAtUVE(cameraPosition - center, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, cameraRotation)) {
        return 0U;
    }

    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(cameraPosition, cameraRotation);
    const float farPlane = distance + boundingRadius * 2.0F + kNearPlaneUVE;
    const Math::Matrix4x4UVE projection = Math::Matrix4x4UVE::PerspectiveUVE(
        kFieldOfViewYRadiansUVE, static_cast<float>(width) / static_cast<float>(height), kNearPlaneUVE, farPlane);
    const Math::Matrix4x4UVE viewProjection = projection * view;
    const Math::Vector3UVE lightDirection = Math::NormalizeUVE(Math::Vector3UVE{-0.5F, -1.0F, -0.35F});

    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    GLint previousVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    GLint previousElementArrayBuffer = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementArrayBuffer);
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    GLfloat previousClearColor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    bool succeeded = EnsureScratchTargetUVE(gl, width, height);

    GLuint colorTexture = 0U;
    if (succeeded) {
        glGenTextures(1, &colorTexture);
        succeeded = colorTexture != 0U;
    }
    if (succeeded) {
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        gl.table.glBindFramebuffer(GL_FRAMEBUFFER, gl.scratchFramebuffer);
        gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        succeeded = gl.table.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    GLuint vao = 0U;
    GLuint vertexBuffer = 0U;
    GLuint indexBuffer = 0U;
    if (succeeded) {
        glViewport(0, 0, width, height);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        SetGlCapabilityUVE(GL_DEPTH_TEST, GL_TRUE);
        glDepthMask(GL_TRUE);
        SetGlCapabilityUVE(GL_CULL_FACE, GL_FALSE);
        SetGlCapabilityUVE(GL_BLEND, GL_FALSE);
        SetGlCapabilityUVE(GL_SCISSOR_TEST, GL_FALSE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl.table.glGenVertexArrays(1, &vao);
        gl.table.glBindVertexArray(vao);
        gl.table.glGenBuffers(1, &vertexBuffer);
        gl.table.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        gl.table.glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Asset::MeshVertexUVE)),
                         mesh.vertices.data(), GL_STREAM_DRAW);
        gl.table.glGenBuffers(1, &indexBuffer);
        gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        gl.table.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(),
                         GL_STREAM_DRAW);
        gl.table.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Asset::MeshVertexUVE),
                                  reinterpret_cast<const void*>(offsetof(Asset::MeshVertexUVE, position)));
        gl.table.glEnableVertexAttribArray(0);
        gl.table.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Asset::MeshVertexUVE),
                                  reinterpret_cast<const void*>(offsetof(Asset::MeshVertexUVE, normal)));
        gl.table.glEnableVertexAttribArray(1);

        gl.table.glUseProgram(gl.shaderProgram);
        gl.table.glUniformMatrix4fv(gl.viewProjectionUniform, 1, GL_TRUE, &viewProjection.m[0][0]);
        gl.table.glUniform3fv(gl.lightDirectionUniform, 1, &lightDirection.x);
        gl.table.glUniform3fv(gl.baseColorUniform, 1, &kBaseColorUVE.x);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        succeeded = glGetError() == GL_NO_ERROR;

        gl.table.glBindVertexArray(0);
    }

    if (vertexBuffer != 0U) {
        gl.table.glDeleteBuffers(1, &vertexBuffer);
    }
    if (indexBuffer != 0U) {
        gl.table.glDeleteBuffers(1, &indexBuffer);
    }
    if (vao != 0U) {
        gl.table.glDeleteVertexArrays(1, &vao);
    }
    if (colorTexture != 0U) {
        gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }

    gl.table.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    gl.table.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    gl.table.glUseProgram(static_cast<GLuint>(previousProgram));
    gl.table.glBindVertexArray(static_cast<GLuint>(previousVao));
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    SetGlCapabilityUVE(GL_DEPTH_TEST, depthTestWasEnabled);
    SetGlCapabilityUVE(GL_CULL_FACE, cullFaceWasEnabled);
    SetGlCapabilityUVE(GL_BLEND, blendWasEnabled);
    SetGlCapabilityUVE(GL_SCISSOR_TEST, scissorWasEnabled);
    glDepthMask(previousDepthMask);
    glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);

    if (!succeeded) {
        if (colorTexture != 0U) {
            glDeleteTextures(1, &colorTexture);
        }
        return 0U;
    }
    return static_cast<std::uintptr_t>(colorTexture);
}

} // namespace UVE::Editor
