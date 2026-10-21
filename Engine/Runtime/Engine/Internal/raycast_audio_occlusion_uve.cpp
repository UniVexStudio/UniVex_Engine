// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/raycast_audio_occlusion_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "uve/math/ray_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/i_raycast_system_uve.h"
#include "uve/physics/raycast_hit_uve.h"
#include "uve/physics/raycast_query_uve.h"

namespace UVE::Core {

namespace {

/// Traces one listener-to-source path. Returns true when something blocks it.
[[nodiscard]] bool IsPathBlockedUVE(const Physics::IRaycastSystemUVE& raycastSystem,
                                    Scene::IEntityManagerUVE& entityManager,
                                    const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                    const Audio::AudioOcclusionQueryUVE& query) {
    const Math::Vector3UVE delta = to - from;
    const float distanceSquared = Math::LengthSquaredUVE(delta);
    if (!std::isfinite(distanceSquared) || distanceSquared < RaycastAudioOcclusionUVE::kMinimumTraceDistanceMetersUVE *
                                                          RaycastAudioOcclusionUVE::kMinimumTraceDistanceMetersUVE) {
        return false; // source on top of the listener: nothing can be in between
    }
    const float distance = std::sqrt(distanceSquared);
    if (!std::isfinite(distance)) {
        return false;
    }

    Physics::RaycastQueryUVE raycast;
    raycast.ray.origin = from;
    raycast.ray.direction = delta * (1.0F / distance);
    // Stop just short of the source so a collider wrapped around the emitter does not read as a
    // wall. One centimetre is below any audible path difference and above float noise at the
    // distances this engine's attenuation model works over.
    raycast.maxDistance = std::max(0.0F, distance - 0.01F);
    raycast.layerMask = query.layerMask;
    raycast.ignoreEntity = query.sourceEntity;
    return raycastSystem.RaycastUVE(entityManager, raycast).has_value();
}

} // namespace

RaycastAudioOcclusionUVE::RaycastAudioOcclusionUVE(Scene::IEntityManagerUVE& entityManager,
                                                   const Physics::IRaycastSystemUVE& raycastSystem) noexcept
    : m_entityManager(entityManager), m_raycastSystem(raycastSystem) {}

float RaycastAudioOcclusionUVE::QueryOcclusionUVE(const Audio::AudioOcclusionQueryUVE& query) const {
    const Math::Vector3UVE earOffset = query.listenerRight * kEarOffsetMetersUVE;
    const Math::Vector3UVE& listener = query.listenerPosition;
    const Math::Vector3UVE& source = query.sourcePosition;

    // Left ear, head centre, right ear. A source half-hidden behind a doorway blocks one or two of
    // these, which is what partial occlusion means; averaging the three turns that into a fraction
    // the system's smoothing can ride instead of a switch.
    const std::array<Math::Vector3UVE, 3> origins{listener - earOffset, listener, listener + earOffset};
    int blocked = 0;
    for (const Math::Vector3UVE& origin : origins) {
        if (IsPathBlockedUVE(m_raycastSystem, m_entityManager, origin, source, query)) {
            ++blocked;
        }
    }
    return static_cast<float>(blocked) / static_cast<float>(origins.size());
}

} // namespace UVE::Core
