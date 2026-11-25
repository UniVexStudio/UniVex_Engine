// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "aabb.h"
#include "quaternion.h"
#include "ray.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

struct Collider3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Collision Box";
    ColliderComponent collider{};
};

[[nodiscard]] bool IsCollider3DObjectDefinitionValid(const Collider3DObjectDefinition& value) noexcept;

void ApplyCollider3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Collider3DObjectDefinition& value);

class Collider3D final {
public:
    [[nodiscard]] static ColliderComponent MakeBox(const Math::Vector3& halfExtents) noexcept;
    [[nodiscard]] static ColliderComponent MakeSphere(float radius) noexcept;
    [[nodiscard]] static ColliderComponent MakeCapsule(float radius, float height) noexcept;

    [[nodiscard]] static bool IsParticipating(const ColliderComponent& collider) noexcept;
    [[nodiscard]] static Math::Aabb GetLocalAabb(const ColliderComponent& collider) noexcept;
    /// World bounds of the shape a mesh of `worldScale` actually occupies.
    ///
    /// The scale is a parameter rather than a field read off the collider because a collider does
    /// not know where it lives: the same authored shape describes a pebble or a wall depending on
    /// the transform above it. Omitting it is what let a 500x floor keep a 1x collider.
    [[nodiscard]] static Math::Aabb GetWorldAabb(const ColliderComponent& collider,
                                                       const Math::Vector3& center,
                                                       const Math::Quaternion& rotation,
                                                       const Math::Vector3& worldScale) noexcept;
    [[nodiscard]] static float GetVolume(const ColliderComponent& collider) noexcept;

    [[nodiscard]] static bool ContainsPoint(const ColliderComponent& collider,
                                               const Math::Vector3& center,
                                               const Math::Quaternion& rotation,
                                               const Math::Vector3& point) noexcept;
    [[nodiscard]] static std::optional<Math::Vector3> ClosestPoint(
        const ColliderComponent& collider, const Math::Vector3& center,
        const Math::Quaternion& rotation, const Math::Vector3& point) noexcept;
    [[nodiscard]] static std::optional<Math::RayHit> IntersectRay(
        const ColliderComponent& collider, const Math::Vector3& center,
        const Math::Quaternion& rotation, const Math::Ray& ray, float maxDistance) noexcept;
};

} // namespace UVE::Scene
