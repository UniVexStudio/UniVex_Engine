// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// The question the Viewport panel asks before it supplies a light of its own: does the document
// light itself? It is answered by the light system's own extraction, never by a second opinion -
// a light the renderer drops must not switch the fallback off, or the panel goes black on account
// of a light that lights nothing.
//
// Its own translation unit, and deliberately free of any OpenGL: this is pure scene logic that a
// test can hold without a renderer, a UI or a window.
#include "integration/editor_mesh_layer.h"

#include <cstddef>

#include "editor_internal_entity_component.h"
#include "visibility_component.h"
#include "directional_light_3d.h"
#include "light_3d.h"
#include "light_system.h"

namespace univex::integration {

bool HasContributingSceneLight(UVE::Scene::IEntityManager& entityManager) {
    // The light system's own extraction, not a second opinion about what a light is: whatever it
    // drops here is exactly what the renderer will drop, so the panel cannot go dark on account of
    // a light that lights nothing.
    const UVE::Render::LightSystem lightSystem;
    const UVE::Render::LightList lights = lightSystem.ExtractActiveLights(entityManager);
    for (std::size_t index = 0U; index < lights.Size(); ++index) {
        const UVE::Render::LightData& light = lights[index];
        if (light.isEditorInternal) {
            continue;
        }
        // Intensity alone is not enough: a light whose colour is black adds nothing however loud
        // its energy setting is.
        if (light.intensity <= 0.0F ||
            UVE::Scene::Light3D::ComputeRec709Luminance(light.color) <= 0.0F) {
            continue;
        }
        return true;
    }
    return false;
}

bool HasContributingDirectionalLight(UVE::Scene::IEntityManager& entityManager) {
    bool contributing = false;
    entityManager.ForEach<UVE::Scene::WorldTransformComponent,
                             UVE::Scene::DirectionalLight3DComponent,
                             UVE::Scene::LightEmitterComponent>(
        [&contributing, &entityManager](const UVE::Scene::Entity entity,
                                        const UVE::Scene::WorldTransformComponent&,
                                        const UVE::Scene::DirectionalLight3DComponent& directional,
                                        const UVE::Scene::LightEmitterComponent& emitter) {
            if (contributing) {
                return;
            }
            if (entityManager.HasComponent<UVE::Scene::VisibilityComponent>(entity)) {
                const UVE::Scene::VisibilityComponent& visibility =
                    entityManager.GetComponent<UVE::Scene::VisibilityComponent>(entity);
                if (!visibility.visible || !visibility.visibleInHierarchy) {
                    return;
                }
            }
            // The light system's own conditions, in its order: everything it would reject here is
            // a sun that lights nothing, and a sun that lights nothing must not silence the
            // preview sun in its favour.
            if (entityManager.HasComponent<UVE::Scene::EditorInternalEntityComponent>(entity) ||
                !UVE::Scene::IsDirectionalLight3DComponentValid(directional) ||
                !UVE::Scene::IsLightEmitterComponentValid(emitter) || emitter.cullMask == 0U ||
                emitter.energy <= 0.0F ||
                UVE::Scene::Light3D::ComputeRec709Luminance(emitter.color) <= 0.0F) {
                return;
            }
            contributing = true;
        });
    return contributing;
}

bool HasAuthoredDirectionalLightNode(UVE::Scene::IEntityManager& entityManager) {
    bool authored = false;
    entityManager.ForEach<UVE::Scene::WorldTransformComponent,
                             UVE::Scene::DirectionalLight3DComponent,
                             UVE::Scene::LightEmitterComponent>(
        [&authored, &entityManager](const UVE::Scene::Entity entity,
                                    const UVE::Scene::WorldTransformComponent&,
                                    const UVE::Scene::DirectionalLight3DComponent& directional,
                                    const UVE::Scene::LightEmitterComponent& emitter) {
            if (authored) {
                return;
            }
            if (entityManager.HasComponent<UVE::Scene::EditorInternalEntityComponent>(entity) ||
                !UVE::Scene::IsDirectionalLight3DComponentValid(directional) ||
                !UVE::Scene::IsLightEmitterComponentValid(emitter)) {
                return;
            }
            authored = true;
        });
    return authored;
}

} // namespace univex::integration
