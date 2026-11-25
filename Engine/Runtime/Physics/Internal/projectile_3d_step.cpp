// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "projectile_3d_step.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "transform_component.h"
#include "world_transform_component.h"
#include "quaternion.h"
#include "object_3d_common.h"
#include "shape_cast_system.h"

namespace UVE::Physics {
namespace {

/// How far off the surface a bounce is placed before the next sweep, in metres.
///
/// A contact point is exactly on the surface, and a sweep that starts exactly on it reports an
/// overlap with no normal - a contact the projectile cannot resolve, so it would be allowed to
/// leave through the wall it just bounced off. A millimetre of air keeps every bounce sweepable,
/// which is the same job the character mover's safe margin does for a body against a floor.
constexpr float kProjectile3DBounceSkin = 1.0e-3F;

[[nodiscard]] Projectile3DStepResult Refuse(const Projectile3DStepCode code,
                                                         const Scene::Projectile3DComponent* const projectile) {
    Projectile3DStepResult result;
    result.code = code;
    if (projectile != nullptr) {
        result.velocity = projectile->velocity;
        result.appliedPolicy = projectile->hitPolicy;
        result.bounceCount = projectile->bounceCount;
        result.remainingLifetime = projectile->remainingLifetime;
    }
    return result;
}

} // namespace

Projectile3DStepResult StepProjectile3D(Scene::IEntityManager& entityManager,
                                                     Scene::ISceneGraph& sceneGraph,
                                                     const Scene::Entity entity,
                                                     const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return Refuse(Projectile3DStepCode::InvalidDeltaTime, nullptr);
    }
    if (!entityManager.IsAlive(entity)) {
        return Refuse(Projectile3DStepCode::UnknownEntity, nullptr);
    }
    if (!entityManager.HasComponent<Scene::Projectile3DComponent>(entity)) {
        return Refuse(Projectile3DStepCode::NotAProjectile, nullptr);
    }
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return Refuse(Projectile3DStepCode::MissingTransform, nullptr);
    }

    const Scene::Projectile3DComponent projectile =
        entityManager.GetComponent<Scene::Projectile3DComponent>(entity);
    if (!Scene::IsProjectile3DObjectComponentValid(projectile)) {
        return Refuse(Projectile3DStepCode::InvalidComponent, &projectile);
    }
    if (!Scene::Projectile3D::IsFlying(projectile)) {
        return Refuse(Projectile3DStepCode::Disabled, &projectile);
    }

    // ---- Integrate, then describe the step's motion in the space the colliders live in ----------
    const Math::Vector3 velocity = Scene::Projectile3D::IntegrateVelocity(
        projectile.velocity, projectile.acceleration, deltaTimeSeconds);
    if (!Scene::IsFinite3DObjectVector(velocity)) {
        return Refuse(Projectile3DStepCode::InvalidComponent, &projectile);
    }

    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    const Math::Vector3 worldVelocity =
        Scene::Projectile3D::ResolveWorldVelocity(velocity, worldTransform.worldRotation);
    Math::Quaternion worldRotation{};
    if (!Math::TryNormalize(worldTransform.worldRotation, worldRotation)) {
        worldRotation = {};
    }
    const float speed = Math::Length(worldVelocity);
    const float stepDistance = speed * deltaTimeSeconds;
    if (!Math::IsFinite(worldTransform.worldPosition) || !Math::IsFinite(worldVelocity) ||
        !std::isfinite(speed) || !std::isfinite(stepDistance)) {
        return Refuse(Projectile3DStepCode::InvalidComponent, &projectile);
    }

    Projectile3DStepResult result;
    result.velocity = velocity;
    result.appliedPolicy = projectile.hitPolicy;
    result.bounceCount = projectile.bounceCount;
    result.remainingLifetime = projectile.remainingLifetime;

    std::optional<SphereCastHit> contact;
    if (stepDistance > 0.0F) {
        SphereCastQuery query{};
        query.ray.origin = worldTransform.worldPosition;
        query.ray.direction = worldVelocity * (1.0F / speed);
        query.radius = projectile.radius;
        query.maxDistance = stepDistance;
        query.layerMask = projectile.collisionMask;
        query.ignoreEntity = entity;
        query.alsoIgnoreEntity = projectile.ignoreEntity;
        contact = ShapeCastSystem::SphereCast(entityManager, query);
        // An overlap the step *begins* inside reports distance zero and no normal: there is no
        // direction to resolve and no surface to have touched, so it is not a contact and the
        // projectile keeps its motion. A projectile fired from inside a launcher volume, or one
        // that has just bounced off the surface it is standing on, is allowed to leave.
        if (contact.has_value() && !Scene::Projectile3D::IsUsableMotion(contact->normal)) {
            contact.reset();
        }
    }

    // ---- Commit the motion ----------------------------------------------------------------------
    Scene::TransformComponent localTransform =
        entityManager.GetComponent<Scene::TransformComponent>(entity);
    Math::Vector3 committedVelocity = velocity;

    if (!contact.has_value()) {
        localTransform.localPosition += committedVelocity * deltaTimeSeconds;
        sceneGraph.SetLocalTransform(entityManager, entity, localTransform);
        result.movedDistance = stepDistance;
        result.code = Projectile3DStepCode::Stepped;
    } else {
        const SphereCastHit& hit = *contact;
        const Math::Vector3 unitNormal = Math::Normalize(hit.normal);
        const float fraction =
            stepDistance > 0.0F ? std::clamp(hit.distance / stepDistance, 0.0F, 1.0F) : 0.0F;
        localTransform.localPosition += committedVelocity * (deltaTimeSeconds * fraction);

        // The contact normal arrives in world space; the reflection is done in the projectile's
        // own axes, so the normal is rotated back through the inverse of the pose the sweep used.
        // A pose whose inverse cannot be built falls back to the world normal - the reflection is
        // then only exact for an unrotated object, which is the best a broken rotation allows.
        Math::Vector3 localNormal = unitNormal;
        Math::Quaternion inverseWorldRotation{};
        if (Math::TryInverse(worldRotation, inverseWorldRotation)) {
            localNormal = Math::RotateVector(inverseWorldRotation, unitNormal);
        }

        result.hasHit = true;
        result.hitEntity = hit.entity;
        result.hitNormal = unitNormal;
        result.hitPosition = hit.center - unitNormal * projectile.radius;
        result.impactSpeed = std::max(0.0F, -Math::Dot(worldVelocity, unitNormal));
        result.movedDistance = stepDistance * fraction;

        const Scene::Projectile3D::ContactMotion motion = Scene::Projectile3D::ResolveContact(
            projectile, committedVelocity, localNormal);
        committedVelocity = motion.velocity;
        if (motion.bounced) {
            localTransform.localPosition += localNormal * kProjectile3DBounceSkin;
            result.bounced = true;
            result.bounceCount = projectile.bounceCount + 1U;
            result.code = Projectile3DStepCode::Bounced;
        } else {
            result.stoppedOnHit = true;
            result.code = Projectile3DStepCode::Stopped;
        }
        sceneGraph.SetLocalTransform(entityManager, entity, localTransform);
    }

    // ---- Lifetime, then the one write-back -------------------------------------------------------
    float remainingLifetime =
        Scene::Projectile3D::TickLifetime(projectile.remainingLifetime, deltaTimeSeconds);
    const bool expired = Scene::Projectile3D::HasExpired(remainingLifetime);
    if (expired) {
        remainingLifetime = 0.0F;
    }
    if (expired && !result.stoppedOnHit) {
        result.code = Projectile3DStepCode::Expired;
    }

    Scene::Projectile3DComponent& live =
        entityManager.GetComponent<Scene::Projectile3DComponent>(entity);
    live.velocity = committedVelocity;
    live.remainingLifetime = remainingLifetime;
    live.active = !expired && !result.stoppedOnHit;
    live.bounceCount = result.bounceCount;
    if (result.hasHit) {
        Scene::Projectile3D::RecordHit(live, result.hitEntity, result.hitPosition, result.hitNormal,
                                             result.impactSpeed);
    }

    result.velocity = committedVelocity;
    result.remainingLifetime = remainingLifetime;
    result.expired = expired;
    return result;
}

} // namespace UVE::Physics
