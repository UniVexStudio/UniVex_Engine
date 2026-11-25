// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "vector2.h"
#include "viewport_scene_interaction.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServices;
}

namespace UVE::App::Viewports {

/// Entity Editor 3D authoring viewport (`entity_viewport`).
///
/// Composes `ViewportSurface` and `ViewportSceneInteraction` for isolated `.uventity` and
/// Widget 3D Stage authoring, completely decoupled from Play-in-Editor hooks and Game HUD passes.
class EntityViewport final {
public:
    EntityViewport(Editor::Editor& editor, Core::EngineServices& services);
    ~EntityViewport() = default;

    EntityViewport(const EntityViewport&) = delete;
    EntityViewport& operator=(const EntityViewport&) = delete;
    EntityViewport(EntityViewport&&) = delete;
    EntityViewport& operator=(EntityViewport&&) = delete;

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return m_surface.Services(); }

    [[nodiscard]] std::uint64_t Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState);

private:
    ViewportSurface m_surface;
    ViewportSceneInteraction m_interaction;
};

} // namespace UVE::App::Viewports
