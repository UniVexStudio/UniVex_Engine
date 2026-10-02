// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/kinematic_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsAnimatableBody3DNodeComponentValidUVE(const AnimatableBody3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.targetVelocity) && std::isfinite(value.interpolation) &&
           value.interpolation >= 0.0F && value.interpolation <= 1.0F;
}

bool IsKinematic3DNodeDefinitionValidUVE(const Kinematic3DNodeDefinitionUVE& value) noexcept {
    // Kinematic is part of the Kinematic3D contract, not a tunable: a dynamic body here
    // would make gravity fight the authored target-velocity motion.
    return IsColliderComponentValidUVE(value.collider) && IsRigidBodyComponentValidUVE(value.body) &&
           value.body.isKinematic && IsAnimatableBody3DNodeComponentValidUVE(value.animatableBody);
}

void ApplyKinematic3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const Kinematic3DNodeDefinitionUVE& value) {
    // Kinematic3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Kinematic3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    entityManager.AddComponentUVE<RigidBodyComponentUVE>(entity, value.body);
    entityManager.AddComponentUVE<AnimatableBody3DNodeComponentUVE>(entity, value.animatableBody);
}

} // namespace UVE::Scene
