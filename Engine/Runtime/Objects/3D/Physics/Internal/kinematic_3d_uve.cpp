// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/kinematic_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

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
    // Kinematic3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Kinematic3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
    entityManager.AddComponentUVE<Kinematic3DComponentUVE>(entity, value.animatableBody);
}

} // namespace UVE::Scene
