#version 330 core
// gizmo_solid.frag
// ---------------------------------------------------------------------------
// A gentle, fixed-key-light shading pass over each face's own flat colour.
// A gizmo is still a UI element that happens to live in world space, so this
// deliberately never re-colours or fully darkens a face - axis identity must
// stay obvious from any angle - it just modulates brightness a little so
// each solid shape (arrow cones, scale cubes, nav balls) reads with some real
// 3D depth instead of a uniformly flat, "drawn" look. The light direction is
// fixed in world space (not tied to the camera), so a shape's shading stays
// stable as the view orbits around it, rather than swimming.
// ---------------------------------------------------------------------------

in vec3 vNormal;
in vec4 vColorAlpha;

uniform float uOpacity;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.82, 0.45));
    float ndotl = max(dot(normal, lightDir), 0.0);
    // Floor kept high (0.68) on purpose: this is a soft shading cue, not real
    // lighting - a face must never read as "black" or as a different hue.
    float shade = 0.68 + 0.32 * ndotl;
    fragColor = vec4(vColorAlpha.rgb * shade, vColorAlpha.a * uOpacity);
}
