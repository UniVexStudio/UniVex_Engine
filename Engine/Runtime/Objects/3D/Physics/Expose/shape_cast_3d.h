// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class ShapeCast3DShapeKind : std::uint8_t {
    Sphere = 0,
    Box = 1,
    Capsule = 2,
};

/// Sweeps a 3D shape (`Sphere`, `Box`, or `Capsule`) along `direction` (in local object space,
/// rotated by the entity's `WorldTransformComponent`) up to `maxDistance` each frame through
/// `Physics::ShapeCastSystem`, recording the earliest obstacle hit in `hit`, `hitEntity`,
/// `hitCenter`, `hitNormal`, and `hitDistance`.
struct ShapeCast3DComponent final {
    ShapeCast3DShapeKind shapeKind = ShapeCast3DShapeKind::Sphere;
    Math::Vector3 direction{0.0F, -1.0F, 0.0F};
    float maxDistance = 10.0F;
    float radius = 0.5F;
    Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
    float height = 1.5F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    Entity alsoIgnoreEntity = kInvalidEntity;
    bool enabled = true;

    // Runtime-only result state, refreshed every frame by UpdateShapeCast3DObjects:
    bool hit = false;
    Entity hitEntity = kInvalidEntity;
    Math::Vector3 hitCenter{0.0F, 0.0F, 0.0F};
    Math::Vector3 hitNormal{0.0F, 0.0F, 0.0F};
    float hitDistance = 0.0F;

    [[nodiscard]] bool operator==(const ShapeCast3DComponent& other) const noexcept {
        return shapeKind == other.shapeKind && direction == other.direction &&
               maxDistance == other.maxDistance && radius == other.radius &&
               halfExtents == other.halfExtents && height == other.height &&
               collisionMask == other.collisionMask &&
               alsoIgnoreEntity == other.alsoIgnoreEntity && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsShapeCast3DComponentValid(const ShapeCast3DComponent& value) noexcept;

[[nodiscard]] ShapeCast3DComponent SanitizeShapeCast3DComponent(
    const ShapeCast3DComponent& value) noexcept;

struct ShapeCast3DObjectDefinition final {
    static constexpr std::string_view defaultName = "ShapeCast3D";
    ShapeCast3DComponent shapeCast{};
};

[[nodiscard]] bool IsShapeCast3DObjectDefinitionValid(
    const ShapeCast3DObjectDefinition& value) noexcept;

void ApplyShapeCast3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const ShapeCast3DObjectDefinition& value);

/// Executes a single `ShapeCast3DComponent` on `entity` against `Physics::ShapeCastSystem`.
/// Returns true when the cast hit an obstacle.
bool StepShapeCast3D(IEntityManager& entityManager, Entity entity);

/// Evaluates all `ShapeCast3DComponent` entities in `entityManager` and returns the number of
/// casts that registered a hit.
std::size_t UpdateShapeCast3DObjects(IEntityManager& entityManager);

} // namespace UVE::Scene
