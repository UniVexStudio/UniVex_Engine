// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"
#include "object_3d.h"

namespace UVE::Scene {

class IEntityManager;

/// What the agent's own steering last reported. Runtime state: written by the navigation step, never
/// authored and never saved - the route is a fact about the scene as it stands, not about the file.
enum class NavigationAgentPathStatus : std::uint8_t {
    /// No target, or the agent is switched off.
    Idle = 0,
    /// Walking the route towards `nextPathPosition`.
    Following,
    /// Standing on the target, inside its own tolerance.
    Finished,
    /// Nothing walkable to follow: either end is off the navmesh.
    Failed,
};

struct NavSeeker3DComponent final {
    Math::Vector3 targetPosition{};
    Math::Vector3 nextPathPosition{};
    Math::Vector3 desiredVelocity{};
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
    NavigationAgentPathStatus pathStatus = NavigationAgentPathStatus::Idle;
    bool avoidanceEnabled = true;
    bool enabled = true;
    bool pathChanged = false;
    bool targetReached = false;

    [[nodiscard]] bool operator==(const NavSeeker3DComponent&) const = default;
};

[[nodiscard]] bool IsNavSeeker3DObjectComponentValid(const NavSeeker3DComponent& value) noexcept;

/// NavSeeker3D's creation recipe — the Object3D baseline plus a default
/// `NavSeeker3DComponent`.
struct NavSeeker3DObjectDefinition final {
    static constexpr std::string_view typeName = "NavSeeker3D";
    static constexpr std::string_view defaultName = "NavSeeker3D";

    NavSeeker3DComponent seeker{};

    [[nodiscard]] bool operator==(const NavSeeker3DObjectDefinition&) const = default;
};

[[nodiscard]] bool IsNavSeeker3DObjectDefinitionValid(
    const NavSeeker3DObjectDefinition& value) noexcept;

void ApplyNavSeeker3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const NavSeeker3DObjectDefinition& value);

/// Node policy for a `NavSeeker3D` navigation agent object.
///
/// Owns effective avoidance radius, planar target-distance / arrival tolerance checks, slow-down
/// speed scaling, and idle state publishing shared by `NavigationRuntime` and gameplay steering.
class NavSeeker3D final {
public:
    NavSeeker3D() = delete;

    /// True when `seeker` is enabled and passes `IsNavSeeker3DObjectComponentValid`.
    [[nodiscard]] static bool IsActive(const NavSeeker3DComponent& seeker) noexcept;

    /// Returns `seeker.avoidanceRadius` when `seeker.avoidanceEnabled` is true, else `0.0F`.
    [[nodiscard]] static float EffectiveAvoidanceRadius(
        const NavSeeker3DComponent& seeker) noexcept;

    /// Computes the horizontal (XZ-plane) distance in metres from `currentPosition` to
    /// `seeker.targetPosition`.
    [[nodiscard]] static float PlanarDistanceToTarget(
        const NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept;

    /// True when `currentPosition` is within `seeker.targetTolerance` horizontally of
    /// `seeker.targetPosition`.
    [[nodiscard]] static bool IsWithinTargetTolerance(
        const NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept;

    /// Computes the arrival speed multiplier in `[0, 1]` for `distanceToTargetMetres` given
    /// `seeker.targetTolerance` and `seeker.slowDownRadius`.
    [[nodiscard]] static float ResolveArrivalSpeedScale(
        const NavSeeker3DComponent& seeker, float distanceToTargetMetres) noexcept;

    /// Resets `seeker`'s runtime steering outputs to `Idle` at `currentPosition` (zero velocity,
    /// `nextPathPosition = currentPosition`, `pathChanged = false`, `targetReached = false`).
    static void PublishIdleState(
        NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept;

    /// Assigns a new finite `targetPosition` on `seeker` and clears `targetReached`.
    static bool TrySetTargetPosition(
        NavSeeker3DComponent& seeker, const Math::Vector3& targetPosition) noexcept;
};

} // namespace UVE::Scene
