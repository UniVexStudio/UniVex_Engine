#version 330 core
// gizmo_solid.frag
// ---------------------------------------------------------------------------
// Two shading models, chosen per face by vLit:
//
//   0 - flat UI shapes (rings, plane handles, discs): a gentle fixed-key-light
//       cue with a high floor, so a face never darkens or changes hue with the
//       view - axis identity must stay obvious from any angle.
//   1 - solid bodies (shafts, cones, cubes, spheres, bones): real two-sided
//       lighting - a fixed key light for form, a camera headlight so the side
//       facing the viewer is never in shadow, and a soft rim toward the
//       silhouette so the outline reads without drawing edge lines.
// ---------------------------------------------------------------------------

in vec3 vNormal;
in vec4 vColorAlpha;
in float vLit;

uniform float uOpacity;
uniform vec3 uViewDir; // from the eye into the scene

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 keyLight = normalize(vec3(0.35, 0.82, 0.45));

    float flatShade = 0.68 + 0.32 * max(dot(normal, keyLight), 0.0);

    // Face the normal toward the eye: the solids are built without a guaranteed winding, and a
    // back face lit as if it pointed away would come out black.
    vec3 toEye = -normalize(uViewDir);
    vec3 facing = dot(normal, toEye) < 0.0 ? -normal : normal;
    float key = max(dot(facing, keyLight), 0.0);
    float head = max(dot(facing, toEye), 0.0);
    float rim = pow(1.0 - head, 3.0);
    float litShade = 0.22 + 0.48 * key + 0.38 * head - 0.18 * rim;

    float shade = mix(flatShade, litShade, clamp(vLit, 0.0, 1.0));
    fragColor = vec4(vColorAlpha.rgb * shade, vColorAlpha.a * uOpacity);
}
