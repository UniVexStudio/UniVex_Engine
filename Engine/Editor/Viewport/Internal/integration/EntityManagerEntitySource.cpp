#include "EntityManagerEntitySource.h"

#include "uve/scene/components/world_transform_component_uve.h"

namespace univex::integration {

std::vector<EntityTransformUVE> EntityManagerEntitySource::GetEntityTransformsUVE() const {
    std::vector<EntityTransformUVE> transforms;
    entityManager_.ForEachUVE<UVE::Scene::WorldTransformComponentUVE>(
        [&transforms](UVE::Scene::EntityUVE /*entity*/,
                      const UVE::Scene::WorldTransformComponentUVE& worldTransform) {
            EntityTransformUVE entry;
            entry.positionX = worldTransform.worldPosition.x;
            entry.positionY = worldTransform.worldPosition.y;
            entry.positionZ = worldTransform.worldPosition.z;
            // Uniform scale only for this first integration slice (see
            // EntityTransformSource.h) - x is as representative as any axis
            // for a symmetric proxy cube.
            entry.uniformScale = worldTransform.worldScale.x;
            transforms.push_back(entry);
        });
    return transforms;
}

} // namespace univex::integration
