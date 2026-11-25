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
#if !defined(UVE_COMPATIBILITY)
// Object-space position, normal, and object-to-world normal basis for triplanar mapping.
out vec3 vObjectPosition;
out vec3 vObjectNormal;
out vec3 vNormalAxisX;
out vec3 vNormalAxisY;
out vec3 vNormalAxisZ;
#endif
out vec4 vLightSpacePosition;
#if !defined(UVE_COMPATIBILITY)
out vec4 vLightSpacePositions[3];
#endif

#if defined(UVE_INSTANCED) || defined(UVE_INDIRECT_INSTANCED)
// The instanced variant reads its per-object transforms from a storage buffer indexed by
// gl_InstanceID instead of from a uniform set once per draw. That is the entire difference
// between the two variants, and it is why this is a #define rather than a second shader file:
// the 240-odd lines of lighting below are shared verbatim, so an instanced object and a
// non-instanced one cannot drift apart in how they are lit.
//
// Matrices arrive TRANSPOSED from the host (Matrix4x4 is row-major; std430 mat4 is
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
uniform int uShadowCascadeCount;
uniform float uShadowMapResolution;

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
#if defined(UVE_COMPATIBILITY)
    if (uShadowCascadeCount > 0) {
        mat4 lightMatrix = uLightSpaceMatrices[0];
        float worldNormalLen = length(vWorldNormal);
        vec3 offsetNormal = worldNormalLen > 1.0e-5 ? (vWorldNormal / worldNormalLen) : vec3(0.0, 1.0, 0.0);
        float texelWorld = 0.0;
        float scale = length(vec3(lightMatrix[0][0], lightMatrix[1][0], lightMatrix[2][0]));
        if (uShadowMapResolution > 1.0 && scale > 1.0e-6) {
            texelWorld = 2.0 / (scale * uShadowMapResolution);
        }
        vLightSpacePosition = lightMatrix * vec4(worldPosition.xyz + offsetNormal * (texelWorld * 0.9), 1.0);
    } else {
        vLightSpacePosition = uLightSpaceMatrix * worldPosition;
    }
#else
    // Scale-invariant object-space coordinates: triplanar projection moves and rotates with the
    // mesh while automatically tiling when the mesh is scaled so textures never stretch.
    vec3 objectScale = vec3(
        max(length(model[0].xyz), 1.0e-5),
        max(length(model[1].xyz), 1.0e-5),
        max(length(model[2].xyz), 1.0e-5)
    );
    vObjectPosition = aPosition * objectScale;
    vObjectNormal = aNormal;
    mat3 normalMat3 = mat3(normalMatrix);
    vNormalAxisX = normalMat3[0];
    vNormalAxisY = normalMat3[1];
    vNormalAxisZ = normalMat3[2];
    vLightSpacePosition = uLightSpaceMatrix * worldPosition;
    // Slope-scaled normal offset: surfaces facing the light shift minimally to keep contact
    // shadows grounded without peter-panning, while grazing angles shift by ~1.35 texels to
    // prevent self-shadowing acne. Skip unused cascades when uShadowCascadeCount < 3.
    int activeCascades = clamp(uShadowCascadeCount, 0, 3);
    vec3 offsetNormal = vec3(0.0, 1.0, 0.0);
    if (activeCascades > 0) {
        float worldNormalLen = length(vWorldNormal);
        offsetNormal = worldNormalLen > 1.0e-5 ? (vWorldNormal / worldNormalLen) : vec3(0.0, 1.0, 0.0);
    }
    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
        if (cascadeIndex >= activeCascades) {
            vLightSpacePositions[cascadeIndex] = vec4(0.0);
            continue;
        }
        mat4 lightMatrix = uLightSpaceMatrices[cascadeIndex];
        float texelWorld = 0.0;
        float scale = length(vec3(lightMatrix[0][0], lightMatrix[1][0], lightMatrix[2][0]));
        if (uShadowMapResolution > 1.0 && scale > 1.0e-6) {
            texelWorld = 2.0 / (scale * uShadowMapResolution);
        }
        vec3 lightAxisZ = vec3(lightMatrix[0][2], lightMatrix[1][2], lightMatrix[2][2]);
        float lightAxisLen = length(lightAxisZ);
        float cosTheta = lightAxisLen > 1.0e-6 ? abs(dot(offsetNormal, lightAxisZ / lightAxisLen)) : 0.0;
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float slopeOffset = texelWorld * mix(0.35, 1.35, sinTheta);
        vLightSpacePositions[cascadeIndex] = lightMatrix * vec4(worldPosition.xyz + offsetNormal * slopeOffset, 1.0);
    }
#endif
    gl_Position = uViewProjection * worldPosition;
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vWorldTangent;
in float vTangentHandedness;
in vec2 vTexCoord;
#if !defined(UVE_COMPATIBILITY)
in vec3 vObjectPosition;
in vec3 vObjectNormal;
in vec3 vNormalAxisX;
in vec3 vNormalAxisY;
in vec3 vNormalAxisZ;
#endif
in vec4 vLightSpacePosition;
#if !defined(UVE_COMPATIBILITY)
in vec4 vLightSpacePositions[3];
#endif
#ifdef UVE_WBOIT
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragRevealage;
#else
out vec4 FragColor;
#endif

struct Light {
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

uniform Light uLights[16];
uniform int uActiveLightCount;
uniform int uLightShadowSlot[16];
#if !defined(UVE_MOBILE) && !defined(UVE_COMPATIBILITY) && !defined(UVE_MOBILE_PROFILE)
struct ClusteredGpuLight {
    vec4 positionAndRange;
    vec4 directionAndType;
    vec4 colorAndIntensity;
    vec4 spotMaskSpecShadow;
};
layout(std430, binding = 4) readonly buffer ClusteredLightBlock {
    ClusteredGpuLight clusteredLights[];
};
layout(std430, binding = 5) readonly buffer ClusteredCellBlock {
    uvec2 clusteredCells[];
};
layout(std430, binding = 6) readonly buffer ClusteredIndexBlock {
    uint clusteredLightIndices[];
};
uniform int uClusteredLightingEnabled = 0;
uniform vec3 uClusterGridDims = vec3(16.0, 9.0, 24.0);
uniform vec2 uClusterNearFar = vec2(0.1, 1000.0);
uniform vec2 uClusterViewportSize = vec2(1280.0, 720.0);
#endif
uniform vec3 uAmbientColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform int uAmbientSource; // 0=None, 1=FlatColor, 2=Sky, 3=EnvironmentMap
// Sky reflections: what a mirror outdoors actually shows. Three bands instead of a captured cube,
// because the sky is the one environment that is smooth enough for a gradient to be honest.
uniform int uSkyReflectionsLinked;
uniform vec3 uSkyReflectionSky;
uniform vec3 uSkyReflectionHorizon;
uniform vec3 uSkyReflectionGround;
uniform sampler2D uAmbientEnvironmentMap;
uniform int uAmbientEnvironmentMapEnabled;
uniform vec3 uViewPosition;
#if UVE_MOBILE
uniform int uOrthographic;
uniform vec3 uCameraForward;
#else
uniform int uOrthographic = 0;
uniform vec3 uCameraForward = vec3(0.0, 0.0, -1.0);
#endif
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
uniform int uNormalFlipGreen;
#else
uniform int uNormalFlipGreen = 0;
#endif
#if UVE_MOBILE
uniform float uDielectricSpecular;
#else
uniform float uDielectricSpecular = 0.5;
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
// Each map's own reading, on top of its UV set (see MaterialMapSettings). All zero reads the map
// as authored: Transform is (tiling - 1, offset); Control is (channel: 0 RGB, 1 R, 2 G, 3 B, 4 A;
// invert; 1 - strength; on UV2).
#if UVE_MOBILE
uniform vec4 uAlbedoMapTransform;
#else
uniform vec4 uAlbedoMapTransform = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uAlbedoMapControl;
#else
uniform vec4 uAlbedoMapControl = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uNormalMapTransform;
#else
uniform vec4 uNormalMapTransform = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uNormalMapControl;
#else
uniform vec4 uNormalMapControl = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uMetallicRoughnessMapTransform;
#else
uniform vec4 uMetallicRoughnessMapTransform = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uMetallicRoughnessMapControl;
#else
uniform vec4 uMetallicRoughnessMapControl = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uOcclusionMapTransform;
#else
uniform vec4 uOcclusionMapTransform = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uOcclusionMapControl;
#else
uniform vec4 uOcclusionMapControl = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uEmissiveMapTransform;
#else
uniform vec4 uEmissiveMapTransform = vec4(0.0);
#endif
#if UVE_MOBILE
uniform vec4 uEmissiveMapControl;
#else
uniform vec4 uEmissiveMapControl = vec4(0.0);
#endif
// UV2: the mesh's texture coordinates with their own scale and offset (meshes carry one UV set),
// used by AO and Emission when the material puts them "on UV2". Each set can instead be triplanar:
// projected along the three axes of the object (or the world) and blended by the normal.
// uUv*Triplanar: 0 off, 1 object space, 2 world space. Triplanar scale is (x, y, x).
#if UVE_MOBILE
uniform vec3 uUv2Scale;
#else
uniform vec3 uUv2Scale = vec3(1.0, 1.0, 1.0);
#endif
#if UVE_MOBILE
uniform vec3 uUv2Offset;
#else
uniform vec3 uUv2Offset = vec3(0.0, 0.0, 0.0);
#endif
#if UVE_MOBILE
uniform int uUv1Triplanar;
#else
uniform int uUv1Triplanar = 0;
#endif
#if UVE_MOBILE
uniform float uUv1TriplanarSharpness;
#else
uniform float uUv1TriplanarSharpness = 1.0;
#endif
#if UVE_MOBILE
uniform int uUv2Triplanar;
#else
uniform int uUv2Triplanar = 0;
#endif
#if UVE_MOBILE
uniform float uUv2TriplanarSharpness;
#else
uniform float uUv2TriplanarSharpness = 1.0;
#endif
#if UVE_MOBILE
uniform int uAoOnUv2;
#else
uniform int uAoOnUv2 = 0;
#endif
#if UVE_MOBILE
uniform int uEmissionOnUv2;
#else
uniform int uEmissionOnUv2 = 0;
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
// Set to 1 on opaque non-blended draws so FragColor.a carries the surface's indirect-lighting
// luminance fraction for ambient-only SSAO modulation in the final composite pass.
#if UVE_MOBILE
uniform int uPackIndirectWeightAlpha;
#else
uniform int uPackIndirectWeightAlpha = 0;
#endif
// Legacy Increment 27 pair retained for project-authored shaders and direct single-map tests.
uniform sampler2D uShadowMapTexture;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uLightSpaceMatrices[3];
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
uniform int uDisableReceiveShadows;
uniform int uReflectionProbeEnabled;
uniform vec3 uReflectionProbePosition;
uniform vec3 uReflectionProbeAxisX;
uniform vec3 uReflectionProbeAxisY;
uniform vec3 uReflectionProbeAxisZ;
uniform vec3 uReflectionProbeHalfExtents;
uniform samplerCube uReflectionProbeCube;
uniform samplerCube uReflectionProbeSecondaryCube;
uniform float uReflectionProbeBlendWeight;
uniform vec3 uGiIndirectIrradiance;

// Unified 2D shadow atlas for spot and point lights, sampled from texture slot 15 - the LAST unit
// inside GLES3's guaranteed 16-unit floor, chosen so it cannot shadow one of the eleven dense
// material/cascade/probe units below it. The renderer binds it there through
// kShadowAtlasTextureSlot; the two names are one contract.
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
// depth at the receiver - see AtlasShadowDepthBias.
uniform vec2 uLightShadowDepthRange[4];
// 1 / atlas resolution, so the PCF tap spacing has one source of truth. Declared with a default so
// a direct RHI caller that never sets it keeps the spacing this shader has always assumed.
#if UVE_MOBILE
uniform vec2 uShadowAtlasTexelSize;
#else
uniform vec2 uShadowAtlasTexelSize = vec2(1.0 / 4096.0);
#endif
const float kPi = 3.14159265359;
const float kBrdfEpsilon = 0.0001;
/// The spot cone's bright inner region as a fraction of its authored outer half-angle; the band
/// between the two is where the light falls off. Derived rather than authored so a material's
/// existing single spotAngleDegrees keeps meaning exactly what it meant.
const float kSpotInnerConeRatio = 0.85;

vec3 SafeNormalize(vec3 value) {
    return value / max(length(value), kBrdfEpsilon);
}

vec3 HemisphereAmbient(vec3 normal) {
    if (dot(uSkyAmbient, uSkyAmbient) + dot(uGroundAmbient, uGroundAmbient) < 1.0e-10) {
        return uAmbientColor;
    }
    float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyLum = vec3(dot(uSkyAmbient, vec3(0.2126, 0.7152, 0.0722)));
    vec3 groundLum = vec3(dot(uGroundAmbient, vec3(0.2126, 0.7152, 0.0722)));
    vec3 balancedSky = mix(skyLum, uSkyAmbient, 0.68);
    vec3 balancedGround = mix(groundLum, uGroundAmbient, 0.72);
    vec3 horizonFill = mix(balancedGround, balancedSky, 0.52);
    float horizonWeight = 1.0 - abs(normal.y);
    vec3 verticalBlend = mix(balancedGround, balancedSky, smoothstep(0.0, 1.0, hemi));
    return mix(verticalBlend, horizonFill, horizonWeight * 0.25);
}

vec2 AmbientEnvironmentUv(vec3 direction) {
    vec3 dir = SafeNormalize(direction);
    float longitude = atan(dir.z, dir.x);
    float latitude = asin(clamp(dir.y, -1.0, 1.0));
    return vec2(longitude * (1.0 / (2.0 * kPi)) + 0.5, latitude * (1.0 / kPi) + 0.5);
}

vec3 SampleAmbientEnvironment(vec3 direction) {
    return max(texture(uAmbientEnvironmentMap, AmbientEnvironmentUv(direction)).rgb, vec3(0.0));
}

vec3 SampleEnvironmentIrradiance(vec3 normal) {
    vec3 n = SafeNormalize(normal);
    vec3 referenceAxis = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalize(cross(referenceAxis, n));
    vec3 bitangent = cross(n, tangent);
    const float spread = 0.65;
    return SampleAmbientEnvironment(n) * 0.4 +
           (SampleAmbientEnvironment(SafeNormalize(n + tangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(n - tangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(n + bitangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(n - bitangent * spread))) * 0.15;
}

vec3 SampleEnvironmentSpecular(vec3 direction, float roughness) {
    vec3 reflection = SafeNormalize(direction);
    vec3 referenceAxis = abs(reflection.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalize(cross(referenceAxis, reflection));
    vec3 bitangent = cross(reflection, tangent);
    float spread = clamp(roughness, 0.0, 1.0) * 0.65;
    return SampleAmbientEnvironment(reflection) * 0.4 +
           (SampleAmbientEnvironment(SafeNormalize(reflection + tangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(reflection - tangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(reflection + bitangent * spread)) +
            SampleAmbientEnvironment(SafeNormalize(reflection - bitangent * spread))) * 0.15;
}

vec3 SkyReflection(vec3 direction, float roughness) {
    float up = clamp(direction.y, -1.0, 1.0);
    vec3 sharp = up >= 0.0
        ? mix(uSkyReflectionHorizon, uSkyReflectionSky, smoothstep(0.0, 0.32, up))
        : mix(uSkyReflectionHorizon, uSkyReflectionGround, smoothstep(0.0, 0.25, -up));
    float blur = roughness * roughness;
    float hemi = clamp(up * 0.5 + 0.5, 0.0, 1.0);
    vec3 softUpper = mix(uSkyReflectionHorizon, uSkyReflectionSky, 0.55);
    vec3 softLower = mix(uSkyReflectionGround, uSkyReflectionHorizon, 0.55);
    vec3 soft = mix(softLower, softUpper, hemi);
    return mix(sharp, soft, blur);
}

vec3 AmbientFromSource(vec3 normal) {
    if (uAmbientSource == 0) {
        return vec3(0.0);
    }
    if (uAmbientSource == 1) {
        return max(uAmbientColor, vec3(0.0));
    }
    if (uAmbientSource == 3 && uAmbientEnvironmentMapEnabled != 0) {
        return max(uAmbientColor, vec3(0.0)) * SampleEnvironmentIrradiance(normal);
    }
    // Sky is the compatibility path and the fallback while an Environment Map asset is loading
    // or missing.
    return HemisphereAmbient(normal);
}

// Roughness-aware cubemap probe sampling: mirror surfaces sample the exact reflection vector,
// while rough surfaces integrate a 5-tap orthonormal cone across the cubemap.
vec3 SampleReflectionProbe(vec3 direction, float roughness) {
    vec3 dir = SafeNormalize(direction);
    if (roughness <= 0.08) {
        return texture(uReflectionProbeCube, dir).rgb;
    }
    vec3 referenceAxis = abs(dir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalize(cross(referenceAxis, dir));
    vec3 bitangent = cross(dir, tangent);
    float spread = roughness * roughness * 0.45;
    return texture(uReflectionProbeCube, dir).rgb * 0.4 +
           (texture(uReflectionProbeCube, dir + tangent * spread).rgb +
            texture(uReflectionProbeCube, dir - tangent * spread).rgb +
            texture(uReflectionProbeCube, dir + bitangent * spread).rgb +
            texture(uReflectionProbeCube, dir - bitangent * spread).rgb) * 0.15;
}

float ReflectionProbeInfluence(vec3 worldPoint) {
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
    return 1.0 - smoothstep(0.0, 1.0, normalizedDistance);
}

vec3 ReflectionProbeBoxProject(vec3 worldOrigin, vec3 worldDirection) {
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
        if (abs(dir) < kBrdfEpsilon) {
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
    if (!(tFar > kBrdfEpsilon)) {
        return worldDirection;
    }
    vec3 localHit = localOrigin + localDirection * tFar;
    vec3 worldHit = uReflectionProbePosition + uReflectionProbeAxisX * localHit.x +
                    uReflectionProbeAxisY * localHit.y + uReflectionProbeAxisZ * localHit.z;
    return worldHit - uReflectionProbePosition;
}

float Pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float DistributionGgx(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPi * denominator * denominator, kBrdfEpsilon);
}

// Height-correlated Smith-GGX visibility term V = G / (4 * NdotV * NdotL).
float VisibilitySmithGGXCorrelated(float normalDotView, float normalDotLight, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ggxV = normalDotLight * sqrt(normalDotView * normalDotView * (1.0 - alpha2) + alpha2);
    float ggxL = normalDotView * sqrt(normalDotLight * normalDotLight * (1.0 - alpha2) + alpha2);
    return 0.5 / max(ggxV + ggxL, kBrdfEpsilon);
}

vec3 FresnelSchlick(float halfDotView, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * Pow5(clamp(1.0 - halfDotView, 0.0, 1.0));
}

// Analytic split-sum GGX environment BRDF approximation (scale, bias).
vec2 EnvBrdfApprox(float roughness, float normalDotView) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * normalDotView)) * r.x + r.y;
    return clamp(vec2(-1.04, 1.04) * a004 + r.zw, vec2(0.0), vec2(1.0));
}

// Screen-space microfacet normal-variance filtering to suppress sub-pixel specular aliasing on
// curved geometry, thin bevels, and high-frequency normal maps.
float ApplyGeometricSpecularAA(vec3 geometricNormal, vec3 shadingNormal, float perceptualRoughness) {
    vec3 dGeoU = dFdx(geometricNormal);
    vec3 dGeoV = dFdy(geometricNormal);
    vec3 dShadU = dFdx(shadingNormal);
    vec3 dShadV = dFdy(shadingNormal);
    float variance = 0.20 * (dot(dGeoU, dGeoU) + dot(dGeoV, dGeoV)) +
                     0.10 * (dot(dShadU, dShadU) + dot(dShadV, dShadV));
    float kernelRoughness2 = min(2.0 * variance, 0.18);
    float r2 = perceptualRoughness * perceptualRoughness;
    return clamp(sqrt(r2 + kernelRoughness2), 0.04, 1.0);
}

float SampleCascadeDepth(int cascadeIndex, vec2 texCoord) {
    if (cascadeIndex == 0) {
        return texture(uShadowMapTextures[0], texCoord).r;
    }
    if (cascadeIndex == 1) {
        return texture(uShadowMapTextures[1], texCoord).r;
    }
    return texture(uShadowMapTextures[2], texCoord).r;
}

vec2 CascadeTexelSize(int cascadeIndex) {
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

ivec2 CascadeMapSize(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return textureSize(uShadowMapTextures[0], 0);
    }
    if (cascadeIndex == 1) {
        return textureSize(uShadowMapTextures[1], 0);
    }
    return textureSize(uShadowMapTextures[2], 0);
}

#if !defined(UVE_COMPATIBILITY)
vec4 CascadeLightSpacePosition(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return vLightSpacePositions[0];
    }
    if (cascadeIndex == 1) {
        return vLightSpacePositions[1];
    }
    return vLightSpacePositions[2];
}
#endif

// 16-tap golden-angle Vogel disc for isotropic, clumping-free soft shadow penumbra integration.
const vec2 kVogelDisk16[16] = vec2[16](
    vec2( 0.176777,  0.000000), vec2(-0.225967,  0.205764), vec2( 0.034729, -0.393506), vec2( 0.281441,  0.372739),
    vec2(-0.511329, -0.095890), vec2( 0.430580, -0.355913), vec2(-0.054116,  0.587895), vec2(-0.382775, -0.477944),
    vec2( 0.677406,  0.118636), vec2(-0.625114,  0.372894), vec2( 0.264935, -0.727892), vec2( 0.140751,  0.798237),
    vec2(-0.800547, -0.252855), vec2( 0.779871, -0.415022), vec2(-0.357475,  0.848231), vec2(-0.427858, -0.875842)
);

float ShadowFactorFromPosition(vec4 lightSpacePosition, vec3 normal, vec3 lightDirection, int cascadeIndex) {
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
                                     : CascadeMapSize(cascadeIndex);
    if (mapSize.x <= 1 || mapSize.y <= 1) {
        return 1.0;
    }

    int filterMode = clamp(uShadowPcfKernelRadius, 0, 2);
    vec2 mapDims = max(vec2(mapSize), vec2(1.0));
    vec2 texelSize = 1.0 / mapDims;
    float currentDepth = projected.z;
    float cosTheta = clamp(dot(normal, lightDirection), 0.0, 1.0);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    // Cascaded draws already apply a slope-scaled vertex normal offset; keep fragment depth bias
    // tight so shadows stay grounded on floors instead of detaching or vanishing near contact.
    float bias = cascadeIndex >= 0
                     ? max(uShadowBias * mix(0.10, 0.35, sinTheta) * max(uShadowNormalBias, 0.25),
                           uShadowBias * 0.08)
                     : max(uShadowBias * mix(0.25, 1.0, sinTheta) * max(uShadowNormalBias, 0.25),
                           uShadowBias * 0.2);
    float compareDepth = currentDepth - bias;
    float visibleWeight = 0.0;
    float totalWeight = 0.0;
#if defined(UVE_COMPATIBILITY)
    float sampledDepth = texture(uShadowMapTexture, projected.xy).r;
    visibleWeight = compareDepth > sampledDepth ? 0.0 : 1.0;
    totalWeight = 1.0;
#else
    if (filterMode == 0) {
        float sampledDepth = cascadeIndex < 0 ? texture(uShadowMapTexture, projected.xy).r
                                              : SampleCascadeDepth(cascadeIndex, projected.xy);
        visibleWeight = compareDepth > sampledDepth ? 0.0 : 1.0;
        totalWeight = 1.0;
    } else {
        float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        float angle = noise * 6.2831853;
        float cosA = cos(angle);
        float sinA = sin(angle);
        mat2 rotation = mat2(cosA, sinA, -sinA, cosA);
        float radius = 2.0;
#if !defined(UVE_MOBILE) && !defined(UVE_MOBILE_PROFILE)
        if (filterMode == 2) {
            mat4 lightMat = cascadeIndex < 0 ? uLightSpaceMatrix : uLightSpaceMatrices[clamp(cascadeIndex, 0, 2)];
            float xyScale = length(vec3(lightMat[0][0], lightMat[1][0], lightMat[2][0]));
            float zScale = length(vec3(lightMat[0][2], lightMat[1][2], lightMat[2][2]));
            float depthToXyRatio = (xyScale > 1.0e-5 && zScale > 1.0e-5) ? (0.5 * xyScale / zScale) : 0.5;
            float blockerSum = 0.0;
            float blockers = 0.0;
            for (int i = 0; i < 16; i += 2) {
                vec2 coord = projected.xy + rotation * kVogelDisk16[i] * texelSize * 6.0;
                float depth = cascadeIndex < 0 ? texture(uShadowMapTexture, coord).r
                                               : SampleCascadeDepth(cascadeIndex, coord);
                if (depth < compareDepth) {
                    blockerSum += depth;
                    blockers += 1.0;
                }
            }
            if (blockers > 0.0) {
                float blocker = blockerSum / blockers;
                radius = clamp(1.5 + (currentDepth - blocker) * depthToXyRatio * 1800.0, 1.5, 8.0);
            } else {
                radius = 1.5;
            }
        }
#endif
#if defined(UVE_MOBILE) || defined(UVE_MOBILE_PROFILE)
        int tapStep = filterMode >= 2 ? 2 : 4;
#else
        int tapStep = 1;
#endif
        for (int i = 0; i < 16; i += tapStep) {
            vec2 offset = kVogelDisk16[i];
            vec2 sampleCoord = projected.xy + rotation * offset * texelSize * radius;
            float sampledDepth = cascadeIndex < 0 ? texture(uShadowMapTexture, sampleCoord).r
                                                  : SampleCascadeDepth(cascadeIndex, sampleCoord);
            float tapW = 1.0 - 0.25 * dot(offset, offset);
            visibleWeight += (compareDepth > sampledDepth ? 0.0 : 1.0) * tapW;
            totalWeight += tapW;
        }
    }
#endif

    float coverage = visibleWeight / max(totalWeight, 1.0e-5);
    float factor = mix(1.0, coverage, clamp(uShadowOpacity, 0.0, 1.0));
    if (isnan(factor) || isinf(factor)) {
        return 1.0;
    }
    return factor;
}

float FadeShadowToLitAtDistance(float shadowFactor, float viewDepth, float finalCascadeFarDepth) {
    float fadeRange = min(max(uShadowMaxDistanceFadeRange, 0.0), max(finalCascadeFarDepth, 0.0));
    if (fadeRange <= 0.0) {
        return shadowFactor;
    }
    float fadeStartDepth = max(finalCascadeFarDepth - fadeRange, 0.0);
    float shadowWeight = 1.0 - smoothstep(fadeStartDepth, finalCascadeFarDepth, viewDepth);
    return mix(1.0, shadowFactor, shadowWeight);
}

float DirectionalShadowFactor(vec3 normal, vec3 lightDirection) {
#if defined(UVE_COMPATIBILITY)
    float shadowFactor = ShadowFactorFromPosition(vLightSpacePosition, normal, lightDirection, -1);
    if (uShadowCascadeCount > 0) {
        float planarDepth = dot(vWorldPosition - uViewPosition, SafeNormalize(uCameraForward));
        float viewDepth = (uOrthographic != 0 || planarDepth > 0.0)
                              ? max(planarDepth, 0.0)
                              : length(vWorldPosition - uViewPosition);
        float farDepth = uShadowCascadeSplits[0];
        if (viewDepth > farDepth) {
            return 1.0;
        }
        return FadeShadowToLitAtDistance(shadowFactor, viewDepth, farDepth);
    }
    return shadowFactor;
#else
    if (uShadowCascadeCount <= 0) {
        // No cascades: one map in uShadowMapTexture, projected by vLightSpacePosition.
        return ShadowFactorFromPosition(vLightSpacePosition, normal, lightDirection, -1);
    }

    float planarDepth = dot(vWorldPosition - uViewPosition, SafeNormalize(uCameraForward));
    float viewDepth = (uOrthographic != 0 || planarDepth > 0.0)
                          ? max(planarDepth, 0.0)
                          : length(vWorldPosition - uViewPosition);
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
    float shadowFactor = ShadowFactorFromPosition(CascadeLightSpacePosition(cascadeIndex), normal,
                                                      lightDirection, cascadeIndex);
    if (cascadeIndex < finalCascadeIndex) {
        float cascadeNearDepth = cascadeIndex == 0 ? 0.0 : uShadowCascadeSplits[cascadeIndex - 1];
        float cascadeFarDepth = uShadowCascadeSplits[cascadeIndex];
        float cascadeDepthRange = max(cascadeFarDepth - cascadeNearDepth, 0.0001);
        float blendWidth = cascadeDepthRange * clamp(uShadowCascadeBlendRatio, 0.0, 0.25);
        float blendStartDepth = cascadeFarDepth - blendWidth;
        if (blendWidth > 0.0 && viewDepth > blendStartDepth) {
            float nextCascadeShadowFactor = ShadowFactorFromPosition(
                CascadeLightSpacePosition(cascadeIndex + 1), normal, lightDirection, cascadeIndex + 1);
            float blendWeight = smoothstep(blendStartDepth, cascadeFarDepth, viewDepth);
            shadowFactor = mix(shadowFactor, nextCascadeShadowFactor, blendWeight);
        }
    }
    float finalFactor = FadeShadowToLitAtDistance(shadowFactor, viewDepth, finalCascadeFarDepth);
    if (isnan(finalFactor) || isinf(finalFactor)) {
        return 1.0;
    }
    return finalFactor;
#endif
}

// kAtlasShadowBiasMetersPerUnit keeps the atlas on the same authored scale as the cascades: the
// directional default uShadowBias (0.0025) is authored against roughly a 40 m cascade half-extent,
// where it is worth about 0.05 m of separation. Reusing that ratio means one authored bias value
// keeps meaning the same "how much gap", even though the two paths store depth in different spaces.
const float kAtlasShadowBiasMetersPerUnit = 20.0;

/// Converts a world-space shadow bias into normalized depth AT THE RECEIVER'S OWN DEPTH.
///
/// A perspective projection's depth slope varies by orders of magnitude along one ray, so a constant
/// normalized-depth bias is either acne beside the near plane or metres of peter-panning out at the
/// light's range - and the same light is both, depending on where the surface is. Mirror of
/// ShadowDepthSlope in light_shadow_planner_uve.h; the unit tests pin the two together.
float AtlasShadowDepthBias(vec2 depthRange, float projectionDepth, float normalDotLight) {
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
    return normalizedBias * kAtlasShadowBiasMetersPerUnit * depthSlope;
}

/// Spot shadows: one perspective depth tile per light, sampled with the same receiver-plane PCF the
/// directional cascades use.
///
/// Depth convention: `projectionDepth` is the engine's [0, 1] projection depth (Matrix4x4::
/// Perspective is Vulkan-style), and GL's depth attachment stores `(ndc + 1) / 2`, which is the
/// `* 0.5 + 0.5` below - exactly the convention ShadowFactorFromPosition has always used.
float SpotShadowFactor(int lightIndex, vec3 normal, vec3 lightDirection) {
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
    float bias = AtlasShadowDepthBias(uLightShadowDepthRange[lightIndex], projectionDepth,
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
/// Explicit constants rather than a runtime basis build: `PointLightShadowFaceTable` in
/// shadow_atlas_manager_uve.h holds the same six triples, and the renderer rasterizes each face
/// through exactly the one this shader samples with. A seam that lands on the wrong face reads as a
/// plausible shadow instead of as a bug, so the two tables are pinned together by tests.
void PointShadowFaceBasis(int faceIndex, out vec3 faceRight, out vec3 faceUp,
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
/// `ShadowAtlasManager::PointLightShadowFaceForDirection` implements.
int PointShadowFace(vec3 direction) {
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
/// Mirror of `PointShadowProjectionDepth` in light_shadow_planner_uve.h, which the CPU uses to
/// predict the same number with the light's own near/far. Both are the inverse of the
/// `AtlasShadowDepthBias` linearization.
float PointShadowProjectionDepth(vec2 depthRange, float axisDistance) {
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
float PointShadowFactor(int lightIndex, vec3 normal, vec3 lightPosition, float lightRange) {
    if (uLightShadowEnabled[lightIndex] == 0) {
        return 1.0;
    }
    vec3 toFrag = vWorldPosition - lightPosition;
    float dist = length(toFrag);
    if (dist <= 0.0001 || dist > lightRange) {
        return 1.0;
    }
    vec3 dir = toFrag / dist;
    int face = PointShadowFace(dir);
    vec3 faceRight;
    vec3 faceUp;
    vec3 faceForward;
    PointShadowFaceBasis(face, faceRight, faceUp, faceForward);

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
    float projectionDepth = PointShadowProjectionDepth(depthRange, axisDistance);
    if (projectionDepth <= 0.0 || projectionDepth >= 1.0) {
        return 1.0;
    }
    float currentDepth = projectionDepth * 0.5 + 0.5;
    float bias = AtlasShadowDepthBias(depthRange, projectionDepth, dot(normal, -dir));

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
float Luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}
vec4 ComposeFragment(vec3 color, float alpha) {
    return vec4(uPremultiplyAlpha != 0 ? color * alpha : color, alpha);
}
vec4 ComposeOutput(vec3 color, float surfaceOpacity, float indirectWeight) {
    if (uPackIndirectWeightAlpha != 0) {
        return vec4(color, clamp(indirectWeight, 0.0, 1.0));
    }
    return ComposeFragment(color, surfaceOpacity);
}
#if defined(UVE_COMPATIBILITY)
vec4 ReadMapCompat(sampler2D map, vec2 uv1, vec2 uv2, vec4 transform, vec4 control, vec4 neutral) {
    if (control.z >= 0.999) {
        return neutral;
    }
    vec2 uv = (control.w > 0.5 ? uv2 : uv1) * (vec2(1.0) + transform.xy) + transform.zw;
    vec4 value = texture(map, uv);
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
void main() {
    vec2 uv1 = vTexCoord * uUvScale.xy + uUvOffset.xy;
    vec2 uv2 = vTexCoord * uUv2Scale.xy + uUv2Offset.xy;
    vec4 albedoSample = ReadMapCompat(uAlbedoTexture, uv1, uv2, uAlbedoMapTransform, uAlbedoMapControl, vec4(1.0));
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    vec3 albedo = albedoSample.rgb * uAlbedoColor;
    float materialAlpha = uAlphaSource == 0 ? clamp(albedoSample.a * uAlbedoAlpha, 0.0, 1.0) : 1.0;
    float surfaceOpacity = clamp(uSurfaceOpacity, 0.0, 1.0) * materialAlpha;
    if (uShadingMode == 1 || uUnshaded != 0) {
        FragColor = ComposeOutput(albedo, surfaceOpacity, 0.0);
        return;
    }
    if (uShadingMode == 2) {
        FragColor = ComposeOutput(mix(albedo * 0.25, vec3(0.05, 0.85, 1.0), 0.75), surfaceOpacity, 0.0);
        return;
    }
    float ambientOcclusion = mix(1.0, ReadMapCompat(uAOTexture, uv1, uv2, uOcclusionMapTransform, uOcclusionMapControl, vec4(1.0)).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 normal = SafeNormalize(vWorldNormal);
    if (dot(normal, normal) <= kBrdfEpsilon * kBrdfEpsilon) {
        normal = vec3(0.0, 0.0, 1.0);
    }
    if (uNormalMapControl.z < 0.999) {
        vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
        if (dot(tangent, tangent) <= 0.00000001) {
            vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            tangent = cross(fallbackAxis, normal);
        }
        tangent = SafeNormalize(tangent);
        vec3 bitangent = SafeNormalize(cross(normal, tangent)) * (vTangentHandedness < 0.0 ? -1.0 : 1.0);
        vec3 tsNormal = ReadMapCompat(uNormalTexture, uv1, uv2, uNormalMapTransform, uNormalMapControl, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
        if (uNormalFlipGreen != 0) {
            tsNormal.y = -tsNormal.y;
        }
        tsNormal.xy *= uNormalScale;
        normal = SafeNormalize(mat3(tangent, bitangent, normal) * tsNormal);
    }
    if (uShadingMode == 4) {
        FragColor = ComposeOutput(normal * 0.5 + 0.5, surfaceOpacity, 0.0);
        return;
    }
    vec3 viewDirection = uOrthographic != 0
                             ? SafeNormalize(-uCameraForward)
                             : SafeNormalize(uViewPosition - vWorldPosition);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    if (uShadingMode == 3) {
        albedo = vec3(0.82);
    }
    vec3 mrSample = ReadMapCompat(uMetallicRoughnessTexture, uv1, uv2, uMetallicRoughnessMapTransform, uMetallicRoughnessMapControl, vec4(1.0)).rgb;
    float metallic = uShadingMode == 3 ? 0.0 : clamp(uMetallic * mrSample.b, 0.0, 1.0);
    float roughness = uShadingMode == 3 ? 0.5 : clamp(uRoughness * mrSample.g, 0.06, 1.0);
    float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    float dielectricF0 = 0.16 * dielectricSpec * dielectricSpec;
    vec3 baseReflectance = mix(vec3(dielectricF0), albedo, metallic);
    float normalDotView = max(dot(normal, viewDirection), 0.001);
    vec2 envBrdf = EnvBrdfApprox(roughness, normalDotView);
    vec3 ambientColor = AmbientFromSource(normal);
    vec3 specEnv = uSkyReflectionsLinked != 0 ? SkyReflection(reflect(-viewDirection, normal), roughness) : ambientColor;
    vec3 indirectLighting = ((vec3(1.0) - baseReflectance) * (1.0 - metallic) * albedo * (ambientColor + max(uGiIndirectIrradiance, vec3(0.0))) +
                             (baseReflectance * envBrdf.x + vec3(envBrdf.y)) * specEnv) * ambientOcclusion;
    vec3 emissiveLighting =
        uEmissiveColor * max(uEmissiveEnergy, 0.0) *
        ReadMapCompat(uEmissiveTexture, uv1, uv2, uEmissiveMapTransform, uEmissiveMapControl, vec4(1.0)).rgb;
    vec3 lighting = indirectLighting + emissiveLighting;

    int activeLightCount = uActiveLightCount < 0 ? 0 : (uActiveLightCount > 0 ? min(uActiveLightCount, 4) : 4);
    for (int lightIndex = 0; lightIndex < activeLightCount; ++lightIndex) {
        Light light = uLights[lightIndex];
        if (light.intensity <= 0.0 || (light.cullMask & uMeshRenderLayers) == 0) {
            continue;
        }
        vec3 lightDirection;
        float attenuation = 1.0;
        if (light.type == 0) {
            lightDirection = SafeNormalize(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = max(length(toLight), 0.0001);
            lightDirection = toLight / distanceToLight;
            attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
            if (light.range > 0.0) {
                float r2 = (distanceToLight / light.range);
                r2 *= r2;
                float w = clamp(1.0 - r2 * r2, 0.0, 1.0);
                attenuation *= w * w;
            }
            if (light.type == 2) {
                float cosOuter = cos(radians(light.spotAngleDegrees));
                float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatio);
                float coneAlignment = dot(-lightDirection, SafeNormalize(light.direction));
                float coneFalloff = clamp((coneAlignment - cosOuter) / max(cosInner - cosOuter, kBrdfEpsilon), 0.0, 1.0);
                attenuation *= coneFalloff * coneFalloff;
            }
        }
        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        if (normalDotLight <= 0.0 || attenuation <= 0.0) {
            continue;
        }
        vec3 halfDirection = SafeNormalize(lightDirection + viewDirection);
        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        vec3 fresnel = FresnelSchlick(halfDotView, baseReflectance);
        float distribution = DistributionGgx(normalDotHalf, roughness);
        float visibility = VisibilitySmithGGXCorrelated(normalDotView, normalDotLight, roughness);
        vec3 specular = (distribution * visibility * fresnel) * max(light.specular, 0.0);
        vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / kPi;
        vec3 directContribution = (diffuse + specular) * (light.color * light.intensity * attenuation) * normalDotLight;
        if (uDisableReceiveShadows == 0 && light.type == 0) {
            directContribution *= clamp(DirectionalShadowFactor(normal, lightDirection), 0.0, 1.0);
        }
        lighting += directContribution;
    }
    vec3 finalColor = max(lighting, vec3(0.0));
    float indirectWeight = clamp(Luminance(indirectLighting) / max(Luminance(finalColor), 1.0e-4), 0.0, 1.0);
    FragColor = ComposeOutput(finalColor, surfaceOpacity, indirectWeight);
}
#else
// One UV set: planar 2D coordinates or normal-weighted triplanar projection.
struct UvSet {
    vec2 uv;
    int triplanar;
    vec2 uvX;
    vec2 uvY;
    vec2 uvZ;
    vec3 weights;
    vec3 triNormal;
};
UvSet MakeUvSet(vec3 scale, vec3 offset, int triplanar, float sharpness) {
    UvSet set;
    set.triplanar = triplanar;
    set.uv = vTexCoord * scale.xy + offset.xy;
    set.uvX = set.uv;
    set.uvY = set.uv;
    set.uvZ = set.uv;
    set.weights = vec3(0.0, 1.0, 0.0);
    set.triNormal = vec3(0.0, 1.0, 0.0);
    if (triplanar != 0) {
        vec3 position = triplanar == 2 ? vWorldPosition : vObjectPosition;
        vec3 n = SafeNormalize(triplanar == 2 ? vWorldNormal : vObjectNormal);
        set.triNormal = n;
        vec3 weights = pow(max(abs(n), vec3(1.0e-4)), vec3(max(sharpness, 0.001)));
        set.weights = weights / max(dot(weights, vec3(1.0)), 1.0e-5);
        // Per-plane 2D projection with consistent handedness before applying scale and offset.
        vec2 pX = vec2(n.x < 0.0 ? -position.z : position.z, -position.y);
        vec2 pY = vec2(position.x, n.y < 0.0 ? -position.z : position.z);
        vec2 pZ = vec2(n.z < 0.0 ? -position.x : position.x, -position.y);
        set.uvX = pX * scale.xy + offset.xy;
        set.uvY = pY * scale.xy + offset.xy;
        set.uvZ = pZ * scale.xy + offset.xy;
    }
    return set;
}
vec4 SampleUvSet(sampler2D map, UvSet set) {
    if (set.triplanar == 0) {
        return texture(map, set.uv);
    }
    // Three axis-aligned planar projections blended by surface normal orientation.
    return texture(map, set.uvX) * set.weights.x +
           texture(map, set.uvY) * set.weights.y +
           texture(map, set.uvZ) * set.weights.z;
}
vec3 SampleTriplanarNormal(sampler2D normalMap, UvSet set, float normalScale, int flipGreen) {
    vec3 tX = texture(normalMap, set.uvX).xyz * 2.0 - 1.0;
    vec3 tY = texture(normalMap, set.uvY).xyz * 2.0 - 1.0;
    vec3 tZ = texture(normalMap, set.uvZ).xyz * 2.0 - 1.0;
    if (flipGreen != 0) {
        tX.y = -tX.y;
        tY.y = -tY.y;
        tZ.y = -tZ.y;
    }
    tX.xy *= normalScale;
    tY.xy *= normalScale;
    tZ.xy *= normalScale;
    vec3 n = set.triNormal;
    float sX = n.x < 0.0 ? -1.0 : 1.0;
    float sY = n.y < 0.0 ? -1.0 : 1.0;
    float sZ = n.z < 0.0 ? -1.0 : 1.0;
    // Whiteout triplanar normal blend preserving the geometric normal on flat normal maps.
    vec3 nX = vec3(n.x * tX.z, n.y - tX.y, n.z + tX.x * sX);
    vec3 nY = vec3(n.x + tY.x, n.y * tY.z, n.z + tY.y * sY);
    vec3 nZ = vec3(n.x + tZ.x * sZ, n.y - tZ.y, n.z * tZ.z);
    vec3 blended = nX * set.weights.x + nY * set.weights.y + nZ * set.weights.z;
    if (set.triplanar == 1) {
        blended = mat3(vNormalAxisX, vNormalAxisY, vNormalAxisZ) * blended;
    }
    return SafeNormalize(blended);
}
// One map read its own way: its UV set, tiling and offset, then the channel, invert and strength
// (strength 0 is `neutral`: what the material looks like without the map).
vec4 ReadMap(sampler2D map, UvSet uv1, UvSet uv2, vec4 transform, vec4 control, vec4 neutral) {
    if (control.z >= 0.999) {
        return neutral;
    }
    UvSet set = uv1;
    if (control.w > 0.5) {
        set = uv2;
    }
    vec2 tiling = vec2(1.0) + transform.xy;
    set.uv = set.uv * tiling + transform.zw;
    set.uvX = set.uvX * tiling + transform.zw;
    set.uvY = set.uvY * tiling + transform.zw;
    set.uvZ = set.uvZ * tiling + transform.zw;
    vec4 value = SampleUvSet(map, set);
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
void main() {
    UvSet uv1 = MakeUvSet(vec3(uUvScale.xy, uUvScale.x), uUvOffset, uUv1Triplanar, uUv1TriplanarSharpness);
    UvSet uv2 = MakeUvSet(uUv2Scale, uUv2Offset, uUv2Triplanar, uUv2TriplanarSharpness);
    vec4 albedoSample = ReadMap(uAlbedoTexture, uv1, uv2, uAlbedoMapTransform, uAlbedoMapControl, vec4(1.0));
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
        FragColor = ComposeOutput(albedo, surfaceOpacity, 0.0);
        return;
    }
    if (uShadingMode == 2) {
        FragColor = ComposeOutput(mix(albedo * 0.25, vec3(0.05, 0.85, 1.0), 0.75), surfaceOpacity, 0.0);
        return;
    }
    float ambientOcclusion = mix(1.0, ReadMap(uAOTexture, uv1, uv2, uOcclusionMapTransform, uOcclusionMapControl, vec4(1.0)).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 geometricNormal = SafeNormalize(vWorldNormal);
    // A few legacy/direct RHI callers do not provide a usable normal attribute. Keep those
    // materials lit instead of allowing a zero vector to poison the tangent frame and BRDF.
    if (dot(geometricNormal, geometricNormal) <= kBrdfEpsilon * kBrdfEpsilon) {
        geometricNormal = vec3(0.0, 0.0, 1.0);
    }
    vec3 normal = geometricNormal;
    if (uNormalMapControl.z < 0.999) {
        if (uv1.triplanar != 0) {
            normal = SampleTriplanarNormal(uNormalTexture, uv1, uNormalScale, uNormalFlipGreen);
        } else {
            vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
            if (dot(tangent, tangent) <= 0.00000001) {
                vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                tangent = cross(fallbackAxis, normal);
            }
            tangent = SafeNormalize(tangent);
            vec3 bitangent = SafeNormalize(cross(normal, tangent));
            bitangent *= vTangentHandedness < 0.0 ? -1.0 : 1.0;
            vec3 tangentSpaceNormal = ReadMap(uNormalTexture, uv1, uv2, uNormalMapTransform, uNormalMapControl, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            if (uNormalFlipGreen != 0) {
                tangentSpaceNormal.y = -tangentSpaceNormal.y;
            }
            tangentSpaceNormal.xy *= uNormalScale;
            normal = SafeNormalize(mat3(tangent, bitangent, normal) * tangentSpaceNormal);
        }
    }

    if (uShadingMode == 4) {
        FragColor = ComposeOutput(normal * 0.5 + 0.5, surfaceOpacity, 0.0);
        return;
    }

    vec3 viewDirection = uOrthographic != 0
                             ? SafeNormalize(-uCameraForward)
                             : SafeNormalize(uViewPosition - vWorldPosition);
    // For two-sided geometry or backfaces, orient the normal outwards from the observed face.
    if (!gl_FrontFacing) {
        normal = -normal;
    }

    if (uShadingMode == 3) {
        albedo = vec3(0.82);
    }

    vec3 metallicRoughnessSample = ReadMap(uMetallicRoughnessTexture, uv1, uv2, uMetallicRoughnessMapTransform, uMetallicRoughnessMapControl, vec4(1.0)).rgb;
    float metallic = uShadingMode == 3 ? 0.0 : clamp(uMetallic * metallicRoughnessSample.b, 0.0, 1.0);
    float rawRoughness = uShadingMode == 3 ? 0.5 : clamp(uRoughness * metallicRoughnessSample.g, 0.04, 1.0);
    float roughness = ApplyGeometricSpecularAA(geometricNormal, normal, rawRoughness);
    float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    float dielectricF0 = 0.16 * dielectricSpec * dielectricSpec;
    vec3 ambientBaseReflectance = mix(vec3(dielectricF0), albedo, metallic);
    float normalDotViewAmbient = max(dot(normal, viewDirection), 0.001);
    vec2 envBrdf = EnvBrdfApprox(roughness, normalDotViewAmbient);
    vec3 energyCompensation = vec3(1.0) + ambientBaseReflectance * (1.0 / max(envBrdf.x + envBrdf.y, 0.05) - 1.0);
    vec3 ambientFresnel =
        ambientBaseReflectance +
        (max(vec3(1.0 - roughness), ambientBaseReflectance) - ambientBaseReflectance) *
            Pow5(clamp(1.0 - normalDotViewAmbient, 0.0, 1.0));
    vec3 ambientDiffuseWeight = (vec3(1.0) - ambientFresnel) * (1.0 - metallic);
    vec3 ambientColor = AmbientFromSource(normal);
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 specEnv = ambientColor;
    if (uSkyReflectionsLinked != 0) {
        specEnv = SkyReflection(reflection, roughness);
    }
    if (uAmbientSource == 3 && uAmbientEnvironmentMapEnabled != 0) {
        specEnv = max(uAmbientColor, vec3(0.0)) * SampleEnvironmentSpecular(reflection, roughness);
    }
    if (uReflectionProbeEnabled != 0) {
        float probeWeight = ReflectionProbeInfluence(vWorldPosition);
        if (probeWeight > 0.0) {
            vec3 sampleDir = ReflectionProbeBoxProject(vWorldPosition, reflection);
            vec3 captured = SampleReflectionProbe(sampleDir, roughness);
            float secondaryBlend = clamp(uReflectionProbeBlendWeight, 0.0, 1.0);
            if (secondaryBlend > 0.001) {
                vec3 secondaryCaptured = texture(uReflectionProbeSecondaryCube, sampleDir).rgb;
                captured = mix(captured, secondaryCaptured, secondaryBlend);
            }
            specEnv = mix(specEnv, captured, probeWeight);
        }
    }
    vec3 giDiffuse = max(uGiIndirectIrradiance, vec3(0.0));
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * (ambientColor + giDiffuse);
    vec3 ambientSpecular = (ambientBaseReflectance * envBrdf.x + vec3(envBrdf.y)) * specEnv * energyCompensation;
    vec3 indirectLighting = (ambientDiffuse + ambientSpecular) * ambientOcclusion;
    vec3 emissiveLighting =
        uEmissiveColor * max(uEmissiveEnergy, 0.0) *
        ReadMap(uEmissiveTexture, uv1, uv2, uEmissiveMapTransform, uEmissiveMapControl, vec4(1.0)).rgb;
    vec3 lighting = indirectLighting + emissiveLighting;

    vec3 baseReflectance = mix(vec3(dielectricF0), albedo, metallic);
    float normalDotView = max(dot(normal, viewDirection), 0.001);

#if !defined(UVE_MOBILE) && !defined(UVE_MOBILE_PROFILE)
    if (uClusteredLightingEnabled != 0) {
        int gridX = max(int(uClusterGridDims.x + 0.5), 1);
        int gridY = max(int(uClusterGridDims.y + 0.5), 1);
        int gridZ = max(int(uClusterGridDims.z + 0.5), 1);
        vec2 screenUv = clamp(gl_FragCoord.xy / max(uClusterViewportSize, vec2(1.0)), vec2(0.0), vec2(0.999999));
        int cx = clamp(int(screenUv.x * float(gridX)), 0, gridX - 1);
        int cy = clamp(int(screenUv.y * float(gridY)), 0, gridY - 1);
        float positiveViewDepth = max(dot(vWorldPosition - uViewPosition, SafeNormalize(uCameraForward)),
                                      max(uClusterNearFar.x, 0.001));
        float safeNear = max(uClusterNearFar.x, 0.001);
        float safeFar = max(uClusterNearFar.y, safeNear + 0.01);
        float sliceRatio = log(clamp(positiveViewDepth, safeNear, safeFar) / safeNear) / log(safeFar / safeNear);
        int cz = clamp(int(sliceRatio * float(gridZ)), 0, gridZ - 1);
        int clusterIndex = (cz * gridY + cy) * gridX + cx;
        uvec2 cell = clusteredCells[clusterIndex];
        uint count = min(cell.y, 64u);
        for (uint ci = 0u; ci < count; ++ci) {
            uint li = clusteredLightIndices[cell.x + ci];
            ClusteredGpuLight gpuLight = clusteredLights[li];
            Light light;
            light.type = int(gpuLight.directionAndType.w + 0.5);
            light.position = gpuLight.positionAndRange.xyz;
            light.direction = gpuLight.directionAndType.xyz;
            light.color = gpuLight.colorAndIntensity.rgb;
            light.intensity = gpuLight.colorAndIntensity.w;
            light.range = gpuLight.positionAndRange.w;
            light.spotAngleDegrees = gpuLight.spotMaskSpecShadow.x;
            light.cullMask = floatBitsToInt(gpuLight.spotMaskSpecShadow.y);
            light.specular = gpuLight.spotMaskSpecShadow.z;
            int encodedShadowSlot = int(gpuLight.spotMaskSpecShadow.w + (gpuLight.spotMaskSpecShadow.w >= 0.0 ? 0.5 : -0.5));
            if (light.intensity <= 0.0 || (light.cullMask & uMeshRenderLayers) == 0) {
                continue;
            }
            vec3 lightDirection;
            float attenuation = 1.0;
            if (light.type == 0) {
                lightDirection = SafeNormalize(-light.direction);
            } else {
                vec3 toLight = light.position - vWorldPosition;
                float distanceToLight = max(length(toLight), 0.0001);
                lightDirection = toLight / distanceToLight;
                attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
                if (light.range > 0.0) {
                    float rangeRatio = distanceToLight / light.range;
                    float r2 = rangeRatio * rangeRatio;
                    float window = clamp(1.0 - r2 * r2, 0.0, 1.0);
                    attenuation *= window * window;
                }
                if (light.type == 2) {
                    float cosOuter = cos(radians(light.spotAngleDegrees));
                    float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatio);
                    float coneAlignment = dot(-lightDirection, SafeNormalize(light.direction));
                    float coneFalloff = clamp((coneAlignment - cosOuter) /
                                                  max(cosInner - cosOuter, kBrdfEpsilon),
                                              0.0, 1.0);
                    attenuation *= coneFalloff * coneFalloff;
                }
            }
            float normalDotLight = max(dot(normal, lightDirection), 0.0);
            if (normalDotLight <= 0.0 || attenuation <= 0.0) {
                continue;
            }
            vec3 halfDirection = SafeNormalize(lightDirection + viewDirection);
            float normalDotHalf = max(dot(normal, halfDirection), 0.0);
            float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
            vec3 fresnel = FresnelSchlick(halfDotView, baseReflectance);
            float distribution = DistributionGgx(normalDotHalf, roughness);
            float visibility = VisibilitySmithGGXCorrelated(normalDotView, normalDotLight, roughness);
            vec3 specular = (distribution * visibility * fresnel) * energyCompensation * max(light.specular, 0.0);
            vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
            vec3 diffuse = diffuseWeight * albedo / kPi;
            vec3 radiance = light.color * light.intensity * attenuation;
            vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;
            if (uDisableReceiveShadows == 0) {
                int shadowSlot = encodedShadowSlot > 0 ? (encodedShadowSlot - 1) : -1;
                if (light.type == 0) {
                    float shadowFactor = DirectionalShadowFactor(normal, lightDirection);
                    if (isnan(shadowFactor) || isinf(shadowFactor)) {
                        shadowFactor = 1.0;
                    }
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 1 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = PointShadowFactor(shadowSlot, normal, light.position, light.range);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 2 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = SpotShadowFactor(shadowSlot, normal, lightDirection);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                }
            }
            if (!isnan(directContribution.r) && !isinf(directContribution.r) &&
                !isnan(directContribution.g) && !isinf(directContribution.g) &&
                !isnan(directContribution.b) && !isinf(directContribution.b)) {
                lighting += directContribution;
            }
        }
    } else
#endif
    {
#if defined(UVE_MOBILE_PROFILE)
        int maxProfileLights = 8;
#else
        int maxProfileLights = 16;
#endif
        int activeLightCount = uActiveLightCount < 0 ? 0 : (uActiveLightCount > 0 ? min(uActiveLightCount, maxProfileLights) : maxProfileLights);
        for (int lightIndex = 0; lightIndex < activeLightCount; ++lightIndex) {
            Light light = uLights[lightIndex];
            if (light.intensity <= 0.0) {
                continue;
            }
            if ((light.cullMask & uMeshRenderLayers) == 0) {
                continue;
            }

            vec3 lightDirection;
            float attenuation = 1.0;
            if (light.type == 0) {
                lightDirection = SafeNormalize(-light.direction);
            } else {
                vec3 toLight = light.position - vWorldPosition;
                float distanceToLight = max(length(toLight), 0.0001);
                lightDirection = toLight / distanceToLight;
                attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
                if (light.range > 0.0) {
                    float rangeRatio = distanceToLight / light.range;
                    float r2 = rangeRatio * rangeRatio;
                    float window = clamp(1.0 - r2 * r2, 0.0, 1.0);
                    attenuation *= window * window;
                }
                if (light.type == 2) {
                    float cosOuter = cos(radians(light.spotAngleDegrees));
                    float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatio);
                    float coneAlignment = dot(-lightDirection, SafeNormalize(light.direction));
                    float coneFalloff = clamp((coneAlignment - cosOuter) /
                                                  max(cosInner - cosOuter, kBrdfEpsilon),
                                              0.0, 1.0);
                    attenuation *= coneFalloff * coneFalloff;
                }
            }

            float normalDotLight = max(dot(normal, lightDirection), 0.0);
            if (normalDotLight <= 0.0 || normalDotView <= 0.0 || attenuation <= 0.0) {
                continue;
            }

            vec3 halfDirection = SafeNormalize(lightDirection + viewDirection);
            float normalDotHalf = max(dot(normal, halfDirection), 0.0);
            float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
            vec3 fresnel = FresnelSchlick(halfDotView, baseReflectance);
            float distribution = DistributionGgx(normalDotHalf, roughness);
            float visibility = VisibilitySmithGGXCorrelated(normalDotView, normalDotLight, roughness);
            vec3 specular = (distribution * visibility * fresnel) * energyCompensation * max(light.specular, 0.0);
            vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
            vec3 diffuse = diffuseWeight * albedo / kPi;
            vec3 radiance = light.color * light.intensity * attenuation;
            vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

            if (uDisableReceiveShadows == 0) {
                int encodedShadowSlot = uLightShadowSlot[lightIndex];
                int shadowSlot = encodedShadowSlot > 0 ? (encodedShadowSlot - 1)
                               : (encodedShadowSlot < 0 ? -1 : (lightIndex < 4 ? lightIndex : -1));
                if (light.type == 0) {
                    float shadowFactor = DirectionalShadowFactor(normal, lightDirection);
                    if (isnan(shadowFactor) || isinf(shadowFactor)) {
                        shadowFactor = 1.0;
                    }
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 1 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = PointShadowFactor(shadowSlot, normal, light.position, light.range);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 2 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = SpotShadowFactor(shadowSlot, normal, lightDirection);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                }
            }
            if (!isnan(directContribution.r) && !isinf(directContribution.r) &&
                !isnan(directContribution.g) && !isinf(directContribution.g) &&
                !isnan(directContribution.b) && !isinf(directContribution.b)) {
                lighting += directContribution;
            }
        }
    }

    vec3 finalColor = max(lighting, vec3(0.0));
#ifdef UVE_WBOIT
    float a = clamp(surfaceOpacity, 0.0, 0.9999);
    float weight = clamp(a * max(1.0e-2, 3.0e3 * pow(max(1.0 - gl_FragCoord.z, 0.0), 3.0)), 1.0e-2, 3.0e3);
    FragColor = vec4(finalColor * a * weight, a * weight);
    FragRevealage = vec4(-log(max(1.0 - a, 1.0e-4)), 0.0, 0.0, 0.0);
#else
    float indirectWeight = clamp(Luminance(indirectLighting) / max(Luminance(finalColor), 1.0e-4), 0.0, 1.0);
    FragColor = ComposeOutput(finalColor, surfaceOpacity, indirectWeight);
#endif
}
#endif
#endif
