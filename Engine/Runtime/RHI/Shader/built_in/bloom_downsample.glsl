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
