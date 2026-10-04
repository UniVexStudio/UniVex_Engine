// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/navigation/nav_path_uve.h"
#include "uve/navigation/navmesh_uve.h"

namespace UVE::Navigation {

/// What an agent is doing about its target, in the terms a script or an inspector asks about.
///
/// There is no "searching" state: a path search is a synchronous walk over the mesh, so the step
/// that decides to search is the step that has the route. A state nothing can ever be in is a state
/// every consumer would have to handle for nothing.
enum class NavAgentStatusUVE : std::uint8_t {
    /// No target, or the agent is switched off. It publishes no velocity.
    Idle = 0,
    /// Walking the path. `nextPathPosition` is the waypoint being approached.
    Following,
    /// Standing on the target: within its tolerance, so it has stopped.
    Finished,
    /// The agent cannot get there - either end is off the navmesh, or there is no region under it.
    Failed,
};

/// How one agent moves, and how often it reconsiders the route.
///
/// `radius` and `height` are the agent's own measurements, not the mesh's: a region is baked for the
/// agent that will walk it, and an agent whose size disagrees with the bake is standing on a mesh
/// that was eroded for somebody else. They are carried here so the mismatch is visible to whoever
/// wires the two together.
struct NavAgentSettingsUVE final {
    float radius = 0.5F;
    float height = 1.8F;
    /// Top speed on a straight leg, in metres per second.
    float maxSpeed = 4.0F;
    /// How hard the published velocity may change per second. The velocity is what a mover is given,
    /// and a mover that is handed a step change every replan jolts the body it drives; 0 removes the
    /// limit, publishing the new direction immediately.
    float accelerationMetresPerSecondSquared = 20.0F;
    /// How often the route is reconsidered, in seconds. Replanning every frame would be correct and
    /// wasteful; replanning rarely leaves an agent walking into a door that closed behind it, so the
    /// interval is the caller's trade-off and its value is what schedules the search.
    float pathUpdateInterval = 0.1F;
    /// How close to a waypoint counts as reached, so the agent turns to the next one.
    float waypointRadiusMetres = 0.4F;
    /// How close to the target counts as arrived. Larger than the waypoint radius on purpose: an
    /// agent that had to stop within 0.4 m of a moving target would spend its last steps hunting.
    float targetToleranceMetres = 1.0F;
    /// Inside this distance from the target the speed is scaled down, so an agent arrives rather
    /// than overshoots.
    float slowDownRadiusMetres = 1.5F;
    /// The polygon layers this agent may walk: a polygon is usable when
    /// `(polygon.navigationLayers & navigationLayers) != 0`.
    std::uint32_t navigationLayers = 1U;
    /// How far either end of the request may sit off the mesh and still be brought onto it.
    float offMeshToleranceMetres = 2.0F;
    /// Another agent whose centre is closer than this pushes this one away, as a velocity offset on
    /// top of the steering (see StepUVE's neighbour overload). 0 switches the rule off for this
    /// agent whether or not the caller passes neighbours.
    float avoidanceRadiusMetres = 2.0F;
    /// The search budget one replan gets, reported back so a truncated search is visible as one.
    std::size_t maximumExpandedPolygons = 4096U;
};

/// Another agent, as this one's steering sees it: where it is and how wide it is. A caller that
/// wants avoidance passes every agent in the scene, itself included - the steering recognises its
/// own position and ignores it.
struct NavAgentObstacleUVE final {
    Math::Vector3UVE position{};
    float radius = 0.5F;
};

/// One agent's steering state, stepped once per simulation step against the mesh under it.
///
/// The split of responsibility is the same one the physics movers use: the agent decides *where* it
/// wants to go and how fast, publishes that as `desiredVelocity`, and something else - a
/// Character3D mover, a kinematic body, a script - is what actually moves the body and hands the new
/// position back on the next step. The agent never writes a transform, which is what keeps it usable
/// under whichever mover a project already has.
class NavAgentUVE final {
public:
    void ConfigureUVE(const NavAgentSettingsUVE& settings) noexcept { m_settings = settings; }
    [[nodiscard]] const NavAgentSettingsUVE& GetSettingsUVE() const noexcept { return m_settings; }

    /// A new target. Setting one that differs from the current target invalidates the route and arms
    /// the next step to search, so a script that changes the target every frame still gets a path to
    /// the newest one rather than walking to the first. Setting the SAME target - which is what a
    /// per-frame pass does, reading it out of a component - is not a new order and leaves the
    /// update interval alone.
    void SetTargetUVE(const Math::Vector3UVE& target) noexcept;
    void ClearTargetUVE() noexcept;

    /// The body's own position, handed back by whatever moved it. The next step plans from here.
    void SetPositionUVE(const Math::Vector3UVE& position) noexcept;

    void SetEnabledUVE(const bool enabled) noexcept;

    /// Switches the layers this agent walks and drops the current route: a path found for the old
    /// layers may cross ground the new ones do not allow.
    void SetNavigationLayersUVE(std::uint32_t navigationLayers) noexcept;

    /// Forgets everything, including the target, and publishes no velocity.
    void ResetUVE() noexcept;

    /// One step of the agent: replan if the interval has elapsed or the target moved, walk the route
    /// towards the next waypoint, and publish the velocity the mover should apply.
    ///
    /// `deltaTimeSeconds` is simulation time, not real time: a paused game freezes agents with
    /// everything else, and the same step count produces the same route the second time.
    void StepUVE(const NavmeshUVE& mesh, float deltaTimeSeconds) noexcept;

    /// The same step with the other agents in the scene: each one inside the avoidance radius pushes
    /// this agent away by a falloff of the distance, on top of the steering and clamped to the same
    /// top speed, so two agents walking at each other part instead of occupying the same metre.
    ///
    /// This is separation steering - a push, not a solved avoidance - so it cannot promise more than
    /// it is: an agent pushed against a wall still walks into the wall, and neither agent yields to
    /// the other with right of way. What it does guarantee is the ordinary corridor case: two agents
    /// on the same route end up beside each other rather than inside each other, and it never breaks
    /// the route or the published status.
    void StepUVE(const NavmeshUVE& mesh, std::span<const NavAgentObstacleUVE> neighbours,
                 float deltaTimeSeconds) noexcept;

    [[nodiscard]] const Math::Vector3UVE& GetPositionUVE() const noexcept { return m_position; }
    [[nodiscard]] const Math::Vector3UVE& GetTargetUVE() const noexcept { return m_target; }
    [[nodiscard]] bool HasTargetUVE() const noexcept { return m_hasTarget; }
    [[nodiscard]] bool IsEnabledUVE() const noexcept { return m_enabled; }

    /// The velocity the mover should apply this step. Zero when there is nothing to walk to.
    [[nodiscard]] const Math::Vector3UVE& GetDesiredVelocityUVE() const noexcept { return m_desiredVelocity; }
    /// The waypoint being approached, or the agent's own position when it is not following a route.
    [[nodiscard]] const Math::Vector3UVE& GetNextPathPositionUVE() const noexcept { return m_nextPathPosition; }
    [[nodiscard]] NavAgentStatusUVE GetStatusUVE() const noexcept { return m_status; }
    /// True on the step a new route replaced the old one, and on the step a route was found where
    /// there was none: the flag a script listens to when it wants to know the agent changed its mind.
    [[nodiscard]] bool GetPathChangedUVE() const noexcept { return m_pathChanged; }
    [[nodiscard]] bool HasReachedTargetUVE() const noexcept { return m_status == NavAgentStatusUVE::Finished; }
    [[nodiscard]] bool HasPathUVE() const noexcept { return !m_path.waypoints.empty(); }

    /// What is left of the route the last search returned, in metres. Zero before one has run.
    [[nodiscard]] float GetRemainingDistanceUVE() const noexcept { return m_remainingDistance; }
    [[nodiscard]] const NavPathUVE& GetPathUVE() const noexcept { return m_path; }
    /// Index of the waypoint being approached, or `waypoints.size()` when the route is done.
    [[nodiscard]] std::size_t GetWaypointIndexUVE() const noexcept { return m_waypointIndex; }

private:
    /// Runs the search and adopts what it found, reporting whether the route changed.
    void ReplanUVE(const NavmeshUVE& mesh) noexcept;

    /// Walks the route forward to the waypoint the agent should be steering at, and stops it when
    /// the agent stands on the target.
    void FollowUVE(std::span<const NavAgentObstacleUVE> neighbours, float deltaTimeSeconds) noexcept;

    NavAgentSettingsUVE m_settings{};
    NavPathUVE m_path{};
    Math::Vector3UVE m_position{};
    Math::Vector3UVE m_target{};
    Math::Vector3UVE m_desiredVelocity{};
    Math::Vector3UVE m_nextPathPosition{};
    /// Time left before the next search, counted in the simulation's own seconds.
    float m_timeUntilReplan = 0.0F;
    float m_remainingDistance = 0.0F;
    std::size_t m_waypointIndex = 0U;
    NavAgentStatusUVE m_status = NavAgentStatusUVE::Idle;
    bool m_enabled = true;
    bool m_hasTarget = false;
    bool m_pathChanged = false;
    /// Set when the target moved since the last search, so the next step searches whatever the
    /// interval says.
    bool m_replanRequested = false;
};

} // namespace UVE::Navigation
