// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/physics_system_uve.h"

#include "uve/physics/physics_constraint_system_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/physics/angular_dynamics_uve.h"
#include "uve/physics/area_3d_runtime_uve.h"
#include "uve/physics/collision_pair_uve.h"
#include "uve/physics/physics_material_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"
#include "uve/objects/3d/static_3d_uve.h"

namespace UVE::Physics {

namespace {

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool TryIntegratePositionUVE(const Math::Vector3UVE& position,
                                           const Math::Vector3UVE& velocity,
                                           float fixedDeltaTimeSeconds,
                                           Math::Vector3UVE& integratedPosition) noexcept {
    const double deltaTime = static_cast<double>(fixedDeltaTimeSeconds);
    const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
    const double x = static_cast<double>(position.x) + static_cast<double>(velocity.x) * deltaTime;
    const double y = static_cast<double>(position.y) + static_cast<double>(velocity.y) * deltaTime;
    const double z = static_cast<double>(position.z) + static_cast<double>(velocity.z) * deltaTime;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > maxFloat ||
        std::abs(y) > maxFloat || std::abs(z) > maxFloat) {
        return false;
    }
    integratedPosition = Math::Vector3UVE{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    return IsFiniteVector3UVE(integratedPosition);
}

/// Moves `entity` by `positionDelta` (via SetLocalTransformUVE, so dirty-flag propagation stays
/// correct) and, if it has a non-kinematic Rigid3DComponentUVE, applies `material`'s combined
/// friction/restitution to the velocity component pointing toward `towardOtherBody`: the
/// tangential (sliding) component is damped by `1 - friction`, and the into-surface component is
/// reflected and scaled by `restitution` rather than simply zeroed. With `friction = 0,
/// restitution = 0` (every collider's default), this reduces to exactly `velocity -=
/// towardOtherBody * intoSurface` — bit-identical to Increment 15's pure vector-rejection formula
/// — never touches velocity that's already separating.
void MoveAndDeflectUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                       Scene::EntityUVE entity, Math::Vector3UVE positionDelta, Math::Vector3UVE towardOtherBody,
                       const PhysicsMaterialUVE& material) {
    Scene::TransformComponentUVE transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    transform.localPosition += positionDelta;
    sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);

    if (!entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
        return;
    }
    Scene::Rigid3DComponentUVE& rigidBody = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
    if (!Scene::Rigid3DUVE::IsDynamicUVE(rigidBody)) {
        return;
    }
    rigidBody.velocity = Scene::Rigid3DUVE::DeflectVelocityUVE(rigidBody.velocity, towardOtherBody,
                                                              material.friction, material.restitution);
}

/// Resolves one overlapping pair: mass-weighted positional correction (a kinematic or
/// collider-only-static side gets 0% of the correction; two dynamic bodies split proportionally
/// to inverse mass) plus per-body velocity deflection using both sides' combined material.
void ResolvePairUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                    const CollisionPairUVE& pair) {
    const auto InverseMassOfUVE = [&entityManager](Scene::EntityUVE entity) {
        if (!entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
            return Scene::Static3DUVE::InverseMassUVE();
        }
        // An object the simulation may not move - a kinematically driven one, a PhysicsObject3D
        // kept as a static obstacle, or one taken out of the world - has no inverse mass to give
        // way with, exactly like a static collider.
        if (!Scene::IsPhysicsObjectSimulatedUVE(entityManager, entity)) {
            return 0.0F;
        }
        const Scene::Rigid3DComponentUVE& rigidBody =
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
        return Scene::Rigid3DUVE::InverseMassUVE(rigidBody);
    };

    // How much of the overlap each side takes: its inverse mass, divided by its authored collision
    // priority, so heavier bodies and higher priorities yield less. With the default priority of 1
    // this is the plain inverse-mass split it has always been.
    const float firstWeight =
        Scene::GetPhysicsObjectYieldWeightUVE(entityManager, pair.first, InverseMassOfUVE(pair.first));
    const float secondWeight =
        Scene::GetPhysicsObjectYieldWeightUVE(entityManager, pair.second, InverseMassOfUVE(pair.second));
    const float totalWeight = firstWeight + secondWeight;
    if (totalWeight <= 0.0F) {
        return; // Both sides immovable (static/kinematic, or never yields) — nothing to resolve.
    }

    const float firstShare = firstWeight / totalWeight;
    const float secondShare = secondWeight / totalWeight;

    // Both entities in `pair` are guaranteed to have ColliderComponentUVE — DetectCollisionsUVE
    // only ever returns pairs where both sides have one.
    const PhysicsMaterialUVE combinedMaterial = CombineMaterialsUVE(
        MaterialOfUVE(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(pair.first)),
        MaterialOfUVE(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(pair.second)));

    if (firstShare > 0.0F) {
        MoveAndDeflectUVE(entityManager, sceneGraph, pair.first,
                          -pair.separationAxis * (pair.penetrationDepth * firstShare), pair.separationAxis,
                          combinedMaterial);
    }
    if (secondShare > 0.0F) {
        MoveAndDeflectUVE(entityManager, sceneGraph, pair.second,
                          pair.separationAxis * (pair.penetrationDepth * secondShare), -pair.separationAxis,
                          combinedMaterial);
    }
}

} // namespace

PhysicsSystemUVE::PhysicsSystemUVE(ICollisionSystemUVE& collisionSystem, Math::Vector3UVE gravity)
    : m_collisionSystem(&collisionSystem), m_gravity(gravity) {}

void PhysicsSystemUVE::SetConstraintSystemUVE(PhysicsConstraintSystemUVE* constraintSystem) noexcept {
    m_constraintSystem = constraintSystem;
}

void PhysicsSystemUVE::StepUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                                float fixedDeltaTimeSeconds) {
    if (!std::isfinite(fixedDeltaTimeSeconds) || fixedDeltaTimeSeconds < 0.0F ||
        !std::isfinite(m_gravity.x) || !std::isfinite(m_gravity.y) || !std::isfinite(m_gravity.z)) {
        return;
    }
    const std::vector<Area3DBodySpaceUVE> areaSpaces = CollectArea3DBodySpacesUVE(entityManager, m_gravity);
    entityManager.ForEachUVE<Scene::TransformComponentUVE, Scene::Rigid3DComponentUVE>(
        [&entityManager, &sceneGraph, &areaSpaces, this, fixedDeltaTimeSeconds](
            Scene::EntityUVE entity, const Scene::TransformComponentUVE& transform,
            Scene::Rigid3DComponentUVE& rigidBody) {
            if (!Scene::IsRigid3DComponentValidUVE(rigidBody)) {
                UVE_ASSERT(Scene::IsRigid3DComponentValidUVE(rigidBody));
                return;
            }
            if (!Scene::Rigid3DUVE::IsDynamicUVE(rigidBody)) {
                return;
            }
            // A stopped physics object is either out of the world (its collider never reaches the
            // broad phase, so it is not here at all) or kept as an immovable obstacle: either way
            // the simulation does not move it.
            if (!Scene::IsPhysicsObjectSimulatedUVE(entityManager, entity)) {
                return;
            }

            Math::Vector3UVE gravity = m_gravity;
            float linearDamp = rigidBody.drag;
            float angularDamp = 0.0F;
            if (const Area3DBodySpaceUVE* space = FindArea3DBodySpaceUVE(areaSpaces, entity)) {
                gravity = space->gravity;
                linearDamp = space->linearDamp;
                angularDamp = space->angularDamp;
            }
            if (!std::isfinite(angularDamp)) {
                return;
            }
            const std::optional<Math::Vector3UVE> candidateVelocity = Scene::Rigid3DUVE::IntegrateLinearVelocityUVE(
                rigidBody.velocity, gravity, rigidBody.gravityScale, linearDamp, fixedDeltaTimeSeconds);
            if (!candidateVelocity.has_value()) {
                return;
            }

            Math::Vector3UVE candidateAngularVelocity = rigidBody.angularVelocity;
            Math::Vector3UVE effectiveTorque = rigidBody.torque;
            const auto gyroscopicTorque = EvaluateGyroscopicTorqueUVE(
                rigidBody.angularVelocity, rigidBody.inverseInertia);
            if (!gyroscopicTorque.has_value()) {
                return;
            }
            effectiveTorque -= *gyroscopicTorque;
            if (!IsFiniteVector3UVE(effectiveTorque)) {
                return;
            }
            const auto integratedAngularVelocity = IntegrateAngularVelocityUVE(
                rigidBody.angularVelocity, effectiveTorque, rigidBody.inverseInertia,
                fixedDeltaTimeSeconds);
            if (!integratedAngularVelocity.has_value() || !IsFiniteVector3UVE(*integratedAngularVelocity)) {
                return;
            }
            candidateAngularVelocity = *integratedAngularVelocity;
            if (angularDamp > 0.0F) {
                candidateAngularVelocity *=
                    std::max(0.0F, 1.0F - angularDamp * fixedDeltaTimeSeconds);
                if (!IsFiniteVector3UVE(candidateAngularVelocity)) {
                    return;
                }
            }

            Scene::TransformComponentUVE newTransform = transform;
            if (!TryIntegratePositionUVE(transform.localPosition, *candidateVelocity, fixedDeltaTimeSeconds,
                                         newTransform.localPosition)) {
                return;
            }
            const float angularSpeedSquared = Math::LengthSquaredUVE(candidateAngularVelocity);
            if (std::isfinite(angularSpeedSquared) && angularSpeedSquared > 1.0e-12F &&
                std::isfinite(fixedDeltaTimeSeconds) && fixedDeltaTimeSeconds >= 0.0F) {
                const float angularSpeed = std::sqrt(angularSpeedSquared);
                const Math::Vector3UVE axis = candidateAngularVelocity * (1.0F / angularSpeed);
                Math::QuaternionUVE deltaRotation;
                if (Math::TryMakeAxisAngleUVE(axis, angularSpeed * fixedDeltaTimeSeconds, deltaRotation)) {
                    Math::QuaternionUVE normalizedRotation;
                    const Math::QuaternionUVE composedRotation =
                        Math::MultiplyUVE(newTransform.localRotation, deltaRotation);
                    if (Math::TryNormalizeUVE(composedRotation, normalizedRotation)) {
                        newTransform.localRotation = normalizedRotation;
                    }
                }
            }
            rigidBody.velocity = *candidateVelocity;
            rigidBody.angularVelocity = candidateAngularVelocity;
            sceneGraph.SetLocalTransformUVE(entityManager, entity, newTransform);
        });

    // SetLocalTransformUVE only marks WorldTransformComponentUVE dirty; propagate now so
    // DetectCollisionsUVE (which reads WorldTransformComponentUVE) sees this step's
    // post-integration positions, not last step's stale ones.
    sceneGraph.UpdateUVE(entityManager);

    const std::vector<CollisionPairUVE> pairs = m_collisionSystem->DetectCollisionsUVE(entityManager);
    for (const CollisionPairUVE& pair : pairs) {
        ResolvePairUVE(entityManager, sceneGraph, pair);
    }

    if (m_constraintSystem != nullptr) {
        static_cast<void>(m_constraintSystem->SolveUVE(entityManager, sceneGraph));
    }

    // Propagate resolution positions too, so a caller inspecting WorldTransformComponentUVE
    // immediately after StepUVE() (tests, or a second StepUVE() this same frame) sees fully
    // resolved state without needing an external UpdateUVE() call first.
    sceneGraph.UpdateUVE(entityManager);
}

} // namespace UVE::Physics
