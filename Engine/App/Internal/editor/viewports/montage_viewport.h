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

/// Motion Montage Editor 3D studio preview viewport (`montage_viewport`).
///
/// Renders the skinned character animation preview over the studio backdrop and floor with
/// independent camera framing, wheel dolly, and GL skeleton bone overlays.
class MontageViewport final {
public:
    MontageViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~MontageViewport() = default;

    MontageViewport(const MontageViewport&) = delete;
    MontageViewport& operator=(const MontageViewport&) = delete;
    MontageViewport(MontageViewport&&) = delete;
    MontageViewport& operator=(MontageViewport&&) = delete;

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
