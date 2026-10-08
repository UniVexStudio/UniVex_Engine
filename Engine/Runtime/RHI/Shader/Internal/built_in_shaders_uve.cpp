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
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform int uNearestUpscaling;
uniform int uToneMappingMethod;
uniform int uFastApproximateAA;
uniform int uScreenSpaceAAQuality;
uniform float uSharpeningAmount;
uniform int uDitheringEnabled;
uniform float uFxaaTexelX;
uniform float uFxaaTexelY;
// The scene depth this frame was rendered with, used only to report coverage. A caller that
// renders into its own texture (RenderFrameToTargetUVE) otherwise has no way to tell which
// pixels the renderer actually covered: the destination depth attachment is cleared by this
// pass and never written, and the scene's clear colour is indistinguishable from dark geometry.
// The editor viewport needs exactly that distinction to lay its grid and gizmos over the frame.
uniform sampler2D uSceneDepthTexture;
// 1 while rendering into a caller-supplied texture, 0 for the presentation surface - whose
// alpha must stay opaque, since some window visuals composite it.
uniform int uWriteCoverageAlpha;
uniform int uHumanEye;
uniform float uHumanEyeCenterScale;
uniform float uHumanEyeTexelX;
uniform float uHumanEyeTexelY;
uniform float uExposure;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogStart;
uniform float uFogEnd;
uniform mat4 uInverseProjection;
uniform float uCameraNear;
uniform float uCameraFar;
uniform float uFogSkyAffect;
uniform int uSkyCovers;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uColorFilter;
uniform float uVignetteIntensity;
uniform float uVignetteRadius;
uniform float uChromaticAberrationIntensity;
uniform float uFilmGrainIntensity;
uniform int uFilmGrainFrame;
uniform float uLensDistortionIntensity;
uniform int uDepthOfFieldEnabled;
uniform int uDepthOfFieldFocusMode;
uniform int uDepthOfFieldBokehShape;
uniform float uDepthOfFieldFocusDistance;
uniform int uMotionBlurEnabled;
uniform float uMotionBlurStrength;
uniform int uMotionBlurSampleCount;
uniform mat4 uPreviousViewProjection;
uniform float uDepthOfFieldAperture;
uniform int uDepthOfFieldQuality;
uniform vec3 uCameraPosition;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunEnergy;
uniform float uFogHeight;
uniform float uFogHeightFalloff;
uniform float uFogSunScatter;
uniform float uLightVolumetricFogEnergy;
uniform int uFogVolumeCount;

struct FogVolumeUVE {
    vec3 position;
    vec3 axisX;
    vec3 axisY;
    vec3 axisZ;
    vec3 scale;
    vec3 size;
    vec3 albedo;
    vec3 emission;
    float density;
    float heightFalloff;
    float edgeFade;
    int shape;
};
uniform FogVolumeUVE uFogVolumes[8];

const int kFogModeLinearUVE = 0; // WorldEnvironmentFogModeUVE::Linear
const int kFogModeHeightUVE = 2; // Exponential is value 1; Height is value 2.
const int kFogVolumeRaySamplesUVE = 12;
const float kFogVolumeScaleEpsilonUVE = 1.0e-6;
const vec2 kDepthOfFieldOffsetsUVE[12] = vec2[12](
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(-1.0, 0.0), vec2(0.0, -1.0),
    vec2(0.7071, 0.7071), vec2(-0.7071, 0.7071), vec2(0.7071, -0.7071), vec2(-0.7071, -0.7071),
    vec2(0.5, 0.8660), vec2(-0.5, 0.8660), vec2(0.5, -0.8660), vec2(-0.5, -0.8660));

vec3 AcesToneMapUVE(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 ApplyToneMappingUVE(vec3 color) {
    return uToneMappingMethod == 0 ? clamp(color, 0.0, 1.0) : AcesToneMapUVE(color);
}

vec2 LensDistortionSourceUVUVE(vec2 uv) {
    float intensity = clamp(uLensDistortionIntensity, 0.0, 1.0);
    if (intensity <= 0.0) {
        return uv;
    }

    float aspect = max(uAspect, 0.0001);
    vec2 aspectVector = vec2(aspect, 1.0);
    vec2 centered = (uv * 2.0 - 1.0) * aspectVector;
    float cornerRadiusSquared = max(dot(aspectVector, aspectVector), 1.0e-6);
    float normalizedRadiusSquared = clamp(dot(centered, centered) / cornerRadiusSquared, 0.0, 1.0);
    centered *= 1.0 - 0.12 * intensity * normalizedRadiusSquared;
    centered.x /= aspect;
    return centered * 0.5 + 0.5;
}

vec2 HumanEyeSourceUVUVE(vec2 uv) {
    uv = LensDistortionSourceUVUVE(uv);
    vec2 ndc = uv * 2.0 - 1.0;
    float k0 = uHumanEyeCenterScale;
    vec2 sampleNdc = ndc * (vec2(k0) + (1.0 - k0) * ndc * ndc);
    return sampleNdc * 0.5 + 0.5;
}

vec3 SampleHdrRawUVE(vec2 uv) {
    if (uNearestUpscaling != 0) {
        ivec2 sourceSize = textureSize(uSourceTexture, 0);
        vec2 clampedUV = clamp(uv, vec2(0.0), vec2(1.0));
        ivec2 sourceTexel = clamp(ivec2(floor(clampedUV * vec2(sourceSize))), ivec2(0), sourceSize - ivec2(1));
        return max(texelFetch(uSourceTexture, sourceTexel, 0).rgb, vec3(0.0));
    }
    return max(texture(uSourceTexture, uv).rgb, vec3(0.0));
}

vec3 SampleHdrUVE(vec2 uv) {
    vec3 center = SampleHdrRawUVE(uv);
    float intensity = clamp(uChromaticAberrationIntensity, 0.0, 1.0);
    if (intensity <= 0.0) {
        return center;
    }

    float aspect = max(uAspect, 0.0001);
    vec2 radial = (vTexCoord * 2.0 - 1.0) * vec2(aspect, 1.0);
    float radialLength = length(radial);
    float cornerLength = length(vec2(aspect, 1.0));
    if (radialLength <= 1.0e-5) {
        return center;
    }

    float normalizedRadius = clamp(radialLength / max(cornerLength, 0.0001), 0.0, 1.0);
    vec2 direction = radial / radialLength;
    vec2 texelSize = 1.0 / vec2(textureSize(uSourceTexture, 0));
    vec2 offset = direction * texelSize * (2.0 * intensity * normalizedRadius * normalizedRadius);
    float red = SampleHdrRawUVE(clamp(uv + offset, vec2(0.0), vec2(1.0))).r;
    float blue = SampleHdrRawUVE(clamp(uv - offset, vec2(0.0), vec2(1.0))).b;
    return vec3(red, center.g, blue);
}

/// Edge-directed, bounded post-process filtering applied before tone mapping. The contrast gate
/// avoids blurring flat regions; quality selects one, two, or three taps on either side of the
/// detected edge. The render-target texel size keeps the filter resolution-independent.
vec3 ApplyFastApproximateAAUVE(vec2 uv) {
    vec3 center = SampleHdrUVE(uv);
    if (uFastApproximateAA == 0 || uFxaaTexelX <= 0.0 || uFxaaTexelY <= 0.0) {
        return center;
    }

    vec2 texel = vec2(uFxaaTexelX, uFxaaTexelY);
    float centerLuma = dot(center, vec3(0.299, 0.587, 0.114));
    vec3 north = SampleHdrUVE(uv + vec2(0.0, texel.y));
    vec3 south = SampleHdrUVE(uv - vec2(0.0, texel.y));
    vec3 east = SampleHdrUVE(uv + vec2(texel.x, 0.0));
    vec3 west = SampleHdrUVE(uv - vec2(texel.x, 0.0));
    float northLuma = dot(north, vec3(0.299, 0.587, 0.114));
    float southLuma = dot(south, vec3(0.299, 0.587, 0.114));
    float eastLuma = dot(east, vec3(0.299, 0.587, 0.114));
    float westLuma = dot(west, vec3(0.299, 0.587, 0.114));
    float lumaMinimum = min(centerLuma, min(min(northLuma, southLuma), min(eastLuma, westLuma)));
    float lumaMaximum = max(centerLuma, max(max(northLuma, southLuma), max(eastLuma, westLuma)));
    float contrast = lumaMaximum - lumaMinimum;
    float threshold = max(0.0312, lumaMaximum * 0.125);
    if (contrast < threshold) {
        return center;
    }

    // Walk along the stronger edge direction, preserving the edge while smoothing its staircase.
    bool verticalEdge = abs(eastLuma - westLuma) > abs(northLuma - southLuma);
    vec2 edgeStep = verticalEdge ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
    vec3 negativeEdge = verticalEdge ? south : west;
    vec3 positiveEdge = verticalEdge ? north : east;
    vec3 weightedColor = center * 2.0 + negativeEdge + positiveEdge;
    float totalWeight = 4.0;
    int quality = clamp(uScreenSpaceAAQuality, 0, 2);
    if (quality >= 1) {
        weightedColor += SampleHdrUVE(uv - edgeStep * 2.0) + SampleHdrUVE(uv + edgeStep * 2.0);
        totalWeight += 2.0;
    }
    if (quality >= 2) {
        weightedColor += SampleHdrUVE(uv - edgeStep * 3.0) + SampleHdrUVE(uv + edgeStep * 3.0);
        totalWeight += 2.0;
    }
    float blend = clamp((contrast - threshold) / max(lumaMaximum, 0.0625), 0.0, 0.5);
    return mix(center, weightedColor / totalWeight, blend);
}

/// Edge-clamped unsharp masking in scene-linear space. Constraining the result to the 3x3
/// neighbourhood avoids bright/dark ringing around high-contrast edges while keeping flat regions
/// exactly unchanged. Zero amount is a strict passthrough.
vec3 ApplySharpeningUVE(vec2 uv, vec3 center) {
    float amount = clamp(uSharpeningAmount, 0.0, 1.0);
    if (amount <= 0.0) {
        return center;
    }

    vec2 texel = 1.0 / vec2(textureSize(uSourceTexture, 0));
    vec2 leftUv = clamp(uv - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0));
    vec2 rightUv = clamp(uv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0));
    vec2 downUv = clamp(uv - vec2(0.0, texel.y), vec2(0.0), vec2(1.0));
    vec2 upUv = clamp(uv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0));
    vec3 left = SampleHdrUVE(leftUv);
    vec3 right = SampleHdrUVE(rightUv);
    vec3 down = SampleHdrUVE(downUv);
    vec3 up = SampleHdrUVE(upUv);
    vec3 neighbourhoodAverage = (left + right + down + up) * 0.25;
    vec3 neighbourhoodMinimum = min(center, min(min(left, right), min(down, up)));
    vec3 neighbourhoodMaximum = max(center, max(max(left, right), max(down, up)));
    vec3 sharpened = center + (center - neighbourhoodAverage) * amount;
    return clamp(sharpened, neighbourhoodMinimum, neighbourhoodMaximum);
}

/// Animated per-pixel monochrome noise for film grain; the renderer advances the frame seed once
/// per submitted frame so the pattern does not crawl with wall-clock timing.
float FilmGrainNoiseUVE(vec2 pixel, int frame) {
    float frameOffset = float(frame) * 37.719;
    return fract(sin(dot(pixel, vec2(12.9898, 78.233)) + frameOffset) * 43758.5453);
}

/// Stable per-pixel noise with a half-LSB amplitude. Dithering is applied after tone mapping and
/// color adjustments, immediately before the target's normalized output format quantizes the value.
float ScreenSpaceDitherNoiseUVE(vec2 pixel) {
    return fract(sin(dot(pixel, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

// Reconstruct view-space position using the exact projection that rendered the depth buffer. The
// engine's OpenGL depth range maps the projection's [0,1] clip Z into the stored [0,1] depth, so
// undo that mapping before multiplying by the inverse projection. Distance is measured along the
// pixel ray, not by linearly interpolating the non-linear perspective depth buffer.
float ReconstructViewDistanceUVE(vec2 uv, float depthSample, float fallbackDistance) {
    vec3 ndc = vec3(uv * 2.0 - 1.0, 2.0 * clamp(depthSample, 0.0, 1.0) - 1.0);
    vec4 viewPosition = uInverseProjection * vec4(ndc, 1.0);
    if (abs(viewPosition.w) <= 1.0e-6) {
        return fallbackDistance;
    }
    float viewDistance = length(viewPosition.xyz / viewPosition.w);
    return (isnan(viewDistance) || isinf(viewDistance)) ? fallbackDistance : max(viewDistance, 0.0);
}

vec2 ApplyBokehShapeOffsetUVE(vec2 offset) {
    if (uDepthOfFieldBokehShape != 1) {
        return offset;
    }

    const float kPiOverThreeUVE = 1.0471975512;
    const float kPiOverSixUVE = 0.5235987756;
    float angle = atan(offset.y, offset.x);
    float sector = floor((angle - kPiOverSixUVE) / kPiOverThreeUVE + 0.5);
    float faceNormal = sector * kPiOverThreeUVE + kPiOverSixUVE;
    float boundaryRadius = 0.8660254038 / max(cos(angle - faceNormal), 1.0e-4);
    return normalize(offset) * boundaryRadius;
}

vec3 ApplyDepthOfFieldUVE(vec2 uv, vec3 center, float centerDepth) {
    float aperture = clamp(uDepthOfFieldAperture, 0.0, 1.0);
    if (uDepthOfFieldEnabled == 0 || aperture <= 0.0) {
        return center;
    }

    float fallbackDistance = max(uCameraFar, uCameraNear);
    float centerDistance = centerDepth < 1.0
                               ? ReconstructViewDistanceUVE(uv, centerDepth, fallbackDistance)
                               : fallbackDistance;
    float focusDistance = max(uDepthOfFieldFocusDistance, max(uCameraNear, 0.05));
    if (uDepthOfFieldFocusMode == 1) {
        vec2 focusUv = vec2(0.5);
        float focusDepth = texture(uSceneDepthTexture, focusUv).r;
        focusDistance = focusDepth < 1.0
                            ? ReconstructViewDistanceUVE(focusUv, focusDepth, fallbackDistance)
                            : fallbackDistance;
    }
    float circleOfConfusion = abs(centerDistance - focusDistance) /
                              max(max(centerDistance, focusDistance), 1.0e-4);
    float blurRadiusPixels = clamp(circleOfConfusion * aperture * 8.0, 0.0, 8.0);
    if (blurRadiusPixels < 0.5) {
        return center;
    }

    int quality = clamp(uDepthOfFieldQuality, 0, 2);
    int sampleCount = (quality + 1) * 4;
    vec2 texelSize = 1.0 / vec2(textureSize(uSourceTexture, 0));
    float depthTolerance = max(centerDistance * 0.05, 0.1);
    vec3 colorSum = center;
    float weightSum = 1.0;
    for (int i = 0; i < 12; ++i) {
        if (i >= sampleCount) {
            break;
        }
        vec2 bokehOffset = ApplyBokehShapeOffsetUVE(kDepthOfFieldOffsetsUVE[i]);
        vec2 sampleUv = clamp(uv + bokehOffset * texelSize * blurRadiusPixels, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(uSceneDepthTexture, sampleUv).r;
        float sampleDistance = sampleDepth < 1.0
                                   ? ReconstructViewDistanceUVE(sampleUv, sampleDepth, fallbackDistance)
                                   : fallbackDistance;
        float depthWeight = exp(-abs(sampleDistance - centerDistance) / depthTolerance);
        colorSum += SampleHdrUVE(sampleUv) * depthWeight;
        weightSum += depthWeight;
    }
    return colorSum / max(weightSum, 1.0e-4);
}

vec3 ApplyCameraMotionBlurUVE(vec2 uv, vec3 center, float depthSample) {
    float strength = clamp(uMotionBlurStrength, 0.0, 1.0);
    if (uMotionBlurEnabled == 0 || strength <= 0.0) {
        return center;
    }

    vec3 ndc = vec3(uv * 2.0 - 1.0, 2.0 * clamp(depthSample, 0.0, 1.0) - 1.0);
    vec4 viewPosition = uInverseProjection * vec4(ndc, 1.0);
    if (abs(viewPosition.w) <= 1.0e-6) {
        return center;
    }
    vec3 viewPoint = viewPosition.xyz / viewPosition.w;
    // The view matrix maps the camera's -Z axis (uCameraForward) onto positive view-space depth, so
    // a reconstructed point in front of the camera has negative viewPoint.z and must be offset by
    // -uCameraForward * viewPoint.z to land back in world space.
    vec3 worldPoint = uCameraPosition + uCameraRight * viewPoint.x + uCameraUp * viewPoint.y -
                      uCameraForward * viewPoint.z;
    vec4 previousClip = uPreviousViewProjection * vec4(worldPoint, 1.0);
    if (previousClip.w <= 1.0e-6) {
        return center;
    }

    vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    vec2 texelSize = 1.0 / vec2(textureSize(uSourceTexture, 0));
    vec2 velocity = uv - previousUv;
    float velocityPixels = length(velocity / texelSize);
    if (isnan(velocityPixels) || isinf(velocityPixels) || velocityPixels < 0.5) {
        return center;
    }
    velocity *= min(1.0, 20.0 / velocityPixels) * strength;

    int sampleCount = clamp(uMotionBlurSampleCount, 4, 12);
    vec3 colorSum = center;
    float weightSum = 1.0;
    for (int i = 0; i < 12; ++i) {
        if (i >= sampleCount) {
            break;
        }
        float t = (float(i) + 0.5) / float(sampleCount);
        vec2 sampleUv = clamp(uv - velocity * t, vec2(0.0), vec2(1.0));
        colorSum += SampleHdrUVE(sampleUv);
        weightSum += 1.0;
    }
    return colorSum / weightSum;
}

float FogVolumeSafeScaleUVE(float axis) {
    return abs(axis) < kFogVolumeScaleEpsilonUVE ? kFogVolumeScaleEpsilonUVE : axis;
}

vec3 FogVolumeWorldToLocalUVE(FogVolumeUVE volume, vec3 worldPoint) {
    vec3 delta = worldPoint - volume.position;
    return vec3(dot(delta, volume.axisX) / FogVolumeSafeScaleUVE(volume.scale.x),
                dot(delta, volume.axisY) / FogVolumeSafeScaleUVE(volume.scale.y),
                dot(delta, volume.axisZ) / FogVolumeSafeScaleUVE(volume.scale.z));
}

float FogVolumeShapeDistanceUVE(FogVolumeUVE volume, vec3 localPoint) {
    vec3 halfSize = volume.size * 0.5;
    if (halfSize.x <= 0.0 || halfSize.y <= 0.0 || halfSize.z <= 0.0) {
        return 2.0;
    }
    if (volume.shape == 4) {
        return 0.0;
    }
    if (volume.shape == 3) {
        return max(abs(localPoint.x) / halfSize.x,
                   max(abs(localPoint.y) / halfSize.y, abs(localPoint.z) / halfSize.z));
    }
    if (volume.shape == 0) {
        vec3 n = localPoint / halfSize;
        return length(n);
    }
    if (volume.shape == 2) {
        vec2 xz = vec2(localPoint.x / halfSize.x, localPoint.z / halfSize.z);
        return max(length(xz), abs(localPoint.y) / halfSize.y);
    }
    if (localPoint.y < -halfSize.y || localPoint.y > halfSize.y) {
        return 2.0;
    }
    float along = (localPoint.y + halfSize.y) / (2.0 * halfSize.y);
    float allowed = 1.0 - along;
    vec2 xz = vec2(localPoint.x / halfSize.x, localPoint.z / halfSize.z);
    float radial = length(xz);
    float radialN = allowed > 1.0e-5 ? radial / allowed : (radial > 1.0e-5 ? 2.0 : 0.0);
    return max(abs(localPoint.y) / halfSize.y, radialN);
}

float FogVolumeOccupancyUVE(FogVolumeUVE volume, float normalizedDistance) {
    if (volume.shape == 4) {
        return 1.0;
    }
    if (normalizedDistance > 1.0) {
        return 0.0;
    }
    if (volume.edgeFade <= 0.0) {
        return 1.0;
    }
    return 1.0 - smoothstep(1.0 - volume.edgeFade, 1.0, normalizedDistance);
}

float FogVolumeHeightTermUVE(FogVolumeUVE volume, vec3 worldPoint, vec3 localPoint) {
    if (volume.heightFalloff <= 0.0) {
        return 1.0;
    }
    float height = volume.shape == 4 ? (worldPoint.y - volume.position.y) : (localPoint.y + volume.size.y * 0.5);
    return exp(-max(height, 0.0) * volume.heightFalloff);
}

float FogVolumeDensityAtUVE(FogVolumeUVE volume, vec3 worldPoint) {
    if (volume.shape == 4) {
        return volume.density * FogVolumeHeightTermUVE(volume, worldPoint, vec3(0.0));
    }
    vec3 localPoint = FogVolumeWorldToLocalUVE(volume, worldPoint);
    float occupancy = FogVolumeOccupancyUVE(volume, FogVolumeShapeDistanceUVE(volume, localPoint));
    if (occupancy <= 0.0) {
        return 0.0;
    }
    return volume.density * occupancy * FogVolumeHeightTermUVE(volume, worldPoint, localPoint);
}

bool FogVolumeIntersectAabbUVE(vec3 origin, vec3 direction, vec3 halfSize, float rayLength, out float enter,
                               out float exit) {
    enter = 0.0;
    exit = rayLength;
    if (rayLength <= 0.0) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        float h = axis == 0 ? halfSize.x : (axis == 1 ? halfSize.y : halfSize.z);
        float o = axis == 0 ? origin.x : (axis == 1 ? origin.y : origin.z);
        float d = axis == 0 ? direction.x : (axis == 1 ? direction.y : direction.z);
        if (h <= 0.0) {
            return false;
        }
        if (abs(d) < 1.0e-5) {
            if (o < -h || o > h) {
                return false;
            }
            continue;
        }
        float inv = 1.0 / d;
        float nearT = (-h - o) * inv;
        float farT = (h - o) * inv;
        if (nearT > farT) {
            float swapT = nearT;
            nearT = farT;
            farT = swapT;
        }
        enter = max(enter, nearT);
        exit = min(exit, farT);
        if (enter >= exit) {
            return false;
        }
    }
    enter = max(enter, 0.0);
    exit = min(exit, rayLength);
    return exit > enter;
}

void FogVolumeIntegrateUVE(FogVolumeUVE volume, vec3 rayOrigin, vec3 rayDirection, float rayLength, inout float tau,
                           inout vec3 colorMass, inout float occupancyMass) {
    float enter = 0.0;
    float exit = rayLength;
    if (volume.shape != 4) {
        vec3 localOrigin = FogVolumeWorldToLocalUVE(volume, rayOrigin);
        vec3 localDirection = vec3(dot(rayDirection, volume.axisX) / FogVolumeSafeScaleUVE(volume.scale.x),
                                   dot(rayDirection, volume.axisY) / FogVolumeSafeScaleUVE(volume.scale.y),
                                   dot(rayDirection, volume.axisZ) / FogVolumeSafeScaleUVE(volume.scale.z));
        if (!FogVolumeIntersectAabbUVE(localOrigin, localDirection, volume.size * 0.5, rayLength, enter, exit)) {
            return;
        }
    }
    float span = exit - enter;
    if (span <= 0.0) {
        return;
    }
    float step = span / float(kFogVolumeRaySamplesUVE);
    for (int i = 0; i < kFogVolumeRaySamplesUVE; ++i) {
        float t = enter + step * (float(i) + 0.5);
        vec3 point = rayOrigin + rayDirection * t;
        tau += FogVolumeDensityAtUVE(volume, point) * step;
        float occupancy = 1.0;
        vec3 localPoint = vec3(0.0);
        if (volume.shape != 4) {
            localPoint = FogVolumeWorldToLocalUVE(volume, point);
            occupancy = FogVolumeOccupancyUVE(volume, FogVolumeShapeDistanceUVE(volume, localPoint));
        }
        if (occupancy > 0.0) {
            float height = FogVolumeHeightTermUVE(volume, point, localPoint);
            colorMass += (volume.albedo * max(volume.density, 0.0) * height + volume.emission) * (occupancy * step);
            occupancyMass += occupancy * step;
        }
    }
}

void main() {
    vec2 sourceUV = LensDistortionSourceUVUVE(vTexCoord);
    float periphery = 0.0;
    if (uHumanEye != 0) {
        vec2 ndc = vTexCoord * 2.0 - 1.0;
        periphery = smoothstep(0.55, 1.2, length(ndc));
        sourceUV = HumanEyeSourceUVUVE(vTexCoord);
    }
    vec3 hdrColor = ApplyFastApproximateAAUVE(sourceUV);
    hdrColor = ApplySharpeningUVE(sourceUV, hdrColor);
    if (periphery > 0.0) {
        vec2 px = vec2(uHumanEyeTexelX, uHumanEyeTexelY) * (1.5 + 4.0 * periphery);
        vec3 blur = hdrColor;
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(px.x, 0.0)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-px.x, 0.0)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(0.0, px.y)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(0.0, -px.y)));
        vec2 diagonal = px * 0.7;
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(diagonal.x, diagonal.y)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-diagonal.x, diagonal.y)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(diagonal.x, -diagonal.y)));
        blur += SampleHdrUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-diagonal.x, -diagonal.y)));
        blur *= 1.0 / 9.0;
        hdrColor = mix(hdrColor, blur, periphery * 0.7);
        float grey = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
        hdrColor = mix(hdrColor, vec3(grey), periphery * 0.2);
    }
    float depth = texture(uSceneDepthTexture, sourceUV).r;
    hdrColor = ApplyDepthOfFieldUVE(sourceUV, hdrColor, depth);
    hdrColor = ApplyCameraMotionBlurUVE(sourceUV, hdrColor, depth);
    float exposure = uExposure > 0.0 ? uExposure : 1.0;
    hdrColor *= exposure;
    int volumeCount = clamp(uFogVolumeCount, 0, 8);
    if (uFogEnabled != 0 || volumeCount > 0) {
        vec2 ndc = sourceUV * 2.0 - 1.0;
        vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
        vec3 viewDir = normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
        float fallbackDistance = depth < 1.0
                                     ? mix(max(uCameraNear, 0.0), max(uCameraFar, 0.0), clamp(depth, 0.0, 1.0))
                                     : max(uCameraFar, 0.0);
        float viewDistance = depth < 1.0
                                 ? ReconstructViewDistanceUVE(sourceUV, depth, fallbackDistance)
                                 : fallbackDistance;
        float globalTau = 0.0;
        if (uFogEnabled != 0) {
            if (depth < 1.0) {
                if (uFogMode == kFogModeLinearUVE) {
                    float fogAmount = clamp((viewDistance - max(uFogStart, 0.0)) /
                                                max(uFogEnd - max(uFogStart, 0.0), 1.0e-5),
                                            0.0, 1.0);
                    // Convert linear opacity to optical depth so it composes with local volumetric
                    // fog through the same transmittance equation.
                    globalTau = -log(max(1.0 - fogAmount, 1.0e-5));
                } else {
                    float density = max(uFogDensity, 0.0);
                    if (uFogMode == kFogModeHeightUVE) {
                        // Preserve the existing height-fog profile as the default mode.
                        vec3 worldPos = uCameraPosition + viewDir * viewDistance;
                        float heightTerm = exp(-max(worldPos.y - uFogHeight, 0.0) /
                                               max(uFogHeightFalloff, 0.01));
                        density *= mix(1.0, heightTerm, 0.85);
                    }
                    globalTau = density * viewDistance;
                }
            } else {
                float sky = clamp(uFogSkyAffect, 0.0, 0.9999);
                globalTau = -log(max(1.0 - sky, 1.0e-5));
            }
        }
        float localTau = 0.0;
        vec3 localColorMass = vec3(0.0);
        float localOccupancyMass = 0.0;
        for (int i = 0; i < volumeCount; ++i) {
            FogVolumeIntegrateUVE(uFogVolumes[i], uCameraPosition, viewDir, max(viewDistance, 0.0), localTau,
                                  localColorMass, localOccupancyMass);
        }
        float combinedTau = globalTau + localTau;
        float fogFactor = 1.0 - exp(-max(combinedTau, 0.0));
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
        float towardSun = pow(max(dot(viewDir, sunDir), 0.0), 8.0);
        float day = smoothstep(-0.08, 0.18, sunDir.y);
        vec3 globalInscatter = max(uFogColor, vec3(0.0));
        globalInscatter = mix(globalInscatter, max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0) *
                                                   max(uLightVolumetricFogEnergy, 0.0),
                              clamp(uFogSunScatter, 0.0, 1.0) * towardSun * day);
        vec3 volumeColor = localOccupancyMass > 0.0 ? localColorMass / localOccupancyMass : globalInscatter;
        float posGlobal = max(globalTau, 0.0);
        float posLocal = max(localTau, 0.0);
        float weight = posGlobal + posLocal;
        vec3 inscatter = weight > 0.0 ? (globalInscatter * posGlobal + volumeColor * posLocal) / weight : globalInscatter;
        hdrColor = mix(hdrColor, inscatter * exposure, fogFactor);
    }
    vec3 ldr = ApplyToneMappingUVE(hdrColor);
    if (uContrast > 0.0 || uSaturation > 0.0 || uBrightness != 0.0 || any(greaterThan(uColorFilter, vec3(0.0)))) {
        float contrast = uContrast > 0.0 ? uContrast : 1.0;
        float saturation = uSaturation > 0.0 ? uSaturation : 1.0;
        vec3 filterColor = any(greaterThan(uColorFilter, vec3(0.0))) ? uColorFilter : vec3(1.0);
        ldr = (ldr - vec3(0.5)) * contrast + vec3(0.5) + vec3(uBrightness);
        float grey = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
        ldr = mix(vec3(grey), ldr, saturation);
        ldr *= max(filterColor, vec3(0.0));
        ldr = clamp(ldr, 0.0, 1.0);
    }
    float vignetteIntensity = clamp(uVignetteIntensity, 0.0, 1.0);
    if (vignetteIntensity > 0.0) {
        float aspect = max(uAspect, 0.0001);
        vec2 centeredScreen = (vTexCoord * 2.0 - 1.0) * vec2(aspect, 1.0);
        float cornerDistance = length(vec2(aspect, 1.0));
        float normalizedDistance = length(centeredScreen) / max(cornerDistance, 0.0001);
        float radius = clamp(uVignetteRadius, 0.0, 0.9999);
        float edgeDarkening = smoothstep(radius, 1.0, normalizedDistance);
        ldr *= 1.0 - vignetteIntensity * edgeDarkening;
    }
    float filmGrainIntensity = clamp(uFilmGrainIntensity, 0.0, 1.0);
    if (filmGrainIntensity > 0.0) {
        float grainNoise = FilmGrainNoiseUVE(floor(gl_FragCoord.xy), uFilmGrainFrame) - 0.5;
        ldr = clamp(ldr + vec3(grainNoise * filmGrainIntensity * 0.08), 0.0, 1.0);
    }
    if (uDitheringEnabled != 0) {
        float dither = ScreenSpaceDitherNoiseUVE(floor(gl_FragCoord.xy)) / 255.0;
        ldr = clamp(ldr + vec3(dither), 0.0, 1.0);
    }
    float covered = (depth < 1.0 || uSkyCovers != 0) ? 1.0 : 0.0;
    float alpha = uWriteCoverageAlpha != 0 ? covered : 1.0;
    FragColor = vec4(ldr, alpha);
}
#endif
)GLSLSRC";


const std::string_view kProceduralSkySource = R"GLSLSRC(#version 450 core

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

uniform sampler2D uSceneDepthTexture;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec3 uSkyColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform float uSkyCurve;
uniform float uGroundCurve;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunEnergy;
uniform sampler2D uSkyTexture;
uniform int uSkyTextureEnabled;

vec3 AtmosphereUpperUVE(vec3 dir, vec3 sun) {
    float mu = clamp(dot(dir, sun), -1.0, 1.0);
    float sunY = sun.y;
    float viewY = dir.y;
    float day = smoothstep(-0.08, 0.18, sunY);
    float sunset = exp(-pow(sunY * 6.0, 2.0)) * smoothstep(-0.15, 0.0, sunY);

    vec3 noonZenith = max(uSkyColor, vec3(0.0));
    vec3 noonHorizon = max(uHorizonColor, vec3(0.0));
    vec3 sunsetZenith = vec3(0.07, 0.09, 0.26);
    vec3 sunsetHorizon = vec3(1.0, 0.36, 0.08);
    vec3 nightZenith = vec3(0.004, 0.006, 0.018);
    vec3 nightHorizon = vec3(0.018, 0.028, 0.055);

    vec3 zenith = mix(nightZenith, noonZenith, day);
    zenith = mix(zenith, sunsetZenith, sunset);
    vec3 horizon = mix(nightHorizon, noonHorizon, day);
    horizon = mix(horizon, sunsetHorizon, sunset);
    float towardSun = pow(max(mu, 0.0), 3.5);
    horizon = mix(horizon, sunsetHorizon, towardSun * max(sunset, (1.0 - day) * 0.25));

    vec3 sky = mix(horizon, zenith, pow(clamp(viewY, 0.0, 1.0), max(uSkyCurve, 0.001)));
    sky *= mix(1.0, 1.55, day * (1.0 - sunset));
    sky = mix(sky, max(uGroundColor, vec3(0.0)) * (0.18 * day), pow(1.0 - clamp(viewY, 0.0, 1.0), 6.0) * 0.22);

    float g = mix(0.76, 0.93, sunset);
    float mieDen = 1.0 + g * g - 2.0 * g * mu;
    float mie = (1.0 - g * g) / max(pow(max(mieDen, 0.001), 1.5), 0.001);
    vec3 sunCol = max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0);
    sky += sunCol * mie * mix(0.012, 0.09, sunset) * day;

    float disk = smoothstep(0.99945, 0.99982, mu) * smoothstep(-0.02, 0.04, sunY);
    sky += sunCol * disk * mix(7.5, 16.0, sunset);
    return max(sky, vec3(0.0));
}

vec3 ProceduralSkyUVE(vec3 viewDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
    if (dir.y >= 0.0) {
        return AtmosphereUpperUVE(dir, sun);
    }
    vec3 horizonDir = normalize(vec3(dir.x, 0.001, dir.z));
    vec3 horizon = AtmosphereUpperUVE(horizonDir, sun);
    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 ground = max(uGroundColor, vec3(0.0)) * (0.12 + 0.88 * day);
    float groundT = pow(clamp(-dir.y, 0.0, 1.0), max(uGroundCurve, 0.001));
    return mix(horizon, ground, groundT);
}

vec2 SkyEquirectUvUVE(vec3 dir) {
    vec3 d = normalize(dir);
    float longitude = 0.0;
    if (abs(d.x) + abs(d.z) >= 1.0e-8) {
        longitude = atan(d.z, d.x);
    }
    float latitude = asin(clamp(d.y, -1.0, 1.0));
    return vec2(longitude * (1.0 / (2.0 * 3.14159265358979323846)) + 0.5,
                latitude * (1.0 / 3.14159265358979323846) + 0.5);
}

void main() {
    if (texture(uSceneDepthTexture, vTexCoord).r < 1.0) {
        discard;
    }
    vec2 ndc = vTexCoord * 2.0 - 1.0;
    vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
    vec3 worldDir = normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
    vec3 color = ProceduralSkyUVE(worldDir);
    if (uSkyTextureEnabled != 0) {
        color = texture(uSkyTexture, SkyEquirectUvUVE(worldDir)).rgb;
    }
    FragColor = vec4(max(color, vec3(0.0)), 1.0);
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
uniform float uEmissiveEnergy = 1.0;
uniform float uNormalScale = 1.0;
uniform float uOcclusionStrength = 1.0;
uniform vec3 uUvScale = vec3(1.0, 1.0, 0.0);
uniform vec3 uUvOffset = vec3(0.0, 0.0, 0.0);
uniform int uUnshaded = 0;
uniform float uAlphaCutoff = 0.0;
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

    return mix(1.0, visibleSamples / float(sampleCount), clamp(uShadowOpacity, 0.0, 1.0));
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
    return FadeShadowToLitAtDistanceUVE(shadowFactor, viewDepth, finalCascadeFarDepth);
}

void main() {
    vec2 uv = vTexCoord * uUvScale.xy + uUvOffset.xy;
    vec4 albedoSample = texture(uAlbedoTexture, uv);
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    vec3 albedo = albedoSample.rgb * uAlbedoColor;
    float surfaceOpacity = clamp(uSurfaceOpacity, 0.0, 1.0);
    if (uUnshaded != 0) {
        FragColor = vec4(albedo, surfaceOpacity);
        return;
    }
    float ambientOcclusion = mix(1.0, texture(uAOTexture, uv).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 normal = SafeNormalizeUVE(vWorldNormal);
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
    vec3 viewDirection = SafeNormalizeUVE(uViewPosition - vWorldPosition);
    vec3 metallicRoughnessSample = texture(uMetallicRoughnessTexture, uv).rgb;
    float metallic = clamp(uMetallic * metallicRoughnessSample.b, 0.0, 1.0);
    float roughness = clamp(uRoughness * metallicRoughnessSample.g, 0.04, 1.0);
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
                        max(4.0 * normalDotView * normalDotLight, kBrdfEpsilonUVE) * max(light.specular, 0.0);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 diffuse = diffuseWeight * albedo / kPiUVE;
        vec3 radiance = light.color * light.intensity * attenuation;
        vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

        if (light.type == 0) {
            directContribution *= DirectionalShadowFactorUVE(normal, lightDirection);
        }
        lighting += directContribution;
    }

    FragColor = vec4(lighting, surfaceOpacity);
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


const std::string_view kDecalSource = R"GLSLSRC(#version 450 core

// Decal3D's paint pass. The geometry arrives already in world space and already clipped to the
// decal's volume by the CPU projection pass (see DecalPatchUVE), so the vertex stage only projects
// it and the fragment stage only asks how much of the decal reaches this pixel.
//
// The fades are re-evaluated here, per pixel, from the authored fields: the CPU evaluates the same
// three fades at each patch's centre to decide whether the patch paints at all, but a patch is one
// receiving face and a wall's face can span the whole volume - a weight flat across it would end in
// a hard rectangle where the artist asked for a fade. The rules below mirror
// Scene::SampleDecal3DUVE() exactly; the coordinate space they read is not mirrored, it is the same
// matrix the CPU clipped in (DecalDrawCommandUVE::worldToUnit).

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uViewProjection;

void main() {
    vWorldPosition = aPosition;
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uViewPosition;
uniform sampler2D uAlbedoTexture;
uniform mat4 uWorldToUnit;
uniform vec3 uProjectionDirection;
uniform vec3 uBaseColor;
uniform vec3 uEmissionColor;
uniform float uAlphaScale;
uniform float uNormalFade;
uniform float uUpperFade;
uniform float uLowerFade;
uniform float uDistanceFadeEnabled;
uniform float uDistanceFadeBegin;
uniform float uDistanceFadeLength;

// Scene::SampleDecal3DUVE()'s helper of the same name: 0 leaves the axis unfaded, 1 fades the whole
// half of the volume, and anything between fades the outer band of it.
float AxisFadeWeight(float unitCoordinate, float band) {
    if (!(band > 0.0)) {
        return 1.0;
    }
    float edge = 1.0 - band;
    if (unitCoordinate <= edge) {
        return 1.0;
    }
    return 1.0 - min((unitCoordinate - edge) / band, 1.0);
}

void main() {
    // The volume's unit coordinates at this pixel: [-1, 1] on each axis, 0 at the volume's centre.
    // The patches are inside it by construction, so "inside" is not re-tested here.
    vec3 local = (uWorldToUnit * vec4(vWorldPosition, 1.0)).xyz;

    // How squarely the surface faces the decal: a surface turned back up the projection direction is
    // fully facing, an edge-on sliver or a backface scores zero.
    float facingTowardsDecal = clamp(dot(normalize(vNormal), -uProjectionDirection), 0.0, 1.0);
    float normalWeight = 1.0 - clamp(uNormalFade, 0.0, 1.0) * (1.0 - facingTowardsDecal);

    // The volume's +Y end is the far end of the projection, each authored band fades its own half.
    float depthWeight = AxisFadeWeight(local.y, uUpperFade) * AxisFadeWeight(-local.y, uLowerFade);

    float distanceWeight = 1.0;
    if (uDistanceFadeEnabled > 0.5) {
        float cameraDistance = distance(uViewPosition, vWorldPosition);
        distanceWeight = uDistanceFadeLength > 0.0
                             ? 1.0 - clamp((cameraDistance - uDistanceFadeBegin) / uDistanceFadeLength, 0.0, 1.0)
                             : (cameraDistance <= uDistanceFadeBegin ? 1.0 : 0.0);
    }

    float weight = clamp(normalWeight * depthWeight * distanceWeight, 0.0, 1.0);

    // The material's texture, sampled with the patch's unit coordinates, and its alpha is what
    // makes a decal the shape the artist drew rather than the rectangle it was clipped to. The
    // renderer binds a 1x1 white texture when the material leaves albedo unset, so an untextured
    // decal paints its flat colour and takes this alpha as 1.
    vec4 albedo = texture(uAlbedoTexture, vTexCoord);
    FragColor = vec4((uBaseColor + uEmissionColor) * albedo.rgb, albedo.a * weight * clamp(uAlphaScale, 0.0, 1.0));
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


const std::string_view kLitPrimitive3DSource = R"GLSLSRC(#version 450 core

// Built-in primitives (PrimitiveMeshComponentUVE: Cube, UVSphere, Plane) carry an authored base
// colour and no material, so they cannot go through the PBR path lit_shadowed_3d.glsl serves -
// there is no albedo/normal/AO texture, no metallic or roughness, and nothing to sample a shadow
// map with. They are still real scene geometry, though, and shading them flat made every primitive
// read as a silhouette with no form.
//
// This is therefore deliberately Lambert-only: the engine's exact light contract (the same
// LightUVE layout, the same type codes, the same range and spot-cone falloff) applied as pure
// diffuse over the selected world ambient source. It has no material maps, specular BRDF, or
// shadowing; a selected world environment map is sampled only for its ambient diffuse contribution.

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
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform int uAmbientSource; // 0=None, 1=FlatColor, 2=Sky, 3=EnvironmentMap
uniform sampler2D uAmbientEnvironmentMap;
uniform int uAmbientEnvironmentMapEnabled;
uniform vec3 uColor;
uniform float uSurfaceOpacity = 1.0;

vec3 SafeNormalizeUVE(vec3 value) {
    float lengthValue = length(value);
    return lengthValue > kEpsilonUVE ? value / lengthValue : vec3(0.0, 1.0, 0.0);
}

vec3 HemisphereAmbientUVE(vec3 normal) {
    if (dot(uSkyAmbient, uSkyAmbient) + dot(uGroundAmbient, uGroundAmbient) < 1.0e-10) {
        return uAmbientColor;
    }
    float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(uGroundAmbient, uSkyAmbient, hemi);
}

vec2 AmbientEnvironmentUvUVE(vec3 direction) {
    vec3 dir = SafeNormalizeUVE(direction);
    float longitude = atan(dir.z, dir.x);
    float latitude = asin(clamp(dir.y, -1.0, 1.0));
    const float kPiUVE = 3.14159265359;
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
    return HemisphereAmbientUVE(normal);
}

void main() {
    vec3 normal = SafeNormalizeUVE(vNormal);
    vec3 ambient = AmbientFromSourceUVE(normal);
    vec3 accumulated = uColor * ambient;

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

    FragColor = vec4(accumulated, clamp(uSurfaceOpacity, 0.0, 1.0));
}
#endif
)GLSLSRC";

const std::string_view kBloomBrightPassSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform float uBloomThreshold;
uniform float uBloomIntensity;
uniform float uBloomSoftKnee;

void main() {
    vec3 hdrColor = max(texture(uSourceTexture, vTexCoord).rgb, vec3(0.0));
    float luminance = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    float threshold = max(uBloomThreshold, 0.0);
    float excess = luminance - threshold;
    float softKnee = clamp(uBloomSoftKnee, 0.0, 1.0);
    if (softKnee > 0.0 && threshold > 0.0) {
        float knee = threshold * softKnee;
        float softContribution = clamp(excess + knee, 0.0, 2.0 * knee);
        softContribution = softContribution * softContribution / (4.0 * knee);
        excess = max(excess, softContribution);
    } else {
        excess = max(excess, 0.0);
    }
    float contribution = excess / max(luminance, 0.0001);
    FragColor = vec4(hdrColor * contribution * max(uBloomIntensity, 0.0), 1.0);
}
#endif
)GLSLSRC";

const std::string_view kBloomDownsampleSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
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

uniform sampler2D uSourceTexture;

void main() {
    // Four linearly filtered samples average the previous bloom level while halving its extent.
    vec2 halfTexel = 0.5 / vec2(textureSize(uSourceTexture, 0));
    vec3 downsampled = texture(uSourceTexture, vTexCoord + vec2(-halfTexel.x, -halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(halfTexel.x, -halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(-halfTexel.x, halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(halfTexel.x, halfTexel.y)).rgb;
    FragColor = vec4(downsampled * 0.25, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kBloomBlurSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
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
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

// The default is a passthrough; the SSAO composite opts into a small AO blur before its Multiply
// blend. Bloom uses this same shader with the blur toggle off and its Additive blend mode.
uniform sampler2D uSourceTexture;
uniform int uSsaoBlurEnabled;

vec4 SampleSourceClampedUVE(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(uSourceTexture, 0));
    vec2 halfTexel = texelSize * 0.5;
    return texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel));
}

void main() {
    if (uSsaoBlurEnabled == 0) {
        FragColor = texture(uSourceTexture, vTexCoord);
        return;
    }

    // A small separable-Gaussian-equivalent 3x3 kernel smooths the half-resolution AO term before
    // the Multiply blend, without needing an extra ping-pong target or feeding back into the source.
    vec2 texelSize = 1.0 / vec2(textureSize(uSourceTexture, 0));
    vec2 x = vec2(texelSize.x, 0.0);
    vec2 y = vec2(0.0, texelSize.y);
    vec4 center = SampleSourceClampedUVE(vTexCoord);
    vec4 axes = SampleSourceClampedUVE(vTexCoord - x) + SampleSourceClampedUVE(vTexCoord + x) +
                SampleSourceClampedUVE(vTexCoord - y) + SampleSourceClampedUVE(vTexCoord + y);
    vec4 diagonals = SampleSourceClampedUVE(vTexCoord - x - y) +
                     SampleSourceClampedUVE(vTexCoord + x - y) +
                     SampleSourceClampedUVE(vTexCoord - x + y) +
                     SampleSourceClampedUVE(vTexCoord + x + y);
    FragColor = (center * 4.0 + axes * 2.0 + diagonals) * (1.0 / 16.0);
}
#endif
)GLSLSRC";

const std::string_view kSsaoSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
out vec2 vTexCoord;

void main() {
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
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
uniform float uPower;
uniform int uSampleCount;

// A 12-entry hemisphere kernel (quality tiers consume its first 4, 8, or all 12 samples) is
// offline-generated, hemisphere-distributed, and biased toward the origin so more samples land
// close to the shaded point. HashUVE() below supplies per-pixel kernel rotation without a vendored
// noise texture, trading a small amount of dither/banding for a compact built-in path. A dedicated
// rotation-noise texture remains a future quality upgrade, not a correctness requirement.
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
    int sampleCount = clamp(uSampleCount, 1, kKernelSizeUVE);
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
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
    float visibility = clamp(1.0 - (occlusion / float(sampleCount)) * max(uIntensity, 0.0), 0.0, 1.0);
    visibility = pow(visibility, clamp(uPower, 0.1, 4.0));
    FragColor = vec4(vec3(visibility), 1.0);
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
