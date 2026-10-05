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

/// True for a vector long enough and finite enough to be a direction. The threshold is squared
/// length, so it is a length of about 1e-6 - far below any authored metre value and far above the
/// noise a denormal would bring.
[[nodiscard]] bool IsUsableDirectionUVE(const Math::Vector3UVE& value) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(value);
    return Math::IsFiniteUVE(value) && std::isfinite(lengthSquared) && lengthSquared > 1.0e-12F;
}

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
    if (!projectile.active) {
        return RefuseUVE(Projectile3DStepCodeUVE::Disabled, &projectile);
    }

    // ---- Integrate, then describe the step's motion in the space the colliders live in ----------
    const Math::Vector3UVE velocity =
        projectile.velocity + projectile.acceleration * deltaTimeSeconds;
    if (!Scene::IsFinite3DObjectVectorUVE(velocity)) {
        return RefuseUVE(Projectile3DStepCodeUVE::InvalidComponent, &projectile);
    }

    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    // A degenerate rotation would send the projectile along a garbage axis; fall back to the
    // identity rather than refuse a body whose pose the scene graph could not resolve, the same
    // way the spring arm and the hitbox sync read it.
    Math::QuaternionUVE worldRotation{};
    if (!Math::TryNormalizeUVE(worldTransform.worldRotation, worldRotation)) {
        worldRotation = {};
    }
    const Math::Vector3UVE worldVelocity = Math::RotateVectorUVE(worldRotation, velocity);
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
        contact = ShapeCastSystemUVE::SphereCastUVE(entityManager, query);
        // An overlap the step *begins* inside reports distance zero and no normal: there is no
        // direction to resolve and no surface to have touched, so it is not a contact and the
        // projectile keeps its motion. A projectile fired from inside a launcher volume, or one
        // that has just bounced off the surface it is standing on, is allowed to leave.
        if (contact.has_value() && !IsUsableDirectionUVE(contact->normal)) {
            contact.reset();
        }
    }

    // ---- Commit the motion ----------------------------------------------------------------------
    Scene::TransformComponentUVE localTransform =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    Math::Vector3UVE committedVelocity = velocity;
    bool expired = false;

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

        if (projectile.hitPolicy == Scene::Projectile3DHitPolicyUVE::Bounce) {
            const Math::Vector3UVE reflected = Scene::ResolveProjectile3DBounceVelocityUVE(
                committedVelocity, localNormal, projectile.restitution, projectile.friction);
            if (IsUsableDirectionUVE(reflected)) {
                committedVelocity = reflected;
                // Leave the surface by the contact skin, along the surface normal, so the next
                // sweep starts outside the obstacle instead of inside it.
                localTransform.localPosition += localNormal * kProjectile3DBounceSkinUVE;
                result.bounced = true;
                result.bounceCount = projectile.bounceCount + 1U;
                result.code = Projectile3DStepCodeUVE::Bounced;
            } else {
                // A bounce that leaves nothing to fly with is a stop: a projectile resting on a
                // wall is not a projectile flying, and hovering there until its lifetime ends
                // would only hide the decision.
                committedVelocity = {};
                result.stoppedOnHit = true;
                result.code = Projectile3DStepCodeUVE::Stopped;
            }
        } else {
            committedVelocity = {};
            result.stoppedOnHit = true;
            result.code = Projectile3DStepCodeUVE::Stopped;
        }
        sceneGraph.SetLocalTransformUVE(entityManager, entity, localTransform);
    }

    // ---- Lifetime, then the one write-back -------------------------------------------------------
    float remainingLifetime = projectile.remainingLifetime - deltaTimeSeconds;
    if (remainingLifetime <= 0.0F) {
        remainingLifetime = 0.0F;
        expired = true;
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
        // Sticky by contract: the last resolved contact is what gameplay reads after the fact, so
        // it is written here and left alone by every step that does not resolve a contact.
        live.hit = true;
        live.hitEntity = result.hitEntity;
        live.hitPosition = result.hitPosition;
        live.hitNormal = result.hitNormal;
        live.impactSpeed = result.impactSpeed;
    }

    result.velocity = committedVelocity;
    result.remainingLifetime = remainingLifetime;
    result.expired = expired;
    return result;
}

} // namespace UVE::Physics
