#version 430 core

// Compute ray-traced shadows, 1-bounce specular reflections, and 1-bounce diffuse GI over the
// scene AABB proxy buffer (RtSceneBoxNode). Works universally on desktop OpenGL 4.3+ and
// Vulkan compute queues (including software lavapipe in CI).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct RtSceneBoxNode {
    vec4 minAndMetallic;
    vec4 maxAndRoughness;
    vec4 albedo;
    vec4 emissive;
};

layout(std430, binding = 0) readonly buffer RtSceneBoxesBlock {
    RtSceneBoxNode sceneBoxes[];
};

layout(std430, binding = 1) readonly buffer RtParamsBlock {
    mat4 uInverseViewProjection;
    vec4 uCameraPositionAndBoxCount;
    vec4 uSunDirectionAndNdcScale;
    vec4 uSunRadianceAndNdcBias;
    vec4 uSkyColorAndWidthHeight;
};

layout(binding = 0) uniform sampler2D uDepthTexture;
layout(rgba16f, binding = 1) uniform writeonly image2D uRtOutputImage;

bool IntersectBox(vec3 origin, vec3 invDir, RtSceneBoxNode box, float tMin, float tMax,
                     out float outT, out vec3 outNormal) {
    vec3 t0 = (box.minAndMetallic.xyz - origin) * invDir;
    vec3 t1 = (box.maxAndRoughness.xyz - origin) * invDir;
    vec3 tsmall = min(t0, t1);
    vec3 tbig = max(t0, t1);
    float tEnter = max(max(tsmall.x, tsmall.y), max(tsmall.z, tMin));
    float tExit = min(min(tbig.x, tbig.y), min(tbig.z, tMax));
    if (tExit < tEnter || tEnter > tMax) {
        return false;
    }
    outT = tEnter;
    if (tEnter == tsmall.x) {
        outNormal = vec3(invDir.x < 0.0 ? 1.0 : -1.0, 0.0, 0.0);
    } else if (tEnter == tsmall.y) {
        outNormal = vec3(0.0, invDir.y < 0.0 ? 1.0 : -1.0, 0.0);
    } else {
        outNormal = vec3(0.0, 0.0, invDir.z < 0.0 ? 1.0 : -1.0);
    }
    return true;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(uRtOutputImage);
    if (pixel.x >= dims.x || pixel.y >= dims.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / max(vec2(dims), vec2(1.0));
    float depth = texture(uDepthTexture, uv).r;
    if (depth >= 0.999999) {
        imageStore(uRtOutputImage, pixel, vec4(uSkyColorAndWidthHeight.rgb, 1.0));
        return;
    }

    float ndcScale = uSunDirectionAndNdcScale.w != 0.0 ? uSunDirectionAndNdcScale.w : 2.0;
    float ndcBias = uSunDirectionAndNdcScale.w != 0.0 ? uSunRadianceAndNdcBias.w : -1.0;
    float ndcZ = depth * ndcScale + ndcBias;
    vec4 wp = uInverseViewProjection * vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec3 worldPos = wp.xyz / max(abs(wp.w), 1.0e-6);

    uint boxCount = uint(max(uCameraPositionAndBoxCount.w, 0.0));
    vec3 sunDir = normalize(uSunDirectionAndNdcScale.xyz);
    vec3 invSunDir = vec3(
        abs(sunDir.x) > 1.0e-6 ? 1.0 / sunDir.x : 1.0e6,
        abs(sunDir.y) > 1.0e-6 ? 1.0 / sunDir.y : 1.0e6,
        abs(sunDir.z) > 1.0e-6 ? 1.0 / sunDir.z : 1.0e6);

    float sunVis = 1.0;
    float hitT = 0.0;
    vec3 hitN = vec3(0.0, 1.0, 0.0);
    for (uint i = 0u; i < boxCount; ++i) {
        if (IntersectBox(worldPos + sunDir * 0.02, invSunDir, sceneBoxes[i], 0.005, 250.0, hitT, hitN)) {
            sunVis = 0.0;
            break;
        }
    }

    vec3 giColor = uSkyColorAndWidthHeight.rgb * 0.25;
    imageStore(uRtOutputImage, pixel, vec4(giColor, sunVis));
}
