// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/physics_system_uve.h"

#include "uve/physics/physics_constraint_system_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/physics/angular_dynamics_uve.h"
#include "uve/physics/area_3d_runtime_uve.h"
#include "uve/physics/collision_pair_uve.h"
#include "uve/physics/continuous_collision_uve.h"
#include "uve/physics/deterministic_physics_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"
#include "uve/physics/physics_islands_uve.h"
#include "uve/physics/physics_material_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"
#include "uve/objects/3d/static_3d_uve.h"
#include "uve/threading/parallel_for_uve.h"

namespace UVE::Physics {

namespace {

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Bounding sphere of a collider's world AABB: its centre plus the half-diagonal as radius.
///
/// Deliberately shape-agnostic. The cache also carries the authored shape, but the AABB is a
/// superset by construction, so a sphere built from it can only ever stop a body EARLY - never
/// late - and early is recoverable while a tunneled hit is not.
[[nodiscard]] Physics::SweptSphereUVE MakeSweptSphereUVE(const Detail::ColliderWorldAabbUVE& collider,
                                                        const Math::Vector3UVE& displacement) noexcept {
    const Math::Vector3UVE center = collider.worldAabb.GetCenterUVE();
    const Math::Vector3UVE extents{collider.worldAabb.max.x - center.x,
                                   collider.worldAabb.max.y - center.y,
                                   collider.worldAabb.max.z - center.z};
    Physics::SweptSphereUVE sphere{};
    sphere.center = center;
    sphere.radius = std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
    sphere.displacement = displacement;
    return sphere;
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

/// Per-body state the island solver mutates.
///
/// The entity manager is explicitly single-threaded (see IEntityManagerUVE's contract), and so is
/// the scene graph, so a worker thread may not read a component or write a transform. The step
/// therefore takes a snapshot of exactly the fields the resolver touches, solves on plain values,
/// and scatters the result from the calling thread.
struct ResolutionBodyUVE final {
    Scene::EntityUVE entity{};
    Math::Vector3UVE localPosition{};
    Math::Vector3UVE velocity{};
    /// True when the body has a dynamic rigid body, i.e. when a correction is allowed to change its
    /// velocity as well as its position.
    bool dynamicUVE = false;
    /// Set once the solver has written to this body. An untouched body is not scattered, so a step
    /// whose pairs all resolve against static geometry does not dirty every transform it looked at.
    bool touchedUVE = false;
};

/// One pair's precomputed corrections, in the order the solver must apply them.
struct ResolutionPairUVE final {
    std::uint32_t firstBodyUVE = 0U;
    std::uint32_t secondBodyUVE = 0U;
    /// Unit vector from `first` toward `second`, as reported by the narrow phase.
    Math::Vector3UVE separationAxis{};
    /// Penetration left after the slop clamp. Kept separate from the shares so the solver performs
    /// the exact same `(penetration * share)` multiplication, in the same order, as the sequential
    /// resolver always has - a reassociated product would round differently and break the promise
    /// that island order does not change the result.
    float penetrationUVE = 0.0F;
    /// How much of that penetration each side takes, from inverse mass and yield weight.
    float firstShareUVE = 0.0F;
    float secondShareUVE = 0.0F;
    float frictionUVE = 0.0F;
    float restitutionUVE = 0.0F;
};

} // namespace

PhysicsSystemUVE::PhysicsSystemUVE(ICollisionSystemUVE& collisionSystem, Math::Vector3UVE gravity)
    : m_collisionSystem(&collisionSystem), m_gravity(gravity) {}

void PhysicsSystemUVE::SetConstraintSystemUVE(PhysicsConstraintSystemUVE* constraintSystem) noexcept {
    m_constraintSystem = constraintSystem;
}

void PhysicsSystemUVE::SetGravityUVE(Math::Vector3UVE gravity) noexcept {
    if (IsFiniteVector3UVE(gravity)) {
        m_gravity = gravity;
    }
}

Math::Vector3UVE PhysicsSystemUVE::GetGravityUVE() const noexcept {
    return m_gravity;
}

void PhysicsSystemUVE::SetSimulationSettingsUVE(const PhysicsSimulationSettingsUVE& settings) noexcept {
    m_settings = settings;
}

PhysicsSimulationSettingsUVE PhysicsSystemUVE::GetSimulationSettingsUVE() const noexcept {
    return m_settings;
}

void PhysicsSystemUVE::SetThreadPoolUVE(Threading::IThreadPoolUVE* threadPool) noexcept {
    m_threadPool = threadPool;
}

void PhysicsSystemUVE::ResolveCollisionPairsUVE(Scene::IEntityManagerUVE& entityManager,
                                                Scene::ISceneGraphUVE& sceneGraph,
                                                const std::vector<CollisionPairUVE>& pairs,
                                                const float fixedDeltaTimeSeconds) {
    m_lastStepIslandCountUVE = 0U;
    m_lastStepParallelIslandCountUVE = 0U;
    if (pairs.empty()) {
        return;
    }

    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);
    if (partition.IsEmptyUVE()) {
        return;
    }
    m_lastStepIslandCountUVE = static_cast<std::uint32_t>(partition.islandsUVE.size());

    // ---- snapshot: the only part that touches the ECS ------------------------------------------
    std::vector<ResolutionBodyUVE> bodies;
    bodies.reserve(partition.bodyCountUVE);
    std::unordered_map<Scene::EntityUVE, std::uint32_t> bodyIndices;
    bodyIndices.reserve(partition.bodyCountUVE);

    const auto bodyIndexOfUVE = [&entityManager, &bodies, &bodyIndices](const Scene::EntityUVE entity) {
        const auto inserted = bodyIndices.try_emplace(entity, static_cast<std::uint32_t>(bodies.size()));
        if (inserted.second) {
            ResolutionBodyUVE body;
            body.entity = entity;
            body.localPosition =
                entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
            if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
                const Scene::Rigid3DComponentUVE& rigidBody =
                    entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
                body.velocity = rigidBody.velocity;
                body.dynamicUVE = Scene::Rigid3DUVE::IsDynamicUVE(rigidBody);
            }
            bodies.push_back(body);
        }
        return inserted.first->second;
    };

    const auto inverseMassOfUVE = [&entityManager](Scene::EntityUVE entity) {
        if (!entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
            return Scene::Static3DUVE::InverseMassUVE();
        }
        // An object the simulation may not move - a kinematically driven one, a PhysicsObject3D
        // kept as a static obstacle, or one taken out of the world - has no inverse mass to give
        // way with, exactly like a static collider.
        if (!Scene::IsPhysicsObjectSimulatedUVE(entityManager, entity)) {
            return 0.0F;
        }
        return Scene::Rigid3DUVE::InverseMassUVE(
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity));
    };

    // Slot-indexed, in island order: slot `i` is pair `partition.pairOrderUVE[i]`. Building the
    // resolution list in solve order (rather than in pair order) means the sequential and parallel
    // paths walk the same sequence, so they cannot drift.
    std::vector<ResolutionPairUVE> resolvedPairs(partition.pairOrderUVE.size());
    for (std::size_t slot = 0U; slot < partition.pairOrderUVE.size(); ++slot) {
        const CollisionPairUVE& source = pairs[partition.pairOrderUVE[slot]];
        ResolutionPairUVE& entry = resolvedPairs[slot];
        entry.firstBodyUVE = bodyIndexOfUVE(source.first);
        entry.secondBodyUVE = bodyIndexOfUVE(source.second);
        entry.separationAxis = source.separationAxis;

        const float firstWeight =
            Scene::GetPhysicsObjectYieldWeightUVE(entityManager, source.first, inverseMassOfUVE(source.first));
        const float secondWeight =
            Scene::GetPhysicsObjectYieldWeightUVE(entityManager, source.second, inverseMassOfUVE(source.second));
        const float totalWeight = firstWeight + secondWeight;
        if (totalWeight <= 0.0F) {
            continue; // Both sides immovable (static/kinematic, or never yields).
        }
        entry.firstShareUVE = firstWeight / totalWeight;
        entry.secondShareUVE = secondWeight / totalWeight;

        const float slop = (std::isfinite(m_settings.penetrationSlop) && m_settings.penetrationSlop > 0.0F)
                               ? m_settings.penetrationSlop
                               : 0.0F;
        float effectivePenetration = std::max(0.0F, source.penetrationDepth - slop);
        if (std::isfinite(m_settings.maxDepenetrationVelocity) && m_settings.maxDepenetrationVelocity > 0.0F &&
            std::isfinite(fixedDeltaTimeSeconds) && fixedDeltaTimeSeconds > 0.0F) {
            effectivePenetration =
                std::min(effectivePenetration, m_settings.maxDepenetrationVelocity * fixedDeltaTimeSeconds);
        }
        entry.penetrationUVE = effectivePenetration;

        // Both entities in `source` are guaranteed to have ColliderComponentUVE - DetectCollisionsUVE
        // only ever returns pairs where both sides have one.
        const PhysicsMaterialUVE combinedMaterial = CombineMaterialsUVE(
            MaterialOfUVE(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(source.first)),
            MaterialOfUVE(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(source.second)));
        entry.frictionUVE = combinedMaterial.friction;
        entry.restitutionUVE = combinedMaterial.restitution;
    }

    // ---- solve: pure value math, safe to run from any thread -----------------------------------
    const auto solvePairsUVE = [&bodies, &resolvedPairs](const std::uint32_t begin, const std::uint32_t end) {
        for (std::uint32_t slot = begin; slot < end; ++slot) {
            const ResolutionPairUVE& pair = resolvedPairs[slot];
            ResolutionBodyUVE& first = bodies[pair.firstBodyUVE];
            ResolutionBodyUVE& second = bodies[pair.secondBodyUVE];
            if (pair.firstShareUVE > 0.0F) {
                first.localPosition += -pair.separationAxis * (pair.penetrationUVE * pair.firstShareUVE);
                if (first.dynamicUVE) {
                    first.velocity = Scene::Rigid3DUVE::DeflectVelocityUVE(
                        first.velocity, pair.separationAxis, pair.frictionUVE, pair.restitutionUVE);
                }
                first.touchedUVE = true;
            }
            if (pair.secondShareUVE > 0.0F) {
                second.localPosition += pair.separationAxis * (pair.penetrationUVE * pair.secondShareUVE);
                if (second.dynamicUVE) {
                    second.velocity = Scene::Rigid3DUVE::DeflectVelocityUVE(
                        second.velocity, -pair.separationAxis, pair.frictionUVE, pair.restitutionUVE);
                }
                second.touchedUVE = true;
            }
        }
    };

    const std::uint32_t pairSlots = static_cast<std::uint32_t>(resolvedPairs.size());
    const bool poolUsable = m_threadPool != nullptr && m_threadPool->GetWorkerCountUVE() > 1U;
    const bool worthSplitting = pairSlots >= m_settings.parallelStepping.minimumPairCount &&
                                partition.islandsUVE.size() > 1U;
    if (m_settings.parallelStepping.enabled && poolUsable && worthSplitting) {
        Threading::ParallelForUVE(
            *m_threadPool, 0U, partition.islandsUVE.size(),
            [&partition, &solvePairsUVE](const std::size_t islandIndex) {
                const PhysicsIslandUVE& island = partition.islandsUVE[islandIndex];
                solvePairsUVE(island.firstPairUVE, island.firstPairUVE + island.pairCountUVE);
            });
        m_lastStepParallelIslandCountUVE = static_cast<std::uint32_t>(partition.islandsUVE.size());
    } else {
        solvePairsUVE(0U, pairSlots);
    }

    // ---- scatter: back on the calling thread ---------------------------------------------------
    for (const ResolutionBodyUVE& body : bodies) {
        if (!body.touchedUVE) {
            continue;
        }
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(body.entity);
        transform.localPosition = body.localPosition;
        sceneGraph.SetLocalTransformUVE(entityManager, body.entity, transform);
        if (body.dynamicUVE) {
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body.entity).velocity = body.velocity;
        }
    }
}

void PhysicsSystemUVE::StepUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                                float fixedDeltaTimeSeconds) {
    if (!std::isfinite(fixedDeltaTimeSeconds) || fixedDeltaTimeSeconds < 0.0F ||
        !std::isfinite(m_gravity.x) || !std::isfinite(m_gravity.y) || !std::isfinite(m_gravity.z)) {
        return;
    }
    const std::vector<Area3DBodySpaceUVE> areaSpaces = CollectArea3DBodySpacesUVE(entityManager, m_gravity);
    // One world-AABB snapshot for the whole step. Only the colliders' extents are needed, so this
    // reuses the same deterministic cache the collision and raycast systems already build rather
    // than introducing a second extraction path that could disagree with theirs.
    const bool continuousCollisionEnabled = m_settings.continuousCollision.enabled;
    std::vector<Detail::ColliderWorldAabbUVE> ccdColliders;
    if (continuousCollisionEnabled) {
        ccdColliders = Detail::BuildColliderWorldAabbCacheUVE(entityManager);
    }
    // Scratch for one mover's filtered target list, reused across entities. Cleared per body so a
    // body that stops being fast cannot inherit the previous body's targets.
    std::vector<Math::AabbUVE> ccdTargets;
    entityManager.ForEachUVE<Scene::TransformComponentUVE, Scene::Rigid3DComponentUVE>(
        [&entityManager, &sceneGraph, &areaSpaces, &ccdColliders, &ccdTargets, continuousCollisionEnabled,
         this, fixedDeltaTimeSeconds](
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
            float linearDamp = rigidBody.drag > 0.0F
                                   ? rigidBody.drag
                                   : ((std::isfinite(m_settings.defaultLinearDamping) &&
                                       m_settings.defaultLinearDamping > 0.0F)
                                          ? m_settings.defaultLinearDamping
                                          : 0.0F);
            float angularDamp = (std::isfinite(m_settings.defaultAngularDamping) &&
                                 m_settings.defaultAngularDamping > 0.0F)
                                    ? m_settings.defaultAngularDamping
                                    : 0.0F;
            if (const Area3DBodySpaceUVE* space = FindArea3DBodySpaceUVE(areaSpaces, entity)) {
                gravity = space->gravity;
                linearDamp = space->linearDamp;
                angularDamp = space->angularDamp;
            }
            if (!std::isfinite(angularDamp)) {
                return;
            }
            const std::optional<Math::Vector3UVE> candidateVelocity = Scene::Rigid3DUVE::IntegrateLinearVelocityUVE(
                rigidBody.velocity, gravity, rigidBody.gravityScale, linearDamp, fixedDeltaTimeSeconds,
                rigidBody.force, Scene::Rigid3DUVE::InverseMassUVE(rigidBody));
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

            Math::Vector3UVE finalLinearVelocity = *candidateVelocity;
            if (m_settings.allowSleeping && std::isfinite(m_settings.sleepThreshold) &&
                m_settings.sleepThreshold > 0.0F) {
                const float thresholdSq = m_settings.sleepThreshold * m_settings.sleepThreshold;
                const float priorLinearSq = Math::LengthSquaredUVE(rigidBody.velocity);
                const float nextLinearSq = Math::LengthSquaredUVE(finalLinearVelocity);
                const float nextAngularSq = Math::LengthSquaredUVE(candidateAngularVelocity);
                if (priorLinearSq > 0.0F && nextLinearSq <= priorLinearSq &&
                    nextLinearSq < thresholdSq && nextAngularSq < thresholdSq) {
                    finalLinearVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
                    candidateAngularVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
                }
            }

            // ---- continuous collision ---------------------------------------------------------
            // The discrete step below moves the body by velocity * dt with no idea what lies
            // between the two positions, so a body travelling more than its own extent in one step
            // can pass clean through a wall. The sweep answers exactly one question - "how far can
            // this body travel before it touches something?" - and shortens the step to that.
            Math::Vector3UVE integrationVelocity = finalLinearVelocity;
            if (continuousCollisionEnabled && fixedDeltaTimeSeconds > 0.0F) {
                const Detail::ColliderWorldAabbUVE* ownCollider = nullptr;
                for (const Detail::ColliderWorldAabbUVE& candidate : ccdColliders) {
                    if (candidate.entity == entity) {
                        ownCollider = &candidate;
                        break;
                    }
                }
                if (ownCollider != nullptr) {
                    const Math::Vector3UVE displacement{
                        finalLinearVelocity.x * fixedDeltaTimeSeconds,
                        finalLinearVelocity.y * fixedDeltaTimeSeconds,
                        finalLinearVelocity.z * fixedDeltaTimeSeconds};
                    ccdTargets.clear();
                    for (const Detail::ColliderWorldAabbUVE& target : ccdColliders) {
                        if (target.entity == entity) {
                            continue;
                        }
                        // Same two-way mask rule the collision system uses, so CCD cannot stop a
                        // body against geometry the discrete resolver would have ignored.
                        if ((ownCollider->collisionLayer & target.collisionMask) == 0U ||
                            (target.collisionLayer & ownCollider->collisionMask) == 0U) {
                            continue;
                        }
                        ccdTargets.push_back(target.worldAabb);
                    }
                    const ContinuousMotionResultUVE swept = ResolveContinuousMotionUVE(
                        MakeSweptSphereUVE(*ownCollider, displacement), ccdTargets,
                        m_settings.continuousCollision);
                    if (swept.contact.has_value()) {
                        const Math::Vector3UVE& normal = swept.contact->normal;
                        const float intoSurface =
                            finalLinearVelocity.x * normal.x + finalLinearVelocity.y * normal.y +
                            finalLinearVelocity.z * normal.z;
                        if (intoSurface < 0.0F) {
                            // Remove only the component pressed into the surface: the tangential
                            // part is what makes a body slide along a wall instead of sticking.
                            finalLinearVelocity.x -= normal.x * intoSurface;
                            finalLinearVelocity.y -= normal.y * intoSurface;
                            finalLinearVelocity.z -= normal.z * intoSurface;
                        }
                        if (swept.contact->timeOfImpact > 0.0F) {
                            const float inverseDelta = 1.0F / fixedDeltaTimeSeconds;
                            integrationVelocity = Math::Vector3UVE{
                                swept.displacement.x * inverseDelta,
                                swept.displacement.y * inverseDelta,
                                swept.displacement.z * inverseDelta};
                        }
                    }
                }
            }

            Scene::TransformComponentUVE newTransform = transform;
            if (!TryIntegratePositionUVE(transform.localPosition, integrationVelocity, fixedDeltaTimeSeconds,
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
            rigidBody.velocity = finalLinearVelocity;
            rigidBody.angularVelocity = candidateAngularVelocity;
            sceneGraph.SetLocalTransformUVE(entityManager, entity, newTransform);
        });

    // SetLocalTransformUVE only marks WorldTransformComponentUVE dirty; propagate now so
    // DetectCollisionsUVE (which reads WorldTransformComponentUVE) sees this step's
    // post-integration positions, not last step's stale ones.
    sceneGraph.UpdateUVE(entityManager);

    std::vector<CollisionPairUVE> pairs = m_collisionSystem->DetectCollisionsUVE(entityManager);
    if (m_settings.deterministic.enabled) {
        // Canonical order before resolution, not after: the solver is sequential, so every pair
        // writes positions the next pair reads. Sorting a list that has already been resolved
        // would change nothing, which is the mistake this line exists to not make.
        CanonicalizeCollisionPairsUVE(pairs);
    }
    ResolveCollisionPairsUVE(entityManager, sceneGraph, pairs, fixedDeltaTimeSeconds);

    if (m_constraintSystem != nullptr) {
        const std::size_t solverIterations =
            m_settings.solverIterations > 0U
                ? m_settings.solverIterations
                : PhysicsConstraintSystemUVE::kMaximumSolverIterationsUVE;
        static_cast<void>(m_constraintSystem->SolveUVE(entityManager, sceneGraph, solverIterations));
    }

    // Propagate resolution positions too, so a caller inspecting WorldTransformComponentUVE
    // immediately after StepUVE() (tests, or a second StepUVE() this same frame) sees fully
    // resolved state without needing an external UpdateUVE() call first.
    sceneGraph.UpdateUVE(entityManager);

    // The digest is taken from the state the step actually published - after resolution, after the
    // constraint solver, after world propagation - because that is the state a peer would send and
    // a replay would compare. Digging it out mid-step would describe a world that never existed.
    if (m_settings.deterministic.enabled) {
        std::vector<DeterministicBodySampleUVE> samples;
        entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::Rigid3DComponentUVE>(
            [&samples](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& world,
                       const Scene::Rigid3DComponentUVE& rigidBody) {
                DeterministicBodySampleUVE sample{};
                sample.entity = entity;
                sample.position = world.worldPosition;
                sample.rotation = world.worldRotation;
                sample.velocity = rigidBody.velocity;
                sample.angularVelocity = rigidBody.angularVelocity;
                samples.push_back(sample);
            });
        m_lastStepDigest = ComputeWorldDigestUVE(samples, m_settings.deterministic);
    } else {
        m_lastStepDigest = 0U;
    }
}

} // namespace UVE::Physics
