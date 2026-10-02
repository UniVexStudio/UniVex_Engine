// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/abstract_physics_nodes_3d_uve.h"

#include "uve/component/physics_object_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {
namespace {

template <typename BaseComponentT>
void ApplyPhysicsBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                         const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplyObject3DRecipeUVE(entityManager, entity, nameFallback);
    if (!entityManager.HasComponentUVE<BaseComponentT>(entity)) {
        entityManager.AddComponentUVE<BaseComponentT>(entity, BaseComponentT{});
    }
}

} // namespace

void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                 const std::string_view nameFallback) {
    ApplyPhysicsBaseUVE<PhysicsObjectComponentUVE>(entityManager, entity, nameFallback);
}

void ApplySolidBody3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view nameFallback) {
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, nameFallback);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity)) {
        entityManager.AddComponentUVE<SolidBodyComponentUVE>(entity, SolidBodyComponentUVE{});
    }
}

} // namespace UVE::Scene
