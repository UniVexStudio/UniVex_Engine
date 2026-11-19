// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "univex/camera/orbit_camera.h"

namespace univex::app {
class ViewportRenderPass;
}

namespace UVE::App::Viewports {

/// Lightweight studio framing camera and GL preview overlay uploader shared by preview viewports
/// (`WidgetPreviewViewportUVE`, `RetargetViewportUVE`, `MontageViewportUVE`, `VfxViewportUVE`).
class ViewportStudioCameraUVE final {
public:
    ViewportStudioCameraUVE();
    ~ViewportStudioCameraUVE() = default;

    ViewportStudioCameraUVE(const ViewportStudioCameraUVE&) = delete;
    ViewportStudioCameraUVE& operator=(const ViewportStudioCameraUVE&) = delete;

    [[nodiscard]] univex::camera::OrbitCamera& CameraUVE() noexcept { return m_camera; }
    [[nodiscard]] const univex::camera::OrbitCamera& CameraUVE() const noexcept { return m_camera; }

    /// Applies front-facing perspective studio framing whenever `studioFramingSerial` advances.
    void ApplyStudioFramingUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

    /// Applies mouse-wheel dolly when hovered and advances camera interpolation.
    void HandleWheelDollyUVE(const Editor::EditorViewportInputUVE& input);

    /// Uploads GL skeleton bone overlays from `overlayState.bones` into `renderPass`.
    static void UploadBoneOverlaysUVE(univex::app::ViewportRenderPass& renderPass,
                                      const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

    /// Uploads GL collider / bounds wireframe lines from `overlayState.colliderLines` into `renderPass`.
    static void UploadColliderLinesUVE(univex::app::ViewportRenderPass& renderPass,
                                       const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

    /// Exports camera state into `overlayState.viewCamera` and clears host-gizmo/mask/depth outputs.
    void ExportCameraForHostUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                int width,
                                int height) const;

private:
    univex::camera::OrbitCamera m_camera;
    std::uint32_t m_appliedStudioFramingSerial = 0U;
};

} // namespace UVE::App::Viewports
