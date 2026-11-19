// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "vector2.h"
#include "viewport_studio_camera.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServicesUVE;
}

namespace UVE::App::Viewports {

/// Retarget Editor 3D studio preview viewport (`retarget_viewport`).
///
/// Renders the source and target skeletons over the studio backdrop and floor with front-facing
/// framing, wheel dolly, and GL skeleton bone overlays.
class RetargetViewportUVE final {
public:
    RetargetViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~RetargetViewportUVE() = default;

    RetargetViewportUVE(const RetargetViewportUVE&) = delete;
    RetargetViewportUVE& operator=(const RetargetViewportUVE&) = delete;
    RetargetViewportUVE(RetargetViewportUVE&&) = delete;
    RetargetViewportUVE& operator=(RetargetViewportUVE&&) = delete;

    [[nodiscard]] const Core::EngineServicesUVE* ServicesUVE() const noexcept { return m_surface.ServicesUVE(); }

    [[nodiscard]] std::uint64_t RenderUVE(const Math::Vector2UVE& availableSize,
                                          Math::Vector2UVE& outUsedSize,
                                          const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

private:
    Editor::EditorUVE& m_editor;
    ViewportSurfaceUVE m_surface;
    ViewportStudioCameraUVE m_studioCamera;
};

} // namespace UVE::App::Viewports
