#version 450 core

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
