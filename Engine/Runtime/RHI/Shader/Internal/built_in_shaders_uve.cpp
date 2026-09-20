// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


// Every constant below is a raw string literal transcribed byte-for-byte from its corresponding
// .glsl file under engine/render/shader/built_in/ - kept in sync by convention, enforced by
// tests/render/shader/built_in_shaders_parity_uve_tests.cpp (reads the real file and asserts
// exact equality against the constant here). Used automatically by ShaderManagerUVE as a fallback
// when the corresponding virtual path isn't reachable (see ShaderProgramDescUVE's doc comment).

#include "uve/rhi_shader/built_in_shaders_uve.h"

namespace UVE::Render::Shader::BuiltIn {

const std::string_view kBasic2DSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uProjection;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uModel * vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform vec4 uColor;

void main() {
    FragColor = uColor;
}
#endif
)GLSLSRC";

const std::string_view kBasic3DSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
out vec4 FragColor;

uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kBasic3DTexturedSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uViewProjection;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

// Placeholder only - no CreateTextureUVE-backed binding exists yet this increment
// (MaterialSystemUVE, a future increment, is what actually binds a real texture here).
uniform sampler2D uTexture;

void main() {
    FragColor = texture(uTexture, vTexCoord);
}
#endif
)GLSLSRC";

const std::string_view kFullscreenQuadSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;

vec3 AcesToneMapUVE(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrColor = max(texture(uSourceTexture, vTexCoord).rgb, vec3(0.0));
    FragColor = vec4(AcesToneMapUVE(hdrColor), 1.0);
}
#endif
)GLSLSRC";

const std::string_view kShadowDepthSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

#ifdef UVE_INSTANCED
// The instanced shadow variant, mirroring lit_shadowed_3d.glsl's arrangement: same define name,
// same binding 0, same transposed upload convention, same uInstanceBaseIndex + gl_InstanceID
// indexing. One rule across both shaders rather than two.
//
// Only the model matrix is needed here - a depth-only pass has no normals to transform - so this
// binds one buffer where the lit shader binds three. The base-index buffer is still required
// because gl_InstanceID restarts at zero for every draw while the frame shares one upload.
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#else
uniform mat4 uModel;
#endif
uniform mat4 uLightSpaceMatrix;

void main() {
#ifdef UVE_INSTANCED
    mat4 model = instanceModels[uInstanceBaseIndex + gl_InstanceID];
#else
    mat4 model = uModel;
#endif
    gl_Position = uLightSpaceMatrix * model * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
void main() {
    // Depth-only pass: no color attachment bound, nothing to write.
}
#endif
)GLSLSRC";

const std::string_view kLitShadowed3DSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vWorldTangent;
out float vTangentHandedness;
out vec2 vTexCoord;
out vec4 vLightSpacePosition;
out vec4 vLightSpacePositions[3];

#ifdef UVE_INSTANCED
// The instanced variant reads its per-object transforms from a storage buffer indexed by
// gl_InstanceID instead of from a uniform set once per draw. That is the entire difference
// between the two variants, and it is why this is a #define rather than a second shader file:
// the 240-odd lines of lighting below are shared verbatim, so an instanced object and a
// non-instanced one cannot drift apart in how they are lit.
//
// Matrices arrive TRANSPOSED from the host (Matrix4x4UVE is row-major; std430 mat4 is
// column-major), exactly as in mesh_skin.glsl - the same convention, deliberately, so there is
// one rule to remember rather than two.
//
// uInstanceBaseIndex offsets into the frame-wide matrix buffer so every batch can share one
// upload rather than one buffer each; gl_InstanceID restarts at 0 for each draw.
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, binding = 1) readonly buffer InstanceNormalTransformBlock {
    mat4 instanceNormalModels[];
};
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#else
uniform mat4 uModel;
// Transpose(inverse(uModel)): correctly transforms normals under non-uniform scale, unlike
// uModel itself (which only preserves normal direction for uniform scale / rigid transforms).
// Tangents are still transformed with uModel directly - that IS the correct convention for a
// surface-parameterization vector, unlike a normal.
uniform mat4 uNormalMatrix;
#endif
uniform mat4 uViewProjection;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uLightSpaceMatrices[3];

void main() {
#ifdef UVE_INSTANCED
    int instanceSlot = uInstanceBaseIndex + gl_InstanceID;
    mat4 model = instanceModels[instanceSlot];
    mat4 normalMatrix = instanceNormalModels[instanceSlot];
#else
    mat4 model = uModel;
    mat4 normalMatrix = uNormalMatrix;
#endif
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = mat3(normalMatrix) * aNormal;
    vWorldTangent = mat3(model) * aTangent.xyz;
    vTangentHandedness = aTangent.w;
    vTexCoord = aTexCoord;
    vLightSpacePosition = uLightSpaceMatrix * worldPosition;
    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
        vLightSpacePositions[cascadeIndex] = uLightSpaceMatrices[cascadeIndex] * worldPosition;
    }
    gl_Position = uViewProjection * worldPosition;
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vWorldTangent;
in float vTangentHandedness;
in vec2 vTexCoord;
in vec4 vLightSpacePosition;
in vec4 vLightSpacePositions[3];
out vec4 FragColor;

struct LightUVE {
    int type; // 0 = Directional, 1 = Point, 2 = Spot
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

uniform LightUVE uLights[4];
uniform vec3 uAmbientColor;
uniform vec3 uViewPosition;
uniform vec3 uAlbedoColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissiveColor;
uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uAOTexture;
// Legacy Increment 27 pair retained for project-authored shaders and direct single-map tests.
uniform sampler2D uShadowMapTexture;
uniform mat4 uLightSpaceMatrix;
uniform int uShadowPcfKernelRadius;
// Increment 30 fixed three-cascade directional shadow contract.
uniform sampler2D uShadowMapTextures[3];
uniform float uShadowCascadeSplits[3];
uniform int uShadowCascadeCount;
// Increment 31: fraction of each non-final cascade depth interval used to cross-fade into the next.
uniform float uShadowCascadeBlendRatio;

const float kPiUVE = 3.14159265359;
const float kBrdfEpsilonUVE = 0.0001;
/// The spot cone's bright inner region as a fraction of its authored outer half-angle; the band
/// between the two is where the light falls off. Derived rather than authored so a material's
/// existing single spotAngleDegrees keeps meaning exactly what it meant.
const float kSpotInnerConeRatioUVE = 0.85;

vec3 SafeNormalizeUVE(vec3 value) {
    return value / max(length(value), kBrdfEpsilonUVE);
}

float DistributionGgxUVE(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPiUVE * denominator * denominator, kBrdfEpsilonUVE);
}

float GeometrySchlickGgxUVE(float normalDotDirection, float roughness) {
    float alpha = roughness * roughness;
    float k = ((alpha + 1.0) * (alpha + 1.0)) * 0.125;
    return normalDotDirection / max(normalDotDirection * (1.0 - k) + k, kBrdfEpsilonUVE);
}

float GeometrySmithUVE(float normalDotView, float normalDotLight, float roughness) {
    return GeometrySchlickGgxUVE(normalDotView, roughness) *
           GeometrySchlickGgxUVE(normalDotLight, roughness);
}

vec3 FresnelSchlickUVE(float halfDotView, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * pow(1.0 - halfDotView, 5.0);
}

float SampleCascadeDepthUVE(int cascadeIndex, vec2 texCoord) {
    if (cascadeIndex == 0) {
        return texture(uShadowMapTextures[0], texCoord).r;
    }
    if (cascadeIndex == 1) {
        return texture(uShadowMapTextures[1], texCoord).r;
    }
    return texture(uShadowMapTextures[2], texCoord).r;
}

vec2 CascadeTexelSizeUVE(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return 1.0 / vec2(textureSize(uShadowMapTextures[0], 0));
    }
    if (cascadeIndex == 1) {
        return 1.0 / vec2(textureSize(uShadowMapTextures[1], 0));
    }
    return 1.0 / vec2(textureSize(uShadowMapTextures[2], 0));
}

vec4 CascadeLightSpacePositionUVE(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return vLightSpacePositions[0];
    }
    if (cascadeIndex == 1) {
        return vLightSpacePositions[1];
    }
    return vLightSpacePositions[2];
}

float ShadowFactorFromPositionUVE(vec4 lightSpacePosition, vec3 normal, vec3 lightDirection, int cascadeIndex) {
    if (abs(lightSpacePosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 projected = lightSpacePosition.xyz / lightSpacePosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.x <= 0.0 || projected.x >= 1.0 || projected.y <= 0.0 || projected.y >= 1.0 ||
        projected.z <= 0.0 || projected.z >= 1.0) {
        return 1.0;
    }

    int kernelRadius = clamp(uShadowPcfKernelRadius, 0, 2);
    vec2 texelSize = cascadeIndex < 0 ? 1.0 / vec2(textureSize(uShadowMapTexture, 0))
                                      : CascadeTexelSizeUVE(cascadeIndex);
    float currentDepth = projected.z;
    float bias = max(0.0025 * (1.0 - max(dot(normal, lightDirection), 0.0)), 0.0005);
    float visibleSamples = 0.0;
    int sampleCount = 0;

    for (int offsetY = -2; offsetY <= 2; ++offsetY) {
        for (int offsetX = -2; offsetX <= 2; ++offsetX) {
            if (abs(offsetX) > kernelRadius || abs(offsetY) > kernelRadius) {
                continue;
            }
            vec2 sampleCoord = projected.xy + vec2(offsetX, offsetY) * texelSize;
            float sampledDepth = cascadeIndex < 0 ? texture(uShadowMapTexture, sampleCoord).r
                                                  : SampleCascadeDepthUVE(cascadeIndex, sampleCoord);
            visibleSamples += currentDepth - bias > sampledDepth ? 0.0 : 1.0;
            ++sampleCount;
        }
    }

    return visibleSamples / float(sampleCount);
}

float DirectionalShadowFactorUVE(vec3 normal, vec3 lightDirection) {
    if (uShadowCascadeCount <= 0) {
        return ShadowFactorFromPositionUVE(vLightSpacePosition, normal, lightDirection, -1);
    }

    float viewDepth = length(vWorldPosition - uViewPosition);
    int cascadeIndex = clamp(uShadowCascadeCount - 1, 0, 2);
    for (int candidateIndex = 0; candidateIndex < 2; ++candidateIndex) {
        if (candidateIndex < uShadowCascadeCount && viewDepth <= uShadowCascadeSplits[candidateIndex]) {
            cascadeIndex = candidateIndex;
            break;
        }
    }
    float shadowFactor = ShadowFactorFromPositionUVE(CascadeLightSpacePositionUVE(cascadeIndex), normal,
                                                      lightDirection, cascadeIndex);
    int finalCascadeIndex = clamp(uShadowCascadeCount - 1, 0, 2);
    if (cascadeIndex >= finalCascadeIndex) {
        return shadowFactor;
    }

    float cascadeNearDepth = cascadeIndex == 0 ? 0.0 : uShadowCascadeSplits[cascadeIndex - 1];
    float cascadeFarDepth = uShadowCascadeSplits[cascadeIndex];
    float cascadeDepthRange = max(cascadeFarDepth - cascadeNearDepth, 0.0001);
    float blendWidth = cascadeDepthRange * clamp(uShadowCascadeBlendRatio, 0.0, 0.25);
    float blendStartDepth = cascadeFarDepth - blendWidth;
    if (blendWidth <= 0.0 || viewDepth <= blendStartDepth) {
        return shadowFactor;
    }

    float nextCascadeShadowFactor = ShadowFactorFromPositionUVE(
        CascadeLightSpacePositionUVE(cascadeIndex + 1), normal, lightDirection, cascadeIndex + 1);
    float blendWeight = smoothstep(blendStartDepth, cascadeFarDepth, viewDepth);
    return mix(shadowFactor, nextCascadeShadowFactor, blendWeight);
}

void main() {
    vec3 albedo = texture(uAlbedoTexture, vTexCoord).rgb * uAlbedoColor;
    float ambientOcclusion = texture(uAOTexture, vTexCoord).r;
    vec3 normal = SafeNormalizeUVE(vWorldNormal);
    vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
    if (dot(tangent, tangent) <= 0.00000001) {
        vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = cross(fallbackAxis, normal);
    }
    tangent = SafeNormalizeUVE(tangent);
    vec3 bitangent = SafeNormalizeUVE(cross(normal, tangent));
    bitangent *= vTangentHandedness < 0.0 ? -1.0 : 1.0;
    vec3 tangentSpaceNormal = texture(uNormalTexture, vTexCoord).xyz * 2.0 - 1.0;
    normal = SafeNormalizeUVE(mat3(tangent, bitangent, normal) * tangentSpaceNormal);
    vec3 viewDirection = SafeNormalizeUVE(uViewPosition - vWorldPosition);
    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.04, 1.0);
    // Ambient, split into diffuse and specular exactly as the direct term is.
    //
    // The old ambient was purely diffuse: albedo * ambient * ao. For a dielectric that is roughly
    // right, but a metal has NO diffuse response at all - its diffuseWeight is (1-F)(1-metallic),
    // which is zero at metallic 1 - so a metal lit only by ambient came out BLACK. That is the
    // single most visible way a correct BRDF still looks wrong, and it is why "the PBR looks
    // broken" usually means "there is no ambient specular".
    //
    // This is not image-based lighting: there is no environment cubemap here, so the ambient
    // colour stands in for the environment's average radiance. What it does buy is the right
    // ENERGY SPLIT - a metal reflects the ambient tinted by its own albedo, a dielectric reflects
    // about 4% of it untinted, and both lose energy to roughness. A real IBL probe replaces the
    // source of that radiance later without changing this structure.
    vec3 ambientBaseReflectance = mix(vec3(0.04), albedo, metallic);
    float normalDotViewAmbient = max(dot(normal, viewDirection), 0.0);
    // Roughness-aware Fresnel: the standard Schlick term goes to white at grazing angles, which on
    // a rough surface produces a bright rim that should not be there - the microfacets point in
    // too many directions to reflect coherently. Clamping the ceiling by (1 - roughness) is the
    // usual, cheap correction.
    vec3 ambientFresnel =
        ambientBaseReflectance +
        (max(vec3(1.0 - roughness), ambientBaseReflectance) - ambientBaseReflectance) *
            pow(1.0 - normalDotViewAmbient, 5.0);
    vec3 ambientDiffuseWeight = (vec3(1.0) - ambientFresnel) * (1.0 - metallic);
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * uAmbientColor;
    vec3 ambientSpecular = ambientFresnel * uAmbientColor;
    // AO occludes both terms. Applying it to the diffuse alone is a common shortcut, but a crevice
    // does not stop reflecting light in a way the sky can reach either.
    vec3 lighting = (ambientDiffuse + ambientSpecular) * ambientOcclusion + uEmissiveColor;

    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        LightUVE light = uLights[lightIndex];
        if (light.intensity <= 0.0) {
            continue;
        }

        vec3 lightDirection;
        float attenuation = 1.0;
        if (light.type == 0) {
            lightDirection = SafeNormalizeUVE(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = max(length(toLight), 0.0001);
            lightDirection = toLight / distanceToLight;
            attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
            if (light.range > 0.0 && distanceToLight > light.range) {
                attenuation = 0.0;
            }
            if (light.type == 2) {
                // Smooth cone falloff rather than a binary in/out test. A hard cutoff produces a
                // jagged, aliased cone edge that no amount of MSAA fixes, because the edge is in
                // the shading rather than in the geometry. The inner cone is derived from the
                // authored outer angle rather than adding a second uniform - one authored angle
                // stays one authored angle, and the material contract does not change.
                float cosOuter = cos(radians(light.spotAngleDegrees));
                float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                // max() guards the degenerate case where the two cosines coincide (a zero-width
                // falloff band), which would otherwise divide by zero.
                float coneFalloff = clamp((coneAlignment - cosOuter) /
                                              max(cosInner - cosOuter, kBrdfEpsilonUVE),
                                          0.0, 1.0);
                // Squared so the falloff is smooth in perceived brightness rather than linear in
                // cosine, which reads as a visible ring at the transition.
                attenuation *= coneFalloff * coneFalloff;
            }
        }

        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        float normalDotView = max(dot(normal, viewDirection), 0.0);
        if (normalDotLight <= 0.0 || normalDotView <= 0.0 || attenuation <= 0.0) {
            continue;
        }

        vec3 halfDirection = SafeNormalizeUVE(lightDirection + viewDirection);
        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        vec3 baseReflectance = mix(vec3(0.04), albedo, metallic);
        vec3 fresnel = FresnelSchlickUVE(halfDotView, baseReflectance);
        float distribution = DistributionGgxUVE(normalDotHalf, roughness);
        float geometry = GeometrySmithUVE(normalDotView, normalDotLight, roughness);
        vec3 specular = (distribution * geometry * fresnel) /
                        max(4.0 * normalDotView * normalDotLight, kBrdfEpsilonUVE);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 diffuse = diffuseWeight * albedo / kPiUVE;
        vec3 radiance = light.color * light.intensity * attenuation;
        vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

        if (light.type == 0) {
            directContribution *= DirectionalShadowFactorUVE(normal, lightDirection);
        }
        lighting += directContribution;
    }

    FragColor = vec4(lighting, 1.0);
}
#endif
)GLSLSRC";



const std::string_view kParticleSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

out vec4 vColor;

uniform mat4 uViewProjection;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec4 vColor;

out vec4 FragColor;

void main() {
    FragColor = vColor;
}
#endif
)GLSLSRC";


const std::string_view kParticleSimulateSource = R"GLSLSRC(#version 430 core

// GPU twin of Scene::ParticleRuntimeUVE::SimulateDetailedUVE's per-particle integration
// (CS4). The CPU runtime remains the authority on WHICH particles exist - emission,
// budgets, lifetime culling and array compaction all stay on the CPU, where they are
// bounded and testable; this kernel does only the part that is pure arithmetic over an
// array, which is exactly the part worth moving to the GPU.
//
// The integration must match the CPU statement for statement, because the engine asserts
// the two agree bit-for-bit:
//
//     nextVelocity = velocity + acceleration * dt;
//     nextPosition = position + nextVelocity * dt;   // semi-implicit Euler: NEW velocity
//     nextLifetime = remainingLifetimeSeconds - dt;
//
// Two deliberate choices protect that equality. First, `precise` on the outputs forbids
// the compiler from contracting `a + b * c` into a fused multiply-add: an FMA keeps more
// intermediate precision, which sounds better but produces a DIFFERENT float than the
// CPU's separate multiply and add, and a result that is merely close is not a result the
// engine can compare. Second, nothing here is reordered or vectorised across particles -
// each invocation owns exactly one particle.
//
// Particles whose lifetime has run out are integrated anyway and left in place with a
// non-positive lifetime; the CPU's compaction pass is what removes them. Skipping them
// here would put a branch in the hot path to save nothing, and would make the readback
// disagree with the CPU on the dead entries' contents.

layout(local_size_x = 64) in;

// std430 packs this struct as 8 consecutive floats with no padding, which is what
// ParticleComputeSimulationUVE::ParticleGpuStateUVE mirrors on the host. vec3 would be
// 16-byte aligned and silently introduce padding, so positions and velocities are spelled
// out as scalars: the host layout assertion and this declaration must agree exactly.
struct ParticleGpuState {
    float positionX;
    float positionY;
    float positionZ;
    float velocityX;
    float velocityY;
    float velocityZ;
    float remainingLifetimeSeconds;
    float padding;
};

layout(std430, binding = 0) buffer ParticleBlock {
    ParticleGpuState particles[];
};

// The kernel's parameters travel in a storage buffer rather than as bare `uniform` scalars.
// That is a portability requirement, not a style choice: GLSL permits non-opaque uniforms at
// global scope and the GL backend resolves them by name, but SPIR-V has no such concept - glslang
// rejects this very file with "non-opaque uniform variables need a layout(location=L)" - so a
// kernel written that way can never run on Vulkan. A std430 block compiles unchanged for both.
layout(std430, binding = 1) readonly buffer ParticleSimulateParams {
    float deltaSeconds;
    float accelerationX;
    float accelerationY;
    float accelerationZ;
    int particleCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // The dispatch rounds up to whole workgroups, so the tail invocations of the last
    // group address particles that do not exist. Without this guard they would write past
    // the live range - the buffer is sized to the instance's capacity, so the write would
    // land inside allocated memory and silently corrupt state the CPU still owns.
    if (index >= uint(params.particleCount)) {
        return;
    }

    ParticleGpuState state = particles[index];

    precise float nextVelocityX = state.velocityX + params.accelerationX * params.deltaSeconds;
    precise float nextVelocityY = state.velocityY + params.accelerationY * params.deltaSeconds;
    precise float nextVelocityZ = state.velocityZ + params.accelerationZ * params.deltaSeconds;

    precise float nextPositionX = state.positionX + nextVelocityX * params.deltaSeconds;
    precise float nextPositionY = state.positionY + nextVelocityY * params.deltaSeconds;
    precise float nextPositionZ = state.positionZ + nextVelocityZ * params.deltaSeconds;

    precise float nextLifetime = state.remainingLifetimeSeconds - params.deltaSeconds;

    particles[index].positionX = nextPositionX;
    particles[index].positionY = nextPositionY;
    particles[index].positionZ = nextPositionZ;
    particles[index].velocityX = nextVelocityX;
    particles[index].velocityY = nextVelocityY;
    particles[index].velocityZ = nextVelocityZ;
    particles[index].remainingLifetimeSeconds = nextLifetime;
}
)GLSLSRC";

const std::string_view kFrustumCullSource = R"GLSLSRC(#version 430 core

// GPU twin of Math::FrustumUVE::IntersectsUVE (CS5) - the conservative centre/extents AABB test
// against six inward-facing planes that Renderer3DUVE and MeshRenderEligibilityUVE already use on
// the CPU:
//
//     radius = extents.x*|n.x| + extents.y*|n.y| + extents.z*|n.z|;
//     if (dot(n, center) + d + radius < 0) -> rejected by this plane, box is invisible
//
// Culling is a BOOLEAN result, which makes it tempting to accept "nearly the same" answers. That
// would be the wrong standard. A box sitting exactly on a plane is where CPU and GPU are most
// likely to differ, and it is also exactly where a difference is visible as an object popping in
// or out depending on which path ran. So the arithmetic underneath the boolean is held to the
// same bit-for-bit rule as the particle kernel: `precise` forbids the compiler from contracting
// the multiply-adds into FMAs, which would keep more intermediate precision and therefore produce
// a DIFFERENT float than the CPU's separate operations - and a different float is what flips a
// borderline decision.
//
// The host computes each box's centre and extents and uploads those rather than min/max, so the
// halving in AabbUVE::GetCenterUVE (including its double-precision fallback for boxes whose
// min+max overflows) happens once, on the CPU, in the CPU's own arithmetic. Recomputing it here
// would introduce a second place for the two paths to disagree, for no benefit.
//
// Deliberately NOT done here: the plane extraction itself. Six planes per frustum is not work
// worth a dispatch, and keeping FrustumUVE::FromViewProjectionUVE as the single authority means
// there is exactly one plane-extraction implementation in the engine to be correct.

layout(local_size_x = 64) in;

// std430, 8 floats, no padding - mirrored by CullBoxGpuUVE on the host. Spelled out as scalars
// for the same reason the particle kernel does: a vec3 here would be 16-byte aligned and silently
// introduce padding the host struct does not have.
struct CullBox {
    float centerX;
    float centerY;
    float centerZ;
    float extentX;
    float extentY;
    float extentZ;
    float padding0;
    float padding1;
};

// A plane as normal + distance: exactly Math::PlaneUVE's layout, four floats.
struct CullPlane {
    float normalX;
    float normalY;
    float normalZ;
    float distance;
};

layout(std430, binding = 0) readonly buffer BoxBlock {
    CullBox boxes[];
};

layout(std430, binding = 1) readonly buffer PlaneBlock {
    CullPlane planes[6];
};

// One uint per box: 1 visible, 0 culled. A uint rather than a packed bitfield because the host
// reads this back and compares it element-wise against the CPU's decision - a bitfield would make
// the readback denser and every mismatch report harder to read, and the buffer is already tiny
// next to the box data it describes.
layout(std430, binding = 2) writeonly buffer VisibilityBlock {
    uint visible[];
};

// Parameters travel in a storage buffer, not as a bare `uniform` scalar - SPIR-V has no
// non-opaque global uniforms, so the uniform form cannot compile for Vulkan at all. See
// particle_simulate.glsl for the same note.
layout(std430, binding = 3) readonly buffer FrustumCullParams {
    int boxCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // Dispatches round up to whole workgroups; the tail invocations own no box. Without this the
    // write would land inside the allocated visibility buffer and corrupt a neighbouring result.
    if (index >= uint(params.boxCount)) {
        return;
    }

    CullBox box = boxes[index];
    uint result = 1u;

    // Plane order is fixed by FrustumUVE (left, right, bottom, top, near, far) and the loop is
    // unrolled over exactly six - a dynamic count would be a different contract, and the CPU side
    // has no such thing.
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        CullPlane plane = planes[planeIndex];

        precise float radius = box.extentX * abs(plane.normalX) + box.extentY * abs(plane.normalY) +
                               box.extentZ * abs(plane.normalZ);
        precise float signedDistance = plane.normalX * box.centerX + plane.normalY * box.centerY +
                                       plane.normalZ * box.centerZ + plane.distance;

        // Matches the CPU's early return exactly, including the strict `< 0` comparison: a box
        // touching the plane exactly is INSIDE, and that boundary has to be the same on both sides.
        if (signedDistance + radius < 0.0) {
            result = 0u;
            break;
        }
    }

    visible[index] = result;
}
)GLSLSRC";

const std::string_view kFrustumCullIndirectSource = R"GLSLSRC(#version 430 core

// CS8: the same frustum test as frustum_cull.glsl, but the result never comes back to the CPU.
//
// frustum_cull.glsl writes one uint per box and the host reads all of them, counts the visible
// ones, and issues draws accordingly. That readback is a full GPU->CPU round trip on the critical
// path, and it is precisely what a culling pass exists to avoid. This kernel instead writes, in
// device memory, the two things a draw actually needs:
//
//   1. the instanceCount field of a DrawIndexedIndirectCommand, and
//   2. a COMPACTED list of which boxes survived, in slot order,
//
// so DrawIndexedIndirectUVE can consume the command directly and the vertex shader can look up
// gl_InstanceID in the compacted list. Nothing on the CPU ever learns how many objects passed.
//
// The test itself is character-for-character the one in frustum_cull.glsl, including `precise`
// forbidding FMA contraction, because the two kernels must agree exactly - CS8's tests verify the
// compacted output against CS5's per-box output, and a divergence in the arithmetic would show up
// as a phantom disagreement that has nothing to do with the compaction being tested.

layout(local_size_x = 64) in;

// Mirrored by CullBoxGpuUVE on the host - shared with frustum_cull.glsl, same layout.
struct CullBox {
    float centerX;
    float centerY;
    float centerZ;
    float extentX;
    float extentY;
    float extentZ;
    float padding0;
    float padding1;
};

struct CullPlane {
    float normalX;
    float normalY;
    float normalZ;
    float distance;
};

layout(std430, binding = 0) readonly buffer BoxBlock {
    CullBox boxes[];
};

layout(std430, binding = 1) readonly buffer PlaneBlock {
    CullPlane planes[6];
};

// The draw parameters themselves, in exactly the five-word order both Vulkan and GL define for an
// indexed indirect draw and DrawIndexedIndirectCommandUVE mirrors on the host. Only instanceCount
// is touched here: the host seeds the other four (they describe the MESH, which no culling
// decision can change) and zeroes instanceCount before the dispatch.
//
// Not `writeonly`: atomicAdd both reads and writes, and a writeonly qualifier would make the
// buffer illegal to use that way.
layout(std430, binding = 2) buffer DrawCommandBlock {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
} drawCommand;

// Slot i holds the index of the i-th surviving box. Capacity equals the box count - the worst
// case is everything visible - so the atomic can never hand out a slot outside the buffer, which
// is why there is no bounds check on the store below and why there must never be one added
// without also changing the allocation.
layout(std430, binding = 3) writeonly buffer VisibleIndexBlock {
    uint visibleIndices[];
};

layout(std430, binding = 4) readonly buffer FrustumCullIndirectParams {
    int boxCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= uint(params.boxCount)) {
        return;
    }

    CullBox box = boxes[index];
    bool visible = true;

    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        CullPlane plane = planes[planeIndex];

        precise float radius = box.extentX * abs(plane.normalX) + box.extentY * abs(plane.normalY) +
                               box.extentZ * abs(plane.normalZ);
        precise float signedDistance = plane.normalX * box.centerX + plane.normalY * box.centerY +
                                       plane.normalZ * box.centerZ + plane.distance;

        if (signedDistance + radius < 0.0) {
            visible = false;
            break;
        }
    }

    if (visible) {
        // This single atomic is the whole point of the pass: it both counts the survivors into the
        // draw's instanceCount and hands this invocation a unique compaction slot, without any
        // ordering between invocations and without the CPU being told the answer.
        //
        // Consequence the host must live with: slot assignment is NOT deterministic, so the
        // compacted list is a SET, not a sequence. Anything verifying it has to sort first.
        const uint slot = atomicAdd(drawCommand.instanceCount, 1u);
        visibleIndices[slot] = index;
    }
}
)GLSLSRC";

const std::string_view kMeshSkinSource = R"GLSLSRC(#version 430 core

// GPU twin of Asset::TrySkinMeshUVE (CS9) - linear blend skinning over many vertices at once.
//
// The CPU side was written first, deliberately: a GPU kernel with no CPU authority to compare
// against cannot be shown to be right. Everything below mirrors that implementation step for step,
// and the ordering of the arithmetic is part of the contract, not an implementation detail.
//
// Three things are load-bearing and must not be "tidied":
//
//   1. BLEND THE MATRICES, THEN TRANSFORM ONCE. Transforming by each joint and blending the
//      results is algebraically identical and numerically different. The CPU blends first; so
//      does this.
//
//   2. SKIP ZERO-WEIGHT SLOTS rather than multiplying by zero. An unused slot may hold any joint
//      index, and 0 * infinity is NaN - the CPU skips, so this skips, or a degenerate joint would
//      poison a vertex on one path only.
//
//   3. `precise` FORBIDS FMA CONTRACTION. A fused multiply-add keeps more intermediate precision
//      and therefore produces a DIFFERENT float than the CPU's separate operations. Same rule as
//      the particle and cull kernels.
//
// Note on precision: the CPU's own general-purpose Math::TransformPointUVE accumulates in double,
// which GLSL has no portable equivalent for (float64 is an optional Vulkan feature absent from
// whole classes of hardware). Rather than accept a permanent ~1 ULP disagreement on roughly one
// vertex in six, the CPU skinning path uses a float-accumulating transform of its own - see
// TransformPointFloatUVE in mesh_skinning_uve.cpp. That is what makes an exact comparison possible
// here at all.

layout(local_size_x = 64) in;

// Mirrored by MeshSkinVertexGpuUVE on the host: position, normal, tangent, handedness. Scalars
// rather than vec3s because a vec3 in std430 is 16-byte aligned and would silently introduce
// padding the host struct does not have.
struct SkinVertex {
    float positionX;
    float positionY;
    float positionZ;
    float normalX;
    float normalY;
    float normalZ;
    float tangentX;
    float tangentY;
    float tangentZ;
    float handedness;
};

// Four joint indices and four weights per vertex, matching MeshSkinningInfluenceUVE.
struct SkinInfluence {
    uint joints[4];
    float weights[4];
};

layout(std430, binding = 0) readonly buffer InputVertexBlock {
    SkinVertex inputVertices[];
};

layout(std430, binding = 1) readonly buffer InfluenceBlock {
    SkinInfluence influences[];
};

// The resolved skinning matrices, one per joint - already composed with each joint's inverse bind
// matrix on the CPU. Pose resolution stays there on purpose: it is a walk down a parent chain over
// a handful of joints, which is serial work a dispatch cannot help with, and keeping
// TryResolvePoseUVE the single authority means there is one implementation to be correct.
//
// mat4 in std430 is column-major with a 16-byte column stride, which is exactly a dense float[16];
// the host uploads Matrix4x4UVE::m transposed into that order. See MeshSkinComputeUVE for why the
// transpose happens on the host rather than here.
layout(std430, binding = 2) readonly buffer SkinningMatrixBlock {
    mat4 skinningMatrices[];
};

layout(std430, binding = 3) writeonly buffer OutputVertexBlock {
    SkinVertex outputVertices[];
};

layout(std430, binding = 4) readonly buffer MeshSkinParams {
    int vertexCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // Dispatches round up to whole workgroups; the tail invocations own no vertex.
    if (index >= uint(params.vertexCount)) {
        return;
    }

    SkinVertex source = inputVertices[index];
    SkinInfluence influence = influences[index];

    // The weighted sum of the influencing joints' matrices - point 1 above.
    //
    // `precise` on the ACCUMULATOR, not just on the final transform: each step here is itself a
    // multiply-add (acc += M * w) and is just as contractible into an FMA as the dot products
    // below. Qualifying only the transform would leave the blend free to diverge, which is the
    // subtler half of the same hazard.
    precise mat4 blended = mat4(0.0);
    for (int slot = 0; slot < 4; ++slot) {
        float weight = influence.weights[slot];
        if (weight == 0.0) {
            continue; // Point 2: skipped, never multiplied by zero.
        }
        blended += skinningMatrices[influence.joints[slot]] * weight;
    }

    // Spelled out element by element rather than as a matrix-vector product: the multiply order
    // and the addition order are what has to match the CPU, and `precise` can only forbid FMA
    // contraction on operations the shader actually names. A `blended * vec4(p, 1.0)` would leave
    // the accumulation order to the compiler.
    //
    // mat4 indexing in GLSL is [column][row], which is why these read transposed relative to the
    // host's row-major Matrix4x4UVE - the host uploads them transposed to make exactly this work.
    precise float positionX = blended[0][0] * source.positionX + blended[1][0] * source.positionY +
                              blended[2][0] * source.positionZ + blended[3][0];
    precise float positionY = blended[0][1] * source.positionX + blended[1][1] * source.positionY +
                              blended[2][1] * source.positionZ + blended[3][1];
    precise float positionZ = blended[0][2] * source.positionX + blended[1][2] * source.positionY +
                              blended[2][2] * source.positionZ + blended[3][2];

    // Directions: the translation column is not read at all. Running a normal through the point
    // transform would displace it by the joint's position.
    precise float normalX = blended[0][0] * source.normalX + blended[1][0] * source.normalY +
                            blended[2][0] * source.normalZ;
    precise float normalY = blended[0][1] * source.normalX + blended[1][1] * source.normalY +
                            blended[2][1] * source.normalZ;
    precise float normalZ = blended[0][2] * source.normalX + blended[1][2] * source.normalY +
                            blended[2][2] * source.normalZ;

    precise float tangentX = blended[0][0] * source.tangentX + blended[1][0] * source.tangentY +
                             blended[2][0] * source.tangentZ;
    precise float tangentY = blended[0][1] * source.tangentX + blended[1][1] * source.tangentY +
                             blended[2][1] * source.tangentZ;
    precise float tangentZ = blended[0][2] * source.tangentX + blended[1][2] * source.tangentY +
                             blended[2][2] * source.tangentZ;

    SkinVertex result;
    result.positionX = positionX;
    result.positionY = positionY;
    result.positionZ = positionZ;
    result.normalX = normalX;
    result.normalY = normalY;
    result.normalZ = normalZ;
    result.tangentX = tangentX;
    result.tangentY = tangentY;
    result.tangentZ = tangentZ;
    // Handedness is a sign carried through untouched, exactly as the CPU does.
    result.handedness = source.handedness;

    outputVertices[index] = result;
}
)GLSLSRC";


const std::string_view kBloomBrightPassSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform float uBloomThreshold;

void main() {
    vec3 hdrColor = max(texture(uSourceTexture, vTexCoord).rgb, vec3(0.0));
    float luminance = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    float contribution = max(luminance - uBloomThreshold, 0.0) / max(luminance, 0.0001);
    FragColor = vec4(hdrColor * contribution, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kBloomBlurSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
// (1,0) for a horizontal pass, (0,1) for a vertical pass - the same shader serves both halves of
// the separable Gaussian blur, one draw each, ping-ponging between two same-sized targets.
// Individual floats, not a vec2: ShaderProgramUVE currently only exposes Float/Int/Bool/Vec3/Mat4
// uniform setters (see shader_program_uve.h), so this avoids adding a new uniform-value type for
// a single consumer.
uniform float uBlurDirectionX;
uniform float uBlurDirectionY;
uniform float uTexelSizeX;
uniform float uTexelSizeY;

void main() {
    // 9-tap Gaussian, weights normalized to sum to 1 (sigma ~= 2 texels).
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 direction = vec2(uBlurDirectionX, uBlurDirectionY);
    vec2 texelSize = vec2(uTexelSizeX, uTexelSizeY);
    vec3 result = texture(uSourceTexture, vTexCoord).rgb * weights[0];
    for (int tap = 1; tap < 5; ++tap) {
        vec2 offset = direction * texelSize * float(tap);
        result += texture(uSourceTexture, vTexCoord + offset).rgb * weights[tap];
        result += texture(uSourceTexture, vTexCoord - offset).rgb * weights[tap];
    }
    FragColor = vec4(result, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kFullscreenCopySource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

// Unmodified passthrough: the actual effect is the pipeline's blend mode this shader is used
// with, not anything computed here - Additive to composite the blurred bloom texture onto the
// HDR scene color, Multiply to composite the SSAO occlusion term onto it.
uniform sampler2D uSourceTexture;

void main() {
    FragColor = texture(uSourceTexture, vTexCoord);
}
#endif
)GLSLSRC";

const std::string_view kSsaoSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTexture;
// The projection matrix and its inverse (Math::TryInverseUVE(), Phase 2d), NOT the combined
// view-projection or its inverse - reconstruction below stays entirely in view space, so only the
// projection step needs undoing (uInverseProjection) and redoing (uProjection, to re-project each
// hemisphere sample and look up its screen position - computed once on the CPU per frame rather
// than inverting uInverseProjection again per pixel).
uniform mat4 uInverseProjection;
uniform mat4 uProjection;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;

// A fixed 12-tap hemisphere kernel (offline-generated, hemisphere-distributed, biased toward the
// origin so more samples land close to the shaded point) stands in for the noise-texture-driven
// per-pixel kernel rotation real engines typically use - HashUVE() below provides the per-pixel
// rotation instead, trading a small amount of dither/banding for not needing a vendored noise
// texture asset. Reasonable for this engine's scope; a dedicated rotation-noise texture is a
// future quality upgrade, not a correctness requirement.
const int kKernelSizeUVE = 12;
const vec3 kKernelUVE[12] = vec3[](
    vec3(-0.0557, 0.0476, 0.0681),
    vec3(0.0117, -0.0727, 0.0766),
    vec3(0.0931, -0.0750, 0.0365),
    vec3(0.1465, -0.0523, 0.0149),
    vec3(0.1313, 0.0982, 0.1146),
    vec3(0.0901, 0.2384, 0.0267),
    vec3(-0.2553, -0.1976, 0.0374),
    vec3(-0.2171, -0.3242, 0.1129),
    vec3(0.2547, -0.2537, 0.3475),
    vec3(0.4424, 0.3262, 0.2557),
    vec3(0.3698, -0.5432, 0.3062),
    vec3(-0.1842, 0.7316, 0.4049)
);

// Reconstructs a view-space position from a depth-buffer sample. GL's own fixed depth-range
// mapping (depth = 0.5 * ndc.z + 0.5, default glDepthRange(0,1)) is inverted first to recover the
// actual clip.z/clip.w ratio the projection matrix produced (this engine's PerspectiveUVE uses a
// [0,1] "Vulkan-style" clip-space z, not OpenGL's traditional [-1,1] - see its doc comment), then
// the standard homogeneous-divide trick recovers view-space xyz regardless of that convention.
vec3 ReconstructViewPositionUVE(vec2 uv, float depthSample) {
    vec3 ndc = vec3(uv * 2.0 - 1.0, 2.0 * depthSample - 1.0);
    vec4 clipPos = vec4(ndc, 1.0);
    vec4 viewPos = uInverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

float HashUVE(vec2 value) {
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 1.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // Background/far plane: never occluded.
        return;
    }
    vec3 centerViewPos = ReconstructViewPositionUVE(vTexCoord, centerDepth);

    // View-space normal from screen-space derivatives of the reconstructed position - avoids
    // needing a dedicated normal G-buffer in this forward renderer.
    // dFdx/dFdy face the +Z view direction (camera looks down -Z) for a screen-space quad that
    // covers increasing x left-to-right and increasing y bottom-to-top, matching this engine's
    // vTexCoord/gl_Position convention - no winding-based flip needed, unlike a mesh normal.
    vec3 viewNormal = normalize(cross(dFdx(centerViewPos), dFdy(centerViewPos)));
    if (viewNormal.z < 0.0) {
        viewNormal = -viewNormal;
    }

    float rotationAngle = HashUVE(vTexCoord) * 6.28318530718;
    float cosAngle = cos(rotationAngle);
    float sinAngle = sin(rotationAngle);
    vec3 randomTangent = normalize(vec3(cosAngle, sinAngle, 0.0));
    vec3 tangent = normalize(randomTangent - viewNormal * dot(randomTangent, viewNormal));
    vec3 bitangent = cross(viewNormal, tangent);
    mat3 tbn = mat3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    for (int sampleIndex = 0; sampleIndex < kKernelSizeUVE; ++sampleIndex) {
        vec3 samplePos = centerViewPos + (tbn * kKernelUVE[sampleIndex]) * uRadius;

        // Re-project the sample point with uProjection to look up what's actually in the depth
        // buffer at that screen location.
        vec4 sampleClip = uProjection * vec4(samplePos, 1.0);
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        vec3 sampledViewPos = ReconstructViewPositionUVE(sampleUv, sampledDepth);

        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(centerViewPos.z - sampledViewPos.z), 0.0001));
        occlusion += (sampledViewPos.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }
    occlusion = 1.0 - (occlusion / float(kKernelSizeUVE)) * uIntensity;
    FragColor = vec4(vec3(clamp(occlusion, 0.0, 1.0)), 1.0);
}
#endif
)GLSLSRC";

const std::string_view kUIOverlaySource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 uProjection;

void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uSourceTexture;

void main() {
    FragColor = texture(uSourceTexture, vTexCoord) * vColor;
}
#endif
)GLSLSRC";

} // namespace UVE::Render::Shader::BuiltIn
