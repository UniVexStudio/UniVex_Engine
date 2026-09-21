// univex/render/GizmoRenderer.h
// -----------------------------------------------------------------------
// Draws a GizmoMesh: line segments expanded into screen-space quads, and
// filled triangles. One instance serves both the transform gizmo and the
// corner nav gizmo — they differ only in the view-projection, viewport and
// placement passed per draw.
//
// Gizmos draw with the depth test off by default. An editor handle that
// disappears inside the object it is manipulating is worse than useless,
// so the widget is composited on top of the scene rather than into it.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>

#include "univex/camera/OrbitCamera.h"
#include "univex/gizmo/GizmoGeometry.h"
#include "univex/math/Mat4.h"
#include "univex/render/GlApi.h"
#include "univex/render/ShaderProgram.h"

namespace univex::render {

using univex::gizmo::GizmoMesh;
using univex::math::Mat4;
using univex::math::Vec3;

struct GizmoDrawParams {
    Mat4 viewProjection = Mat4::Identity();
    Vec3 origin{};              // where the widget's own origin sits in world space
    float scale = 1.f;          // gizmo units -> world units
    float viewportWidth = 1.f;  // pixels; the line pass needs it to size quads
    float viewportHeight = 1.f;
    float opacity = 1.f;

    // Gizmos normally draw over the scene, but they still need to occlude
    // THEMSELVES — without it a solid handle cube is see-through, because
    // its own back faces paint over its front ones. The caller clears the
    // depth buffer before the pass and turns both of these on.
    //
    // `depthWrite` applies to OPAQUE solids only. Translucent ones always test depth without
    // writing it: a plane handle at alpha 0.22, or the faded far half of a rotation ring, used to
    // punch the depth buffer and then reject every handle behind it regardless of how see-through
    // it was — which is what made the widget's overlapping parts look cut apart rather than
    // layered.
    bool depthTest = false;
    bool depthWrite = false;

    // Points from the eye into the scene. Used to sort the translucent solids back to front,
    // since blending is order-dependent and they no longer have depth writes to order them.
    Vec3 viewDirection{0.f, 0.f, -1.f};

    // Painter's order for passes that have no depth buffer to sort them:
    // the nav gizmo wants its axis stubs under its balls.
    bool drawLinesFirst = false;
};

class GizmoRenderer {
public:
    GizmoRenderer() = default;
    ~GizmoRenderer();

    GizmoRenderer(const GizmoRenderer&) = delete;
    GizmoRenderer& operator=(const GizmoRenderer&) = delete;
    GizmoRenderer(GizmoRenderer&& other) noexcept;
    GizmoRenderer& operator=(GizmoRenderer&& other) noexcept;

    [[nodiscard]] static std::optional<GizmoRenderer> Create(std::string& outError);

    [[nodiscard]] bool Valid() const { return lineProgram_.Valid() && solidProgram_.Valid(); }

    // Uploads and draws in one call. The mesh is small (a few hundred
    // segments) and changes every frame anyway as the near-side arcs
    // follow the camera, so streaming it is cheaper than trying to cache it.
    void Draw(const GizmoMesh& mesh, const GizmoDrawParams& params) const;

    // The world-space scale that makes a gizmo of `gizmoPixelRadius` pixels, given the camera,
    // the viewport height and THE PIVOT THE WIDGET IS DRAWN AT. Gizmo units are authored so the
    // outermost handle sits at ~1.9 units from the pivot.
    //
    // The pivot is a parameter rather than an assumption because it is routinely not the camera's
    // orbit target: the transform gizmo sits on the selected node, and orbiting the view leaves
    // that node wherever it was. Sizing from the orbit distance instead made the widget grow and
    // shrink with the camera rather than with its own depth.
    [[nodiscard]] static float ScaleForPixelRadius(const univex::camera::OrbitCamera& camera,
                                                   int framebufferHeight,
                                                   float gizmoPixelRadius,
                                                   const Vec3& pivot);

private:
    // Which half of the solid geometry a triangle pass covers. They are drawn as two passes
    // with different depth-mask and ordering rules; see GizmoDrawParams::depthWrite.
    enum class TrianglePassUVE { Opaque, Translucent };

    void Destroy() noexcept;
    void UploadAndDrawLines(const GizmoMesh& mesh, const GizmoDrawParams& params) const;
    void UploadAndDrawTriangles(const GizmoMesh& mesh, const GizmoDrawParams& params,
                                TrianglePassUVE pass) const;

    ShaderProgram lineProgram_;
    ShaderProgram solidProgram_;
    GLuint lineVao_ = 0, lineVbo_ = 0;
    GLuint solidVao_ = 0, solidVbo_ = 0;
};

} // namespace univex::render
