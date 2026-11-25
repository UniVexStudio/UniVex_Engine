// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "univex/render/gl_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "editor.h"
#include "entity.h"
#include "quaternion.h"
#include "univex/camera/orbit_camera.h"
#include "univex/gizmo/gizmo_geometry.h"
#include "univex/render/selection_outline_renderer.h"
#include "univex/render/shader_program.h"
#include "vector3.h"

namespace UVE::Scene {
class IEntityManager;
}

namespace UVE::App::Viewports {

class ViewportSurface;

/// Snapshot of an editor orbit camera pose (used for Play-mode and Camera3D preview restoration).
struct ViewportCameraPose final {
    univex::math::Vec3 target{};
    float yaw = 0.0F;
    float pitch = 0.0F;
    float distance = 1.0F;
    bool orthographic = false;
};

/// Composable interactive 3D authoring controller owned by `LevelViewport` and
/// `EntityViewport`.
///
/// Owns the orbit camera, mouse/keyboard navigation (Alt+Left orbit, Middle pan, Wheel dolly,
/// Right-hold WASD/QE eased fly camera, PRISM nav-gizmo orbit requests, Numpad/Alt named views,
/// bookmarks, F-to-focus), smooth Camera3D look-through flight, entity picking and context-toolbar
/// arming, throttled object-gizmo and selection-outline caches, silhouette mask readback, and
/// half-resolution double-PBO scene depth export for the PRISM shell.
class ViewportSceneInteraction final {
public:
    ViewportSceneInteraction(Editor::Editor& editor, Scene::IEntityManager& entityManager);
    ~ViewportSceneInteraction();

    ViewportSceneInteraction(const ViewportSceneInteraction&) = delete;
    ViewportSceneInteraction& operator=(const ViewportSceneInteraction&) = delete;
    ViewportSceneInteraction(ViewportSceneInteraction&&) = delete;
    ViewportSceneInteraction& operator=(ViewportSceneInteraction&&) = delete;

    [[nodiscard]] univex::camera::OrbitCamera& Camera() noexcept { return m_camera; }
    [[nodiscard]] const univex::camera::OrbitCamera& Camera() const noexcept { return m_camera; }
    [[nodiscard]] bool IsPreviewingCamera() const noexcept { return m_previewing; }

    [[nodiscard]] ViewportCameraPose CapturePose() const noexcept;
    void RestorePose(const ViewportCameraPose& pose);

    /// Applies editor overlay settings (projection, named view, focus requests, grid appearance,
    /// selection outline settings, axis palette, bones, and collider wireframes) to this camera
    /// and the target surface.
    void ApplyOverlayState(ViewportSurface& surface,
                              const Editor::Editor::ViewportOverlayState& overlayState,
                              bool suppressGrid);

    /// Advances smooth fly-in / fly-out camera transitions when previewing a document `Camera3D`.
    /// Returns the `Camera3D` entity to look directly through once the fly-in completes.
    [[nodiscard]] std::optional<Scene::Entity> UpdateCameraPreviewFlight(
        bool allowPreview,
        const Editor::EditorViewportInput& input);

    /// Processes gizmo pivot tracking, nav-gizmo orbit requests, entity picking, context toolbar
    /// right-clicks, and (when `skipAuthoringNavigation` is false) fly/orbit/pan/zoom/hotkeys.
    void UpdateInteraction(const Editor::Editor::ViewportOverlayState& overlayState,
                              int width,
                              int height,
                              bool skipAuthoringNavigation);

    /// Advances camera animations and publishes the camera focus target back to `Editor`.
    void AdvanceCameraAndPublishFocus(const Editor::EditorViewportInput& input);

    /// Refreshes the throttled spatial object gizmo cache and routes it either to the PRISM host
    /// or to the surface's GL overlay pass.
    void UpdateObjectGizmoOverlay(ViewportSurface& surface,
                                     const Editor::Editor::ViewportOverlayState& overlayState,
                                     int width,
                                     int height,
                                     std::optional<Scene::Entity> lookThroughCamera,
                                     bool wantOverlay);

    /// Exports camera state, transform-gizmo pivot, and host-drawn gizmo geometry into
    /// `overlayState`.
    void ExportViewForHost(const Editor::Editor::ViewportOverlayState& overlayState,
                              int width,
                              int height,
                              bool wantOverlay);

    /// Exports the surface grid settings into `overlayState.hostGrid`.
    void ExportGridForHost(const ViewportSurface& surface,
                              const Editor::Editor::ViewportOverlayState& overlayState,
                              bool want);

    /// Downsamples and asynchronously reads back linear view-forward scene depth via double PBOs
    /// for PRISM gizmo occlusion.
    void ExportSceneDepthForHost(ViewportSurface& surface,
                                    const Editor::Editor::ViewportOverlayState& overlayState,
                                    int width,
                                    int height);

    /// Updates cached selection triangles and either reads back a silhouette mask for PRISM or
    /// renders the GL selection outline directly into the currently bound MSAA framebuffer.
    void RenderSelectionOutlineOrMask(ViewportSurface& surface,
                                         const Editor::Editor::ViewportOverlayState& overlayState,
                                         int width,
                                         int height);

    [[nodiscard]] static univex::math::Vec3 NamedViewDirection(Editor::Editor::ViewportView view);

private:
    struct CameraFlightPose final {
        univex::math::Vec3 target{};
        float yaw = 0.0F;
        float pitch = 0.0F;
        float distance = 1.0F;
        float fovY = 0.87F;
    };

    struct CameraFlight final {
        CameraFlightPose from;
        CameraFlightPose to;
        float t = 0.0F;
        bool entering = true;
    };

    struct SelectedEntitySnapshot final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Math::Vector3 position{};
        Math::Quaternion rotation{};
    };

    static constexpr float kCameraFlightSeconds = 0.45F;

    void ApplyNamedView(Editor::Editor::ViewportView view);
    void FocusCameraOnEntity(Scene::Entity entity);
    [[nodiscard]] CameraFlightPose CurrentFlightPose() const;
    void ApplyFlightPose(const CameraFlightPose& pose);
    [[nodiscard]] std::optional<CameraFlightPose> CameraEntityPose(Scene::Entity entity) const;

    void UpdateSelectionGizmo();
    void UpdateSelectionFromMouse(const Editor::Editor::ViewportOverlayState& overlayState,
                                     int width,
                                     int height,
                                     bool suppressed);
    void CommitMarqueeSelection(const Editor::EditorViewportInput& input,
                                   int width,
                                   int height,
                                   float minX,
                                   float minY,
                                   float maxX,
                                   float maxY);
    void UpdateEntityContextToolbarFromMouse(const Editor::EditorViewportInput& input,
                                                int width,
                                                int height,
                                                bool suppressed);
    void UpdateTouchControllerCamera(const Editor::Editor::ViewportOverlayState& overlayState);
    void UpdateOrbitPanZoomFromMouse(const Editor::EditorViewportInput& input,
                                        int framebufferHeight,
                                        bool suppressOrbit);
    [[nodiscard]] bool UpdateFlyCamera(const Editor::EditorViewportInput& input, bool suppressed);
    void UpdateViewportViewHotkeys(const Editor::EditorViewportInput& input);
    void UpdateViewportBookmarkHotkeys(const Editor::EditorViewportInput& input);

    [[nodiscard]] bool EnsureHostDepth(ViewportSurface& surface, int width, int height);
    [[nodiscard]] bool AnySelectedEntityMoved() const;

    Editor::Editor& m_editor;
    Scene::IEntityManager& m_entityManager;
    univex::camera::OrbitCamera m_camera;

    std::uint32_t m_appliedViewRequestSerial = 0U;
    std::uint32_t m_appliedFocusRequestSerial = 0U;
    bool m_pointerOverOverlay = false;

    std::optional<ViewportCameraPose> m_poseBeforePreview;
    bool m_previewing = false;
    std::optional<CameraFlight> m_flight;
    float m_fovBeforePreview = 0.87F;
    Scene::Entity m_lastPreviewCamera = Scene::kInvalidEntity;

    bool m_flyArmed = false;
    bool m_flying = false;
    float m_flySpeed = 6.0F;
    univex::math::Vec3 m_flyVelocity{0.0F, 0.0F, 0.0F};
    univex::math::Vec3 m_touchMoveVelocity{0.0F, 0.0F, 0.0F};

    bool m_selectionPressActive = false;
    bool m_marqueeActive = false;
    float m_selectionPressX = 0.0F;
    float m_selectionPressY = 0.0F;
    bool m_contextToolbarPressActive = false;
    float m_contextToolbarPressX = 0.0F;
    float m_contextToolbarPressY = 0.0F;

    std::optional<univex::math::Vec3> m_gizmoPivot;
    univex::gizmo::GizmoMesh m_hostOverlayMesh;
    univex::gizmo::GizmoMesh m_hostBonesMesh;
    std::vector<univex::gizmo::GizmoLine> m_hostColliderLines;
    std::size_t m_hostOverlayOccludedFrom = std::numeric_limits<std::size_t>::max();

    float m_cachedOverlayAspect = 0.0F;
    Scene::Entity m_cachedLookThroughCamera = Scene::kInvalidEntity;
    std::size_t m_cachedOverlayEntityCount = 0U;
    bool m_cachedOverlayActive = false;
    float m_overlayRefreshTimerSeconds = 0.0F;

    std::vector<Scene::Entity> m_lastSelectedEntities;
    Scene::Entity m_lastSelectedEntity = Scene::kInvalidEntity;
    std::vector<univex::render::SelectionOutlineVertex> m_cachedSelectionTriangles;
    std::vector<std::uint8_t> m_lastMask;
    std::uint64_t m_lastMaskKey = 0U;
    std::vector<SelectedEntitySnapshot> m_lastSelectedSnapshots;

    std::optional<univex::render::ShaderProgram> m_hostDepthProgram;
    bool m_hostDepthFailed = false;
    GLuint m_hostDepthFbo = 0U;
    GLuint m_hostDepthTexture = 0U;
    std::array<GLuint, 2> m_hostDepthPbo{};
    std::array<GLsizeiptr, 2> m_hostDepthPboBytes{};
    std::array<bool, 2> m_hostDepthPboFilled{};
    std::array<GLsync, 2> m_hostDepthFence{};   // set after each PBO's glReadPixels; mapped only once signalled
    std::size_t m_hostDepthPboNext = 0U;
    int m_hostDepthWidth = 0;
    int m_hostDepthHeight = 0;
    std::vector<float> m_cachedHostDepth;
    int m_cachedHostDepthWidth = 0;
    int m_cachedHostDepthHeight = 0;
    std::uint64_t m_cachedHostDepthKey = 0U;
    std::uint32_t m_hostDepthFrameCounter = 0U;
};

} // namespace UVE::App::Viewports
