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
    // Clamped to 1.0, and not just for tidiness. A surface facing the camera - every disc and ring
    // in this widget, since they are built camera-aligned - has dot(facing, toEye) == 1.0 up to
    // rounding, and the rounding can land ON THE WRONG SIDE of it. `1.0 - head` then goes very
    // slightly negative and pow(negative, 3.0) is undefined, which every driver here returns as
    // NaN. That NaN used to survive the mix() below even for flat shapes (whose vLit is 0), because
    // mix(x, y, 0) is x*(1-0) + y*0 and NaN*0 is still NaN - so the whole face wrote NaN, and a
    // NaN colour lands in the framebuffer as black. That was the broken black pie-slicing across
    // the nav gizmo's balls: not z-fighting, not a missing texture, just NaN out of a pow().
    float head = clamp(dot(facing, toEye), 0.0, 1.0);
    float rim = pow(1.0 - head, 3.0);
    float litShade = 0.22 + 0.48 * key + 0.38 * head - 0.18 * rim;

    // Select rather than mix: a flat shape never reads litShade, so it can never be hurt by it.
    float shade = vLit > 0.0 ? mix(flatShade, litShade, clamp(vLit, 0.0, 1.0)) : flatShade;
    fragColor = vec4(vColorAlpha.rgb * shade, vColorAlpha.a * uOpacity);
}
