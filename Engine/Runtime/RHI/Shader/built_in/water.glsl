#version 450 core

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

float LinearizeDepth(float depth) {
    float near = max(uCameraNear, 0.01);
    float far = max(uCameraFar, 1.0);
    if (uOrthographic != 0) {
        return mix(near, far, clamp(depth, 0.0, 1.0));
    }
    return (2.0 * near * far) / (far + near - depth * (far - near));
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
