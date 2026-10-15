#version 450 core

// Occlusion proxy for Renderer3DUVE's GPU occlusion stage (Foundation WP3). The pipeline that owns
// this shader is depth-tested (LessOrEqual), writes no depth, and blends additively; because the
// fragment contributes exactly zero, the colour attachment is left untouched and the only
// observable effect is the sample count inside the query bracket that surrounds the draw.

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProjection;

void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(0.0);
}
#endif
