// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// Spline sampling: cubic Bézier plus uniform Catmull-Rom, as pure functions — the math half of
/// the engine's curve story (the authored half is `Asset::CurveAssetUVE`). Camera paths,
/// projectile arcs and motion trails sample these directly; `t` in [0, 1] walks one segment.
/// Extrapolation past the ends is defined (the polynomials extend) but callers clamp.

/// A cubic Bézier point: `p0` at `t == 0`, `p3` at `t == 1`, `p1`/`p2` shaping the tangents.
[[nodiscard]] inline Vector2UVE CubicBezierUVE(const Vector2UVE& p0, const Vector2UVE& p1,
                                              const Vector2UVE& p2, const Vector2UVE& p3,
                                              const float t) noexcept {
    const float u = 1.0F - t;
    const float a = u * u * u;
    const float b = 3.0F * u * u * t;
    const float c = 3.0F * u * t * t;
    const float d = t * t * t;
    return Vector2UVE{a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                      a * p0.y + b * p1.y + c * p2.y + d * p3.y};
}

/// 3D cubic Bézier.
[[nodiscard]] inline Vector3UVE CubicBezierUVE(const Vector3UVE& p0, const Vector3UVE& p1,
                                              const Vector3UVE& p2, const Vector3UVE& p3,
                                              const float t) noexcept {
    const float u = 1.0F - t;
    const float a = u * u * u;
    const float b = 3.0F * u * u * t;
    const float c = 3.0F * u * t * t;
    const float d = t * t * t;
    return Vector3UVE{a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                      a * p0.y + b * p1.y + c * p2.y + d * p3.y,
                      a * p0.z + b * p1.z + c * p2.z + d * p3.z};
}

/// A uniform Catmull-Rom point on the segment `p1 → p2`, with `p0`/`p3` shaping the end
/// tangents (the standard 0.5-tension form). Stringing segments shares tangents, so a chain of
/// these is C1 through every interior point.
[[nodiscard]] inline Vector2UVE CatmullRomUVE(const Vector2UVE& p0, const Vector2UVE& p1,
                                              const Vector2UVE& p2, const Vector2UVE& p3,
                                              const float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return Vector2UVE{0.5F * ((2.0F * p1.x + (-p0.x + p2.x) * t +
                               (2.0F * p0.x - 5.0F * p1.x + 4.0F * p2.x - p3.x) * t2 +
                               (-p0.x + 3.0F * p1.x - 3.0F * p2.x + p3.x) * t3)),
                      0.5F * ((2.0F * p1.y + (-p0.y + p2.y) * t +
                               (2.0F * p0.y - 5.0F * p1.y + 4.0F * p2.y - p3.y) * t2 +
                               (-p0.y + 3.0F * p1.y - 3.0F * p2.y + p3.y) * t3))};
}

/// 3D uniform Catmull-Rom.
[[nodiscard]] inline Vector3UVE CatmullRomUVE(const Vector3UVE& p0, const Vector3UVE& p1,
                                              const Vector3UVE& p2, const Vector3UVE& p3,
                                              const float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return Vector3UVE{
        0.5F * ((2.0F * p1.x + (-p0.x + p2.x) * t + (2.0F * p0.x - 5.0F * p1.x + 4.0F * p2.x - p3.x) * t2 +
                 (-p0.x + 3.0F * p1.x - 3.0F * p2.x + p3.x) * t3)),
        0.5F * ((2.0F * p1.y + (-p0.y + p2.y) * t + (2.0F * p0.y - 5.0F * p1.y + 4.0F * p2.y - p3.y) * t2 +
                 (-p0.y + 3.0F * p1.y - 3.0F * p2.y + p3.y) * t3)),
        0.5F * ((2.0F * p1.z + (-p0.z + p2.z) * t + (2.0F * p0.z - 5.0F * p1.z + 4.0F * p2.z - p3.z) * t2 +
                 (-p0.z + 3.0F * p1.z - 3.0F * p2.z + p3.z) * t3))};
}

} // namespace UVE::Math
