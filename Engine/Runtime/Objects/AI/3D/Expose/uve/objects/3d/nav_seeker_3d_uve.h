// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// What the agent's own steering last reported. Runtime state: written by the navigation step, never
/// authored and never saved - the route is a fact about the scene as it stands, not about the file.
enum class NavigationAgentPathStatusUVE : std::uint8_t {
    /// No target, or the agent is switched off.
    Idle = 0,
    /// Walking the route towards `nextPathPosition`.
    Following,
    /// Standing on the target, inside its own tolerance.
    Finished,
    /// Nothing walkable to follow: either end is off the navmesh.
    Failed,
};

struct NavSeeker3DComponentUVE final {
    Math::Vector3UVE targetPosition{};
    Math::Vector3UVE nextPathPosition{};
    Math::Vector3UVE desiredVelocity{};
    float radius = 0.5F;
    float height = 1.8F;
    float maxSpeed = 4.0F;
    /// How hard `desiredVelocity` may change per second. 0 publishes the wanted velocity on the
    /// first step. The velocity is what a mover is handed, and an unbounded step change is a jolt.
    float acceleration = 20.0F;
    float pathUpdateInterval = 0.1F;
    /// How close to a waypoint counts as reached, so the agent turns to the next one.
    float waypointRadius = 0.4F;
    /// How close to the target counts as arrived. Larger than the waypoint radius on purpose: an
    /// agent that had to stop within a metre of a moving target would hunt for it.
    float targetTolerance = 1.0F;
    /// Inside this distance from the target the speed is scaled down, so the agent arrives rather
    /// than overshoots. 0 keeps full speed until the tolerance stops it.
    float slowDownRadius = 1.5F;
    /// Another agent whose centre is closer than this pushes this one away, when avoidance is on.
    float avoidanceRadius = 2.0F;
    std::uint32_t navigationLayers = 1U;
    NavigationAgentPathStatusUVE pathStatus = NavigationAgentPathStatusUVE::Idle;
    bool avoidanceEnabled = true;
    bool enabled = true;
    bool pathChanged = false;
    bool targetReached = false;

    [[nodiscard]] bool operator==(const NavSeeker3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsNavSeeker3DObjectComponentValidUVE(const NavSeeker3DComponentUVE& value) noexcept;

/// NavSeeker3D's creation recipe — the Object3D baseline plus a default
/// `NavSeeker3DComponentUVE`.
struct NavSeeker3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "NavSeeker3D";
    static constexpr std::string_view defaultName = "NavSeeker3D";

    NavSeeker3DComponentUVE seeker{};

    [[nodiscard]] bool operator==(const NavSeeker3DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsNavSeeker3DObjectDefinitionValidUVE(
    const NavSeeker3DObjectDefinitionUVE& value) noexcept;

void ApplyNavSeeker3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const NavSeeker3DObjectDefinitionUVE& value);

/// Node policy for a `NavSeeker3D` navigation agent object.
///
/// Owns effective avoidance radius, planar target-distance / arrival tolerance checks, slow-down
/// speed scaling, and idle state publishing shared by `NavigationRuntimeUVE` and gameplay steering.
class NavSeeker3DUVE final {
public:
    NavSeeker3DUVE() = delete;

    /// True when `seeker` is enabled and passes `IsNavSeeker3DObjectComponentValidUVE`.
    [[nodiscard]] static bool IsActiveUVE(const NavSeeker3DComponentUVE& seeker) noexcept;

    /// Returns `seeker.avoidanceRadius` when `seeker.avoidanceEnabled` is true, else `0.0F`.
    [[nodiscard]] static float EffectiveAvoidanceRadiusUVE(
        const NavSeeker3DComponentUVE& seeker) noexcept;

    /// Computes the horizontal (XZ-plane) distance in metres from `currentPosition` to
    /// `seeker.targetPosition`.
    [[nodiscard]] static float PlanarDistanceToTargetUVE(
        const NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept;

    /// True when `currentPosition` is within `seeker.targetTolerance` horizontally of
    /// `seeker.targetPosition`.
    [[nodiscard]] static bool IsWithinTargetToleranceUVE(
        const NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept;

    /// Computes the arrival speed multiplier in `[0, 1]` for `distanceToTargetMetres` given
    /// `seeker.targetTolerance` and `seeker.slowDownRadius`.
    [[nodiscard]] static float ResolveArrivalSpeedScaleUVE(
        const NavSeeker3DComponentUVE& seeker, float distanceToTargetMetres) noexcept;

    /// Resets `seeker`'s runtime steering outputs to `Idle` at `currentPosition` (zero velocity,
    /// `nextPathPosition = currentPosition`, `pathChanged = false`, `targetReached = false`).
    static void PublishIdleStateUVE(
        NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept;

    /// Assigns a new finite `targetPosition` on `seeker` and clears `targetReached`.
    static bool TrySetTargetPositionUVE(
        NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& targetPosition) noexcept;
};

} // namespace UVE::Scene
