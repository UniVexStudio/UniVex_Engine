// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/physics/area_overlap_lifecycle_tracker_uve.h"
#include "uve/physics/area_overlap_system_uve.h"

namespace UVE::Scene {

// ---- Trigger volumes ---------------------------------------------------------------
//
// Area3D overlap edges are raw facts - this pair entered, that pair left. Triggers author what
// the edges MEAN: a one-shot that fires its cutscene the first time anything walks in and then
// spends itself, a pickup that fires on every enter, a damage volume that fires on entry and
// keeps ticking on a cadence while anything stays inside. The component sits on the area entity
// and UpdateTriggerVolumesUVE() matches it against `pair.area` every frame.
//
// Each fire carries the trigger, the interactor that caused it (the entering body for edge
// fires, the first overlapping body in snapshot order for cadence ticks) and a monotonic fire
// count; the engine bridge turns those into TriggerFiredEventUVE. The cooldown is global to the
// trigger, not per interactor - staggering per-body cooldowns is a follow-up, not v1.
//
// One-shots stay spent across save/load: armed, the fire count and both clocks are all
// serialized, so only the explicit re-arm API brings a trigger back. A non-positive or
// non-finite step freezes the clocks but still processes enter edges - transitions are
// ephemeral and the tracker never repeats them, so dropping them would lose fires.

enum class TriggerFirePolicyUVE : std::uint8_t {
    /// Fires on the first ready enter edge, then disarms until re-armed.
    OneShot = 0U,
    /// Fires on every ready enter edge while armed.
    EveryEnter = 1U,
    /// Fires on every ready enter edge plus a cadence tick every `intervalSeconds` while at
    /// least one overlap remains; every fire restarts the cadence clock.
    WhileOccupied = 2U,
};

struct TriggerVolumeComponentUVE final {
    TriggerFirePolicyUVE policy = TriggerFirePolicyUVE::OneShot;
    float intervalSeconds = 1.0F;
    float cooldownSeconds = 0.0F;
    bool armed = true;
    std::uint32_t firedCount = 0U;
    float cooldownRemaining = 0.0F;
    float intervalRemaining = 0.0F;

    [[nodiscard]] bool operator==(const TriggerVolumeComponentUVE&) const = default;
};

/// A known policy, a positive finite interval, and finite non-negative cooldown and clocks.
[[nodiscard]] bool IsTriggerVolumeComponentValidUVE(const TriggerVolumeComponentUVE& value) noexcept;

struct TriggerFiredResultUVE final {
    EntityUVE trigger = kInvalidEntityUVE;
    EntityUVE interactor = kInvalidEntityUVE;
    std::uint32_t firedCount = 0U;

    [[nodiscard]] bool operator==(const TriggerFiredResultUVE&) const = default;
};

/// Evaluates every TriggerVolumeComponentUVE against this frame's overlap snapshot and enter/exit
/// transitions, aging cooldown and interval clocks by `deltaSeconds` and returning one entry per
/// fire: triggers in (index, generation) order, each trigger's edge fires before its cadence
/// tick. Invalid and disarmed volumes are skipped wholesale; an empty occupancy resets the
/// interval clock so the next visit starts fresh. Never invalidates a valid volume.
[[nodiscard]] std::vector<TriggerFiredResultUVE> UpdateTriggerVolumesUVE(
    IEntityManagerUVE& entityManager, const Physics::AreaOverlapQueryResultUVE& snapshot,
    std::span<const Physics::AreaOverlapTransitionUVE> transitions, float deltaSeconds);

/// Re-arms `trigger` (latch back on, both clocks zeroed, fire count untouched) so a spent
/// one-shot can fire again. False when the entity carries no trigger volume. Re-arming is the
/// recovery path, so it works on invalid volumes too - firing still waits until they're fixed.
[[nodiscard]] bool RearmTriggerVolumeUVE(IEntityManagerUVE& entityManager, EntityUVE trigger);

/// Drops the latch on `trigger` so it stops firing until re-armed. False when the entity
/// carries no trigger volume.
[[nodiscard]] bool DisarmTriggerVolumeUVE(IEntityManagerUVE& entityManager, EntityUVE trigger);

} // namespace UVE::Scene
