#version 330 core
// gizmo_line.frag
// ---------------------------------------------------------------------------
// Analytic anti-aliasing: coverage falls off over exactly one pixel either
// side of the line's nominal width, with round caps at both ends, so a 2.3 px line looks 2.3 px wide with
// clean edges whether or not the framebuffer is multisampled.
// ---------------------------------------------------------------------------

in vec3 vColor;
in float vDistPx;
in float vHalfWidthPx;
in float vAlongPx;
in float vHalfLenPx;

uniform float uOpacity;

out vec4 fragColor;

void main() {
    // Distance to the segment, not to its infinite line: round caps.
    float beyondEnd = max(abs(vAlongPx) - vHalfLenPx, 0.0);
    float distPx = length(vec2(beyondEnd, vDistPx));
    float coverage = clamp(vHalfWidthPx + 0.5 - distPx, 0.0, 1.0);
    if (coverage <= 0.0) discard;
    fragColor = vec4(vColor, coverage * uOpacity);
}
