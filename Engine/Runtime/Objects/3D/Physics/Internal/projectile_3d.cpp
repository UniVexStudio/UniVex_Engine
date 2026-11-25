// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "projectile_3d.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

/// Clamps an authored coefficient into the only range that means anything here. A non-finite value
/// is refused by the component's own validator before it gets this far; the clamp exists so a
/// caller reading a hand-built value directly can never gain energy from it.
[[nodiscard]] float ClampCoefficient(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

} // namespace

bool IsKnownProjectile3DHitPolicy(const Projectile3DHitPolicy policy) noexcept {
    switch (policy) {
        case Projectile3DHitPolicy::Stop:
        case Projectile3DHitPolicy::Bounce:
            return true;
    }
    return false;
}

bool Projectile3D::IsFlying(const Projectile3DComponent& projectile) noexcept {
    return projectile.active;
}

bool Projectile3D::AcceptsObstacle(const Entity projectileEntity,
                                         const Projectile3DComponent& projectile,
                                         const Entity obstacle) noexcept {
    if (obstacle == kInvalidEntity || obstacle == projectileEntity) {
        return false;
    }
    return projectile.ignoreEntity == kInvalidEntity || obstacle != projectile.ignoreEntity;
}

Math::Vector3 Projectile3D::IntegrateVelocity(const Math::Vector3& velocity,
                                                       const Math::Vector3& acceleration,
                                                       const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return velocity;
    }
    return velocity + acceleration * deltaTimeSeconds;
}

Math::Vector3 Projectile3D::ResolveWorldVelocity(const Math::Vector3& localVelocity,
                                                          const Math::Quaternion& worldRotation) noexcept {
    if (!IsFinite3DObjectVector(localVelocity)) {
        return {};
    }
    Math::Quaternion rotation{};
    if (!Math::TryNormalize(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVector(rotation, localVelocity);
}

Math::Vector3 Projectile3D::BounceVelocity(const Math::Vector3& velocity,
                                                    const Math::Vector3& normal,
                                                    const float restitution,
                                                    const float friction) noexcept {
    const float normalLengthSquared = Math::LengthSquared(normal);
    if (!IsFinite3DObjectVector(velocity) || !IsFinite3DObjectVector(normal) ||
        !std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-12F || !std::isfinite(restitution) ||
        !std::isfinite(friction)) {
        return velocity;
    }

    const Math::Vector3 unitNormal = Math::Normalize(normal);
    const Math::Vector3 normalComponent = unitNormal * Math::Dot(velocity, unitNormal);
    const Math::Vector3 tangentComponent = velocity - normalComponent;
    return tangentComponent * (1.0F - ClampCoefficient(friction)) -
           normalComponent * ClampCoefficient(restitution);
}

bool Projectile3D::IsUsableMotion(const Math::Vector3& velocity) noexcept {
    const float lengthSquared = Math::LengthSquared(velocity);
    return IsFinite3DObjectVector(velocity) && std::isfinite(lengthSquared) && lengthSquared > 1.0e-12F;
}

Projectile3D::ContactMotion Projectile3D::ResolveContact(
    const Projectile3DComponent& projectile, const Math::Vector3& localVelocity,
    const Math::Vector3& localNormal) noexcept {
    if (projectile.hitPolicy == Projectile3DHitPolicy::Bounce) {
        const Math::Vector3 reflected =
            BounceVelocity(localVelocity, localNormal, projectile.restitution, projectile.friction);
        if (IsUsableMotion(reflected)) {
            return ContactMotion{reflected, true, false};
        }
    }
    return ContactMotion{{}, false, true};
}

float Projectile3D::TickLifetime(const float remainingLifetime, const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(remainingLifetime) || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return remainingLifetime;
    }
    const float next = remainingLifetime - deltaTimeSeconds;
    if (!std::isfinite(next) || next <= 0.0F) {
        return 0.0F;
    }
    return next;
}

bool Projectile3D::HasExpired(const float remainingLifetime) noexcept {
    return !std::isfinite(remainingLifetime) || remainingLifetime <= 0.0F;
}

void Projectile3D::RecordHit(Projectile3DComponent& projectile, const Entity hitEntity,
                                   const Math::Vector3& hitPosition, const Math::Vector3& hitNormal,
                                   const float impactSpeed) noexcept {
    projectile.hit = true;
    projectile.hitEntity = hitEntity;
    projectile.hitPosition = hitPosition;
    projectile.hitNormal = hitNormal;
    projectile.impactSpeed = impactSpeed;
}

Math::Vector3 ResolveProjectile3DBounceVelocity(const Math::Vector3& velocity,
                                                     const Math::Vector3& normal,
                                                     const float restitution,
                                                     const float friction) noexcept {
    return Projectile3D::BounceVelocity(velocity, normal, restitution, friction);
}

bool IsProjectile3DObjectComponentValid(const Projectile3DComponent& value) noexcept {
    if (!IsFinite3DObjectVector(value.velocity) || !IsFinite3DObjectVector(value.acceleration) ||
        !std::isfinite(value.radius) || value.radius <= 0.0F || !std::isfinite(value.maxLifetime) ||
        value.maxLifetime <= 0.0F || !std::isfinite(value.remainingLifetime) || value.remainingLifetime < 0.0F ||
        value.remainingLifetime > value.maxLifetime || !IsKnownProjectile3DHitPolicy(value.hitPolicy) ||
        !std::isfinite(value.restitution) || value.restitution < 0.0F || value.restitution > 1.0F ||
        !std::isfinite(value.friction) || value.friction < 0.0F || value.friction > 1.0F) {
        return false;
    }

    // Runtime result state only has to stay readable: a claimed hit names its entity and carries
    // finite numbers, so a consumer that trusts `hit` can never act on a hit that points nowhere.
    if (!value.hit) {
        return true;
    }
    return value.hitEntity != kInvalidEntity && IsFinite3DObjectVector(value.hitPosition) &&
           IsFinite3DObjectVector(value.hitNormal) && std::isfinite(value.impactSpeed) && value.impactSpeed >= 0.0F;
}

} // namespace UVE::Scene
