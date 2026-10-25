// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// The question the Viewport panel asks before it supplies a light of its own: does the document
// light itself? It is answered by the light system's own extraction, never by a second opinion -
// a light the renderer drops must not switch the fallback off, or the panel goes black on account
// of a light that lights nothing.
//
// Its own translation unit, and deliberately free of any OpenGL: this is pure scene logic that a
// test can hold without a renderer, an ImGui context or a window.
#include "integration/EditorMeshLayer.h"

#include <cstddef>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/light_3d_uve.h"
#include "uve/render_systems/light_system_uve.h"

namespace univex::integration {

bool HasContributingSceneLightUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    // The light system's own extraction, not a second opinion about what a light is: whatever it
    // drops here is exactly what the renderer will drop, so the panel cannot go dark on account of
    // a light that lights nothing.
    const UVE::Render::LightSystemUVE lightSystem;
    const UVE::Render::LightListUVE lights = lightSystem.ExtractActiveLightsUVE(entityManager);
    for (std::size_t index = 0U; index < lights.SizeUVE(); ++index) {
        const UVE::Render::LightDataUVE& light = lights[index];
        if (light.isEditorInternal) {
            continue;
        }
        // Intensity alone is not enough: a light whose colour is black adds nothing however loud
        // its energy setting is.
        if (light.intensity <= 0.0F ||
            UVE::Scene::Light3DUVE::ComputeRec709LuminanceUVE(light.color) <= 0.0F) {
            continue;
        }
        return true;
    }
    return false;
}

bool HasContributingDirectionalLightUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    bool contributing = false;
    entityManager.ForEachUVE<UVE::Scene::WorldTransformComponentUVE,
                             UVE::Scene::DirectionalLight3DComponentUVE,
                             UVE::Scene::LightEmitterComponentUVE>(
        [&contributing, &entityManager](const UVE::Scene::EntityUVE entity,
                                        const UVE::Scene::WorldTransformComponentUVE&,
                                        const UVE::Scene::DirectionalLight3DComponentUVE& directional,
                                        const UVE::Scene::LightEmitterComponentUVE& emitter) {
            if (contributing) {
                return;
            }
            // The light system's own conditions, in its order: everything it would reject here is
            // a sun that lights nothing, and a sun that lights nothing must not silence the
            // preview sun in its favour.
            if (entityManager.HasComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity) ||
                !UVE::Scene::IsDirectionalLight3DComponentValidUVE(directional) ||
                !UVE::Scene::IsLightEmitterComponentValidUVE(emitter) || emitter.cullMask == 0U ||
                emitter.energy <= 0.0F ||
                UVE::Scene::Light3DUVE::ComputeRec709LuminanceUVE(emitter.color) <= 0.0F) {
                return;
            }
            contributing = true;
        });
    return contributing;
}

} // namespace univex::integration
