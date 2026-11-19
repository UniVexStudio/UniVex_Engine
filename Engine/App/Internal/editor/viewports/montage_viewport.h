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

/// Motion Montage Editor 3D studio preview viewport (`montage_viewport`).
///
/// Renders the skinned character animation preview over the studio backdrop and floor with
/// independent camera framing, wheel dolly, and GL skeleton bone overlays.
class MontageViewportUVE final {
public:
    MontageViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~MontageViewportUVE() = default;

    MontageViewportUVE(const MontageViewportUVE&) = delete;
    MontageViewportUVE& operator=(const MontageViewportUVE&) = delete;
    MontageViewportUVE(MontageViewportUVE&&) = delete;
    MontageViewportUVE& operator=(MontageViewportUVE&&) = delete;

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
