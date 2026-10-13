#version 330 core
// infinite_grid.frag
// ---------------------------------------------------------------------------
// Ray/plane infinite grid with an auto-adjusting (decade-LOD) line spacing.
//
// Two things make this behave like an editor grid in a real engine rather than
// a textured plane:
//
//  1. TRUE INFINITY + CORRECT DEPTH. Each pixel's world ray is intersected
//     with y = 0. Pixels whose ray never hits the plane in front of the camera
//     are discarded, so the grid terminates exactly at the horizon. The hit
//     point is re-projected to write gl_FragDepth, so the grid depth-tests
//     against scene geometry properly instead of floating over or under it.
//
//  2. AUTO-ADJUSTING SPACING. The grid never shows a fixed 1-unit cell. It
//     measures how much world space one pixel covers (screen-space
//     derivatives) and picks the decade of spacing that keeps cells at roughly
//     uTargetCellPixels on screen. Four decades are drawn at once and
//     cross-faded by the fractional part of the LOD, so zooming slides the
//     tiers continuously (1 m -> 10 m -> 100 m ...) with no popping and
//     constant on-screen density from centimetres to kilometres.
//
//     A useful side effect: because the spacing grows with distance, the
//     argument to fract() below stays in a small numeric range even when the
//     camera is far from the origin, which is what keeps fp32 precision from
//     making distant lines wobble.
//
// The general approach (procedural lines via fract()/derivative-based
// anti-aliasing, decade LOD, ray-cast infinite plane) is the standard one used
// across many engines and public shader collections; it is not a copy of any
// single proprietary implementation.
// ---------------------------------------------------------------------------

in vec4 vNearPointH;
in vec4 vFarPointH;

uniform mat4  uViewProj;
uniform vec3  uCameraPos;

uniform float uBaseSpacing;       // finest grid spacing, world units (e.g. 1.0 = 1 m)
uniform float uTargetCellPixels;  // desired minimum on-screen cell size before stepping a decade
uniform float uLineWidthPixels;
uniform float uAxisWidthPixels;

uniform vec3  uThinColor;
uniform vec3  uMidColor;
uniform vec3  uThickColor;
uniform float uThinIntensity;
uniform float uMidIntensity;
uniform float uThickIntensity;

uniform vec3  uAxisColorX;        // line along world X (at z = 0)
uniform vec3  uAxisColorY;        // vertical line through the origin (x = 0, z = 0)
uniform vec3  uAxisColorZ;        // line along world Z (at x = 0)

uniform float uFadeStart;         // world-space ground distance where the fade begins
uniform float uFadeEnd;           // ... and where it reaches zero
uniform float uOpacity;
uniform float uGridPlane;         // 0 = ground XZ, 1 = XY (Front/Back), 2 = ZY (Left/Right)

out vec4 fragColor;

const float kLog10 = 0.43429448190325176; // 1 / ln(10)

// Anti-aliased coverage of the nearest grid line at `p`, for one spacing.
// `worldPerPixel` is how much world space a single pixel covers along each
// axis, so the line keeps a constant pixel width no matter how far away or how
// obliquely the ground is being viewed.
float GridCoverage(vec2 p, float spacing, vec2 worldPerPixel, float widthPixels) {
    vec2 halfWidth = worldPerPixel * widthPixels * 0.5;
    vec2 distToLine = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 coverage = 1.0 - clamp(distToLine / max(halfWidth, vec2(1e-9)), 0.0, 1.0);
    return max(coverage.x, coverage.y);
}

// Coverage of a single line at coord == 0 (the world axes).
// Box-filtered in pixels, like the gizmo strokes: a solid core `widthPixels` wide and a one-pixel
// feather. The earlier tent (1 - d / halfWidth) had no core, so a line that happened to fall on a
// pixel boundary - the screen centre of every orthographic axis view - came out as two half-strength
// rows, reading as a grey line instead of the axis colour.
float AxisCoverage(float coord, float worldPerPixel, float widthPixels) {
    float distancePixels = abs(coord) / max(worldPerPixel, 1e-9);
    return clamp(widthPixels * 0.5 + 0.5 - distancePixels, 0.0, 1.0);
}

// Composite `src` on top of the accumulated `dst`.
vec4 Over(vec4 dst, vec3 srcColor, float srcAlpha) {
    srcAlpha = clamp(srcAlpha, 0.0, 1.0);
    return vec4(mix(dst.rgb, srcColor, srcAlpha),
                dst.a + srcAlpha * (1.0 - dst.a));
}

// Coverage of a world axis - the line through the origin along unit vector `axis` - for this
// pixel's ray, from the ray's closest approach to the line. Used for all three axes so each is
// drawn as a real line in space: X and Z used to be read off the ground-plane hit, which does not
// exist when the ground is seen edge-on (Front, Back, Left, Right), so those views lost them.
//
// The distance is measured SIGNED - projected onto the direction perpendicular to both the axis
// and the ray - rather than with length(). length() has a kink at the line, its screen-space
// derivative collapses for a 2x2 quad straddling it, and the line then flickers in and out;
// the signed value is smooth through zero. Nothing here branches or discards, so the derivative
// quad stays valid. A ray (nearly) parallel to the axis sees it as a single point, so the line
// fades out there instead of smearing across the view.
float AxisLineCoverage(vec3 nearPoint, vec3 rayDir, vec3 axis, float widthPixels, out vec3 closestPoint) {
    vec3 nearPerp = nearPoint - dot(nearPoint, axis) * axis;
    vec3 rayPerp = rayDir - dot(rayDir, axis) * axis;
    float t = clamp(-dot(nearPerp, rayPerp) / max(dot(rayPerp, rayPerp), 1e-9), -1e6, 1e6);
    closestPoint = nearPoint + t * rayDir;
    vec3 side = cross(axis, rayDir);
    float sideLength = max(length(side), 1e-9);
    float signedDistance = dot(closestPoint, side) / sideLength;
    float worldPerPixel = max(length(vec2(dFdx(signedDistance), dFdy(signedDistance))), 1e-9);
    float notAlongRay = smoothstep(0.02, 0.08, sideLength / max(length(rayDir), 1e-9));
    return AxisCoverage(signedDistance, worldPerPixel, widthPixels) * (t > 0.0 ? 1.0 : 0.0) * notAlongRay;
}

// The grid maths below is written for the ground: a plane y = 0 with its lines in x and z. The
// other planes reuse it by relabelling the axes so the plane's normal lands in the "y" slot -
// ToGridPlane() into that space, FromGridPlane() back to the world. Both are pure swizzles.
vec3 ToGridPlane(vec3 v) {
    if (uGridPlane > 1.5) return vec3(v.z, v.x, v.y); // ZY plane, normal X
    if (uGridPlane > 0.5) return vec3(v.x, v.z, v.y); // XY plane, normal Z
    return v;
}

vec3 FromGridPlane(vec3 p) {
    if (uGridPlane > 1.5) return vec3(p.y, p.z, p.x);
    if (uGridPlane > 0.5) return vec3(p.x, p.z, p.y);
    return p;
}

void main() {
    // The divide happens here, per fragment: see infinite_grid.vert for why it cannot happen there.
    vec3 vNearPoint = vNearPointH.xyz / vNearPointH.w;
    vec3 vFarPoint = vFarPointH.xyz / vFarPointH.w;
    vec3 rayDir = vFarPoint - vNearPoint;

    // Ray/plane intersection with y = 0.
    //
    // Two deliberate details here. First, the division is guarded so worldPos
    // is finite for EVERY pixel, including ones whose ray is parallel to the
    // ground: an inf or NaN would otherwise leak sideways through dFdx/dFdy
    // into a perfectly valid neighbouring pixel and paint garbage along the
    // horizon. Second, nothing is discarded until after the derivatives have
    // been taken — derivatives are only well defined when the whole 2x2 quad
    // is still live, so an early `discard` here would make the LOD undefined
    // exactly where the grid is most stretched.
    vec3 planeNear = ToGridPlane(vNearPoint);
    vec3 planeRay = ToGridPlane(rayDir);
    float denom = planeRay.y;
    // A ray running along the ground - the horizon row of an eye-level view, or every row of an
    // orthographic side view - has no usable hit. Dividing by its near-zero y threw worldPos out to
    // ~1e10, the grid maths on that came back NaN, and a NaN defeated every comparison below:
    // the depth then came from that far-away point and the whole row, axis lines included, failed
    // the depth test. isnan() cannot be relied on to catch it (compilers may assume no NaNs), so
    // the hit is kept finite here - the near point stands in - and its contribution zeroed.
    float groundHitValid = abs(denom) > 1e-6 * length(rayDir) ? 1.0 : 0.0;
    float safeDenom = groundHitValid > 0.5 ? denom : 1.0;
    float t = groundHitValid > 0.5 ? clamp(-planeNear.y / safeDenom, -1e6, 1e6) : 0.0;
    // Only a hit in front of the near plane is a hit. With the eye on the ground itself (an
    // eye-level side view) the near point is already past the plane and the solution lands
    // behind it, between eye and near plane; drawn, that bogus grid won every pixel's depth.
    groundHitValid *= step(0.0, t);
    // In grid-plane space: .xz are the grid's two line directions, .y is (zero) height off it.
    vec3 worldPos = planeNear + t * planeRay;

    // ---- how much world space does one pixel cover here? ------------------
    vec2 worldPerPixel = vec2(
        length(vec2(dFdx(worldPos.x), dFdy(worldPos.x))),
        length(vec2(dFdx(worldPos.z), dFdy(worldPos.z)))
    );
    float pixelWorld = max(max(worldPerPixel.x, worldPerPixel.y), 1e-9);

    // ---- world axes, each as a line in space (see AxisLineCoverage) --------
    vec3 axisXClosestPoint;
    vec3 axisYClosestPoint;
    vec3 axisZClosestPoint;
    float axisX = AxisLineCoverage(vNearPoint, rayDir, vec3(1.0, 0.0, 0.0), uAxisWidthPixels, axisXClosestPoint);
    float axisY = AxisLineCoverage(vNearPoint, rayDir, vec3(0.0, 1.0, 0.0), uAxisWidthPixels, axisYClosestPoint);
    float axisZ = AxisLineCoverage(vNearPoint, rayDir, vec3(0.0, 0.0, 1.0), uAxisWidthPixels, axisZClosestPoint);
    // An axis that passes through the eye - the X axis in a Left or Right view, Z in Front or Back,
    // Y in Top or Bottom - is a single vanishing point on screen, not a line. Every ray starts on
    // it, so without this it "hit" at the eye itself, behind the near plane, and that depth failed
    // the depth test for the whole view, taking the other axes with it.
    float eyeScale = max(uFadeStart, 1e-6) * 1e-3;
    axisX *= step(eyeScale, length(axisXClosestPoint - uCameraPos));
    axisY *= step(eyeScale, length(axisYClosestPoint - uCameraPos));
    axisZ *= step(eyeScale, length(axisZClosestPoint - uCameraPos));

    // ---- pick the decade of spacing, and how far through it we are --------
    // The upper clamp keeps pow(10, floor(lod)) finite for the stretched
    // pixels right at the horizon; 10^20 world units is far past any scene,
    // so it never constrains a spacing anyone will actually see.
    float lod = clamp(log(pixelWorld * uTargetCellPixels / uBaseSpacing) * kLog10, 0.0, 20.0);
    float lodFade = fract(lod);
    float spacing0 = uBaseSpacing * pow(10.0, floor(lod));
    float spacing1 = spacing0 * 10.0;
    float spacing2 = spacing1 * 10.0;
    float spacing3 = spacing2 * 10.0;

    float cov0 = GridCoverage(worldPos.xz, spacing0, worldPerPixel, uLineWidthPixels);
    float cov1 = GridCoverage(worldPos.xz, spacing1, worldPerPixel, uLineWidthPixels);
    float cov2 = GridCoverage(worldPos.xz, spacing2, worldPerPixel, uLineWidthPixels);
    float cov3 = GridCoverage(worldPos.xz, spacing3, worldPerPixel, uLineWidthPixels);

    // Each tier slides one step "finer" in appearance as lodFade goes 0 -> 1,
    // so at the moment the LOD ticks over, tier N looks exactly like tier N-1
    // did an instant earlier and nothing pops.
    vec3  color0 = uThinColor;                        float alpha0 = uThinIntensity * (1.0 - lodFade);
    vec3  color1 = mix(uMidColor,   uThinColor, lodFade); float alpha1 = mix(uMidIntensity,   uThinIntensity, lodFade);
    vec3  color2 = mix(uThickColor, uMidColor,  lodFade); float alpha2 = mix(uThickIntensity, uMidIntensity,  lodFade);
    vec3  color3 = uThickColor;                       float alpha3 = uThickIntensity * lodFade;

    vec4 accum = vec4(uThinColor, 0.0);
    accum = Over(accum, color0, cov0 * alpha0);
    accum = Over(accum, color1, cov1 * alpha1);
    accum = Over(accum, color2, cov2 * alpha2);
    accum = Over(accum, color3, cov3 * alpha3);

    // ---- horizon fade for the ground-plane grid ------------------------------
    float groundDist = length(worldPos.xz - ToGridPlane(uCameraPos).xz);
    float groundFade = 1.0 - smoothstep(uFadeStart, uFadeEnd, groundDist);
    float groundAlpha = accum.a * groundFade * uOpacity;
    groundAlpha *= groundHitValid;

    // ---- axes: each fades by its own distance from the camera --------------
    // Not by the ground hit: that is unrelated to how far along an axis this pixel is, and for
    // the Y axis (or any axis seen edge-on) there may be no ground hit at all.
    float axisXAlpha = axisX * (1.0 - smoothstep(uFadeStart, uFadeEnd, length(axisXClosestPoint - uCameraPos))) * uOpacity;
    float axisYAlpha = axisY * (1.0 - smoothstep(uFadeStart, uFadeEnd, length(axisYClosestPoint - uCameraPos))) * uOpacity;
    float axisZAlpha = axisZ * (1.0 - smoothstep(uFadeStart, uFadeEnd, length(axisZClosestPoint - uCameraPos))) * uOpacity;

    vec4 finalColor = vec4(accum.rgb, groundAlpha);
    finalColor = Over(finalColor, uAxisColorX, axisXAlpha);
    finalColor = Over(finalColor, uAxisColorZ, axisZAlpha);
    finalColor = Over(finalColor, uAxisColorY, axisYAlpha);
    float finalAlpha = finalColor.a;
    if (finalAlpha < 0.002) discard;   // nothing to show; don't touch depth either

    // ---- real depth, so the grid composites with scene geometry -----------
    // Whichever content wins this pixel supplies the depth - the ground hit for the grid, or the
    // closest point on whichever axis line is strongest here.
    vec3 depthSourcePos = FromGridPlane(worldPos);
    float strongest = groundAlpha;
    if (axisXAlpha > strongest) { strongest = axisXAlpha; depthSourcePos = axisXClosestPoint; }
    if (axisZAlpha > strongest) { strongest = axisZAlpha; depthSourcePos = axisZClosestPoint; }
    if (axisYAlpha > strongest) { strongest = axisYAlpha; depthSourcePos = axisYClosestPoint; }
    vec4 clip = uViewProj * vec4(depthSourcePos, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    fragColor = vec4(finalColor.rgb, finalAlpha);
}
