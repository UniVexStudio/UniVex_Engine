// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/character_world_query_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/kinematic_3d_uve.h"
#include "uve/physics/i_collision_system_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Physics {
namespace {

/// Below this a motion has no direction to sweep along.
constexpr float kMinimumSweepLengthUVE = 1.0e-6F;

[[nodiscard]] bool IsUsableUVE(const Math::Vector3UVE& value) noexcept {
    return Math::IsFiniteUVE(value);
}

[[nodiscard]] bool IsUsableUveEntityTransformUVE(const Scene::WorldTransformComponentUVE& world) noexcept {
    return IsUsableUVE(world.worldPosition);
}

/// Axis-aligned, so the world half extents are the local ones. Spelled out rather than assumed
/// because a rotated character with a box collider is a real thing a game does, and the day that
/// rotation matters this is the one function that has to learn about it.
[[nodiscard]] Math::Vector3UVE WorldHalfExtentsUVE(const Scene::ColliderComponentUVE& collider) noexcept {
    const Math::Vector3UVE local = Scene::GetColliderLocalHalfExtentsUVE(collider);
    return Math::Vector3UVE{std::abs(local.x), std::abs(local.y), std::abs(local.z)};
}

} // namespace

CharacterWorldQueryUVE::CharacterWorldQueryUVE(Scene::IEntityManagerUVE& entityManager,
                                               ICollisionSystemUVE& collisionSystem,
                                               const Scene::EntityUVE entity)
    : m_entityManager(&entityManager), m_collisionSystem(&collisionSystem), m_entity(entity) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return;
    }
    if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity) &&
        !entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity).isKinematic) {
        return;
    }
    const Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    if (!IsUsableUveEntityTransformUVE(world)) {
        return;
    }
    const Scene::ColliderComponentUVE& collider =
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity);
    m_center = world.worldPosition;
    m_halfExtents = WorldHalfExtentsUVE(collider);
    m_layer = collider.collisionLayer;
    m_mask = collider.collisionMask;
    m_isMovable = IsUsableUVE(m_halfExtents);
}

void CharacterWorldQueryUVE::SetIgnoredEntityUVE(const Scene::EntityUVE entity) noexcept {
    m_ignoredEntity = entity;
}

Scene::EntityUVE CharacterWorldQueryUVE::GetEntityUVE() const noexcept {
    return m_entity;
}

bool CharacterWorldQueryUVE::IsMovableCharacterUVE() const noexcept {
    return m_isMovable;
}

Math::Vector3UVE CharacterWorldQueryUVE::GetCenterUVE() const {
    if (!m_isMovable) {
        // "I cannot describe this body" told as a position, because that is the only channel this
        // seam has: a mover asked to move a body through a world that cannot say where it is refuses
        // the move outright (InvalidWorld) instead of moving it from the origin.
        return Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::quiet_NaN()};
    }
    return m_center;
}

Math::Vector3UVE CharacterWorldQueryUVE::GetHalfExtentsUVE() const noexcept {
    return m_halfExtents;
}

bool CharacterWorldQueryUVE::IsCollidableUVE(const Detail::ColliderWorldAabbUVE& collider) const noexcept {
    if (collider.entity == m_entity || collider.entity == m_ignoredEntity) {
        return false;
    }
    // Both directions, exactly as the collision system filters pairs: the character has to accept
    // the target's layer *and* the target has to accept the character's.
    return (collider.collisionLayer & m_mask) != 0U && (collider.collisionMask & m_layer) != 0U;
}

std::vector<Scene::CharacterSlideCollisionUVE> CharacterWorldQueryUVE::GetOverlapsUVE() const {
    std::vector<Scene::CharacterSlideCollisionUVE> overlaps;
    if (!m_isMovable) {
        return overlaps;
    }
    const std::vector<CollisionPairUVE> pairs = m_collisionSystem->DetectCollisionsUVE(*m_entityManager);
    overlaps.reserve(pairs.size());
    for (const CollisionPairUVE& pair : pairs) {
        const bool characterIsFirst = pair.first == m_entity;
        if (!characterIsFirst && pair.second != m_entity) {
            continue;
        }
        if (pair.first == m_ignoredEntity || pair.second == m_ignoredEntity) {
            continue;
        }
        // A pair's axis points from the pair's first entity toward its second. The mover wants the
        // surface normal, which points from the surface back at the character - so the axis is
        // negated when the character is the one it points away from.
        const Math::Vector3UVE normal = characterIsFirst ? -pair.separationAxis : pair.separationAxis;
        if (!std::isfinite(pair.penetrationDepth) || pair.penetrationDepth <= 0.0F ||
            !IsUsableUVE(normal) || Math::LengthSquaredUVE(normal) <= 0.0F) {
            continue;
        }
        Scene::CharacterSlideCollisionUVE overlap;
        overlap.entity = characterIsFirst ? pair.second : pair.first;
        overlap.normal = normal;
        // The exact contact point of an AABB pair is on the face between them; the character's own
        // center is the honest stand-in that costs nothing to compute and cannot be wrong about
        // which side it is on.
        overlap.position = m_center;
        overlap.depth = pair.penetrationDepth;
        overlap.travel = 0.0F;
        overlaps.push_back(overlap);
    }
    return overlaps;
}

std::optional<Scene::CharacterSlideCollisionUVE> CharacterWorldQueryUVE::SweepUVE(
    const Math::Vector3UVE& center, const Math::Vector3UVE& motion, const float margin) const {
    if (!m_isMovable || !IsUsableUVE(center) || !IsUsableUVE(motion) || !std::isfinite(margin) ||
        margin < 0.0F) {
        return std::nullopt;
    }
    const float motionLength = std::sqrt(Math::LengthSquaredUVE(motion));
    if (!std::isfinite(motionLength) || motionLength <= kMinimumSweepLengthUVE) {
        return std::nullopt;
    }
    if (!m_colliders.has_value()) {
        m_colliders = Detail::BuildColliderWorldAabbCacheUVE(*m_entityManager);
    }

    // The moving body is swept as its world AABB: exact for a box character against an unrotated
    // box target, and conservative for everything else - it can stop early, never tunnel.
    const Math::AabbUVE movingAabb = Math::AabbUVE::FromCenterExtentsUVE(center, m_halfExtents);
    const float sweepDistance = std::max(0.0F, motionLength - margin);
    if (motionLength <= kMinimumSweepLengthUVE) {
        return std::nullopt;
    }

    std::optional<Scene::CharacterSlideCollisionUVE> closest;
    float closestDistance = 0.0F;
    for (const Detail::ColliderWorldAabbUVE& collider : *m_colliders) {
        if (!IsCollidableUVE(collider)) {
            continue;
        }
        // The moving body's own AABB against the target's AABB, which is the convention the
        // existing kinematic move already resolves against: the boxes meet exactly where the
        // bodies would, and a body resting against a surface is *touching*, not overlapping
        // (AabbUVE::IntersectsUVE is strict), so resting contact stays sweepable.
        const std::optional<Math::SweptAabbHitUVE> swept =
            Math::SweepAabbUVE(movingAabb, motion, collider.worldAabb);
        if (!swept.has_value()) {
            continue;
        }
        const float distance = swept->time * motionLength;
        if (distance > sweepDistance + margin) {
            // The hit is past the budget: the body stops at the budget, so this contact is not
            // this sweep's answer.
            continue;
        }
        const bool closer = !closest.has_value() || distance < closestDistance;
        const bool tied =
            closest.has_value() && distance == closestDistance && collider.entity.index < closest->entity.index;
        if (!closer && !tied) {
            continue;
        }
        Scene::CharacterSlideCollisionUVE hit;
        hit.entity = collider.entity;
        // Math::SweepAabbUVE reports the direction the body was moving when it met the face - the
        // component to remove from the remaining motion. The mover wants the surface normal, which
        // points back at the body, so the two are exact opposites and the sign lives here alone.
        hit.normal = -swept->normal;
        hit.position = center + motion * swept->time;
        // The skin: motion stops this far short of the surface, which is what keeps the body's
        // next query well-posed instead of exactly touching.
        hit.travel = std::max(0.0F, distance - margin);
        hit.depth = 0.0F;
        closest = hit;
        closestDistance = distance;
    }
    return closest;
}

Math::Vector3UVE CharacterWorldQueryUVE::GetSurfaceVelocityUVE(const Scene::EntityUVE entity) const {
    if (!m_entityManager->IsAliveUVE(entity) || entity == m_entity) {
        return Math::Vector3UVE{};
    }
    // Whatever is moving underfoot: a rigid body's own velocity, or a kinematic body's target -
    // the two ways a body can be moving without the character being told about it.
    if (m_entityManager->HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
        const Math::Vector3UVE velocity =
            m_entityManager->GetComponentUVE<Scene::Rigid3DComponentUVE>(entity).velocity;
        if (IsUsableUVE(velocity)) {
            return velocity;
        }
    }
    if (m_entityManager->HasComponentUVE<Scene::Kinematic3DComponentUVE>(entity)) {
        const Scene::Kinematic3DComponentUVE& kinematic =
            m_entityManager->GetComponentUVE<Scene::Kinematic3DComponentUVE>(entity);
        if (kinematic.active && IsUsableUVE(kinematic.targetVelocity)) {
            return kinematic.targetVelocity;
        }
    }
    return Math::Vector3UVE{};
}

std::optional<Math::Vector3UVE> CharacterWorldQueryUVE::TryGetEntityCenterUVE(
    const Scene::EntityUVE entity) const {
    if (!m_entityManager->IsAliveUVE(entity) ||
        !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3UVE position =
        m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition;
    if (!IsUsableUVE(position)) {
        return std::nullopt;
    }
    return position;
}

// =================================================================================================
// One step of one character.
// =================================================================================================

Character3DStepResultUVE StepCharacter3DUVE(Scene::IEntityManagerUVE& entityManager,
                                            Scene::ISceneGraphUVE& sceneGraph,
                                            ICollisionSystemUVE& collisionSystem,
                                            const Scene::EntityUVE entity,
                                            const CharacterMotionInputUVE& input, const float gravityY,
                                            const float deltaTimeSeconds) {
    Character3DStepResultUVE report;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F || !std::isfinite(gravityY)) {
        report.code = Character3DStepCodeUVE::InvalidDeltaTime;
        return report;
    }
    if (!entityManager.IsAliveUVE(entity)) {
        report.code = Character3DStepCodeUVE::UnknownEntity;
        return report;
    }
    if (!entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity)) {
        report.code = Character3DStepCodeUVE::NotAController;
        return report;
    }
    if (!entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        report.code = Character3DStepCodeUVE::MissingCollider;
        return report;
    }
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        report.code = Character3DStepCodeUVE::MissingTransform;
        return report;
    }
    if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity) &&
        !entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity).isKinematic) {
        report.code = Character3DStepCodeUVE::NonKinematicBody;
        return report;
    }

    // The world this step moves through is the world as of now: the mover reads transforms, so the
    // graph has to have written them before the first query.
    sceneGraph.UpdateUVE(entityManager);

    // Copied, worked on, and written back: the move can add components elsewhere (a push, a
    // callback), which can move this one in storage.
    Scene::CharacterControllerComponentUVE controller =
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
    const bool wasOnFloor = controller.grounded;

    report.jumped = StepCharacterIntentUVE(controller, input, gravityY, deltaTimeSeconds);

    // SolidBody3D's motion locks: along a locked axis the body neither moves nor keeps speed.
    if (entityManager.HasComponentUVE<Scene::SolidBodyComponentUVE>(entity)) {
        const Scene::SolidBodyComponentUVE& locks =
            entityManager.GetComponentUVE<Scene::SolidBodyComponentUVE>(entity);
        controller.velocity.x = locks.lockMotionX ? 0.0F : controller.velocity.x;
        controller.velocity.y = locks.lockMotionY ? 0.0F : controller.velocity.y;
        controller.velocity.z = locks.lockMotionZ ? 0.0F : controller.velocity.z;
    }

    CharacterWorldQueryUVE world(entityManager, collisionSystem, entity);
    if (!world.IsMovableCharacterUVE()) {
        report.code = Character3DStepCodeUVE::InvalidWorld;
        return report;
    }

    Scene::CharacterMotionStateUVE state = Scene::MakeCharacterMotionStateUVE(controller);
    // The intent step decides a jump by clearing `grounded`; the mover must still know it was
    // standing when the step began, or it would refuse a step-up and the platform that was left.
    state.grounded = wasOnFloor;

    Scene::CharacterMotionConfigUVE config = Scene::MakeCharacterMotionConfigUVE(controller);
    config.jumpedThisStep = report.jumped;

    const Math::Vector3UVE desiredMotion = controller.velocity * deltaTimeSeconds;
    report.motion = Scene::Character3DUVE::MoveAndSlideUVE(world, config, state, desiredMotion, deltaTimeSeconds);
    report.stepped = true;

    if (report.motion.code == Scene::CharacterMoveCodeUVE::Moved) {
        // Only a finished move is written to the world. A refused move (invalid state or world)
        // leaves the body exactly where it was, so a caller can retry or teleport it deliberately
        // instead of finding it half-moved.
        const Math::Vector3UVE applied = report.motion.appliedMotion;
        if (IsUsableUVE(applied) && applied != Math::Vector3UVE{}) {
            Scene::TransformComponentUVE transform =
                entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
            transform.localPosition += applied;
            // Through the graph, not around it: the world transform has to be marked dirty and
            // recomputed, or the next step's queries would be answered about where the body used
            // to be. This is the one call that makes the move visible to the rest of the frame.
            sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
            sceneGraph.UpdateUVE(entityManager);
        }
        Scene::StoreCharacterMotionStateUVE(controller, state);
        FinishCharacterStepUVE(controller,
                               Physics::CharacterMoveOutcomeUVE{report.motion.onFloor, report.motion.floorNormal,
                                                                report.motion.onCeiling},
                               report.jumped, deltaTimeSeconds);
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity) = controller;
        report.code = report.motion.blocked ? Character3DStepCodeUVE::Blocked : Character3DStepCodeUVE::Stepped;
        return report;
    }

    // The mover refused: the component keeps its authored values and untouched velocity, but the
    // intent step already advanced the coyote and buffer clocks, so those are written back.
    entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity) = controller;
    report.code = Character3DStepCodeUVE::InvalidWorld;
    return report;
}

namespace {

/// The speed a push falls back to when the authored one is missing or meaningless. Spelling the
/// fallback out here rather than trusting the caller keeps a script from turning a shove into a
/// launch by writing zero.
constexpr float kDefaultDynamicPushSpeedUVE = 5.0F;

} // namespace

std::size_t PushBodiesFromCharacterMoveUVE(
    Scene::IEntityManagerUVE& entityManager,
    const std::vector<Scene::CharacterSlideCollisionUVE>& contacts,
    const CharacterDynamicPushPolicyUVE& policy) {
    if (!policy.enabled || !std::isfinite(policy.strength) || policy.strength <= 0.0F ||
        !std::isfinite(policy.deltaTimeSeconds) || policy.deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    const float maximumSpeed =
        std::isfinite(policy.maximumSpeed) && policy.maximumSpeed > 0.0F
            ? policy.maximumSpeed
            : kDefaultDynamicPushSpeedUVE;

    std::size_t pushedCount = 0U;
    std::vector<Scene::EntityUVE> alreadyPushed;
    alreadyPushed.reserve(contacts.size());
    for (const Scene::CharacterSlideCollisionUVE& contact : contacts) {
        const float normalLengthSquared = Math::LengthSquaredUVE(contact.normal);
        if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F) {
            continue;
        }
        // The mover's contact normal points from the surface back at the character, so the push
        // goes the other way: into the surface, which is away from the character and into the body
        // that was in its path.
        const Math::Vector3UVE pushDirection =
            contact.normal * (-1.0F / std::sqrt(normalLengthSquared));
        // The part of the motion the surface removed: what the character was actually pressing
        // into the body with. A contact the character merely brushed past pushes nothing.
        const float intoTarget = Math::DotUVE(contact.motion, pushDirection);
        if (!std::isfinite(intoTarget) || intoTarget <= 0.0F || !Math::IsFiniteUVE(contact.motion)) {
            continue;
        }
        if (std::find(alreadyPushed.begin(), alreadyPushed.end(), contact.entity) != alreadyPushed.end()) {
            continue;
        }
        if (!entityManager.IsAliveUVE(contact.entity) ||
            !entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(contact.entity)) {
            continue;
        }
        // A body the simulation is not moving - a PhysicsObject3D kept as a static obstacle, or
        // one taken out of the world - is not something a walk into it can push, however it came
        // to be in the contact list.
        if (!Scene::IsPhysicsObjectSimulatedUVE(entityManager, contact.entity)) {
            continue;
        }
        Scene::Rigid3DComponentUVE& rigidBody =
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(contact.entity);
        if (!Scene::IsRigid3DComponentValidUVE(rigidBody) || rigidBody.isKinematic ||
            rigidBody.mass <= 0.0F) {
            continue;
        }
        const float desiredNormalSpeed =
            std::min(maximumSpeed, intoTarget * policy.strength / policy.deltaTimeSeconds);
        if (!std::isfinite(desiredNormalSpeed) || desiredNormalSpeed <= 0.0F) {
            continue;
        }
        const float currentNormalSpeed = Math::DotUVE(rigidBody.velocity, pushDirection);
        if (!std::isfinite(currentNormalSpeed)) {
            continue;
        }
        const float speedDelta = desiredNormalSpeed - currentNormalSpeed;
        if (!std::isfinite(speedDelta) || speedDelta <= 0.0F) {
            continue;
        }
        const Math::Vector3UVE proposed = rigidBody.velocity + pushDirection * speedDelta;
        if (!Math::IsFiniteUVE(proposed)) {
            continue;
        }
        rigidBody.velocity = proposed;
        alreadyPushed.push_back(contact.entity);
        ++pushedCount;
    }
    return pushedCount;
}

} // namespace UVE::Physics
