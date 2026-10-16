// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the PlaneMesh3D scene object: the component set and defaults a freshly
/// created PlaneMesh3D entity attaches — a ground-plane primitive mesh plus a thin flat collider
/// (`0.5 x 0.025 x 0.5`).
struct PlaneMesh3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Plane";

    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Plane, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.025F, 0.5F}};
};

[[nodiscard]] bool IsPlaneMesh3DObjectDefinitionValidUVE(const PlaneMesh3DObjectDefinitionUVE& value) noexcept;

void ApplyPlaneMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const PlaneMesh3DObjectDefinitionUVE& value);

/// Node policy for a `PlaneMesh3D` primitive object.
class PlaneMesh3DUVE final {
public:
    PlaneMesh3DUVE() = delete;

    static constexpr Math::Vector3UVE kDefaultColliderHalfExtentsUVE{0.5F, 0.025F, 0.5F};
    static constexpr Math::Vector3UVE kLocalNormalUVE{0.0F, 1.0F, 0.0F};

    /// True when `mesh` is valid and configured as a `Plane` primitive.
    [[nodiscard]] static bool IsPlaneMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept;

    /// Returns the authored floor-slab collider half-extents `{0.5, 0.025, 0.5}`.
    [[nodiscard]] static Math::Vector3UVE DefaultColliderHalfExtentsUVE() noexcept;

    /// Returns the local-space AABB enclosing the plane slab.
    [[nodiscard]] static Math::AabbUVE LocalBoundsUVE() noexcept;

    /// Resolves the plane's world-space surface normal (`+Y` rotated by `worldRotation`).
    /// Falls back to `{0, 1, 0}` if `worldRotation` cannot be normalized.
    [[nodiscard]] static Math::Vector3UVE WorldNormalUVE(
        const Math::QuaternionUVE& worldRotation) noexcept;

    /// Computes the world-space area of the unit XZ plane scaled by `worldScale`. Returns `0.0F`
    /// when `worldScale.x` or `worldScale.z` is non-finite or non-positive.
    [[nodiscard]] static float SurfaceAreaFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept;

    /// Synchronises `collider` half-extents to the thin floor-slab default.
    static void SyncColliderHalfExtentsUVE(ColliderComponentUVE& collider) noexcept;
};

} // namespace UVE::Scene
