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
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uCameraNear;
uniform float uCameraFar;
uniform float uFogSkyAffect;
uniform int uSkyCovers;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uColorFilter;
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

vec3 AcesToneMapUVE(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec2 HumanEyeSourceUVUVE(vec2 uv) {
    vec2 ndc = uv * 2.0 - 1.0;
    float k0 = uHumanEyeCenterScale;
    vec2 sampleNdc = ndc * (vec2(k0) + (1.0 - k0) * ndc * ndc);
    return sampleNdc * 0.5 + 0.5;
}

vec3 SampleHdrUVE(vec2 uv) {
    return max(texture(uSourceTexture, uv).rgb, vec3(0.0));
}

void main() {
    vec2 sourceUV = vTexCoord;
    float periphery = 0.0;
    if (uHumanEye != 0) {
        vec2 ndc = vTexCoord * 2.0 - 1.0;
        periphery = smoothstep(0.55, 1.2, length(ndc));
        sourceUV = HumanEyeSourceUVUVE(vTexCoord);
    }
    vec3 hdrColor = SampleHdrUVE(sourceUV);
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
    float exposure = uExposure > 0.0 ? uExposure : 1.0;
    hdrColor *= exposure;
    float depth = texture(uSceneDepthTexture, sourceUV).r;
    if (uFogEnabled != 0) {
        vec2 ndc = sourceUV * 2.0 - 1.0;
        vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
        vec3 viewDir = normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
        float fogFactor = clamp(uFogSkyAffect, 0.0, 1.0);
        if (depth < 1.0) {
            float viewDistance = mix(max(uCameraNear, 0.0), max(uCameraFar, 0.0), clamp(depth, 0.0, 1.0));
            vec3 worldPos = uCameraPosition + viewDir * viewDistance;
            float heightTerm = exp(-max(worldPos.y - uFogHeight, 0.0) / max(uFogHeightFalloff, 0.01));
            float density = max(uFogDensity, 0.0) * mix(1.0, heightTerm, 0.85);
            fogFactor = 1.0 - exp(-density * viewDistance);
            fogFactor = clamp(fogFactor, 0.0, 1.0);
        }
        vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
        float towardSun = pow(max(dot(viewDir, sunDir), 0.0), 8.0);
        float day = smoothstep(-0.08, 0.18, sunDir.y);
        vec3 inscatter = max(uFogColor, vec3(0.0));
        inscatter = mix(inscatter, max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0),
                        clamp(uFogSunScatter, 0.0, 1.0) * towardSun * day);
        hdrColor = mix(hdrColor, inscatter * exposure, fogFactor);
    }
    vec3 ldr = AcesToneMapUVE(hdrColor);
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
    float covered = (depth < 1.0 || uSkyCovers != 0) ? 1.0 : 0.0;
    float alpha = uWriteCoverageAlpha != 0 ? covered : 1.0;
    FragColor = vec4(ldr, alpha);
}
#endif
