// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the SphereMesh3D scene object: the component set and defaults a
/// freshly created SphereMesh3D entity attaches — a UV-sphere primitive mesh plus a conservative
/// box collider (`{0.5, 0.5, 0.5}`).
struct SphereMesh3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UV Sphere";

    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsSphereMesh3DObjectDefinitionValidUVE(const SphereMesh3DObjectDefinitionUVE& value) noexcept;

void ApplySphereMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const SphereMesh3DObjectDefinitionUVE& value);

/// Node policy for a `SphereMesh3D` primitive object.
class SphereMesh3DUVE final {
public:
    SphereMesh3DUVE() = delete;

    static constexpr float kDefaultRadiusUVE = 0.5F;
    static constexpr Math::Vector3UVE kDefaultHalfExtentsUVE{0.5F, 0.5F, 0.5F};

    /// True when `mesh` is valid and configured as a `UVSphere` primitive.
    [[nodiscard]] static bool IsSphereMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept;

    /// Returns the authored default collider half-extents `{0.5, 0.5, 0.5}`.
    [[nodiscard]] static Math::Vector3UVE DefaultColliderHalfExtentsUVE() noexcept;

    /// Returns the unit UV sphere's local-space AABB (`[-0.5, +0.5]^3`).
    [[nodiscard]] static Math::AabbUVE LocalBoundsUVE() noexcept;

    /// Computes the effective world-space bounding radius for a unit sphere scaled by
    /// `worldScale` (using the maximum positive axis scale). Returns `0.0F` if invalid.
    [[nodiscard]] static float EffectiveRadiusFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept;

    /// Synchronises `collider` half-extents to the unit sphere's default bounds.
    static void SyncColliderHalfExtentsUVE(ColliderComponentUVE& collider) noexcept;
};

} // namespace UVE::Scene
