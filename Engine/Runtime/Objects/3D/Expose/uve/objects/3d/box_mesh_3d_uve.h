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

/// Authoring definition for the BoxMesh3D scene object: the component set and defaults a freshly
/// created BoxMesh3D entity attaches — a cube primitive mesh plus a matching box collider.
struct BoxMesh3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Cube";

    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsBoxMesh3DObjectDefinitionValidUVE(const BoxMesh3DObjectDefinitionUVE& value) noexcept;

void ApplyBoxMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const BoxMesh3DObjectDefinitionUVE& value);

/// Node policy for a `BoxMesh3D` primitive object.
class BoxMesh3DUVE final {
public:
    BoxMesh3DUVE() = delete;

    static constexpr Math::Vector3UVE kDefaultHalfExtentsUVE{0.5F, 0.5F, 0.5F};

    /// True when `mesh` is valid and configured as a `Cube` primitive.
    [[nodiscard]] static bool IsBoxMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept;

    /// Returns the authored default box collider half-extents `{0.5, 0.5, 0.5}`.
    [[nodiscard]] static Math::Vector3UVE DefaultColliderHalfExtentsUVE() noexcept;

    /// Returns the unit cube's local-space AABB (`[-0.5, +0.5]^3`).
    [[nodiscard]] static Math::AabbUVE LocalBoundsUVE() noexcept;

    /// Computes the world-space volume of the unit cube scaled by `worldScale`. Returns `0.0F`
    /// when any scale axis is non-finite or non-positive.
    [[nodiscard]] static float VolumeFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept;

    /// Synchronises `collider` to the unit cube's box shape and default half-extents.
    static void SyncColliderToBoxUVE(ColliderComponentUVE& collider) noexcept;
};

} // namespace UVE::Scene
