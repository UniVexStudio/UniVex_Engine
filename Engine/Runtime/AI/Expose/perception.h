// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "utility_ai.h"
#include "entity.h"
#include "vector3.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Perception ------------------------------------------------------------------------------
//
// The senses that write what brains read. A sensor watches one gameplay tag ("enemy", "prey"):
// sight reports the nearest watched entity inside range, field-of-view cone, and (when asked) an
// unoccluded ray; hearing reports the loudest noise inside radius times loudness. Both write
// blackboard entries on the sensor's own entity, which a brain then scores like any other input.
//
// THE KEY CONVENTION. Sight writes "sight.<tag>.visible" (0/1), "sight.<tag>.distance01", and the
// vector "sight.<tag>.position"; hearing writes "hearing.loudest" and the vector
// "hearing.position". Watched tags stay within 32 bytes so composed keys fit the board's 64-byte
// cap. When nothing is sensed, only the live flags drop (visible to 0, loudest to 0) - position
// and distance keep their last sensed values, because a guard that lost its enemy should walk to
// where it last saw them, not to the origin. Hearing ignores occlusion in v1: walls deafen
// nothing yet.

inline constexpr std::size_t kMaximumWatchedTagBytes = 32U;
inline constexpr float kPerceptionOmnidirectionalDegrees = 360.0F;

struct PerceptionComponent final {
    /// Gameplay tag this sensor watches, within 32 bytes.
    std::string watchedTag;
    float sightRangeMetres = 20.0F;
    /// Full field-of-view angle in degrees, 0..360. 360 sees behind itself.
    float sightFieldOfViewDegrees = 90.0F;
    /// Hearing reach at full loudness; 0 is deaf.
    float hearingRadiusMetres = 15.0F;
    bool requiresLineOfSight = true;

    [[nodiscard]] bool operator==(const PerceptionComponent&) const = default;
};

/// A continuous or decaying sound in the world: a machine drones (decay 0), a gunshot bangs (the
/// game sets loudness; the sync decays it back to silence).
struct NoiseEmitterComponent final {
    float loudness = 1.0F;
    float decayPerSecond = 0.0F;

    [[nodiscard]] bool operator==(const NoiseEmitterComponent&) const = default;
};

struct SightTarget final {
    Entity entity = kInvalidEntity;
    Math::Vector3 position{};

    [[nodiscard]] bool operator==(const SightTarget&) const = default;
};

struct HeardNoise final {
    Math::Vector3 position{};
    float loudness = 0.0F;

    [[nodiscard]] bool operator==(const HeardNoise&) const = default;
};

struct SightResult final {
    bool visible = false;
    float distance01 = 1.0F;
    Math::Vector3 position{};
    Entity entity = kInvalidEntity;

    [[nodiscard]] bool operator==(const SightResult&) const = default;
};

struct HearingResult final {
    float loudness = 0.0F;
    Math::Vector3 position{};

    [[nodiscard]] bool operator==(const HearingResult&) const = default;
};

/// True when something other than `self` and `target` stands between `from` and `to`. The engine
/// answers with a raycast; tests answer with a script.
using SightOcclusionQuery =
    std::function<bool(const Math::Vector3& from, const Math::Vector3& to, Entity target)>;

/// A usable watched tag, a positive finite sight range, a 0..360 field of view, a finite
/// non-negative hearing radius.
[[nodiscard]] bool IsPerceptionComponentValid(const PerceptionComponent& sensor) noexcept;
/// Loudness in 0..1, decay finite and non-negative.
[[nodiscard]] bool IsNoiseEmitterComponentValid(const NoiseEmitterComponent& emitter) noexcept;

/// Decays loudness toward silence, clamping at zero. A non-positive or non-finite step, or a
/// zero decay, leaves the emitter untouched.
void DecayNoiseEmitter(NoiseEmitterComponent& emitter, float deltaSeconds) noexcept;

/// The nearest candidate inside range and cone with a clear ray, if any: candidates sort by
/// distance and test nearest-first, so the common case costs one occlusion query. A candidate at
/// the sensor's own position is seen without a query. Never calls `occlusion` when the sensor
/// does not require line of sight.
[[nodiscard]] SightResult SenseSight(const Math::Vector3& sensorPosition,
                                           const Math::Vector3& sensorForward,
                                           const PerceptionComponent& sensor,
                                           std::span<const SightTarget> candidates,
                                           const SightOcclusionQuery& occlusion);

/// The loudest noise inside radius times loudness, perceived with distance falloff (full loudness
/// at the sensor's feet, silence at the edge of reach). First wins ties.
[[nodiscard]] HearingResult SenseHearing(const Math::Vector3& sensorPosition,
                                               float hearingRadiusMetres,
                                               std::span<const HeardNoise> noises) noexcept;

/// Writes both results into the sensor's blackboard under the key convention above. A full board
/// keeps stale senses rather than failing.
void WritePerceptionResults(BlackboardComponent& board, std::string_view watchedTag,
                               const SightResult& sight, const HearingResult& hearing);

} // namespace UVE::Scene
