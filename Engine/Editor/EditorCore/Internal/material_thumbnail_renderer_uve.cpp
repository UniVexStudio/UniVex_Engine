// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/editor/material_thumbnail_renderer_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// See uve/rhi_opengl/gl_functions_uve.h for why glext.h is safe to include here: it only
// supplies Khronos PFNGL*PROC typedefs and GL_* constants, never a loader library. The GL proc
// loading itself is not hand-rolled - this class shares the engine's one contract loader, exactly
// like MeshThumbnailRendererUVE. The GL surface stays confined to this .cpp: the public header
// never includes a GL header.
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include "uve/rhi_opengl/gl_functions_uve.h"

#include "uve/asset/material_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Editor {

namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kFieldOfViewYRadiansUVE = 0.6981317F; // 40 degrees, the same framing as mesh previews
constexpr float kSphereFillFractionUVE = 0.86F;        // of the smaller render dimension
constexpr float kNearPlaneFractionUVE = 0.05F;

// A preview never needs more detail than this from a source map: the sphere covers at most a few
// hundred pixels, and a 4K albedo would otherwise cost 64 MB of upload per cache miss for pixels
// no one can see. Larger maps are box-filtered down on the CPU before upload.
constexpr int kMaxPreviewTextureSizeUVE = 512;

constexpr std::uint32_t kPreviewSphereSlicesUVE = 96U;
constexpr std::uint32_t kPreviewSphereStacksUVE = 64U;
constexpr float kPreviewSphereRadiusUVE = 0.5F;

// A fixed three-light studio rig (directions point from the surface toward each light) plus a
// hemisphere ambient. The rig is what makes the preview legible: a single key light leaves the
// far side of the sphere black, which reads as "broken material" rather than "unlit".
constexpr float kKeyLightDirectionUVE[3] = {-0.45F, 0.85F, 0.55F};
constexpr float kKeyLightColorUVE[3] = {3.12F, 3.02F, 2.88F};
// From below the horizon: a cool bounce that keeps the underside readable without pretending the
// sphere stands on a floor.
constexpr float kFillLightDirectionUVE[3] = {0.75F, -0.15F, 0.65F};
constexpr float kFillLightColorUVE[3] = {0.43F, 0.48F, 0.57F};
constexpr float kRimLightDirectionUVE[3] = {0.10F, 0.46F, -0.95F};
constexpr float kRimLightColorUVE[3] = {1.62F, 1.62F, 1.68F};
constexpr float kSkyAmbientUVE[3] = {0.144F, 0.160F, 0.192F};
constexpr float kGroundAmbientUVE[3] = {0.035F, 0.033F, 0.029F};

constexpr const char* kVertexShaderSourceUVE = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;
uniform mat4 uViewProjection;
out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vTangent;
void main() {
    vWorldPosition = aPosition;
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    vTangent = aTangent;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";

constexpr const char* kFragmentShaderSourceUVE = R"GLSL(#version 450 core
const float kPi = 3.14159265359;

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vTangent;
out vec4 FragColor;

uniform vec3 uCameraPosition;

uniform vec3 uAlbedoColor;
uniform float uAlbedoAlpha;
uniform float uMetallic;
uniform float uRoughness;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform float uNormalScale;
uniform bool uFlipGreen;
uniform float uOcclusionStrength;
uniform vec3 uEmissiveColor;
uniform float uEmissiveEnergy;
uniform bool uUnshaded;
uniform float uAlphaCutoff;
uniform bool uBlendEnabled;

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

uniform vec3 uKeyDirection;
uniform vec3 uKeyColor;
uniform vec3 uFillDirection;
uniform vec3 uFillColor;
uniform vec3 uRimDirection;
uniform vec3 uRimColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;

// Cook-Torrance GGX: D (Trowbridge-Reitz), Smith height-correlated visibility, Schlick Fresnel.
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
    // Energy conservation: whatever the specular lobe reflects is not available to the diffuse
    // lobe, and a metal has no diffuse response at all.
    const vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuseWeight * albedo / kPi + specular) * radiance * normalDotLight;
}

// ACES filmic curve (Narkowicz 2015 fit). The preview is display-referred, so the linear radiance
// the BRDF produces has to be compressed before it can be shown as an 8-bit sRGB image; without
// this, every highlight above 1.0 clips to a flat white disc.
vec3 ToneMapUVE(vec3 color) {
    const vec3 numerator = color * (2.51 * color + 0.03);
    const vec3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return clamp(numerator / denominator, 0.0, 1.0);
}

// Exact sRGB transfer function (not a 2.2 gamma approximation), matching Math::LinearToDisplayUVE.
vec3 LinearToDisplayUVE(vec3 color) {
    const vec3 low = color * 12.92;
    const vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(color, vec3(0.0031308)));
}

void main() {
    const vec2 uv = vTexCoord * uUvScale + uUvOffset;
    vec4 albedoSample = vec4(1.0);
    if (uHasAlbedoTexture) {
        albedoSample = texture(uAlbedoTexture, uv);
    }
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    const vec3 albedo = albedoSample.rgb * uAlbedoColor;
    const float surfaceAlpha = clamp(albedoSample.a * uAlbedoAlpha, 0.0, 1.0);
    const float outputAlpha = uBlendEnabled ? surfaceAlpha : 1.0;
    if (uUnshaded) {
        FragColor = vec4(LinearToDisplayUVE(clamp(albedo, 0.0, 1.0)), outputAlpha);
        return;
    }

    vec3 shadingNormal = normalize(vNormal);
    vec3 tangent = vTangent - shadingNormal * dot(shadingNormal, vTangent);
    if (dot(tangent, tangent) <= 1.0e-8) {
        const vec3 fallbackAxis = abs(shadingNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = cross(fallbackAxis, shadingNormal);
    }
    tangent = normalize(tangent);
    vec3 bitangent = normalize(cross(shadingNormal, tangent));
    if (uFlipGreen) {
        bitangent = -bitangent;
    }
    if (uHasNormalTexture) {
        vec3 tangentSpaceNormal = texture(uNormalTexture, uv).xyz * 2.0 - 1.0;
        tangentSpaceNormal.xy *= uNormalScale;
        shadingNormal = normalize(mat3(tangent, bitangent, shadingNormal) * tangentSpaceNormal);
    }

    const vec3 view = normalize(uCameraPosition - vWorldPosition);
    // Two-sided safety, exactly like the runtime material shader: a preview must never show a
    // black hole where a normal happens to face away from the camera.
    if (dot(shadingNormal, view) < 0.0) {
        shadingNormal = -shadingNormal;
    }

    float metallic = uMetallic;
    float roughness = uRoughness;
    if (uHasMetallicRoughnessTexture) {
        const vec3 metallicRoughness = texture(uMetallicRoughnessTexture, uv).rgb;
        metallic *= metallicRoughness.b;
        roughness *= metallicRoughness.g;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    // A perfectly smooth surface under a directional light is a single-pixel highlight (and a
    // division by a near-zero normal distribution), so the preview clamps to a usable minimum.
    roughness = clamp(roughness, 0.045, 1.0);

    float occlusion = 1.0;
    if (uHasAOTexture) {
        occlusion = mix(1.0, texture(uAOTexture, uv).r, clamp(uOcclusionStrength, 0.0, 1.0));
    }

    const vec3 baseReflectance = mix(vec3(0.04), albedo, metallic);
    vec3 color = DirectLightUVE(shadingNormal, view, normalize(uKeyDirection), uKeyColor, albedo, baseReflectance,
                                metallic, roughness) +
                 DirectLightUVE(shadingNormal, view, normalize(uFillDirection), uFillColor, albedo, baseReflectance,
                                metallic, roughness) +
                 DirectLightUVE(shadingNormal, view, normalize(uRimDirection), uRimColor, albedo, baseReflectance,
                                metallic, roughness);

    // Ambient as a hemisphere environment: diffuse gets the irradiance for the surface normal, and
    // specular gets the one for the reflection vector. A metal lit only by ambient would otherwise
    // be black - the reflection lobe is a metal's entire response.
    const vec3 irradiance = mix(uGroundAmbient, uSkyAmbient, shadingNormal.y * 0.5 + 0.5) * occlusion;
    const vec3 diffuseAmbient = albedo * (1.0 - metallic) * irradiance;
    const vec3 reflection = reflect(-view, shadingNormal);
    const vec3 environment = mix(uGroundAmbient, uSkyAmbient, reflection.y * 0.5 + 0.5);
    const float normalDotView = max(dot(shadingNormal, view), 0.0);
    // Roughness-aware Fresnel: a rough surface does not get the bright grazing-angle rim that
    // Schlick's white ceiling would give it.
    const vec3 ambientFresnel =
        baseReflectance + (max(vec3(1.0 - roughness), baseReflectance) - baseReflectance) * pow(1.0 - normalDotView, 5.0);
    const vec3 specularAmbient = environment * ambientFresnel * mix(1.0, occlusion, 0.6);
    color += diffuseAmbient + specularAmbient;

    if (uHasEmissiveTexture) {
        color += texture(uEmissiveTexture, uv).rgb * uEmissiveColor * uEmissiveEnergy;
    } else {
        color += uEmissiveColor * uEmissiveEnergy;
    }

    FragColor = vec4(LinearToDisplayUVE(ToneMapUVE(color)), outputAlpha);
}
)GLSL";

/// One sphere vertex. Positions/normals/tangents are in the preview sphere's own space, which is
/// also the world space the shader and camera work in (the sphere is the only object drawn).
struct PreviewVertexUVE final {
    float position[3];
    float normal[3];
    float texCoord[2];
    float tangent[3];
};

enum class PreviewUniformUVE : std::size_t {
    ViewProjection = 0,
    CameraPosition,
    AlbedoColor,
    AlbedoAlpha,
    Metallic,
    Roughness,
    UvScale,
    UvOffset,
    NormalScale,
    FlipGreen,
    OcclusionStrength,
    EmissiveColor,
    EmissiveEnergy,
    Unshaded,
    AlphaCutoff,
    BlendEnabled,
    HasAlbedoTexture,
    HasNormalTexture,
    HasMetallicRoughnessTexture,
    HasAOTexture,
    HasEmissiveTexture,
    AlbedoTexture,
    NormalTexture,
    MetallicRoughnessTexture,
    AOTexture,
    EmissiveTexture,
    KeyDirection,
    KeyColor,
    FillDirection,
    FillColor,
    RimDirection,
    RimColor,
    SkyAmbient,
    GroundAmbient,
    Count,
};

constexpr std::size_t kPreviewUniformCountUVE = static_cast<std::size_t>(PreviewUniformUVE::Count);

/// Must stay in the same order as PreviewUniformUVE; InitializeUVE() resolves every name once.
constexpr std::array<const char*, kPreviewUniformCountUVE> kPreviewUniformNamesUVE{
    "uViewProjection",       "uCameraPosition",   "uAlbedoColor",       "uAlbedoAlpha",      "uMetallic",
    "uRoughness",            "uUvScale",          "uUvOffset",          "uNormalScale",      "uFlipGreen",
    "uOcclusionStrength",    "uEmissiveColor",    "uEmissiveEnergy",    "uUnshaded",         "uAlphaCutoff",
    "uBlendEnabled",         "uHasAlbedoTexture", "uHasNormalTexture",  "uHasMetallicRoughnessTexture",
    "uHasAOTexture",         "uHasEmissiveTexture", "uAlbedoTexture",   "uNormalTexture",    "uMetallicRoughnessTexture",
    "uAOTexture",            "uEmissiveTexture",  "uKeyDirection",      "uKeyColor",         "uFillDirection",
    "uFillColor",            "uRimDirection",     "uRimColor",          "uSkyAmbient",       "uGroundAmbient",
};

void SetGlCapabilityUVE(const GLenum capability, const GLboolean enabled) noexcept {
    if (enabled == GL_TRUE) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

/// Bridges GLFW's proc-address lookup (GLFWglproc, a void(*)(void)) to the shared GL loader's
/// void*-based signature - the same one-line adapter GlRenderDeviceUVE and
/// MeshThumbnailRendererUVE pass.
[[nodiscard]] void* GrabGlProcAddressUVE(const char* const name) noexcept {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

[[nodiscard]] bool CompileShaderStageUVE(const Render::Detail::GlFunctionsUVE& table, const GLenum stage,
                                         const char* const source, GLuint& outShader) noexcept {
    outShader = table.glCreateShader(stage);
    if (outShader == 0U) {
        return false;
    }
    table.glShaderSource(outShader, 1, &source, nullptr);
    table.glCompileShader(outShader);
    GLint compiled = GL_FALSE;
    table.glGetShaderiv(outShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        table.glDeleteShader(outShader);
        outShader = 0U;
        return false;
    }
    return true;
}

/// Builds the fixed preview sphere as UV-sphere geometry with analytic normals and tangents. The
/// seam duplicates its column (u = 0 and u = 1) so a wrapped albedo/normal map has no tear, and
/// the two pole rows emit the usual degenerate triangles instead of needing a separate fan path.
void BuildPreviewSphereUVE(std::vector<PreviewVertexUVE>& outVertices, std::vector<std::uint32_t>& outIndices) {
    const std::size_t columns = static_cast<std::size_t>(kPreviewSphereSlicesUVE) + 1U;
    const std::size_t rows = static_cast<std::size_t>(kPreviewSphereStacksUVE) + 1U;
    outVertices.clear();
    outVertices.reserve(columns * rows);
    for (std::uint32_t stack = 0U; stack <= kPreviewSphereStacksUVE; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(kPreviewSphereStacksUVE);
        const float polar = v * kPiUVE;
        const float sinPolar = std::sin(polar);
        const float cosPolar = std::cos(polar);
        for (std::uint32_t slice = 0U; slice <= kPreviewSphereSlicesUVE; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kPreviewSphereSlicesUVE);
            const float azimuth = u * 2.0F * kPiUVE;
            const float sinAzimuth = std::sin(azimuth);
            const float cosAzimuth = std::cos(azimuth);
            const float normalX = sinPolar * cosAzimuth;
            const float normalZ = sinPolar * sinAzimuth;
            PreviewVertexUVE vertex{};
            vertex.position[0] = normalX * kPreviewSphereRadiusUVE;
            vertex.position[1] = cosPolar * kPreviewSphereRadiusUVE;
            vertex.position[2] = normalZ * kPreviewSphereRadiusUVE;
            vertex.normal[0] = normalX;
            vertex.normal[1] = cosPolar;
            vertex.normal[2] = normalZ;
            vertex.texCoord[0] = u;
            vertex.texCoord[1] = v;
            // d(position)/d(u): increasing azimuth at constant polar angle. cross(normal, tangent)
            // is then d(position)/d(v), which is exactly the engine shader's bitangent convention.
            vertex.tangent[0] = -sinAzimuth;
            vertex.tangent[1] = 0.0F;
            vertex.tangent[2] = cosAzimuth;
            outVertices.push_back(vertex);
        }
    }
    outIndices.clear();
    outIndices.reserve(static_cast<std::size_t>(kPreviewSphereSlicesUVE) *
                       static_cast<std::size_t>(kPreviewSphereStacksUVE) * 6U);
    for (std::uint32_t stack = 0U; stack < kPreviewSphereStacksUVE; ++stack) {
        for (std::uint32_t slice = 0U; slice < kPreviewSphereSlicesUVE; ++slice) {
            const std::uint32_t topLeft = stack * (kPreviewSphereSlicesUVE + 1U) + slice;
            const std::uint32_t topRight = topLeft + 1U;
            const std::uint32_t bottomLeft = topLeft + (kPreviewSphereSlicesUVE + 1U);
            const std::uint32_t bottomRight = bottomLeft + 1U;
            outIndices.push_back(topLeft);
            outIndices.push_back(topRight);
            outIndices.push_back(bottomLeft);
            outIndices.push_back(topRight);
            outIndices.push_back(bottomRight);
            outIndices.push_back(bottomLeft);
        }
    }
}

} // namespace

/// The shared GL proc table, the compiled shader program and its resolved uniform locations, the
/// preview sphere's GPU buffers, and the scratch render target - kept out of the public header for
/// the reasons that header documents.
struct MaterialThumbnailRendererUVE::GlStateUVE {
    Render::Detail::GlFunctionsUVE table;
    bool functionsLoaded = false;

    GLuint shaderProgram = 0U;
    bool programLinked = false;
    std::array<GLint, kPreviewUniformCountUVE> uniformLocations{};

    GLuint previewVertexArray = 0U;
    GLuint previewVertexBuffer = 0U;
    GLuint previewIndexBuffer = 0U;
    GLsizei previewIndexCount = 0;

    /// GL 1.4, so it has no prototype in <GL/gl.h> and is not part of the shared table: resolved
    /// by name exactly like the table's own entries. Null only on a context that cannot exist here
    /// (the editor requires 3.3 core), in which case glBlendFunc stands in.
    PFNGLBLENDFUNCSEPARATEPROC blendFuncSeparate = nullptr;

    GLuint scratchFramebuffer = 0U;
    GLuint scratchDepthRenderbuffer = 0U;
    int scratchWidth = 0;
    int scratchHeight = 0;
};

namespace {

/// Sets the blend factors through the resolved glBlendFuncSeparate, falling back to the GL 1.1
/// glBlendFunc (which a context without the 1.4 entry point cannot distinguish from it anyway).
void SetBlendFactorsUVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const GLenum sourceRgb,
                        const GLenum destinationRgb, const GLenum sourceAlpha, const GLenum destinationAlpha) noexcept {
    if (gl.blendFuncSeparate != nullptr) {
        gl.blendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
    } else {
        glBlendFunc(sourceRgb, destinationRgb);
    }
}

/// Creates (once) and resizes (as needed) the scratch framebuffer + depth-renderbuffer reused by
/// every RenderThumbnailUVE() call, mirroring MeshThumbnailRendererUVE's target management. Leaves
/// GL_FRAMEBUFFER/GL_RENDERBUFFER on the scratch objects; the caller saves/restores around it.
[[nodiscard]] bool EnsureScratchTargetUVE(MaterialThumbnailRendererUVE::GlStateUVE& gl, const int width,
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

/// Box-filters `source` down to `targetWidth` x `targetHeight` into `outPixels`. Every source
/// pixel contributes to exactly the target pixel it covers (integer area ranges), which keeps a
/// 4K albedo's average colour honest instead of point-sampling one texel in every eighth column.
void DownscaleRgba8UVE(const MaterialThumbnailImageUVE& source, const int targetWidth, const int targetHeight,
                       std::vector<std::uint8_t>& outPixels) {
    outPixels.assign(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4U, 0U);
    for (int y = 0; y < targetHeight; ++y) {
        const int sourceY0 = (y * source.height) / targetHeight;
        const int sourceY1 = std::max(sourceY0 + 1, ((y + 1) * source.height) / targetHeight);
        for (int x = 0; x < targetWidth; ++x) {
            const int sourceX0 = (x * source.width) / targetWidth;
            const int sourceX1 = std::max(sourceX0 + 1, ((x + 1) * source.width) / targetWidth);
            std::uint32_t sums[4] = {0U, 0U, 0U, 0U};
            std::uint32_t count = 0U;
            for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY) {
                const std::size_t rowOffset =
                    static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(source.width) * 4U;
                for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX) {
                    const std::size_t offset = rowOffset + static_cast<std::size_t>(sourceX) * 4U;
                    for (std::size_t channel = 0U; channel < 4U; ++channel) {
                        sums[channel] += source.rgba8[offset + channel];
                    }
                    ++count;
                }
            }
            const std::size_t outOffset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(targetWidth) + static_cast<std::size_t>(x)) *
                4U;
            if (count == 0U) {
                continue;
            }
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                outPixels[outOffset + channel] =
                    static_cast<std::uint8_t>((sums[channel] + count / 2U) / count);
            }
        }
    }
}

/// Uploads one preview image as a mipmapped 2D texture. sRGB images use an sRGB internal format so
/// the GPU decodes them exactly like the runtime renderer's sRGB textures do; data maps stay
/// linear. Returns 0 on failure (a zero name, or a GL error after the upload).
[[nodiscard]] GLuint UploadPreviewTextureUVE(const Render::Detail::GlFunctionsUVE& table,
                                             const MaterialThumbnailImageUVE& image,
                                             const bool forceLinear) noexcept {
    if (!image.IsValidUVE()) {
        return 0U;
    }
    const std::uint8_t* pixels = image.rgba8.data();
    int width = image.width;
    int height = image.height;
    std::vector<std::uint8_t> scaled;
    if (width > kMaxPreviewTextureSizeUVE || height > kMaxPreviewTextureSizeUVE) {
        int targetWidth = kMaxPreviewTextureSizeUVE;
        int targetHeight = kMaxPreviewTextureSizeUVE;
        if (width >= height) {
            targetHeight = std::max(1, static_cast<int>((static_cast<std::int64_t>(height) * kMaxPreviewTextureSizeUVE) /
                                                        width));
        } else {
            targetWidth = std::max(1, static_cast<int>((static_cast<std::int64_t>(width) * kMaxPreviewTextureSizeUVE) /
                                                       height));
        }
        DownscaleRgba8UVE(image, targetWidth, targetHeight, scaled);
        if (scaled.empty()) {
            return 0U;
        }
        pixels = scaled.data();
        width = targetWidth;
        height = targetHeight;
    }

    const GLint internalFormat = image.srgb && !forceLinear ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    if (texture == 0U) {
        return 0U;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Materials tile their UVs (uvScale/uvOffset above 1 is a normal authoring choice), so the
    // preview repeats rather than clamps - the same wrap the runtime sampler uses.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    table.glGenerateMipmap(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0U;
    }
    return texture;
}

[[nodiscard]] GLint UniformLocationUVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl,
                                       const PreviewUniformUVE uniform) noexcept {
    return gl.uniformLocations[static_cast<std::size_t>(uniform)];
}

void SetFloatUVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const PreviewUniformUVE uniform,
                 const float value) noexcept {
    const GLint location = UniformLocationUVE(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform1f(location, value);
    }
}

void SetIntUVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const PreviewUniformUVE uniform,
               const int value) noexcept {
    const GLint location = UniformLocationUVE(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform1i(location, value);
    }
}

void SetBoolUVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const PreviewUniformUVE uniform,
                const bool value) noexcept {
    SetIntUVE(gl, uniform, value ? 1 : 0);
}

void SetVector2UVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const PreviewUniformUVE uniform,
                   const float* const values) noexcept {
    const GLint location = UniformLocationUVE(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform2fv(location, 1, values);
    }
}

void SetVector3UVE(const MaterialThumbnailRendererUVE::GlStateUVE& gl, const PreviewUniformUVE uniform,
                   const float* const values) noexcept {
    const GLint location = UniformLocationUVE(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform3fv(location, 1, values);
    }
}

} // namespace

MaterialThumbnailRendererUVE::MaterialThumbnailRendererUVE() = default;
MaterialThumbnailRendererUVE::~MaterialThumbnailRendererUVE() = default;

void MaterialThumbnailRendererUVE::InitializeUVE() {
    m_gl = std::make_unique<GlStateUVE>();
    GlStateUVE& gl = *m_gl;

    gl.table = Render::Detail::LoadGlFunctionsUVE(&GrabGlProcAddressUVE);
    gl.functionsLoaded = gl.table.IsCompleteUVE();
    gl.blendFuncSeparate =
        reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(GrabGlProcAddressUVE("glBlendFuncSeparate"));
    if (!gl.functionsLoaded) {
        return;
    }

    GLuint vertexShader = 0U;
    GLuint fragmentShader = 0U;
    const bool vertexCompiled = CompileShaderStageUVE(gl.table, GL_VERTEX_SHADER, kVertexShaderSourceUVE, vertexShader);
    const bool fragmentCompiled =
        vertexCompiled &&
        CompileShaderStageUVE(gl.table, GL_FRAGMENT_SHADER, kFragmentShaderSourceUVE, fragmentShader);
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
    for (std::size_t index = 0U; index < kPreviewUniformCountUVE; ++index) {
        gl.uniformLocations[index] = gl.table.glGetUniformLocation(gl.shaderProgram, kPreviewUniformNamesUVE[index]);
    }
    gl.programLinked = true;

    // The preview shape never changes, so it is uploaded once - unlike a mesh preview, whose
    // geometry differs per asset and is streamed per call.
    std::vector<PreviewVertexUVE> vertices;
    std::vector<std::uint32_t> indices;
    BuildPreviewSphereUVE(vertices, indices);
    gl.table.glGenVertexArrays(1, &gl.previewVertexArray);
    gl.table.glGenBuffers(1, &gl.previewVertexBuffer);
    gl.table.glGenBuffers(1, &gl.previewIndexBuffer);
    if (gl.previewVertexArray == 0U || gl.previewVertexBuffer == 0U || gl.previewIndexBuffer == 0U) {
        ShutdownUVE();
        return;
    }
    gl.table.glBindVertexArray(gl.previewVertexArray);
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, gl.previewVertexBuffer);
    gl.table.glBufferData(GL_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(vertices.size() * sizeof(PreviewVertexUVE)), vertices.data(),
                          GL_STATIC_DRAW);
    gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl.previewIndexBuffer);
    gl.table.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                          GL_STATIC_DRAW);
    gl.table.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertexUVE),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertexUVE, position)));
    gl.table.glEnableVertexAttribArray(0);
    gl.table.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertexUVE),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertexUVE, normal)));
    gl.table.glEnableVertexAttribArray(1);
    gl.table.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PreviewVertexUVE),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertexUVE, texCoord)));
    gl.table.glEnableVertexAttribArray(2);
    gl.table.glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertexUVE),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertexUVE, tangent)));
    gl.table.glEnableVertexAttribArray(3);
    gl.table.glBindVertexArray(0);
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, 0);
    // The element-array binding is vertex-array state: with the core profile's default VAO
    // unbound, glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...) is an error, and the preview VAO must
    // keep its own index buffer bound anyway.
    gl.previewIndexCount = static_cast<GLsizei>(indices.size());
}

void MaterialThumbnailRendererUVE::ShutdownUVE() noexcept {
    if (!m_gl) {
        return;
    }
    GlStateUVE& gl = *m_gl;
    if (gl.functionsLoaded) {
        if (gl.previewVertexBuffer != 0U) {
            gl.table.glDeleteBuffers(1, &gl.previewVertexBuffer);
            gl.previewVertexBuffer = 0U;
        }
        if (gl.previewIndexBuffer != 0U) {
            gl.table.glDeleteBuffers(1, &gl.previewIndexBuffer);
            gl.previewIndexBuffer = 0U;
        }
        if (gl.previewVertexArray != 0U) {
            gl.table.glDeleteVertexArrays(1, &gl.previewVertexArray);
            gl.previewVertexArray = 0U;
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
    }
    gl.programLinked = false;
    m_gl.reset();
}

std::uintptr_t MaterialThumbnailRendererUVE::RenderThumbnailUVE(const Asset::MaterialAssetUVE& material,
                                                                const MaterialThumbnailTexturesUVE& textures,
                                                                const int width, const int height) {
    if (!m_gl || !m_gl->functionsLoaded || !m_gl->programLinked) {
        return 0U;
    }
    if (width <= 0 || height <= 0 || m_gl->previewIndexCount <= 0) {
        return 0U;
    }
    GlStateUVE& gl = *m_gl;

    // Frame the fixed sphere to the requested fill fraction. Solving the silhouette angle rather
    // than using a fixed camera distance keeps the preview identical at every thumbnail size.
    const float halfFieldOfView = kFieldOfViewYRadiansUVE * 0.5F;
    const float tangentFill = kSphereFillFractionUVE * std::tan(halfFieldOfView);
    const float distance = kPreviewSphereRadiusUVE * std::sqrt(1.0F + 1.0F / (tangentFill * tangentFill));
    const Math::Vector3UVE viewDirection = Math::NormalizeUVE(Math::Vector3UVE{0.72F, 0.45F, 1.0F});
    const Math::Vector3UVE cameraPosition = viewDirection * distance;

    Math::QuaternionUVE cameraRotation{};
    if (!Math::TryMakeLookAtUVE(cameraPosition, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, cameraRotation)) {
        return 0U;
    }
    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(cameraPosition, cameraRotation);
    const float nearPlane = std::max(0.01F, distance * kNearPlaneFractionUVE);
    const float farPlane = distance + kPreviewSphereRadiusUVE * 2.0F + nearPlane;
    const Math::Matrix4x4UVE projection = Math::Matrix4x4UVE::PerspectiveUVE(
        kFieldOfViewYRadiansUVE, static_cast<float>(width) / static_cast<float>(height), nearPlane, farPlane);
    const Math::Matrix4x4UVE viewProjection = projection * view;
    const float cameraPositionValues[3] = {cameraPosition.x, cameraPosition.y, cameraPosition.z};

    const bool blendEnabled = Asset::ShouldBucketMaterialTransparentUVE(material);

    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    GLint previousVertexArray = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    GLint previousElementArrayBuffer = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementArrayBuffer);
    GLint previousActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    constexpr std::size_t kPreviewTextureUnitCountUVE = 5U;
    std::array<GLint, kPreviewTextureUnitCountUVE> previousTextureBindings{};
    for (std::size_t unit = 0U; unit < kPreviewTextureUnitCountUVE; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBindings[unit]);
    }
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencilTestWasEnabled = glIsEnabled(GL_STENCIL_TEST);
    GLint previousCullFaceMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFaceMode);
    GLint previousFrontFace = GL_CCW;
    glGetIntegerv(GL_FRONT_FACE, &previousFrontFace);
    GLint previousBlendSourceRgb = GL_ONE;
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSourceRgb);
    GLint previousBlendDestinationRgb = GL_ZERO;
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDestinationRgb);
    GLint previousBlendSourceAlpha = GL_ONE;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSourceAlpha);
    GLint previousBlendDestinationAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDestinationAlpha);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
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

    // Upload the material's maps. A slot whose bytes are missing/invalid uploads nothing, and the
    // corresponding uHas*Texture stays false so the flat material value applies.
    std::array<GLuint, kPreviewTextureUnitCountUVE> materialTextures{};
    if (succeeded) {
        materialTextures[0] = UploadPreviewTextureUVE(gl.table, textures.albedo, /*forceLinear=*/false);
        materialTextures[1] = UploadPreviewTextureUVE(gl.table, textures.normal, /*forceLinear=*/true);
        materialTextures[2] = UploadPreviewTextureUVE(gl.table, textures.metallicRoughness, /*forceLinear=*/true);
        materialTextures[3] = UploadPreviewTextureUVE(gl.table, textures.ao, /*forceLinear=*/true);
        materialTextures[4] = UploadPreviewTextureUVE(gl.table, textures.emissive, /*forceLinear=*/false);
    }

    if (succeeded) {
        glViewport(0, 0, width, height);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        SetGlCapabilityUVE(GL_DEPTH_TEST, GL_TRUE);
        glDepthMask(GL_TRUE);
        SetGlCapabilityUVE(GL_STENCIL_TEST, GL_FALSE);
        SetGlCapabilityUVE(GL_SCISSOR_TEST, GL_FALSE);
        SetGlCapabilityUVE(GL_CULL_FACE, GL_TRUE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        SetGlCapabilityUVE(GL_BLEND, blendEnabled ? GL_TRUE : GL_FALSE);
        SetBlendFactorsUVE(gl, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl.table.glUseProgram(gl.shaderProgram);
        gl.table.glUniformMatrix4fv(UniformLocationUVE(gl, PreviewUniformUVE::ViewProjection), 1, GL_TRUE,
                                    &viewProjection.m[0][0]);
        SetVector3UVE(gl, PreviewUniformUVE::CameraPosition, cameraPositionValues);

        const float albedoColor[3] = {material.albedoColor.r, material.albedoColor.g, material.albedoColor.b};
        SetVector3UVE(gl, PreviewUniformUVE::AlbedoColor, albedoColor);
        SetFloatUVE(gl, PreviewUniformUVE::AlbedoAlpha, material.albedoAlpha);
        SetFloatUVE(gl, PreviewUniformUVE::Metallic, material.metallic);
        SetFloatUVE(gl, PreviewUniformUVE::Roughness, material.roughness);
        const float uvScale[2] = {material.uvScale.x, material.uvScale.y};
        const float uvOffset[2] = {material.uvOffset.x, material.uvOffset.y};
        SetVector2UVE(gl, PreviewUniformUVE::UvScale, uvScale);
        SetVector2UVE(gl, PreviewUniformUVE::UvOffset, uvOffset);
        SetFloatUVE(gl, PreviewUniformUVE::NormalScale, material.normalScale);
        SetBoolUVE(gl, PreviewUniformUVE::FlipGreen, material.normalFlipGreen);
        SetFloatUVE(gl, PreviewUniformUVE::OcclusionStrength, material.occlusionStrength);
        const float emissiveColor[3] = {material.emissiveColor.r, material.emissiveColor.g,
                                        material.emissiveColor.b};
        SetVector3UVE(gl, PreviewUniformUVE::EmissiveColor, emissiveColor);
        SetFloatUVE(gl, PreviewUniformUVE::EmissiveEnergy, material.emissiveEnergy);
        SetBoolUVE(gl, PreviewUniformUVE::Unshaded, material.unshaded);
        SetFloatUVE(gl, PreviewUniformUVE::AlphaCutoff, material.alphaCutoff);
        SetBoolUVE(gl, PreviewUniformUVE::BlendEnabled, blendEnabled);

        SetBoolUVE(gl, PreviewUniformUVE::HasAlbedoTexture, materialTextures[0] != 0U);
        SetBoolUVE(gl, PreviewUniformUVE::HasNormalTexture, materialTextures[1] != 0U);
        SetBoolUVE(gl, PreviewUniformUVE::HasMetallicRoughnessTexture, materialTextures[2] != 0U);
        SetBoolUVE(gl, PreviewUniformUVE::HasAOTexture, materialTextures[3] != 0U);
        SetBoolUVE(gl, PreviewUniformUVE::HasEmissiveTexture, materialTextures[4] != 0U);
        constexpr std::array<PreviewUniformUVE, kPreviewTextureUnitCountUVE> samplerUniforms{
            PreviewUniformUVE::AlbedoTexture, PreviewUniformUVE::NormalTexture,
            PreviewUniformUVE::MetallicRoughnessTexture, PreviewUniformUVE::AOTexture,
            PreviewUniformUVE::EmissiveTexture};
        for (std::size_t unit = 0U; unit < kPreviewTextureUnitCountUVE; ++unit) {
            gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, materialTextures[unit]);
            SetIntUVE(gl, samplerUniforms[unit], static_cast<int>(unit));
        }

        SetVector3UVE(gl, PreviewUniformUVE::KeyDirection, kKeyLightDirectionUVE);
        SetVector3UVE(gl, PreviewUniformUVE::KeyColor, kKeyLightColorUVE);
        SetVector3UVE(gl, PreviewUniformUVE::FillDirection, kFillLightDirectionUVE);
        SetVector3UVE(gl, PreviewUniformUVE::FillColor, kFillLightColorUVE);
        SetVector3UVE(gl, PreviewUniformUVE::RimDirection, kRimLightDirectionUVE);
        SetVector3UVE(gl, PreviewUniformUVE::RimColor, kRimLightColorUVE);
        SetVector3UVE(gl, PreviewUniformUVE::SkyAmbient, kSkyAmbientUVE);
        SetVector3UVE(gl, PreviewUniformUVE::GroundAmbient, kGroundAmbientUVE);

        gl.table.glBindVertexArray(gl.previewVertexArray);
        glDrawElements(GL_TRIANGLES, gl.previewIndexCount, GL_UNSIGNED_INT, nullptr);
        succeeded = glGetError() == GL_NO_ERROR;
        gl.table.glBindVertexArray(0);
    }

    // The texture is rendered larger than it is ever drawn, so it needs a mip chain: without one,
    // a card sampling 256 px down to 64 with bilinear filtering reads four texels and still
    // aliases. The mipmap min filter is only switched on once the chain exists - a texture whose
    // mip levels are missing would render black, not just aliased.
    if (succeeded) {
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        gl.table.glGenerateMipmap(GL_TEXTURE_2D);
        if (glGetError() == GL_NO_ERROR) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
    }

    for (GLuint& texture : materialTextures) {
        if (texture != 0U) {
            glDeleteTextures(1, &texture);
            texture = 0U;
        }
    }
    if (colorTexture != 0U) {
        gl.table.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }

    gl.table.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    gl.table.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    gl.table.glUseProgram(static_cast<GLuint>(previousProgram));
    // The previous VAO comes back first: the element-array binding belongs to whichever VAO is
    // bound, so restoring it with no VAO bound would be a core-profile GL error rather than a
    // state restore. ARRAY_BUFFER is not VAO state and can be rebound before or after.
    gl.table.glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));
    for (std::size_t unit = 0U; unit < kPreviewTextureUnitCountUVE; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBindings[unit]));
    }
    gl.table.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    SetGlCapabilityUVE(GL_DEPTH_TEST, depthTestWasEnabled);
    SetGlCapabilityUVE(GL_CULL_FACE, cullFaceWasEnabled);
    SetGlCapabilityUVE(GL_BLEND, blendWasEnabled);
    SetGlCapabilityUVE(GL_SCISSOR_TEST, scissorWasEnabled);
    SetGlCapabilityUVE(GL_STENCIL_TEST, stencilTestWasEnabled);
    glCullFace(static_cast<GLenum>(previousCullFaceMode));
    glFrontFace(static_cast<GLenum>(previousFrontFace));
    SetBlendFactorsUVE(gl, static_cast<GLenum>(previousBlendSourceRgb),
                       static_cast<GLenum>(previousBlendDestinationRgb),
                       static_cast<GLenum>(previousBlendSourceAlpha),
                       static_cast<GLenum>(previousBlendDestinationAlpha));
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
