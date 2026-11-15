// app/ViewportRenderPass.h
// -----------------------------------------------------------------------
// One frame of the viewport's own non-scene layers, in draw order:
//
//   background gradient  (View Environment)
//   infinite ground grid (View Grid)
//   bones, collision wireframes and camera frustums
//
// Scene geometry is NOT drawn here: entities are rendered by the host
// engine's own renderer (Renderer3DUVE, via EditorMeshLayerUVE) and
// composited against this image. This pass owns only the things the
// renderer has no concept of - the editor's grid, overlays and backdrop.
// The transform and orientation gizmos are the editor shell's own.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <utility>
#include <cstdint>
#include <vector>
#include <string>

#include "univex/camera/orbit_camera.h"
#include "univex/gizmo/bone_shape.h"
#include "univex/gizmo/gizmo_geometry.h"
#include "univex/gizmo/gizmo_style.h"
#include "univex/render/gizmo_renderer.h"
#include "univex/render/infinite_grid_renderer.h"
#include "univex/render/selection_outline.h"
#include "univex/render/selection_outline_renderer.h"
#include "univex/viewport/axis_palette_apply.h"
#include "univex/viewport/viewport_settings.h"

namespace univex::app {

using univex::camera::OrbitCamera;
using univex::gizmo::GizmoStyle;
using univex::math::Mat4;
using univex::math::Vec3;

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
    /// `neutralEditorFallback` is reserved for Main's empty authoring scene. It must not alter
    /// Entity Editor, Retarget, standalone viewport, or authored WorldEnvironment presentation.
    void RenderBackgroundUVE(bool neutralEditorFallback = false) const;
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
    /// The selection silhouette mask only, read back (top-down, one byte per pixel), for a host
    /// that draws the outline band itself. False when there is nothing to outline.
    [[nodiscard]] bool ReadSelectionMaskUVE(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight,
                                            const std::vector<univex::render::SelectionOutlineVertex>& triangles,
                                            std::vector<std::uint8_t>& outMask);
    [[nodiscard]] const univex::render::SelectionOutlineSettings& SelectionOutlineSettingsUVE() const {
        return selectionOutlineSettings_;
    }
    [[nodiscard]] univex::viewport::AxisPaletteUVE GetAxisPaletteUVE() const;

    // The Skeleton3D bones to draw this frame, in world space (empty: none). Drawn over the
    // scene, so a rig inside its mesh is still visible and editable.
    void SetBonesUVE(std::vector<univex::gizmo::BoneOverlayUVE> bones) { bones_ = std::move(bones); }

    void SetCameraFrustumMeshUVE(univex::gizmo::GizmoMesh mesh) { cameraFrustumMesh_ = std::move(mesh); }

    // Collision shapes to draw this frame (the standard "Show Collision"): world-space line segments,
    // empty for none. Unlike the bones, these are DEPTH-TESTED against the scene and drawn before
    // the depth buffer is cleared - a collider behind a wall is behind a wall, which is the whole
    // point of looking at it from the camera the player will use.
    void SetColliderWireframesUVE(std::vector<univex::gizmo::GizmoLine> lines) {
        colliderLines_ = std::move(lines);
    }

private:
    void DrawBackground() const;
    void DrawBones(const OrbitCamera& camera, int width, int height) const;
    void DrawColliderWireframes(const OrbitCamera& camera, int width, int height) const;
    void DrawCameraFrustums(const OrbitCamera& camera, int width, int height) const;
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
    std::vector<univex::gizmo::BoneOverlayUVE> bones_;
    std::vector<univex::gizmo::GizmoLine> colliderLines_;
    univex::gizmo::GizmoMesh cameraFrustumMesh_;
};

} // namespace univex::app
