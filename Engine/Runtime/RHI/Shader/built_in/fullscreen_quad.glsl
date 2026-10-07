#version 450 core

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
