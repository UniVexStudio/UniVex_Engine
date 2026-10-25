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
    float covered = depth < 1.0 ? 1.0 : (uSkyCovers != 0 ? 0.75 : 0.0);
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
uniform float uSunAngularDiameter;
uniform float uSunDiscIntensity;
uniform float uSunLimbDarkening;
uniform int uAtmosphericSky;
uniform float uAtmosphereRayleigh;
uniform float uAtmosphereMie;
uniform float uAtmosphereOzone;
uniform float uAtmosphereMieAnisotropy;
uniform float uAtmosphereMultiScatter;
uniform float uAtmosphereIntensity;
uniform int uAtmosphereSteps;
uniform vec3 uCameraPosition;
uniform float uSkyTime;
uniform float uStarDensity;
uniform float uStarBrightness;
uniform float uStarSeed;
uniform float uMilkyWayStrength;
uniform int uCloudsEnabled;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudAltitude;
uniform vec2 uCloudWind;
uniform float uCloudSoftness;
uniform float uCloudSilverLining;
uniform sampler2D uSkyTexture;
uniform int uSkyTextureEnabled;

// The disc: a body of the light the scene is lit by, not a texture on the sky. Its size is the
// light's own angular diameter, so a moon and a sun over the same sky differ in this term and
// nothing else. cos() of the half-angle is the dot product at the rim, which is why the whole test
// is a comparison against mu and needs no branch or texture fetch.
//
// The rim is feathered over the outer 43% of the radius rather than drawn as a hard circle: at
// ordinary viewport sizes and with no anti-aliasing on this pass, an unfeathered disc at a
// realistic angular size flickers between frames as the view moves under it.
//
// Shared by both skies below, because the disc belongs to the light in either case. It is a 0
// when the light asks for no disc, so the caller can add it unconditionally.
float SunDiscUVE(float mu, float sunY) {
    float sunRadius = radians(max(uSunAngularDiameter, 0.0)) * 0.5;
    if (sunRadius <= 0.0) {
        return 0.0;
    }
    float cosRim = cos(sunRadius);
    float cosCore = cos(sunRadius * 0.57);
    float mask = smoothstep(cosRim, cosCore, mu);
    // Limb darkening: the disc's own surface is dimmer at its edge than at its centre, because the
    // line of sight leaves the photosphere at a shallower angle there. 0 is a flat disc.
    float rimT = clamp((1.0 - mu) / max(1.0 - cosRim, 1.0e-9), 0.0, 1.0);
    float limb = 1.0 - clamp(uSunLimbDarkening, 0.0, 1.0) * (1.0 - sqrt(max(1.0 - rimT, 0.0)));
    return mask * limb * smoothstep(-0.02, 0.04, sunY) * max(uSunDiscIntensity, 0.0);
}

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
    sky = mix(sky, max(uGroundColor, vec3(0.0)) * (0.05 * day), pow(1.0 - clamp(viewY, 0.0, 1.0), 8.0) * 0.08);

    float g = mix(0.76, 0.93, sunset);
    float mieDen = 1.0 + g * g - 2.0 * g * mu;
    float mie = (1.0 - g * g) / max(pow(max(mieDen, 0.001), 1.5), 0.001);
    vec3 sunCol = max(uSunColor, vec3(1.0)) * max(uSunEnergy, 1.0);
    sky += sunCol * mie * mix(0.012, 0.09, sunset) * day;

    sky += sunCol * SunDiscUVE(mu, sunY) * mix(7.5, 16.0, sunset);
    return max(sky, vec3(0.0));
}

// ---- Physical atmosphere ----------------------------------------------------------------
//
// Single scattering through a spherical shell of air. The shape of the sky is a result here, not
// an input: a blue zenith, a pale horizon and a red sunset all fall out of three coefficients and
// the distance the light travelled through each of them.
//
// The numbers are Earth's, in metres and inverse metres, because that is the only way the model
// stays honest - the planet's radius sets how fast the air thins, and a coefficient in invented
// units would produce a sky that only looks right at one sun angle.
const float kPlanetRadiusUVE = 6360000.0;
const float kAtmosphereRadiusUVE = 6420000.0;
const float kRayleighScaleHeightUVE = 8000.0;
const float kMieScaleHeightUVE = 1200.0;
const float kOzoneCenterUVE = 25000.0;
const float kOzoneWidthUVE = 15000.0;
// Rayleigh scattering per metre at sea level, by channel. Blue scatters roughly six times as
// strongly as red, which is the entire reason the sky is blue.
const vec3 kRayleighScatterUVE = vec3(5.8e-6, 13.5e-6, 33.1e-6);
// Ozone absorbs in the Chappuis band - mostly green, some red - and almost nowhere else. Its tent
// profile sits high in the stratosphere, and it is what turns a twilight sky from grey to violet.
const vec3 kOzoneAbsorbUVE = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kMieScatterUVE = 21.0e-6;
// Haze both scatters and absorbs; the ratio is its single-scattering albedo, here about 0.91.
const float kMieExtinctionUVE = 1.1;
// The sun's radiance into this model. Chosen so a noon sky lands in the same range as the lit
// surfaces around it, which keeps exposure and bloom settings from needing two answers.
const float kSkySunRadianceUVE = 14.0;
// Where the eye sits above the ground, in metres. The air above a person is what a person sees.
const float kObserverHeightUVE = 120.0;
const int kLightStepsUVE = 4;
// How much of the extinction the multiple-scattering stand-in is allowed to feel. Re-scattered
// light arrives by many paths, so its own path is not the straight line to the sun.
const float kMultiScatterTauUVE = 0.12;
const float kMultiScatterGainUVE = 0.9;
// Above this much direct light the sky is already bright and the stand-in contributes nothing.
const float kMultiScatterFillUVE = 0.5;

struct AtmosphereDepthUVE {
    float rayleigh;
    float mie;
    float ozone;
};

// Entry and exit distances of a ray through a sphere centred on the planet. False when the ray
// misses it entirely; either root may be negative depending on where the ray starts.
bool RaySphereUVE(vec3 origin, vec3 dir, float radius, out float tNear, out float tFar) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        tNear = 0.0;
        tFar = 0.0;
        return false;
    }
    float root = sqrt(discriminant);
    tNear = -b - root;
    tFar = -b + root;
    return tFar > 0.0;
}

// How much air one path passes through, in the same units the coefficients are per.
AtmosphereDepthUVE AtmosphereOpticalDepthUVE(vec3 origin, vec3 dir, float pathLength, const int steps,
                                             const float rayleigh, const float mie, const float ozone) {
    AtmosphereDepthUVE depth;
    depth.rayleigh = 0.0;
    depth.mie = 0.0;
    depth.ozone = 0.0;
    if (pathLength <= 0.0 || steps <= 0) {
        return depth;
    }
    // Not named `length`: GLSL would resolve the local before the built-in and every length()
    // call inside this function would stop compiling.
    float segment = pathLength / float(steps);
    for (int i = 0; i < steps; ++i) {
        vec3 point = origin + dir * (segment * (float(i) + 0.5));
        // Clamped at the ground: a sample that lands a centimetre under the surface is a rounding
        // artifact of the ray's exit point, not air at negative altitude, and exp() of a negative
        // height would hand back a density larger than the ground itself has.
        float height = max(length(point) - kPlanetRadiusUVE, -1.0);
        depth.rayleigh += exp(-height / kRayleighScaleHeightUVE) * rayleigh * segment;
        depth.mie += exp(-height / kMieScaleHeightUVE) * mie * segment;
        depth.ozone += max(0.0, 1.0 - abs(height - kOzoneCenterUVE) / kOzoneWidthUVE) * ozone * segment;
    }
    return depth;
}

vec3 PhysicalSkyUVE(vec3 viewDir, vec3 sunDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(sunDir) > 1.0e-5 ? normalize(sunDir) : vec3(0.0, 1.0, 0.0);
    vec3 eye = vec3(0.0, kPlanetRadiusUVE + kObserverHeightUVE, 0.0);

    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 ground = max(uGroundColor, vec3(0.0)) * mix(0.35, 1.0, day);

    float groundNear = 0.0;
    float groundFar = 0.0;
    if (RaySphereUVE(eye, dir, kPlanetRadiusUVE, groundNear, groundFar) && groundNear > 0.0) {
        // The ray meets the ground: there is no sky along it, only terrain the level may or may
        // not have drawn. The band stands in for the missing geometry.
        return ground;
    }

    float skyNear = 0.0;
    float skyFar = 0.0;
    if (!RaySphereUVE(eye, dir, kAtmosphereRadiusUVE, skyNear, skyFar)) {
        return ground;
    }

    int steps = clamp(uAtmosphereSteps, 4, 64);
    float rayleigh = max(uAtmosphereRayleigh, 0.0);
    float mie = max(uAtmosphereMie, 0.0);
    float ozone = max(uAtmosphereOzone, 0.0);

    float segment = skyFar / float(steps);
    float travelledRayleigh = 0.0;
    float travelledMie = 0.0;
    float travelledOzone = 0.0;
    vec3 scatteredRayleigh = vec3(0.0);
    vec3 scatteredMie = vec3(0.0);
    vec3 scatteredMultiple = vec3(0.0);

    for (int i = 0; i < steps; ++i) {
        vec3 point = eye + dir * (segment * (float(i) + 0.5));
        float height = max(length(point) - kPlanetRadiusUVE, -1.0);
        float densityRayleigh = exp(-height / kRayleighScaleHeightUVE) * rayleigh;
        float densityMie = exp(-height / kMieScaleHeightUVE) * mie;
        float densityOzone = max(0.0, 1.0 - abs(height - kOzoneCenterUVE) / kOzoneWidthUVE) * ozone;
        travelledRayleigh += densityRayleigh * segment;
        travelledMie += densityMie * segment;
        travelledOzone += densityOzone * segment;

        // Shadowed air: a sample whose line to the sun is blocked by the planet contributes no
        // light at all, which is what makes the sunset terminator a sharp edge on the ground.
        float sunNear = 0.0;
        float sunFar = 0.0;
        if (RaySphereUVE(point, sun, kPlanetRadiusUVE, sunNear, sunFar) && sunNear > 0.0) {
            continue;
        }
        float lightNear = 0.0;
        float lightFar = 0.0;
        float lightPath = RaySphereUVE(point, sun, kAtmosphereRadiusUVE, lightNear, lightFar) ? lightFar : 0.0;
        AtmosphereDepthUVE toSun =
            AtmosphereOpticalDepthUVE(point, sun, lightPath, kLightStepsUVE, rayleigh, mie, ozone);

        vec3 extinction = kRayleighScatterUVE * (travelledRayleigh + toSun.rayleigh) +
                          vec3(kMieScatterUVE * kMieExtinctionUVE) * (travelledMie + toSun.mie) +
                          kOzoneAbsorbUVE * (travelledOzone + toSun.ozone);
        vec3 transmittance = exp(-extinction);
        scatteredRayleigh += densityRayleigh * segment * transmittance;
        scatteredMie += densityMie * segment * transmittance;
        // The stand-in: the same air, but its light is allowed only part of the extinction it
        // would have earned on a straight path, because it did not take one.
        scatteredMultiple +=
            (densityRayleigh * kRayleighScatterUVE + vec3(densityMie * kMieScatterUVE)) *
            exp(-extinction * kMultiScatterTauUVE) * segment;
    }

    // Rayleigh scatters almost evenly in both directions; the 1 + mu^2 is its charge-dipole
    // pattern, which is why the sky is slightly brighter toward and away from the sun than across.
    float mu = clamp(dot(dir, sun), -1.0, 1.0);
    float phaseRayleigh = (3.0 / (16.0 * 3.14159265358979323846)) * (1.0 + mu * mu);
    // Henyey-Greenstein for the haze: g at 0.76 puts most of its light in a forward cone, which
    // is the bright halo that reaches across the sky near the sun at any hour.
    float g = clamp(uAtmosphereMieAnisotropy, -0.999, 0.999);
    float g2 = g * g;
    float phaseMie = (3.0 / (8.0 * 3.14159265358979323846)) * ((1.0 - g2) * (1.0 + mu * mu)) /
                     ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * mu, 1.0e-4), 1.5));

    float radiance = max(uAtmosphereIntensity, 0.0) * kSkySunRadianceUVE * max(uSunEnergy, 0.0);
    vec3 direct = radiance * (scatteredRayleigh * kRayleighScatterUVE * phaseRayleigh +
                              scatteredMie * (kMieScatterUVE * phaseMie));

    // Gated on where single scattering came up short instead of added everywhere: added flat, it
    // whitens the sunset it should be filling in, and the reddest part of the sky is exactly the
    // part that needs it least.
    float luminance = dot(direct, vec3(0.2126, 0.7152, 0.0722));
    float fill = clamp(1.0 - luminance / kMultiScatterFillUVE, 0.0, 1.0);
    vec3 multiple = radiance * scatteredMultiple *
                    (kMultiScatterGainUVE * max(uAtmosphereMultiScatter, 0.0) /
                     (4.0 * 3.14159265358979323846)) *
                    fill;

    vec3 sky = direct + multiple;
    // The disc is added inside the atmosphere, not on top of it, so its own light is dimmed by the
    // same air the rest of the sky came through. At the horizon that is what reddens it.
    sky += max(uSunColor, vec3(0.0)) * SunDiscUVE(mu, sun.y) * mix(7.5, 16.0, 1.0 - day);
    vec3 nightSky = vec3(0.004, 0.006, 0.018);
    return max(sky * day + nightSky * (1.0 - day), vec3(0.0));
}

// ---- Night and cloud ---------------------------------------------------------------------
//
// The sky so far is what the air does to sunlight. These two are what is in front of and behind
// that air: a star field with a galactic band behind it, and a cloud deck in front. Both are
// overlays rather than parts of either sky model, so they draw over the gradient and the
// scattering model alike and are occluded in the same order by the same rule.

// A cheap integer hash. Written with 32-bit multiplies instead of sin(): sin() is fast to type and
// slow to run, and on some drivers its low bits are not random enough for a star field, which
// shows up as visible diagonal banding in the noise.
uint SkyHashUVE(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float SkyHashUnitUVE(uint x) {
    return float(x & 0x00FFFFFFU) * (1.0 / 16777215.0);
}

float SkyCellNoiseUVE(ivec3 cell) {
    uint h = SkyHashUVE(uint(cell.x) * 0x9e3779b1U);
    h = SkyHashUVE(h ^ (uint(cell.y) * 0x85ebca77U));
    h = SkyHashUVE(h ^ (uint(cell.z) * 0xc2b2ae3dU));
    return SkyHashUnitUVE(h);
}

vec4 SkyCellRandomsUVE(ivec3 cell, float seed) {
    uint h = SkyHashUVE(uint(cell.x) * 0x9e3779b1U);
    h = SkyHashUVE(h ^ (uint(cell.y) * 0x85ebca77U));
    h = SkyHashUVE(h ^ (uint(cell.z) * 0xc2b2ae3dU));
    h = SkyHashUVE(h ^ uint(max(seed, 0.0)));
    return vec4(SkyHashUnitUVE(h), SkyHashUnitUVE(SkyHashUVE(h)),
                SkyHashUnitUVE(SkyHashUVE(h ^ 0x68bc21ebU)),
                SkyHashUnitUVE(SkyHashUVE(h ^ 0x02e5be93U)));
}

// Value noise, 2D and 3D. Smoothstep-interpolated hashes rather than a gradient noise: the
// difference is invisible at cloud frequency, and this way the whole thing is integer hashing with
// no table to upload or keep in sync.
float SkyNoiseUVE(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    ivec3 base = ivec3(int(cell.x), int(cell.y), 0);
    float a = SkyCellNoiseUVE(base);
    float b = SkyCellNoiseUVE(base + ivec3(1, 0, 0));
    float c = SkyCellNoiseUVE(base + ivec3(0, 1, 0));
    float d = SkyCellNoiseUVE(base + ivec3(1, 1, 0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float SkyNoiseUVE(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 base = ivec3(cell);
    float c000 = SkyCellNoiseUVE(base);
    float c100 = SkyCellNoiseUVE(base + ivec3(1, 0, 0));
    float c010 = SkyCellNoiseUVE(base + ivec3(0, 1, 0));
    float c110 = SkyCellNoiseUVE(base + ivec3(1, 1, 0));
    float c001 = SkyCellNoiseUVE(base + ivec3(0, 0, 1));
    float c101 = SkyCellNoiseUVE(base + ivec3(1, 0, 1));
    float c011 = SkyCellNoiseUVE(base + ivec3(0, 1, 1));
    float c111 = SkyCellNoiseUVE(base + ivec3(1, 1, 1));
    float bottom = mix(mix(c000, c100, u.x), mix(c010, c110, u.x), u.y);
    float top = mix(mix(c001, c101, u.x), mix(c011, c111, u.x), u.y);
    return mix(bottom, top, u.z);
}

float SkyFbmUVE(vec2 p, const int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += SkyNoiseUVE(p) * amplitude;
        total += amplitude;
        p = p * 2.03 + vec2(17.1, 9.7);
        amplitude *= 0.5;
    }
    return sum / max(total, 1.0e-5);
}

float SkyFbmUVE(vec3 p, const int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += SkyNoiseUVE(p) * amplitude;
        total += amplitude;
        p = p * 2.03 + vec3(5.3, 11.7, 3.1);
        amplitude *= 0.5;
    }
    return sum / max(total, 1.0e-5);
}

// Stars: one candidate per cell on a shell of radius uStarDensity around the eye, so the cells are
// all the same angular size and the field has no seam or pole where they bunch up. The neighbours
// are checked too, because a star placed near a cell boundary is partly in the next cell and would
// otherwise be cut in half.
float StarFieldUVE(vec3 dir) {
    if (uStarBrightness <= 0.0 || uStarDensity <= 0.0) {
        return 0.0;
    }
    vec3 p = dir * uStarDensity;
    ivec3 base = ivec3(floor(p));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                ivec3 cell = base + ivec3(x, y, z);
                vec4 r = SkyCellRandomsUVE(cell, uStarSeed);
                // Most cells are empty; this threshold is what makes the sky mostly dark. It uses
                // a different channel from the size and the brightness on purpose - sharing one
                // would make every star the same, because a cell that passes the threshold already
                // has its threshold channel near 1.
                if (r.w < 0.97) {
                    continue;
                }
                vec3 centre = vec3(cell) + vec3(0.15 + 0.7 * r.x, 0.15 + 0.7 * r.y, 0.15 + 0.7 * r.z);
                float angle = acos(clamp(dot(dir, normalize(centre)), -1.0, 1.0));
                // 0.0004 to 0.0016 radians is roughly half a pixel to two pixels at a 60-degree
                // field of view, which is the range where a star is a point on screen and not a
                // disc. Squaring the hash packs most stars near the small end.
                float radius = mix(0.00040, 0.0016, r.z * r.z);
                float magnitude = mix(0.18, 1.0, r.y * r.y);
                sum += (1.0 - smoothstep(0.0, radius, angle)) * magnitude;
            }
        }
    }
    return sum;
}

// The galactic band: a great circle of concentrated starlight, with dust lanes through it. The
// band is the plane through the eye whose normal is fixed, so it passes through the zenith at a
// set tilt rather than sitting at a constant altitude.
float MilkyWayUVE(vec3 dir) {
    if (uMilkyWayStrength <= 0.0) {
        return 0.0;
    }
    const vec3 normal = vec3(0.3714, 0.7428, 0.5571);
    float distanceFromBand = dot(dir, normal);
    float band = exp(-distanceFromBand * distanceFromBand * 90.0);
    if (band < 0.002) {
        return 0.0;
    }
    // Stretched noise along the band: a rift reads as structure, isotropic noise reads as fog.
    float broad = SkyFbmUVE(dir * 5.0, 4);
    float fine = SkyFbmUVE(dir * 14.0, 3);
    float stars = SkyFbmUVE(dir * 40.0, 2);
    float dust = smoothstep(0.30, 0.80, broad * 0.62 + fine * 0.38);
    return band * dust * (0.55 + 0.45 * stars);
}

// The cloud deck: a plane at uCloudAltitude, sampled where the view ray crosses it. Flat rather
// than volumetric, which is the honest trade - it is one texture lookup's worth of cost and it
// reads correctly at every angle except straight up through a broken deck.
vec4 CloudsUVE(vec3 dir, vec3 sun, float day) {
    if (uCloudsEnabled == 0 || dir.y <= 0.004 || uCloudAltitude <= 0.0) {
        return vec4(0.0);
    }
    float travel = uCloudAltitude / dir.y;
    // Bounded so a ray skimming the horizon does not project to infinity and sample the same
    // stretched texel across the whole bottom of the screen.
    travel = min(travel, 60000.0);
    vec3 crossing = uCameraPosition + dir * travel;
    // One noise cell is about 830 metres of deck at this scale, with the octaves below it taking
    // the detail down to roughly a hundred - the range where a cumulus edge still reads as an edge
    // rather than as a smear.
    vec2 uv = (crossing.xz + uCloudWind * uSkyTime) * 0.0012;

    float broad = SkyFbmUVE(uv, 4);
    float detail = SkyFbmUVE(uv * 3.7 + vec2(31.4, 12.9), 3);
    // The threshold is the coverage: raising it pushes the noise field further under the cut, so
    // more of the sky is clear. Erring the field by a second, finer noise is what stops the deck
    // reading as one smooth blob and gives it ragged edges.
    float threshold = 1.0 - clamp(uCloudCoverage, 0.0, 1.0);
    float softness = max(uCloudSoftness, 0.001);
    float value = broad + (detail - 0.5) * 0.28;
    float amount = smoothstep(threshold, threshold + softness, value);
    amount *= clamp(uCloudDensity, 0.0, 1.0);
    // Faded out near the horizon. A flat deck projects to infinitely compressed detail there, and
    // the compressed stripes shimmer as the camera turns; the fade is also what the atmosphere
    // itself would do to a deck seen at that angle.
    amount *= smoothstep(0.0, 0.06, dir.y);
    if (amount <= 0.001) {
        return vec4(0.0);
    }

    // Thickness from the same field: the middle of a cloud is thicker than its edge, and the
    // difference is what makes it read as a body instead of a stencil.
    float thickness = smoothstep(threshold, min(threshold + softness * 3.0, 1.0), value);
    float mu = clamp(dot(dir, sun), 0.0, 1.0);
    // Silver lining: thin cloud toward the sun transmits it, and the brighter the thinner it is.
    // The pow() on mu keeps the effect near the sun rather than washing the whole deck.
    float lining = pow(mu, 6.0) * (1.0 - thickness) * max(uCloudSilverLining, 0.0);
    float forward = pow(mu, 2.0);

    // Underside in shade, topside lit. At night only the moonlit value survives, which is why the
    // night factor is applied to the lit term and not to the base colour.
    vec3 shade = vec3(0.30, 0.33, 0.40);
    vec3 lit = vec3(0.96, 0.97, 1.0);
    vec3 body = mix(shade, lit, thickness * 0.75 + 0.25 * forward);
    // Warm the deck at a low sun, so it takes the horizon colour the rest of the sky has.
    float lowSun = smoothstep(0.35, 0.0, sun.y) * day;
    body = mix(body, body * vec3(1.18, 0.94, 0.78), lowSun * 0.7);
    body = body * mix(0.10, 1.0, day);
    body += max(uSunColor, vec3(0.0)) * lining * day * 2.4;

    return vec4(max(body, vec3(0.0)), amount);
}

// Draws the two night layers and the deck in the order light arrives at the eye: stars and the
// band are behind the air, cloud is in front of it. Called by both sky models, so a scene that
// switches source keeps its stars and its weather.
vec3 ApplySkyOverlaysUVE(vec3 base, vec3 dir, vec3 sun, float day) {
    float night = 1.0 - smoothstep(-0.08, 0.18, sun.y);
    if (uStarBrightness > 0.0 && night > 0.002) {
        // Slightly cool white: real starlight is not neutral, and a pure white field reads as
        // noise rather than as sky.
        vec3 tint = vec3(0.86, 0.92, 1.0);
        base += tint * (StarFieldUVE(dir) * uStarBrightness * night);
        base += tint * (MilkyWayUVE(dir) * uMilkyWayStrength * night * 0.35);
    }
    vec4 cloud = CloudsUVE(dir, sun, day);
    return mix(base, cloud.rgb, cloud.a);
}

vec3 ProceduralSkyUVE(vec3 viewDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);

    // A scene choosing Sky > Atmosphere gets the scattering model instead of the gradient. Both
    // read the same sun and the same ground colour, so switching between them in the Inspector
    // changes how the sky is built without changing what it is lit by.
    if (uAtmosphericSky != 0) {
        return ApplySkyOverlaysUVE(PhysicalSkyUVE(dir, sun), dir, sun, smoothstep(-0.08, 0.18, sun.y));
    }

    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 groundBase = max(uGroundColor, vec3(0.0));
    vec3 groundLit = groundBase * mix(0.40, 1.0, day);
    vec3 horizonLit = max(uHorizonColor, vec3(0.0)) * mix(0.35, 1.0, day);

    if (dir.y < -0.001) {
        float groundT = clamp(-dir.y, 0.0, 1.0);
        float groundCurve = clamp(max(uGroundCurve, 0.01), 0.1, 4.0);
        float falloff = pow(groundT, groundCurve);
        vec3 ground = mix(horizonLit, groundLit, falloff);
        float seamBlend = smoothstep(-0.015, 0.0, dir.y);
        return mix(ground, horizonLit, seamBlend);
    }

    vec3 upperSky = AtmosphereUpperUVE(dir, sun);
    float horizonBlend = smoothstep(0.0, 0.015, dir.y);
    return mix(horizonLit, upperSky, horizonBlend);
}

/// The gradient sky with the night and cloud overlays on top. Separate from the body above so the
/// ground branch below the horizon can return early without skipping them.
vec3 GradientSkyWithOverlaysUVE(vec3 dir, vec3 sun) {
    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 base = ProceduralSkyUVE(dir);
    // Below the horizon there is no sky to overlay: the ground band already stands in for the
    // terrain, and stars under it would be visible through the floor.
    if (dir.y < -0.001) {
        return base;
    }
    return ApplySkyOverlaysUVE(base, dir, sun, day);
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
    // Same guard the sky functions use internally: a zero direction has no normalize.
    vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
    vec3 color;
    if (uSkyTextureEnabled != 0) {
        // A panorama replaces the sky, not the weather: cloud and stars still sit in front of and
        // behind it, so a photographed sky can have a moving deck over it.
        color = texture(uSkyTexture, SkyEquirectUvUVE(worldDir)).rgb;
        if (worldDir.y > -0.001) {
            color = ApplySkyOverlaysUVE(color, worldDir, sunDir, smoothstep(-0.08, 0.18, sunDir.y));
        }
    } else if (uAtmosphericSky != 0) {
        color = ProceduralSkyUVE(worldDir);
    } else {
        color = GradientSkyWithOverlaysUVE(worldDir, sunDir);
    }
    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
#endif
)GLSLSRC";

const std::string_view kShadowDepthSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

#if defined(UVE_INSTANCED) || defined(UVE_INDIRECT_INSTANCED)
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
#ifdef UVE_INDIRECT_INSTANCED
layout(std430, binding = 3) readonly buffer VisibleIndexBlock {
    uint visibleIndices[];
};
#endif
#else
uniform mat4 uModel;
#endif
uniform mat4 uLightSpaceMatrix;

void main() {
#if defined(UVE_INDIRECT_INSTANCED)
    mat4 model = instanceModels[uInstanceBaseIndex + int(visibleIndices[gl_InstanceID])];
#elif defined(UVE_INSTANCED)
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

const std::string_view kOcclusionProxySource = R"GLSLSRC(#version 450 core

// Occlusion proxy for Renderer3DUVE's GPU occlusion stage (Foundation WP3). The pipeline that owns
// this shader is depth-tested (LessOrEqual), writes no depth, and blends additively; because the
// fragment contributes exactly zero, the colour attachment is left untouched and the only
// observable effect is the sample count inside the query bracket that surrounds the draw.

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(0.0);
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
// Sky reflections: what a mirror outdoors actually shows. Three bands instead of a captured cube,
// because the sky is the one environment that is smooth enough for a gradient to be honest.
uniform int uSkyReflectionsLinked;
uniform vec3 uSkyReflectionSky;
uniform vec3 uSkyReflectionHorizon;
uniform vec3 uSkyReflectionGround;
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

vec3 SkyReflectionUVE(vec3 direction, float roughness) {
    // Direction picks the band: overhead, horizon, or the bounce off the ground. A mirror then reads
    // as a gradient rather than one flat colour, which is the difference between chrome in a box and
    // chrome under open sky. Roughness pulls it back toward the ambient - a rough surface integrates
    // over a wide cone, and the diffuse-ish ambient already is that integral, so this avoids
    // pretending a blurry surface still shows a crisp horizon line.
    float up = clamp(direction.y, -1.0, 1.0);
    vec3 sharp = up >= 0.0
        ? mix(uSkyReflectionHorizon, uSkyReflectionSky, smoothstep(0.0, 0.30, up))
        : mix(uSkyReflectionHorizon, uSkyReflectionGround, smoothstep(0.0, 0.22, -up));
    float blur = roughness * roughness;
    vec3 soft = mix(uSkyReflectionSky, uSkyReflectionHorizon, 0.5);
    return mix(sharp, soft, blur);
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
    if (uSkyReflectionsLinked != 0) {
        specEnv = SkyReflectionUVE(reflection, roughness);
    }
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
    if (centerDepth >= 0.9999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // Background/far plane: never occluded.
        return;
    }
    vec3 centerViewPos = ReconstructViewPositionUVE(vTexCoord, centerDepth);

    // View-space normal from screen-space derivatives of the reconstructed position.
    vec3 dx = dFdx(centerViewPos);
    vec3 dy = dFdy(centerViewPos);
    vec3 n = cross(dx, dy);
    float nLenSq = dot(n, n);
    vec3 viewNormal = nLenSq > 1.0e-8 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    if (isnan(viewNormal.x) || isnan(viewNormal.y) || isnan(viewNormal.z) ||
        isinf(viewNormal.x) || isinf(viewNormal.y) || isinf(viewNormal.z)) {
        viewNormal = vec3(0.0, 0.0, 1.0);
    }
    if (dot(viewNormal, -centerViewPos) < 0.0) {
        viewNormal = -viewNormal;
    }

    float rotationAngle = HashUVE(vTexCoord) * 6.28318530718;
    float cosAngle = cos(rotationAngle);
    float sinAngle = sin(rotationAngle);
    vec3 randomTangent = normalize(vec3(cosAngle, sinAngle, 0.0));
    vec3 perp = randomTangent - viewNormal * dot(randomTangent, viewNormal);
    float pLenSq = dot(perp, perp);
    vec3 tangent = pLenSq > 1.0e-8 ? perp * inversesqrt(pLenSq) : vec3(1.0, 0.0, 0.0);
    vec3 bitangent = cross(viewNormal, tangent);
    mat3 tbn = mat3(tangent, bitangent, viewNormal);

    float normalViewDot = max(dot(viewNormal, normalize(-centerViewPos)), 0.15);
    float angleBias = max(uBias, uBias / normalViewDot);

    float occlusion = 0.0;
    int sampleCount = clamp(uSampleCount, 1, kKernelSizeUVE);
    int validSamples = 0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        vec3 samplePos = centerViewPos + (tbn * kKernelUVE[sampleIndex]) * uRadius;

        // Re-project the sample point with uProjection to look up what's actually in the depth
        // buffer at that screen location.
        vec4 sampleClip = uProjection * vec4(samplePos, 1.0);
        if (abs(sampleClip.w) < 0.0001) {
            continue;
        }
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        if (isnan(sampleUv.x) || isnan(sampleUv.y) || isinf(sampleUv.x) || isinf(sampleUv.y) ||
            sampleUv.x < 0.001 || sampleUv.x > 0.999 || sampleUv.y < 0.001 || sampleUv.y > 0.999) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        if (sampledDepth >= 0.9999) {
            continue;
        }
        vec3 sampledViewPos = ReconstructViewPositionUVE(sampleUv, sampledDepth);

        float depthDelta = abs(centerViewPos.z - sampledViewPos.z);
        if (depthDelta > uRadius * 2.5) {
            continue;
        }
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(depthDelta, 0.0001));
        occlusion += (sampledViewPos.z >= samplePos.z + angleBias ? 1.0 : 0.0) * rangeCheck;
        ++validSamples;
    }
    float rawOcc = validSamples > 0 ? (occlusion / float(sampleCount)) : 0.0;
    float visibility = clamp(1.0 - rawOcc * max(uIntensity, 0.0), 0.15, 1.0);
    visibility = pow(visibility, clamp(uPower, 0.1, 4.0));
    if (isnan(visibility) || isinf(visibility)) {
        visibility = 1.0;
    }

    // Edge fade prevents glancing angle blackouts at screen/viewport borders
    vec2 edgeDist = min(vTexCoord, 1.0 - vTexCoord);
    float edgeFade = smoothstep(0.0, 0.02, min(edgeDist.x, edgeDist.y));
    visibility = mix(1.0, visibility, edgeFade);

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

const std::string_view kDebugLineSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProjection;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kDebugPointSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProjection;
uniform float uPointSize;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kSprite2DSource = R"GLSLSRC(#version 450 core
#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProjection;
out vec2 vTexCoord;
out vec4 vColor;
void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif
#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    FragColor = texture(uTexture, vTexCoord) * vColor;
}
#endif
)GLSLSRC";

const std::string_view kContactShadowSource = R"GLSLSRC(#version 450 core

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
)GLSLSRC";

} // namespace UVE::Render::Shader::BuiltIn
