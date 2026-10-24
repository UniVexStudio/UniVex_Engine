#version 450 core

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

#if defined(UVE_INSTANCED) || defined(UVE_INDIRECT_INSTANCED)
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
#ifdef UVE_INDIRECT_INSTANCED
layout(std430, binding = 3) readonly buffer VisibleIndexBlock {
    uint visibleIndices[];
};
#endif
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
#if defined(UVE_INDIRECT_INSTANCED)
    int instanceSlot = uInstanceBaseIndex + int(visibleIndices[gl_InstanceID]);
    mat4 model = instanceModels[instanceSlot];
    mat4 normalMatrix = instanceNormalModels[instanceSlot];
#elif defined(UVE_INSTANCED)
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
    int cullMask;
    float specular;
};

uniform LightUVE uLights[4];
uniform vec3 uAmbientColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform int uAmbientSource; // 0=None, 1=FlatColor, 2=Sky, 3=EnvironmentMap
uniform sampler2D uAmbientEnvironmentMap;
uniform int uAmbientEnvironmentMapEnabled;
uniform vec3 uViewPosition;
uniform vec3 uAlbedoColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissiveColor;
uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uAOTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform sampler2D uEmissiveTexture;
#if UVE_MOBILE
uniform float uEmissiveEnergy;
#else
uniform float uEmissiveEnergy = 1.0;
#endif
#if UVE_MOBILE
uniform float uNormalScale;
#else
uniform float uNormalScale = 1.0;
#endif
#if UVE_MOBILE
uniform float uOcclusionStrength;
#else
uniform float uOcclusionStrength = 1.0;
#endif
#if UVE_MOBILE
uniform vec3 uUvScale;
#else
uniform vec3 uUvScale = vec3(1.0, 1.0, 0.0);
#endif
#if UVE_MOBILE
uniform vec3 uUvOffset;
#else
uniform vec3 uUvOffset = vec3(0.0, 0.0, 0.0);
#endif
#if UVE_MOBILE
uniform int uUnshaded;
#else
uniform int uUnshaded = 0;
#endif
#if UVE_MOBILE
uniform int uShadingMode;
#else
uniform int uShadingMode = 0;
#endif
#if UVE_MOBILE
uniform float uAlphaCutoff;
#else
uniform float uAlphaCutoff = 0.0;
#endif
// The material's own alpha, kept separate from uSurfaceOpacity (the SurfaceInstance3D fade) so a
// texture's alpha and an object fading out multiply instead of one silently replacing the other.
#if UVE_MOBILE
uniform float uAlbedoAlpha;
#else
uniform float uAlbedoAlpha = 1.0;
#endif
// 0 = AlbedoAlpha (texture alpha times uAlbedoAlpha), 2 = None (alpha stays 1). 1 is reserved for
// the separate-texture-channel source, which no shader input carries yet - it behaves like None
// rather than guessing at a channel.
#if UVE_MOBILE
uniform int uAlphaSource;
#else
uniform int uAlphaSource = 0;
#endif
// Set only for a PremultipliedAlpha blend mode: the pipeline adds the source with a ONE factor, so
// rgb has to arrive already multiplied by alpha or the surface composites too bright.
#if UVE_MOBILE
uniform int uPremultiplyAlpha;
#else
uniform int uPremultiplyAlpha = 0;
#endif
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
uniform float uShadowMaxDistanceFadeRange;
uniform int uMeshRenderLayers;
uniform float uSurfaceOpacity;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uShadowOpacity;
uniform int uReflectionProbeEnabled;
uniform vec3 uReflectionProbePosition;
uniform vec3 uReflectionProbeAxisX;
uniform vec3 uReflectionProbeAxisY;
uniform vec3 uReflectionProbeAxisZ;
uniform vec3 uReflectionProbeHalfExtents;
uniform samplerCube uReflectionProbeCube;

// Unified 2D shadow atlas for spot and point lights, sampled from texture slot 15 - the LAST unit
// inside GLES3's guaranteed 16-unit floor, chosen so it cannot shadow one of the eleven dense
// material/cascade/probe units below it. The renderer binds it there through
// kShadowAtlasTextureSlotUVE; the two names are one contract.
//
// Every array here is indexed by the light's slot in uLights[], not by "nth shadowed light": a light
// whose tile was dropped this frame simply has uLightShadowEnabled == 0 and its own slot stays empty.
uniform sampler2D uShadowAtlasTexture;
// Spot lights: one perspective tile each, sampled through this light's own matrix and rect.
uniform mat4 uLightShadowMatrices[4];
uniform vec4 uLightShadowTileRects[4];
// Point lights: six tiles each, one per cube face, indexed [lightIndex * 6 + face]. A spot light's
// entries stay all-zero and are never read - the light's type selects which half of this contract
// applies, so the two paths cannot accidentally sample each other's tiles.
uniform vec4 uLightShadowCubeRects[24];
uniform int uLightShadowEnabled[4];
// (near, far) of each light's projection, needed to convert a world-space bias into normalized
// depth at the receiver - see AtlasShadowDepthBiasUVE.
uniform vec2 uLightShadowDepthRange[4];
// 1 / atlas resolution, so the PCF tap spacing has one source of truth. Declared with a default so
// a direct RHI caller that never sets it keeps the spacing this shader has always assumed.
#if UVE_MOBILE
uniform vec2 uShadowAtlasTexelSize;
#else
uniform vec2 uShadowAtlasTexelSize = vec2(1.0 / 4096.0);
#endif
const float kPiUVE = 3.14159265359;
const float kBrdfEpsilonUVE = 0.0001;
/// The spot cone's bright inner region as a fraction of its authored outer half-angle; the band
/// between the two is where the light falls off. Derived rather than authored so a material's
/// existing single spotAngleDegrees keeps meaning exactly what it meant.
const float kSpotInnerConeRatioUVE = 0.85;

vec3 SafeNormalizeUVE(vec3 value) {
    return value / max(length(value), kBrdfEpsilonUVE);
}

vec3 HemisphereAmbientUVE(vec3 normal) {
    if (dot(uSkyAmbient, uSkyAmbient) + dot(uGroundAmbient, uGroundAmbient) < 1.0e-10) {
        return uAmbientColor;
    }
    float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyLum = vec3(dot(uSkyAmbient, vec3(0.2126, 0.7152, 0.0722)));
    vec3 groundLum = vec3(dot(uGroundAmbient, vec3(0.2126, 0.7152, 0.0722)));
    vec3 balancedSky = mix(skyLum, uSkyAmbient, 0.32);
    vec3 balancedGround = mix(groundLum, uGroundAmbient, 0.38);
    return mix(balancedGround, balancedSky, hemi);
}

vec2 AmbientEnvironmentUvUVE(vec3 direction) {
    vec3 dir = SafeNormalizeUVE(direction);
    float longitude = atan(dir.z, dir.x);
    float latitude = asin(clamp(dir.y, -1.0, 1.0));
    return vec2(longitude * (1.0 / (2.0 * kPiUVE)) + 0.5, latitude * (1.0 / kPiUVE) + 0.5);
}

vec3 SampleAmbientEnvironmentUVE(vec3 direction) {
    return max(texture(uAmbientEnvironmentMap, AmbientEnvironmentUvUVE(direction)).rgb, vec3(0.0));
}

vec3 SampleEnvironmentIrradianceUVE(vec3 normal) {
    vec3 n = SafeNormalizeUVE(normal);
    vec3 referenceAxis = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalizeUVE(cross(referenceAxis, n));
    vec3 bitangent = cross(n, tangent);
    const float spread = 0.65;
    return SampleAmbientEnvironmentUVE(n) * 0.4 +
           (SampleAmbientEnvironmentUVE(SafeNormalizeUVE(n + tangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(n - tangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(n + bitangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(n - bitangent * spread))) * 0.15;
}

vec3 SampleEnvironmentSpecularUVE(vec3 direction, float roughness) {
    vec3 reflection = SafeNormalizeUVE(direction);
    vec3 referenceAxis = abs(reflection.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalizeUVE(cross(referenceAxis, reflection));
    vec3 bitangent = cross(reflection, tangent);
    float spread = clamp(roughness, 0.0, 1.0) * 0.65;
    return SampleAmbientEnvironmentUVE(reflection) * 0.4 +
           (SampleAmbientEnvironmentUVE(SafeNormalizeUVE(reflection + tangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(reflection - tangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(reflection + bitangent * spread)) +
            SampleAmbientEnvironmentUVE(SafeNormalizeUVE(reflection - bitangent * spread))) * 0.15;
}

vec3 AmbientFromSourceUVE(vec3 normal) {
    if (uAmbientSource == 0) {
        return vec3(0.0);
    }
    if (uAmbientSource == 1) {
        return max(uAmbientColor, vec3(0.0));
    }
    if (uAmbientSource == 3 && uAmbientEnvironmentMapEnabled != 0) {
        return max(uAmbientColor, vec3(0.0)) * SampleEnvironmentIrradianceUVE(normal);
    }
    // Sky is the compatibility path and the fallback while an Environment Map asset is loading
    // or missing.
    return HemisphereAmbientUVE(normal);
}

// Tier 2.3: the probe is a real cubemap now — one sampler, one texture() call. The face-select
// if-chain, planar projection, and per-face samplers it replaces are gone (see git history);
// GPU-side face selection also filters seamlessly across face edges, where the manual math
// clamped to each face's edge texels (final-look parity passes with the 2D viewport).
vec3 SampleReflectionProbeUVE(vec3 direction) {
    return texture(uReflectionProbeCube, direction).rgb;
}

float ReflectionProbeInfluenceUVE(vec3 worldPoint) {
    vec3 halfExtents = uReflectionProbeHalfExtents;
    if (halfExtents.x <= 0.0 || halfExtents.y <= 0.0 || halfExtents.z <= 0.0) {
        return 0.0;
    }
    vec3 delta = worldPoint - uReflectionProbePosition;
    vec3 localPoint = vec3(dot(delta, uReflectionProbeAxisX), dot(delta, uReflectionProbeAxisY),
                           dot(delta, uReflectionProbeAxisZ));
    float normalizedDistance = max(max(abs(localPoint.x) / halfExtents.x, abs(localPoint.y) / halfExtents.y),
                                   abs(localPoint.z) / halfExtents.z);
    if (normalizedDistance >= 1.0) {
        return 0.0;
    }
    return 1.0 - normalizedDistance;
}

vec3 ReflectionProbeBoxProjectUVE(vec3 worldOrigin, vec3 worldDirection) {
    vec3 halfExtents = uReflectionProbeHalfExtents;
    vec3 delta = worldOrigin - uReflectionProbePosition;
    vec3 localOrigin = vec3(dot(delta, uReflectionProbeAxisX), dot(delta, uReflectionProbeAxisY),
                            dot(delta, uReflectionProbeAxisZ));
    vec3 localDirection = vec3(dot(worldDirection, uReflectionProbeAxisX),
                               dot(worldDirection, uReflectionProbeAxisY),
                               dot(worldDirection, uReflectionProbeAxisZ));
    float tFar = 1.0e20;
    for (int axis = 0; axis < 3; ++axis) {
        float origin = axis == 0 ? localOrigin.x : (axis == 1 ? localOrigin.y : localOrigin.z);
        float dir = axis == 0 ? localDirection.x : (axis == 1 ? localDirection.y : localDirection.z);
        float halfExtent = axis == 0 ? halfExtents.x : (axis == 1 ? halfExtents.y : halfExtents.z);
        if (abs(dir) < kBrdfEpsilonUVE) {
            if (origin < -halfExtent || origin > halfExtent) {
                return worldDirection;
            }
            continue;
        }
        float inv = 1.0 / dir;
        float t1 = (-halfExtent - origin) * inv;
        float t2 = (halfExtent - origin) * inv;
        tFar = min(tFar, max(t1, t2));
    }
    if (!(tFar > kBrdfEpsilonUVE)) {
        return worldDirection;
    }
    vec3 localHit = localOrigin + localDirection * tFar;
    vec3 worldHit = uReflectionProbePosition + uReflectionProbeAxisX * localHit.x +
                    uReflectionProbeAxisY * localHit.y + uReflectionProbeAxisZ * localHit.z;
    return worldHit - uReflectionProbePosition;
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
    ivec2 sz = ivec2(1);
    if (cascadeIndex == 0) {
        sz = textureSize(uShadowMapTextures[0], 0);
    } else if (cascadeIndex == 1) {
        sz = textureSize(uShadowMapTextures[1], 0);
    } else {
        sz = textureSize(uShadowMapTextures[2], 0);
    }
    return 1.0 / max(vec2(sz), vec2(1.0));
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

    ivec2 mapSize = cascadeIndex < 0 ? textureSize(uShadowMapTexture, 0)
                                     : textureSize(uShadowMapTextures[clamp(cascadeIndex, 0, 2)], 0);
    if (mapSize.x <= 1 || mapSize.y <= 1) {
        return 1.0;
    }

    int kernelRadius = clamp(uShadowPcfKernelRadius, 0, 2);
    vec2 texelSize = cascadeIndex < 0 ? 1.0 / max(vec2(mapSize), vec2(1.0))
                                      : CascadeTexelSizeUVE(cascadeIndex);
    float currentDepth = projected.z;
    float bias = max(uShadowBias * (1.0 - max(dot(normal, lightDirection), 0.0)) * max(uShadowNormalBias, 0.0),
                     uShadowBias * 0.2);
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

    float factor = mix(1.0, visibleSamples / float(sampleCount), clamp(uShadowOpacity, 0.0, 1.0));
    if (isnan(factor) || isinf(factor)) {
        return 1.0;
    }
    return factor;
}

float FadeShadowToLitAtDistanceUVE(float shadowFactor, float viewDepth, float finalCascadeFarDepth) {
    float fadeRange = min(max(uShadowMaxDistanceFadeRange, 0.0), max(finalCascadeFarDepth, 0.0));
    if (fadeRange <= 0.0) {
        return shadowFactor;
    }
    float fadeStartDepth = max(finalCascadeFarDepth - fadeRange, 0.0);
    float shadowWeight = 1.0 - smoothstep(fadeStartDepth, finalCascadeFarDepth, viewDepth);
    return mix(1.0, shadowFactor, shadowWeight);
}

float DirectionalShadowFactorUVE(vec3 normal, vec3 lightDirection) {
    if (uShadowCascadeCount <= 0) {
        // No cascades: one map in uShadowMapTexture, projected by vLightSpacePosition.
        return ShadowFactorFromPositionUVE(vLightSpacePosition, normal, lightDirection, -1);
    }

    float viewDepth = length(vWorldPosition - uViewPosition);
    int finalCascadeIndex = clamp(uShadowCascadeCount - 1, 0, 2);
    float finalCascadeFarDepth = uShadowCascadeSplits[finalCascadeIndex];
    // Beyond the last split the light casts no shadow; at the split the fade has reached fully lit.
    if (viewDepth > finalCascadeFarDepth) {
        return 1.0;
    }
    int cascadeIndex = finalCascadeIndex;
    for (int candidateIndex = 0; candidateIndex < 2; ++candidateIndex) {
        if (candidateIndex < uShadowCascadeCount && viewDepth <= uShadowCascadeSplits[candidateIndex]) {
            cascadeIndex = candidateIndex;
            break;
        }
    }
    float shadowFactor = ShadowFactorFromPositionUVE(CascadeLightSpacePositionUVE(cascadeIndex), normal,
                                                      lightDirection, cascadeIndex);
    if (cascadeIndex < finalCascadeIndex) {
        float cascadeNearDepth = cascadeIndex == 0 ? 0.0 : uShadowCascadeSplits[cascadeIndex - 1];
        float cascadeFarDepth = uShadowCascadeSplits[cascadeIndex];
        float cascadeDepthRange = max(cascadeFarDepth - cascadeNearDepth, 0.0001);
        float blendWidth = cascadeDepthRange * clamp(uShadowCascadeBlendRatio, 0.0, 0.25);
        float blendStartDepth = cascadeFarDepth - blendWidth;
        if (blendWidth > 0.0 && viewDepth > blendStartDepth) {
            float nextCascadeShadowFactor = ShadowFactorFromPositionUVE(
                CascadeLightSpacePositionUVE(cascadeIndex + 1), normal, lightDirection, cascadeIndex + 1);
            float blendWeight = smoothstep(blendStartDepth, cascadeFarDepth, viewDepth);
            shadowFactor = mix(shadowFactor, nextCascadeShadowFactor, blendWeight);
        }
    }
    float finalFactor = FadeShadowToLitAtDistanceUVE(shadowFactor, viewDepth, finalCascadeFarDepth);
    if (isnan(finalFactor) || isinf(finalFactor)) {
        return 1.0;
    }
    return finalFactor;
}

// kAtlasShadowBiasMetersPerUnitUVE keeps the atlas on the same authored scale as the cascades: the
// directional default uShadowBias (0.0025) is authored against roughly a 40 m cascade half-extent,
// where it is worth about 0.05 m of separation. Reusing that ratio means one authored bias value
// keeps meaning the same "how much gap", even though the two paths store depth in different spaces.
const float kAtlasShadowBiasMetersPerUnitUVE = 20.0;

/// Converts a world-space shadow bias into normalized depth AT THE RECEIVER'S OWN DEPTH.
///
/// A perspective projection's depth slope varies by orders of magnitude along one ray, so a constant
/// normalized-depth bias is either acne beside the near plane or metres of peter-panning out at the
/// light's range - and the same light is both, depending on where the surface is. Mirror of
/// ShadowDepthSlopeUVE in light_shadow_planner_uve.h; the unit tests pin the two together.
float AtlasShadowDepthBiasUVE(vec2 depthRange, float projectionDepth, float normalDotLight) {
    float nearPlane = max(depthRange.x, 0.01);
    float farPlane = max(depthRange.y, nearPlane + 0.1);
    // Same shape as the directional bias, reinterpreted in metres rather than normalized units.
    float normalizedBias = max(uShadowBias * (1.0 - clamp(normalDotLight, 0.0, 1.0)) *
                                   max(uShadowNormalBias, 0.0),
                               uShadowBias * 0.2);
    float linearDepth =
        (nearPlane * farPlane) / max(farPlane - (farPlane - nearPlane) * projectionDepth, 1.0e-6);
    float depthSlope =
        (nearPlane * farPlane) / max((farPlane - nearPlane) * linearDepth * linearDepth, 1.0e-6);
    return normalizedBias * kAtlasShadowBiasMetersPerUnitUVE * depthSlope;
}

/// Spot shadows: one perspective depth tile per light, sampled with the same receiver-plane PCF the
/// directional cascades use.
///
/// Depth convention: `projectionDepth` is the engine's [0, 1] projection depth (Matrix4x4UVE::
/// PerspectiveUVE is Vulkan-style), and GL's depth attachment stores `(ndc + 1) / 2`, which is the
/// `* 0.5 + 0.5` below - exactly the convention ShadowFactorFromPositionUVE has always used.
float SpotShadowFactorUVE(int lightIndex, vec3 normal, vec3 lightDirection) {
    if (uLightShadowEnabled[lightIndex] == 0) {
        return 1.0;
    }
    vec4 lightSpacePos = uLightShadowMatrices[lightIndex] * vec4(vWorldPosition, 1.0);
    if (abs(lightSpacePos.w) <= 0.0001) {
        return 1.0;
    }
    vec3 projected = lightSpacePos.xyz / lightSpacePos.w;
    float projectionDepth = projected.z;
    vec2 tileUv = projected.xy * 0.5 + 0.5;
    // Rejected outside the light's own frustum: behind its near plane, past its range, or outside
    // the cone. Those fragments are lit by something else, never shadowed by this light.
    if (tileUv.x < 0.0 || tileUv.x > 1.0 || tileUv.y < 0.0 || tileUv.y > 1.0 ||
        projectionDepth <= 0.0 || projectionDepth >= 1.0) {
        return 1.0;
    }
    vec4 tileRect = uLightShadowTileRects[lightIndex];
    vec2 tileSize = tileRect.zw - tileRect.xy;
    if (tileSize.x <= 0.0 || tileSize.y <= 0.0) {
        return 1.0;
    }

    float currentDepth = projectionDepth * 0.5 + 0.5;
    float bias = AtlasShadowDepthBiasUVE(uLightShadowDepthRange[lightIndex], projectionDepth,
                                         dot(normal, lightDirection));
    vec2 atlasTexelSize = max(uShadowAtlasTexelSize, vec2(0.0));
    int kernelRadius = clamp(uShadowPcfKernelRadius, 0, 2);
    float visibleSamples = 0.0;
    int sampleCount = 0;

    vec2 minUv = tileRect.xy + atlasTexelSize * 0.5;
    vec2 maxUv = tileRect.zw - atlasTexelSize * 0.5;

    for (int offsetY = -2; offsetY <= 2; ++offsetY) {
        for (int offsetX = -2; offsetX <= 2; ++offsetX) {
            if (abs(offsetX) > kernelRadius || abs(offsetY) > kernelRadius) {
                continue;
            }
            vec2 sampleUv = tileRect.xy + tileUv * tileSize + vec2(offsetX, offsetY) * atlasTexelSize;
            sampleUv = clamp(sampleUv, minUv, maxUv);
            float sampledDepth = texture(uShadowAtlasTexture, sampleUv).r;
            visibleSamples += (currentDepth - bias > sampledDepth) ? 0.0 : 1.0;
            ++sampleCount;
        }
    }
    float factor = mix(1.0, visibleSamples / float(max(sampleCount, 1)), clamp(uShadowOpacity, 0.0, 1.0));
    return (isnan(factor) || isinf(factor)) ? 1.0 : factor;
}

/// The six cube faces, in the order +X, -X, +Y, -Y, +Z, -Z.
///
/// Explicit constants rather than a runtime basis build: `PointLightShadowFaceTableUVE` in
/// shadow_atlas_manager_uve.h holds the same six triples, and the renderer rasterizes each face
/// through exactly the one this shader samples with. A seam that lands on the wrong face reads as a
/// plausible shadow instead of as a bug, so the two tables are pinned together by tests.
void PointShadowFaceBasisUVE(int faceIndex, out vec3 faceRight, out vec3 faceUp,
                             out vec3 faceForward) {
    if (faceIndex == 0) {
        faceRight = vec3(0.0, 0.0, -1.0); faceUp = vec3(0.0, 1.0, 0.0); faceForward = vec3(1.0, 0.0, 0.0);
    } else if (faceIndex == 1) {
        faceRight = vec3(0.0, 0.0, 1.0); faceUp = vec3(0.0, 1.0, 0.0); faceForward = vec3(-1.0, 0.0, 0.0);
    } else if (faceIndex == 2) {
        faceRight = vec3(1.0, 0.0, 0.0); faceUp = vec3(0.0, 0.0, -1.0); faceForward = vec3(0.0, 1.0, 0.0);
    } else if (faceIndex == 3) {
        faceRight = vec3(1.0, 0.0, 0.0); faceUp = vec3(0.0, 0.0, 1.0); faceForward = vec3(0.0, -1.0, 0.0);
    } else if (faceIndex == 4) {
        faceRight = vec3(1.0, 0.0, 0.0); faceUp = vec3(0.0, 1.0, 0.0); faceForward = vec3(0.0, 0.0, 1.0);
    } else {
        faceRight = vec3(-1.0, 0.0, 0.0); faceUp = vec3(0.0, 1.0, 0.0); faceForward = vec3(0.0, 0.0, -1.0);
    }
}

/// Cube face a direction lands on by dominant axis, ties broken X then Y then Z - the same rule
/// `ShadowAtlasManagerUVE::PointLightShadowFaceForDirectionUVE` implements.
int PointShadowFaceUVE(vec3 direction) {
    vec3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        return direction.x >= 0.0 ? 0 : 1;
    }
    if (a.y >= a.z) {
        return direction.y >= 0.0 ? 2 : 3;
    }
    return direction.z >= 0.0 ? 4 : 5;
}

/// `[0, 1]` projection depth a point at `axisDistance` along the face axis writes.
///
/// Mirror of `PointShadowProjectionDepthUVE` in light_shadow_planner_uve.h, which the CPU uses to
/// predict the same number with the light's own near/far. Both are the inverse of the
/// `AtlasShadowDepthBiasUVE` linearization.
float PointShadowProjectionDepthUVE(vec2 depthRange, float axisDistance) {
    float nearPlane = max(depthRange.x, 0.01);
    float farPlane = max(depthRange.y, nearPlane + 0.1);
    return farPlane * (1.0 - nearPlane / max(axisDistance, nearPlane)) / (farPlane - nearPlane);
}

/// Point shadows: six perspective faces packed into six atlas tiles, one per cube direction.
///
/// A single 2D octahedral map is what an omnidirectional shadow "wants", but it cannot be
/// rasterized directly: the mapping is rational, so producing it needs either a cube render plus a
/// resolve pass or a geometry shader, and neither exists in this renderer. Six perspective faces
/// are the shape a depth-only pass can actually write - the same face matrices the CPU plans with -
/// and the shader selects a face by dominant axis, which is exactly the seam rule a cube-map
/// sampler uses internally.
float PointShadowFactorUVE(int lightIndex, vec3 normal, vec3 lightPosition, float lightRange) {
    if (uLightShadowEnabled[lightIndex] == 0) {
        return 1.0;
    }
    vec3 toFrag = vWorldPosition - lightPosition;
    float dist = length(toFrag);
    if (dist <= 0.0001 || dist > lightRange) {
        return 1.0;
    }
    vec3 dir = toFrag / dist;
    int face = PointShadowFaceUVE(dir);
    vec3 faceRight;
    vec3 faceUp;
    vec3 faceForward;
    PointShadowFaceBasisUVE(face, faceRight, faceUp, faceForward);

    // The face's own coordinates are the ray's components along its basis. Their RATIO is the
    // normalized device position - dividing the absolute distance instead would make the sample
    // drift toward the tile centre as the light's range grows, which looks like a shadow that
    // shrinks away from the caster rather than like broken math.
    float axisDot = dot(dir, faceForward);
    if (axisDot <= 0.0001) {
        return 1.0;
    }
    vec2 faceNdc = vec2(dot(dir, faceRight), dot(dir, faceUp)) / axisDot;
    vec2 tileUv = faceNdc * 0.5 + 0.5;
    if (tileUv.x < 0.0 || tileUv.x > 1.0 || tileUv.y < 0.0 || tileUv.y > 1.0) {
        return 1.0;
    }

    vec4 tileRect = uLightShadowCubeRects[lightIndex * 6 + face];
    vec2 tileSize = tileRect.zw - tileRect.xy;
    if (tileSize.x <= 0.0 || tileSize.y <= 0.0) {
        // This face did not make it into the atlas this frame (its cull found nothing, or the frame
        // budget ran out). The light stays enabled for the faces that did.
        return 1.0;
    }

    // Distance along the face axis in metres - clip-space w for a vertex rendered through this
    // face's matrix, and therefore what the depth attachment stored.
    float axisDistance = axisDot * dist;
    vec2 depthRange = uLightShadowDepthRange[lightIndex];
    float projectionDepth = PointShadowProjectionDepthUVE(depthRange, axisDistance);
    if (projectionDepth <= 0.0 || projectionDepth >= 1.0) {
        return 1.0;
    }
    float currentDepth = projectionDepth * 0.5 + 0.5;
    float bias = AtlasShadowDepthBiasUVE(depthRange, projectionDepth, dot(normal, -dir));

    vec2 atlasTexelSize = max(uShadowAtlasTexelSize, vec2(0.0));
    int kernelRadius = clamp(uShadowPcfKernelRadius, 0, 2);
    float visibleSamples = 0.0;
    int sampleCount = 0;

    vec2 minUv = tileRect.xy + atlasTexelSize * 0.5;
    vec2 maxUv = tileRect.zw - atlasTexelSize * 0.5;

    for (int offsetY = -2; offsetY <= 2; ++offsetY) {
        for (int offsetX = -2; offsetX <= 2; ++offsetX) {
            if (abs(offsetX) > kernelRadius || abs(offsetY) > kernelRadius) {
                continue;
            }
            vec2 sampleUv = tileRect.xy + tileUv * tileSize + vec2(offsetX, offsetY) * atlasTexelSize;
            sampleUv = clamp(sampleUv, minUv, maxUv);
            float sampledDepth = texture(uShadowAtlasTexture, sampleUv).r;
            visibleSamples += (currentDepth - bias > sampledDepth) ? 0.0 : 1.0;
            ++sampleCount;
        }
    }
    float factor = mix(1.0, visibleSamples / float(max(sampleCount, 1)), clamp(uShadowOpacity, 0.0, 1.0));
    return (isnan(factor) || isinf(factor)) ? 1.0 : factor;
}
// One exit point for the colour/alpha pair, so the four debug and unshaded early-outs cannot drift
// from the lit path on premultiplication. Only the pipeline state decides whether it is needed.
vec4 ComposeFragmentUVE(vec3 color, float alpha) {
    return vec4(uPremultiplyAlpha != 0 ? color * alpha : color, alpha);
}
void main() {
    vec2 uv = vTexCoord * uUvScale.xy + uUvOffset.xy;
    vec4 albedoSample = texture(uAlbedoTexture, uv);
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    vec3 albedo = albedoSample.rgb * uAlbedoColor;
    // Material alpha from the authored source, then the instance's fade on top of it. Before this,
    // the material's alpha never reached the framebuffer at all: every surface composited at the
    // instance opacity, so a 50%-alpha texture drew fully opaque and a tinted-glass material had
    // no way to be translucent.
    float materialAlpha = uAlphaSource == 0 ? clamp(albedoSample.a * uAlbedoAlpha, 0.0, 1.0) : 1.0;
    float surfaceOpacity = clamp(uSurfaceOpacity, 0.0, 1.0) * materialAlpha;
    if (uShadingMode == 1 || uUnshaded != 0) {
        FragColor = ComposeFragmentUVE(albedo, surfaceOpacity);
        return;
    }
    if (uShadingMode == 2) {
        FragColor = ComposeFragmentUVE(mix(albedo * 0.25, vec3(0.05, 0.85, 1.0), 0.75), surfaceOpacity);
        return;
    }
    float ambientOcclusion = mix(1.0, texture(uAOTexture, uv).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 normal = SafeNormalizeUVE(vWorldNormal);
    // A few legacy/direct RHI callers do not provide a usable normal attribute. Keep those
    // materials lit instead of allowing a zero vector to poison the tangent frame and BRDF.
    if (dot(normal, normal) <= kBrdfEpsilonUVE * kBrdfEpsilonUVE) {
        normal = vec3(0.0, 0.0, 1.0);
    }
    vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
    if (dot(tangent, tangent) <= 0.00000001) {
        vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = cross(fallbackAxis, normal);
    }
    tangent = SafeNormalizeUVE(tangent);
    vec3 bitangent = SafeNormalizeUVE(cross(normal, tangent));
    bitangent *= vTangentHandedness < 0.0 ? -1.0 : 1.0;
    vec3 tangentSpaceNormal = texture(uNormalTexture, uv).xyz * 2.0 - 1.0;
    tangentSpaceNormal.xy *= uNormalScale;
    normal = SafeNormalizeUVE(mat3(tangent, bitangent, normal) * tangentSpaceNormal);

    if (uShadingMode == 4) {
        FragColor = ComposeFragmentUVE(normal * 0.5 + 0.5, surfaceOpacity);
        return;
    }

    vec3 viewDirection = SafeNormalizeUVE(uViewPosition - vWorldPosition);
    // Meshes authored with the opposite winding/normal convention are still valid two-sided
    // surfaces. Orient the final mapped normal toward the viewer before evaluating the BRDF.
    if (dot(normal, viewDirection) < 0.0) {
        normal = -normal;
    }

    if (uShadingMode == 3) {
        albedo = vec3(0.82);
    }

    vec3 metallicRoughnessSample = texture(uMetallicRoughnessTexture, uv).rgb;
    float metallic = uShadingMode == 3 ? 0.0 : clamp(uMetallic * metallicRoughnessSample.b, 0.0, 1.0);
    float roughness = uShadingMode == 3 ? 0.5 : clamp(uRoughness * metallicRoughnessSample.g, 0.04, 1.0);
    // Ambient, split into diffuse and specular exactly as the direct term is.
    //
    // The old ambient was purely diffuse: albedo * ambient * ao. For a dielectric that is roughly
    // right, but a metal has NO diffuse response at all - its diffuseWeight is (1-F)(1-metallic),
    // which is zero at metallic 1 - so a metal lit only by ambient came out BLACK. That is the
    // single most visible way a correct BRDF still looks wrong, and it is why "the PBR looks
    // broken" usually means "there is no ambient specular".
    //
    // Specular ambient comes from the selected source or the strongest captured ReflectionProbe3D
    // at the eye (six 2D faces, box-projected). Environment-map ambient uses a small equirectangular
    // cone filter; the legacy Sky source remains the hemisphere approximation.
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
    vec3 ambientColor = AmbientFromSourceUVE(normal);
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 specEnv = ambientColor;
    if (uAmbientSource == 3 && uAmbientEnvironmentMapEnabled != 0) {
        specEnv = max(uAmbientColor, vec3(0.0)) * SampleEnvironmentSpecularUVE(reflection, roughness);
    }
    if (uReflectionProbeEnabled != 0) {
        float probeWeight = ReflectionProbeInfluenceUVE(vWorldPosition);
        if (probeWeight > 0.0) {
            vec3 sampleDir = ReflectionProbeBoxProjectUVE(vWorldPosition, reflection);
            vec3 captured = SampleReflectionProbeUVE(sampleDir);
            specEnv = mix(specEnv, captured, probeWeight * (1.0 - roughness * roughness));
        }
    }
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * ambientColor;
    vec3 ambientSpecular = ambientFresnel * specEnv;
    // AO occludes both terms. Applying it to the diffuse alone is a common shortcut, but a crevice
    // does not stop reflecting light in a way the sky can reach either.
    vec3 lighting = (ambientDiffuse + ambientSpecular) * ambientOcclusion +
                    uEmissiveColor * max(uEmissiveEnergy, 0.0) * texture(uEmissiveTexture, uv).rgb;

    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        LightUVE light = uLights[lightIndex];
        if (light.intensity <= 0.0) {
            continue;
        }
        if ((light.cullMask & uMeshRenderLayers) == 0) {
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

        // Treat the material as two-sided for direct lighting. Imported meshes and legacy RHI
        // callers can disagree on winding; using the absolute cosine prevents a valid back-face
        // normal from collapsing the entire BRDF to black while shadowing remains directional.
        float normalDotLight = mix(abs(dot(normal, lightDirection)), 1.0, 0.18);
        float normalDotView = max(abs(dot(normal, viewDirection)), 0.08);
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
                        max(4.0 * normalDotView * normalDotLight, kBrdfEpsilonUVE) * max(light.specular, 0.0);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 diffuse = diffuseWeight * albedo / kPiUVE;
        vec3 radiance = light.color * light.intensity * attenuation;
        vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

        if (light.type == 0) {
            float shadowFactor = DirectionalShadowFactorUVE(normal, lightDirection);
            if (isnan(shadowFactor) || isinf(shadowFactor)) {
                shadowFactor = 1.0;
            }
            directContribution *= mix(0.28, 1.0, clamp(shadowFactor, 0.0, 1.0));
        } else if (light.type == 1) {
            float shadowFactor = PointShadowFactorUVE(lightIndex, normal, light.position, light.range);
            directContribution *= mix(0.28, 1.0, clamp(shadowFactor, 0.0, 1.0));
        } else if (light.type == 2) {
            float shadowFactor = SpotShadowFactorUVE(lightIndex, normal, lightDirection);
            directContribution *= mix(0.28, 1.0, clamp(shadowFactor, 0.0, 1.0));
        }
        if (!isnan(directContribution.r) && !isinf(directContribution.r) &&
            !isnan(directContribution.g) && !isinf(directContribution.g) &&
            !isnan(directContribution.b) && !isinf(directContribution.b)) {
            lighting += directContribution;
        }
    }

    // Safe baseline ambient fill so unlit back-faces or scenes without an explicit sky are never pitch black.
    // Preserves the accumulated PBR direct lighting and highlights instead of discarding them.
    vec3 baselineAmbient = max(ambientColor, vec3(0.04)) * albedo * ambientOcclusion;
    vec3 finalColor = max(lighting, baselineAmbient);
    FragColor = ComposeFragmentUVE(finalColor, surfaceOpacity);
}
#endif
