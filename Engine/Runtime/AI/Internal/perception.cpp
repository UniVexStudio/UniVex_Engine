// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "perception.h"
#include "scalar.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] float DistanceBetween(const Math::Vector3& from, const Math::Vector3& to) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool IsPerceptionComponentValid(const PerceptionComponent& sensor) noexcept {
    return !sensor.watchedTag.empty() && sensor.watchedTag.size() <= kMaximumWatchedTagBytes &&
           std::isfinite(sensor.sightRangeMetres) && sensor.sightRangeMetres > 0.0F &&
           std::isfinite(sensor.sightFieldOfViewDegrees) && sensor.sightFieldOfViewDegrees >= 0.0F &&
           sensor.sightFieldOfViewDegrees <= kPerceptionOmnidirectionalDegrees &&
           std::isfinite(sensor.hearingRadiusMetres) && sensor.hearingRadiusMetres >= 0.0F;
}

bool IsNoiseEmitterComponentValid(const NoiseEmitterComponent& emitter) noexcept {
    return std::isfinite(emitter.loudness) && emitter.loudness >= 0.0F && emitter.loudness <= 1.0F &&
           std::isfinite(emitter.decayPerSecond) && emitter.decayPerSecond >= 0.0F;
}

void DecayNoiseEmitter(NoiseEmitterComponent& emitter, const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || !std::isfinite(emitter.decayPerSecond) ||
        emitter.decayPerSecond <= 0.0F) {
        return;
    }
    emitter.loudness = std::max(0.0F, emitter.loudness - emitter.decayPerSecond * deltaSeconds);
}

SightResult SenseSight(const Math::Vector3& sensorPosition, const Math::Vector3& sensorForward,
                             const PerceptionComponent& sensor,
                             const std::span<const SightTarget> candidates,
                             const SightOcclusionQuery& occlusion) {
    SightResult result;
    if (!std::isfinite(sensor.sightRangeMetres) || sensor.sightRangeMetres <= 0.0F) {
        return result;
    }
    const float forwardLength = DistanceBetween(Math::Vector3{}, sensorForward);
    const bool knowsWhereItFaces = forwardLength > 1e-6F;
    const float cosineHalfAngle =
        sensor.sightFieldOfViewDegrees >= kPerceptionOmnidirectionalDegrees - 1e-4F
            ? -2.0F
            : std::cos(Math::DegToRad(sensor.sightFieldOfViewDegrees * 0.5F));
    std::vector<std::pair<float, SightTarget>> inView;
    for (const SightTarget& candidate : candidates) {
        const float distance = DistanceBetween(sensorPosition, candidate.position);
        if (distance > sensor.sightRangeMetres) {
            continue;
        }
        if (distance < 1e-6F) {
            result.visible = true;
            result.distance01 = 0.0F;
            result.position = candidate.position;
            result.entity = candidate.entity;
            return result;
        }
        if (knowsWhereItFaces && cosineHalfAngle > -1.5F) {
            const float dot = ((candidate.position.x - sensorPosition.x) * sensorForward.x +
                               (candidate.position.y - sensorPosition.y) * sensorForward.y +
                               (candidate.position.z - sensorPosition.z) * sensorForward.z) /
                              (distance * forwardLength);
            if (dot < cosineHalfAngle) {
                continue;
            }
        }
        inView.emplace_back(distance, candidate);
    }
    std::sort(inView.begin(), inView.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    for (const auto& [distance, candidate] : inView) {
        if (sensor.requiresLineOfSight && occlusion(sensorPosition, candidate.position, candidate.entity)) {
            continue;
        }
        result.visible = true;
        result.distance01 = distance / sensor.sightRangeMetres;
        result.position = candidate.position;
        result.entity = candidate.entity;
        return result;
    }
    return result;
}

HearingResult SenseHearing(const Math::Vector3& sensorPosition, const float hearingRadiusMetres,
                                 const std::span<const HeardNoise> noises) noexcept {
    HearingResult result;
    if (!std::isfinite(hearingRadiusMetres) || hearingRadiusMetres <= 0.0F) {
        return result;
    }
    for (const HeardNoise& noise : noises) {
        if (!std::isfinite(noise.loudness) || noise.loudness <= 0.0F) {
            continue;
        }
        const float reach = hearingRadiusMetres * noise.loudness;
        const float distance = DistanceBetween(sensorPosition, noise.position);
        if (distance > reach) {
            continue;
        }
        const float perceived = noise.loudness * (1.0F - distance / reach);
        if (perceived > result.loudness) {
            result.loudness = perceived;
            result.position = noise.position;
        }
    }
    return result;
}

void WritePerceptionResults(BlackboardComponent& board, const std::string_view watchedTag,
                               const SightResult& sight, const HearingResult& hearing) {
    const std::string sightPrefix = std::string("sight.") + std::string(watchedTag) + ".";
    static_cast<void>(
        SetBlackboardValue(board, sightPrefix + "visible", sight.visible ? 1.0F : 0.0F));
    if (sight.visible) {
        static_cast<void>(SetBlackboardValue(board, sightPrefix + "distance01", sight.distance01));
        static_cast<void>(SetBlackboardVector(board, sightPrefix + "position", sight.position));
    }
    static_cast<void>(SetBlackboardValue(board, "hearing.loudest", hearing.loudness));
    if (hearing.loudness > 0.0F) {
        static_cast<void>(SetBlackboardVector(board, "hearing.position", hearing.position));
    }
}

} // namespace UVE::Scene
