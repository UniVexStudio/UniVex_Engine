// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "primitive_mesh_component.h"
#include "aabb.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the PlaneMesh3D scene object: the component set and defaults a freshly
/// created PlaneMesh3D entity attaches — a ground-plane primitive mesh plus a thin flat collider
/// (`0.5 x 0.025 x 0.5`).
struct PlaneMesh3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Plane";

    PrimitiveMeshComponent mesh{PrimitiveMeshKind::Plane, Math::Vector3{1.0F, 1.0F, 1.0F}};
    ColliderComponent collider{Math::Vector3{0.5F, 0.025F, 0.5F}};
};

[[nodiscard]] bool IsPlaneMesh3DObjectDefinitionValid(const PlaneMesh3DObjectDefinition& value) noexcept;

void ApplyPlaneMesh3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const PlaneMesh3DObjectDefinition& value);

/// Node policy for a `PlaneMesh3D` primitive object.
class PlaneMesh3D final {
public:
    PlaneMesh3D() = delete;

    static constexpr Math::Vector3 kDefaultColliderHalfExtents{0.5F, 0.025F, 0.5F};
    static constexpr Math::Vector3 kLocalNormal{0.0F, 1.0F, 0.0F};

    /// True when `mesh` is valid and configured as a `Plane` primitive.
    [[nodiscard]] static bool IsPlaneMesh(const PrimitiveMeshComponent& mesh) noexcept;

    /// Returns the authored floor-slab collider half-extents `{0.5, 0.025, 0.5}`.
    [[nodiscard]] static Math::Vector3 DefaultColliderHalfExtents() noexcept;

    /// Returns the local-space AABB enclosing the plane slab.
    [[nodiscard]] static Math::Aabb LocalBounds() noexcept;

    /// Resolves the plane's world-space surface normal (`+Y` rotated by `worldRotation`).
    /// Falls back to `{0, 1, 0}` if `worldRotation` cannot be normalized.
    [[nodiscard]] static Math::Vector3 WorldNormal(
        const Math::Quaternion& worldRotation) noexcept;

    /// Computes the world-space area of the unit XZ plane scaled by `worldScale`. Returns `0.0F`
    /// when `worldScale.x` or `worldScale.z` is non-finite or non-positive.
    [[nodiscard]] static float SurfaceAreaFromScale(const Math::Vector3& worldScale) noexcept;

    /// Synchronises `collider` half-extents to the thin floor-slab default.
    static void SyncColliderHalfExtents(ColliderComponent& collider) noexcept;
};

} // namespace UVE::Scene
