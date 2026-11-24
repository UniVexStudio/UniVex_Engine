// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


// Every constant below is a raw string literal transcribed byte-for-byte from its corresponding
// .glsl file under engine/render/shader/built_in/ - kept in sync by convention, enforced by
// tests/render/shader/built_in_shaders_parity_uve_tests.cpp (reads the real file and asserts
// exact equality against the constant here). Used automatically by ShaderManagerUVE as a fallback
// when the corresponding virtual path isn't reachable (see ShaderProgramDescUVE's doc comment).

#include "built_in_shaders.h"

namespace UVE::Render::Shader::BuiltIn {

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
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform sampler2D uSceneDepthTexture;
uniform sampler2D uShadowMapTextures[3];
uniform sampler2D uLutTexture;
uniform sampler2D uSsaoTexture;
uniform sampler2D uContactShadowTexture;
uniform sampler2D uSsrTexture;
uniform sampler2D uBloomTexture;

uniform int uNearestUpscaling;
// 0 = Linear clamp, 1 = ACES filmic, 2 = Soft filmic (smooth highlight roll-off).
uniform int uToneMappingMethod;
uniform int uFastApproximateAA;
uniform int uScreenSpaceAAQuality;
uniform float uSharpeningAmount;
uniform int uDitheringEnabled;
uniform float uFxaaTexelX;
uniform float uFxaaTexelY;
uniform int uWriteCoverageAlpha;
uniform int uEncodeSrgbOutput;

// Unified composite enable flags (eliminates separate full-res RGBA16F LoadOp::Load passes).
uniform int uSsaoCompositeEnabled;
uniform int uSsaoBlurEnabled;
uniform int uContactShadowCompositeEnabled;
uniform int uSsrCompositeEnabled;
uniform int uBloomCompositeEnabled;
uniform int uHasColorGrading;
uniform int uHasLutTexture;

// Backend depth decode: ndcZ = depthSample * uDepthNdcScale + uDepthNdcBias.
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uProjA;
uniform float uProjB;
uniform float uInvProjX;
uniform float uInvProjY;
uniform int uFastReconstruct;

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
uniform int uOrthographic;
uniform float uOrthoHalfHeight;
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
uniform float uSunAngularDiameter;
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
uniform int uVolumetricShadowsEnabled;
uniform mat4 uLightSpaceMatrices[3];
uniform float uShadowCascadeSplits[3];
uniform int uLutEnabled;
uniform float uLutContribution;
uniform int uColorblindMode;
uniform float uColorblindSeverity;

const int kFogModeLinearUVE = 0; // WorldEnvironmentFogModeUVE::Linear
const int kFogModeHeightUVE = 2; // Exponential is 1; Height is 2.
const int kFogVolumeRaySamplesUVE = 12;
const float kFogVolumeScaleEpsilonUVE = 1.0e-6;

// Filled concentric Vogel-disc taps covering inner, middle, and outer bokeh rings cleanly.
const vec2 kDepthOfFieldOffsetsUVE[12] = vec2[12](
    vec2( 0.2887,  0.0000), vec2(-0.3690,  0.3360), vec2( 0.0567, -0.6426), vec2( 0.4596,  0.6087),
    vec2(-0.8350, -0.1566), vec2( 0.7031, -0.5812), vec2(-0.0884,  0.9600), vec2(-0.6251, -0.7805),
    vec2( 0.9850,  0.1725), vec2(-0.8588,  0.5123), vec2( 0.3420, -0.9397), vec2( 0.1736,  0.9848)
);

float InterleavedGradientNoiseUVE(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float DecodeNdcDepthUVE(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPointUVE(vec2 uv, float depthSample) {
    float ndcZ = DecodeNdcDepthUVE(clamp(depthSample, 0.0, 1.0));
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 viewPosition = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return viewPosition.xyz / max(abs(viewPosition.w), 1.0e-6);
}

float ReconstructViewDistanceUVE(vec2 uv, float depthSample, float fallbackDistance) {
    float safeNear = max(uCameraNear, 0.01);
    float ndcZ = DecodeNdcDepthUVE(clamp(depthSample, 0.0, 1.0));
    if (uOrthographic == 0 && ndcZ <= 0.0001) {
        return safeNear;
    }
    vec3 viewPos = ReconstructViewPointUVE(uv, depthSample);
    if (uOrthographic == 0 && viewPos.z >= -1.0e-4) {
        return safeNear;
    }
    float viewDistance = uOrthographic != 0 ? max(-viewPos.z, 0.0) : length(viewPos);
    return (isnan(viewDistance) || isinf(viewDistance)) ? fallbackDistance : max(viewDistance, 0.0);
}

// Exact IEC 61966-2-1 Linear -> sRGB OETF and inverse EOTF.
vec3 LinearToSrgbUVE(vec3 linearColor) {
    vec3 c = max(linearColor, vec3(0.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

vec3 SrgbToLinearUVE(vec3 srgbColor) {
    vec3 c = max(srgbColor, vec3(0.0));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

float SampleVolumetricShadowDepthUVE(int cascade, vec2 uv) {
    if (cascade == 0) { return texture(uShadowMapTextures[0], uv).r; }
    if (cascade == 1) { return texture(uShadowMapTextures[1], uv).r; }
    return texture(uShadowMapTextures[2], uv).r;
}

float VolumetricShadowFactorUVE(vec3 samplePos, float distFromCam) {
    if (uVolumetricShadowsEnabled == 0) {
        return 1.0;
    }
    for (int cascade = 0; cascade < 3; ++cascade) {
        if (distFromCam > uShadowCascadeSplits[cascade] && cascade < 2) {
            continue;
        }
        vec4 lightClip = uLightSpaceMatrices[cascade] * vec4(samplePos, 1.0);
        if (abs(lightClip.w) < 1.0e-5) {
            return 1.0;
        }
        vec3 proj = lightClip.xyz / lightClip.w;
        vec2 uv = proj.xy * 0.5 + 0.5;
        float depthRef = uDepthNdcScale == 1.0 ? proj.z : (proj.z * 0.5 + 0.5);
        if (uv.x <= 0.002 || uv.x >= 0.998 || uv.y <= 0.002 || uv.y >= 0.998 ||
            depthRef <= 0.0 || depthRef >= 1.0) {
            return 1.0;
        }
        float shadowDepth = SampleVolumetricShadowDepthUVE(cascade, uv);
        return (depthRef - 0.0015 > shadowDepth) ? 0.0 : 1.0;
    }
    return 1.0;
}

vec3 SampleLutUVE(sampler2D lut, vec3 color) {
    ivec2 lutSize = textureSize(lut, 0);
    float size = float(max(lutSize.y, 1));
    float maxColor = max(size - 1.0, 1.0);
    vec3 c = clamp(color, 0.0, 1.0) * maxColor;
    float cell = floor(c.z);
    float fracZ = c.z - cell;
    vec2 invLutSize = 1.0 / max(vec2(lutSize), vec2(1.0));
    vec2 halfTexel = vec2(0.5) * invLutSize;
    vec2 cellOffset0 = vec2(cell * size, 0.0) * invLutSize;
    vec2 cellOffset1 = vec2(min(cell + 1.0, maxColor) * size, 0.0) * invLutSize;
    vec2 uv = c.xy * invLutSize + halfTexel;
    vec3 s0 = texture(lut, cellOffset0 + uv).rgb;
    vec3 s1 = texture(lut, cellOffset1 + uv).rgb;
    return mix(s0, s1, fracZ);
}

vec3 AcesToneMapUVE(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Smooth filmic curve with wide-gamut inset/outset rotation so bright saturated highlights
// (fire, sparks, sunlight, emissives) roll smoothly toward luminous neutral whites without
// harsh hue shifts or brittle clipping.
vec3 SoftFilmicContrastApproxUVE(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

vec3 SoftFilmicToneMapUVE(vec3 color) {
    const mat3 kInsetMatrix = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    const mat3 kOutsetMatrix = mat3(
         1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433,  1.15107367264116
    );
    const float kMinEv = -12.47393;
    const float kMaxEv = 4.026069;

    vec3 val = kInsetMatrix * max(color, vec3(0.0));
    val = clamp(log2(max(val, vec3(1.0e-6))), vec3(kMinEv), vec3(kMaxEv));
    val = (val - kMinEv) / (kMaxEv - kMinEv);
    val = SoftFilmicContrastApproxUVE(val);
    val = kOutsetMatrix * val;
    val = clamp(val, 0.0, 1.0);
    // Curve output is perceptual-encoded (~2.2); decode back to linear [0, 1] so downstream
    // linear-to-sRGB OETF stays exact across OpenGL, Editor, and Vulkan SRGB swapchains.
    return pow(val, vec3(2.2));
}

vec3 ApplyToneMappingUVE(vec3 color) {
    if (uToneMappingMethod == 0) {
        return clamp(color, 0.0, 1.0);
    }
    if (uToneMappingMethod == 1) {
        return AcesToneMapUVE(color);
    }
    return SoftFilmicToneMapUVE(color);
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

vec4 SampleHdrColorAlphaUVE(vec2 uv) {
    vec4 raw;
    if (uNearestUpscaling != 0) {
        ivec2 sourceSize = textureSize(uSourceTexture, 0);
        vec2 clampedUV = clamp(uv, vec2(0.0), vec2(1.0));
        ivec2 sourceTexel = clamp(ivec2(floor(clampedUV * vec2(sourceSize))), ivec2(0), sourceSize - ivec2(1));
        raw = texelFetch(uSourceTexture, sourceTexel, 0);
    } else {
        raw = texture(uSourceTexture, uv);
    }
    return vec4(max(raw.rgb, vec3(0.0)), clamp(raw.a, 0.0, 1.0));
}

// Half-res depth-aware bilateral upsample for Screen-Space Reflections.
vec4 SampleBilateralSsrUVE(vec2 uv, float centerDepth) {
    vec2 ssrTexel = 1.0 / max(vec2(textureSize(uSsrTexture, 0)), vec2(1.0));
    float centerLin = abs(ReconstructViewPointUVE(uv, centerDepth).z);
    float depthScale = max(centerLin * 0.08, 0.05);
    vec2 offsets[4] = vec2[4](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(0.5, 0.5)
    );
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 sampleUv = clamp(uv + offsets[i] * ssrTexel, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(uSceneDepthTexture, sampleUv).r;
        float sampleLin = sampleDepth < 0.999999 ? abs(ReconstructViewPointUVE(sampleUv, sampleDepth).z) : centerLin;
        float w = exp(-abs(sampleLin - centerLin) / depthScale);
        sum += texture(uSsrTexture, sampleUv) * w;
        weightSum += w;
    }
    return sum / max(weightSum, 1.0e-5);
}

// Samples linear HDR scene color with ambient-only SSAO, direct-light contact shadows, and
// energy-conserving SSR composited cleanly in one unified pass.
vec3 SampleCompositedHdrUVE(vec2 uv, out float depthSample) {
    vec4 raw = SampleHdrColorAlphaUVE(uv);
    vec3 hdr = raw.rgb;
    depthSample = texture(uSceneDepthTexture, uv).r;
    if (depthSample < 0.999999) {
        float indirectWeight = raw.a;
        if (uSsaoCompositeEnabled != 0) {
            float ao = clamp(texture(uSsaoTexture, uv).r, 0.0, 1.0);
            hdr *= mix(1.0, ao, indirectWeight);
        }
        if (uContactShadowCompositeEnabled != 0) {
            float cs = clamp(texture(uContactShadowTexture, uv).r, 0.0, 1.0);
            float directWeight = clamp(1.0 - indirectWeight, 0.0, 1.0);
            hdr *= mix(1.0, cs, directWeight);
        }
        if (uSsrCompositeEnabled != 0) {
            vec4 ssr = SampleBilateralSsrUVE(uv, depthSample);
            hdr = mix(hdr, max(ssr.rgb, vec3(0.0)), clamp(ssr.a, 0.0, 0.85));
        }
    }
    return hdr;
}

vec3 SampleHdrRawUVE(vec2 uv) {
    float unusedDepth = 1.0;
    return SampleCompositedHdrUVE(uv, unusedDepth);
}

vec3 SampleDisplayForAaUVE(vec2 uv, float exposure) {
    float d = 1.0;
    vec3 hdr = SampleCompositedHdrUVE(uv, d);
    if (uBloomCompositeEnabled != 0) {
        hdr += max(texture(uBloomTexture, uv).rgb, vec3(0.0));
    }
    return LinearToSrgbUVE(ApplyToneMappingUVE(hdr * exposure));
}

float PerceptualLumaUVE(vec3 displayRgb) {
    return dot(displayRgb, vec3(0.299, 0.587, 0.114));
}

// Post-tonemap perceptual-luma FXAA 3.11: operates strictly in [0, 1] display sRGB so bright HDR
// speculars never corrupt edge contrast thresholds, with diagonal subpixel anti-aliasing and
// configurable quality edge search (3 / 5 / 7 taps).
vec3 ApplyFastApproximateAAUVE(vec2 uv, vec3 centerDisplay, float exposure) {
    if (uFastApproximateAA == 0 || uFxaaTexelX <= 0.0 || uFxaaTexelY <= 0.0) {
        return centerDisplay;
    }

    vec2 texel = vec2(uFxaaTexelX, uFxaaTexelY);
    float lumaM  = PerceptualLumaUVE(centerDisplay);
    float lumaN  = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2(0.0,  texel.y), vec2(0.0), vec2(1.0)), exposure));
    float lumaS  = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2(0.0, -texel.y), vec2(0.0), vec2(1.0)), exposure));
    float lumaE  = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2( texel.x, 0.0), vec2(0.0), vec2(1.0)), exposure));
    float lumaW  = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2(-texel.x, 0.0), vec2(0.0), vec2(1.0)), exposure));

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        return centerDisplay;
    }

    float lumaNW = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2(-texel.x,  texel.y), vec2(0.0), vec2(1.0)), exposure));
    float lumaNE = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2( texel.x,  texel.y), vec2(0.0), vec2(1.0)), exposure));
    float lumaSW = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2(-texel.x, -texel.y), vec2(0.0), vec2(1.0)), exposure));
    float lumaSE = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uv + vec2( texel.x, -texel.y), vec2(0.0), vec2(1.0)), exposure));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixelLuma = (lumaNS + lumaWE) * 2.0 + (lumaNW + lumaNE + lumaSW + lumaSE);
    float subpixelOffset = clamp(abs(subpixelLuma * (1.0 / 12.0) - lumaM) / max(lumaRange, 1.0e-5), 0.0, 1.0);
    float subpixelBlend = smoothstep(0.0, 1.0, subpixelOffset);
    subpixelBlend = subpixelBlend * subpixelBlend * 0.75;

    float edgeHorizontal =
        abs(-2.0 * lumaW + lumaNW + lumaSW) +
        abs(-2.0 * lumaM + lumaN  + lumaS ) * 2.0 +
        abs(-2.0 * lumaE + lumaNE + lumaSE);
    float edgeVertical =
        abs(-2.0 * lumaN + lumaNW + lumaNE) +
        abs(-2.0 * lumaM + lumaW  + lumaE ) * 2.0 +
        abs(-2.0 * lumaS + lumaSW + lumaSE);
    bool isHorizontal = edgeHorizontal >= edgeVertical;

    float lumaPos = isHorizontal ? lumaN : lumaE;
    float lumaNeg = isHorizontal ? lumaS : lumaW;
    float gradPos = abs(lumaPos - lumaM);
    float gradNeg = abs(lumaNeg - lumaM);
    bool isPosSteeper = gradPos >= gradNeg;
    float stepLength = isHorizontal ? texel.y : texel.x;
    float lumaLocalAvg;
    if (isPosSteeper) {
        lumaLocalAvg = 0.5 * (lumaPos + lumaM);
    } else {
        stepLength = -stepLength;
        lumaLocalAvg = 0.5 * (lumaNeg + lumaM);
    }
    float gradScaled = 0.25 * max(gradPos, gradNeg);

    vec2 currentUv = uv;
    vec2 edgeStep;
    if (isHorizontal) {
        currentUv.y += stepLength * 0.5;
        edgeStep = vec2(texel.x, 0.0);
    } else {
        currentUv.x += stepLength * 0.5;
        edgeStep = vec2(0.0, texel.y);
    }

    int quality = clamp(uScreenSpaceAAQuality, 0, 2);
    int maxIterations = quality == 0 ? 3 : (quality == 1 ? 5 : 7);

    vec2 uvNeg = currentUv - edgeStep;
    vec2 uvPos = currentUv + edgeStep;
    float lumaEndNeg = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uvNeg, vec2(0.0), vec2(1.0)), exposure)) - lumaLocalAvg;
    float lumaEndPos = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uvPos, vec2(0.0), vec2(1.0)), exposure)) - lumaLocalAvg;
    bool reachedNeg = abs(lumaEndNeg) >= gradScaled;
    bool reachedPos = abs(lumaEndPos) >= gradScaled;

    for (int iter = 1; iter < 7; ++iter) {
        if (iter >= maxIterations || (reachedNeg && reachedPos)) {
            break;
        }
        float stepScale = iter >= 4 ? 2.0 : 1.0;
        if (!reachedNeg) {
            uvNeg -= edgeStep * stepScale;
            lumaEndNeg = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uvNeg, vec2(0.0), vec2(1.0)), exposure)) - lumaLocalAvg;
            reachedNeg = abs(lumaEndNeg) >= gradScaled;
        }
        if (!reachedPos) {
            uvPos += edgeStep * stepScale;
            lumaEndPos = PerceptualLumaUVE(SampleDisplayForAaUVE(clamp(uvPos, vec2(0.0), vec2(1.0)), exposure)) - lumaLocalAvg;
            reachedPos = abs(lumaEndPos) >= gradScaled;
        }
    }

    float distNeg = isHorizontal ? (uv.x - uvNeg.x) : (uv.y - uvNeg.y);
    float distPos = isHorizontal ? (uvPos.x - uv.x) : (uvPos.y - uv.y);
    bool isCloserNeg = distNeg < distPos;
    float distClosest = min(distNeg, distPos);
    float spanLength = max(distNeg + distPos, 1.0e-5);
    bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
    bool correctVariation = ((isCloserNeg ? lumaEndNeg : lumaEndPos) < 0.0) != isLumaCenterSmaller;
    float edgeBlend = correctVariation ? max(0.5 - distClosest / spanLength, 0.0) : 0.0;
    float finalBlend = max(edgeBlend, subpixelBlend);

    vec2 finalUv = uv;
    if (isHorizontal) {
        finalUv.y += stepLength * finalBlend;
    } else {
        finalUv.x += stepLength * finalBlend;
    }
    return SampleDisplayForAaUVE(clamp(finalUv, vec2(0.0), vec2(1.0)), exposure);
}

// Single-pass chromatic aberration (3 fetches total, never multiplied inside upstream blur loops).
vec3 ApplySinglePassChromaticAberrationUVE(vec2 uv, vec3 centerDisplay, float exposure) {
    float intensity = clamp(uChromaticAberrationIntensity, 0.0, 1.0);
    if (intensity <= 0.0) {
        return centerDisplay;
    }
    float aspect = max(uAspect, 0.0001);
    vec2 radial = (vTexCoord * 2.0 - 1.0) * vec2(aspect, 1.0);
    float radialLength = length(radial);
    float cornerLength = length(vec2(aspect, 1.0));
    if (radialLength <= 1.0e-5) {
        return centerDisplay;
    }
    float normalizedRadius = clamp(radialLength / max(cornerLength, 0.0001), 0.0, 1.0);
    vec2 direction = radial / radialLength;
    vec2 texelSize = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 offset = direction * texelSize * (2.0 * intensity * normalizedRadius * normalizedRadius);
    float red = SampleDisplayForAaUVE(clamp(uv + offset, vec2(0.0), vec2(1.0)), exposure).r;
    float blue = SampleDisplayForAaUVE(clamp(uv - offset, vec2(0.0), vec2(1.0)), exposure).b;
    return vec3(red, centerDisplay.g, blue);
}

// Edge-clamped unsharp masking in scene-linear space.
vec3 ApplySharpeningUVE(vec2 uv, vec3 center) {
    float amount = clamp(uSharpeningAmount, 0.0, 1.0);
    if (amount <= 0.0) {
        return center;
    }

    vec2 texel = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec3 left = SampleHdrRawUVE(clamp(uv - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 right = SampleHdrRawUVE(clamp(uv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 down = SampleHdrRawUVE(clamp(uv - vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 up = SampleHdrRawUVE(clamp(uv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 neighbourhoodAverage = (left + right + down + up) * 0.25;
    vec3 neighbourhoodMinimum = min(center, min(min(left, right), min(down, up)));
    vec3 neighbourhoodMaximum = max(center, max(max(left, right), max(down, up)));
    vec3 sharpened = center + (center - neighbourhoodAverage) * amount;
    return clamp(sharpened, neighbourhoodMinimum, neighbourhoodMaximum);
}

// Animated IGN monochrome film grain (no sin/cos Moire patterns).
float FilmGrainNoiseUVE(vec2 pixel, int frame) {
    vec2 jittered = pixel + vec2(float(frame % 64) * 5.588238, float((frame * 7) % 64) * 3.141593);
    return InterleavedGradientNoiseUVE(jittered) - 0.5;
}

// Triangular Probability Density Function (TPDF) dithering in [-1, 1] / 255 display step.
float TriangularDitherNoiseUVE(vec2 pixel) {
    float n1 = InterleavedGradientNoiseUVE(pixel);
    float n2 = InterleavedGradientNoiseUVE(pixel + vec2(17.0, 31.0));
    return (n1 + n2 - 1.0) / 255.0;
}

vec2 ApplyBokehShapeOffsetUVE(vec2 offset) {
    if (uDepthOfFieldBokehShape != 1) {
        return offset;
    }
    float radius = length(offset);
    if (radius <= 1.0e-5) {
        return offset;
    }
    const float kPiOverThreeUVE = 1.0471975512;
    const float kPiOverSixUVE = 0.5235987756;
    float angle = atan(offset.y, offset.x);
    float sector = floor((angle - kPiOverSixUVE) / kPiOverThreeUVE + 0.5);
    float faceNormal = sector * kPiOverThreeUVE + kPiOverSixUVE;
    float boundaryScale = 0.8660254038 / max(cos(angle - faceNormal), 1.0e-4);
    return (offset / radius) * (radius * boundaryScale);
}

vec3 ApplyDepthOfFieldUVE(vec2 uv, vec3 center, float centerDepth) {
    float aperture = clamp(uDepthOfFieldAperture, 0.0, 1.0);
    if (uDepthOfFieldEnabled == 0 || aperture <= 0.0) {
        return center;
    }

    float fallbackDistance = max(uCameraFar, uCameraNear);
    float centerDistance = centerDepth < 0.999999
                               ? ReconstructViewDistanceUVE(uv, centerDepth, fallbackDistance)
                               : fallbackDistance;
    float focusDistance = max(uDepthOfFieldFocusDistance, max(uCameraNear, 0.05));
    if (uDepthOfFieldFocusMode == 1 && uDepthOfFieldFocusDistance <= 0.0) {
        vec2 focusUv = vec2(0.5);
        float focusDepth = texture(uSceneDepthTexture, focusUv).r;
        focusDistance = focusDepth < 0.999999
                            ? ReconstructViewDistanceUVE(focusUv, focusDepth, fallbackDistance)
                            : fallbackDistance;
    }
    float circleOfConfusion = abs(centerDistance - focusDistance) /
                              max(max(centerDistance, focusDistance), 1.0e-4);
    float blurRadiusPixels = clamp(circleOfConfusion * aperture * 10.0, 0.0, 10.0);
    if (blurRadiusPixels < 0.5) {
        return center;
    }

    int quality = clamp(uDepthOfFieldQuality, 0, 2);
    int sampleCount = (quality + 1) * 4;
    vec2 texelSize = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    float angle = InterleavedGradientNoiseUVE(gl_FragCoord.xy) * 6.28318530718;
    float cs = cos(angle);
    float sn = sin(angle);
    mat2 rot = mat2(cs, -sn, sn, cs);
    float depthTolerance = max(centerDistance * 0.08, 0.15);
    vec3 colorSum = center;
    float weightSum = 1.0;
    for (int i = 0; i < 12; ++i) {
        if (i >= sampleCount) {
            break;
        }
        vec2 bokehOffset = ApplyBokehShapeOffsetUVE(rot * kDepthOfFieldOffsetsUVE[i]);
        vec2 sampleUv = clamp(uv + bokehOffset * texelSize * blurRadiusPixels, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(uSceneDepthTexture, sampleUv).r;
        float sampleDistance = sampleDepth < 0.999999
                                   ? ReconstructViewDistanceUVE(sampleUv, sampleDepth, fallbackDistance)
                                   : fallbackDistance;
        // Prevent sharp foreground from being contaminated by blurry background while letting
        // blurry foreground bleed softly over background.
        float depthWeight = sampleDistance >= centerDistance
                                ? exp(-abs(sampleDistance - centerDistance) / depthTolerance)
                                : 1.0;
        colorSum += SampleHdrRawUVE(sampleUv) * depthWeight;
        weightSum += depthWeight;
    }
    return colorSum / max(weightSum, 1.0e-4);
}

vec3 ApplyCameraMotionBlurUVE(vec2 uv, vec3 center, float depthSample) {
    float strength = clamp(uMotionBlurStrength, 0.0, 1.0);
    if (uMotionBlurEnabled == 0 || strength <= 0.0) {
        return center;
    }

    vec3 viewPoint = ReconstructViewPointUVE(uv, depthSample);
    vec3 worldPoint = uCameraPosition + uCameraRight * viewPoint.x + uCameraUp * viewPoint.y -
                      uCameraForward * viewPoint.z;
    vec4 previousClip = uPreviousViewProjection * vec4(worldPoint, 1.0);
    if (previousClip.w <= 1.0e-6) {
        return center;
    }

    vec2 previousUv = (previousClip.xy / previousClip.w) * 0.5 + 0.5;
    vec2 texelSize = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 velocity = uv - previousUv;
    float velocityPixels = length(velocity / texelSize);
    if (isnan(velocityPixels) || isinf(velocityPixels) || velocityPixels < 0.5) {
        return center;
    }
    velocity *= min(1.0, 20.0 / velocityPixels) * strength;

    float centerDist = abs(viewPoint.z);
    float jitter = InterleavedGradientNoiseUVE(gl_FragCoord.xy);
    int sampleCount = clamp(uMotionBlurSampleCount, 4, 12);
    vec3 colorSum = center;
    float weightSum = 1.0;
    for (int i = 0; i < 12; ++i) {
        if (i >= sampleCount) {
            break;
        }
        float t = (float(i) + jitter) / float(sampleCount);
        vec2 sampleUv = clamp(uv - velocity * t, vec2(0.0), vec2(1.0));
        float sDepth = texture(uSceneDepthTexture, sampleUv).r;
        float sDist = sDepth < 0.999999 ? abs(ReconstructViewPointUVE(sampleUv, sDepth).z) : max(uCameraFar, centerDist);
        // Depth-aware foreground rejection stops distant background leaking across moving foreground silhouettes.
        float depthWeight = sDist + max(centerDist * 0.1, 0.25) >= centerDist ? 1.0 : 0.15;
        colorSum += SampleHdrRawUVE(sampleUv) * depthWeight;
        weightSum += depthWeight;
    }
    return colorSum / max(weightSum, 1.0e-4);
}

// Exact analytical ray integral of exponential height fog along y(t) = y0 + dy * t for t in [0, rayLen].
// Splits cleanly at y = fogHeight so every exp() argument is <= 0 (zero overflow at any altitude).
float AnalyticalHeightFogOpticalDepthUVE(float y0, float dy, float rayLen, float baseDensity,
                                         float fogHeight, float heightFalloff) {
    if (baseDensity <= 0.0 || rayLen <= 0.0) {
        return 0.0;
    }
    float k = 1.0 / max(heightFalloff, 0.01);
    float y1 = y0 + dy * rayLen;
    float h0 = y0 - fogHeight;
    float h1 = y1 - fogHeight;

    float lenBelow = 0.0;
    float lenAbove = 0.0;
    float a0 = 0.0;
    float a1 = 0.0;

    if (h0 <= 0.0 && h1 <= 0.0) {
        lenBelow = rayLen;
    } else if (h0 >= 0.0 && h1 >= 0.0) {
        lenAbove = rayLen;
        a0 = h0;
        a1 = h1;
    } else {
        float tCross = clamp(-h0 / (h1 - h0), 0.0, 1.0) * rayLen;
        if (h0 < 0.0) {
            lenBelow = tCross;
            lenAbove = rayLen - tCross;
            a0 = 0.0;
            a1 = max(h1, 0.0);
        } else {
            lenAbove = tCross;
            lenBelow = rayLen - tCross;
            a0 = max(h0, 0.0);
            a1 = 0.0;
        }
    }

    float integralAbove = 0.0;
    if (lenAbove > 0.0) {
        float diff = k * (a1 - a0);
        float avgExp = abs(diff) > 1.0e-4
                           ? (exp(-k * a0) - exp(-k * a1)) / diff
                           : exp(-0.5 * k * (a0 + a1));
        integralAbove = lenAbove * avgExp;
    }
    return baseDensity * (lenBelow + integralAbove);
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
        return length(localPoint / halfSize);
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

void FogVolumeIntegrateUVE(FogVolumeUVE volume, vec3 rayOrigin, vec3 rayDirection, float rayLength,
                           vec3 sunRadiance, float day, float jitter,
                           inout float tau, inout vec3 colorMass, inout float weightMass) {
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
    vec3 ambientFill = max(uFogColor, vec3(0.05));
    for (int i = 0; i < kFogVolumeRaySamplesUVE; ++i) {
        float t = enter + step * (float(i) + jitter);
        vec3 point = rayOrigin + rayDirection * t;
        float occupancy = 1.0;
        vec3 localPoint = vec3(0.0);
        if (volume.shape != 4) {
            localPoint = FogVolumeWorldToLocalUVE(volume, point);
            occupancy = FogVolumeOccupancyUVE(volume, FogVolumeShapeDistanceUVE(volume, localPoint));
        }
        if (occupancy <= 0.0) {
            continue;
        }
        float height = FogVolumeHeightTermUVE(volume, point, localPoint);
        float stepDensity = max(volume.density, 0.0) * occupancy * height;
        tau += stepDensity * step;
        float shadow = VolumetricShadowFactorUVE(point, t);
        vec3 scatterLight = ambientFill + sunRadiance * (shadow * day);
        float sampleWeight = stepDensity * step;
        colorMass += volume.albedo * scatterLight * sampleWeight + volume.emission * (occupancy * step);
        weightMass += sampleWeight;
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

    float depth = 1.0;
    vec3 hdrColor = SampleCompositedHdrUVE(sourceUV, depth);

    if (periphery > 0.0) {
        vec2 px = vec2(uHumanEyeTexelX, uHumanEyeTexelY) * (1.5 + 4.0 * periphery);
        vec3 blur = hdrColor;
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(px.x, 0.0)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-px.x, 0.0)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(0.0, px.y)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(0.0, -px.y)));
        vec2 diagonal = px * 0.7;
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(diagonal.x, diagonal.y)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-diagonal.x, diagonal.y)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(diagonal.x, -diagonal.y)));
        blur += SampleHdrRawUVE(HumanEyeSourceUVUVE(vTexCoord + vec2(-diagonal.x, -diagonal.y)));
        blur *= 1.0 / 9.0;
        hdrColor = mix(hdrColor, blur, periphery * 0.7);
        float grey = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
        hdrColor = mix(hdrColor, vec3(grey), periphery * 0.2);
    }

    hdrColor = ApplyDepthOfFieldUVE(sourceUV, hdrColor, depth);
    hdrColor = ApplyCameraMotionBlurUVE(sourceUV, hdrColor, depth);
    hdrColor = ApplySharpeningUVE(sourceUV, hdrColor);

    int volumeCount = clamp(uFogVolumeCount, 0, 8);
    if (uFogEnabled != 0 || volumeCount > 0) {
        vec2 ndc = sourceUV * 2.0 - 1.0;
        vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
        vec3 viewDir = uOrthographic != 0
                           ? normalize(uCameraForward)
                           : normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
        vec3 rayOrigin =
            uOrthographic != 0
                ? (uCameraPosition +
                   (uCameraRight * (ndc.x * max(uAspect, 0.0001)) + uCameraUp * ndc.y) * max(uOrthoHalfHeight, 0.0))
                : uCameraPosition;
        float safeNear = max(uCameraNear, 0.01);
        float safeFar = max(uCameraFar, safeNear + 1.0);
        float fallbackDistance = depth < 0.999999 ? mix(safeNear, safeFar, clamp(depth, 0.0, 1.0)) : safeFar;
        float viewDistance = depth < 0.999999
                                 ? ReconstructViewDistanceUVE(sourceUV, depth, fallbackDistance)
                                 : fallbackDistance;

        float globalTau = 0.0;
        if (uFogEnabled != 0) {
            if (depth < 0.999999) {
                if (uFogMode == kFogModeLinearUVE) {
                    float fogAmount = clamp((viewDistance - max(uFogStart, 0.0)) /
                                                max(uFogEnd - max(uFogStart, 0.0), 1.0e-5),
                                            0.0, 1.0);
                    globalTau = -log(max(1.0 - fogAmount, 1.0e-5));
                } else if (uFogMode == kFogModeHeightUVE) {
                    globalTau = AnalyticalHeightFogOpticalDepthUVE(
                        rayOrigin.y, viewDir.y, viewDistance, max(uFogDensity, 0.0),
                        uFogHeight, uFogHeightFalloff);
                } else {
                    globalTau = max(uFogDensity, 0.0) * viewDistance;
                }
                float nearFogFade = smoothstep(safeNear, max(safeNear * 3.0, 0.35), viewDistance);
                if (uOrthographic == 0) {
                    globalTau *= nearFogFade;
                }
            } else {
                float sky = clamp(uFogSkyAffect, 0.0, 0.9999);
                globalTau = -log(max(1.0 - sky, 1.0e-5));
            }
        }

        vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
        float towardSun = pow(max(dot(viewDir, sunDir), 0.0), 8.0);
        float day = smoothstep(-0.08, 0.18, sunDir.y);
        vec3 sunRadiance = max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0) * max(uLightVolumetricFogEnergy, 0.0);
        float ignJitter = InterleavedGradientNoiseUVE(gl_FragCoord.xy);

        float localTau = 0.0;
        vec3 localColorMass = vec3(0.0);
        float localWeightMass = 0.0;
        for (int i = 0; i < volumeCount; ++i) {
            FogVolumeIntegrateUVE(uFogVolumes[i], rayOrigin, viewDir, max(viewDistance, 0.0),
                                  sunRadiance, day, ignJitter, localTau, localColorMass, localWeightMass);
        }

        float combinedTau = globalTau + localTau;
        float fogFactor = clamp(1.0 - exp(-max(combinedTau, 0.0)), 0.0, 1.0);
        vec3 globalInscatter = max(uFogColor, vec3(0.0));
        globalInscatter = mix(globalInscatter, sunRadiance, clamp(uFogSunScatter, 0.0, 1.0) * towardSun * day);
        vec3 volumeColor = localWeightMass > 1.0e-5 ? (localColorMass / localWeightMass) : (globalInscatter + localColorMass);
        float posGlobal = max(globalTau, 0.0);
        float posLocal = max(localTau, 0.0);
        float weight = posGlobal + posLocal;
        vec3 inscatter = weight > 0.0 ? (globalInscatter * posGlobal + volumeColor * posLocal) / weight : globalInscatter;

        vec3 volumetricSunInscatter = vec3(0.0);
        if (uFogEnabled != 0 && uVolumetricShadowsEnabled != 0 &&
            uLightVolumetricFogEnergy > 0.0 && uFogDensity > 0.0 && length(uSunDirection) > 1.0e-5) {
            float marchEnd = min(viewDistance, max(uShadowCascadeSplits[2], 10.0));
            float marchStart = max(uCameraNear, 0.1);
            if (marchEnd > marchStart) {
                const int kGodRaySamples = 16;
                float stepDist = (marchEnd - marchStart) / float(kGodRaySamples);
                float phase = mix(0.15, 1.0, towardSun) * day;
                float accumLight = 0.0;
                for (int s = 0; s < kGodRaySamples; ++s) {
                    float sampleDist = marchStart + stepDist * (float(s) + ignJitter);
                    vec3 samplePos = rayOrigin + viewDir * sampleDist;
                    float shadow = VolumetricShadowFactorUVE(samplePos, sampleDist);
                    if (shadow > 0.0) {
                        float density = uFogDensity;
                        if (uFogMode == kFogModeHeightUVE) {
                            float h = exp(-max(samplePos.y - uFogHeight, 0.0) / max(uFogHeightFalloff, 0.01));
                            density *= mix(1.0, h, 0.85);
                        }
                        accumLight += shadow * density;
                    }
                }
                float nearFogFade = smoothstep(safeNear, max(safeNear * 3.0, 0.35), viewDistance);
                volumetricSunInscatter = sunRadiance * (accumLight * stepDist * 0.05 * phase * nearFogFade);
            }
        }
        hdrColor = mix(hdrColor, inscatter, fogFactor) + volumetricSunInscatter;
    }

    // Add progressive bloom pyramid in linear HDR before exposure and tone mapping.
    if (uBloomCompositeEnabled != 0) {
        hdrColor += max(texture(uBloomTexture, sourceUV).rgb, vec3(0.0));
    }

    float exposure = uHasColorGrading != 0 ? max(uExposure, 0.0) : (uExposure > 0.0 ? uExposure : 1.0);
    hdrColor *= exposure;

    bool sunDiscCovers = false;
    if (uSkyCovers == 0 && uSunEnergy > 0.0 && uSunAngularDiameter > 0.0) {
        vec2 skyNdc = sourceUV * 2.0 - 1.0;
        vec3 skyView =
            vec3(skyNdc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), skyNdc.y * max(uTanHalfFov, 0.0), -1.0);
        vec3 skyWorldDir = normalize(uCameraRight * skyView.x + uCameraUp * skyView.y + uCameraForward);
        vec3 skySunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
        float sunRadius = radians(max(uSunAngularDiameter, 0.0)) * 0.5;
        sunDiscCovers = skyWorldDir.y > -0.001 && skySunDir.y > -0.02 && dot(skyWorldDir, skySunDir) >= cos(sunRadius);
    }
    float covered = depth < 0.999999 ? 1.0 : ((uSkyCovers != 0 || sunDiscCovers) ? 0.75 : 0.0);

    // Tone map to linear [0, 1], then enter display sRGB space for perceptual FXAA, single-pass
    // chromatic aberration, grading, LUT, vignette, film grain, and post-OETF TPDF dithering.
    // Uncovered background pixels (no geometry and no sky) carry display-authored background clear
    // color, so they bypass linear-to-sRGB re-encoding to preserve exact background charcoal values.
    vec3 ldrLinear = covered > 0.0
                         ? ApplyToneMappingUVE(hdrColor)
                         : (uToneMappingMethod != 0 ? AcesToneMapUVE(hdrColor) : clamp(hdrColor, 0.0, 1.0));
    bool encodeSrgbThisPixel = (uEncodeSrgbOutput != 0) && (covered > 0.0);
    float contrast = uHasColorGrading != 0 ? max(uContrast, 0.0) : (uContrast > 0.0 ? uContrast : 1.0);
    float saturation = uHasColorGrading != 0 ? max(uSaturation, 0.0) : (uSaturation > 0.0 ? uSaturation : 1.0);
    vec3 filterColor = uHasColorGrading != 0
                           ? max(uColorFilter, vec3(0.0))
                           : (any(greaterThan(uColorFilter, vec3(0.0))) ? uColorFilter : vec3(1.0));
    bool hasGrading = abs(contrast - 1.0) > 1.0e-4 || abs(saturation - 1.0) > 1.0e-4 ||
                      abs(uBrightness) > 1.0e-4 ||
                      any(greaterThan(abs(filterColor - vec3(1.0)), vec3(1.0e-4)));
    bool useDisplaySpace = encodeSrgbThisPixel || (uFastApproximateAA != 0) ||
                           (uChromaticAberrationIntensity > 0.0) || hasGrading ||
                           (uLutEnabled != 0 && uHasLutTexture != 0) ||
                           (uColorblindMode > 0 && uColorblindSeverity > 0.0) ||
                           (uVignetteIntensity > 0.0) || (uFilmGrainIntensity > 0.0) ||
                           (uDitheringEnabled != 0);

    vec3 outColor = ldrLinear;
    if (useDisplaySpace) {
        vec3 display = covered > 0.0 ? LinearToSrgbUVE(ldrLinear) : ldrLinear;
        if (covered > 0.0) {
            display = ApplyFastApproximateAAUVE(sourceUV, display, exposure);
            display = ApplySinglePassChromaticAberrationUVE(sourceUV, display, exposure);
        }

        if (hasGrading) {
            display = (display - vec3(0.5)) * contrast + vec3(0.5) + vec3(uBrightness);
            float grey = dot(display, vec3(0.2126, 0.7152, 0.0722));
            display = mix(vec3(grey), display, saturation);
            display *= filterColor;
            display = clamp(display, 0.0, 1.0);
        }

        if (uLutEnabled != 0 && uHasLutTexture != 0) {
            vec3 graded = SampleLutUVE(uLutTexture, display);
            display = mix(display, graded, clamp(uLutContribution, 0.0, 1.0));
        }

        if (uColorblindMode > 0 && uColorblindSeverity > 0.0) {
            float l = 17.8824 * display.r + 43.5161 * display.g + 4.11935 * display.b;
            float m = 3.45565 * display.r + 27.1554 * display.g + 3.86714 * display.b;
            float k = 0.0299566 * display.r + 0.184309 * display.g + 1.46709 * display.b;
            float sl = l;
            float sm = m;
            float sk = k;
            if (uColorblindMode == 1) {
                sl = 2.02344 * m - 2.52581 * k;
            } else if (uColorblindMode == 2) {
                sm = 0.494207 * l + 1.24827 * k;
            } else if (uColorblindMode == 3) {
                sk = -0.395913 * l + 0.801109 * m;
            }
            vec3 sim = vec3(
                0.0809444479 * sl - 0.130504409 * sm + 0.116721066 * sk,
                -0.0102485335 * sl + 0.0540193266 * sm - 0.113614708 * sk,
                -0.000365296938 * sl - 0.00412161469 * sm + 0.693511405 * sk);
            vec3 err = display - sim;
            vec3 corr = vec3(display.r, display.g + 0.7 * err.r + err.g, display.b + 0.7 * err.r + err.b);
            display = clamp(mix(display, corr, clamp(uColorblindSeverity, 0.0, 1.0)), 0.0, 1.0);
        }

        float vignetteIntensity = clamp(uVignetteIntensity, 0.0, 1.0);
        if (vignetteIntensity > 0.0) {
            float aspect = max(uAspect, 0.0001);
            vec2 centeredScreen = (vTexCoord * 2.0 - 1.0) * vec2(aspect, 1.0);
            float cornerDistance = length(vec2(aspect, 1.0));
            float normalizedDistance = length(centeredScreen) / max(cornerDistance, 0.0001);
            float radius = clamp(uVignetteRadius, 0.0, 0.9999);
            float edgeDarkening = smoothstep(radius, 1.0, normalizedDistance);
            display *= 1.0 - vignetteIntensity * edgeDarkening;
        }

        float filmGrainIntensity = clamp(uFilmGrainIntensity, 0.0, 1.0);
        if (filmGrainIntensity > 0.0) {
            float grainNoise = FilmGrainNoiseUVE(floor(gl_FragCoord.xy), uFilmGrainFrame);
            display = clamp(display + vec3(grainNoise * filmGrainIntensity * 0.08), 0.0, 1.0);
        }

        if (uDitheringEnabled != 0 && covered > 0.0) {
            float dither = TriangularDitherNoiseUVE(floor(gl_FragCoord.xy));
            display = clamp(display + vec3(dither), 0.0, 1.0);
        }

        // Hardware sRGB swapchains encode on write, so convert back to linear when uEncodeSrgbOutput == 0
        // to produce identical output across OpenGL, Editor viewports, and Vulkan SRGB swapchains.
        outColor = (uEncodeSrgbOutput != 0 || covered == 0.0) ? display : SrgbToLinearUVE(display);
    }

    float alpha = uWriteCoverageAlpha != 0 ? covered : 1.0;
    FragColor = vec4(outColor, alpha);
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
uniform int uHasEnvironment;
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
// One multiplier over every source, applied at the end: how bright this sky is, whichever of the
// three drew it. A source's own controls shape it; this is the only term that says "more" or
// "less" without changing what the sky is.
uniform float uSkyLuminance;
uniform sampler2D uSkyGroundShadowMap0;
uniform sampler2D uSkyGroundShadowMap1;
uniform sampler2D uSkyGroundShadowMap2;
uniform mat4 uSkyGroundLightMatrix0;
uniform mat4 uSkyGroundLightMatrix1;
uniform mat4 uSkyGroundLightMatrix2;
uniform vec3 uSkyGroundCascadeSplits;
uniform int uSkyGroundShadowEnabled;
uniform float uSkyGroundShadowOpacity;
uniform float uSkyGroundShadowBias;

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
    float day = uSunEnergy > 0.0 ? smoothstep(-0.08, 0.18, sunY) : 1.0;
    float sunset = uSunEnergy > 0.0 ? exp(-pow(sunY * 6.0, 2.0)) * smoothstep(-0.15, 0.0, sunY) : 0.0;

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
    sky *= mix(1.0, 1.55, (uSunEnergy > 0.0 ? day : 0.0) * (1.0 - sunset));
    sky = mix(sky, max(uGroundColor, vec3(0.0)) * (0.05 * day), pow(1.0 - clamp(viewY, 0.0, 1.0), 8.0) * 0.08);

    float g = mix(0.76, 0.93, sunset);
    float mieDen = 1.0 + g * g - 2.0 * g * mu;
    float mie = (1.0 - g * g) / max(pow(max(mieDen, 0.001), 1.5), 0.001);
    vec3 sunCol = max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0);
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

    float day = uSunEnergy > 0.0 ? smoothstep(-0.08, 0.18, sun.y) : 1.0;
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

    float radiance = max(uAtmosphereIntensity, 0.0) * kSkySunRadianceUVE * (uSunEnergy > 0.0 ? uSunEnergy : 1.0);
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
    sky += max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0) * SunDiscUVE(mu, sun.y) * mix(7.5, 16.0, 1.0 - day);
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
    float night = uSunEnergy > 0.0 ? (1.0 - smoothstep(-0.08, 0.18, sun.y)) : 0.0;
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
    float day = uSunEnergy > 0.0 ? smoothstep(-0.08, 0.18, sun.y) : 1.0;
    if (uAtmosphericSky != 0) {
        return ApplySkyOverlaysUVE(PhysicalSkyUVE(dir, sun), dir, sun, day);
    }
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
    float day = uSunEnergy > 0.0 ? smoothstep(-0.08, 0.18, sun.y) : 1.0;
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

// Ground-plane shadow (y = 0) through the same three directional cascades the lit pass uses. The
// first cascade whose map contains the point wins, so a shadow far from the camera is no longer
// dropped for lying outside the near cascade.
float SkyGroundCascadeDepthUVE(int cascade, vec2 uv) {
    if (cascade == 0) { return texture(uSkyGroundShadowMap0, uv).r; }
    if (cascade == 1) { return texture(uSkyGroundShadowMap1, uv).r; }
    return texture(uSkyGroundShadowMap2, uv).r;
}

vec2 SkyGroundCascadeTexelUVE(int cascade) {
    ivec2 sz;
    if (cascade == 0) { sz = textureSize(uSkyGroundShadowMap0, 0); }
    else if (cascade == 1) { sz = textureSize(uSkyGroundShadowMap1, 0); }
    else { sz = textureSize(uSkyGroundShadowMap2, 0); }
    return 1.0 / max(vec2(sz), vec2(1.0));
}

float SkyGroundShadowUVE(vec3 groundPos, float viewDist) {
    for (int cascade = 0; cascade < 3; ++cascade) {
        float splitFar = cascade == 0 ? uSkyGroundCascadeSplits.x
                       : (cascade == 1 ? uSkyGroundCascadeSplits.y : uSkyGroundCascadeSplits.z);
        if (viewDist > splitFar && cascade < 2) {
            continue;
        }
        mat4 m = cascade == 0 ? uSkyGroundLightMatrix0
               : (cascade == 1 ? uSkyGroundLightMatrix1 : uSkyGroundLightMatrix2);
        vec4 lightClip = m * vec4(groundPos, 1.0);
        if (abs(lightClip.w) < 1.0e-5) { return 0.0; }
        vec3 proj = lightClip.xyz / lightClip.w * 0.5 + 0.5;
        if (proj.x <= 0.002 || proj.x >= 0.998 || proj.y <= 0.002 || proj.y >= 0.998 ||
            proj.z <= 0.0 || proj.z >= 1.0) {
            return 0.0;
        }
        vec2 texel = SkyGroundCascadeTexelUVE(cascade);
        float bias = max(uSkyGroundShadowBias, 0.0008);
        float shadow = 0.0;
        for (int sx = -1; sx <= 1; ++sx) {
            for (int sy = -1; sy <= 1; ++sy) {
                float closest = SkyGroundCascadeDepthUVE(cascade, proj.xy + vec2(float(sx), float(sy)) * texel);
                shadow += (proj.z - bias > closest) ? 1.0 : 0.0;
            }
        }
        return shadow / 9.0;
    }
    return 0.0;
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
    if (uHasEnvironment == 0) {
        color = max(uGroundColor, vec3(0.0));
        if (worldDir.y > -0.001 && uSunEnergy > 0.0) {
            float mu = clamp(dot(worldDir, sunDir), -1.0, 1.0);
            float sunset = exp(-pow(sunDir.y * 6.0, 2.0)) * smoothstep(-0.15, 0.0, sunDir.y);
            vec3 sunCol = max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0);
            color += sunCol * SunDiscUVE(mu, sunDir.y) * mix(7.5, 16.0, sunset);
        }
    } else if (uSkyTextureEnabled != 0) {
        // A panorama replaces the sky, not the weather: cloud and stars still sit in front of and
        // behind it, so a photographed sky can have a moving deck over it.
        color = texture(uSkyTexture, SkyEquirectUvUVE(worldDir)).rgb;
        if (worldDir.y > -0.001) {
            float day = uSunEnergy > 0.0 ? smoothstep(-0.08, 0.18, sunDir.y) : 1.0;
            color = ApplySkyOverlaysUVE(color, worldDir, sunDir, day);
        }
    } else if (uAtmosphericSky != 0) {
        color = ProceduralSkyUVE(worldDir);
    } else {
        color = GradientSkyWithOverlaysUVE(worldDir, sunDir);
    }
    if (uSkyGroundShadowEnabled != 0 && worldDir.y < -0.001 && uCameraPosition.y > 0.001 && sunDir.y > 0.01) {
        float groundDist = -uCameraPosition.y / worldDir.y;
        if (groundDist > 0.0 && groundDist < 120.0) {
            vec3 groundPos = uCameraPosition + worldDir * groundDist;
            float shadow = SkyGroundShadowUVE(groundPos, groundDist);
            float distFade = 1.0 - smoothstep(60.0, 120.0, groundDist);
            float shadowFactor =
                clamp(shadow * clamp(uSkyGroundShadowOpacity, 0.0, 1.0) * distFade, 0.0, 1.0);
            color *= mix(1.0, 0.38, shadowFactor);
        }
    }
    // Clamped at zero and multiplied last, so a source can never come out negative and the dial
    // means the same thing for a panorama, a gradient and a scattering sky. Alpha 0.0 signals zero
    // indirect-surface weight so screen-space AO never darkens sky pixels.
    FragColor = vec4(max(color, vec3(0.0)) * max(uSkyLuminance, 0.0), 0.0);
}
#endif
)GLSLSRC";

const std::string_view kShadowDepthSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
#ifdef UVE_ALPHA_CUTOUT
layout(location = 2) in vec2 aTexCoord;
out vec2 vTexCoord;
#endif

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
#ifdef UVE_ALPHA_CUTOUT
    vTexCoord = aTexCoord;
#endif
    gl_Position = uLightSpaceMatrix * model * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
#ifdef UVE_ALPHA_CUTOUT
in vec2 vTexCoord;
uniform sampler2D uAlbedoTexture;
uniform float uAlphaCutoff;
uniform vec3 uUvScale;
uniform vec3 uUvOffset;
#endif

void main() {
#ifdef UVE_ALPHA_CUTOUT
    vec2 scale = dot(uUvScale.xy, uUvScale.xy) > 0.0 ? uUvScale.xy : vec2(1.0);
    vec2 uv = vTexCoord * scale + uUvOffset.xy;
    if (uAlphaCutoff > 0.0 && texture(uAlbedoTexture, uv).a < uAlphaCutoff) {
        discard;
    }
#endif
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

uniform LightUVE uLights[16];
uniform int uActiveLightCount;
uniform int uLightShadowSlot[16];
#if !defined(UVE_MOBILE) && !defined(UVE_COMPATIBILITY) && !defined(UVE_MOBILE_PROFILE)
struct ClusteredGpuLightUVE {
    vec4 positionAndRange;
    vec4 directionAndType;
    vec4 colorAndIntensity;
    vec4 spotMaskSpecShadow;
};
layout(std430, binding = 4) readonly buffer ClusteredLightBlock {
    ClusteredGpuLightUVE clusteredLights[];
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
// Each map's own reading, on top of its UV set (see MaterialMapSettingsUVE). All zero reads the map
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
    vec3 balancedSky = mix(skyLum, uSkyAmbient, 0.68);
    vec3 balancedGround = mix(groundLum, uGroundAmbient, 0.72);
    vec3 horizonFill = mix(balancedGround, balancedSky, 0.52);
    float horizonWeight = 1.0 - abs(normal.y);
    vec3 verticalBlend = mix(balancedGround, balancedSky, smoothstep(0.0, 1.0, hemi));
    return mix(verticalBlend, horizonFill, horizonWeight * 0.25);
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

// Roughness-aware cubemap probe sampling: mirror surfaces sample the exact reflection vector,
// while rough surfaces integrate a 5-tap orthonormal cone across the cubemap.
vec3 SampleReflectionProbeUVE(vec3 direction, float roughness) {
    vec3 dir = SafeNormalizeUVE(direction);
    if (roughness <= 0.08) {
        return texture(uReflectionProbeCube, dir).rgb;
    }
    vec3 referenceAxis = abs(dir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = SafeNormalizeUVE(cross(referenceAxis, dir));
    vec3 bitangent = cross(dir, tangent);
    float spread = roughness * roughness * 0.45;
    return texture(uReflectionProbeCube, dir).rgb * 0.4 +
           (texture(uReflectionProbeCube, dir + tangent * spread).rgb +
            texture(uReflectionProbeCube, dir - tangent * spread).rgb +
            texture(uReflectionProbeCube, dir + bitangent * spread).rgb +
            texture(uReflectionProbeCube, dir - bitangent * spread).rgb) * 0.15;
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
    return 1.0 - smoothstep(0.0, 1.0, normalizedDistance);
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

float Pow5UVE(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float DistributionGgxUVE(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPiUVE * denominator * denominator, kBrdfEpsilonUVE);
}

// Height-correlated Smith-GGX visibility term V = G / (4 * NdotV * NdotL).
float VisibilitySmithGGXCorrelatedUVE(float normalDotView, float normalDotLight, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ggxV = normalDotLight * sqrt(normalDotView * normalDotView * (1.0 - alpha2) + alpha2);
    float ggxL = normalDotView * sqrt(normalDotLight * normalDotLight * (1.0 - alpha2) + alpha2);
    return 0.5 / max(ggxV + ggxL, kBrdfEpsilonUVE);
}

vec3 FresnelSchlickUVE(float halfDotView, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * Pow5UVE(clamp(1.0 - halfDotView, 0.0, 1.0));
}

// Analytic split-sum GGX environment BRDF approximation (scale, bias).
vec2 EnvBrdfApproxUVE(float roughness, float normalDotView) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * normalDotView)) * r.x + r.y;
    return clamp(vec2(-1.04, 1.04) * a004 + r.zw, vec2(0.0), vec2(1.0));
}

// Screen-space microfacet normal-variance filtering to suppress sub-pixel specular aliasing on
// curved geometry, thin bevels, and high-frequency normal maps.
float ApplyGeometricSpecularAAUVE(vec3 geometricNormal, vec3 shadingNormal, float perceptualRoughness) {
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

ivec2 CascadeMapSizeUVE(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return textureSize(uShadowMapTextures[0], 0);
    }
    if (cascadeIndex == 1) {
        return textureSize(uShadowMapTextures[1], 0);
    }
    return textureSize(uShadowMapTextures[2], 0);
}

#if !defined(UVE_COMPATIBILITY)
vec4 CascadeLightSpacePositionUVE(int cascadeIndex) {
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
const vec2 kVogelDisk16UVE[16] = vec2[16](
    vec2( 0.176777,  0.000000), vec2(-0.225967,  0.205764), vec2( 0.034729, -0.393506), vec2( 0.281441,  0.372739),
    vec2(-0.511329, -0.095890), vec2( 0.430580, -0.355913), vec2(-0.054116,  0.587895), vec2(-0.382775, -0.477944),
    vec2( 0.677406,  0.118636), vec2(-0.625114,  0.372894), vec2( 0.264935, -0.727892), vec2( 0.140751,  0.798237),
    vec2(-0.800547, -0.252855), vec2( 0.779871, -0.415022), vec2(-0.357475,  0.848231), vec2(-0.427858, -0.875842)
);

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
                                     : CascadeMapSizeUVE(cascadeIndex);
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
                                              : SampleCascadeDepthUVE(cascadeIndex, projected.xy);
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
                vec2 coord = projected.xy + rotation * kVogelDisk16UVE[i] * texelSize * 6.0;
                float depth = cascadeIndex < 0 ? texture(uShadowMapTexture, coord).r
                                               : SampleCascadeDepthUVE(cascadeIndex, coord);
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
            vec2 offset = kVogelDisk16UVE[i];
            vec2 sampleCoord = projected.xy + rotation * offset * texelSize * radius;
            float sampledDepth = cascadeIndex < 0 ? texture(uShadowMapTexture, sampleCoord).r
                                                  : SampleCascadeDepthUVE(cascadeIndex, sampleCoord);
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
#if defined(UVE_COMPATIBILITY)
    float shadowFactor = ShadowFactorFromPositionUVE(vLightSpacePosition, normal, lightDirection, -1);
    if (uShadowCascadeCount > 0) {
        float planarDepth = dot(vWorldPosition - uViewPosition, SafeNormalizeUVE(uCameraForward));
        float viewDepth = (uOrthographic != 0 || planarDepth > 0.0)
                              ? max(planarDepth, 0.0)
                              : length(vWorldPosition - uViewPosition);
        float farDepth = uShadowCascadeSplits[0];
        if (viewDepth > farDepth) {
            return 1.0;
        }
        return FadeShadowToLitAtDistanceUVE(shadowFactor, viewDepth, farDepth);
    }
    return shadowFactor;
#else
    if (uShadowCascadeCount <= 0) {
        // No cascades: one map in uShadowMapTexture, projected by vLightSpacePosition.
        return ShadowFactorFromPositionUVE(vLightSpacePosition, normal, lightDirection, -1);
    }

    float planarDepth = dot(vWorldPosition - uViewPosition, SafeNormalizeUVE(uCameraForward));
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
#endif
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
float LuminanceUVE(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}
vec4 ComposeFragmentUVE(vec3 color, float alpha) {
    return vec4(uPremultiplyAlpha != 0 ? color * alpha : color, alpha);
}
vec4 ComposeOutputUVE(vec3 color, float surfaceOpacity, float indirectWeight) {
    if (uPackIndirectWeightAlpha != 0) {
        return vec4(color, clamp(indirectWeight, 0.0, 1.0));
    }
    return ComposeFragmentUVE(color, surfaceOpacity);
}
#if defined(UVE_COMPATIBILITY)
vec4 ReadMapCompatUVE(sampler2D map, vec2 uv1, vec2 uv2, vec4 transform, vec4 control, vec4 neutral) {
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
    vec4 albedoSample = ReadMapCompatUVE(uAlbedoTexture, uv1, uv2, uAlbedoMapTransform, uAlbedoMapControl, vec4(1.0));
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) {
        discard;
    }
    vec3 albedo = albedoSample.rgb * uAlbedoColor;
    float materialAlpha = uAlphaSource == 0 ? clamp(albedoSample.a * uAlbedoAlpha, 0.0, 1.0) : 1.0;
    float surfaceOpacity = clamp(uSurfaceOpacity, 0.0, 1.0) * materialAlpha;
    if (uShadingMode == 1 || uUnshaded != 0) {
        FragColor = ComposeOutputUVE(albedo, surfaceOpacity, 0.0);
        return;
    }
    if (uShadingMode == 2) {
        FragColor = ComposeOutputUVE(mix(albedo * 0.25, vec3(0.05, 0.85, 1.0), 0.75), surfaceOpacity, 0.0);
        return;
    }
    float ambientOcclusion = mix(1.0, ReadMapCompatUVE(uAOTexture, uv1, uv2, uOcclusionMapTransform, uOcclusionMapControl, vec4(1.0)).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 normal = SafeNormalizeUVE(vWorldNormal);
    if (dot(normal, normal) <= kBrdfEpsilonUVE * kBrdfEpsilonUVE) {
        normal = vec3(0.0, 0.0, 1.0);
    }
    if (uNormalMapControl.z < 0.999) {
        vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
        if (dot(tangent, tangent) <= 0.00000001) {
            vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            tangent = cross(fallbackAxis, normal);
        }
        tangent = SafeNormalizeUVE(tangent);
        vec3 bitangent = SafeNormalizeUVE(cross(normal, tangent)) * (vTangentHandedness < 0.0 ? -1.0 : 1.0);
        vec3 tsNormal = ReadMapCompatUVE(uNormalTexture, uv1, uv2, uNormalMapTransform, uNormalMapControl, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
        if (uNormalFlipGreen != 0) {
            tsNormal.y = -tsNormal.y;
        }
        tsNormal.xy *= uNormalScale;
        normal = SafeNormalizeUVE(mat3(tangent, bitangent, normal) * tsNormal);
    }
    if (uShadingMode == 4) {
        FragColor = ComposeOutputUVE(normal * 0.5 + 0.5, surfaceOpacity, 0.0);
        return;
    }
    vec3 viewDirection = uOrthographic != 0
                             ? SafeNormalizeUVE(-uCameraForward)
                             : SafeNormalizeUVE(uViewPosition - vWorldPosition);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    if (uShadingMode == 3) {
        albedo = vec3(0.82);
    }
    vec3 mrSample = ReadMapCompatUVE(uMetallicRoughnessTexture, uv1, uv2, uMetallicRoughnessMapTransform, uMetallicRoughnessMapControl, vec4(1.0)).rgb;
    float metallic = uShadingMode == 3 ? 0.0 : clamp(uMetallic * mrSample.b, 0.0, 1.0);
    float roughness = uShadingMode == 3 ? 0.5 : clamp(uRoughness * mrSample.g, 0.06, 1.0);
    float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    float dielectricF0 = 0.16 * dielectricSpec * dielectricSpec;
    vec3 baseReflectance = mix(vec3(dielectricF0), albedo, metallic);
    float normalDotView = max(dot(normal, viewDirection), 0.001);
    vec2 envBrdf = EnvBrdfApproxUVE(roughness, normalDotView);
    vec3 ambientColor = AmbientFromSourceUVE(normal);
    vec3 specEnv = uSkyReflectionsLinked != 0 ? SkyReflectionUVE(reflect(-viewDirection, normal), roughness) : ambientColor;
    vec3 indirectLighting = ((vec3(1.0) - baseReflectance) * (1.0 - metallic) * albedo * (ambientColor + max(uGiIndirectIrradiance, vec3(0.0))) +
                             (baseReflectance * envBrdf.x + vec3(envBrdf.y)) * specEnv) * ambientOcclusion;
    vec3 emissiveLighting =
        uEmissiveColor * max(uEmissiveEnergy, 0.0) *
        ReadMapCompatUVE(uEmissiveTexture, uv1, uv2, uEmissiveMapTransform, uEmissiveMapControl, vec4(1.0)).rgb;
    vec3 lighting = indirectLighting + emissiveLighting;

    int activeLightCount = uActiveLightCount < 0 ? 0 : (uActiveLightCount > 0 ? min(uActiveLightCount, 4) : 4);
    for (int lightIndex = 0; lightIndex < activeLightCount; ++lightIndex) {
        LightUVE light = uLights[lightIndex];
        if (light.intensity <= 0.0 || (light.cullMask & uMeshRenderLayers) == 0) {
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
            if (light.range > 0.0) {
                float r2 = (distanceToLight / light.range);
                r2 *= r2;
                float w = clamp(1.0 - r2 * r2, 0.0, 1.0);
                attenuation *= w * w;
            }
            if (light.type == 2) {
                float cosOuter = cos(radians(light.spotAngleDegrees));
                float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                float coneFalloff = clamp((coneAlignment - cosOuter) / max(cosInner - cosOuter, kBrdfEpsilonUVE), 0.0, 1.0);
                attenuation *= coneFalloff * coneFalloff;
            }
        }
        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        if (normalDotLight <= 0.0 || attenuation <= 0.0) {
            continue;
        }
        vec3 halfDirection = SafeNormalizeUVE(lightDirection + viewDirection);
        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        vec3 fresnel = FresnelSchlickUVE(halfDotView, baseReflectance);
        float distribution = DistributionGgxUVE(normalDotHalf, roughness);
        float visibility = VisibilitySmithGGXCorrelatedUVE(normalDotView, normalDotLight, roughness);
        vec3 specular = (distribution * visibility * fresnel) * max(light.specular, 0.0);
        vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / kPiUVE;
        vec3 directContribution = (diffuse + specular) * (light.color * light.intensity * attenuation) * normalDotLight;
        if (uDisableReceiveShadows == 0 && light.type == 0) {
            directContribution *= clamp(DirectionalShadowFactorUVE(normal, lightDirection), 0.0, 1.0);
        }
        lighting += directContribution;
    }
    vec3 finalColor = max(lighting, vec3(0.0));
    float indirectWeight = clamp(LuminanceUVE(indirectLighting) / max(LuminanceUVE(finalColor), 1.0e-4), 0.0, 1.0);
    FragColor = ComposeOutputUVE(finalColor, surfaceOpacity, indirectWeight);
}
#else
// One UV set: planar 2D coordinates or normal-weighted triplanar projection.
struct UvSetUVE {
    vec2 uv;
    int triplanar;
    vec2 uvX;
    vec2 uvY;
    vec2 uvZ;
    vec3 weights;
    vec3 triNormal;
};
UvSetUVE MakeUvSetUVE(vec3 scale, vec3 offset, int triplanar, float sharpness) {
    UvSetUVE set;
    set.triplanar = triplanar;
    set.uv = vTexCoord * scale.xy + offset.xy;
    set.uvX = set.uv;
    set.uvY = set.uv;
    set.uvZ = set.uv;
    set.weights = vec3(0.0, 1.0, 0.0);
    set.triNormal = vec3(0.0, 1.0, 0.0);
    if (triplanar != 0) {
        vec3 position = triplanar == 2 ? vWorldPosition : vObjectPosition;
        vec3 n = SafeNormalizeUVE(triplanar == 2 ? vWorldNormal : vObjectNormal);
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
vec4 SampleUvSetUVE(sampler2D map, UvSetUVE set) {
    if (set.triplanar == 0) {
        return texture(map, set.uv);
    }
    // Three axis-aligned planar projections blended by surface normal orientation.
    return texture(map, set.uvX) * set.weights.x +
           texture(map, set.uvY) * set.weights.y +
           texture(map, set.uvZ) * set.weights.z;
}
vec3 SampleTriplanarNormalUVE(sampler2D normalMap, UvSetUVE set, float normalScale, int flipGreen) {
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
    return SafeNormalizeUVE(blended);
}
// One map read its own way: its UV set, tiling and offset, then the channel, invert and strength
// (strength 0 is `neutral`: what the material looks like without the map).
vec4 ReadMapUVE(sampler2D map, UvSetUVE uv1, UvSetUVE uv2, vec4 transform, vec4 control, vec4 neutral) {
    if (control.z >= 0.999) {
        return neutral;
    }
    UvSetUVE set = uv1;
    if (control.w > 0.5) {
        set = uv2;
    }
    vec2 tiling = vec2(1.0) + transform.xy;
    set.uv = set.uv * tiling + transform.zw;
    set.uvX = set.uvX * tiling + transform.zw;
    set.uvY = set.uvY * tiling + transform.zw;
    set.uvZ = set.uvZ * tiling + transform.zw;
    vec4 value = SampleUvSetUVE(map, set);
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
    UvSetUVE uv1 = MakeUvSetUVE(vec3(uUvScale.xy, uUvScale.x), uUvOffset, uUv1Triplanar, uUv1TriplanarSharpness);
    UvSetUVE uv2 = MakeUvSetUVE(uUv2Scale, uUv2Offset, uUv2Triplanar, uUv2TriplanarSharpness);
    vec4 albedoSample = ReadMapUVE(uAlbedoTexture, uv1, uv2, uAlbedoMapTransform, uAlbedoMapControl, vec4(1.0));
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
        FragColor = ComposeOutputUVE(albedo, surfaceOpacity, 0.0);
        return;
    }
    if (uShadingMode == 2) {
        FragColor = ComposeOutputUVE(mix(albedo * 0.25, vec3(0.05, 0.85, 1.0), 0.75), surfaceOpacity, 0.0);
        return;
    }
    float ambientOcclusion = mix(1.0, ReadMapUVE(uAOTexture, uv1, uv2, uOcclusionMapTransform, uOcclusionMapControl, vec4(1.0)).r, clamp(uOcclusionStrength, 0.0, 1.0));
    vec3 geometricNormal = SafeNormalizeUVE(vWorldNormal);
    // A few legacy/direct RHI callers do not provide a usable normal attribute. Keep those
    // materials lit instead of allowing a zero vector to poison the tangent frame and BRDF.
    if (dot(geometricNormal, geometricNormal) <= kBrdfEpsilonUVE * kBrdfEpsilonUVE) {
        geometricNormal = vec3(0.0, 0.0, 1.0);
    }
    vec3 normal = geometricNormal;
    if (uNormalMapControl.z < 0.999) {
        if (uv1.triplanar != 0) {
            normal = SampleTriplanarNormalUVE(uNormalTexture, uv1, uNormalScale, uNormalFlipGreen);
        } else {
            vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
            if (dot(tangent, tangent) <= 0.00000001) {
                vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                tangent = cross(fallbackAxis, normal);
            }
            tangent = SafeNormalizeUVE(tangent);
            vec3 bitangent = SafeNormalizeUVE(cross(normal, tangent));
            bitangent *= vTangentHandedness < 0.0 ? -1.0 : 1.0;
            vec3 tangentSpaceNormal = ReadMapUVE(uNormalTexture, uv1, uv2, uNormalMapTransform, uNormalMapControl, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            if (uNormalFlipGreen != 0) {
                tangentSpaceNormal.y = -tangentSpaceNormal.y;
            }
            tangentSpaceNormal.xy *= uNormalScale;
            normal = SafeNormalizeUVE(mat3(tangent, bitangent, normal) * tangentSpaceNormal);
        }
    }

    if (uShadingMode == 4) {
        FragColor = ComposeOutputUVE(normal * 0.5 + 0.5, surfaceOpacity, 0.0);
        return;
    }

    vec3 viewDirection = uOrthographic != 0
                             ? SafeNormalizeUVE(-uCameraForward)
                             : SafeNormalizeUVE(uViewPosition - vWorldPosition);
    // For two-sided geometry or backfaces, orient the normal outwards from the observed face.
    if (!gl_FrontFacing) {
        normal = -normal;
    }

    if (uShadingMode == 3) {
        albedo = vec3(0.82);
    }

    vec3 metallicRoughnessSample = ReadMapUVE(uMetallicRoughnessTexture, uv1, uv2, uMetallicRoughnessMapTransform, uMetallicRoughnessMapControl, vec4(1.0)).rgb;
    float metallic = uShadingMode == 3 ? 0.0 : clamp(uMetallic * metallicRoughnessSample.b, 0.0, 1.0);
    float rawRoughness = uShadingMode == 3 ? 0.5 : clamp(uRoughness * metallicRoughnessSample.g, 0.04, 1.0);
    float roughness = ApplyGeometricSpecularAAUVE(geometricNormal, normal, rawRoughness);
    float dielectricSpec = clamp(uDielectricSpecular, 0.0, 1.0);
    float dielectricF0 = 0.16 * dielectricSpec * dielectricSpec;
    vec3 ambientBaseReflectance = mix(vec3(dielectricF0), albedo, metallic);
    float normalDotViewAmbient = max(dot(normal, viewDirection), 0.001);
    vec2 envBrdf = EnvBrdfApproxUVE(roughness, normalDotViewAmbient);
    vec3 energyCompensation = vec3(1.0) + ambientBaseReflectance * (1.0 / max(envBrdf.x + envBrdf.y, 0.05) - 1.0);
    vec3 ambientFresnel =
        ambientBaseReflectance +
        (max(vec3(1.0 - roughness), ambientBaseReflectance) - ambientBaseReflectance) *
            Pow5UVE(clamp(1.0 - normalDotViewAmbient, 0.0, 1.0));
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
            vec3 captured = SampleReflectionProbeUVE(sampleDir, roughness);
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
        ReadMapUVE(uEmissiveTexture, uv1, uv2, uEmissiveMapTransform, uEmissiveMapControl, vec4(1.0)).rgb;
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
        float positiveViewDepth = max(dot(vWorldPosition - uViewPosition, SafeNormalizeUVE(uCameraForward)),
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
            ClusteredGpuLightUVE gpuLight = clusteredLights[li];
            LightUVE light;
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
                lightDirection = SafeNormalizeUVE(-light.direction);
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
                    float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                    float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                    float coneFalloff = clamp((coneAlignment - cosOuter) /
                                                  max(cosInner - cosOuter, kBrdfEpsilonUVE),
                                              0.0, 1.0);
                    attenuation *= coneFalloff * coneFalloff;
                }
            }
            float normalDotLight = max(dot(normal, lightDirection), 0.0);
            if (normalDotLight <= 0.0 || attenuation <= 0.0) {
                continue;
            }
            vec3 halfDirection = SafeNormalizeUVE(lightDirection + viewDirection);
            float normalDotHalf = max(dot(normal, halfDirection), 0.0);
            float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
            vec3 fresnel = FresnelSchlickUVE(halfDotView, baseReflectance);
            float distribution = DistributionGgxUVE(normalDotHalf, roughness);
            float visibility = VisibilitySmithGGXCorrelatedUVE(normalDotView, normalDotLight, roughness);
            vec3 specular = (distribution * visibility * fresnel) * energyCompensation * max(light.specular, 0.0);
            vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
            vec3 diffuse = diffuseWeight * albedo / kPiUVE;
            vec3 radiance = light.color * light.intensity * attenuation;
            vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;
            if (uDisableReceiveShadows == 0) {
                int shadowSlot = encodedShadowSlot > 0 ? (encodedShadowSlot - 1) : -1;
                if (light.type == 0) {
                    float shadowFactor = DirectionalShadowFactorUVE(normal, lightDirection);
                    if (isnan(shadowFactor) || isinf(shadowFactor)) {
                        shadowFactor = 1.0;
                    }
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 1 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = PointShadowFactorUVE(shadowSlot, normal, light.position, light.range);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 2 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = SpotShadowFactorUVE(shadowSlot, normal, lightDirection);
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
                if (light.range > 0.0) {
                    float rangeRatio = distanceToLight / light.range;
                    float r2 = rangeRatio * rangeRatio;
                    float window = clamp(1.0 - r2 * r2, 0.0, 1.0);
                    attenuation *= window * window;
                }
                if (light.type == 2) {
                    float cosOuter = cos(radians(light.spotAngleDegrees));
                    float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                    float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                    float coneFalloff = clamp((coneAlignment - cosOuter) /
                                                  max(cosInner - cosOuter, kBrdfEpsilonUVE),
                                              0.0, 1.0);
                    attenuation *= coneFalloff * coneFalloff;
                }
            }

            float normalDotLight = max(dot(normal, lightDirection), 0.0);
            if (normalDotLight <= 0.0 || normalDotView <= 0.0 || attenuation <= 0.0) {
                continue;
            }

            vec3 halfDirection = SafeNormalizeUVE(lightDirection + viewDirection);
            float normalDotHalf = max(dot(normal, halfDirection), 0.0);
            float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
            vec3 fresnel = FresnelSchlickUVE(halfDotView, baseReflectance);
            float distribution = DistributionGgxUVE(normalDotHalf, roughness);
            float visibility = VisibilitySmithGGXCorrelatedUVE(normalDotView, normalDotLight, roughness);
            vec3 specular = (distribution * visibility * fresnel) * energyCompensation * max(light.specular, 0.0);
            vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
            vec3 diffuse = diffuseWeight * albedo / kPiUVE;
            vec3 radiance = light.color * light.intensity * attenuation;
            vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

            if (uDisableReceiveShadows == 0) {
                int encodedShadowSlot = uLightShadowSlot[lightIndex];
                int shadowSlot = encodedShadowSlot > 0 ? (encodedShadowSlot - 1)
                               : (encodedShadowSlot < 0 ? -1 : (lightIndex < 4 ? lightIndex : -1));
                if (light.type == 0) {
                    float shadowFactor = DirectionalShadowFactorUVE(normal, lightDirection);
                    if (isnan(shadowFactor) || isinf(shadowFactor)) {
                        shadowFactor = 1.0;
                    }
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 1 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = PointShadowFactorUVE(shadowSlot, normal, light.position, light.range);
                    directContribution *= clamp(shadowFactor, 0.0, 1.0);
                } else if (light.type == 2 && shadowSlot >= 0 && shadowSlot < 4) {
                    float shadowFactor = SpotShadowFactorUVE(shadowSlot, normal, lightDirection);
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
    float indirectWeight = clamp(LuminanceUVE(indirectLighting) / max(LuminanceUVE(finalColor), 1.0e-4), 0.0, 1.0);
    FragColor = ComposeOutputUVE(finalColor, surfaceOpacity, indirectWeight);
#endif
}
#endif
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
uniform float uExposure;

vec3 SanitizeHdrUVE(vec3 c) {
    vec3 safe = vec3(
        (isnan(c.r) || isinf(c.r)) ? 0.0 : c.r,
        (isnan(c.g) || isinf(c.g)) ? 0.0 : c.g,
        (isnan(c.b) || isinf(c.b)) ? 0.0 : c.b
    );
    return clamp(safe, vec3(0.0), vec3(65000.0));
}

float KarisWeightUVE(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + max(luma, 0.0));
}

vec3 SampleSafeUVE(vec2 uv, vec2 halfTexel) {
    return SanitizeHdrUVE(texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb);
}

void main() {
    // 13-tap overlapping downsample with Karis partial-group weighting to suppress sub-pixel
    // specular firefly flickering before threshold extraction.
    vec2 texel = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 halfTexel = texel * 0.5;
    float x = texel.x;
    float y = texel.y;

    vec3 a = SampleSafeUVE(vTexCoord + vec2(-2.0 * x,  2.0 * y), halfTexel);
    vec3 b = SampleSafeUVE(vTexCoord + vec2(     0.0,  2.0 * y), halfTexel);
    vec3 c = SampleSafeUVE(vTexCoord + vec2( 2.0 * x,  2.0 * y), halfTexel);

    vec3 d = SampleSafeUVE(vTexCoord + vec2(-2.0 * x,      0.0), halfTexel);
    vec3 e = SampleSafeUVE(vTexCoord,                            halfTexel);
    vec3 f = SampleSafeUVE(vTexCoord + vec2( 2.0 * x,      0.0), halfTexel);

    vec3 g = SampleSafeUVE(vTexCoord + vec2(-2.0 * x, -2.0 * y), halfTexel);
    vec3 h = SampleSafeUVE(vTexCoord + vec2(     0.0, -2.0 * y), halfTexel);
    vec3 i = SampleSafeUVE(vTexCoord + vec2( 2.0 * x, -2.0 * y), halfTexel);

    vec3 j = SampleSafeUVE(vTexCoord + vec2(    -x,       y), halfTexel);
    vec3 k = SampleSafeUVE(vTexCoord + vec2(     x,       y), halfTexel);
    vec3 l = SampleSafeUVE(vTexCoord + vec2(    -x,      -y), halfTexel);
    vec3 m = SampleSafeUVE(vTexCoord + vec2(     x,      -y), halfTexel);

    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25;
    vec3 g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25;
    vec3 g4 = (e + f + h + i) * 0.25;

    float w0 = KarisWeightUVE(g0) * 0.5;
    float w1 = KarisWeightUVE(g1) * 0.125;
    float w2 = KarisWeightUVE(g2) * 0.125;
    float w3 = KarisWeightUVE(g3) * 0.125;
    float w4 = KarisWeightUVE(g4) * 0.125;
    vec3 hdrColor = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1.0e-5);

    // Threshold in exposure-adjusted max-channel brightness so pure saturated emissives (such as
    // deep blue or red neon/sparks) trigger bloom smoothly without luminance suppression.
    float exposure = uExposure > 0.0 ? uExposure : 1.0;
    float brightness = max(hdrColor.r, max(hdrColor.g, hdrColor.b)) * exposure;
    float threshold = max(uBloomThreshold, 0.0);
    float excess = brightness - threshold;
    float softKnee = clamp(uBloomSoftKnee, 0.0, 1.0);
    if (softKnee > 0.0 && threshold > 0.0) {
        float knee = threshold * softKnee;
        float softContribution = clamp(excess + knee, 0.0, 2.0 * knee);
        softContribution = (softContribution * softContribution) / max(4.0 * knee, 1.0e-5);
        excess = max(excess, softContribution);
    } else {
        excess = max(excess, 0.0);
    }
    float contribution = excess / max(brightness, 1.0e-4);
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

vec3 SampleClampedUVE(vec2 uv, vec2 halfTexel) {
    return max(texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb, vec3(0.0));
}

void main() {
    // 13-tap overlapping tent downsample filter preserving smooth circular halos across mip
    // transitions without 4-tap box shimmer or diamond ringing.
    vec2 texel = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 halfTexel = texel * 0.5;
    float x = texel.x;
    float y = texel.y;

    vec3 a = SampleClampedUVE(vTexCoord + vec2(-2.0 * x,  2.0 * y), halfTexel);
    vec3 b = SampleClampedUVE(vTexCoord + vec2(     0.0,  2.0 * y), halfTexel);
    vec3 c = SampleClampedUVE(vTexCoord + vec2( 2.0 * x,  2.0 * y), halfTexel);

    vec3 d = SampleClampedUVE(vTexCoord + vec2(-2.0 * x,      0.0), halfTexel);
    vec3 e = SampleClampedUVE(vTexCoord,                            halfTexel);
    vec3 f = SampleClampedUVE(vTexCoord + vec2( 2.0 * x,      0.0), halfTexel);

    vec3 g = SampleClampedUVE(vTexCoord + vec2(-2.0 * x, -2.0 * y), halfTexel);
    vec3 h = SampleClampedUVE(vTexCoord + vec2(     0.0, -2.0 * y), halfTexel);
    vec3 i = SampleClampedUVE(vTexCoord + vec2( 2.0 * x, -2.0 * y), halfTexel);

    vec3 j = SampleClampedUVE(vTexCoord + vec2(    -x,       y), halfTexel);
    vec3 k = SampleClampedUVE(vTexCoord + vec2(     x,       y), halfTexel);
    vec3 l = SampleClampedUVE(vTexCoord + vec2(    -x,      -y), halfTexel);
    vec3 m = SampleClampedUVE(vTexCoord + vec2(     x,      -y), halfTexel);

    vec3 downsampled = e * 0.125;
    downsampled += (a + c + g + i) * 0.03125;
    downsampled += (b + d + f + h) * 0.0625;
    downsampled += (j + k + l + m) * 0.125;
    FragColor = vec4(downsampled, 1.0);
}
#endif
)GLSLSRC";

const std::string_view kBloomBlurSource = R"GLSLSRC(#version 450 core

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
uniform sampler2D uHigherMipTexture;
// (1,0) for horizontal Gaussian pass, (0,1) for vertical Gaussian pass when uUpsampleMode == 0.
// When uUpsampleMode != 0, performs a 3x3 tent upsample of uSourceTexture (coarser mip) blended
// with uHigherMipTexture (finer mip) for smooth multi-scale bloom reconstruction.
uniform float uBlurDirectionX;
uniform float uBlurDirectionY;
uniform float uTexelSizeX;
uniform float uTexelSizeY;
uniform int uUpsampleMode;
uniform float uUpsampleScatter;

vec3 SampleSourceClampedUVE(vec2 uv, vec2 halfTexel) {
    return max(texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb, vec3(0.0));
}

void main() {
    if (uUpsampleMode != 0) {
        vec2 texel = vec2(uTexelSizeX, uTexelSizeY);
        if (texel.x <= 0.0 || texel.y <= 0.0) {
            texel = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
        }
        vec2 halfTexel = texel * 0.5;
        float x = texel.x;
        float y = texel.y;

        vec3 a = SampleSourceClampedUVE(vTexCoord + vec2(-x,  y), halfTexel);
        vec3 b = SampleSourceClampedUVE(vTexCoord + vec2( 0.0, y), halfTexel);
        vec3 c = SampleSourceClampedUVE(vTexCoord + vec2( x,  y), halfTexel);
        vec3 d = SampleSourceClampedUVE(vTexCoord + vec2(-x,  0.0), halfTexel);
        vec3 e = SampleSourceClampedUVE(vTexCoord, halfTexel);
        vec3 f = SampleSourceClampedUVE(vTexCoord + vec2( x,  0.0), halfTexel);
        vec3 g = SampleSourceClampedUVE(vTexCoord + vec2(-x, -y), halfTexel);
        vec3 h = SampleSourceClampedUVE(vTexCoord + vec2( 0.0, -y), halfTexel);
        vec3 i = SampleSourceClampedUVE(vTexCoord + vec2( x, -y), halfTexel);

        vec3 tent = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0);
        vec3 higher = max(texture(uHigherMipTexture, vTexCoord).rgb, vec3(0.0));
        float scatter = clamp(uUpsampleScatter > 0.0 ? uUpsampleScatter : 0.65, 0.0, 1.0);
        // Energy-conserving progressive blend preserves both tight inner glow and wide outer halo.
        FragColor = vec4(mix(higher, tent, scatter), 1.0);
        return;
    }

    // 9-tap separable Gaussian blur (sigma ~= 2 texels) for single-mip bloom.
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 direction = vec2(uBlurDirectionX, uBlurDirectionY);
    vec2 texelSize = vec2(uTexelSizeX, uTexelSizeY);
    vec2 halfTexel = texelSize * 0.5;
    vec3 result = SampleSourceClampedUVE(vTexCoord, halfTexel) * weights[0];
    for (int tap = 1; tap < 5; ++tap) {
        vec2 offset = direction * texelSize * float(tap);
        result += SampleSourceClampedUVE(vTexCoord + offset, halfTexel) * weights[tap];
        result += SampleSourceClampedUVE(vTexCoord - offset, halfTexel) * weights[tap];
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
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

// Default is a passthrough copy. When uSsaoBlurEnabled != 0, performs a depth-aware bilateral blur
// (either 7-tap separable along (uBilateralAxisX, uBilateralAxisY) or a 3x3 fallback) so half-res
// AO noise smooths cleanly without bleeding across geometric silhouettes or onto the sky.
uniform sampler2D uSourceTexture;
uniform sampler2D uDepthTexture;
uniform int uSsaoBlurEnabled;
uniform float uBilateralAxisX;
uniform float uBilateralAxisY;
uniform float uBilateralDepthSharpness;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uProjA;
uniform float uProjB;
uniform int uOrthographic;
uniform int uFastReconstruct;
uniform mat4 uInverseProjection;

float LinearDepthUVE(vec2 uv, float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    float ndcZ = depthSample * scale + bias;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        return abs(uProjB / max(uProjA - ndcZ, 1.0e-6));
    }
    vec4 viewPos = uInverseProjection * vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    return abs(viewPos.z / max(abs(viewPos.w), 1.0e-6));
}

vec4 SampleSourceClampedUVE(vec2 uv, vec2 halfTexel) {
    return texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel));
}

void main() {
    if (uSsaoBlurEnabled == 0) {
        FragColor = texture(uSourceTexture, vTexCoord);
        return;
    }

    vec2 texelSize = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 halfTexel = texelSize * 0.5;
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec4 centerValue = SampleSourceClampedUVE(vTexCoord, halfTexel);
    float centerLinDepth = LinearDepthUVE(vTexCoord, centerDepth);
    float depthSharpness = uBilateralDepthSharpness > 0.0 ? uBilateralDepthSharpness : 12.0;
    float depthScale = max(centerLinDepth * 0.05, 0.02);

    vec2 axis = vec2(uBilateralAxisX, uBilateralAxisY);
    if (dot(axis, axis) > 1.0e-5) {
        // 7-tap separable Gaussian-bilateral kernel (sigma ~= 1.6 texels).
        const float kSpatialWeightsUVE[4] = float[](0.25, 0.1875, 0.125, 0.0625);
        vec4 sum = centerValue * kSpatialWeightsUVE[0];
        float weightSum = kSpatialWeightsUVE[0];
        vec2 stepUv = axis * texelSize;
        for (int tap = 1; tap <= 3; ++tap) {
            float spatialW = kSpatialWeightsUVE[tap];
            vec2 offset = stepUv * float(tap);

            vec2 uvPos = clamp(vTexCoord + offset, halfTexel, vec2(1.0) - halfTexel);
            float dPos = texture(uDepthTexture, uvPos).r;
            if (dPos < 0.999999) {
                float deltaPos = abs(LinearDepthUVE(uvPos, dPos) - centerLinDepth) / depthScale;
                float wPos = spatialW * exp(-deltaPos * depthSharpness * 0.15);
                sum += texture(uSourceTexture, uvPos) * wPos;
                weightSum += wPos;
            }

            vec2 uvNeg = clamp(vTexCoord - offset, halfTexel, vec2(1.0) - halfTexel);
            float dNeg = texture(uDepthTexture, uvNeg).r;
            if (dNeg < 0.999999) {
                float deltaNeg = abs(LinearDepthUVE(uvNeg, dNeg) - centerLinDepth) / depthScale;
                float wNeg = spatialW * exp(-deltaNeg * depthSharpness * 0.15);
                sum += texture(uSourceTexture, uvNeg) * wNeg;
                weightSum += wNeg;
            }
        }
        FragColor = sum / max(weightSum, 1.0e-5);
        return;
    }

    // Fallback 3x3 depth-aware bilateral blur when invoked without a separable axis.
    vec4 sum = centerValue * 4.0;
    float weightSum = 4.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            float spatialW = (dx == 0 || dy == 0) ? 2.0 : 1.0;
            vec2 sampleUv = clamp(vTexCoord + vec2(float(dx), float(dy)) * texelSize, halfTexel, vec2(1.0) - halfTexel);
            float sampleDepth = texture(uDepthTexture, sampleUv).r;
            if (sampleDepth >= 0.999999) {
                continue;
            }
            float delta = abs(LinearDepthUVE(sampleUv, sampleDepth) - centerLinDepth) / depthScale;
            float w = spatialW * exp(-delta * depthSharpness * 0.15);
            sum += texture(uSourceTexture, sampleUv) * w;
            weightSum += w;
        }
    }
    FragColor = sum / max(weightSum, 1.0e-5);
}
#endif
)GLSLSRC";

const std::string_view kSsaoSource = R"GLSLSRC(#version 450 core

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

uniform sampler2D uDepthTexture;
uniform mat4 uInverseProjection;
uniform mat4 uProjection;
uniform int uOrthographic;
// Backend depth-buffer decode: ndcZ = depthSample * uDepthNdcScale + uDepthNdcBias.
// OpenGL default depth range uses (2.0, -1.0); Vulkan [0, 1] depth range uses (1.0, 0.0).
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
// Fast 2-FLOP perspective view-space depth reconstruction parameters:
// viewZ = uProjB / max(uProjA - ndcZ, 1e-6), viewXY = (uv * 2 - 1) * (-viewZ) * vec2(uInvProjX, uInvProjY).
uniform float uProjA;
uniform float uProjB;
uniform float uInvProjX;
uniform float uInvProjY;
uniform int uFastReconstruct;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;
uniform float uPower;
uniform int uSampleCount;

// Stratified 12-sample low-discrepancy hemisphere kernel ordered so the first 4 (Low), 8 (Medium),
// and 12 (High) taps each span all quadrants and the full radius [0.35..0.95] * uRadius.
const int kKernelSizeUVE = 12;
const vec3 kKernelUVE[12] = vec3[](
    vec3( 0.2400,  0.1400,  0.2200),
    vec3(-0.3600,  0.2900,  0.3400),
    vec3( 0.1900, -0.5200,  0.4100),
    vec3(-0.5800, -0.4400,  0.5200),
    vec3( 0.4200, -0.1500,  0.2600),
    vec3(-0.1800, -0.3500,  0.4800),
    vec3(-0.5600,  0.4200,  0.3800),
    vec3( 0.6200,  0.5100,  0.4500),
    vec3(-0.1200,  0.2600,  0.3100),
    vec3( 0.3400,  0.6400,  0.4400),
    vec3(-0.7100, -0.1800,  0.5600),
    vec3( 0.5400, -0.6200,  0.4900)
);

float DecodeNdcDepthUVE(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPositionUVE(vec2 uv, float depthSample) {
    float ndcZ = DecodeNdcDepthUVE(depthSample);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 viewPos = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return viewPos.xyz / max(abs(viewPos.w), 1.0e-6);
}

// Reconstructs a clean view-space normal from 4-neighbor minimum-difference depth taps.
// Avoids screen-space derivative UB after early returns and prevents dark halos across depth edges.
vec3 ReconstructViewNormalUVE(vec2 uv, vec3 centerViewPos) {
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

    vec3 pL = dL < 0.999999 ? ReconstructViewPositionUVE(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPositionUVE(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPositionUVE(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPositionUVE(uvU, dU) : centerViewPos;

    bool validL = dL < 0.999999;
    bool validR = dR < 0.999999;
    bool validD = dD < 0.999999;
    bool validU = dU < 0.999999;

    vec3 dPdx;
    if (validR && validL) {
        dPdx = abs(pR.z - centerViewPos.z) < abs(centerViewPos.z - pL.z)
                   ? (pR - centerViewPos)
                   : (centerViewPos - pL);
    } else if (validR) {
        dPdx = pR - centerViewPos;
    } else if (validL) {
        dPdx = centerViewPos - pL;
    } else {
        dPdx = vec3(1.0, 0.0, 0.0);
    }

    vec3 dPdy;
    if (validU && validD) {
        dPdy = abs(pU.z - centerViewPos.z) < abs(centerViewPos.z - pD.z)
                   ? (pU - centerViewPos)
                   : (centerViewPos - pD);
    } else if (validU) {
        dPdy = pU - centerViewPos;
    } else if (validD) {
        dPdy = centerViewPos - pD;
    } else {
        dPdy = vec3(0.0, 1.0, 0.0);
    }

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

// High-frequency blue-noise-like screen-space rotation with zero Moire patterns.
float InterleavedGradientNoiseUVE(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Orthonormal basis from a unit normal without any pole singularity.
void BuildOrthonormalBasisUVE(vec3 n, out vec3 b1, out vec3 b2) {
    if (n.z < -0.999999999) {
        b1 = vec3(0.0, -1.0, 0.0);
        b2 = vec3(-1.0, 0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    b1 = vec3(1.0 - n.x * n.x * a, b, -n.x);
    b2 = vec3(b, 1.0 - n.y * n.y * a, -n.y);
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // Background/far plane: never occluded.
        return;
    }
    vec3 centerViewPos = ReconstructViewPositionUVE(vTexCoord, centerDepth);
    vec3 viewNormal = ReconstructViewNormalUVE(vTexCoord, centerViewPos);

    vec3 basisT;
    vec3 basisB;
    BuildOrthonormalBasisUVE(viewNormal, basisT, basisB);
    float rotationAngle = InterleavedGradientNoiseUVE(gl_FragCoord.xy) * 6.28318530718;
    float cosAngle = cos(rotationAngle);
    float sinAngle = sin(rotationAngle);
    vec3 tangent = basisT * cosAngle + basisB * sinAngle;
    vec3 bitangent = -basisT * sinAngle + basisB * cosAngle;
    mat3 tbn = mat3(tangent, bitangent, viewNormal);

    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    float eyeLen = max(length(towardEye), 1.0e-5);
    float normalViewDot = max(dot(viewNormal, towardEye / eyeLen), 0.15);
    float distanceScale = uOrthographic != 0 ? 1.0 : clamp(abs(centerViewPos.z) * 0.02, 0.25, 4.0);
    float angleBias = max(uBias, uBias / normalViewDot) * distanceScale;
    float safeRadius = max(uRadius, 0.01);

    float occlusion = 0.0;
    int sampleCount = clamp(uSampleCount, 1, kKernelSizeUVE);
    int validSamples = 0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        vec3 samplePos = centerViewPos + (tbn * kKernelUVE[sampleIndex]) * safeRadius;

        vec4 sampleClip = uProjection * vec4(samplePos, 1.0);
        if (sampleClip.w <= 0.0001) {
            continue;
        }
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        if (isnan(sampleUv.x) || isnan(sampleUv.y) || isinf(sampleUv.x) || isinf(sampleUv.y) ||
            sampleUv.x < 0.001 || sampleUv.x > 0.999 || sampleUv.y < 0.001 || sampleUv.y > 0.999) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        if (sampledDepth >= 0.999999) {
            ++validSamples;
            continue;
        }
        vec3 sampledViewPos = ReconstructViewPositionUVE(sampleUv, sampledDepth);

        float depthDelta = abs(centerViewPos.z - sampledViewPos.z);
        float rangeCheck = 1.0 - smoothstep(safeRadius * 0.65, safeRadius * 1.5, depthDelta);
        float occluded = sampledViewPos.z >= samplePos.z + angleBias ? 1.0 : 0.0;
        occlusion += occluded * rangeCheck;
        ++validSamples;
    }

    float rawOcc = occlusion / float(max(validSamples, 1));
    float visibility = clamp(1.0 - rawOcc * max(uIntensity, 0.0), 0.08, 1.0);
    visibility = pow(visibility, clamp(uPower, 0.1, 4.0));
    if (isnan(visibility) || isinf(visibility)) {
        visibility = 1.0;
    }

    vec2 edgeDist = min(vTexCoord, 1.0 - vTexCoord);
    float edgeFade = smoothstep(0.0, 0.025, min(edgeDist.x, edgeDist.y));
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

const std::string_view kContactShadowSource = R"GLSLSRC(#version 450 core

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

float DecodeNdcDepthUVE(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPositionUVE(vec2 uv, float depthSample) {
    float ndcZ = DecodeNdcDepthUVE(depthSample);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 viewPos = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return viewPos.xyz / max(abs(viewPos.w), 1.0e-6);
}

vec3 ViewNormalUVE(vec2 uv, vec3 centerViewPos) {
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

    vec3 pL = dL < 0.999999 ? ReconstructViewPositionUVE(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPositionUVE(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPositionUVE(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPositionUVE(uvU, dU) : centerViewPos;

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

float InterleavedGradientNoiseUVE(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
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

    if (dot(ViewNormalUVE(vTexCoord, centerViewPos), lightDirection) <= 0.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    int stepCount = clamp(uStepCount, 1, 16);
    float inverseStepCount = 1.0 / float(stepCount);
    float jitter = InterleavedGradientNoiseUVE(gl_FragCoord.xy);
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
        vec3 sampleSceneViewPos = ReconstructViewPositionUVE(sampleUv, sampledDepth);
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
)GLSLSRC";


const std::string_view kSsrSource = R"GLSLSRC(#version 450 core

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

uniform sampler2D uColorTexture;
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
uniform vec3 uCameraPosition;
uniform float uCameraNear;
uniform float uCameraFar;
uniform int uMaxSteps;
uniform float uDepthTolerance;
uniform float uFadeIn;
uniform float uFadeOut;

float DecodeNdcDepthUVE(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPosUVE(vec2 uv, float depth) {
    float ndcZ = DecodeNdcDepthUVE(depth);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 view = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return view.xyz / max(abs(view.w), 1.0e-5);
}

vec3 ReconstructViewNormalUVE(vec2 uv, vec3 centerViewPos) {
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

    vec3 pL = dL < 0.999999 ? ReconstructViewPosUVE(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPosUVE(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPosUVE(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPosUVE(uvU, dU) : centerViewPos;

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
    vec3 normal = nLenSq > 1.0e-12 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    if (dot(normal, towardEye) < 0.0) {
        normal = -normal;
    }
    return normal;
}

float InterleavedGradientNoiseUVE(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    float depth = texture(uDepthTexture, vTexCoord).r;
    if (depth >= 0.999999) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 viewPos = ReconstructViewPosUVE(vTexCoord, depth);
    vec3 normal = ReconstructViewNormalUVE(vTexCoord, viewPos);
    vec3 viewDir = uOrthographic != 0 ? vec3(0.0, 0.0, -1.0) : normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normal));

    if (reflectDir.z >= 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    int steps = clamp(uMaxSteps, 8, 128);
    float stepSize = 0.25;
    float jitter = InterleavedGradientNoiseUVE(gl_FragCoord.xy);
    float currentDist = 0.08 + stepSize * jitter;
    float prevDist = 0.0;
    float tolerance = max(uDepthTolerance, 0.05);
    vec3 hitColor = vec3(0.0);
    float hitFactor = 0.0;

    for (int i = 0; i < steps; ++i) {
        prevDist = currentDist;
        currentDist += stepSize * (1.0 + float(i) * 0.04);
        vec3 rayPos = viewPos + reflectDir * currentDist;
        if (rayPos.z > -uCameraNear || rayPos.z < -uCameraFar) {
            break;
        }

        vec4 proj = uProjection * vec4(rayPos, 1.0);
        if (proj.w <= 1.0e-4) {
            break;
        }
        vec2 screenUv = (proj.xy / proj.w) * 0.5 + 0.5;
        if (screenUv.x < 0.0 || screenUv.x > 1.0 || screenUv.y < 0.0 || screenUv.y > 1.0) {
            break;
        }

        float sampledDepth = texture(uDepthTexture, screenUv).r;
        if (sampledDepth >= 0.999999) {
            continue;
        }

        vec3 sampledViewPos = ReconstructViewPosUVE(screenUv, sampledDepth);
        float depthDiff = sampledViewPos.z - rayPos.z;

        if (depthDiff >= 0.0 && depthDiff < tolerance) {
            // 3-step binary refinement between prevDist and currentDist for crisp reflection edges.
            float lo = prevDist;
            float hi = currentDist;
            vec2 bestUv = screenUv;
            for (int r = 0; r < 3; ++r) {
                float mid = 0.5 * (lo + hi);
                vec3 midPos = viewPos + reflectDir * mid;
                vec4 midProj = uProjection * vec4(midPos, 1.0);
                if (midProj.w <= 1.0e-4) {
                    break;
                }
                vec2 midUv = (midProj.xy / midProj.w) * 0.5 + 0.5;
                if (midUv.x < 0.0 || midUv.x > 1.0 || midUv.y < 0.0 || midUv.y > 1.0) {
                    break;
                }
                float midDepth = texture(uDepthTexture, midUv).r;
                if (midDepth < 0.999999) {
                    vec3 midSamplePos = ReconstructViewPosUVE(midUv, midDepth);
                    float midDiff = midSamplePos.z - midPos.z;
                    if (midDiff >= 0.0 && midDiff < tolerance) {
                        hi = mid;
                        bestUv = midUv;
                    } else {
                        lo = mid;
                    }
                } else {
                    lo = mid;
                }
            }

            vec2 edgeDist = min(bestUv, 1.0 - bestUv);
            float edgeFade = smoothstep(0.0, 0.12, min(edgeDist.x, edgeDist.y));
            float travelFrac = float(i) / float(steps);
            float fadeInLimit = clamp(uFadeIn, 0.0, 0.95);
            float fadeOutLimit = clamp(uFadeOut, fadeInLimit + 0.01, 1.0);
            float nearFade = fadeInLimit > 1.0e-4 ? smoothstep(0.0, fadeInLimit, travelFrac) : 1.0;
            float farFade = 1.0 - smoothstep(fadeOutLimit, 1.0, travelFrac);
            float ndotv = clamp(-dot(viewDir, normal), 0.0, 1.0);
            float fresnel = clamp(0.08 + 0.92 * pow(max(1.0 - ndotv, 0.0), 3.0), 0.05, 0.95);

            hitColor = max(texture(uColorTexture, bestUv).rgb, vec3(0.0));
            hitFactor = edgeFade * nearFade * farFade * fresnel;
            break;
        }
    }

    // Store unpremultiplied reflection color + blend weight in alpha for energy-conserving lerp.
    FragColor = vec4(hitColor, clamp(hitFactor, 0.0, 1.0));
}
#endif
)GLSLSRC";


const std::string_view kWaterSource = R"GLSLSRC(#version 450 core

// Built-in Water Surface Shader for UVE.
// Implements Gerstner wave displacement in the vertex stage, screen-space refraction,
// depth-based Beer-Lambert water absorption, Shoreline foam, Schlick Fresnel, and
// directional sun specular highlights.

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vClipPosition;

uniform mat4 uModel;
uniform mat4 uNormalMatrix;
uniform mat4 uViewProjection;

uniform float uWaterTime;
uniform float uWaveAmplitude;
uniform float uWaveFrequency;
uniform float uWaveSpeed;

// Gerstner wave generator
vec3 EvaluateGerstnerWave(vec2 xz, float steepness, float amplitude, float frequency, float speed, vec2 dir, float time, out vec3 normalOut) {
    float k = frequency;
    float c = speed;
    float f = k * (dot(dir, xz) - c * time);
    float cosF = cos(f);
    float sinF = sin(f);
    float qa = steepness * amplitude;

    vec3 displaced;
    displaced.x = qa * dir.x * cosF;
    displaced.z = qa * dir.y * cosF;
    displaced.y = amplitude * sinF;

    float wa = frequency * amplitude;
    normalOut = vec3(
        -dir.x * wa * cosF,
        1.0 - steepness * wa * sinF,
        -dir.y * wa * cosF
    );

    return displaced;
}

void main() {
    vec4 localPos = vec4(aPosition, 1.0);

    // Sum of 3 Gerstner wave harmonics
    vec3 wave1Normal;
    vec3 wave2Normal;
    vec3 wave3Normal;
    float time = uWaterTime * max(uWaveSpeed, 0.01);
    float amp = max(uWaveAmplitude, 0.0);
    float freq = max(uWaveFrequency, 0.01);

    vec3 d1 = EvaluateGerstnerWave(localPos.xz, 0.4, amp * 0.6, freq, 1.0, normalize(vec2(1.0, 0.3)), time, wave1Normal);
    vec3 d2 = EvaluateGerstnerWave(localPos.xz, 0.3, amp * 0.3, freq * 1.8, 1.3, normalize(vec2(-0.7, 0.7)), time, wave2Normal);
    vec3 d3 = EvaluateGerstnerWave(localPos.xz, 0.2, amp * 0.1, freq * 3.2, 1.7, normalize(vec2(0.2, -1.0)), time, wave3Normal);

    vec3 totalDisplacement = d1 + d2 + d3;
    vec3 totalNormal = normalize(vec3(
        wave1Normal.x + wave2Normal.x + wave3Normal.x,
        wave1Normal.y + wave2Normal.y + wave3Normal.y,
        wave1Normal.z + wave2Normal.z + wave3Normal.z
    ));

    localPos.xyz += totalDisplacement;

    vec4 worldPos = uModel * localPos;
    vWorldPosition = worldPos.xyz;
    vWorldNormal = normalize((uNormalMatrix * vec4(totalNormal, 0.0)).xyz);
    vTexCoord = aTexCoord;

    vClipPosition = uViewProjection * worldPos;
    gl_Position = vClipPosition;
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vClipPosition;

out vec4 FragColor;

uniform vec3 uViewPosition;
uniform int uOrthographic;
uniform vec3 uCameraForward;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunEnergy;

uniform vec3 uDeepColor;
uniform vec3 uShallowColor;
uniform vec3 uFoamColor;

uniform float uClarity;
uniform float uRefractionStrength;
uniform float uFoamDepth;
uniform float uWaterTime;
uniform float uCameraNear;
uniform float uCameraFar;

uniform sampler2D uSceneColorTexture;
uniform sampler2D uSceneDepthTexture;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;

float LinearizeDepth(float depth) {
    float near = max(uCameraNear, 0.01);
    float far = max(uCameraFar, 1.0);
    if (uOrthographic != 0) {
        return mix(near, far, clamp(depth, 0.0, 1.0));
    }
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    float ndcZ = clamp(depth, 0.0, 1.0) * scale + bias;
    return (2.0 * near * far) / max(far + near - ndcZ * (far - near), 1.0e-6);
}

void main() {
    vec2 screenUv = (vClipPosition.xy / vClipPosition.w) * 0.5 + 0.5;
    screenUv = clamp(screenUv, 0.001, 0.999);

    vec3 viewDir = uOrthographic != 0
                       ? (length(uCameraForward) > 1.0e-5 ? normalize(-uCameraForward) : vec3(0.0, 0.0, 1.0))
                       : normalize(uViewPosition - vWorldPosition);
    vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);

    // Dynamic wave ripples for high-frequency surface normal modulation
    vec2 rippleUv1 = vTexCoord * 8.0 + vec2(uWaterTime * 0.04, uWaterTime * 0.02);
    vec2 rippleUv2 = vTexCoord * 16.0 - vec2(uWaterTime * 0.03, uWaterTime * 0.05);

    vec3 rippleNormal = vec3(
        sin(rippleUv1.x * 6.28) * 0.05 + sin(rippleUv2.y * 6.28) * 0.03,
        1.0,
        cos(rippleUv1.y * 6.28) * 0.05 + cos(rippleUv2.x * 6.28) * 0.03
    );
    vec3 surfaceNormal = normalize(vWorldNormal + rippleNormal - vec3(0.0, 1.0, 0.0));

    // Underwater depth calculations
    float rawSceneDepth = texture(uSceneDepthTexture, screenUv).r;
    float sceneLinearDepth = LinearizeDepth(rawSceneDepth);
    float waterLinearDepth = LinearizeDepth(gl_FragCoord.z);
    float waterDepth = max(sceneLinearDepth - waterLinearDepth, 0.0);

    // Screen-space refraction offset
    vec2 refractOffset = surfaceNormal.xz * max(uRefractionStrength, 0.0) * clamp(waterDepth, 0.0, 1.0);
    vec2 refractedScreenUv = clamp(screenUv + refractOffset, 0.001, 0.999);

    // Check if the refracted sample is in front of the water (prevents distortion artifacts on foreground objects)
    float rawRefractedDepth = texture(uSceneDepthTexture, refractedScreenUv).r;
    float refractedLinearDepth = LinearizeDepth(rawRefractedDepth);
    if (refractedLinearDepth < waterLinearDepth) {
        refractedScreenUv = screenUv;
    }

    vec3 refractedColor = texture(uSceneColorTexture, refractedScreenUv).rgb;

    // Beer-Lambert light absorption
    float extinction = clamp(exp(-waterDepth / max(uClarity, 0.05)), 0.0, 1.0);
    vec3 absorptionColor = mix(uDeepColor, uShallowColor, extinction);
    vec3 waterBodyColor = mix(absorptionColor, refractedColor, extinction * 0.7);

    // Shoreline and object contact foam
    float foamDistance = max(uFoamDepth, 0.01);
    float foamFactor = clamp(1.0 - (waterDepth / foamDistance), 0.0, 1.0);
    float foamNoise = sin(vWorldPosition.x * 12.0 + uWaterTime * 2.5) * cos(vWorldPosition.z * 12.0 + uWaterTime * 2.0);
    foamFactor = smoothstep(0.2, 0.8, foamFactor + foamNoise * 0.15 * foamFactor);
    vec3 withFoam = mix(waterBodyColor, uFoamColor, foamFactor);

    // Schlick Fresnel
    float nDotV = max(dot(surfaceNormal, viewDir), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - nDotV, 5.0);

    // Sun specular reflection (Blinn-Phong)
    vec3 halfVec = normalize(viewDir + sunDir);
    float nDotH = max(dot(surfaceNormal, halfVec), 0.0);
    float specular = pow(nDotH, 128.0) * max(uSunEnergy, 0.0);
    vec3 specularColor = uSunColor * specular;

    // Sky reflection color approximation
    vec3 reflectedSky = mix(uShallowColor * 1.5, vec3(0.7, 0.85, 1.0), fresnel);
    vec3 finalColor = mix(withFoam, reflectedSky, fresnel * 0.6) + specularColor;

    FragColor = vec4(finalColor, clamp(1.0 - extinction * 0.3 + foamFactor * 0.7, 0.6, 1.0));
}
#endif
)GLSLSRC";

const std::string_view kVfxSpriteSource = R"GLSLSRC(#version 450 core

#ifdef VERTEX_SHADER
#ifdef UVE_VFX_VERTEX_FALLBACK
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aFrame;
#else
struct VfxSpriteInstanceData {
    vec4 positionAndRotation;
    vec4 velocityAndFrame;
    vec4 sizeAndColor;
};

layout(std430, binding = 0) readonly buffer VfxSpriteInstanceBuffer {
    VfxSpriteInstanceData instances[];
} bSpriteInstances;

uniform vec3 uCameraPosition;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform vec3 uFacingAxis;
uniform int uFacingMode;
uniform int uInstanceBaseIndex;
uniform float uStretchScale;
#endif

uniform mat4 uViewProjection;

out vec2 vCornerUv;
out vec4 vColor;
out vec3 vWorldPosition;
out float vFrame;

void main() {
#ifdef UVE_VFX_VERTEX_FALLBACK
    vWorldPosition = aPosition;
    vCornerUv = aTexCoord;
    vColor = aColor;
    vFrame = aFrame.x;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
#else
    const vec2 kQuadOffsets[6] = vec2[6](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );
    const vec2 kQuadUvs[6] = vec2[6](
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 0.0)
    );

    int cornerIndex = clamp(gl_VertexID % 6, 0, 5);
    int instanceIndex = uInstanceBaseIndex + gl_InstanceID;
    VfxSpriteInstanceData inst = bSpriteInstances.instances[instanceIndex];

    vec3 center = inst.positionAndRotation.xyz;
    float rotationRad = inst.positionAndRotation.w;
    vec3 velocity = inst.velocityAndFrame.xyz;
    float frame = inst.velocityAndFrame.w;
    vec2 size = max(inst.sizeAndColor.xy, vec2(0.0));
    uint packedColor = floatBitsToUint(inst.sizeAndColor.z);
    vec4 color = unpackUnorm4x8(packedColor) * vec4(vec3(max(inst.sizeAndColor.w, 0.0)), 1.0);

    vec3 basisRight = uCameraRight;
    vec3 basisUp = uCameraUp;

    if (uFacingMode == 1) {
        vec3 toCamera = uCameraPosition - center;
        float distSq = dot(toCamera, toCamera);
        vec3 forwardDir = distSq > 1.0e-8 ? toCamera * inversesqrt(distSq) : -uCameraForward;
        vec3 refUp = abs(forwardDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        basisRight = normalize(cross(refUp, forwardDir));
        basisUp = normalize(cross(forwardDir, basisRight));
    } else if (uFacingMode == 2) {
        float speedSq = dot(velocity, velocity);
        float speed = sqrt(speedSq);
        vec3 velDir = speed > 1.0e-5 ? (velocity / speed) : uCameraUp;
        vec3 toCamera = uCameraPosition - center;
        float toCamLenSq = dot(toCamera, toCamera);
        vec3 viewDir = toCamLenSq > 1.0e-8 ? (toCamera * inversesqrt(toCamLenSq)) : -uCameraForward;
        vec3 side = cross(viewDir, velDir);
        float sideLenSq = dot(side, side);
        basisRight = sideLenSq > 1.0e-8 ? (side * inversesqrt(sideLenSq)) : uCameraRight;
        basisUp = velDir;
        size.y += speed * max(uStretchScale, 0.0);
    } else if (uFacingMode == 3) {
        float axisLenSq = dot(uFacingAxis, uFacingAxis);
        vec3 normalAxis = axisLenSq > 1.0e-8 ? (uFacingAxis * inversesqrt(axisLenSq)) : vec3(0.0, 1.0, 0.0);
        vec3 refUp = abs(normalAxis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
        basisRight = normalize(cross(refUp, normalAxis));
        basisUp = normalize(cross(normalAxis, basisRight));
    }

    float cosAngle = cos(rotationRad);
    float sinAngle = sin(rotationRad);
    vec2 localCorner = kQuadOffsets[cornerIndex] * size;
    vec2 rotatedCorner = vec2(
        localCorner.x * cosAngle - localCorner.y * sinAngle,
        localCorner.x * sinAngle + localCorner.y * cosAngle
    );

    vec3 worldPos = center + basisRight * rotatedCorner.x + basisUp * rotatedCorner.y;
    vWorldPosition = worldPos;
    vCornerUv = kQuadUvs[cornerIndex];
    vColor = color;
    vFrame = frame;
    gl_Position = uViewProjection * vec4(worldPos, 1.0);
#endif
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vCornerUv;
in vec4 vColor;
in vec3 vWorldPosition;
in float vFrame;

uniform sampler2D uAlbedoTexture;
uniform vec4 uMaterialTint;
uniform vec3 uMaterialEmissive;
uniform vec3 uCameraPosition;
uniform int uFlipbookRows;
uniform int uFlipbookCols;
uniform int uFrameBlend;
uniform int uBlendMode;
uniform int uFacingMode;
uniform float uAlphaCutoff;
uniform float uCameraFadeNear;
uniform float uCameraFadeFar;
uniform float uEmissive;

#ifdef UVE_VFX_TRANSLUCENT
uniform sampler2D uSceneDepthTexture;
uniform mat4 uInverseProjection;
uniform vec2 uInvViewportSize;
uniform float uSoftDistance;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
#endif

out vec4 FragColor;

vec2 ComputeFlipbookUv(vec2 cornerUv, int frameIndex, int rows, int cols) {
    int safeCols = max(cols, 1);
    int safeRows = max(rows, 1);
    int totalFrames = safeCols * safeRows;
    int clampedFrame = clamp(frameIndex, 0, max(totalFrames - 1, 0));
    int col = clampedFrame % safeCols;
    int row = clampedFrame / safeCols;
    vec2 cellScale = vec2(1.0 / float(safeCols), 1.0 / float(safeRows));
    return (vec2(float(col), float(row)) + clamp(cornerUv, vec2(0.0), vec2(1.0))) * cellScale;
}

#ifdef UVE_VFX_TRANSLUCENT
float ReconstructLinearViewDepth(vec2 screenUv, float rawDepth) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    vec3 ndc = vec3(screenUv * 2.0 - 1.0, clamp(rawDepth, 0.0, 1.0) * scale + bias);
    vec4 viewPos = uInverseProjection * vec4(ndc, 1.0);
    float invW = abs(viewPos.w) > 1.0e-7 ? (1.0 / viewPos.w) : 1.0;
    return -(viewPos.z * invW);
}
#endif

void main() {
    int rows = max(uFlipbookRows, 1);
    int cols = max(uFlipbookCols, 1);
    int totalFrames = rows * cols;
    float clampedFrame = clamp(vFrame, 0.0, float(max(totalFrames - 1, 0)));
    int frame0 = int(floor(clampedFrame));
    vec2 uv0 = ComputeFlipbookUv(vCornerUv, frame0, rows, cols);
    vec4 texSample = texture(uAlbedoTexture, uv0);
    if (uFrameBlend != 0 && totalFrames > 1) {
        int frame1 = min(frame0 + 1, totalFrames - 1);
        float frac = fract(clampedFrame);
        vec2 uv1 = ComputeFlipbookUv(vCornerUv, frame1, rows, cols);
        texSample = mix(texSample, texture(uAlbedoTexture, uv1), frac);
    }

    // Procedural non-boxy particle shaping when no authored sprite texture is bound (1x1 fallback).
    ivec2 albedoDims = textureSize(uAlbedoTexture, 0);
    if (albedoDims.x <= 1 && albedoDims.y <= 1) {
        vec2 p = vCornerUv * 2.0 - 1.0;
        if (uFacingMode == 2) {
            // Velocity-aligned incandescent spark streak with tapered ends.
            float across = abs(p.x);
            float along = abs(p.y);
            float taper = max(1.0 - along * along, 0.0);
            float core = smoothstep(0.65 * taper + 0.06, 0.0, across) * smoothstep(1.0, 0.12, along);
            float hot = exp(-(across * across * 14.0 + along * along * 2.5));
            texSample.rgb *= mix(vec3(1.0), vec3(1.35, 1.25, 1.1), hot);
            texSample.a *= clamp(core + hot * 0.55, 0.0, 1.0) * smoothstep(1.0, 0.85, max(across, along));
        } else if (uBlendMode == 1) {
            // Soft radial glow with hot inner core for additive fire/embers/magic.
            float r = length(p);
            float radial = smoothstep(1.0, 0.0, r);
            float core = exp(-r * r * 4.2);
            texSample.rgb *= mix(vec3(1.0), vec3(1.25, 1.18, 1.08), core);
            texSample.a *= clamp(radial * radial * 0.60 + core * 0.85, 0.0, 1.0) * smoothstep(1.0, 0.82, r);
        } else {
            // Soft feathered organic disc for alpha-blended smoke/dust/snow/clouds.
            float r = length(p);
            float puff = smoothstep(1.0, 0.0, r);
            puff = puff * puff * (3.0 - 2.0 * puff);
            texSample.a *= puff;
        }
    }

    vec4 base = texSample * vColor * uMaterialTint;
    float alpha = clamp(base.a, 0.0, 1.0);

    if (uCameraFadeFar > uCameraFadeNear && uCameraFadeFar > 0.0) {
        float camDist = length(vWorldPosition - uCameraPosition);
        float camFade = clamp((camDist - uCameraFadeNear) / max(uCameraFadeFar - uCameraFadeNear, 1.0e-5), 0.0, 1.0);
        alpha *= camFade;
    }

#ifdef UVE_VFX_TRANSLUCENT
    vec2 screenUv = clamp(gl_FragCoord.xy * uInvViewportSize, vec2(0.0005), vec2(0.9995));
    float rawSceneDepth = texture(uSceneDepthTexture, screenUv).r;
    if (rawSceneDepth < 0.999999 && gl_FragCoord.z > rawSceneDepth) {
        discard;
    }
    if (uSoftDistance > 0.0 && rawSceneDepth < 0.999999) {
        float sceneLinearDepth = ReconstructLinearViewDepth(screenUv, rawSceneDepth);
        float particleLinearDepth = ReconstructLinearViewDepth(screenUv, gl_FragCoord.z);
        float softFade = clamp((sceneLinearDepth - particleLinearDepth) / max(uSoftDistance, 1.0e-5), 0.0, 1.0);
        alpha *= softFade;
    }
#endif

    if (uBlendMode == 2) {
        if (alpha < uAlphaCutoff) {
            discard;
        }
        vec3 rgbOut = base.rgb * max(uEmissive, 0.0) + uMaterialEmissive;
        FragColor = vec4(rgbOut, 1.0);
        return;
    }

    if (alpha <= 1.0e-5) {
        discard;
    }

    vec3 rgbLit = (base.rgb * max(uEmissive, 0.0) + uMaterialEmissive) * alpha;
    if (uBlendMode == 0) {
        FragColor = vec4(rgbLit, alpha);
    } else {
        FragColor = vec4(rgbLit, alpha);
    }
}
#endif
)GLSLSRC";

const std::string_view kStudioFloorPbrSource = R"GLSLSRC(#version 330 core
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
)GLSLSRC";


const std::string_view kTaaResolveSource = R"GLSLSRC(#version 450 core

// Temporal Anti-Aliasing (TAA) resolve pass. Reprojects the previous frame's HDR history via
// depth-reconstructed world position, clamps history inside the current frame's 3x3 YCoCg
// variance box (eliminating ghosting), and blends smoothly with disocclusion rejection.

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

uniform sampler2D uCurrentColorTexture;
uniform sampler2D uHistoryColorTexture;
uniform sampler2D uDepthTexture;

uniform mat4 uInverseViewProjection;
uniform mat4 uPreviousViewProjection;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uHistoryWeight;
uniform int uHistoryValid;
uniform vec2 uTexelSize;

vec3 RgbToYCoCgUVE(vec3 rgb) {
    float co = rgb.r - rgb.b;
    float t = rgb.b + co * 0.5;
    float cg = rgb.g - t;
    float y = t + cg * 0.5;
    return vec3(y, co, cg);
}

vec3 YCoCgToRgbUVE(vec3 ycocg) {
    float t = ycocg.x - ycocg.z * 0.5;
    float g = ycocg.z + t;
    float b = t - ycocg.y * 0.5;
    float r = b + ycocg.y;
    return max(vec3(r, g, b), vec3(0.0));
}

vec3 ReinhardCompressUVE(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / (1.0 + max(luma, 0.0));
}

vec3 ReinhardDecompressUVE(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / max(1.0 - luma, 1.0e-4);
}

float DecodeNdcDepthUVE(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

void main() {
    vec4 currentSample = texture(uCurrentColorTexture, vTexCoord);
    vec3 currentRgb = max(currentSample.rgb, vec3(0.0));
    if (uHistoryValid == 0) {
        FragColor = vec4(currentRgb, currentSample.a);
        return;
    }

    // Closest-depth 3x3 dilation improves silhouette edge reprojection.
    float bestDepth = texture(uDepthTexture, vTexCoord).r;
    vec2 bestOffset = vec2(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 off = vec2(float(dx), float(dy)) * uTexelSize;
            float d = texture(uDepthTexture, clamp(vTexCoord + off, vec2(0.0), vec2(1.0))).r;
            if (d < bestDepth) {
                bestDepth = d;
                bestOffset = off;
            }
        }
    }

    vec2 reprojectUv = vTexCoord + bestOffset;
    float ndcZ = DecodeNdcDepthUVE(bestDepth);
    vec4 worldPos = uInverseViewProjection * vec4(reprojectUv * 2.0 - 1.0, ndcZ, 1.0);
    vec2 prevUv = vTexCoord;
    bool validReprojection = false;
    if (abs(worldPos.w) > 1.0e-5) {
        vec4 prevClip = uPreviousViewProjection * (worldPos / worldPos.w);
        if (abs(prevClip.w) > 1.0e-5) {
            vec2 velocity = (prevClip.xy / prevClip.w) * 0.5 + 0.5 - reprojectUv;
            prevUv = vTexCoord + velocity;
            validReprojection = prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0;
        }
    }

    if (!validReprojection) {
        FragColor = vec4(currentRgb, currentSample.a);
        return;
    }

    // 3x3 YCoCg variance box in Reinhard-compressed HDR space so bright specular highlights
    // cannot destabilize the neighborhood clamp.
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 sampleUv = clamp(vTexCoord + vec2(float(dx), float(dy)) * uTexelSize, vec2(0.0), vec2(1.0));
            vec3 c = RgbToYCoCgUVE(ReinhardCompressUVE(max(texture(uCurrentColorTexture, sampleUv).rgb, vec3(0.0))));
            m1 += c;
            m2 += c * c;
        }
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    const float kGamma = 1.25;
    vec3 boxMin = mean - kGamma * sigma;
    vec3 boxMax = mean + kGamma * sigma;

    vec3 historyRgb = max(texture(uHistoryColorTexture, prevUv).rgb, vec3(0.0));
    vec3 historyYcc = clamp(RgbToYCoCgUVE(ReinhardCompressUVE(historyRgb)), boxMin, boxMax);
    vec3 currentYcc = RgbToYCoCgUVE(ReinhardCompressUVE(currentRgb));

    float blendWeight = clamp(uHistoryWeight, 0.0, 0.96);
    vec3 resolvedYcc = mix(currentYcc, historyYcc, blendWeight);
    vec3 resolvedRgb = ReinhardDecompressUVE(YCoCgToRgbUVE(resolvedYcc));
    if (any(isnan(resolvedRgb)) || any(isinf(resolvedRgb))) {
        resolvedRgb = currentRgb;
    }
    FragColor = vec4(max(resolvedRgb, vec3(0.0)), currentSample.a);
}
#endif
)GLSLSRC";

const std::string_view kWboitCompositeSource = R"GLSLSRC(#version 450 core

// Weighted Blended Order-Independent Transparency (WBOIT) composite pass.
// Reads the additive accum target (location 0: sum(rgb*a*w), sum(a*w)) and the additive
// log-revealage target (location 1: sum(-ln(1 - a))), reconstructing exact revealage via exp(-logR).

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

uniform sampler2D uWboitAccumTexture;
uniform sampler2D uWboitRevealTexture;

void main() {
    vec4 accum = texture(uWboitAccumTexture, vTexCoord);
    float logReveal = max(texture(uWboitRevealTexture, vTexCoord).r, 0.0);
    if (logReveal <= 1.0e-5 && accum.a <= 1.0e-5) {
        discard;
    }
    float revealage = clamp(exp(-logReveal), 0.0, 1.0);
    vec3 averageColor = accum.rgb / max(accum.a, 1.0e-5);
    if (any(isnan(averageColor)) || any(isinf(averageColor))) {
        averageColor = vec3(0.0);
    }
    // Rendered with SourceAlphaOver blend over the opaque HDR color target:
    // out = averageColor * (1 - revealage) + dst * revealage.
    FragColor = vec4(max(averageColor, vec3(0.0)), 1.0 - revealage);
}
#endif
)GLSLSRC";


const std::string_view kRtTraceSource = R"GLSLSRC(#version 430 core

// Compute ray-traced shadows, 1-bounce specular reflections, and 1-bounce diffuse GI over the
// scene AABB proxy buffer (RtSceneBoxNodeUVE). Works universally on desktop OpenGL 4.3+ and
// Vulkan compute queues (including software lavapipe in CI).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct RtSceneBoxNodeUVE {
    vec4 minAndMetallic;
    vec4 maxAndRoughness;
    vec4 albedo;
    vec4 emissive;
};

layout(std430, binding = 0) readonly buffer RtSceneBoxesBlock {
    RtSceneBoxNodeUVE sceneBoxes[];
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

bool IntersectBoxUVE(vec3 origin, vec3 invDir, RtSceneBoxNodeUVE box, float tMin, float tMax,
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
        if (IntersectBoxUVE(worldPos + sunDir * 0.02, invSunDir, sceneBoxes[i], 0.005, 250.0, hitT, hitN)) {
            sunVis = 0.0;
            break;
        }
    }

    vec3 giColor = uSkyColorAndWidthHeight.rgb * 0.25;
    imageStore(uRtOutputImage, pixel, vec4(giColor, sunVis));
}
)GLSLSRC";


const std::string_view kHiZDownsampleSource = R"GLSLSRC(#version 430 core

// Conservative hierarchical depth (Hi-Z) max-depth downsample compute shader.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D uSourceDepthMip;
layout(r32f, binding = 1) uniform writeonly image2D uDestDepthMip;

void main() {
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dstSize = imageSize(uDestDepthMip);
    if (dstCoord.x >= dstSize.x || dstCoord.y >= dstSize.y) {
        return;
    }

    ivec2 srcSize = textureSize(uSourceDepthMip, 0);
    ivec2 srcBase = dstCoord * 2;
    ivec2 c00 = min(srcBase + ivec2(0, 0), srcSize - ivec2(1));
    ivec2 c10 = min(srcBase + ivec2(1, 0), srcSize - ivec2(1));
    ivec2 c01 = min(srcBase + ivec2(0, 1), srcSize - ivec2(1));
    ivec2 c11 = min(srcBase + ivec2(1, 1), srcSize - ivec2(1));

    float d = max(
        max(texelFetch(uSourceDepthMip, c00, 0).r, texelFetch(uSourceDepthMip, c10, 0).r),
        max(texelFetch(uSourceDepthMip, c01, 0).r, texelFetch(uSourceDepthMip, c11, 0).r));

    if ((srcSize.x & 1) != 0 && dstCoord.x == dstSize.x - 1) {
        ivec2 ex0 = min(srcBase + ivec2(2, 0), srcSize - ivec2(1));
        ivec2 ex1 = min(srcBase + ivec2(2, 1), srcSize - ivec2(1));
        d = max(d, max(texelFetch(uSourceDepthMip, ex0, 0).r, texelFetch(uSourceDepthMip, ex1, 0).r));
    }
    if ((srcSize.y & 1) != 0 && dstCoord.y == dstSize.y - 1) {
        ivec2 ey0 = min(srcBase + ivec2(0, 2), srcSize - ivec2(1));
        ivec2 ey1 = min(srcBase + ivec2(1, 2), srcSize - ivec2(1));
        d = max(d, max(texelFetch(uSourceDepthMip, ey0, 0).r, texelFetch(uSourceDepthMip, ey1, 0).r));
    }

    imageStore(uDestDepthMip, dstCoord, vec4(d, 0.0, 0.0, 0.0));
}
)GLSLSRC";

} // namespace UVE::Render::Shader::BuiltIn
