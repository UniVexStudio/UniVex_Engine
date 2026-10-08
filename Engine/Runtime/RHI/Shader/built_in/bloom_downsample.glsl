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

void main() {
    // Four linearly filtered samples average the previous bloom level while halving its extent.
    vec2 halfTexel = 0.5 / vec2(textureSize(uSourceTexture, 0));
    vec3 downsampled = texture(uSourceTexture, vTexCoord + vec2(-halfTexel.x, -halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(halfTexel.x, -halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(-halfTexel.x, halfTexel.y)).rgb +
                       texture(uSourceTexture, vTexCoord + vec2(halfTexel.x, halfTexel.y)).rgb;
    FragColor = vec4(downsampled * 0.25, 1.0);
}
#endif
