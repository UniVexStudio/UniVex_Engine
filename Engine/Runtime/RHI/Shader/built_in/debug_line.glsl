#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProjection;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
#endif
