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

// Unmodified passthrough: the actual effect is the pipeline's blend mode this shader is used
// with, not anything computed here - Additive to composite the blurred bloom texture onto the
// HDR scene color, Multiply to composite the SSAO occlusion term onto it.
uniform sampler2D uSourceTexture;

void main() {
    FragColor = texture(uSourceTexture, vTexCoord);
}
#endif
