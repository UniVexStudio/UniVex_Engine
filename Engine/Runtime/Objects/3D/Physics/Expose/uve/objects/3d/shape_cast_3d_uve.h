// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class ShapeCast3DShapeKindUVE : std::uint8_t {
    Sphere = 0,
    Box = 1,
    Capsule = 2,
};

/// Sweeps a 3D shape (`Sphere`, `Box`, or `Capsule`) along `direction` (in local object space,
/// rotated by the entity's `WorldTransformComponentUVE`) up to `maxDistance` each frame through
/// `Physics::ShapeCastSystemUVE`, recording the earliest obstacle hit in `hit`, `hitEntity`,
/// `hitCenter`, `hitNormal`, and `hitDistance`.
struct ShapeCast3DComponentUVE final {
    ShapeCast3DShapeKindUVE shapeKind = ShapeCast3DShapeKindUVE::Sphere;
    Math::Vector3UVE direction{0.0F, -1.0F, 0.0F};
    float maxDistance = 10.0F;
    float radius = 0.5F;
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    float height = 1.5F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    EntityUVE alsoIgnoreEntity = kInvalidEntityUVE;
    bool enabled = true;

    // Runtime-only result state, refreshed every frame by UpdateShapeCast3DObjectsUVE:
    bool hit = false;
    EntityUVE hitEntity = kInvalidEntityUVE;
    Math::Vector3UVE hitCenter{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE hitNormal{0.0F, 0.0F, 0.0F};
    float hitDistance = 0.0F;

    [[nodiscard]] bool operator==(const ShapeCast3DComponentUVE& other) const noexcept {
        return shapeKind == other.shapeKind && direction == other.direction &&
               maxDistance == other.maxDistance && radius == other.radius &&
               halfExtents == other.halfExtents && height == other.height &&
               collisionMask == other.collisionMask &&
               alsoIgnoreEntity == other.alsoIgnoreEntity && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsShapeCast3DComponentValidUVE(const ShapeCast3DComponentUVE& value) noexcept;

[[nodiscard]] ShapeCast3DComponentUVE SanitizeShapeCast3DComponentUVE(
    const ShapeCast3DComponentUVE& value) noexcept;

struct ShapeCast3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "ShapeCast3D";
    ShapeCast3DComponentUVE shapeCast{};
};

[[nodiscard]] bool IsShapeCast3DObjectDefinitionValidUVE(
    const ShapeCast3DObjectDefinitionUVE& value) noexcept;

void ApplyShapeCast3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const ShapeCast3DObjectDefinitionUVE& value);

/// Executes a single `ShapeCast3DComponentUVE` on `entity` against `Physics::ShapeCastSystemUVE`.
/// Returns true when the cast hit an obstacle.
bool StepShapeCast3DUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

/// Evaluates all `ShapeCast3DComponentUVE` entities in `entityManager` and returns the number of
/// casts that registered a hit.
std::size_t UpdateShapeCast3DObjectsUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
