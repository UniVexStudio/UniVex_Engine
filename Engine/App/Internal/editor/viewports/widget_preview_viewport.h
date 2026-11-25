// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "vector2.h"
#include "viewport_studio_camera.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServices;
}

namespace UVE::App::Viewports {

/// Widget Designer live 3D View preview viewport (`widget_preview_viewport`).
///
/// Switches cleanly between Studio framing and direct `StageCamera` look-through without camera
/// fly animation, picking, or authoring gizmos.
class WidgetPreviewViewport final {
public:
    WidgetPreviewViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~WidgetPreviewViewport() = default;

    WidgetPreviewViewport(const WidgetPreviewViewport&) = delete;
    WidgetPreviewViewport& operator=(const WidgetPreviewViewport&) = delete;
    WidgetPreviewViewport(WidgetPreviewViewport&&) = delete;
    WidgetPreviewViewport& operator=(WidgetPreviewViewport&&) = delete;

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return m_surface.Services(); }

    [[nodiscard]] std::uint64_t Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState);

private:
    Editor::Editor& m_editor;
    ViewportSurface m_surface;
    ViewportStudioCamera m_studioCamera;
};

} // namespace UVE::App::Viewports
