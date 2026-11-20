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
