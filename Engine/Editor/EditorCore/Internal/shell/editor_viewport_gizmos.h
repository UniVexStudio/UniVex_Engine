// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_icons.h"

#include "prism/Gizmo.h"
#include "prism/Widget.h"

#include <array>
#include <optional>
#include <cstdint>
#include <utility>
#include <vector>

namespace UVE::Editor {

/// Every viewport gizmo, drawn by PRISM over the engine's image of the scene (stacked on the
/// viewport's EngineView). The data and the actions are the engine's:
///
///   * the camera, the selection pivot and the object gizmo geometry (camera frustums, light
///     directions, fog volumes, probes, partitions, visibility regions, occluders, colliders,
///     bones) come from the viewport renderer through ViewportOverlayState's output fields;
///   * the transform gizmo drives the editor's gesture API (Begin / PreviewTranslate /
///     PreviewTransform / CommitTransformGesture), so parent space, snapping and undo are the
///     editor's own;
///   * the navigation gizmo snaps the view with RequestViewportView and orbits by handing the
///     renderer pixel deltas (orbitRequestX/Y);
///   * light and camera billboards use the engine's object icons and select their object;
///   * the ground grid (the renderer's grid settings) and the selection outline (the renderer's
///     silhouette mask) are drawn here too, and everything but the transform and navigation
///     gizmos hides behind the scene through the renderer's exported scene depth.
///
/// The widget claims the pointer only over a handle, so the camera and picking keep every other
/// click (consumesHostInput).
class EditorViewportGizmos final : public prism::Widget {
public:
    explicit EditorViewportGizmos(Editor& editor);
    ~EditorViewportGizmos() override;

    /// Refreshes the camera, pivot, marquee box, and billboards from the view the renderer just produced.
    void Sync();
    /// Samples the Android-only floating dual-stick touch controller into `state` (no-op on Desktop
    /// and auto-hidden whenever a hardware mouse or external keyboard is connected).
    void ApplyTouchControllerToState(Editor::ViewportOverlayState& state);
    /// Pixels the navigation gizmo wants the view orbited by this frame (consumed on read).
    [[nodiscard]] std::array<float, 2> TakeOrbitRequest();
    /// The world point under canvas point `p`: the scene surface there (from the last frame's
    /// depth), else where the ray meets the ground plane Y=0. Empty when the view has no camera yet
    /// or the ray points away from both.
    [[nodiscard]] std::optional<std::array<float, 3>> WorldPointUnder(prism::Vec2 p) const;
    /// Marks where dragged content would land (or clears it with nullopt); drawn as a small marker.
    void SetDropPreview(std::optional<std::array<float, 3>> worldPoint);

    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    bool consumesHostInput() const override { return m_hot || m_dragging != Drag::None; }
    bool focusable() const override { return true; }

private:
    enum class Drag : std::uint8_t { None, Transform, Nav };
    /// The PRISM glyph an object is drawn with, from its own components.
    enum class Glyph : std::uint8_t { Icon, Camera, DirectionalLight, SpotLight };
    struct Billboard final {
        Scene::Entity entity = Scene::kInvalidEntity;
        prism::Vec3 position;
        EditorIcons::Image icon;
        Glyph glyph = Glyph::Icon;
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

    struct TouchControllerLayout final {
        prism::Vec2 leftCenter{};
        prism::Vec2 rightCenter{};
        float padRadius = 40.0F;
        float thumbRadius = 16.0F;
        prism::Rect upBtn{};
        prism::Rect downBtn{};
        prism::Rect zoomInBtn{};
        prism::Rect zoomOutBtn{};
        prism::Rect toggleBtn{};
    };

    [[nodiscard]] bool UpdateCamera();
    void UpdateHover(prism::Vec2 p);
    [[nodiscard]] int BillboardAt(prism::Vec2 p) const;
    [[nodiscard]] bool TransformGizmoActive() const;
    [[nodiscard]] bool TouchControllerVisible() const;
    [[nodiscard]] TouchControllerLayout ComputeTouchLayout() const;
    [[nodiscard]] bool TryClaimAndroidTouch(float physicalX, float physicalY);

    Editor& m_editor;
    EditorIcons m_icons;
    prism::GizmoCamera m_camera;
    bool m_cameraValid = false;
    prism::TransformGizmo m_gizmo;
    prism::NavGizmo m_nav;
    std::vector<Billboard> m_billboards;

    bool m_hot = false;
    int m_lastNavHover = -1;
    int m_lastGizmoHover = -1;
    Drag m_dragging = Drag::None;
    prism::Vec2 m_navPress{0.0F, 0.0F};
    prism::Vec2 m_navLast{0.0F, 0.0F};
    bool m_navMoved = false;
    int m_navPressedBall = -1;
    std::array<float, 2> m_orbitRequest{0.0F, 0.0F};
    // Scene depth (occlusion), the ground grid and the selection outline band for this view.
    prism::GizmoDepth m_depth;
    std::optional<std::array<float, 3>> m_dropPreview;
    std::vector<float> m_depthCopy;
    std::vector<Editor::ViewportOverlayState::GizmoLine> m_lines;
    std::vector<Editor::ViewportOverlayState::GizmoTriangle> m_triangles;
    float m_aspect = 1.6F;
    bool m_gameWorkspace = false;
    prism::WorldGrid m_grid;
    bool m_gridVisible = false;
    std::vector<std::uint32_t> m_outlinePixels;
    prism::Rect m_outlineRect{};
    std::array<int, 2> m_outlineSize{0, 0};
    Math::Vector3 m_baselineScale{1.0F, 1.0F, 1.0F};

    // Marquee box-selection overlay (Desktop LMB hold+drag & Android non-controller hold+drag).
    bool m_marqueeActive = false;
    prism::Rect m_marqueeRect{};

    // Android-only floating dual-thumb viewport controller state.
    bool m_touchControllerVisible = false;
    bool m_touchControllerEnabled = true;
    std::int32_t m_leftStickPointerId = -1;
    std::int32_t m_rightStickPointerId = -1;
    prism::Vec2 m_leftThumbOffset{0.0F, 0.0F};
    prism::Vec2 m_rightThumbOffset{0.0F, 0.0F};
    bool m_touchUpHeld = false;
    bool m_touchDownHeld = false;
    bool m_touchZoomInHeld = false;
    bool m_touchZoomOutHeld = false;

    /// What the overlay last painted, hashed (see Sync): unchanged, it is not painted again.
    std::uint64_t m_drawnHash = 0U;
};

} // namespace UVE::Editor
