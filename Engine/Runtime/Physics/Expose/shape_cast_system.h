// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>

#include "ray.h"
#include "physics_material.h"
#include "entity.h"

namespace UVE::Scene {
class IEntityManager;
}

namespace UVE::Physics {

/// Bounded sphere-cast query. Sphere targets use exact moving-sphere/sphere time-of-impact and
/// capsule targets use exact point-ray/capsule time-of-impact with the moving radius expanded into
/// the capsule; box and unknown targets remain conservatively expanded cached world AABBs. This is
/// read-only and does not claim exact oriented-box casts or backend replacement.
struct SphereCastQuery final {
    Math::Ray ray{};
    float radius = 0.0F;
    float maxDistance = 0.0F;
    std::uint32_t layerMask = 0xFFFFFFFFU;
    Scene::Entity ignoreEntity{};
    Scene::Entity alsoIgnoreEntity{};
};

struct BoxCastQuery final {
    Math::Ray ray{};
    Math::Vector3 halfExtents{};
    float maxDistance = 0.0F;
    std::uint32_t layerMask = 0xFFFFFFFFU;
    Scene::Entity ignoreEntity{};
};

struct CapsuleCastQuery final {
    Math::Ray ray{};
    float radius = 0.0F;
    float height = 0.0F;
    float maxDistance = 0.0F;
    std::uint32_t layerMask = 0xFFFFFFFFU;
    Scene::Entity ignoreEntity{};
};

struct CapsuleCastHit final {
    Scene::Entity entity;
    /// Center of the moving local-Y capsule at first entry into the conservative expanded AABB.
    Math::Vector3 center;
    /// Axis-aligned face normal of the expanded AABB; zero when the ray starts inside it.
    Math::Vector3 normal;
    float distance = 0.0F;
    PhysicsMaterial material;
};

struct BoxCastHit final {
    Scene::Entity entity;
    /// Center of the moving axis-aligned box at first entry into the conservative expanded AABB.
    Math::Vector3 center;
    /// Axis-aligned face normal of the expanded AABB; zero when the ray starts inside it.
    Math::Vector3 normal;
    float distance = 0.0F;
    PhysicsMaterial material;
};

struct SphereCastHit final {
    Scene::Entity entity;
    /// Center of the moving sphere at first contact with the exact sphere/capsule target or the
    /// conservative expanded AABB fallback.
    Math::Vector3 center;
    /// Exact target-surface normal for sphere/capsule targets; axis-aligned fallback normal for an
    /// expanded AABB, and zero when the ray starts inside the target.
    Math::Vector3 normal;
    float distance = 0.0F;
    PhysicsMaterial material;
};

class ShapeCastSystem final {
public:
    [[nodiscard]] static std::optional<SphereCastHit> SphereCast(
        Scene::IEntityManager& entityManager, const SphereCastQuery& query);

    /// Conservative axis-aligned box cast over cached world AABBs. The mover half-extents expand
    /// each target AABB; this is read-only and deliberately does not claim an oriented/exact box cast.
    [[nodiscard]] static std::optional<BoxCastHit> BoxCast(
        Scene::IEntityManager& entityManager, const BoxCastQuery& query);

    /// Conservative axis-aligned local-Y capsule cast. The mover capsule's broad-phase half-extents
    /// expand each target AABB; this does not claim exact or rotated capsule geometry.
    [[nodiscard]] static std::optional<CapsuleCastHit> CapsuleCast(
        Scene::IEntityManager& entityManager, const CapsuleCastQuery& query);
};

} // namespace UVE::Physics
