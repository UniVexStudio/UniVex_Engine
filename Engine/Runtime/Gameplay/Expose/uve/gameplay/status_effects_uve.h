// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/gameplay/gameplay_attributes_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Status effects ----------------------------------------------------------------
//
// Temporary forces on attribute pools: poison draining stamina, a regen aura refilling mana,
// a might buff inflating the stamina pool itself. Each effect names its target pool and a signed
// magnitude per second (positive damages, negative heals); reapplying an effect refreshes its
// clock, and effects stack only under the Stack policy - two poisons are one poison with a fresh
// clock unless the second application asked to stack.
//
// A nonzero maxDelta also shifts the target pool's maximum while the effect lives (negative
// shrinks it): might raises the ceiling, weakness lowers it. The shift settles the moment the
// pool exists - or on the next tick when the pool arrives late - and unwinds on remove, expiry
// and retarget, clamping current into the restored ceiling; maximums are pools-only, so health
// never takes a maxDelta. Stacks multiply the per-second drift; the whole instance shares one
// clock, and removal drops every stack at once.
//
// The tick ages every effect and applies the attribute-targeted ones. Effects aimed at the
// reserved "health" id are computed but NOT applied here: this file never sees a HealthComponent,
// so the engine sync applies those to health (damage AND healing) and queues the health events,
// the same bridge the strike pipeline already uses. Attribute regen drifts silently in the same
// tick -
// passive drift fires no Damaged events - except that a pool emptied by decay still reports
// Depleted, since "stamina hit zero" is worth reacting to whatever the cause.

inline constexpr std::size_t kMaximumStatusEffectsUVE = 8U;
inline constexpr std::size_t kMaximumStatusEffectIdBytesUVE = 48U;

enum class StatusEffectStackingUVE : std::uint8_t {
    /// Reapplying overwrites target, magnitude, clock and cap, and resets to one stack.
    Refresh = 0U,
    /// Reapplying adds a stack (up to the latest cap), refreshes the shared clock and
    /// overwrites target and magnitude; the whole instance still expires as one.
    Stack = 1U,
};

struct StatusEffectUVE final {
    std::string effectId;
    std::string attributeId;
    float magnitudePerSecond = 0.0F;
    float remainingSeconds = 0.0F;
    StatusEffectStackingUVE stacking = StatusEffectStackingUVE::Refresh;
    std::uint32_t stacks = 1U;
    std::uint32_t maxStacks = 1U;
    /// Maximum shift while live; applied means the target pool has already taken it.
    float maxDelta = 0.0F;
    bool maxApplied = false;

    [[nodiscard]] bool operator==(const StatusEffectUVE&) const = default;
};

struct StatusEffectsComponentUVE final {
    std::vector<StatusEffectUVE> effects;

    [[nodiscard]] bool operator==(const StatusEffectsComponentUVE&) const = default;
};

/// Bounded count, usable ids, a named target pool, finite magnitude, non-negative time left, a
/// known stacking policy, sane stack counts (at least one, within cap) and a finite maxDelta.
[[nodiscard]] bool IsStatusEffectsComponentValidUVE(const StatusEffectsComponentUVE& value) noexcept;

/// Adds `effectId`, or refreshes the held one (target, magnitude, clock and cap all come from
/// the latest application; a Stack reapply also adds a stack up to `maxStacks`, a Refresh one
/// resets to a single stack). A nonzero `maxDelta` shifts the target pool's maximum at once, or
/// stays pending until the pool exists. False for bad ids, non-finite magnitude, non-positive
/// duration, an unknown stacking policy, a zero cap, a non-finite maxDelta, a maxDelta aimed at
/// health, a broken target pool, or a full set.
[[nodiscard]] bool ApplyStatusEffectUVE(GameplayAttributesComponentUVE& attributes,
                                        StatusEffectsComponentUVE& effects, std::string effectId,
                                        std::string attributeId, float magnitudePerSecond,
                                        float durationSeconds,
                                        StatusEffectStackingUVE stacking = StatusEffectStackingUVE::Refresh,
                                        std::uint32_t maxStacks = 1U, float maxDelta = 0.0F);

/// Removes `effectId` with every stack it holds, unwinding a settled maxDelta first. False when
/// it was never there.
[[nodiscard]] bool RemoveStatusEffectUVE(GameplayAttributesComponentUVE& attributes,
                                         StatusEffectsComponentUVE& effects, std::string_view effectId);

struct GameplayAttributeTickResultUVE final {
    /// What caused this entry: an effect id, or empty for passive regen drift.
    std::string effectId;
    std::string attributeId;
    /// Signed drift: positive damaged the pool, negative healed it.
    float amount = 0.0F;
    /// Pool remainder after the drift; zero when the pool is unknown (health, missing).
    float remaining = 0.0F;
    /// False for health-targeted effects (the sync applies those) and missing pools.
    bool applied = false;
    bool expired = false;
    bool depleted = false;

    [[nodiscard]] bool operator==(const GameplayAttributeTickResultUVE&) const = default;
};

/// Advances `status` (may be null: pools without effects still regen) and `attributes` by
/// `deltaSeconds`: effects apply first, then regen. Expired effects apply their final tick and are
/// dropped. Returns one entry per effect plus one per drifted pool, for event queueing. A
/// non-positive or non-finite step returns nothing and touches nothing. Stacks multiply the drift
/// (health-bound included); pending maxDeltas settle against late pools, and an expiring effect
/// unwinds its maximum before it is dropped, flagging Depleted when the restored ceiling zeroes a
/// pool that still held something.
[[nodiscard]] std::vector<GameplayAttributeTickResultUVE> TickGameplayAttributesUVE(
    GameplayAttributesComponentUVE& attributes, StatusEffectsComponentUVE* status, float deltaSeconds);

} // namespace UVE::Scene
