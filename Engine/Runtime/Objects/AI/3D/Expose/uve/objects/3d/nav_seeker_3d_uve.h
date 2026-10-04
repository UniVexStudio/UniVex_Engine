// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

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
};

[[nodiscard]] bool IsNavSeeker3DObjectComponentValidUVE(const NavSeeker3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
