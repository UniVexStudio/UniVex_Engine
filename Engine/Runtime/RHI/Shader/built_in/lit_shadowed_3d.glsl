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
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
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
uniform int uReflectionProbeEnabled;
uniform vec3 uReflectionProbePosition;
uniform vec3 uReflectionProbeAxisX;
uniform vec3 uReflectionProbeAxisY;
uniform vec3 uReflectionProbeAxisZ;
uniform vec3 uReflectionProbeHalfExtents;
uniform sampler2D uReflectionProbeFaces[6];

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
    return mix(uGroundAmbient, uSkyAmbient, hemi);
}

int SelectCubemapFaceUVE(vec3 direction) {
    vec3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        return direction.x >= 0.0 ? 0 : 1;
    }
    if (a.y >= a.z) {
        return direction.y >= 0.0 ? 2 : 3;
    }
    return direction.z >= 0.0 ? 4 : 5;
}

vec3 CubemapFaceForwardUVE(int face) {
    if (face == 0) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (face == 1) {
        return vec3(-1.0, 0.0, 0.0);
    }
    if (face == 2) {
        return vec3(0.0, 1.0, 0.0);
    }
    if (face == 3) {
        return vec3(0.0, -1.0, 0.0);
    }
    if (face == 4) {
        return vec3(0.0, 0.0, 1.0);
    }
    return vec3(0.0, 0.0, -1.0);
}

vec3 CubemapFaceUpUVE(int face) {
    if (face == 2) {
        return vec3(0.0, 0.0, -1.0);
    }
    if (face == 3) {
        return vec3(0.0, 0.0, 1.0);
    }
    return vec3(0.0, 1.0, 0.0);
}

vec3 SampleReflectionProbeFaceUVE(int face, vec2 uv) {
    if (face == 0) {
        return texture(uReflectionProbeFaces[0], uv).rgb;
    }
    if (face == 1) {
        return texture(uReflectionProbeFaces[1], uv).rgb;
    }
    if (face == 2) {
        return texture(uReflectionProbeFaces[2], uv).rgb;
    }
    if (face == 3) {
        return texture(uReflectionProbeFaces[3], uv).rgb;
    }
    if (face == 4) {
        return texture(uReflectionProbeFaces[4], uv).rgb;
    }
    return texture(uReflectionProbeFaces[5], uv).rgb;
}

vec3 SampleReflectionProbeUVE(vec3 direction) {
    int face = SelectCubemapFaceUVE(direction);
    vec3 forward = CubemapFaceForwardUVE(face);
    vec3 right = SafeNormalizeUVE(cross(forward, CubemapFaceUpUVE(face)));
    vec3 cameraUp = cross(-forward, right);
    float denom = max(dot(direction, forward), kBrdfEpsilonUVE);
    vec2 uv = vec2(dot(direction, right), dot(direction, cameraUp)) / denom * 0.5 + 0.5;
    return SampleReflectionProbeFaceUVE(face, uv);
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
    // Past the last cascade the light casts no shadow: that is where its shadow distance ends.
    if (viewDepth > uShadowCascadeSplits[clamp(uShadowCascadeCount - 1, 0, 2)]) {
        return 1.0;
    }
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
    // Specular ambient comes from the strongest captured ReflectionProbe3D at the eye (six 2D
    // faces, box-projected). Diffuse ambient stays the hemisphere - a 128 LDR capture is a poor
    // irradiance map. Disabled probes leave this path identical to hemisphere-only.
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
    vec3 ambientColor = HemisphereAmbientUVE(normal);
    vec3 specEnv = ambientColor;
    if (uReflectionProbeEnabled != 0) {
        float probeWeight = ReflectionProbeInfluenceUVE(vWorldPosition);
        if (probeWeight > 0.0) {
            vec3 reflection = reflect(-viewDirection, normal);
            vec3 sampleDir = ReflectionProbeBoxProjectUVE(vWorldPosition, reflection);
            vec3 captured = SampleReflectionProbeUVE(sampleDir);
            specEnv = mix(ambientColor, captured, probeWeight * (1.0 - roughness * roughness));
        }
    }
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * ambientColor;
    vec3 ambientSpecular = ambientFresnel * specEnv;
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
