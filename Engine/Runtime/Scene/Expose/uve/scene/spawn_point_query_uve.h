// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"

namespace UVE::Scene {

// Hard storage bound for one spawn query's result list - the same bounded-list discipline as
// kMaximumHitbox3DStrikesUVE / kMaximumInteractionAreaCandidatesUVE: fixed-size so a caller can
// hold it anywhere without a heap allocation of its own, and overflow is REPORTED rather than
// silently pretending the extra points do not exist. 32 is far past any hand-authored scene's
// spawner count; a caller that genuinely needs every point asks for more pages from the same
// query rather than this list growing per frame.
inline constexpr std::size_t kMaximumSpawnPointQueryResultsUVE = 32U;

/// What one query looks for. `tag` is matched exactly against the authored `spawnTag`; an empty
/// tag means "every tag", which is what the editor's play-entry pick and a plain "any spawn"
/// respawn both want ("team red spawn" is a second query with tag = "team red"). `consumeOneShot`
/// is the checkpoint rule: when true, every one-shot point the call actually hands out is disabled
/// as it is handed out, so a second call cannot spawn from the same point twice. Results past the
/// storage bound are not handed out and therefore NOT spent - the caller whose list was full can
/// still reach them next call, which is how a bounded list stays honest.
struct SpawnPoint3DQueryUVE final {
    std::string tag;
    bool consumeOneShot = false;
};

/// One spawn point as the query resolves it. `pose` is the object's WORLD pose composed with the
/// authored local offset (SpawnPoint3DPoseUVE's contract), so a caller can place an actor from it
/// directly and never touches LocalPosition/Rotation itself. `consumed` is true only when THIS
/// call spent the point.
struct SpawnPoint3DQueryResultUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    std::string tag;
    SpawnPoint3DPoseUVE pose{};
    bool oneShot = false;
    bool consumed = false;
};

/// The bounded result list. Results are sorted by SortsBeforeSpawnPointUVE - (entity.index,
/// entity.generation) ascending, stable content order and never pool order - so `results[0]` is
/// exactly the pick ResolveSpawnPoint3DSelectionUVE makes from the same set, and a saved scene
/// answers the same way on every machine. The two read one shared predicate rather than two
/// copies of the rule; that predicate is the only thing the resolver and this query agree on by
/// construction instead of by convention.
struct SpawnPoint3DQueryResultsUVE final {
    std::array<SpawnPoint3DQueryResultUVE, kMaximumSpawnPointQueryResultsUVE> results{};
    std::size_t count = 0U;
    bool overflowed = false;

    [[nodiscard]] bool HasAnyUVE() const noexcept { return count != 0U; }

    /// The first result in stable content order, or nullptr when the query found nothing - the
    /// "where does the player start" one-liner every caller would otherwise write.
    [[nodiscard]] const SpawnPoint3DQueryResultUVE* FirstUVE() const noexcept {
        return count == 0U ? nullptr : &results[0];
    }
};

/// The spawn query others call - "where should this actor appear", answered from authored data.
///
/// A point participates when it is enabled, passes its own validator, carries a world transform
/// (the scene graph has swept since it was authored), matches the query's tag, and composes to a
/// finite pose. Everything else is skipped rather than approximated: a disabled point is not a
/// spawn, and a point whose authored rotation is degenerate is not teleported into garbage.
/// Skipping is per-point, so one malformed spawner never hides the rest of the scene's.
///
/// The point itself is never ticked - Godot's PlayerStart-shaped hole is a query, not a system,
/// and this is that query. Deterministic order plus the bounded, overflow-flagged list is the
/// whole contract; deciding *which* result to use (the editor takes the first, a respawn screen
/// might filter and offer several) stays with the caller.
[[nodiscard]] SpawnPoint3DQueryResultsUVE QuerySpawnPointsUVE(IEntityManagerUVE& entityManager,
                                                             const SpawnPoint3DQueryUVE& query);

/// Spends one point explicitly: disables it and answers whether it was a live one-shot that got
/// spent. The query's `consumeOneShot` is the batch form; this is the single-point form, for a
/// caller that must not spend a point before it knows the spawn actually succeeded (the editor
/// teleports first, consumes second, so a refused teleport leaves its checkpoint intact).
/// A reusable point, a point already spent, an unknown entity and an entity without a spawn
/// component all answer false and change nothing.
[[nodiscard]] bool ConsumeSpawnPointUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

} // namespace UVE::Scene
