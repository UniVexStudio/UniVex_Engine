// app/ViewportRenderPass.h
// -----------------------------------------------------------------------
// One frame of the viewport's own non-scene layers, in draw order:
//
//   background gradient  (View Environment)
//   infinite ground grid (View Grid)
//   transform gizmo      (View Transform Gizmo)   - on top, no depth test
//   orientation gizmo    (View Gizmos)            - own corner viewport
//
// Scene geometry is NOT drawn here: entities are rendered by the host
// engine's own renderer (Renderer3DUVE, via EditorMeshLayerUVE) and
// composited against this image. This pass owns only the things the
// renderer has no concept of - the editor's grid, gizmos and backdrop.
//
// Both the interactive demo and the headless capture tool render through
// this same function on purpose — a screenshot is only evidence about the
// app if it came out of the same code path the app runs.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <utility>
#include <vector>
#include <string>

#include "univex/camera/OrbitCamera.h"
#include "univex/gizmo/BoneShape.h"
#include "univex/gizmo/GizmoGeometry.h"
#include "univex/gizmo/GizmoStyle.h"
#include "univex/gizmo/NavGizmo.h"
#include "univex/render/GizmoRenderer.h"
#include "univex/render/InfiniteGridRenderer.h"
#include "univex/render/SelectionOutline.h"
#include "univex/render/SelectionOutlineRenderer.h"
#include "univex/viewport/AxisPaletteApply.h"
#include "univex/viewport/ViewportSettings.h"

namespace univex::app {

using univex::camera::OrbitCamera;
using univex::gizmo::GizmoMode;
using univex::gizmo::GizmoStyle;
using univex::math::Mat4;
using univex::math::Vec3;

// Where the orientation gizmo lives, in OpenGL viewport coordinates
// (origin bottom-left).
struct NavViewportRect {
    int x = 0;
    int y = 0;
    int size = 0;
};

class ViewportRenderPass {
public:
    ViewportRenderPass() = default;
    ~ViewportRenderPass();

    ViewportRenderPass(const ViewportRenderPass&) = delete;
    ViewportRenderPass& operator=(const ViewportRenderPass&) = delete;
    ViewportRenderPass(ViewportRenderPass&& other) noexcept;
    ViewportRenderPass& operator=(ViewportRenderPass&& other) noexcept;

    [[nodiscard]] static std::optional<ViewportRenderPass> Create(std::string& outError);

    // Clears, then runs every layer in order. For hosts that draw nothing of
    // their own - the standalone demo and the headless capture tool.
    void RenderFrame(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight) const;

    // The same layers, individually, for a host that has to interleave its own
    // rendering between them. The editor draws the engine's real scene geometry
    // between the background and the grid so all three share one depth buffer:
    // the grid then depth-tests against actual meshes instead of being reconciled
    // with them afterwards, and the gizmos - which must never be occluded by the
    // object they manipulate - go last of all.
    //
    // Expected order: ClearUVE, RenderBackgroundUVE, [host scene geometry],
    // RenderGridUVE, RenderOverlayUVE.
    void ClearUVE(int framebufferWidth, int framebufferHeight) const;
    void RenderBackgroundUVE() const;
    void RenderGridUVE(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight) const;
    void RenderOverlayUVE(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight) const;

    [[nodiscard]] univex::render::InfiniteGridRenderer& Grid() { return grid_; }
    [[nodiscard]] const univex::render::InfiniteGridRenderer& Grid() const { return grid_; }

    [[nodiscard]] univex::viewport::ViewportSettings& Settings() { return settings_; }
    [[nodiscard]] const univex::viewport::ViewportSettings& Settings() const { return settings_; }

    [[nodiscard]] GizmoStyle& Style() { return style_; }
    [[nodiscard]] const GizmoStyle& Style() const { return style_; }

    // ---- axis colours -----------------------------------------------------
    // The one way to change the X/Y/Z hues, because they have two consumers that must not drift:
    // the gizmo's own axes and the grid's axis lines, which run through the same origin. Setting
    // both here, with the grid taking AxisPaletteUVE's darker variant, keeps that relationship a
    // single rule rather than something each caller has to remember.
    //
    // An invalid palette (a channel outside 0..1, or NaN - see IsAxisPaletteValidUVE) is refused
    // outright and nothing changes, so a corrupt settings file cannot leave the viewport drawing
    // axes in colours nobody picked. Returns whether the palette was applied.
    bool SetAxisPaletteUVE(const univex::viewport::AxisPaletteUVE& palette);
    /// Overall strength of the ground grid, clamped to 0..1.
    void SetGridOpacityUVE(float opacity);
    /// Which world plane the grid is drawn on (see univex::render::GridPlane).
    void SetGridPlaneUVE(univex::render::GridPlane plane);
    /// The grid's finest spacing in world units (GridSettings::baseSpacing). A size that is not
    /// finite or not positive is ignored and the grid keeps its current spacing.
    void SetGridCellSizeUVE(float cellSize);
    /// How the selection outline looks (colour clamped to 0..1, thickness to 1..6 px).
    void SetSelectionOutlineUVE(const univex::render::SelectionOutlineSettings& settings);
    /// Draws the outline of the selected meshes' world-space triangles. Call after the scene and
    /// the grid, before RenderOverlayUVE, so the gizmos stay on top.
    void RenderSelectionOutlineUVE(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight,
                                   const std::vector<univex::render::SelectionOutlineVertex>& triangles);
    [[nodiscard]] univex::viewport::AxisPaletteUVE GetAxisPaletteUVE() const;

    void SetGizmoMode(GizmoMode mode) { gizmoMode_ = mode; }
    [[nodiscard]] GizmoMode Mode() const { return gizmoMode_; }

    // The transform gizmo's world-space pivot defaults to the camera's own orbit target - fine
    // for this module's own standalone demo (there is no independent "selected object" concept
    // there), but wrong once a host editor drives selection: orbiting the camera must not drag
    // the gizmo along with it. When set, the gizmo draws at `pivot` regardless of where the
    // camera is currently looking; pass nullopt to restore the original camera-target behavior.
    void SetGizmoPivotOverride(std::optional<Vec3> pivot) { gizmoPivotOverride_ = pivot; }

    // The Skeleton3D bones to draw this frame, in world space (empty: none). Drawn over the
    // scene, under the transform gizmo, so a rig inside its mesh is still visible and editable.
    void SetBonesUVE(std::vector<univex::gizmo::BoneOverlayUVE> bones) { bones_ = std::move(bones); }

    // ---- nav gizmo geometry, shared with input handling -------------------
    // The nav gizmo's own camera: the main camera's rotation, no translation.
    [[nodiscard]] static Mat4 NavViewMatrix(const OrbitCamera& camera);
    [[nodiscard]] static Mat4 NavViewProjection(const GizmoStyle& style, const OrbitCamera& camera);
    [[nodiscard]] static NavViewportRect NavViewportRectFor(const GizmoStyle& style,
                                                            int framebufferWidth,
                                                            int framebufferHeight);

private:
    void DrawBackground() const;
    void DrawTransformGizmo(const OrbitCamera& camera, int width, int height) const;
    void DrawBones(const OrbitCamera& camera, int width, int height) const;
    void DrawNavGizmo(const OrbitCamera& camera, int width, int height) const;
    void Destroy() noexcept;

    univex::render::InfiniteGridRenderer grid_;
    univex::render::GizmoRenderer gizmos_;
    // Optional: a driver that cannot build its shaders loses the outline, not the viewport.
    std::optional<univex::render::SelectionOutlineRenderer> selectionOutline_;
    univex::render::SelectionOutlineSettings selectionOutlineSettings_{};
    univex::render::ShaderProgram backgroundProgram_;
    GLuint backgroundVao_ = 0;
    GLuint backgroundVbo_ = 0;

    univex::viewport::ViewportSettings settings_{};
    GizmoStyle style_{};
    GizmoMode gizmoMode_ = GizmoMode::Universal;
    std::optional<Vec3> gizmoPivotOverride_;
    std::vector<univex::gizmo::BoneOverlayUVE> bones_;
};

} // namespace univex::app
