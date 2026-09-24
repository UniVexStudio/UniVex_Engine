#version 330 core
// Selected meshes, drawn flat into a one-channel mask: the channel holds how strongly the pixel is
// selected (the active node brighter than the rest of a multi-selection).
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aWeight;
uniform mat4 uViewProj;
out float vWeight;
void main() {
    vWeight = aWeight;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
