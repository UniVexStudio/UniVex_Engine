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
