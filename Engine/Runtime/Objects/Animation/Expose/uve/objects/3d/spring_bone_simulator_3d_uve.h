// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Spherical obstacle volume in world space (`worldPosition + Rotate(worldRotation, centerOffset)`)
/// that pushes simulated spring-bone tips outside its surface.
struct SpringBoneCollision3DComponentUVE final {
    Math::Vector3UVE centerOffset{0.0F, 0.0F, 0.0F};
    float radius = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const SpringBoneCollision3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsSpringBoneCollision3DComponentValidUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept;

[[nodiscard]] SpringBoneCollision3DComponentUVE SanitizeSpringBoneCollision3DComponentUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept;

struct SpringBoneCollision3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SpringBoneCollision3D";
    SpringBoneCollision3DComponentUVE collision{};
};

[[nodiscard]] bool IsSpringBoneCollision3DObjectDefinitionValidUVE(
    const SpringBoneCollision3DObjectDefinitionUVE& value) noexcept;

void ApplySpringBoneCollision3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const SpringBoneCollision3DObjectDefinitionUVE& value);

/// Secondary-motion ("jiggle bone") spring simulator for a bone on a target `Skeleton3DComponentUVE`
/// or a standalone transform node. Simulates a virtual tip at `tipOffsetLocal` from the joint pivot
/// with spring `stiffness`, `damping`, and `gravity`, pushing the tip outside any enabled
/// `SpringBoneCollision3DComponentUVE` sphere and rotating the bone toward the displaced tip.
struct SpringBoneSimulator3DComponentUVE final {
    EntityUVE skeleton = kInvalidEntityUVE;
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
    std::string boneName{};
    Math::Vector3UVE tipOffsetLocal{0.0F, -0.25F, 0.0F};
    float stiffness = 15.0F;
    float damping = 0.35F;
    float gravityScale = 0.0F;
    Math::Vector3UVE gravityDirection{0.0F, -1.0F, 0.0F};
    float hitRadius = 0.05F;
    bool enabled = true;

    // Runtime state, never persisted:
    bool initialized = false;
    Math::Vector3UVE currentTipWorldPosition{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE tipVelocity{0.0F, 0.0F, 0.0F};

    [[nodiscard]] bool operator==(const SpringBoneSimulator3DComponentUVE& other) const noexcept {
        return skeleton == other.skeleton && boneIndex == other.boneIndex &&
               boneName == other.boneName && tipOffsetLocal == other.tipOffsetLocal &&
               stiffness == other.stiffness && damping == other.damping &&
               gravityScale == other.gravityScale &&
               gravityDirection == other.gravityDirection && hitRadius == other.hitRadius &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsSpringBoneSimulator3DComponentValidUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept;

[[nodiscard]] SpringBoneSimulator3DComponentUVE SanitizeSpringBoneSimulator3DComponentUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept;

/// Integrates `simulator`'s virtual tip toward `restTipWorldPosition` while keeping a fixed link
/// length `|tipOffsetLocal|` from `pivotWorldPosition`.
void StepSpringBoneTipStateUVE(SpringBoneSimulator3DComponentUVE& simulator,
                               const Math::Vector3UVE& pivotWorldPosition,
                               const Math::Vector3UVE& restTipWorldPosition,
                               float deltaTimeSeconds) noexcept;

/// Pushes `simulator.currentTipWorldPosition` outside the sphere `(sphereCenterWorld, sphereRadius)`
/// while maintaining the link length from `pivotWorldPosition`. Returns true if a collision occurred.
bool ResolveSpringBoneSphereCollisionUVE(SpringBoneSimulator3DComponentUVE& simulator,
                                         const Math::Vector3UVE& pivotWorldPosition,
                                         const Math::Vector3UVE& sphereCenterWorld,
                                         float sphereRadius) noexcept;

struct SpringBoneSimulator3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SpringBoneSimulator3D";
    SpringBoneSimulator3DComponentUVE simulator{};
};

[[nodiscard]] bool IsSpringBoneSimulator3DObjectDefinitionValidUVE(
    const SpringBoneSimulator3DObjectDefinitionUVE& value) noexcept;

void ApplySpringBoneSimulator3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const SpringBoneSimulator3DObjectDefinitionUVE& value);

/// Steps all enabled `SpringBoneSimulator3DComponentUVE` entities against all enabled
/// `SpringBoneCollision3DComponentUVE` obstacles and returns the number of simulators stepped.
std::size_t UpdateSpringBoneSimulator3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                  float deltaTimeSeconds);

} // namespace UVE::Scene
