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
/// (`WidgetPreviewViewport`, `RetargetViewport`, `MontageViewport`, `VfxViewport`).
class ViewportStudioCamera final {
public:
    ViewportStudioCamera();
    ~ViewportStudioCamera() = default;

    ViewportStudioCamera(const ViewportStudioCamera&) = delete;
    ViewportStudioCamera& operator=(const ViewportStudioCamera&) = delete;

    [[nodiscard]] univex::camera::OrbitCamera& Camera() noexcept { return m_camera; }
    [[nodiscard]] const univex::camera::OrbitCamera& Camera() const noexcept { return m_camera; }

    /// Applies front-facing perspective studio framing whenever `studioFramingSerial` advances.
    void ApplyStudioFraming(const Editor::Editor::ViewportOverlayState& overlayState);

    /// Applies mouse-wheel dolly when hovered and advances camera interpolation.
    void HandleWheelDolly(const Editor::EditorViewportInput& input);

    /// Uploads GL skeleton bone overlays from `overlayState.bones` into `renderPass`.
    static void UploadBoneOverlays(univex::app::ViewportRenderPass& renderPass,
                                      const Editor::Editor::ViewportOverlayState& overlayState);

    /// Uploads GL collider / bounds wireframe lines from `overlayState.colliderLines` into `renderPass`.
    static void UploadColliderLines(univex::app::ViewportRenderPass& renderPass,
                                       const Editor::Editor::ViewportOverlayState& overlayState);

    /// Exports camera state into `overlayState.viewCamera` and clears host-gizmo/mask/depth outputs.
    void ExportCameraForHost(const Editor::Editor::ViewportOverlayState& overlayState,
                                int width,
                                int height) const;

private:
    univex::camera::OrbitCamera m_camera;
    std::uint32_t m_appliedStudioFramingSerial = 0U;
};

} // namespace UVE::App::Viewports
