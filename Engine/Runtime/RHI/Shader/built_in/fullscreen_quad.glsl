#version 450 core

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
