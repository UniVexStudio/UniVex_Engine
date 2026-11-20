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

/// Widget Designer live 3D View preview viewport (`widget_preview_viewport`).
///
/// Switches cleanly between Studio framing and direct `StageCamera` look-through without camera
/// fly animation, picking, or authoring gizmos.
class WidgetPreviewViewportUVE final {
public:
    WidgetPreviewViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~WidgetPreviewViewportUVE() = default;

    WidgetPreviewViewportUVE(const WidgetPreviewViewportUVE&) = delete;
    WidgetPreviewViewportUVE& operator=(const WidgetPreviewViewportUVE&) = delete;
    WidgetPreviewViewportUVE(WidgetPreviewViewportUVE&&) = delete;
    WidgetPreviewViewportUVE& operator=(WidgetPreviewViewportUVE&&) = delete;

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
