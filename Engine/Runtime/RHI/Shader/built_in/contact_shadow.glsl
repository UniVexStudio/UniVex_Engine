#version 450 core

// Screen-space contact shadows. Refines shadow maps with a short view-space ray march along the
// primary light direction using unified backend depth decode, 4-neighbor minimum-difference
// normals, and interleaved gradient noise step jitter.

#ifdef VERTEX_SHADER
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
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform int uOrthographic;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uProjA;
uniform float uProjB;
uniform float uInvProjX;
uniform float uInvProjY;
uniform int uFastReconstruct;
uniform vec3 uLightDirectionView;
uniform float uLengthMeters;
uniform float uThicknessMeters;
uniform float uStrength;
uniform int uStepCount;
uniform float uDepthBiasMeters;

float DecodeNdcDepth(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPosition(vec2 uv, float depthSample) {
    float ndcZ = DecodeNdcDepth(depthSample);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 viewPos = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return viewPos.xyz / max(abs(viewPos.w), 1.0e-6);
}

vec3 ViewNormal(vec2 uv, vec3 centerViewPos) {
    vec2 texel = 1.0 / max(vec2(textureSize(uDepthTexture, 0)), vec2(1.0));
    vec2 halfTexel = texel * 0.5;
    vec2 uvL = clamp(uv - vec2(texel.x, 0.0), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvR = clamp(uv + vec2(texel.x, 0.0), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvD = clamp(uv - vec2(0.0, texel.y), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvU = clamp(uv + vec2(0.0, texel.y), halfTexel, vec2(1.0) - halfTexel);

    float dL = texture(uDepthTexture, uvL).r;
    float dR = texture(uDepthTexture, uvR).r;
    float dD = texture(uDepthTexture, uvD).r;
    float dU = texture(uDepthTexture, uvU).r;

    vec3 pL = dL < 0.999999 ? ReconstructViewPosition(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPosition(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPosition(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPosition(uvU, dU) : centerViewPos;

    vec3 dPdx = (dR < 0.999999 && dL < 0.999999)
                    ? (abs(pR.z - centerViewPos.z) < abs(centerViewPos.z - pL.z)
                           ? (pR - centerViewPos)
                           : (centerViewPos - pL))
                    : (dR < 0.999999 ? (pR - centerViewPos)
                                   : (dL < 0.999999 ? (centerViewPos - pL) : vec3(1.0, 0.0, 0.0)));
    vec3 dPdy = (dU < 0.999999 && dD < 0.999999)
                    ? (abs(pU.z - centerViewPos.z) < abs(centerViewPos.z - pD.z)
                           ? (pU - centerViewPos)
                           : (centerViewPos - pD))
                    : (dU < 0.999999 ? (pU - centerViewPos)
                                   : (dD < 0.999999 ? (centerViewPos - pD) : vec3(0.0, 1.0, 0.0)));

    vec3 n = cross(dPdx, dPdy);
    float nLenSq = dot(n, n);
    vec3 viewNormal = nLenSq > 1.0e-12 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    if (isnan(viewNormal.x) || isnan(viewNormal.y) || isnan(viewNormal.z) ||
        isinf(viewNormal.x) || isinf(viewNormal.y) || isinf(viewNormal.z)) {
        viewNormal = vec3(0.0, 0.0, 1.0);
    }
    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    if (dot(viewNormal, towardEye) < 0.0) {
        viewNormal = -viewNormal;
    }
    return viewNormal;
}

float InterleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    vec3 centerViewPos = ReconstructViewPosition(vTexCoord, centerDepth);

    vec3 lightDirection = uLightDirectionView;
    float lightLengthSq = dot(lightDirection, lightDirection);
    if (lightLengthSq < 1.0e-12) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    lightDirection *= inversesqrt(lightLengthSq);

    if (dot(ViewNormal(vTexCoord, centerViewPos), lightDirection) <= 0.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    int stepCount = clamp(uStepCount, 1, 16);
    float inverseStepCount = 1.0 / float(stepCount);
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy);
    int testedSteps = 0;
    float occludedWeight = 0.0;
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
        float fraction = (float(stepIndex) + 0.5 + (jitter - 0.5) * 0.75) * inverseStepCount;
        float distance = uLengthMeters * fraction * fraction;
        vec3 samplePoint = centerViewPos + lightDirection * distance;

        vec4 sampleClip = uProjection * vec4(samplePoint, 1.0);
        if (sampleClip.w <= 1.0e-4) {
            continue;
        }
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        if (isnan(sampleUv.x) || isnan(sampleUv.y) || isinf(sampleUv.x) || isinf(sampleUv.y) ||
            sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        if (sampledDepth >= 0.999999) {
            continue;
        }
        vec3 sampleSceneViewPos = ReconstructViewPosition(sampleUv, sampledDepth);
        float delta = sampleSceneViewPos.z - samplePoint.z;
        ++testedSteps;
        if (delta > uDepthBiasMeters && delta < uThicknessMeters) {
            float fade = 1.0 - smoothstep(0.7, 1.0, fraction);
            occludedWeight += fade;
        }
    }

    float visibility = 1.0;
    if (testedSteps > 0) {
        visibility = 1.0 - clamp(uStrength, 0.0, 1.0) * clamp(occludedWeight / float(testedSteps), 0.0, 1.0);
    }
    FragColor = vec4(visibility, visibility, visibility, 1.0);
}
#endif
