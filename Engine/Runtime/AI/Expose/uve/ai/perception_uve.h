// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/ai/utility_ai_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

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

inline constexpr std::size_t kMaximumWatchedTagBytesUVE = 32U;
inline constexpr float kPerceptionOmnidirectionalDegreesUVE = 360.0F;

struct PerceptionComponentUVE final {
    /// Gameplay tag this sensor watches, within 32 bytes.
    std::string watchedTag;
    float sightRangeMetres = 20.0F;
    /// Full field-of-view angle in degrees, 0..360. 360 sees behind itself.
    float sightFieldOfViewDegrees = 90.0F;
    /// Hearing reach at full loudness; 0 is deaf.
    float hearingRadiusMetres = 15.0F;
    bool requiresLineOfSight = true;

    [[nodiscard]] bool operator==(const PerceptionComponentUVE&) const = default;
};

/// A continuous or decaying sound in the world: a machine drones (decay 0), a gunshot bangs (the
/// game sets loudness; the sync decays it back to silence).
struct NoiseEmitterComponentUVE final {
    float loudness = 1.0F;
    float decayPerSecond = 0.0F;

    [[nodiscard]] bool operator==(const NoiseEmitterComponentUVE&) const = default;
};

struct SightTargetUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    Math::Vector3UVE position{};

    [[nodiscard]] bool operator==(const SightTargetUVE&) const = default;
};

struct HeardNoiseUVE final {
    Math::Vector3UVE position{};
    float loudness = 0.0F;

    [[nodiscard]] bool operator==(const HeardNoiseUVE&) const = default;
};

struct SightResultUVE final {
    bool visible = false;
    float distance01 = 1.0F;
    Math::Vector3UVE position{};
    EntityUVE entity = kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const SightResultUVE&) const = default;
};

struct HearingResultUVE final {
    float loudness = 0.0F;
    Math::Vector3UVE position{};

    [[nodiscard]] bool operator==(const HearingResultUVE&) const = default;
};

/// True when something other than `self` and `target` stands between `from` and `to`. The engine
/// answers with a raycast; tests answer with a script.
using SightOcclusionQueryUVE =
    std::function<bool(const Math::Vector3UVE& from, const Math::Vector3UVE& to, EntityUVE target)>;

/// A usable watched tag, a positive finite sight range, a 0..360 field of view, a finite
/// non-negative hearing radius.
[[nodiscard]] bool IsPerceptionComponentValidUVE(const PerceptionComponentUVE& sensor) noexcept;
/// Loudness in 0..1, decay finite and non-negative.
[[nodiscard]] bool IsNoiseEmitterComponentValidUVE(const NoiseEmitterComponentUVE& emitter) noexcept;

/// Decays loudness toward silence, clamping at zero. A non-positive or non-finite step, or a
/// zero decay, leaves the emitter untouched.
void DecayNoiseEmitterUVE(NoiseEmitterComponentUVE& emitter, float deltaSeconds) noexcept;

/// The nearest candidate inside range and cone with a clear ray, if any: candidates sort by
/// distance and test nearest-first, so the common case costs one occlusion query. A candidate at
/// the sensor's own position is seen without a query. Never calls `occlusion` when the sensor
/// does not require line of sight.
[[nodiscard]] SightResultUVE SenseSightUVE(const Math::Vector3UVE& sensorPosition,
                                           const Math::Vector3UVE& sensorForward,
                                           const PerceptionComponentUVE& sensor,
                                           std::span<const SightTargetUVE> candidates,
                                           const SightOcclusionQueryUVE& occlusion);

/// The loudest noise inside radius times loudness, perceived with distance falloff (full loudness
/// at the sensor's feet, silence at the edge of reach). First wins ties.
[[nodiscard]] HearingResultUVE SenseHearingUVE(const Math::Vector3UVE& sensorPosition,
                                               float hearingRadiusMetres,
                                               std::span<const HeardNoiseUVE> noises) noexcept;

/// Writes both results into the sensor's blackboard under the key convention above. A full board
/// keeps stale senses rather than failing.
void WritePerceptionResultsUVE(BlackboardComponentUVE& board, std::string_view watchedTag,
                               const SightResultUVE& sight, const HearingResultUVE& hearing);

} // namespace UVE::Scene
