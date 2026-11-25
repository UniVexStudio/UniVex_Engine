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

/// VFX / Particle Graph Editor 3D dark studio preview viewport (`vfx_viewport`).
///
/// Renders live particle systems elevated above a real textured PBR dark studio floor mesh
/// (`y = 0`) with 3-point studio lighting, full 360-degree interactive orbit / pan / dolly / reset
/// camera controls centered on the elevated emitter, optional fixed-bounds wireframe overlay,
/// and zero grid clutter.
class VfxViewport final {
public:
    VfxViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~VfxViewport() = default;

    VfxViewport(const VfxViewport&) = delete;
    VfxViewport& operator=(const VfxViewport&) = delete;
    VfxViewport(VfxViewport&&) = delete;
    VfxViewport& operator=(VfxViewport&&) = delete;

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return m_surface.Services(); }

    [[nodiscard]] std::uint64_t Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState);

private:
    void ApplyStudioFramingAndInput(const Editor::Editor::ViewportOverlayState& overlayState,
                                       int viewportHeight);
    void ClampCameraAboveStudioFloor();

    Editor::Editor& m_editor;
    ViewportSurface m_surface;
    ViewportStudioCamera m_studioCamera;
    std::uint32_t m_appliedFramingSerial = 0U;
    bool m_orbitDragging = false;
    bool m_panDragging = false;
    bool m_dollyDragging = false;
    Editor::EditorViewportInput::Button m_activeDragButton =
        Editor::EditorViewportInput::Button::Left;
};

} // namespace UVE::App::Viewports
