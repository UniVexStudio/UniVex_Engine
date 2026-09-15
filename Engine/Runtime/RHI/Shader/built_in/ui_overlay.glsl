#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 uProjection;

void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uSourceTexture;

void main() {
    FragColor = texture(uSourceTexture, vTexCoord) * vColor;
}
#endif
