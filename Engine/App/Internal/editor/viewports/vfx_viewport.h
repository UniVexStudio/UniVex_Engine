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

/// VFX / Particle Graph Editor 3D preview viewport (`vfx_viewport`).
///
/// Renders live particle systems over the studio backdrop and floor with `fixedBounds` framing,
/// wheel dolly, fixed-bounds wireframe overlay, and explicit support for toggling the world
/// reference grid via `overlayState.gridVisible`.
class VfxViewportUVE final {
public:
    VfxViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~VfxViewportUVE() = default;

    VfxViewportUVE(const VfxViewportUVE&) = delete;
    VfxViewportUVE& operator=(const VfxViewportUVE&) = delete;
    VfxViewportUVE(VfxViewportUVE&&) = delete;
    VfxViewportUVE& operator=(VfxViewportUVE&&) = delete;

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
