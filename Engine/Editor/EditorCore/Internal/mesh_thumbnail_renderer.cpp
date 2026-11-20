// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mesh_thumbnail_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// See uve/rhi_opengl/gl_functions_uve.h for why glext.h is safe to include here: it only
// supplies Khronos PFNGL*PROC typedefs and GL_* constants, never a loader library. The GL proc
// loading itself is NOT hand-rolled here anymore - this class shares the engine's one contract
// loader (AUDIT 5.6, EditorCore half). The GL surface stays confined to this .cpp: the public
// header below never includes a GL header.
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include "gl_functions.h"

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

constexpr const char* kVertexShaderSourceUVE = R"GLSL(#version 330 core
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

constexpr const char* kFragmentShaderSourceUVE = R"GLSL(#version 330 core
const float kPi = 3.14159265359;

in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vWorldPosition;
out vec4 FragColor;

uniform vec3 uCameraPosition;
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uDielectricSpecular;
uniform float uNormalScale;
uniform bool uFlipGreen;
uniform float uOcclusionStrength;
uniform vec3 uEmissiveColor;
uniform float uEmissiveEnergy;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform bool uUnshaded;

uniform bool uHasAlbedoTexture;
uniform bool uHasNormalTexture;
uniform bool uHasMetallicRoughnessTexture;
uniform bool uHasAOTexture;
uniform bool uHasEmissiveTexture;
uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform sampler2D uAOTexture;
uniform sampler2D uEmissiveTexture;

float DistributionGGXUVE(vec3 normal, vec3 halfway, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float normalDotHalfway = max(dot(normal, halfway), 0.0);
    const float denominator = normalDotHalfway * normalDotHalfway * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPi * denominator * denominator, 1.0e-5);
}

float VisibilitySmithGGXCorrelatedUVE(float normalDotView, float normalDotLight, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float viewTerm = normalDotLight * sqrt(normalDotView * normalDotView * (1.0 - alphaSquared) + alphaSquared);
    const float lightTerm = normalDotView * sqrt(normalDotLight * normalDotLight * (1.0 - alphaSquared) + alphaSquared);
    return 0.5 / max(viewTerm + lightTerm, 1.0e-5);
}

vec3 FresnelSchlickUVE(float viewDotHalfway, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * pow(1.0 - viewDotHalfway, 5.0);
}

vec3 DirectLightUVE(vec3 normal, vec3 view, vec3 lightDirection, vec3 radiance, vec3 albedo,
                    vec3 baseReflectance, float metallic, float roughness) {
    const float normalDotLight = max(dot(normal, lightDirection), 0.0);
    if (normalDotLight <= 0.0) {
        return vec3(0.0);
    }
    const vec3 halfway = normalize(view + lightDirection);
    const float normalDotView = max(dot(normal, view), 1.0e-4);
    const float distribution = DistributionGGXUVE(normal, halfway, roughness);
    const float visibility = VisibilitySmithGGXCorrelatedUVE(normalDotView, normalDotLight, roughness);
    const vec3 fresnel = FresnelSchlickUVE(max(dot(halfway, view), 0.0), baseReflectance);
    const vec3 specular = fresnel * distribution * visibility;
    const vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuseWeight * albedo / kPi + specular) * radiance * normalDotLight;
}

vec3 ToneMapUVE(vec3 color) {
    const vec3 numerator = color * (2.51 * color + 0.03);
    const vec3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return clamp(numerator / denominator, 0.0, 1.0);
}

vec3 LinearToDisplayUVE(vec3 color) {
    const vec3 low = color * 12.92;
    const vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(color, vec3(0.0031308)));
}

vec3 PerturbNormalUVE(vec3 geomNormal, vec3 worldPos, vec2 uv, vec3 tangentNormal) {
    const vec3 dp1 = dFdx(worldPos);
    const vec3 dp2 = dFdy(worldPos);
    const vec2 duv1 = dFdx(uv);
    const vec2 duv2 = dFdy(uv);
    const vec3 dp2perp = cross(dp2, geomNormal);
    const vec3 dp1perp = cross(geomNormal, dp1);
    const vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float maxLenSq = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (maxLenSq <= 1.0e-10) {
        return geomNormal;
    }
    const float invMax = inversesqrt(maxLenSq);
    return normalize(mat3(tangent * invMax, bitangent * invMax, geomNormal) * tangentNormal);
}

void main() {
    const vec2 uv = vTexCoord * uUvScale + uUvOffset;
    vec4 albedoSample = vec4(1.0);
    if (uHasAlbedoTexture) {
        albedoSample = texture(uAlbedoTexture, uv);
        if (albedoSample.a < 0.05) {
            discard;
        }
    }
    const vec3 albedo = uBaseColor * albedoSample.rgb;
    if (uUnshaded) {
        FragColor = vec4(LinearToDisplayUVE(clamp(albedo, 0.0, 1.0)), 1.0);
        return;
    }

    vec3 shadingNormal = normalize(vNormal);
    if (uHasNormalTexture) {
        vec3 tangentNormal = texture(uNormalTexture, uv).xyz * 2.0 - 1.0;
        if (uFlipGreen) {
            tangentNormal.y = -tangentNormal.y;
        }
        tangentNormal.xy *= uNormalScale;
        shadingNormal = PerturbNormalUVE(shadingNormal, vWorldPosition, uv, tangentNormal);
    }

    const vec3 view = normalize(uCameraPosition - vWorldPosition);
    if (dot(shadingNormal, view) < 0.0) {
        shadingNormal = -shadingNormal;
    }

    float metallic = uMetallic;
    float roughness = uRoughness;
    if (uHasMetallicRoughnessTexture) {
        const vec3 mr = texture(uMetallicRoughnessTexture, uv).rgb;
        metallic *= mr.b;
        roughness *= mr.g;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.045, 1.0);

    float occlusion = 1.0;
    if (uHasAOTexture) {
        occlusion = mix(1.0, texture(uAOTexture, uv).r, clamp(uOcclusionStrength, 0.0, 1.0));
    }

    const float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    const vec3 baseReflectance = mix(vec3(0.16 * dielectricSpec * dielectricSpec), albedo, metallic);

    // Balanced three-point studio rig matching MaterialThumbnailRendererUVE so mesh and material
    // thumbnails agree on exposure, roughness response, and metal reflections.
    const vec3 keyDir = normalize(vec3(-0.45, 0.85, 0.55));
    const vec3 keyColor = vec3(2.05, 1.98, 1.88);
    const vec3 fillDir = normalize(vec3(0.75, -0.15, 0.65));
    const vec3 fillColor = vec3(0.42, 0.46, 0.54);
    const vec3 rimDir = normalize(vec3(0.10, 0.46, -0.95));
    const vec3 rimColor = vec3(0.55, 0.58, 0.65);
    const vec3 skyAmbient = vec3(0.16, 0.18, 0.22);
    const vec3 groundAmbient = vec3(0.045, 0.042, 0.038);

    vec3 color = DirectLightUVE(shadingNormal, view, keyDir, keyColor, albedo, baseReflectance, metallic, roughness) +
                 DirectLightUVE(shadingNormal, view, fillDir, fillColor, albedo, baseReflectance, metallic, roughness) +
                 DirectLightUVE(shadingNormal, view, rimDir, rimColor, albedo, baseReflectance, metallic, roughness);

    const vec3 irradiance = mix(groundAmbient, skyAmbient, shadingNormal.y * 0.5 + 0.5) * occlusion;
    const vec3 diffuseAmbient = albedo * (1.0 - metallic) * irradiance;
    const vec3 reflection = reflect(-view, shadingNormal);
    const vec3 environment = mix(groundAmbient, skyAmbient, reflection.y * 0.5 + 0.5);
    const float normalDotView = max(dot(shadingNormal, view), 0.0);
    const vec3 ambientFresnel =
        baseReflectance + (max(vec3(1.0 - roughness), baseReflectance) - baseReflectance) * pow(1.0 - normalDotView, 5.0);
    const vec3 specularAmbient = environment * ambientFresnel * mix(1.0, occlusion, 0.6);
    color += diffuseAmbient + specularAmbient;

    if (uHasEmissiveTexture) {
        color += texture(uEmissiveTexture, uv).rgb * uEmissiveColor * uEmissiveEnergy;
    } else {
        color += uEmissiveColor * uEmissiveEnergy;
    }

    FragColor = vec4(LinearToDisplayUVE(ToneMapUVE(color)), 1.0);
}
)GLSL";

constexpr const char* kBoneVertexShaderSourceUVE = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main() {
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";

constexpr const char* kBoneFragmentShaderSourceUVE = R"GLSL(#version 330 core
out vec4 FragColor;
uniform vec4 uBoneColor;
void main() {
    FragColor = uBoneColor;
}
)GLSL";

constexpr GLenum kReadFramebufferUVE = 0x8CA8U;
constexpr GLenum kDrawFramebufferUVE = 0x8CA9U;

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

/// The shared GL proc table (see uve/rhi_opengl/gl_functions_uve.h), compiled shader program, and
/// scratch render target this renderer needs, kept out of the public header (see the header's class
/// doc comment).
struct MeshThumbnailRendererUVE::GlStateUVE {
    Render::Detail::GlFunctionsUVE table;

    bool functionsLoaded = false;

    GLuint shaderProgram = 0U;
    GLint viewProjectionUniform = -1;
    GLint cameraPositionUniform = -1;
    GLint baseColorUniform = -1;
    GLint metallicUniform = -1;
    GLint roughnessUniform = -1;
    GLint dielectricSpecularUniform = -1;
    GLint normalScaleUniform = -1;
    GLint flipGreenUniform = -1;
    GLint occlusionStrengthUniform = -1;
    GLint emissiveColorUniform = -1;
    GLint emissiveEnergyUniform = -1;
    GLint uvScaleUniform = -1;
    GLint uvOffsetUniform = -1;
    GLint unshadedUniform = -1;
    GLint hasAlbedoTextureUniform = -1;
    GLint hasNormalTextureUniform = -1;
    GLint hasMetallicRoughnessTextureUniform = -1;
    GLint hasAOTextureUniform = -1;
    GLint hasEmissiveTextureUniform = -1;
    GLint albedoTextureUniform = -1;
    GLint normalTextureUniform = -1;
    GLint metallicRoughnessTextureUniform = -1;
    GLint aoTextureUniform = -1;
    GLint emissiveTextureUniform = -1;
    bool programLinked = false;

    GLuint boneShaderProgram = 0U;
    GLint boneViewProjectionUniform = -1;
    GLint boneColorUniform = -1;
    bool boneProgramLinked = false;

    GLuint scratchFramebuffer = 0U;
    GLuint scratchDepthRenderbuffer = 0U;
    GLuint scratchColorTexture = 0U;
    GLuint resolveFramebuffer = 0U;
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

/// Creates (once) and resizes (as needed) the scratch framebuffer + depth-renderbuffer (+ optional
/// supersampled color texture & resolve framebuffer) reused by every RenderThumbnailUVE() call.
[[nodiscard]] bool EnsureScratchTargetUVE(MeshThumbnailRendererUVE::GlStateUVE& gl, const int width,
                                           const int height, const bool supersample) noexcept {
    const bool firstTime = gl.scratchFramebuffer == 0U;
    if (firstTime) {
        gl.table.glGenFramebuffers(1, &gl.scratchFramebuffer);
        gl.table.glGenRenderbuffers(1, &gl.scratchDepthRenderbuffer);
        if (gl.scratchFramebuffer == 0U || gl.scratchDepthRenderbuffer == 0U) {
            return false;
        }
    }
    if (supersample && gl.resolveFramebuffer == 0U) {
        gl.table.glGenFramebuffers(1, &gl.resolveFramebuffer);
        glGenTextures(1, &gl.scratchColorTexture);
        if (gl.resolveFramebuffer == 0U || gl.scratchColorTexture == 0U) {
            return false;
        }
    }
    if (firstTime || gl.scratchWidth != width || gl.scratchHeight != height) {
        gl.table.glBindRenderbuffer(GL_RENDERBUFFER, gl.scratchDepthRenderbuffer);
        gl.table.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        if (supersample && gl.scratchColorTexture != 0U) {
            glBindTexture(GL_TEXTURE_2D, gl.scratchColorTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
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
    gl.cameraPositionUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uCameraPosition");
    gl.baseColorUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uBaseColor");
    gl.metallicUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uMetallic");
    gl.roughnessUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uRoughness");
    gl.dielectricSpecularUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uDielectricSpecular");
    gl.normalScaleUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uNormalScale");
    gl.flipGreenUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uFlipGreen");
    gl.occlusionStrengthUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uOcclusionStrength");
    gl.emissiveColorUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uEmissiveColor");
    gl.emissiveEnergyUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uEmissiveEnergy");
    gl.uvScaleUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uUvScale");
    gl.uvOffsetUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uUvOffset");
    gl.unshadedUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uUnshaded");
    gl.hasAlbedoTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uHasAlbedoTexture");
    gl.hasNormalTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uHasNormalTexture");
    gl.hasMetallicRoughnessTextureUniform =
        gl.table.glGetUniformLocation(gl.shaderProgram, "uHasMetallicRoughnessTexture");
    gl.hasAOTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uHasAOTexture");
    gl.hasEmissiveTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uHasEmissiveTexture");
    gl.albedoTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uAlbedoTexture");
    gl.normalTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uNormalTexture");
    gl.metallicRoughnessTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uMetallicRoughnessTexture");
    gl.aoTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uAOTexture");
    gl.emissiveTextureUniform = gl.table.glGetUniformLocation(gl.shaderProgram, "uEmissiveTexture");
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
        if (gl.scratchColorTexture != 0U) {
            glDeleteTextures(1, &gl.scratchColorTexture);
            gl.scratchColorTexture = 0U;
        }
        if (gl.resolveFramebuffer != 0U) {
            gl.table.glDeleteFramebuffers(1, &gl.resolveFramebuffer);
            gl.resolveFramebuffer = 0U;
        }
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
    GLint previousActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    constexpr std::size_t kTextureUnitCountUVE = 5U;
    std::array<GLint, kTextureUnitCountUVE> previousTextures{};
    std::array<GLint, kTextureUnitCountUVE> previousSamplers{};
    for (std::size_t unit = 0U; unit < kTextureUnitCountUVE; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextures[unit]);
        if (gl.table.glBindSampler != nullptr) {
            glGetIntegerv(GL_SAMPLER_BINDING, &previousSamplers[unit]);
            gl.table.glBindSampler(static_cast<GLuint>(unit), 0U);
        }
    }
    gl.table.glActiveTexture(GL_TEXTURE0);
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    GLfloat previousClearColor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    const bool supersample = gl.table.glBlitFramebuffer != nullptr && width <= 1024 && height <= 1024;
    const int renderWidth = supersample ? width * 2 : width;
    const int renderHeight = supersample ? height * 2 : height;
    bool succeeded = EnsureScratchTargetUVE(gl, renderWidth, renderHeight, supersample);

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

        if (supersample) {
            gl.table.glBindFramebuffer(GL_FRAMEBUFFER, gl.resolveFramebuffer);
            gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
            succeeded = gl.table.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (succeeded) {
                gl.table.glBindFramebuffer(GL_FRAMEBUFFER, gl.scratchFramebuffer);
                gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                                gl.scratchColorTexture, 0);
                succeeded = gl.table.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            }
        } else {
            gl.table.glBindFramebuffer(GL_FRAMEBUFFER, gl.scratchFramebuffer);
            gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
            succeeded = gl.table.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
    }

    // Upload each distinct (image, forceLinear) texture once with proper sRGB/linear format and mipmaps.
    struct UploadedMapUVE {
        const MaterialThumbnailImageUVE* image = nullptr;
        bool forceLinear = false;
        GLuint id = 0U;
    };
    std::vector<UploadedMapUVE> uploadedMaps;
    const auto textureOf = [&gl, &uploadedMaps](const MaterialThumbnailImageUVE* image,
                                                const bool forceLinear) -> GLuint {
        if (image == nullptr || !image->IsValidUVE()) {
            return 0U;
        }
        for (const UploadedMapUVE& known : uploadedMaps) {
            if (known.image == image && known.forceLinear == forceLinear) {
                return known.id;
            }
        }
        GLuint id = 0U;
        glGenTextures(1, &id);
        if (id != 0U) {
            gl.table.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            const GLint internalFormat = image->srgb && !forceLinear ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         image->rgba8.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl.table.glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            glGetError() == GL_NO_ERROR ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        }
        uploadedMaps.push_back(UploadedMapUVE{image, forceLinear, id});
        return id;
    };

    GLuint vao = 0U;
    GLuint vertexBuffer = 0U;
    GLuint indexBuffer = 0U;
    if (succeeded) {
        glViewport(0, 0, renderWidth, renderHeight);
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
        gl.table.glUniform3fv(gl.cameraPositionUniform, 1, &cameraPosition.x);
        gl.table.glUniform1i(gl.albedoTextureUniform, 0);
        gl.table.glUniform1i(gl.normalTextureUniform, 1);
        gl.table.glUniform1i(gl.metallicRoughnessTextureUniform, 2);
        gl.table.glUniform1i(gl.aoTextureUniform, 3);
        gl.table.glUniform1i(gl.emissiveTextureUniform, 4);

        const auto bindMap = [&gl](const GLenum unit, const GLint hasUniform, const GLuint texId) {
            if (texId != 0U) {
                gl.table.glActiveTexture(unit);
                glBindTexture(GL_TEXTURE_2D, texId);
            }
            if (hasUniform >= 0) {
                gl.table.glUniform1i(hasUniform, texId != 0U ? 1 : 0);
            }
        };

        // Each surface in its own material's PBR look; see-through ones are left out.
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
            gl.table.glUniform1f(gl.metallicUniform, look.metallic);
            gl.table.glUniform1f(gl.roughnessUniform, look.roughness);
            gl.table.glUniform1f(gl.dielectricSpecularUniform, std::clamp(look.dielectricSpecular, 0.0F, 1.0F));
            gl.table.glUniform1f(gl.normalScaleUniform, look.normalScale);
            gl.table.glUniform1i(gl.flipGreenUniform, look.flipGreen ? 1 : 0);
            gl.table.glUniform1f(gl.occlusionStrengthUniform, look.occlusionStrength);
            gl.table.glUniform3fv(gl.emissiveColorUniform, 1, look.emissiveColor.data());
            gl.table.glUniform1f(gl.emissiveEnergyUniform, look.emissiveEnergy);
            gl.table.glUniform2fv(gl.uvScaleUniform, 1, look.uvScale.data());
            gl.table.glUniform2fv(gl.uvOffsetUniform, 1, look.uvOffset.data());
            gl.table.glUniform1i(gl.unshadedUniform, look.unshaded ? 1 : 0);

            bindMap(GL_TEXTURE0, gl.hasAlbedoTextureUniform, textureOf(look.albedo, /*forceLinear=*/false));
            bindMap(GL_TEXTURE1, gl.hasNormalTextureUniform, textureOf(look.normal, /*forceLinear=*/true));
            bindMap(GL_TEXTURE2, gl.hasMetallicRoughnessTextureUniform,
                    textureOf(look.metallicRoughness, /*forceLinear=*/true));
            bindMap(GL_TEXTURE3, gl.hasAOTextureUniform, textureOf(look.ao, /*forceLinear=*/true));
            bindMap(GL_TEXTURE4, gl.hasEmissiveTextureUniform, textureOf(look.emissive, /*forceLinear=*/false));

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

    if (succeeded && supersample) {
        gl.table.glBindFramebuffer(kReadFramebufferUVE, gl.scratchFramebuffer);
        gl.table.glBindFramebuffer(kDrawFramebufferUVE, gl.resolveFramebuffer);
        gl.table.glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        succeeded = glGetError() == GL_NO_ERROR;
    }

    if (succeeded) {
        gl.table.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        gl.table.glGenerateMipmap(GL_TEXTURE_2D);
        if (glGetError() == GL_NO_ERROR) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
    }

    for (const UploadedMapUVE& map : uploadedMaps) {
        if (map.id != 0U) {
            glDeleteTextures(1, &map.id);
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
        gl.table.glBindFramebuffer(GL_FRAMEBUFFER, supersample ? gl.resolveFramebuffer : gl.scratchFramebuffer);
        gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }

    gl.table.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    gl.table.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    gl.table.glUseProgram(static_cast<GLuint>(previousProgram));
    gl.table.glBindVertexArray(static_cast<GLuint>(previousVao));
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));
    for (std::size_t unit = 0U; unit < kTextureUnitCountUVE; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextures[unit]));
        if (gl.table.glBindSampler != nullptr) {
            gl.table.glBindSampler(static_cast<GLuint>(unit), static_cast<GLuint>(previousSamplers[unit]));
        }
    }
    gl.table.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    SetGlCapabilityUVE(GL_DEPTH_TEST, depthTestWasEnabled);
    SetGlCapabilityUVE(GL_CULL_FACE, cullFaceWasEnabled);
    SetGlCapabilityUVE(GL_BLEND, blendWasEnabled);
    SetGlCapabilityUVE(GL_SCISSOR_TEST, scissorWasEnabled);
    glDepthMask(previousDepthMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
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
