// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/kinematic_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {

bool IsKinematic3DObjectComponentValidUVE(const Kinematic3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.targetVelocity) && std::isfinite(value.interpolation) &&
           value.interpolation >= 0.0F && value.interpolation <= 1.0F;
}

bool IsKinematic3DObjectDefinitionValidUVE(const Kinematic3DObjectDefinitionUVE& value) noexcept {
    // Kinematic is part of the Kinematic3D contract, not a tunable: a dynamic body here
    // would make gravity fight the authored target-velocity motion.
    return IsColliderComponentValidUVE(value.collider) && IsRigid3DComponentValidUVE(value.body) &&
           value.body.isKinematic && IsKinematic3DObjectComponentValidUVE(value.animatableBody);
}

void ApplyKinematic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const Kinematic3DObjectDefinitionUVE& value) {
    // Object3D > PhysicsObject3D > Kinematic3D. The physics object base is what gives a stopped
    // platform a meaning - kept in the world as an immovable obstacle, taken out of it, or left
    // moving - and lets an author weight it against another body in a contact.
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, Kinematic3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
    entityManager.AddComponentUVE<Kinematic3DComponentUVE>(entity, value.animatableBody);
}

} // namespace UVE::Scene
