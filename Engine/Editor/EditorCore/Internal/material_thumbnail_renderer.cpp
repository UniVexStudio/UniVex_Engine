// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "material_thumbnail_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <cstring>
#include <vector>

// See uve/rhi_opengl/gl_functions_uve.h for why glext.h is safe to include here: it only
// supplies Khronos PFNGL*PROC typedefs and GL_* constants, never a loader library. The GL proc
// loading itself is not hand-rolled - this class shares the engine's one contract loader, exactly
// like MeshThumbnailRenderer. The GL surface stays confined to this .cpp: the public header
// never includes a GL header.
#include "gl_system_headers.h"
#include <GLFW/glfw3.h>

#include "gl_functions.h"
#include "logging_macros.h"
#include "prism/GlBackend.h"

#include "material_asset.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Editor {

namespace {

constexpr float kPi = 3.14159265358979323846F;
// A 60 degree lens moved in to the sphere, not a narrow lens from far away: the sphere keeps a
// natural perspective, and every preview frames it to the same fraction, so all are one size.
constexpr float kFieldOfViewYRadians = 1.0471976F;  // 60 degrees
constexpr float kSphereFillFraction = 0.80F;        // of the smaller render dimension
constexpr float kNearPlaneFraction = 0.05F;

// A preview never needs more detail than this from a source map: the sphere covers at most a few
// hundred pixels, and a 4K albedo would otherwise cost 64 MB of upload per cache miss for pixels
// no one can see. Larger maps are box-filtered down on the CPU before upload.
constexpr int kMaxPreviewTextureSize = 512;

constexpr std::uint32_t kPreviewSphereSlices = 96U;
constexpr std::uint32_t kPreviewSphereStacks = 64U;
constexpr float kPreviewSphereRadius = 0.5F;

// Balanced three-point studio rig (directions point from the surface toward each light) plus a
// hemisphere ambient. Calibrated so whites and metals stay realistic and never blow out, while
// shadow sides still show surface roughness and normal detail.
constexpr float kKeyLightDirection[3] = {-0.45F, 0.85F, 0.55F};
constexpr float kKeyLightColor[3] = {2.05F, 1.98F, 1.88F};
constexpr float kFillLightDirection[3] = {0.75F, -0.15F, 0.65F};
constexpr float kFillLightColor[3] = {0.42F, 0.46F, 0.54F};
constexpr float kRimLightDirection[3] = {0.10F, 0.46F, -0.95F};
constexpr float kRimLightColor[3] = {0.55F, 0.58F, 0.65F};
constexpr float kSkyAmbient[3] = {0.16F, 0.18F, 0.22F};
constexpr float kGroundAmbient[3] = {0.045F, 0.042F, 0.038F};

constexpr const char* kVertexShaderSource = R"GLSL(#version 330 core
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

constexpr const char* kFragmentShaderSource = R"GLSL(#version 330 core
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
uniform float uDielectricSpecular;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform int uUv1Triplanar;
uniform float uUv1TriplanarSharpness;
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
// Each map's own reading, as the runtime lit shader reads it (see its ReadMap): per map
// (albedo, normal, metallic-roughness, AO, emissive) Transform is (tiling - 1, offset) and Control
// is (channel: 0 RGB, 1-4 R/G/B/A; invert; 1 - strength; on UV2). All zero reads it as authored.
uniform vec4 uMapTransform[5];
uniform vec4 uMapControl[5];
uniform vec2 uUv2Scale;
uniform vec2 uUv2Offset;
uniform int uUv2Triplanar;
uniform float uUv2TriplanarSharpness;

uniform vec3 uKeyDirection;
uniform vec3 uKeyColor;
uniform vec3 uFillDirection;
uniform vec3 uFillColor;
uniform vec3 uRimDirection;
uniform vec3 uRimColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;

// Cook-Torrance GGX: D (Trowbridge-Reitz), Smith height-correlated visibility, Schlick Fresnel.
float DistributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalfway = max(dot(normal, halfway), 0.0);
    float denominator = normalDotHalfway * normalDotHalfway * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPi * denominator * denominator, 1.0e-5);
}

float VisibilitySmithGGXCorrelated(float normalDotView, float normalDotLight, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float viewTerm = normalDotLight * sqrt(normalDotView * normalDotView * (1.0 - alphaSquared) + alphaSquared);
    float lightTerm = normalDotView * sqrt(normalDotLight * normalDotLight * (1.0 - alphaSquared) + alphaSquared);
    return 0.5 / max(viewTerm + lightTerm, 1.0e-5);
}

vec3 FresnelSchlick(float viewDotHalfway, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * pow(1.0 - viewDotHalfway, 5.0);
}

vec3 DirectLight(vec3 normal, vec3 view, vec3 lightDirection, vec3 radiance, vec3 albedo,
                    vec3 baseReflectance, float metallic, float roughness) {
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    if (normalDotLight <= 0.0) {
        return vec3(0.0);
    }
    vec3 halfway = normalize(view + lightDirection);
    float normalDotView = max(dot(normal, view), 1.0e-4);
    float distribution = DistributionGGX(normal, halfway, roughness);
    float visibility = VisibilitySmithGGXCorrelated(normalDotView, normalDotLight, roughness);
    vec3 fresnel = FresnelSchlick(max(dot(halfway, view), 0.0), baseReflectance);
    vec3 specular = fresnel * distribution * visibility;
    // Energy conservation: whatever the specular lobe reflects is not available to the diffuse
    // lobe, and a metal has no diffuse response at all.
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuseWeight * albedo / kPi + specular) * radiance * normalDotLight;
}

// ACES filmic curve (Narkowicz 2015 fit). The preview is display-referred, so the linear radiance
// the BRDF produces has to be compressed before it can be shown as an 8-bit sRGB image; without
// this, every highlight above 1.0 clips to a flat white disc.
vec3 ToneMap(vec3 color) {
    vec3 numerator = color * (2.51 * color + 0.03);
    vec3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return clamp(numerator / denominator, 0.0, 1.0);
}

// Exact sRGB transfer function (not a 2.2 gamma approximation), matching Math::LinearToDisplay.
vec3 LinearToDisplay(vec3 color) {
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(color, vec3(0.0031308)));
}

struct PreviewUvSet {
    int triplanar;
    vec2 uv;
    vec2 uvX;
    vec2 uvY;
    vec2 uvZ;
    vec3 weights;
    vec3 triNormal;
};

PreviewUvSet BuildPreviewUvSet(vec2 scale, vec2 offset, int triplanar, float sharpness) {
    PreviewUvSet set;
    set.triplanar = triplanar;
    set.uv = vTexCoord * scale + offset;
    set.uvX = set.uv;
    set.uvY = set.uv;
    set.uvZ = set.uv;
    set.weights = vec3(0.0, 1.0, 0.0);
    set.triNormal = normalize(vNormal);
    if (triplanar != 0) {
        vec3 pos = vWorldPosition;
        vec3 n = set.triNormal;
        vec3 w = pow(max(abs(n), vec3(1.0e-4)), vec3(max(sharpness, 0.001)));
        set.weights = w / max(dot(w, vec3(1.0)), 1.0e-5);
        vec2 pX = vec2(n.x < 0.0 ? -pos.z : pos.z, -pos.y);
        vec2 pY = vec2(pos.x, n.y < 0.0 ? -pos.z : pos.z);
        vec2 pZ = vec2(n.z < 0.0 ? -pos.x : pos.x, -pos.y);
        set.uvX = pX * scale + offset;
        set.uvY = pY * scale + offset;
        set.uvZ = pZ * scale + offset;
    }
    return set;
}

vec4 ReadMap(sampler2D map, int slot, PreviewUvSet uv1, PreviewUvSet uv2, vec4 neutral) {
    vec4 transform = uMapTransform[slot];
    vec4 control = uMapControl[slot];
    PreviewUvSet set = control.w > 0.5 ? uv2 : uv1;
    vec2 scale = vec2(1.0) + transform.xy;
    vec4 value;
    if (set.triplanar != 0) {
        value = texture(map, set.uvX * scale + transform.zw) * set.weights.x +
                texture(map, set.uvY * scale + transform.zw) * set.weights.y +
                texture(map, set.uvZ * scale + transform.zw) * set.weights.z;
    } else {
        value = texture(map, set.uv * scale + transform.zw);
    }
    int channel = int(control.x + 0.5);
    if (channel >= 1) {
        float grey = channel == 1 ? value.r : channel == 2 ? value.g : channel == 3 ? value.b : value.a;
        value = vec4(grey, grey, grey, channel == 4 ? 1.0 : value.a);
    }
    if (control.y > 0.5) {
        value.rgb = vec3(1.0) - value.rgb;
    }
    return mix(value, neutral, clamp(control.z, 0.0, 1.0));
}

vec3 SamplePreviewTriplanarNormal(sampler2D normalMap, PreviewUvSet set, vec4 transform) {
    vec2 scale = vec2(1.0) + transform.xy;
    vec3 tX = texture(normalMap, set.uvX * scale + transform.zw).xyz * 2.0 - 1.0;
    vec3 tY = texture(normalMap, set.uvY * scale + transform.zw).xyz * 2.0 - 1.0;
    vec3 tZ = texture(normalMap, set.uvZ * scale + transform.zw).xyz * 2.0 - 1.0;
    if (uFlipGreen) {
        tX.y = -tX.y;
        tY.y = -tY.y;
        tZ.y = -tZ.y;
    }
    tX.xy *= uNormalScale;
    tY.xy *= uNormalScale;
    tZ.xy *= uNormalScale;
    vec3 n = set.triNormal;
    float sX = n.x < 0.0 ? -1.0 : 1.0;
    float sY = n.y < 0.0 ? -1.0 : 1.0;
    float sZ = n.z < 0.0 ? -1.0 : 1.0;
    vec3 nX = vec3(n.x * tX.z, n.y - tX.y, n.z + tX.x * sX);
    vec3 nY = vec3(n.x + tY.x, n.y * tY.z, n.z + tY.y * sY);
    vec3 nZ = vec3(n.x + tZ.x * sZ, n.y - tZ.y, n.z * tZ.z);
    return normalize(nX * set.weights.x + nY * set.weights.y + nZ * set.weights.z);
}

void main() {
    PreviewUvSet uv1 = BuildPreviewUvSet(uUvScale, uUvOffset, uUv1Triplanar, uUv1TriplanarSharpness);
    PreviewUvSet uv2 = BuildPreviewUvSet(uUv2Scale, uUv2Offset, uUv2Triplanar, uUv2TriplanarSharpness);
    vec4 albedoSample = vec4(1.0);
    if (uHasAlbedoTexture) {
        albedoSample = ReadMap(uAlbedoTexture, 0, uv1, uv2, vec4(1.0));
    }
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    vec3 albedo = albedoSample.rgb * uAlbedoColor;
    float surfaceAlpha = clamp(albedoSample.a * uAlbedoAlpha, 0.0, 1.0);
    float outputAlpha = uBlendEnabled ? surfaceAlpha : 1.0;
    if (uUnshaded) {
        FragColor = vec4(LinearToDisplay(clamp(albedo, 0.0, 1.0)), outputAlpha);
        return;
    }

    vec3 shadingNormal = normalize(vNormal);
    if (uHasNormalTexture && uMapControl[1].z < 0.999) {
        PreviewUvSet normalSet = uMapControl[1].w > 0.5 ? uv2 : uv1;
        if (normalSet.triplanar != 0) {
            vec3 triNormal = SamplePreviewTriplanarNormal(uNormalTexture, normalSet, uMapTransform[1]);
            shadingNormal = normalize(mix(triNormal, shadingNormal, clamp(uMapControl[1].z, 0.0, 1.0)));
        } else {
            vec3 tangent = vTangent - shadingNormal * dot(shadingNormal, vTangent);
            if (dot(tangent, tangent) <= 1.0e-8) {
                vec3 fallbackAxis = abs(shadingNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                tangent = cross(fallbackAxis, shadingNormal);
            }
            tangent = normalize(tangent);
            vec3 bitangent = normalize(cross(shadingNormal, tangent));
            vec3 tangentSpaceNormal = ReadMap(uNormalTexture, 1, uv1, uv2, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            if (uFlipGreen) {
                tangentSpaceNormal.y = -tangentSpaceNormal.y;
            }
            tangentSpaceNormal.xy *= uNormalScale;
            shadingNormal = normalize(mat3(tangent, bitangent, shadingNormal) * tangentSpaceNormal);
        }
    }

    vec3 view = normalize(uCameraPosition - vWorldPosition);
    // Two-sided safety, exactly like the runtime material shader: a preview must never show a
    // black hole where a normal happens to face away from the camera.
    if (dot(shadingNormal, view) < 0.0) {
        shadingNormal = -shadingNormal;
    }

    float metallic = uMetallic;
    float roughness = uRoughness;
    if (uHasMetallicRoughnessTexture) {
        vec3 metallicRoughness = ReadMap(uMetallicRoughnessTexture, 2, uv1, uv2, vec4(1.0)).rgb;
        metallic *= metallicRoughness.b;
        roughness *= metallicRoughness.g;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    // A perfectly smooth surface under a directional light is a single-pixel highlight (and a
    // division by a near-zero normal distribution), so the preview clamps to a usable minimum.
    roughness = clamp(roughness, 0.045, 1.0);

    float occlusion = 1.0;
    if (uHasAOTexture) {
        occlusion = mix(1.0, ReadMap(uAOTexture, 3, uv1, uv2, vec4(1.0)).r, clamp(uOcclusionStrength, 0.0, 1.0));
    }

    float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    vec3 baseReflectance = mix(vec3(0.16 * dielectricSpec * dielectricSpec), albedo, metallic);
    vec3 color = DirectLight(shadingNormal, view, normalize(uKeyDirection), uKeyColor, albedo, baseReflectance,
                                metallic, roughness) +
                 DirectLight(shadingNormal, view, normalize(uFillDirection), uFillColor, albedo, baseReflectance,
                                metallic, roughness) +
                 DirectLight(shadingNormal, view, normalize(uRimDirection), uRimColor, albedo, baseReflectance,
                                metallic, roughness);

    // Ambient as a hemisphere environment: diffuse gets the irradiance for the surface normal, and
    // specular gets the one for the reflection vector. A metal lit only by ambient would otherwise
    // be black - the reflection lobe is a metal's entire response.
    vec3 irradiance = mix(uGroundAmbient, uSkyAmbient, shadingNormal.y * 0.5 + 0.5) * occlusion;
    vec3 diffuseAmbient = albedo * (1.0 - metallic) * irradiance;
    vec3 reflection = reflect(-view, shadingNormal);
    vec3 environment = mix(uGroundAmbient, uSkyAmbient, reflection.y * 0.5 + 0.5);
    float normalDotView = max(dot(shadingNormal, view), 0.0);
    // Roughness-aware Fresnel: a rough surface does not get the bright grazing-angle rim that
    // Schlick's white ceiling would give it.
    vec3 ambientFresnel =
        baseReflectance + (max(vec3(1.0 - roughness), baseReflectance) - baseReflectance) * pow(1.0 - normalDotView, 5.0);
    vec3 specularAmbient = environment * ambientFresnel * mix(1.0, occlusion, 0.6);
    color += diffuseAmbient + specularAmbient;

    if (uHasEmissiveTexture) {
        color += ReadMap(uEmissiveTexture, 4, uv1, uv2, vec4(1.0)).rgb * uEmissiveColor * uEmissiveEnergy;
    } else {
        color += uEmissiveColor * uEmissiveEnergy;
    }

    FragColor = vec4(LinearToDisplay(ToneMap(color)), outputAlpha);
}
)GLSL";

/// One sphere vertex. Positions/normals/tangents are in the preview sphere's own space, which is
/// also the world space the shader and camera work in (the sphere is the only object drawn).
struct PreviewVertex final {
    float position[3];
    float normal[3];
    float texCoord[2];
    float tangent[3];
};

enum class PreviewUniform : std::size_t {
    ViewProjection = 0,
    CameraPosition,
    AlbedoColor,
    AlbedoAlpha,
    Metallic,
    Roughness,
    DielectricSpecular,
    UvScale,
    UvOffset,
    Uv1Triplanar,
    Uv1TriplanarSharpness,
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
    MapTransform,
    MapControl,
    Uv2Scale,
    Uv2Offset,
    Uv2Triplanar,
    Uv2TriplanarSharpness,
    Count,
};

constexpr std::size_t kPreviewUniformCount = static_cast<std::size_t>(PreviewUniform::Count);

/// Must stay in the same order as PreviewUniform; Initialize() resolves every name once.
constexpr std::array<const char*, kPreviewUniformCount> kPreviewUniformNames{
    "uViewProjection",       "uCameraPosition",   "uAlbedoColor",       "uAlbedoAlpha",      "uMetallic",
    "uRoughness",            "uDielectricSpecular", "uUvScale",         "uUvOffset",         "uUv1Triplanar",
    "uUv1TriplanarSharpness", "uNormalScale",     "uFlipGreen",         "uOcclusionStrength", "uEmissiveColor",
    "uEmissiveEnergy",       "uUnshaded",         "uAlphaCutoff",       "uBlendEnabled",     "uHasAlbedoTexture",
    "uHasNormalTexture",     "uHasMetallicRoughnessTexture", "uHasAOTexture", "uHasEmissiveTexture",
    "uAlbedoTexture",        "uNormalTexture",    "uMetallicRoughnessTexture", "uAOTexture", "uEmissiveTexture",
    "uKeyDirection",         "uKeyColor",         "uFillDirection",     "uFillColor",        "uRimDirection",
    "uRimColor",             "uSkyAmbient",       "uGroundAmbient",     "uMapTransform",     "uMapControl",
    "uUv2Scale",             "uUv2Offset",        "uUv2Triplanar",      "uUv2TriplanarSharpness",
};

void SetGlCapability(const GLenum capability, const GLboolean enabled) noexcept {
    if (enabled == GL_TRUE) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

/// Bridges GLFW's proc-address lookup (GLFWglproc, a void(*)(void)) to the shared GL loader's
/// void*-based signature - the same one-line adapter GlRenderDevice and
/// MeshThumbnailRenderer pass.
[[nodiscard]] void* GrabGlProcAddress(const char* const name) noexcept {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

[[nodiscard]] bool CompileShaderStage(const Render::Detail::GlFunctions& table, const GLenum stage,
                                         const char* const source, GLuint& outShader) noexcept {
    outShader = table.glCreateShader(stage);
    if (outShader == 0U) {
        return false;
    }
    // Written for desktop GLSL 3.30; on OpenGL ES the header becomes GLSL ES 3.00.
    const std::string retargeted =
        prism::glslForContext(source, reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    const char* const retargetedSource = retargeted.c_str();
    table.glShaderSource(outShader, 1, &retargetedSource, nullptr);
    table.glCompileShader(outShader);
    GLint compiled = GL_FALSE;
    table.glGetShaderiv(outShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        char infoLog[1024]{};
        GLsizei logLen = 0;
        if (table.glGetShaderInfoLog != nullptr) {
            table.glGetShaderInfoLog(outShader, static_cast<GLsizei>(sizeof(infoLog) - 1U), &logLen, infoLog);
        }
        UVE_ERROR("MaterialThumbnailRenderer: shader compile failed (stage 0x{:X}): {}",
                  static_cast<unsigned int>(stage), infoLog);
        table.glDeleteShader(outShader);
        outShader = 0U;
        return false;
    }
    return true;
}

/// Builds the fixed preview sphere as UV-sphere geometry with analytic normals and tangents. The
/// seam duplicates its column (u = 0 and u = 1) so a wrapped albedo/normal map has no tear, and
/// the two pole rows emit the usual degenerate triangles instead of needing a separate fan path.
void BuildPreviewSphere(std::vector<PreviewVertex>& outVertices, std::vector<std::uint32_t>& outIndices) {
    const std::size_t columns = static_cast<std::size_t>(kPreviewSphereSlices) + 1U;
    const std::size_t rows = static_cast<std::size_t>(kPreviewSphereStacks) + 1U;
    outVertices.clear();
    outVertices.reserve(columns * rows);
    for (std::uint32_t stack = 0U; stack <= kPreviewSphereStacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(kPreviewSphereStacks);
        const float polar = v * kPi;
        const float sinPolar = std::sin(polar);
        const float cosPolar = std::cos(polar);
        for (std::uint32_t slice = 0U; slice <= kPreviewSphereSlices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kPreviewSphereSlices);
            const float azimuth = u * 2.0F * kPi;
            const float sinAzimuth = std::sin(azimuth);
            const float cosAzimuth = std::cos(azimuth);
            const float normalX = sinPolar * cosAzimuth;
            const float normalZ = sinPolar * sinAzimuth;
            PreviewVertex vertex{};
            vertex.position[0] = normalX * kPreviewSphereRadius;
            vertex.position[1] = cosPolar * kPreviewSphereRadius;
            vertex.position[2] = normalZ * kPreviewSphereRadius;
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
    outIndices.reserve(static_cast<std::size_t>(kPreviewSphereSlices) *
                       static_cast<std::size_t>(kPreviewSphereStacks) * 6U);
    for (std::uint32_t stack = 0U; stack < kPreviewSphereStacks; ++stack) {
        for (std::uint32_t slice = 0U; slice < kPreviewSphereSlices; ++slice) {
            const std::uint32_t topLeft = stack * (kPreviewSphereSlices + 1U) + slice;
            const std::uint32_t topRight = topLeft + 1U;
            const std::uint32_t bottomLeft = topLeft + (kPreviewSphereSlices + 1U);
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
struct MaterialThumbnailRenderer::GlState {
    Render::Detail::GlFunctions table;
    bool functionsLoaded = false;

    GLuint shaderProgram = 0U;
    bool programLinked = false;
    std::array<GLint, kPreviewUniformCount> uniformLocations{};

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
    GLuint scratchColorTexture = 0U;
    GLuint resolveFramebuffer = 0U;
    int scratchWidth = 0;
    int scratchHeight = 0;
};

namespace {

constexpr GLenum kReadFramebuffer = 0x8CA8U;
constexpr GLenum kDrawFramebuffer = 0x8CA9U;

/// Sets the blend factors through the resolved glBlendFuncSeparate, falling back to the GL 1.1
/// glBlendFunc (which a context without the 1.4 entry point cannot distinguish from it anyway).
void SetBlendFactors(const MaterialThumbnailRenderer::GlState& gl, const GLenum sourceRgb,
                        const GLenum destinationRgb, const GLenum sourceAlpha, const GLenum destinationAlpha) noexcept {
    if (gl.blendFuncSeparate != nullptr) {
        gl.blendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
    } else {
        glBlendFunc(sourceRgb, destinationRgb);
    }
}

/// Creates (once) and resizes (as needed) the scratch framebuffer + depth-renderbuffer (+ optional
/// supersampled color texture & resolve framebuffer) reused by every RenderThumbnail() call.
[[nodiscard]] bool EnsureScratchTarget(MaterialThumbnailRenderer::GlState& gl, const int width,
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

/// Box-filters `source` down to `targetWidth` x `targetHeight` into `outPixels`. Every source
/// pixel contributes to exactly the target pixel it covers (integer area ranges), which keeps a
/// 4K albedo's average colour honest instead of point-sampling one texel in every eighth column.
void DownscaleRgba8(const MaterialThumbnailImage& source, const int targetWidth, const int targetHeight,
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
[[nodiscard]] GLuint UploadPreviewTexture(const Render::Detail::GlFunctions& table,
                                             const MaterialThumbnailImage& image,
                                             const bool forceLinear) noexcept {
    if (!image.IsValid()) {
        return 0U;
    }
    const std::uint8_t* pixels = image.rgba8.data();
    int width = image.width;
    int height = image.height;
    std::vector<std::uint8_t> scaled;
    if (width > kMaxPreviewTextureSize || height > kMaxPreviewTextureSize) {
        int targetWidth = kMaxPreviewTextureSize;
        int targetHeight = kMaxPreviewTextureSize;
        if (width >= height) {
            targetHeight = std::max(1, static_cast<int>((static_cast<std::int64_t>(height) * kMaxPreviewTextureSize) /
                                                        width));
        } else {
            targetWidth = std::max(1, static_cast<int>((static_cast<std::int64_t>(width) * kMaxPreviewTextureSize) /
                                                       height));
        }
        DownscaleRgba8(image, targetWidth, targetHeight, scaled);
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

[[nodiscard]] GLint UniformLocation(const MaterialThumbnailRenderer::GlState& gl,
                                       const PreviewUniform uniform) noexcept {
    return gl.uniformLocations[static_cast<std::size_t>(uniform)];
}

void SetFloat(const MaterialThumbnailRenderer::GlState& gl, const PreviewUniform uniform,
                 const float value) noexcept {
    const GLint location = UniformLocation(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform1f(location, value);
    }
}

void SetInt(const MaterialThumbnailRenderer::GlState& gl, const PreviewUniform uniform,
               const int value) noexcept {
    const GLint location = UniformLocation(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform1i(location, value);
    }
}

void SetBool(const MaterialThumbnailRenderer::GlState& gl, const PreviewUniform uniform,
                const bool value) noexcept {
    SetInt(gl, uniform, value ? 1 : 0);
}

void SetVector2(const MaterialThumbnailRenderer::GlState& gl, const PreviewUniform uniform,
                   const float* const values) noexcept {
    const GLint location = UniformLocation(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform2fv(location, 1, values);
    }
}

void SetVector3(const MaterialThumbnailRenderer::GlState& gl, const PreviewUniform uniform,
                   const float* const values) noexcept {
    const GLint location = UniformLocation(gl, uniform);
    if (location >= 0) {
        gl.table.glUniform3fv(location, 1, values);
    }
}

} // namespace

MaterialThumbnailRenderer::MaterialThumbnailRenderer() = default;
MaterialThumbnailRenderer::~MaterialThumbnailRenderer() = default;

void MaterialThumbnailRenderer::Initialize() {
    m_gl = std::make_unique<GlState>();
    GlState& gl = *m_gl;

    gl.table = Render::Detail::LoadGlFunctions(&GrabGlProcAddress);
    gl.functionsLoaded = gl.table.IsComplete();
    gl.blendFuncSeparate =
        reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(GrabGlProcAddress("glBlendFuncSeparate"));
    if (!gl.functionsLoaded) {
        UVE_ERROR("MaterialThumbnailRenderer: OpenGL function table incomplete; PBR material thumbnails disabled");
        return;
    }

    GLuint vertexShader = 0U;
    GLuint fragmentShader = 0U;
    const bool vertexCompiled = CompileShaderStage(gl.table, GL_VERTEX_SHADER, kVertexShaderSource, vertexShader);
    const bool fragmentCompiled =
        vertexCompiled &&
        CompileShaderStage(gl.table, GL_FRAGMENT_SHADER, kFragmentShaderSource, fragmentShader);
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
        char infoLog[1024]{};
        GLsizei logLen = 0;
        if (gl.table.glGetProgramInfoLog != nullptr) {
            gl.table.glGetProgramInfoLog(gl.shaderProgram, static_cast<GLsizei>(sizeof(infoLog) - 1U), &logLen, infoLog);
        }
        UVE_ERROR("MaterialThumbnailRenderer: shader link failed: {}", infoLog);
        gl.table.glDeleteProgram(gl.shaderProgram);
        gl.shaderProgram = 0U;
        return;
    }
    for (std::size_t index = 0U; index < kPreviewUniformCount; ++index) {
        gl.uniformLocations[index] = gl.table.glGetUniformLocation(gl.shaderProgram, kPreviewUniformNames[index]);
    }
    gl.programLinked = true;

    // The preview shape never changes, so it is uploaded once - unlike a mesh preview, whose
    // geometry differs per asset and is streamed per call.
    std::vector<PreviewVertex> vertices;
    std::vector<std::uint32_t> indices;
    BuildPreviewSphere(vertices, indices);
    gl.table.glGenVertexArrays(1, &gl.previewVertexArray);
    gl.table.glGenBuffers(1, &gl.previewVertexBuffer);
    gl.table.glGenBuffers(1, &gl.previewIndexBuffer);
    if (gl.previewVertexArray == 0U || gl.previewVertexBuffer == 0U || gl.previewIndexBuffer == 0U) {
        Shutdown();
        return;
    }
    gl.table.glBindVertexArray(gl.previewVertexArray);
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, gl.previewVertexBuffer);
    gl.table.glBufferData(GL_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(vertices.size() * sizeof(PreviewVertex)), vertices.data(),
                          GL_STATIC_DRAW);
    gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl.previewIndexBuffer);
    gl.table.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                          GL_STATIC_DRAW);
    gl.table.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertex),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertex, position)));
    gl.table.glEnableVertexAttribArray(0);
    gl.table.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertex),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertex, normal)));
    gl.table.glEnableVertexAttribArray(1);
    gl.table.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PreviewVertex),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertex, texCoord)));
    gl.table.glEnableVertexAttribArray(2);
    gl.table.glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewVertex),
                                   reinterpret_cast<const void*>(offsetof(PreviewVertex, tangent)));
    gl.table.glEnableVertexAttribArray(3);
    gl.table.glBindVertexArray(0);
    gl.table.glBindBuffer(GL_ARRAY_BUFFER, 0);
    // The element-array binding is vertex-array state: with the core profile's default VAO
    // unbound, glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...) is an error, and the preview VAO must
    // keep its own index buffer bound anyway.
    gl.previewIndexCount = static_cast<GLsizei>(indices.size());
}

void MaterialThumbnailRenderer::Shutdown() noexcept {
    if (!m_gl) {
        return;
    }
    GlState& gl = *m_gl;
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
    }
    gl.programLinked = false;
    m_gl.reset();
}

std::uintptr_t MaterialThumbnailRenderer::RenderThumbnail(const Asset::MaterialAsset& material,
                                                                const MaterialThumbnailTextures& textures,
                                                                const int width, const int height) {
    if (!m_gl && glfwGetCurrentContext() != nullptr) {
        Initialize();
    }
    if (!m_gl || !m_gl->functionsLoaded || !m_gl->programLinked) {
        return 0U;
    }
    if (width <= 0 || height <= 0 || m_gl->previewIndexCount <= 0) {
        return 0U;
    }
    GlState& gl = *m_gl;

    // Frame the fixed sphere to the requested fill fraction. Solving the silhouette angle rather
    // than using a fixed camera distance keeps the preview identical at every thumbnail size.
    const float halfFieldOfView = kFieldOfViewYRadians * 0.5F;
    const float tangentFill = kSphereFillFraction * std::tan(halfFieldOfView);
    const float distance = kPreviewSphereRadius * std::sqrt(1.0F + 1.0F / (tangentFill * tangentFill));
    const Math::Vector3 viewDirection = Math::Normalize(Math::Vector3{0.72F, 0.45F, 1.0F});
    const Math::Vector3 cameraPosition = viewDirection * distance;

    Math::Quaternion cameraRotation{};
    if (!Math::TryMakeLookAt(cameraPosition, Math::Vector3{0.0F, 1.0F, 0.0F}, cameraRotation)) {
        return 0U;
    }
    const Math::Matrix4x4 view =
        Math::Matrix4x4::ViewFromPositionAndRotation(cameraPosition, cameraRotation);
    const float nearPlane = std::max(0.01F, distance * kNearPlaneFraction);
    const float farPlane = distance + kPreviewSphereRadius * 2.0F + nearPlane;
    const Math::Matrix4x4 projection = Math::Matrix4x4::Perspective(
        kFieldOfViewYRadians, static_cast<float>(width) / static_cast<float>(height), nearPlane, farPlane);
    const Math::Matrix4x4 viewProjection = projection * view;
    const float cameraPositionValues[3] = {cameraPosition.x, cameraPosition.y, cameraPosition.z};

    const bool blendEnabled = Asset::ShouldBucketMaterialTransparent(material);

    // Drain pre-existing GL errors from unrelated passes so glGetError() below only reflects this draw.
    while (glGetError() != GL_NO_ERROR) {
    }

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
    constexpr std::size_t kPreviewTextureUnitCount = 5U;
    std::array<GLint, kPreviewTextureUnitCount> previousTextureBindings{};
    std::array<GLint, kPreviewTextureUnitCount> previousSamplerBindings{};
    for (std::size_t unit = 0U; unit < kPreviewTextureUnitCount; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBindings[unit]);
        if (gl.table.glBindSampler != nullptr) {
            glGetIntegerv(GL_SAMPLER_BINDING, &previousSamplerBindings[unit]);
            gl.table.glBindSampler(static_cast<GLuint>(unit), 0U);
        }
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

    const bool supersample = gl.table.glBlitFramebuffer != nullptr && width <= 1024 && height <= 1024;
    const int renderWidth = supersample ? width * 2 : width;
    const int renderHeight = supersample ? height * 2 : height;
    bool succeeded = EnsureScratchTarget(gl, renderWidth, renderHeight, supersample);

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

    // Upload the material's maps. A slot whose bytes are missing/invalid uploads nothing, and the
    // corresponding uHas*Texture stays false so the flat material value applies.
    std::array<GLuint, kPreviewTextureUnitCount> materialTextures{};
    if (succeeded) {
        materialTextures[0] = UploadPreviewTexture(gl.table, textures.albedo, /*forceLinear=*/false);
        materialTextures[1] = UploadPreviewTexture(gl.table, textures.normal, /*forceLinear=*/true);
        materialTextures[2] = UploadPreviewTexture(gl.table, textures.metallicRoughness, /*forceLinear=*/true);
        materialTextures[3] = UploadPreviewTexture(gl.table, textures.ao, /*forceLinear=*/true);
        materialTextures[4] = UploadPreviewTexture(gl.table, textures.emissive, /*forceLinear=*/false);
    }

    if (succeeded) {
        glViewport(0, 0, renderWidth, renderHeight);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        SetGlCapability(GL_DEPTH_TEST, GL_TRUE);
        glDepthMask(GL_TRUE);
        SetGlCapability(GL_STENCIL_TEST, GL_FALSE);
        SetGlCapability(GL_SCISSOR_TEST, GL_FALSE);
        SetGlCapability(GL_CULL_FACE, GL_TRUE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        SetGlCapability(GL_BLEND, blendEnabled ? GL_TRUE : GL_FALSE);
        SetBlendFactors(gl, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl.table.glUseProgram(gl.shaderProgram);
        gl.table.glUniformMatrix4fv(UniformLocation(gl, PreviewUniform::ViewProjection), 1, GL_TRUE,
                                    &viewProjection.m[0][0]);
        SetVector3(gl, PreviewUniform::CameraPosition, cameraPositionValues);

        const float albedoColor[3] = {material.albedoColor.r, material.albedoColor.g, material.albedoColor.b};
        SetVector3(gl, PreviewUniform::AlbedoColor, albedoColor);
        SetFloat(gl, PreviewUniform::AlbedoAlpha, material.albedoAlpha);
        SetFloat(gl, PreviewUniform::Metallic, material.metallic);
        SetFloat(gl, PreviewUniform::Roughness, material.roughness);
        SetFloat(gl, PreviewUniform::DielectricSpecular, std::clamp(material.dielectricSpecular, 0.0F, 1.0F));
        const float uvScale[2] = {material.uvScale.x, material.uvScale.y};
        const float uvOffset[2] = {material.uvOffset.x, material.uvOffset.y};
        SetVector2(gl, PreviewUniform::UvScale, uvScale);
        SetVector2(gl, PreviewUniform::UvOffset, uvOffset);
        SetInt(gl, PreviewUniform::Uv1Triplanar,
                  material.worldSpaceTriplanar ? 2 : (material.triplanarEnabled ? 1 : 0));
        SetFloat(gl, PreviewUniform::Uv1TriplanarSharpness, material.triplanarBlendSharpness);
        SetFloat(gl, PreviewUniform::NormalScale, material.normalScale);
        SetBool(gl, PreviewUniform::FlipGreen, material.normalFlipGreen);
        SetFloat(gl, PreviewUniform::OcclusionStrength, material.occlusionStrength);
        const float emissiveColor[3] = {material.emissiveColor.r, material.emissiveColor.g,
                                        material.emissiveColor.b};
        SetVector3(gl, PreviewUniform::EmissiveColor, emissiveColor);
        SetFloat(gl, PreviewUniform::EmissiveEnergy, material.emissiveEnergy);
        SetBool(gl, PreviewUniform::Unshaded, material.unshaded);
        SetFloat(gl, PreviewUniform::AlphaCutoff, material.alphaCutoff);
        SetBool(gl, PreviewUniform::BlendEnabled, blendEnabled);

        SetBool(gl, PreviewUniform::HasAlbedoTexture, materialTextures[0] != 0U);
        SetBool(gl, PreviewUniform::HasNormalTexture, materialTextures[1] != 0U);
        SetBool(gl, PreviewUniform::HasMetallicRoughnessTexture, materialTextures[2] != 0U);
        SetBool(gl, PreviewUniform::HasAOTexture, materialTextures[3] != 0U);
        SetBool(gl, PreviewUniform::HasEmissiveTexture, materialTextures[4] != 0U);
        constexpr std::array<PreviewUniform, kPreviewTextureUnitCount> samplerUniforms{
            PreviewUniform::AlbedoTexture, PreviewUniform::NormalTexture,
            PreviewUniform::MetallicRoughnessTexture, PreviewUniform::AOTexture,
            PreviewUniform::EmissiveTexture};
        for (std::size_t unit = 0U; unit < kPreviewTextureUnitCount; ++unit) {
            gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, materialTextures[unit]);
            SetInt(gl, samplerUniforms[unit], static_cast<int>(unit));
        }

        SetVector3(gl, PreviewUniform::KeyDirection, kKeyLightDirection);
        SetVector3(gl, PreviewUniform::KeyColor, kKeyLightColor);
        SetVector3(gl, PreviewUniform::FillDirection, kFillLightDirection);
        SetVector3(gl, PreviewUniform::FillColor, kFillLightColor);
        SetVector3(gl, PreviewUniform::RimDirection, kRimLightDirection);
        SetVector3(gl, PreviewUniform::RimColor, kRimLightColor);
        SetVector3(gl, PreviewUniform::SkyAmbient, kSkyAmbient);
        SetVector3(gl, PreviewUniform::GroundAmbient, kGroundAmbient);

        // Each map read as the runtime reads it: its own tiling, offset, channel, invert and strength.
        const float uv2Scale[2] = {material.uv2Scale.x, material.uv2Scale.y};
        const float uv2Offset[2] = {material.uv2Offset.x, material.uv2Offset.y};
        SetVector2(gl, PreviewUniform::Uv2Scale, uv2Scale);
        SetVector2(gl, PreviewUniform::Uv2Offset, uv2Offset);
        SetInt(gl, PreviewUniform::Uv2Triplanar,
               material.uv2WorldTriplanar ? 2 : (material.uv2Triplanar ? 1 : 0));
        SetFloat(gl, PreviewUniform::Uv2TriplanarSharpness, material.uv2TriplanarSharpness);
        constexpr std::array<Asset::MaterialMap, 5> kPreviewMaps{
            Asset::MaterialMap::Albedo, Asset::MaterialMap::Normal, Asset::MaterialMap::MetallicRoughness,
            Asset::MaterialMap::Occlusion, Asset::MaterialMap::Emissive};
        std::array<float, 20> mapTransforms{};
        std::array<float, 20> mapControls{};
        for (std::size_t slot = 0U; slot < kPreviewMaps.size(); ++slot) {
            const Asset::MaterialMapSettings& settings = material.Map(kPreviewMaps[slot]);
            const bool onUv2 = settings.onUv2 || (slot == 3U && material.aoOnUv2) || (slot == 4U && material.emissionOnUv2);
            mapTransforms[slot * 4U + 0U] = settings.tiling.x - 1.0F;
            mapTransforms[slot * 4U + 1U] = settings.tiling.y - 1.0F;
            mapTransforms[slot * 4U + 2U] = settings.offset.x;
            mapTransforms[slot * 4U + 3U] = settings.offset.y;
            mapControls[slot * 4U + 0U] = settings.channel == Asset::MaterialTextureChannel::Rgb
                                              ? 0.0F
                                              : static_cast<float>(static_cast<int>(settings.channel) + 1);
            mapControls[slot * 4U + 1U] = settings.invert ? 1.0F : 0.0F;
            mapControls[slot * 4U + 2U] = 1.0F - std::clamp(settings.strength, 0.0F, 1.0F);
            mapControls[slot * 4U + 3U] = onUv2 ? 1.0F : 0.0F;
        }
        if (const GLint location = UniformLocation(gl, PreviewUniform::MapTransform); location >= 0) {
            gl.table.glUniform4fv(location, 5, mapTransforms.data());
        }
        if (const GLint location = UniformLocation(gl, PreviewUniform::MapControl); location >= 0) {
            gl.table.glUniform4fv(location, 5, mapControls.data());
        }

        gl.table.glBindVertexArray(gl.previewVertexArray);
        glDrawElements(GL_TRIANGLES, gl.previewIndexCount, GL_UNSIGNED_INT, nullptr);
        succeeded = glGetError() == GL_NO_ERROR;
        gl.table.glBindVertexArray(0);
    }

    if (succeeded && supersample) {
        gl.table.glBindFramebuffer(kReadFramebuffer, gl.scratchFramebuffer);
        gl.table.glBindFramebuffer(kDrawFramebuffer, gl.resolveFramebuffer);
        gl.table.glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        succeeded = glGetError() == GL_NO_ERROR;
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
        gl.table.glBindFramebuffer(GL_FRAMEBUFFER, supersample ? gl.resolveFramebuffer : gl.scratchFramebuffer);
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
    if (previousVertexArray != 0) {
        gl.table.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));
    }
    for (std::size_t unit = 0U; unit < kPreviewTextureUnitCount; ++unit) {
        gl.table.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBindings[unit]));
        if (gl.table.glBindSampler != nullptr) {
            gl.table.glBindSampler(static_cast<GLuint>(unit), static_cast<GLuint>(previousSamplerBindings[unit]));
        }
    }
    gl.table.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    SetGlCapability(GL_DEPTH_TEST, depthTestWasEnabled);
    SetGlCapability(GL_CULL_FACE, cullFaceWasEnabled);
    SetGlCapability(GL_BLEND, blendWasEnabled);
    SetGlCapability(GL_SCISSOR_TEST, scissorWasEnabled);
    SetGlCapability(GL_STENCIL_TEST, stencilTestWasEnabled);
    glCullFace(static_cast<GLenum>(previousCullFaceMode));
    glFrontFace(static_cast<GLenum>(previousFrontFace));
    SetBlendFactors(gl, static_cast<GLenum>(previousBlendSourceRgb),
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
