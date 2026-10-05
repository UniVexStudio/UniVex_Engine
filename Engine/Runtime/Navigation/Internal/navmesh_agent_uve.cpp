// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/navigation/navmesh_agent_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace UVE::Navigation {

namespace {

/// True when two points are the same place for the purpose of "did the route change". Heights are
/// compared too: a route that moved from one floor to another is a different route even where the
/// plan view is identical.
[[nodiscard]] bool SamePointUVE(const Math::Vector3UVE& left, const Math::Vector3UVE& right) noexcept {
    constexpr float kToleranceUVE = 1.0e-3F;
    return std::fabs(left.x - right.x) <= kToleranceUVE && std::fabs(left.y - right.y) <= kToleranceUVE &&
           std::fabs(left.z - right.z) <= kToleranceUVE;
}

/// Distance in the plane the agents walk on: a path length that grew because the floor rose under
/// the agent would be a length no mover could act on.
[[nodiscard]] float PlanarDistanceUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to) noexcept {
    const float deltaX = to.x - from.x;
    const float deltaZ = to.z - from.z;
    return std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
}

/// The velocity offset the other agents contribute: a push away from each neighbour inside the
/// avoidance radius, stronger the closer it is, summed and then scaled to fit the agent's top speed
/// by the caller. Agents at the same spot contribute nothing - that is this agent's own entry in the
/// list, and two bodies truly standing inside each other is for the mover to untangle.
[[nodiscard]] Math::Vector3UVE SeparationVelocityUVE(const Math::Vector3UVE& position,
                                                     const NavAgentSettingsUVE& settings,
                                                     const std::span<const NavAgentObstacleUVE> neighbours) noexcept {
    if (settings.avoidanceRadiusMetres <= 0.0F || neighbours.empty()) {
        return Math::Vector3UVE{};
    }
    Math::Vector3UVE push{};
    for (const NavAgentObstacleUVE& neighbour : neighbours) {
        const float awayX = position.x - neighbour.position.x;
        const float awayZ = position.z - neighbour.position.z;
        const float distanceSquared = awayX * awayX + awayZ * awayZ;
        if (distanceSquared <= 1.0e-8F) {
            continue;
        }
        const float distance = std::sqrt(distanceSquared);
        const float reach = settings.avoidanceRadiusMetres + neighbour.radius;
        if (distance >= reach) {
            continue;
        }
        const float strength = settings.maxSpeed * (1.0F - distance / reach) / distance;
        push.x += awayX * strength;
        push.z += awayZ * strength;
    }
    return push;
}

/// Whether two routes differ in any way a follower would notice: their waypoints, or whether they
/// reach the goal at all.
[[nodiscard]] bool RoutesDifferUVE(const NavPathUVE& previous, const NavPathUVE& current) noexcept {
    if (previous.IsUsableUVE() != current.IsUsableUVE() || previous.status != current.status) {
        return true;
    }
    if (previous.waypoints.size() != current.waypoints.size()) {
        return true;
    }
    for (std::size_t index = 0U; index < current.waypoints.size(); ++index) {
        if (!SamePointUVE(previous.waypoints[index], current.waypoints[index])) {
            return true;
        }
    }
    return false;
}

} // namespace

void NavAgentUVE::SetTargetUVE(const Math::Vector3UVE& target) noexcept {
    if (m_hasTarget && SamePointUVE(m_target, target)) {
        return;
    }
    m_target = target;
    m_hasTarget = true;
    // A new target is a new question, so the schedule is reset rather than waited out: an agent told
    // where to go and then left to stand for a tenth of a second looks broken at every order.
    m_replanRequested = true;
    m_timeUntilReplan = 0.0F;
}

void NavAgentUVE::ClearTargetUVE() noexcept {
    m_hasTarget = false;
    m_path.ClearUVE();
    m_desiredVelocity = Math::Vector3UVE{};
    m_nextPathPosition = m_position;
    m_remainingDistance = 0.0F;
    m_waypointIndex = 0U;
    m_timeUntilReplan = 0.0F;
    m_replanRequested = false;
    m_status = NavAgentStatusUVE::Idle;
}

void NavAgentUVE::SetPositionUVE(const Math::Vector3UVE& position) noexcept {
    m_position = position;
}

void NavAgentUVE::SetEnabledUVE(const bool enabled) noexcept {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!enabled) {
        // Switching an agent off is a decision about it, not a pause of its step: it stops being
        // driven, so nothing should read a velocity out of it and walk the body anyway.
        m_desiredVelocity = Math::Vector3UVE{};
        m_nextPathPosition = m_position;
        m_path.ClearUVE();
        m_remainingDistance = 0.0F;
        m_waypointIndex = 0U;
        m_status = NavAgentStatusUVE::Idle;
        return;
    }
    m_replanRequested = true;
    m_timeUntilReplan = 0.0F;
}

void NavAgentUVE::SetNavigationLayersUVE(const std::uint32_t navigationLayers) noexcept {
    if (m_settings.navigationLayers == navigationLayers) {
        return;
    }
    m_settings.navigationLayers = navigationLayers;
    // The route in hand was found for the old layers and may cross ground this agent may not use.
    // The status is left for the next step to write: it is a report about a step, and the step that
    // follows this change is the one that searched under the new layers.
    m_path.ClearUVE();
    m_replanRequested = true;
    m_timeUntilReplan = 0.0F;
}

void NavAgentUVE::ResetUVE() noexcept {
    m_path.ClearUVE();
    m_position = Math::Vector3UVE{};
    m_target = Math::Vector3UVE{};
    m_desiredVelocity = Math::Vector3UVE{};
    m_nextPathPosition = Math::Vector3UVE{};
    m_timeUntilReplan = 0.0F;
    m_remainingDistance = 0.0F;
    m_waypointIndex = 0U;
    m_status = NavAgentStatusUVE::Idle;
    m_enabled = true;
    m_hasTarget = false;
    m_pathChanged = false;
    m_replanRequested = false;
}

void NavAgentUVE::StepUVE(const NavmeshUVE& mesh, const float deltaTimeSeconds) noexcept {
    StepUVE(mesh, std::span<const NavAgentObstacleUVE>{}, deltaTimeSeconds);
}

void NavAgentUVE::StepUVE(const NavmeshUVE& mesh, const std::span<const NavAgentObstacleUVE> neighbours,
                          const float deltaTimeSeconds) noexcept {
    m_pathChanged = false;
    const float stepSeconds = deltaTimeSeconds > 0.0F ? deltaTimeSeconds : 0.0F;

    if (!m_enabled || !m_hasTarget) {
        m_desiredVelocity = Math::Vector3UVE{};
        m_nextPathPosition = m_position;
        m_remainingDistance = 0.0F;
        m_status = NavAgentStatusUVE::Idle;
        return;
    }

    m_timeUntilReplan -= stepSeconds;
    if (m_replanRequested || m_timeUntilReplan <= 0.0F) {
        ReplanUVE(mesh);
        m_timeUntilReplan = m_settings.pathUpdateInterval;
    }

    if (!m_path.IsUsableUVE()) {
        m_desiredVelocity = Math::Vector3UVE{};
        m_nextPathPosition = m_position;
        m_remainingDistance = 0.0F;
        m_status = NavAgentStatusUVE::Failed;
        return;
    }

    FollowUVE(neighbours, stepSeconds);
}

void NavAgentUVE::ReplanUVE(const NavmeshUVE& mesh) noexcept {
    NavPathRequestUVE request{};
    request.start = m_position;
    request.target = m_target;
    request.navigationLayers = m_settings.navigationLayers;
    request.offMeshToleranceMetres = m_settings.offMeshToleranceMetres;
    request.maximumExpandedNodes = m_settings.maximumExpandedNodes;
    const NavPathUVE path = FindNavPathUVE(mesh, request);
    m_replanRequested = false;

    // The flag a script watches: a route that changed is an agent that changed its mind, and the
    // case that matters most is the one where there was no route and now there is.
    m_pathChanged = RoutesDifferUVE(m_path, path);
    m_path = path;
    m_waypointIndex = 0U;
    if (!m_path.IsUsableUVE()) {
        m_status = NavAgentStatusUVE::Failed;
        return;
    }
    // Search is over; where the agent is on the route is what the follower below decides, including
    // whether it is already standing on the target and this route is one it never has to walk.
    m_status = NavAgentStatusUVE::Following;
    m_remainingDistance = m_path.lengthMetres;
}

void NavAgentUVE::FollowUVE(const std::span<const NavAgentObstacleUVE> neighbours,
                            const float deltaTimeSeconds) noexcept {
    if (m_path.waypoints.empty()) {
        // A usable route always ends at the goal, so an empty one means the search stopped exactly on
        // it. Standing there is the whole of the agent's job.
        m_status = NavAgentStatusUVE::Finished;
        m_desiredVelocity = Math::Vector3UVE{};
        m_nextPathPosition = m_target;
        m_remainingDistance = 0.0F;
        return;
    }

    const float distanceToTarget = PlanarDistanceUVE(m_position, m_target);
    if (distanceToTarget <= m_settings.targetToleranceMetres) {
        // Close enough is arrived: the agent stops where it stands rather than walking into the
        // target's own position, which for a moving target is a chase it would never win.
        m_status = NavAgentStatusUVE::Finished;
        m_desiredVelocity = Math::Vector3UVE{};
        m_nextPathPosition = m_target;
        m_remainingDistance = distanceToTarget;
        m_waypointIndex = m_path.waypoints.size();
        return;
    }

    // Waypoints already reached are walked past without steering, which is what keeps an agent from
    // turning back for a corner it has rounded.
    while (m_waypointIndex < m_path.waypoints.size() &&
           PlanarDistanceUVE(m_position, m_path.waypoints[m_waypointIndex]) <= m_settings.waypointRadiusMetres) {
        ++m_waypointIndex;
    }
    if (m_waypointIndex >= m_path.waypoints.size()) {
        // Every waypoint is behind the agent but the target's tolerance has not been met - the target
        // moved, or the mover overshot. Steering at the target itself is the honest answer; the next
        // search will produce a route from wherever the agent ended up.
        m_waypointIndex = m_path.waypoints.size() - 1U;
    }

    const Math::Vector3UVE& waypoint = m_path.waypoints[m_waypointIndex];
    m_nextPathPosition = waypoint;

    // What is left of the route, measured from where the agent stands through the waypoints it has
    // not reached yet.
    float remaining = PlanarDistanceUVE(m_position, waypoint);
    for (std::size_t index = m_waypointIndex + 1U; index < m_path.waypoints.size(); ++index) {
        remaining += PlanarDistanceUVE(m_path.waypoints[index - 1U], m_path.waypoints[index]);
    }
    m_remainingDistance = remaining;

    Math::Vector3UVE direction = waypoint - m_position;
    direction.y = 0.0F;
    const float directionLengthSquared = direction.x * direction.x + direction.z * direction.z;
    if (directionLengthSquared <= 1.0e-8F) {
        // Standing exactly on the waypoint with the target still out of tolerance: nothing to steer
        // at this step, and the next search will replace the route that led here.
        m_desiredVelocity = Math::Vector3UVE{};
        return;
    }
    const float inverseLength = 1.0F / std::sqrt(directionLengthSquared);
    direction = Math::Vector3UVE{direction.x * inverseLength, 0.0F, direction.z * inverseLength};

    float speed = m_settings.maxSpeed;
    if (m_settings.slowDownRadiusMetres > 0.0F && distanceToTarget < m_settings.slowDownRadiusMetres) {
        speed *= distanceToTarget / m_settings.slowDownRadiusMetres;
    }
    Math::Vector3UVE wanted{direction.x * speed, 0.0F, direction.z * speed};
    if (m_settings.avoidanceRadiusMetres > 0.0F) {
        const Math::Vector3UVE push = SeparationVelocityUVE(m_position, m_settings, neighbours);
        wanted.x += push.x;
        wanted.z += push.z;
        // The push may not make the agent faster than it can walk: a neighbour is something to walk
        // around, not a reason to sprint.
        const float speedSquared = wanted.x * wanted.x + wanted.z * wanted.z;
        if (speedSquared > m_settings.maxSpeed * m_settings.maxSpeed) {
            const float scale = m_settings.maxSpeed / std::sqrt(speedSquared);
            wanted.x *= scale;
            wanted.z *= scale;
        }
    }

    if (m_settings.accelerationMetresPerSecondSquared <= 0.0F || deltaTimeSeconds <= 0.0F) {
        m_desiredVelocity = wanted;
    } else {
        // A step change in the published velocity is a step change in whatever moves the body, which
        // is felt through the whole chain as a jolt; the limit is what turns a replan into a turn.
        const Math::Vector3UVE change = wanted - m_desiredVelocity;
        const float changeLength = std::sqrt(change.x * change.x + change.z * change.z);
        const float maximumChange = m_settings.accelerationMetresPerSecondSquared * deltaTimeSeconds;
        m_desiredVelocity = changeLength <= maximumChange || changeLength <= 1.0e-8F
                                ? wanted
                                : m_desiredVelocity + change * (maximumChange / changeLength);
    }
    m_status = NavAgentStatusUVE::Following;
}

} // namespace UVE::Navigation
