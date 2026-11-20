// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>

#include "editor.h"
#include "vector2.h"
#include "viewport_scene_interaction.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineServicesUVE;
}

namespace UVE::App::Viewports {

/// Entity Editor 3D authoring viewport (`entity_viewport`).
///
/// Composes `ViewportSurfaceUVE` and `ViewportSceneInteractionUVE` for isolated `.uventity` and
/// Widget 3D Stage authoring, completely decoupled from Play-in-Editor hooks and Game HUD passes.
class EntityViewportUVE final {
public:
    EntityViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services);
    ~EntityViewportUVE() = default;

    EntityViewportUVE(const EntityViewportUVE&) = delete;
    EntityViewportUVE& operator=(const EntityViewportUVE&) = delete;
    EntityViewportUVE(EntityViewportUVE&&) = delete;
    EntityViewportUVE& operator=(EntityViewportUVE&&) = delete;

    [[nodiscard]] const Core::EngineServicesUVE* ServicesUVE() const noexcept { return m_surface.ServicesUVE(); }

    [[nodiscard]] std::uint64_t RenderUVE(const Math::Vector2UVE& availableSize,
                                          Math::Vector2UVE& outUsedSize,
                                          const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

private:
    ViewportSurfaceUVE m_surface;
    ViewportSceneInteractionUVE m_interaction;
};

} // namespace UVE::App::Viewports
