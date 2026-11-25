// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_world_query.h"
#include "character_3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "character_controller_component.h"
#include "collider_component.h"
#include "predicted_character_component.h"
#include "rigid_3d_component.h"
#include "solid_body_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "aabb.h"
#include "abstract_physics_objects_3d.h"
#include "kinematic_3d.h"
#include "i_collision_system.h"
#include "i_scene_graph.h"

namespace UVE::Physics {
namespace {

/// Below this a motion has no direction to sweep along.
constexpr float kMinimumSweepLength = 1.0e-6F;

[[nodiscard]] bool IsUsable(const Math::Vector3& value) noexcept {
    return Math::IsFinite(value);
}

[[nodiscard]] bool IsUsableUveEntityTransform(const Scene::WorldTransformComponent& world) noexcept {
    return IsUsable(world.worldPosition);
}

/// Axis-aligned, so the world half extents are the local ones. Spelled out rather than assumed
/// because a rotated character with a box collider is a real thing a game does, and the day that
/// rotation matters this is the one function that has to learn about it.
[[nodiscard]] Math::Vector3 WorldHalfExtents(const Scene::ColliderComponent& collider) noexcept {
    const Math::Vector3 local = Scene::GetColliderLocalHalfExtents(collider);
    return Math::Vector3{std::abs(local.x), std::abs(local.y), std::abs(local.z)};
}

} // namespace

CharacterWorldQuery::CharacterWorldQuery(Scene::IEntityManager& entityManager,
                                               ICollisionSystem& collisionSystem,
                                               const Scene::Entity entity,
                                               const Scene::Entity shapeEntity)
    : m_entityManager(&entityManager), m_collisionSystem(&collisionSystem), m_entity(entity) {
    // The body is the entity that moves; the shape is the entity that carries the collider. They
    // are the same object unless the author gave the character its own Collider3D, in which case
    // the shape sits under the body in the tree.
    const Scene::Entity shape =
        shapeEntity != Scene::kInvalidEntity ? shapeEntity : entity;
    if (shape != entity) {
        // A body must never collide with the shape it is built from: its own child would stop it
        // dead on the first sweep, which reads as a character frozen in place the moment a
        // collider is added.
        m_ignoredEntity = shape;
    }
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::CharacterControllerComponent>(entity) ||
        !entityManager.HasComponent<Scene::ColliderComponent>(shape) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return;
    }
    if (entityManager.HasComponent<Scene::Rigid3DComponent>(entity) &&
        !entityManager.GetComponent<Scene::Rigid3DComponent>(entity).isKinematic) {
        return;
    }
    const Scene::WorldTransformComponent& world =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    if (!IsUsableUveEntityTransform(world)) {
        return;
    }
    // The shape's numbers AND the shape's place. Everything about the body - size, layer and mask -
    // is the collider's, and so is where it is: a Collider3D authored under a character sits
    // wherever the author put it, and reading its centre off the body instead drew the shape in one
    // place and collided in another. That is a capsule hovering over an invisible box, and it is
    // what an author sees when they move the shape to where the body should stand.
    const Scene::ColliderComponent& collider =
        entityManager.GetComponent<Scene::ColliderComponent>(shape);
    // The shape carries the body's size at whatever scale its branch of the tree is drawn at; a
    // child already has its parents' scale folded into its world transform, so this is the whole
    // chain and not just the Collider3D's own local scale.
    Math::Vector3 shapeScale{1.0F, 1.0F, 1.0F};
    m_center = world.worldPosition;
    if (entityManager.HasComponent<Scene::WorldTransformComponent>(shape)) {
        const Scene::WorldTransformComponent& shapeWorld =
            entityManager.GetComponent<Scene::WorldTransformComponent>(shape);
        shapeScale = shapeWorld.worldScale;
        if (IsUsableUveEntityTransform(shapeWorld)) {
            m_center = shapeWorld.worldPosition;
        }
    }
    m_halfExtents = WorldHalfExtents(Scene::ScaleCollider(collider, shapeScale));
    m_layer = collider.collisionLayer;
    m_mask = collider.collisionMask;
    m_isMovable = IsUsable(m_halfExtents);
}

void CharacterWorldQuery::SetIgnoredEntity(const Scene::Entity entity) noexcept {
    m_ignoredEntity = entity;
}

Scene::Entity CharacterWorldQuery::GetEntity() const noexcept {
    return m_entity;
}

bool CharacterWorldQuery::IsMovableCharacter() const noexcept {
    return m_isMovable;
}

Math::Vector3 CharacterWorldQuery::GetCenter() const {
    if (!m_isMovable) {
        // "I cannot describe this body" told as a position, because that is the only channel this
        // seam has: a mover asked to move a body through a world that cannot say where it is refuses
        // the move outright (InvalidWorld) instead of moving it from the origin.
        return Math::Vector3{std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::quiet_NaN()};
    }
    return m_center;
}

Math::Vector3 CharacterWorldQuery::GetHalfExtents() const noexcept {
    return m_halfExtents;
}

bool CharacterWorldQuery::IsCollidable(const Detail::ColliderWorldAabb& collider) const noexcept {
    if (collider.entity == m_entity || collider.entity == m_ignoredEntity) {
        return false;
    }
    // Both directions, exactly as the collision system filters pairs: the character has to accept
    // the target's layer *and* the target has to accept the character's.
    return (collider.collisionLayer & m_mask) != 0U && (collider.collisionMask & m_layer) != 0U;
}

std::vector<Scene::CharacterSlideCollision> CharacterWorldQuery::GetOverlapsAt(
    const Math::Vector3& center) const {
    std::vector<Scene::CharacterSlideCollision> overlaps;
    if (!m_isMovable || m_collisionSystem == nullptr || !IsUsable(center)) {
        return overlaps;
    }
    if (!m_colliders.has_value()) {
        m_colliders = Detail::BuildColliderWorldAabbCache(*m_entityManager);
    }
    // Same shape the sweep uses: a recovery and a sweep cannot disagree about whether this pose
    // is free. The normal points from the surface back at the body, which is -axis of the MTV
    // (ComputePenetration's axis points from the moving box toward the target).
    const Math::Aabb body = Math::Aabb::FromCenterExtents(center, m_halfExtents);
    for (const Detail::ColliderWorldAabb& collider : *m_colliders) {
        if (!IsCollidable(collider)) {
            continue;
        }
        const std::optional<Math::Penetration> penetration =
            Math::ComputePenetration(body, collider.worldAabb);
        if (!penetration.has_value() || !std::isfinite(penetration->depth) ||
            penetration->depth <= 0.0F || !IsUsable(penetration->axis) ||
            Math::LengthSquared(penetration->axis) <= 0.0F) {
            continue;
        }
        Scene::CharacterSlideCollision overlap;
        overlap.entity = collider.entity;
        overlap.normal = penetration->axis * -1.0F;
        overlap.position = center;
        overlap.depth = penetration->depth;
        overlap.travel = 0.0F;
        overlaps.push_back(overlap);
    }
    return overlaps;
}

std::optional<Scene::CharacterSlideCollision> CharacterWorldQuery::Sweep(
    const Math::Vector3& center, const Math::Vector3& motion, const float margin) const {
    if (!m_isMovable || !IsUsable(center) || !IsUsable(motion) || !std::isfinite(margin) ||
        margin < 0.0F) {
        return std::nullopt;
    }
    const float motionLength = std::sqrt(Math::LengthSquared(motion));
    if (!std::isfinite(motionLength) || motionLength <= kMinimumSweepLength) {
        return std::nullopt;
    }
    if (!m_colliders.has_value()) {
        m_colliders = Detail::BuildColliderWorldAabbCache(*m_entityManager);
    }

    // The moving body is swept as its world AABB: exact for a box character against an unrotated
    // box target, and conservative for everything else - it can stop early, never tunnel.
    const Math::Aabb movingAabb = Math::Aabb::FromCenterExtents(center, m_halfExtents);
    const float sweepDistance = std::max(0.0F, motionLength - margin);
    if (motionLength <= kMinimumSweepLength) {
        return std::nullopt;
    }

    std::optional<Scene::CharacterSlideCollision> closest;
    float closestDistance = 0.0F;
    for (const Detail::ColliderWorldAabb& collider : *m_colliders) {
        if (!IsCollidable(collider)) {
            continue;
        }
        // The moving body's own AABB against the target's AABB, which is the convention the
        // existing kinematic move already resolves against: the boxes meet exactly where the
        // bodies would, and a body resting against a surface is *touching*, not overlapping
        // (Aabb::Intersects is strict), so resting contact stays sweepable.
        const std::optional<Math::SweptAabbHit> swept =
            Math::SweepAabb(movingAabb, motion, collider.worldAabb);
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
        Scene::CharacterSlideCollision hit;
        hit.entity = collider.entity;
        // Math::SweepAabb reports the direction the body was moving when it met the face - the
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

Math::Vector3 CharacterWorldQuery::GetSurfaceVelocity(const Scene::Entity entity) const {
    if (!m_entityManager->IsAlive(entity) || entity == m_entity) {
        return Math::Vector3{};
    }
    // Whatever is moving underfoot: a rigid body's own velocity, or a kinematic body's target -
    // the two ways a body can be moving without the character being told about it.
    if (m_entityManager->HasComponent<Scene::Rigid3DComponent>(entity)) {
        const Math::Vector3 velocity =
            m_entityManager->GetComponent<Scene::Rigid3DComponent>(entity).velocity;
        if (IsUsable(velocity)) {
            return velocity;
        }
    }
    if (m_entityManager->HasComponent<Scene::Kinematic3DComponent>(entity)) {
        const Scene::Kinematic3DComponent& kinematic =
            m_entityManager->GetComponent<Scene::Kinematic3DComponent>(entity);
        if (kinematic.active && IsUsable(kinematic.targetVelocity)) {
            return kinematic.targetVelocity;
        }
    }
    return Math::Vector3{};
}

std::optional<Math::Vector3> CharacterWorldQuery::TryGetEntityCenter(
    const Scene::Entity entity) const {
    if (!m_entityManager->IsAlive(entity) ||
        !m_entityManager->HasComponent<Scene::WorldTransformComponent>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3 position =
        m_entityManager->GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    if (!IsUsable(position)) {
        return std::nullopt;
    }
    return position;
}

// =================================================================================================
// One step of a character that has no shape.
// =================================================================================================

namespace {

/// Moves a body that has nothing to collide with.
///
/// A controller is not a collider. A Character3D with no Collider3D of its own and none under it
/// has no shape, and having no shape is not the same as being held up: the body falls - gravity
/// and its own velocity, applied straight to the transform, no contacts - so it drops through the
/// level until the author gives it a shape. Standing still instead was read, correctly, as an
/// invisible collider the character had been born with.
[[nodiscard]] Character3DStepResult StepShapelessCharacter(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    const Scene::Entity entity, const CharacterMotionInput& input, const float gravityY,
    const float deltaTimeSeconds) {
    Character3DStepResult report;
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        report.code = Character3DStepCode::MissingTransform;
        return report;
    }
    Scene::CharacterControllerComponent controller =
        entityManager.GetComponent<Scene::CharacterControllerComponent>(entity);
    // There is no floor to stand on: a body that was on one a moment ago is in the air now, and
    // the intent step has to see that or it would cancel the gravity that is about to move it.
    controller.grounded = false;
    controller.isOnCeiling = false;
    report.jumped = StepCharacterIntent(controller, input, gravityY, deltaTimeSeconds);
    // SolidBody3D's motion locks, the same way the shaped step applies them.
    if (entityManager.HasComponent<Scene::SolidBodyComponent>(entity)) {
        const Scene::SolidBodyComponent& locks =
            entityManager.GetComponent<Scene::SolidBodyComponent>(entity);
        controller.velocity.x = locks.lockMotionX ? 0.0F : controller.velocity.x;
        controller.velocity.y = locks.lockMotionY ? 0.0F : controller.velocity.y;
        controller.velocity.z = locks.lockMotionZ ? 0.0F : controller.velocity.z;
    }

    report.motion.code = Scene::CharacterMoveCode::Moved;
    report.motion.requestedMotion = controller.velocity * deltaTimeSeconds;
    // Nothing is in the way, so everything that was asked for is what happens.
    report.motion.appliedMotion = report.motion.requestedMotion;
    report.motion.slideMotion = report.motion.appliedMotion;
    report.motion.finalCenter =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition +
        report.motion.appliedMotion;
    if (IsUsable(report.motion.appliedMotion) && report.motion.appliedMotion != Math::Vector3{}) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localPosition += report.motion.appliedMotion;
        // Through the graph, so the world transform the next step reads is recomputed rather than
        // stale - the same reason the shaped move writes its motion back this way.
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    Scene::CharacterMotionState state = Scene::MakeCharacterMotionState(controller);
    state.grounded = false;
    state.onWall = false;
    state.onCeiling = false;
    state.lastMotion = report.motion.appliedMotion;
    Scene::StoreCharacterMotionState(controller, state);
    FinishCharacterStep(controller,
                           Physics::CharacterMoveOutcome{false, controller.upDirection, false},
                           report.jumped, deltaTimeSeconds);
    entityManager.GetComponent<Scene::CharacterControllerComponent>(entity) = controller;
    report.stepped = true;
    report.code = Character3DStepCode::NoShape;
    return report;
}

} // namespace

// =================================================================================================
// One step of one character.
// =================================================================================================

Character3DStepResult StepCharacter3D(Scene::IEntityManager& entityManager,
                                            Scene::ISceneGraph& sceneGraph,
                                            ICollisionSystem& collisionSystem,
                                            const Scene::Entity entity,
                                            const CharacterMotionInput& input, const float gravityY,
                                            const float deltaTimeSeconds) {
    Character3DStepResult report;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F || !std::isfinite(gravityY)) {
        report.code = Character3DStepCode::InvalidDeltaTime;
        return report;
    }
    if (!entityManager.IsAlive(entity)) {
        report.code = Character3DStepCode::UnknownEntity;
        return report;
    }
    if (!entityManager.HasComponent<Scene::CharacterControllerComponent>(entity)) {
        report.code = Character3DStepCode::NotAController;
        return report;
    }
    if (entityManager.HasComponent<Scene::PredictedCharacterComponent>(entity)) {
        report.code = Character3DStepCode::PredictedElsewhere;
        return report;
    }
    // The shape can be a Collider3D of the character's own under it, which is how an author gives
    // a body a collider they can select and see. Resolved once here so every question below is
    // asked about the right entity.
    const Scene::Entity shapeEntity =
        Scene::ResolveCharacterShapeEntity(entityManager, sceneGraph, entity);
    if (shapeEntity == Scene::kInvalidEntity) {
        return StepShapelessCharacter(entityManager, sceneGraph, entity, input, gravityY,
                                         deltaTimeSeconds);
    }
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        report.code = Character3DStepCode::MissingTransform;
        return report;
    }
    if (entityManager.HasComponent<Scene::Rigid3DComponent>(entity) &&
        !entityManager.GetComponent<Scene::Rigid3DComponent>(entity).isKinematic) {
        report.code = Character3DStepCode::NonKinematicBody;
        return report;
    }

    // The world this step moves through is the world as of now: the mover reads transforms, so the
    // graph has to have written them before the first query.
    sceneGraph.Update(entityManager);

    // Copied, worked on, and written back: the move can add components elsewhere (a push, a
    // callback), which can move this one in storage.
    Scene::CharacterControllerComponent controller =
        entityManager.GetComponent<Scene::CharacterControllerComponent>(entity);
    const bool wasOnFloor = controller.grounded;

    report.jumped = StepCharacterIntent(controller, input, gravityY, deltaTimeSeconds);

    // SolidBody3D's motion locks: along a locked axis the body neither moves nor keeps speed.
    if (entityManager.HasComponent<Scene::SolidBodyComponent>(entity)) {
        const Scene::SolidBodyComponent& locks =
            entityManager.GetComponent<Scene::SolidBodyComponent>(entity);
        controller.velocity.x = locks.lockMotionX ? 0.0F : controller.velocity.x;
        controller.velocity.y = locks.lockMotionY ? 0.0F : controller.velocity.y;
        controller.velocity.z = locks.lockMotionZ ? 0.0F : controller.velocity.z;
    }

    CharacterWorldQuery world(entityManager, collisionSystem, entity, shapeEntity);
    if (!world.IsMovableCharacter()) {
        report.code = Character3DStepCode::InvalidWorld;
        return report;
    }

    Scene::CharacterMotionState state = Scene::MakeCharacterMotionState(controller);
    // The intent step decides a jump by clearing `grounded`; the mover must still know it was
    // standing when the step began, or it would refuse a step-up and the platform that was left.
    state.grounded = wasOnFloor;

    Scene::CharacterMotionConfig config = Scene::MakeCharacterMotionConfig(controller);
    config.jumpedThisStep = report.jumped;

    const Math::Vector3 desiredMotion = controller.velocity * deltaTimeSeconds;
    report.motion = Scene::Character3D::MoveAndSlide(world, config, state, desiredMotion, deltaTimeSeconds);
    report.stepped = true;

    if (report.motion.code == Scene::CharacterMoveCode::Moved) {
        // Only a finished move is written to the world. A refused move (invalid state or world)
        // leaves the body exactly where it was, so a caller can retry or teleport it deliberately
        // instead of finding it half-moved.
        const Math::Vector3 applied = report.motion.appliedMotion;
        if (IsUsable(applied) && applied != Math::Vector3{}) {
            Scene::TransformComponent transform =
                entityManager.GetComponent<Scene::TransformComponent>(entity);
            transform.localPosition += applied;
            // Through the graph, not around it: the world transform has to be marked dirty and
            // recomputed, or the next step's queries would be answered about where the body used
            // to be. This is the one call that makes the move visible to the rest of the frame.
            sceneGraph.SetLocalTransform(entityManager, entity, transform);
            sceneGraph.Update(entityManager);
        }
        Scene::StoreCharacterMotionState(controller, state);
        FinishCharacterStep(controller,
                               Physics::CharacterMoveOutcome{report.motion.onFloor, report.motion.floorNormal,
                                                                report.motion.onCeiling},
                               report.jumped, deltaTimeSeconds);
        entityManager.GetComponent<Scene::CharacterControllerComponent>(entity) = controller;
        report.code = report.motion.blocked ? Character3DStepCode::Blocked : Character3DStepCode::Stepped;
        return report;
    }

    // The mover refused: the component keeps its authored values and untouched velocity, but the
    // intent step already advanced the coyote and buffer clocks, so those are written back.
    entityManager.GetComponent<Scene::CharacterControllerComponent>(entity) = controller;
    report.code = Character3DStepCode::InvalidWorld;
    return report;
}

namespace {

/// The speed a push falls back to when the authored one is missing or meaningless. Spelling the
/// fallback out here rather than trusting the caller keeps a script from turning a shove into a
/// launch by writing zero.
constexpr float kDefaultDynamicPushSpeed = 5.0F;

} // namespace

std::size_t PushBodiesFromCharacterMove(
    Scene::IEntityManager& entityManager,
    const std::vector<Scene::CharacterSlideCollision>& contacts,
    const CharacterDynamicPushPolicy& policy) {
    if (!policy.enabled || !std::isfinite(policy.strength) || policy.strength <= 0.0F ||
        !std::isfinite(policy.deltaTimeSeconds) || policy.deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    const float maximumSpeed =
        std::isfinite(policy.maximumSpeed) && policy.maximumSpeed > 0.0F
            ? policy.maximumSpeed
            : kDefaultDynamicPushSpeed;

    std::size_t pushedCount = 0U;
    std::vector<Scene::Entity> alreadyPushed;
    alreadyPushed.reserve(contacts.size());
    for (const Scene::CharacterSlideCollision& contact : contacts) {
        const float normalLengthSquared = Math::LengthSquared(contact.normal);
        if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F) {
            continue;
        }
        // The mover's contact normal points from the surface back at the character, so the push
        // goes the other way: into the surface, which is away from the character and into the body
        // that was in its path.
        const Math::Vector3 pushDirection =
            contact.normal * (-1.0F / std::sqrt(normalLengthSquared));
        // The part of the motion the surface removed: what the character was actually pressing
        // into the body with. A contact the character merely brushed past pushes nothing.
        const float intoTarget = Math::Dot(contact.motion, pushDirection);
        if (!std::isfinite(intoTarget) || intoTarget <= 0.0F || !Math::IsFinite(contact.motion)) {
            continue;
        }
        if (std::find(alreadyPushed.begin(), alreadyPushed.end(), contact.entity) != alreadyPushed.end()) {
            continue;
        }
        if (!entityManager.IsAlive(contact.entity) ||
            !entityManager.HasComponent<Scene::Rigid3DComponent>(contact.entity)) {
            continue;
        }
        // A body the simulation is not moving - a PhysicsObject3D kept as a static obstacle, or
        // one taken out of the world - is not something a walk into it can push, however it came
        // to be in the contact list.
        if (!Scene::IsPhysicsObjectSimulated(entityManager, contact.entity)) {
            continue;
        }
        Scene::Rigid3DComponent& rigidBody =
            entityManager.GetComponent<Scene::Rigid3DComponent>(contact.entity);
        if (!Scene::IsRigid3DComponentValid(rigidBody) || rigidBody.isKinematic ||
            rigidBody.mass <= 0.0F) {
            continue;
        }
        const float desiredNormalSpeed =
            std::min(maximumSpeed, intoTarget * policy.strength / policy.deltaTimeSeconds);
        if (!std::isfinite(desiredNormalSpeed) || desiredNormalSpeed <= 0.0F) {
            continue;
        }
        const float currentNormalSpeed = Math::Dot(rigidBody.velocity, pushDirection);
        if (!std::isfinite(currentNormalSpeed)) {
            continue;
        }
        const float speedDelta = desiredNormalSpeed - currentNormalSpeed;
        if (!std::isfinite(speedDelta) || speedDelta <= 0.0F) {
            continue;
        }
        const Math::Vector3 proposed = rigidBody.velocity + pushDirection * speedDelta;
        if (!Math::IsFinite(proposed)) {
            continue;
        }
        rigidBody.velocity = proposed;
        alreadyPushed.push_back(contact.entity);
        ++pushedCount;
    }
    return pushedCount;
}

} // namespace UVE::Physics
