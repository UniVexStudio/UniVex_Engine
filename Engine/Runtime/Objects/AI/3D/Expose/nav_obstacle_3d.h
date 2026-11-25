// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "entity.h"
#include "world_transform_component.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Dynamic navigation obstacle cylinder (`radius`, `height`, `velocity`) on `avoidanceLayers` that
/// repels nearby `NavSeeker3DComponent` agents during navigation steps.
struct NavigationObstacle3DComponent final {
    float radius = 0.75F;
    float height = 2.0F;
    Math::Vector3 velocity{0.0F, 0.0F, 0.0F};
    std::uint32_t avoidanceLayers = 1U;
    bool affectNavigationMesh = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationObstacle3DComponent&) const = default;
};

[[nodiscard]] bool IsNavigationObstacle3DComponentValid(
    const NavigationObstacle3DComponent& value) noexcept;

[[nodiscard]] NavigationObstacle3DComponent SanitizeNavigationObstacle3DComponent(
    const NavigationObstacle3DComponent& value) noexcept;

struct NavigationObstacle3DObjectDefinition final {
    static constexpr std::string_view defaultName = "NavigationObstacle3D";
    NavigationObstacle3DComponent obstacle{};
};

[[nodiscard]] bool IsNavigationObstacle3DObjectDefinitionValid(
    const NavigationObstacle3DObjectDefinition& value) noexcept;

void ApplyNavigationObstacle3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const NavigationObstacle3DObjectDefinition& value);

/// Off-mesh traversal link (`startOffset`, `endOffset` in local space) connecting two disconnected
/// navigation regions or ledges (`bidirectional`, `travelCostMultiplier`, `navigationLayers`).
struct NavigationLink3DComponent final {
    Math::Vector3 startOffset{0.0F, 0.0F, 0.0F};
    Math::Vector3 endOffset{0.0F, 0.0F, -2.0F};
    bool bidirectional = true;
    float travelCostMultiplier = 1.0F;
    std::uint32_t navigationLayers = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationLink3DComponent&) const = default;
};

[[nodiscard]] bool IsNavigationLink3DComponentValid(
    const NavigationLink3DComponent& value) noexcept;

[[nodiscard]] NavigationLink3DComponent SanitizeNavigationLink3DComponent(
    const NavigationLink3DComponent& value) noexcept;

struct NavigationLink3DObjectDefinition final {
    static constexpr std::string_view defaultName = "NavigationLink3D";
    NavigationLink3DComponent link{};
};

[[nodiscard]] bool IsNavigationLink3DObjectDefinitionValid(
    const NavigationLink3DObjectDefinition& value) noexcept;

void ApplyNavigationLink3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                              const NavigationLink3DObjectDefinition& value);

/// Resolves the world-space start and end endpoints of `link` under `worldTransform` and returns
/// the weighted traversal cost (`distance * travelCostMultiplier`).
[[nodiscard]] float ResolveNavigationLink3DWorldEndpointsAndCost(
    const NavigationLink3DComponent& link, const WorldTransformComponent& worldTransform,
    Math::Vector3& outWorldStart, Math::Vector3& outWorldEnd) noexcept;

/// Applies horizontal avoidance repulsion from all enabled `NavigationObstacle3DComponent`
/// entities onto active `NavSeeker3DComponent::desiredVelocity` vectors. Returns the number of
/// agents deflected.
std::size_t ApplyNavigationObstacles3DToAgents(IEntityManager& entityManager);

} // namespace UVE::Scene
