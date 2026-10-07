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
