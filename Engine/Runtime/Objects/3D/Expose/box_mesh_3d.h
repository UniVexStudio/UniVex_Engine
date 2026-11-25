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

/// Authoring definition for the BoxMesh3D scene object: the component set and defaults a freshly
/// created BoxMesh3D entity attaches — a cube primitive mesh plus a matching box collider.
struct BoxMesh3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Cube";

    PrimitiveMeshComponent mesh{PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}};
    ColliderComponent collider{Math::Vector3{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsBoxMesh3DObjectDefinitionValid(const BoxMesh3DObjectDefinition& value) noexcept;

void ApplyBoxMesh3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const BoxMesh3DObjectDefinition& value);

/// Node policy for a `BoxMesh3D` primitive object.
class BoxMesh3D final {
public:
    BoxMesh3D() = delete;

    static constexpr Math::Vector3 kDefaultHalfExtents{0.5F, 0.5F, 0.5F};

    /// True when `mesh` is valid and configured as a `Cube` primitive.
    [[nodiscard]] static bool IsBoxMesh(const PrimitiveMeshComponent& mesh) noexcept;

    /// Returns the authored default box collider half-extents `{0.5, 0.5, 0.5}`.
    [[nodiscard]] static Math::Vector3 DefaultColliderHalfExtents() noexcept;

    /// Returns the unit cube's local-space AABB (`[-0.5, +0.5]^3`).
    [[nodiscard]] static Math::Aabb LocalBounds() noexcept;

    /// Computes the world-space volume of the unit cube scaled by `worldScale`. Returns `0.0F`
    /// when any scale axis is non-finite or non-positive.
    [[nodiscard]] static float VolumeFromScale(const Math::Vector3& worldScale) noexcept;

    /// Synchronises `collider` to the unit cube's box shape and default half-extents.
    static void SyncColliderToBox(ColliderComponent& collider) noexcept;
};

} // namespace UVE::Scene
