#version 450 core

// Screen-space contact shadows. See contact_shadows_uve.h for why this pass exists and what it can
// and cannot see; this file is the GPU half of that contract and the CPU functions there are its
// mirror. The step schedule, the occlusion rule, the depth bias and the visibility mapping below are
// the same arithmetic in both places, which is what lets the CPU tests say something true about what
// the GPU will produce.

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required. Same mapping as
// ssao.glsl - the texture coordinate must use the inverse of gl_Position's mapping.
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTexture;
// Projection and its inverse only, exactly as SSAO: the march stays in view space, so the only
// transform needed is the one that produced the depth buffer and its undo.
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
// Direction from the surface TOWARD the light, in view space, normalized on the CPU. The renderer
// derives it from the same sun direction it hands the sky and the volumetric pass, so the contact
// shadow is always aligned with the light that casts it.
uniform vec3 uLightDirectionView;
uniform float uLengthMeters;
uniform float uThicknessMeters;
uniform float uStrength;
uniform int uStepCount;
uniform float uDepthBiasMeters;

// Recovers view-space position from a raw depth-buffer sample. GL maps clip.z to [0,1]
// (depth = 0.5 * ndc.z + 0.5), and this engine's PerspectiveUVE already produces a [0,1] clip-space
// z, so inverting the GL mapping first recovers the ratio the projection matrix produced; the
// homogeneous divide then gives view-space xyz regardless of that convention. Identical to
// ssao.glsl's ReconstructViewPositionUVE, deliberately: two different reconstructions of the same
// buffer would disagree at the precision limit.
vec3 ReconstructViewPositionUVE(vec2 uv, float depthSample) {
    vec3 ndc = vec3(uv * 2.0 - 1.0, 2.0 * depthSample - 1.0);
    vec4 clipPos = vec4(ndc, 1.0);
    vec4 viewPos = uInverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// View-space normal from screen-space derivatives of the reconstructed position, the same way SSAO
// gets one. There is no normal buffer in this pipeline, and the ray march needs the normal for one
// decision only: whether this surface faces the light at all.
vec3 ViewNormalUVE(vec3 viewPosition) {
    vec3 dx = dFdx(viewPosition);
    vec3 dy = dFdy(viewPosition);
    vec3 n = cross(dx, dy);
    float nLenSq = dot(n, n);
    vec3 viewNormal = nLenSq > 1.0e-8 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    if (isnan(viewNormal.x) || isnan(viewNormal.y) || isnan(viewNormal.z) ||
        isinf(viewNormal.x) || isinf(viewNormal.y) || isinf(viewNormal.z)) {
        viewNormal = vec3(0.0, 0.0, 1.0);
    }
    // Face the camera; the sign of a derivative-derived normal is otherwise arbitrary.
    if (dot(viewNormal, -viewPosition) < 0.0) {
        viewNormal = -viewNormal;
    }
    return viewNormal;
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.9999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // Background: nothing to shadow.
        return;
    }
    vec3 centerViewPos = ReconstructViewPositionUVE(vTexCoord, centerDepth);

    vec3 lightDirection = uLightDirectionView;
    float lightLengthSq = dot(lightDirection, lightDirection);
    if (lightLengthSq < 1.0e-12) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    lightDirection *= inversesqrt(lightLengthSq);

    // A surface facing away from the light receives none of it, so there is no direct light here for
    // a contact shadow to remove. Marching anyway would darken the ambient light that is the only
    // thing lighting this pixel, which is exactly the kind of wrong darkening this pass must not add.
    if (dot(ViewNormalUVE(centerViewPos), lightDirection) <= 0.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Quadratic schedule, matching FillContactShadowRayStepsUVE(): the first sample lands at
    // length / count^2 and the last at exactly length, so the budget is spent where contact lives.
    int stepCount = clamp(uStepCount, 1, 16);
    float inverseStepCount = 1.0 / float(stepCount);
    int testedSteps = 0;
    int occludedSteps = 0;
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
        float fraction = float(stepIndex + 1) * inverseStepCount;
        float distance = uLengthMeters * fraction * fraction;
        vec3 samplePoint = centerViewPos + lightDirection * distance;

        vec4 sampleClip = uProjection * vec4(samplePoint, 1.0);
        if (abs(sampleClip.w) < 1.0e-4) {
            continue;
        }
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        // Off-screen samples are not occluders: the march simply cannot see them. Skipping keeps
        // them out of the tested count, so a ray that leaves the screen is not read as "unoccluded
        // along its whole length".
        if (isnan(sampleUv.x) || isnan(sampleUv.y) || isinf(sampleUv.x) || isinf(sampleUv.y) ||
            sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        if (sampledDepth >= 0.9999) {
            continue; // Background behind the ray: nothing can occlude.
        }
        vec3 sampleSceneViewPos = ReconstructViewPositionUVE(sampleUv, sampledDepth);
        // Both depths are view-space z: nearer the camera is a LARGER value (the camera looks down
        // -Z), so a positive delta means the scene sample stands between the camera and the ray.
        float delta = sampleSceneViewPos.z - samplePoint.z;
        ++testedSteps;
        if (delta > uDepthBiasMeters && delta < uThicknessMeters) {
            ++occludedSteps;
        }
    }

    float visibility = 1.0;
    if (testedSteps > 0) {
        visibility = 1.0 - clamp(uStrength, 0.0, 1.0) * (float(occludedSteps) / float(testedSteps));
    }
    FragColor = vec4(visibility, visibility, visibility, 1.0);
}
#endif
