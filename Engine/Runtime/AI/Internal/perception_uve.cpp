// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ai/perception_uve.h"
#include "uve/math/scalar_uve.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] float DistanceBetweenUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool IsPerceptionComponentValidUVE(const PerceptionComponentUVE& sensor) noexcept {
    return !sensor.watchedTag.empty() && sensor.watchedTag.size() <= kMaximumWatchedTagBytesUVE &&
           std::isfinite(sensor.sightRangeMetres) && sensor.sightRangeMetres > 0.0F &&
           std::isfinite(sensor.sightFieldOfViewDegrees) && sensor.sightFieldOfViewDegrees >= 0.0F &&
           sensor.sightFieldOfViewDegrees <= kPerceptionOmnidirectionalDegreesUVE &&
           std::isfinite(sensor.hearingRadiusMetres) && sensor.hearingRadiusMetres >= 0.0F;
}

bool IsNoiseEmitterComponentValidUVE(const NoiseEmitterComponentUVE& emitter) noexcept {
    return std::isfinite(emitter.loudness) && emitter.loudness >= 0.0F && emitter.loudness <= 1.0F &&
           std::isfinite(emitter.decayPerSecond) && emitter.decayPerSecond >= 0.0F;
}

void DecayNoiseEmitterUVE(NoiseEmitterComponentUVE& emitter, const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || !std::isfinite(emitter.decayPerSecond) ||
        emitter.decayPerSecond <= 0.0F) {
        return;
    }
    emitter.loudness = std::max(0.0F, emitter.loudness - emitter.decayPerSecond * deltaSeconds);
}

SightResultUVE SenseSightUVE(const Math::Vector3UVE& sensorPosition, const Math::Vector3UVE& sensorForward,
                             const PerceptionComponentUVE& sensor,
                             const std::span<const SightTargetUVE> candidates,
                             const SightOcclusionQueryUVE& occlusion) {
    SightResultUVE result;
    if (!std::isfinite(sensor.sightRangeMetres) || sensor.sightRangeMetres <= 0.0F) {
        return result;
    }
    const float forwardLength = DistanceBetweenUVE(Math::Vector3UVE{}, sensorForward);
    const bool knowsWhereItFaces = forwardLength > 1e-6F;
    const float cosineHalfAngle =
        sensor.sightFieldOfViewDegrees >= kPerceptionOmnidirectionalDegreesUVE - 1e-4F
            ? -2.0F
            : std::cos(Math::DegToRadUVE(sensor.sightFieldOfViewDegrees * 0.5F));
    std::vector<std::pair<float, SightTargetUVE>> inView;
    for (const SightTargetUVE& candidate : candidates) {
        const float distance = DistanceBetweenUVE(sensorPosition, candidate.position);
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

HearingResultUVE SenseHearingUVE(const Math::Vector3UVE& sensorPosition, const float hearingRadiusMetres,
                                 const std::span<const HeardNoiseUVE> noises) noexcept {
    HearingResultUVE result;
    if (!std::isfinite(hearingRadiusMetres) || hearingRadiusMetres <= 0.0F) {
        return result;
    }
    for (const HeardNoiseUVE& noise : noises) {
        if (!std::isfinite(noise.loudness) || noise.loudness <= 0.0F) {
            continue;
        }
        const float reach = hearingRadiusMetres * noise.loudness;
        const float distance = DistanceBetweenUVE(sensorPosition, noise.position);
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

void WritePerceptionResultsUVE(BlackboardComponentUVE& board, const std::string_view watchedTag,
                               const SightResultUVE& sight, const HearingResultUVE& hearing) {
    const std::string sightPrefix = std::string("sight.") + std::string(watchedTag) + ".";
    static_cast<void>(
        SetBlackboardValueUVE(board, sightPrefix + "visible", sight.visible ? 1.0F : 0.0F));
    if (sight.visible) {
        static_cast<void>(SetBlackboardValueUVE(board, sightPrefix + "distance01", sight.distance01));
        static_cast<void>(SetBlackboardVectorUVE(board, sightPrefix + "position", sight.position));
    }
    static_cast<void>(SetBlackboardValueUVE(board, "hearing.loudest", hearing.loudness));
    if (hearing.loudness > 0.0F) {
        static_cast<void>(SetBlackboardVectorUVE(board, "hearing.position", hearing.position));
    }
}

} // namespace UVE::Scene
