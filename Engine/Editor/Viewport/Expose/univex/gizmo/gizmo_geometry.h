// univex/gizmo/GizmoGeometry.h
// -----------------------------------------------------------------------
// Overlay geometry as plain data: line segments (which the renderer turns
// into screen-space quads - real triangles, never GL_LINES) and solid
// triangles. Bones, collision wireframes, camera frustums and the other
// object helpers are built into a GizmoMesh. Nothing here knows about OpenGL.
// -----------------------------------------------------------------------
#pragma once

#include <vector>

#include "univex/gizmo/gizmo_style.h"
#include "univex/math/vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

struct GizmoLine {
    Vec3 a{}, b{};
    Vec3 color{};
    float widthPx = 2.f;
};

struct GizmoTriangle {
    Vec3 a{}, b{}, c{};
    Vec3 color{};
    float alpha = 1.f;
    // How much real lighting the face takes: 0 for flat UI shapes (rings, plane handles, discs),
    // which keep a gentle fixed shade so their colour never changes with the angle; 1 for solid
    // bodies (shafts, cones, cubes, spheres, bones), lit from both sides so they read as 3D.
    float lit = 0.f;
};

struct GizmoMesh {
    std::vector<GizmoLine> lines;
    std::vector<GizmoTriangle> triangles;

    void Clear() { lines.clear(); triangles.clear(); }
    [[nodiscard]] bool Empty() const { return lines.empty() && triangles.empty(); }
};

} // namespace univex::gizmo
