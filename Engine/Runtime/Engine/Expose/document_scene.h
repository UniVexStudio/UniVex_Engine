// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <memory>

#include "engine_services.h"

namespace UVE::Core {

/// A second scene beside the engine's own: its own entities, scene graph and event bus, drawn by
/// its own Renderer3D (with its own mesh, camera and light systems) configured exactly like the
/// engine's. Everything else - assets, shaders, the render device, input, audio - is the engine's.
///
/// An editor opens one per asset document (the Entity Editor's entity) so editing it never touches
/// the level. GetServices() is the engine's services with the scene half replaced, so code that
/// reads its scene through EngineServices works on this scene unchanged.
///
/// The renderer is per scene because it keeps per-entity state (the spatial index fed by entity
/// events, placement caches) keyed by entity ids, and two entity managers hand out the same ids.
/// The event bus is per scene for the same reason: an entity destroyed here must not reach the
/// level's listeners as a level entity.
///
/// Created by EngineCore::CreateDocumentScene(); valid while that engine is initialized.
/// Main thread only.
class DocumentScene {
public:
    virtual ~DocumentScene() = default;
    [[nodiscard]] virtual EngineServices& GetServices() noexcept = 0;
    [[nodiscard]] Scene::IEntityManager& GetEntityManager() noexcept { return GetServices().GetEntityManager(); }
    [[nodiscard]] Scene::ISceneGraph& GetSceneGraph() noexcept { return GetServices().GetSceneGraph(); }
};

} // namespace UVE::Core
