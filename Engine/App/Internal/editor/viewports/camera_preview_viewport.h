// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "univex/camera/orbit_camera.h"
#include "vector2.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServicesUVE;
}

namespace UVE::App::Viewports {

/// Details `Camera3D` corner picture-in-picture viewport (`camera_preview_viewport`).
///
/// Composes only `ViewportSurfaceUVE` for an ultra-lean, read-only direct cut through the selected
/// `Camera3D` with preview lighting and backdrop. Carries zero input handling, zero flight
/// animation, zero gizmo building, zero depth readback, and zero Play-in-Editor state.
class CameraPreviewViewportUVE final {
public:
    CameraPreviewViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~CameraPreviewViewportUVE() = default;

    CameraPreviewViewportUVE(const CameraPreviewViewportUVE&) = delete;
    CameraPreviewViewportUVE& operator=(const CameraPreviewViewportUVE&) = delete;
    CameraPreviewViewportUVE(CameraPreviewViewportUVE&&) = delete;
    CameraPreviewViewportUVE& operator=(CameraPreviewViewportUVE&&) = delete;

    [[nodiscard]] const Core::EngineServicesUVE* ServicesUVE() const noexcept { return m_surface.ServicesUVE(); }

    [[nodiscard]] std::uint64_t RenderUVE(const Math::Vector2UVE& availableSize,
                                          Math::Vector2UVE& outUsedSize,
                                          const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

private:
    Editor::EditorUVE& m_editor;
    ViewportSurfaceUVE m_surface;
    univex::camera::OrbitCamera m_fallbackCamera;
};

} // namespace UVE::App::Viewports
