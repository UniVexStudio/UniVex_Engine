#version 330 core
// infinite_grid.vert
// ---------------------------------------------------------------------------
// One fullscreen triangle. Rather than pushing a big ground quad through the
// pipeline (which is only ever "large", never infinite, and always has an edge
// to hide), this rebuilds a world-space view ray per pixel and intersects it
// with the ground plane in the fragment shader. That makes the plane
// mathematically infinite: it ends at the true horizon, not at a quad boundary.
//
// The two unprojected points are passed on in HOMOGENEOUS form and divided per
// fragment. Unprojection is linear in clip space, so the homogeneous vectors
// interpolate exactly across the triangle; the divided points do not, because
// w changes across the screen. Dividing here and interpolating the result bent
// every ray toward the triangle's off-screen corner, which shifted the whole grid
// sideways against the scene and the gizmos drawn with the same camera.
// ---------------------------------------------------------------------------

layout(location = 0) in vec2 aClipPos;   // fullscreen triangle, clip-space XY

uniform mat4 uInvViewProj;

out vec4 vNearPointH;   // near-plane hit, homogeneous world space (divide per fragment)
out vec4 vFarPointH;    // ... and the far plane

void main() {
    vNearPointH = uInvViewProj * vec4(aClipPos, -1.0, 1.0);
    vFarPointH  = uInvViewProj * vec4(aClipPos,  1.0, 1.0);
    gl_Position = vec4(aClipPos, 0.0, 1.0);
}
