// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_prism_icons.h"

#include "prism/Gizmo.h"
#include "prism/Widget.h"

#include <array>
#include <cstdint>
#include <vector>

namespace UVE::Editor {

/// Every viewport gizmo, drawn by PRISM over the engine's image of the scene (stacked on the
/// viewport's EngineView). The data and the actions are the engine's:
///
///   * the camera, the selection pivot and the object gizmo geometry (camera frustums, light
///     directions, fog volumes, probes, partitions, visibility regions, occluders, colliders,
///     bones) come from the viewport renderer through ViewportOverlayStateUVE's output fields;
///   * the transform gizmo drives the editor's gesture API (Begin / PreviewTranslate /
///     PreviewTransform / CommitTransformGestureUVE), so parent space, snapping and undo are the
///     editor's own;
///   * the navigation gizmo snaps the view with RequestViewportViewUVE and orbits by handing the
///     renderer pixel deltas (orbitRequestX/Y);
///   * light and camera billboards use the engine's object icons and select their object;
///   * the ground grid (the renderer's grid settings) and the selection outline (the renderer's
///     silhouette mask) are drawn here too, and everything but the transform and navigation
///     gizmos hides behind the scene through the renderer's exported scene depth.
///
/// The widget claims the pointer only over a handle, so the camera and picking keep every other
/// click (consumesHostInput).
class EditorPrismViewportGizmosUVE final : public prism::Widget {
public:
    explicit EditorPrismViewportGizmosUVE(EditorUVE& editor);

    /// Refreshes the camera, pivot and billboards from the view the renderer just produced.
    void SyncUVE();
    /// Pixels the navigation gizmo wants the view orbited by this frame (consumed on read).
    [[nodiscard]] std::array<float, 2> TakeOrbitRequestUVE();

    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    bool consumesHostInput() const override { return m_hot || m_dragging != DragUVE::None; }
    bool focusable() const override { return true; }

private:
    enum class DragUVE : std::uint8_t { None, Transform, Nav };
    struct BillboardUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        prism::Vec3 position;
        EditorPrismIconsUVE::ImageUVE icon;
    };

    [[nodiscard]] bool UpdateCameraUVE();
    void UpdateHoverUVE(prism::Vec2 p);
    [[nodiscard]] int BillboardAtUVE(prism::Vec2 p) const;
    [[nodiscard]] bool TransformGizmoActiveUVE() const;

    EditorUVE& m_editor;
    EditorPrismIconsUVE m_icons;
    prism::GizmoCamera m_camera;
    bool m_cameraValid = false;
    prism::TransformGizmo m_gizmo;
    prism::NavGizmo m_nav;
    std::vector<BillboardUVE> m_billboards;

    bool m_hot = false;
    DragUVE m_dragging = DragUVE::None;
    prism::Vec2 m_navPress{0.0F, 0.0F};
    prism::Vec2 m_navLast{0.0F, 0.0F};
    bool m_navMoved = false;
    int m_navPressedBall = -1;
    std::array<float, 2> m_orbitRequest{0.0F, 0.0F};
    // Scene depth (occlusion), the ground grid and the selection outline band for this view.
    prism::GizmoDepth m_depth;
    prism::WorldGrid m_grid;
    bool m_gridVisible = false;
    std::vector<std::uint32_t> m_outlinePixels;
    prism::Rect m_outlineRect{};
    std::array<int, 2> m_outlineSize{0, 0};
    Math::Vector3UVE m_baselineScale{1.0F, 1.0F, 1.0F};
};

} // namespace UVE::Editor
