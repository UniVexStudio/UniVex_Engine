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

/// VFX / Particle Graph Editor 3D dark studio preview viewport (`vfx_viewport`).
///
/// Renders live particle systems elevated above a real textured PBR dark studio floor mesh
/// (`y = 0`) with 3-point studio lighting, full 360-degree interactive orbit / pan / dolly / reset
/// camera controls centered on the elevated emitter, optional fixed-bounds wireframe overlay,
/// and zero grid clutter.
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
    void ApplyStudioFramingAndInputUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                       int viewportHeight);
    void ClampCameraAboveStudioFloorUVE();

    Editor::EditorUVE& m_editor;
    ViewportSurfaceUVE m_surface;
    ViewportStudioCameraUVE m_studioCamera;
    std::uint32_t m_appliedFramingSerial = 0U;
    bool m_orbitDragging = false;
    bool m_panDragging = false;
    bool m_dollyDragging = false;
    Editor::EditorViewportInputUVE::ButtonUVE m_activeDragButton =
        Editor::EditorViewportInputUVE::ButtonUVE::Left;
};

} // namespace UVE::App::Viewports
