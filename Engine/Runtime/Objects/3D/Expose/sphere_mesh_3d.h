// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "primitive_mesh_component.h"
#include "aabb.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the SphereMesh3D scene object: the component set and defaults a
/// freshly created SphereMesh3D entity attaches — a UV-sphere primitive mesh plus a conservative
/// box collider (`{0.5, 0.5, 0.5}`).
struct SphereMesh3DObjectDefinition final {
    static constexpr std::string_view defaultName = "UV Sphere";

    PrimitiveMeshComponent mesh{PrimitiveMeshKind::UVSphere, Math::Vector3{1.0F, 1.0F, 1.0F}};
    ColliderComponent collider{Math::Vector3{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsSphereMesh3DObjectDefinitionValid(const SphereMesh3DObjectDefinition& value) noexcept;

void ApplySphereMesh3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const SphereMesh3DObjectDefinition& value);

/// Node policy for a `SphereMesh3D` primitive object.
class SphereMesh3D final {
public:
    SphereMesh3D() = delete;

    static constexpr float kDefaultRadius = 0.5F;
    static constexpr Math::Vector3 kDefaultHalfExtents{0.5F, 0.5F, 0.5F};

    /// True when `mesh` is valid and configured as a `UVSphere` primitive.
    [[nodiscard]] static bool IsSphereMesh(const PrimitiveMeshComponent& mesh) noexcept;

    /// Returns the authored default collider half-extents `{0.5, 0.5, 0.5}`.
    [[nodiscard]] static Math::Vector3 DefaultColliderHalfExtents() noexcept;

    /// Returns the unit UV sphere's local-space AABB (`[-0.5, +0.5]^3`).
    [[nodiscard]] static Math::Aabb LocalBounds() noexcept;

    /// Computes the effective world-space bounding radius for a unit sphere scaled by
    /// `worldScale` (using the maximum positive axis scale). Returns `0.0F` if invalid.
    [[nodiscard]] static float EffectiveRadiusFromScale(const Math::Vector3& worldScale) noexcept;

    /// Synchronises `collider` half-extents to the unit sphere's default bounds.
    static void SyncColliderHalfExtents(ColliderComponent& collider) noexcept;
};

} // namespace UVE::Scene
