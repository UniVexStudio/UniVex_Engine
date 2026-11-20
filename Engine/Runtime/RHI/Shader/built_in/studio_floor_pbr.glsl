#version 330 core
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// studio_floor_pbr.glsl - Dark studio cyclorama floor surface shader for preview viewports.
// Evaluates procedural brushed basalt/concrete Albedo, Normal, and ORM maps with height-correlated
// Smith-GGX microfacet lighting, 3-point studio light rig, hemisphere ambient, and smooth radial
// distance dissolution into the dark studio backdrop.

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

uniform mat4 uViewProjection;
#if UVE_MOBILE
uniform float uFloorY;
#else
uniform float uFloorY = 0.0;
#endif

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vWorldTangent;
out vec3 vWorldBitangent;
out vec2 vTexCoord;

void main() {
    vec3 worldPos = vec3(aPosition.x, aPosition.y + uFloorY, aPosition.z);
    vWorldPos = worldPos;
    vWorldNormal = normalize(aNormal);
    vWorldTangent = normalize(aTangent.xyz);
    vWorldBitangent = cross(vWorldNormal, vWorldTangent) * aTangent.w;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * vec4(worldPos, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vWorldTangent;
in vec3 vWorldBitangent;
in vec2 vTexCoord;

uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uOrmMap;

uniform vec3 uViewPosition;
#if UVE_MOBILE
uniform vec3 uPivotPosition;
uniform float uTileScale;
uniform float uFadeStartRadius;
uniform float uFadeEndRadius;
uniform vec3 uBaseTint;
uniform float uRoughnessScale;
uniform float uSpecularStrength;
uniform vec3 uSkyTopColor;
uniform vec3 uSkyHorizonColor;
uniform vec3 uSkyGroundColor;
uniform float uAmbientEnergy;
uniform int uLightCount;
#else
uniform vec3 uPivotPosition = vec3(0.0, 0.0, 0.0);
uniform float uTileScale = 0.5;
uniform float uFadeStartRadius = 10.0;
uniform float uFadeEndRadius = 36.0;
uniform vec3 uBaseTint = vec3(1.0, 1.0, 1.0);
uniform float uRoughnessScale = 1.0;
uniform float uSpecularStrength = 0.55;
uniform vec3 uSkyTopColor = vec3(0.032, 0.035, 0.044);
uniform vec3 uSkyHorizonColor = vec3(0.048, 0.052, 0.064);
uniform vec3 uSkyGroundColor = vec3(0.022, 0.024, 0.030);
uniform float uAmbientEnergy = 0.45;
uniform int uLightCount = 3;
#endif

uniform vec3 uLightPositions[4];
uniform vec3 uLightColors[4];
uniform float uLightRanges[4];

out vec4 FragColor;

const float kPiUVE = 3.14159265359;

vec2 EnvBrdfApproxUVE(float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

float DistributionGGXUVE(float NoH, float alpha) {
    float a2 = alpha * alpha;
    float denom = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(kPiUVE * denom * denom, 1.0e-7);
}

float VisibilitySmithGGXCorrelatedUVE(float NoV, float NoL, float alpha) {
    float a2 = alpha * alpha;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1.0e-5);
}

void main() {
    float radialDist = length(vWorldPos.xz - uPivotPosition.xz);
    float radialAlpha = 1.0 - smoothstep(uFadeStartRadius, max(uFadeStartRadius + 0.5, uFadeEndRadius), radialDist);
    if (radialAlpha < 0.005) {
        discard;
    }

    vec2 uv = vWorldPos.xz * max(uTileScale, 0.01);
    vec3 albedoSample = texture(uAlbedoMap, uv).rgb * uBaseTint;
    vec3 normalSample = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    vec3 ormSample = texture(uOrmMap, uv).rgb;

    float ao = clamp(ormSample.r, 0.0, 1.0);
    float roughness = clamp(ormSample.g * uRoughnessScale, 0.05, 1.0);
    float metallic = clamp(ormSample.b, 0.0, 1.0);

    vec3 Ngeom = normalize(vWorldNormal);
    vec3 T = normalize(vWorldTangent - Ngeom * dot(vWorldTangent, Ngeom));
    vec3 B = normalize(cross(Ngeom, T));
    mat3 tbn = mat3(T, B, Ngeom);
    vec3 N = normalize(tbn * normalize(vec3(normalSample.xy * 0.75, max(normalSample.z, 0.25))));

    // Geometric specular anti-aliasing from screen-space normal derivatives.
    vec3 dndu = dFdx(N);
    vec3 dndv = dFdy(N);
    float variance = 0.25 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernelRoughness2 = min(2.0 * variance, 0.18);
    roughness = clamp(sqrt(roughness * roughness + kernelRoughness2), 0.05, 1.0);
    float alpha = roughness * roughness;

    vec3 V = normalize(uViewPosition - vWorldPos);
    float NoV = max(dot(N, V), 1.0e-4);

    vec3 f0 = mix(vec3(0.04 * clamp(uSpecularStrength / 0.5, 0.0, 2.0)), albedoSample, metallic);
    vec2 envBrdf = EnvBrdfApproxUVE(roughness, NoV);
    vec3 envSpecularColor = f0 * envBrdf.x + envBrdf.y;
    vec3 energyCompensation = 1.0 + f0 * (1.0 / max(envBrdf.x + envBrdf.y, 1.0e-4) - 1.0);

    // Subtle soft pool of studio light centered beneath the elevated emitter pivot.
    float centerPool = exp(-0.045 * radialDist * radialDist);
    float hemiUp = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientSky = mix(uSkyHorizonColor, uSkyTopColor, hemiUp);
    vec3 ambient = mix(uSkyGroundColor, ambientSky, hemiUp) * max(uAmbientEnergy, 0.0) * (0.75 + 0.45 * centerPool);

    vec3 R = reflect(-V, N);
    float rUp = clamp(R.y, 0.0, 1.0);
    vec3 studioRefl = mix(uSkyHorizonColor, uSkyTopColor, pow(rUp, mix(0.65, 1.5, 1.0 - roughness)));
    studioRefl *= (0.65 + 0.55 * centerPool) * max(uAmbientEnergy, 0.1);

    float specAo = clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
    vec3 diffuseLit = albedoSample * (1.0 - metallic) * ambient * ao;
    vec3 specularLit = studioRefl * envSpecularColor * specAo;

    int activeLights = clamp(uLightCount, 0, 4);
    for (int i = 0; i < activeLights; ++i) {
        vec3 toLight = uLightPositions[i] - vWorldPos;
        float distSq = dot(toLight, toLight);
        float dist = sqrt(max(distSq, 1.0e-6));
        vec3 L = toLight / dist;
        float NoL = max(dot(N, L), 0.0);
        if (NoL <= 0.0) {
            continue;
        }
        float range = max(uLightRanges[i], 0.5);
        float distRatio = dist / range;
        float distRatio2 = distRatio * distRatio;
        float window = clamp(1.0 - distRatio2 * distRatio2, 0.0, 1.0);
        float attenuation = (window * window) / (1.0 + 0.12 * distSq);

        vec3 H = normalize(V + L);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        float v1 = 1.0 - VoH;
        float v2 = v1 * v1;
        vec3 F = f0 + (1.0 - f0) * (v2 * v2 * v1);
        float D = DistributionGGXUVE(NoH, alpha);
        float Vis = VisibilitySmithGGXCorrelatedUVE(NoV, NoL, alpha);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuseTerm = kD * albedoSample / kPiUVE;
        vec3 specularTerm = (D * Vis) * F * energyCompensation;

        vec3 radiance = uLightColors[i] * attenuation * NoL;
        diffuseLit += diffuseTerm * radiance * ao;
        specularLit += specularTerm * radiance * specAo;
    }

    vec3 color = diffuseLit + specularLit;
    FragColor = vec4(max(color, vec3(0.0)), radialAlpha);
}
#endif
