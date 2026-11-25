// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "univex/camera/orbit_camera.h"
#include "vector2.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServices;
}

namespace UVE::App::Viewports {

/// Details `Camera3D` corner picture-in-picture viewport (`camera_preview_viewport`).
///
/// Composes only `ViewportSurface` for an ultra-lean, read-only direct cut through the selected
/// `Camera3D` with preview lighting and backdrop. Carries zero input handling, zero flight
/// animation, zero gizmo building, zero depth readback, and zero Play-in-Editor state.
class CameraPreviewViewport final {
public:
    CameraPreviewViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~CameraPreviewViewport() = default;

    CameraPreviewViewport(const CameraPreviewViewport&) = delete;
    CameraPreviewViewport& operator=(const CameraPreviewViewport&) = delete;
    CameraPreviewViewport(CameraPreviewViewport&&) = delete;
    CameraPreviewViewport& operator=(CameraPreviewViewport&&) = delete;

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return m_surface.Services(); }

    [[nodiscard]] std::uint64_t Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState);

private:
    Editor::Editor& m_editor;
    ViewportSurface m_surface;
    univex::camera::OrbitCamera m_fallbackCamera;
};

} // namespace UVE::App::Viewports
