// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/rigid_3d_uve.h"

#include "uve/component/collider_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {

bool IsRigid3DObjectDefinitionValidUVE(const Rigid3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) && IsRigid3DComponentValidUVE(value.body);
}

void ApplyRigid3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Rigid3DObjectDefinitionUVE& value) {
    // Object3D > PhysicsObject3D > Rigid3D. The physics object base is what says what this body's
    // disabled state means - taken out of the world, kept as an immovable obstacle, or left
    // simulating - and how much of an overlap it yields next to another body.
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, Rigid3DObjectDefinitionUVE::defaultName);
    // Apply-if-missing, like the other body kinds: re-applying a definition must not wipe a
    // collider that was authored on the entity afterwards.
    if (!entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    }
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
}

} // namespace UVE::Scene
