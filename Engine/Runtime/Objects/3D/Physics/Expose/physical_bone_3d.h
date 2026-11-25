// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "entity.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Ragdoll physics bone component (`skeletonEntity`, `boneIndex`, `mass`, `linearDamping`,
/// `simulatePhysics`) driven by or driving a bone on a `Skeleton3DComponent`.
struct PhysicalBone3DComponent final {
    Entity skeletonEntity = kInvalidEntity;
    std::int32_t boneIndex = 0;
    float mass = 5.0F;
    float linearDamping = 0.1F;
    bool simulatePhysics = false;
    Math::Vector3 linearVelocity{0.0F, 0.0F, 0.0F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const PhysicalBone3DComponent&) const = default;
};

[[nodiscard]] bool IsPhysicalBone3DComponentValid(
    const PhysicalBone3DComponent& value) noexcept;

[[nodiscard]] PhysicalBone3DComponent SanitizePhysicalBone3DComponent(
    const PhysicalBone3DComponent& value) noexcept;

struct PhysicalBone3DObjectDefinition final {
    static constexpr std::string_view defaultName = "PhysicalBone3D";
    PhysicalBone3DComponent physicalBone{};
};

[[nodiscard]] bool IsPhysicalBone3DObjectDefinitionValid(
    const PhysicalBone3DObjectDefinition& value) noexcept;

void ApplyPhysicalBone3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const PhysicalBone3DObjectDefinition& value);

/// Steps all enabled `PhysicalBone3DComponent` entities by `deltaTimeSeconds` under `gravity`.
/// When `simulatePhysics` is false, snaps the bone entity's local position to the target skeleton
/// bone's pose; when `simulatePhysics` is true, integrates `linearVelocity` and writes the pose
/// back to the skeleton bone.
std::size_t UpdatePhysicalBone3DObjects(IEntityManager& entityManager,
                                           const Math::Vector3& gravity, float deltaTimeSeconds);

} // namespace UVE::Scene
