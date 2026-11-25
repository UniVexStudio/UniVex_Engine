// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "entity.h"
#include "aabb.h"
#include "object_3d_common.h"
#include "object_3d.h"

namespace UVE::Scene {

class IEntityManager;

struct NavMeshVolume3DComponent final {
    Math::Vector3 boundsHalfExtents{10.0F, 2.0F, 10.0F};
    std::string navigationMeshAssetPath;
    std::uint32_t navigationLayers = 1U;

    // What the region is baked for. These are agent measurements rather than mesh ones - the grid
    // the volume is rasterized on, the agent's width used to erode walls and ledges away, its height
    // for headroom, how steep a surface it can climb and how tall a step it can take. Two agent
    // types on one platform need two regions, because a single mesh can only be eroded for one
    // width.
    float cellSize = 0.5F;
    float agentRadius = 0.5F;
    float agentHeight = 1.8F;
    float maximumSlopeDegrees = 45.0F;
    float maximumStepHeight = 0.4F;

    bool enabled = true;
    /// Set from a script, the editor or a tool to ask for a re-rasterization: the navigation runtime
    /// clears it once the bake has run. An author does not need to raise it for a region that was
    /// moved or resized - a bake follows its bounds - but the collision world moving without the
    /// region moving is what it is for.
    bool rebuildRequested = false;

    [[nodiscard]] bool operator==(const NavMeshVolume3DComponent&) const = default;
};

[[nodiscard]] bool IsNavMeshVolume3DObjectComponentValid(const NavMeshVolume3DComponent& value) noexcept;

/// NavMeshVolume3D's creation recipe — the Object3D baseline plus a default
/// `NavMeshVolume3DComponent`.
struct NavMeshVolume3DObjectDefinition final {
    static constexpr std::string_view typeName = "NavMeshVolume3D";
    static constexpr std::string_view defaultName = "NavMeshVolume3D";

    NavMeshVolume3DComponent volume{};

    [[nodiscard]] bool operator==(const NavMeshVolume3DObjectDefinition&) const = default;
};

[[nodiscard]] bool IsNavMeshVolume3DObjectDefinitionValid(
    const NavMeshVolume3DObjectDefinition& value) noexcept;

void ApplyNavMeshVolume3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                             const NavMeshVolume3DObjectDefinition& value);

/// Node policy for a `NavMeshVolume3D` navigation region object.
///
/// Owns world-space AABB construction, point-to-volume distance queries, navigation-layer bitmask
/// matching, and rebuild-request latching shared by `NavigationRuntime` and scene tools.
class NavMeshVolume3D final {
public:
    NavMeshVolume3D() = delete;

    /// True when `volume` is enabled and passes `IsNavMeshVolume3DObjectComponentValid`.
    [[nodiscard]] static bool IsBakingEnabled(const NavMeshVolume3DComponent& volume) noexcept;

    /// Builds the axis-aligned world-space volume around `worldPosition` using
    /// `volume.boundsHalfExtents`.
    [[nodiscard]] static Math::Aabb ResolveWorldBounds(
        const NavMeshVolume3DComponent& volume, const Math::Vector3& worldPosition) noexcept;

    /// Euclidean distance in metres from `point` to `bounds` (`0.0F` when `point` lies on or
    /// inside `bounds`).
    [[nodiscard]] static float DistanceToVolumeBounds(
        const Math::Aabb& bounds, const Math::Vector3& point) noexcept;

    /// True when `point` lies on or inside `volume`'s world-space AABB around `worldPosition`.
    [[nodiscard]] static bool ContainsWorldPoint(
        const NavMeshVolume3DComponent& volume, const Math::Vector3& worldPosition,
        const Math::Vector3& point) noexcept;

    /// True when `volume.navigationLayers` shares at least one bit with `layerMask`.
    [[nodiscard]] static bool SharesNavigationLayer(
        const NavMeshVolume3DComponent& volume, std::uint32_t layerMask) noexcept;

    /// Flags `volume.rebuildRequested = true`.
    static void RequestRebuild(NavMeshVolume3DComponent& volume) noexcept;

    /// Reads and clears `volume.rebuildRequested`.
    static bool ConsumeRebuildRequest(NavMeshVolume3DComponent& volume) noexcept;
};

} // namespace UVE::Scene
