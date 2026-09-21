#version 450 core

// Built-in primitives (PrimitiveMeshComponentUVE: Cube, UVSphere, Plane) carry an authored base
// colour and no material, so they cannot go through the PBR path lit_shadowed_3d.glsl serves -
// there is no albedo/normal/AO texture, no metallic or roughness, and nothing to sample a shadow
// map with. They are still real scene geometry, though, and shading them flat made every primitive
// read as a silhouette with no form.
//
// This is therefore deliberately Lambert-only: the engine's exact light contract (the same
// LightUVE layout, the same type codes, the same range and spot-cone falloff) applied as pure
// diffuse over the world's ambient term. No specular, no shadows, no textures - each of those
// needs material data a primitive does not have, and faking them would be worse than not having
// them.

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
// Transpose(inverse(uModel)): correctly transforms normals under non-uniform scale, which
// primitives routinely have (a Plane node is authored by scaling one axis flat).
uniform mat4 uNormalMatrix;
uniform mat4 uViewProjection;

out vec3 vWorldPosition;
out vec3 vNormal;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vNormal = mat3(uNormalMatrix) * aNormal;
    gl_Position = uViewProjection * worldPosition;
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vNormal;

out vec4 FragColor;

const int kMaxLightsUVE = 4;
const float kEpsilonUVE = 0.0001;
// Matches lit_shadowed_3d.glsl exactly: one authored outer angle, inner cone derived from it.
const float kSpotInnerConeRatioUVE = 0.85;

struct LightUVE {
    int type; // 0 = Directional, 1 = Point, 2 = Spot
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

uniform LightUVE uLights[kMaxLightsUVE];
uniform vec3 uAmbientColor;
uniform vec3 uColor;

vec3 SafeNormalizeUVE(vec3 value) {
    float lengthValue = length(value);
    return lengthValue > kEpsilonUVE ? value / lengthValue : vec3(0.0, 1.0, 0.0);
}

void main() {
    vec3 normal = SafeNormalizeUVE(vNormal);
    vec3 accumulated = uColor * uAmbientColor;

    for (int lightIndex = 0; lightIndex < kMaxLightsUVE; ++lightIndex) {
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
            float distanceToLight = max(length(toLight), kEpsilonUVE);
            lightDirection = toLight / distanceToLight;
            attenuation = 1.0 / max(distanceToLight * distanceToLight, kEpsilonUVE);
            if (light.range > 0.0 && distanceToLight > light.range) {
                attenuation = 0.0;
            }
            if (light.type == 2) {
                float cosOuter = cos(radians(light.spotAngleDegrees));
                float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                float coneFalloff = clamp((coneAlignment - cosOuter) / max(cosInner - cosOuter, kEpsilonUVE),
                                          0.0, 1.0);
                attenuation *= coneFalloff;
            }
        }

        float diffuse = max(dot(normal, lightDirection), 0.0);
        accumulated += uColor * light.color * (light.intensity * attenuation * diffuse);
    }

    FragColor = vec4(accumulated, 1.0);
}
#endif
