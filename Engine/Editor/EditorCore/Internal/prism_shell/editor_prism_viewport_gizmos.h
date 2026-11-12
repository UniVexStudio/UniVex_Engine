// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_prism_icons.h"

#include "prism/Gizmo.h"
#include "prism/Widget.h"

#include <array>
#include <cstdint>
#include <utility>
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
    /// The PRISM glyph an object is drawn with, from its own components.
    enum class GlyphUVE : std::uint8_t { Icon, Camera, DirectionalLight, SpotLight };
    struct BillboardUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        prism::Vec3 position;
        EditorPrismIconsUVE::ImageUVE icon;
        GlyphUVE glyph = GlyphUVE::Icon;
        prism::Vec3 forward{0.0F, 0.0F, -1.0F};
        prism::Vec3 up{0.0F, 1.0F, 0.0F};
        float fovYDegrees = 60.0F;
        bool orthographic = false;
        float spotHalfAngleDegrees = 45.0F;
        float range = 10.0F;
        prism::Color tint{230, 232, 240};
        /// Screen segments the glyph was last drawn with (picking).
        std::vector<std::pair<prism::Vec2, prism::Vec2>> segments;
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
    std::vector<float> m_depthCopy;
    std::vector<EditorUVE::ViewportOverlayStateUVE::GizmoLineUVE> m_lines;
    std::vector<EditorUVE::ViewportOverlayStateUVE::GizmoTriangleUVE> m_triangles;
    float m_aspect = 1.6F;
    bool m_gameWorkspace = false;
    prism::WorldGrid m_grid;
    bool m_gridVisible = false;
    std::vector<std::uint32_t> m_outlinePixels;
    prism::Rect m_outlineRect{};
    std::array<int, 2> m_outlineSize{0, 0};
    Math::Vector3UVE m_baselineScale{1.0F, 1.0F, 1.0F};
};

} // namespace UVE::Editor
