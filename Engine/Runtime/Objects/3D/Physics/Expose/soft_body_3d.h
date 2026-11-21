// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "entity.h"
#include "aabb.h"
#include "vector2.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumSoftBody3DPointsUVE = 4096U;

struct SoftBody3DPointUVE final {
    Math::Vector3UVE position{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE velocity{0.0F, 0.0F, 0.0F};
    bool pinned = false;

    [[nodiscard]] bool operator==(const SoftBody3DPointUVE&) const = default;
};

struct SoftBodySphereColliderUVE final {
    Math::Vector3UVE center{0.0F, 0.0F, 0.0F};
    float radius = 0.5F;
};

struct SoftBodyAabbColliderUVE final {
    Math::AabbUVE bounds{};
};

/// Deformable XPBD cloth / soft-body physics component built on `SurfaceInstance3DBaseUVE`.
/// When `gridWidth >= 2 && gridHeight >= 2 && points.size() == gridWidth * gridHeight`, solves
/// structural, shear, and bending XPBD distance constraints; otherwise solves a 1D chain.
struct SoftBody3DComponentUVE final {
    std::vector<SoftBody3DPointUVE> points{};
    std::uint32_t gridWidth = 0U;
    std::uint32_t gridHeight = 0U;
    std::uint32_t substeps = 4U;
    float stiffness = 0.8F;
    float compliance = 0.0005F;
    float shearCompliance = 0.002F;
    float bendCompliance = 0.02F;
    float damping = 0.05F;
    float restSegmentLength = 0.5F;
    float gravityScale = 1.0F;
    Math::Vector3UVE windVelocity{0.0F, 0.0F, 0.0F};
    float windDrag = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const SoftBody3DComponentUVE&) const = default;
};

[[nodiscard]] SoftBody3DComponentUVE BuildClothGridSoftBody3DUVE(
    std::uint32_t width, std::uint32_t height, float spacingMetres,
    const Math::Vector3UVE& origin = Math::Vector3UVE{0.0F, 0.0F, 0.0F},
    bool pinTopRow = true);

[[nodiscard]] bool IsSoftBody3DComponentValidUVE(const SoftBody3DComponentUVE& value) noexcept;

[[nodiscard]] SoftBody3DComponentUVE SanitizeSoftBody3DComponentUVE(
    const SoftBody3DComponentUVE& value);

struct SoftBody3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SoftBody3D";
    SoftBody3DComponentUVE softBody{};
};

[[nodiscard]] bool IsSoftBody3DObjectDefinitionValidUVE(
    const SoftBody3DObjectDefinitionUVE& value) noexcept;

void ApplySoftBody3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const SoftBody3DObjectDefinitionUVE& value);

/// Steps all enabled `SoftBody3DComponentUVE` entities by `deltaTimeSeconds` under `gravity`
/// with XPBD constraint solving and optional sphere/AABB collision resolution.
std::size_t UpdateSoftBody3DObjectsUVE(IEntityManagerUVE& entityManager,
                                       const Math::Vector3UVE& gravity, float deltaTimeSeconds,
                                       std::span<const SoftBodySphereColliderUVE> sphereColliders,
                                       std::span<const SoftBodyAabbColliderUVE> boxColliders = {});

/// Steps all enabled `SoftBody3DComponentUVE` entities by `deltaTimeSeconds` under `gravity`.
std::size_t UpdateSoftBody3DObjectsUVE(IEntityManagerUVE& entityManager,
                                       const Math::Vector3UVE& gravity, float deltaTimeSeconds);

/// Extruded 2D polygon collision shape (`vertices2D`, `depthMetres`) along local Z
/// `[-0.5*depthMetres, +0.5*depthMetres]`.
struct CollisionPolygon3DComponentUVE final {
    std::vector<Math::Vector2UVE> vertices2D{};
    float depthMetres = 1.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CollisionPolygon3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsCollisionPolygon3DComponentValidUVE(
    const CollisionPolygon3DComponentUVE& value) noexcept;

[[nodiscard]] CollisionPolygon3DComponentUVE SanitizeCollisionPolygon3DComponentUVE(
    const CollisionPolygon3DComponentUVE& value);

struct CollisionPolygon3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CollisionPolygon3D";
    CollisionPolygon3DComponentUVE polygon{};
};

[[nodiscard]] bool IsCollisionPolygon3DObjectDefinitionValidUVE(
    const CollisionPolygon3DObjectDefinitionUVE& value) noexcept;

void ApplyCollisionPolygon3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const CollisionPolygon3DObjectDefinitionUVE& value);

class CollisionPolygon3DUVE final {
public:
    CollisionPolygon3DUVE() = delete;

    [[nodiscard]] static Math::AabbUVE ComputeLocalAabbUVE(
        const CollisionPolygon3DComponentUVE& polygon) noexcept;

    [[nodiscard]] static bool ContainsLocalPointUVE(
        const CollisionPolygon3DComponentUVE& polygon,
        const Math::Vector3UVE& localPoint) noexcept;
};

} // namespace UVE::Scene
