// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Physics {

/// Upper bound on the strike pairs one sync report may carry, mirroring
/// kMaximumAreaOverlapResultsUVE's role for the area-overlap query. The report is complete below
/// this bound; at or above it the caller is told (see `strikesTruncated`) and the lifecycle tracker
/// refuses to infer exits from an incomplete snapshot.
///
/// A hitbox's own `strikes` array is a much smaller bound (16) and is the *component's* storage;
/// this one bounds the whole scan, which is what a scene-wide consumer sees.
inline constexpr std::size_t kMaximumHitbox3DStrikeResultsUVE = 4096U;

/// One resolved strike as the scan saw it this tick: which hitbox found which hurtbox, how deeply,
/// along which axis, and on which damage channel.
///
/// This is the pairing's evidence, not a consequence - the engine resolves the pairing and reports
/// it; what a strike *means* (damage, knockback, i-frames) is gameplay, and it decides it from the
/// typed events this record travels in.
struct Hitbox3DStrikePairUVE final {
    Scene::EntityUVE hitbox = Scene::kInvalidEntityUVE;
    Scene::EntityUVE hurtbox = Scene::kInvalidEntityUVE;
    /// Exact oriented-box minimum-translation depth, in world units.
    float penetrationDepth = 0.0F;
    /// The minimum-translation direction, world space, hitbox toward hurtbox, unit length when the
    /// penetration was finite.
    Math::Vector3UVE axis{};
    /// Both boxes' channel; a strike requires them to be equal, so one copy names the pairing.
    std::string damageChannel;

    [[nodiscard]] bool operator==(const Hitbox3DStrikePairUVE&) const noexcept = default;
};

/// What one hitbox-vs-hurtbox scan saw, plus the counts that make it auditable.
struct Hitbox3DSyncReportUVE final {
    /// How many hurtboxes the first pass accepted as candidates (enabled, valid, posed).
    std::size_t hurtboxCount = 0U;
    /// How many hitboxes the second pass visited.
    std::size_t hitboxCount = 0U;
    /// Any hitbox's own strike list overflowed this tick. The pair list is then incomplete: the
    /// extra hurtboxes exist, but this report does not name them, and a consumer diffing snapshots
    /// must NOT read their absence as "the strike ended".
    bool strikesTruncated = false;
    /// Every resolved pairing this tick, in scan order (hitboxes in ECS order, hurtboxes in
    /// candidate order).
    std::vector<Hitbox3DStrikePairUVE> strikes;

    [[nodiscard]] bool IsTruncatedUVE() const noexcept { return strikesTruncated; }
};

/// Runs the whole per-tick hitbox/hurtbox scan and writes each hitbox's runtime strike list.
///
/// Pass 1 snapshots every enabled, valid hurtbox that has a world transform - the candidate set the
/// mutation pass evaluates against, so an entity iteration is never held open across a second one.
/// The candidate set is deliberately unbounded: capping it would mean silently pretending hurtboxes
/// beyond the cap do not exist. Only the per-hitbox strike LIST is bounded, and it reports its own
/// overflow.
///
/// Pass 2 refreshes every hitbox's strike state. A pairing requires all of:
///
///   * Hitbox3DUVE::AcceptsTargetUVE and Hurtbox3DUVE::AcceptsAttackerUVE;
///   * a real oriented-box overlap (exact 15-axis test; touching boundaries are not strikes).
/// Each node then commits its own list (unique, deepest first, cap 16, once-per-activation).
///
/// This is a seam rather than more code in the engine-core tick for the same reason
/// Physics::SyncInteractionAreasUVE() is: the tick owns WHEN this runs, not what the rules are, and
/// the rules are exactly what needed to be testable without standing up an EngineCoreUVE.
[[nodiscard]] Hitbox3DSyncReportUVE SyncHitboxes3DUVE(Scene::IEntityManagerUVE& entityManager);

} // namespace UVE::Physics
