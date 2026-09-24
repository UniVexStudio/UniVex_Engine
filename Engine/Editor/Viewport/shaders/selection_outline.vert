#version 330 core
layout(location = 0) in vec2 aClipPos;
void main() {
    gl_Position = vec4(aClipPos, 0.0, 1.0);
}
