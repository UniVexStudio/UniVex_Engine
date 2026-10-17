// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Dynamic navigation obstacle cylinder (`radius`, `height`, `velocity`) on `avoidanceLayers` that
/// repels nearby `NavSeeker3DComponentUVE` agents during navigation steps.
struct NavigationObstacle3DComponentUVE final {
    float radius = 0.75F;
    float height = 2.0F;
    Math::Vector3UVE velocity{0.0F, 0.0F, 0.0F};
    std::uint32_t avoidanceLayers = 1U;
    bool affectNavigationMesh = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationObstacle3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsNavigationObstacle3DComponentValidUVE(
    const NavigationObstacle3DComponentUVE& value) noexcept;

[[nodiscard]] NavigationObstacle3DComponentUVE SanitizeNavigationObstacle3DComponentUVE(
    const NavigationObstacle3DComponentUVE& value) noexcept;

struct NavigationObstacle3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationObstacle3D";
    NavigationObstacle3DComponentUVE obstacle{};
};

[[nodiscard]] bool IsNavigationObstacle3DObjectDefinitionValidUVE(
    const NavigationObstacle3DObjectDefinitionUVE& value) noexcept;

void ApplyNavigationObstacle3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const NavigationObstacle3DObjectDefinitionUVE& value);

/// Off-mesh traversal link (`startOffset`, `endOffset` in local space) connecting two disconnected
/// navigation regions or ledges (`bidirectional`, `travelCostMultiplier`, `navigationLayers`).
struct NavigationLink3DComponentUVE final {
    Math::Vector3UVE startOffset{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE endOffset{0.0F, 0.0F, -2.0F};
    bool bidirectional = true;
    float travelCostMultiplier = 1.0F;
    std::uint32_t navigationLayers = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationLink3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsNavigationLink3DComponentValidUVE(
    const NavigationLink3DComponentUVE& value) noexcept;

[[nodiscard]] NavigationLink3DComponentUVE SanitizeNavigationLink3DComponentUVE(
    const NavigationLink3DComponentUVE& value) noexcept;

struct NavigationLink3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationLink3D";
    NavigationLink3DComponentUVE link{};
};

[[nodiscard]] bool IsNavigationLink3DObjectDefinitionValidUVE(
    const NavigationLink3DObjectDefinitionUVE& value) noexcept;

void ApplyNavigationLink3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const NavigationLink3DObjectDefinitionUVE& value);

/// Resolves the world-space start and end endpoints of `link` under `worldTransform` and returns
/// the weighted traversal cost (`distance * travelCostMultiplier`).
[[nodiscard]] float ResolveNavigationLink3DWorldEndpointsAndCostUVE(
    const NavigationLink3DComponentUVE& link, const WorldTransformComponentUVE& worldTransform,
    Math::Vector3UVE& outWorldStart, Math::Vector3UVE& outWorldEnd) noexcept;

/// Applies horizontal avoidance repulsion from all enabled `NavigationObstacle3DComponentUVE`
/// entities onto active `NavSeeker3DComponentUVE::desiredVelocity` vectors. Returns the number of
/// agents deflected.
std::size_t ApplyNavigationObstacles3DToAgentsUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
