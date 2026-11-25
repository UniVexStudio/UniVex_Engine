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

class IEntityManager;

inline constexpr std::size_t kMaximumSoftBody3DPoints = 4096U;

struct SoftBody3DPoint final {
    Math::Vector3 position{0.0F, 0.0F, 0.0F};
    Math::Vector3 velocity{0.0F, 0.0F, 0.0F};
    bool pinned = false;

    [[nodiscard]] bool operator==(const SoftBody3DPoint&) const = default;
};

struct SoftBodySphereCollider final {
    Math::Vector3 center{0.0F, 0.0F, 0.0F};
    float radius = 0.5F;
};

struct SoftBodyAabbCollider final {
    Math::Aabb bounds{};
};

/// Deformable XPBD cloth / soft-body physics component built on `SurfaceInstance3DBase`.
/// When `gridWidth >= 2 && gridHeight >= 2 && points.size() == gridWidth * gridHeight`, solves
/// structural, shear, and bending XPBD distance constraints; otherwise solves a 1D chain.
struct SoftBody3DComponent final {
    std::vector<SoftBody3DPoint> points{};
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
    Math::Vector3 windVelocity{0.0F, 0.0F, 0.0F};
    float windDrag = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const SoftBody3DComponent&) const = default;
};

[[nodiscard]] SoftBody3DComponent BuildClothGridSoftBody3D(
    std::uint32_t width, std::uint32_t height, float spacingMetres,
    const Math::Vector3& origin = Math::Vector3{0.0F, 0.0F, 0.0F},
    bool pinTopRow = true);

[[nodiscard]] bool IsSoftBody3DComponentValid(const SoftBody3DComponent& value) noexcept;

[[nodiscard]] SoftBody3DComponent SanitizeSoftBody3DComponent(
    const SoftBody3DComponent& value);

struct SoftBody3DObjectDefinition final {
    static constexpr std::string_view defaultName = "SoftBody3D";
    SoftBody3DComponent softBody{};
};

[[nodiscard]] bool IsSoftBody3DObjectDefinitionValid(
    const SoftBody3DObjectDefinition& value) noexcept;

void ApplySoftBody3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const SoftBody3DObjectDefinition& value);

/// Steps all enabled `SoftBody3DComponent` entities by `deltaTimeSeconds` under `gravity`
/// with XPBD constraint solving and optional sphere/AABB collision resolution.
std::size_t UpdateSoftBody3DObjects(IEntityManager& entityManager,
                                       const Math::Vector3& gravity, float deltaTimeSeconds,
                                       std::span<const SoftBodySphereCollider> sphereColliders,
                                       std::span<const SoftBodyAabbCollider> boxColliders = {});

/// Steps all enabled `SoftBody3DComponent` entities by `deltaTimeSeconds` under `gravity`.
std::size_t UpdateSoftBody3DObjects(IEntityManager& entityManager,
                                       const Math::Vector3& gravity, float deltaTimeSeconds);

/// Extruded 2D polygon collision shape (`vertices2D`, `depthMetres`) along local Z
/// `[-0.5*depthMetres, +0.5*depthMetres]`.
struct CollisionPolygon3DComponent final {
    std::vector<Math::Vector2> vertices2D{};
    float depthMetres = 1.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CollisionPolygon3DComponent&) const = default;
};

[[nodiscard]] bool IsCollisionPolygon3DComponentValid(
    const CollisionPolygon3DComponent& value) noexcept;

[[nodiscard]] CollisionPolygon3DComponent SanitizeCollisionPolygon3DComponent(
    const CollisionPolygon3DComponent& value);

struct CollisionPolygon3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CollisionPolygon3D";
    CollisionPolygon3DComponent polygon{};
};

[[nodiscard]] bool IsCollisionPolygon3DObjectDefinitionValid(
    const CollisionPolygon3DObjectDefinition& value) noexcept;

void ApplyCollisionPolygon3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const CollisionPolygon3DObjectDefinition& value);

class CollisionPolygon3D final {
public:
    CollisionPolygon3D() = delete;

    [[nodiscard]] static Math::Aabb ComputeLocalAabb(
        const CollisionPolygon3DComponent& polygon) noexcept;

    [[nodiscard]] static bool ContainsLocalPoint(
        const CollisionPolygon3DComponent& polygon,
        const Math::Vector3& localPoint) noexcept;
};

} // namespace UVE::Scene
