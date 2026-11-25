// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_controller.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "scalar.h"
#include "vector3.h"
#include "collider_world_aabb_cache.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] bool HasFiniteLength(const Math::Vector3& value) noexcept {
    const float lengthSquared = Math::LengthSquared(value);
    return std::isfinite(lengthSquared) && lengthSquared >= 0.0F;
}

[[nodiscard]] float FiniteLength(const Math::Vector3& value) noexcept {
    return HasFiniteLength(value) ? std::sqrt(Math::LengthSquared(value)) : 0.0F;
}

[[nodiscard]] Math::Vector3 RemoveIntoNormalComponent(
    const Math::Vector3& displacement, const Math::Vector3& normal) noexcept {
    const float normalLengthSquared = Math::LengthSquared(normal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F) {
        return displacement;
    }
    const float intoSurface = Math::Dot(displacement, normal);
    if (!std::isfinite(intoSurface) || intoSurface <= 0.0F) {
        return displacement;
    }
    return displacement - normal * (intoSurface / normalLengthSquared);
}

bool ValidateControllerInput(Scene::IEntityManager& entityManager,
                                  const CharacterControllerInput& input,
                                  CharacterControllerMoveResult& result) {
    if (!entityManager.IsAlive(input.entity)) {
        result.code = CharacterControllerMoveCode::InvalidEntity;
        return false;
    }
    if (!entityManager.HasComponent<Scene::TransformComponent>(input.entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(input.entity)) {
        result.code = CharacterControllerMoveCode::MissingTransform;
        return false;
    }
    if (!entityManager.HasComponent<Scene::ColliderComponent>(input.entity)) {
        result.code = CharacterControllerMoveCode::MissingCollider;
        return false;
    }
    if (!Math::IsFinite(input.desiredDisplacement) || !HasFiniteLength(input.desiredDisplacement) ||
        !Scene::IsColliderComponentValid(
            entityManager.GetComponent<Scene::ColliderComponent>(input.entity))) {
        result.code = CharacterControllerMoveCode::InvalidInput;
        return false;
    }
    if (entityManager.HasComponent<Scene::Rigid3DComponent>(input.entity)) {
        const Scene::Rigid3DComponent& rigidBody =
            entityManager.GetComponent<Scene::Rigid3DComponent>(input.entity);
        if (!Scene::IsRigid3DComponentValid(rigidBody)) {
            result.code = CharacterControllerMoveCode::InvalidInput;
            return false;
        }
        if (!rigidBody.isKinematic) {
            result.code = CharacterControllerMoveCode::NonKinematicBody;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ApplyLocalDelta(Scene::IEntityManager& entityManager,
                                      Scene::ISceneGraph& sceneGraph, Scene::Entity entity,
                                      const Math::Vector3& delta) {
    if (!Math::IsFinite(delta)) {
        return false;
    }
    Scene::TransformComponent transform = entityManager.GetComponent<Scene::TransformComponent>(entity);
    const Math::Vector3 candidatePosition = transform.localPosition + delta;
    if (!Math::IsFinite(candidatePosition)) {
        return false;
    }
    transform.localPosition = candidatePosition;
    sceneGraph.SetLocalTransform(entityManager, entity, transform);
    return true;
}

[[nodiscard]] float ResolveGroundSlopeCosine(
    const CharacterControllerInput& input, CharacterControllerMoveResult& result) noexcept {
    float degrees = input.maximumGroundSlopeDegrees;
    if (!std::isfinite(degrees) || degrees < 0.0F) {
        degrees = CharacterController::kDefaultMaximumGroundSlopeDegrees;
        result.inputClamped = true;
    }
    if (degrees > CharacterController::kMaximumGroundSlopeDegrees) {
        degrees = CharacterController::kMaximumGroundSlopeDegrees;
        result.inputClamped = true;
    }
    return std::cos(Math::DegToRad(degrees));
}

void RegisterGroundContact(CharacterControllerMoveResult& result,
                              const Math::Vector3& controllerToTargetNormal,
                              const float minimumGroundNormalY) noexcept {
    const float lengthSquared = Math::LengthSquared(controllerToTargetNormal);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return;
    }
    const Math::Vector3 supportNormal =
        -controllerToTargetNormal * (1.0F / std::sqrt(lengthSquared));
    if (!std::isfinite(supportNormal.y) || supportNormal.y < minimumGroundNormalY) {
        return;
    }
    if (!result.grounded || supportNormal.y > result.groundNormal.y) {
        result.grounded = true;
        result.groundNormal = supportNormal;
    }
}

struct DynamicBodyPushPolicy final {
    bool enabled = false;
    float strength = 0.0F;
    float maximumSpeed = 0.0F;
    float deltaTimeSeconds = 0.0F;
};

[[nodiscard]] DynamicBodyPushPolicy ResolveDynamicBodyPushPolicy(
    const CharacterControllerInput& input, CharacterControllerMoveResult& result) noexcept {
    DynamicBodyPushPolicy policy;
    if (!input.pushDynamicBodies) {
        return policy;
    }
    policy.enabled = true;
    policy.strength = input.dynamicBodyPushStrength;
    if (!std::isfinite(policy.strength) || policy.strength < 0.0F) {
        policy.strength = 0.0F;
        result.dynamicBodyPushClamped = true;
        result.inputClamped = true;
    }
    policy.maximumSpeed = input.maximumDynamicBodyPushSpeed;
    if (!std::isfinite(policy.maximumSpeed) || policy.maximumSpeed <= 0.0F) {
        policy.maximumSpeed = 5.0F;
        result.dynamicBodyPushClamped = true;
        result.inputClamped = true;
    }
    policy.deltaTimeSeconds = input.dynamicBodyPushDeltaTimeSeconds;
    if (!std::isfinite(policy.deltaTimeSeconds) || policy.deltaTimeSeconds <= 0.0F) {
        policy.deltaTimeSeconds = 1.0F / 60.0F;
        result.dynamicBodyPushClamped = true;
        result.inputClamped = true;
    }
    return policy;
}

[[nodiscard]] bool ApplyDynamicBodyPush(
    Scene::IEntityManager& entityManager, Scene::Entity targetEntity,
    const Math::Vector3& contactNormal, const Math::Vector3& controllerDisplacement,
    const DynamicBodyPushPolicy& policy) {
    if (!policy.enabled || policy.strength <= 0.0F || !std::isfinite(policy.maximumSpeed) ||
        policy.maximumSpeed <= 0.0F || !std::isfinite(policy.deltaTimeSeconds) ||
        policy.deltaTimeSeconds <= 0.0F || !entityManager.IsAlive(targetEntity) ||
        !entityManager.HasComponent<Scene::Rigid3DComponent>(targetEntity)) {
        return false;
    }
    const float normalLengthSquared = Math::LengthSquared(contactNormal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F ||
        !Math::IsFinite(controllerDisplacement)) {
        return false;
    }
    const Math::Vector3 normal = contactNormal * (1.0F / std::sqrt(normalLengthSquared));
    const float intoTarget = Math::Dot(controllerDisplacement, normal);
    if (!std::isfinite(intoTarget) || intoTarget <= 0.0F) {
        return false;
    }

    Scene::Rigid3DComponent& rigidBody =
        entityManager.GetComponent<Scene::Rigid3DComponent>(targetEntity);
    if (!Scene::IsRigid3DComponentValid(rigidBody) || rigidBody.isKinematic ||
        rigidBody.mass <= 0.0F) {
        return false;
    }
    const float desiredNormalSpeed = std::min(
        policy.maximumSpeed, intoTarget * policy.strength / policy.deltaTimeSeconds);
    if (!std::isfinite(desiredNormalSpeed) || desiredNormalSpeed <= 0.0F) {
        return false;
    }
    const float currentNormalSpeed = Math::Dot(rigidBody.velocity, normal);
    if (!std::isfinite(currentNormalSpeed)) {
        return false;
    }
    const float speedDelta = desiredNormalSpeed - currentNormalSpeed;
    if (!std::isfinite(speedDelta) || speedDelta <= 0.0F) {
        return false;
    }
    const Math::Vector3 proposedVelocity = rigidBody.velocity + normal * speedDelta;
    if (!Math::IsFinite(proposedVelocity)) {
        return false;
    }
    rigidBody.velocity = proposedVelocity;
    return true;
}

struct ToICandidate final {
    Scene::Entity entity;
    Math::SweptAabbHit hit;
};

[[nodiscard]] bool CollisionFilterAccepts(const Scene::ColliderComponent& moving,
                                              std::uint32_t targetLayer,
                                              std::uint32_t targetMask) noexcept {
    return (moving.collisionMask & targetLayer) != 0U &&
           (targetMask & moving.collisionLayer) != 0U;
}

[[nodiscard]] std::optional<ToICandidate> FindEarliestToI(
    const Math::Aabb& movingAabb, const Math::Vector3& remaining,
    const Scene::ColliderComponent& movingCollider,
    const std::vector<Detail::ColliderWorldAabb>& cache,
    Scene::Entity movingEntity, std::size_t maximumTargets, bool& truncated) {
    truncated = false;
    std::size_t evaluatedCount = 0U;
    std::optional<ToICandidate> best;
    for (const Detail::ColliderWorldAabb& candidate : cache) {
        if (candidate.entity == movingEntity ||
            !CollisionFilterAccepts(movingCollider, candidate.collisionLayer, candidate.collisionMask)) {
            continue;
        }
        const std::optional<Math::SweptAabbHit> hit =
            Math::SweepAabb(movingAabb, remaining, candidate.worldAabb);
        if (!hit.has_value()) {
            continue;
        }
        if (evaluatedCount >= maximumTargets) {
            truncated = true;
            break;
        }
        ++evaluatedCount;
        const bool earlier = !best.has_value() || hit->time < best->hit.time - CharacterController::kToIEpsilon;
        const bool tied = best.has_value() &&
                          std::fabs(hit->time - best->hit.time) <= CharacterController::kToIEpsilon;
        if (earlier || (tied && candidate.entity.index < best->entity.index)) {
            best = ToICandidate{candidate.entity, *hit};
        }
    }
    return best;
}

[[nodiscard]] bool HasControllerCollision(Scene::IEntityManager& entityManager,
                                              ICollisionSystem& collisionSystem,
                                              Scene::Entity controller) {
    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    return std::any_of(pairs.begin(), pairs.end(), [&](const CollisionPair& pair) {
        return pair.first == controller || pair.second == controller;
    });
}

[[nodiscard]] bool IsCollisionFreeAfterDelta(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    ICollisionSystem& collisionSystem, Scene::Entity controller,
    const Math::Vector3& delta) {
    if (!ApplyLocalDelta(entityManager, sceneGraph, controller, delta)) {
        return false;
    }
    sceneGraph.Update(entityManager);
    const bool collisionFree = !HasControllerCollision(entityManager, collisionSystem, controller);
    if (!ApplyLocalDelta(entityManager, sceneGraph, controller, -delta)) {
        return false;
    }
    sceneGraph.Update(entityManager);
    return collisionFree;
}

[[nodiscard]] bool TryStepUp(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    ICollisionSystem& collisionSystem, Scene::Entity controller,
    const Math::Vector3& horizontalDisplacement, float maximumStepHeight,
    bool& outContactsTruncated, Math::Vector3& outNetDisplacement) {
    const Math::Vector3 lift{0.0F, maximumStepHeight, 0.0F};
    if (!ApplyLocalDelta(entityManager, sceneGraph, controller, lift)) {
        return false;
    }
    sceneGraph.Update(entityManager);
    if (HasControllerCollision(entityManager, collisionSystem, controller)) {
        if (!ApplyLocalDelta(entityManager, sceneGraph, controller, -lift)) {
            return false;
        }
        sceneGraph.Update(entityManager);
        return false;
    }

    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(controller);
    // The sweeper's own shape is read at the scale the mesh is drawn at, exactly like the
    // broadphase reads everyone else's: a controller that sweeps at unit size through a world that
    // collides at full size is a body that walks into a floor it should be standing on.
    const Scene::ColliderComponent movingCollider = Scene::ScaleCollider(
        entityManager.GetComponent<Scene::ColliderComponent>(controller),
        worldTransform.worldScale);
    const Math::Aabb movingAabb = Math::Aabb::FromCenterExtents(
        worldTransform.worldPosition, Scene::GetColliderLocalHalfExtents(movingCollider));
    const std::vector<Detail::ColliderWorldAabb> cache =
        Detail::BuildColliderWorldAabbCache(entityManager);
    bool targetCacheTruncated = false;
    const std::optional<ToICandidate> horizontalImpact = FindEarliestToI(
        movingAabb, horizontalDisplacement, movingCollider, cache, controller,
        CharacterController::kMaximumToITargets, targetCacheTruncated);
    outContactsTruncated = outContactsTruncated || targetCacheTruncated;
    if (horizontalImpact.has_value()) {
        if (!ApplyLocalDelta(entityManager, sceneGraph, controller, -lift)) {
            return false;
        }
        sceneGraph.Update(entityManager);
        return false;
    }

    if (!ApplyLocalDelta(entityManager, sceneGraph, controller, horizontalDisplacement)) {
        return false;
    }
    sceneGraph.Update(entityManager);

    const Math::Vector3 fullDrop{0.0F, -maximumStepHeight, 0.0F};
    float dropDistance = maximumStepHeight;
    if (!IsCollisionFreeAfterDelta(entityManager, sceneGraph, collisionSystem, controller, fullDrop)) {
        float freeDistance = 0.0F;
        float blockedDistance = maximumStepHeight;
        for (std::size_t iteration = 0U; iteration < CharacterController::kMaximumStepSearchIterations;
             ++iteration) {
            const float candidateDistance = (freeDistance + blockedDistance) * 0.5F;
            const Math::Vector3 candidateDrop{0.0F, -candidateDistance, 0.0F};
            if (IsCollisionFreeAfterDelta(entityManager, sceneGraph, collisionSystem, controller, candidateDrop)) {
                freeDistance = candidateDistance;
            } else {
                blockedDistance = candidateDistance;
            }
        }
        if (freeDistance <= CharacterController::kMinimumMovementDistance) {
            if (!ApplyLocalDelta(entityManager, sceneGraph, controller, -horizontalDisplacement) ||
                !ApplyLocalDelta(entityManager, sceneGraph, controller, -lift)) {
                return false;
            }
            sceneGraph.Update(entityManager);
            return false;
        }
        dropDistance = freeDistance;
    }

    const Math::Vector3 actualDrop{0.0F, -dropDistance, 0.0F};
    if (!ApplyLocalDelta(entityManager, sceneGraph, controller, actualDrop)) {
        return false;
    }
    sceneGraph.Update(entityManager);
    outNetDisplacement = lift + horizontalDisplacement + actualDrop;
    return true;
}

} // namespace

CcdEligibilityStatus EvaluateCcdEligibility(const Math::Vector3 displacement,
                                                   const float minimumSweepDistance) noexcept {
    if (!Math::IsFinite(displacement) || !HasFiniteLength(displacement) ||
        !std::isfinite(minimumSweepDistance) || minimumSweepDistance <= 0.0F) {
        return CcdEligibilityStatus::Invalid;
    }
    const float displacementLength = FiniteLength(displacement);
    if (!std::isfinite(displacementLength)) {
        return CcdEligibilityStatus::Invalid;
    }
    return displacementLength >= minimumSweepDistance ? CcdEligibilityStatus::Enabled
                                                       : CcdEligibilityStatus::Disabled;
}

CharacterControllerMoveResult CharacterController::Move(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    ICollisionSystem& collisionSystem, const CharacterControllerInput& input) {
    CharacterControllerMoveResult result;
    result.requestedDisplacement = input.desiredDisplacement;
    result.remainingDisplacement = input.desiredDisplacement;

    if (!ValidateControllerInput(entityManager, input, result)) {
        return result;
    }

    std::size_t maximumSubsteps = std::min(input.maximumSubsteps, kMaximumSubsteps);
    if (input.maximumSubsteps > kMaximumSubsteps) {
        result.inputClamped = true;
    }
    if (maximumSubsteps == 0U) {
        maximumSubsteps = 1U;
        result.inputClamped = true;
    }

    float maximumSubstepDistance = input.maximumSubstepDistance;
    if (!std::isfinite(maximumSubstepDistance) || maximumSubstepDistance <= 0.0F) {
        maximumSubstepDistance = kDefaultMaximumSubstepDistance;
        result.inputClamped = true;
    }
    const float minimumGroundNormalY = ResolveGroundSlopeCosine(input, result);
    const DynamicBodyPushPolicy pushPolicy = ResolveDynamicBodyPushPolicy(input, result);

    sceneGraph.Update(entityManager);
    while (result.substeps < maximumSubsteps &&
           FiniteLength(result.remainingDisplacement) > kMinimumMovementDistance) {
        const float remainingLength = FiniteLength(result.remainingDisplacement);
        const float stepLength = std::min(remainingLength, maximumSubstepDistance);
        const Math::Vector3 step = result.remainingDisplacement * (stepLength / remainingLength);
        if (!ApplyLocalDelta(entityManager, sceneGraph, input.entity, step)) {
            result.code = CharacterControllerMoveCode::InvalidInput;
            return result;
        }
        result.remainingDisplacement -= step;
        result.appliedDisplacement += step;
        if (!Math::IsFinite(result.remainingDisplacement) ||
            !Math::IsFinite(result.appliedDisplacement)) {
            result.code = CharacterControllerMoveCode::InvalidInput;
            return result;
        }
        ++result.substeps;
        sceneGraph.Update(entityManager);

        const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
        std::size_t entityPairCount = 0U;
        for (const CollisionPair& pair : pairs) {
            if (pair.first != input.entity && pair.second != input.entity) {
                continue;
            }
            if (entityPairCount >= kMaximumContacts) {
                result.contactsTruncated = true;
                break;
            }
            ++entityPairCount;
            const float separationAxisLengthSquared = Math::LengthSquared(pair.separationAxis);
            if (!std::isfinite(pair.penetrationDepth) || pair.penetrationDepth <= 0.0F ||
                !Math::IsFinite(pair.separationAxis) || !std::isfinite(separationAxisLengthSquared) ||
                std::abs(separationAxisLengthSquared - 1.0F) > 1.0e-3F) {
                continue;
            }
            const bool controllerIsFirst = pair.first == input.entity;
            const Scene::Entity targetEntity = controllerIsFirst ? pair.second : pair.first;
            const Math::Vector3 contactNormal = controllerIsFirst
                ? pair.separationAxis : -pair.separationAxis;
            const Math::Vector3 correction = controllerIsFirst
                ? -pair.separationAxis * pair.penetrationDepth
                : pair.separationAxis * pair.penetrationDepth;
            if (!Math::IsFinite(correction)) {
                continue;
            }
            RegisterGroundContact(result, contactNormal, minimumGroundNormalY);
            if (!ApplyLocalDelta(entityManager, sceneGraph, input.entity, correction)) {
                result.code = CharacterControllerMoveCode::InvalidInput;
                return result;
            }
            result.appliedDisplacement += correction;
            result.remainingDisplacement = RemoveIntoNormalComponent(
                result.remainingDisplacement, contactNormal);
            if (!Math::IsFinite(result.appliedDisplacement) ||
                !Math::IsFinite(result.remainingDisplacement)) {
                result.code = CharacterControllerMoveCode::InvalidInput;
                return result;
            }
            if (ApplyDynamicBodyPush(entityManager, targetEntity, contactNormal, step, pushPolicy)) {
                ++result.pushedBodyCount;
            }
            result.blocked = true;
            ++result.contactCount;
        }
        if (entityPairCount > 0U) {
            sceneGraph.Update(entityManager);
        }
    }

    result.code = CharacterControllerMoveCode::Moved;
    return result;
}

CharacterControllerMoveResult CharacterController::MoveWithToI(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    ICollisionSystem& collisionSystem, const CharacterControllerInput& input) {
    CharacterControllerMoveResult result;
    result.requestedDisplacement = input.desiredDisplacement;
    result.remainingDisplacement = input.desiredDisplacement;
    if (!ValidateControllerInput(entityManager, input, result)) {
        return result;
    }

    std::size_t maximumIterations = std::min(input.maximumSubsteps, kMaximumToIIterations);
    if (input.maximumSubsteps > kMaximumToIIterations) {
        result.inputClamped = true;
    }
    if (maximumIterations == 0U) {
        maximumIterations = 1U;
        result.inputClamped = true;
    }
    float maximumStepDistance = input.maximumSubstepDistance;
    if (!std::isfinite(maximumStepDistance) || maximumStepDistance <= 0.0F) {
        maximumStepDistance = kDefaultMaximumSubstepDistance;
        result.inputClamped = true;
    }
    const float minimumGroundNormalY = ResolveGroundSlopeCosine(input, result);
    const DynamicBodyPushPolicy pushPolicy = ResolveDynamicBodyPushPolicy(input, result);
    float maximumStepHeight = input.maximumStepHeight;
    if (!std::isfinite(maximumStepHeight) || maximumStepHeight < 0.0F) {
        maximumStepHeight = 0.0F;
        result.inputClamped = true;
    }

    sceneGraph.Update(entityManager);
    const std::vector<CollisionPair> initialPairs = collisionSystem.DetectCollisions(entityManager);
    if (std::any_of(initialPairs.begin(), initialPairs.end(), [&](const CollisionPair& pair) {
            return pair.first == input.entity || pair.second == input.entity;
        })) {
        return Move(entityManager, sceneGraph, collisionSystem, input);
    }

    while (result.substeps < maximumIterations &&
           FiniteLength(result.remainingDisplacement) > kMinimumMovementDistance) {
        const float remainingLength = FiniteLength(result.remainingDisplacement);
        const float consideredLength = std::min(remainingLength, maximumStepDistance);
        const Math::Vector3 consideredDisplacement =
            result.remainingDisplacement * (consideredLength / remainingLength);
        const Scene::WorldTransformComponent& worldTransform =
            entityManager.GetComponent<Scene::WorldTransformComponent>(input.entity);
        const Scene::ColliderComponent movingCollider = Scene::ScaleCollider(
            entityManager.GetComponent<Scene::ColliderComponent>(input.entity),
            worldTransform.worldScale);
        const Math::Aabb movingAabb = Math::Aabb::FromCenterExtents(
            worldTransform.worldPosition, Scene::GetColliderLocalHalfExtents(movingCollider));
        const std::vector<Detail::ColliderWorldAabb> cache =
            Detail::BuildColliderWorldAabbCache(entityManager);
        bool targetCacheTruncated = false;
        const std::optional<ToICandidate> candidate = FindEarliestToI(
            movingAabb, consideredDisplacement, movingCollider, cache, input.entity,
            kMaximumToITargets, targetCacheTruncated);
        result.contactsTruncated = result.contactsTruncated || targetCacheTruncated;

        if (!candidate.has_value()) {
            if (!ApplyLocalDelta(entityManager, sceneGraph, input.entity, consideredDisplacement)) {
                result.code = CharacterControllerMoveCode::InvalidInput;
                return result;
            }
            result.remainingDisplacement -= consideredDisplacement;
            result.appliedDisplacement += consideredDisplacement;
            if (!Math::IsFinite(result.remainingDisplacement) ||
                !Math::IsFinite(result.appliedDisplacement)) {
                result.code = CharacterControllerMoveCode::InvalidInput;
                return result;
            }
            ++result.substeps;
            sceneGraph.Update(entityManager);
            continue;
        }

        const float impactTime = std::clamp(candidate->hit.time, 0.0F, 1.0F);
        const Math::Vector3 horizontalDisplacement{
            consideredDisplacement.x, 0.0F, consideredDisplacement.z};
        if (maximumStepHeight > kMinimumMovementDistance &&
            std::fabs(candidate->hit.normal.y) < minimumGroundNormalY &&
            FiniteLength(horizontalDisplacement) > kMinimumMovementDistance) {
            Math::Vector3 stepDisplacement{};
            if (TryStepUp(entityManager, sceneGraph, collisionSystem, input.entity,
                             horizontalDisplacement, maximumStepHeight, result.contactsTruncated,
                             stepDisplacement)) {
                result.remainingDisplacement -= horizontalDisplacement;
                result.appliedDisplacement += stepDisplacement;
                result.blocked = true;
                result.toiUsed = true;
                result.stepUpUsed = true;
                result.earliestImpactTime = std::min(result.earliestImpactTime, impactTime);
                ++result.contactCount;
                ++result.substeps;
                continue;
            }
        }

        const float advanceTime = std::max(0.0F, impactTime - kToIEpsilon);
        const Math::Vector3 preImpactDisplacement = consideredDisplacement * advanceTime;
        if (!ApplyLocalDelta(entityManager, sceneGraph, input.entity, preImpactDisplacement)) {
            result.code = CharacterControllerMoveCode::InvalidInput;
            return result;
        }
        result.remainingDisplacement -= preImpactDisplacement;
        result.appliedDisplacement += preImpactDisplacement;
        if (!Math::IsFinite(result.remainingDisplacement) ||
            !Math::IsFinite(result.appliedDisplacement)) {
            result.code = CharacterControllerMoveCode::InvalidInput;
            return result;
        }
        ++result.substeps;
        sceneGraph.Update(entityManager);

        result.blocked = true;
        result.toiUsed = true;
        RegisterGroundContact(result, candidate->hit.normal, minimumGroundNormalY);
        if (ApplyDynamicBodyPush(entityManager, candidate->entity, candidate->hit.normal,
                                    consideredDisplacement, pushPolicy)) {
            ++result.pushedBodyCount;
        }
        result.earliestImpactTime = std::min(result.earliestImpactTime, impactTime);
        ++result.contactCount;
        result.remainingDisplacement = RemoveIntoNormalComponent(
            result.remainingDisplacement, candidate->hit.normal);
    }

    result.code = CharacterControllerMoveCode::Moved;
    return result;
}

} // namespace UVE::Physics
