// univex/gizmo/SolidPrimitives.h
// -----------------------------------------------------------------------
// The lit solid shapes every editor widget is built from: cylinder, cone,
// sphere. Shared so the transform gizmo, the nav gizmo and the skeleton's
// bones are one family - same tessellation, same lighting flag - rather than
// three hand-rolled variants that drift apart.
//
// Every triangle comes out with GizmoTriangle::lit = 1. `cullToward`, when
// given, drops triangles whose outward normal faces away from the eye; the
// nav gizmo draws without a depth buffer, and a closed shape painted without
// culling shows its far side through its near one.
// -----------------------------------------------------------------------
#pragma once

#include <optional>

#include "univex/gizmo/GizmoGeometry.h"
#include "univex/math/Vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

/// Open tube from `a` to `b`.
void AddSolidCylinderUVE(GizmoMesh& mesh, const Vec3& a, const Vec3& b, float radius, int segments,
                         const Vec3& color, float alpha = 1.f, std::optional<Vec3> cullToward = std::nullopt);

/// Cone from a capped circular base at `base` to a point at `tip`.
void AddSolidConeUVE(GizmoMesh& mesh, const Vec3& base, const Vec3& tip, float radius, int segments,
                     const Vec3& color, float alpha = 1.f, std::optional<Vec3> cullToward = std::nullopt);

/// UV sphere; `rings` latitude bands, `segments` longitude slices.
void AddSolidSphereUVE(GizmoMesh& mesh, const Vec3& center, float radius, int rings, int segments,
                       const Vec3& color, float alpha = 1.f, std::optional<Vec3> cullToward = std::nullopt);

/// Axis-aligned box, all six faces lit.
void AddSolidBoxUVE(GizmoMesh& mesh, const Vec3& center, float size, const Vec3& color, float alpha = 1.f);

} // namespace univex::gizmo
