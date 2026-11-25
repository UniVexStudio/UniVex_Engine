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

/// Retarget Editor 3D studio preview viewport (`retarget_viewport`).
///
/// Renders the source and target skeletons over the studio backdrop and floor with front-facing
/// framing, wheel dolly, and GL skeleton bone overlays.
class RetargetViewport final {
public:
    RetargetViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~RetargetViewport() = default;

    RetargetViewport(const RetargetViewport&) = delete;
    RetargetViewport& operator=(const RetargetViewport&) = delete;
    RetargetViewport(RetargetViewport&&) = delete;
    RetargetViewport& operator=(RetargetViewport&&) = delete;

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
