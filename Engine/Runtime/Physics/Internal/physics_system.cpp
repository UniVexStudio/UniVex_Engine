// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_system.h"

#include "physics_constraint_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "engine_assert.h"
#include "quaternion.h"
#include "angular_dynamics.h"
#include "area_3d_runtime.h"
#include "collision_pair.h"
#include "continuous_collision.h"
#include "deterministic_physics.h"
#include "collider_world_aabb_cache.h"
#include "physics_islands.h"
#include "physics_material.h"
#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "abstract_physics_objects_3d.h"
#include "rigid_3d.h"
#include "static_3d.h"
#include "parallel_for.h"

namespace UVE::Physics {

namespace {

[[nodiscard]] bool IsFiniteVector3(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Bounding sphere of a collider's world AABB: its centre plus the half-diagonal as radius.
///
/// Deliberately shape-agnostic. The cache also carries the authored shape, but the AABB is a
/// superset by construction, so a sphere built from it can only ever stop a body EARLY - never
/// late - and early is recoverable while a tunneled hit is not.
[[nodiscard]] Physics::SweptSphere MakeSweptSphere(const Detail::ColliderWorldAabb& collider,
                                                        const Math::Vector3& displacement) noexcept {
    const Math::Vector3 center = collider.worldAabb.GetCenter();
    const Math::Vector3 extents{collider.worldAabb.max.x - center.x,
                                   collider.worldAabb.max.y - center.y,
                                   collider.worldAabb.max.z - center.z};
    Physics::SweptSphere sphere{};
    sphere.center = center;
    sphere.radius = std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
    sphere.displacement = displacement;
    return sphere;
}

[[nodiscard]] bool TryIntegratePosition(const Math::Vector3& position,
                                           const Math::Vector3& velocity,
                                           float fixedDeltaTimeSeconds,
                                           Math::Vector3& integratedPosition) noexcept {
    const double deltaTime = static_cast<double>(fixedDeltaTimeSeconds);
    const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
    const double x = static_cast<double>(position.x) + static_cast<double>(velocity.x) * deltaTime;
    const double y = static_cast<double>(position.y) + static_cast<double>(velocity.y) * deltaTime;
    const double z = static_cast<double>(position.z) + static_cast<double>(velocity.z) * deltaTime;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > maxFloat ||
        std::abs(y) > maxFloat || std::abs(z) > maxFloat) {
        return false;
    }
    integratedPosition = Math::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    return IsFiniteVector3(integratedPosition);
}

/// Per-body state the island solver mutates.
///
/// The entity manager is explicitly single-threaded (see IEntityManager's contract), and so is
/// the scene graph, so a worker thread may not read a component or write a transform. The step
/// therefore takes a snapshot of exactly the fields the resolver touches, solves on plain values,
/// and scatters the result from the calling thread.
struct ResolutionBody final {
    Scene::Entity entity{};
    Math::Vector3 localPosition{};
    Math::Vector3 velocity{};
    /// True when the body has a dynamic rigid body, i.e. when a correction is allowed to change its
    /// velocity as well as its position.
    bool dynamic = false;
    /// Set once the solver has written to this body. An untouched body is not scattered, so a step
    /// whose pairs all resolve against static geometry does not dirty every transform it looked at.
    bool touched = false;
};

/// One pair's precomputed corrections, in the order the solver must apply them.
struct ResolutionPair final {
    std::uint32_t firstBody = 0U;
    std::uint32_t secondBody = 0U;
    /// Unit vector from `first` toward `second`, as reported by the narrow phase.
    Math::Vector3 separationAxis{};
    /// Penetration left after the slop clamp. Kept separate from the shares so the solver performs
    /// the exact same `(penetration * share)` multiplication, in the same order, as the sequential
    /// resolver always has - a reassociated product would round differently and break the promise
    /// that island order does not change the result.
    float penetration = 0.0F;
    /// How much of that penetration each side takes, from inverse mass and yield weight.
    float firstShare = 0.0F;
    float secondShare = 0.0F;
    float friction = 0.0F;
    float restitution = 0.0F;
};

} // namespace

PhysicsSystem::PhysicsSystem(ICollisionSystem& collisionSystem, Math::Vector3 gravity)
    : m_collisionSystem(&collisionSystem), m_gravity(gravity) {}

void PhysicsSystem::SetConstraintSystem(PhysicsConstraintSystem* constraintSystem) noexcept {
    m_constraintSystem = constraintSystem;
}

void PhysicsSystem::SetGravity(Math::Vector3 gravity) noexcept {
    if (IsFiniteVector3(gravity)) {
        m_gravity = gravity;
    }
}

Math::Vector3 PhysicsSystem::GetGravity() const noexcept {
    return m_gravity;
}

void PhysicsSystem::SetSimulationSettings(const PhysicsSimulationSettings& settings) noexcept {
    m_settings = settings;
}

PhysicsSimulationSettings PhysicsSystem::GetSimulationSettings() const noexcept {
    return m_settings;
}

void PhysicsSystem::SetThreadPool(Threading::IThreadPool* threadPool) noexcept {
    m_threadPool = threadPool;
}

void PhysicsSystem::ResolveCollisionPairs(Scene::IEntityManager& entityManager,
                                                Scene::ISceneGraph& sceneGraph,
                                                const std::vector<CollisionPair>& pairs,
                                                const float fixedDeltaTimeSeconds) {
    m_lastStepIslandCount = 0U;
    m_lastStepParallelIslandCount = 0U;
    if (pairs.empty()) {
        return;
    }

    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);
    if (partition.IsEmpty()) {
        return;
    }
    m_lastStepIslandCount = static_cast<std::uint32_t>(partition.islands.size());

    // ---- snapshot: the only part that touches the ECS ------------------------------------------
    std::vector<ResolutionBody> bodies;
    bodies.reserve(partition.bodyCount);
    std::unordered_map<Scene::Entity, std::uint32_t> bodyIndices;
    bodyIndices.reserve(partition.bodyCount);

    const auto bodyIndexOf = [&entityManager, &bodies, &bodyIndices](const Scene::Entity entity) {
        const auto inserted = bodyIndices.try_emplace(entity, static_cast<std::uint32_t>(bodies.size()));
        if (inserted.second) {
            ResolutionBody body;
            body.entity = entity;
            body.localPosition =
                entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition;
            if (entityManager.HasComponent<Scene::Rigid3DComponent>(entity)) {
                const Scene::Rigid3DComponent& rigidBody =
                    entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
                body.velocity = rigidBody.velocity;
                body.dynamic = Scene::Rigid3D::IsDynamic(rigidBody);
            }
            bodies.push_back(body);
        }
        return inserted.first->second;
    };

    const auto inverseMassOf = [&entityManager](Scene::Entity entity) {
        if (!entityManager.HasComponent<Scene::Rigid3DComponent>(entity)) {
            return Scene::Static3D::InverseMass();
        }
        // An object the simulation may not move - a kinematically driven one, a PhysicsObject3D
        // kept as a static obstacle, or one taken out of the world - has no inverse mass to give
        // way with, exactly like a static collider.
        if (!Scene::IsPhysicsObjectSimulated(entityManager, entity)) {
            return 0.0F;
        }
        return Scene::Rigid3D::InverseMass(
            entityManager.GetComponent<Scene::Rigid3DComponent>(entity));
    };

    // Slot-indexed, in island order: slot `i` is pair `partition.pairOrder[i]`. Building the
    // resolution list in solve order (rather than in pair order) means the sequential and parallel
    // paths walk the same sequence, so they cannot drift.
    std::vector<ResolutionPair> resolvedPairs(partition.pairOrder.size());
    for (std::size_t slot = 0U; slot < partition.pairOrder.size(); ++slot) {
        const CollisionPair& source = pairs[partition.pairOrder[slot]];
        ResolutionPair& entry = resolvedPairs[slot];
        entry.firstBody = bodyIndexOf(source.first);
        entry.secondBody = bodyIndexOf(source.second);
        entry.separationAxis = source.separationAxis;

        const float firstWeight =
            Scene::GetPhysicsObjectYieldWeight(entityManager, source.first, inverseMassOf(source.first));
        const float secondWeight =
            Scene::GetPhysicsObjectYieldWeight(entityManager, source.second, inverseMassOf(source.second));
        const float totalWeight = firstWeight + secondWeight;
        if (totalWeight <= 0.0F) {
            continue; // Both sides immovable (static/kinematic, or never yields).
        }
        entry.firstShare = firstWeight / totalWeight;
        entry.secondShare = secondWeight / totalWeight;

        const float slop = (std::isfinite(m_settings.penetrationSlop) && m_settings.penetrationSlop > 0.0F)
                               ? m_settings.penetrationSlop
                               : 0.0F;
        float effectivePenetration = std::max(0.0F, source.penetrationDepth - slop);
        if (std::isfinite(m_settings.maxDepenetrationVelocity) && m_settings.maxDepenetrationVelocity > 0.0F &&
            std::isfinite(fixedDeltaTimeSeconds) && fixedDeltaTimeSeconds > 0.0F) {
            effectivePenetration =
                std::min(effectivePenetration, m_settings.maxDepenetrationVelocity * fixedDeltaTimeSeconds);
        }
        entry.penetration = effectivePenetration;

        // Both entities in `source` are guaranteed to have ColliderComponent - DetectCollisions
        // only ever returns pairs where both sides have one.
        const PhysicsMaterial combinedMaterial = CombineMaterials(
            MaterialOf(entityManager.GetComponent<Scene::ColliderComponent>(source.first)),
            MaterialOf(entityManager.GetComponent<Scene::ColliderComponent>(source.second)));
        entry.friction = combinedMaterial.friction;
        entry.restitution = combinedMaterial.restitution;
    }

    // ---- solve: pure value math, safe to run from any thread -----------------------------------
    const auto solvePairs = [&bodies, &resolvedPairs](const std::uint32_t begin, const std::uint32_t end) {
        for (std::uint32_t slot = begin; slot < end; ++slot) {
            const ResolutionPair& pair = resolvedPairs[slot];
            ResolutionBody& first = bodies[pair.firstBody];
            ResolutionBody& second = bodies[pair.secondBody];
            if (pair.firstShare > 0.0F) {
                first.localPosition += -pair.separationAxis * (pair.penetration * pair.firstShare);
                if (first.dynamic) {
                    first.velocity = Scene::Rigid3D::DeflectVelocity(
                        first.velocity, pair.separationAxis, pair.friction, pair.restitution);
                }
                first.touched = true;
            }
            if (pair.secondShare > 0.0F) {
                second.localPosition += pair.separationAxis * (pair.penetration * pair.secondShare);
                if (second.dynamic) {
                    second.velocity = Scene::Rigid3D::DeflectVelocity(
                        second.velocity, -pair.separationAxis, pair.friction, pair.restitution);
                }
                second.touched = true;
            }
        }
    };

    const std::uint32_t pairSlots = static_cast<std::uint32_t>(resolvedPairs.size());
    const bool poolUsable = m_threadPool != nullptr && m_threadPool->GetWorkerCount() > 1U;
    const bool worthSplitting = pairSlots >= m_settings.parallelStepping.minimumPairCount &&
                                partition.islands.size() > 1U;
    if (m_settings.parallelStepping.enabled && poolUsable && worthSplitting) {
        Threading::ParallelFor(
            *m_threadPool, 0U, partition.islands.size(),
            [&partition, &solvePairs](const std::size_t islandIndex) {
                const PhysicsIsland& island = partition.islands[islandIndex];
                solvePairs(island.firstPair, island.firstPair + island.pairCount);
            });
        m_lastStepParallelIslandCount = static_cast<std::uint32_t>(partition.islands.size());
    } else {
        solvePairs(0U, pairSlots);
    }

    // ---- scatter: back on the calling thread ---------------------------------------------------
    for (const ResolutionBody& body : bodies) {
        if (!body.touched) {
            continue;
        }
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(body.entity);
        transform.localPosition = body.localPosition;
        sceneGraph.SetLocalTransform(entityManager, body.entity, transform);
        if (body.dynamic) {
            entityManager.GetComponent<Scene::Rigid3DComponent>(body.entity).velocity = body.velocity;
        }
    }
}

void PhysicsSystem::Step(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                                float fixedDeltaTimeSeconds) {
    if (!std::isfinite(fixedDeltaTimeSeconds) || fixedDeltaTimeSeconds < 0.0F ||
        !std::isfinite(m_gravity.x) || !std::isfinite(m_gravity.y) || !std::isfinite(m_gravity.z)) {
        return;
    }
    const std::vector<Area3DBodySpace> areaSpaces = CollectArea3DBodySpaces(entityManager, m_gravity);
    // One world-AABB snapshot for the whole step. Only the colliders' extents are needed, so this
    // reuses the same deterministic cache the collision and raycast systems already build rather
    // than introducing a second extraction path that could disagree with theirs.
    const bool continuousCollisionEnabled = m_settings.continuousCollision.enabled;
    std::vector<Detail::ColliderWorldAabb> ccdColliders;
    if (continuousCollisionEnabled) {
        ccdColliders = Detail::BuildColliderWorldAabbCache(entityManager);
    }
    // Scratch for one mover's filtered target list, reused across entities. Cleared per body so a
    // body that stops being fast cannot inherit the previous body's targets.
    std::vector<Math::Aabb> ccdTargets;
    entityManager.ForEach<Scene::TransformComponent, Scene::Rigid3DComponent>(
        [&entityManager, &sceneGraph, &areaSpaces, &ccdColliders, &ccdTargets, continuousCollisionEnabled,
         this, fixedDeltaTimeSeconds](
            Scene::Entity entity, const Scene::TransformComponent& transform,
            Scene::Rigid3DComponent& rigidBody) {
            if (!Scene::IsRigid3DComponentValid(rigidBody)) {
                UVE_ASSERT(Scene::IsRigid3DComponentValid(rigidBody));
                return;
            }
            if (!Scene::Rigid3D::IsDynamic(rigidBody)) {
                return;
            }
            // A stopped physics object is either out of the world (its collider never reaches the
            // broad phase, so it is not here at all) or kept as an immovable obstacle: either way
            // the simulation does not move it.
            if (!Scene::IsPhysicsObjectSimulated(entityManager, entity)) {
                return;
            }

            Math::Vector3 gravity = m_gravity;
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
            if (const Area3DBodySpace* space = FindArea3DBodySpace(areaSpaces, entity)) {
                gravity = space->gravity;
                linearDamp = space->linearDamp;
                angularDamp = space->angularDamp;
            }
            if (!std::isfinite(angularDamp)) {
                return;
            }
            const std::optional<Math::Vector3> candidateVelocity = Scene::Rigid3D::IntegrateLinearVelocity(
                rigidBody.velocity, gravity, rigidBody.gravityScale, linearDamp, fixedDeltaTimeSeconds,
                rigidBody.force, Scene::Rigid3D::InverseMass(rigidBody));
            if (!candidateVelocity.has_value()) {
                return;
            }

            Math::Vector3 candidateAngularVelocity = rigidBody.angularVelocity;
            Math::Vector3 effectiveTorque = rigidBody.torque;
            const auto gyroscopicTorque = EvaluateGyroscopicTorque(
                rigidBody.angularVelocity, rigidBody.inverseInertia);
            if (!gyroscopicTorque.has_value()) {
                return;
            }
            effectiveTorque -= *gyroscopicTorque;
            if (!IsFiniteVector3(effectiveTorque)) {
                return;
            }
            const auto integratedAngularVelocity = IntegrateAngularVelocity(
                rigidBody.angularVelocity, effectiveTorque, rigidBody.inverseInertia,
                fixedDeltaTimeSeconds);
            if (!integratedAngularVelocity.has_value() || !IsFiniteVector3(*integratedAngularVelocity)) {
                return;
            }
            candidateAngularVelocity = *integratedAngularVelocity;
            if (angularDamp > 0.0F) {
                candidateAngularVelocity *=
                    std::max(0.0F, 1.0F - angularDamp * fixedDeltaTimeSeconds);
                if (!IsFiniteVector3(candidateAngularVelocity)) {
                    return;
                }
            }

            Math::Vector3 finalLinearVelocity = *candidateVelocity;
            if (m_settings.allowSleeping && std::isfinite(m_settings.sleepThreshold) &&
                m_settings.sleepThreshold > 0.0F) {
                const float thresholdSq = m_settings.sleepThreshold * m_settings.sleepThreshold;
                const float priorLinearSq = Math::LengthSquared(rigidBody.velocity);
                const float nextLinearSq = Math::LengthSquared(finalLinearVelocity);
                const float nextAngularSq = Math::LengthSquared(candidateAngularVelocity);
                if (priorLinearSq > 0.0F && nextLinearSq <= priorLinearSq &&
                    nextLinearSq < thresholdSq && nextAngularSq < thresholdSq) {
                    finalLinearVelocity = Math::Vector3{0.0F, 0.0F, 0.0F};
                    candidateAngularVelocity = Math::Vector3{0.0F, 0.0F, 0.0F};
                }
            }

            // ---- continuous collision ---------------------------------------------------------
            // The discrete step below moves the body by velocity * dt with no idea what lies
            // between the two positions, so a body travelling more than its own extent in one step
            // can pass clean through a wall. The sweep answers exactly one question - "how far can
            // this body travel before it touches something?" - and shortens the step to that.
            Math::Vector3 integrationVelocity = finalLinearVelocity;
            if (continuousCollisionEnabled && fixedDeltaTimeSeconds > 0.0F) {
                const Detail::ColliderWorldAabb* ownCollider = nullptr;
                for (const Detail::ColliderWorldAabb& candidate : ccdColliders) {
                    if (candidate.entity == entity) {
                        ownCollider = &candidate;
                        break;
                    }
                }
                if (ownCollider != nullptr) {
                    const Math::Vector3 displacement{
                        finalLinearVelocity.x * fixedDeltaTimeSeconds,
                        finalLinearVelocity.y * fixedDeltaTimeSeconds,
                        finalLinearVelocity.z * fixedDeltaTimeSeconds};
                    ccdTargets.clear();
                    for (const Detail::ColliderWorldAabb& target : ccdColliders) {
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
                    const ContinuousMotionResult swept = ResolveContinuousMotion(
                        MakeSweptSphere(*ownCollider, displacement), ccdTargets,
                        m_settings.continuousCollision);
                    if (swept.contact.has_value()) {
                        const Math::Vector3& normal = swept.contact->normal;
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
                            integrationVelocity = Math::Vector3{
                                swept.displacement.x * inverseDelta,
                                swept.displacement.y * inverseDelta,
                                swept.displacement.z * inverseDelta};
                        }
                    }
                }
            }

            Scene::TransformComponent newTransform = transform;
            if (!TryIntegratePosition(transform.localPosition, integrationVelocity, fixedDeltaTimeSeconds,
                                         newTransform.localPosition)) {
                return;
            }
            const float angularSpeedSquared = Math::LengthSquared(candidateAngularVelocity);
            if (std::isfinite(angularSpeedSquared) && angularSpeedSquared > 1.0e-12F &&
                std::isfinite(fixedDeltaTimeSeconds) && fixedDeltaTimeSeconds >= 0.0F) {
                const float angularSpeed = std::sqrt(angularSpeedSquared);
                const Math::Vector3 axis = candidateAngularVelocity * (1.0F / angularSpeed);
                Math::Quaternion deltaRotation;
                if (Math::TryMakeAxisAngle(axis, angularSpeed * fixedDeltaTimeSeconds, deltaRotation)) {
                    Math::Quaternion normalizedRotation;
                    const Math::Quaternion composedRotation =
                        Math::Multiply(newTransform.localRotation, deltaRotation);
                    if (Math::TryNormalize(composedRotation, normalizedRotation)) {
                        newTransform.localRotation = normalizedRotation;
                    }
                }
            }
            rigidBody.velocity = finalLinearVelocity;
            rigidBody.angularVelocity = candidateAngularVelocity;
            sceneGraph.SetLocalTransform(entityManager, entity, newTransform);
        });

    // SetLocalTransform only marks WorldTransformComponent dirty; propagate now so
    // DetectCollisions (which reads WorldTransformComponent) sees this step's
    // post-integration positions, not last step's stale ones.
    sceneGraph.Update(entityManager);

    std::vector<CollisionPair> pairs = m_collisionSystem->DetectCollisions(entityManager);
    if (m_settings.deterministic.enabled) {
        // Canonical order before resolution, not after: the solver is sequential, so every pair
        // writes positions the next pair reads. Sorting a list that has already been resolved
        // would change nothing, which is the mistake this line exists to not make.
        CanonicalizeCollisionPairs(pairs);
    }
    ResolveCollisionPairs(entityManager, sceneGraph, pairs, fixedDeltaTimeSeconds);

    if (m_constraintSystem != nullptr) {
        const std::size_t solverIterations =
            m_settings.solverIterations > 0U
                ? m_settings.solverIterations
                : PhysicsConstraintSystem::kMaximumSolverIterations;
        static_cast<void>(m_constraintSystem->Solve(entityManager, sceneGraph, solverIterations));
    }

    // Propagate resolution positions too, so a caller inspecting WorldTransformComponent
    // immediately after Step() (tests, or a second Step() this same frame) sees fully
    // resolved state without needing an external Update() call first.
    sceneGraph.Update(entityManager);

    // The digest is taken from the state the step actually published - after resolution, after the
    // constraint solver, after world propagation - because that is the state a peer would send and
    // a replay would compare. Digging it out mid-step would describe a world that never existed.
    if (m_settings.deterministic.enabled) {
        std::vector<DeterministicBodySample> samples;
        entityManager.ForEach<Scene::WorldTransformComponent, Scene::Rigid3DComponent>(
            [&samples](Scene::Entity entity, const Scene::WorldTransformComponent& world,
                       const Scene::Rigid3DComponent& rigidBody) {
                DeterministicBodySample sample{};
                sample.entity = entity;
                sample.position = world.worldPosition;
                sample.rotation = world.worldRotation;
                sample.velocity = rigidBody.velocity;
                sample.angularVelocity = rigidBody.angularVelocity;
                samples.push_back(sample);
            });
        m_lastStepDigest = ComputeWorldDigest(samples, m_settings.deterministic);
    } else {
        m_lastStepDigest = 0U;
    }
}

} // namespace UVE::Physics
