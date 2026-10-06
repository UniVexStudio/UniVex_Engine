// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/projectile_3d_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

/// Clamps an authored coefficient into the only range that means anything here. A non-finite value
/// is refused by the component's own validator before it gets this far; the clamp exists so a
/// caller reading a hand-built value directly can never gain energy from it.
[[nodiscard]] float ClampCoefficientUVE(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

} // namespace

bool IsKnownProjectile3DHitPolicyUVE(const Projectile3DHitPolicyUVE policy) noexcept {
    switch (policy) {
        case Projectile3DHitPolicyUVE::Stop:
        case Projectile3DHitPolicyUVE::Bounce:
            return true;
    }
    return false;
}

bool Projectile3DUVE::IsFlyingUVE(const Projectile3DComponentUVE& projectile) noexcept {
    return projectile.active;
}

bool Projectile3DUVE::AcceptsObstacleUVE(const EntityUVE projectileEntity,
                                         const Projectile3DComponentUVE& projectile,
                                         const EntityUVE obstacle) noexcept {
    if (obstacle == kInvalidEntityUVE || obstacle == projectileEntity) {
        return false;
    }
    return projectile.ignoreEntity == kInvalidEntityUVE || obstacle != projectile.ignoreEntity;
}

Math::Vector3UVE Projectile3DUVE::IntegrateVelocityUVE(const Math::Vector3UVE& velocity,
                                                       const Math::Vector3UVE& acceleration,
                                                       const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return velocity;
    }
    return velocity + acceleration * deltaTimeSeconds;
}

Math::Vector3UVE Projectile3DUVE::ResolveWorldVelocityUVE(const Math::Vector3UVE& localVelocity,
                                                          const Math::QuaternionUVE& worldRotation) noexcept {
    if (!IsFinite3DObjectVectorUVE(localVelocity)) {
        return {};
    }
    Math::QuaternionUVE rotation{};
    if (!Math::TryNormalizeUVE(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVectorUVE(rotation, localVelocity);
}

Math::Vector3UVE Projectile3DUVE::BounceVelocityUVE(const Math::Vector3UVE& velocity,
                                                    const Math::Vector3UVE& normal,
                                                    const float restitution,
                                                    const float friction) noexcept {
    const float normalLengthSquared = Math::LengthSquaredUVE(normal);
    if (!IsFinite3DObjectVectorUVE(velocity) || !IsFinite3DObjectVectorUVE(normal) ||
        !std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-12F || !std::isfinite(restitution) ||
        !std::isfinite(friction)) {
        return velocity;
    }

    const Math::Vector3UVE unitNormal = Math::NormalizeUVE(normal);
    const Math::Vector3UVE normalComponent = unitNormal * Math::DotUVE(velocity, unitNormal);
    const Math::Vector3UVE tangentComponent = velocity - normalComponent;
    return tangentComponent * (1.0F - ClampCoefficientUVE(friction)) -
           normalComponent * ClampCoefficientUVE(restitution);
}

bool Projectile3DUVE::IsUsableMotionUVE(const Math::Vector3UVE& velocity) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(velocity);
    return IsFinite3DObjectVectorUVE(velocity) && std::isfinite(lengthSquared) && lengthSquared > 1.0e-12F;
}

Projectile3DUVE::ContactMotionUVE Projectile3DUVE::ResolveContactUVE(
    const Projectile3DComponentUVE& projectile, const Math::Vector3UVE& localVelocity,
    const Math::Vector3UVE& localNormal) noexcept {
    if (projectile.hitPolicy == Projectile3DHitPolicyUVE::Bounce) {
        const Math::Vector3UVE reflected =
            BounceVelocityUVE(localVelocity, localNormal, projectile.restitution, projectile.friction);
        if (IsUsableMotionUVE(reflected)) {
            return ContactMotionUVE{reflected, true, false};
        }
    }
    return ContactMotionUVE{{}, false, true};
}

float Projectile3DUVE::TickLifetimeUVE(const float remainingLifetime, const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(remainingLifetime) || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return remainingLifetime;
    }
    const float next = remainingLifetime - deltaTimeSeconds;
    if (!std::isfinite(next) || next <= 0.0F) {
        return 0.0F;
    }
    return next;
}

bool Projectile3DUVE::HasExpiredUVE(const float remainingLifetime) noexcept {
    return !std::isfinite(remainingLifetime) || remainingLifetime <= 0.0F;
}

void Projectile3DUVE::RecordHitUVE(Projectile3DComponentUVE& projectile, const EntityUVE hitEntity,
                                   const Math::Vector3UVE& hitPosition, const Math::Vector3UVE& hitNormal,
                                   const float impactSpeed) noexcept {
    projectile.hit = true;
    projectile.hitEntity = hitEntity;
    projectile.hitPosition = hitPosition;
    projectile.hitNormal = hitNormal;
    projectile.impactSpeed = impactSpeed;
}

Math::Vector3UVE ResolveProjectile3DBounceVelocityUVE(const Math::Vector3UVE& velocity,
                                                     const Math::Vector3UVE& normal,
                                                     const float restitution,
                                                     const float friction) noexcept {
    return Projectile3DUVE::BounceVelocityUVE(velocity, normal, restitution, friction);
}

bool IsProjectile3DObjectComponentValidUVE(const Projectile3DComponentUVE& value) noexcept {
    if (!IsFinite3DObjectVectorUVE(value.velocity) || !IsFinite3DObjectVectorUVE(value.acceleration) ||
        !std::isfinite(value.radius) || value.radius <= 0.0F || !std::isfinite(value.maxLifetime) ||
        value.maxLifetime <= 0.0F || !std::isfinite(value.remainingLifetime) || value.remainingLifetime < 0.0F ||
        value.remainingLifetime > value.maxLifetime || !IsKnownProjectile3DHitPolicyUVE(value.hitPolicy) ||
        !std::isfinite(value.restitution) || value.restitution < 0.0F || value.restitution > 1.0F ||
        !std::isfinite(value.friction) || value.friction < 0.0F || value.friction > 1.0F) {
        return false;
    }

    // Runtime result state only has to stay readable: a claimed hit names its entity and carries
    // finite numbers, so a consumer that trusts `hit` can never act on a hit that points nowhere.
    if (!value.hit) {
        return true;
    }
    return value.hitEntity != kInvalidEntityUVE && IsFinite3DObjectVectorUVE(value.hitPosition) &&
           IsFinite3DObjectVectorUVE(value.hitNormal) && std::isfinite(value.impactSpeed) && value.impactSpeed >= 0.0F;
}

} // namespace UVE::Scene
