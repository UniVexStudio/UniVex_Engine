#version 330 core
in float vWeight;
out vec4 fragColor;
void main() {
    fragColor = vec4(vWeight, 0.0, 0.0, 1.0);
}
