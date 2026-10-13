// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/hitbox_strike_uve.h"

namespace UVE::Physics {

/// Server fixed-tick numbering, app-provided (the same ticks the server simulates on). The
/// client names its view with the newest server tick it has - its prediction layer's last
/// acked tick - and the server rewinds to it. u64 like prediction ticks: never wraps.
using LagTickUVE = std::uint64_t;

/// Snapshots kept: about a second at 60 Hz, the standard lag-compensation window. Older
/// targets clamp to the oldest survivor and say so.
inline constexpr std::size_t kLagHistoryMaximumTicksUVE = 64U;

/// One entity's rewindable pose: position, orientation, and world scale. Rewound
/// half-extents are the authored size times the absolute rewound scale, so a mid-window
/// rescale resolves against the size the shooter saw; zero or non-finite products are
/// unhittable (fail closed). The live scan ignores world scale, so lag resolves agree with
/// it wherever scale is 1 - and with the render everywhere.
struct LagPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

/// What a rewound overlap resolve found: pairs in the live report's own shape, so the engine
/// merges them into the live report BEFORE the lifecycle tracker diffs it. Merging (never a
/// second tracker update) is what keeps compensated hits from exiting every live strike -
/// and what makes a replayed shot idempotent, since the pair is already in the baseline.
struct LagShotReportUVE final {
    std::vector<Hitbox3DStrikePairUVE> pairs;
    /// The snapshot tick actually resolved against (the target, unless clamped).
    LagTickUVE resolvedTick = 0U;
    /// The target predated the oldest snapshot (or postdated the newest): resolved against
    /// the nearest survivor instead.
    bool clamped = false;
    /// Poses were blended between the resolved tick and the next (a fractional resolve with
    /// an exact floor and a consecutive ceiling). Gaps, the newest tick, and clamped floors
    /// fall back to the floor snapshot with this false.
    bool interpolated = false;
    /// Snapshot entities carrying a currently-vulnerable hurtbox: the candidate set.
    std::size_t candidatesConsidered = 0U;

    [[nodiscard]] bool operator==(const LagShotReportUVE&) const = default;
};

/// What a rewound ray resolve found: the nearest vulnerable hurtbox along the ray, if any.
/// A ray hit is not a hitbox strike (no hitbox is involved): gameplay applies damage itself
/// (ApplyHitboxStrikeToHealthUVE takes an entity plus an amount) and owns the consequences,
/// exactly like the live strike path leaves meaning to gameplay.
struct LagRayReportUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    /// Metres along the (normalized) ray. Zero on a miss.
    float distance = 0.0F;
    /// The hit point in world space. Zero on a miss.
    Math::Vector3UVE point{};
    LagTickUVE resolvedTick = 0U;
    bool clamped = false;
    /// Poses were blended between the resolved tick and the next - see LagShotReportUVE.
    bool interpolated = false;

    [[nodiscard]] bool operator==(const LagRayReportUVE&) const = default;
};

struct LagCompensatorStatsUVE final {
    std::uint64_t snapshotsRecorded = 0U;
    std::uint64_t snapshotsEvicted = 0U;
    /// Records refused for a non-increasing tick (history only moves forward).
    std::uint64_t recordsRejected = 0U;
    std::uint64_t shotsResolved = 0U;
    std::uint64_t raysResolved = 0U;
    std::uint64_t resolvesClamped = 0U;
    /// Fractional resolves that actually blended (fallbacks count as plain resolves).
    std::uint64_t resolvesInterpolated = 0U;
    /// Overlap pairs found across all shot resolves.
    std::uint64_t pairsFound = 0U;

    [[nodiscard]] bool operator==(const LagCompensatorStatsUVE&) const noexcept = default;
};

/// Server-side rewind for hit registration. Records per-tick poses of every combatant (any
/// entity with a hitbox or hurtbox plus a world transform); resolves a lagged shot or ray
/// against the poses at the shooter's tick. Poses rewind (position, rotation, scale) -
/// everything else (vulnerability,
/// channels, layers, activation memory, effects) evaluates live, which is the standard
/// approximation and an honest one: a target that started i-frames after the shot still had
/// none when it was taken, and this resolves what the geometry says.
///
/// Nothing here touches the scene graph or rewrites a transform: poses are read from history
/// and evaluated value-only through the same narrow-phase the live sync uses, so a resolve
/// against an unmoved world answers exactly what the live sync answered. Compensated pairs
/// never land in component strike lists (those say "touching right now", and the past is not
/// now); they travel in the merged report, where the lifecycle tracker turns them into the
/// same Entered events and damage live strikes get.
///
/// Fractional ticks interpolate between consecutive snapshots (positions lerp, rotations
/// slerp), so a shooter's view between records resolves against the in-between instead of
/// the floor - without the fraction the window still quantizes to 16 ms at 60 Hz.
/// Interpolation needs an exact floor and the very next tick: gaps, the newest tick, and
/// clamped floors all fall back to the floor snapshot and say so (`interpolated` stays
/// false). The client's fire offset within its view tick rides the shot protocol;
/// callers feed hand-known fractions until it lands.
class HitboxLagCompensatorUVE final {
public:
    HitboxLagCompensatorUVE() = default;

    /// Snapshots every combatant's current pose under `tick`. Ticks must increase: a repeat
    /// or rewind is refused (false) and counted, recording nothing.
    [[nodiscard]] bool RecordUVE(LagTickUVE tick, Scene::IEntityManagerUVE& entityManager);

    /// The shooter's hitbox (current state, rewound pose) against every vulnerable hurtbox
    /// (current state, rewound pose) at `targetTick`. Empty pairs when the shooter has no
    /// armed hitbox, when it was never recorded, or when nothing overlapped - fail closed,
    /// like the live scan.
    [[nodiscard]] LagShotReportUVE ResolveShotUVE(Scene::EntityUVE shooter, LagTickUVE targetTick,
                                                 Scene::IEntityManagerUVE& entityManager);

    /// `ray` (its direction is normalized) against the same rewound candidate set, nearest
    /// hit wins. The shooter's hitbox lends its combat identity (layers, mask, channel) for
    /// the same symmetric acceptance the live scan runs; without one the ray resolves empty.
    /// A zero or non-finite direction, or a non-positive range, resolves empty.
    [[nodiscard]] LagRayReportUVE ResolveRayUVE(Scene::EntityUVE shooter, const Math::RayUVE& ray,
                                               float maxDistance, LagTickUVE targetTick,
                                               Scene::IEntityManagerUVE& entityManager);

    /// Fractional-tick shot resolve: poses at `tick` blended toward `tick + 1` by
    /// `tickFraction` (positions lerp, rotations slerp). The fraction must be finite and in
    /// [0, 1) - anything else resolves empty, fail closed. Blending needs the floor tick
    /// itself on record plus the very next tick; gaps, the newest tick, clamped floors, and
    /// fraction 0 all resolve the floor snapshot exactly like ResolveShotUVE, with
    /// `interpolated` false. `resolvedTick` names the floor; only `interpolated` says the
    /// ceiling joined in.
    [[nodiscard]] LagShotReportUVE ResolveShotAtUVE(Scene::EntityUVE shooter, LagTickUVE tick,
                                                   float tickFraction,
                                                   Scene::IEntityManagerUVE& entityManager);

    /// Fractional-tick ray resolve: the same bracket rules as ResolveShotAtUVE, the same
    /// nearest-hit answer as ResolveRayUVE. Range and direction validation runs before the
    /// fraction is even looked at - a degenerate ray is empty whatever time it names.
    [[nodiscard]] LagRayReportUVE ResolveRayAtUVE(Scene::EntityUVE shooter, const Math::RayUVE& ray,
                                                 float maxDistance, LagTickUVE tick,
                                                 float tickFraction,
                                                 Scene::IEntityManagerUVE& entityManager);

    [[nodiscard]] LagCompensatorStatsUVE StatsUVE() const noexcept { return m_stats; }
    [[nodiscard]] std::size_t HistorySizeUVE() const noexcept { return m_history.size(); }

private:
    /// One tick's poses in record order (the entity manager's iteration order, deterministic
    /// per frame like the live scan's candidate order).
    using SnapshotUVE = std::vector<std::pair<Scene::EntityUVE, LagPoseUVE>>;

    [[nodiscard]] const SnapshotUVE* FindSnapshotUVE(LagTickUVE targetTick, LagTickUVE& resolvedTick,
                                                     bool& clamped) const;

    /// The bracket behind a fractional resolve: the floor snapshot plus, when the floor is
    /// the tick itself and the very next tick is on record, the blended poses in `scratch`.
    /// Anything else (empty history, fraction 0, clamped floor, missing ceiling) answers
    /// the floor with `interpolated` false.
    [[nodiscard]] const SnapshotUVE* ResolveBracketUVE(LagTickUVE tick, float fraction,
                                                       LagTickUVE& resolvedTick, bool& clamped,
                                                       bool& interpolated,
                                                       SnapshotUVE& scratch) const;

    /// The narrow-phase loops behind both resolve spellings: the guards, the acceptance
    /// gates, and the pairing, against an already-chosen snapshot. A resolve against an
    /// unmoved world answers exactly what the live sync answered, blended or not.
    [[nodiscard]] LagShotReportUVE ResolveShotFromUVE(Scene::EntityUVE shooter,
                                                     const SnapshotUVE* snapshot,
                                                     LagTickUVE resolvedTick, bool clamped,
                                                     Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] LagRayReportUVE ResolveRayFromUVE(Scene::EntityUVE shooter, const Math::RayUVE& ray,
                                                   float maxDistance, const SnapshotUVE* snapshot,
                                                   LagTickUVE resolvedTick, bool clamped,
                                                   Scene::IEntityManagerUVE& entityManager);

    std::map<LagTickUVE, SnapshotUVE> m_history;
    LagTickUVE m_lastRecorded = 0U;
    bool m_hasHistory = false;
    LagCompensatorStatsUVE m_stats;
};

} // namespace UVE::Physics
