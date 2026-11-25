// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "world_transform_component.h"
#include "aabb.h"
#include "ray.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Static3D scene object: the component set and defaults a
/// freshly created Static3D entity attaches. The recipe itself is a plain default collider —
/// identical today to Collider3D's, because a collider-only entity (ColliderComponent with no
/// Rigid3DComponent) is exactly how this engine represents non-moving world geometry. The
/// kinds stay separate because their *intent* differs (a static body vs a bare shape), and when
/// static-body-specific defaults emerge, this file is their one home. The recipe used to be
/// hardcoded inline in Editor's creation switch; per Engine/Runtime/Scene/README.md's
/// "one truth per concept" rule this holds the *recipe*, not a second copy of component storage.
struct Static3DObjectDefinition final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty".)
    static constexpr std::string_view defaultName = "Static3D";

    /// Static-body authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponent collider{};
};

[[nodiscard]] bool IsStatic3DObjectDefinitionValid(const Static3DObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaseline - this kind is Object3D plus its recipe.
void ApplyStatic3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Static3DObjectDefinition& value);

class Static3D final {
public:
    [[nodiscard]] static bool IsImmovable() noexcept;
    [[nodiscard]] static float InverseMass() noexcept;
    [[nodiscard]] static Math::Vector3 ResolveZeroVelocity() noexcept;
    [[nodiscard]] static Math::Vector3 ResolveZeroAngularVelocity() noexcept;
    [[nodiscard]] static bool IsWorldGeometry(const IEntityManager& entityManager,
                                                 Entity entity) noexcept;
    [[nodiscard]] static Math::Aabb ComputeWorldAabb(
        const ColliderComponent& collider,
        const WorldTransformComponent& worldTransform) noexcept;
    [[nodiscard]] static bool ContainsWorldPoint(
        const ColliderComponent& collider,
        const WorldTransformComponent& worldTransform,
        const Math::Vector3& worldPoint) noexcept;
    [[nodiscard]] static std::optional<Math::RayHit> IntersectWorldRay(
        const ColliderComponent& collider,
        const WorldTransformComponent& worldTransform,
        const Math::Ray& ray, float maxDistance) noexcept;
};

} // namespace UVE::Scene
