// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Ragdoll physics bone component (`skeletonEntity`, `boneIndex`, `mass`, `linearDamping`,
/// `simulatePhysics`) driven by or driving a bone on a `Skeleton3DComponentUVE`.
struct PhysicalBone3DComponentUVE final {
    EntityUVE skeletonEntity = kInvalidEntityUVE;
    std::int32_t boneIndex = 0;
    float mass = 5.0F;
    float linearDamping = 0.1F;
    bool simulatePhysics = false;
    Math::Vector3UVE linearVelocity{0.0F, 0.0F, 0.0F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const PhysicalBone3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPhysicalBone3DComponentValidUVE(
    const PhysicalBone3DComponentUVE& value) noexcept;

[[nodiscard]] PhysicalBone3DComponentUVE SanitizePhysicalBone3DComponentUVE(
    const PhysicalBone3DComponentUVE& value) noexcept;

struct PhysicalBone3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "PhysicalBone3D";
    PhysicalBone3DComponentUVE physicalBone{};
};

[[nodiscard]] bool IsPhysicalBone3DObjectDefinitionValidUVE(
    const PhysicalBone3DObjectDefinitionUVE& value) noexcept;

void ApplyPhysicalBone3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const PhysicalBone3DObjectDefinitionUVE& value);

/// Steps all enabled `PhysicalBone3DComponentUVE` entities by `deltaTimeSeconds` under `gravity`.
/// When `simulatePhysics` is false, snaps the bone entity's local position to the target skeleton
/// bone's pose; when `simulatePhysics` is true, integrates `linearVelocity` and writes the pose
/// back to the skeleton bone.
std::size_t UpdatePhysicalBone3DObjectsUVE(IEntityManagerUVE& entityManager,
                                           const Math::Vector3UVE& gravity, float deltaTimeSeconds);

} // namespace UVE::Scene
