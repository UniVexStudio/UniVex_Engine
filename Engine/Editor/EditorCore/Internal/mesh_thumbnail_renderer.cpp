// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mesh_thumbnail_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// See uve/rhi_opengl/gl_functions_uve.h for why glext.h is safe to include here: it only
// supplies Khronos PFNGL*PROC typedefs and GL_* constants, never a loader library. The GL proc
// loading itself is NOT hand-rolled here anymore - this class shares the engine's one contract
// loader (AUDIT 5.6, EditorCore half). The GL surface stays confined to this .cpp: the public
// header below never includes a GL header.
#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <cstring>
#include <dlfcn.h>
#include <string>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>
#endif

#include "gl_functions.h"
#include "editor_ui_assets.h"

#include "mesh_asset.h"
#include "material_thumbnail_renderer.h"
#include "aabb.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Editor {

namespace {

constexpr float kFieldOfViewYRadiansUVE = 0.6981317F; // 40 degrees
constexpr float kCameraDistanceMarginUVE = 1.06F;
// Only a guard against a mesh with no extent at all: a small model (a 2 cm bolt, a coin) is framed
// to fill the card like any other, rather than shown as a dot in a quarter-metre view.
constexpr float kMinimumBoundingRadiusUVE = 1.0e-4F;
// The near plane follows the camera, which follows the mesh's size, so a small mesh is never cut
// by a fixed near plane and a large one keeps the same depth precision.
constexpr float kNearPlaneFractionUVE = 0.05F;

// Default neutral tone when no texture is available.
constexpr Math::Vector3UVE kBaseColorUVE{0.70F, 0.72F, 0.76F};

constexpr const char* kVertexShaderSourceUVE = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vWorldPosition;
void main() {
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    vWorldPosition = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";

constexpr const char* kFragmentShaderSourceUVE = R"GLSL(#version 450 core
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vWorldPosition;
out vec4 FragColor;
uniform vec3 uLightDirection;
uniform vec3 uBaseColor;
uniform bool uHasAlbedoTexture;
uniform sampler2D uAlbedoTexture;
uniform vec3 uCameraPosition;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 albedo = uBaseColor;
    if (uHasAlbedoTexture) {
        vec4 sampled = texture(uAlbedoTexture, vTexCoord);
        if (sampled.a < 0.05) {
            discard;
        }
        albedo = uBaseColor * sampled.rgb;
    }

    // One light: the sun, warm white, with only the sky's soft fill around it (no fill or rim lights).
    vec3 sunDir = normalize(-uLightDirection);
    float sun = max(dot(normal, sunDir), 0.0);
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);

    float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(vec3(0.10, 0.10, 0.11), vec3(0.24, 0.25, 0.27), hemi);

    vec3 halfVec = normalize(sunDir + viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 32.0) * 0.18 * step(0.0, dot(normal, sunDir));

    vec3 litColor = albedo * (sky + sun * vec3(1.05, 1.0, 0.94)) + vec3(spec);

    // ACES filmic tonemapping for vivid, crisp rendering
    litColor = litColor / (litColor + vec3(0.12)) * 1.12;
    FragColor = vec4(clamp(litColor, 0.0, 1.0), 1.0);
}
)GLSL";

constexpr const char* kBoneVertexShaderSourceUVE = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main() {
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";

constexpr const char* kBoneFragmentShaderSourceUVE = R"GLSL(#version 450 core
out vec4 FragColor;
uniform vec4 uBoneColor;
void main() {
    FragColor = uBoneColor;
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
#if defined(__ANDROID__)
    void* symbol = reinterpret_cast<void*>(eglGetProcAddress(name));
    return symbol != nullptr ? symbol : dlsym(RTLD_DEFAULT, name);
#else
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
#endif
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
    GLint hasAlbedoTextureUniform = -1;
    GLint albedoTextureUniform = -1;
    GLint cameraPositionUniform = -1;
    bool programLinked = false;

    GLuint boneShaderProgram = 0U;
    GLint boneViewProjectionUniform = -1;
    GLint boneColorUniform = -1;
    bool boneProgramLinked = false;

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
#if defined(__ANDROID__)
    std::string translated;
    const char* effectiveSource = source;
    if (source != nullptr && std::strncmp(source, "#version ", 9) == 0 && std::strstr(source, "300 es") == nullptr) {
        if (const char* newline = std::strchr(source, '\n')) {
            translated = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
            translated += (newline + 1);
            effectiveSource = translated.c_str();
        }
    }
    gl.table.glShaderSource(outShader, 1, &effectiveSource, nullptr);
#else
    gl.table.glShaderSource(outShader, 1, &source, nullptr);
#endif
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
    gl.hasAlbedoTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uHasAlbedoTexture");
    gl.albedoTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uAlbedoTexture");
    gl.cameraPositionUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uCameraPosition");
    gl.programLinked = true;

    GLuint boneVertexShader = 0U;
    GLuint boneFragmentShader = 0U;
    if (CompileShaderStageUVE(gl, GL_VERTEX_SHADER, kBoneVertexShaderSourceUVE, boneVertexShader) &&
        CompileShaderStageUVE(gl, GL_FRAGMENT_SHADER, kBoneFragmentShaderSourceUVE, boneFragmentShader)) {
        gl.boneShaderProgram = gl.table.glCreateProgram();
        gl.table.glAttachShader(gl.boneShaderProgram, boneVertexShader);
        gl.table.glAttachShader(gl.boneShaderProgram, boneFragmentShader);
        gl.table.glLinkProgram(gl.boneShaderProgram);
        GLint boneLinked = GL_FALSE;
        gl.table.glGetProgramiv(gl.boneShaderProgram, GL_LINK_STATUS, &boneLinked);
        gl.table.glDeleteShader(boneVertexShader);
        gl.table.glDeleteShader(boneFragmentShader);
        if (boneLinked == GL_TRUE) {
            gl.boneViewProjectionUniform = gl.table.glGetUniformLocation(gl.boneShaderProgram, "uViewProjection");
            gl.boneColorUniform = gl.table.glGetUniformLocation(gl.boneShaderProgram, "uBoneColor");
            gl.boneProgramLinked = true;
        } else {
            gl.table.glDeleteProgram(gl.boneShaderProgram);
            gl.boneShaderProgram = 0U;
        }
    }
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
        if (gl.boneShaderProgram != 0U) {
            gl.table.glDeleteProgram(gl.boneShaderProgram);
            gl.boneShaderProgram = 0U;
        }
    }
    gl.programLinked = false;
    gl.boneProgramLinked = false;
    m_gl.reset();
}

std::uintptr_t MeshThumbnailRendererUVE::RenderThumbnailUVE(const Asset::MeshAssetUVE& mesh, const int width,
                                                             const int height,
                                                             const MaterialThumbnailImageUVE* albedoTexture) {
    // One look for every slot (and for a mesh with no slots).
    MeshThumbnailLookUVE look;
    if (albedoTexture != nullptr) {
        look.colour = {1.0F, 1.0F, 1.0F};
        look.albedo = albedoTexture;
    }
    const std::size_t slots = std::max<std::size_t>(1U, mesh.materialGuids.size());
    return RenderThumbnailUVE(mesh, width, height, std::vector<MeshThumbnailLookUVE>(slots, look));
}

std::uintptr_t MeshThumbnailRendererUVE::RenderThumbnailUVE(const Asset::MeshAssetUVE& mesh, const int width,
                                                             const int height,
                                                             const std::vector<MeshThumbnailLookUVE>& looksBySlot,
                                                             const std::vector<MeshThumbnailBoneUVE>& bones,
                                                             const float* const viewDirectionIn) {
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
    // A little from the right and above, mostly from the front.
    Math::Vector3UVE viewDirection = Math::NormalizeUVE(Math::Vector3UVE{0.38F, 0.22F, 1.0F});
    if (viewDirectionIn != nullptr) {
        const Math::Vector3UVE asked{viewDirectionIn[0], viewDirectionIn[1], viewDirectionIn[2]};
        // Straight up or down has no "up" to look with: tipped a little off the vertical.
        if (std::abs(asked.y) < 0.995F && (asked.x != 0.0F || asked.z != 0.0F)) {
            viewDirection = Math::NormalizeUVE(asked);
        }
    }
    // Framed on the model itself, not on a sphere around it: the camera stands just far enough
    // back that every corner of the box fits the picture, so the model fills the tile.
    float distance = 0.0F;
    {
        const Math::Vector3UVE forward = viewDirection * -1.0F;
        const Math::Vector3UVE side = Math::NormalizeUVE(Math::CrossUVE(forward, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
        const Math::Vector3UVE upward = Math::CrossUVE(side, forward);
        const float tanHalf = std::tan(kFieldOfViewYRadiansUVE * 0.5F);
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        for (int corner = 0; corner < 8; ++corner) {
            const Math::Vector3UVE d{(corner & 1) != 0 ? extents.x : -extents.x, (corner & 2) != 0 ? extents.y : -extents.y,
                                     (corner & 4) != 0 ? extents.z : -extents.z};
            const float x = std::abs(Math::DotUVE(d, side));
            const float y = std::abs(Math::DotUVE(d, upward));
            const float z = Math::DotUVE(d, viewDirection);
            distance = std::max({distance, z + x / (tanHalf * aspect), z + y / tanHalf});
        }
        distance = std::max(distance * kCameraDistanceMarginUVE, boundingRadius * 0.5F);
    }
    const Math::Vector3UVE cameraPosition = center + viewDirection * distance;

    Math::QuaternionUVE cameraRotation{};
    if (!Math::TryMakeLookAtUVE(cameraPosition - center, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, cameraRotation)) {
        return 0U;
    }

    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(cameraPosition, cameraRotation);
    const float nearPlane = distance * kNearPlaneFractionUVE;
    const float farPlane = distance + boundingRadius * 2.0F + nearPlane;
    Math::Matrix4x4UVE projection = Math::Matrix4x4UVE::PerspectiveUVE(
        kFieldOfViewYRadiansUVE, static_cast<float>(width) / static_cast<float>(height), nearPlane, farPlane);
    // Invert Y in projection so OpenGL's bottom-up FBO maps right-side up to the UI's top-down (0,0)-(1,1) UV coordinates.
    projection.m[1][1] = -projection.m[1][1];
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

    // Each distinct albedo map uploaded once.
    std::vector<std::pair<const MaterialThumbnailImageUVE*, GLuint>> albedoTextures;
    const auto textureOf = [&albedoTextures](const MaterialThumbnailImageUVE* image) -> GLuint {
        if (image == nullptr || !image->IsValidUVE()) {
            return 0U;
        }
        for (const auto& [known, id] : albedoTextures) {
            if (known == image) {
                return id;
            }
        }
        GLuint id = 0U;
        glGenTextures(1, &id);
        if (id != 0U) {
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->rgba8.data());
        }
        albedoTextures.emplace_back(image, id);
        return id;
    };

    GLuint vao = 0U;
    GLuint vertexBuffer = 0U;
    GLuint indexBuffer = 0U;
    if (succeeded) {
        glViewport(0, 0, width, height);
        // Transparent background: the preview is the mesh alone, composited over whatever tile shows it.
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
        gl.table.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Asset::MeshVertexUVE),
                                  reinterpret_cast<const void*>(offsetof(Asset::MeshVertexUVE, u)));
        gl.table.glEnableVertexAttribArray(2);

        gl.table.glUseProgram(gl.shaderProgram);
        gl.table.glUniformMatrix4fv(gl.viewProjectionUniform, 1, GL_TRUE, &viewProjection.m[0][0]);
        gl.table.glUniform3fv(gl.lightDirectionUniform, 1, &lightDirection.x);
        gl.table.glUniform3fv(gl.cameraPositionUniform, 1, &cameraPosition.x);
        gl.table.glUniform1i(gl.albedoTextureUniform, 0);
        // Each surface in its own material's look; see-through ones are left out.
        for (std::size_t index = 0U; index < mesh.SurfaceCountUVE(); ++index) {
            const Asset::MeshSurfaceUVE surface = mesh.GetSurfaceUVE(index);
            if (surface.indexCount == 0U || static_cast<std::size_t>(surface.firstIndex) + surface.indexCount > mesh.indices.size()) {
                continue;
            }
            MeshThumbnailLookUVE look;
            look.colour = {kBaseColorUVE.x, kBaseColorUVE.y, kBaseColorUVE.z};
            if (surface.materialSlot < looksBySlot.size()) {
                look = looksBySlot[surface.materialSlot];
            }
            if (look.skip) {
                continue;
            }
            gl.table.glUniform3fv(gl.baseColorUniform, 1, look.colour.data());
            const GLuint texture = textureOf(look.albedo);
            if (texture != 0U) {
                // glActiveTexture is a GL 1.3 entry point: Windows' opengl32 <GL/gl.h> only declares
                // GL 1.1, so it must come from the shared loaded proc table like every other post-1.1 call.
                gl.table.glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
            }
            gl.table.glUniform1i(gl.hasAlbedoTextureUniform, texture != 0U ? 1 : 0);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(surface.indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(static_cast<std::uintptr_t>(surface.firstIndex) * sizeof(std::uint32_t)));
        }
        succeeded = glGetError() == GL_NO_ERROR;

        gl.table.glBindVertexArray(0);

        // The skeleton over the model, as the viewport draws it: a slim outline from each joint to
        // the next, in light strokes, never hiding the model.
        if (succeeded && !bones.empty() && gl.boneProgramLinked) {
            const Math::Vector3UVE forward = viewDirection * -1.0F;
            std::vector<Math::Vector3UVE> lines;
            lines.reserve(bones.size() * 16U);
            for (const MeshThumbnailBoneUVE& bone : bones) {
                const Math::Vector3UVE head{bone.head[0], bone.head[1], bone.head[2]};
                const Math::Vector3UVE tail{bone.tail[0], bone.tail[1], bone.tail[2]};
                const Math::Vector3UVE along = tail - head;
                const float length = Math::LengthUVE(along);
                if (length < 1.0e-4F) {
                    continue;
                }
                const Math::Vector3UVE axis = along * (1.0F / length);
                Math::Vector3UVE across = Math::CrossUVE(axis, forward);
                const float acrossLength = Math::LengthUVE(across);
                across = acrossLength > 1.0e-4F ? across * (1.0F / acrossLength) : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
                const float half = std::clamp(length * 0.08F, boundingRadius * 0.004F, boundingRadius * 0.025F);
                const Math::Vector3UVE waist = head + along * 0.2F;
                const Math::Vector3UVE a = waist + across * half;
                const Math::Vector3UVE b = waist - across * half;
                lines.insert(lines.end(), {head, a, a, tail, tail, b, b, head});
            }
            if (!lines.empty()) {
                GLuint boneVao = 0U;
                GLuint boneVbo = 0U;
                gl.table.glGenVertexArrays(1, &boneVao);
                gl.table.glBindVertexArray(boneVao);
                gl.table.glGenBuffers(1, &boneVbo);
                gl.table.glBindBuffer(GL_ARRAY_BUFFER, boneVbo);
                gl.table.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(Math::Vector3UVE)),
                                      lines.data(), GL_STREAM_DRAW);
                gl.table.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Math::Vector3UVE), nullptr);
                gl.table.glEnableVertexAttribArray(0);
                gl.table.glUseProgram(gl.boneShaderProgram);
                gl.table.glUniformMatrix4fv(gl.boneViewProjectionUniform, 1, GL_TRUE, &viewProjection.m[0][0]);
                constexpr float kBoneColourUVE[4] = {0.55F, 0.95F, 0.80F, 0.95F};
                gl.table.glUniform4fv(gl.boneColorUniform, 1, kBoneColourUVE);
                SetGlCapabilityUVE(GL_DEPTH_TEST, GL_FALSE);
                SetGlCapabilityUVE(GL_BLEND, GL_TRUE);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
                gl.table.glBindVertexArray(0);
                gl.table.glDeleteBuffers(1, &boneVbo);
                gl.table.glDeleteVertexArrays(1, &boneVao);
            }
        }
    }

    for (const auto& [image, id] : albedoTextures) {
        if (id != 0U) {
            glDeleteTextures(1, &id);
        }
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
    EditorUiAssetsUVE::RegisterDynamicTextureSizeUVE(static_cast<std::uintptr_t>(colorTexture), width, height);
    return static_cast<std::uintptr_t>(colorTexture);
}

} // namespace UVE::Editor
