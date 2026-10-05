// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/projectile_3d_step_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/physics/shape_cast_system_uve.h"

namespace UVE::Physics {
namespace {

/// How far off the surface a bounce is placed before the next sweep, in metres.
///
/// A contact point is exactly on the surface, and a sweep that starts exactly on it reports an
/// overlap with no normal - a contact the projectile cannot resolve, so it would be allowed to
/// leave through the wall it just bounced off. A millimetre of air keeps every bounce sweepable,
/// which is the same job the character mover's safe margin does for a body against a floor.
constexpr float kProjectile3DBounceSkinUVE = 1.0e-3F;

[[nodiscard]] Projectile3DStepResultUVE RefuseUVE(const Projectile3DStepCodeUVE code,
                                                         const Scene::Projectile3DComponentUVE* const projectile) {
    Projectile3DStepResultUVE result;
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

Projectile3DStepResultUVE StepProjectile3DUVE(Scene::IEntityManagerUVE& entityManager,
                                                     Scene::ISceneGraphUVE& sceneGraph,
                                                     const Scene::EntityUVE entity,
                                                     const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return RefuseUVE(Projectile3DStepCodeUVE::InvalidDeltaTime, nullptr);
    }
    if (!entityManager.IsAliveUVE(entity)) {
        return RefuseUVE(Projectile3DStepCodeUVE::UnknownEntity, nullptr);
    }
    if (!entityManager.HasComponentUVE<Scene::Projectile3DComponentUVE>(entity)) {
        return RefuseUVE(Projectile3DStepCodeUVE::NotAProjectile, nullptr);
    }
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return RefuseUVE(Projectile3DStepCodeUVE::MissingTransform, nullptr);
    }

    const Scene::Projectile3DComponentUVE projectile =
        entityManager.GetComponentUVE<Scene::Projectile3DComponentUVE>(entity);
    if (!Scene::IsProjectile3DObjectComponentValidUVE(projectile)) {
        return RefuseUVE(Projectile3DStepCodeUVE::InvalidComponent, &projectile);
    }
    if (!Scene::Projectile3DUVE::IsFlyingUVE(projectile)) {
        return RefuseUVE(Projectile3DStepCodeUVE::Disabled, &projectile);
    }

    // ---- Integrate, then describe the step's motion in the space the colliders live in ----------
    const Math::Vector3UVE velocity = Scene::Projectile3DUVE::IntegrateVelocityUVE(
        projectile.velocity, projectile.acceleration, deltaTimeSeconds);
    if (!Scene::IsFinite3DObjectVectorUVE(velocity)) {
        return RefuseUVE(Projectile3DStepCodeUVE::InvalidComponent, &projectile);
    }

    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    const Math::Vector3UVE worldVelocity =
        Scene::Projectile3DUVE::ResolveWorldVelocityUVE(velocity, worldTransform.worldRotation);
    Math::QuaternionUVE worldRotation{};
    if (!Math::TryNormalizeUVE(worldTransform.worldRotation, worldRotation)) {
        worldRotation = {};
    }
    const float speed = Math::LengthUVE(worldVelocity);
    const float stepDistance = speed * deltaTimeSeconds;
    if (!Math::IsFiniteUVE(worldTransform.worldPosition) || !Math::IsFiniteUVE(worldVelocity) ||
        !std::isfinite(speed) || !std::isfinite(stepDistance)) {
        return RefuseUVE(Projectile3DStepCodeUVE::InvalidComponent, &projectile);
    }

    Projectile3DStepResultUVE result;
    result.velocity = velocity;
    result.appliedPolicy = projectile.hitPolicy;
    result.bounceCount = projectile.bounceCount;
    result.remainingLifetime = projectile.remainingLifetime;

    std::optional<SphereCastHitUVE> contact;
    if (stepDistance > 0.0F) {
        SphereCastQueryUVE query{};
        query.ray.origin = worldTransform.worldPosition;
        query.ray.direction = worldVelocity * (1.0F / speed);
        query.radius = projectile.radius;
        query.maxDistance = stepDistance;
        query.layerMask = projectile.collisionMask;
        query.ignoreEntity = entity;
        query.alsoIgnoreEntity = projectile.ignoreEntity;
        contact = ShapeCastSystemUVE::SphereCastUVE(entityManager, query);
        // An overlap the step *begins* inside reports distance zero and no normal: there is no
        // direction to resolve and no surface to have touched, so it is not a contact and the
        // projectile keeps its motion. A projectile fired from inside a launcher volume, or one
        // that has just bounced off the surface it is standing on, is allowed to leave.
        if (contact.has_value() && !Scene::Projectile3DUVE::IsUsableMotionUVE(contact->normal)) {
            contact.reset();
        }
    }

    // ---- Commit the motion ----------------------------------------------------------------------
    Scene::TransformComponentUVE localTransform =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    Math::Vector3UVE committedVelocity = velocity;

    if (!contact.has_value()) {
        localTransform.localPosition += committedVelocity * deltaTimeSeconds;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, localTransform);
        result.movedDistance = stepDistance;
        result.code = Projectile3DStepCodeUVE::Stepped;
    } else {
        const SphereCastHitUVE& hit = *contact;
        const Math::Vector3UVE unitNormal = Math::NormalizeUVE(hit.normal);
        const float fraction =
            stepDistance > 0.0F ? std::clamp(hit.distance / stepDistance, 0.0F, 1.0F) : 0.0F;
        localTransform.localPosition += committedVelocity * (deltaTimeSeconds * fraction);

        // The contact normal arrives in world space; the reflection is done in the projectile's
        // own axes, so the normal is rotated back through the inverse of the pose the sweep used.
        // A pose whose inverse cannot be built falls back to the world normal - the reflection is
        // then only exact for an unrotated object, which is the best a broken rotation allows.
        Math::Vector3UVE localNormal = unitNormal;
        Math::QuaternionUVE inverseWorldRotation{};
        if (Math::TryInverseUVE(worldRotation, inverseWorldRotation)) {
            localNormal = Math::RotateVectorUVE(inverseWorldRotation, unitNormal);
        }

        result.hasHit = true;
        result.hitEntity = hit.entity;
        result.hitNormal = unitNormal;
        result.hitPosition = hit.center - unitNormal * projectile.radius;
        result.impactSpeed = std::max(0.0F, -Math::DotUVE(worldVelocity, unitNormal));
        result.movedDistance = stepDistance * fraction;

        const Scene::Projectile3DUVE::ContactMotionUVE motion = Scene::Projectile3DUVE::ResolveContactUVE(
            projectile, committedVelocity, localNormal);
        committedVelocity = motion.velocity;
        if (motion.bounced) {
            localTransform.localPosition += localNormal * kProjectile3DBounceSkinUVE;
            result.bounced = true;
            result.bounceCount = projectile.bounceCount + 1U;
            result.code = Projectile3DStepCodeUVE::Bounced;
        } else {
            result.stoppedOnHit = true;
            result.code = Projectile3DStepCodeUVE::Stopped;
        }
        sceneGraph.SetLocalTransformUVE(entityManager, entity, localTransform);
    }

    // ---- Lifetime, then the one write-back -------------------------------------------------------
    float remainingLifetime =
        Scene::Projectile3DUVE::TickLifetimeUVE(projectile.remainingLifetime, deltaTimeSeconds);
    const bool expired = Scene::Projectile3DUVE::HasExpiredUVE(remainingLifetime);
    if (expired) {
        remainingLifetime = 0.0F;
    }
    if (expired && !result.stoppedOnHit) {
        result.code = Projectile3DStepCodeUVE::Expired;
    }

    Scene::Projectile3DComponentUVE& live =
        entityManager.GetComponentUVE<Scene::Projectile3DComponentUVE>(entity);
    live.velocity = committedVelocity;
    live.remainingLifetime = remainingLifetime;
    live.active = !expired && !result.stoppedOnHit;
    live.bounceCount = result.bounceCount;
    if (result.hasHit) {
        Scene::Projectile3DUVE::RecordHitUVE(live, result.hitEntity, result.hitPosition, result.hitNormal,
                                             result.impactSpeed);
    }

    result.velocity = committedVelocity;
    result.remainingLifetime = remainingLifetime;
    result.expired = expired;
    return result;
}

} // namespace UVE::Physics
